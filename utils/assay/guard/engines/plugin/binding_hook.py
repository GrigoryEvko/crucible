"""binding-hook — no code calls the hook get<I> of a structured binding by name.

THE RULE
    The quarantine plugin of utils/tools/quarantine/ gives a finding of the
    kind binding-hook for each call of a function with the annotation
    foundation::linear::binding_hook of include/foundation/Linear.h outside
    the initializer of a name of a binding, in each build and each mode
    (binding.cpp).  The rule reads those finding lines in the sections of the
    objects of the target all of one build (assay.guard.engines.plugin.runner,
    THE SECTIONS).  The same line from two objects counts one time.  The
    entity of a finding is the qualified name of the hook.

    The rule has no ledger, and each finding is an error.  A call in an
    opt-out region is a finding of the kind opted-out of the rule quarantine,
    and the ledger of that rule counts it.  Each problem of the census of the
    runner is an error too.

Usage
    guard check --rule binding-hook --build-dir DIR [--warnings-dir DIR]
    guard check --engine plugin --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import runner

RULE = "binding-hook"


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each call of the hook of a structured binding in the build, and each problem of the census.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    found = census.errors(RULE)
    for item in census.of_rule(RULE):
        found.append(
            check_report.Finding(
                "error",
                item.file,
                item.line,
                RULE,
                f"a call of the hook `{item.entity}` of a structured binding.  Write a structured binding of the "
                f"product: a direct call is a second form of the access to a part, and a call on an rvalue moves one "
                f"part out and drops the other parts (CLAUDE.md section XXII)",
                item.kind,
                item.entity,
            )
        )
    return found
