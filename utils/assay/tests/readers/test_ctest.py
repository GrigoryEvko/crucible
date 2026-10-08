"""The test of the reader of ctest: the test list, the results of a run and the records of the tests (assay.readers.ctest).

`guard selftest tests.readers.test_ctest` runs the test.  The planted cases
use no program, or a planted ctest that writes a planted listing.  The last
case configures a scratch project that includes cmake/TestLauncher.cmake.  It
uses the cmake and the ctest that ASSAY_TEST_CMAKE and ASSAY_TEST_CTEST name,
and the registration of the test sets them.  With no variable, the case uses the
cmake and the ctest of PATH at the pin.  Then it runs the tests of the project
and reads the list, the JUnit report and the records that ctest and the test
launcher write.  Each case writes one line.  Exit 0 when each case holds,
else 1.
"""

import json
import os
import sys
import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import proc, testing
from assay.core.paths import REPO_ROOT
from assay.readers import cmake, ctest

# A listing of three tests.  The backtrace of each test is the stack of its registration, innermost first.  The
# script test comes from crucible_script_test() in the file of its directory.  Its call is the line of that call.
LISTING = {
    "kind": "ctestInfo",
    "backtraceGraph": {
        "commands": ["add_test", "crucible_script_test", "crucible_test"],
        "files": ["/s/test/CMakeLists.txt", "/s/cmake/TestLauncher.cmake"],
        "nodes": [
            {"file": 0},
            {"command": 0, "file": 0, "line": 164, "parent": 0},
            {"command": 1, "file": 0, "line": 337, "parent": 0},
            {"command": 0, "file": 1, "line": 58, "parent": 2},
            {"command": 2, "file": 0, "line": 237, "parent": 0},
            {"command": 0, "file": 0, "line": 134, "parent": 4},
        ],
    },
    "tests": [
        {
            "name": "direct",
            "command": ["/usr/bin/python3", "/s/check.py"],
            "backtrace": 1,
            "properties": [{"name": "LABELS", "value": ["ci_guard", "meta"]}, {"name": "TIMEOUT", "value": 30}],
        },
        {
            "name": "wrapped",
            "command": [
                "/usr/bin/python3",
                "-S",
                "/s/utils/bin/loop",
                "test-step",
                "--name",
                "wrapped",
                "/usr/bin/python3",
                "/s/check.py",
            ],
            "backtrace": 3,
            "properties": [{"name": "LABELS", "value": ["ci_guard"]}],
        },
        {"name": "test_arena", "command": ["/b/test/test_arena"], "backtrace": 5},
    ],
}

# A planted ctest: it writes the file listing.txt of the directory of --test-dir, and it exits with the status of
# status.txt.
PLANTED_CTEST = """\
import sys
from pathlib import Path
build = Path(sys.argv[sys.argv.index("--test-dir") + 1])
sys.stdout.write((build / "listing.txt").read_text())
status = build / "status.txt"
sys.exit(int(status.read_text()) if status.is_file() else 0)
"""


def refused(read: Callable[[], object]) -> str:
    """Return the message of the CtestError that a call raises, or an empty text when the call raises none."""
    try:
        read()
    except ctest.CtestError as problem:
        return str(problem)
    return ""


def listing_cases(expect: testing.Expect) -> None:
    """Check the read of a listing: the command, the properties as words and the call of each test."""
    tests = {test.name: test for test in ctest.parse(json.dumps(LISTING))}
    expect(
        "parse gives each test of the listing with its command",
        set(tests) == {"direct", "wrapped", "test_arena"} and tests["test_arena"].command == ("/b/test/test_arena",),
    )
    expect(
        "a property gives its words, also a property with one value that is not a list",
        tests["direct"].words_of("LABELS") == ("ci_guard", "meta")
        and tests["direct"].words_of("TIMEOUT") == ("30",)
        and tests["direct"].words_of("ENVIRONMENT") == (),
    )
    expect(
        "the place of a test of add_test is its add_test",
        (tests["direct"].file, tests["direct"].line) == ("/s/test/CMakeLists.txt", 164),
    )
    expect(
        "the place of a script test is its call of crucible_script_test(), and not the add_test inside it",
        (tests["wrapped"].file, tests["wrapped"].line) == ("/s/test/CMakeLists.txt", 337),
    )
    expect(
        "the place of a test of another function is its add_test",
        (tests["test_arena"].file, tests["test_arena"].line) == ("/s/test/CMakeLists.txt", 134),
    )
    expect("parse refuses a text that is not JSON", "not JSON" in refused(lambda: ctest.parse("{")))
    expect("parse refuses a JSON text with no array tests", "no array tests" in refused(lambda: ctest.parse("{}")))


def planted_ctest(build: Path, listing: str, status: int = 0) -> None:
    """Make the planted ctest the ctest of a build directory, and give it its listing and its exit status."""
    program = build / "planted-ctest.py"
    program.write_text(f"#!{sys.executable}\n{PLANTED_CTEST}", encoding="utf-8")
    program.chmod(0o755)
    cmake.plant(build, cache={"CMAKE_CTEST_COMMAND": ("FILEPATH", str(program))})
    (build / "listing.txt").write_text(listing, encoding="utf-8")
    (build / "status.txt").write_text(f"{status}\n", encoding="utf-8")


def tool_cases(expect: testing.Expect) -> None:
    """Check the run of the ctest of the CMake cache, with a planted ctest."""
    with tempfile.TemporaryDirectory(prefix="assay-ctest-") as scratch:
        build = Path(scratch)
        expect(
            "tests refuses a build directory with no reply of the cache, and names the cache",
            "cache" in refused(lambda: ctest.tests(build)),
        )
        cmake.plant(build, cache={"CMAKE_GENERATOR": "Ninja"})
        expect(
            "tests refuses a cache that names no ctest",
            "has no entry CMAKE_CTEST_COMMAND" in refused(lambda: ctest.tests(build)),
        )
        cmake.plant(build, cache={"CMAKE_CTEST_COMMAND": ("FILEPATH", str(build / "no-such-ctest"))})
        expect("tests refuses a ctest that does not exist", "does not exist" in refused(lambda: ctest.tests(build)))
        planted_ctest(build, json.dumps(LISTING))
        expect(
            "tests runs the ctest of the cache and reads its listing",
            [test.name for test in ctest.tests(build)] == ["direct", "wrapped", "test_arena"],
        )
        expect(
            "tests with a label gives only the tests with that label",
            [test.name for test in ctest.tests(build, "meta")] == ["direct"],
        )
        planted_ctest(build, json.dumps(LISTING), status=8)
        expect("tests checks the exit status of ctest", "exits with 8" in refused(lambda: ctest.tests(build)))


JUNIT = """<?xml version="1.0" encoding="UTF-8"?>
<testsuite tests="5">
  <testcase name="passed" status="run"/>
  <testcase name="failed" status="fail"><failure message="Failed"/></testcase>
  <testcase name="failed_with_no_status"><failure message="Failed"/></testcase>
  <testcase name="skipped" status="notrun"/>
  <testcase name="off" status="disabled"/>
</testsuite>
"""


def results_cases(expect: testing.Expect) -> None:
    """Check the read of a JUnit report: each status, a case with no status, and each refusal."""
    with tempfile.TemporaryDirectory(prefix="assay-ctest-") as scratch:
        report = Path(scratch) / "junit.xml"
        report.write_text(JUNIT, encoding="utf-8")
        expect(
            "results gives the status of each test case, and a failure child with no status is a failure",
            ctest.results(report)
            == {
                "passed": ctest.TestStatus.RUN,
                "failed": ctest.TestStatus.FAIL,
                "failed_with_no_status": ctest.TestStatus.FAIL,
                "skipped": ctest.TestStatus.NOTRUN,
                "off": ctest.TestStatus.DISABLED,
            },
        )
        report.write_text('<testsuite><testcase name="a" status="lost"/></testsuite>', encoding="utf-8")
        expect(
            "results refuses a status that ctest does not write",
            "does not write" in refused(lambda: ctest.results(report)),
        )
        report.write_text("<testsuite>", encoding="utf-8")
        expect(
            "results refuses a report that is not XML, and names the command that writes it",
            "--output-junit" in refused(lambda: ctest.results(report)),
        )
        expect(
            "results refuses a report that does not exist",
            "cannot be read" in refused(lambda: ctest.results(Path(scratch) / "missing.xml")),
        )


def record_cases(expect: testing.Expect) -> None:
    """Check the read of the records of the tests that assay.core.meter writes."""
    with tempfile.TemporaryDirectory(prefix="assay-ctest-") as scratch:
        build = Path(scratch)
        expect("a build directory with no record gives no record", ctest.costs(build) == {})
        run = cost_meter.Measurement(3, 1.5, 0.25, 2.0, 2048, 0.5, 100.0, None)
        cost_meter.write_test_record(str(build), "planted_test", "/b/test/planted_test", run, "failed")
        expect(
            "costs gives the record of each test by its name, with its result, its status and its cost",
            ctest.costs(build)
            == {
                "planted_test": ctest.TestRecord(
                    "planted_test", "failed", "/b/test/planted_test", 3, 1.75, 2.0, 2048, 100.0
                )
            },
        )
        record = Path(cost_meter.test_record_file(str(build), "planted_test"))
        record.write_text(record.read_text().replace('"test": "planted_test"', '"test": "another_test"'))
        expect(
            "costs refuses a record whose file has the name of another test",
            "another name" in refused(lambda: ctest.costs(build)),
        )
        record.write_text('{"format": 1, "step": "compile", "cost": {}}')
        expect(
            "costs refuses a record that is not a record of a test, and names the command that writes it",
            "loop test-step" in refused(lambda: ctest.costs(build)),
        )
        record.write_text("{")
        expect("costs refuses a record that is not JSON", "cannot be read" in refused(lambda: ctest.costs(build)))


def program(name: str) -> str:
    """Return the program of the last case: the variable ASSAY_TEST_NAME, or the pinned program of PATH, or ""."""
    given = os.environ.get(f"ASSAY_TEST_{name.upper()}")
    if given:
        return given
    from assay.setup import pins

    try:
        return pins.pinned_program(name)
    except pins.PinError:
        return ""


def project_cases(expect: testing.Expect) -> None:
    """Configure and test a scratch project with the test launcher, and read what ctest and the launcher write."""
    cmake_program, ctest_program = program("cmake"), program("ctest")
    expect("the cmake and the ctest of the project case are given", bool(cmake_program and ctest_program))
    if not (cmake_program and ctest_program):
        return
    with tempfile.TemporaryDirectory(prefix="assay-ctest-") as scratch:
        source, build = Path(scratch) / "source", Path(scratch) / "build"
        source.mkdir()
        (source / "exit.py").write_text("import sys\nsys.exit(int(sys.argv[1]))\n", encoding="utf-8")
        (source / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.29)\nproject(planted NONE)\nenable_testing()\n"
            f'set(CRUCIBLE_PYTHON3 "{sys.executable}")\ninclude("{REPO_ROOT / "cmake" / "TestLauncher.cmake"}")\n'
            f'crucible_script_test(NAME planted_pass COMMAND "{sys.executable}" "{source / "exit.py"}" 0)\n'
            f'crucible_script_test(NAME planted_fail COMMAND "{sys.executable}" "{source / "exit.py"}" 1)\n'
            'set_tests_properties(planted_pass PROPERTIES LABELS "ci_guard")\n',
            encoding="utf-8",
        )
        cmake.request(build)
        configured = proc.run(proc.Command([cmake_program, "-S", str(source), "-B", str(build)], timeout_s=proc.TOOL_S))
        expect(
            "the scratch project configures" + (f": {configured.err.strip()[-300:]}" if configured.exit else ""),
            configured.exit == 0,
        )
        if configured.exit != 0:
            return
        tests = {test.name: test for test in ctest.tests(build)}
        expect(
            "tests reads the listing of ctest, with the launcher in front of each command and the line of each "
            "call of crucible_script_test()",
            set(tests) == {"planted_pass", "planted_fail"}
            and "test-step" in tests["planted_pass"].command
            and (Path(tests["planted_fail"].file).name, tests["planted_fail"].line) == ("CMakeLists.txt", 7),
        )
        expect(
            "tests with a label reads the labels of ctest",
            [test.name for test in ctest.tests(build, "ci_guard")] == ["planted_pass"],
        )
        report = build / "junit.xml"
        environment = proc.environment({})
        environment.pop("CRUCIBLE_TEST_LAUNCHED", None)
        proc.run(
            proc.Command(
                [ctest_program, "--test-dir", str(build), "--output-junit", str(report)],
                timeout_s=proc.TOOL_S,
                env=environment,
            )
        )
        expect(
            "results reads the JUnit report of ctest",
            ctest.results(report) == {"planted_pass": ctest.TestStatus.RUN, "planted_fail": ctest.TestStatus.FAIL},
        )
        records = ctest.costs(build)
        expect(
            "costs reads the record that the launcher writes for each script test",
            {name: (record.result, record.exit_code) for name, record in records.items()}
            == {"planted_pass": ("passed", 0), "planted_fail": ("failed", 1)}
            and records["planted_pass"].program == sys.executable,
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."), argv, (listing_cases, tool_cases, results_cases, record_cases, project_cases)
    )
