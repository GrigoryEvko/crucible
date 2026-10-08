"""no-raw-array-member — no data member and no alias of a C array type in a base header.

THE RULE
    No build checks a subscript of a C array member.  A write after its end
    goes into the next member of the same object, and the address sanitizer
    sees valid memory there.  An array type of the base, such as
    `fixy::FixedArray`, checks each subscript.  The quarantine plugin gives a
    finding for each data member and each alias of a C array type
    (utils/tools/quarantine/declaration.cpp, THE RULE no-raw-array-member):
      array-member   a data member of a C array type
      array-alias    an alias or a typedef of a C array type
    A static data member is no part of the layout of the object, and it is
    none.  A probe type of a header self-test uses a C array on purpose.  A
    namespace with a name that ends with _self_test holds it, and it is none.
    The rule reads the lines in the scope of its row of utils/registry.txt.

THE LEDGER
    A row of utils/ledgers/no-raw-array-member.txt admits one member or one
    alias: `PATH | KIND | admit | ENTITY | REASON`.  The entity is the full
    name of the member or of the alias, with its namespaces and classes.  A
    row that admits no finding fails the rule.

Usage
    guard check --rule no-raw-array-member --build-dir DIR [--warnings-dir DIR]
    guard write --rule no-raw-array-member --build-dir DIR

Complexity: linear in the lines of the rule.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, runner

RULE = "no-raw-array-member"
ACTIONS = {
    "array-member": "is a data member of a C array type.  Use a fixed array of the base",
    "array-alias": "is an alias of a C array type.  Use a fixed array of the base",
}


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each member and each alias of a C array type, and each problem of the census.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return declarations.rule_findings(context, RULE, ACTIONS)
