"""must-consume — each path of a body uses a linear value exactly one time.

Rule
    A class that carries the annotation foundation::linear::linear of
    include/foundation/Linear.h is linear, and so is a class that can move and
    holds a linear part by value.  The quarantine plugin of
    utils/tools/quarantine/ walks the paths of each body, and it gives a line
    for each linear object that the paths do not use exactly one time (the
    family linear, linear.cpp).  The objects are the linear locals and the
    linear parameters by value or by rvalue reference.  The kinds:
      unconsumed            the object leaves its scope at an exit, and no
                            path to that exit consumes it
      unconsumed-on-path    the object leaves its scope at an exit, and only
                            some paths to that exit consume it
      double-consume        a consume of an object that a path consumed
      assign-over-live      an assignment over a value that a path did not
                            consume, which drops that value
      self-move             a move assignment of an object from itself: the
                            target and the source name one object.  It reads
                            the operator= of each class, as a session handle
                            over a Resource that is no linear class is no
                            linear class.  No ledger row admits it
                            (REFUSED_KINDS)
      discarded-temporary   a linear temporary that the code discards
      unanalyzed            a function with a goto or a label
      borrow-move-out       a function moves a linear value out of a
                            parameter that its caller lends, and a path
                            leaves the parameter spent (linear.cpp, the lent
                            parameters)
      closure-move-out      a call of a closure whose call operator moves a
                            linear capture out, outside the once door
                            call_once of include/foundation/Linear.h, so a
                            second call gives a second value (linear.cpp, the
                            closures that move a capture)
      capture-unconsumed    a closure that the once door owns, whose call
                            operator leaves a linear capture by value live on
                            a path, so the end of the closure drops it.  The
                            line is at the lambda (linear.cpp, the closures
                            that a call owns)
    A linear sum gives the kinds of the sums in place of unconsumed,
    unconsumed-on-path and discarded-temporary.
    The plugin reads a body of a base file in the sentinel unit of the file,
    and an instance of a template in each unit that makes it.  The entity of a
    line is FUNCTION: OBJECT (TYPE): FUNCTION has no template argument, and
    TYPE is the type of the object with its template arguments and with each
    fresh brand written as the word brand.  So an instance of one template
    that ends a different type at one line gives its own line, and the ledger
    counts it.

Sums
    A linear sum, an Option, a Result or a OneOf of a linear alternative,
    gives its alternative out only through a consumer (linear.cpp, the sums).
    The plugin reads the empty state of an Option through the annotations of
    include/foundation/Linear.h, so an Option that is empty on each path to
    its end needs no consume.  The four kinds:
      option-unconsumed     a linear sum that can end with its alternative,
                            on each path or on some paths
      option-discarded      a temporary of a linear sum that the code
                            discards
      option-known-full     a door that needs an empty sum, such as fill or
                            expect_none, on a sum that each path shows full
      option-known-empty    a door that needs a full sum, such as expect, on a
                            sum that each path shows empty
    The plugin makes each line of the four kinds a compile error in each build
    and each mode, so no object holds one.  No ledger row admits such a line,
    and a write refuses a build that holds one.

Fields, accessors, captures
    An object of a class that is linear by its parts gets one slot for each
    linear field, so a move of each field consumes the object.  The move of
    the reference that an accessor such as operator* gives consumes its
    object.  A capture by reference hides each later consume, so the object
    gives no line, and the rule linear-capture reports the capture.

Doors
    A door ends each linear value of its frame (P10 of
    misc/12_10_2026_linear.md).  A function that carries the annotation
    drop_door or detach_door is a door, and so is each function that a
    linear-door row of utils/rule-table.txt names.  These rows are the roster
    of the doors.  The plugin gives the record door for each row whose
    function a unit read.  A row with no record in the build is stale: the
    kind stale-door, which no ledger row admits (REFUSED_KINDS).  A tree with
    no utils/rule-table.txt, such as the tree of a plant, has no stale row.

Ledger
    utils/ledgers/must-consume.txt counts the findings of each directory and
    kind, and each count row gives a reason (the ledger of
    assay.guard.engines.plugin.linear_family).

Write
    A write counts the lines of the rule in each build (the write of
    assay.guard.engines.plugin.linear_family).  A stale linear-door row and a
    line of a kind of the sums stop the write.

Usage
    guard check --rule must-consume --build-dir DIR [--warnings-dir DIR]
    guard write --rule must-consume --build-dir DIR [--raise] [--reason TEXT]

Complexity: linear in the lines of the rule, and linear in the rows of the ledger times its reason rows.
"""

from collections import Counter
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import ruletable
from assay.core.paths import REPO_ROOT
from assay.guard.engines.plugin import linear_family, runner

RULE = "must-consume"
# The record of a linear-door row that a unit read (the doors).
DOOR_RECORD = "door"
# The kind of a stale linear-door row.  No ledger row admits it, nor a count row with no reason.
STALE_DOOR_KIND = "stale-door"
# The kind of a move assignment of an object from itself.  No ledger row admits it, and the plugin gives it as an
# error in each mode.  The guards of the session handles stay as the run-time backstop of an alias through two names.
SELF_MOVE_KIND = "self-move"
REFUSED_KINDS = linear_family.REFUSED_KINDS | {STALE_DOOR_KIND, SELF_MOVE_KIND}
# The rule table of a tree, relative to its root.
TABLE = ruletable.TABLE.relative_to(REPO_ROOT)
ACTIONS = {
    "unconsumed": "the object leaves its scope with no consume.  Give the value to its consumer, or end it at a "
    "named door",
    "unconsumed-on-path": "the object leaves its scope with no consume on some paths.  Consume it on each path",
    "double-consume": "the object is consumed a second time on a path.  Consume it one time",
    "assign-over-live": "the object is an assignment over a value that no path consumed, so the value drops.  Consume "
    "the value first",
    SELF_MOVE_KIND: "the object is moved into itself: the target and the source of the move assignment are one object.  "
    "Remove the assignment, or move the value into a different object",
    "discarded-temporary": "the object is a linear temporary that the code discards.  Give it to its consumer",
    "unanalyzed": "the object has a goto or a label, and the rule does not read it.  Use the statements of structured "
    "control",
    "option-unconsumed": "the linear sum can end with its payload.  Consume it on each path, test an Option empty "
    "before its end, or end an empty Option with expect_none(reason)",
    "option-discarded": "the object is a temporary linear sum that the code discards.  Give it to a consumer, or end an "
    "empty Option with expect_none(reason)",
    "option-known-full": "a door that needs an empty sum runs on a sum that each path shows full, so its check ends the "
    "process.  Consume the payload first, or use replace",
    "option-known-empty": "a door that needs a full sum runs on a sum that each path shows empty, so its check ends the "
    "process.  Fill the sum first, or end it with expect_none(reason)",
    "borrow-move-out": "the function moves the value out of a parameter that its caller lends, and a path leaves it "
    "spent.  Take the value with foundation::linear::exchange, give the parameter a value again on each path, or take "
    "it by value",
    "closure-move-out": "a call of a closure that moves a linear capture out can run again and give a second value.  "
    "Give the closure to foundation::linear::call_once, which owns it and calls it one time",
    "capture-unconsumed": "the once door owns the closure, and its call operator leaves a linear capture by value live "
    "on a path, so the end of the closure drops it.  Consume each such capture on each path of the body, or capture "
    "the value by reference and consume it in the caller",
}
# The kinds of the sums.  Each line is a compile error, and no ledger row admits one.
SUM_KINDS = ("option-unconsumed", "option-discarded", "option-known-full", "option-known-empty")


def stale_doors(root: Path, records: set[str]) -> list[check_report.Finding]:
    """Give an error for each linear-door row with no record in the build (the doors).

    Raises:
        BuildError: If the rule table cannot be read
    """
    path = root / TABLE
    if not path.is_file():
        return []
    try:
        table = ruletable.load(path)
    except (ruletable.TableError, OSError) as problem:
        raise runner.BuildError(f"the rule table cannot be read: {problem}") from None
    return [
        check_report.Finding(
            "error",
            str(TABLE),
            row.line,
            RULE,
            f"the linear-door row of {row.function} names a function that no unit of the build read.  The row is "
            f"stale: remove it, or correct the name of the door",
            STALE_DOOR_KIND,
            row.function,
        )
        for row in table.linear_doors
        if row.function not in records
    ]


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give an error for each line of the rule, each count row with no reason, each stale door row and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build or the rule table cannot be read
    """
    found = linear_family.line_findings(context, RULE, ACTIONS, records=(DOOR_RECORD,))
    doors = {item.entity for item in context.census().of_rule(RULE) if item.kind == DOOR_RECORD}
    return found + stale_doors(context.root, doors)


def build_counts(rule: rule_registry.Rule, context: runner.PluginContext) -> Counter[rule_ledger.Key]:
    """Return the count of each key of the lines of the rule in the build of the context (the write).

    A line of a kind of SUM_KINDS is a problem of the build, so the write refuses the build (the sums).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read, or it has a problem or a stale linear-door row
    """
    return linear_family.build_counts(rule, context, problem_kinds=(STALE_DOOR_KIND, *SUM_KINDS))


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the ledger of the rule from the lines of the builds of the request (the write).

    Returns:
        0 when the ledger is written.  1 when a build has a problem, a count rose in the mode lower or an input cannot
        be read.  SKIPPED when the rule does not apply to a build
    """
    return linear_family.write(rule, request, lambda context: build_counts(rule, context))
