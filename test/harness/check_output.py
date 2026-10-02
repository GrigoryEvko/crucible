#!/usr/bin/env python3
"""Run the probes of the test harness and check their output.

usage: check_output.py PROBE_REPORT PROBE_FATAL_TWICE

PROBE_REPORT (test/harness/probe_report.cpp) writes a pass line and reports,
and then fails an assert.  Its standard output must hold the exact bytes,
its standard error must hold the report and the text of the failed assert
with its condition, its file and line and its function, and it must end by
SIGABRT.

PROBE_FATAL_TWICE (test/harness/probe_fatal_twice.cpp) catches two fatal
exits on one thread with abort probes.  Its standard error must hold the
text of each one, and it must end with status 0.

Each probe runs with a soft core limit of one byte, so an abort writes no
core dump (CLAUDE.md section XIII, rule 7), and each probe has ten seconds.
Exit 0 when each check passes, 1 when one fails, 2 on a usage error.
"""

from __future__ import annotations

import re
import resource
import signal
import subprocess
import sys

ASSERT_LINE = re.compile(r"^the assert is on line ([0-9]+)$", re.M)
LONG_LINE = "the fifty characters of one line of the long text\n"
EXPECTED_OUT = ("probe_report: the pass line\n"
                "3 of -4 on the out sink, true, {braces}\n"
                + LONG_LINE * 12)


def no_core_dump() -> None:
    """Set a soft core limit of one byte in the child, as the test launcher does."""
    _, hard = resource.getrlimit(resource.RLIMIT_CORE)
    resource.setrlimit(resource.RLIMIT_CORE, (1, hard))


def run(probe: str) -> subprocess.CompletedProcess[str]:
    """Run one probe and give its status and its two outputs.

    Args:
        probe: The path of the probe program

    Returns:
        The completed process, with the text of standard output and standard error
    """
    return subprocess.run([probe], capture_output=True, text=True, timeout=10, preexec_fn=no_core_dump, check=False)


def check(condition: bool, what: str, result: subprocess.CompletedProcess[str]) -> bool:
    """Print a failure with the outputs of the probe when condition is false.

    Args:
        condition: The checked fact
        what: The fact in words
        result: The run of the probe, for the report of a failure

    Returns:
        condition
    """
    if not condition:
        print(f"check_output: FAIL: {what} (status {result.returncode})", file=sys.stderr)
        print(f"standard output:\n{result.stdout}", file=sys.stderr)
        print(f"standard error:\n{result.stderr}", file=sys.stderr)
    return condition


def check_report_probe(probe: str) -> bool:
    """Check the pass line, the reports and the failed assert of probe_report."""
    result = run(probe)
    fatal = "foundation: fatal: assert: queue_depth == 4\n  at "
    line = ASSERT_LINE.search(result.stderr)
    site = f"probe_report.cpp:{line.group(1) if line else '?'} in main\n"
    passed = check(result.returncode == -signal.SIGABRT, "the failed assert ends the process by SIGABRT", result)
    passed = check(result.stdout == EXPECTED_OUT, "standard output holds the pass line and the reports", result) and passed
    passed = check(result.stderr.startswith("[text] on the err sink\n"), "standard error holds the report",
                   result) and passed
    passed = check(line is not None, "standard error holds the line of the assert", result) and passed
    passed = check(fatal in result.stderr, "the report of the assert names its condition", result) and passed
    passed = check(site in result.stderr, "the report of the assert names its file, its line and its function",
                   result) and passed
    return passed


def check_fatal_twice_probe(probe: str) -> bool:
    """Check that each of two caught fatal exits on one thread writes its text."""
    result = run(probe)
    passed = check(result.returncode == 0, "the two abort probes catch the two fatal exits", result)
    for which in ("first", "second"):
        text = f"foundation: fatal: the {which} fatal exit of the probe\n"
        passed = check(text in result.stderr, f"standard error holds the text of the {which} fatal exit",
                       result) and passed
    return passed


def main(arguments: list[str]) -> int:
    """Run the two probes and give the exit status of the check."""
    if len(arguments) != 3:
        print(__doc__, file=sys.stderr)
        return 2
    passed = check_report_probe(arguments[1])
    passed = check_fatal_twice_probe(arguments[2]) and passed
    print("check_output: " + ("all checks passed" if passed else "FAILED"))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
