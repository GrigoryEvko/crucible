"""guard explain — print one rule: its row, its section of CLAUDE.md, its action, its policy, its plants and its inputs.

usage: guard explain RULE
"""

import sys

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core.paths import REPO_ROOT
from assay.guard import check
from assay.guard.listing import holder

GUIDE = REPO_ROOT / "CLAUDE.md"


def main(argv: list[str]) -> int:
    """Print the rule that the command line names.

    Returns:
        0, 1 when the registry has no such rule, 2 on a usage error
    """
    if len(argv) != 1:
        print("usage: guard explain RULE", file=sys.stderr)
        return 2
    registry = check.load_registry()
    if registry is None:
        return 2
    rule = registry.rules.get(argv[0])
    if rule is None:
        print(f"guard explain: the registry has no rule {argv[0]}.  `guard list` prints the rules", file=sys.stderr)
        return 1
    from assay.guard.engines.meta import runner

    print(f"{rule.id}: {rule.refuses}")
    print(f"  action:  {rule.action}")
    print(f"  engine:  {rule.engine.value}, scope {', '.join(rule.scope)}")
    print(f"  held by: {holder(rule)}")
    if rule.threshold is not None:
        print(f"  thresholds: warning {rule.threshold.warn:g}, error {rule.threshold.error:g}, unit {rule.threshold.unit}")
    ledger = rule_ledger.shown_path(rule.id)
    if rule.policy == rule_registry.Policy.NONE:
        print("  ledger:  none")
    elif rule.has_module:
        print(f"  ledger:  {ledger}, policy {rule.policy.value}; `python3 utils/bin/guard write --rule {rule.id}` "
              f"writes it")
    else:
        print(f"  ledger:  policy {rule.policy.value}; the legacy module holds its own ledger file")
    plants = runner.plants_of(rule.id)
    print(f"  plants:  {', '.join(path.name for path in plants) if plants else 'none'}")
    if rule.inputs:
        print(f"  inputs:  {', '.join(rule.inputs)}")
    text = rule_registry.doc_section(GUIDE.read_text(encoding="utf-8"), rule.doc) if GUIDE.is_file() else None
    print(f"  CLAUDE.md {rule.doc}:")
    print("\n".join(f"    {line}" for line in (text or "(no such section)").splitlines()))
    return 0
