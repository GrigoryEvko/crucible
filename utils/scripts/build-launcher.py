#!/usr/bin/env python3
"""build-launcher — run one compile or one link of the build, record its cost, and hold its budget.

cmake/BuildLauncher.cmake puts this script in front of each C and C++
compile, in front of ccache when ccache is the compiler launcher, and in
front of each link of an executable or a shared library:

    python3 -S build-launcher.py [ccache [KEY=VALUE]...] COMPILER ARGS... -o OBJECT -c SOURCE
    python3 -S build-launcher.py --link LINKER ARGS... -o OUTPUT

WHAT THE SCRIPT DOES
    It runs the command through utils/scripts/cost_meter.py, with the same
    standard streams, so the output of the step is the output of the
    command.  It writes the record OUTPUT.cost (the format is in
    cost_meter.py), and it exits with the status of the command.  When the
    command ends on a signal, the script ends on the same signal.  A failure
    to measure or to write the record never changes the status and prints
    nothing.  A command with no -o, or a compile with no -c, runs in its own
    place, and the script records nothing.

THE BUDGET ROWS
    A compile reads the rows compile-cpu and compile-memory of
    utils/scripts/budgets.txt, and a link reads the rows link-time and
    link-memory.
      * The CPU time.  Each process of the step gets RLIMIT_CPU at three
        times the error threshold of the time row.  A step that reaches the
        limit ends on SIGKILL, and the script prints an error line that says
        so.  Below the limit, a time over the warning threshold gives a
        warning line.  The error threshold itself is a check after the build,
        because the CPU time of one step depends on the load of the host.
      * The peak memory, which is almost the same on each run of one step.
        Over the error threshold, the script removes the output, prints an
        error line and exits with status 1, unless a row of
        utils/scripts/<memory row>-ledger.txt admits the output for the kind
        of the build, with a reason.  Over the warning threshold, it prints a
        warning line.
    The script prints at most one line, after the output of the command, in
    the format of utils/scripts/check_report.py.  A GB is 2^30 bytes.

A CCACHE HIT
    A compile sets CCACHE_STATSLOG to OBJECT.ccache-stats.  A hit counter in
    that log means that ccache gave a stored object and the compiler did not
    run.  The record then says "hit" and holds no time, and it keeps the cost
    of the last real compile of the earlier record as "last_cost".  A hit has
    no memory to judge.

The variables CRUCIBLE_BUILD_BUDGETS and CRUCIBLE_BUILD_LEDGERS name another
budget table and another directory of ledgers.  test/build_launcher_test.py
uses them.
"""

import os
import sys

import cost_meter

STEP_ROWS = {"compile": ("compile-cpu", "compile-memory"), "link": ("link-time", "link-memory")}
CPU_LIMIT_FACTOR = 3
BUDGETS_ENV = "CRUCIBLE_BUILD_BUDGETS"
LEDGERS_ENV = "CRUCIBLE_BUILD_LEDGERS"
SCRIPTS = os.path.dirname(os.path.abspath(__file__))


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


def admission(memory_row: str, output: str) -> tuple[bool, str]:
    """Look for the ledger row that admits an output over the error threshold of its memory row.

    Args:
        memory_row: compile-memory or link-memory
        output: The absolute output path

    Returns:
        Whether a row admits the output, and the words that tell which row or why none does
    """
    ledger = os.path.join(os.environ.get(LEDGERS_ENV) or SCRIPTS, f"{memory_row}-ledger.txt")
    repository = os.path.dirname(os.path.dirname(SCRIPTS))
    shown = os.path.relpath(ledger, repository) if ledger.startswith(repository + os.sep) else ledger
    build_dir = cost_meter.build_dir_of(output)
    kind = cost_meter.read_kind(build_dir) if build_dir else None
    if build_dir is None or kind is None:
        return False, (f"the build kind is not known ({cost_meter.KIND_FILE} of the build directory), so no row of "
                       f"{shown} can admit it")
    item = os.path.relpath(output, build_dir)
    try:
        rows, _, _ = cost_meter.read_ledger_rows(ledger)
    except OSError:
        rows = {}
    row = rows.get((kind, item))
    if row is None:
        return False, f"no row of {shown} admits {item} for the kind {kind}"
    return True, f"the row {shown}:{row[0]} admits it: {row[2]}"


def judge(step: str, output: str, run: cost_meter.Measurement, budget: dict[str, tuple[float, float]],
          hit: bool) -> tuple[str, tuple[str, str, str] | None]:
    """Decide the result of one step and the one line to print.

    Args:
        step: "compile" or "link"
        output: The absolute output path
        run: The measurement of the command
        budget: The thresholds of the rows of the step that the budget table gives
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
    if memory_row in budget and run.peak_gb > budget[memory_row][1]:
        is_admitted, words = admission(memory_row, output)
        message = (f"the {step} job took {run.peak_gb:.2f} GB of peak memory, over the error threshold of "
                   f"{budget[memory_row][1]:g} GB, and {words}")
        if is_admitted:
            return "built", ("warning", memory_row, message)
        return "rejected", ("error", memory_row, f"{message}.  The launcher removed the output.  Make the step "
                                                 f"smaller, or add a row with a reason")
    parts = []
    check = None
    for row, value, text, unit in ((time_row, run.cpu_s, f"{run.cpu_s:.1f} s CPU", "s"),
                                   (memory_row, run.peak_gb, f"{run.peak_gb:.2f} GB of peak memory", "GB")):
        if row in budget and value > budget[row][0]:
            check = check or row
            level, limit = ("error", budget[row][1]) if value > budget[row][1] else ("warning", budget[row][0])
            parts.append(f"{text}, over the {level} threshold of {limit:g} {unit}")
    if check is None:
        return "built", None
    return "built", ("warning", check, f"the {step} job took " + ", and ".join(parts))


def write(step: str, output: str, source: str | None, run: cost_meter.Measurement, result: str) -> None:
    """Write the record of one step beside its output.

    Args:
        step: "compile" or "link"
        output: The absolute output path
        source: The absolute source of a compile, or None
        run: The measurement of the command
        result: The result of judge()
    """
    record_path = output + cost_meter.RECORD_SUFFIX
    fields = [("format", str(cost_meter.RECORD_FORMAT)), ("step", f'"{step}"'), ("result", f'"{result}"')]
    if source is not None:
        fields.append(("source", cost_meter.quote(source)))
    fields.append(("output", cost_meter.quote(output)))
    try:
        fields.append(("output_bytes", str(os.stat(output).st_size)))
    except OSError:
        pass
    fields.append(("exit", str(run.exit_code)))
    if result == "hit":
        carried = cost_meter.carried_cost(record_path)
        if carried is not None:
            fields.append(("last_cost", carried))
    else:
        fields.append(("cost", cost_meter.cost_block(run)))
    cost_meter.write_record(record_path, fields)


def main() -> None:
    """Run the step of the command line, and end as it ended."""
    arguments = sys.argv[1:]
    step = "compile"
    if arguments[:1] == ["--link"]:
        step, arguments = "link", arguments[1:]
    if not arguments:
        print("usage: build-launcher.py [--link] COMMAND [ARG]...", file=sys.stderr)
        sys.exit(2)
    paths = step_paths(step, arguments)
    if paths is None:
        try:
            os.execvp(arguments[0], arguments)
        except OSError as error:
            print(f"build-launcher: the command {arguments[0]} cannot start: {error}", file=sys.stderr)
            sys.exit(127)
    output, source = paths
    time_row, memory_row = STEP_ROWS[step]
    try:
        budget = cost_meter.budget_rows(os.environ.get(BUDGETS_ENV) or os.path.join(SCRIPTS, "budgets.txt"),
                                        (time_row, memory_row))
    except (OSError, ValueError):
        budget = {}
    cpu_limit = CPU_LIMIT_FACTOR * budget[time_row][1] if time_row in budget else None
    environment = None
    stats_path = outer_log = None
    if step == "compile":
        stats_path = output + ".ccache-stats"
        try:
            os.unlink(stats_path)
        except OSError:
            pass
        outer_log = os.environ.get("CCACHE_STATSLOG")
        environment = dict(os.environ, CCACHE_STATSLOG=stats_path)
    try:
        run = cost_meter.measure(arguments, environment, cpu_limit)
    except OSError as error:
        print(f"build-launcher: the command {arguments[0]} cannot start: {error}", file=sys.stderr)
        sys.exit(127)
    # A failure inside the measurement keeps the status of the command: a
    # measurement never fails a step.  Only a decided rejection changes it.
    status = run.exit_code
    result = "built" if status == 0 else "failed"
    line = None
    try:
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
        write(step, output, source, run, result)
    except BaseException:  # noqa: BLE001
        pass
    cost_meter.end_like(status)


if __name__ == "__main__":
    main()
