#!/usr/bin/env python3
"""check-test-time — no test that runs without the test launcher takes more than the budget of the row test-time.

utils/scripts/test-launcher.py judges each executable test while it runs.
CMake gives that launcher only to a test whose command is an executable
target, so a guard, a negative fixture and another script test run with no
launcher.  A ctest test cannot measure the run that it is part of.  ctest
runs this check after the tests of each run in a build directory:
cmake/TestLauncher.cmake writes BUILD_DIR/CTestCustom.cmake, which gives the
check as the command CTEST_CUSTOM_POST_TEST.  ctest prints the findings, and
an error makes ctest exit with a failure status after the tests:

    ctest --test-dir BUILD_DIR -R REGEX -j24
    ...
    check-test-time: 212 tests without the launcher, the slowest ... at 5.67 s, 0 finding(s).

WHAT THE CHECK READS
    * The log of the ctest run: the name, the command and the wall time of
      each test.  With --from-ctest, the check is the command of ctest, and
      it reads the log that its parent ctest holds open: ctest writes the log
      of the run to Testing/Temporary/LastTest.log.tmp<SUFFIX> and renames it
      to LastTest.log after this command.  /proc/PARENT/fd names the file.
      Without --from-ctest, the check reads --log, by default
      BUILD_DIR/Testing/Temporary/LastTest.log of the last ctest command.
      `ctest --show-only` writes that file too, with no test in it.
    * The row test-time of utils/scripts/budgets.txt: the warning threshold
      and the error threshold, in seconds.
    * The kind of the build (BUILD_DIR/build-kind.txt).
    * utils/scripts/test-time-ledger.txt, `kind | test | value | reason`: the
      tests that take more than the error threshold at this time.
    * The test list of the build (ctest --show-only=json-v1), only when a
      test has a finding or the ledger has a row of the kind: the CMake file
      and the line that register each test, for the place of a finding, and
      the tests that the build registers.
    The variables CRUCIBLE_TEST_BUDGETS and CRUCIBLE_TEST_LEDGERS name another
    budget table and another directory of ledgers, as for the test launcher.
    Only a self-test sets them.

THE LOG
    ctest writes one block for each test when the test ends:

        INDEX/COUNT Test: NAME
        Command: "PROGRAM" "ARGUMENT"...
        ...
        Output:
        ----------
        THE OUTPUT OF THE TEST
        <end of output>
        Test time =   SECONDS sec
        ----------
        Test Passed.
        "NAME" end time: DATE

    A block ends at the first trailer whose end-time line names the test of
    the block.  So a line of the output that looks like a header or a trailer
    of another test does not end the block.  A test whose command names
    test-launcher.py is a test of the launcher, and the check skips it.  A
    test that ran more than one time (ctest --repeat) counts with its longest
    run.

LEVELS
    * A test above the warning threshold gives a warning.
    * A test above the error threshold gives an error, unless a ledger row of
      the kind names it.  A test with a row gives a warning, so the debt stays
      visible.  In a build of a kind with tsan, a test above the error
      threshold gives a warning, as in the launcher.
    * A row of the kind whose test ran and took no more than the error
      threshold is an error: remove the row.  A row of the kind whose test
      the build does not register is an error too.  A row whose test did not
      run in this log gives no finding, because a run of a part of the suite
      is permitted.

    A time is the wall time of one test while the other tests of the run run
    at the same time, so the error threshold sits above the noise of the
    shared host.  The commit that sets the row states the measured noise.
    On a GitHub runner, each error that judges a time is a warning that says
    that it was demoted (utils/scripts/cost_meter.py, A CI RUNNER).  A row of
    a test that the build does not register and an input that the check
    cannot read stay errors.

    The log gives no memory of a test.  The row test-memory holds only the
    tests of the launcher.

Usage
    check-test-time.py --build-dir BUILD_DIR [--from-ctest | --log FILE] [--ctest CTEST] [--warnings-dir DIR]
    check-test-time.py --build-dir BUILD_DIR [--log FILE] --write
    check-test-time.py --self-test [--cmake CMAKE] [--ctest CTEST]

Exit 0 with no error, 1 on an error or an input that the check cannot read, 2 on a
usage error or a failed self-test.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Callable
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
import cost_meter  # noqa: E402

CHECK = "test-time"
SCRIPTS = Path(__file__).resolve().parent
ROOT = SCRIPTS.parents[1]
LEDGER_NAME = "test-time-ledger.txt"
BUDGETS_ENV = "CRUCIBLE_TEST_BUDGETS"
LEDGERS_ENV = "CRUCIBLE_TEST_LEDGERS"
TEMPORARY = Path("Testing") / "Temporary"
DEFAULT_LOG = TEMPORARY / "LastTest.log"
LOG_PREFIX = "LastTest.log"
LAUNCHER = "test-launcher.py"
HEADER = re.compile(r"\d+/\d+ Test: (?P<name>.+)")
SECONDS = re.compile(r"Test time = +(?P<seconds>[0-9]+(?:\.[0-9]+)?) sec")
RULE = re.compile(r"-+")
QUOTED = re.compile(r'"((?:[^"\\]|\\.)*)"')
END_OF_OUTPUT = "<end of output>"
# The trailer of a block: the end mark, the time, a rule, the result and the end time.
TRAILER_LINES = 5


@dataclass(frozen=True, slots=True)
class TestTime:
    """The wall time of one test of a ctest log."""

    name: str
    seconds: float


@dataclass(frozen=True, slots=True)
class Place:
    """The CMake file and the line that register one test."""

    path: str
    line: int


@dataclass(frozen=True, slots=True)
class Row:
    """One ledger row of the kind of the build."""

    line: int
    value: float
    reason: str


@dataclass(frozen=True, slots=True)
class RunLog:
    """The tests of one ctest log: the longest time of each test, and the tests of the launcher."""

    times: list[TestTime]
    launched: set[str]


def is_launched(command: str) -> bool:
    """Tell whether the Command line of a block names the test launcher as a program or an argument."""
    return any(Path(word).name == LAUNCHER for word in QUOTED.findall(command))


def trailer_seconds(lines: list[str], index: int, name: str) -> float | None:
    """Return the time of the trailer of the test `name` that starts at `index`, or None when no such trailer starts there."""
    if index + TRAILER_LINES > len(lines) or not lines[index].endswith(END_OF_OUTPUT):
        return None
    time_match = SECONDS.fullmatch(lines[index + 1])
    is_trailer = (time_match is not None and RULE.fullmatch(lines[index + 2]) is not None
                  and lines[index + 3].startswith("Test ") and lines[index + 4].startswith(f'"{name}" end time:'))
    return float(time_match["seconds"]) if is_trailer and time_match is not None else None


def read_log(path: Path) -> RunLog:
    """Read the longest wall time of each test of one ctest log, and the tests of the launcher.

    Complexity: O(n) in the lines of the log.

    Raises:
        ValueError: If the log cannot be read
    """
    try:
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    except OSError as exc:
        raise ValueError(f"the ctest log {path} cannot be read: {exc}") from None
    longest: dict[str, float] = {}
    launched: set[str] = set()
    name: str | None = None
    command = ""
    index = 0
    while index < len(lines):
        line = lines[index]
        if name is None:
            header = HEADER.fullmatch(line)
            if header is not None:
                name, command = header["name"], ""
        elif not command and line.startswith("Command: "):
            command = line
        else:
            seconds = trailer_seconds(lines, index, name)
            if seconds is not None:
                longest[name] = max(seconds, longest.get(name, 0.0))
                if is_launched(command):
                    launched.add(name)
                name = None
                index += TRAILER_LINES
        index += 1
    return RunLog([TestTime(test, seconds) for test, seconds in longest.items()], launched)


def log_of_parent(build: Path, parent: int) -> Path:
    """Return the log that the parent ctest holds open for the build directory: the log of the run that ends.

    Raises:
        ValueError: If the parent holds no log of the build directory open
    """
    temporary = (build / TEMPORARY).resolve()
    descriptors = Path(f"/proc/{parent}/fd")
    try:
        links = sorted(descriptors.iterdir())
    except OSError as exc:
        raise ValueError(f"the descriptors of the parent process {parent} cannot be read: {exc}") from None
    for link in links:
        try:
            target = Path(os.readlink(link))
        except OSError:
            continue
        if target.parent == temporary and target.name.startswith(LOG_PREFIX):
            return target
    raise ValueError(f"the parent process {parent} holds no log of {temporary} open.  --from-ctest is for the "
                     f"command CTEST_CUSTOM_POST_TEST of ctest, which cmake/TestLauncher.cmake writes into "
                     f"BUILD_DIR/CTestCustom.cmake")


def read_ledger(path: Path, kind: str) -> dict[str, Row]:
    """Read the rows of the ledger that name the kind of the build.

    Args:
        path: The ledger
        kind: The kind of the build

    Returns:
        Each row of the kind, by its test

    Raises:
        ValueError: If a row of the ledger is malformed
        OSError: If the ledger cannot be read
    """
    rows, problems, _ = cost_meter.read_ledger_rows(str(path))
    if problems:
        number, problem = problems[0]
        raise ValueError(f"{path}:{number}: {problem}")
    return {item: Row(*row) for (row_kind, item), row in rows.items() if row_kind == kind}


def read_places(build: Path, ctest: str) -> dict[str, Place]:
    """Read the place that registers each test of a build.

    The place is the outermost call in the CMake file of the directory: the
    call of the registration function, or add_test itself.

    Args:
        build: The build directory
        ctest: The ctest program

    Returns:
        The place of each test by its name

    Raises:
        ValueError: If ctest cannot list the tests of the build
    """
    proc = subprocess.run([ctest, "--test-dir", str(build), "--show-only=json-v1"], capture_output=True, text=True,
                          check=False)
    if proc.returncode != 0:
        raise ValueError(f"`ctest --show-only=json-v1` failed for {build}: {proc.stderr.strip()}")
    try:
        listing = json.loads(proc.stdout)
        graph = listing["backtraceGraph"]
        files, nodes = graph["files"], graph["nodes"]
        places: dict[str, Place] = {}
        for test in listing["tests"]:
            index = test.get("backtrace")
            line = 0
            path = ""
            while isinstance(index, int):
                node = nodes[index]
                if "line" in node:
                    path, line = files[node["file"]], node["line"]
                index = node.get("parent")
            places[test["name"]] = Place(display(Path(path)), line) if path else Place(display(build), 0)
        return places
    except (ValueError, KeyError, IndexError, TypeError) as exc:
        raise ValueError(f"the test list of {build} cannot be read: {exc}") from None


def display(path: Path) -> str:
    """Return a path relative to the repository root when it is under the root.

    Args:
        path: An absolute path

    Returns:
        The text of the path
    """
    try:
        return path.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(path)


def has_finding(times: list[TestTime], launched: set[str], budget: check_report.Budget) -> bool:
    """Tell whether a test that runs without the launcher is over the warning threshold."""
    return any(item.name not in launched and item.seconds > budget.warn for item in times)


def evaluate(times: list[TestTime], places: dict[str, Place], launched: set[str], ledger: dict[str, Row],
             budget: check_report.Budget, kind: str, ledger_name: str) -> list[check_report.Finding]:
    """Compare the time of each test that runs without the launcher with the budget and the ledger.

    Complexity: O(n) in the tests and the ledger rows.

    Args:
        times: The tests of the log
        places: The place of each test of the build.  It can be empty when no test has a finding and the ledger has
            no row of the kind
        launched: The tests that the launcher runs and judges
        ledger: The ledger rows of the kind of the build
        budget: The row test-time
        kind: The kind of the build
        ledger_name: The path of the ledger, for its findings

    Returns:
        The findings
    """
    findings: list[check_report.Finding] = []
    measured = {item.name: item.seconds for item in times if item.name not in launched}
    is_tsan = "-tsan" in kind
    for item in times:
        if item.name in launched:
            continue
        level = check_report.classify(item.seconds, budget)
        if level is None:
            continue
        place = places.get(item.name, Place(ledger_name, 0))
        limit = budget.error if level == "error" else budget.warn
        text = f"the test {item.name} took {item.seconds:.2f} s, more than the {level} threshold {limit:g} s"
        if level == "error" and item.name in ledger:
            findings.append(check_report.Finding("warning", place.path, place.line, CHECK,
                                                 f"{text}.  The row {ledger_name}:{ledger[item.name].line} admits "
                                                 f"it: {ledger[item.name].reason}"))
        elif level == "error" and is_tsan:
            findings.append(check_report.Finding("warning", place.path, place.line, CHECK,
                                                 f"{text}.  The build kind {kind} budgets no wall time, so the timeout "
                                                 f"of the test preset is the hard stop."))
        elif level == "error":
            findings.append(check_report.judged("error", place.path, place.line, CHECK,
                                                f"{text}.  Make the test faster, or split it into several tests "
                                                f"over disjoint cases."))
        else:
            findings.append(check_report.Finding("warning", place.path, place.line, CHECK, f"{text}."))
    for name, row in sorted(ledger.items()):
        if name not in places:
            findings.append(check_report.Finding("error", ledger_name, row.line, CHECK,
                                                 f"the ledger names the test {name}, which the build does not "
                                                 f"register.  Remove the row."))
        elif name in measured and measured[name] <= budget.error:
            findings.append(check_report.judged("error", ledger_name, row.line, CHECK,
                                                f"the test {name} took {measured[name]:.2f} s, no more than the "
                                                f"error threshold {budget.error:g} s.  Remove its row."))
    return findings


def write_ledger(path: Path, times: list[TestTime], launched: set[str], kind: str, budget: check_report.Budget) -> None:
    """Write the rows of the kind again for the tests that run without the launcher.

    A row of another kind and a row of a test that the launcher runs stay.  A
    test that keeps its row keeps its reason.

    Args:
        path: The ledger
        times: The tests of the log
        launched: The tests that the launcher runs
        kind: The kind of the build
        budget: The row test-time
    """
    rows, _, header = cost_meter.read_ledger_rows(str(path))
    kept = {key: value for key, value in rows.items() if key[0] != kind or key[1] in launched}
    for item in times:
        if item.name not in launched and item.seconds > budget.error:
            reason = rows.get((kind, item.name), (0, 0.0, "the wall time in one ctest run"))[2]
            kept[(kind, item.name)] = (0, item.seconds, reason)
    lines = [f"{key[0]} | {key[1]} | {value[1]:.1f} | {value[2]}\n" for key, value in sorted(kept.items())]
    path.write_text("\n".join(header).rstrip("\n") + "\n" + ("\n" + "".join(lines) if lines else ""),
                    encoding="utf-8")


def run(build: Path, log: Path | None, ctest: str, warnings_dir: Path | None, should_write: bool) -> int:
    """Run the check, or the write of the ledger, over the log of one ctest run.

    Args:
        build: The build directory
        log: The log, or None to read the log that the parent ctest holds open
        ctest: The ctest program, for the test list of the build
        warnings_dir: The warnings directory, or None
        should_write: Write the rows of the kind of the build again

    Returns:
        The exit code
    """
    budgets = Path(os.environ.get(BUDGETS_ENV) or check_report.BUDGETS)
    ledger_path = Path(os.environ.get(LEDGERS_ENV) or SCRIPTS) / LEDGER_NAME
    shown_log = display(log) if log is not None else display(build / TEMPORARY)
    try:
        budget = check_report.read_budgets(budgets)[CHECK]
        kind = cost_meter.read_kind(str(build))
        if kind is None:
            raise ValueError(f"{build}/{cost_meter.KIND_FILE} does not exist, so the kind of the build is not known")
        if log is None:
            log = log_of_parent(build, os.getppid())
            shown_log = display(log)
        run_log = read_log(log)
        ledger = read_ledger(ledger_path, kind)
        needs_places = bool(ledger) or has_finding(run_log.times, run_log.launched, budget)
        places = read_places(build, ctest) if needs_places and not should_write else {}
    except (ValueError, KeyError, OSError) as exc:
        problem = f"the row {CHECK} is missing from the budget table" if isinstance(exc, KeyError) else str(exc)
        return check_report.emit([check_report.Finding("error", shown_log, 0, CHECK,
                                                       f"the check cannot read its input: {problem}")],
                                 CHECK, warnings_dir)
    if should_write:
        write_ledger(ledger_path, run_log.times, run_log.launched, kind, budget)
        print(f"check-test-time: wrote {display(ledger_path)}.", file=sys.stderr)
        return 0
    findings = evaluate(run_log.times, places, run_log.launched, ledger, budget, kind, display(ledger_path))
    code = check_report.emit(findings, CHECK, warnings_dir)
    # A run with no test, such as `ctest --show-only=json-v1`, prints nothing.  ctest 4.3 runs this command after
    # that run too, and it writes the output of the command after the JSON on its standard output.
    if not run_log.times and not findings:
        return code
    judged = [item for item in run_log.times if item.name not in run_log.launched]
    slowest = max(judged, key=lambda item: item.seconds) if judged else None
    print(f"check-test-time: {len(judged)} tests without the launcher"
          + (f", the slowest {slowest.name} at {slowest.seconds:.2f} s" if slowest else "")
          + f", {len(findings)} finding(s).", file=sys.stderr)
    return code


# ── The self-test ──────────────────────────────────────────────────


def block(index: int, name: str, seconds: float, command: str = '"/usr/bin/python3" "check.py"', output: str = "",
          result: str = "Test Passed.") -> str:
    """Return the block that ctest writes to its log for one test."""
    return (f"{index}/9 Testing: {name}\n{index}/9 Test: {name}\nCommand: {command}\nDirectory: /b\n"
            f'"{name}" start time: Oct 02 19:26 CEST\nOutput:\n{"-" * 58}\n{output}{END_OF_OUTPUT}\n'
            f"Test time = {seconds:7.2f} sec\n{'-' * 58}\n{result}\n"
            f'"{name}" end time: Oct 02 19:26 CEST\n"{name}" time elapsed: 00:00:00\n{"-" * 58}\n\n')


# A planted script test that runs for the seconds of its argument, with a busy loop.
BUSY = "import sys, time\nend = time.monotonic() + float(sys.argv[1])\nwhile time.monotonic() < end:\n    pass\n"


def self_test(cmake: str, ctest: str) -> int:
    """Run the cases of the self-test with GITHUB_ACTIONS removed, except in the cases that set it.

    Returns:
        0 when every case holds, 2 otherwise
    """
    with check_report.github_actions(False):
        return self_test_cases(cmake, ctest)


def self_test_cases(cmake: str, ctest: str) -> int:
    """Plant each level, each form of the log and each failure mode in scratch files, and check each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    budget = check_report.Budget(CHECK, 5.0, 20.0, "s", "the wall time of one test")
    places = {name: Place("test/CMakeLists.txt", line) for line, name in enumerate(
        ("fast", "slow", "slower", "listed", "listed_fast", "listed_absent", "launched"), start=10)}
    kind = "x86_64-debug-asan"

    def judge(times: dict[str, float], ledger: dict[str, str], build_kind: str = kind) -> list[check_report.Finding]:
        rows = {name: Row(3, 25.0, reason) for name, reason in ledger.items()}
        return evaluate([TestTime(name, value) for name, value in times.items()], places, {"launched"}, rows, budget,
                        build_kind, "ledger")

    expect("no finding: every test at or below the warning threshold", judge({"fast": 1.0, "slow": 5.0}, {}) == [])
    found = judge({"slow": 7.0}, {})
    expect("a warning: a test above the warning threshold, at its place",
           len(found) == 1 and found[0].level == "warning" and (found[0].path, found[0].line) == (
               "test/CMakeLists.txt", 11))
    found = judge({"slower": 25.0}, {})
    expect("an error: a test above the error threshold with no row",
           len(found) == 1 and found[0].level == "error" and "split it" in found[0].message)
    found = judge({"slower": 25.0}, {}, "x86_64-debug-tsan")
    expect("a warning: a test above the error threshold in a build of a tsan kind",
           len(found) == 1 and found[0].level == "warning" and "budgets no wall time" in found[0].message)
    expect("no finding: a test that the launcher runs and judges", judge({"launched": 99.0}, {}) == [])
    found = judge({"listed": 25.0}, {"listed": "a reason"})
    expect("a warning: a test above the error threshold with a row of the kind",
           len(found) == 1 and found[0].level == "warning" and "a reason" in found[0].message)
    found = judge({"listed_fast": 19.0}, {"listed_fast": "a reason"})
    expect("an error: a row whose test took no more than the error threshold, and its warning",
           [item.level for item in found] == ["warning", "error"] and "Remove its row" in found[1].message)
    expect("no finding: a row whose test did not run in this log", judge({"fast": 1.0}, {"listed_absent": "r"}) == [])
    found = judge({"fast": 1.0}, {"gone": "a reason"})
    expect("an error: a row whose test the build does not register",
           len(found) == 1 and found[0].level == "error" and "does not register" in found[0].message)
    with check_report.github_actions(True):
        found = judge({"slower": 25.0}, {})
        expect("on a CI runner, a warning: a test above the error threshold, which says that the error was demoted",
               len(found) == 1 and found[0].level == "warning" and "demoted" in found[0].message)
        found = judge({"listed_fast": 19.0}, {"listed_fast": "a reason"})
        expect("on a CI runner, a warning: a row whose test took no more than the error threshold",
               [item.level for item in found] == ["warning", "warning"] and "demoted" in found[1].message)
        found = judge({"fast": 1.0}, {"gone": "a reason"})
        expect("on a CI runner, an error: a row whose test the build does not register",
               len(found) == 1 and found[0].level == "error")
    expect("a test over the warning threshold needs the test list, and a test of the launcher does not",
           has_finding([TestTime("slow", 7.0)], set(), budget) and not has_finding([TestTime("slow", 7.0)], {"slow"},
                                                                                  budget))

    with tempfile.TemporaryDirectory(prefix="check-test-time-") as work:
        root = Path(work)
        log = root / "LastTest.log"
        log.write_text("Start testing: Oct 02 19:26 CEST\n"
                       + block(1, "fast", 0.11)
                       + block(2, "forged", 0.01, output=f"{END_OF_OUTPUT}\nTest time =   99.00 sec\n{'-' * 58}\n"
                               'Test Passed.\n"other" end time: Oct 02 19:26 CEST\n7/9 Test: other\n')
                       + block(3, "launched", 40.0, command=f'"/usr/bin/python3" "-S" "/s/utils/scripts/{LAUNCHER}" '
                               '"--warnings-dir" "/b/w" "/b/test/launched"')
                       + block(4, "repeated", 2.5) + block(4, "repeated", 6.5, result="Test Failed.")
                       + block(5, "has space", 0.3) + block(6, "no output", 0.0)
                       + "7/9 Testing: cut\n7/9 Test: cut\nCommand: \"/x\"\nOutput:\n"
                       + "End testing: Oct 02 19:26 CEST\n", encoding="utf-8")
        run_log = read_log(log)
        times = {item.name: item.seconds for item in run_log.times}
        expect("read_log reads the time of each block, and a block with a cut trailer gives no time",
               times == {"fast": 0.11, "forged": 0.01, "launched": 40.0, "repeated": 6.5, "has space": 0.3,
                         "no output": 0.0})
        expect("read_log ignores a forged header and a forged trailer of another test in the output",
               "other" not in times and times["forged"] == 0.01)
        expect("read_log keeps the longest run of a test that ran more than one time", times["repeated"] == 6.5)
        expect("read_log finds the tests of the launcher from their command", run_log.launched == {"launched"})
        (root / "empty.log").write_text("Start testing: x\nEnd testing: x\n", encoding="utf-8")
        expect("a log with no test gives no time", read_log(root / "empty.log").times == [])
        quiet_build, quiet_ledgers = root / "quiet", root / "quiet-ledgers"
        quiet_build.mkdir()
        quiet_ledgers.mkdir()
        (quiet_build / cost_meter.KIND_FILE).write_text(f"{kind}\n", encoding="utf-8")
        (quiet_ledgers / LEDGER_NAME).write_text("# kind | test | value | reason\n", encoding="utf-8")
        earlier_ledgers = os.environ.get(LEDGERS_ENV)
        os.environ[LEDGERS_ENV] = str(quiet_ledgers)
        try:
            with contextlib.redirect_stdout(io.StringIO()) as printed, \
                    contextlib.redirect_stderr(io.StringIO()) as complained:
                status = run(quiet_build, root / "empty.log", ctest, None, False)
        finally:
            if earlier_ledgers is None:
                os.environ.pop(LEDGERS_ENV, None)
            else:
                os.environ[LEDGERS_ENV] = earlier_ledgers
        expect("a run with no test, such as `ctest --show-only=json-v1` under ctest 4.3, prints nothing",
               status == 0 and printed.getvalue() == "" and complained.getvalue() == "")
        try:
            read_log(root / "missing.log")
            expect("read_log refuses a missing log", False)
        except ValueError:
            expect("read_log refuses a missing log", True)
        try:
            log_of_parent(root, os.getpid())
            expect("log_of_parent refuses a process that holds no log of the build directory", False)
        except ValueError as exc:
            expect("log_of_parent refuses a process that holds no log of the build directory",
                   "holds no log" in str(exc))
        temporary = root / TEMPORARY
        temporary.mkdir(parents=True)
        with open(temporary / f"{LOG_PREFIX}.tmp1f2e3", "w", encoding="utf-8"):
            expect("log_of_parent finds the log that a process holds open",
                   log_of_parent(root, os.getpid()) == (temporary / f"{LOG_PREFIX}.tmp1f2e3").resolve())

        ledger = root / "ledger.txt"
        for label, body in (("three cells", "k | a | 1\n"), ("an empty reason", "k | a | 1 | \n"),
                            ("a value that is not a number", "k | a | x | r\n")):
            ledger.write_text(body, encoding="utf-8")
            try:
                read_ledger(ledger, "k")
                expect(f"read_ledger refuses {label}", False)
            except ValueError:
                expect(f"read_ledger refuses {label}", True)
        ledger.write_text("# a header\n\nother-kind | b | 40.0 | another kind\nx86_64-debug-asan | launched | 40.0 | "
                          "the launcher\nx86_64-debug-asan | c | 21.0 | kept\n", encoding="utf-8")
        write_ledger(ledger, [TestTime("b", 30.0), TestTime("a", 1.0), TestTime("c", 22.0)], {"launched"}, kind,
                     budget)
        rows = read_ledger(ledger, kind)
        expect("--write keeps a reason, adds a row above the error threshold, and keeps a row of a launched test",
               set(rows) == {"b", "c", "launched"} and rows["c"].reason == "kept")
        expect("--write keeps a row of another kind", set(read_ledger(ledger, "other-kind")) == {"b"})
        expect("the repository budget table has the row test-time", CHECK in check_report.read_budgets())
        warnings_dir = root / "warnings"
        with contextlib.redirect_stdout(io.StringIO()) as printed:
            status = check_report.emit(judge({"slow": 7.0}, {}), CHECK, warnings_dir)
        expect("warnings only give exit status 0, a line of the format and a warnings file",
               status == 0 and (warnings_dir / f"{CHECK}.txt").is_file()
               and check_report.parse_line(printed.getvalue().splitlines()[0]) is not None)
        with contextlib.redirect_stdout(io.StringIO()):
            status = check_report.emit(judge({"slower": 25.0}, {}), CHECK, warnings_dir)
        expect("an error gives exit status 1 and removes the warnings file",
               status == 1 and not (warnings_dir / f"{CHECK}.txt").exists())

        through_ctest(root / "ctest", cmake, ctest, expect)
    if failures:
        print(f"check-test-time --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-test-time --self-test: every case holds.")
    return 0


def through_ctest(work: Path, cmake: str, ctest: str, expect: Callable[[str, bool], None]) -> None:
    """Run real ctest runs of a scratch project that includes cmake/TestLauncher.cmake, and check each verdict.

    The planted budget tables make each verdict independent of the speed of the host: a table of 0 and 0 makes
    each test an error, 0 and 1000 a warning, and 1000 and 1000 no finding.
    """
    source, build, ledgers = work / "source", work / "build", work / "ledgers"
    for directory in (source, ledgers):
        directory.mkdir(parents=True)
    (source / "busy.py").write_text(BUSY, encoding="utf-8")
    (source / LAUNCHER).write_text(BUSY, encoding="utf-8")
    python = sys.executable
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.29)\nproject(planted NONE)\nenable_testing()\n"
        f'set(CRUCIBLE_PYTHON3 "{python}")\ninclude("{SCRIPTS.parents[1] / "cmake" / "TestLauncher.cmake"}")\n'
        f'add_test(NAME planted_script COMMAND "{python}" "{source / "busy.py"}" 0.05)\n'
        f'add_test(NAME planted_wrapped COMMAND "${{CMAKE_COMMAND}}" -E env A=1 "{python}" "{source / "busy.py"}" 0.05)\n'
        f'add_test(NAME planted_launched COMMAND "{python}" "{source / LAUNCHER}" 0.05)\n', encoding="utf-8")
    configured = subprocess.run([cmake, "-S", str(source), "-B", str(build)], capture_output=True, text=True,
                                check=False)
    expect("a scratch project that includes cmake/TestLauncher.cmake configures",
           configured.returncode == 0 and (build / "CTestCustom.cmake").is_file())
    if configured.returncode != 0:
        print(configured.stdout + configured.stderr)
        return
    (build / cost_meter.KIND_FILE).write_text("x86_64-debug-asan\n", encoding="utf-8")
    table = work / "budgets.txt"
    warnings_file = build / "check-warnings" / f"{CHECK}.txt"

    def ctest_run(thresholds: str, *arguments: str, **extra: str) -> tuple[int, str]:
        table.write_text(f"test-time | {thresholds} | s | the wall time\n", encoding="utf-8")
        environment = dict(os.environ, **{BUDGETS_ENV: str(table), LEDGERS_ENV: str(ledgers)})
        environment.pop(cost_meter.GITHUB_ACTIONS_ENV, None)
        environment.update(extra)
        proc = subprocess.run([ctest, "--test-dir", str(build), "-j3", *arguments], capture_output=True, text=True,
                              env=environment, check=False)
        return proc.returncode, proc.stdout + proc.stderr

    (ledgers / LEDGER_NAME).write_text("# kind | test | value | reason\n", encoding="utf-8")
    status, said = ctest_run("0 | 0")
    expect("ctest fails after its tests when a script test is over the error threshold, and names the test and "
           "its place", status != 0 and "error: [test-time] the test planted_script took" in said
           and "error: [test-time] the test planted_wrapped took" in said and "CMakeLists.txt:" in said)
    expect("the check skips a test whose command names the test launcher",
           "planted_launched took" not in said and "2 tests without the launcher" in said)
    status, said = ctest_run("0 | 1000")
    expect("ctest passes with a warning for a script test over the warning threshold, and writes the warnings "
           "file", status == 0 and "warning: [test-time] the test planted_script took" in said
           and "planted_script" in (warnings_file.read_text(encoding="utf-8") if warnings_file.is_file() else ""))
    status, said = ctest_run("1000 | 1000", "-R", "planted_script")
    expect("a run of a part of the suite under the thresholds gives no finding and removes the warnings file",
           status == 0 and "[test-time]" not in said and "1 tests without the launcher" in said
           and not warnings_file.exists())
    (ledgers / LEDGER_NAME).write_text("# kind | test | value | reason\n"
                                       "x86_64-debug-asan | planted_script | 1.0 | a planted reason\n"
                                       "x86_64-debug-asan | planted_wrapped | 1.0 | a planted reason\n",
                                       encoding="utf-8")
    status, said = ctest_run("0 | 0")
    expect("a ledger row of the kind admits a script test over the error threshold, with a warning",
           status == 0 and said.count("a planted reason") == 2 and "error:" not in said)
    (ledgers / LEDGER_NAME).write_text("# kind | test | value | reason\n", encoding="utf-8")
    status, said = ctest_run("0 | 0", GITHUB_ACTIONS="true")
    expect("on a CI runner, ctest passes with a demoted warning for a script test over the error threshold",
           status == 0 and "warning: [test-time] the test planted_script took" in said and "demoted" in said)
    status, said = ctest_run("0 | 0", "--show-only")
    expect("ctest --show-only runs no check", status == 0 and "[test-time]" not in said)


def main(argv: list[str]) -> int:
    """Run the check, the write of the ledger, or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    parser = argparse.ArgumentParser(prog="check-test-time.py", description=__doc__.split("\n", 1)[0])
    check_report.add_arguments(parser)
    parser.add_argument("--build-dir", type=Path, help="the build directory of the ctest run")
    parser.add_argument("--from-ctest", action="store_true",
                        help="read the log that the parent ctest holds open: the check is the command of ctest")
    parser.add_argument("--log", type=Path, default=DEFAULT_LOG,
                        help=f"the ctest log, relative to the build directory (default {DEFAULT_LOG})")
    parser.add_argument("--ctest", default=shutil.which("ctest") or "ctest", help="the ctest program")
    parser.add_argument("--cmake", default=shutil.which("cmake") or "cmake",
                        help="the cmake program of the self-test")
    parser.add_argument("--write", action="store_true", help="write the rows of the kind of the build again")
    parser.add_argument("--self-test", action="store_true", help="plant each level and each failure mode")
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return self_test(arguments.cmake, arguments.ctest)
    if arguments.build_dir is None:
        parser.error("give --build-dir BUILD_DIR, or --self-test")
    if arguments.from_ctest and arguments.write:
        parser.error("--write reads a finished log: give --log, not --from-ctest")
    build = arguments.build_dir.resolve()
    log = None if arguments.from_ctest else (arguments.log if arguments.log.is_absolute() else build / arguments.log)
    return run(build, log, arguments.ctest, arguments.warnings_dir, arguments.write)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
