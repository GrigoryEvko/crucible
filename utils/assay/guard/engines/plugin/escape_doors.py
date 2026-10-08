"""escape-doors — each public function of a base header that hands out a raw reference or pointer is an admitted door.

THE RULE
    fixy is safe by default.  A raw reference or pointer goes out of a wrapper
    only through an explicit door, and the compiler checks the types of the
    door.  The quarantine plugin reads each public function of a base header
    that returns a reference or a raw pointer, or can through decltype(auto).
    These are the doors (utils/tools/quarantine/declaration.cpp, THE RULE
    escape-doors):
      * An accessor is no finding: an operator other than a conversion, and a
        getter with a name of the list kAccessors of the plugin
      * A mint is a door of the rule mint-pattern, and no finding
      * A hatch, such as from_raw, into_raw, release or declassify, is the
        kind hatch: an explicit and discouraged door
      * Each other door is the kind bare-door, a conversion too.
    A deleted function, a friend declaration with no body, a `private` or a
    `protected` member and a name that ends with an underscore are no door.
    The foundation/reflect/RawEscape.h census reads the members of each
    wrapper through reflection, and this rule reads the free functions and
    the function templates too.

THE LEDGER
    A row of utils/ledgers/escape-doors.txt admits one door:
    `PATH | KIND | admit | ENTITY | REASON`.  The entity is the full name of
    the function, with the types of its parameters and its qualifiers.  Each
    hatch has its own row, and a reviewer then reads each new hatch.  A row that
    admits no finding fails the rule.

Usage
    guard check --rule escape-doors --build-dir DIR [--warnings-dir DIR]
    guard write --rule escape-doors --build-dir DIR

Complexity: linear in the lines of the rule.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, runner

RULE = "escape-doors"
ACTIONS = {
    "bare-door": "hands out a raw reference or pointer, and it is no accessor, no mint and no hatch.  Make it an "
    "accessor, a mint or a hatch, or add a ledger row with the reason",
    "hatch": "is a hatch that hands out a raw reference or pointer.  Add a ledger row with the reason, or remove it",
}


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each bare door and each hatch, and each problem of the census.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return declarations.rule_findings(context, RULE, ACTIONS)
