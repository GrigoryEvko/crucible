#!/usr/bin/env python3
"""check-cache-size — report each cache under the cache root that holds more than its limit.

THE CHECK
    Each subdirectory of the root of utils/scripts/cache_dir.py is a cache,
    and cache_dir.LIMITS gives the limit of each one.  The check takes the
    size of each cache from a sample of its files (cache_dir.py, THE SAMPLE),
    so a run does not stat each file of a large cache.  It divides the size by
    the limit, and it compares the quotient with the row cache-size of
    utils/scripts/budgets.txt.  A cache over the warning threshold gives a
    warning, and a cache over the error threshold gives an error.  The
    eviction of each cache holds it under its limit.  So a cache over its
    limit shows an eviction that does not run, or a limit that is too small.
    A subdirectory with no row of LIMITS gives a warning, because no eviction
    and no check knows its limit.

THE COST
    Measured on 2026-10-02 on the build host: a sample of the root with nine
    caches and about 330,000 files takes about 0.2 s.  A walk of each file of
    the preprocessed store alone took 6.7 s at 2,257,639 files.

NOT APPLICABLE
    With $CRUCIBLE_CACHE_DIR set to "off", or with no root directory, the check
    has no cache to read, and it gives no finding.

Usage
    check-cache-size.py [--warnings-dir DIR]
    check-cache-size.py --self-test

Exit 0 with no finding or with warnings only, 1 with an error, 2 on a usage
error or a failed self-test.
"""

from __future__ import annotations

import argparse
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cache_dir  # noqa: E402
import check_report  # noqa: E402

CHECK = "cache-size"
GIB = float(1 << 30)


def findings(root: Path | None, limits: dict[str, int], budget: check_report.Budget,
             rng=None) -> list[check_report.Finding]:
    """Return a finding for each cache over its limit, and for each subdirectory of the root with no limit.

    Complexity: one sample of each cache, about n * SAMPLE_FANS / 256 stat
    calls for n files under fan-out directories (cache_dir.estimated_bytes).

    Args:
        root: The root of the caches, or None when the caches are off
        limits: The limit of each cache, in allocated bytes
        budget: The row cache-size of the budget table
        rng: The random generator of the samples, or None for a new one

    Returns:
        The findings, in the order of the names
    """
    found: list[check_report.Finding] = []
    if root is None or not root.is_dir():
        return found
    for directory in sorted(path for path in root.iterdir() if path.is_dir() and not path.is_symlink()):
        limit = limits.get(directory.name)
        if limit is None:
            found.append(check_report.Finding(
                "warning", str(directory), 0, CHECK,
                f"the cache {directory.name} has no row of LIMITS in utils/scripts/cache_dir.py, so no eviction "
                f"and no check knows its limit.  Add a row for it, or remove the directory."))
            continue
        size = cache_dir.estimated_bytes(directory, rng)
        level = check_report.classify(size / limit, budget)
        if level is None:
            continue
        threshold = check_report.threshold_text(budget.warn if level == "warning" else budget.error)
        found.append(check_report.Finding(
            level, str(directory), 0, CHECK,
            f"the cache {directory.name} holds {size / GIB:.2f} GiB, {size / limit:.2f} times its limit of "
            f"{limit / GIB:.2f} GiB in LIMITS of utils/scripts/cache_dir.py.  The {level} threshold of the row "
            f"{CHECK} is {threshold} times the limit.  The eviction of the cache holds it under its limit, so the "
            f"eviction does not run, or the limit is too small."))
    return found


def self_test() -> int:
    """Plant caches in a scratch root and prove each verdict of the check.

    Returns:
        0 when every case holds, else 2
    """
    import random

    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        """Record one case."""
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    budget = check_report.read_budgets()[CHECK]
    expect("the row cache-size warns over the limit and fails over twice the limit",
           budget.warn == 1 and budget.error == 2)
    with tempfile.TemporaryDirectory(prefix="cache-size-") as scratch:
        root = Path(scratch)
        for name, files in (("under", 2), ("over", 6), ("far", 10), ("undeclared", 1)):
            for index in range(files):
                path = root / name / f"{index:02x}" / f"{index:064x}"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(bytes(1 << 16))
        limits = {"under": 4 << 16, "over": 4 << 16, "far": 4 << 16}
        verdicts = {Path(found.path).name: found.level for found in findings(root, limits, budget,
                                                                              random.Random(1))}
        expect("a cache under its limit gives no finding", "under" not in verdicts)
        expect("a cache over its limit and under twice the limit gives a warning", verdicts.get("over") == "warning")
        expect("a cache over twice its limit gives an error", verdicts.get("far") == "error")
        expect("a directory with no limit gives a warning", verdicts.get("undeclared") == "warning")
        expect("an error fails the check, and warnings alone do not",
               check_report.emit(findings(root, limits, budget), CHECK, None) == 1
               and check_report.emit(findings(root, {**limits, "far": 16 << 16}, budget), CHECK, None) == 0)
        expect("with the caches off the check reads no cache", findings(None, limits, budget) == [])
        expect("a missing root gives no finding", findings(root / "missing", limits, budget) == [])
    expect("each cache that LIMITS declares is a directory name", all("/" not in name for name in cache_dir.LIMITS))
    if failures:
        print(f"check-cache-size --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-cache-size --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check on the root of the caches, or the self-test.

    Returns:
        The exit code of the module text
    """
    if argv == ["--self-test"]:
        return self_test()
    parser = argparse.ArgumentParser(prog="check-cache-size.py", description=__doc__.splitlines()[0])
    check_report.add_arguments(parser)
    arguments = parser.parse_args(argv)
    budget = check_report.read_budgets()[CHECK]
    return check_report.emit(findings(cache_dir.base_root(), cache_dir.LIMITS, budget), CHECK, arguments.warnings_dir)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
