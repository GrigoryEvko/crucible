"""stub-discipline — each header with a stub honesty marker has a CRUCIBLE_STUB deprecation, and the reverse.

THE RULE
    An honesty marker is a constexpr bool with the value `false` and a name
    that ends with _implemented, _attached or _ready.  It states at compile
    time that a surface does not do its live work.  A stub deprecation is the
    attribute deprecated with a reason that starts with CRUCIBLE_STUB:.  Each
    call of the stub then gets a warning at compile time.  The pair holds in
    each header of include/.  The quarantine plugin reads each declaration of
    a header (utils/tools/quarantine/declaration.cpp, THE RULE
    stub-discipline):
      marker-without-stub   a header with a marker and no stub deprecation,
                            at its first marker.  The stub looks live
      stub-without-marker   a header with a stub deprecation and no marker,
                            at its first deprecation.  Code has no constant
                            to test the stub by

    The rule has no ledger, and each finding is an error.  Each problem of the
    census of the runner is an error too.

Usage
    guard check --rule stub-discipline --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the lines of the rule.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, runner

RULE = "stub-discipline"
ACTIONS = {
    "marker-without-stub": "is an honesty marker, and its header has no CRUCIBLE_STUB deprecation.  Put "
    '[[deprecated("CRUCIBLE_STUB: ...")]] on each stub entry point',
    "stub-without-marker": "has a CRUCIBLE_STUB deprecation, and its header has no honesty marker.  Declare a "
    "constexpr bool *_implemented, *_attached or *_ready with the value false",
}


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each header with a marker and no stub deprecation, and the reverse, and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return declarations.rule_findings(context, RULE, ACTIONS)
