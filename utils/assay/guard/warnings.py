"""guard warnings — print the warnings that the rules wrote for one build directory, and the totals of the build.

A rule that gives warnings and no error writes them to
<build>/check-warnings/RULE.txt (assay.core.report).  ctest shows no output of
a test with no error.  `guard warnings` prints those warnings after a build or
a test run.  In a GitHub workflow it also prints each one as an annotation.
After the warnings, it prints the time totals and the build totals of the build
directory, which assay.guard.engines.buildsys.cost_budgets adds (THE TOTALS of
that module).

The test launcher and the fixture driver write one file for each test:
CHECK.TEST.txt for each check of TEST_KEYED_CHECKS.  No run writes the file of
a test again after the test goes from the tree.  Before it reads the files,
`guard warnings` reads the tests that the build registers (assay.readers.ctest),
and it removes the file of each test that the build does not register.  When
it cannot read the tests, it keeps each file and tells why.

    python3 utils/bin/guard warnings BUILD_DIR
    python3 utils/bin/guard selftest guard.warnings

The exit status is 0 when each file has the format, also with warnings.  It is
1 when a line of a warnings file does not have the format.  It is 1 also when a
case of the self-test does not hold, and it is 2 for a usage error.
"""

import sys
import tempfile
from collections import Counter
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import record as cost_record
from assay.core import report as check_report
from assay.core import testing
from assay.core.exit import ExitCode
from assay.guard.engines.buildsys import cost_budgets
from assay.readers import census as build_census
from assay.readers import cmake as cmake_reader
from assay.readers import ctest as ctest_reader

USAGE = "usage: guard warnings BUILD_DIR | guard selftest guard.warnings"
# The checks that use the name of one test as the writer key (assay.core.report, THE WARNINGS DIRECTORY).
TEST_KEYED_CHECKS = frozenset({"test-time", "test-memory", "fixture-cpu", "fixture-instructions"})


def test_keyed_files(warnings_dir: Path) -> list[tuple[str, str]]:
    """Return the check and the test of each warnings file that has the name of a test as its writer key.

    Complexity: linear in the files of the warnings directory.
    """
    if not warnings_dir.is_dir():
        return []
    named = (path.name.split(".") for path in sorted(warnings_dir.glob("*.*.txt")))
    return [(parts[0], parts[1]) for parts in named if len(parts) == 3 and parts[0] in TEST_KEYED_CHECKS]


def remove_gone_tests(build_dir: Path, warnings_dir: Path) -> None:
    """Remove the warnings file of each test that the build does not register, and write one line for each.

    Complexity: one listing of ctest when a file has the key of a test, and linear in the files.
    """
    keyed = test_keyed_files(warnings_dir)
    if not keyed:
        return
    try:
        registered = {test.name for test in ctest_reader.tests(build_dir)}
    except ctest_reader.CtestError as problem:
        check_report.write_line(
            f"guard warnings: each warnings file of a test stays, because the tests of the build are not known: "
            f"{problem}"
        )
        return
    for check, test in keyed:
        if test not in registered:
            check_report.write_warnings(warnings_dir, check, [], test)
            check_report.write_line(
                f"guard warnings: removed {warnings_dir / f'{check}.{test}.txt'}, because the build registers no "
                f"test {test}"
            )


def main(argv: list[str]) -> int:
    """Print the warnings and the totals of one build directory.

    Args:
        argv: The words after `guard warnings`: one build directory, or --self-test

    Returns:
        0 when each warnings file has the format, 1 when a line does not, 2 for a usage error
    """
    if argv == ["--self-test"]:
        return self_test()
    if len(argv) != 1:
        check_report.write_line(USAGE, is_error=True)
        return ExitCode.USAGE
    build_dir = Path(argv[0]).resolve()
    warnings_dir = build_dir / check_report.WARNINGS_SUBDIR
    remove_gone_tests(build_dir, warnings_dir)
    findings, problems = check_report.read_warnings_dir(warnings_dir)
    findings += cost_budgets.total_report_totals(build_dir)
    findings += cost_budgets.total_report_build(build_dir)
    annotate = cost_meter.is_github_actions()
    for found in findings:
        check_report.write_line(found.text())
        if annotate:
            check_report.write_line(found.annotation())
    for problem in problems:
        check_report.write_line(problem, is_error=True)
    counts = Counter(found.check for found in findings)
    summary = ", ".join(f"{check} {count}" for check, count in sorted(counts.items()))
    check_report.write_line(
        f"guard warnings: {len(findings)} warning(s) in {warnings_dir} and the totals"
        + (f": {summary}" if summary else "")
    )
    return ExitCode.FINDINGS if problems else ExitCode.OK


def print_cases(expect: testing.Expect) -> None:
    """Print the warnings files of a planted build directory, and refuse a line that does not have the format."""
    with tempfile.TemporaryDirectory(prefix="guard-warnings-") as scratch:
        build = Path(scratch)
        warnings_dir = build / check_report.WARNINGS_SUBDIR
        warnings_dir.mkdir()
        (warnings_dir / "alpha.txt").write_text(
            check_report.Finding("warning", "a.h", 1, "alpha", "m").text() + "\n", encoding="utf-8"
        )
        status, text = testing.captured(lambda: main([str(build)]))
        expect(
            "a build with no census prints the warnings and tells why it has no totals",
            status == 0 and "a.h:1: warning: [alpha] m" in text and "no time totals" in text,
        )
        (warnings_dir / "alpha.txt").write_text("not a finding\n", encoding="utf-8")
        status, _ = testing.captured(lambda: main([str(build)]))
        expect("a warnings file with a line of a different shape gives status 1", status == 1)
    status, _ = testing.captured(lambda: main([]))
    expect("a call with no build directory is a usage error", status == 2)


def gone_test_cases(expect: testing.Expect) -> None:
    """Remove the warnings file of a test that a planted build does not register, and keep each other file."""
    with tempfile.TemporaryDirectory(prefix="guard-warnings-") as scratch:
        build = Path(scratch)
        warnings_dir = build / check_report.WARNINGS_SUBDIR
        for check, key in (("test-memory", "kept_test"), ("test-memory", "gone_test"), ("alpha", "gone_test")):
            found = check_report.Finding("warning", "a.h", 1, check, f"{check} {key}")
            check_report.write_warnings(warnings_dir, check, [found], key)
        status, text = testing.captured(lambda: main([str(build)]))
        expect(
            "a build whose tests are not known keeps each warnings file of a test, and tells why",
            status == 0 and "each warnings file of a test stays" in text and "test-memory gone_test" in text,
        )
        tool = build / "planted-ctest"
        tool.write_text(f'#!{sys.executable} -S\nprint(\'{{"tests": [{{"name": "kept_test"}}]}}\')\n', encoding="utf-8")
        tool.chmod(0o755)
        cmake_reader.plant(build, cache={"CMAKE_CTEST_COMMAND": ("FILEPATH", str(tool))})
        status, text = testing.captured(lambda: main([str(build)]))
        expect(
            "the file of a test that the build does not register goes, and the file of a registered test and the file "
            "of a check with another key stay",
            status == 0
            and "because the build registers no test gone_test" in text
            and "test-memory gone_test" not in text
            and "test-memory kept_test" in text
            and "alpha gone_test" in text
            and test_keyed_files(warnings_dir) == [("test-memory", "kept_test")],
        )


def planted_unit(kind: str, cpu: float | None) -> build_census.Unit:
    """Return a planted unit of a census, with a CPU time or no time."""
    return build_census.Unit(kind, f"{kind}-{cpu}", "src/x.cpp", build_census.Cost(cpu, None))


def time_total_cases(expect: testing.Expect) -> None:
    """Add the three time totals of a planted census, and compare each one with planted thresholds."""
    budgets = {
        rule_id: check_report.Budget(rule_id, limit, limit, "s", "m")
        for rule_id, limit in (("total-compile-cpu", 100.0), ("total-fixture-cpu", 50.0), ("total-test-cpu", 10.0))
    }
    units = [
        planted_unit("object", 60.0),
        planted_unit("object", 30.0),
        planted_unit("fixture", 20.0),
        planted_unit("fixture", None),
    ]
    census = build_census.Census(Path("/b"), units, {})
    found, lines = cost_budgets.total_time_totals(census, [4.0, 5.0], budgets, "build")
    expect(
        "totals at or below their thresholds give no warning, and one line each",
        not found
        and len(lines) == 3
        and "total-compile-cpu 90 s CPU in 2 units" in lines[0]
        and "total-fixture-cpu 20 s CPU in 1 units, 1 with no time" in lines[1],
    )
    census.units.append(planted_unit("object", 20.0))
    found, _ = cost_budgets.total_time_totals(census, [4.0, 5.0, 3.0, None], budgets, "build")
    expect(
        "a total over its threshold gives a warning and no error, and it counts the units with no time",
        [(item.check, item.level) for item in found]
        == [("total-compile-cpu", "warning"), ("total-test-cpu", "warning")]
        and "110 s of CPU time together (3 units)" in found[0].message
        and "(3 units, 1 with no time)" in found[1].message,
    )
    found, lines = cost_budgets.total_time_totals(census, [], {}, "build")
    expect(
        "a total with no measured rule gives no warning and tells so",
        not found and cost_budgets.TOTAL_NO_RULE in lines[0],
    )
    registry = check_report.read_budgets()
    expect(
        "the registry has a measured rule for each time total",
        all(rule_id in registry for rule_id, _, _ in cost_budgets.TOTAL_TIMES),
    )


def planted_record(
    step: cost_record.Step, cpu: float, end: float, wall: float, peak_gb: float
) -> cost_record.CostRecord:
    """Return a planted record of a step that ran, with its cost block."""
    cost = cost_record.Cost(cpu, cpu, 0.0, wall, int(peak_gb * cost_meter.KB_PER_GB), 1.0, end)
    return cost_record.CostRecord(step, "built", None, None, None, 0, cost, None)


def build_total_cases(expect: testing.Expect) -> None:
    """Give the four build totals of planted records and a planted configure, and compare each one with thresholds."""
    records = [
        planted_record(cost_record.Step.LINK, 2.0, 10.0, 4.0, 2.0),
        planted_record(cost_record.Step.COMPILE, 5.0, 9.0, 5.0, 3.0),
        cost_record.CostRecord(cost_record.Step.COMPILE, "hit", None, None, None, 0, None, None),
    ]
    budgets = {
        rule_id: check_report.Budget(rule_id, limit, limit, "u", "m")
        for rule_id, limit in (
            ("link-count", 1.0),
            ("total-link-cpu", 1.5),
            ("build-peak-memory", 4.0),
            ("configure-time", 10.0),
        )
    }
    configure = {"start": "2026-10-02T00:00:00Z", "configure_s": 9.0, "generate_s": 3.0, "total_s": 12.0}
    found, lines = cost_budgets.total_build_totals(records, configure, budgets, "build")
    expect(
        "the build totals warn on the link CPU, on the peak memory of the steps that overlap and on the configure, "
        "and not on a count at its threshold",
        sorted(item.check for item in found) == ["build-peak-memory", "configure-time", "total-link-cpu"]
        and all(item.level == "warning" for item in found)
        and any("5 GB" in item.message for item in found),
    )
    expect(
        "the build totals give one line for each total, with the parts of the configure",
        len(lines) == 4 and "link-count 1 links" in lines[0] and "(configure 9.0 s, generate 3.0 s" in lines[3],
    )
    found, lines = cost_budgets.total_build_totals([], None, budgets, "build")
    expect("the build totals give no warning with no record and no configure", not found and "no value" in lines[3])
    registry = check_report.read_budgets()
    expect("the registry has a measured rule for each build total", all(rule_id in registry for rule_id in budgets))


def record_cases(expect: testing.Expect) -> None:
    """Read the planted records of a build directory: the tests, the compiles and the links."""
    from assay.loop import step as step_meter

    with tempfile.TemporaryDirectory(prefix="report-totals-") as scratch:
        build = Path(scratch).resolve()
        (build / "test").mkdir()
        for name, program, cpu in (
            ("test_planted", str(build / "test" / "test_planted"), 2.5),
            ("planted_script", "/usr/bin/python3", 7.0),
        ):
            run = cost_meter.Measurement(0, cpu, 0.0, cpu, 1024, 1.0, 1.0, None)
            cost_meter.write_test_record(str(build), name, program, run, "passed")
        expect(
            "the record of an executable test gives its CPU time, and the record of a script test has no part",
            cost_budgets.total_test_times(build) == [2.5],
        )
        run = cost_meter.Measurement(0, 2.0, 0.0, 4.0, 2048, 1.0, 10.0, None)
        step_meter.write("link", str(build / "test" / "test_planted"), None, run, "built")
        step_meter.write("compile", str(build / "test" / "a.cpp.o"), str(build / "a.cpp"), run, "built")
        expect(
            "the build records are the records of the links and the compiles, and no record of a test",
            sorted(str(item.step) for item in cost_budgets.total_build_records(build)) == ["compile", "link"],
        )


def self_test() -> int:
    """Run each case of the print of the warnings files and of the totals, and write one line for each."""
    return testing.run(
        "guard.warnings", [], (print_cases, gone_test_cases, time_total_cases, build_total_cases, record_cases)
    )
