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

`guard selftest tests.core.test_testing` does a test of the module.
"""

import contextlib
import io
import os
from collections.abc import Callable, Iterator, Mapping, Sequence
from pathlib import Path

from assay.core import report as check_report
from assay.core.exit import ExitCode

type Expect = Callable[[str, bool], None]
type Group = Callable[[Expect], None]
TEST_COMPILER = "ASSAY_TEST_CXX"
COMPILER_PREFIX = "CRUCIBLE_GCC16_PREFIX"


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
