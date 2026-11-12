"""loop step — run one compile or one link of the build, record its cost, and hold its budget.

cmake/BuildLauncher.cmake puts this subcommand in front of each C and C++
compile, in front of ccache when ccache is the compiler launcher, and in
front of each link of an executable or a shared library:

    python3 -S utils/bin/loop step [ccache [KEY=VALUE]...] COMPILER ARGS... -o OBJECT -c SOURCE
    python3 -S utils/bin/loop step --link LINKER ARGS... -o OUTPUT

WHAT THE SUBCOMMAND DOES
    It runs the command through utils/assay/core/meter.py, with the same
    standard streams, so the output of the step is the output of the
    command.  It writes the record OUTPUT.cost with assay.core.record, and it
    exits with the status of the command.  The record of a compile
    also holds the user instructions of the compiler run when the host gives
    an exact count (meter.py, THE INSTRUCTION COUNT), which costs
    approximately 1 ms.  When the command ends on a signal, the subcommand
    ends on the same signal.  A failure to measure or to write the record
    never changes the status and prints nothing.  The rules that read the
    record, such as step-cost, find a missing record, and they fail.  A command with no -o, or a compile with no -c, runs in its own
    place, and the subcommand records nothing.

THE MEASURED RULES
    A compile reads the limit of the kind compile of the measured rule
    step-memory of utils/registry.txt, and a link reads the limit of the
    kind link (assay.core.budget, THE STEP RULES).
      * The CPU time.  Each process of the step gets RLIMIT_CPU.  The limit
        of a compile is COMPILE_CPU_LIMIT_S, 600 s, and the limit of a link
        is LINK_CPU_LIMIT_S, 150 s.  Each limit is a hard stop for a step
        that does not end, and not a budget.  No rule judges the CPU time of
        a step, because it changes with the load of the host.  The kind
        compile of the rule step-cost judges the cost of a compile after the
        build.
        A step that reaches its limit ends on SIGKILL, and the subcommand
        prints an error line that says so.  The line names the check
        loop-step, because no rule holds the limit.  The limit counts the
        user time and the system time.  When the host is short of memory, a
        process can stay in the kernel for a long time, and that time is
        system time.  On 2026-10-12, a compile of 6.6 s of CPU time used
        69.2 s of system time and no user time.  Then a limit of 60 s stopped
        it.  A link of 0.4 s used 16.1 s before a limit of 15 s stopped it.
        The limit is only for a step that does not end, and it is far above
        such times.
        On a GitHub runner, the step gets no limit, and a step past its
        limit gives a warning line that says that the time error was demoted
        (utils/assay/core/meter.py, A CI RUNNER).
        Off a CI runner, a step past that limit with no limit on it (the
        operating system refused the limit) fails after the step, as the
        limit would stop it.
      * The peak memory, which is almost the same on each run of one step.
        Above the limit, the subcommand removes the output, prints an error
        line and exits with status 1, unless a row of the ledger
        utils/ledgers/step-memory.txt admits the output, with a reason:
        '@build/OUTPUT | STEP | GB | - | REASON', with OUTPUT relative to the
        build directory and STEP compile or link (meter.py, THE REGISTRY AND
        THE LEDGERS).  The subcommand reads that ledger only for a step above the
        limit.  A step that a row admits prints no line.  The subcommand does
        not judge the memory of an output in the directory bench of the build
        (assay.core.budget, THE SCOPE).
    The subcommand prints at most one line, after the output of the command,
    in the format of utils/assay/core/report.py.  A GB is 2^30 bytes.

A CCACHE HIT
    A compile sets CCACHE_STATSLOG to OBJECT.ccache-stats, and
    assay.readers.ccache reads that log.  A hit counter in the log means that
    ccache gave a stored object and the compiler did not run.  The record
    then says "hit" and holds no time of its own.  A hit has no memory to
    judge.

THE COST STORE
    After each compile through ccache that ran the compiler, the subcommand
    writes the cost block of the step into the cost store
    (assay.core.cost_store).  The key of the entry holds the object and the
    command.  A hit reads the block of its key, and its record keeps the
    block as "last_cost".  The compile that made the object can be in a
    different build directory or work tree.  The compiler meter of the miss
    writes an entry before ccache stores the object, and the subcommand
    gives the meter the inputs of the key (THE ORDER of
    assay.core.cost_store).  A step that the subcommand rejects drops the
    entry of its key.

A HIT WITH NO COST
    The store can hold no block of the key of a hit, or a block that does
    not read.  An example is an entry that the bound of the store dropped,
    while ccache keeps the object.  The subcommand then runs the step again
    with CCACHE_RECACHE=1, so ccache runs the compiler and stores the object
    again.  The record and the cost store get the cost of that second call,
    and the subcommand judges it as each other compile.  The rule
    instruction-total then gets a count of each object.  The second call
    runs only when the cost store is on and the host gives an exact counter
    (meter.py, THE INSTRUCTION COUNT).  Else the hit has no last cost, as a
    compile on such a host has no count.

    The subcommand imports the store and hashlib only for a compile through
    ccache.  On 2026-10-11 the store added a median of 15 ms to a step of
    the largest object of the Debug build, 10.74 MB.  The host was busy, and
    the 21 runs of a hit and of a miss each added 9 ms at the minimum.

A COMPILE THROUGH CCACHE
    A compile whose command starts with a program named ccache runs with the
    environment of meter.compiler_meter_environment(), so the meter of `loop
    meter-compiler` counts the compiler run of a miss alone and writes the
    count to OBJECT.compiler-count.  The launcher reads and removes that file
    after the step (meter.py, A COMPILE THROUGH CCACHE).  The record then gives the count of the compiler run as
    compiler_instructions, and the rest of the step as ccache_instructions.
    For a compile with no ccache, the count of the step is the count of the
    compiler run.

The variables CRUCIBLE_BUILD_REGISTRY and CRUCIBLE_BUILD_LEDGERS name another
registry and another directory of ledgers, which holds RULE.txt for each
planted ledger.  utils/assay/tests/loop/test_step.py uses them.
"""

import os
import sys

from assay.core import budget as cost_budget
from assay.core import meter as cost_meter
from assay.core import record as cost_record
from assay.readers import ccache

# The measured rule of the peak memory of a step.  The kind of a step is its name, compile or link (THE MEASURED RULES).
MEMORY_RULE = cost_budget.STEP_MEMORY
# The CPU limit of each process of a compile, in seconds.  This limit is a hard stop for a compile that does not end,
# and not a budget (THE MEASURED RULES).  On 2026-11-11, the compile of bench/bench_cipher.cpp took 31.5 s of CPU time
# at 224 jobs and 17.5 s alone.
COMPILE_CPU_LIMIT_S = 600
# The CPU limit of each process of a link, in seconds: a hard stop for a link that does not end (THE MEASURED RULES).
LINK_CPU_LIMIT_S = 150
CPU_LIMITS_S = {"compile": COMPILE_CPU_LIMIT_S, "link": LINK_CPU_LIMIT_S}
# The check that the line of a step at its CPU limit names.  The step meter gives the line, and no rule holds it.
LIMIT_CHECK = "loop-step"
# The name of the program of a compile command that runs ccache (cmake/Ccache.cmake uses the same rule).
CCACHE_NAME = "ccache"
# The variable that makes ccache run the compiler and store its object again, for a hit with no cost (A HIT WITH NO
# COST).
RECACHE_ENV = "CCACHE_RECACHE"
REGISTRY_ENV = "CRUCIBLE_BUILD_REGISTRY"
LEDGERS_ENV = "CRUCIBLE_BUILD_LEDGERS"
# The directory utils.  This module is utils/assay/loop/step.py.  It calculates the paths with os.path and not with
# assay.core.paths, because each compile and each link imports it.
UTILS = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# The registry of the measured rules, utils/registry.txt.
REGISTRY_FILE = os.path.join(UTILS, "registry.txt")
# The source root of the build, which CMake gives the launcher as the directory above utils (THE COST STORE).
SOURCE_ROOT = os.path.dirname(UTILS)


def step_paths(step: str, command: list[str]) -> tuple[str, str | None] | None:
    """Find the output and the source of one compile or link command.

    Args:
        step: "compile" or "link"
        command: The command, from the first word of the launcher chain

    Returns:
        The absolute output and the absolute source (None for a link), or None when the command does not
        make one output
    """
    output = source = None
    index = 1
    while index < len(command):
        word = command[index]
        if word == "-o" and index + 1 < len(command):
            output = command[index + 1]
            index += 1
        elif word.startswith("-o") and len(word) > 2:
            output = word[2:]
        elif word == "-c" and index + 1 < len(command) and not command[index + 1].startswith("-"):
            source = command[index + 1]
            index += 1
        index += 1
    if output is None or (step == "compile" and source is None):
        return None
    return os.path.abspath(output), None if source is None else os.path.abspath(source)


def build_item(output: str) -> str:
    """Return an output relative to the build directory that holds it, or the output when no build directory holds it.

    Args:
        output: The absolute output path
    """
    build_dir = cost_meter.build_dir_of(output)
    return output if build_dir is None else os.path.relpath(output, build_dir)


def admission(step: str, output: str, peak_gb: float) -> tuple[bool, str]:
    """Look for the ledger row of the kind of the step that keeps an output above the limit of that kind.

    A row keeps the output up to its kept value with the noise of the unit GB (assay.core.budget, THE NOISE).

    Args:
        step: "compile" or "link", the kind of the row
        output: The absolute output path
        peak_gb: The peak memory of the step, in GB

    Returns:
        Whether a row keeps the output, and the words that tell which row or why none does.  A problem of the
        ledger keeps nothing, and the words give its line
    """
    ledger = cost_meter.ledger_file(MEMORY_RULE, os.environ.get(LEDGERS_ENV))
    shown = cost_meter.shown_ledger(ledger)
    build_dir = cost_meter.build_dir_of(output)
    if build_dir is None:
        return False, (f"no build directory holds the output (no directory above it holds {cost_meter.KIND_FILE}), so "
                       f"no row of {shown} can admit it")
    item = os.path.relpath(output, build_dir)
    row, shown, problem = cost_budget.row_of(MEMORY_RULE, step, cost_meter.ITEM_OUTPUT, ledger, item)
    if row is None:
        words = f"no row of the kind {step} of {shown} keeps {item}"
        if problem:
            words += f", and the ledger has a problem at {shown}:{problem}"
        return False, words
    noise = cost_budget.noise_of("GB")
    if peak_gb > row.value * (1.0 + noise):
        return False, (f"the row {shown}:{row.line} keeps it only up to {row.value:g} GB with the noise {noise:.0%}: "
                       f"{row.reason}")
    return True, f"the row {shown}:{row.line} keeps it up to {row.value:g} GB with the noise {noise:.0%}: {row.reason}"


def cpu_limit(step: str) -> tuple[int, str]:
    """Return the CPU limit of each process of one step in seconds, and the words that tell the limit.

    Args:
        step: "compile" or "link"

    Returns:
        COMPILE_CPU_LIMIT_S or LINK_CPU_LIMIT_S, the hard stop of the step (THE MEASURED RULES)
    """
    return CPU_LIMITS_S[step], f"the hard stop for a {step} that does not end"


def judge(step: str, output: str, run: cost_meter.Measurement, budget: dict[str, float],
          hit: bool) -> tuple[str, tuple[str, str, str] | None]:
    """Decide the result of one step and the one line to print.

    Args:
        step: "compile" or "link"
        output: The absolute output path
        run: The measurement of the command
        budget: The limit of each kind of the rule step-memory that the registry gives
        hit: Whether ccache gave the output

    Returns:
        The result ("built", "failed", "rejected" or "hit"), and the level, the check and the message of the
        line, or None
    """
    if hit:
        return "hit", None
    limit, limit_words = cpu_limit(step)
    if run.exit_code != 0:
        if run.reached_cpu_limit:
            return "failed", ("error", LIMIT_CHECK,
                              f"the {step} job used {run.cpu_s:.1f} s CPU and reached the hard limit of "
                              f"{run.cpu_limit_s} s, {limit_words}, so the operating system stopped it")
        return "failed", None
    is_over = step in budget and cost_budget.verdict(run.peak_gb, budget[step], None) == cost_budget.ERROR
    if is_over and cost_budget.is_judged(build_item(output)):
        is_admitted, words = admission(step, output, run.peak_gb)
        if is_admitted:
            return "built", None
        message = (f"the {step} job took {run.peak_gb:.2f} GB of peak memory, above the limit of the kind {step} "
                   f"{budget[step]:g} GB, and {words}")
        return "rejected", ("error", MEMORY_RULE, f"{message}.  The launcher removed the output.  Make the step "
                                                  f"smaller, or add a row with a reason")
    if run.cpu_limit_s is None and run.cpu_s > limit:
        # On a GitHub runner, limit_cpu() of meter.py sets no limit, and the step runs to its end (A CI RUNNER).
        message = (f"the {step} job used {run.cpu_s:.1f} s CPU, more than the hard limit of {limit} s, "
                   f"{limit_words}, and no CPU limit stopped it")
        if cost_meter.is_github_actions():
            # A runner is slower than the build host, so the error of the hard stop is a warning there.
            return "built", ("warning", LIMIT_CHECK, f"{message}.  {cost_meter.CI_DEMOTION}.")
        return "rejected", ("error", LIMIT_CHECK, f"{message}.  The launcher removed the output")
    return "built", None


def compile_counts(run: cost_meter.Measurement, is_ccache: bool,
                   count_path: str | None) -> tuple[tuple[str, int], ...]:
    """Return the instruction counts of the cost block of one compile (A COMPILE THROUGH CCACHE).

    Args:
        run: The measurement of the command
        is_ccache: Whether the command runs ccache
        count_path: The file of the counts of the meter, or None when the call had no meter

    Returns:
        The key and the value of each exact count: the compiler run, and for a ccache miss the rest of the step.
        No count when the compiler run has no exact count
    """
    if not is_ccache:
        return () if run.instructions is None else ((cost_record.COMPILER_COUNT_KEY, run.instructions),)
    if count_path is None:
        return ()
    has_run, compiler = cost_meter.read_compiler_count(count_path)
    if not has_run or compiler is None:
        return ()
    counts = [(cost_record.COMPILER_COUNT_KEY, compiler)]
    if run.instructions is not None and run.instructions >= compiler:
        counts.append((cost_record.CCACHE_COUNT_KEY, run.instructions - compiler))
    return tuple(counts)


def is_ccache_hit(stats_path: str, outer_log: str | None) -> bool:
    """Return True when ccache gave a stored object, from the stats log of one call, and remove the log (A CCACHE HIT).

    Args:
        stats_path: The stats log of the call
        outer_log: The stats log that the environment of the build named, or None.  The
            counters of the call go to it too, and that log then holds each call of the build

    Returns:
        True when assay.readers.ccache reads a hit counter in the log
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
    return ccache.parse_stats_log(text).is_hit


def remembered_cost(output: str, command: list[str], result: str, run: cost_meter.Measurement,
                    counts: tuple[tuple[str, int], ...]) -> cost_record.Cost | None:
    """Write the cost of a compile through ccache into the cost store, or read the cost of a hit from it.

    THE COST STORE gives the rules.  A built compile writes its cost block
    with the key of its object and its command.  A hit reads the block of
    its key.

    Args:
        output: The absolute output path
        command: The command of the step, from the first word after the launcher
        result: The result of judge(): "built" or "hit"
        run: The measurement of the command
        counts: The instruction counts of the cost block, from compile_counts()

    Returns:
        For a hit, the cost of the compile that made the object, or None when the store holds no cost of its key.
        For a built compile, None

    Raises:
        OSError: If the object cannot be read, or the store cannot be written
    """
    from assay.core import cost_store

    key = cost_store.key_of(output, command, cost_meter.build_dir_of(output), SOURCE_ROOT)
    if key is None:
        return None
    if result != cost_record.HIT:
        cost_store.put(key, cost_record.cost_text(cost_record.measured(run, counts)))
        return None
    block = cost_store.get(key, run.end)
    return None if block is None else cost_record.cost_from_text(block)


def cost_variables(output: str, command: list[str], budget: dict[str, float]) -> dict[str, str]:
    """Return the variable that gives the compiler meter the inputs of the key of a compile (THE COST STORE).

    The memory limit of the inputs is the limit of the kind compile, or none for an output that no rule judges.

    Args:
        output: The absolute output path
        command: The command of the step, from the first word after the launcher
        budget: The limit of each kind of the rule step-memory that the registry gives
    """
    from assay.core import cost_store

    limit = budget.get("compile") if cost_budget.is_judged(build_item(output)) else None
    text = cost_store.key_inputs(output, command, cost_meter.build_dir_of(output), SOURCE_ROOT, limit)
    return {cost_store.KEY_ENV: text}


def drop_cost(output: str, command: list[str]) -> None:
    """Remove the entry of the cost store of a rejected compile through ccache, before the removal of its output.

    A hit of its object then has no cost, and a hit with no cost compiles again (A HIT WITH NO COST).

    Raises:
        OSError: If the object cannot be read
    """
    from assay.core import cost_store

    key = cost_store.key_of(output, command, cost_meter.build_dir_of(output), SOURCE_ROOT)
    if key is not None:
        cost_store.drop(key)


def compiles_again(is_ccache: bool, result: str, last_cost: cost_record.Cost | None) -> bool:
    """Tell if a compile through ccache is a hit with no cost that a second call can measure (A HIT WITH NO COST).

    Args:
        is_ccache: Whether the command runs ccache
        result: The result of judge()
        last_cost: The cost of the hit from remembered_cost(), or None
    """
    if not is_ccache or result != cost_record.HIT or last_cost is not None:
        return False
    from assay.core import cost_store

    return cost_store.store_directory() is not None and cost_meter.host_gives_counter()


class Call:
    """One run of the command of a step and its verdict (A HIT WITH NO COST).

    `stats_path`, `outer_log` and `count_path` are the stats log of the call, the stats log of the build and the file
    of the counts of the meter, or None.  judged() gives the result, the line, the counts and the last cost.
    """

    __slots__ = ("count_path", "counts", "last_cost", "line", "outer_log", "result", "run", "stats_path")

    def __init__(self, run: cost_meter.Measurement, stats_path: str | None, outer_log: str | None,
                 count_path: str | None) -> None:
        """Hold the measurement and its files, with the result of its status and no line, count or last cost."""
        self.run = run
        self.stats_path = stats_path
        self.outer_log = outer_log
        self.count_path = count_path
        self.result = "built" if run.exit_code == 0 else "failed"
        self.line: tuple[str, str, str] | None = None
        self.counts: tuple[tuple[str, int], ...] = ()
        self.last_cost: cost_record.Cost | None = None


def called(step: cost_record.Step, arguments: list[str], output: str, budget: dict[str, float], is_ccache: bool,
           extra: dict[str, str]) -> Call:
    """Run the command of a step one time, with the stats log of the call and the meter of a compile through ccache.

    Args:
        step: COMPILE or LINK of assay.core.record
        arguments: The command, from the first word after the launcher
        output: The absolute output path
        budget: The limit of each kind of the rule step-memory that the registry gives
        is_ccache: Whether the command runs ccache
        extra: More variables of the environment of a compile, such as RECACHE_ENV

    Raises:
        OSError: If the command cannot start
    """
    environment = None
    stats_path = outer_log = count_path = None
    if step == "compile":
        stats_path = output + ".ccache-stats"
        try:
            os.unlink(stats_path)
        except OSError:
            pass
        outer_log = os.environ.get("CCACHE_STATSLOG")
        environment = dict(os.environ, CCACHE_STATSLOG=stats_path, **extra)
    if is_ccache and environment is not None:
        count_path = output + cost_meter.COMPILER_COUNT_SUFFIX
        try:
            os.unlink(count_path)
        except OSError:
            pass
        metered = cost_meter.compiler_meter_environment(environment, count_path)
        if metered is None:
            count_path = None
        else:
            environment = {**metered, **cost_variables(output, arguments, budget)}
    run = cost_meter.measure(arguments, environment, CPU_LIMITS_S[step], count_instructions=step == "compile")
    return Call(run, stats_path, outer_log, count_path)


def judged(call: Call, step: cost_record.Step, arguments: list[str], output: str, budget: dict[str, float],
           is_ccache: bool) -> None:
    """Give a call its result, its line, its counts and its last cost, and keep or drop the cost of a compile.

    Raises:
        OSError: If a file of the call, the object or the cost store cannot be read or written
    """
    if step == "compile":
        call.counts = compile_counts(call.run, is_ccache, call.count_path)
    hit = call.stats_path is not None and is_ccache_hit(call.stats_path, call.outer_log)
    call.result, call.line = judge(step, output, call.run, budget, hit)
    if is_ccache and call.result in (cost_record.HIT, "built"):
        call.last_cost = remembered_cost(output, arguments, call.result, call.run, call.counts)
    if is_ccache and call.result == "rejected":
        drop_cost(output, arguments)


def step_record(step: cost_record.Step, output: str, source: str | None, run: cost_meter.Measurement, result: str,
                counts: tuple[tuple[str, int], ...] = (),
                last_cost: cost_record.Cost | None = None) -> cost_record.CostRecord:
    """Return the record of one step: its fields, the bytes of its output, and its cost block when it is not a hit.

    Args:
        step: COMPILE or LINK of assay.core.record
        output: The absolute output path
        source: The absolute source of a compile, or None
        run: The measurement of the command
        result: The result of judge()
        counts: The instruction counts of the cost block, from compile_counts()
        last_cost: For a hit, the cost of the compile that made the object, from remembered_cost(), or None
    """
    try:
        size: int | None = os.stat(output).st_size
    except OSError:
        size = None
    is_hit = result == cost_record.HIT
    cost = None if is_hit else cost_record.measured(run, counts)
    return cost_record.CostRecord(step, result, source, output, size, run.exit_code, cost,
                                  last_cost if is_hit else None)


def main(argv: list[str]) -> int:
    """Run the step of the command line, and end as it ended.

    Args:
        argv: [--link] COMMAND [ARG]...

    Returns:
        2 on a usage error.  Else the function ends the process as the command ended
    """
    arguments = argv
    step: cost_record.Step = cost_record.COMPILE
    if arguments[:1] == ["--link"]:
        step, arguments = cost_record.LINK, arguments[1:]
    if not arguments:
        print("usage: loop step [--link] COMMAND [ARG]...", file=sys.stderr)
        return 2
    paths = step_paths(step, arguments)
    if paths is None:
        try:
            os.execvp(arguments[0], arguments)
        except OSError as error:
            print(f"loop step: the command {arguments[0]} cannot start: {error}", file=sys.stderr)
            sys.exit(127)
    output, source = paths
    try:
        budget = cost_meter.budget_rows(os.environ.get(REGISTRY_ENV) or REGISTRY_FILE, (MEMORY_RULE,)).get(MEMORY_RULE, {})
    except (OSError, ValueError):
        budget = {}
    is_ccache = step == "compile" and os.path.basename(arguments[0]) == CCACHE_NAME
    # A hit with no cost runs the command a second time with RECACHE_ENV (A HIT WITH NO COST).
    extra: dict[str, str] = {}
    while True:
        try:
            call = called(step, arguments, output, budget, is_ccache, extra)
        except OSError as error:
            print(f"loop step: the command {arguments[0]} cannot start: {error}", file=sys.stderr)
            sys.exit(127)
        # A failure inside the measurement keeps the status of the command: a
        # measurement never fails a step.  Only a decided rejection changes it.
        is_again = False
        try:
            judged(call, step, arguments, output, budget, is_ccache)
            is_again = not extra and compiles_again(is_ccache, call.result, call.last_cost)
        except BaseException:  # noqa: BLE001
            pass
        if not is_again:
            break
        extra = {RECACHE_ENV: "1"}
    status = call.run.exit_code
    if call.result == "rejected":
        status = 1
        try:
            os.unlink(output)
        except OSError:
            pass
    try:
        if call.line is not None:
            level, check, message = call.line
            print(cost_meter.finding_text(level, source or output, check, message), file=sys.stderr)
    except BaseException:  # noqa: BLE001
        pass
    try:
        # The record of an output is OUTPUT.cost.  record_file() gives it as a Path, and pathlib costs an import.
        cost_record.write(output + cost_record.SUFFIX,
                          step_record(step, output, source, call.run, call.result, call.counts, call.last_cost))
    except BaseException:  # noqa: BLE001
        pass
    cost_meter.end_like(status)
    return status
