"""heal — tell when the cache of a build directory keeps a compiler that its toolchain file does not resolve.

usage: loop heal BUILD_DIR NAME

THE STALE COMPILER
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
      * The command compares the two real paths, with each symlink resolved.
        So a new prefix and a symlink that points to a new prefix both give
        a stale compiler.  A cache and a toolchain file that name the same
        symlink agree.
    A build directory with no reply of the cache gives no stale compiler.

THE OUTPUT
    When a compiler is stale, the command prints one line that names the
    paths of the cache and of the toolchain file, with NAME, the name of
    the configuration, at its start.  Else it prints nothing.  The gate runs
    this command of the warm tree before it builds a configuration, so the
    rules of the commit decide, and it configures a build directory with a
    stale compiler again with `cmake --preset NAME -B BUILD_DIR --fresh`
    (THE COMPILER HEAL of assay.loop.gate).  Outside the gate,
    cmake/StaleCompiler.cmake refuses such a cache in each configure.

Exit status: 0, or 2 on a usage error.
"""

import argparse
import os
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

from assay.readers import cmake as cmake_reader
from assay.setup import gcc as patched_gcc

# The toolchain files of the tree, each with the cross target of its compilers, or None for the compilers of the
# host, and the cache entries of the compilers that the command compares (THE STALE COMPILER).
TOOLCHAIN_TARGETS: dict[str, str | None] = {
    "Toolchain-gcc16.cmake": None,
    "Toolchain-gcc16-aarch64.cmake": "aarch64-linux-gnu",
}
COMPILER_ENTRIES = ("CMAKE_CXX_COMPILER", "CMAKE_C_COMPILER")
# The word of a configure that drops the cache of the build directory first.
FRESH_WORD = "--fresh"


@dataclass(frozen=True, slots=True)
class StaleCompiler:
    """A compiler of the cache of a build directory that its toolchain file does not resolve (THE STALE COMPILER).

    `entry` is the cache entry.  `cached` and `resolved` are the paths as the cache and the toolchain file give them,
    and `cached_real` and `resolved_real` are the same paths with each symlink resolved.
    """

    entry: str
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


def stale_compilers(build: Path) -> tuple[StaleCompiler, ...]:
    """Return each compiler of the cache of a build directory that its toolchain file does not resolve.

    THE STALE COMPILER gives the rules.  Complexity: one read of the reply of the cache.
    """
    try:
        entries = cmake_reader.cache(build)
    except cmake_reader.CmakeError:
        return ()
    toolchain = entries.get("CMAKE_TOOLCHAIN_FILE")
    resolved = {} if toolchain is None else resolved_compilers(toolchain.value)
    stale: list[StaleCompiler] = []
    for entry in COMPILER_ENTRIES:
        cached, compiler = entries.get(entry), resolved.get(entry)
        if cached is None or compiler is None or PurePosixPath(cached.value).name != compiler.name:
            continue
        cached_real, resolved_real = (
            os.path.realpath(cached.value),
            os.path.realpath(compiler),
        )
        if cached_real != resolved_real:
            stale.append(StaleCompiler(entry, cached.value, cached_real, str(compiler), resolved_real))
    return tuple(stale)


def heal_line(name: str, build: Path, stale: tuple[StaleCompiler, ...]) -> str:
    """Return the line that names each stale compiler of the build directory of a configuration, or an empty line."""
    if not stale:
        return ""
    parts = " and ".join(
        f"{item.entry} {item.cached} ({item.cached_real}) in place of {item.resolved} ({item.resolved_real})"
        for item in stale
    )
    return (
        f"loop land: {name}: the cache of {build} names {parts} of the toolchain file, so the gate configures the "
        f"build again with {FRESH_WORD}"
    )


def main(argv: list[str]) -> int:
    """Print the line of the stale compilers of a build directory, or nothing (THE OUTPUT).

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
    line = heal_line(args.name, build, stale_compilers(build))
    if line:
        print(line)
    return 0
