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
of its program.  In a cross build, CMake puts an emulator in front of the
program, and cmake/TestLauncher.cmake gives the option --emulator-words
(THE EMULATOR).  The driver of a negative fixture, `loop fixture`, writes the
same record with the same judge.  A fixture then pays no second start of
Python.

WHAT THE SUBCOMMAND DOES
    It runs the command through utils/assay/core/meter.py, with the same
    standard streams.  It counts the user instructions of the command and of
    each process that the command starts (meter.py, THE INSTRUCTION COUNT).
    The command gets the clean environment of THE ENVIRONMENT, and
    CRUCIBLE_TEST_LAUNCHED with the name of the test.  A fixture driver in the
    command then gives the record and the judge to this subcommand.  It
    writes the record BUILD/test-records/NAME.test.cost (assay.core.record,
    THE RECORDS), and it ends as the command ended: with its status, or on its
    signal.  With no --build-dir, the build directory is the nearest
    directory above the program with a build-kind.txt.  With no such
    directory, the subcommand writes no record.  A failure to measure, to
    read the registry or to write the record does not change the status.

THE EMULATOR
    A cross build runs each executable test through the emulator of
    CMAKE_CROSSCOMPILING_EMULATOR, such as qemu-aarch64-static with its
    options.  CMake puts the emulator after the launcher and before the
    program of the test:

        ... test-step --build-dir BUILD --emulator-words 3 /usr/bin/qemu-aarch64-static -L SYSROOT BUILD/test/T

    cmake/TestLauncher.cmake gives the launcher --emulator-words N, the
    number of the words of the emulator.  The launcher runs the full
    command, with the emulator.  The program of the test is the word after
    the emulator.  The name, the record, the read set and the findings of
    the test belong to that program.  Each test then has a record of its
    own.  program_index() gives the index of the program in the command.
    launched_program() gives the program of the command of a test to a
    reader of the tests of a build, such as assay.loop.affected.

THE ENVIRONMENT
    The subcommand runs the test in a clean environment
    (canonical_environment()).  The result of a test then does not change
    with a variable of the shell, such as LD_LIBRARY_PATH or a variable of a
    git hook.  These are the rules:
      * The subcommand keeps a variable of its environment when KEPT_NAMES
        names it, or when its name starts with a word of KEPT_PREFIXES.  It
        drops each other variable.
      * It sets each variable of FIXED_VALUES to its value: the locale
        C.UTF-8 and the time zone UTC.
      * It sets PYCACHE_PREFIX_ENV to the cache of the compiled modules in
        the cache root, the directory that each master names in
        sys.pycache_prefix.  A Python process of the test then reads and
        writes no __pycache__ directory of the tree.  A compiled module is
        then in no read set, and the cache root is a runtime root of the
        read set (assay.loop.readset).  With the caches off, the launcher
        sets NO_BYTECODE_ENV, as the masters do.
      * It sets SAFE_PATH_ENV.  A Python process of the test then keeps the
        working directory and the directory of its script off sys.path.  Its
        import system then lists no such directory, and the listing is in no
        read set.  Each master puts the directory utils on sys.path itself.
      * It keeps each absolute directory of PATH one time, in the same
        sequence.  It drops an empty item and a relative item, because the
        two name a directory that changes with the directory of the test.
      * It drops MAKEFLAGS, the jobserver that ctest gives each test.  It
        gives the value of MAKEFLAGS to the test as TEST_JOBSERVER_ENV of
        assay.core.meter (THE JOBSERVER OF A TEST there).  The groups of a
        test module then run at the same time on the free jobs.  ninja or
        make in a test does not obey the jobserver.  The clean environment
        holds no such variable, because a jobserver changes no result.
      * It gives CALL_PROFILE_ENV and LINES_ENV of assay.core.meter to the
        test with no change, and the clean environment does not hold them
        (THE COST of assay.core.calls).
    `loop test` puts the same environment into the key of each test behind
    the launcher (assay.loop.affected).  A variable that the launcher drops
    then changes no result and no key.  A variable that a test gets from its
    property ENVIRONMENT goes through the same rules.  The rule
    test-registration refuses such a variable when the launcher drops it.

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
    The launcher judges three measured rules of utils/registry.txt.
    test-time is the wall time of the test, and test-memory is the peak
    resident memory of its largest process.  test-instructions is the user
    instructions of the test and of each process that it starts.
      * The wall time increases with the load of the host.  When the host is
        short of memory, or when each core is in use, a run takes more time
        and more system time.  The user instructions do not change with the
        load (utils/assay/core/meter.py, THE INSTRUCTION COUNT).
        ccache_wrapper_self_test ran 43.4 s of user time in 7.4 s of wall
        time on a quiet host.  On 2026-10-12, the same test took 56 s to 166 s
        of wall time, when the host was short of memory.
      * When the run holds an accurate count, test-instructions holds the
        error level of the test, and test-time gives a warning only.  The
        record keeps the count (key test_instructions, assay.core.record).  A
        run with no count keeps the error level of test-time.  A host with no
        accurate counter gives no count.  test-instructions reads the run
        alone, because its count is almost the same on each run.
      * A test that waits for a different thread in a loop runs more
        instructions when the host is in use.  On 2026-10-12, with two tests
        for each CPU, the counts of such tests increased up to 21 times, to
        at most 12.1 G.  That count is less than the warning threshold of
        test-instructions, and such a test has no row.
      * The load of the host only makes a run slower.  The warnings of
        test-time then read the smallest wall time of the window of the test.
        The window is the last RECENT_RUNS runs with the status 0 that did the
        measured work, this run with them.  Each run of a window has one
        version of the test (THE VERSION).  The record keeps the window and
        its version (keys walls and version, assay.core.record).  A test that
        waits for the same time on each run has that time in each run of its
        window, and it gives a warning.  A run with a different status, or a
        run that did not do the measured work, is judged alone.  Such a run
        does not go into the window.
      * The errors of test-time read the run alone.  An error then comes from
        the first run with no count over the error threshold.  test-memory
        reads the run alone, because the memory is almost the same on each
        run.
      * Over the warning threshold of a rule, the subcommand prints a warning
        line in the format of utils/assay/core/report.py and writes it to
        DIR/RULE.NAME.txt, so the tests that run at the same time do not write
        one file.  Under the warning threshold, it removes that file.  The
        line names the program when the program is a file of the build
        directory, and the record of the test for a script test.
      * Over the error threshold, it prints an error line, and a test that
        passed fails with status 1.
      * A row of the ledger utils/ledgers/RULE.txt keeps a test over the
        warning threshold: '- | KIND | VALUE | TEST | REASON'.  KIND is the
        name of the configuration of the build or its build kind, and VALUE
        is the measure when the row was written, in the unit of the rule
        (meter.py, THE REGISTRY AND THE LEDGERS).  A kept test gives a
        warning that names the row, up to VALUE with the noise of the unit
        (assay.core.budget, THE NOISE).  A test over that value gives the
        level of a test with no row.  A row whose test stays at or below the
        warning threshold gives no finding.  A ledger fails only on a growth
        (THE POLICIES of assay.core.ledger).  A ledger that does not
        exist has no row, and the subcommand then imports no ledger module.
    Memory and the instruction count are almost the same on each run, so
    their error thresholds sit above the largest measure.  Wall time depends
    on the load of the host, so its error threshold sits above the noise that
    the commit of the threshold states, and the timeout of the test preset is
    the hard stop.  In a build of a
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

THE VERSION
    The window holds the runs of one version of a test.  A run of a different
    version drops the runs of the window.  After a change that makes a test
    longer, the test then gives its warning on its first run.  The version is
    a digest of the words of the command and of the files of the test
    (version_of()).  For each file, the digest holds its length in bytes, its
    change time and its inode.  The files of a command are these files:
      * each word of the command that names a file, and the VALUE of each word
        NAME=VALUE that names a file
      * for a word that names a master of utils/bin/, each module of the
        package assay, because the master runs them.
    The fixture driver gives the version of its compile: the command of the
    compile and each file that the compile read.  A run with no version is
    judged alone, and the record keeps it alone.  The version holds no other
    input of a test, such as a data file that the test opens.  The read set
    holds such a file (THE READ SET).

THE READ SET
    The launcher can trace the system calls of each test with a build
    directory (assay.loop.tracer).  A rule test and a group test of the
    master guard get no trace (has_derived_inputs()).  `loop test` gives
    such a test the inputs of its rule or its group, and it reads no read
    set of it.  The test of one module, `guard selftest MODULE`, gets the
    trace.  A module can import a module by name from a listing, and it can
    read a ledger, a plant or a file of the tree.  On 2026-10-18, the traces
    of 105 such tests read 227 modules and 66 ledgers that their import
    closures do not hold.  The trace gives the read set of the
    run: each file that the test and its children open for read, run or list.
    The launcher writes it to BUILD/test-records/NAME.test.reads
    (assay.loop.readset).  `loop test` puts its key parts into the key of
    the test (assay.loop.affected, THE READ SET).
    The launcher traces a run when the test has no current read set
    (assay.loop.readset, THE RETRACE).  The version of a trace is the version
    of the run (THE VERSION) with the clean environment.  Each other run has
    no trace and costs no import of the tracer.  A failure of the trace or of
    the write of the read set does not change the status of the test.

    The kernel admits one listener of seccomp in the chain of a task.  A
    traced test cannot trace a test of its own, because its listener then
    gets EBUSY.  The option `--trace never` keeps such a test untraced, and
    crucible_script_test() of cmake/TestLauncher.cmake gives it.  A test with
    the option runs on each call of `loop test`.  The launcher sets TRACED_ENV
    for a traced run.  A test that must hold a listener reads the variable.
    It fails when the variable is set, because its cases of the trace cannot
    hold.  The clean environment does not hold the variable.  A launcher in
    the test then drops it for the tests that it starts.

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
from assay.core import cache_root
from assay.core import meter as cost_meter
from assay.core import record as cost_record

# The name Callable is only for the types, and a test does not pay the import of collections.abc.
TYPE_CHECKING = False
if TYPE_CHECKING:
    from collections.abc import Callable, Iterable, Mapping, Sequence

    from assay.loop.readset import Plan

ROWS = ("test-time", "test-memory", "test-instructions")
# The rule that holds the error level of a run with an accurate instruction count, and the instructions in one G.
COUNT_ROW = "test-instructions"
GIGA = 1e9
# The number of runs in the window of a test.  The warnings of test-time read the smallest wall time of the window
# (THE MEASURED RULES).
RECENT_RUNS = 5
REGISTRY_ENV = "CRUCIBLE_TEST_REGISTRY"
LEDGERS_ENV = "CRUCIBLE_TEST_LEDGERS"
CORES_ENV = "CRUCIBLE_TEST_CORES"
CORES_KEEP = "keep"
# The variable that tells a fixture driver in the command that this subcommand records and judges the test.
LAUNCHED_ENV = "CRUCIBLE_TEST_LAUNCHED"
# The variable that tells a test that this subcommand traces its run (THE READ SET).
TRACED_ENV = "CRUCIBLE_TEST_TRACED"
# The variables that the test gets from the environment of the launcher (THE ENVIRONMENT).  The sanitizer options and
# GITHUB_ACTIONS change a result.  HOME, TMPDIR and XDG_CACHE_HOME give the caches and the scratch directories.
KEPT_NAMES = frozenset(
    {
        "ASAN_OPTIONS",
        "GITHUB_ACTIONS",
        "HOME",
        "LOGNAME",
        "LSAN_OPTIONS",
        "PATH",
        "TMPDIR",
        "TSAN_OPTIONS",
        "UBSAN_OPTIONS",
        "USER",
        cache_root.HOME_VARIABLE,
    }
)
# The starts of the names of the other variables that the test gets.  They are the variables of the tree, of its tests,
# of ccache and of the session oracle.
KEPT_PREFIXES = ("ASSAY_", "CCACHE_", "CRUCIBLE_", "SESSION_ORACLE_")
# The variables that the launcher gives each test, with their values.
FIXED_VALUES = (("LANG", "C.UTF-8"), ("LC_ALL", "C.UTF-8"), ("TZ", "UTC"))
# The variable of the directory of the compiled modules of Python, and the variable that stops their writes (THE
# ENVIRONMENT).
PYCACHE_PREFIX_ENV = "PYTHONPYCACHEPREFIX"
NO_BYTECODE_ENV = "PYTHONDONTWRITEBYTECODE"
# The variable that keeps the working directory and the directory of the script off sys.path (THE ENVIRONMENT).
SAFE_PATH_ENV = "PYTHONSAFEPATH"
# The variables of the launcher that the clean environment does not hold.  The launcher gives the profile variables to
# the test with no change (THE ENVIRONMENT).
PROFILE_NAMES = (cost_meter.CALL_PROFILE_ENV, cost_meter.LINES_ENV)
PASSED_THROUGH = frozenset({LAUNCHED_ENV, TRACED_ENV, cost_meter.TEST_JOBSERVER_ENV, *PROFILE_NAMES})
SEARCH_PATH = "PATH"
# The options before the command, each with one value, and the one value of --trace (THE READ SET).
OPTIONS = ("--warnings-dir", "--build-dir", "--name", "--emulator-words", "--trace")
TRACE_NEVER = "never"
# The directory of the test groups of the subcommand selftest of the master guard, relative to the root (THE READ SET).
TEST_GROUPS = "utils/assay/tests"
# The end of the path of the master, and the subcommand that this module is.  A test command can name the master of
# an export of the tree.  A reader then finds the master by the end of its path.
MASTER = "utils/bin/loop"
SUBCOMMAND = "test-step"
# The ends of the paths of the three masters, which run the modules of the package (THE VERSION).
GUARD_MASTER = "utils/bin/guard"
MASTERS = (GUARD_MASTER, MASTER, "utils/bin/setup")
PACKAGE = "assay"
USAGE = (
    "usage: loop test-step [--warnings-dir DIR] [--build-dir BUILD] [--name NAME] [--emulator-words N] "
    "[--trace never] [EMULATOR [EMULATOR_ARG]...] PROGRAM [ARG]... | guard selftest tests.loop.test_teststep"
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


def is_kept(name: str) -> bool:
    """Return True for a variable that the test gets from the environment of the launcher (THE ENVIRONMENT)."""
    return name in KEPT_NAMES or name.startswith(KEPT_PREFIXES)


def search_path(value: str) -> str:
    """Return each absolute directory of a search path one time, in the same sequence (THE ENVIRONMENT).

    Complexity: linear in the length of the value.
    """
    directories: list[str] = []
    for item in value.split(os.pathsep):
        if os.path.isabs(item) and item not in directories:
            directories.append(item)
    return os.pathsep.join(directories)


def canonical_environment(environment: Mapping[str, str]) -> tuple[tuple[str, str], ...]:
    """Return the clean environment of a test from the environment of the launcher, sorted by name (THE ENVIRONMENT).

    The result holds no CRUCIBLE_TEST_LAUNCHED, because the launcher sets it
    for each test.  Complexity: O(n log n) for n variables.
    """
    kept = {
        name: value
        for name, value in environment.items()
        if is_kept(name) and name not in PASSED_THROUGH
    }
    if SEARCH_PATH in kept:
        directories = search_path(kept[SEARCH_PATH])
        if directories:
            kept[SEARCH_PATH] = directories
        else:
            del kept[SEARCH_PATH]
    kept.update(FIXED_VALUES)
    kept[SAFE_PATH_ENV] = "1"
    caches = cache_root.base_root(environment)
    if caches is None:
        kept[NO_BYTECODE_ENV] = "1"
    else:
        kept[PYCACHE_PREFIX_ENV] = os.path.join(caches, cache_root.COMPILED_CACHE)
    return tuple(sorted(kept.items()))


def ledger_row(row: str, name: str, kind: str | None, ledgers: str | None = None,
               configuration: str | None = None) -> tuple[str, str, float] | None:
    """Return the place, the reason and the kept value of the ledger row that names a test for the build.

    Args:
        row: A rule of ROWS
        name: The name of the test
        kind: The kind of the build, or None when it is not known
        ledgers: The directory of the ledgers, or None for the directory of CRUCIBLE_TEST_LEDGERS, else of the tree
        configuration: The name of the configuration of the build, or None for the build kind

    Returns:
        The place of the row (path:line), its reason and its kept value, or None
    """
    if kind is None:
        return None
    directory = ledgers if ledgers is not None else os.environ.get(LEDGERS_ENV)
    ledger = cost_meter.ledger_file(row, directory)
    found, shown, _ = cost_budget.row_of(row, cost_meter.ITEM_NAME, ledger, kind, name, configuration)
    return None if found is None else (f"{shown}:{found.line}", found.reason, found.value)


def judge(name: str, run: cost_meter.Measurement, budget: dict[str, tuple[float, float]],
          kind: str | None, ledgers: str | None = None,
          configuration: str | None = None,
          walls: tuple[float, ...] = ()) -> list[tuple[str, str, str]]:
    """Compare the measures of one test with its measured rules (THE MEASURED RULES).

    The witness of test-memory judges the planted record of a test with this
    function, and with the ledgers of the copy of a plant
    (assay.guard.engines.buildsys.held).

    Args:
        name: The name of the test
        run: The measurement of the command, with its accurate instruction count or None
        budget: The thresholds of the measured rules that the registry gives
        kind: The kind of the build, or None
        ledgers: The directory of the ledgers (ledger_row()), or None
        configuration: The name of the configuration of the build, or None for the build kind
        walls: The window of the test, the newest run first, with this run.  The warnings of test-time read its
            smallest wall time.  An empty window judges the run alone

    Returns:
        One (level, rule, message) for each rule with a finding.  On a GitHub runner, a time error is a warning
    """
    findings = []

    def add(level: str, row: str, message: str) -> None:
        shown_level, shown_message = cost_meter.ci_verdict(level, row, message)
        findings.append((shown_level, row, shown_message))

    measures = [("test-time", run.wall_s, "s", f"took {run.wall_s:.2f} s of wall time"),
                ("test-memory", run.peak_gb, "GB", f"took {run.peak_gb:.3f} GB of peak memory")]
    if run.instructions is not None:
        giga = run.instructions / GIGA
        measures.append((COUNT_ROW, giga, "G", f"ran {giga:.2f} G user instructions"))
    # With an accurate count, test-instructions holds the error level, and the wall time gives a warning only.
    count_note = (f"The run holds an instruction count, so the measured rule {COUNT_ROW} holds the error level of the "
                  f"test, and the wall time, which rises with the load of the host, gives a warning only")
    for row, measure, unit, text in measures:
        if row not in budget:
            continue
        warn, error = budget[row]
        admitted = ledger_row(row, name, kind, ledgers, configuration)
        kept = None if admitted is None else admitted[2]
        said = f"the test {name} {text}"
        noise = cost_budget.noise_of(unit)
        found = cost_budget.verdict(measure, warn, error, kept, noise)
        is_time = row == "test-time"
        is_count_held = is_time and run.instructions is not None and COUNT_ROW in budget
        is_tsan_time = is_time and kind is not None and "-tsan" in kind
        is_run_error = not is_count_held and (found == cost_budget.ERROR
                                              or (found == cost_budget.RISEN and measure > error))
        value = measure
        if is_time and len(walls) > 1 and not is_run_error:
            value = min(walls)
            said += f", and at least {value:.2f} s in each of the last {len(walls)} runs of its window"
            found = cost_budget.verdict(value, warn, error, kept, noise)
        over = f"over the {'error' if value > error else 'warning'} threshold of {error if value > error else warn:g}"
        if admitted is not None and found == cost_budget.ADMITTED:
            add("warning", row, f"{said}, {over} {unit}, and the row {admitted[0]} keeps it up to {admitted[2]:g} "
                                f"{unit} with the noise {noise:.0%}: {admitted[1]}")
        elif admitted is not None and found == cost_budget.RISEN:
            level = "warning" if is_tsan_time or is_count_held else cost_budget.risen_level(value, error)
            note = f".  {count_note}" if is_count_held and value > error else ""
            add(level, row, f"{said}, {over} {unit}, and more than the row {admitted[0]} keeps: {admitted[2]:g} "
                            f"{unit} with the noise {noise:.0%}.  Make the test smaller.  When the growth is "
                            f"wanted, write the measured value into the row{note}")
        elif found == cost_budget.ERROR and is_count_held:
            add("warning", row, f"{said}, over the error threshold of {error:g} {unit}.  {count_note}")
        elif found == cost_budget.ERROR and is_tsan_time:
            add("warning", row, f"{said}, over the error threshold of {error:g} {unit}.  The build kind {kind} "
                                f"budgets no wall time, so the timeout of the test preset is the hard stop")
        elif found == cost_budget.ERROR:
            add("error", row, f"{said}, over the error threshold of {error:g} {unit}, and no row of "
                              f"utils/ledgers/{row}.txt keeps it for {configuration or kind}.  Make the test smaller, "
                              f"or split it into tests over disjoint cases")
        elif found == cost_budget.WARNING:
            add("warning", row, f"{said}, over the warning threshold of {warn:g} {unit}")
    return findings


def package_modules(utils: str) -> list[str]:
    """Return the path of each module of the package assay in the directory `utils`, as a sorted list.

    A directory __pycache__ holds no module.  Complexity: one read of each directory of the package.
    """
    found: list[str] = []
    pending = [os.path.join(utils, PACKAGE)]
    while pending:
        try:
            entries = sorted(os.scandir(pending.pop()), key=lambda entry: entry.name)
        except OSError:
            continue
        for entry in entries:
            if entry.is_dir(follow_symlinks=False) and entry.name != "__pycache__":
                pending.append(entry.path)
            elif entry.name.endswith(".py"):
                found.append(entry.path)
    return sorted(found)


def command_files(command: Sequence[str]) -> list[str]:
    """Return each path that the words of a command name, for the version of the test (THE VERSION).

    Complexity: linear in the words, and one walk of the package for a word that names a master.
    """
    paths: list[str] = []
    for word in command:
        paths.append(word)
        _, equals, value = word.partition("=")
        if equals and value:
            paths.append(value)
        if word.endswith(MASTERS):
            paths.extend(package_modules(os.path.dirname(os.path.dirname(word))))
    return paths


def has_derived_inputs(command: Sequence[str]) -> bool:
    """Tell if a command runs the master guard with words that `loop test` gives the inputs of (THE READ SET).

    The script is the first word after the options of the interpreter.  The words are a rule test, `guard check` or
    `guard witness`, or a group test, `guard selftest GROUP` (assay.loop.inputs, THE TESTS).  GROUP names a group as
    the subcommand selftest reads it: a name with no dot, and a directory of TEST_GROUPS in the root of the master.
    The test of one module, `guard selftest MODULE`, gets no such inputs.  Complexity: linear in the options, and one
    stat for a group test.
    """
    if not command or not os.path.basename(command[0]).startswith("python"):
        return False
    index = 1
    while index < len(command) and command[index].startswith("-"):
        index += 1
    if index >= len(command) or not command[index].endswith(GUARD_MASTER):
        return False
    words = command[index + 1:]
    if words[:1] in (["check"], ["witness"]):
        return True
    if words[:1] != ["selftest"] or len(words) < 2 or "." in words[1]:
        return False
    root = os.path.abspath(command[index]).removesuffix(GUARD_MASTER)
    return os.path.isdir(os.path.join(root, TEST_GROUPS, words[1]))


def version_of(words: Sequence[str], files: Iterable[str]) -> str:
    """Return the version of a test: a digest of the words of its command and of its files (THE VERSION).

    A path that names no regular file adds nothing.  The digest is the CRC-32 of the text and the CRC-32 of the text
    in reverse.  The two detect a change, and the launcher then pays no import of hashlib.

    Args:
        words: The words of the command of the test
        files: The paths of the files of the test

    Returns:
        16 lowercase hexadecimal digits.  Complexity: one stat of each path, and linear in the length of the words
    """
    import binascii
    import stat

    parts = list(words)
    for path in files:
        try:
            found = os.stat(path)
        except OSError, ValueError:
            continue
        if stat.S_ISREG(found.st_mode):
            parts.append(f"{path}\0{found.st_size}\0{found.st_mtime_ns}\0{found.st_ino}")
    text = "\n".join(parts).encode(errors="surrogateescape")
    return f"{binascii.crc32(text):08x}{binascii.crc32(text[::-1]):08x}"


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

    __slots__ = ("build_dir", "emulator_words", "name", "trace", "warnings_dir")

    def __init__(self) -> None:
        """Start with no option."""
        self.warnings_dir: str | None = None
        self.build_dir: str | None = None
        self.name: str | None = None
        self.emulator_words: str | None = None
        self.trace: str | None = None


def option_field(option: str) -> str:
    """Return the field of Options that holds the value of an option of OPTIONS, such as build_dir for --build-dir."""
    return option.removeprefix("--").replace("-", "_")


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
        field = option_field(argv[index])
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


def program_index(options: Options, command: Sequence[str]) -> int | None:
    """Return the index in `command` of the program of the test, after the words of its emulator (THE EMULATOR).

    Args:
        options: The options of the launcher
        command: The command after the options

    Returns:
        0 with no --emulator-words.  None when the value of --emulator-words is not a count of words before the
        program
    """
    count = options.emulator_words
    if count is None:
        return 0
    if not (count.isascii() and count.isdigit()) or int(count) >= len(command):
        return None
    return int(count)


def launched_program(command: list[str]) -> str | None:
    """Return the program of a test command behind this launcher, after the words of an emulator (THE EMULATOR).

    A command with no launcher gives its first word.  The result is None for an empty command.  It is None for a
    launcher with no program, or with a value of --emulator-words that is not a count.  Complexity: linear in the
    words.
    """
    for index, word in enumerate(command[:-1]):
        if word.endswith(MASTER) and command[index + 1] == SUBCOMMAND:
            read = read_options(command[index + 2:])
            if read is None:
                return None
            options, launched_command = read
            place = program_index(options, launched_command)
            return None if place is None else launched_command[place]
    return command[0] if command else None


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
                     budget: dict[str, tuple[float, float]] | None = None, is_measured: bool = True,
                     version: str = "") -> int:
    """Judge one test, report each finding, write its record, and return the status that the test ends with.

    `loop test-step` and `loop fixture` call this function.  The two then
    give the same record and the same verdict.  A failure to read the
    registry, to report or to write the record does not change the status.
    The function reads the window of the test from its earlier record.  The
    new record keeps the window with this run when the two runs have one
    version, else this run alone (THE MEASURED RULES, THE VERSION).

    Args:
        name: The name of the test
        program: The program of the test, for the record
        finding_path: The path that a finding names
        build_dir: The build directory, or None for no record and no build kind
        run: The measurement of the test, with its status
        warnings_dir: The warnings directory, or None
        budget: The thresholds of the rules, or None to read them from the registry (read_budget())
        is_measured: False when the run does not do the work that a row keeps, such as a fixture with a result from
            the store.  The window then does not take the run
        version: The version of the test (version_of()), or the empty text when it is not known

    Returns:
        The status of the test, or 1 when the judge rejects a test with the status 0
    """
    status = run.exit_code
    result = "passed" if status == 0 else "failed"
    thresholds = read_budget() if budget is None else budget
    kind = cost_meter.read_kind(build_dir) if build_dir else None
    configuration = cost_meter.read_configuration(build_dir) if build_dir else None
    record_path = cost_record.test_record_file(build_dir, name) if build_dir is not None else None
    earlier_version, earlier_walls = cost_record.recent_window(record_path) if record_path is not None else ("", ())
    # The runs of a different version, or of no version, are not runs of this test (THE VERSION).
    earlier = earlier_walls if version and version == earlier_version else ()
    # A run with a status other than 0, or a run that did not do the measured work, is judged alone.  It stays out of
    # the window, and the record keeps the window of the earlier record.
    is_counted = is_measured and status == 0
    walls = (run.wall_s, *earlier)[:RECENT_RUNS] if is_counted else ()
    no_findings: list[tuple[str, str, str]] = []
    findings = safely(lambda: judge(name, run, thresholds, kind, configuration=configuration, walls=walls),
                      no_findings)
    if status == 0 and any(level == "error" for level, _, _ in findings):
        status, result = 1, "rejected"
    safely(lambda: report(name, finding_path, findings, warnings_dir), None)
    if record_path is not None:
        counts = () if run.instructions is None else ((cost_record.TEST_COUNT_KEY, run.instructions),)
        record = cost_record.TestRecord(name, result, program, run.exit_code, cost_record.measured(run, counts),
                                        walls if is_counted else earlier_walls,
                                        version if is_counted else earlier_version)
        safely(lambda: cost_record.write(record_path, record), None)
    return status


def traced_run(build_dir: str, name: str, command: Sequence[str], version: str,
               clean: Iterable[tuple[str, str]]) -> Plan | None:
    """Return the plan of a traced run of a test, or None when its read set is current (THE READ SET).

    Args:
        build_dir: The build directory of the test
        name: The name of the test
        command: The command of the test
        version: The version of the run (THE VERSION)
        clean: The clean environment of the test
    """
    from assay.loop import readset

    words = [version, *(f"{key}={value}" for key, value in clean)]
    return readset.plan(build_dir, name, command, version_of(words, ()))


def main(argv: list[str]) -> int:
    """Run the test of the command line, and end as it ended.

    Args:
        argv: [--warnings-dir DIR] [--build-dir BUILD] [--name NAME] [--emulator-words N] [--trace never]
            [EMULATOR...] PROGRAM [ARG]...

    Returns:
        2 on a usage error.  Else the function ends the process as the test ended
    """
    parsed = read_options(argv)
    program_at = None if parsed is None else program_index(*parsed)
    program_word = "" if parsed is None or program_at is None else parsed[1][program_at]
    name = "" if parsed is None else parsed[0].name or os.path.basename(program_word)
    trace = None if parsed is None else parsed[0].trace
    if parsed is None or program_at is None or not is_test_name(name) or trace not in (None, TRACE_NEVER):
        if parsed is None:
            problem = ""
        elif program_at is None:
            problem = (f"--emulator-words {parsed[0].emulator_words!r} is not a count of the words of the emulator "
                       f"before the program.  ")
        elif trace not in (None, TRACE_NEVER):
            problem = f"--trace takes the value {TRACE_NEVER}, and it got {trace!r}.  "
        else:
            problem = (f"the test name {name!r} is not a word of letters, digits, '_', '.' and '-' that starts with a "
                       f"letter, a digit or '_'.  ")
        print(f"loop test-step: {problem}{USAGE}", file=sys.stderr)
        return 2
    options, command = parsed
    # The program of the test is the word after its emulator (THE EMULATOR).  The full command runs.
    program = os.path.abspath(program_word) if os.sep in program_word else program_word
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
    # The version names the files as they are when the test starts.
    version = safely(lambda: version_of(command, command_files(command)), "")
    if not keeps_cores():
        stop_core_dumps()
    # The launcher puts the clean environment and the variable in its own environment, which the test gets.  A copy of
    # the environment for the child costs more.
    clean = canonical_environment(os.environ)
    makeflags = os.environ.get(cost_meter.MAKEFLAGS_ENV)
    profiles = {variable: os.environ[variable] for variable in PROFILE_NAMES if os.environ.get(variable)}
    os.environ.clear()
    os.environ.update(clean)
    os.environ[LAUNCHED_ENV] = name
    if makeflags:
        os.environ[cost_meter.TEST_JOBSERVER_ENV] = makeflags
    os.environ.update(profiles)
    traced_dir = None if trace == TRACE_NEVER or has_derived_inputs(command) else build_dir
    plan = None if traced_dir is None else safely(lambda: traced_run(traced_dir, name, command, version, clean), None)
    if plan is not None:
        os.environ[TRACED_ENV] = "1"
    try:
        run = cost_meter.measure(command, count_instructions=True, supervisor=None if plan is None else plan.tracer)
    except OSError as error:
        print(f"loop test-step: the test {command[0]} cannot start: {error}", file=sys.stderr)
        sys.exit(127)
    if plan is not None:
        safely(lambda: plan.save(run.exit_code), None)
    status = judge_and_record(name, program, place, build_dir, run, options.warnings_dir, budget, version=version)
    stop_core_dumps()
    cost_meter.end_like(status)
    return status
