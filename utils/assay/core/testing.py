"""The run of the cases of one test module of the package.

THE CASES
    A test module gives its cases in groups.  A group is a function that
    takes the function `expect`.  It calls that function one time for each
    case, with the name of the case and its result.  Each case writes one
    line through assay.core.report.  A case that does not hold writes FAIL,
    and each other case writes ok.

THE GROUPS AT THE SAME TIME
    `loop test-step` gives a test the jobserver of the test run as the
    variable TEST_JOBSERVER_ENV of assay.core.meter (THE JOBSERVER OF A TEST
    there).  With that variable, two groups or more and one thread, run()
    runs each group in a child process of its own (forked() of
    assay.core.proc).  A child starts only on a free job of the jobserver.
    The test holds one job.  Its groups then run one after the other when
    the other jobs are in use.  At the end of a test run, more jobs are free, and
    more groups run at the same time.  run() writes the output of each group
    in the sequence of the groups.  The output is then the same as the
    output of a run in sequence.  With no such variable, run() calls each group in
    sequence in this process.

    The status of a child is ExitCode.OK or ExitCode.FINDINGS.  The FAIL
    lines of its output give the number of its cases that do not hold.  A
    different status counts one more case that does not hold.  Examples are
    ExitCode.TOOL_FAILED after an exception, and the signal that ended the
    child.

    A group in a child cannot see a change that a different group makes in
    the process, such as a variable of the module.  Each group then holds
    its own state.  A group that raises an exception writes its traceback,
    and run() counts one case that does not hold for it.

THE STATUS
    run() returns ExitCode.OK when each case holds, and ExitCode.FINDINGS when
    a case does not hold.  A test module takes no argument.  A call with an
    argument writes the usage to the standard error and returns
    ExitCode.USAGE.

THE OUTPUT OF A CASE
    captured() runs one action of a case.  It gives the result of the action
    and the text that the action writes.  The text holds the writes to the
    standard output and to the standard error, in their sequence.

THE ENVIRONMENT OF A CASE
    environment_variable() sets one variable of the environment of this
    process for a block, or removes it for the block.  After the block, also
    after an exception, the variable gets its earlier value again.

THE COMPILER
    A test module with compile cases gets the patched compiler from
    patched_compiler().  The variable ASSAY_TEST_CXX names the compiler, and
    the registration of the test in CMake gives it.  With no such variable,
    the compiler is g++-16p in usr/bin of the prefix that
    CRUCIBLE_GCC16_PREFIX names.  When the two variables are empty, the
    text is empty.  held_compiler() also holds the case that a variable
    names a compiler file.

THE PLANTED PROGRAM
    planted_program() writes a Python script as a program file of a case,
    such as a planted compiler, cmake or ninja.  The first line of the file
    runs the Python of this process.  The mode of the file lets each user
    run it.

THE WAIT
    wait_until() looks at a condition at a constant interval, until the
    condition holds or a limit ends.  It tells if the condition holds at
    the end.  A case that waits for a different thread or process calls it.

THE END OF A PROCESS
    is_gone() tells if a process ended before a limit.  It reads the state
    of the process from /proc/PID/stat.  A process with no such file, and a
    process in the state Z or X, is gone.  init reaps an orphan that a
    signal stopped.  Then the file of such a process goes.

`guard selftest tests.core.test_testing` does a test of the module.
"""

import contextlib
import io
import os
import sys
import threading
import time
from collections.abc import Callable, Iterator, Mapping, Sequence
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import proc
from assay.core import report as check_report
from assay.core.exit import ExitCode

type Expect = Callable[[str, bool], None]
type Group = Callable[[Expect], None]
TEST_COMPILER = "ASSAY_TEST_CXX"
COMPILER_PREFIX = "CRUCIBLE_GCC16_PREFIX"
WAIT_LIMIT_S = 10.0
WAIT_STEP_S = 0.02
# The state letters of /proc/PID/stat of a process that ended.  A process with no such file gets "X" (THE END OF A
# PROCESS).
GONE_STATES = frozenset({"Z", "X"})
# The marks of the line of a case that holds and of a case that does not hold (THE CASES).
HOLDS_MARK = "  ok   "
FAILS_MARK = "  FAIL "


def patched_compiler(environment: Mapping[str, str] = os.environ) -> str:
    """Return the patched compiler of the compile cases (THE COMPILER), or an empty text when no variable names it."""
    named = environment.get(TEST_COMPILER, "")
    if named:
        return named
    prefix = environment.get(COMPILER_PREFIX, "")
    return str(Path(prefix) / "usr" / "bin" / "g++-16p") if prefix else ""


def held_compiler(expect: Expect) -> str | None:
    """Return the patched compiler of the compile cases, or None after a case that does not hold (THE COMPILER).

    The case holds when a variable names a compiler file.
    """
    compiler = patched_compiler()
    is_named = bool(compiler) and Path(compiler).is_file()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the patched compiler", is_named)
    return compiler if is_named else None


def planted_program(directory: Path, name: str, text: str) -> str:
    """Write a Python script as an executable program file of a case, and return its path (THE PLANTED PROGRAM).

    Args:
        directory: The directory of the file
        name: The name of the file
        text: The Python text of the script, without its first line
    """
    path = directory / name
    path.write_text(f"#!{sys.executable}\n{text}", encoding="utf-8")
    path.chmod(0o755)
    return str(path)


@contextlib.contextmanager
def environment_variable(name: str, value: str | None) -> Iterator[None]:
    """Set one variable of the environment for the block, or remove it when `value` is None (THE ENVIRONMENT OF A CASE)."""
    saved = os.environ.get(name)
    if value is None:
        os.environ.pop(name, None)
    else:
        os.environ[name] = value
    try:
        yield
    finally:
        if saved is None:
            os.environ.pop(name, None)
        else:
            os.environ[name] = saved


def wait_until(condition: Callable[[], bool], limit_s: float = WAIT_LIMIT_S, step_s: float = WAIT_STEP_S) -> bool:
    """Wait in steps of `step_s` seconds until a condition holds, at most for `limit_s` seconds (THE WAIT).

    Return True when the condition holds at the end.  Complexity: at most limit_s / step_s calls of the condition.
    """
    deadline = time.monotonic() + limit_s
    while not condition() and time.monotonic() < deadline:
        time.sleep(step_s)
    return condition()


def process_state(pid: int) -> str:
    """Return the state letter of a process from /proc/PID/stat, or "X" with no such file (THE END OF A PROCESS)."""
    try:
        return Path(f"/proc/{pid}/stat").read_text(encoding="ascii").rsplit(")", 1)[1].split()[0]
    except FileNotFoundError, ProcessLookupError, IndexError:
        return "X"


def is_gone(pid: int, wait_s: float = WAIT_LIMIT_S) -> bool:
    """Tell if a process is gone or a zombie before `wait_s` seconds end (THE END OF A PROCESS)."""
    return wait_until(lambda: process_state(pid) in GONE_STATES, wait_s)


def captured[Result](action: Callable[[], Result]) -> tuple[Result, str]:
    """Run one action, and give its result and the text of the two standard streams (THE OUTPUT OF A CASE)."""
    with contextlib.redirect_stdout(io.StringIO()) as printed, contextlib.redirect_stderr(printed):
        return action(), printed.getvalue()


def run_group(group: Group) -> int:
    """Run one group of cases in this process, and return the number of its cases that do not hold (THE CASES)."""
    failures = 0

    def expect(case: str, holds: bool) -> None:
        """Write the line of one case, and count a case that does not hold."""
        nonlocal failures
        check_report.write_line(f"{HOLDS_MARK if holds else FAILS_MARK}{case}")
        if not holds:
            failures += 1

    group(expect)
    return failures


def forked_failures(groups: Sequence[Group], makeflags: str) -> int:
    """Run each group in a child process on the jobs of a jobserver, and return the cases that do not hold.

    The function writes the output of each group in the sequence of the groups (THE GROUPS AT THE SAME TIME).  A
    jobserver that cannot be opened gives the run in sequence, because the jobserver changes no result.
    """
    from assay.core import jobserver

    # The status of a child tells if a case of its group does not hold.  The lines of the output give the number.
    actions = [lambda group=group: ExitCode.FINDINGS if run_group(group) else ExitCode.OK for group in groups]
    try:
        with jobserver.client_of({jobserver.VARIABLE: makeflags}) as client:
            if client is None:
                return sum(run_group(group) for group in groups)
            ends = proc.forked(actions, client)
    except jobserver.JobserverError:
        return sum(run_group(group) for group in groups)
    failures = 0
    for group, end in zip(groups, ends, strict=True):
        text = proc.decoded(end.output)
        if text:
            check_report.write_line(text.removesuffix("\n"))
        lines = sum(1 for line in text.splitlines() if line.startswith(FAILS_MARK))
        if end.status == ExitCode.OK:
            continue
        if end.status == ExitCode.FINDINGS:
            failures += max(lines, 1)
            continue
        name = getattr(group, "__name__", repr(group))
        check_report.write_line(f"{FAILS_MARK}the group {name} ended with the status {end.status}")
        failures += lines + 1
    return failures


def run(module: str, argv: Sequence[str], groups: Sequence[Group]) -> ExitCode:
    """Run each group of cases of one test module (THE CASES), and return its status (THE STATUS).

    The groups run at the same time on the jobs of the test run when the launcher gives a jobserver (THE GROUPS AT THE
    SAME TIME).

    Args:
        module: The dotted name of the test module in the package assay, such as tests.core.test_rows
        argv: The words after the name of the module on the command line
        groups: The groups of cases, in the sequence of the run
    """
    if argv:
        check_report.write_line(f"usage: guard selftest {module}", is_error=True)
        return ExitCode.USAGE
    makeflags = os.environ.get(cost_meter.TEST_JOBSERVER_ENV, "")
    if makeflags and len(groups) > 1 and threading.active_count() == 1:
        failures = forked_failures(groups, makeflags)
    else:
        failures = sum(run_group(group) for group in groups)
    if failures:
        check_report.write_line(f"{module}: FAILED, {failures} case(s) did not hold")
        return ExitCode.FINDINGS
    check_report.write_line(f"{module}: each case holds.")
    return ExitCode.OK
