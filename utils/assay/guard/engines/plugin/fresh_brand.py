"""fresh-brand — no caller names or deduces the fresh brand of a mint.

THE RULE
    A mint takes its fresh brand from the default argument of a template
    parameter, a closure type that CRUCIBLE_FRESH_BRAND or
    CRUCIBLE_SCOPE_BRAND of include/foundation/Brand.h writes.  A caller that
    gives that parameter a brand that it holds makes a second value of one
    identity, such as a second exclusive token.  The quarantine plugin of
    utils/tools/quarantine/ reads each function template of the tree whose
    parameter has a closure as its default (fresh_brand.cpp).  The kinds:
      deduced-brand   the parameter stands in a deduced context of the
                      function type, so the address of the function with a
                      target type, a conversion or an explicit specialization
                      names a brand
      named-brand     no parameter pack stands before the parameter, so an
                      explicit template argument names a brand
    The entity is the qualified name of the template and the position of the
    parameter in its template parameter list.  The rule reads those finding
    lines in the sections of the objects of the target all of one build
    (assay.guard.engines.plugin.runner, THE SECTIONS).

    The plugin makes each finding a compile error in each build and each
    mode, so no object holds a line of the rule.  The rule has no ledger, and
    each line that a report holds is an error.  A test of an open route puts
    the template in an opt-out region of the reason class PROBE:, and the rule
    quarantine counts the opted-out finding.  Each problem of the census of
    the runner is an error too.

Usage
    guard check --rule fresh-brand --build-dir DIR [--warnings-dir DIR]
    guard check --engine plugin --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import runner

RULE = "fresh-brand"
ACTIONS = {
    "deduced-brand": "stands in a deduced context of the function type, so a caller can deduce a brand that it "
    "holds.  Write the brand through foundation::brand::undeducible_brand_t of include/foundation/Brand.h, which "
    "keeps it out of each deduction",
    "named-brand": "has no parameter pack before it, so an explicit template argument names a brand that the caller "
    "holds.  Put a pack class... Refused before the brand and require that the pack is empty",
}


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each line of the rule in the build, and each problem of the census.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    found = census.errors(RULE)
    for item in census.of_rule(RULE):
        action = ACTIONS.get(item.kind, "is a finding of the rule")
        found.append(
            check_report.Finding(
                "error",
                item.file,
                item.line,
                RULE,
                f"the fresh brand of {item.entity} {action} (CLAUDE.md section XXII)",
                item.kind,
                item.entity,
            )
        )
    return found
