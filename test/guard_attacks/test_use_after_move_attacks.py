#!/usr/bin/env python3
"""The shrink-only ledger of uses after move that the use_after_move guard misses.

use_after_move_evasions.cpp holds one function for each evasion.  This test
scans that file with the guard and requires these things:

- The guard reports none of the evasions.  A report means the guard learned
  the shape: delete the function from the corpus and its row from LEDGER.
- Each evade_ function in the corpus has a row in LEDGER, each row has its
  function, and the ledger does not grow past its bound.
- The scan reads the corpus.  The test scans a copy of the corpus with one
  real use after move added, and the guard must report that one and no
  other.  So a scan that reads nothing, or a corpus that does not parse,
  cannot pass.

The function names come from the parse tree of the corpus, not from a
text search, so a function that a search would miss still needs a row.

Run from the repository root, or pass the root as the only argument.
"""
import importlib.util
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "scripts"))

import tsast  # noqa: E402  (the path insert above has to come first)

LEDGER: dict[str, str] = {
    "evade_move_through_callee":
        "steal takes Token& and moves inside its body.  The call site does not say std::move, "
        "and the guard reads one body at a time.",
    "evade_use_through_pointer":
        "alias holds the address of token.  The guard does not track what a pointer points at, "
        "so *alias is a new key.",
    "evade_use_through_returned_reference":
        "alias binds to what same returns.  The guard binds a reference only to a plain path, "
        "and a call result is not one.",
    "evade_late_reference_capture":
        "The lambda body reads token by reference, and it runs after the move.  The guard walks the "
        "body where the lambda appears, before the move.",
    "evade_try_that_moves_on_failure":
        "try_keep takes its argument by value, so it moves even when it fails.  The try_ rule "
        "assumes that a failed try_ call leaves its argument whole.",
}
LEDGER_BOUND = 5
EVASION_PREFIX = "evade_"

CORPUS = Path("test/guard_attacks/use_after_move_evasions.cpp")
GUARD = Path("scripts/check-use-after-move.py")
CONTROL_NAME = "control_use_after_move"
CONTROL = f"""
namespace use_after_move_evasions {{
void {CONTROL_NAME}(Token token) {{ sink(std::move(token)); read(token); }}
}}
"""


def load_guard(root: Path):
    """Import the guard as a module, so its walker can read one file."""
    sys.path.insert(0, str(root / "scripts"))
    spec = importlib.util.spec_from_file_location("check_use_after_move", root / GUARD)
    module = importlib.util.module_from_spec(spec)
    sys.modules["check_use_after_move"] = module
    spec.loader.exec_module(module)
    return module


def scan(guard, path: Path) -> tuple[list, list[str]]:
    """Walk one file with the guard, and return its findings and its function names."""
    tree = next(tsast.parse([path], strict=False))
    walk = guard.Walk(tree, str(CORPUS))
    names = [walk.function_name(function) for function in tree.find("function_definition")]
    return walk.run(), names


def main(argv: list[str]) -> int:
    """Scan the corpus and a copy with a control, and compare what the guard reports with the ledger."""
    root = Path(argv[0]) if argv else Path.cwd()
    try:
        guard = load_guard(root)
        findings, names = scan(guard, root / CORPUS)
        with tempfile.TemporaryDirectory() as tmp:
            copy = Path(tmp) / CORPUS.name
            copy.write_text((root / CORPUS).read_text(encoding="utf-8") + CONTROL, encoding="utf-8")
            control_findings, _ = scan(guard, copy)
    except tsast.KitMissing as exc:
        print(f"test_use_after_move_attacks: {exc}", file=sys.stderr)
        return 3
    functions = [name for name in names if name.startswith(EVASION_PREFIX)]
    failures = []
    for finding in findings:
        if finding.key == "<parse-error>":
            failures.append(f"the guard cannot parse {CORPUS}: fix the corpus")
            continue
        failures.append(f"the guard now catches {finding.function} ({finding.key} at line {finding.line}): "
                        f"delete the function from {CORPUS} and its row from LEDGER")
    control = [(f.function, f.key) for f in control_findings]
    if control != [(CONTROL_NAME, "token")] and not failures:
        failures.append(f"the scan of the corpus with a planted use after move reported {control}, "
                        f"where it must report only {CONTROL_NAME}: the scan does not read the corpus")
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
