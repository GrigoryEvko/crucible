"""check-test-time — each test of a build runs with the judge of the measured rules test-time and test-memory.

THE JUDGE
    The test launcher `loop test-step` (utils/assay/loop/teststep.py) and the
    fixture driver `loop fixture` judge each test while it runs.  They compare
    the wall time of the test with the measured rule test-time, and the peak
    memory of its largest process with the measured rule test-memory of
    utils/registry.txt.  A test over an error threshold that no ledger row
    admits fails.  Each run writes the record of the test,
    BUILD_DIR/test-records/NAME.test.cost (utils/assay/core/meter.py, THE
    RECORD).

THE RULE
    A test cannot see a test that runs with no judge, or a ledger row that
    names a test of no build.  This check reads the tests that the build
    directory registers (assay.readers.ctest) and the ledgers of the two
    rules, and it refuses:
      unjudged  a test with no test launcher and no fixture driver in its
                command.  Its time and its memory have no judge.  CMake
                gives the launcher to a test of an executable target, and
                crucible_script_test() of cmake/TestLauncher.cmake gives it to
                a script test
      unknown   a ledger row of one of the two rules for the kind of the build
                that names a test that the build does not register
    The judge refuses a row whose test runs at or below the error threshold
    of its rule.

    utils/ledgers/test-time.txt and utils/ledgers/test-memory.txt hold the
    tests that can be over the error threshold of their rule, one row for each
    test and build kind: '- | KIND | VALUE | TEST | REASON'
    (utils/assay/core/meter.py, THE REGISTRY AND THE LEDGERS).  A ledger that
    does not exist has no row.

WRITE
    --write writes the rows of the kind of the build again, from the records
    of the build directory.  Each registered test whose last record is over
    the error threshold of a rule gets a row with its measure, and a row keeps
    its reason.  A row of a different kind stays.

The variables CRUCIBLE_TEST_REGISTRY and CRUCIBLE_TEST_LEDGERS name a
different registry and a different directory of ledgers, as for the test
launcher.  Only a self-test sets them.

Usage
    guard check --rule test-time --build-dir BUILD_DIR [--warnings-dir DIR]
    guard check --rule test-time --build-dir BUILD_DIR --write
    guard check --rule test-time --self-test [--cmake CMAKE] [--ctest CTEST]

    The self-test runs the cmake and the ctest of PATH only when they give the
    pinned version (utils/assay/setup/pins.py).

Exit 0 with no error, 1 on an error or an input that the check cannot read, 2
on a usage error or a failed self-test.
"""

from __future__ import annotations

import itertools
import os
import sys
import tempfile
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import cli, proc, testing
from assay.core import meter as cost_meter
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.core.paths import REPO_ROOT
from assay.readers import cmake as cmake_reader
from assay.readers import ctest as ctest_reader
from assay.setup import pins as cmake_pin

CHECK = "test-time"
ROWS = ("test-time", "test-memory")
ROOT = REPO_ROOT
# The head comment of a ledger that --write makes.
LEDGER_COMMENT = (
    "# The ledger of the measured rule {rule} of utils/registry.txt: the tests that can be over its error threshold.\n"
    "# A row is '- | KIND | VALUE | TEST | REASON'.  KIND is the build kind of BUILD_DIR/build-kind.txt, and VALUE\n"
    "# is the measure of the test at the last --write.  A row applies only to a build of its kind.\n"
    "# `python3 utils/bin/guard check --rule test-time --build-dir DIR --write` writes this file.\n"
)
REGISTRY_ENV = "CRUCIBLE_TEST_REGISTRY"
LEDGERS_ENV = "CRUCIBLE_TEST_LEDGERS"
# The file name of the master loop, and the subcommands of the judge: the test launcher and the fixture driver.
MASTER = "loop"
JUDGES = ("test-step", "fixture")
DEFAULT_REASON = "the measure of one ctest run"


@dataclass(frozen=True, slots=True)
class Row:
    """One ledger row of the kind of the build."""

    line: int
    value: float
    reason: str


def is_judged(command: Sequence[str]) -> bool:
    """Return True when a command starts the test launcher or the fixture driver: the master loop and one of JUDGES."""
    return any(
        os.path.basename(word) == MASTER and following in JUDGES for word, following in itertools.pairwise(command)
    )


def ledger_path(rule: str) -> Path:
    """Return the ledger of one rule, in the directory of CRUCIBLE_TEST_LEDGERS when the environment names one."""
    return Path(cost_meter.ledger_file(rule, os.environ.get(LEDGERS_ENV)))


def read_ledger(rule: str, kind: str) -> dict[str, Row]:
    """Read the rows of one ledger that name the kind of the build.

    Returns:
        Each row of the kind, by its test.  A ledger that does not exist has no row

    Raises:
        ValueError: If a row of the ledger is malformed
        OSError: If the ledger exists and cannot be read
    """
    path = ledger_path(rule)
    rows, problems, _ = cost_meter.read_kind_rows(rule, cost_meter.ITEM_NAME, str(path))
    if problems:
        number, problem = problems[0]
        raise ValueError(f"{path}:{number}: {problem}")
    return {item: Row(*row) for (row_kind, item), row in rows.items() if row_kind == kind}


def display(path: Path | str) -> str:
    """Return a path relative to the repository root when it is under the root."""
    resolved = Path(path)
    try:
        return resolved.resolve().relative_to(ROOT).as_posix()
    except ValueError:
        return str(resolved)


def evaluate(
    tests: Sequence[ctest_reader.TestEntry], ledgers: Mapping[str, Mapping[str, Row]], build: Path
) -> list[check_report.Finding]:
    """Give each test with no judge, and each ledger row of a test that the build does not register.

    Complexity: O(t * w) for t tests of w words, and linear in the ledger rows.

    Args:
        tests: The registered tests of the build
        ledgers: The rows of the kind of the build of each rule
        build: The build directory, which a finding names for a test with no CMake file

    Returns:
        The findings
    """
    findings: list[check_report.Finding] = []
    for test in tests:
        if is_judged(test.command):
            continue
        place = display(test.file) if test.file else display(build)
        findings.append(
            check_report.Finding(
                "error",
                place,
                test.line,
                CHECK,
                f"the test {test.name} runs `{' '.join(test.command[:3])}` with no "
                f"judge of its time and its memory.  Register it with crucible_script_test() of "
                f"cmake/TestLauncher.cmake",
                "unjudged",
                test.name,
            )
        )
    names = {test.name for test in tests}
    for rule in ROWS:
        shown = display(ledger_path(rule))
        for name, row in sorted(ledgers.get(rule, {}).items()):
            if name not in names:
                findings.append(
                    check_report.Finding(
                        "error",
                        shown,
                        row.line,
                        CHECK,
                        f"the ledger of {rule} names the test {name}, which the build "
                        f"does not register.  Remove the row",
                        "unknown",
                        name,
                    )
                )
    return findings


def measures(record: ctest_reader.TestRecord) -> dict[str, float]:
    """Return the measure of each rule in one record: the wall time in s and the peak memory in GB."""
    return {"test-time": record.wall_s, "test-memory": record.peak_rss_kb / cost_meter.KB_PER_GB}


def write_ledgers(
    build: Path,
    tests: Sequence[ctest_reader.TestEntry],
    records: Mapping[str, ctest_reader.TestRecord],
    kind: str,
    budgets: Mapping[str, check_report.Budget],
) -> list[str]:
    """Write the rows of the kind of the build again for each rule, from the records (WRITE).

    Returns:
        The ledgers that the call wrote, as the messages show them
    """
    names = {test.name for test in tests}
    written: list[str] = []
    for rule in ROWS:
        path = ledger_path(rule)
        rows, _, _ = cost_meter.read_kind_rows(rule, cost_meter.ITEM_NAME, str(path))
        kept = {key: (value, reason) for key, (_, value, reason) in rows.items() if key[0] != kind}
        for name, record in sorted(records.items()):
            value = measures(record)[rule]
            if name in names and value > budgets[rule].error:
                reason = rows.get((kind, name), (0, 0.0, DEFAULT_REASON))[2]
                kept[(kind, name)] = (round(value, 2), reason)
        cost_meter.write_kind_rows(rule, cost_meter.ITEM_NAME, str(path), kept, LEDGER_COMMENT.format(rule=rule))
        written.append(display(path))
    return written


def summary(
    tests: Sequence[ctest_reader.TestEntry],
    records: Mapping[str, ctest_reader.TestRecord],
    findings: Sequence[check_report.Finding],
) -> str:
    """Return the line that tells what the check read: the tests, the records, the longest and the largest record."""
    judged = sum(1 for test in tests if is_judged(test.command))
    slowest = max(records.values(), key=lambda record: record.wall_s, default=None)
    largest = max(records.values(), key=lambda record: record.peak_rss_kb, default=None)
    parts = [f"check-test-time: {len(tests)} tests, {judged} with a judge, {len(records)} records"]
    if slowest is not None and largest is not None:
        parts.append(
            f"the longest {slowest.name} at {slowest.wall_s:.2f} s, the largest {largest.name} at "
            f"{largest.peak_rss_kb / cost_meter.KB_PER_GB:.3f} GB"
        )
    parts.append(f"{len(findings)} finding(s).")
    return ", ".join(parts)


def run(build: Path, warnings_dir: Path | None, should_write: bool) -> int:
    """Run the check, or the write of the ledgers, for one build directory.

    Returns:
        The exit code
    """
    registry = Path(os.environ.get(REGISTRY_ENV) or check_report.REGISTRY)
    try:
        budgets = check_report.read_budgets(registry)
        missing = [rule for rule in ROWS if rule not in budgets]
        if missing:
            raise ValueError(f"the registry {registry} has no measured rule {', '.join(missing)}")
        kind = cost_meter.read_kind(str(build))
        if kind is None:
            raise ValueError(f"{build}/{cost_meter.KIND_FILE} does not exist, so the kind of the build is not known")
        tests = ctest_reader.tests(build)
        ledgers = {rule: read_ledger(rule, kind) for rule in ROWS}
        records = ctest_reader.costs(build)
    except (ValueError, OSError, ctest_reader.CtestError) as problem:
        return check_report.emit(
            [check_report.Finding("error", display(build), 0, CHECK, f"the check cannot read its input: {problem}")],
            CHECK,
            warnings_dir,
        )
    if should_write:
        for shown in write_ledgers(build, tests, records, kind, budgets):
            check_report.write_line(f"check-test-time: wrote {shown}.", is_error=True)
        return 0
    findings = evaluate(tests, ledgers, build)
    code = check_report.emit(findings, CHECK, warnings_dir)
    check_report.write_line(summary(tests, records, findings), is_error=True)
    return code


# ── The self-test ──────────────────────────────────────────────────

# A planted script test: it exits with the status of its argument.
PLANTED_SCRIPT = "import sys\nsys.exit(int(sys.argv[1]))\n"


def entry(name: str, *command: str) -> ctest_reader.TestEntry:
    """Return a planted test that line 7 of a planted CMake file registers."""
    return ctest_reader.TestEntry(name, command, {}, "/s/test/CMakeLists.txt", 7)


def self_test(cmake: str, ctest: str) -> int:
    """Run the cases of the self-test (assay.core.testing) with GITHUB_ACTIONS removed.

    Returns:
        0 when each case holds, 2 when a case does not hold
    """

    def project_cases(expect: testing.Expect) -> None:
        with tempfile.TemporaryDirectory(prefix="check-test-time-") as work:
            through_ctest(Path(work), cmake, ctest, expect)

    with check_report.github_actions(False):
        status = testing.run("guard.legacy.check_test_time", [], (planted_cases, project_cases))
    return 0 if status == ExitCode.OK else 2


def planted_cases(expect: testing.Expect) -> None:
    """Plant each finding of a registered test and of a ledger row."""
    launcher = ("/usr/bin/python3", "-S", "/s/utils/bin/loop", "test-step", "--name")
    tests = [
        entry("launched", *launcher, "launched", "/usr/bin/python3", "/s/check.py"),
        entry("test_arena", "/usr/bin/python3", "-S", "/s/utils/bin/loop", "test-step", "/b/test/test_arena"),
        entry("neg_fixture", "/usr/bin/python3", "-S", "/s/utils/bin/loop", "fixture", "/b", "/s/n.cpp"),
        entry("bare", "/usr/bin/python3", "/s/check.py"),
        entry("named_loop", "/usr/bin/python3", "/s/utils/bin/loop", "derive", "--check"),
    ]
    found = evaluate(tests, {}, Path("/b"))
    expect(
        "a test that runs neither the launcher nor the fixture driver is an error at its place, and the launcher, "
        "an executable behind the launcher and a fixture are not",
        [(item.entity, item.kind, item.line) for item in found]
        == [("bare", "unjudged", 7), ("named_loop", "unjudged", 7)],
    )
    found = evaluate(
        tests[:3], {"test-time": {"gone": Row(4, 40.0, "r")}, "test-memory": {"launched": Row(5, 2.0, "r")}}, Path("/b")
    )
    expect(
        "a ledger row of a test that the build does not register is an error at its line, and a row of a "
        "registered test is not",
        [(item.entity, item.kind, item.line) for item in found] == [("gone", "unknown", 4)],
    )


def through_ctest(work: Path, cmake: str, ctest: str, expect: testing.Expect) -> None:
    """Configure a scratch project with cmake/TestLauncher.cmake, run it with ctest, and check each verdict.

    The planted registries make each verdict independent of the speed of the host: the thresholds 0 and 0 make
    each test an error, 0 and 1000 a warning, and 1000 and 1000 no finding.
    """
    source, build, ledgers = work / "source", work / "build", work / "ledgers"
    for directory in (source, ledgers):
        directory.mkdir(parents=True)
    script = source / "planted.py"
    script.write_text(PLANTED_SCRIPT, encoding="utf-8")
    python = sys.executable
    (source / "CMakeLists.txt").write_text(
        "cmake_minimum_required(VERSION 3.29)\nproject(planted NONE)\nenable_testing()\n"
        f'set(CRUCIBLE_PYTHON3 "{python}")\ninclude("{ROOT / "cmake" / "TestLauncher.cmake"}")\n'
        f'crucible_script_test(NAME planted_script COMMAND "{python}" "{script}" 0)\n'
        f'crucible_script_test(NAME planted_wrapped COMMAND "${{CMAKE_COMMAND}}" -E env A=1 "{python}" "{script}" 0)\n'
        f'add_test(NAME planted_bare COMMAND "{python}" "{script}" 0)\n',
        encoding="utf-8",
    )
    cmake_reader.request(build)
    configured = proc.run(proc.Command([cmake, "-S", str(source), "-B", str(build)], timeout_s=proc.TOOL_S))
    expect(
        "a scratch project that includes cmake/TestLauncher.cmake configures",
        configured.exit == 0 and not (build / "CTestCustom.cmake").exists(),
    )
    if configured.exit != 0:
        check_report.write_line(configured.out + configured.err)
        return
    (build / cost_meter.KIND_FILE).write_text("x86_64-debug-asan\n", encoding="utf-8")
    registry = work / "registry.txt"
    warnings_file = build / "check-warnings" / f"{CHECK}.planted_script.txt"

    def plant(warn: float, error: float) -> dict[str, str]:
        registry.write_text(
            rule_registry.planted(
                [
                    rule_registry.measured_rule(CHECK, warn, error, "s", "the wall time"),
                    rule_registry.measured_rule("test-memory", 1000, 1000, "GB", "the peak memory"),
                ]
            ),
            encoding="utf-8",
        )
        environment = dict(os.environ, **{REGISTRY_ENV: str(registry), LEDGERS_ENV: str(ledgers)})
        environment.pop(cost_meter.GITHUB_ACTIONS_ENV, None)
        environment.pop("CRUCIBLE_TEST_LAUNCHED", None)
        return environment

    def ctest_run(warn: float, error: float, *arguments: str) -> tuple[int, str]:
        # -V shows the output of each test, which holds the lines of the judge.
        done = proc.run(
            proc.Command(
                [ctest, "--test-dir", str(build), "-j3", "-V", *arguments],
                timeout_s=proc.TOOL_S,
                env=plant(warn, error),
            )
        )
        return done.exit, done.out + done.err

    def check(*arguments: str) -> tuple[int, str]:
        done = proc.run(
            proc.Command(
                [
                    python,
                    str(ROOT / "utils" / "bin" / "guard"),
                    "check",
                    "--rule",
                    CHECK,
                    "--build-dir",
                    str(build),
                    *arguments,
                ],
                timeout_s=proc.TOOL_S,
                env=plant(5, 30),
            )
        )
        return done.exit, done.out + done.err

    status, said = check()
    expect(
        "the check refuses the test that runs with no judge, at its call in the CMake file, and no wrapped test",
        status == 1
        and "the test planted_bare runs" in said
        and "CMakeLists.txt:8: error" in said
        and "planted_script runs" not in said
        and "planted_wrapped runs" not in said,
    )
    status, said = ctest_run(0, 0, "-R", "planted_script|planted_wrapped")
    expect(
        "a script test over the error threshold fails itself, with an error line that names the test",
        status != 0
        and "error: [test-time] the test planted_script took" in said
        and "error: [test-time] the test planted_wrapped took" in said,
    )
    results = {name: record.result for name, record in ctest_reader.costs(build).items()}
    expect(
        "each script test has a record with its result",
        results == {"planted_script": "rejected", "planted_wrapped": "rejected"},
    )
    status, said = ctest_run(0, 1000, "-R", "planted_script")
    expect(
        "a script test over the warning threshold passes with a warning, and writes its warnings file",
        status == 0 and "warning: [test-time] the test planted_script took" in said and warnings_file.is_file(),
    )
    status, said = ctest_run(1000, 1000, "-R", "planted_script")
    expect(
        "a script test under the thresholds gives no finding and removes its warnings file",
        status == 0 and "[test-time]" not in said and not warnings_file.exists(),
    )
    ctest_run(0, 0, "-R", "planted_script")
    done = proc.run(
        proc.Command(
            [
                python,
                str(ROOT / "utils" / "bin" / "guard"),
                "check",
                "--rule",
                CHECK,
                "--build-dir",
                str(build),
                "--write",
            ],
            timeout_s=proc.TOOL_S,
            env=plant(0, 0),
        )
    )
    ledger = Path(cost_meter.ledger_file(CHECK, str(ledgers)))
    written = ledger.read_text(encoding="utf-8") if ledger.is_file() else ""
    expect(
        "--write gives each test over the error threshold a row from its record, and keeps the head comment",
        done.exit == 0
        and "| x86_64-debug-asan |" in written
        and "| planted_script | " + DEFAULT_REASON in written
        and written.startswith("# The ledger of the measured rule test-time"),
    )
    status, said = ctest_run(0, 0, "-R", "planted_script")
    expect(
        "a ledger row of the kind admits the script test over the error threshold, with a warning",
        status == 0 and DEFAULT_REASON in said and "error: [test-time]" not in said,
    )
    ledger.write_text(written + "- | x86_64-debug-asan | 40 | planted_gone | a planted reason\n", encoding="utf-8")
    status, said = check()
    expect(
        "the check refuses a ledger row of a test that the build does not register",
        status == 1 and "names the test planted_gone, which the build does not register" in said,
    )
    ledger.unlink()
    status, said = ctest_run(0, 0, "--show-only")
    expect("ctest --show-only runs no test and writes no record", status == 0 and "[test-time]" not in said)


def main(argv: list[str]) -> int:
    """Run the check, the write of the ledgers, or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    parser = cli.parser("guard check --rule test-time", __doc__.split("\n", 1)[0])
    check_report.add_arguments(parser)
    parser.add_argument("--build-dir", type=Path, help="the build directory")
    parser.add_argument("--ctest", help="the ctest program of the self-test (default: the ctest of PATH at the pin)")
    parser.add_argument("--cmake", help="the cmake program of the self-test (default: the cmake of PATH at the pin)")
    parser.add_argument("--write", action="store_true", help="write the rows of the kind of the build again")
    parser.add_argument("--self-test", action="store_true", help="plant each finding and run a scratch project")
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        try:
            cmake = arguments.cmake or cmake_pin.pinned_program("cmake")
            ctest = arguments.ctest or cmake_pin.pinned_program("ctest")
        except cmake_pin.PinError as error:
            check_report.write_line(f"check-test-time --self-test: {error}", is_error=True)
            return 2
        return self_test(cmake, ctest)
    if arguments.build_dir is None:
        parser.error("give --build-dir BUILD_DIR, or --self-test")
    return run(arguments.build_dir.resolve(), arguments.warnings_dir, arguments.write)
