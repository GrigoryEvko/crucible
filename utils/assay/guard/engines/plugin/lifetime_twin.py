"""lifetime-twin — each parameter that claims a lifetime bound has the deleted overload that enforces it.

THE RULE
    GCC 16 does not read clang::lifetimebound or gnu::lifetimebound, and no
    compiler checks a lifetime bound.  A deleted overload that takes an
    rvalue reference at the same index refuses a temporary at the call.  It
    is the better match for a prvalue argument.  CRUCIBLE_LIFETIMEBOUND of
    foundation/Platform.h gives the attribute quarantine::lifetimebound, and
    the quarantine plugin reads it (utils/tools/quarantine/declaration.cpp,
    THE RULE lifetime-twin):
      missing-twin   a marked parameter with no twin.  The twin of the
                     parameter at index I of a function F is a deleted
                     overload of F.  It is in the scope of F and in the file
                     of the parameter.  It takes as many parameters as F,
                     or ends with a pack.  At index I it takes an rvalue
                     reference to the type of the parameter, or to a
                     template parameter
    A marker on a declaration that is no parameter is a compile error in
    each build.  The rule reads the lines in the scope of its row of
    utils/registry.txt.

    The rule has no ledger, and each finding is an error.  Each problem of the
    census of the runner is an error too.

Usage
    guard check --rule lifetime-twin --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the lines of the rule.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, runner

RULE = "lifetime-twin"
ACTIONS = {
    "missing-twin": "claims a lifetime bound, and no deleted overload refuses a temporary there.  Declare the "
    "overload with an rvalue reference at that index and `= delete`, beside the function",
}


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each marked parameter with no deleted overload, and each problem of the census.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return declarations.rule_findings(context, RULE, ACTIONS)
