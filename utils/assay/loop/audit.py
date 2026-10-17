"""loop audit — compare the tests that `loop test` ran with its plan, and add the call profiles of a run.

THE PLAN AND THE RUN
    Each call of `loop test` writes its plan, BUILD_DIR/affected-tests/plan.json
    (THE PLAN of assay.loop.affected).  For each test, the plan holds the kind,
    the reason, the shared key and the skip.  The merged JUnit report of the
    call, BUILD_DIR/affected-tests/junit.xml, gives the tests that ran.  The
    record of each test gives the wall time of its last run
    (assay.readers.ctest).  The name of the configuration of a build directory
    comes from its file build-configuration.txt.

THE FINDINGS
    missed     A test that the plan selects and that did not run.  For
               example, ctest did not start it.  A missed test is an error,
               and the command ends with ExitCode.FINDINGS.
    unplanned  A test that ran and that the plan skips.
    again      A shared key that ran in two build directories or more.  The
               shared key holds each input of the test (THE SHARED STORE of
               assay.loop.affected).  The runs after the first run of a key
               then do the same work again.
    unkeyed    A test with no shared key that ran in two build directories
               or more, such as a negative fixture or a script test.  The
               report cannot tell if those runs do the same work.
    The report gives the count and the test time of each kind of finding.  It
    also gives the tests with the largest test time.  For a key, the test
    time of the runs after the first run is the sum of their times less the
    largest time.

THE CALL PROFILES
    With `--calls DIR`, the command adds the call profiles that a run with
    CALL_PROFILE_ENV of assay.core.meter wrote into DIR (THE RECORD of
    assay.core.calls).  The report gives these totals:
      * For each command, such as `guard selftest` or `loop test-step`: the
        processes, the wall time, the CPU time and the import time.  The
        import time of a process is the cumulative time of _find_and_load of
        the import machinery of Python.
      * The commands that ran two times or more with the same words.  Each
        run after the first run does the same work again.
      * The functions with the largest own time, the sum over the processes.
      * The functions of the package that ran in two processes or more, by
        their cumulative time.  Such a function can read the same file in
        each process.  The list does not hold an entry point, such as main or
        the body of a module.
    The times of a function are wall times, which hold the waits of the
    function.  The path of a master in the words of a process gives the
    directory utils of its tree.  The report writes a file of that directory
    as PACKAGE_MARK and its path in the directory, and two trees then give
    one function.  `--command TEXT` keeps only the processes of one command,
    such as "loop fixture".

`guard selftest tests.loop.test_audit` does a test of the module.

Complexity: linear in the tests of the build directories and in the rows of
the call profiles.
"""

import json
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import calls as call_profile
from assay.core import cli
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.loop import affected
from assay.readers import ctest as ctest_reader

# The statuses of a test that ran (assay.readers.ctest, THE STATUS OF A TEST).
RAN = frozenset({ctest_reader.TestStatus.RUN, ctest_reader.TestStatus.FAIL, ctest_reader.TestStatus.SKIPPED})
# The file of the name of the configuration of a build directory.
CONFIGURATION_FILE = "build-configuration.txt"
# The number of names that the report gives for each finding.
SHOWN = 12
# The function of the import machinery of Python.  Its cumulative time is the import time of a process.
IMPORT_FUNCTION = "_find_and_load"
# The names of the entry points that the list of the work in two processes does not hold (THE CALL PROFILES).
ENTRY_NAMES = frozenset(
    {"<module>", "main", "run", "run_module", "run_group", "forked_failures", "_run_child", "_fork_action"}
)
# The mark of the directory utils of a tree in the path of a function (THE CALL PROFILES).
PACKAGE_MARK = "<utils>"
# The module of the profile itself, which the list of the work in two processes does not hold.
PROFILE_MODULE = f"{PACKAGE_MARK}/assay/core/calls.py"
# The fields of a call profile that hold a number (THE RECORD of assay.core.calls).
NUMBER_FIELDS = ("pid", "parent", "start", "end", "cpu_s")

# One row of a call profile: file, line, name, calls, primitive calls, own time, cumulative time (assay.core.calls).
type FunctionRow = tuple[str, int, str, int, int, float, float]
type FunctionKey = tuple[str, int, str]


@dataclass(frozen=True, slots=True)
class Run:
    """One test that ran in one build directory: its configuration, its shared key, its kind and its wall time."""

    configuration: str
    name: str
    shared: str
    kind: str
    wall_s: float


@dataclass(frozen=True, slots=True)
class BuildAudit:
    """The plan and the run of one build directory (THE PLAN AND THE RUN)."""

    configuration: str
    planned: int
    selected: tuple[str, ...]
    ran: tuple[Run, ...]
    missed: tuple[str, ...]
    unplanned: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class CallProfile:
    """One call profile: the words of the command, the process, the times and the rows of the functions."""

    argv: tuple[str, ...]
    pid: int
    parent: int
    start: float
    end: float
    cpu_s: float
    functions: tuple[FunctionRow, ...]


@dataclass(frozen=True, slots=True)
class FunctionTotal:
    """The sum of one function over the call profiles: its calls, its own time, its cumulative time, its processes."""

    calls: int
    own_s: float
    cumulative_s: float
    processes: int


@dataclass(frozen=True, slots=True)
class CommandTotal:
    """The sum of one command over the call profiles: its processes, wall time, CPU time and import time."""

    processes: int
    wall_s: float
    cpu_s: float
    import_s: float


class AuditError(Exception):
    """The plan, the JUnit report or the records of a build directory, or a call profile, cannot be read."""


def configuration_of(build_dir: Path) -> str:
    """Return the name of the configuration of a build directory, or the name of the directory with no such file."""
    try:
        return (build_dir / CONFIGURATION_FILE).read_text(encoding="utf-8").strip() or build_dir.name
    except OSError:
        return build_dir.name


def audit_build(build_dir: Path) -> BuildAudit:
    """Compare the plan of the last call in one build directory with the tests that ran (THE FINDINGS).

    Raises:
        AuditError: If the plan, the JUnit report or a record cannot be read
    """
    state_dir = build_dir / affected.STATE_DIRECTORY
    try:
        plan = affected.read_plan(state_dir / affected.PLAN_FILE)
        statuses = ctest_reader.results(state_dir / "junit.xml") if (state_dir / "junit.xml").is_file() else {}
        records = ctest_reader.costs(build_dir)
    except (affected.RunnerError, ctest_reader.CtestError) as problem:
        raise AuditError(f"{build_dir}: {problem}") from None
    configuration = configuration_of(build_dir)
    selected: list[str] = []
    missed: list[str] = []
    unplanned: list[str] = []
    ran: list[Run] = []
    for entry in plan:
        status = statuses.get(entry.name)
        is_selected = not entry.is_skipped and entry.kind != affected.KIND_UNNAMED
        has_run = status in RAN
        if is_selected:
            selected.append(entry.name)
            if not has_run and status is not ctest_reader.TestStatus.DISABLED:
                missed.append(entry.name)
        elif has_run:
            unplanned.append(entry.name)
        if has_run:
            record = records.get(entry.name)
            wall_s = record.cost.wall_s if record is not None else 0.0
            ran.append(Run(configuration, entry.name, entry.shared, entry.kind, wall_s))
    return BuildAudit(configuration, len(plan), tuple(selected), tuple(ran), tuple(missed), tuple(unplanned))


def groups_in_two_builds(runs: Sequence[Run], is_by_shared_key: bool) -> dict[str, list[Run]]:
    """Group the runs by their shared key, or by their name, and keep each group of two build directories or more.

    Complexity: linear in the runs.
    """
    groups: dict[str, list[Run]] = {}
    for run in runs:
        key = run.shared if is_by_shared_key else run.name
        if key:
            groups.setdefault(key, []).append(run)
    return {key: group for key, group in groups.items() if len({run.configuration for run in group}) > 1}


def seconds_after_first(group: Sequence[Run]) -> float:
    """Return the test time of the runs after the first run of a group: the sum of the times less the largest time."""
    return sum(run.wall_s for run in group) - max(run.wall_s for run in group)


def report_groups(label: str, groups: dict[str, list[Run]]) -> None:
    """Write the count and the test time of the runs after the first run of each group, and the largest groups."""
    runs_after_first = sum(len(group) - 1 for group in groups.values())
    seconds = sum(seconds_after_first(group) for group in groups.values())
    kinds: dict[str, int] = {}
    for group in groups.values():
        kinds[group[0].kind] = kinds.get(group[0].kind, 0) + len(group) - 1
    shown_kinds = ", ".join(f"{kind} {count}" for kind, count in sorted(kinds.items(), key=lambda item: -item[1]))
    check_report.write_line(
        f"loop audit: {len(groups)} {label} ran in two build directories or more: {runs_after_first} runs after the "
        f"first run, {seconds:.1f} s of test time" + (f" (by kind: {shown_kinds})" if shown_kinds else "")
    )
    for group in sorted(groups.values(), key=seconds_after_first, reverse=True)[:SHOWN]:
        configurations = " ".join(sorted(run.configuration for run in group))
        check_report.write_line(
            f"loop audit:   {group[0].name}: {seconds_after_first(group):.1f} s more in {configurations}"
        )


def audit(build_dirs: Sequence[Path]) -> ExitCode:
    """Write the findings of the build directories, and return FINDINGS when a selected test did not run (THE FINDINGS).

    Raises:
        AuditError: If the plan, the JUnit report or a record of a build directory cannot be read
    """
    audits = [audit_build(build_dir) for build_dir in build_dirs]
    for item in audits:
        check_report.write_line(
            f"loop audit: {item.configuration}: {item.planned} tests in the plan, {len(item.selected)} selected, "
            f"{len(item.ran)} ran, {len(item.missed)} missed, {len(item.unplanned)} unplanned"
        )
        for label, names in (("missed", item.missed), ("unplanned", item.unplanned)):
            if names:
                more = f" and {len(names) - SHOWN} more" if len(names) > SHOWN else ""
                check_report.write_line(f"loop audit:   {label}: {' '.join(names[:SHOWN])}{more}")
    runs = [run for item in audits for run in item.ran]
    report_groups("shared keys", groups_in_two_builds(runs, is_by_shared_key=True))
    unkeyed = [run for run in runs if not run.shared]
    report_groups("tests with no shared key", groups_in_two_builds(unkeyed, is_by_shared_key=False))
    return ExitCode.FINDINGS if any(item.missed for item in audits) else ExitCode.OK


def command_of(argv: Sequence[str]) -> str:
    """Return the command of a process: the name of its master and its subcommand, such as `guard selftest`."""
    return " ".join([Path(argv[0]).name if argv else "?", *argv[1:2]])


def function_row(row: object) -> FunctionRow | None:
    """Return one row of the functions of a call profile, or None when it does not have the shape of a row."""
    if not isinstance(row, list) or len(row) != 7:
        return None
    file, line, name, called, primitive, own_s, cumulative_s = row
    if not (isinstance(file, str) and isinstance(name, str)):
        return None
    if not (isinstance(line, int) and isinstance(called, int) and isinstance(primitive, int)):
        return None
    if not (isinstance(own_s, (int, float)) and isinstance(cumulative_s, (int, float))):
        return None
    return (file, line, name, called, primitive, float(own_s), float(cumulative_s))


def call_profile_of(path: Path, document: object) -> CallProfile:
    """Return the call profile of one file (THE RECORD of assay.core.calls).

    Raises:
        AuditError: If the document does not have the format and the shape of a call profile
    """
    shape = f"the call profile {path} does not have the format {call_profile.FORMAT} and its shape"
    if not isinstance(document, dict) or document.get("format") != call_profile.FORMAT:
        raise AuditError(shape)
    argv, rows = document.get("argv"), document.get("functions")
    numbers = [document.get(key) for key in NUMBER_FIELDS]
    if not isinstance(argv, list) or not isinstance(rows, list):
        raise AuditError(shape)
    values = [float(number) for number in numbers if isinstance(number, (int, float))]
    functions = [function_row(row) for row in rows]
    kept = tuple(row for row in functions if row is not None)
    if len(values) != len(NUMBER_FIELDS) or len(kept) != len(functions):
        raise AuditError(shape)
    pid, parent, start, end, cpu_s = values
    return CallProfile(tuple(str(word) for word in argv), int(pid), int(parent), start, end, cpu_s, kept)


def read_profiles(directory: Path) -> list[CallProfile]:
    """Read each call profile of a directory, in the sequence of the file names (THE RECORD of assay.core.calls).

    Raises:
        AuditError: If the directory or a profile cannot be read, or a profile has a different format or shape
    """
    try:
        paths = sorted(directory.glob(f"*{call_profile.SUFFIX}"))
    except OSError as problem:
        raise AuditError(f"the call profiles in {directory} cannot be read: {problem}") from None
    profiles: list[CallProfile] = []
    for path in paths:
        try:
            document = json.loads(path.read_text(encoding="utf-8"))
        except (OSError, ValueError) as problem:
            raise AuditError(f"the call profile {path} cannot be read: {problem}") from None
        profiles.append(call_profile_of(path, document))
    return profiles


def utils_of(argv: Sequence[str]) -> str:
    """Return the directory utils of the tree of a process, from the path of its master, or an empty text."""
    master = Path(argv[0]) if argv else Path()
    return str(master.parent.parent) if master.parent.name == "bin" and master.is_absolute() else ""


def marked_file(file: str, utils: str) -> str:
    """Return the path of a function with PACKAGE_MARK for the directory utils of its tree (THE CALL PROFILES)."""
    return PACKAGE_MARK + file.removeprefix(utils) if utils and file.startswith(utils + "/") else file


def function_location(key: FunctionKey) -> str:
    """Return the name of a function, with its path in the package or its full path, and its line."""
    file, line, name = key
    return f"{name} ({file.removeprefix(PACKAGE_MARK + '/')}:{line})"


def command_totals(profiles: Sequence[CallProfile]) -> dict[str, CommandTotal]:
    """Return the total of each command of the call profiles (THE CALL PROFILES).

    Complexity: linear in the rows of the call profiles.
    """
    groups: dict[str, list[CallProfile]] = {}
    for profile in profiles:
        groups.setdefault(command_of(profile.argv), []).append(profile)
    return {
        command: CommandTotal(
            len(group),
            sum(profile.end - profile.start for profile in group),
            sum(profile.cpu_s for profile in group),
            sum(row[6] for profile in group for row in profile.functions if row[2] == IMPORT_FUNCTION),
        )
        for command, group in groups.items()
    }


def function_totals(profiles: Sequence[CallProfile]) -> dict[FunctionKey, FunctionTotal]:
    """Return the total of each function of the call profiles (THE CALL PROFILES).

    Complexity: linear in the rows of the call profiles.
    """
    groups: dict[FunctionKey, list[FunctionRow]] = {}
    for profile in profiles:
        utils = utils_of(profile.argv)
        for row in profile.functions:
            groups.setdefault((marked_file(row[0], utils), row[1], row[2]), []).append(row)
    return {
        key: FunctionTotal(
            sum(row[3] for row in rows), sum(row[5] for row in rows), sum(row[6] for row in rows), len(rows)
        )
        for key, rows in groups.items()
    }


def report_calls(directory: Path, command: str = "") -> None:
    """Write the totals of the call profiles of a directory, or of the processes of one command (THE CALL PROFILES).

    Raises:
        AuditError: If the directory or a profile cannot be read
    """
    profiles = [profile for profile in read_profiles(directory) if not command or command_of(profile.argv) == command]
    commands = command_totals(profiles)
    functions = function_totals(profiles)
    check_report.write_line(
        f"loop audit: {len(profiles)} call profiles of {len(commands)} commands, "
        f"{sum(item.wall_s for item in commands.values()):.1f} s of wall time, "
        f"{sum(item.cpu_s for item in commands.values()):.1f} s of CPU time, "
        f"{sum(item.import_s for item in commands.values()):.1f} s of imports"
    )
    for name, total in sorted(commands.items(), key=lambda item: -item[1].cpu_s)[:SHOWN]:
        check_report.write_line(
            f"loop audit:   {name}: {total.processes} processes, {total.wall_s:.1f} s wall, {total.cpu_s:.1f} s CPU, "
            f"{total.import_s:.1f} s imports"
        )
    same_words: dict[tuple[str, ...], list[float]] = {}
    for profile in profiles:
        same_words.setdefault(profile.argv, []).append(profile.cpu_s)
    again = {words: times for words, times in same_words.items() if len(times) > 1}
    check_report.write_line(
        f"loop audit: {len(again)} commands ran two times or more with the same words: "
        f"{sum(len(times) - 1 for times in again.values())} runs after the first run, "
        f"{sum(sum(times) - max(times) for times in again.values()):.1f} s of CPU time"
    )
    for words, times in sorted(again.items(), key=lambda item: -(sum(item[1]) - max(item[1])))[:SHOWN]:
        check_report.write_line(f"loop audit:   {len(times)} runs, {sum(times):.1f} s CPU: {' '.join(words)}")
    check_report.write_line("loop audit: the functions with the largest own time:")
    for key, function in sorted(functions.items(), key=lambda item: -item[1].own_s)[:SHOWN]:
        check_report.write_line(
            f"loop audit:   {function.own_s:.2f} s own, {function.cumulative_s:.2f} s total, {function.calls} calls in "
            f"{function.processes} processes: {function_location(key)}"
        )
    check_report.write_line("loop audit: the functions of the package that ran in two processes or more:")
    shared_work = [
        (key, function)
        for key, function in functions.items()
        if function.processes > 1
        and key[0].startswith(PACKAGE_MARK + "/")
        and key[0] != PROFILE_MODULE
        and key[2] not in ENTRY_NAMES
    ]
    for key, function in sorted(shared_work, key=lambda item: -item[1].cumulative_s)[:SHOWN]:
        check_report.write_line(
            f"loop audit:   {function.cumulative_s:.2f} s in {function.processes} processes, {function.calls} calls: "
            f"{function_location(key)}"
        )


def main(argv: list[str]) -> int:
    """Compare the last call of `loop test` with its plan in each build directory of the command line.

    Args:
        argv: The words after `loop audit`: [--calls DIR] [BUILD_DIR...]

    Returns:
        OK, or FINDINGS when a selected test did not run.  USAGE with no build directory and no --calls.
        MISSING_INPUT when a file of a build directory or a call profile cannot be read
    """
    parser = cli.parser("loop audit", "compare the tests that the last call of loop test ran with its plan")
    parser.add_argument("build_dirs", nargs="*", type=Path, metavar="BUILD_DIR")
    parser.add_argument("--calls", type=Path, metavar="DIR", help="add the call profiles in DIR (THE CALL PROFILES)")
    parser.add_argument(
        "--command", default="", help="keep only the call profiles of one command, such as 'loop fixture'"
    )
    options = parser.parse_args(argv)
    if not options.build_dirs and options.calls is None:
        check_report.write_line(
            "loop audit: give a build directory, or the directory of the call profiles with --calls", is_error=True
        )
        return ExitCode.USAGE
    try:
        status = audit([build_dir.resolve() for build_dir in options.build_dirs]) if options.build_dirs else ExitCode.OK
        if options.calls is not None:
            report_calls(options.calls, options.command)
        return status
    except AuditError as problem:
        check_report.write_line(f"loop audit: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
