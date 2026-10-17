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

THE LINES
    A run with LINES_ENV of assay.core.meter writes the rows of the lines
    (THE LINES of assay.core.calls).  `--lines` writes a table of each
    function with the largest cumulative time, LINE_FUNCTIONS functions, as
    line_profiler does.  `--lines TEXT` writes the table of each function
    with TEXT in its name or its path.  A table has one row for each line of
    the function.  The row holds these fields:
      - The hits, the wall time and the time for each hit
      - The part of the cumulative time of the function
      - The CPU time and the text of the line.
    The sums go over each process of the run.  The text comes from the file
    of the first process that ran the line.  When that file is not
    available, it comes from the same path in the directory utils of this
    tree.

    After the table of a function, the report expands each line into the
    tables of the functions of the package that it calls.  THE CALLS OF A
    LINE of assay.core.calls gives these calls.  A callee gets a table when its calls from
    the line take DRILL_PART or more of the wall time of the first function.
    The report goes down `--depth N` levels, DRILL_DEPTH by default.  A
    callee with a table above gets one line with its location.  A callee on
    the path gets no table.  The report then follows the hot path down into
    the callees with no list of functions.

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
# The number of functions that `--lines` with no text writes (THE LINES).
LINE_FUNCTIONS = 5
# The smallest part of the wall time of the first function, and the levels, of a callee that gets a table (THE LINES).
DRILL_PART = 0.05
DRILL_DEPTH = 3

# The types of the fields of a row of the functions and of a row of the lines (assay.core.calls).
type MeasuredRow = tuple[str, int, str, int, int, float, float]
# One row of a call profile: file, line, name, calls, primitive calls, own time, cumulative time (assay.core.calls).
type FunctionRow = MeasuredRow
type FunctionKey = tuple[str, int, str]
# One row of the lines: file, line, full name, first line of the function, hits, wall time, CPU time (THE LINES).
type LineRow = MeasuredRow
# The calls of a line to one callee (THE CALLS OF A LINE of assay.core.calls).  The fields are the file, the first
# line and the full name of the caller, and the line.  Then come the same three of the callee, the calls and the time.
type CallRow = tuple[str, int, str, int, str, int, str, int, float]
# The marked key of a caller and its line.
type CallSite = tuple[FunctionKey, int]


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
    lines: tuple[LineRow, ...] = ()
    calls: tuple[CallRow, ...] = ()


@dataclass(frozen=True, slots=True)
class CallTotal:
    """The sum of the calls of one line to one callee over the call profiles: the calls and the wall time."""

    calls: int
    wall_s: float


@dataclass(frozen=True, slots=True)
class LineTotal:
    """The sum of one line over the call profiles: its hits, its wall time and its CPU time (THE LINES)."""

    hits: int
    wall_s: float
    cpu_s: float


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


def measured_row(row: object) -> MeasuredRow | None:
    """Return one row of the functions or of the lines of a call profile, or None when it does not have the shape.

    The two rows have the same types of fields: a file, a number, a name, two counts and two times (assay.core.calls).
    """
    if not isinstance(row, list) or len(row) != 7:
        return None
    file, number, name, first_count, second_count, first_s, second_s = row
    if not (isinstance(file, str) and isinstance(name, str)):
        return None
    if not (isinstance(number, int) and isinstance(first_count, int) and isinstance(second_count, int)):
        return None
    if not (isinstance(first_s, (int, float)) and isinstance(second_s, (int, float))):
        return None
    return (file, number, name, first_count, second_count, float(first_s), float(second_s))


def call_row(row: object) -> CallRow | None:
    """Return one call of a line of a call profile, or None when it does not have the shape of a row."""
    if not isinstance(row, list) or len(row) != 9:
        return None
    caller_file, caller_first, caller_name, line, callee_file, callee_first, callee_name, calls, wall_s = row
    if not all(isinstance(word, str) for word in (caller_file, caller_name, callee_file, callee_name)):
        return None
    if not all(isinstance(number, int) for number in (caller_first, line, callee_first, calls)):
        return None
    if not isinstance(wall_s, (int, float)):
        return None
    return (caller_file, caller_first, caller_name, line, callee_file, callee_first, callee_name, calls, float(wall_s))


def call_profile_of(path: Path, document: object) -> CallProfile:
    """Return the call profile of one file (THE RECORD of assay.core.calls).

    Raises:
        AuditError: If the document does not have the format and the shape of a call profile
    """
    shape = f"the call profile {path} does not have the format {call_profile.FORMAT} and its shape"
    if not isinstance(document, dict) or document.get("format") != call_profile.FORMAT:
        raise AuditError(shape)
    argv, rows, line_rows = document.get("argv"), document.get("functions"), document.get("lines")
    call_rows = document.get("calls")
    numbers = [document.get(key) for key in NUMBER_FIELDS]
    if not (
        isinstance(argv, list)
        and isinstance(rows, list)
        and isinstance(line_rows, list)
        and isinstance(call_rows, list)
    ):
        raise AuditError(shape)
    values = [float(number) for number in numbers if isinstance(number, (int, float))]
    functions = [measured_row(row) for row in rows]
    kept = tuple(row for row in functions if row is not None)
    lines = [measured_row(row) for row in line_rows]
    kept_lines = tuple(row for row in lines if row is not None)
    calls = [call_row(row) for row in call_rows]
    kept_calls = tuple(row for row in calls if row is not None)
    if len(values) != len(NUMBER_FIELDS) or len(kept) != len(functions):
        raise AuditError(shape)
    if len(kept_lines) != len(lines) or len(kept_calls) != len(calls):
        raise AuditError(shape)
    pid, parent, start, end, cpu_s = values
    words = tuple(str(word) for word in argv)
    return CallProfile(words, int(pid), int(parent), start, end, cpu_s, kept, kept_lines, kept_calls)


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


def line_totals(profiles: Sequence[CallProfile]) -> tuple[dict[FunctionKey, dict[int, LineTotal]], dict[str, Path]]:
    """Return the sum of each line of each function over the call profiles, and the file of a process for each marked file.

    The key of a function holds its marked file, its first line and its full name (THE LINES).  Complexity:
    linear in the rows of the lines.
    """
    sums: dict[FunctionKey, dict[int, list[float]]] = {}
    process_files: dict[str, Path] = {}
    for profile in profiles:
        utils = utils_of(profile.argv)
        for file, line, name, first_line, hits, wall_s, cpu_s in profile.lines:
            marked = marked_file(file, utils)
            process_files.setdefault(marked, Path(file))
            counts = sums.setdefault((marked, first_line, name), {}).setdefault(line, [0, 0.0, 0.0])
            counts[0] += hits
            counts[1] += wall_s
            counts[2] += cpu_s
    totals = {
        key: {line: LineTotal(int(counts[0]), counts[1], counts[2]) for line, counts in lines.items()}
        for key, lines in sums.items()
    }
    return totals, process_files


def call_totals(profiles: Sequence[CallProfile]) -> dict[CallSite, dict[FunctionKey, CallTotal]]:
    """Return the sum of the calls of each line to each callee over the call profiles (THE CALLS OF A LINE).

    Complexity: linear in the calls of the lines.
    """
    sums: dict[CallSite, dict[FunctionKey, list[float]]] = {}
    for profile in profiles:
        utils = utils_of(profile.argv)
        for (
            caller_file,
            caller_first,
            caller_name,
            line,
            callee_file,
            callee_first,
            callee_name,
            calls,
            wall_s,
        ) in profile.calls:
            site = ((marked_file(caller_file, utils), caller_first, caller_name), line)
            callee = (marked_file(callee_file, utils), callee_first, callee_name)
            counts = sums.setdefault(site, {}).setdefault(callee, [0, 0.0])
            counts[0] += calls
            counts[1] += wall_s
    return {
        site: {callee: CallTotal(int(counts[0]), counts[1]) for callee, counts in callees.items()}
        for site, callees in sums.items()
    }


def source_lines(marked: str, process_files: dict[str, Path]) -> list[str]:
    """Return the lines of the file of a function, from the file of a process or from this tree, or none (THE LINES)."""
    from assay.core import meter as cost_meter

    candidates = [path for path in (process_files.get(marked),) if path is not None]
    candidates.append(Path(cost_meter.UTILS + marked.removeprefix(PACKAGE_MARK)))
    for candidate in candidates:
        try:
            return Path(candidate).read_text(encoding="utf-8", errors="replace").splitlines()
        except OSError:
            continue
    return []


def write_line_table(
    key: FunctionKey,
    lines: dict[int, LineTotal],
    function: FunctionTotal | None,
    process_files: dict[str, Path],
    indent: str = "",
) -> None:
    """Write the table of the lines of one function, as line_profiler does (THE LINES)."""
    marked, first_line, _ = key
    head = f"loop audit: {indent}"
    wall_total = sum(total.wall_s for total in lines.values())
    denominator = function.cumulative_s if function is not None and function.cumulative_s > 0 else wall_total
    calls = f"{function.calls} calls in {function.processes} processes, " if function is not None else ""
    own = f", {function.own_s:.3f} s own" if function is not None else ""
    check_report.write_line(f"{head}function {function_location(key)}")
    check_report.write_line(f"{head}  {calls}{denominator:.3f} s cumulative{own}")
    check_report.write_line(
        f"{head}  {'Line':>6} {'Hits':>10} {'Wall s':>10} {'Per hit us':>11} {'% Wall':>7} {'CPU s':>10}  Contents"
    )
    text = source_lines(marked, process_files)
    for number in range(first_line, max(lines, default=first_line) + 1):
        contents = text[number - 1] if 0 < number <= len(text) else ""
        total = lines.get(number)
        if total is None:
            check_report.write_line(f"{head}  {number:>6} {'':>10} {'':>10} {'':>11} {'':>7} {'':>10}  {contents}")
            continue
        per_hit_us = total.wall_s / total.hits * 1e6 if total.hits else 0.0
        part = total.wall_s / denominator * 100 if denominator > 0 else 0.0
        check_report.write_line(
            f"{head}  {number:>6} {total.hits:>10} {total.wall_s:>10.4f} {per_hit_us:>11.2f} {part:>7.1f} "
            f"{total.cpu_s:>10.4f}  {contents}"
        )


@dataclass(frozen=True, slots=True)
class LineReport:
    """The sums that the tables of the lines read: the lines, the functions, the calls of a line, the process files."""

    lines: dict[FunctionKey, dict[int, LineTotal]]
    functions: dict[FunctionKey, FunctionTotal]
    calls: dict[CallSite, dict[FunctionKey, CallTotal]]
    process_files: dict[str, Path]

    def cumulative(self, key: FunctionKey) -> float:
        """Return the cumulative time of a function, or the sum of the times of its lines."""
        function = self.functions.get(key)
        if function is not None:
            return function.cumulative_s
        return sum(item.wall_s for item in self.lines.get(key, {}).values())


def write_callees(
    report: LineReport, key: FunctionKey, root_s: float, depth: int, path: list[FunctionKey], written: set[FunctionKey]
) -> None:
    """Expand each line of a function into the tables of the functions that it calls (THE LINES).

    A callee gets a table when its calls from the line take DRILL_PART or more
    of `root_s`, the wall time of the first function.  Complexity: linear in
    the tables that it writes and in their calls.
    """
    if depth <= 0:
        return
    indent = "  " * len(path)
    for line in sorted(report.lines.get(key, {})):
        callees = report.calls.get((key, line), {})
        for callee, total in sorted(callees.items(), key=lambda pair: pair[1].wall_s, reverse=True):
            if total.wall_s < DRILL_PART * root_s or callee in path:
                continue
            part = total.wall_s / root_s * 100 if root_s > 0 else 0.0
            check_report.write_line(
                f"loop audit: {indent}> line {line} calls {function_location(callee)}: {total.calls} calls, "
                f"{total.wall_s:.3f} s, {part:.1f} % of the first function"
            )
            if callee in written or callee not in report.lines:
                continue
            written.add(callee)
            path.append(callee)
            write_line_table(
                callee, report.lines[callee], report.functions.get(callee), report.process_files, indent + "  "
            )
            write_callees(report, callee, root_s, depth - 1, path, written)
            path.pop()


def report_lines(directory: Path, text: str, command: str = "", depth: int = DRILL_DEPTH) -> None:
    """Write the tables of the lines of the hot functions, or of the functions that `text` names (THE LINES).

    After each table, the report expands the hot lines into the tables of their callees, to `depth` levels.

    Raises:
        AuditError: If the directory or a profile cannot be read, or it holds no rows of the lines
    """
    profiles = [profile for profile in read_profiles(directory) if not command or command_of(profile.argv) == command]
    totals, process_files = line_totals(profiles)
    if not totals:
        raise AuditError(
            f"the call profiles in {directory} hold no rows of the lines: run with {call_profile.LINES_ON} in the "
            "variable CRUCIBLE_CALL_PROFILE_LINES (THE LINES of assay.core.calls)"
        )
    report = LineReport(totals, function_totals(profiles), call_totals(profiles), process_files)
    if text:
        chosen = [key for key in totals if text in function_location(key)]
    else:
        candidates = [
            key for key in totals if key[0] != PROFILE_MODULE and key[2].rsplit(".", 1)[-1] not in ENTRY_NAMES
        ]
        chosen = sorted(candidates, key=report.cumulative, reverse=True)[:LINE_FUNCTIONS]
    if not chosen:
        check_report.write_line(f"loop audit: no function of the lines holds {text!r}")
    written: set[FunctionKey] = set()
    for key in chosen:
        if key in written:
            check_report.write_line(f"loop audit: function {function_location(key)} has a table above")
            continue
        written.add(key)
        write_line_table(key, totals[key], report.functions.get(key), process_files)
        write_callees(report, key, report.cumulative(key), depth, [key], written)


def main(argv: list[str]) -> int:
    """Compare the last call of `loop test` with its plan in each build directory of the command line.

    Args:
        argv: The words after `loop audit`: [--calls DIR [--command TEXT] [--lines [TEXT] [--depth N]]] [BUILD_DIR...]

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
    parser.add_argument(
        "--lines",
        nargs="?",
        const="",
        default=None,
        metavar="TEXT",
        help="write the table of the lines of the hot functions, or of each function that holds TEXT (THE LINES)",
    )
    parser.add_argument(
        "--depth",
        type=int,
        default=DRILL_DEPTH,
        metavar="N",
        help=f"expand the hot lines into the tables of their callees to N levels (THE LINES, {DRILL_DEPTH} by default)",
    )
    options = parser.parse_args(argv)
    if not options.build_dirs and options.calls is None:
        check_report.write_line(
            "loop audit: give a build directory, or the directory of the call profiles with --calls", is_error=True
        )
        return ExitCode.USAGE
    if options.lines is not None and options.calls is None:
        check_report.write_line("loop audit: --lines reads the call profiles of --calls DIR", is_error=True)
        return ExitCode.USAGE
    try:
        status = audit([build_dir.resolve() for build_dir in options.build_dirs]) if options.build_dirs else ExitCode.OK
        if options.calls is not None:
            report_calls(options.calls, options.command)
        if options.calls is not None and options.lines is not None:
            report_lines(options.calls, options.lines, options.command, max(options.depth, 0))
        return status
    except AuditError as problem:
        check_report.write_line(f"loop audit: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
