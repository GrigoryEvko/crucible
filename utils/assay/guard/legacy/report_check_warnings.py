"""The time totals and the build totals of one build directory, which `guard warnings` prints after the warnings.

THE TIME TOTALS
    On 192 cores the wall time of a build is at least its total CPU time
    divided by 192, and many small steps that each pass every per-job
    threshold can increase the total.  The module adds three totals and
    compares each one with its measured rule of utils/registry.txt:

      total-compile-cpu   the CPU time of the last real compile of each object
                          of the target all: the cost block of the record of
                          the build launcher, or the last cost that the record
                          of a ccache hit keeps (utils/assay/readers/census.py)
      total-fixture-cpu   the user and system CPU time of the last real compile
                          of each negative fixture, from its record
      total-test-cpu      the CPU time of each executable test, from its record
                          (assay.readers.ctest): a record with a program in
                          the build directory.  A script test and a negative
                          fixture have a record too, and no part in the total

    A total over its threshold gives a warning, never an error, because the
    CPU time rises with the load of the shared host.  A unit with no time is
    counted in the message and has no part in the total.  When the census does
    not apply to the build (only some targets built, no fixture ran), the
    module tells why and gives no total.

THE BUILD TOTALS
    Four more values come from each record of a compile or a link of the
    build launcher in the build directory, and from the configure, also when
    the census does not apply.  Each one gives a warning only:

      link-count          the number of link records
      total-link-cpu      the CPU time of the link records, added
      build-peak-memory   the largest sum of the peak memory of the steps
                          whose wall times overlap (utils/assay/loop/history.py,
                          THE COST RECORDS)
      configure-time      the last configure, from configure-time.txt and
                          build.ninja (utils/assay/loop/history.py, THE
                          CONFIGURE TIME)

    python3 utils/bin/guard selftest guard.legacy.report_check_warnings
"""

from __future__ import annotations

import json
import os
import sys
import tempfile
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import report as check_report
from assay.core.paths import REPO_ROOT
from assay.loop import history as loop_history
from assay.readers import census as build_census
from assay.readers import ctest as ctest_reader

# Each time total, and the words that tell what it adds.
TOTALS = (("total-compile-cpu", "object", "the last real compile of each object of all"),
          ("total-fixture-cpu", "fixture", "the last real compile of each negative fixture"),
          ("total-test-cpu", "test", "each executable test in its last run"))
NO_RULE = ", no measured rule"


def read_test_times(build_dir: Path) -> list[float | None]:
    """Return the CPU time of the record of each executable test of a build directory.

    An executable test has a program in the build directory.  Complexity: linear in the records of the build directory.

    Raises:
        ctest_reader.CtestError: If a record cannot be read
    """
    build = build_dir.resolve()
    return [record.cpu_s for record in ctest_reader.costs(build_dir).values()
            if os.path.isabs(record.program) and Path(record.program).resolve().is_relative_to(build)]


def time_totals(census: build_census.Census, test_times: list[float | None], budgets: dict[str, check_report.Budget],
                place: str) -> tuple[list[check_report.Finding], list[str]]:
    """Add the three time totals and compare each one with its measured rule.

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
        threshold = f", warning threshold {budget.warn:g} s" if budget is not None else NO_RULE
        unknown = f", {missing} with no time" if missing else ""
        lines.append(f"report-check-warnings: {row} {total:.0f} s CPU in {len(known)} units{unknown}{threshold}")
        if budget is not None and total > budget.warn:
            findings.append(check_report.Finding(
                "warning", place, 0, row,
                f"{meaning} took {total:.0f} s of CPU time together ({len(known)} units{unknown}), over the threshold "
                f"{budget.warn:g} s of the rule {row}.  On 192 cores the wall time is at least the total divided by "
                f"192.  A time total gives a warning only, because the CPU time rises with the load of the host"))
    return findings, lines


def read_build_records(build_dir: Path) -> list[dict[str, object]]:
    """Return each record of a compile or a link of the build launcher in a build directory.

    Complexity: linear in the files of the build directory.
    """
    records: list[dict[str, object]] = []
    for path in build_dir.rglob(f"*{cost_meter.RECORD_SUFFIX}"):
        if path.name.endswith(cost_meter.TEST_RECORD_SUFFIX):
            continue
        try:
            record = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError):
            continue
        if isinstance(record, dict) and record.get("step") in ("compile", "link"):
            records.append(record)
    return records


def build_totals(records: list[dict[str, object]], configure: dict[str, object] | None,
                 budgets: dict[str, check_report.Budget], place: str) -> tuple[list[check_report.Finding], list[str]]:
    """Return the warnings and the summary lines of the four build totals (THE BUILD TOTALS).

    Args:
        records: The records of the compiles and the links of the build directory
        configure: The times of the last configure, or None
        budgets: The thresholds of the measured rules
        place: The path that a finding names

    Returns:
        A warning for each total over its threshold, and one summary line for each total
    """
    links = [record for record in records if record.get("step") == "link"]
    link_cpu = round(sum(loop_history.number(record.get("cost"), "cpu_s") or 0.0 for record in links), 1)
    peak = loop_history.peak_concurrent_gb([record["cost"] for record in records if isinstance(record.get("cost"), dict)])
    configured = loop_history.number(configure, "total_s")
    detail = "" if configure is None else (f" (configure {configure['configure_s']} s, generate {configure['generate_s']} "
                                           f"s, reply of the file API {configure.get('file_api_s', 0)} s, at "
                                           f"{configure['start']})")
    values = (("link-count", float(len(links)), "links", "the link records of the build directory", ""),
              ("total-link-cpu", link_cpu, "s", "the link records of the build directory together", ""),
              ("build-peak-memory", peak, "GB", "the compiles and links whose wall times overlap", ""),
              ("configure-time", configured, "s", "the last configure of the build directory", detail))
    findings: list[check_report.Finding] = []
    lines: list[str] = []
    for row, value, unit, what, extra in values:
        budget = budgets.get(row)
        threshold = f", warning threshold {budget.warn:g} {unit}" if budget is not None else NO_RULE
        shown = "no value" if value is None else f"{value:g} {unit}"
        lines.append(f"report-check-warnings: {row} {shown}{extra}{threshold}")
        findings += loop_history.warning_over(budgets, row, value, unit, what, place)
    return findings, lines


def report_build(build_dir: Path) -> list[check_report.Finding]:
    """Print the build totals of a build directory, and return their warnings."""
    try:
        budgets = check_report.read_budgets()
    except (OSError, ValueError) as reason:
        print(f"report-check-warnings: no build totals: {reason}")
        return []
    findings, lines = build_totals(read_build_records(build_dir), loop_history.configure_time(build_dir), budgets,
                                   build_census.shown(build_dir, REPO_ROOT))
    for line in lines:
        print(line)
    return findings


def report_totals(build_dir: Path) -> list[check_report.Finding]:
    """Print the time totals of a build directory, and return the warnings of the totals."""
    try:
        budgets = check_report.read_budgets()
        census = build_census.read_census(build_dir, REPO_ROOT, read_files=False)
        test_times = read_test_times(build_dir)
    except (build_census.NotApplicable, build_census.CensusError, ctest_reader.CtestError, OSError,
            ValueError) as reason:
        print(f"report-check-warnings: no time totals: {reason}")
        return []
    findings, lines = time_totals(census, test_times, budgets, build_census.shown(build_dir, REPO_ROOT))
    for line in lines:
        print(line)
    return findings


def self_test() -> int:
    """Do a test of the time totals and of the build totals, with planted input.

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
    expect("a total with no measured rule gives no warning and says so", not found and NO_RULE in lines[0])
    expect("the registry has a measured rule for each total",
           all(row in check_report.read_budgets() for row, _, _ in TOTALS))

    gigabyte = loop_history.KB_PER_GB
    planted = [{"step": "link", "result": "built", "cost": {"cpu_s": 2.0, "end": 10.0, "wall_s": 4.0,
                                                          "peak_rss_kb": 2 * gigabyte}},
               {"step": "compile", "result": "built", "cost": {"cpu_s": 5.0, "end": 9.0, "wall_s": 5.0,
                                                             "peak_rss_kb": 3 * gigabyte}},
               {"step": "compile", "result": "hit"}]
    build_rows = {row: check_report.Budget(row, limit, limit, "u", "m")
                  for row, limit in (("link-count", 1.0), ("total-link-cpu", 1.5), ("build-peak-memory", 4.0),
                                     ("configure-time", 10.0))}
    configure = {"start": "2026-10-02T00:00:00Z", "configure_s": 9.0, "generate_s": 3.0, "total_s": 12.0}
    found, lines = build_totals(planted, configure, build_rows, "build")
    expect("build_totals warns on the link CPU, on the peak memory of the steps that overlap and on the configure, and "
           "not on a count at its threshold",
           sorted(f.check for f in found) == ["build-peak-memory", "configure-time", "total-link-cpu"]
           and all(f.level == "warning" for f in found) and any("5 GB" in f.message for f in found))
    expect("build_totals gives one line for each total, with the parts of the configure",
           len(lines) == 4 and "link-count 1 links" in lines[0] and "(configure 9.0 s, generate 3.0 s" in lines[3])
    found, lines = build_totals([], None, build_rows, "build")
    expect("build_totals gives no warning with no record and no configure", not found and "no value" in lines[3])
    expect("the registry has a measured rule for each build total",
           all(row in check_report.read_budgets() for row in build_rows))

    with tempfile.TemporaryDirectory(prefix="report-totals-") as scratch:
        build = Path(scratch).resolve()
        (build / "test").mkdir()
        for name, program, cpu in (("test_planted", str(build / "test" / "test_planted"), 2.5),
                                   ("planted_script", "/usr/bin/python3", 7.0)):
            run = cost_meter.Measurement(0, cpu, 0.0, cpu, 1024, 1.0, 1.0, None)
            cost_meter.write_test_record(str(build), name, program, run, "passed")
        expect("the record of an executable test gives its CPU time, and the record of a script test has no part",
               read_test_times(build) == [2.5])
        (build / "test" / f"test_planted{cost_meter.RECORD_SUFFIX}").write_text(json.dumps(planted[0]), encoding="utf-8")
        (build / "test" / f"a.cpp.o{cost_meter.RECORD_SUFFIX}").write_text(json.dumps(planted[1]), encoding="utf-8")
        expect("read_build_records reads the records of the links and the compiles, and no record of a test",
               sorted(str(item["step"]) for item in read_build_records(build)) == ["compile", "link"])
    if failures:
        print(f"report-check-warnings --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("report-check-warnings --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Run the self-test of the totals.

    Args:
        argv: --self-test

    Returns:
        The status of the self-test, or 2 for a usage error
    """
    if argv != ["--self-test"]:
        print("usage: guard selftest guard.legacy.report_check_warnings", file=sys.stderr)
        return 2
    return self_test()
