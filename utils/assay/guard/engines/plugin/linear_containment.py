"""linear-containment — a linear value stays in a class that keeps it linear.

THE RULE
    The quarantine plugin of utils/tools/quarantine/ reads each class and each
    declaration of a unit (linear.cpp, THE SHAPES).  The kinds:
      pinned-holder     a class that holds a linear part by value, cannot move,
                        and has no door at the end of its scope: no annotation
                        scope_end_door and no destructor that is a door.  Its
                        destructor drops the linear part
      copyable-holder   a class that holds a linear part and has a
                        user-provided copy constructor or copy assignment that
                        is not deleted, so a copy makes a second value
      library-holder    a field, a return value, a parameter by value, a static
                        object or a local whose type holds a class of the
                        library with a linear argument.  Such a class is no
                        aggregate, and it is linear by its parts or it has a
                        destructor that is not trivial, such as std::vector,
                        std::optional, std::expected, std::pair and std::tuple.
                        Its operations copy or drop a value with no door.  A
                        local that a call of the same type initializes takes the
                        type of the callee, so the signature of the callee holds
                        the line
    The entity of a line names the class, or the declaration and the class of
    the library.

THE LEDGER, THE WRITE
    assay.guard.engines.plugin.linear_family.

Usage
    guard check --rule linear-containment --build-dir DIR [--warnings-dir DIR]
    guard write --rule linear-containment --build-dir DIR [--raise] [--reason TEXT]

Complexity: linear in the lines of the rule.
"""

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.plugin import linear_family, runner

RULE = "linear-containment"
REFUSED_KINDS = linear_family.REFUSED_KINDS
ACTIONS = {
    "pinned-holder": "the class cannot move, and its destructor drops a linear value with no door.  Give the class the "
    "annotation scope_end_door, or a door destructor",
    "copyable-holder": "a copy of the class makes a second linear value.  Remove the copy",
    "library-holder": "a class of the library holds a linear value, and its operations copy or drop it with no door.  "
    "Hold the value in Option, Result or OneOf of the Choice family",
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
