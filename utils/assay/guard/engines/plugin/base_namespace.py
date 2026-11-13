"""base-namespace — a file outside the base declares nothing in a namespace of the base.

THE RULE
    The quarantine plugin of utils/tools/quarantine/ gives a finding for each
    declaration in a namespace of the base, foundation or fixy and each
    namespace inside them, that a file outside the base spells, in each build
    and each mode (namespace.cpp):
      declaration      a declaration of such a namespace, a redeclaration of an
                       entity of the base, the definition of an entity that the
                       base only declares, and the definition of a member of a
                       class of the base outside the class
      using-directive  a using-directive of such a namespace, and a
                       using-directive of the global namespace when the unit
                       reads a file of the base after it
    An explicit or a partial specialization is no finding: the rules home and
    specialization-home judge it.  The rule reads the finding lines in the
    sections of the objects of the target all of one build
    (assay.guard.engines.plugin.runner, THE SECTIONS).  The same line from two
    objects counts one time.

    The rule has no ledger, and each finding is an error.  A declaration in an
    opt-out region is a finding of the kind opted-out of the rule quarantine,
    and the ledger of that rule counts it.  Each problem of the census of the
    runner is an error too.

Usage
    guard check --rule base-namespace --build-dir DIR [--warnings-dir DIR]
    guard check --engine plugin --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import runner

RULE = "base-namespace"

# The text of the error of each kind, before the entity.
MESSAGES = {
    "declaration": "a file outside the base declares an entity in a namespace of the base",
    "using-directive": "a using-directive outside the base can change a lookup of the base",
}


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each declaration in a namespace of the base outside the base, and each problem of the census.

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
                f"{MESSAGES.get(item.kind, item.kind)} ({item.entity}).  Declare the entity in a namespace of this "
                f"file, extend a function of the base through an overload beside your own type, or move the "
                f"directive into a function body (CLAUDE.md section XXII)",
                item.kind,
                item.entity,
            )
        )
    return found
