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
    record, such as compile-cpu, find a missing record, and they fail.  A command with no -o, or a compile
    with no -c, runs in its own place, and the subcommand records nothing.

THE MEASURED RULES
    A compile reads the measured rules compile-cpu and compile-memory of
    utils/registry.txt, and a link reads the measured rules link-time and
    link-memory.
      * The CPU time.  Each process of the step gets RLIMIT_CPU at three
        times the error threshold of the time row.  A step that reaches the
        limit ends on SIGKILL, and the subcommand prints an error line that
        says so.  Below the limit, a time over the warning threshold gives a
        warning line.  The error threshold itself is a check after the build,
        because the CPU time of one step depends on the load of the host.
        When the record of a compile holds an instruction count, that check
        gives the error level to the count (the row compile-instructions),
        and the CPU time gives a warning only.
        On a GitHub runner, the step gets no limit, and a step past three
        times the error threshold gives a warning line that says that the
        time error was demoted (utils/assay/core/meter.py, A CI RUNNER).
        Off a CI runner, a step past that limit with no limit on it (the
        operating system refused the limit) fails after the step, as the
        limit would stop it.
      * The peak memory, which is almost the same on each run of one step.
        Over the error threshold, the subcommand removes the output, prints
        an error line and exits with status 1, unless a row of the ledger
        utils/ledgers/<memory rule>.txt admits the output for the kind of the
        build, with a reason: '@build/OUTPUT | KIND | GB | - | REASON', with
        OUTPUT relative to the build directory (meter.py, THE REGISTRY AND THE
        LEDGERS).  The subcommand reads that ledger only for a step over the
        error threshold.  Over the warning threshold, it prints a warning line.
    The subcommand prints at most one line, after the output of the command,
    in the format of utils/assay/core/report.py.  A GB is 2^30 bytes.

A CCACHE HIT
    A compile sets CCACHE_STATSLOG to OBJECT.ccache-stats.  A hit counter in
    that log means that ccache gave a stored object and the compiler did not
    run.  The record then says "hit" and holds no time, and it keeps the cost
    of the last real compile of the earlier record as "last_cost".  A hit has
    no memory to judge.

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

STEP_ROWS = {"compile": ("compile-cpu", "compile-memory"), "link": ("link-time", "link-memory")}
CPU_LIMIT_FACTOR = 3
# The name of the program of a compile command that runs ccache (cmake/Ccache.cmake uses the same rule).
CCACHE_NAME = "ccache"
REGISTRY_ENV = "CRUCIBLE_BUILD_REGISTRY"
LEDGERS_ENV = "CRUCIBLE_BUILD_LEDGERS"
# The directory utils.  This module is utils/assay/loop/step.py.  It calculates the paths with os.path and not with
# assay.core.paths, because each compile and each link imports it.
UTILS = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
# The registry of the measured rules, utils/registry.txt.
REGISTRY_FILE = os.path.join(UTILS, "registry.txt")


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


def admission(memory_row: str, output: str, peak_gb: float) -> tuple[bool, str]:
    """Look for the ledger row that keeps an output over the error threshold of its memory row.

    A row keeps the output up to its kept value with the noise of the unit GB (assay.core.budget, THE NOISE).

    Args:
        memory_row: compile-memory or link-memory
        output: The absolute output path
        peak_gb: The peak memory of the step, in GB

    Returns:
        Whether a row keeps the output, and the words that tell which row or why none does.  A problem of the
        ledger keeps nothing, and the words give its line
    """
    ledger = cost_meter.ledger_file(memory_row, os.environ.get(LEDGERS_ENV))
    shown = cost_meter.shown_ledger(ledger)
    build_dir = cost_meter.build_dir_of(output)
    kind = cost_meter.read_kind(build_dir) if build_dir else None
    if build_dir is None or kind is None:
        return False, (f"the build kind is not known ({cost_meter.KIND_FILE} of the build directory), so no row of "
                       f"{shown} can admit it")
    item = os.path.relpath(output, build_dir)
    configuration = cost_meter.read_configuration(build_dir)
    row, shown, problem = cost_budget.row_of(memory_row, cost_meter.ITEM_OUTPUT, ledger, kind, item, configuration)
    if row is None:
        words = f"no row of {shown} keeps {item} for {configuration or kind}"
        if problem:
            words += f", and the ledger has a problem at {shown}:{problem}"
        return False, words
    noise = cost_budget.noise_of("GB")
    if peak_gb > row.value * (1.0 + noise):
        return False, (f"the row {shown}:{row.line} keeps it only up to {row.value:g} GB with the noise {noise:.0%}: "
                       f"{row.reason}")
    return True, f"the row {shown}:{row.line} keeps it up to {row.value:g} GB with the noise {noise:.0%}: {row.reason}"


def judge(step: str, output: str, run: cost_meter.Measurement, budget: dict[str, tuple[float, float]],
          hit: bool) -> tuple[str, tuple[str, str, str] | None]:
    """Decide the result of one step and the one line to print.

    Args:
        step: "compile" or "link"
        output: The absolute output path
        run: The measurement of the command
        budget: The thresholds of the measured rules of the step that the registry gives
        hit: Whether ccache gave the output

    Returns:
        The result ("built", "failed", "rejected" or "hit"), and the level, the check and the message of the
        line, or None
    """
    time_row, memory_row = STEP_ROWS[step]
    if hit:
        return "hit", None
    if run.exit_code != 0:
        if run.reached_cpu_limit:
            return "failed", ("error", time_row,
                              f"the {step} job used {run.cpu_s:.1f} s CPU and reached the hard limit of "
                              f"{run.cpu_limit_s} s, three times the error threshold of {budget[time_row][1]:g} s "
                              f"(rounded up to a whole second), so the operating system stopped it")
        return "failed", None
    if memory_row in budget and cost_budget.verdict(run.peak_gb, *budget[memory_row], None) == cost_budget.ERROR:
        is_admitted, words = admission(memory_row, output, run.peak_gb)
        message = (f"the {step} job took {run.peak_gb:.2f} GB of peak memory, over the error threshold of "
                   f"{budget[memory_row][1]:g} GB, and {words}")
        if is_admitted:
            return "built", ("warning", memory_row, message)
        return "rejected", ("error", memory_row, f"{message}.  The launcher removed the output.  Make the step "
                                                 f"smaller, or add a row with a reason")
    if time_row in budget and run.cpu_limit_s is None and run.cpu_s > CPU_LIMIT_FACTOR * budget[time_row][1]:
        level, message = cost_meter.ci_verdict(
            "error", time_row, f"the {step} job used {run.cpu_s:.1f} s CPU, more than the hard limit of "
                               f"{CPU_LIMIT_FACTOR * budget[time_row][1]:g} s, three times the error threshold of "
                               f"{budget[time_row][1]:g} s, and no CPU limit stopped it")
        if level == "error":
            return "rejected", (level, time_row, f"{message}.  The launcher removed the output")
        return "built", (level, time_row, message)
    parts = []
    check = None
    for row, value, text, unit in ((time_row, run.cpu_s, f"{run.cpu_s:.1f} s CPU", "s"),
                                   (memory_row, run.peak_gb, f"{run.peak_gb:.2f} GB of peak memory", "GB")):
        found = cost_budget.verdict(value, *budget[row], None) if row in budget else cost_budget.NO_FINDING
        if found != cost_budget.NO_FINDING:
            check = check or row
            limit = budget[row][1] if found == cost_budget.ERROR else budget[row][0]
            parts.append(f"{text}, over the {found} threshold of {limit:g} {unit}")
    if check is None:
        return "built", None
    return "built", ("warning", check, f"the {step} job took " + ", and ".join(parts))


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


def step_record(step: cost_record.Step, output: str, source: str | None, run: cost_meter.Measurement, result: str,
                counts: tuple[tuple[str, int], ...] = ()) -> cost_record.CostRecord:
    """Return the record of one step: its fields, the bytes of its output, and its cost block when it is not a hit.

    A hit keeps the last cost of the earlier record of the output (assay.core.record, THE WRITE).

    Args:
        step: COMPILE or LINK of assay.core.record
        output: The absolute output path
        source: The absolute source of a compile, or None
        run: The measurement of the command
        result: The result of judge()
        counts: The instruction counts of the cost block, from compile_counts()
    """
    try:
        size: int | None = os.stat(output).st_size
    except OSError:
        size = None
    cost = None if result == cost_record.HIT else cost_record.measured(run, counts)
    return cost_record.CostRecord(step, result, source, output, size, run.exit_code, cost)


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
    time_row, memory_row = STEP_ROWS[step]
    try:
        budget = cost_meter.budget_rows(os.environ.get(REGISTRY_ENV) or REGISTRY_FILE, (time_row, memory_row))
    except (OSError, ValueError):
        budget = {}
    cpu_limit = CPU_LIMIT_FACTOR * budget[time_row][1] if time_row in budget else None
    environment = None
    stats_path = outer_log = count_path = None
    is_ccache = step == "compile" and os.path.basename(arguments[0]) == CCACHE_NAME
    if step == "compile":
        stats_path = output + ".ccache-stats"
        try:
            os.unlink(stats_path)
        except OSError:
            pass
        outer_log = os.environ.get("CCACHE_STATSLOG")
        environment = dict(os.environ, CCACHE_STATSLOG=stats_path)
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
            environment = metered
    try:
        run = cost_meter.measure(arguments, environment, cpu_limit, count_instructions=step == "compile")
    except OSError as error:
        print(f"loop step: the command {arguments[0]} cannot start: {error}", file=sys.stderr)
        sys.exit(127)
    # A failure inside the measurement keeps the status of the command: a
    # measurement never fails a step.  Only a decided rejection changes it.
    status = run.exit_code
    result = "built" if status == 0 else "failed"
    line = None
    counts: tuple[tuple[str, int], ...] = ()
    try:
        if step == "compile":
            counts = compile_counts(run, is_ccache, count_path)
        hit = stats_path is not None and cost_meter.ccache_hit(stats_path, outer_log)
        result, line = judge(step, output, run, budget, hit)
    except BaseException:  # noqa: BLE001
        pass
    if result == "rejected":
        status = 1
        try:
            os.unlink(output)
        except OSError:
            pass
    try:
        if line is not None:
            level, check, message = line
            print(cost_meter.finding_text(level, source or output, check, message), file=sys.stderr)
    except BaseException:  # noqa: BLE001
        pass
    try:
        # The record of an output is OUTPUT.cost.  record_file() gives it as a Path, and pathlib costs an import.
        cost_record.write(output + cost_record.SUFFIX, step_record(step, output, source, run, result, counts))
    except BaseException:  # noqa: BLE001
        pass
    cost_meter.end_like(status)
    return status
