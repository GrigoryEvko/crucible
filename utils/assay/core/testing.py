"""The run of the cases of one test module of the package.

THE CASES
    A test module gives its cases in groups.  A group is a function that
    takes the function `expect`.  It calls that function one time for each
    case, with the name of the case and its result.  run() calls each group in sequence.
    Each case writes one line through assay.core.report.  A case that does
    not hold writes FAIL, and each other case writes ok.

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
import time
from collections.abc import Callable, Iterator, Mapping, Sequence
from pathlib import Path

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


def run(module: str, argv: Sequence[str], groups: Sequence[Group]) -> ExitCode:
    """Run each group of cases of one test module (THE CASES), and return its status (THE STATUS).

    Args:
        module: The dotted name of the test module in the package assay, such as tests.core.test_rows
        argv: The words after the name of the module on the command line
        groups: The groups of cases, in the sequence of the run
    """
    if argv:
        check_report.write_line(f"usage: guard selftest {module}", is_error=True)
        return ExitCode.USAGE
    failures: list[str] = []

    def expect(case: str, holds: bool) -> None:
        """Write the line of one case, and keep the name of a case that does not hold."""
        check_report.write_line(f"  {'ok  ' if holds else 'FAIL'} {case}")
        if not holds:
            failures.append(case)

    for group in groups:
        group(expect)
    if failures:
        check_report.write_line(f"{module}: FAILED, {len(failures)} case(s) did not hold")
        return ExitCode.FINDINGS
    check_report.write_line(f"{module}: each case holds.")
    return ExitCode.OK
