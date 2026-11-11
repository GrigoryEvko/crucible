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

A TRACED RUN
    With a supervisor, measure() starts the command with supervisor.start(),
    and with no spawn.  It calls supervisor.serve() until the command ends,
    waits for the command, reads the counter, and calls supervisor.finish().
    The tracer of assay.loop.tracer is such a supervisor, and `loop test-step`
    gives it for a traced run of a test.  The wall time and the count end at
    the wait.  The answers to the processes that outlive the command are then
    not in the measure.

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
    schedstat file gives the clock alone.  With count_instructions,
    start_self() opens a counter of the user instructions
    (open_instruction_counter() with own).  The counter counts this process
    from the call on, and each process that it starts after the call.
    measure_self() reads it.  The count does not hold the start of the
    interpreter and the imports before the call.

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
    The hard stops of the steps apply to the build host of the tree.  A
    GitHub runner has 4 slower vCPUs and 16 GB, and it runs two or three jobs
    at a time.  A step there can pass a hard stop with no defect in the tree.
    When the environment sets GITHUB_ACTIONS to "true", limit_cpu() sets no
    CPU limit, because a step that the limit stops cannot become a warning.
    `loop step` then gives a warning for a step past its hard stop.  No
    measured rule judges a time, so an error of a measured rule stays an
    error on a runner.

THE JOBSERVER OF A TEST
    ctest gives each test the variable MAKEFLAGS of the GNU make jobserver
    that `loop test` operates (assay.core.jobserver).  `loop test-step` gives
    a test a clean environment, with no MAKEFLAGS.  ninja and make in a test
    then do not obey that jobserver.  The launcher gives the value of
    MAKEFLAGS to the test as TEST_JOBSERVER_ENV.  assay.core.testing reads it,
    and it runs the groups of a test module at the same time on the free
    jobs.  The variable changes no result.  The key of a test then does not
    hold it.  The launcher gives CALL_PROFILE_ENV and LINES_ENV to the test
    by the same rule (assay.core.calls).

THE RECORD
    A launcher writes the record of each step with assay.core.record, from
    its Measurement (record.measured()).  That module holds the format of
    each record and its one reader and writer.

THE REGISTRY AND THE LEDGERS
    budget_rows() reads the limits of some measured rules of
    utils/registry.txt, through the rows reader of assay.core.rows, which
    imports no module.  A rule has one limit, or one limit for each kind of
    item, such as compile, link and test.  assay.core.registry is the full
    reader of the registry, and its import of dataclasses costs 13 ms, so a
    launcher does not import it.

    read_ledger_rows() reads the ledger of a measured rule of the policy max,
    utils/ledgers/RULE.txt, through assay.core.ledger.  A row keeps one item
    over the limit of its kind, and the rows judge each build of the tree
    (assay.core.budget, THE VERDICT and THE ROWS):

        PATH | KIND | VALUE | ENTITY | REASON

    KIND is the kind of the item, and it is empty for a rule with one limit.
    The judge of a kind gives the form of its item: an output of the build
    directory (PATH @build/OUTPUT, and no ENTITY), a file of the source tree
    (PATH FILE, and no ENTITY), or a name, such as the name of a test (PATH
    "-", and ENTITY NAME).  Two kinds can have the same item, such as an
    object for the kinds function and object, so the kind is a part of the
    key of a row.  VALUE is the kept value, in the unit of the rule.  A row
    with a kind that the rule does not have is a problem of the ledger, and
    so is a row that does not have the form of its kind.  A ledger that does
    not exist has no row, and the call then imports no module, so a launcher
    pays the import of assay.core.ledger only when a ledger exists.
    write_ledger_rows() writes the rows of such a ledger again.

THE MEMO OF THE ROWS
    The import of assay.core.ledger and the parse of the registry cost about
    60 ms.  The launcher of each test and the fixture driver read three
    ledgers, and in the gate of a1df30f04 each of 10,890 processes paid that
    cost.  read_ledger_rows() then keeps the rows and the problems of a ledger
    in the cache ROWS_CACHE of the root of the caches
    (assay.core.cache_root).  The key of an entry holds the rule, the form of
    the item of each kind that the caller judges, and the resolved path of
    the ledger.  It also holds the stat
    fields of each input of the parse: the ledger, the registry, and the
    modules of ROWS_MODULES.  A read with an entry imports no module but
    hashlib and json.  The function writes an entry only when no input
    changed in ROWS_SETTLE_NS (has_settled()).  A change in the same clock
    tick then cannot hide.  An entry holds the rows in the sequence of their lines, as the
    parse gives them.  A read with an entry and a read with a parse then give
    the same sequence.  `loop test` holds the cache in its row of LIMITS of
    assay.core.cache.

    The build kind of a build directory is the one line of
    BUILD_DIR/build-kind.txt, which cmake/BuildLauncher.cmake writes at each
    configure, for example x86_64-debug-asan.  The same configure writes the
    name of the configuration of the build in BUILD_DIR/build-configuration.txt
    (read_configuration()), for example x86_64-debug-asan-bench-fuzz.  That
    name is the build kind with the words of the options of NAME_OPTIONS of
    assay.core.configuration.
"""

import os
import sys
import time

# The names are only for the types, and a step meter does not pay the import of typing.
TYPE_CHECKING = False
if TYPE_CHECKING:
    from collections.abc import Mapping
    from typing import Protocol

    class Supervisor(Protocol):
        """The start of a command that a supervisor monitors, such as the tracer of assay.loop.tracer (A TRACED RUN)."""

        def start(self, command: list[str], environment: Mapping[str, str]) -> int:
            """Start the command, and return its process ID."""

        def serve(self, child: int) -> None:
            """Monitor the command until it ends."""

        def finish(self) -> None:
            """Stop the monitor after the wait for the command, and raise OSError when the command did not start."""


KIND_FILE = "build-kind.txt"
CONFIGURATION_FILE = "build-configuration.txt"
KB_PER_GB = 1024 * 1024
GITHUB_ACTIONS_ENV = "GITHUB_ACTIONS"
CI_DEMOTION = ("The step demoted this time error to a warning on a CI runner (GITHUB_ACTIONS is true): the hard "
               "stops apply to the build host, and a CI runner is slower")
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
# inherit, exclude_kernel, exclude_hv: a counter of this process from the open on, and of its new processes (THE OWN
# RUN).
PERF_OWN_FLAGS = (1 << 1) | (1 << 5) | (1 << 6)
PERF_FLAG_FD_CLOEXEC = 8
# The variable that names the file of the counts of the compiler runs of one ccache call, the suffix of that file
# beside the object, and the line of a run whose count is not exact (A COMPILE THROUGH CCACHE).
COMPILER_COUNT_ENV = "CRUCIBLE_COMPILER_COUNT"
COMPILER_COUNT_SUFFIX = ".compiler-count"
INEXACT_COUNT = "-"
CCACHE_PREFIX_ENV = "CCACHE_PREFIX"
# The variable of the GNU make jobserver, and the variable that gives its value to a test (THE JOBSERVER OF A TEST).
MAKEFLAGS_ENV = "MAKEFLAGS"
TEST_JOBSERVER_ENV = "CRUCIBLE_TEST_MAKEFLAGS"
# The directory of the call profiles of a diagnostic run (THE RECORD of assay.core.calls).
CALL_PROFILE_ENV = "CRUCIBLE_CALL_PROFILE"
# The variable that selects the profile of the lines, and not cProfile (THE LINES of assay.core.calls).
LINES_ENV = "CRUCIBLE_CALL_PROFILE_LINES"
# The directory utils.  This module is utils/assay/core/meter.py.  It calculates the paths with os.path and not with
# assay.core.paths, because each compile and each link imports it, and pathlib costs an import.
UTILS = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# The master loop, utils/bin/loop.
LOOP_MASTER = os.path.join(UTILS, "bin", "loop")
# The directory of the ledgers of the rules, utils/ledgers (assay.core.ledger).
LEDGER_DIRECTORY = os.path.join(UTILS, "ledgers")
# The prefix of a PATH cell that names a file of the build directory (assay.core.ledger, THE FILE).
BUILD_PATH_PREFIX = "@build/"
# The cache of the rows of the ledgers of the measured rules, and the format of an entry (THE MEMO OF THE ROWS).
ROWS_CACHE = "ledger-rows"
ROWS_FORMAT = 3
# The period in which a change of an input of the parse makes read_ledger_rows() write no entry (THE MEMO OF THE ROWS).
ROWS_SETTLE_NS = 50_000_000
# The modules of this directory that make the rows of a ledger from its text (THE MEMO OF THE ROWS).
ROWS_MODULES = ("meter.py", "ledger.py", "rows.py", "registry.py", "paths.py")
# The forms of the item of a row of a ledger of a measured rule (THE REGISTRY AND THE LEDGERS), and the PATH cell and
# the ENTITY cell of the row of each.
ITEM_OUTPUT = "output"
ITEM_FILE = "file"
ITEM_NAME = "name"
ITEM_CELLS = {ITEM_OUTPUT: (f"{BUILD_PATH_PREFIX}OUTPUT", "-"), ITEM_FILE: ("FILE", "-"), ITEM_NAME: ("-", "NAME")}
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


def limit_cpu(seconds: float) -> int | None:
    """Give this process, and each process that it starts after the call, a CPU limit.

    The kernel counts the limit in whole seconds, so the limit is the number
    of seconds rounded up.  The soft limit and the hard limit are equal, so a
    process that reaches the limit gets SIGKILL and no SIGXCPU.  The default
    action of SIGXCPU writes a core dump, and the GCC 16 driver crashes when
    it reports a compiler that SIGXCPU stopped.  A hard limit that the
    environment set lower stays.  On a GitHub runner, the function sets no
    limit, because the error of a hard stop there is a warning (the module
    docstring tells why).

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


def open_instruction_counter(own: bool = False) -> int | None:
    """Open a counter of the user instructions of each process that this process starts after the call.

    THE INSTRUCTION COUNT in the module docstring gives the rules.  With
    `own`, the counter also counts this process from the call on (THE OWN
    RUN).  Close the counter with read_instruction_counter().

    Args:
        own: Count this process too, from the call on

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
              (PERF_READ_TIMES, 8), (PERF_OWN_FLAGS if own else PERF_ATTR_FLAGS, 8))
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


def host_gives_counter() -> bool:
    """Return True when this host gives an accurate instruction counter (THE INSTRUCTION COUNT).

    A test of a meter reads it, and the test then knows if a record of the host can hold a count.
    """
    probe = open_instruction_counter()
    if probe is None:
        return False
    os.close(probe)
    return True


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
            count_instructions: bool = False, supervisor: Supervisor | None = None) -> Measurement:
    """Run one command, wait for it, and return its status and its resource use.

    From the call on, this process ignores SIGINT (the module docstring tells why).

    Args:
        command: The command and its arguments.  The first word is found on PATH
        environment: The environment of the command, or None for the environment of the caller
        cpu_limit_s: The CPU limit of each process of the command, or None for no limit
        count_instructions: Whether to count the user instructions of the command
        supervisor: The start of a traced run, or None for a spawn (A TRACED RUN)

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
    chosen = os.environ if environment is None else environment
    started = time.perf_counter()
    try:
        if supervisor is None:
            child = os.posix_spawnp(command[0], command, chosen, setsigdef=(signal.SIGINT,))
        else:
            child = supervisor.start(command, chosen)
    except OSError:
        if counter is not None:
            os.close(counter)
        raise
    if supervisor is not None:
        supervisor.serve(child)
    _, status, usage = os.wait4(child, 0)
    wall_s = time.perf_counter() - started
    instructions = read_instruction_counter(counter) if counter is not None else None
    if supervisor is not None:
        supervisor.finish()
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


class OwnStart:
    """The start of a measure of this process (THE OWN RUN).

    `clock_s` is the monotonic clock at the start of the process, `load` the
    one-minute load average, and `counter` the file descriptor of the counter
    of its user instructions, or None.
    """

    __slots__ = ("clock_s", "counter", "load")

    def __init__(self, clock_s: float, load: float, counter: int | None) -> None:
        """Hold the clock, the load and the counter of the start."""
        self.clock_s = clock_s
        self.load = load
        self.counter = counter


def start_self(count_instructions: bool = False) -> OwnStart:
    """Return the start of a measure of this process, which measure_self() reads (THE OWN RUN).

    A process that is a test itself, such as the fixture driver, measures its
    own run with measure_self().  The start is the monotonic clock less
    process_lead_s().  The wall time of measure_self() then holds the start
    of the interpreter and the imports before the call.

    Args:
        count_instructions: Open a counter of the user instructions of this process from the call on
    """
    now = time.perf_counter()
    try:
        load = os.getloadavg()[0]
    except OSError:
        load = -1.0
    counter = open_instruction_counter(own=True) if count_instructions else None
    return OwnStart(now - process_lead_s(), load, counter)


def measure_self(exit_code: int, start: OwnStart) -> Measurement:
    """Return the measurement of this process from `start` on, as measure() gives it for a command.

    The CPU time holds this process and each process that it waited for, and
    the peak memory is that of the largest of them.  The CPU time and the
    peak memory start with the process.  The wall time starts at the start
    that start_self() gives, which holds the start of the interpreter (THE
    OWN RUN).  The call reads and closes the counter of the start.

    Args:
        exit_code: The status that the process ends with
        start: The value of start_self() at the start of the measure

    Returns:
        The measurement, with no CPU limit.  It holds the instruction count only when the start has a counter and the
        count is accurate
    """
    import resource

    own = resource.getrusage(resource.RUSAGE_SELF)
    children = resource.getrusage(resource.RUSAGE_CHILDREN)
    instructions = read_instruction_counter(start.counter) if start.counter is not None else None
    return Measurement(exit_code, own.ru_utime + children.ru_utime, own.ru_stime + children.ru_stime,
                       time.perf_counter() - start.clock_s, max(own.ru_maxrss, children.ru_maxrss), start.load,
                       time.time(), None, instructions)


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

    The process gives the signal the default action, and then it sends the
    signal to itself.  The kernel refuses a change of the action of SIGKILL
    and SIGSTOP, and their action is always the default action.  The process
    then sends such a signal with no change.

    Args:
        exit_code: The status of the command, or minus its signal number
    """
    sys.stdout.flush()
    sys.stderr.flush()
    if exit_code < 0:
        import signal

        number = -exit_code
        if number not in (signal.SIGKILL, signal.SIGSTOP):
            signal.signal(number, signal.SIG_DFL)
        os.kill(os.getpid(), number)
        os._exit(128 + number)
    os._exit(exit_code)


def budget_rows(path: str, names: tuple[str, ...]) -> dict[str, dict[str, float]]:
    """Read the limits of some measured rules of the registry.

    The rule registry holds the schema of each row, so this reader
    reads only the field limit of the named rules.  A rule with one limit
    has the kind "" (limits() of assay.core.rows).

    Args:
        path: The registry
        names: The IDs of the rules

    Returns:
        The limit of each kind of item of each named rule that the registry gives with the field

    Raises:
        OSError: If the registry cannot be read
        ValueError: If the registry does not obey the rows grammar, or the limit of a named rule does not read

    Complexity: linear in the length of the registry.
    """
    from assay.core import rows as rows_grammar

    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    try:
        parsed = rows_grammar.parse(text, path)
    except rows_grammar.RowsError as problem:
        raise ValueError(str(problem)) from None
    found: dict[str, dict[str, float]] = {}
    for row in parsed:
        if row.words[0] != "rule" or len(row.words) < 2 or row.words[1] not in names:
            continue
        values = dict(word.partition("=")[::2] for word in row.words[2:])
        if "limit" in values:
            found[row.words[1]] = dict(rows_grammar.limits(values["limit"], row, "limit"))
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


def item_row(form: str, kind: str) -> str:
    """Return the shape of a ledger row of an item of the form `form` and of the kind `kind`, for a message.

    Args:
        form: ITEM_OUTPUT, ITEM_FILE or ITEM_NAME
        kind: The kind of the item, or "" for a rule with one limit
    """
    path, entity = ITEM_CELLS[form]
    return f"{path} | {kind or '-'} | VALUE | {entity} | REASON"


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


def rows_memo(rule_id: str, forms: Mapping[str, str], path: str,
              settle_ns: int = ROWS_SETTLE_NS) -> tuple[str, bool] | None:
    """Return the entry of the rows of a ledger in the cache, and True when each input settled (THE MEMO OF THE ROWS).

    The result is None with no cache, or when an input cannot be read.  An
    input settled when it did not change in `settle_ns` before the call.  A
    test gives a long period, so that its result does not change with the
    load of the host.  `forms` gives the form of the item of each kind that
    the caller judges (read_ledger_rows()).
    """
    from assay.core import cache_root

    base = cache_root.base_root()
    if base is None:
        return None
    here = os.path.dirname(os.path.abspath(__file__))
    inputs = [path, os.path.join(UTILS, "registry.txt"), *(os.path.join(here, name) for name in ROWS_MODULES)]
    try:
        statuses = [os.stat(item) for item in inputs]
    except OSError:
        return None
    import hashlib
    import json

    fields = [[item.st_dev, item.st_ino, item.st_size, item.st_mtime_ns, item.st_ctime_ns] for item in statuses]
    material = json.dumps([ROWS_FORMAT, rule_id, sorted(forms.items()), os.path.realpath(path), fields])
    digest = hashlib.sha256(material.encode("utf-8", "surrogateescape")).hexdigest()[:32]
    is_settled = has_settled(max(item.st_ctime_ns for item in statuses), settle_ns)
    return os.path.join(base, ROWS_CACHE, digest[:2], f"{digest}.json"), is_settled


def has_settled(change_ns: int, settle_ns: int) -> bool:
    """Tell if a change time of an inode is more than `settle_ns` before the clock (THE MEMO OF THE ROWS)."""
    return change_ns < time.time_ns() - settle_ns


def read_rows_memo(entry: str) -> tuple[dict[tuple[str, str], tuple[int, float, str]], list[tuple[int, str]]] | None:
    """Return the rows and the problems of an entry of the cache, or None when it is not there or has no such shape."""
    import json

    try:
        with open(entry, encoding="utf-8", errors="surrogateescape") as handle:
            document = json.load(handle)
        rows = {(str(kind), str(item)): (int(line), float(value), str(reason))
                for kind, item, line, value, reason in document["rows"]}
        problems = [(int(line), str(text)) for line, text in document["problems"]]
    except (OSError, ValueError, KeyError, TypeError):
        return None
    return (rows, problems) if document.get("format") == ROWS_FORMAT else None


def write_rows_memo(entry: str, rows: dict[tuple[str, str], tuple[int, float, str]],
                    problems: list[tuple[int, str]]) -> None:
    """Write an entry of the cache through a temporary file and a rename.  A write that fails writes nothing."""
    import json

    lined = sorted(rows.items(), key=lambda pair: pair[1][0])
    document = {"format": ROWS_FORMAT, "rows": [[*key, *value] for key, value in lined], "problems": problems}
    temporary = f"{entry}.{os.getpid()}.tmp"
    try:
        os.makedirs(os.path.dirname(entry), exist_ok=True)
        with open(temporary, "w", encoding="utf-8", errors="surrogateescape") as handle:
            json.dump(document, handle)
        os.replace(temporary, entry)
    except OSError:
        try:
            os.unlink(temporary)
        except OSError:
            pass


def read_ledger_rows(rule_id: str, forms: Mapping[str, str], path: str,
                     settle_ns: int = ROWS_SETTLE_NS) -> tuple[dict[tuple[str, str], tuple[int, float, str]],
                                                               list[tuple[int, str]], str]:
    """Read the ledger of a measured rule of the policy max: the row of each item of the kinds that the caller judges.

    THE REGISTRY AND THE LEDGERS gives the rows.  A ledger that does not exist
    has no row, and the call then imports no module.  An entry of the cache
    gives the rows of a ledger with no parse (THE MEMO OF THE ROWS).

    Args:
        rule_id: The ID of the measured rule
        forms: The form of the item of each kind that the caller judges: ITEM_OUTPUT, ITEM_FILE or ITEM_NAME.  A rule
            with one limit has the kind ""
        path: The ledger, from ledger_file()
        settle_ns: The settle period of the entry of the cache (rows_memo())

    Returns:
        The row of each kind and item, with its line, its value and its reason; each problem with its line, or line
        0 for a problem of the file as a whole; and the path of the ledger from shown_ledger()

    Raises:
        OSError: If the ledger exists and cannot be read, or the registry cannot be read
    """
    shown = shown_ledger(path)
    if not os.path.isfile(path):
        return {}, [], shown
    memo = rows_memo(rule_id, forms, path, settle_ns)
    held = read_rows_memo(memo[0]) if memo is not None else None
    if held is not None:
        return held[0], held[1], shown
    rows, problems = parsed_ledger_rows(rule_id, forms, path)
    if memo is not None and memo[1]:
        write_rows_memo(memo[0], rows, problems)
    return rows, problems, shown


def parsed_ledger_rows(rule_id: str, forms: Mapping[str, str],
                       path: str) -> tuple[dict[tuple[str, str], tuple[int, float, str]], list[tuple[int, str]]]:
    """Parse the ledger of a measured rule of the policy max, and return its rows and its problems.

    read_ledger_rows() calls it.  A row whose KIND is not a kind of the rule
    is a problem, and so is a row of a kind of `forms` whose PATH and ENTITY
    do not have the form of that kind (THE REGISTRY AND THE LEDGERS).  The
    call skips a row of a different kind of the rule, because a different
    judge reads it.  Complexity: linear in the rows.

    Raises:
        OSError: If the ledger or the registry cannot be read
    """
    from pathlib import Path

    from assay.core import ledger as rule_ledger
    from assay.core import rows as rows_grammar

    try:
        rule = rule_ledger.rule_of(rule_id)
    except (rule_ledger.LedgerError, rows_grammar.RowsError) as problem:
        return {}, [(0, f"the registry gives no measured rule {rule_id} for the ledger: {problem}")]
    file = Path(path)
    try:
        ledger = rule_ledger.read(rule, file=file)
    except rows_grammar.RowsError as problem:
        return {}, [(problem.line, str(problem).removeprefix(f"{problem.path}:{problem.line}: "))]
    kinds = rule.threshold.kinds if rule.threshold is not None else ("",)
    rows: dict[tuple[str, str], tuple[int, float, str]] = {}
    problems: list[tuple[int, str]] = []
    for key, entry in sorted(ledger.base.items(), key=lambda pair: pair[1].line):
        if key.kind not in kinds:
            if kinds == ("",):
                shape = item_row(forms.get("", ITEM_OUTPUT), "")
                problems.append((entry.line, f"a row of the rule {rule_id} is '{shape}'"))
            else:
                text = (f"the row has the kind {key.kind or '-'}, and the kind of a row of the rule {rule_id} is one "
                        f"of {', '.join(kinds)}")
                problems.append((entry.line, text))
            continue
        form = forms.get(key.kind)
        if form is None:
            continue
        item = item_of(key.path, key.entity, form)
        if item is None:
            of_kind = f" of the kind {key.kind}" if key.kind else ""
            problems.append((entry.line, f"a row{of_kind} of the rule {rule_id} is '{item_row(form, key.kind)}'"))
        else:
            rows[(key.kind, item)] = (entry.line, float(entry.value), entry.reason)
    return rows, problems


def write_ledger_rows(rule_id: str, forms: Mapping[str, str], path: str,
                      rows: dict[tuple[str, str], tuple[float, str]], comment: str) -> None:
    """Write the ledger of a measured rule of the policy max with these rows, sorted by their keys.

    The ledger takes `comment` as its head comment on each write
    (assay.core.ledger, THE HEAD COMMENT).  A ledger with no row has no file.

    Args:
        rule_id: The ID of the measured rule
        forms: The form of the item of each kind: ITEM_OUTPUT, ITEM_FILE or ITEM_NAME
        path: The ledger, from ledger_file()
        rows: The value and the reason of each kind and item
        comment: The head comment of the ledger, with its line breaks

    Raises:
        OSError: If the ledger or the registry cannot be read, or the ledger cannot be written
        ValueError: If the ledger or the registry does not obey its schema, or a cell cannot hold its text
    """
    from dataclasses import replace
    from pathlib import Path

    from assay.core import ledger as rule_ledger

    file = Path(path)
    ledger = replace(rule_ledger.read(rule_ledger.rule_of(rule_id), file=file), comment=comment)
    base: dict[rule_ledger.Key, rule_ledger.Entry] = {}
    for (kind, item), (value, reason) in rows.items():
        item_path, entity = item_cells(item, forms[kind])
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
    return read_line(os.path.join(build_dir, KIND_FILE))


def read_configuration(build_dir: str) -> str | None:
    """Return the name of the configuration of a build directory, from its build-configuration.txt.

    Args:
        build_dir: The build directory

    Returns:
        The name, or None when the file is missing or empty
    """
    return read_line(os.path.join(build_dir, CONFIGURATION_FILE))


def read_line(path: str) -> str | None:
    """Return the text of a file of one line with no space at its ends, or None when the file is missing or empty."""
    try:
        with open(path, encoding="utf-8") as handle:
            text = handle.read().strip()
    except OSError:
        return None
    return text or None


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
