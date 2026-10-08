"""permission-storage — a Permission held as a class member is the [[no_unique_address]] member of a handle.

THE RULE
    A Permission token is linear.  A class that holds one is correct when the
    class is a handle that proves single ownership through its lifetime.  The
    mark [[no_unique_address]] folds the token to no byte, and it states the
    handle shape.  A member with no such mark can be aliased across threads,
    and the type system does not see it.  The quarantine plugin gives a
    finding for each such member (utils/tools/quarantine/declaration.cpp, THE
    RULE permission-storage):
      permission-member   a data member with a type that names a Permission,
                          also through a template argument or an alias, with
                          no [[no_unique_address]]
      static-permission   a static data member with a type that names a
                          Permission
    A pointer or a reference member holds no token of its own, and it is
    none.  The rule reads the lines in the scope of its row of
    utils/registry.txt.  include/foundation/permissions/Permission.h gives the
    token and its pool, and it gives no finding.

    The rule has no ledger, and each finding is an error.  Each problem of the
    census of the runner is an error too.

Usage
    guard check --rule permission-storage --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the lines of the rule.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, runner

RULE = "permission-storage"
# The header that gives the token and its pool.
SUBSTRATE = frozenset({"include/foundation/permissions/Permission.h"})
ACTIONS = {
    "permission-member": "holds a Permission with no [[no_unique_address]].  Make the member the "
    "[[no_unique_address]] member of a handle",
    "static-permission": "is a static member that holds a Permission.  Make it the [[no_unique_address]] member of "
    "a handle",
}


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each member that holds a Permission and has no handle shape, and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return declarations.rule_findings(context, RULE, ACTIONS, SUBSTRATE)
