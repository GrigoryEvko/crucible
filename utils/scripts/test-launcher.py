#!/usr/bin/env python3
"""test-launcher — run one test, record its cost, and hold its budget.

cmake/TestLauncher.cmake sets CMAKE_TEST_LAUNCHER, so CMake puts this script
in front of each test whose command is an executable target:

    python3 -S test-launcher.py --warnings-dir DIR EXECUTABLE ARG...

The name of the test is the file name of the executable.  CMake gives a
launcher no test name, and each executable test of the tree has the name of
its target.  A test whose command is a script, for example a guard or a
negative fixture, gets no launcher, and utils/scripts/check-test-time.py
reads its wall time from the JUnit report of the run.

WHAT THE SCRIPT DOES
    It runs the command through utils/scripts/cost_meter.py, with the same
    standard streams.  It writes the record EXECUTABLE.test.cost (the format
    is in cost_meter.py, with the step "test"), and it ends as the command
    ended: with its status, or on its signal.  A failure to measure, to read
    the budget or to write the record never changes the status.

THE BUDGET ROWS
    test-time is the wall time of the test, and test-memory is the peak
    resident memory of its largest process (utils/scripts/budgets.txt).
      * Over the warning threshold of a row, the script prints a warning
        line in the format of utils/scripts/check_report.py and writes it to
        DIR/ROW.NAME.txt, so the tests that run at the same time do not write
        one file.  Under the warning threshold, it removes that file.
      * Over the error threshold, it prints an error line, and a test that
        passed fails with status 1, unless a row of
        utils/scripts/ROW-ledger.txt admits the test for the kind of the
        build (`kind | test | value | reason`).  An admitted test gives a
        warning.  A row whose test stays at or below the error threshold is
        an error too, so that the row goes.
    Memory is almost the same on each run, so its error threshold sits above
    the largest measure.  Wall time depends on the load of the host, so its
    error threshold sits above the noise that the commit of the row states,
    and the timeout of the test preset is the hard stop.  In a build of a
    kind with tsan, a wall time over the error threshold gives a warning
    only: ThreadSanitizer serializes the threads of a test, and one test took
    15 s in one run and 91 s in the next on the shared host.
    A test that failed keeps its status, so a failure, a skip code and a
    WILL_FAIL test keep the meaning that ctest gives them.  A WILL_FAIL test
    over its budget therefore does not fail.  No executable test of the tree
    has WILL_FAIL at this time.

The variables CRUCIBLE_TEST_BUDGETS and CRUCIBLE_TEST_LEDGERS name another
budget table and another directory of ledgers, for the self-test.

    test-launcher.py --self-test
"""

import os
import sys

import cost_meter

ROWS = ("test-time", "test-memory")
BUDGETS_ENV = "CRUCIBLE_TEST_BUDGETS"
LEDGERS_ENV = "CRUCIBLE_TEST_LEDGERS"
SCRIPTS = os.path.dirname(os.path.abspath(__file__))
RECORD_SUFFIX = ".test" + cost_meter.RECORD_SUFFIX


def ledger_row(row: str, name: str, kind: str | None) -> tuple[str, str] | None:
    """Return the place and the reason of the ledger row that names a test for the kind of the build.

    Args:
        row: test-time or test-memory
        name: The name of the test
        kind: The kind of the build, or None when it is not known

    Returns:
        The place of the row (path:line) and its reason, or None
    """
    ledger = os.path.join(os.environ.get(LEDGERS_ENV) or SCRIPTS, f"{row}-ledger.txt")
    if kind is None:
        return None
    try:
        rows, _, _ = cost_meter.read_ledger_rows(ledger)
    except OSError:
        return None
    found = rows.get((kind, name))
    if found is None:
        return None
    repository = os.path.dirname(os.path.dirname(SCRIPTS))
    shown = os.path.relpath(ledger, repository) if ledger.startswith(repository + os.sep) else ledger
    return f"{shown}:{found[0]}", found[2]


def judge(name: str, run: cost_meter.Measurement, budget: dict[str, tuple[float, float]],
          kind: str | None) -> list[tuple[str, str, str]]:
    """Compare the measures of one test with its budget rows.

    Args:
        name: The name of the test
        run: The measurement of the command
        budget: The thresholds of the rows that the budget table gives
        kind: The kind of the build, or None

    Returns:
        One (level, row, message) for each row with a finding
    """
    findings = []
    for row, value, unit, text in (("test-time", run.wall_s, "s", f"{run.wall_s:.2f} s of wall time"),
                                   ("test-memory", run.peak_gb, "GB", f"{run.peak_gb:.3f} GB of peak memory")):
        if row not in budget:
            continue
        warn, error = budget[row]
        admitted = ledger_row(row, name, kind)
        said = f"the test {name} took {text}"
        if admitted is not None and value <= error:
            findings.append(("error", row, f"{said}, no more than the error threshold of {error:g} {unit}.  Remove "
                                           f"its row {admitted[0]}"))
        elif value > error and admitted is not None:
            findings.append(("warning", row, f"{said}, over the error threshold of {error:g} {unit}, and the row "
                                             f"{admitted[0]} admits it: {admitted[1]}"))
        elif value > error and row == "test-time" and kind is not None and "-tsan" in kind:
            findings.append(("warning", row, f"{said}, over the error threshold of {error:g} {unit}.  The build kind "
                                             f"{kind} budgets no wall time, so the timeout of the test preset is the "
                                             f"hard stop"))
        elif value > error:
            findings.append(("error", row, f"{said}, over the error threshold of {error:g} {unit}, and no row of "
                                           f"utils/scripts/{row}-ledger.txt admits it for the kind {kind}.  Make the "
                                           f"test smaller, or split it into tests over disjoint cases"))
        elif value > warn:
            findings.append(("warning", row, f"{said}, over the warning threshold of {warn:g} {unit}"))
    return findings


def report(name: str, place: str, findings: list[tuple[str, str, str]], warnings_dir: str | None) -> None:
    """Print each finding, and write or remove the warnings file of this test for each row.

    utils/scripts/check_report.py is imported only when a finding or a stale
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

    if SCRIPTS not in sys.path:
        sys.path.insert(0, SCRIPTS)
    import check_report

    for row in ROWS:
        lines = [check_report.Finding(level, place, 0, row, message)
                 for level, finding_row, message in findings if finding_row == row]
        for line in lines:
            print(line.text(), file=sys.stderr)
        if warnings_dir is not None:
            check_report.write_warnings(Path(warnings_dir), row, [line for line in lines if line.level == "warning"],
                                        name)


def write(record_path: str, name: str, command: str, run: cost_meter.Measurement, result: str) -> None:
    """Write the record of one test.

    Args:
        record_path: The record file
        name: The name of the test
        command: The absolute path of the executable
        run: The measurement of the command
        result: "passed", "failed" or "rejected" (the launcher failed a test that passed)
    """
    cost_meter.write_record(record_path, [
        ("format", str(cost_meter.RECORD_FORMAT)), ("step", '"test"'), ("result", f'"{result}"'),
        ("test", cost_meter.quote(name)), ("command", cost_meter.quote(command)), ("exit", str(run.exit_code)),
        ("cost", cost_meter.cost_block(run))])


def main(arguments: list[str]) -> None:
    """Run the test of the command line, and end as it ended.

    Args:
        arguments: The arguments after the program name
    """
    warnings_dir = None
    if arguments[:1] == ["--warnings-dir"] and len(arguments) >= 2:
        warnings_dir, arguments = arguments[1], arguments[2:]
    if not arguments:
        print("usage: test-launcher.py [--warnings-dir DIR] EXECUTABLE [ARG]... | --self-test", file=sys.stderr)
        sys.exit(2)
    command = os.path.abspath(arguments[0])
    name = os.path.basename(command)
    try:
        budget = cost_meter.budget_rows(os.environ.get(BUDGETS_ENV) or os.path.join(SCRIPTS, "budgets.txt"), ROWS)
    except (OSError, ValueError):
        budget = {}
    try:
        run = cost_meter.measure(arguments)
    except OSError as error:
        print(f"test-launcher: the test {arguments[0]} cannot start: {error}", file=sys.stderr)
        sys.exit(127)
    status = run.exit_code
    result = "passed" if status == 0 else "failed"
    try:
        build_dir = cost_meter.build_dir_of(command)
        kind = cost_meter.read_kind(build_dir) if build_dir else None
        findings = judge(name, run, budget, kind)
        if status == 0 and any(level == "error" for level, _, _ in findings):
            status, result = 1, "rejected"
        report(name, command, findings, warnings_dir)
    except BaseException:  # noqa: BLE001
        pass
    try:
        write(command + RECORD_SUFFIX, name, command, run, result)
    except BaseException:  # noqa: BLE001
        pass
    cost_meter.end_like(status)


# ── The self-test ──────────────────────────────────────────────────


def self_test() -> int:
    """Run the launcher on planted tests in a scratch build directory, and check each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    import json
    import subprocess
    import tempfile
    from pathlib import Path

    failures: list[str] = []

    def expect(label: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {label}")
        if not holds:
            failures.append(label)

    with tempfile.TemporaryDirectory(prefix="test-launcher-") as work:
        root = Path(work)
        build = root / "build"
        (build / "test").mkdir(parents=True)
        (build / "CMakeCache.txt").write_text("# a planted cache\n")
        (build / cost_meter.KIND_FILE).write_text("x86_64-debug-asan\n")
        ledgers = root / "ledgers"
        ledgers.mkdir()
        table = root / "budgets.txt"
        warnings = build / "check-warnings"
        program = build / "test" / "test_planted"
        program.write_text("#!/bin/sh\nexit \"${1:-0}\"\n")
        program.chmod(0o755)
        killer = build / "test" / "test_killed"
        killer.write_text("#!/bin/sh\nkill -TERM $$\n")
        killer.chmod(0o755)

        def budget(time_rows: str, memory_rows: str) -> None:
            table.write_text(f"test-time | {time_rows} | s | the wall time\ntest-memory | {memory_rows} | GB | the "
                             f"peak memory\n")

        def ledger(row: str, text: str) -> None:
            (ledgers / f"{row}-ledger.txt").write_text("# kind | test | value | reason\n" + text)

        def launch(target: Path, *args: str) -> tuple[int, str]:
            environment = dict(os.environ, **{BUDGETS_ENV: str(table), LEDGERS_ENV: str(ledgers)})
            proc = subprocess.run([sys.executable, "-S", __file__, "--warnings-dir", str(warnings), str(target),
                                   *args], env=environment, capture_output=True, text=True, check=False)
            return proc.returncode, proc.stderr

        ledger("test-time", "")
        ledger("test-memory", "")
        budget("1000 | 2000", "1000 | 2000")
        for code in ("0", "1", "3"):
            status, said = launch(program, code)
            expect(f"the status {code} of the test goes through, with no finding", status == int(code)
                   and "[test-" not in said)
        record = json.loads((build / "test" / ("test_planted" + RECORD_SUFFIX)).read_text())
        expect("the record names the test, its step, its result and its cost",
               record["step"] == "test" and record["test"] == "test_planted" and record["result"] == "failed"
               and record["exit"] == 3 and record["cost"]["wall_s"] >= 0)
        status, _ = launch(killer)
        expect("a test that a signal stops ends on the same signal", status == -15)

        budget("0 | 2000", "0 | 2000")
        status, said = launch(program)
        own = warnings / "test-time.test_planted.txt"
        expect("a test over the warning thresholds passes, with a warning for each row",
               status == 0 and said.count(": warning: [test-") == 2 and own.is_file()
               and (warnings / "test-memory.test_planted.txt").is_file())
        budget("1000 | 2000", "1000 | 2000")
        status, said = launch(program)
        expect("a test under the thresholds removes its warnings files", status == 0 and not own.exists()
               and not (warnings / "test-memory.test_planted.txt").exists())

        budget("0 | 0", "1000 | 2000")
        status, said = launch(program)
        expect("a passed test over the time error threshold fails, with an error line",
               status == 1 and ": error: [test-time] " in said and "no row of" in said)
        record = json.loads((build / "test" / ("test_planted" + RECORD_SUFFIX)).read_text())
        expect("the record of the failed test says rejected", record["result"] == "rejected")
        status, said = launch(program, "3")
        expect("a test that failed keeps its status over the error threshold",
               status == 3 and ": error: [test-time] " in said)
        (build / cost_meter.KIND_FILE).write_text("x86_64-debug-tsan\n")
        status, said = launch(program)
        expect("a build of a tsan kind warns on a time over the error threshold, and the test passes",
               status == 0 and ": warning: [test-time] " in said and "budgets no wall time" in said)
        (build / cost_meter.KIND_FILE).write_text("x86_64-debug-asan\n")
        budget("1000 | 2000", "0 | 0")
        status, said = launch(program)
        expect("a passed test over the memory error threshold fails", status == 1 and ": error: [test-memory] " in said)
        ledger("test-memory", "x86_64-debug-asan | test_planted | 0.01 | a planted reason\n")
        status, said = launch(program)
        expect("a ledger row of the kind admits the test, with a warning",
               status == 0 and ": warning: [test-memory] " in said and "a planted reason" in said)
        ledger("test-memory", "x86_64-release | test_planted | 0.01 | a planted reason\n")
        status, said = launch(program)
        expect("a ledger row of another kind admits nothing", status == 1)
        ledger("test-memory", "x86_64-debug-asan | test_planted | 0.01 | a planted reason\n")
        budget("1000 | 2000", "1000 | 2000")
        status, said = launch(program)
        expect("a ledger row whose test stays under the error threshold fails the test",
               status == 1 and "Remove its row" in said)
        status, said = launch(root / "no-such-test")
        expect("a test that cannot start gives status 127", status == 127 and "cannot start" in said)
    if failures:
        print(f"test-launcher --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("test-launcher --self-test: every case holds.")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    main(sys.argv[1:])
