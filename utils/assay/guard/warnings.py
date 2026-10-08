"""guard warnings — print the warnings that the rules wrote for one build directory, and the totals of the build.

A rule that passes with warnings writes them to <build>/check-warnings/RULE.txt
(utils/assay/core/report.py).  ctest shows no output of a test that passes,
so `guard warnings` prints those warnings after a build or a test run.  In a
GitHub workflow it also prints each one as an annotation.  After the
warnings, it prints the time totals and the build totals of the build
directory, which utils/assay/guard/legacy/report_check_warnings.py adds.

    python3 utils/bin/guard warnings BUILD_DIR
    python3 utils/bin/guard selftest guard.warnings

The exit status is 0 when every file has the format, also with warnings.  It is
1 when a line of a warnings file does not have the format, and 2 for a usage
error or a failed self-test.
"""

import contextlib
import io
import sys
import tempfile
from collections import Counter
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import report as check_report
from assay.guard.legacy import report_check_warnings as totals

USAGE = "usage: guard warnings BUILD_DIR | guard selftest guard.warnings"


def main(argv: list[str]) -> int:
    """Print the warnings and the totals of one build directory.

    Args:
        argv: The words after `guard warnings`: one build directory, or --self-test

    Returns:
        0 when every warnings file has the format, 1 when a line does not, 2 for a usage error
    """
    if argv == ["--self-test"]:
        return self_test()
    if len(argv) != 1:
        print(USAGE, file=sys.stderr)
        return 2
    build_dir = Path(argv[0]).resolve()
    warnings_dir = build_dir / check_report.WARNINGS_SUBDIR
    findings, problems = check_report.read_warnings_dir(warnings_dir)
    findings += totals.report_totals(build_dir)
    findings += totals.report_build(build_dir)
    annotate = cost_meter.is_github_actions()
    for found in findings:
        print(found.text())
        if annotate:
            print(found.annotation())
    for problem in problems:
        print(problem, file=sys.stderr)
    counts = Counter(found.check for found in findings)
    summary = ", ".join(f"{check} {count}" for check, count in sorted(counts.items()))
    print(f"guard warnings: {len(findings)} warning(s) in {warnings_dir} and the totals"
          + (f": {summary}" if summary else ""))
    return 1 if problems else 0


def self_test() -> int:
    """Do a test of the print of the warnings files, with planted input.

    Returns:
        0 when every case holds, else 2
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="guard-warnings-") as scratch:
        build = Path(scratch)
        warnings_dir = build / check_report.WARNINGS_SUBDIR
        warnings_dir.mkdir()
        (warnings_dir / "alpha.txt").write_text(check_report.Finding("warning", "a.h", 1, "alpha", "m").text() + "\n",
                                                encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()) as output, contextlib.redirect_stderr(io.StringIO()):
            status = main([str(build)])
        expect("a build with no census prints the warnings and tells why it has no totals",
               status == 0 and "a.h:1: warning: [alpha] m" in output.getvalue()
               and "no time totals" in output.getvalue())
        (warnings_dir / "alpha.txt").write_text("not a finding\n", encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            status = main([str(build)])
        expect("a malformed warnings file gives status 1", status == 1)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            status = main([])
        expect("a call with no build directory is a usage error", status == 2)
    if failures:
        print(f"guard warnings --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("guard warnings --self-test: every case holds.")
    return 0
