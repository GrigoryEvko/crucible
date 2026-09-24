#!/usr/bin/env python3
"""The shrink-only ledger of uses after move that the use_after_move guard misses.

use_after_move_evasions.cpp holds one function for each evasion.  This test
scans that file with the guard and requires two things:

- The guard reports none of them.  A report means the guard learned the
  shape: delete the function from the corpus and its row from LEDGER.
- Every function in the corpus has a row in LEDGER, and the ledger does not
  grow past its bound.

Run from the repository root, or pass the root as the only argument.
"""
import importlib.util
import re
import sys
from pathlib import Path

LEDGER: dict[str, str] = {}
LEDGER_BOUND = 0

CORPUS = Path("test/guard_attacks/use_after_move_evasions.cpp")
GUARD = Path("scripts/check-use-after-move.py")


def load_guard(root: Path):
    """Import the guard as a module, so its scanner can read one file."""
    spec = importlib.util.spec_from_file_location("check_use_after_move", root / GUARD)
    module = importlib.util.module_from_spec(spec)
    sys.modules["check_use_after_move"] = module
    spec.loader.exec_module(module)
    return module


def main(argv: list[str]) -> int:
    """Scan the corpus, and compare what the guard reports with the ledger."""
    root = Path(argv[0]) if argv else Path.cwd()
    guard = load_guard(root)
    findings = guard.scan_file(str(CORPUS), str(root))
    functions = re.findall(r"\bvoid\s+(evade_\w+)\s*\(", (root / CORPUS).read_text())
    failures = []
    for finding in findings:
        failures.append(f"the guard now catches {finding.function} ({finding.key} at line {finding.line}): "
                        f"delete the function from {CORPUS} and its row from LEDGER")
    for name in functions:
        if name not in LEDGER:
            failures.append(f"{name} has no ledger row: state why the guard cannot see it")
    for name in LEDGER:
        if name not in functions:
            failures.append(f"stale ledger row {name}: the corpus no longer holds it, delete the row")
    if len(LEDGER) > LEDGER_BOUND:
        failures.append(f"the ledger holds {len(LEDGER)} rows, above its bound {LEDGER_BOUND}; it only shrinks")
    for failure in failures:
        print(f"FAIL: {failure}", file=sys.stderr)
    if failures:
        return 1
    print(f"test_use_after_move_attacks: {len(functions)} evasions on the ledger, each still unseen")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
