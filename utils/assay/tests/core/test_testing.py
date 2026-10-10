"""The test of the run of the cases of a test module (assay.core.testing): the lines, the status and the usage.

`guard selftest tests.core.test_testing` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.
"""

import contextlib
import io
import os
from collections.abc import Sequence

from assay.core import report as check_report
from assay.core import testing
from assay.core.exit import ExitCode


def captured(argv: Sequence[str], groups: Sequence[testing.Group]) -> tuple[ExitCode, str, str]:
    """Run planted groups, and return the status, the standard output and the standard error."""
    out = io.StringIO()
    err = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        status = testing.run("tests.planted", argv, groups)
    return status, out.getvalue(), err.getvalue()


def holds(expect: testing.Expect) -> None:
    """A planted group with one case that holds."""
    expect("a planted case that holds", True)


def fails(expect: testing.Expect) -> None:
    """A planted group with one case that does not hold."""
    expect("a planted case that does not hold", False)


def run_cases(expect: testing.Expect) -> None:
    """Check the lines and the status of a run."""
    status, out, err = captured([], [holds, holds])
    expect(
        "a run with each case that holds returns OK, and writes one line for each case and one summary",
        status == ExitCode.OK
        and out
        == ("  ok   a planted case that holds\n  ok   a planted case that holds\ntests.planted: each case holds.\n")
        and not err,
    )
    status, out, err = captured([], [holds, fails, holds])
    expect(
        "a case that does not hold gives FINDINGS, and the run continues to the subsequent groups",
        status == ExitCode.FINDINGS
        and out.count("  ok   ") == 2
        and "  FAIL a planted case that does not hold\n" in out
        and out.endswith("tests.planted: FAILED, 1 case(s) did not hold\n"),
    )
    status, out, err = captured(["--extra"], [holds])
    expect(
        "an argument gives USAGE, writes the usage to the standard error and runs no group",
        status == ExitCode.USAGE and not out and err == "usage: guard selftest tests.planted\n",
    )
    status, out, err = captured([], [])
    expect("a run with no group returns OK", status == ExitCode.OK and out == "tests.planted: each case holds.\n")


def writes_two_streams() -> tuple[str, int]:
    """A planted action: it writes to the two standard streams and returns a value that is not a status."""
    check_report.write_line("first")
    check_report.write_line("second", is_error=True)
    check_report.write_line("third")
    return "result", 3


def output_cases(expect: testing.Expect) -> None:
    """Check the output of one action of a case (THE OUTPUT OF A CASE)."""
    result, text = testing.captured(writes_two_streams)
    expect(
        "captured gives the result of the action and the two streams in the sequence of the writes",
        result == ("result", 3) and text == "first\nsecond\nthird\n",
    )
    nothing, empty = testing.captured(lambda: None)
    expect("captured of an action that writes nothing gives an empty text", nothing is None and not empty)


def compiler_cases(expect: testing.Expect) -> None:
    """Check the patched compiler of each environment."""
    expect("ASSAY_TEST_CXX names the compiler", testing.patched_compiler({"ASSAY_TEST_CXX": "/opt/cxx"}) == "/opt/cxx")
    expect(
        "ASSAY_TEST_CXX comes before the prefix",
        testing.patched_compiler({"ASSAY_TEST_CXX": "/opt/cxx", "CRUCIBLE_GCC16_PREFIX": "/gcc"}) == "/opt/cxx",
    )
    expect(
        "the prefix gives g++-16p in usr/bin",
        testing.patched_compiler({"CRUCIBLE_GCC16_PREFIX": "/gcc"}) == "/gcc/usr/bin/g++-16p",
    )
    expect("no variable gives an empty text", testing.patched_compiler({}) == "")


def environment_cases(expect: testing.Expect) -> None:
    """Check that environment_variable() sets or removes a variable for its block only (THE ENVIRONMENT OF A CASE)."""
    name = "ASSAY_TEST_PLANTED_VARIABLE"
    with testing.environment_variable(name, "outer"):
        with testing.environment_variable(name, "inner"):
            inner = os.environ.get(name)
        after_inner = os.environ.get(name)
        with testing.environment_variable(name, None):
            removed = name not in os.environ
        after_removal = os.environ.get(name)
        try:
            with testing.environment_variable(name, "raised"):
                raise KeyError(name)
        except KeyError:
            pass
        after_exception = os.environ.get(name)
    expect(
        "environment_variable() sets a value and removes a variable for its block, and puts back the earlier value "
        "after the block and after an exception",
        (inner, after_inner, removed, after_removal, after_exception) == ("inner", "outer", True, "outer", "outer"),
    )
    expect("a variable that was not set is not set after the block", name not in os.environ)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."), argv, (run_cases, output_cases, compiler_cases, environment_cases)
    )
