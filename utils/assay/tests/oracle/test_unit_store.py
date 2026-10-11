"""The test of assay.oracle.unit_store: each rule of the store, on scratch units in a scratch store.

THE CASES
    The cases build static units and programs with the patched compiler of
    assay.core.testing.  A wrapper of the compiler logs each command, and a
    case can then count the compiles and the links of a lookup.  In some
    cases, the store must refuse a result.  The store then takes the change time of
    the new file as the latest start of its builds.  The change is then in
    the settle period, also on a slow host.  The cases that read a stored
    result come after one wait that follows the last change of the setup.  A
    wait can only make a file older.  A case after a subsequent change must
    give a miss, or a result with the inputs of a stored result.  The time of
    the change then has no effect on the verdict.

THE SPELLED ROOTS
    The commands of one program give a library directory and an include
    directory with "..", as GCC gives its own directories.  A library in a
    directory that is a parent of these spellings makes that directory a
    search root.  Before the paths of a record had no link in them, the store
    made probes from the two spellings.  One probe then went to the directory
    of the test.  The case changes that directory before the build, and the
    store then kept no result.  `guard selftest oracle` runs the module with
    the other modules of its group.

THE PARTS
    With `--part K/N`, the part of STORE_PART runs the cases of the store.
    The part of ROOTS_PART runs the cases of the spelled roots (THE PARTS of
    assay.tests.oracle).  Part N - 1 runs the cases of sweep_sources().
    Each of the first two groups waits one time for the settle period of the
    store.  The two waits then go to two parts of the group oracle, and with
    more than FIRST_UNIT_PART parts, no unit of test_runner goes to them.

`guard selftest tests.oracle.test_unit_store [--part K/N]` runs the test alone.
"""

import os
import stat
import tempfile
import time
from collections.abc import Sequence
from pathlib import Path

from assay.core import cache_root, paths, proc, testing
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.guard import selftest
from assay.loop.fixture import store as compile_store
from assay.oracle.unit_store import UnitStore
from assay.tests import oracle as oracle_parts

# The group oracle gives this module the part of the call (THE PARTS).
PARTED = True

# The wait after the setup.  Each file of the setup is then older than the settle period of the store.
SETTLED_S = compile_store._SETTLE_NS / 1e9 + 0.2


def counting_compiler(cxx: str, directory: Path) -> tuple[str, Path]:
    """Return a compiler that runs ``cxx`` and logs its arguments, and the log.

    One line of the log is one compile or one link.  A line of a query of the
    driver, of a search query or of a run for the dependencies is not.
    """
    log = directory / "runs.log"
    wrapper = directory / "counted-cxx"
    wrapper.write_text(f'#!/bin/sh\necho "$*" >> "{log}"\nexec "{cxx}" "$@"\n', encoding="utf-8")
    wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR)
    return str(wrapper), log


def builds(log: Path) -> int:
    """Return the count of compiles and links in the log of counting_compiler().  Complexity: O(lines of the log)."""
    try:
        lines = log.read_text(encoding="utf-8").splitlines()
    except OSError:
        return 0
    return sum(" -print-" not in f" {line}" and " -E " not in f" {line} " and " -MG" not in line for line in lines)


def build_library(cxx: str, directory: Path, value: int, name: str = "value") -> bool:
    """Build directory/libNAME.so with a function NAME_number that returns ``value``.  Return True when it builds."""
    source = directory / f"{name}.cpp"
    source.write_text(f"int {name}_number() {{ return {value}; }}\n", encoding="utf-8")
    command = [cxx, "-shared", "-fPIC", str(source), "-o", str(directory / f"lib{name}.so")]
    return proc.run(proc.Command(command, timeout_s=proc.TOOL_S)).exit == 0


def store_cases(expect: testing.Expect, cxx: str, root: Path) -> None:
    """Run each case of the rules of the store in ``root`` (THE CASES)."""
    include, early, library, early_library, scratch = (
        root / name for name in ("include", "early", "library", "early_library", "scratch")
    )
    for directory in (include, early, library, early_library, scratch):
        directory.mkdir()
    (include / "probe.h").write_text("#pragma once\ninline constexpr int probe_value = 1;\n")
    expect("the scratch library builds", build_library(cxx, library, 7))
    static = [
        (
            "unit.cpp",
            '#include "probe.h"\nstatic_assert(probe_value == 1, "the probe value");\nint main() { return 0; }\n',
        )
    ]
    planted = [("planted.cpp", '#include "probe.h"\nstatic_assert(probe_value == 2, "the planted probe value");\n')]
    program = [
        (
            "main.cpp",
            (
                '#include <cstdio>\n#include "probe.h"\nint value_number();\n'
                'int main() { std::printf("value %d\\n", value_number() + probe_value); return 0; }\n'
            ),
        )
    ]
    flags = ["-std=c++20", f"-I{early}", f"-I{include}"]
    link_flags = [f"-L{early_library}", f"-L{library}", "-lvalue", f"-Wl,-rpath,{early_library}:{library}"]
    store_root = root / "store"
    counted, log = counting_compiler(cxx, root)

    def unit_store(unit_flags: Sequence[str] = tuple(flags), latest_start_ns: int | None = None) -> UnitStore:
        """Return a store of the units of the case, with the counted compiler."""
        return UnitStore(counted, unit_flags, link_flags, scratch, store_root, latest_start_ns=latest_start_ns)

    first = unit_store(latest_start_ns=(include / "probe.h").stat().st_ctime_ns)
    fresh = first.static(static)
    first.static(planted)
    first.program(program)
    reader = unit_store()
    kept = [
        reader.stored(unit)
        for unit in (reader.static_unit(static), reader.static_unit(planted), reader.program_unit(program))
    ]
    expect(
        "a unit whose inputs changed in the settle period builds, and the store keeps no result",
        fresh.returncode == 0 and kept == [None, None, None],
    )
    time.sleep(SETTLED_S)

    cold = unit_store().static(static)
    before = builds(log)
    warm = unit_store().static(static)
    expect(
        "a warm static unit comes from the store, and no compile runs",
        not cold.is_stored
        and cold.returncode == 0
        and warm.is_stored
        and warm.returncode == 0
        and builds(log) == before,
    )
    unit_store().static(planted)
    planted_warm = unit_store().static(planted)
    expect(
        "a failed compile comes from the store with its error output",
        planted_warm.is_stored and planted_warm.returncode == 1 and "the planted probe value" in planted_warm.stderr,
    )
    renamed = [("renamed.cpp", static[0][1])]
    # The first build starts at most one settle period after the write of
    # the source.  The source is then in its settle period, also on a slow
    # host.
    written_ns = unit_store().sources(renamed)[0].stat().st_ctime_ns
    renamed_first = unit_store(latest_start_ns=written_ns + compile_store._SETTLE_NS).static(renamed)
    expect(
        "a new source does not wait for the settle period: its first build stores the result",
        not renamed_first.is_stored and unit_store().static(renamed).is_stored,
    )
    # An empty element of LD_LIBRARY_PATH names the working directory of the loader (THE RUN ENVIRONMENT of the
    # store).  The first build of a new program then also stores its result.
    with testing.environment_variable("LD_LIBRARY_PATH", f"{early_library}{os.pathsep}"):
        new_program = [("new_main.cpp", program[0][1])]
        written_ns = unit_store().sources(new_program)[0].stat().st_ctime_ns
        new_first = unit_store(latest_start_ns=written_ns + compile_store._SETTLE_NS).program(new_program)
        new_second = unit_store().program(new_program)
    expect(
        "with an empty element in LD_LIBRARY_PATH, the first build of a new program stores the result",
        not new_first.is_stored
        and new_first.stdout == "value 8\n"
        and new_second.is_stored
        and new_second.stdout == "value 8\n",
    )
    directory = unit_store().sources(static)[0].parent
    (directory / "probe.h").write_text("#pragma once\ninline constexpr int probe_value = 4;\n")
    beside = unit_store().static(static)
    expect(
        "a file beside the sources makes the unit miss, and the include finds it",
        not beside.is_stored and beside.returncode == 1,
    )
    (directory / "probe.h").unlink()
    (directory / "unit.cpp").write_text("int damaged_source;\n")
    rewritten = unit_store().static(static)
    expect(
        "a source with other bytes is written again before the lookup, so the stored result holds",
        rewritten.is_stored
        and rewritten.returncode == 0
        and (directory / "unit.cpp").read_text(encoding="utf-8") == static[0][1],
    )
    unit_store().program(program)
    before = builds(log)
    program_warm = unit_store().program(program)
    expect(
        "a warm program comes from the store with the output of its run, and no compile or link runs",
        program_warm.is_stored and program_warm.stdout == "value 8\n" and builds(log) == before,
    )
    unstable = [unit_store().program(program, ["unstable"], is_stable=lambda result: False) for _ in range(2)]
    expect(
        "a run that the caller calls unstable is not stored",
        [result.is_stored for result in unstable] == [False, False] and unstable[1].stdout == "value 8\n",
    )
    expect("the arguments of the run are part of the key", not unit_store().program(program, ["other"]).is_stored)
    expect("a new compile flag gives a new key", not unit_store([*flags, "-DEXTRA_FLAG=1"]).static(static).is_stored)
    from concurrent.futures import ThreadPoolExecutor

    with ThreadPoolExecutor(max_workers=4) as pool:
        parallel = list(pool.map(lambda files: unit_store().static(files), [static, static, planted, planted]))
    expect(
        "units in many threads give the stored results",
        [(result.returncode, result.is_stored) for result in parallel] == [(0, True), (0, True), (1, True), (1, True)],
    )

    (include / "probe.h").write_text("#pragma once\ninline constexpr int probe_value = 2;\n")
    changed = unit_store().static(static)
    expect(
        "a changed header makes the unit miss, so the store gives no stale result",
        not changed.is_stored and changed.returncode == 1 and "the probe value" in changed.stderr,
    )
    changed_program = unit_store().program(program)
    expect(
        "a changed header makes the program miss",
        not changed_program.is_stored and changed_program.stdout == "value 9\n",
    )
    (include / "probe.h").write_text("#pragma once\ninline constexpr int probe_value = 1;\n")
    expect("the header with the stored bytes reads the stored result again", unit_store().static(static).is_stored)

    (early / "probe.h").write_text("#pragma once\ninline constexpr int probe_value = 3;\n")
    shadowed = unit_store().static(static)
    expect(
        "a header that a directory earlier on the search path now holds makes the unit miss",
        not shadowed.is_stored and shadowed.returncode == 1,
    )
    (early / "probe.h").unlink()

    expect("a changed library builds", build_library(cxx, library, 9))
    relinked = unit_store().program(program)
    expect(
        "a changed library that the link and the run read makes the program miss",
        not relinked.is_stored and relinked.stdout == "value 10\n",
    )
    expect(
        "a library in a directory that the linker and the loader tried first builds",
        build_library(cxx, early_library, 11),
    )
    probed = unit_store().program(program)
    expect(
        "a library on a path that the linker and the loader tried and did not find makes the program miss",
        not probed.is_stored and probed.stdout == "value 12\n",
    )

    with testing.environment_variable(cache_root.ROOT_VARIABLE, cache_root.OFF):
        disabled = UnitStore(cxx, flags, link_flags, scratch)
        result = disabled.static(static)
    expect(
        "with the caches off, a unit builds and there is no store",
        disabled.store is None and result.returncode == 0 and not result.is_stored,
    )


def spelled_root_cases(expect: testing.Expect, cxx: str, root: Path) -> None:
    """A change of the directory above the spelled roots stops no record of their program (THE SPELLED ROOTS)."""
    directories = root / "gcc" / "lib" / "x", root / "gcc" / "lib64", root / "gcc" / "include", root / "deep" / "one"
    spelled_lib, lib64, include, deep = directories
    scratch = root / "scratch"
    for directory in (*directories, scratch):
        directory.mkdir(parents=True)
    (include / "spelled.h").write_text("#pragma once\ninline constexpr int spelled_value = 1;\n")
    expect(
        "the two libraries of the spelled roots build",
        build_library(cxx, lib64, 7) and build_library(cxx, spelled_lib, 2, "other"),
    )
    program = [
        (
            "spelled.cpp",
            (
                '#include "spelled.h"\nint value_number();\nint other_number();\n'
                "int main() { return value_number() + other_number() + spelled_value == 10 ? 0 : 1; }\n"
            ),
        )
    ]
    flags = ["-std=c++20", f"-I{deep}", f"-I{spelled_lib}/../../include"]
    link_flags = [
        f"-L{spelled_lib}",
        "-lother",
        f"-L{spelled_lib}/../../lib64",
        "-lvalue",
        f"-Wl,-rpath,{spelled_lib}:{lib64}",
    ]
    store_root = root / "store"

    def unit_store(latest_start_ns: int | None = None) -> UnitStore:
        """Return a store of the program of the case."""
        return UnitStore(cxx, flags, link_flags, scratch, store_root, latest_start_ns=latest_start_ns)

    unit_store().sources(program)
    time.sleep(SETTLED_S)
    touched = root / "touched"
    touched.write_text("")
    touched.unlink()
    first = unit_store(latest_start_ns=root.stat().st_ctime_ns).program(program)
    second = unit_store().program(program)
    expect(
        "a library and a header that the commands give through a directory with '..' make no probe outside "
        "the inputs, so a change of the directory above them stops no record",
        first.returncode == 0 and not first.is_stored and second.is_stored and second.returncode == 0,
    )


def sweep_cases(expect: testing.Expect) -> None:
    """sweep_sources() holds the byte limit of the sources, and it keeps a directory that a unit can use."""
    with tempfile.TemporaryDirectory(prefix="unit_store_sweep_") as scratch:
        root = Path(scratch)
        store = UnitStore("c++", (), (), root / "scratch", store_root=root / "store")
        # Four directories of 40 KB each: three with a use some hours ago, and one with a use a minute ago.  The time
        # of the write of a directory stands for the clock.
        ages = {"aa": 5 * 3600.0, "bb": 4 * 3600.0, "cc": 3 * 3600.0, "dd": 60.0}
        for name, age in ages.items():
            directory = store.sources_root / name / (name * 32)
            directory.mkdir(parents=True)
            (directory / "unit.cpp").write_bytes(b"x" * (40 << 10))
            written = directory.stat().st_mtime
            os.utime(directory, (written - age, written - age))
        # A directory takes 0 or 4 KB of its own on the file systems of the host.  The limit gives one result in the two
        # conditions.
        removed = store.sweep_sources(max_bytes=90 << 10)
        kept = [path.name for path in paths.listing(store.sources_root) if path.is_dir() and paths.listing(path)]
        expect(
            "over the size limit, the sweep removes the directories with the oldest use, and keeps a directory with a "
            "use in the last two hours",
            removed == 3 and kept == ["dd"],
        )
        expect(
            "a second sweep in the same day is not due when a recent sample is under the limit",
            store.sweep_sources(max_bytes=90 << 10) == 0,
        )
        with testing.environment_variable(cache_root.ROOT_VARIABLE, cache_root.OFF):
            expect(
                "with the caches off there is no store, and the sweep removes nothing",
                UnitStore("c++", (), (), root / "scratch").sweep_sources() == 0,
            )


def store_group(expect: testing.Expect) -> None:
    """Run the cases of the store in a scratch directory of their own."""
    cxx = testing.held_compiler(expect)
    if cxx is not None:
        with tempfile.TemporaryDirectory(prefix="unit_store_test_") as scratch:
            store_cases(expect, cxx, Path(scratch))


def spelled_root_group(expect: testing.Expect) -> None:
    """Run the cases of the spelled roots in a scratch directory of their own."""
    cxx = testing.held_compiler(expect)
    if cxx is not None:
        with tempfile.TemporaryDirectory(prefix="unit_store_spelled_") as scratch:
            spelled_root_cases(expect, cxx, Path(scratch))


def main(argv: list[str]) -> int:
    """Run the cases of one part, and write one line for each (THE PARTS)."""
    module = __name__.removeprefix("assay.")
    part = selftest.parsed_part(argv)
    if part is None:
        check_report.write_line(f"usage: guard selftest {module} [--part K/N], with 0 <= K < N", is_error=True)
        return ExitCode.USAGE
    index, count = part
    groups: list[testing.Group] = []
    if index == oracle_parts.role_part(oracle_parts.STORE_PART, count):
        groups.append(store_group)
    if index == oracle_parts.role_part(oracle_parts.ROOTS_PART, count):
        groups.append(spelled_root_group)
    if index == count - 1:
        groups.append(sweep_cases)
    return testing.run(module, [], groups)
