"""cost_meter — the measuring core of the launchers in front of the build steps and the tests.

utils/scripts/build-launcher.py imports this module for each compile and
each link of the build.  A launcher in front of a test imports it in the same
way.  The module imports only os, sys and time, so an import costs about one
millisecond.  It imports signal and resource only to run a command, _ctypes
only to count instructions, and utils/scripts/check_report.py only to write a
finding line.

MEASURE
    measure(command, environment, cpu_limit_s) runs a command with the
    standard streams of the caller, waits for it, and returns a Measurement:
    the status, the user and system CPU time, the wall time and the peak
    resident memory, from wait4.  The CPU time includes each process that the
    command waited for.  The peak memory is that of the largest single
    process.  ninja and make interrupt the process group of a job, which holds
    the launcher and the command.  From the call of measure() on, the caller
    ignores SIGINT, and the command gets the default action of SIGINT.  An
    interrupt then stops the command, and the caller writes its record and
    ends on the signal of the command with end_like().  With a handler, the
    interrupt can arrive after wait4 returns, at a point that has no handler
    for it.  With a CPU limit, each process of the command gets
    RLIMIT_CPU of that many seconds, so a process that runs away ends on
    SIGKILL and does not run for minutes.  On a CI runner, limit_cpu() sets
    no limit (A CI RUNNER below tells why).  With count_instructions, the
    measurement also holds the user instructions of the command (THE
    INSTRUCTION COUNT below).

THE INSTRUCTION COUNT
    The CPU time of one compile rises with the load of the host, because two
    threads share each core.  The number of user instructions does not: four
    compiles of test/test_arena.cpp at a load of 1,030 on the build host gave
    6.3692 G to 6.3704 G instructions (0.02 %).  open_instruction_counter()
    opens a hardware counter of the instructions in user mode with
    perf_event_open, through _ctypes, which costs approximately 0.5 ms to
    import.  The counter is off in this process, each process that this
    process starts after the call gets a copy of it, and the copy starts to
    count when that process calls exec.  So the count holds the command and
    each process that it starts, and not this process.  The kernel adds the
    count of each process to the counter when the process ends.  With
    perf_event_paranoid at 2, the kernel lets each user count its own
    processes in user mode.  read_instruction_counter() gives the count only
    when the counter ran for the full time that it was on.  A count that the
    kernel had to share with other counters, or a counter that a core type
    of a hybrid CPU did not have, is an estimate, and the function then
    gives None.  The function gives None also when the kernel or a seccomp
    filter refuses perf_event_open (a container, a virtual machine with no
    hardware counter), when the architecture is not in PERF_EVENT_OPEN, and
    when the environment sets CRUCIBLE_COUNT_INSTRUCTIONS to 0.  A count is
    exact or absent.

A CI RUNNER
    The thresholds of the time rows (TIME_ROWS: compile-cpu, link-time,
    test-time and fixture-cpu) apply to the build host of the tree, which has
    384 hardware threads.  A GitHub runner has 4 slower vCPUs and 16 GB, and
    it runs two or three jobs at a time.  A time there can exceed an error
    threshold with no defect in the tree.  When the environment sets
    GITHUB_ACTIONS to "true", ci_verdict() changes an error of a time row to
    a warning, and the message tells so.  limit_cpu() then sets no CPU limit,
    because a step that the limit stops cannot become a warning.  An error of
    a memory row, of a size row or of an input stays an error.  Each launcher
    and each check that judges a time calls ci_verdict().

THE RECORD
    write_record(path, fields) writes a JSON object with one key on each line.
    It writes a scratch file and renames it, so a reader sees the whole record
    or the earlier one.  The keys of a record:
      format        1
      step          "compile" or "link" (a launcher of tests writes "test")
      result        "built", "failed", "rejected" (the launcher refused the
                    output after the step) or "hit" (ccache gave the output)
      source        the source of a compile
      output        the absolute path of the output
      output_bytes  the size of the output after the step, when it exists
      exit          the status of the command, or minus the signal number
      cost          for each result except "hit": cpu_s, user_s, system_s,
                    wall_s, peak_rss_kb, load (the one-minute load average at
                    the start) and end (seconds since the epoch), and
                    instructions (the user instructions of the step) when
                    the measurement holds an exact count
      last_cost     for "hit": the cost block of the earlier record, when it
                    had one, so a hit does not erase the last real measure
    cost_block() writes the cost block, and carried_cost() reads it back as
    text, with no JSON parser.

THE BUDGET TABLE AND THE LEDGERS
    budget_rows() reads the two thresholds of some rows of
    utils/scripts/budgets.txt.  check_report.read_budgets() is the full
    parser of the table, and a launcher calls it only when it writes a
    finding.  read_ledger_rows() reads the rows of a ledger of a budget row,
    for example utils/scripts/compile-memory-ledger.txt:

        kind | item | value | reason

    The build kind of a build directory is the one line of
    BUILD_DIR/build-kind.txt, which cmake/BuildLauncher.cmake writes at each
    configure, for example x86_64-debug-asan.
"""

import os
import sys
import time

RECORD_FORMAT = 1
RECORD_SUFFIX = ".cost"
KIND_FILE = "build-kind.txt"
CARRIED_KEYS = ('"cost": ', '"last_cost": ')
KB_PER_GB = 1024 * 1024
# The rows of utils/scripts/budgets.txt whose value is a time.  The self-test
# of utils/scripts/check_report.py holds this set equal to the rows of unit s.
TIME_ROWS = frozenset({"compile-cpu", "link-time", "test-time", "fixture-cpu", "total-compile-cpu", "total-fixture-cpu",
                       "total-test-cpu"})
GITHUB_ACTIONS_ENV = "GITHUB_ACTIONS"
CI_DEMOTION = ("The check demoted this time error to a warning on a CI runner (GITHUB_ACTIONS is true): the time "
               "thresholds apply to the build host, and a CI runner is slower")
# CRUCIBLE_COUNT_INSTRUCTIONS=0 opens no instruction counter, as on a host with no counter.
INSTRUCTIONS_ENV = "CRUCIBLE_COUNT_INSTRUCTIONS"
# The number of the system call perf_event_open on each architecture of the tree.
PERF_EVENT_OPEN = {"x86_64": 298, "aarch64": 241}
# The fields of struct perf_event_attr that the counter sets, in the layout of
# PERF_ATTR_SIZE_VER0, which each kernel accepts: PERF_TYPE_HARDWARE,
# PERF_COUNT_HW_INSTRUCTIONS, and a read of the count with the time that the
# counter was on and the time that it ran.
PERF_ATTR_SIZE = 64
PERF_TYPE_HARDWARE = 0
PERF_COUNT_HW_INSTRUCTIONS = 1
PERF_READ_TIMES = 0x1 | 0x2
# disabled, inherit, exclude_kernel, exclude_hv, enable_on_exec.
PERF_ATTR_FLAGS = (1 << 0) | (1 << 1) | (1 << 5) | (1 << 6) | (1 << 12)
PERF_FLAG_FD_CLOEXEC = 8


class Measurement:
    """The status and the resource use of one command."""

    __slots__ = ("exit_code", "user_s", "system_s", "wall_s", "peak_rss_kb", "load", "end", "cpu_limit_s",
                 "instructions")

    def __init__(self, exit_code: int, user_s: float, system_s: float, wall_s: float, peak_rss_kb: int,
                 load: float, end: float, cpu_limit_s: int | None, instructions: int | None = None) -> None:
        """Hold the values of one run, the CPU limit that each process of the run had or None, and the exact user
        instructions of the run or None."""
        self.exit_code = exit_code
        self.user_s = user_s
        self.system_s = system_s
        self.wall_s = wall_s
        self.peak_rss_kb = peak_rss_kb
        self.load = load
        self.end = end
        self.cpu_limit_s = cpu_limit_s
        self.instructions = instructions

    @property
    def reached_cpu_limit(self) -> bool:
        """Whether the run failed after its CPU time reached the limit of its processes."""
        return self.exit_code != 0 and self.cpu_limit_s is not None and self.cpu_s >= self.cpu_limit_s - 0.05

    @property
    def cpu_s(self) -> float:
        """The user and system CPU time of the command and of each process that it waited for."""
        return self.user_s + self.system_s

    @property
    def peak_gb(self) -> float:
        """The peak resident memory of the largest process, in GB of 2^30 bytes."""
        return self.peak_rss_kb / KB_PER_GB


def is_github_actions() -> bool:
    """Tell whether the process runs in a GitHub workflow.

    Returns:
        True when the environment sets GITHUB_ACTIONS to "true"
    """
    return os.environ.get(GITHUB_ACTIONS_ENV, "") == "true"


def ci_verdict(level: str, check: str, message: str) -> tuple[str, str]:
    """Return the level and the message of one judgment of a measured value, for the host that measured it.

    Args:
        level: "warning" or "error"
        check: The row of the budget table that the judgment reads
        message: The message of the judgment

    Returns:
        A warning whose message gives the reason, for an error of a row in TIME_ROWS on a GitHub runner.
        Else the level and the message, unchanged
    """
    if level != "error" or check not in TIME_ROWS or not is_github_actions():
        return level, message
    stop = "" if message.endswith(".") else "."
    return "warning", f"{message}{stop}  {CI_DEMOTION}."


def limit_cpu(seconds: float) -> int | None:
    """Give this process, and each process that it starts after the call, a CPU limit.

    The kernel counts the limit in whole seconds, so the limit is the number
    of seconds rounded up.  The soft limit and the hard limit are equal, so a
    process that reaches the limit gets SIGKILL and no SIGXCPU.  The default
    action of SIGXCPU writes a core dump, and the GCC 16 driver crashes when
    it reports a compiler that SIGXCPU stopped.  A hard limit that the
    environment set lower stays.  On a GitHub runner, the function sets no
    limit, because a time error there is a warning (the module docstring
    tells why).

    Args:
        seconds: The limit, in seconds of CPU time

    Returns:
        The limit that each process has, or None when the call set no limit
    """
    if is_github_actions():
        return None
    import resource

    limit = max(1, -int(-seconds // 1))
    try:
        _, hard = resource.getrlimit(resource.RLIMIT_CPU)
        if hard != resource.RLIM_INFINITY:
            limit = min(limit, hard)
        resource.setrlimit(resource.RLIMIT_CPU, (limit, limit))
    except (OSError, ValueError):
        return None
    return limit


def open_instruction_counter() -> int | None:
    """Open a counter of the user instructions of each process that this process starts after the call.

    THE INSTRUCTION COUNT in the module docstring gives the rules.  Close the
    counter with read_instruction_counter().

    Returns:
        The file descriptor of the counter, or None when the host gives no counter
    """
    number = PERF_EVENT_OPEN.get(os.uname().machine)
    if number is None or os.environ.get(INSTRUCTIONS_ENV) == "0":
        return None
    try:
        import _ctypes
    except ImportError:
        return None

    class CLong(_ctypes._SimpleCData):
        _type_ = "l"

    class Function(_ctypes.CFuncPtr):
        _flags_ = _ctypes.FUNCFLAG_CDECL | _ctypes.FUNCFLAG_USE_ERRNO
        _restype_ = CLong

    class Library:
        _handle = _ctypes.dlopen(None, os.RTLD_NOW)

    fields = ((PERF_TYPE_HARDWARE, 4), (PERF_ATTR_SIZE, 4), (PERF_COUNT_HW_INSTRUCTIONS, 8), (0, 8), (0, 8),
              (PERF_READ_TIMES, 8), (PERF_ATTR_FLAGS, 8))
    attr = b"".join(value.to_bytes(width, "little") for value, width in fields)
    attr += bytes(PERF_ATTR_SIZE - len(attr))
    try:
        syscall = Function(("syscall", Library()))
        result = syscall(CLong(number), attr, CLong(0), CLong(-1), CLong(-1), CLong(PERF_FLAG_FD_CLOEXEC))
    except (AttributeError, OSError, TypeError, ValueError):
        return None
    # ctypes gives a C long result as an int, because CLong derives directly from _SimpleCData.
    descriptor = result if isinstance(result, int) else result.value
    return descriptor if descriptor >= 0 else None


def read_instruction_counter(descriptor: int) -> int | None:
    """Read the count of a counter of open_instruction_counter() after each counted process ended, and close it.

    Args:
        descriptor: The file descriptor of the counter

    Returns:
        The user instructions of the counted processes, or None when the count is not exact
    """
    try:
        data = os.read(descriptor, 24)
    except OSError:
        data = b""
    finally:
        os.close(descriptor)
    if len(data) != 24:
        return None
    value, enabled, running = (int.from_bytes(data[start:start + 8], "little") for start in (0, 8, 16))
    return value if running == enabled else None


def measure(command: list[str], environment: dict[str, str] | None = None, cpu_limit_s: float | None = None,
            count_instructions: bool = False) -> Measurement:
    """Run one command, wait for it, and return its status and its resource use.

    From the call on, this process ignores SIGINT (the module docstring tells why).

    Args:
        command: The command and its arguments.  The first word is found on PATH
        environment: The environment of the command, or None for the environment of the caller
        cpu_limit_s: The CPU limit of each process of the command, or None for no limit
        count_instructions: Whether to count the user instructions of the command

    Returns:
        The measurement.  An exit code of 127 means that the command did not start

    Raises:
        OSError: If the command cannot start
    """
    import signal

    try:
        load = os.getloadavg()[0]
    except OSError:
        load = -1.0
    applied_limit = limit_cpu(cpu_limit_s) if cpu_limit_s is not None else None
    counter = open_instruction_counter() if count_instructions else None
    signal.signal(signal.SIGINT, signal.SIG_IGN)
    started = time.perf_counter()
    try:
        child = os.posix_spawnp(command[0], command, os.environ if environment is None else environment,
                                setsigdef=(signal.SIGINT,))
    except OSError:
        if counter is not None:
            os.close(counter)
        raise
    _, status, usage = os.wait4(child, 0)
    wall_s = time.perf_counter() - started
    instructions = read_instruction_counter(counter) if counter is not None else None
    exit_code = -os.WTERMSIG(status) if os.WIFSIGNALED(status) else os.WEXITSTATUS(status)
    return Measurement(exit_code, usage.ru_utime, usage.ru_stime, wall_s, usage.ru_maxrss, load, time.time(),
                       applied_limit, instructions)


def end_like(exit_code: int) -> None:
    """End this process as the command ended: with its status, or on its signal.

    Args:
        exit_code: The status of the command, or minus its signal number
    """
    sys.stdout.flush()
    sys.stderr.flush()
    if exit_code < 0:
        import signal

        signal.signal(-exit_code, signal.SIG_DFL)
        os.kill(os.getpid(), -exit_code)
        os._exit(128 - exit_code)
    os._exit(exit_code)


def quote(text: str) -> str:
    """Return text as a JSON string, with each character outside printable ASCII escaped.

    Args:
        text: The text, which can hold surrogate escapes of a file name

    Returns:
        The JSON string, with its quotes
    """
    parts = ['"']
    for char in text:
        code = ord(char)
        if char in '"\\':
            parts.append("\\" + char)
        elif 0x20 <= code < 0x7F:
            parts.append(char)
        elif code > 0xFFFF:
            code -= 0x10000
            parts.append(f"\\u{0xD800 + (code >> 10):04x}\\u{0xDC00 + (code & 0x3FF):04x}")
        else:
            parts.append(f"\\u{code:04x}")
    parts.append('"')
    return "".join(parts)


def cost_block(run: Measurement) -> str:
    """Return the cost block of one measurement as JSON text, with its instructions when it holds an exact count."""
    instructions = "" if run.instructions is None else f', "instructions": {run.instructions}'
    return (f'{{"cpu_s": {run.cpu_s:.3f}, "user_s": {run.user_s:.3f}, "system_s": {run.system_s:.3f}, '
            f'"wall_s": {run.wall_s:.3f}, "peak_rss_kb": {run.peak_rss_kb}, "load": {run.load:.2f}, '
            f'"end": {run.end:.3f}{instructions}}}')


def carried_cost(record_path: str) -> str | None:
    """Return the cost block of the earlier record of an output, as its JSON text.

    Args:
        record_path: The record file

    Returns:
        The value of its "cost" or "last_cost" line, or None
    """
    try:
        with open(record_path, encoding="utf-8") as handle:
            for line in handle:
                for key in CARRIED_KEYS:
                    if line.startswith(key):
                        return line[len(key):].rstrip().rstrip(",")
    except OSError:
        return None
    return None


def write_record(path: str, fields: list[tuple[str, str]]) -> None:
    """Write one record, a JSON object with one key on each line.

    Args:
        path: The record file
        fields: Each key and its value as JSON text, in order
    """
    staging = f"{path}.{os.getpid()}.tmp"
    with open(staging, "w", encoding="ascii") as handle:
        handle.write("{\n" + ",\n".join(f'"{key}": {value}' for key, value in fields) + "\n}\n")
    os.replace(staging, path)


def ccache_hit(stats_path: str, outer_log: str | None) -> bool:
    """Tell whether ccache gave a stored output, from the stats log of one call, and remove the log.

    ccache writes the counters of a call to the file that CCACHE_STATSLOG
    names.  A counter direct_cache_hit or preprocessed_cache_hit means that
    the compiler did not run.

    Args:
        stats_path: The stats log of the call
        outer_log: The stats log that the environment of the build named, or None.  The
            counters of the call go to it too, so a log of the whole build stays whole

    Returns:
        True when the log holds a hit counter
    """
    try:
        with open(stats_path, encoding="utf-8", errors="replace") as handle:
            text = handle.read()
    except OSError:
        return False
    try:
        os.unlink(stats_path)
    except OSError:
        pass
    if outer_log:
        with open(outer_log, "a", encoding="utf-8", errors="replace") as handle:
            handle.write(text)
    counters = {line.strip() for line in text.splitlines()}
    return "direct_cache_hit" in counters or "preprocessed_cache_hit" in counters


def budget_rows(path: str, names: tuple[str, ...]) -> dict[str, tuple[float, float]]:
    """Read the warning threshold and the error threshold of some rows of the budget table.

    Args:
        path: The budget table
        names: The check names of the rows

    Returns:
        The thresholds of each row that the table gives

    Raises:
        OSError: If the table cannot be read
        ValueError: If a threshold of one of the rows is not a number
    """
    rows: dict[str, tuple[float, float]] = {}
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            cells = line.split("|")
            if len(cells) == 5 and cells[0].strip() in names:
                rows[cells[0].strip()] = (float(cells[1]), float(cells[2]))
    return rows


def read_ledger_rows(path: str) -> tuple[dict[tuple[str, str], tuple[int, float, str]], list[tuple[int, str]],
                                         list[str]]:
    """Read the rows of one ledger: kind | item | value | reason.

    A line that starts with '#' and an empty line are not rows.  The comment
    lines before the first row are the header of the ledger.

    Args:
        path: The ledger

    Returns:
        Each row by its kind and item, with its line, its value and its reason; each problem with its
        line; and the header lines

    Raises:
        OSError: If the ledger cannot be read
    """
    import re

    kind_pattern = re.compile(r"[a-z0-9_]+(?:-[a-z0-9_]+)*")
    rows: dict[tuple[str, str], tuple[int, float, str]] = {}
    problems: list[tuple[int, str]] = []
    header: list[str] = []
    with open(path, encoding="utf-8") as handle:
        lines = handle.read().splitlines()
    for number, raw in enumerate(lines, start=1):
        text = raw.strip()
        if not text or text.startswith("#"):
            if not rows and not problems:
                header.append(raw)
            continue
        cells = [cell.strip() for cell in text.split(" | ")]
        if len(cells) != 4:
            problems.append((number, f"a row has four cells (kind | item | value | reason), and this row has "
                                     f"{len(cells)}"))
            continue
        kind, item, value_text, reason = cells
        problem = None
        if not kind_pattern.fullmatch(kind):
            problem = f"the kind {kind!r} is not lowercase words with hyphens"
        elif not item:
            problem = "the row names no item"
        elif not reason:
            problem = f"the row of {item} gives no reason"
        elif (kind, item) in rows:
            problem = f"the item {item} has a second row of kind {kind}"
        else:
            try:
                rows[(kind, item)] = (number, float(value_text), reason)
            except ValueError:
                problem = f"the value {value_text!r} is not a number"
        if problem is not None:
            problems.append((number, problem))
    return rows, problems, header


def build_dir_of(path: str) -> str | None:
    """Return the build directory that holds a path: the nearest directory above it with a CMakeCache.txt.

    Args:
        path: An absolute path inside a build directory

    Returns:
        The build directory, or None
    """
    directory = os.path.dirname(path)
    while True:
        if os.path.isfile(os.path.join(directory, "CMakeCache.txt")):
            return directory
        parent = os.path.dirname(directory)
        if parent == directory:
            return None
        directory = parent


def read_kind(build_dir: str) -> str | None:
    """Return the build kind of a build directory, from its build-kind.txt.

    Args:
        build_dir: The build directory

    Returns:
        The kind, or None when the file does not exist or is empty
    """
    try:
        with open(os.path.join(build_dir, KIND_FILE), encoding="utf-8") as handle:
            kind = handle.read().strip()
    except OSError:
        return None
    return kind or None


def finding_text(level: str, path: str, check: str, message: str) -> str:
    """Return one finding line in the format of utils/scripts/check_report.py.

    Args:
        level: "warning" or "error"
        path: The absolute path that the finding names
        check: The check name
        message: The message

    Returns:
        The line, with the path relative to the repository root when it is in the tree
    """
    from pathlib import Path

    scripts = os.path.dirname(os.path.abspath(__file__))
    if scripts not in sys.path:
        sys.path.insert(0, scripts)
    import check_report
    from repo_root import REPO_ROOT

    resolved = Path(path)
    shown = str(resolved.relative_to(REPO_ROOT)) if resolved.is_relative_to(REPO_ROOT) else path
    return check_report.Finding(level, shown, 0, check, message).text()
