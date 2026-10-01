#!/usr/bin/env python3
"""Print the warnings that the compile-time checks of one build directory wrote.

A check that passes with warnings writes them to <build>/check-warnings/CHECK.txt
(utils/scripts/check_report.py).  ctest shows no output of a test that passes,
so this script prints those warnings after a build or a test run.  In a GitHub
workflow it also prints each one as an annotation.

    python3 utils/scripts/report-check-warnings.py BUILD_DIR

The exit status is 0 when every file has the format, also with warnings.  It is
1 when a line of a warnings file does not have the format.
"""

from __future__ import annotations

import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import check_report  # noqa: E402
import cost_meter  # noqa: E402


def main(argv: list[str]) -> int:
    """Print the warnings of one build directory.

    Args:
        argv: The arguments: one build directory

    Returns:
        0 when every warnings file has the format, 1 when a line does not, 2 for a usage error
    """
    if len(argv) != 1:
        print("usage: report-check-warnings.py BUILD_DIR", file=sys.stderr)
        return 2
    warnings_dir = Path(argv[0]) / check_report.WARNINGS_SUBDIR
    findings, problems = check_report.read_warnings_dir(warnings_dir)
    annotate = cost_meter.is_github_actions()
    for found in findings:
        print(found.text())
        if annotate:
            print(found.annotation())
    for problem in problems:
        print(problem, file=sys.stderr)
    counts = Counter(found.check for found in findings)
    summary = ", ".join(f"{check} {count}" for check, count in sorted(counts.items()))
    print(f"report-check-warnings: {len(findings)} warning(s) in {warnings_dir}" + (f": {summary}" if summary else ""))
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
