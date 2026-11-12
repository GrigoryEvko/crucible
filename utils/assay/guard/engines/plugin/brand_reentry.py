"""brand-reentry — a branded value never reaches the mint site that made it again.

THE RULE
    The quarantine plugin of utils/tools/quarantine/ records the call graph of
    the bodies of each unit (linear.cpp, THE SHAPES).  At the end of the unit
    it gives a line for each call with a branded argument, made in the
    function that minted the brand, whose callee can reach that function again.
    The second run mints the same brand, so a value of one run can stand in
    for a value of the other.  The kind:
      reentry   such a call
    The graph holds the bodies that the unit reads, so a path through a body
    of a library, such as std::invoke, gives no line.

THE LEDGER, THE WRITE
    assay.guard.engines.plugin.linear_family.

Usage
    guard check --rule brand-reentry --build-dir DIR [--warnings-dir DIR]
    guard write --rule brand-reentry --build-dir DIR [--raise] [--reason TEXT]

Complexity: linear in the lines of the rule.
"""

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.plugin import linear_family, runner

RULE = "brand-reentry"
REFUSED_KINDS = linear_family.REFUSED_KINDS
ACTIONS = {
    "reentry": "the callee can reach the mint site of the brand again, so two runs share one brand.  Mint the brand "
    "outside the recursion, or give each run a run-time id",
}


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give an error for each line of the rule, each count row with no reason and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return linear_family.line_findings(context, RULE, ACTIONS)


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the ledger of the rule from the lines of the builds of the request (THE WRITE of linear_family)."""
    return linear_family.write(rule, request)
