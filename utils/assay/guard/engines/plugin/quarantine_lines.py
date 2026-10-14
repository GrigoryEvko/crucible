"""quarantine-lines — a line that a change adds holds no quarantine finding, unless the change took the same finding away.

THE RULE
    Rule R6 of CLAUDE.md section XXII: each line that a change writes or
    changes holds only objects of base types.  The rule quarantine counts the
    findings of each file, kind and entity, and its key holds no line.  A
    change can then take one use away and write the same use in new code, and
    no count changes.  This rule reads the line of each finding of the rule
    quarantine (THE SECTIONS of the runner).  It also reads the lines of the
    change (THE DIFF of assay.core.git).  A hunk is one block of the diff
    with no unchanged line.
      * A finding on a line that a hunk adds is an error, unless the hunk
        removes lines or the change moved the line.  New code holds no
        finding.
      * A hunk that removes lines can keep the findings that the change took
        away from its file (THE FINDINGS THAT THE CHANGE TOOK AWAY).  A line
        that a change of a base API forces then keeps its use (CLAUDE.md
        section XXII, "Callers of a changed API").
      * A moved line can keep a finding that the change took away from a
        file of the change (THE MOVE).
      * Each finding above those numbers is an error, and its message names
        its line.

WHERE THE RULE RUNS
    The test guard_plugin runs `guard check --engine plugin`, and `loop test`
    runs that test each time.  change_base() gives the base of the change
    (THE CHANGE of assay.core.git):
      * In a work tree of a branch, the change is each commit of the branch
        and each edit that is not committed.  Its base is the merge base with
        main.
      * In a checkout of main with edits, the change is the edits.
      * In a clean checkout of main, the change is the last commit.  A merge
        commit is judged against its first parent.
    The rule does not apply to a build that made only some targets (THE
    STATUS of the runner).  A build of named targets is such a build, and the
    full test run of the build directory applies the rule.

THE FINDINGS THAT THE CHANGE TOOK AWAY
    A run has no build of the base, and the rule does not see the finding of a
    removed line.  The ledger of the rule quarantine at the base gives the
    count N of each file, kind and entity in the configuration of the build.
    U is the number of the findings of that key on the lines of the file that
    the change does not add.  The change took R = N - U findings away from the
    file, and 0 when N is less than U.  A file that the change removes took
    each of its N findings away.  A file that moved has the key of its path at
    the base (THE MOVES of assay.core.git), and a new file has N = 0.
    First, the rule gives R to the findings of the key in the hunks of the
    file that remove lines.  It gives R in the sequence of the lines.

THE MOVE
    A change can move lines from one file to a different file, for example
    when it divides a header into two new headers.  A moved line is a line
    that the change adds with the text of a line that the change removes
    (THE MOVED LINES of assay.core.git).  The first step can use only a part
    of the R of a file.  The parts that it does not use go into one pool for
    each kind and entity, for all the files of the change.  Then each finding
    on a moved line that the first step did not pay takes one finding of its
    kind and entity from that pool.  The rule uses the sequence of the files
    and the lines.  A move then keeps each finding that it moves, and the
    count of the change does not increase.

    A move to a different namespace of the tree changes an entity and not
    its finding (THE MOVE of assay.core.ledger).  In each step, the same
    entity pays first.  Then an entity that agrees pays (entities_agree() of
    assay.core.ledger).  These lines stay errors:
      * A moved line with a kind and an entity that the change did not take
        away
      * A line with a text that the change also changed.

THE KEYS
    The key of a finding is its kind and the entity of its row in the ledger
    of the rule quarantine (entity_key of assay.guard.engines.plugin.quarantine).
    The rule reads each kind of the rule quarantine other than opted-out and
    region.  Only `guard write --admit` adds to a region, and the rule history
    holds that (THE ADMITTED COUNTS of assay.guard.engines.meta.history).  A
    file of the build directory (@build/) has no history, and the rule
    quarantine holds it alone.  Each line of a new file that is not in the
    index is an added line.

NOT APPLICABLE
    The rule does not apply in these conditions:
      * The tree is not a git work tree.
      * HEAD is a root commit, and the working tree holds no change.
      * The ledger of the rule quarantine at the base does not hold the
        configuration of the build.
      * The census does not apply (THE STATUS of the runner).

Usage
    guard check --rule quarantine-lines --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build and in the length of the diff.  The sort of the m moved texts
costs O(m log m), and the sort of the f findings on added lines costs O(f log f).
"""

from collections import Counter
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import git, ruletable
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.plugin import runner
from assay.guard.engines.plugin.quarantine import OPTED_OUT, REGION, entity_key
from assay.readers import sections as quarantine_sections

RULE = "quarantine-lines"
QUARANTINE_RULE = "quarantine"
# The kinds of the rule quarantine that this rule does not judge (THE KEYS).
SKIPPED_KINDS = frozenset((OPTED_OUT, REGION))
# The file, the kind and the entity of a finding, and the kind and the entity of a pool of THE MOVE.
LineKey = tuple[str, str, str]
PoolKey = tuple[str, str]


@dataclass(frozen=True, slots=True)
class Added:
    """One finding on an added line, with its hunk and the R of its file and key (THE FINDINGS THAT THE CHANGE TOOK AWAY)."""

    item: quarantine_sections.Finding
    hunk: git.Hunk
    taken_away: int


def base_counts(root: Path, base: str, configuration: str) -> Mapping[rule_ledger.Key, int]:
    """Return the count of each file, kind and entity of the ledger of the rule quarantine at the base.

    Raises:
        NotApplicable: If the base holds no such ledger, or the ledger does not hold the configuration
    """
    shown = rule_ledger.shown_path(QUARANTINE_RULE)
    try:
        text = git.show(root, base, shown)
    except git.GitError as problem:
        raise runner.NotApplicable(f"the ledger {shown} at the base {base[:9]} cannot be read: {problem}") from None
    if text is None:
        raise runner.NotApplicable(f"the base {base[:9]} holds no {shown}")
    try:
        ledger = rule_ledger.parse(
            rows_grammar.decode(text, shown), f"{base[:9]}:{shown}", QUARANTINE_RULE, rule_registry.Policy.COUNT
        )
    except (rows_grammar.RowsError, rule_ledger.LedgerError) as problem:
        raise runner.NotApplicable(f"the ledger {shown} at the base {base[:9]} does not read: {problem}") from None
    if ledger.configuration(configuration) is None:
        raise runner.NotApplicable(
            f"the ledger {shown} at the base {base[:9]} does not hold the configuration {configuration}"
        )
    return ledger.expected(configuration)


def changes_of(root: Path) -> tuple[str, tuple[git.FileDiff, ...]]:
    """Return the base of the change and the lines of each file of the change, with each new file that is not in the index.

    Raises:
        NotApplicable: If the tree is not a git work tree, or it is a root commit with no change
    """
    try:
        base = git.change_base(root)
        if base is None:
            raise runner.NotApplicable("HEAD is a root commit, and the working tree holds no change")
        diffs = git.file_diffs(root, base, with_new=True)
    except git.GitError as problem:
        raise runner.NotApplicable(f"the change of {root} cannot be read: {problem}") from None
    return base, diffs


def grouped(
    items: Sequence[quarantine_sections.Finding], changes: Mapping[str, git.FileDiff]
) -> tuple[Counter[LineKey], dict[LineKey, list[tuple[quarantine_sections.Finding, git.Hunk]]]]:
    """Return the number of findings of each key on the lines that the change does not add, and the added findings.

    Only the files of the change count.  Complexity: O(n * h) for n findings and h hunks of a file.
    """
    unchanged: Counter[LineKey] = Counter()
    added: dict[LineKey, list[tuple[quarantine_sections.Finding, git.Hunk]]] = {}
    for item in items:
        change = changes.get(item.file)
        if change is None or item.kind in SKIPPED_KINDS or item.file.startswith(ruletable.BUILD_PREFIX):
            continue
        key = (item.file, item.kind, entity_key(item.kind, item.entity))
        hunk = next((candidate for candidate in change.hunks if candidate.adds(item.line)), None)
        if hunk is None:
            unchanged[key] += 1
        else:
            added.setdefault(key, []).append((item, hunk))
    return unchanged, added


def taken_away(
    diffs: Sequence[git.FileDiff], counts: Mapping[rule_ledger.Key, int], unchanged: Mapping[LineKey, int]
) -> tuple[dict[LineKey, int], Counter[PoolKey]]:
    """Return the R of each key of each file of the change that the change keeps, and the R of the removed files.

    THE FINDINGS THAT THE CHANGE TOOK AWAY gives R.  The R of a removed file goes straight into the pool of its kind
    and entity (THE MOVE).  Complexity: linear in the rows of the ledger and in the files of the change.
    """
    by_path: dict[str, list[rule_ledger.Key]] = {}
    for key in counts:
        if key.kind not in SKIPPED_KINDS:
            by_path.setdefault(key.path, []).append(key)
    own: dict[LineKey, int] = {}
    pool: Counter[PoolKey] = Counter()
    for change in diffs:
        if change.old_path is None or change.old_path.startswith(ruletable.BUILD_PREFIX):
            continue
        for key in by_path.get(change.old_path, ()):
            if change.new_path is None:
                pool[(key.kind, key.entity)] += counts[key]
            else:
                kept = (change.new_path, key.kind, key.entity)
                own[kept] = max(0, counts[key] - unchanged.get(kept, 0))
    return own, pool


def took(credits: Counter[PoolKey], kind: str, entity: str, agreeing: bool) -> bool:
    """Take one finding of a kind from the credits, and tell if one was there.

    With `agreeing` False the finding has the same entity.  With `agreeing` True its entity agrees, as a move to a
    different namespace of the tree gives (entities_agree() of assay.core.ledger).  Complexity: O(c) for c credits.
    """
    if not agreeing:
        if credits[(kind, entity)] <= 0:
            return False
        credits[(kind, entity)] -= 1
        return True
    if not rule_ledger.tree_qualifiers(entity):
        return False
    for (other_kind, other_entity), count in sorted(credits.items()):
        if other_kind == kind and count > 0 and rule_ledger.entities_agree(other_entity, entity):
            credits[(other_kind, other_entity)] -= 1
            return True
    return False


def unpaid(
    added: Mapping[LineKey, Sequence[tuple[quarantine_sections.Finding, git.Hunk]]],
    own: Mapping[LineKey, int],
    pool: Mapping[PoolKey, int],
    moves: git.Moves,
) -> list[Added]:
    """Return each added finding that no finding that the change took away pays for (THE RULE).

    The first step pays the findings of the hunks that remove lines from the R of their file.  The part of R that it
    does not use goes into the pool of its kind and entity.  The second step pays each finding on a moved line from
    the pool (THE MOVE).  In each step the same entity pays first, and then an entity that agrees.  The pool of the
    caller does not change.  Complexity: O(f log f + w * c) for f added findings, w findings that the same entity
    does not pay, and c credits.
    """
    entries = sorted(
        ((key, item, hunk) for key, pairs in added.items() for item, hunk in pairs),
        key=lambda entry: (entry[1].file, entry[1].line, entry[1].column),
    )
    own_credits: dict[str, Counter[PoolKey]] = {}
    for (path, kind, entity), count in own.items():
        own_credits.setdefault(path, Counter())[(kind, entity)] += count
    for agreeing in (False, True):
        waiting: list[tuple[LineKey, quarantine_sections.Finding, git.Hunk]] = []
        for key, item, hunk in entries:
            if hunk.old_count > 0 and took(own_credits.setdefault(key[0], Counter()), key[1], key[2], agreeing):
                continue
            waiting.append((key, item, hunk))
        entries = waiting
    credits = Counter(pool)
    for left in own_credits.values():
        credits.update(left)
    for agreeing in (False, True):
        waiting = []
        for key, item, hunk in entries:
            if moves.is_moved(item.file, item.line) and took(credits, key[1], key[2], agreeing):
                continue
            waiting.append((key, item, hunk))
        entries = waiting
    return [Added(item, hunk, own.get(key, 0)) for key, item, hunk in entries]


def message(found: Added, is_moved: bool, base: str) -> str:
    """Return the message of one added finding that no finding that the change took away pays for."""
    item, hunk = found.item, found.hunk
    what = f"{item.kind} {item.entity}" if item.entity else item.kind
    if is_moved:
        cause = (
            "the change moved the line from a removed line with the same text, and the change took away no more "
            "findings of this kind and entity in its files (THE MOVE)"
        )
    elif hunk.old_count > 0:
        cause = (
            f"the hunks of the file that remove lines took {found.taken_away} findings of this kind and entity away, "
            f"no line that the change removes has the text of this line, and the change adds more"
        )
    else:
        cause = (
            f"the hunk at line {hunk.new_start} removes no line, no line that the change removes has the text of "
            f"this line, and a new line holds only types and entities of the base"
        )
    return (
        f"the change against {base[:9]} adds the line {item.line}, and it holds the finding {what}: {cause} (CLAUDE.md "
        f"section XXII, R6).  Use a type or an entity of fixy or foundation, or send the operation to the owner of the "
        f"base"
    )


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each finding on an added line that no finding that the change took away pays for (THE RULE).

    Raises:
        NotApplicable: If the rule does not apply (NOT APPLICABLE)
        BuildError: If the build cannot be read
    """
    census = context.census()
    base, diffs = changes_of(context.root)
    counts = base_counts(context.root, base, context.configuration().name)
    changes = {change.new_path: change for change in diffs if change.new_path is not None}
    unchanged, added = grouped(census.of_rule(QUARANTINE_RULE), changes)
    own, pool = taken_away(diffs, counts, unchanged)
    moves = git.moves_of(diffs)
    found = census.errors(RULE)
    for entry in unpaid(added, own, pool, moves):
        item = entry.item
        found.append(
            check_report.Finding(
                "error",
                item.file,
                item.line,
                RULE,
                message(entry, moves.is_moved(item.file, item.line), base),
                "",
                f"{item.kind} {entity_key(item.kind, item.entity)}".rstrip(),
            )
        )
    return found
