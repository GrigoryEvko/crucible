#!/usr/bin/env python3
"""Run the probes of the test harness and check their output.

usage: check_output.py PROBE_REPORT PROBE_FATAL_TWICE PROBE_DIAG [EMULATOR [ARG]...]

EMULATOR and its arguments run each probe in a cross build, such as
qemu-aarch64-static -L SYSROOT.  test/CMakeLists.txt gives them from the
target property CROSSCOMPILING_EMULATOR.  A native build gives none.

PROBE_REPORT (test/harness/probe_report.cpp) writes a pass line and reports,
and then fails an assert.  Its standard output must hold the exact bytes,
its standard error must hold the report and the text of the failed assert
with its condition, its file and line and its function, and it must end by
SIGABRT.  The file is the path of the probe from the source root, because
the macro maps of cmake/Ccache.cmake give that path to each build directory
and each checkout, with and without ccache.

PROBE_FATAL_TWICE (test/harness/probe_fatal_twice.cpp) catches two fatal
exits on one thread with abort probes.  Then it opens a second arm of the
probe while one arm is open, and the open arm catches the refusal.  Its
standard error must hold the text of each fatal exit and of the refusal, and
it must end with status 0.

PROBE_DIAG (test/harness/probe_diag.cpp) writes the records and the lines of
the diagnostic emitter of foundation/diag/JsonEmitter.h.  Each line of its
output must hold the exact bytes.  The prose of the catalog is the one part
that the check does not compare: a record with that prose must hold the
fields around it.

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
# The path of probe_report from the source root.
PROBE_REPORT_PATH = "test/harness/probe_report.cpp"
LONG_LINE = "the fifty characters of one line of the long text\n"
EXPECTED_OUT = ("probe_report: the pass line\n"
                "3 of -4 on the out sink, true, {braces}\n"
                + LONG_LINE * 12)

# The lines of probe_diag.  The records come from the fields of the probe, and
# each byte that JSON reserves is escaped.
LEGACY_LINE = "crucible-violation: category=DetSafeLeak fn=fn_name detail=detail text\n"
CAPPED_LINE = "crucible-violation: category=DetSafeLeak fn=" + "x" * 4096 + " detail=d\n"
ESCAPE_RECORD = ('{"format_version":1,"source_position":{"file":"escape.cpp","line":1,"column":2,'
                 '"function":"escape_fn"},"error_code":"EffectRowMismatch","goal":"goal","have":"have",'
                 '"gap":"quote \\" slash \\\\ newline\\n tab \\t ctrl \\u0001","suggestion":"suggestion",'
                 '"related_snippets":["x < y"]}\n')
VIOLATION_HEAD = ('{"format_version":1,"source_position":{"file":"test_diag.cpp","line":77,"column":9,'
                  '"function":"test_fn"},"error_code":"EffectRowMismatch","goal":"')
VIOLATION_MIDDLE = '","have":"test_fn","gap":"caller row lacks Bg","suggestion":"'
RECORD_TAIL = '","related_snippets":[]}\n'
FALLBACK_HEAD = ('{"format_version":1,"source_position":{"file":"","line":0,"column":0,"function":""},'
                 '"error_code":"DetSafeLeak","goal":"')
FALLBACK_MIDDLE = '","have":"","gap":"","suggestion":"'
LONG_RECORD = ('{"format_version":1,"source_position":{"file":"long.cpp","line":3,"column":4,'
               '"function":"long_fn"},"error_code":"E","goal":"G","have":"H","gap":"' + 'ab\\"\\n' * 1024
               + '","suggestion":"S","related_snippets":[]}\n')
RECORD_MAX_BYTES = 32768
EXACT_RECORD = ('{"format_version":1,"source_position":{"file":"","line":0,"column":0,"function":""},'
                '"error_code":"E","goal":"G","have":"H","gap":"' + "g" * 32596
                + '","suggestion":"S","related_snippets":[]}\n')
RESULTS_LINE = "results: true true true true true false\n"
SECOND_ARM_TEXT = "foundation: fatal: a second arm of the abort probe starts while the first arm is open\n"
REFUSED_LINE = "format refused\n"
DEFAULT_SINK_LINE = "crucible-violation: category=DetSafeLeak fn=default_fn detail=default detail\n"


def no_core_dump() -> None:
    """Set a soft core limit of one byte in the child, as the test launcher does."""
    _, hard = resource.getrlimit(resource.RLIMIT_CORE)
    resource.setrlimit(resource.RLIMIT_CORE, (1, hard))


def run(probe: str, emulator: list[str]) -> subprocess.CompletedProcess[str]:
    """Run one probe and give its status and its two outputs.

    Args:
        probe: The path of the probe program
        emulator: The words of the emulator that runs the probe, or none

    Returns:
        The completed process, with the text of standard output and standard error
    """
    return subprocess.run(
        [*emulator, probe], capture_output=True, text=True, timeout=10, preexec_fn=no_core_dump, check=False
    )


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


def check_report_probe(probe: str, emulator: list[str]) -> bool:
    """Check the pass line, the reports and the failed assert of probe_report."""
    result = run(probe, emulator)
    fatal = "foundation: fatal: assert: queue_depth == 4\n  at "
    line = ASSERT_LINE.search(result.stderr)
    site = f"queue_depth == 4\n  at {PROBE_REPORT_PATH}:{line.group(1) if line else '?'} in main\n"
    passed = check(result.returncode == -signal.SIGABRT, "the failed assert ends the process by SIGABRT", result)
    passed = check(result.stdout == EXPECTED_OUT, "standard output holds the pass line and the reports", result) and passed
    passed = check(result.stderr.startswith("[text] on the err sink\n"), "standard error holds the report",
                   result) and passed
    passed = check(line is not None, "standard error holds the line of the assert", result) and passed
    passed = check(fatal in result.stderr, "the report of the assert names its condition", result) and passed
    passed = check(site in result.stderr,
                   "the report of the assert names its file from the source root, its line and its function",
                   result) and passed
    return passed


def check_fatal_twice_probe(probe: str, emulator: list[str]) -> bool:
    """Check that each of two caught fatal exits on one thread, and the refusal of a second open arm, write a text."""
    result = run(probe, emulator)
    passed = check(result.returncode == 0, "the abort probes catch the two fatal exits and the refusal", result)
    for which in ("first", "second"):
        text = f"foundation: fatal: the {which} fatal exit of the probe\n"
        passed = check(text in result.stderr, f"standard error holds the text of the {which} fatal exit",
                       result) and passed
    passed = check(SECOND_ARM_TEXT in result.stderr, "standard error holds the refusal of a second open arm",
                   result) and passed
    return passed


def holds_prose(line: str, head: str, middle: str) -> bool:
    """Tell if a record line is head, a text of the catalog, middle, a text of the catalog and the tail of a record.

    Args:
        line: One line of the output, with its newline
        head: The bytes before the first text of the catalog
        middle: The bytes between the two texts of the catalog

    Returns:
        True when the line has that shape and each text of the catalog holds at least one character
    """
    if not line.startswith(head) or not line.endswith(RECORD_TAIL):
        return False
    first, found, second = line[len(head):len(line) - len(RECORD_TAIL)].partition(middle)
    return bool(found) and bool(first) and bool(second)


def check_diag_probe(probe: str, emulator: list[str]) -> bool:
    """Check each record and each line of probe_diag, and the line of the default sink on standard error."""
    result = run(probe, emulator)
    lines = result.stdout.splitlines(keepends=True)
    passed = check(result.returncode == 0, "probe_diag ends with status 0", result)
    passed = check(len(lines) == 10, "standard output holds 10 lines", result) and passed
    if len(lines) != 10:
        return False
    exact = [(0, LEGACY_LINE, "the line of a violation"),
             (1, CAPPED_LINE, "a field of a line is cut at 4096 characters"),
             (2, ESCAPE_RECORD, "a record escapes each byte that JSON reserves"),
             (5, LONG_RECORD, "a record longer than the buffer of the sink keeps each byte"),
             (6, EXACT_RECORD, "a record of exactly the largest size is written"),
             (7, RESULTS_LINE, "a record one byte past the largest size is refused, and the others are written"),
             (8, ESCAPE_RECORD, "the format form of a record gives the bytes of the sink"),
             (9, REFUSED_LINE, "the format form refuses a text that is too small")]
    for index, text, what in exact:
        passed = check(lines[index] == text, what, result) and passed
    passed = check(len(lines[6]) == RECORD_MAX_BYTES, "the largest record has json_record_max_bytes bytes",
                   result) and passed
    passed = check(holds_prose(lines[3], VIOLATION_HEAD, VIOLATION_MIDDLE),
                   "a violation record holds its position, its catalog prose, its function and its gap",
                   result) and passed
    passed = check(holds_prose(lines[4], FALLBACK_HEAD, FALLBACK_MIDDLE),
                   "a record with empty fields holds the name and the prose of its category", result) and passed
    passed = check(result.stderr == DEFAULT_SINK_LINE,
                   "the default sink writes the line of a violation to standard error", result) and passed
    return passed


def main(arguments: list[str]) -> int:
    """Run the three probes and give the exit status of the check."""
    if len(arguments) < 4:
        print(__doc__, file=sys.stderr)
        return 2
    emulator = arguments[4:]
    passed = check_report_probe(arguments[1], emulator)
    passed = check_fatal_twice_probe(arguments[2], emulator) and passed
    passed = check_diag_probe(arguments[3], emulator) and passed
    print("check_output: " + ("all checks passed" if passed else "FAILED"))
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
