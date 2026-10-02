#!/usr/bin/env python3
"""Print the warnings that the compile-time checks of one build directory wrote, and the time totals of the build.

A check that passes with warnings writes them to <build>/check-warnings/CHECK.txt
(utils/scripts/check_report.py).  ctest shows no output of a test that passes,
so this script prints those warnings after a build or a test run.  In a GitHub
workflow it also prints each one as an annotation.

THE TIME TOTALS
    On 192 cores the wall time of a build is at least its total CPU time
    divided by 192, and many small steps that each pass every per-job budget
    can make the total grow.  The script adds three totals and compares each
    one with its row of utils/scripts/budgets.txt:

      total-compile-cpu   the CPU time of the last real compile of each object
                          of the target all: the cost block of the record of
                          the build launcher, or the last cost that the record
                          of a ccache hit keeps (utils/scripts/build_census.py)
      total-fixture-cpu   the user and system CPU time of the last real compile
                          of each negative fixture, from its record
      total-test-cpu      the CPU time of each executable test in the record
                          EXECUTABLE.test.cost of the test launcher.  A script
                          test has no launcher, so it has no part in the total

    A total over its threshold gives a warning, never an error, because the
    CPU time rises with the load of the shared host.  A unit with no time is
    counted in the message and has no part in the total.  When the census does
    not apply to the build (only some targets built, no fixture ran), the
    script tells why and gives no total.

    python3 utils/scripts/report-check-warnings.py BUILD_DIR
    python3 utils/scripts/report-check-warnings.py --self-test

The exit status is 0 when every file has the format, also with warnings.  It is
1 when a line of a warnings file does not have the format, and 2 for a usage
error or a failed self-test.
"""

from __future__ import annotations

import contextlib
import io
import json
import sys
import tempfile
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_census  # noqa: E402
import check_report  # noqa: E402
import cost_meter  # noqa: E402
from repo_root import REPO_ROOT  # noqa: E402

TEST_RECORD_SUFFIX = ".test" + cost_meter.RECORD_SUFFIX
# Each time total, and the words that tell what it adds.
TOTALS = (("total-compile-cpu", "object", "the last real compile of each object of all"),
          ("total-fixture-cpu", "fixture", "the last real compile of each negative fixture"),
          ("total-test-cpu", "test", "each executable test in its last run"))


def read_test_times(build_dir: Path) -> list[float | None]:
    """Return the CPU time of the record of each executable test of a build directory, or None for a record with no time.

    Complexity: linear in the files of the build directory.
    """
    times: list[float | None] = []
    for path in build_dir.rglob(f"*{TEST_RECORD_SUFFIX}"):
        try:
            record = json.loads(path.read_text(encoding="utf-8"))
            cpu = record["cost"]["cpu_s"]
            times.append(float(cpu) if isinstance(cpu, (int, float)) and not isinstance(cpu, bool) else None)
        except (OSError, ValueError, KeyError, TypeError):
            times.append(None)
    return times


def time_totals(census: build_census.Census, test_times: list[float | None], budgets: dict[str, check_report.Budget],
                place: str) -> tuple[list[check_report.Finding], list[str]]:
    """Add the three time totals and compare each one with its budget row.

    Returns:
        A warning for each total over its threshold, and one summary line for each total
    """
    times = {
        "object": [unit.cost.cpu_s for unit in census.units if unit.kind == "object"],
        "fixture": [unit.cost.cpu_s for unit in census.units if unit.kind == "fixture"],
        "test": test_times,
    }
    findings: list[check_report.Finding] = []
    lines: list[str] = []
    for row, kind, meaning in TOTALS:
        known = [value for value in times[kind] if value is not None]
        missing = len(times[kind]) - len(known)
        total = sum(known)
        budget = budgets.get(row)
        threshold = f", warning threshold {budget.warn:g} s" if budget is not None else ", no budget row"
        unknown = f", {missing} with no time" if missing else ""
        lines.append(f"report-check-warnings: {row} {total:.0f} s CPU in {len(known)} units{unknown}{threshold}")
        if budget is not None and total > budget.warn:
            findings.append(check_report.Finding(
                "warning", place, 0, row,
                f"{meaning} took {total:.0f} s of CPU time together ({len(known)} units{unknown}), over the threshold "
                f"{budget.warn:g} s of the row {row}.  On 192 cores the wall time is at least the total divided by "
                f"192.  A time total gives a warning only, because the CPU time rises with the load of the host"))
    return findings, lines


def report_totals(build_dir: Path) -> list[check_report.Finding]:
    """Print the time totals of a build directory, and return the warnings of the totals."""
    try:
        budgets = check_report.read_budgets()
        census = build_census.read_census(build_dir, REPO_ROOT, read_files=False)
    except (build_census.NotApplicable, build_census.CensusError, OSError, ValueError) as reason:
        print(f"report-check-warnings: no time totals: {reason}")
        return []
    findings, lines = time_totals(census, read_test_times(build_dir), budgets, build_census.shown(build_dir, REPO_ROOT))
    for line in lines:
        print(line)
    return findings


def main(argv: list[str]) -> int:
    """Print the warnings and the time totals of one build directory.

    Args:
        argv: The arguments: one build directory, or --self-test

    Returns:
        0 when every warnings file has the format, 1 when a line does not, 2 for a usage error
    """
    if argv == ["--self-test"]:
        return self_test()
    if len(argv) != 1:
        print("usage: report-check-warnings.py BUILD_DIR | --self-test", file=sys.stderr)
        return 2
    build_dir = Path(argv[0]).resolve()
    warnings_dir = build_dir / check_report.WARNINGS_SUBDIR
    findings, problems = check_report.read_warnings_dir(warnings_dir)
    findings += report_totals(build_dir)
    annotate = cost_meter.is_github_actions()
    for found in findings:
        print(found.text())
        if annotate:
            print(found.annotation())
    for problem in problems:
        print(problem, file=sys.stderr)
    counts = Counter(found.check for found in findings)
    summary = ", ".join(f"{check} {count}" for check, count in sorted(counts.items()))
    print(f"report-check-warnings: {len(findings)} warning(s) in {warnings_dir} and the time totals"
          + (f": {summary}" if summary else ""))
    return 1 if problems else 0


def self_test() -> int:
    """Do a test of the time totals and of the warnings files, with planted input.

    Returns:
        0 when every case holds, else 2
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    def unit(kind: str, cpu: float | None) -> build_census.Unit:
        return build_census.Unit(kind, f"{kind}-{cpu}", "src/x.cpp", build_census.Cost(cpu, None))

    budgets = {row: check_report.Budget(row, limit, limit, "s", "m")
               for row, limit in (("total-compile-cpu", 100.0), ("total-fixture-cpu", 50.0), ("total-test-cpu", 10.0))}
    census = build_census.Census(Path("/b"), [unit("object", 60.0), unit("object", 30.0), unit("fixture", 20.0),
                                              unit("fixture", None)], {})
    found, lines = time_totals(census, [4.0, 5.0], budgets, "build")
    expect("totals under their thresholds give no warning, and one line each",
           not found and len(lines) == 3 and "total-compile-cpu 90 s CPU in 2 units" in lines[0]
           and "total-fixture-cpu 20 s CPU in 1 units, 1 with no time" in lines[1])
    census.units.append(unit("object", 20.0))
    found, _ = time_totals(census, [4.0, 5.0, 3.0, None], budgets, "build")
    expect("a total over its threshold gives a warning, never an error, that counts the units with no time",
           [(f.check, f.level) for f in found] == [("total-compile-cpu", "warning"), ("total-test-cpu", "warning")]
           and "110 s of CPU time together (3 units)" in found[0].message
           and "(3 units, 1 with no time)" in found[1].message)
    found, lines = time_totals(census, [], {}, "build")
    expect("a total with no budget row gives no warning and says so", not found and "no budget row" in lines[0])
    expect("the repository table has a row for each total",
           all(row in check_report.read_budgets() for row, _, _ in TOTALS))

    with tempfile.TemporaryDirectory(prefix="report-warnings-") as scratch:
        build = Path(scratch)
        warnings_dir = build / check_report.WARNINGS_SUBDIR
        warnings_dir.mkdir()
        (warnings_dir / "alpha.txt").write_text(check_report.Finding("warning", "a.h", 1, "alpha", "m").text() + "\n",
                                                encoding="utf-8")
        record = build / "test" / f"test_planted{TEST_RECORD_SUFFIX}"
        record.parent.mkdir()
        record.write_text(json.dumps({"format": 1, "step": "test", "cost": {"cpu_s": 2.5}}), encoding="utf-8")
        expect("a test record gives its CPU time", read_test_times(build) == [2.5])
        with contextlib.redirect_stdout(io.StringIO()) as output, contextlib.redirect_stderr(io.StringIO()):
            status = main([str(build)])
        expect("a build with no census prints the warnings and tells why it has no totals",
               status == 0 and "a.h:1: warning: [alpha] m" in output.getvalue()
               and "no time totals" in output.getvalue())
        (warnings_dir / "alpha.txt").write_text("not a finding\n", encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            status = main([str(build)])
        expect("a malformed warnings file gives status 1", status == 1)
    if failures:
        print(f"report-check-warnings --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("report-check-warnings --self-test: every case holds.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
