"""meter — the measuring core of the launchers in front of the build steps and the tests.

utils/assay/loop/step.py (`loop step`) imports this module for each compile
and each link of the build.  The launcher in front of a test, `loop
test-step`, imports it in the same way.  The module imports only os, sys and
time, so an import costs about one millisecond.  It imports signal and
resource only to run a command, _ctypes only to count instructions,
utils/assay/core/report.py only to write a finding line, and
utils/assay/core/ledger.py only to read or write a ledger that exists.

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

THE OWN RUN
    A process that is a test itself, such as the fixture driver `loop
    fixture`, measures its own run: start_self() at the start of its work,
    and measure_self() at its end.  The CPU time and the peak memory of
    getrusage start with the process.  The wall time starts at the monotonic
    clock less process_lead_s(): the CPU time and the run-queue wait of the
    task since its fork, from /proc/self/schedstat.  The wall time then holds
    the start of the interpreter and the imports before start_self(), apart
    from the time that the process slept in them.  On 2026-10-09, 20 runs of
    the fixture neg_platform_apple_aarch64 from the store took 33.1 ms at
    p50, from the spawn to wait4 in the parent.  The record of the driver
    gave 29.0 ms at p50, 4.3 ms less (3.7 ms to 6.3 ms).  The remaining time
    is the spawn in the parent and the exit after the record.  With the
    clock alone, the record gave 10.0 ms, 22.6 ms less.  A host with no
    schedstat file gives the clock alone.

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

A COMPILE THROUGH CCACHE
    On a miss, ccache runs the preprocessor and then the compiler.  On a hit,
    it runs neither.  The count of the full step then holds the preprocessor
    run and the work of ccache: test/test_trace_ring_pop_batch.cpp ran
    19.29 G instructions as a direct compile and 21.25 G through a miss.  So
    a compile record gives the count of the compiler run alone, which is the
    same in each path.  For a step whose command starts with ccache,
    compiler_meter_environment() sets CCACHE_PREFIX to the interpreter of
    this process, the master loop and the subcommand meter-compiler, and
    COMPILER_COUNT_ENV to a file.  ccache puts the prefix only in front of the compiler run
    (prefix_command_cpp is the prefix of the preprocessor run), also when
    CCACHE_DISABLE is set.  ccache does not hash the prefix, so a hit stays a
    hit.  The meter (meter_compiler) counts the compiler run with a counter
    of its own, adds one line to the file, and exits as the compiler exited.
    read_compiler_count() reads the file.  The meter costs approximately 57 M
    instructions and 12 ms of CPU time for each miss.  ccache splits the
    prefix at each space, so the launcher sets no prefix when a path holds a
    space or when the environment already sets CCACHE_PREFIX, and the record
    then holds no count.  CCACHE_PREFIX takes the place of a prefix_command
    of a ccache configuration file, and the tree sets no such prefix.  The
    negative fixtures (utils/assay/loop/fixture/store.py) and the walk units
    that `loop derive walk-units --measure` measures run the compiler
    directly, so their counts are counts of the compiler run too.

A CI RUNNER
    The thresholds of the time rules (TIME_ROWS: compile-cpu, link-time,
    test-time and fixture-cpu) apply to the build host of the tree, which has
    384 hardware threads.  A GitHub runner has 4 slower vCPUs and 16 GB, and
    it runs two or three jobs at a time.  A time there can exceed an error
    threshold with no defect in the tree.  When the environment sets
    GITHUB_ACTIONS to "true", ci_verdict() changes an error of a time rule to
    a warning, and the message tells so.  limit_cpu() then sets no CPU limit,
    because a step that the limit stops cannot become a warning.  An error of
    a memory rule, of a size rule or of an input stays an error.  Each launcher
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
                    the start) and end (seconds since the epoch).  A compile
                    also has compiler_instructions, the exact user
                    instructions of the compiler run, when the host gives
                    the count (A COMPILE THROUGH CCACHE).  A ccache miss also
                    has ccache_instructions, the instructions of the rest of
                    the step: the preprocessor run, ccache and the meter
      last_cost     for "hit": the cost block of the earlier record, when it
                    had one, so a hit does not erase the last real measure
    cost_block() writes the cost block, and carried_cost() reads it back as
    text, with no JSON parser.  A cost block of an earlier format holds the
    key instructions, the count of the full step, and no reader takes it.

    The record of a step of the build is OUTPUT.cost, in the directory of
    its output.  The record of a test is test_record_file(): NAME.test.cost
    in the directory test-records of the build directory.  Its keys are
    format, step ("test"), result ("passed", "failed" or "rejected"), test
    (the name of the test), command (the program of the test), exit and
    cost.  `loop test-step` writes it for an executable test and for a
    script test, and `loop fixture` writes it for a negative fixture.
    assay.readers.ctest reads it (costs()).

THE REGISTRY AND THE LEDGERS
    budget_rows() reads the two thresholds of some measured rules of
    utils/registry.txt, through the rows reader of assay.core.rows, which
    imports no module.  assay.core.registry is the full reader of the
    registry, and its import of dataclasses costs 13 ms, so a launcher does not
    import it.

    read_kind_rows() reads the ledger of a measured rule of the policy max,
    utils/ledgers/RULE.txt, through assay.core.ledger.  A row admits one item
    over the error threshold of the rule, for one build kind:

        PATH | KIND | VALUE | ENTITY | REASON

    KIND is the build kind.  The rule gives the form of its item: an output
    of the build directory (PATH @build/OUTPUT, and no ENTITY), a file of the
    source tree (PATH FILE, and no ENTITY), or a name, such as the name of a
    test (PATH "-", and ENTITY NAME).  VALUE is the measure when the row was
    written, in the unit of the rule.  A row that does not have the form of
    its rule is a problem of the ledger.  A ledger that does not exist has no
    row, and the call then imports no module, so a launcher pays the import
    of assay.core.ledger only when a ledger exists.  write_kind_rows()
    writes the rows of such a ledger again.

    The build kind of a build directory is the one line of
    BUILD_DIR/build-kind.txt, which cmake/BuildLauncher.cmake writes at each
    configure, for example x86_64-debug-asan.
"""

import os
import sys
import time

RECORD_FORMAT = 1
RECORD_SUFFIX = ".cost"
# The directory of the records of the tests in a build directory, and the suffix of one record (THE RECORD).
TEST_RECORD_DIRECTORY = "test-records"
TEST_RECORD_SUFFIX = ".test" + RECORD_SUFFIX
KIND_FILE = "build-kind.txt"
CARRIED_KEYS = ('"cost": ', '"last_cost": ')
KB_PER_GB = 1024 * 1024
# The measured rules of utils/registry.txt whose value is a time.  The self-test
# of utils/assay/core/report.py holds this set equal to the rules of unit s.
TIME_ROWS = frozenset({"compile-cpu", "link-time", "test-time", "fixture-cpu", "total-compile-cpu", "total-fixture-cpu",
                       "total-test-cpu", "edit-build-cpu", "edit-test-cpu", "edit-build-wall", "edit-test-wall",
                       "configure-time", "noop-build-time", "total-link-cpu"})
GITHUB_ACTIONS_ENV = "GITHUB_ACTIONS"
CI_DEMOTION = ("The check demoted this time error to a warning on a CI runner (GITHUB_ACTIONS is true): the time "
               "thresholds apply to the build host, and a CI runner is slower")
# The CPU time and the run-queue wait of this task since its fork, in nanoseconds (THE OWN RUN).
SCHEDSTAT_FILE = "/proc/self/schedstat"
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
# The keys of the counts in the cost block of a compile record (THE RECORD).
COMPILER_COUNT_KEY = "compiler_instructions"
CCACHE_COUNT_KEY = "ccache_instructions"
# The variable that names the file of the counts of the compiler runs of one ccache call, the suffix of that file
# beside the object, and the line of a run whose count is not exact (A COMPILE THROUGH CCACHE).
COMPILER_COUNT_ENV = "CRUCIBLE_COMPILER_COUNT"
COMPILER_COUNT_SUFFIX = ".compiler-count"
INEXACT_COUNT = "-"
CCACHE_PREFIX_ENV = "CCACHE_PREFIX"
# The directory utils.  This module is utils/assay/core/meter.py.  It calculates the paths with os.path and not with
# assay.core.paths, because each compile and each link imports it, and pathlib costs an import.
UTILS = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# The master loop, utils/bin/loop.
LOOP_MASTER = os.path.join(UTILS, "bin", "loop")
# The directory of the ledgers of the rules, utils/ledgers (assay.core.ledger).
LEDGER_DIRECTORY = os.path.join(UTILS, "ledgers")
# The prefix of a PATH cell that names a file of the build directory (assay.core.ledger, THE FILE).
BUILD_PATH_PREFIX = "@build/"
# The forms of the item of a row of a ledger of a measured rule (THE REGISTRY AND THE LEDGERS), and the row of each.
ITEM_OUTPUT = "output"
ITEM_FILE = "file"
ITEM_NAME = "name"
ITEM_ROWS = {ITEM_OUTPUT: f"{BUILD_PATH_PREFIX}OUTPUT | KIND | VALUE | - | REASON",
             ITEM_FILE: "FILE | KIND | VALUE | - | REASON", ITEM_NAME: "- | KIND | VALUE | NAME | REASON"}
# The characters of a word of a build kind.
KIND_CHARACTERS = frozenset("abcdefghijklmnopqrstuvwxyz0123456789_")
# The words of the prefix that ccache puts in front of the compiler run of a miss (A COMPILE THROUGH CCACHE).
COMPILER_METER = (LOOP_MASTER, "meter-compiler")


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
        check: The ID of the measured rule that the judgment reads
        message: The message of the judgment

    Returns:
        A warning whose message gives the reason, for an error of a rule in TIME_ROWS on a GitHub runner.
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


def process_lead_s() -> float:
    """Return the time of this process before the call, from /proc/self/schedstat, in seconds.

    The kernel gives the CPU time of the task and its wait on a run queue
    since the fork, in nanoseconds.  Their sum is the wall time of the task
    since the fork, less the time that the task slept.  An example of a sleep
    is a read from a disk.  The sum is then a lower bound of that wall time.
    A kernel with no schedstat file gives 0.
    """
    try:
        descriptor = os.open(SCHEDSTAT_FILE, os.O_RDONLY)
    except OSError:
        return 0.0
    try:
        fields = os.read(descriptor, 128).split()
    except OSError:
        fields = []
    finally:
        os.close(descriptor)
    if len(fields) < 2 or not (fields[0].isdigit() and fields[1].isdigit()):
        return 0.0
    return (int(fields[0]) + int(fields[1])) / 1e9


def start_self() -> tuple[float, float]:
    """Return the start of a measure of this process: the monotonic clock at its start, and the one-minute load average.

    A process that is a test itself, such as the fixture driver, measures its
    own run with measure_self().  The start is the monotonic clock less
    process_lead_s().  The wall time of measure_self() then holds the start
    of the interpreter and the imports before the call (THE OWN RUN).
    """
    now = time.perf_counter()
    try:
        load = os.getloadavg()[0]
    except OSError:
        load = -1.0
    return now - process_lead_s(), load


def measure_self(exit_code: int, start: tuple[float, float]) -> Measurement:
    """Return the measurement of this process from `start` on, as measure() gives it for a command.

    The CPU time holds this process and each process that it waited for, and
    the peak memory is that of the largest of them.  The CPU time and the
    peak memory start with the process.  The wall time starts at the start
    that start_self() gives, which holds the start of the interpreter (THE
    OWN RUN).

    Args:
        exit_code: The status that the process ends with
        start: The value of start_self() at the start of the measure

    Returns:
        The measurement, with no CPU limit and no instruction count
    """
    import resource

    own = resource.getrusage(resource.RUSAGE_SELF)
    children = resource.getrusage(resource.RUSAGE_CHILDREN)
    started, load = start
    return Measurement(exit_code, own.ru_utime + children.ru_utime, own.ru_stime + children.ru_stime,
                       time.perf_counter() - started, max(own.ru_maxrss, children.ru_maxrss), load, time.time(), None)


def compiler_meter_environment(environment: dict[str, str], count_path: str) -> dict[str, str] | None:
    """Return the environment of a ccache call that counts its compiler run, or None when the call cannot count it.

    A COMPILE THROUGH CCACHE in the module docstring gives the rules.

    Args:
        environment: The environment of the ccache call
        count_path: The file that the meter adds the count of each compiler run to

    Returns:
        The environment with CCACHE_PREFIX and COMPILER_COUNT_ENV, or None when the environment already sets
        CCACHE_PREFIX or a word of the prefix holds a space
    """
    words = (sys.executable, *COMPILER_METER)
    if environment.get(CCACHE_PREFIX_ENV) or not all(word and " " not in word for word in words):
        return None
    return dict(environment, **{CCACHE_PREFIX_ENV: " ".join(words), COMPILER_COUNT_ENV: count_path})


def read_compiler_count(count_path: str) -> tuple[bool, int | None]:
    """Read the counts that the meter added to a file for the compiler runs of one ccache call, and remove the file.

    Args:
        count_path: The file

    Returns:
        Whether a compiler run added a line, and the sum of the counts, or None when a count is not exact or a line
        is not a count
    """
    try:
        with open(count_path, encoding="ascii", errors="replace") as handle:
            lines = handle.read().split()
    except OSError:
        return False, None
    try:
        os.unlink(count_path)
    except OSError:
        pass
    if not lines:
        return False, None
    if not all(line.isdigit() for line in lines):
        return True, None
    return True, sum(int(line) for line in lines)


def meter_compiler(command: list[str]) -> None:
    """Run the compiler run of a ccache call, add its count to the file of COMPILER_COUNT_ENV, and end as it ended.

    `loop meter-compiler` calls this function as the prefix of ccache (A
    COMPILE THROUGH CCACHE).  ccache keeps the standard streams as the output
    of the compiler, so the function writes to them only when the command
    cannot start.

    Args:
        command: The compiler command that ccache gives the prefix
    """
    if not command:
        print("loop meter-compiler: give the compiler command after the subcommand", file=sys.stderr)
        os._exit(2)
    try:
        run = measure(command, None, None, count_instructions=True)
    except OSError as error:
        print(f"loop meter-compiler: the command {command[0]} cannot start: {error}", file=sys.stderr)
        os._exit(127)
    target = os.environ.get(COMPILER_COUNT_ENV)
    if target:
        line = INEXACT_COUNT if run.instructions is None else str(run.instructions)
        try:
            with open(target, "a", encoding="ascii") as handle:
                handle.write(f"{line}\n")
        except OSError:
            pass
    end_like(run.exit_code)


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


def cost_block(run: Measurement, counts: tuple[tuple[str, int], ...] = ()) -> str:
    """Return the cost block of one measurement as JSON text.

    Args:
        run: The measurement
        counts: The key and the exact value of each instruction count of the block (THE RECORD)

    Returns:
        The block, with the times, the peak memory and each count
    """
    extra = "".join(f', "{key}": {value}' for key, value in counts)
    return (f'{{"cpu_s": {run.cpu_s:.3f}, "user_s": {run.user_s:.3f}, "system_s": {run.system_s:.3f}, '
            f'"wall_s": {run.wall_s:.3f}, "peak_rss_kb": {run.peak_rss_kb}, "load": {run.load:.2f}, '
            f'"end": {run.end:.3f}{extra}}}')


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
    # The text is ASCII, because quote() escapes each other character.  Python loads the UTF-8 codec at its start,
    # and the ASCII codec costs one more import on each step and each test.
    with open(staging, "w", encoding="utf-8") as handle:
        handle.write("{\n" + ",\n".join(f'"{key}": {value}' for key, value in fields) + "\n}\n")
    os.replace(staging, path)


def test_record_file(build_dir: str, name: str) -> str:
    """Return the record of one test in a build directory (THE RECORD).

    Args:
        build_dir: The build directory
        name: The name of the test

    Returns:
        BUILD_DIR/test-records/NAME.test.cost
    """
    return os.path.join(build_dir, TEST_RECORD_DIRECTORY, name + TEST_RECORD_SUFFIX)


def write_test_record(build_dir: str, name: str, command: str, run: Measurement, result: str) -> None:
    """Write the record of one test, and make its directory when necessary.

    The function makes the directory only when the write finds no directory.
    A test of a build directory with records then makes no more system calls.

    Args:
        build_dir: The build directory
        name: The name of the test
        command: The program of the test
        run: The measurement of the test
        result: "passed", "failed" or "rejected" (the judge refused a test with the status 0)
    """
    path = test_record_file(build_dir, name)
    fields = [("format", str(RECORD_FORMAT)), ("step", '"test"'), ("result", f'"{result}"'), ("test", quote(name)),
              ("command", quote(command)), ("exit", str(run.exit_code)), ("cost", cost_block(run))]
    try:
        write_record(path, fields)
    except FileNotFoundError:
        os.makedirs(os.path.dirname(path), exist_ok=True)
        write_record(path, fields)


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
    """Read the warning threshold and the error threshold of some measured rules of the registry.

    The rule registry-shape holds the schema of each row, so this reader
    reads only the fields warn and error of the named rules.

    Args:
        path: The registry
        names: The IDs of the rules

    Returns:
        The thresholds of each named rule that the registry gives with both fields

    Raises:
        OSError: If the registry cannot be read
        ValueError: If the registry does not obey the rows grammar, or a threshold of a named rule is not a number

    Complexity: linear in the length of the registry.
    """
    from assay.core import rows as rows_grammar

    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    try:
        parsed = rows_grammar.parse(text, path)
    except rows_grammar.RowsError as problem:
        raise ValueError(str(problem)) from None
    found: dict[str, tuple[float, float]] = {}
    for row in parsed:
        if row.words[0] != "rule" or len(row.words) < 2 or row.words[1] not in names:
            continue
        values = dict(word.partition("=")[::2] for word in row.words[2:])
        if "warn" in values and "error" in values:
            found[row.words[1]] = (rows_grammar.decimal(values["warn"], row, "warning threshold"),
                                   rows_grammar.decimal(values["error"], row, "error threshold"))
    return found


def ledger_file(rule_id: str, directory: str | None = None) -> str:
    """Return the ledger of a measured rule: utils/ledgers/RULE.txt, or RULE.txt in a directory of planted ledgers.

    Args:
        rule_id: The ID of the rule
        directory: The directory of planted ledgers of a test, or None for the ledgers of the tree
    """
    return os.path.join(directory or LEDGER_DIRECTORY, f"{rule_id}.txt")


def shown_ledger(path: str) -> str:
    """Return the path of a ledger as a finding names it: relative to the source root when it is in the tree."""
    root = os.path.dirname(UTILS)
    return os.path.relpath(path, root) if path.startswith(root + os.sep) else path


def is_build_kind(text: str) -> bool:
    """Tell whether a text is a build kind: words of a-z, 0-9 and '_', with one hyphen between two words."""
    return all(word and set(word) <= KIND_CHARACTERS for word in text.split("-"))


def item_of(path: str, entity: str, form: str) -> str | None:
    """Return the item of a ledger row, or None when the PATH cell and the ENTITY cell do not have the form `form`.

    Args:
        path: The PATH of the row, "" for the cell "-"
        entity: The ENTITY of the row, "" for the cell "-"
        form: ITEM_OUTPUT, ITEM_FILE or ITEM_NAME
    """
    if form == ITEM_OUTPUT:
        is_output = path.startswith(BUILD_PATH_PREFIX) and len(path) > len(BUILD_PATH_PREFIX) and not entity
        return path[len(BUILD_PATH_PREFIX):] if is_output else None
    if form == ITEM_FILE:
        return path if path and not path.startswith(BUILD_PATH_PREFIX) and not entity else None
    return entity if not path and entity else None


def item_cells(item: str, form: str) -> tuple[str, str]:
    """Return the PATH and the ENTITY of the ledger row of an item of the form `form`, each "" for the cell "-"."""
    if form == ITEM_OUTPUT:
        return BUILD_PATH_PREFIX + item, ""
    if form == ITEM_FILE:
        return item, ""
    return "", item


def read_kind_rows(rule_id: str, form: str, path: str) -> tuple[dict[tuple[str, str], tuple[int, float, str]],
                                                                list[tuple[int, str]], str]:
    """Read the ledger of a measured rule of the policy max: each row by its build kind and its item.

    THE REGISTRY AND THE LEDGERS gives the rows.  A ledger that does not exist
    has no row, and the call then imports no module.

    Args:
        rule_id: The ID of the measured rule
        form: The form of the item of a row: ITEM_OUTPUT, ITEM_FILE or ITEM_NAME
        path: The ledger, from ledger_file()

    Returns:
        Each row by its kind and its item, with its line, its value and its reason; each problem with its line, or
        line 0 for a problem of the file as a whole; and the path of the ledger from shown_ledger()

    Raises:
        OSError: If the ledger exists and cannot be read, or the registry cannot be read
    """
    shown = shown_ledger(path)
    if not os.path.isfile(path):
        return {}, [], shown
    from pathlib import Path

    from assay.core import ledger as rule_ledger
    from assay.core import rows as rows_grammar

    try:
        rule = rule_ledger.rule_of(rule_id)
    except (rule_ledger.LedgerError, rows_grammar.RowsError) as problem:
        return {}, [(0, f"the registry gives no measured rule {rule_id} for the ledger: {problem}")], shown
    file = Path(path)
    try:
        ledger = rule_ledger.read(rule, file=file)
    except rows_grammar.RowsError as problem:
        return {}, [(problem.line, str(problem).removeprefix(f"{problem.path}:{problem.line}: "))], shown
    rows: dict[tuple[str, str], tuple[int, float, str]] = {}
    problems: list[tuple[int, str]] = []
    for key, entry in sorted(ledger.base.items(), key=lambda pair: pair[1].line):
        item = item_of(key.path, key.entity, form)
        if not is_build_kind(key.kind):
            problems.append((entry.line, f"the kind {key.kind!r} is not lowercase words with hyphens"))
        elif item is None:
            problems.append((entry.line, f"a row of the rule {rule_id} is '{ITEM_ROWS[form]}'"))
        else:
            rows[(key.kind, item)] = (entry.line, float(entry.value), entry.reason)
    return rows, problems, shown


def write_kind_rows(rule_id: str, form: str, path: str, rows: dict[tuple[str, str], tuple[float, str]],
                    comment: str) -> None:
    """Write the ledger of a measured rule of the policy max with these rows, sorted by their keys.

    The ledger keeps its head comment, and a new ledger takes `comment`.  A
    ledger with no row has no file.

    Args:
        rule_id: The ID of the measured rule
        form: The form of the item of a row: ITEM_OUTPUT, ITEM_FILE or ITEM_NAME
        path: The ledger, from ledger_file()
        rows: The value and the reason of each kind and item
        comment: The head comment of a new ledger, with its line breaks

    Raises:
        OSError: If the ledger or the registry cannot be read, or the ledger cannot be written
        ValueError: If the ledger or the registry does not obey its schema, or a cell cannot hold its text
    """
    from dataclasses import replace
    from pathlib import Path

    from assay.core import ledger as rule_ledger

    file = Path(path)
    ledger = rule_ledger.read(rule_ledger.rule_of(rule_id), file=file)
    if not file.is_file():
        ledger = replace(ledger, comment=comment)
    base: dict[rule_ledger.Key, rule_ledger.Entry] = {}
    for (kind, item), (value, reason) in rows.items():
        item_path, entity = item_cells(item, form)
        key = rule_ledger.Key(item_path, kind, entity)
        base[key] = rule_ledger.Entry(key, value, reason)
    rule_ledger.store(replace(ledger, base=base), file=file)


def build_dir_of(path: str) -> str | None:
    """Return the build directory that holds a path: the nearest directory above it with a build-kind.txt.

    cmake/BuildLauncher.cmake writes build-kind.txt into the top directory of
    each build of the tree, and the callers read the kind of that build.  This
    module owns the file, and the search imports no reader.

    Args:
        path: An absolute path inside a build directory

    Returns:
        The build directory, or None
    """
    directory = os.path.dirname(path)
    while True:
        if os.path.isfile(os.path.join(directory, KIND_FILE)):
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
    """Return one finding line in the format of utils/assay/core/report.py.

    Args:
        level: "warning" or "error"
        path: The absolute path that the finding names
        check: The check name
        message: The message

    Returns:
        The line, with the path relative to the repository root when it is in the tree
    """
    from pathlib import Path

    from assay.core import report as check_report
    from assay.core.paths import REPO_ROOT

    resolved = Path(path)
    shown = str(resolved.relative_to(REPO_ROOT)) if resolved.is_relative_to(REPO_ROOT) else path
    return check_report.Finding(level, shown, 0, check, message).text()
