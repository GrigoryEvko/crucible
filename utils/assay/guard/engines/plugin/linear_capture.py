"""linear-capture — a closure takes a linear object by value, never by reference.

THE RULE
    The quarantine plugin of utils/tools/quarantine/ reads the initialization of
    each capture of each closure in a body (linear.cpp, THE SHAPES).  The kind:
      reference-capture  a capture by reference of a linear object, by its name
                         or by the default capture.  No rule sees a consume
                         through such a capture, and the rule must-consume gives
                         the captured object no line
    Decision 9 of misc/12_10_2026_linear.md: a closure with a linear capture is
    linear by its parts, the API that runs it takes it by value, and the call
    is std::move(closure)().

THE LEDGER, THE WRITE
    assay.guard.engines.plugin.linear_family.

Usage
    guard check --rule linear-capture --build-dir DIR [--warnings-dir DIR]
    guard write --rule linear-capture --build-dir DIR [--raise] [--reason TEXT]

Complexity: linear in the lines of the rule.
"""

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.plugin import linear_family, runner

RULE = "linear-capture"
REFUSED_KINDS = linear_family.REFUSED_KINDS
ACTIONS = {
    "reference-capture": "the closure captures a linear object by reference, so no rule sees its consume.  Move the "
    "object into the closure",
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
