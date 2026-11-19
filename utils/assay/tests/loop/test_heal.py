"""The test of the stale compilers of a build directory (assay.loop.heal).

`guard selftest tests.loop.test_heal` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The cases plant install prefixes with empty compilers, a symlink to a
prefix, and build directories with a planted reply of the cache.  A cache
with the compilers of an old prefix must give a line that names the paths of
the cache and of the toolchain file, and the command `loop heal` must print
it.  A cache that agrees, a wrapper, a toolchain file that the tree does not
hold and a build directory with no reply must give no line.  A symlink that
points to a new prefix must give a line, and a cache and a toolchain file
that name one symlink must agree.  The defaults of assay.setup.gcc must be
the defaults of the toolchain files of this tree.
"""

import os
import sys
import tempfile
from pathlib import Path

from assay.core import paths, proc, testing
from assay.loop import heal
from assay.readers import cmake as cmake_reader
from assay.readers import ninja as ninja_reader
from assay.setup import gcc as patched_gcc

NATIVE = "Toolchain-gcc16.cmake"
CROSS = "Toolchain-gcc16-aarch64.cmake"


def planted_prefix(work: Path, name: str) -> Path:
    """Plant an install prefix of the patched GCC with the four compilers that the command reads, and return it."""
    bin_directory = work / name / "usr" / "bin"
    bin_directory.mkdir(parents=True)
    for program in (
        "g++-16p",
        "gcc-16p",
        "aarch64-linux-gnu-g++-16p",
        "aarch64-linux-gnu-gcc-16p",
    ):
        (bin_directory / program).write_text("", encoding="utf-8")
    return work / name


def program(prefix: Path, name: str) -> str:
    """Return the path of one program of an install prefix."""
    return str(prefix / "usr" / "bin" / name)


def resolved_records(toolchain: str) -> dict[str, str]:
    """Return the records of a configure now: the real path of each compiler that the toolchain file resolves."""
    resolved = heal.resolved_compilers(f"/planted/tree/cmake/{toolchain}")
    return {heal.RECORD_ENTRIES[entry]: os.path.realpath(compiler) for entry, compiler in resolved.items()}


def plant_compilers(
    build: Path, toolchain: str, cxx: str, cc: str | None = None, records: dict[str, str] | None = None
) -> None:
    """Plant a configured build directory whose cache names a toolchain file, its compilers and their records."""
    build.mkdir(parents=True, exist_ok=True)
    ninja_reader.plant_graph(build, "")
    cache: dict[str, str | tuple[str, str]] = {
        "CMAKE_TOOLCHAIN_FILE": ("FILEPATH", f"/planted/tree/cmake/{toolchain}"),
        "CMAKE_CXX_COMPILER": ("FILEPATH", cxx),
        **(records or {}),
    }
    if cc is not None:
        cache["CMAKE_C_COMPILER"] = ("FILEPATH", cc)
    cmake_reader.plant(build, cache=cache)


def line_of(build: Path) -> str:
    """Return the line of the command for the build directory of the configuration default."""
    return heal.heal_line("default", build, heal.stale_compilers(build))


def native_cases(expect: testing.Expect) -> None:
    """Hold the stale compilers of the toolchain file of the host, with a new prefix and with a symlink."""
    with tempfile.TemporaryDirectory(prefix="heal-native-") as work_text:
        work = Path(work_text)
        old, new = planted_prefix(work, "old"), planted_prefix(work, "new")
        link = work / "link"
        link.symlink_to(old)
        build = work / "build"
        with testing.environment_variable("CRUCIBLE_CXX", None):
            with testing.environment_variable("CRUCIBLE_GCC16_PREFIX", str(new)):
                plant_compilers(build, NATIVE, program(old, "g++-16p"), program(old, "gcc-16p"))
                line = line_of(build)
                expect(
                    f"a cache with the compilers of an old prefix gives a line that names the paths of the cache and "
                    f"of the toolchain file, for the two compilers: {line}",
                    line.startswith("loop land: default: ")
                    and program(old, "g++-16p") in line
                    and program(new, "g++-16p") in line
                    and "CMAKE_CXX_COMPILER" in line
                    and "CMAKE_C_COMPILER" in line
                    and heal.FRESH_WORD in line,
                )
                command = [
                    sys.executable,
                    str(paths.REPO_ROOT / "utils" / "bin" / "loop"),
                    "heal",
                    str(build),
                    "default",
                ]
                completed = proc.run(proc.Command(command, timeout_s=proc.TOOL_S))
                expect(
                    "the command `loop heal` prints the line and exits 0",
                    completed.exit == 0 and completed.out.strip() == line,
                )
                plant_compilers(
                    build, NATIVE, program(new, "g++-16p"), program(new, "gcc-16p"), resolved_records(NATIVE)
                )
                completed = proc.run(proc.Command(command, timeout_s=proc.TOOL_S))
                expect(
                    "a cache with the compilers that the toolchain file resolves gives no line, and the command "
                    "prints nothing",
                    line_of(build) == "" and completed.exit == 0 and completed.out == "",
                )
                plant_compilers(build, NATIVE, program(old, "afl-g++-fast"), records=resolved_records(NATIVE))
                expect(
                    "a wrapper with a name of its own, such as the afl-g++-fast of loop fuzz, gives no line",
                    line_of(build) == "",
                )
                plant_compilers(build, "Toolchain-other.cmake", program(old, "g++-16p"))
                expect(
                    "a toolchain file that the tree does not hold gives no line",
                    line_of(build) == "",
                )
                plant_compilers(build, NATIVE, program(old, "g++-16p"))
                for reply_file in build.joinpath(*cmake_reader.API_DIRECTORY, "reply").iterdir():
                    reply_file.unlink()
                expect(
                    "a build directory with no reply of the cache gives no line",
                    line_of(build) == "",
                )
            with testing.environment_variable("CRUCIBLE_GCC16_PREFIX", str(link)):
                plant_compilers(build, NATIVE, program(link, "g++-16p"), records=resolved_records(NATIVE))
                expect(
                    "a cache and a toolchain file that name one symlink agree",
                    line_of(build) == "",
                )
                link.unlink()
                link.symlink_to(new)
                (build / heal.NINJA_LOG).write_text("# ninja log v7\n", encoding="utf-8")
                line = line_of(build)
                completed = proc.run(proc.Command(command, timeout_s=proc.TOOL_S))
                expect(
                    f"a symlink that points to a new prefix gives a line also when the cache and the toolchain file "
                    f"name it, from the record of the configure, and the command removes the log of Ninja: {line}",
                    "recorded real path" in line
                    and program(new, "g++-16p") in line
                    and heal.NINJA_LOG in line
                    and completed.exit == 0
                    and not (build / heal.NINJA_LOG).exists(),
                )
                plant_compilers(build, NATIVE, program(old, "g++-16p"))
                line = line_of(build)
                expect(
                    f"a symlink of the toolchain file that points to a new prefix gives a line for a cache of the old "
                    f"prefix: {line}",
                    program(link, "g++-16p") in line and program(new, "g++-16p") in line,
                )
            with testing.environment_variable("CRUCIBLE_GCC16_PREFIX", str(new)):
                plant_compilers(build, NATIVE, program(new, "g++-16p"), program(new, "gcc-16p"))
                (build / heal.NINJA_LOG).write_text("# ninja log v7\n", encoding="utf-8")
                line = line_of(build)
                completed = proc.run(proc.Command(command, timeout_s=proc.TOOL_S))
                expect(
                    f"a cache with no record heals one time, and the log of Ninja stays: {line}",
                    "no recorded real path" in line
                    and heal.NINJA_LOG not in line
                    and completed.exit == 0
                    and (build / heal.NINJA_LOG).is_file(),
                )
            with (
                testing.environment_variable("CRUCIBLE_GCC16_PREFIX", None),
                testing.environment_variable("CRUCIBLE_CXX", program(new, "g++-16p")),
            ):
                plant_compilers(build, NATIVE, program(old, "g++-16p"))
                expect(
                    "CRUCIBLE_CXX names the compiler first, as the toolchain file resolves it",
                    program(new, "g++-16p") in line_of(build),
                )


def cross_cases(expect: testing.Expect) -> None:
    """Hold the stale compilers of the toolchain file of a cross target."""
    with tempfile.TemporaryDirectory(prefix="heal-cross-") as work_text:
        work = Path(work_text)
        old, new = planted_prefix(work, "old"), planted_prefix(work, "new")
        build = work / "build-syntax-aarch64"
        target = patched_gcc.CROSS_TARGETS["aarch64-linux-gnu"]
        with testing.environment_variable(target.prefix_variable, str(new)):
            plant_compilers(build, CROSS, program(old, "aarch64-linux-gnu-g++-16p"))
            expect(
                "a cross build of an old prefix gives a line with the prefix of its cross target",
                program(new, "aarch64-linux-gnu-g++-16p") in line_of(build),
            )
            plant_compilers(build, CROSS, program(new, "aarch64-linux-gnu-g++-16p"), records=resolved_records(CROSS))
            expect(
                "a cross build of the prefix of its cross target gives no line",
                line_of(build) == "",
            )


def mirror_cases(expect: testing.Expect) -> None:
    """Hold that the defaults of assay.setup.gcc are the defaults of the toolchain files of this tree."""
    cmake_directory = paths.REPO_ROOT / "cmake"
    expect(
        "each toolchain file of TOOLCHAIN_TARGETS is a file of the tree",
        all((cmake_directory / name).is_file() for name in heal.TOOLCHAIN_TARGETS),
    )
    native = (cmake_directory / NATIVE).read_text(encoding="utf-8")
    expect(
        "the toolchain file of the host resolves CRUCIBLE_CXX, then CRUCIBLE_GCC16_PREFIX, then HOME_PREFIX",
        "$ENV{CRUCIBLE_CXX}" in native
        and "$ENV{CRUCIBLE_GCC16_PREFIX}" in native
        and f'"$ENV{{HOME}}/{patched_gcc.HOME_PREFIX.as_posix()}"' in native,
    )
    stale_text = (cmake_directory / "StaleCompiler.cmake").read_text(encoding="utf-8")
    expect(
        "cmake/StaleCompiler.cmake writes each record entry that the command reads",
        all(record in stale_text for record in heal.RECORD_ENTRIES.values()),
    )
    for name, triplet in heal.TOOLCHAIN_TARGETS.items():
        if triplet is None:
            continue
        target = patched_gcc.CROSS_TARGETS[triplet]
        text = (cmake_directory / name).read_text(encoding="utf-8")
        expect(
            f"the toolchain file {name} resolves {target.prefix_variable}, then {target.home_prefix}",
            f"$ENV{{{target.prefix_variable}}}" in text and f'"$ENV{{HOME}}/{target.home_prefix.as_posix()}"' in text,
        )


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(__name__.removeprefix("assay."), argv, (native_cases, cross_cases, mirror_cases))
