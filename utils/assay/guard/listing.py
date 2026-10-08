"""guard list — print the rules of the registry, or the ledger rows of each rule for one file.

usage: guard list [--engine E]
       guard list --path FILE

The first form prints one line for each rule: its ID, its engine, its
policy, its thresholds, and the module that holds it (its own module, its
legacy module or its holder).  The second form prints each ledger row of each
rule of utils/ledgers/ whose path is FILE, so a reader at the code sees each
admission of that file.
"""

import sys
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT
from assay.core.registry import Rule
from assay.guard import check


def holder(rule: Rule) -> str:
    """Return the words that tell what holds a rule."""
    if rule.legacy is not None:
        return f"legacy assay.{rule.legacy.module}" + (f" {' '.join(rule.legacy.words)}" if rule.legacy.words else "")
    if rule.held_by is not None:
        return f"held by {rule.held_by}"
    return f"module assay.guard.engines.{rule.engine.value}.{rule.id.replace('-', '_')}"


def main(argv: list[str]) -> int:
    """Print the rules, or the ledger rows of one file.

    Returns:
        0, or 2 on a usage error
    """
    from assay.core import cli

    parser = cli.parser("guard list", "print the rules of the registry, or the ledger rows of one file")
    parser.add_argument("--engine", default=None)
    parser.add_argument("--path", default=None, metavar="FILE")
    options = parser.parse_args(argv)
    registry = check.load_registry()
    if registry is None:
        return 2
    if options.path is not None:
        wanted = Path(options.path).as_posix().removeprefix("./")
        shown = 0
        for rule in registry.rules.values():
            if not rule_ledger.ledger_path(rule.id).is_file():
                continue
            try:
                ledger = rule_ledger.read(rule)
            except (rows_grammar.RowsError, OSError) as problem:
                print(f"guard list: the ledger of {rule.id} cannot be read: {problem}", file=sys.stderr)
                continue
            for key, entry in sorted(ledger.base.items()):
                if key.path == wanted:
                    shown += 1
                    print(f"{rule_ledger.shown_path(rule.id)}:{entry.line}: {rule.id} {key.kind or '-'} "
                          f"{rule_ledger.value_text(entry.value)} {key.entity or '-'}: {entry.reason or 'no reason'}")
        print(f"guard list: {shown} ledger rows of utils/ledgers/ name {wanted}.  The legacy ledgers of "
              f"utils/scripts/ are not in this list")
        return 0
    for rule in sorted(registry.rules.values(), key=lambda item: item.id):
        if options.engine is not None and rule.engine.value != options.engine:
            continue
        threshold = (f" warn {check_report.threshold_text(rule.threshold.warn)} error "
                     f"{check_report.threshold_text(rule.threshold.error)} {rule.threshold.unit}"
                     if rule.threshold is not None else "")
        print(f"{rule.id:28} {rule.engine.value:6} {rule.policy.value:8}{threshold}; {holder(rule)}")
    print(f"guard list: {len(registry.rules)} rules in {REPO_ROOT / 'utils' / 'registry.txt'}")
    return 0
