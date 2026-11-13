"""brand-escape — a fresh brand stays in the scope that made it.

THE RULE
    A fresh brand is the closure type of a lambda with no capture and no
    parameter, such as CRUCIBLE_FRESH_BRAND of include/foundation/Brand.h in a
    template argument.  Two runs of one mint site share one brand, so a brand
    that leaves its scope lets a value of one run stand in for a value of the
    other (probe E3 of misc/12_10_2026_linear.md).  A brand that a template
    argument of the function or of its class gave came from outside, so it is
    no escape.  The type of a value argument, such as a function pointer, gives
    a brand too.  The quarantine plugin of utils/tools/quarantine/ reads each
    leaf (linear.cpp, THE SHAPES).  The kinds:
      escaped-return            a return type
      escaped-member            a data member of a class that is no closure
      escaped-static            an object with static or thread storage
                                duration, also when a template argument gave
                                the brand, because each run of the mint site
                                shares the object
      escaped-container         a local of a class of the library
      escaped-function-pointer  a local of a function pointer type
      escaped-alias             an alias outside a body
    The entity names the place of the lambda of the brand, because GCC prints
    each brand as <lambda()>.

THE SCOPE BRANDS
    A scope brand names one call of a scope mint, such as mint_branded_scope
    of include/foundation/core/Branded.h, and CRUCIBLE_SCOPE_BRAND writes it
    with the annotation foundation::brand::scope_brand.  A value of it leaves
    no call of the mint, also when a template argument gave the brand
    (linear.cpp, THE SCOPE BRANDS).  The two kinds:
      scope-static              an object with static or thread storage
                                duration that holds a scope brand, also a
                                static data member or a variable template
                                instance that a body names
      scope-return              a scope mint whose result holds its own brand
    The plugin makes each line of the two kinds a compile error in each build
    and each mode, so no object holds one.  No ledger row admits such a line,
    and a write refuses a build that holds one.

THE LEDGER, THE WRITE
    assay.guard.engines.plugin.linear_family.

Usage
    guard check --rule brand-escape --build-dir DIR [--warnings-dir DIR]
    guard write --rule brand-escape --build-dir DIR [--raise] [--reason TEXT]

Complexity: linear in the lines of the rule.
"""

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.plugin import linear_family, runner

RULE = "brand-escape"
REFUSED_KINDS = linear_family.REFUSED_KINDS
_ACTION = (
    "a fresh brand leaves the scope that made it.  Give the brand to the function as its own defaulted last "
    "template parameter, or give a stored identity a run-time id"
)
ACTIONS = {
    kind: _ACTION
    for kind in (
        "escaped-return",
        "escaped-member",
        "escaped-static",
        "escaped-container",
        "escaped-function-pointer",
        "escaped-alias",
    )
}
# The kinds of THE SCOPE BRANDS.  Each line is a compile error, and no ledger row admits one.
SCOPE_KINDS = ("scope-static", "scope-return")
ACTIONS["scope-static"] = (
    "an object of static or thread storage duration holds a value of a scope brand, so the value outlives the call "
    "of its mint.  Keep the value in an object of automatic storage duration in the body of the scope"
)
ACTIONS["scope-return"] = (
    "a scope mint gives a result that holds its own scope brand.  Give a result that holds no value of the scope, "
    "such as the number of an index"
)


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give an error for each line of the rule, each count row with no reason and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return linear_family.line_findings(context, RULE, ACTIONS)


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the ledger of the rule from the lines of the builds of the request (THE WRITE of linear_family).

    A line of a kind of SCOPE_KINDS is a problem of the build, so the write refuses the build.
    """
    return linear_family.write(
        rule, request, lambda context: linear_family.build_counts(rule, context, problem_kinds=SCOPE_KINDS)
    )
