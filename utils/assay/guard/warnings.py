"""guard warnings — print the warnings that the rules wrote for one build directory, and the totals of the build.

A rule that gives warnings and no error writes them to
<build>/check-warnings/RULE.txt (assay.core.report).  ctest shows no output of
a test with no error.  `guard warnings` prints those warnings after a build or
a test run.  In a GitHub workflow it also prints each one as an annotation.

THE TOTALS
    Before the warnings, the command prints the totals of the build
    directory.  Each total is a plain number with its unit.  No rule judges a
    total, because a CPU time and a wall time change with the load of the
    host.  The totals:
      * the CPU time of the last real compile of each negative fixture, from
        the census of assay.readers.census
      * the CPU time of each executable test in its last run, from the
        records of the test launcher.  A script test has no part
      * the link records of the build launcher and their CPU time
      * the largest sum of the peak memory of the compiles and the links
        whose wall times overlap, an upper bound (assay.loop.history, THE COST
        RECORDS)
      * the wall time of the last configure less its plugin build, and the
        plugin build (assay.loop.history, THE CONFIGURE TIME and THE PLUGIN
        BUILD).
    A unit with no time has no part in a time total, and the line of the
    total gives the count of such units.  When the census or the test
    records cannot be read, one line tells why.

The test launcher and the fixture driver write one file for each test:
CHECK.TEST.txt for each check of TEST_KEYED_CHECKS.  No run writes the file of
a test again after the test goes from the tree.  Before it reads the files,
`guard warnings` reads the tests that the build registers (assay.readers.ctest),
and it removes the file of each test that the build does not register.  When
it cannot read the tests, it keeps each file and tells why.

Each writer of a warnings file names a rule of the registry.  No run writes the
file of a rule again after the rule goes from the registry.  `guard warnings`
also removes the file of each rule that the registry does not name.  When it
cannot read the registry, it keeps each file and tells why.  Each line that it
writes about the files and the totals starts with PREFIX.

    python3 utils/bin/guard warnings BUILD_DIR
    python3 utils/bin/guard selftest guard.warnings

The exit status is 0 when each file has the format, also with warnings.  It is
1 when a line of a warnings file does not have the format.  It is 1 also when a
case of the self-test does not hold, and it is 2 for a usage error.
"""

import os
import sys
import tempfile
from collections import Counter
from collections.abc import Mapping, Sequence
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import record as cost_record
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import testing
from assay.core.exit import ExitCode
from assay.core.paths import REPO_ROOT
from assay.readers import census as build_census
from assay.readers import cmake as cmake_reader
from assay.readers import ctest as ctest_reader

USAGE = "usage: guard warnings BUILD_DIR | guard selftest guard.warnings"
# The prefix of each line that the command writes about the warnings files and the totals.
PREFIX = "guard warnings:"
# The checks that use the name of one test as the writer key (assay.core.report, THE WARNINGS DIRECTORY).
TEST_KEYED_CHECKS = frozenset({"test-memory", "test-instructions", "fixture-instructions"})


def test_keyed_files(warnings_dir: Path) -> list[tuple[str, str]]:
    """Return the check and the test of each warnings file that has the name of a test as its writer key.

    The file is CHECK.TEST.txt, and the name of a test can hold a dot (assay.core.report, THE WARNINGS DIRECTORY).
    Complexity: linear in the files of the warnings directory.
    """
    if not warnings_dir.is_dir():
        return []
    named = (path.name.removesuffix(".txt").partition(".") for path in sorted(warnings_dir.glob("*.*.txt")))
    return [(check, test) for check, _, test in named if check in TEST_KEYED_CHECKS and test]


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
            f"{PREFIX} each warnings file of a test stays, because the tests of the build are not known: {problem}"
        )
        return
    for check, test in keyed:
        if test not in registered:
            check_report.write_warnings(warnings_dir, check, [], test)
            check_report.write_line(
                f"{PREFIX} removed {warnings_dir / f'{check}.{test}.txt'}, because the build registers no test {test}"
            )


def remove_gone_rules(warnings_dir: Path, registry_path: Path = rule_registry.REGISTRY) -> None:
    """Remove the warnings file of each rule that the registry does not name, and write one line for each.

    Complexity: one read of the registry when the directory holds a file, and linear in the files.
    """
    files = sorted(warnings_dir.glob("*.txt")) if warnings_dir.is_dir() else []
    if not files:
        return
    try:
        rules = rule_registry.load(registry_path).rules
    except (rows_grammar.RowsError, OSError) as problem:
        check_report.write_line(f"{PREFIX} each warnings file stays, because the registry cannot be read: {problem}")
        return
    for path in files:
        rule_id = path.name.split(".", 1)[0]
        if rule_id not in rules:
            path.unlink(missing_ok=True)
            check_report.write_line(f"{PREFIX} removed {path}, because the registry names no rule {rule_id}")


def test_times(build_dir: Path) -> list[float | None]:
    """Return the CPU time of the record of each executable test of a build directory: a program in the directory.

    Complexity: linear in the records of the build directory.

    Raises:
        CtestError: If a record cannot be read
    """
    build = build_dir.resolve()
    return [
        record.cost.cpu_s
        for record in ctest_reader.costs(build_dir).values()
        if os.path.isabs(record.program) and Path(record.program).resolve().is_relative_to(build)
    ]


def build_records(build_dir: Path) -> list[cost_record.CostRecord]:
    """Return each record of a compile or a link of the build launcher in a build directory.

    A record that cannot be read, and the record of a test, have no part.
    Complexity: linear in the files of the build directory.
    """
    records: list[cost_record.CostRecord] = []
    for path in sorted(build_dir.rglob(f"*{cost_record.SUFFIX}")):
        if path.name.endswith(cost_record.TEST_SUFFIX):
            continue
        try:
            records.append(cost_record.read(path))
        except cost_record.RecordError:
            continue
    return records


def time_lines(census: build_census.Census, times: Sequence[float | None]) -> list[str]:
    """Return the line of each time total: the CPU time of the fixtures and of the executable tests (THE TOTALS).

    Complexity: linear in the units and the tests.
    """
    groups = (
        ("the negative fixtures", [unit.cost.cpu_s for unit in census.units if unit.kind == "fixture"]),
        ("the executable tests", list(times)),
    )
    lines: list[str] = []
    for what, values in groups:
        known = [value for value in values if value is not None]
        missing = len(values) - len(known)
        unknown = f", {missing} with no time" if missing else ""
        lines.append(f"{PREFIX} {what} took {sum(known):.0f} s of CPU time in {len(known)} units{unknown}")
    return lines


def build_lines(records: Sequence[cost_record.CostRecord], configure: Mapping[str, object] | None) -> list[str]:
    """Return the lines of the build totals of the records of a build directory and of its last configure (THE TOTALS).

    Args:
        records: The records of the compiles and the links of the build directory
        configure: The times of the last configure (assay.loop.history.configure_time()), or None
    """
    from assay.loop import history as loop_history

    links = [record for record in records if record.step == cost_record.LINK]
    link_cpu = sum(record.cost.cpu_s for record in links if record.cost is not None)
    peak = loop_history.peak_concurrent_gb([record.cost for record in records if record.cost is not None])
    lines = [f"{PREFIX} the build directory holds {len(links)} link records, which took {link_cpu:.1f} s of CPU time"]
    if peak is None:
        lines.append(f"{PREFIX} no record of a compile or a link holds a peak memory")
    else:
        lines.append(f"{PREFIX} the compiles and the links whose wall times overlap took at most {peak:g} GB of memory")
    if configure is None:
        lines.append(f"{PREFIX} the last configure of the build directory has no record of its time")
        return lines
    lines.append(
        f"{PREFIX} the last configure took {configure['total_s']} s less its plugin build (configure "
        f"{configure['configure_s']} s, generate {configure['generate_s']} s, reply of the file API "
        f"{configure['file_api_s']} s, at {configure['start']})"
    )
    plugin_s = loop_history.number(configure, "plugin_build_s")
    if plugin_s:
        lines.append(f"{PREFIX} the plugin build of the last configure took {plugin_s:g} s")
    else:
        lines.append(f"{PREFIX} the last configure built no plugin")
    return lines


def total_lines(build_dir: Path) -> list[str]:
    """Return the line of each total of a build directory (THE TOTALS).

    Complexity: linear in the files of the build directory.
    """
    from assay.loop import history as loop_history

    try:
        census = build_census.read_census(build_dir, REPO_ROOT, read_files=False)
        lines = time_lines(census, test_times(build_dir))
    except (
        build_census.NotApplicable,
        build_census.CensusError,
        ctest_reader.CtestError,
        OSError,
        ValueError,
    ) as reason:
        lines = [f"{PREFIX} no time totals: {reason}"]
    return [*lines, *build_lines(build_records(build_dir), loop_history.configure_time(build_dir))]


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
    remove_gone_rules(warnings_dir)
    findings, problems = check_report.read_warnings_dir(warnings_dir)
    for line in total_lines(build_dir):
        check_report.write_line(line)
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
        f"{PREFIX} {len(findings)} warning(s) in {warnings_dir}" + (f": {summary}" if summary else "")
    )
    return ExitCode.FINDINGS if problems else ExitCode.OK


def print_cases(expect: testing.Expect) -> None:
    """Print the warnings files of a planted build directory, and refuse a line that does not have the format."""
    with tempfile.TemporaryDirectory(prefix="guard-warnings-") as scratch:
        build = Path(scratch)
        warnings_dir = build / check_report.WARNINGS_SUBDIR
        warnings_dir.mkdir()
        (warnings_dir / "python-lint.txt").write_text(
            check_report.Finding("warning", "a.h", 1, "python-lint", "m").text() + "\n", encoding="utf-8"
        )
        status, text = testing.captured(lambda: main([str(build)]))
        expect(
            "a build with no census prints the warnings and tells why it has no time totals",
            status == 0 and "a.h:1: warning: [python-lint] m" in text and "no time totals" in text,
        )
        expect(
            "each line about the totals and the summary starts with the name of the command",
            "guard warnings: no time totals" in text
            and "guard warnings: 1 warning(s)" in text
            and "report-check-warnings" not in text,
        )
        (warnings_dir / "python-lint.txt").write_text("not a finding\n", encoding="utf-8")
        status, _ = testing.captured(lambda: main([str(build)]))
        expect("a warnings file with a line of a different shape gives status 1", status == 1)
    status, _ = testing.captured(lambda: main([]))
    expect("a call with no build directory is a usage error", status == 2)


def gone_test_cases(expect: testing.Expect) -> None:
    """Remove the warnings file of a test that a planted build does not register, and keep each other file."""
    with tempfile.TemporaryDirectory(prefix="guard-warnings-") as scratch:
        build = Path(scratch)
        warnings_dir = build / check_report.WARNINGS_SUBDIR
        for check, key in (
            ("test-memory", "kept_test"),
            ("test-memory", "gone_test"),
            ("python-lint", "gone_test"),
            ("test-instructions", "kept.sh"),
            ("test-instructions", "gone.sh"),
        ):
            found = check_report.Finding("warning", "a.h", 1, check, f"{check} {key}")
            check_report.write_warnings(warnings_dir, check, [found], key)
        status, text = testing.captured(lambda: main([str(build)]))
        expect(
            "a build whose tests are not known keeps each warnings file of a test, and tells why",
            status == 0 and "each warnings file of a test stays" in text and "test-memory gone_test" in text,
        )
        tool = build / "planted-ctest"
        tool.write_text(
            f'#!{sys.executable} -S\nprint(\'{{"tests": [{{"name": "kept_test"}}, {{"name": "kept.sh"}}]}}\')\n',
            encoding="utf-8",
        )
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
            and "python-lint gone_test" in text
            and test_keyed_files(warnings_dir) == [("test-instructions", "kept.sh"), ("test-memory", "kept_test")],
        )
        expect(
            "the file of a test whose name holds a dot goes when the build does not register the test",
            "because the build registers no test gone.sh" in text and "test-instructions kept.sh" in text,
        )


def gone_rule_cases(expect: testing.Expect) -> None:
    """Remove the warnings file of a rule that the registry does not name, and keep the file of a named rule."""
    with tempfile.TemporaryDirectory(prefix="guard-warnings-") as scratch:
        build = Path(scratch)
        warnings_dir = build / check_report.WARNINGS_SUBDIR
        for check, key in (("python-lint", None), ("gone-rule", None), ("gone-rule", "part1")):
            found = check_report.Finding("warning", "a.h", 1, check, f"{check} {key}")
            check_report.write_warnings(warnings_dir, check, [found], key)
        broken = build / "registry.txt"
        broken.write_text("format 1\nrule gone-rule\n", encoding="utf-8")
        _, text = testing.captured(lambda: remove_gone_rules(warnings_dir, broken))
        expect(
            "a registry that cannot be read keeps each warnings file, and tells why",
            "each warnings file stays" in text and len(sorted(warnings_dir.glob("*.txt"))) == 3,
        )
        status, text = testing.captured(lambda: main([str(build)]))
        expect(
            "the files of a rule that the registry does not name go, and the file of a named rule stays",
            status == 0
            and "because the registry names no rule gone-rule" in text
            and "gone-rule None" not in text
            and "gone-rule part1" not in text
            and "python-lint None" in text
            and [path.name for path in sorted(warnings_dir.glob("*.txt"))] == ["python-lint.txt"],
        )


def planted_unit(kind: str, cpu: float | None) -> build_census.Unit:
    """Return a planted unit of a census, with a CPU time or no time."""
    return build_census.Unit(kind, f"{kind}-{cpu}", "src/x.cpp", build_census.Cost(cpu, None))


def time_line_cases(expect: testing.Expect) -> None:
    """Add the two time totals of a planted census and planted tests, and give each one as a plain number."""
    units = [
        planted_unit("object", 60.0),
        planted_unit("fixture", 20.0),
        planted_unit("fixture", 10.0),
    ]
    census = build_census.Census(Path("/b"), units, {})
    lines = time_lines(census, [4.0, 5.0])
    expect(
        "the two time totals add the CPU time of their units, and an object of all is in neither",
        lines
        == [
            "guard warnings: the negative fixtures took 30 s of CPU time in 2 units",
            "guard warnings: the executable tests took 9 s of CPU time in 2 units",
        ],
    )
    census.units.append(planted_unit("fixture", None))
    lines = time_lines(census, [4.0, None])
    expect(
        "a unit with no time has no part in a total, and the line gives the count of such units",
        lines
        == [
            "guard warnings: the negative fixtures took 30 s of CPU time in 2 units, 1 with no time",
            "guard warnings: the executable tests took 4 s of CPU time in 1 units, 1 with no time",
        ],
    )


def planted_record(
    step: cost_record.Step, cpu: float, end: float, wall: float, peak_gb: float
) -> cost_record.CostRecord:
    """Return a planted record of a step that ran, with its cost block."""
    cost = cost_record.Cost(cpu, cpu, 0.0, wall, int(peak_gb * cost_meter.KB_PER_GB), 1.0, end)
    return cost_record.CostRecord(step, "built", None, None, None, 0, cost, None)


def build_line_cases(expect: testing.Expect) -> None:
    """Give the build totals of planted records and a planted configure as plain numbers with their units."""
    records = [
        planted_record(cost_record.LINK, 2.0, 10.0, 4.0, 2.0),
        planted_record(cost_record.COMPILE, 5.0, 9.0, 5.0, 3.0),
        cost_record.CostRecord(cost_record.COMPILE, cost_record.HIT, None, None, None, 0, None, None),
    ]
    configure = {
        "start": "2026-10-02T00:00:00Z",
        "configure_s": 9.0,
        "generate_s": 3.0,
        "file_api_s": 0.5,
        "total_s": 12.5,
        "plugin_build_s": 7.0,
    }
    lines = build_lines(records, configure)
    expect(
        "the build totals give the links and their CPU time, the peak memory of the steps that overlap, the configure "
        "less its plugin build with its parts, and the plugin build",
        lines
        == [
            "guard warnings: the build directory holds 1 link records, which took 2.0 s of CPU time",
            "guard warnings: the compiles and the links whose wall times overlap took at most 5 GB of memory",
            (
                "guard warnings: the last configure took 12.5 s less its plugin build (configure 9.0 s, generate "
                "3.0 s, reply of the file API 0.5 s, at 2026-10-02T00:00:00Z)"
            ),
            "guard warnings: the plugin build of the last configure took 7 s",
        ],
    )
    no_plugin = build_lines([], {**configure, "plugin_build_s": 0.0})
    expect(
        "a last configure that built no plugin gives a line that tells so", no_plugin[-1].endswith("built no plugin")
    )
    empty = build_lines([], None)
    expect(
        "a build directory with no record and no configure gives a line for each total that tells so",
        empty
        == [
            "guard warnings: the build directory holds 0 link records, which took 0.0 s of CPU time",
            "guard warnings: no record of a compile or a link holds a peak memory",
            "guard warnings: the last configure of the build directory has no record of its time",
        ],
    )


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
            record = cost_record.TestRecord(name, "passed", program, 0, cost_record.measured(run))
            cost_record.write(cost_record.test_record_file(build, name), record)
        expect(
            "the record of an executable test gives its CPU time, and the record of a script test has no part",
            test_times(build) == [2.5],
        )
        run = cost_meter.Measurement(0, 2.0, 0.0, 4.0, 2048, 1.0, 10.0, None)
        steps: list[tuple[cost_record.Step, Path, str | None]] = [
            (cost_record.LINK, build / "test" / "test_planted", None),
            (cost_record.COMPILE, build / "test" / "a.cpp.o", str(build / "a.cpp")),
        ]
        for step, output, source in steps:
            built = step_meter.step_record(step, str(output), source, run, "built")
            cost_record.write(cost_record.record_file(output), built)
        expect(
            "the build records are the records of the links and the compiles, and no record of a test",
            sorted(str(item.step) for item in build_records(build)) == ["compile", "link"],
        )


def self_test() -> int:
    """Run each case of the print of the warnings files and of the totals, and write one line for each."""
    return testing.run(
        "guard.warnings",
        [],
        (print_cases, gone_test_cases, gone_rule_cases, time_line_cases, build_line_cases, record_cases),
    )
