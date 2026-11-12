"""must-consume — each path of a body uses a linear value exactly one time.

THE RULE
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
      discarded-temporary   a linear temporary that the code discards
      unanalyzed            a function with a goto or a label
    The plugin reads a body of a base file in the sentinel unit of the file,
    and an instance of a template in each unit that makes it.  The entity of a
    line is FUNCTION: OBJECT, with no template argument, so each instance of
    one template gives the same line.

THE FIELDS, THE ACCESSORS, THE CAPTURES
    An object of a class that is linear by its parts gets one slot for each
    linear field, so a move of each field consumes the object.  The move of
    the reference that an accessor such as operator* gives consumes its
    object.  A capture by reference hides each later consume, so the object
    gives no line, and the rule linear-capture reports the capture.

THE DOORS
    A door ends each linear value of its frame (P10 of
    misc/12_10_2026_linear.md).  A function that carries the annotation
    drop_door or detach_door is a door, and so is each function that a
    linear-door row of utils/rule-table.txt names.  These rows are the roster
    of the doors.  The plugin gives the record door for each row whose
    function a unit read.  A row with no record in the build is stale: the
    kind stale-door, which no ledger row admits (REFUSED_KINDS).  A tree with
    no utils/rule-table.txt, such as the tree of a plant, has no stale row.

THE LEDGER
    utils/ledgers/must-consume.txt counts the findings of each directory and
    kind, and each count row gives a reason (THE LEDGER of
    assay.guard.engines.plugin.linear_family).

THE WRITE
    A write counts the lines of the rule in each build (THE WRITE of
    assay.guard.engines.plugin.linear_family).  A stale linear-door row stops
    the write.

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
# The record of a linear-door row that a unit read (THE DOORS).
DOOR_RECORD = "door"
# The kind of a stale linear-door row.  No ledger row admits it, nor a count row with no reason.
STALE_DOOR_KIND = "stale-door"
REFUSED_KINDS = linear_family.REFUSED_KINDS | {STALE_DOOR_KIND}
# The rule table of a tree, relative to its root.
TABLE = ruletable.TABLE.relative_to(REPO_ROOT)
ACTIONS = {
    "unconsumed": "the object leaves its scope with no consume.  Give the value to its consumer, or end it at a "
    "named door",
    "unconsumed-on-path": "the object leaves its scope with no consume on some paths.  Consume it on each path",
    "double-consume": "the object is consumed a second time on a path.  Consume it one time",
    "assign-over-live": "the object is an assignment over a value that no path consumed, so the value drops.  Consume "
    "the value first",
    "discarded-temporary": "the object is a linear temporary that the code discards.  Give it to its consumer",
    "unanalyzed": "the object has a goto or a label, and the rule does not read it.  Use the statements of structured "
    "control",
}


def stale_doors(root: Path, records: set[str]) -> list[check_report.Finding]:
    """Give an error for each linear-door row with no record in the build (THE DOORS).

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
    """Return the count of each key of the lines of the rule in the build of the context (THE WRITE).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read, or it has a problem or a stale linear-door row
    """
    return linear_family.build_counts(rule, context, problem_kinds=(STALE_DOOR_KIND,))


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the ledger of the rule from the lines of the builds of the request (THE WRITE).

    Returns:
        0 when the ledger is written.  1 when a build has a problem, a count rose in the mode lower or an input cannot
        be read.  SKIPPED when the rule does not apply to a build
    """
    return linear_family.write(rule, request, lambda context: build_counts(rule, context))
