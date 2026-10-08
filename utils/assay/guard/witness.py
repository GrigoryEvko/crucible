"""guard witness — run each rule on its plants and its controls.

usage: guard witness [--rule ID]... [--engine E]...

A plant is a tree in utils/plants/RULE/ that the rule must refuse, and a
control is a tree that it must accept (assay.guard.engines.meta.runner, THE
WITNESS).  The command runs the witness of each rule that the call names and
that has a module of its own.  The rule rule-witness holds that each rule has a
minimum of one plant and one control.
"""

import sys

from assay.core import registry as rule_registry
from assay.core.paths import REPO_ROOT
from assay.guard import check


def main(argv: list[str]) -> int:
    """Run the witness of each rule that the command line names.

    Returns:
        0 when each witness holds, 1 when one fails, 2 on a usage error
    """
    from assay.core import cli
    from assay.guard.engines.meta import runner

    parser = cli.parser("guard witness", "run each rule on its plants and its controls")
    parser.add_argument("--rule", dest="rules", action="append", default=[], metavar="ID")
    parser.add_argument("--engine", dest="engines", action="append", default=[], metavar="E")
    options = parser.parse_args(argv)
    registry = check.load_registry()
    if registry is None:
        return 2
    chosen = check.selected(registry, options.rules, options.engines)
    if isinstance(chosen, str):
        print(f"guard witness: {chosen}", file=sys.stderr)
        return 2
    statuses = [runner.witness(rule, REPO_ROOT) for rule in chosen if rule.engine == rule_registry.Engine.META]
    tree_rules = [rule for rule in chosen if rule.engine == rule_registry.Engine.TSAST]
    if tree_rules:
        from assay.guard.engines.tsast import runner as tree_runner

        statuses += [tree_runner.witness(rule, REPO_ROOT) for rule in tree_rules]
    return max(statuses)
