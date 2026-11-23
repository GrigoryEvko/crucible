"""drop-door — each end of a linear value is a named door.

Rule
    The quarantine plugin of utils/tools/quarantine/ walks each body (linear.cpp,
    the shapes).  The rule must-consume reads the tracked locals and parameters,
    and this rule reads each other linear object.  The kinds:
      untracked-assignment  an assignment over a linear object that is no tracked
                            object of must-consume: a member, an element, or an
                            object behind a reference or a pointer.  The old
                            value drops.  A parameter of a reference that is not
                            const is a lent object of must-consume (the lent
                            parameters of linear.cpp), so must-consume reads an
                            assignment over it
      destroy               an explicit destructor call of a linear object that
                            is no tracked object, the clobber of a trivial
                            destructor, and a delete-expression
    A door, a destructor, a move, and a constructor or an assignment of a class
    with the annotation linear end or move their values by their contract, so
    the rule reads none of them (P10 of misc/12_10_2026_linear.md).
    The entity of a line is FUNCTION: OBJECT (TYPE): TYPE is the type of the
    object that ends, with its template arguments and with each fresh brand
    written as the word brand.  So an instance of one template that ends a new
    type at one line, such as a new handle in the payload of an Option, gives
    its own line, and the ledger counts it.

Ledger, write
    assay.guard.engines.plugin.linear_family.

Usage
    guard check --rule drop-door --build-dir DIR [--warnings-dir DIR]
    guard write --rule drop-door --build-dir DIR [--raise] [--reason TEXT]

Complexity: linear in the lines of the rule.
"""

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.plugin import linear_family, runner

RULE = "drop-door"
REFUSED_KINDS = linear_family.REFUSED_KINDS
ACTIONS = {
    "untracked-assignment": "the assignment drops the old linear value with no door.  Give the old value to its "
    "consumer first, or end it through a function with the annotation drop_door",
    "destroy": "the code ends a linear value with no door.  End it through a function with the annotation drop_door "
    "or detach_door",
}


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give an error for each line of the rule, each count row with no reason and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return linear_family.line_findings(context, RULE, ACTIONS)


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the ledger of the rule from the lines of the builds of the request (the write of linear_family)."""
    return linear_family.write(rule, request)
