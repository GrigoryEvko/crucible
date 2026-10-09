"""loop test-step — run one test, record its cost, and hold its budget.

cmake/TestLauncher.cmake puts this subcommand in front of each test of the
tree, apart from a negative fixture.  CMAKE_TEST_LAUNCHER puts it in front of
a test of an executable target:

    python3 -S utils/bin/loop test-step --warnings-dir DIR --build-dir BUILD EXECUTABLE ARG...

The function crucible_script_test() puts it in front of each script test,
with the name of the test:

    python3 -S utils/bin/loop test-step --warnings-dir DIR --build-dir BUILD --name NAME PROGRAM ARG...

CMake gives a launcher no test name, and each executable test of the tree has
the name of its target.  The name of a test with no --name is the file name
of its program.  The driver of a negative fixture, `loop fixture`, writes the
same record with the same judge.  A fixture then pays no second start of
Python.

WHAT THE SUBCOMMAND DOES
    It runs the command through utils/assay/core/meter.py, with the same
    standard streams.  The environment of the command gets
    CRUCIBLE_TEST_LAUNCHED with the name of the test.  A fixture driver in the
    command then gives the record and the judge to this subcommand.  It
    writes the record BUILD/test-records/NAME.test.cost (assay.core.record,
    THE RECORDS), and it ends as the command ended: with its status, or on its
    signal.  With no --build-dir, the build directory is the nearest
    directory above the program with a build-kind.txt.  With no such
    directory, the subcommand writes no record.  A failure to measure, to
    read the registry or to write the record does not change the status.

CORE DUMPS
    A death test forks a child that aborts, and the parent waits for the
    child.  The kernel writes the core dump of the child before the wait
    ends.  On the build host, kernel.core_pattern sends each core dump to
    systemd-coredump, and the kernel then ignores a core limit of 0.  One
    dump costs approximately 0.2 s of wall time in the test and 0.25 s of
    CPU time in the handler, and it leaves a core in the store of the
    handler.  At a soft core limit of exactly 1 byte, the kernel writes no
    core dump, to a handler or to a file, and a debugger can still attach.
    The sanitizer runtimes set the same limit.
      * Before the subcommand starts the test, it sets its own soft
        RLIMIT_CORE to 1 byte, so the test and each child of the test get
        that limit.  The hard limit does not change.  At a hard limit of 0,
        the limit stays 0, and a handler still gets each core.
      * CRUCIBLE_TEST_CORES=keep keeps the core limit of the environment, for
        a developer who wants the core of a crash.  For each other value of
        the variable, the subcommand stops the core dumps and prints a note.
      * The subcommand ends on the signal of the test with a soft limit of 1
        byte also when the test keeps its cores.  The core of the launcher
        itself has no use.

THE MEASURED RULES
    test-time is the wall time of the test, and test-memory is the peak
    resident memory of its largest process (the measured rules of
    utils/registry.txt).
      * Over the warning threshold of a rule, the subcommand prints a warning
        line in the format of utils/assay/core/report.py and writes it to
        DIR/RULE.NAME.txt, so the tests that run at the same time do not write
        one file.  Under the warning threshold, it removes that file.  The
        line names the program when the program is a file of the build
        directory, and the record of the test for a script test.
      * Over the error threshold, it prints an error line, and a test that
        passed fails with status 1, unless a row of the ledger
        utils/ledgers/RULE.txt admits the test for the kind of the build:
        '- | KIND | VALUE | TEST | REASON', with VALUE the measure when the
        row was written, in the unit of the rule (meter.py, THE REGISTRY AND
        THE LEDGERS).  An admitted test gives a warning.  A row whose test
        stays at or below the error threshold is an error too, so that the
        row goes.  A ledger that does not exist has no row, and the
        subcommand then imports no ledger module.
    Memory is almost the same on each run, so its error threshold sits above
    the largest measure.  Wall time depends on the load of the host, so its
    error threshold sits above the noise that the commit of the threshold
    states, and the timeout of the test preset is the hard stop.  In a build of a
    kind with tsan, a wall time over the error threshold gives a warning
    only: ThreadSanitizer serializes the threads of a test, and one test took
    15 s in one run and 91 s in the next on the shared host.  On a GitHub
    runner, each time error is a warning that says that it was demoted
    (utils/assay/core/meter.py, A CI RUNNER), and a memory error stays an
    error.
    A test that failed keeps its status, so a failure, a skip code and a
    WILL_FAIL test keep the meaning that ctest gives them.  A WILL_FAIL test
    over its budget therefore does not fail.  No test of the tree has
    WILL_FAIL at this time.

The variables CRUCIBLE_TEST_REGISTRY and CRUCIBLE_TEST_LEDGERS name another
registry and another directory of ledgers, which holds RULE.txt for each
planted ledger, for its test.
CRUCIBLE_TEST_CORES is the variable of CORE DUMPS above.

This module holds no self-test, because each test pays the import of the
module.  `guard selftest tests.loop.test_teststep` runs its test.
"""

import os
import sys

from assay.core import budget as cost_budget
from assay.core import meter as cost_meter
from assay.core import record as cost_record

# The name Callable is only for the types, and a test does not pay the import of collections.abc.
TYPE_CHECKING = False
if TYPE_CHECKING:
    from collections.abc import Callable

ROWS = ("test-time", "test-memory")
REGISTRY_ENV = "CRUCIBLE_TEST_REGISTRY"
LEDGERS_ENV = "CRUCIBLE_TEST_LEDGERS"
CORES_ENV = "CRUCIBLE_TEST_CORES"
CORES_KEEP = "keep"
# The variable that tells a fixture driver in the command that this subcommand records and judges the test.
LAUNCHED_ENV = "CRUCIBLE_TEST_LAUNCHED"
# The options before the command, each with one value.
OPTIONS = ("--warnings-dir", "--build-dir", "--name")
# The end of the path of the master, and the subcommand that this module is.  A test command can name the master of
# an export of the tree.  A reader then finds the master by the end of its path.
MASTER = "utils/bin/loop"
SUBCOMMAND = "test-step"
USAGE = (
    "usage: loop test-step [--warnings-dir DIR] [--build-dir BUILD] [--name NAME] PROGRAM [ARG]... | "
    "guard selftest tests.loop.test_teststep"
)
# The characters of a test name: the characters of a writer key of utils/assay/core/report.py, which the test of this
# module holds equal to WRITER_KEY.  A name does not start with '-' or '.'.
NAME_CHARACTERS = frozenset("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-")
# The kernel writes no core dump at this soft limit, also when
# kernel.core_pattern names a handler (fs/coredump.c).
NO_CORE_LIMIT_BYTES = 1
# The directory utils.  This module is utils/assay/loop/teststep.py.  It calculates the paths with os.path and not
# with assay.core.paths, because each test imports it.
UTILS = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# The registry of the measured rules, utils/registry.txt.
REGISTRY_FILE = os.path.join(UTILS, "registry.txt")


def stop_core_dumps() -> None:
    """Set the soft core limit of this process, and of each process that it starts after the call, to 1 byte.

    The hard limit does not change.  A hard limit of 0 is lower than 1 byte,
    so the call then changes nothing.  A failure of the call changes nothing
    either, because a core dump costs time and does not change a result.
    """
    import resource

    try:
        _, hard = resource.getrlimit(resource.RLIMIT_CORE)
        if hard == resource.RLIM_INFINITY or hard >= NO_CORE_LIMIT_BYTES:
            resource.setrlimit(resource.RLIMIT_CORE, (NO_CORE_LIMIT_BYTES, hard))
    except (OSError, ValueError):
        pass


def keeps_cores() -> bool:
    """Tell whether the environment keeps the core dumps of the test, and print a note for a value that is not known.

    Returns:
        True when CRUCIBLE_TEST_CORES is "keep"
    """
    value = os.environ.get(CORES_ENV)
    if value is None or value == CORES_KEEP:
        return value == CORES_KEEP
    print(f"loop test-step: {CORES_ENV} is {value!r}, and only {CORES_KEEP!r} keeps the core dumps of the test",
          file=sys.stderr)
    return False


def ledger_row(row: str, name: str, kind: str | None, ledgers: str | None = None) -> tuple[str, str] | None:
    """Return the place and the reason of the ledger row that names a test for the kind of the build.

    Args:
        row: test-time or test-memory
        name: The name of the test
        kind: The kind of the build, or None when it is not known
        ledgers: The directory of the ledgers, or None for the directory of CRUCIBLE_TEST_LEDGERS, else of the tree

    Returns:
        The place of the row (path:line) and its reason, or None
    """
    if kind is None:
        return None
    directory = ledgers if ledgers is not None else os.environ.get(LEDGERS_ENV)
    ledger = cost_meter.ledger_file(row, directory)
    found, shown, _ = cost_budget.row_of(row, cost_meter.ITEM_NAME, ledger, kind, name)
    return None if found is None else (f"{shown}:{found.line}", found.reason)


def judge(name: str, run: cost_meter.Measurement, budget: dict[str, tuple[float, float]],
          kind: str | None, ledgers: str | None = None) -> list[tuple[str, str, str]]:
    """Compare the measures of one test with its measured rules.

    The witness of test-memory judges the planted record of a test with this
    function, and with the ledgers of the copy of a plant
    (assay.guard.engines.buildsys.held).

    Args:
        name: The name of the test
        run: The measurement of the command
        budget: The thresholds of the measured rules that the registry gives
        kind: The kind of the build, or None
        ledgers: The directory of the ledgers (ledger_row()), or None

    Returns:
        One (level, rule, message) for each rule with a finding.  On a GitHub runner, a time error is a warning
    """
    findings = []

    def add(level: str, row: str, message: str) -> None:
        shown_level, shown_message = cost_meter.ci_verdict(level, row, message)
        findings.append((shown_level, row, shown_message))

    for row, value, unit, text in (("test-time", run.wall_s, "s", f"{run.wall_s:.2f} s of wall time"),
                                   ("test-memory", run.peak_gb, "GB", f"{run.peak_gb:.3f} GB of peak memory")):
        if row not in budget:
            continue
        warn, error = budget[row]
        admitted = ledger_row(row, name, kind, ledgers)
        said = f"the test {name} took {text}"
        found = cost_budget.verdict(value, warn, error, admitted is not None)
        if admitted is not None and found == cost_budget.UNNEEDED:
            add("error", row, f"{said}, no more than the error threshold of {error:g} {unit}.  Remove its row "
                              f"{admitted[0]}")
        elif admitted is not None:
            add("warning", row, f"{said}, over the error threshold of {error:g} {unit}, and the row {admitted[0]} "
                                f"admits it: {admitted[1]}")
        elif found == cost_budget.ERROR and row == "test-time" and kind is not None and "-tsan" in kind:
            add("warning", row, f"{said}, over the error threshold of {error:g} {unit}.  The build kind {kind} "
                                f"budgets no wall time, so the timeout of the test preset is the hard stop")
        elif found == cost_budget.ERROR:
            add("error", row, f"{said}, over the error threshold of {error:g} {unit}, and no row of "
                              f"utils/ledgers/{row}.txt admits it for the kind {kind}.  Make the test smaller, "
                              f"or split it into tests over disjoint cases")
        elif found == cost_budget.WARNING:
            add("warning", row, f"{said}, over the warning threshold of {warn:g} {unit}")
    return findings


def report(name: str, place: str, findings: list[tuple[str, str, str]], warnings_dir: str | None) -> None:
    """Print each finding, and write or remove the warnings file of this test for each row.

    utils/assay/core/report.py is imported only when a finding or a stale
    warnings file needs it, because each test pays for the import.

    Args:
        name: The name of the test, the writer key of its warnings files
        place: The path that a finding names
        findings: The findings of judge()
        warnings_dir: The warnings directory, or None
    """
    stale = warnings_dir is not None and any(
        os.path.exists(os.path.join(warnings_dir, f"{row}.{name}.txt")) for row in ROWS)
    if not findings and not stale:
        return
    from pathlib import Path

    from assay.core import report as check_report

    for row in ROWS:
        lines = [check_report.Finding(level, place, 0, row, message)
                 for level, finding_row, message in findings if finding_row == row]
        for line in lines:
            print(line.text(), file=sys.stderr)
        if warnings_dir is not None:
            check_report.write_warnings(Path(warnings_dir), row, [line for line in lines if line.level == "warning"],
                                        name)


def is_test_name(name: str) -> bool:
    """Return True when a text can name a test: a writer key of utils/assay/core/report.py, which is also a file name."""
    return bool(name) and not name.startswith(("-", ".")) and set(name) <= NAME_CHARACTERS


class Options:
    """The options of the command line: each one is None when the command line does not give it."""

    __slots__ = ("build_dir", "name", "warnings_dir")

    def __init__(self) -> None:
        """Start with no option."""
        self.warnings_dir: str | None = None
        self.build_dir: str | None = None
        self.name: str | None = None


def read_options(argv: list[str]) -> tuple[Options, list[str]] | None:
    """Divide the words of the command line into the options and the command.

    Args:
        argv: [OPTION VALUE]... PROGRAM [ARG]...

    Returns:
        The options, and the command.  None when an option has no value, an option comes two times, or no command
        follows the options
    """
    options = Options()
    index = 0
    while index < len(argv) and argv[index] in OPTIONS:
        field = argv[index].removeprefix("--").replace("-", "_")
        if index + 1 >= len(argv) or getattr(options, field) is not None:
            return None
        setattr(options, field, argv[index + 1])
        index += 2
    return (options, argv[index:]) if index < len(argv) else None


def launched(command: list[str]) -> list[str]:
    """Return the command that a test runs behind this launcher, or the command itself when no launcher runs it.

    A test command of the tree is `python3 -S SRC/utils/bin/loop test-step [OPTION VALUE]... PROGRAM [ARG]...`.
    Complexity: linear in the words of the command.
    """
    for index, word in enumerate(command[:-1]):
        if word.endswith(MASTER) and command[index + 1] == SUBCOMMAND:
            read = read_options(command[index + 2:])
            return read[1] if read is not None else command
    return command


def read_budget() -> dict[str, tuple[float, float]]:
    """Read the thresholds of the rules of the launcher from the registry, or give none when it cannot be read."""
    try:
        return cost_meter.budget_rows(os.environ.get(REGISTRY_ENV) or REGISTRY_FILE, ROWS)
    except (OSError, ValueError):
        return {}


def safely[T](action: Callable[[], T], fallback: T) -> T:
    """Return the value of one step of the judge, or the fallback when the step fails.

    A failure to read a ledger, to report a finding or to write the record
    does not change the status of a test (judge_and_record()).
    """
    try:
        return action()
    except BaseException:  # noqa: BLE001
        return fallback


def judge_and_record(name: str, program: str, finding_path: str, build_dir: str | None,
                     run: cost_meter.Measurement, warnings_dir: str | None,
                     budget: dict[str, tuple[float, float]] | None = None) -> int:
    """Judge one test, report each finding, write its record, and return the status that the test ends with.

    `loop test-step` and `loop fixture` call this function.  The two then
    give the same record and the same verdict.  A failure to read the
    registry, to report or to write the record does not change the status.

    Args:
        name: The name of the test
        program: The program of the test, for the record
        finding_path: The path that a finding names
        build_dir: The build directory, or None for no record and no build kind
        run: The measurement of the test, with its status
        warnings_dir: The warnings directory, or None
        budget: The thresholds of the rules, or None to read them from the registry (read_budget())

    Returns:
        The status of the test, or 1 when the judge rejects a test with the status 0
    """
    status = run.exit_code
    result = "passed" if status == 0 else "failed"
    thresholds = read_budget() if budget is None else budget
    kind = cost_meter.read_kind(build_dir) if build_dir else None
    no_findings: list[tuple[str, str, str]] = []
    findings = safely(lambda: judge(name, run, thresholds, kind), no_findings)
    if status == 0 and any(level == "error" for level, _, _ in findings):
        status, result = 1, "rejected"
    safely(lambda: report(name, finding_path, findings, warnings_dir), None)
    if build_dir is not None:
        record = cost_record.TestRecord(name, result, program, run.exit_code, cost_record.measured(run))
        safely(lambda: cost_record.write(cost_record.test_record_file(build_dir, name), record), None)
    return status


def main(argv: list[str]) -> int:
    """Run the test of the command line, and end as it ended.

    Args:
        argv: [--warnings-dir DIR] [--build-dir BUILD] [--name NAME] PROGRAM [ARG]...

    Returns:
        2 on a usage error.  Else the function ends the process as the test ended
    """
    parsed = read_options(argv)
    name = "" if parsed is None else parsed[0].name or os.path.basename(parsed[1][0])
    if parsed is None or not is_test_name(name):
        problem = "" if parsed is None else (f"the test name {name!r} is not a word of letters, digits, '_', '.' "
                                             f"and '-' that starts with a letter, a digit or '_'.  ")
        print(f"loop test-step: {problem}{USAGE}", file=sys.stderr)
        return 2
    options, command = parsed
    program = os.path.abspath(command[0]) if os.sep in command[0] else command[0]
    build_dir = options.build_dir
    if build_dir is None and os.path.isabs(program):
        build_dir = cost_meter.build_dir_of(program)
    if build_dir is not None:
        build_dir = os.path.abspath(build_dir)
    is_built = build_dir is not None and program.startswith(build_dir + os.sep)
    place = program if is_built or build_dir is None else cost_record.test_record_file(build_dir, name)
    # The launcher reads the registry before the test runs.  The parse then frees its memory before the start of the
    # test, and the start uses that memory again, with fewer page faults.
    budget = read_budget()
    if not keeps_cores():
        stop_core_dumps()
    # The launcher puts the variable in its own environment, which the test gets.  A copy of the environment for the
    # child costs more.
    os.environ[LAUNCHED_ENV] = name
    try:
        run = cost_meter.measure(command)
    except OSError as error:
        print(f"loop test-step: the test {command[0]} cannot start: {error}", file=sys.stderr)
        sys.exit(127)
    status = judge_and_record(name, program, place, build_dir, run, options.warnings_dir, budget)
    stop_core_dumps()
    cost_meter.end_like(status)
    return status
