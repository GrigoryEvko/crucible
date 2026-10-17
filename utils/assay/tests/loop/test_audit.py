"""The test of `loop audit` (assay.loop.audit): the plan and the run of planted build directories.

`guard selftest tests.loop.test_audit` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The cases plant the plan of `loop test`, its JUnit report and the records of
the tests in two build directories.  The report must find each kind of THE
FINDINGS: a missed test, an unplanned test, a shared key that ran again, and
a test with no shared key.  The call cases plant call profiles, and the
report must give the totals of THE CALL PROFILES.  The line cases plant a
source file and the rows of its lines and of the calls of its lines.  The
report must give the table of each line, and it must expand a hot line into
the table of its callee (THE LINES).
"""

import json
import tempfile
from collections.abc import Sequence
from pathlib import Path

from assay.core import calls as call_profile
from assay.core import meter as cost_meter
from assay.core import record as cost_record
from assay.core import testing
from assay.core.exit import ExitCode
from assay.loop import affected, audit
from assay.readers import ctest as ctest_reader


def plant_build(
    root: Path,
    configuration: str,
    planned: list[affected.PlannedTest],
    statuses: Sequence[tuple[str, str]],
    walls: Sequence[tuple[str, float]],
) -> Path:
    """Write one planted build directory: its configuration, its plan, its JUnit report and the record of each test."""
    build = root / configuration
    state = build / affected.STATE_DIRECTORY
    state.mkdir(parents=True)
    (build / audit.CONFIGURATION_FILE).write_text(f"{configuration}\n", encoding="utf-8")
    decisions = [
        affected.Decision(
            planted_test(entry.name), entry.kind, entry.reason, shared=entry.shared, is_skipped=entry.is_skipped
        )
        for entry in planned
    ]
    affected.write_plan(state / affected.PLAN_FILE, decisions)
    cases = "".join(f'<testcase name="{name}" status="{status}"/>' for name, status in statuses)
    (state / "junit.xml").write_text(f'<testsuite tests="{len(statuses)}">{cases}</testsuite>', encoding="utf-8")
    for name, wall_s in walls:
        run = cost_meter.Measurement(0, wall_s, 0.0, wall_s, 1024, 0.0, 1.0, None)
        record = cost_record.TestRecord(name, "passed", "/planted/program", 0, cost_record.measured(run))
        cost_record.write(Path(cost_record.test_record_file(str(build), name)), record)
    return build


def planted_test(name: str) -> ctest_reader.TestEntry:
    """Return a planted registered test with one word as its command."""
    return ctest_reader.TestEntry(name, ("/planted/program",))


def plant_profile(
    directory: Path,
    name: str,
    argv: list[str],
    cpu_s: float,
    rows: list[list[object]],
    lines: Sequence[list[object]] = (),
    calls: Sequence[list[object]] = (),
) -> None:
    """Write one planted call profile of one second of wall time (THE RECORD of assay.core.calls).

    With rows of `lines` and `calls`, the profile has the mode of the lines (THE LINES of assay.core.calls).
    """
    document = {
        "format": call_profile.FORMAT,
        "mode": call_profile.MODE_LINES if lines else call_profile.MODE_FUNCTIONS,
        "argv": argv,
        "pid": 1,
        "parent": 0,
        "start": 10.0,
        "end": 11.0,
        "cpu_s": cpu_s,
        "functions": rows,
        "lines": list(lines),
        "calls": list(calls),
    }
    (directory / f"{name}{call_profile.SUFFIX}").write_text(json.dumps(document), encoding="utf-8")


def audit_cases(expect: testing.Expect) -> None:
    """Check the findings of two planted build directories (THE FINDINGS of assay.loop.audit)."""
    guard = affected.KIND_GUARD
    plan = [
        affected.PlannedTest("same_key", guard, affected.REASON_CHANGED, "key-one", False),
        affected.PlannedTest("missed_test", guard, affected.REASON_CHANGED, "key-two", False),
        affected.PlannedTest("skipped_test", guard, affected.REASON_SAME, "key-three", True),
        affected.PlannedTest("neg_planted", affected.KIND_FIXTURE, affected.REASON_FIXTURE, "", False),
    ]
    with tempfile.TemporaryDirectory(prefix="assay-audit-") as scratch:
        root = Path(scratch)
        first = plant_build(
            root,
            "planted-debug",
            plan,
            (("same_key", "run"), ("skipped_test", "run"), ("neg_planted", "run"), ("missed_test", "notrun")),
            (("same_key", 2.0), ("skipped_test", 1.0), ("neg_planted", 0.5)),
        )
        second = plant_build(
            root,
            "planted-release",
            plan,
            (("same_key", "run"), ("missed_test", "run"), ("neg_planted", "run")),
            (("same_key", 3.0), ("missed_test", 1.0), ("neg_planted", 0.25)),
        )
        found = audit.audit_build(first)
        expect(
            "a selected test that did not run is missed, and a test that ran with a skip in the plan is unplanned",
            found.missed == ("missed_test",) and found.unplanned == ("skipped_test",) and found.planned == 4,
        )
        status, out = testing.captured(lambda: audit.audit([first, second]))
        expect(
            "the report gives each build directory, and it ends with FINDINGS for a missed test",
            status == ExitCode.FINDINGS
            and "planted-debug: 4 tests in the plan, 3 selected, 3 ran, 1 missed, 1 unplanned" in out
            and "planted-release: 4 tests in the plan, 3 selected, 3 ran, 0 missed, 0 unplanned" in out,
        )
        expect(
            "a shared key that ran in the two build directories gives the time of the run after the first run",
            "1 shared keys ran in two build directories or more: 1 runs after the first run, 2.0 s of test time" in out
            and "same_key: 2.0 s more in planted-debug planted-release" in out,
        )
        expect(
            "a test with no shared key that ran in the two build directories is unkeyed, with its kind",
            "1 tests with no shared key ran in two build directories or more: 1 runs after the first run, 0.2 s of "
            "test time (by kind: fixture 1)" in out,
        )
        status, out = testing.captured(lambda: audit.audit([second]))
        expect("one build directory with no missed test gives OK", status == ExitCode.OK)
        main_status, out = testing.captured(lambda: audit.main([str(root / "absent")]))
        expect(
            "a build directory with no plan gives MISSING_INPUT and names the plan",
            main_status == ExitCode.MISSING_INPUT and "plan.json" in out,
        )


def calls_cases(expect: testing.Expect) -> None:
    """Check the totals of planted call profiles (THE CALL PROFILES of assay.loop.audit)."""
    # The masters of the planted profiles are in /tree/utils/bin.  /tree/utils is then the directory of the package.
    package = "/tree/utils/assay/core/rows.py"
    import_row = ["<frozen importlib._bootstrap>", 1, audit.IMPORT_FUNCTION, 3, 1, 0.01, 0.25]
    read_row = [package, 7, "read_table", 1, 1, 0.5, 0.75]
    main_row = [package, 90, "main", 1, 1, 0.0, 0.9]
    with tempfile.TemporaryDirectory(prefix="assay-audit-") as scratch:
        directory = Path(scratch)
        words = ["/tree/utils/bin/guard", "selftest", "tests.core.test_rows"]
        plant_profile(directory, "1-1", words, 0.5, [import_row, read_row, main_row])
        plant_profile(directory, "2-2", words, 0.75, [import_row, read_row, main_row])
        plant_profile(directory, "3-3", ["/tree/utils/bin/loop", "test-step", "one"], 0.25, [import_row])
        status, out = testing.captured(lambda: audit.main(["--calls", str(directory)]))
        expect(
            "the report gives the profiles, the commands and the import time",
            status == ExitCode.OK
            and "3 call profiles of 2 commands, 3.0 s of wall time, 1.5 s of CPU time, 0.8 s of imports" in out
            and "guard selftest: 2 processes, 2.0 s wall, 1.2 s CPU, 0.5 s imports" in out,
        )
        expect(
            "a command with the same words in two processes gives the CPU time of the second run",
            "1 commands ran two times or more with the same words: 1 runs after the first run, 0.5 s of CPU" in out,
        )
        expect(
            "a function of the package in two processes is in the list of shared work, and main is not",
            "1.50 s in 2 processes, 2 calls: read_table (assay/core/rows.py:7)" in out
            and "processes, 2 calls: main" not in out,
        )
        (directory / f"4-4{call_profile.SUFFIX}").write_text('{"format": 1, "argv": 3}', encoding="utf-8")
        status, out = testing.captured(lambda: audit.main(["--calls", str(directory)]))
        expect(
            "a profile of a different shape gives MISSING_INPUT and names the file",
            status == ExitCode.MISSING_INPUT and "4-4.json" in out,
        )
        status, out = testing.captured(lambda: audit.main([]))
        expect("no build directory and no --calls gives USAGE", status == ExitCode.USAGE)


PLANTED_SOURCE = """def outer():
    prepare()
    callee()
    return 1


def callee():
    total = sum(range(10))
    return total
"""


def lines_cases(expect: testing.Expect) -> None:
    """Check the tables of the lines and the expansion of a hot line into its callee (THE LINES of assay.loop.audit)."""
    with tempfile.TemporaryDirectory(prefix="assay-audit-") as scratch:
        utils = Path(scratch) / "utils"
        source = utils / "assay" / "core" / "planted.py"
        source.parent.mkdir(parents=True)
        source.write_text(PLANTED_SOURCE, encoding="utf-8")
        real, words = str(source), [str(utils / "bin" / "guard"), "selftest", "planted"]
        functions = [[real, 1, "outer", 1, 1, 0.1, 1.0], [real, 7, "callee", 1, 1, 0.8, 0.8]]
        lines = [
            [real, 2, "outer", 1, 1, 0.05, 0.05],
            [real, 3, "outer", 1, 1, 0.9, 0.85],
            [real, 4, "outer", 1, 1, 0.05, 0.05],
            [real, 8, "callee", 7, 1, 0.7, 0.7],
            [real, 9, "callee", 7, 1, 0.1, 0.1],
        ]
        calls = [
            [real, 1, "outer", 3, real, 7, "callee", 1, 0.8],
            [real, 1, "outer", 2, real, 7, "callee", 1, 0.01],
            [real, 7, "callee", 8, real, 1, "outer", 1, 0.5],
        ]
        directory = Path(scratch) / "profiles"
        directory.mkdir()
        plant_profile(directory, "1-1", words, 1.0, functions, lines, calls)
        status, out = testing.captured(lambda: audit.main(["--calls", str(directory), "--lines", "outer"]))
        expect(
            "--lines TEXT gives the table of the function, with its calls, its times and the text of each line",
            status == ExitCode.OK
            and "function outer (assay/core/planted.py:1)" in out
            and "1 calls in 1 processes, 1.000 s cumulative, 0.100 s own" in out
            and "0.9000   900000.00    90.0     0.8500      callee()" in out,
        )
        expect(
            "a hot line expands into the table of its callee, below the table of its caller",
            "> line 3 calls callee (assay/core/planted.py:7): 1 calls, 0.800 s, 80.0 % of the first function" in out
            and out.count("function callee (assay/core/planted.py:7)") == 1
            and out.index("> line 3 calls") < out.index("function callee"),
        )
        expect(
            "a call of a line below DRILL_PART, and a call back into a function on the path, get no table",
            "> line 2 calls" not in out and "> line 8 calls" not in out,
        )
        status, out = testing.captured(
            lambda: audit.main(["--calls", str(directory), "--lines", "outer", "--depth", "0"])
        )
        expect("--depth 0 expands no line", status == ExitCode.OK and "> line" not in out)
        status, out = testing.captured(lambda: audit.main(["--calls", str(directory), "--lines"]))
        expect(
            "--lines with no text writes each hot function one time, and a callee with a table above gets one line",
            status == ExitCode.OK
            and out.count("function callee (assay/core/planted.py:7)") == 2
            and "function callee (assay/core/planted.py:7) has a table above" in out,
        )
        status, out = testing.captured(lambda: audit.main(["--calls", str(directory), "--lines", "absent"]))
        expect("--lines with a text that no function holds says so", "no function of the lines holds 'absent'" in out)
        status, out = testing.captured(lambda: audit.main(["--lines"]))
        expect("--lines with no --calls gives USAGE", status == ExitCode.USAGE)
        quiet = Path(scratch) / "quiet"
        quiet.mkdir()
        plant_profile(quiet, "1-1", words, 1.0, functions)
        status, out = testing.captured(lambda: audit.main(["--calls", str(quiet), "--lines"]))
        expect(
            "profiles with no rows of the lines give MISSING_INPUT and name the variable",
            status == ExitCode.MISSING_INPUT and "CRUCIBLE_CALL_PROFILE_LINES" in out,
        )


def plan_cases(expect: testing.Expect) -> None:
    """Check the reader of the plan: a damaged plan and an entry that is not a test give an error."""
    with tempfile.TemporaryDirectory(prefix="assay-audit-") as scratch:
        path = Path(scratch) / affected.PLAN_FILE
        path.write_text("{", encoding="utf-8")
        refused = []
        for text in ("{", '{"format": 9, "tests": []}', '{"format": 1, "tests": [{"name": 3}]}'):
            path.write_text(text, encoding="utf-8")
            try:
                affected.read_plan(path)
            except affected.RunnerError as problem:
                refused.append(str(problem))
        expect(
            "read_plan refuses a plan that is not JSON, a different format and an entry that is not a test",
            len(refused) == 3 and "not JSON" in refused[0] and "format" in refused[1] and "not a test" in refused[2],
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (audit_cases, calls_cases, lines_cases, plan_cases))
