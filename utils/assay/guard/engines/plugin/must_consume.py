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
    kind (the policy count), with the key of the rule quarantine
    (assay.guard.engines.plugin.quarantine, THE KEY).  A row holds no entity
    and no line.  Each count row gives a reason: in its own REASON cell, or
    in a row `reason PREFIX | REASON` of a prefix that holds its directory
    (THE REASON OF A PREFIX of assay.core.ledger).  A count row with no
    reason is the kind no-reason, which no ledger row admits
    (REFUSED_KINDS).  The rule is in report mode: a finding is counted and
    is no compile error, and a count can only fall.  `guard list --build-dir
    DIR --path DIRECTORY/` prints the lines of a directory.

THE WRITE
    A write counts the lines of the rule in each build.  A count row with
    no reason is a problem of the ledger and not of a build, so a write does
    not count it, and the check reports it until the row gets its reason.
    A stale linear-door row stops the write.

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
from assay.core import rows as rows_grammar
from assay.core import ruletable
from assay.core.paths import REPO_ROOT
from assay.guard.engines.plugin import declarations, runner
from assay.guard.engines.plugin.quarantine import directory_of

RULE = "must-consume"
# The record of a linear-door row that a unit read (THE DOORS).
DOOR_RECORD = "door"
# The kinds of a count row with no reason and of a stale linear-door row.  No ledger row admits them.
NO_REASON_KIND = "no-reason"
STALE_DOOR_KIND = "stale-door"
REFUSED_KINDS = frozenset({NO_REASON_KIND, STALE_DOOR_KIND})
# The rule table of a tree, relative to its root.
TABLE = ruletable.TABLE.relative_to(REPO_ROOT)
ACTIONS = {
    "unconsumed": "leaves its scope with no consume.  Give the value to its consumer, or end it at a named door",
    "unconsumed-on-path": "leaves its scope with no consume on some paths.  Consume it on each path",
    "double-consume": "is consumed a second time on a path.  Consume it one time",
    "assign-over-live": "is an assignment over a value that no path consumed, so the value drops.  Consume the value "
    "first",
    "discarded-temporary": "is a linear temporary that the code discards.  Give it to its consumer",
    "unanalyzed": "has a goto or a label, and the rule does not read it.  Use the statements of structured control",
}


def message(item_text: str, kind: str) -> str:
    """Return the message of one line of the rule: the line and the action of its kind."""
    return f"{item_text}: the object {ACTIONS.get(kind, 'is a finding of the rule')}"


def prefix_reason(ledger: rule_ledger.Ledger, path: str) -> str:
    """Return the reason of the longest reason row that holds a path, or "" (THE REASON OF A PREFIX)."""
    held = [item for item in ledger.reasons if item.holds(path)]
    return max(held, key=lambda item: len(item.prefix)).reason if held else ""


def reason_errors(rule: rule_registry.Rule, root: Path) -> list[check_report.Finding]:
    """Give one error for each count row of the ledger with no reason (THE LEDGER).

    A ledger that cannot be read gives no error here: the judgement of the ledger reports it.  Complexity: linear in
    the count rows times the reason rows.
    """
    try:
        ledger = rule_ledger.read(rule, root)
    except rows_grammar.RowsError, OSError:
        return []
    shown = rule_ledger.shown_path(rule.id)
    return [
        check_report.Finding(
            "error",
            shown,
            ledger.line_of(key),
            RULE,
            f"the row of {key.path} and the kind {key.kind} gives no reason.  Give the reason in its REASON cell, or "
            f"in a row `reason PREFIX | REASON` of a prefix that holds the directory",
            NO_REASON_KIND,
            f"{key.path} {key.kind}",
        )
        for key in sorted(ledger.count_keys())
        if not ledger.reason_of(key) and not prefix_reason(ledger, key.path)
    ]


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

    The path of the error of a line is the directory of its file, the key of the ledger (THE LEDGER).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build or the rule table cannot be read
    """
    census = context.census()
    lines = census.of_rule(RULE)
    found = census.errors(RULE)
    found += [
        check_report.Finding(
            "error", directory_of(item.file), item.line, RULE, message(item.text(), item.kind), item.kind
        )
        for item in lines
        if item.kind != DOOR_RECORD
    ]
    found += reason_errors(declarations.rule_of(context, RULE), context.root)
    found += stale_doors(context.root, {item.entity for item in lines if item.kind == DOOR_RECORD})
    return found


def build_counts(rule: rule_registry.Rule, context: runner.PluginContext) -> Counter[rule_ledger.Key]:
    """Return the count of each key of the lines of the rule in the build of the context (THE WRITE).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read, or it has a problem or a stale linear-door row
    """
    found = [item for item in context.findings_of(rule) if item.level == "error" and item.kind != NO_REASON_KIND]
    problems = [item for item in found if not item.kind or item.kind == STALE_DOOR_KIND]
    if problems:
        raise runner.BuildError(
            f"the build has {len(problems)} problems, such as {problems[0].path}:{problems[0].line}: "
            f"{problems[0].message}.  Correct them first"
        )
    return Counter(rule_ledger.key_of(item) for item in found)


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the ledger of the rule from the lines of the builds of the request (THE WRITE).

    Returns:
        0 when the ledger is written.  1 when a build has a problem, a count rose in the mode lower or an input cannot
        be read.  SKIPPED when the rule does not apply to a build
    """
    try:
        ledger = rule_ledger.read(rule, request.root)
    except (rows_grammar.RowsError, OSError) as problem:
        check_report.write_line(f"guard write: the ledger of {rule.id} is not written: {problem}", is_error=True)
        return 1
    per_build = runner.counts_of_builds(rule.id, request, lambda context: build_counts(rule, context))
    if isinstance(per_build, int):
        return per_build
    try:
        written = rule_ledger.write_counts(
            rule, ledger, rule_ledger.largest_counts(per_build), request.mode, request.reason, request.is_partial
        )
    except rule_ledger.LedgerError as problem:
        check_report.write_line(f"guard write: the ledger of {rule.id} is not written: {problem}", is_error=True)
        return 1
    rule_ledger.store(written, request.root)
    check_report.write_line(f"guard write: {rule_ledger.shown_path(rule.id)} holds {len(written.base)} rows")
    return 0
