"""guard explain — print one rule: its row, its section of CLAUDE.md, its action, its policy, its plants and its inputs.

usage: guard explain RULE

A rule module can give the function explained(context), which returns the
facts that the rule finds in the tree, one line for each fact.  The command
prints them in the part "derived".  The rule erased-brand gives its branded
templates there.
"""

import sys

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
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
    explained = getattr(runner.rule_module(rule), "explained", None) if rule.has_module else None
    if explained is not None:
        try:
            lines = list(explained(check.rule_context(rule, REPO_ROOT, None)))
        except runner.NotApplicable as reason:
            lines = [f"the facts of the tree are not available: {reason}"]
        check_report.write_line("\n".join(["  derived:", *(f"    {line}" for line in lines)]))
    text = rule_registry.doc_section(GUIDE.read_text(encoding="utf-8"), rule.doc) if GUIDE.is_file() else None
    print(f"  CLAUDE.md {rule.doc}:")
    print("\n".join(f"    {line}" for line in (text or "(no such section)").splitlines()))
    return 0
