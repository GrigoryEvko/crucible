#!/usr/bin/env python3
"""check-test-time — no test that runs without the test launcher takes more than the budget of the row test-time.

utils/scripts/test-launcher.py judges each executable test while it runs.
CMake gives that launcher only to a test whose command is an executable
target, so a guard, a negative fixture and another script test run with no
launcher.  A ctest test cannot measure the run that it is part of, so this
check reads the wall time of those tests from the JUnit report of the run:

    ctest --test-dir BUILD_DIR -j16 --output-junit ctest-junit.xml
    python3 utils/scripts/check-test-time.py --build-dir BUILD_DIR

ctest writes a relative --output-junit path inside the build directory, and
--junit takes the same relative path (the preset value is ctest-junit.xml).

WHAT THE CHECK READS
    * The JUnit report: the name and the wall time of each test case.
    * The row test-time of utils/scripts/budgets.txt: the warning threshold
      and the error threshold, in seconds.
    * The test list of the build (ctest --show-only=json-v1): the CMake file
      and the line that register each test, for the place of a finding, and
      the tests that the launcher runs, which the check skips.
    * The kind of the build (BUILD_DIR/build-kind.txt).
    * utils/scripts/test-time-ledger.txt, `kind | test | value | reason`: the
      tests that take more than the error threshold at this time.

LEVELS
    * A test above the warning threshold gives a warning.
    * A test above the error threshold gives an error, unless a ledger row of
      the kind names it.  A test with a row gives a warning, so the debt stays
      visible.  In a build of a kind with tsan, a test above the error
      threshold gives a warning, as in the launcher.
    * A row of the kind whose test ran and took no more than the error
      threshold is an error: remove the row.  A row of the kind whose test
      the build does not register is an error too.  A row whose test did not
      run in this report gives no finding, because a run of a part of the
      suite is permitted.

    A time is the wall time of one test while the other tests of the run run
    at the same time, so the error threshold sits above the noise of the
    shared host.  The commit that sets the row states the measured noise.
    On a GitHub runner, each error that judges a time is a warning that says
    that it was demoted (utils/scripts/cost_meter.py, A CI RUNNER).  A row of
    a test that the build does not register and an input that the check
    cannot read stay errors.

Usage
    check-test-time.py --build-dir BUILD_DIR [--junit FILE] [--warnings-dir DIR]
    check-test-time.py --build-dir BUILD_DIR [--junit FILE] --write
    check-test-time.py --self-test

Exit 0 with no error, 1 on an error or an input that the check cannot read, 2 on a
usage error or a failed self-test.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import math
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
import cost_meter  # noqa: E402

CHECK = "test-time"
ROOT = Path(__file__).resolve().parents[2]
LEDGER = Path(__file__).resolve().parent / "test-time-ledger.txt"
DEFAULT_JUNIT = "ctest-junit.xml"
LAUNCHER = "test-launcher.py"


@dataclass(frozen=True, slots=True)
class TestTime:
    """The wall time of one test case of a JUnit report."""

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


def read_junit(path: Path) -> list[TestTime]:
    """Read the name and the wall time of each test case of a JUnit report.

    Complexity: O(n) in the size of the report.

    Args:
        path: The report that ctest --output-junit wrote

    Returns:
        The test cases in report order

    Raises:
        ValueError: If the report cannot be read, is not XML, has no test case, or has a test case
            with no name or with a time that is not a finite number
    """
    try:
        tree = ElementTree.parse(path)
    except (OSError, ElementTree.ParseError) as exc:
        raise ValueError(f"the JUnit report {path} cannot be read: {exc}") from None
    times: list[TestTime] = []
    for case in tree.getroot().iter("testcase"):
        name = case.get("name", "")
        text = case.get("time", "")
        try:
            seconds = float(text)
        except ValueError:
            raise ValueError(f"the test case {name!r} of {path} has the time {text!r}, which is not a number") from None
        if not name or not math.isfinite(seconds) or seconds < 0:
            raise ValueError(f"a test case of {path} has no name or a time that is not a finite number")
        times.append(TestTime(name, seconds))
    if not times:
        raise ValueError(f"the JUnit report {path} holds no test case")
    return times


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


def read_places(build: Path) -> tuple[dict[str, Place], set[str]]:
    """Read the place that registers each test of a build, and the tests that the launcher runs.

    The place is the outermost call in the CMake file of the directory: the
    call of the registration function, or add_test itself.

    Args:
        build: The build directory

    Returns:
        The place of each test by its name, and the names of the tests whose command is the launcher

    Raises:
        ValueError: If ctest cannot list the tests of the build
    """
    proc = subprocess.run(["ctest", "--test-dir", str(build), "--show-only=json-v1"], capture_output=True, text=True,
                          check=False)
    if proc.returncode != 0:
        raise ValueError(f"`ctest --show-only=json-v1` failed for {build}: {proc.stderr.strip()}")
    try:
        listing = json.loads(proc.stdout)
        graph = listing["backtraceGraph"]
        files, nodes = graph["files"], graph["nodes"]
        places: dict[str, Place] = {}
        launched: set[str] = set()
        for test in listing["tests"]:
            if any(Path(part).name == LAUNCHER for part in test.get("command") or []):
                launched.add(test["name"])
            index = test.get("backtrace")
            line = 0
            path = ""
            while isinstance(index, int):
                node = nodes[index]
                if "line" in node:
                    path, line = files[node["file"]], node["line"]
                index = node.get("parent")
            places[test["name"]] = Place(display(Path(path)), line) if path else Place(display(build), 0)
        return places, launched
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


def evaluate(times: list[TestTime], places: dict[str, Place], launched: set[str], ledger: dict[str, Row],
             budget: check_report.Budget, kind: str, ledger_name: str) -> list[check_report.Finding]:
    """Compare the time of each test that runs without the launcher with the budget and the ledger.

    Complexity: O(n) in the test cases and the ledger rows.

    Args:
        times: The test cases of the report
        places: The place of each test of the build
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
        times: The test cases of the report
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


def self_test() -> int:
    """Run the cases of the self-test with GITHUB_ACTIONS removed, except in the cases that set it.

    Returns:
        0 when every case holds, 2 otherwise
    """
    with check_report.github_actions(False):
        return self_test_cases()


def self_test_cases() -> int:
    """Plant each level and each failure mode in scratch files, and check each verdict.

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

    def run(times: dict[str, float], ledger: dict[str, str], build_kind: str = kind) -> list[check_report.Finding]:
        rows = {name: Row(3, 25.0, reason) for name, reason in ledger.items()}
        return evaluate([TestTime(name, value) for name, value in times.items()], places, {"launched"}, rows, budget,
                        build_kind, "ledger")

    expect("no finding: every test at or below the warning threshold", run({"fast": 1.0, "slow": 5.0}, {}) == [])
    found = run({"slow": 7.0}, {})
    expect("a warning: a test above the warning threshold, at its place",
           len(found) == 1 and found[0].level == "warning" and (found[0].path, found[0].line) == (
               "test/CMakeLists.txt", 11))
    found = run({"slower": 25.0}, {})
    expect("an error: a test above the error threshold with no row",
           len(found) == 1 and found[0].level == "error" and "split it" in found[0].message)
    found = run({"slower": 25.0}, {}, "x86_64-debug-tsan")
    expect("a warning: a test above the error threshold in a build of a tsan kind",
           len(found) == 1 and found[0].level == "warning" and "budgets no wall time" in found[0].message)
    expect("no finding: a test that the launcher runs and judges", run({"launched": 99.0}, {}) == [])
    found = run({"listed": 25.0}, {"listed": "a reason"})
    expect("a warning: a test above the error threshold with a row of the kind",
           len(found) == 1 and found[0].level == "warning" and "a reason" in found[0].message)
    found = run({"listed_fast": 19.0}, {"listed_fast": "a reason"})
    expect("an error: a row whose test took no more than the error threshold, and its warning",
           [item.level for item in found] == ["warning", "error"] and "Remove its row" in found[1].message)
    expect("no finding: a row whose test did not run in this report", run({"fast": 1.0}, {"listed_absent": "r"})
           == [])
    found = run({"fast": 1.0}, {"gone": "a reason"})
    expect("an error: a row whose test the build does not register",
           len(found) == 1 and found[0].level == "error" and "does not register" in found[0].message)
    with check_report.github_actions(True):
        found = run({"slower": 25.0}, {})
        expect("on a CI runner, a warning: a test above the error threshold, which says that the error was demoted",
               len(found) == 1 and found[0].level == "warning" and "demoted" in found[0].message)
        found = run({"listed_fast": 19.0}, {"listed_fast": "a reason"})
        expect("on a CI runner, a warning: a row whose test took no more than the error threshold",
               [item.level for item in found] == ["warning", "warning"] and "demoted" in found[1].message)
        found = run({"fast": 1.0}, {"gone": "a reason"})
        expect("on a CI runner, an error: a row whose test the build does not register",
               len(found) == 1 and found[0].level == "error")

    with tempfile.TemporaryDirectory(prefix="check-test-time-") as work:
        root = Path(work)
        report = root / "junit.xml"
        report.write_text('<testsuite><testcase name="a" time="1.5" status="run"/>'
                          '<testcase name="b" time="30" status="fail"/></testsuite>', encoding="utf-8")
        expect("a report reads", read_junit(report) == [TestTime("a", 1.5), TestTime("b", 30.0)])
        for label, body in (("not XML", "<testsuite"), ("no test case", "<testsuite></testsuite>"),
                            ("a time that is not a number", '<testsuite><testcase name="a" time="x"/></testsuite>'),
                            ("a negative time", '<testsuite><testcase name="a" time="-1"/></testsuite>')):
            report.write_text(body, encoding="utf-8")
            try:
                read_junit(report)
                expect(f"read_junit refuses {label}", False)
            except ValueError:
                expect(f"read_junit refuses {label}", True)
        try:
            read_junit(root / "missing.xml")
            expect("read_junit refuses a missing report", False)
        except ValueError:
            expect("read_junit refuses a missing report", True)
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
            status = check_report.emit(run({"slow": 7.0}, {}), CHECK, warnings_dir)
        expect("warnings only give exit status 0, a line of the format and a warnings file",
               status == 0 and (warnings_dir / f"{CHECK}.txt").is_file()
               and check_report.parse_line(printed.getvalue().splitlines()[0]) is not None)
        with contextlib.redirect_stdout(io.StringIO()):
            status = check_report.emit(run({"slower": 25.0}, {}), CHECK, warnings_dir)
        expect("an error gives exit status 1 and removes the warnings file",
               status == 1 and not (warnings_dir / f"{CHECK}.txt").exists())
    if failures:
        print(f"check-test-time --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-test-time --self-test: every case holds.")
    return 0


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
    parser.add_argument("--junit", type=Path, default=Path(DEFAULT_JUNIT),
                        help=f"the JUnit report, relative to the build directory (default {DEFAULT_JUNIT})")
    parser.add_argument("--write", action="store_true", help="write the rows of the kind of the build again")
    parser.add_argument("--self-test", action="store_true", help="plant each level and each failure mode")
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return self_test()
    if arguments.build_dir is None:
        parser.error("give --build-dir BUILD_DIR, or --self-test")
    build = arguments.build_dir.resolve()
    report = arguments.junit if arguments.junit.is_absolute() else build / arguments.junit
    try:
        budget = check_report.read_budgets()[CHECK]
        kind = cost_meter.read_kind(str(build))
        if kind is None:
            raise ValueError(f"{build}/{cost_meter.KIND_FILE} does not exist, so the kind of the build is not known")
        times = read_junit(report)
        places, launched = read_places(build)
        ledger = read_ledger(LEDGER, kind)
    except (ValueError, KeyError, OSError) as exc:
        problem = f"the row {CHECK} is missing from the budget table" if isinstance(exc, KeyError) else str(exc)
        return check_report.emit([check_report.Finding("error", display(report), 0, CHECK,
                                                       f"the check cannot read its input: {problem}")],
                                 CHECK, arguments.warnings_dir)
    if arguments.write:
        write_ledger(LEDGER, times, launched, kind, budget)
        print(f"check-test-time: wrote {display(LEDGER)}.", file=sys.stderr)
        return 0
    findings = evaluate(times, places, launched, ledger, budget, kind, display(LEDGER))
    code = check_report.emit(findings, CHECK, arguments.warnings_dir)
    judged = [item for item in times if item.name not in launched]
    slowest = max(judged, key=lambda item: item.seconds) if judged else None
    print(f"check-test-time: {len(judged)} tests without the launcher"
          + (f", the slowest {slowest.name} at {slowest.seconds:.2f} s" if slowest else "")
          + f", {len(findings)} finding(s).", file=sys.stderr)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
