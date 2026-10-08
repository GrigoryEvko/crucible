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

THE COMPILER
    A test module with compile cases gets the patched compiler from
    patched_compiler().  The variable ASSAY_TEST_CXX names the compiler, and
    the registration of the test in CMake gives it.  With no such variable,
    the compiler is g++-16p in usr/bin of the prefix that
    CRUCIBLE_GCC16_PREFIX names.  When the two variables are empty, the
    text is empty.

`guard selftest tests.core.test_testing` does a test of the module.
"""

import os
from collections.abc import Callable, Mapping, Sequence
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
