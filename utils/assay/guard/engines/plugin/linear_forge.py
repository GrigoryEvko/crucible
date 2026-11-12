"""linear-forge — no route of bytes builds a sealed value.

THE RULE
    A sealed type is a linear type, or a type that the checked lifetime start
    of include/foundation/Lifetime.h refuses: it carries the annotation
    no_start_over_bytes of include/foundation/ByteSeal.h, holds a part that
    carries it, or holds a reference.  The language refuses std::bit_cast of a
    class that is not trivially copyable, and -Werror=class-memaccess refuses a
    byte copy into a class with an assignment that is not trivial.  The rule
    owners refuses std::start_lifetime_as outside foundation/Lifetime.h.  The
    quarantine plugin of utils/tools/quarantine/ reads the two routes that stay
    (linear.cpp, THE SHAPES):
      sealed-union       a union that holds a sealed type.  A read of the
                         inactive member forges a value (probe E3 of
                         misc/12_10_2026_linear.md)
      unsealed-lifetime  a class with the annotation linear that the checked
                         lifetime start admits, because neither the class nor a
                         part of it carries no_start_over_bytes

THE LEDGER, THE WRITE
    assay.guard.engines.plugin.linear_family.

Usage
    guard check --rule linear-forge --build-dir DIR [--warnings-dir DIR]
    guard write --rule linear-forge --build-dir DIR [--raise] [--reason TEXT]

Complexity: linear in the lines of the rule.
"""

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.plugin import linear_family, runner

RULE = "linear-forge"
REFUSED_KINDS = linear_family.REFUSED_KINDS
ACTIONS = {
    "sealed-union": "the union holds a sealed value, and a read of its inactive member forges one.  Hold the value "
    "with a tag in the Choice family",
    "unsealed-lifetime": "the checked lifetime start admits the linear class, so a start over bytes forges a value.  "
    "Give the class the annotation no_start_over_bytes or a byte_seal member",
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
