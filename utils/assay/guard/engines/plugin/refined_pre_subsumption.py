"""refined-pre-subsumption — a precondition does not test again the predicate of a Refined parameter.

THE RULE
    A parameter of type fixy::Refined<Pred, T> or fixy::SealedRefined<Pred,
    T> carries a proof: the predicate held when the value was built.  The
    quarantine plugin of utils/tools/quarantine/ reads each call of
    decide::NAME in a CRUCIBLE_PRE, CRUCIBLE_PRE_FAST or CRUCIBLE_PRE_MSG of
    the same function.  When NAME tests that predicate and the arguments name
    the parameter, the call is a finding of the kind `retest` (THE REFINED
    PARAMETERS in expression.cpp).  A cite row of the rule table gives the
    procedures that test each predicate.  The entity of a finding is
    `FUNCTION: PARAMETER: decide::NAME`.

    The rule reads the finding lines in the sections of the objects of the
    target all of one build (assay.guard.engines.plugin.runner, THE
    SECTIONS).  It keeps the files in the scope of its row of the registry,
    because a test spells the pattern on purpose.

THE LEDGER
    utils/ledgers/refined-pre-subsumption.txt admits a deliberate test with a
    count and a reason (assay.core.ledger).  Each problem of the census of
    the runner is an error too.

Usage
    guard check --rule refined-pre-subsumption --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build.
"""

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.plugin import runner

RULE = "refined-pre-subsumption"


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each test of a proved predicate in the scope of the rule, and each problem of the census.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    scope = rule_registry.load().rules[RULE].scope
    found = census.errors(RULE)
    for item in census.of_rule(RULE):
        if not item.file.startswith(scope):
            continue
        found.append(
            check_report.Finding(
                "error",
                item.file,
                item.line,
                RULE,
                f"a precondition tests again a predicate that the type of the parameter proves ({item.entity}).  "
                f"Remove the precondition, or give it a reviewed row with its reason in the ledger",
                item.kind,
                item.entity,
            )
        )
    return found
