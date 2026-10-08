"""The test of assay.setup.pins: the pin of CMake, the programs of a build directory, and the walk to the ctest of a run.

THE CTEST RUN
    The last case runs `setup check --build-dir` as a test of a scratch
    project, and a ctest of the pin runs it.  It shows that the walk finds the
    process of that ctest, and that its exe link gives the version of the
    program.
    ASSAY_TEST_CMAKE, ASSAY_TEST_CTEST and ASSAY_TEST_NINJA name the tools,
    and the registration of the test in CMake gives them.  The rule tool-pins
    holds the rows of the table, and its plants are its tests.

`guard selftest tests.setup.test_pins` runs the test.
"""

import os
import sys
import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.core import pins as pin_table
from assay.core import proc, testing
from assay.core.paths import SETUP
from assay.readers import cmake as cmake_reader
from assay.setup import pins

TOOLS = ("ASSAY_TEST_CMAKE", "ASSAY_TEST_CTEST", "ASSAY_TEST_NINJA")


def write_program(path: Path, text: str, status: int = 0) -> Path:
    """Write a shell program that prints TEXT and exits with STATUS, and return its path."""
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(f"#!/bin/sh\nprintf '%s\\n' '{text}'\nexit {status}\n", encoding="utf-8")
    path.chmod(0o755)
    return path


def planted_table(*versions: str) -> tuple[pin_table.Pin, ...]:
    """Return a pin table with one row of cmake for each version, each for a platform of its own."""
    platforms = ("linux-x86_64", "linux-aarch64", "darwin-x86_64")
    rows = [
        f"pin cmake via=archive version={version} platform={platforms[index]} "
        f"source=https://example.invalid/cmake.whl sha256={'0' * 64} | a planted pin"
        for index, version in enumerate(versions)
    ]
    return pin_table.parse("# a planted pin table\nformat 1\n" + "".join(f"{row}\n" for row in rows), "planted")


def refused(action: Callable[[], object], fragment: str) -> bool:
    """Return True when ACTION raises a PinError with a message that holds FRAGMENT."""
    try:
        action()
    except pins.PinError as error:
        return fragment in str(error)
    return False


def pin_cases(expect: testing.Expect) -> None:
    """Check the pin of CMake of the tree and of planted tables."""
    tree = pins.read_pin()
    numbers = tree.version.split(".")
    expect("the pin of the tree gives one version X.Y.Z", len(numbers) == 3 and all(part.isdigit() for part in numbers))
    expect(
        "the pin of the tree gives a digest for linux-x86_64 and for linux-aarch64",
        set(tree.digests) == {"linux-x86_64", "linux-aarch64"} and all(len(d) == 64 for d in tree.digests.values()),
    )
    expect(
        "the first row of cmake of the tree is the place of a finding",
        pin_table.PINS.read_text(encoding="utf-8").splitlines()[tree.line - 1].startswith("pin cmake "),
    )
    expect("a pin of two platforms reads", pins.read_pin(planted_table("4.4.2", "4.4.2")).version == "4.4.2")
    expect(
        "a pin of two versions is an error",
        refused(lambda: pins.read_pin(planted_table("4.4.2", "4.4.3")), "two versions 4.4.2 and 4.4.3"),
    )
    expect(
        "a table with no row of cmake is an error",
        refused(lambda: pins.read_pin(planted_table()), "has no row of the pin"),
    )
    expect("each pin of the tree has a holder", all(pins.holder(pin.name) is not None for pin in pin_table.load()))


def program_cases(expect: testing.Expect) -> None:
    """Check the version of a program, the programs of a build directory and the program of PATH."""
    tree = pins.read_pin()
    with tempfile.TemporaryDirectory(prefix="cmake-pin-") as directory:
        work = Path(directory)
        programs = work / "programs"
        pinned_ctest = write_program(programs / "pinned" / "ctest", f"ctest version {tree.version}")
        pinned_cmake = write_program(programs / "pinned" / "cmake", f"cmake version {tree.version}")
        other_ctest = write_program(programs / "other" / "ctest", "ctest version 4.3.0")
        expect("program_version reads the first line", pins.program_version(other_ctest) == ("ctest", "4.3.0"))
        expect(
            "program_version gives None for a program that fails",
            pins.program_version(write_program(programs / "failing", f"ctest version {tree.version}", 1)) is None,
        )
        expect(
            "program_version gives None for a line of another shape",
            pins.program_version(write_program(programs / "shapeless", "GNU bash, version 5.2")) is None,
        )
        expect("program_version gives None for an absent program", pins.program_version(programs / "absent") is None)

        def build(name: str, cmake_program: Path | None, ctest_program: Path | None) -> Path:
            """Plant a build directory with a cache that names the two programs."""
            build_dir = work / "builds" / name
            build_dir.mkdir(parents=True)
            entries = {"CMAKE_COMMAND": str(cmake_program)} if cmake_program else {}
            entries.update({"CMAKE_CTEST_COMMAND": str(ctest_program)} if ctest_program else {})
            cmake_reader.plant(build_dir, cache=entries)
            return build_dir

        pinned_build = build("pinned", pinned_cmake, pinned_ctest)
        expect(
            "configured_program reads CMAKE_CTEST_COMMAND",
            pins.configured_program(pinned_build, "ctest") == str(pinned_ctest),
        )
        expect(
            "a build directory with no cache is an error",
            refused(lambda: pins.configured_program(work / "absent", "cmake"), "holds no index of the CMake file API"),
        )
        expect(
            "a cache with no CMAKE_CTEST_COMMAND is an error",
            refused(
                lambda: pins.configured_program(build("no_ctest", pinned_cmake, None), "ctest"),
                "has no entry CMAKE_CTEST_COMMAND",
            ),
        )
        expect(
            "a cache that names an absent program is an error",
            refused(
                lambda: pins.configured_program(build("absent_program", programs / "absent", pinned_ctest), "cmake"),
                "which does not exist",
            ),
        )
        saved_path = os.environ.get("PATH", "")
        try:
            os.environ["PATH"] = f"{programs / 'other'}:{saved_path}"
            pins.pinned_program.cache_clear()
            expect(
                "a ctest of another version in PATH is an error",
                refused(lambda: pins.pinned_program("ctest"), "gives ctest 4.3.0, but "),
            )
            os.environ["PATH"] = f"{programs / 'pinned'}:{saved_path}"
            pins.pinned_program.cache_clear()
            expect("the pinned ctest in PATH is the program", pins.pinned_program("ctest") == str(pinned_ctest))
        finally:
            os.environ["PATH"] = saved_path
            pins.pinned_program.cache_clear()


def walk_cases(expect: testing.Expect) -> None:
    """Check the walk to the ctest of a run in copies of the shape of /proc."""
    tree = pins.read_pin()
    with tempfile.TemporaryDirectory(prefix="cmake-pin-walk-") as directory:
        work = Path(directory)
        pinned_ctest = write_program(work / "programs" / "ctest", f"ctest version {tree.version}")

        # cmake -E env (400) runs the check (500), and a ctest (300) runs
        # cmake -E env.  The ctest of a chain is the program that its exe link
        # names.
        def chain(name: str, runner: Path | None, runner_name: str = "ctest") -> Path:
            """Write a chain of three processes, and return the copy of /proc."""
            proc_root = work / "proc" / name
            pins.write_process(proc_root, 500, 400, "python3")
            pins.write_process(proc_root, 400, 300, "cmake")
            pins.write_process(proc_root, 300, 1, runner_name, runner)
            return proc_root

        expect(
            "the walk finds the ctest above cmake -E env",
            pins.ancestor_ctest(chain("found", pinned_ctest), 500) == work / "proc" / "found" / "300" / "exe",
        )
        expect(
            "the walk finds no ctest in a chain with none",
            pins.ancestor_ctest(chain("none", None, "bash"), 500) is None,
        )
        looping = work / "proc" / "loop"
        pins.write_process(looping, 500, 400, "python3")
        pins.write_process(looping, 400, 500, "cmake")
        expect("the walk stops in a chain with a loop", pins.ancestor_ctest(looping, 500) is None)


def ctest_run_cases(expect: testing.Expect) -> None:
    """Run the check of a build as a test of a scratch project with a ctest of the pin (THE CTEST RUN)."""
    cmake, ctest, ninja = (os.environ.get(name, "") for name in TOOLS)
    expect(f"{', '.join(TOOLS)} name the tools of the ctest run", bool(cmake and ctest and ninja))
    if not (cmake and ctest and ninja):
        return
    with tempfile.TemporaryDirectory(prefix="cmake-pin-run-") as directory:
        source, build_dir = Path(directory) / "src", Path(directory) / "build"
        source.mkdir(parents=True)
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.25)\nproject(cmake_pin_probe NONE)\nenable_testing()\n"
            f'add_test(NAME probe COMMAND "{sys.executable}" "{SETUP}" check --build-dir "{build_dir}")\n',
            encoding="utf-8",
        )
        # The scratch project has no cmake_file_api call.  Its build directory gets the query of the reply of the
        # cache.
        cmake_reader.request(build_dir)
        try:
            configured = proc.run(
                proc.Command(
                    [cmake, "-S", str(source), "-B", str(build_dir), "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}"],
                    timeout_s=proc.TOOL_S,
                )
            )
            ran = proc.run(
                proc.Command([ctest, "--test-dir", str(build_dir), "--output-on-failure"], timeout_s=proc.TOOL_S)
            )
        except proc.ToolFailed as problem:
            expect(f"the scratch project configures and its test runs: {problem}", False)
            return
        expect("the scratch project configures", configured.exit == 0)
        expect("the check gives no error when the ctest of the build runs it", ran.exit == 0)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (pin_cases, program_cases, walk_cases, ctest_run_cases))
