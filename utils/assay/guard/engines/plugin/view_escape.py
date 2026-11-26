"""view-escape — no code keeps or reads a borrow of an object that ended.

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
    stack frame.  The kind static-borrow is an object of static or thread
    storage whose type holds a borrow with no brand of static storage.  The
    kind stale-borrow is a use of a name after an event that ends the object
    that the name borrows: a move, an assignment, a call or the end of a scope
    (live.cpp, the ends).  The kind two-names is a call whose two arguments
    name one object of a linear class or of a unique borrow class, and one of
    them can write or move it (live.cpp, the two names).  One argument names
    the object itself, and the other one names it itself too, or gives a loan
    or a capture of it.  The body of a door of the base with the annotation
    two_names_door gives no line of this kind.  The kind number-borrow is a
    borrow class that names no object: it holds no pointer and states no
    refers_by_number{} of foundation/Transfer.h, so no end can reach its loans
    (route.cpp, the numbers).  The rule reads those
    finding lines in the sections of the objects of the target all of one build
    (assay.guard.engines.plugin.runner, the sections).  The same line from two
    objects counts one time.

    A View with a brand or a lifetime parameter would carry the fact in its
    type.  C++ gives neither, so the plugin holds it.

    The rule has no ledger, and each finding is an error.  A return, a store,
    an object or a use in an opt-out region is a finding of the kind opted-out
    of the rule quarantine, and the ledger of that rule counts it.  Each
    problem of the census of the runner is an error too.

Usage
    guard check --rule view-escape --build-dir DIR [--warnings-dir DIR]
    guard check --engine plugin --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import runner

RULE = "view-escape"
# The message of each kind (the rule).
MESSAGES = {
    "escaped-borrow": (
        "a function returns a borrow of an object that ends with its frame ({entity}), so a read of the result reads "
        "a dead stack frame.  Return a value that owns its storage, or take the object by a reference that "
        "CRUCIBLE_LIFETIMEBOUND marks"
    ),
    "stored-borrow": (
        "a function stores a borrow of an object that ends with its frame ({entity}), so a later read of the store "
        "reads a dead stack frame.  Store a value that owns its storage, or a borrow of an object that outlives the "
        "store"
    ),
    "static-borrow": (
        "an object of static or thread storage holds a borrow that names no object of static storage ({entity}), so "
        "a read of the object can read dead storage.  Keep the borrow in an object of automatic storage, or take a "
        "Ref of a static object through ref_of_static"
    ),
    "stale-borrow": (
        "a use reads a borrow of an object that ended ({entity}), so it reads dead storage.  Take the borrow again "
        "after the change, or change the owner after the last use of the borrow"
    ),
    "two-names": (
        "a call gives one linear object or one unique borrow to two parameters ({entity}), so the callee can write "
        "or move it under one name and then use it under the other name.  Give the object once"
    ),
    "number-borrow": (
        "a borrow class names no object by a pointer ({entity}), so no end of an owner can reach its loans.  A class "
        "that refers to a resource of the kernel by its number states refers_by_number{}, and its first type "
        "argument names the resource"
    ),
}


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each return, store, object and use of the rule, and one error for each problem of the census.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    found = census.errors(RULE)
    for item in census.of_rule(RULE):
        message = MESSAGES[item.kind].format(entity=item.entity)
        found.append(check_report.Finding("error", item.file, item.line, RULE, message, item.kind, item.entity))
    return found
