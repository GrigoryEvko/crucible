"""view-escape — a function keeps no borrow of an object that ends with its frame.

Rule
    The quarantine plugin of utils/tools/quarantine/ reads each return of each
    function whose return type holds a borrow, and gives a finding of the kind
    escaped-borrow for a return whose borrow names a local, a parameter by
    value or a temporary, through the calls that pass a borrow on, in each
    build and each mode (escape.cpp).  A read of the result reads a dead stack
    frame.  The kind stored-borrow is a store of such a borrow into an object
    that outlives the frame: the object of a parameter by reference, a member
    of the object of a member function, or an object of static or thread
    storage (escape.cpp, the stores).  A later read of the store reads a dead
    stack frame.  The rule reads those finding lines in the sections of the
    objects of the target all of one build (assay.guard.engines.plugin.runner,
    the sections).  The same line from two objects counts one time.

    A View with a brand or a lifetime parameter would carry the fact in its
    type.  C++ gives neither, so the plugin holds it.

    The rule has no ledger, and each finding is an error.  A return or a store
    in an opt-out region is a finding of the kind opted-out of the rule
    quarantine, and the ledger of that rule counts it.  Each problem of the
    census of the runner is an error too.

Usage
    guard check --rule view-escape --build-dir DIR [--warnings-dir DIR]
    guard check --engine plugin --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import runner

RULE = "view-escape"
# The kind of a store (the rule).
STORED_KIND = "stored-borrow"


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each return or store of a borrow of an object that ends with its frame, and one error for
    each problem of the census.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    found = census.errors(RULE)
    for item in census.of_rule(RULE):
        if item.kind == STORED_KIND:
            message = (
                f"a function stores a borrow of an object that ends with its frame ({item.entity}), so a later "
                f"read of the store reads a dead stack frame.  Store a value that owns its storage, or a borrow of "
                f"an object that outlives the store"
            )
        else:
            message = (
                f"a function returns a borrow of an object that ends with its frame ({item.entity}), so a read of "
                f"the result reads a dead stack frame.  Return a value that owns its storage, or take the object "
                f"by a reference that CRUCIBLE_LIFETIMEBOUND marks"
            )
        found.append(check_report.Finding("error", item.file, item.line, RULE, message, item.kind, item.entity))
    return found
