"""view-escape — a function returns no borrow of an object that ends with its frame.

Rule
    The quarantine plugin of utils/tools/quarantine/ reads each return of each
    function whose return type holds a borrow, and gives a finding of the kind
    escaped-borrow for a return whose borrow names a local, a parameter by
    value or a temporary, through the doors of the base that make a borrow, in
    each build and each mode (escape.cpp).  A read of the result reads a dead
    stack frame.  The rule reads those finding lines in the sections of the
    objects of the target all of one build (assay.guard.engines.plugin.runner,
    the sections).  The same line from two objects counts one time.

    A View with a brand or a lifetime parameter would carry the fact in its
    type.  C++ gives neither, so the plugin holds it.

    The rule has no ledger, and each finding is an error.  A return in an
    opt-out region is a finding of the kind opted-out of the rule quarantine,
    and the ledger of that rule counts it.  Each problem of the census of the
    runner is an error too.

Usage
    guard check --rule view-escape --build-dir DIR [--warnings-dir DIR]
    guard check --engine plugin --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import runner

RULE = "view-escape"


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each return of a borrow of an object that ends with its frame, and each problem of the census.

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
                f"a function returns a borrow of an object that ends with its frame ({item.entity}), so a read of "
                f"the result reads a dead stack frame.  Return a value that owns its storage, or take the object "
                f"by a reference that CRUCIBLE_LIFETIMEBOUND marks",
                item.kind,
                item.entity,
            )
        )
    return found
