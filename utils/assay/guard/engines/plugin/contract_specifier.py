"""contract-specifier — no declaration of the tree has a `pre` or a `post` contract specifier.

THE RULE
    The quarantine plugin of utils/tools/quarantine/ gives a finding of the
    kind contract-specifier for each `pre` and each `post` specifier on a
    declaration of a file in the source root, in each build and each mode
    (contract.cpp, and CLAUDE.md section XII, the contract rule).  The rule
    reads those finding lines in the sections of the objects of the target
    all of one build (assay.guard.engines.plugin.runner, THE SECTIONS).  The
    same line from two objects counts one time.  The entity of a finding is
    pre or post.

    The rule has no ledger, and each finding is an error.  A specifier in an
    opt-out region is a finding of the kind opted-out of the rule quarantine,
    and the ledger of that rule counts it.  Each problem of the census of the
    runner is an error too.

Usage
    guard check --rule contract-specifier --build-dir DIR [--warnings-dir DIR]
    guard check --engine plugin --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import runner

RULE = "contract-specifier"
# The statement that replaces each specifier (CLAUDE.md section XII).
MACROS = {"pre": "CRUCIBLE_PRE(condition)", "post": "CRUCIBLE_POST(result, condition)"}


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each contract specifier of the build, and each problem of the census.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    found = census.errors(RULE)
    for item in census.of_rule(RULE):
        macro = MACROS.get(item.entity, f"{MACROS['pre']} or {MACROS['post']}")
        found.append(
            check_report.Finding(
                "error",
                item.file,
                item.line,
                RULE,
                f"a `{item.entity}` contract specifier on a declaration.  Write {macro} as a statement of the body.  "
                f"GCC 16 does not keep the contract specifier of a template in a header unit or in a precompiled header, "
                f"and a constant evaluation can ignore the specifier (CLAUDE.md section XII)",
                item.kind,
                item.entity,
            )
        )
    return found
