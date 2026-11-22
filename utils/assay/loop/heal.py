"""heal — tell when the cache of a build directory keeps a compiler that its toolchain file does not resolve.

usage: loop heal BUILD_DIR NAME

Stale compiler
    CMake keeps the compiler of the first configure of a build directory.
    A toolchain file of the tree puts its compiler in the cache with no
    FORCE, so a new default prefix, such as the prefix of a new patch of
    GCC, never reaches a build directory that exists.  The command compares
    the compilers of the cache with the compilers that the toolchain file
    resolves (stale_compilers()):
      * The toolchain file is CMAKE_TOOLCHAIN_FILE of the cache.  Each key
        of TOOLCHAIN_TARGETS names one: cmake/Toolchain-gcc16.cmake resolves
        the compilers of the host (assay.setup.gcc.toolchain_compilers), and
        cmake/Toolchain-gcc16-aarch64.cmake the compilers of its cross
        target (assay.setup.gcc.cross_toolchain_compilers).  A different
        toolchain file, or no toolchain file, gives no stale compiler.
      * The command reads each entry of COMPILER_ENTRIES that the cache
        holds.  An entry with a file name other than the name of the
        resolved compiler is a wrapper, such as the afl-g++-fast of `loop
        fuzz`, and is not stale.
      * The command compares the real paths, with each symlink resolved, so a
        new prefix and a symlink that points to a new prefix both give a
        stale compiler.  A compiler is stale in three cases, in this order:
          CACHED      the cache names a compiler whose real path is not the
                      real path of the resolved compiler
          RECORDED    the configure recorded the real path of the resolved
                      compiler (RECORD_ENTRIES, cmake/StaleCompiler.cmake),
                      and the toolchain file now resolves a different real
                      path, such as a symlink that points to a new prefix
          UNRECORDED  the cache holds no record, so the command cannot tell
                      the compiler of the objects.  Such a build directory
                      heals one time, and its configure then records it.
    A build directory with no reply of the cache gives no stale compiler.

Output
    When a compiler is stale, the command prints one line that names the
    paths of the cache and of the toolchain file, with NAME, the name of
    the configuration, at its start.  Else it prints nothing.  A RECORDED
    compiler keeps the paths of the commands of the build, so Ninja would
    build no object again: the command then removes the log of Ninja
    (NINJA_LOG), and each object builds again.  The gate runs
    this command of the warm tree before it builds a configuration, so the
    rules of the commit decide, and it configures a build directory with a
    stale compiler again with `cmake --preset NAME -B BUILD_DIR --fresh`
    (the compiler heal of assay.loop.gate).  Outside the gate,
    cmake/StaleCompiler.cmake refuses such a cache in each configure.

Exit status: 0, or 2 on a usage error.
"""

import argparse
import enum
import os
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from assay.readers import cmake as cmake_reader
from assay.setup import gcc as patched_gcc

# The toolchain files of the tree, each with the cross target of its compilers, or None for the compilers of the
# host, and the cache entries of the compilers that the command compares (the stale compiler).
TOOLCHAIN_TARGETS: dict[str, str | None] = {
    "Toolchain-gcc16.cmake": None,
    "Toolchain-gcc16-aarch64.cmake": "aarch64-linux-gnu",
}
COMPILER_ENTRIES = ("CMAKE_CXX_COMPILER", "CMAKE_C_COMPILER")
# The cache entry of the real path that the configure recorded for each compiler (cmake/StaleCompiler.cmake).
RECORD_ENTRIES = {
    "CMAKE_CXX_COMPILER": "CRUCIBLE_CONFIGURED_CXX_REAL",
    "CMAKE_C_COMPILER": "CRUCIBLE_CONFIGURED_C_REAL",
}
# The log of Ninja in a build directory.  An object with no entry in the log builds again.
NINJA_LOG = ".ninja_log"
# The word of a configure that drops the cache of the build directory first.
FRESH_WORD = "--fresh"


class Staleness(enum.Enum):
    """The case of a stale compiler (the stale compiler)."""

    CACHED = "cached"
    RECORDED = "recorded"
    UNRECORDED = "unrecorded"


@dataclass(frozen=True, slots=True)
class StaleCompiler:
    """A compiler of the cache of a build directory that its toolchain file does not resolve (the stale compiler).

    `entry` is the cache entry of the compiler, and `kind` the case.  `cached` is the path of the cache for CACHED,
    the recorded real path for RECORDED, and an empty text for UNRECORDED, and `cached_real` is its real path.
    `resolved` is the path that the toolchain file gives, and `resolved_real` its real path.
    """

    entry: str
    kind: Staleness
    cached: str
    cached_real: str
    resolved: str
    resolved_real: str


def resolved_compilers(toolchain_file: str) -> dict[str, Path]:
    """Return the compiler of each entry of COMPILER_ENTRIES that a toolchain file of the tree resolves.

    A toolchain file whose name is no key of TOOLCHAIN_TARGETS resolves no compiler.
    """
    name = PurePosixPath(toolchain_file).name
    if name not in TOOLCHAIN_TARGETS:
        return {}
    target = TOOLCHAIN_TARGETS[name]
    if target is None:
        c_compiler, cxx_compiler = patched_gcc.toolchain_compilers()
    else:
        c_compiler, cxx_compiler = patched_gcc.cross_toolchain_compilers(patched_gcc.CROSS_TARGETS[target])
    return {"CMAKE_CXX_COMPILER": cxx_compiler, "CMAKE_C_COMPILER": c_compiler}


def stale_compiler(entry: str, entries: dict[str, cmake_reader.CacheEntry], compiler: Path) -> StaleCompiler | None:
    """Return the stale compiler of one entry of COMPILER_ENTRIES, or None (the stale compiler)."""
    resolved_real = os.path.realpath(compiler)
    cached = entries.get(entry)
    if cached is not None and PurePosixPath(cached.value).name == compiler.name:
        cached_real = os.path.realpath(cached.value)
        if cached_real != resolved_real:
            return StaleCompiler(entry, Staleness.CACHED, cached.value, cached_real, str(compiler), resolved_real)
    record = entries.get(RECORD_ENTRIES[entry])
    if record is None:
        return StaleCompiler(entry, Staleness.UNRECORDED, "", "", str(compiler), resolved_real)
    if record.value != resolved_real:
        return StaleCompiler(entry, Staleness.RECORDED, record.value, record.value, str(compiler), resolved_real)
    return None


def stale_compilers(build: Path) -> tuple[StaleCompiler, ...]:
    """Return each compiler of the cache of a build directory that its toolchain file does not resolve.

    The section on a stale compiler gives the rules.  Complexity: one read of the reply of the cache.
    """
    try:
        entries = cmake_reader.cache(build)
    except cmake_reader.CmakeError:
        return ()
    toolchain = entries.get("CMAKE_TOOLCHAIN_FILE")
    resolved = {} if toolchain is None else resolved_compilers(toolchain.value)
    stale = (stale_compiler(entry, entries, resolved[entry]) for entry in COMPILER_ENTRIES if entry in resolved)
    return tuple(item for item in stale if item is not None)


def needs_rebuild(stale: tuple[StaleCompiler, ...]) -> bool:
    """Tell if a stale compiler keeps the paths of the commands of the build, so each object must build again."""
    return any(item.kind is Staleness.RECORDED for item in stale)


def stale_part(item: StaleCompiler) -> str:
    """Return the words that name one stale compiler in the line of the heal."""
    resolved = f"{item.resolved} ({item.resolved_real})"
    if item.kind is Staleness.CACHED:
        return f"{item.entry} {item.cached} ({item.cached_real}) in place of {resolved}"
    if item.kind is Staleness.RECORDED:
        return f"{item.entry} with the recorded real path {item.cached} in place of {resolved}"
    return f"{item.entry} with no recorded real path, for {resolved}"


def heal_line(name: str, build: Path, stale: tuple[StaleCompiler, ...]) -> str:
    """Return the line that names each stale compiler of the build directory of a configuration, or an empty line."""
    if not stale:
        return ""
    parts = " and ".join(stale_part(item) for item in stale)
    rebuild = f", and removes {NINJA_LOG} so each object builds again" if needs_rebuild(stale) else ""
    return (
        f"loop land: {name}: the cache of {build} names {parts} of the toolchain file, so the gate configures the "
        f"build again with {FRESH_WORD}{rebuild}"
    )


def main(argv: list[str]) -> int:
    """Print the line of the stale compilers of a build directory, or nothing (the output).

    Args:
        argv: The words after `loop heal`

    Returns:
        0, or 2 on a usage error
    """
    parser = argparse.ArgumentParser(prog="loop heal", description=__doc__.split("\n", 1)[0])
    parser.add_argument("build_dir", type=Path, help="the build directory")
    parser.add_argument("name", help="the name of the configuration, at the start of the line")
    args = parser.parse_args(argv)
    build = args.build_dir.resolve()
    stale = stale_compilers(build)
    if needs_rebuild(stale):
        (build / NINJA_LOG).unlink(missing_ok=True)
    line = heal_line(args.name, build, stale)
    if line:
        print(line)
    return 0
