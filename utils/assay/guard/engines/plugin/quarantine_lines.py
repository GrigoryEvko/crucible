"""quarantine-lines — a line that a change adds holds no quarantine finding, unless its hunk took the same finding away.

THE RULE
    Rule R6 of CLAUDE.md section XXII: each line that a change writes or
    changes holds only objects of base types.  The rule quarantine counts the
    findings of each file, kind and entity, and its key holds no line.  A
    change can then take one use away and write the same use in new code, and
    no count changes.  This rule reads the line of each finding of the rule
    quarantine (THE SECTIONS of the runner).  It also reads the hunks of the
    change (THE CHANGE of assay.core.git).  A hunk is one block of the diff
    with no unchanged line.
      * A finding on a line that a hunk adds is an error when the hunk
        removes no line.  New code holds no finding.
      * A hunk that removes lines can keep the findings that the change took
        away (THE FINDINGS THAT THE CHANGE TOOK AWAY).  A line that a change
        of a base API forces then keeps its use (CLAUDE.md section XXII,
        "Callers of a changed API").  Each finding above that number is an
        error.

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
    STATUS of the runner).  The build of a guard run is such a build, and the
    full test run of the build directory applies the rule.

THE FINDINGS THAT THE CHANGE TOOK AWAY
    A run has no build of the base, and the rule does not see the finding of a
    removed line.  The ledger of the rule quarantine at the base gives the
    count N of each file, kind and entity in the configuration of the build.
    U is the number of the findings of that key on the lines that the change
    does not add.  The change took R = N - U findings away, and 0 when N is
    less than U.  The rule gives R to the findings of the key on the added
    lines of the hunks that remove lines.  It gives them in the sequence of
    the lines.  A file that moved has the key of its path at the base (THE
    MOVES of assay.core.git), and a new file has N = 0.

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

Complexity: linear in the finding lines of the build and in the length of the diff.  The f findings of one file and key
add O(f log f).
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
# The number of lines of a hunk that holds each line of a new file.
ALL_LINES = 1 << 62


@dataclass(frozen=True, slots=True)
class KeyLines:
    """The findings of one file and key: the number on the lines that the change does not add, and those of each hunk."""

    unchanged: int
    added: Mapping[git.Hunk, tuple[quarantine_sections.Finding, ...]]


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


def changes_of(root: Path) -> tuple[str, dict[str, git.FileChange]]:
    """Return the base of the change and the hunks of each changed file, with a new file as one hunk of each line.

    Raises:
        NotApplicable: If the tree is not a git work tree, or it is a root commit with no change
    """
    try:
        base = git.change_base(root)
        if base is None:
            raise runner.NotApplicable("HEAD is a root commit, and the working tree holds no change")
        changes = git.file_changes(root, base)
        for path in git.new_files(root):
            changes[path] = git.FileChange(path, None, (git.Hunk(0, 0, 1, ALL_LINES),))
    except git.GitError as problem:
        raise runner.NotApplicable(f"the change of {root} cannot be read: {problem}") from None
    return base, changes


def grouped(
    items: Sequence[quarantine_sections.Finding], changes: Mapping[str, git.FileChange]
) -> dict[tuple[str, str, str], KeyLines]:
    """Group the findings of each changed file by key, for each key with a finding on an added line.

    Complexity: O(n * h) for n findings and h hunks of a file.
    """
    unchanged: Counter[tuple[str, str, str]] = Counter()
    added: dict[tuple[str, str, str], dict[git.Hunk, list[quarantine_sections.Finding]]] = {}
    for item in items:
        change = changes.get(item.file)
        if change is None or item.kind in SKIPPED_KINDS or item.file.startswith(ruletable.BUILD_PREFIX):
            continue
        key = (item.file, item.kind, entity_key(item.kind, item.entity))
        hunk = next((candidate for candidate in change.hunks if candidate.adds(item.line)), None)
        if hunk is None:
            unchanged[key] += 1
        else:
            added.setdefault(key, {}).setdefault(hunk, []).append(item)
    return {
        key: KeyLines(unchanged[key], {hunk: tuple(found) for hunk, found in hunks.items()})
        for key, hunks in added.items()
    }


def refused(lines: KeyLines, taken_away: int) -> list[tuple[quarantine_sections.Finding, git.Hunk, bool]]:
    """Return each added finding that no removed finding pays for, with its hunk, and a bool that tells if it removes lines.

    The findings of the hunks that remove lines take the TAKEN_AWAY findings first, in the sequence of their lines.
    """
    paid = taken_away
    found: list[tuple[quarantine_sections.Finding, git.Hunk, bool]] = []
    for hunk in sorted(lines.added, key=lambda item: item.new_start):
        removes = hunk.old_count > 0
        for item in sorted(lines.added[hunk], key=lambda finding: (finding.line, finding.column)):
            if removes and paid > 0:
                paid -= 1
            else:
                found.append((item, hunk, removes))
    return found


def message(item: quarantine_sections.Finding, hunk: git.Hunk, removes: bool, taken_away: int, base: str) -> str:
    """Return the message of one added finding that no removed finding pays for."""
    what = f"{item.kind} {item.entity}".rstrip()
    if not removes:
        cause = (
            f"the hunk at line {hunk.new_start} removes no line, and a new line holds only types and entities of the "
            f"base"
        )
    else:
        cause = (
            f"the hunks of the file that remove lines took {taken_away} findings of this kind and entity away, and "
            f"the change adds more"
        )
    return (
        f"the change against {base[:9]} adds this line, and it holds the finding {what}: {cause} (CLAUDE.md section "
        f"XXII, R6).  Use a type or an entity of fixy or foundation, or send the operation to the owner of the base"
    )


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each finding on an added line that no removed finding pays for (THE RULE).

    Raises:
        NotApplicable: If the rule does not apply (NOT APPLICABLE)
        BuildError: If the build cannot be read
    """
    census = context.census()
    base, changes = changes_of(context.root)
    counts = base_counts(context.root, base, context.configuration().name)
    found = census.errors(RULE)
    for (path, kind, entity), lines in sorted(grouped(census.of_rule(QUARANTINE_RULE), changes).items()):
        old_path = changes[path].old_path
        before = counts.get(rule_ledger.Key(old_path, kind, entity), 0) if old_path is not None else 0
        taken_away = max(0, before - lines.unchanged)
        for item, hunk, removes in refused(lines, taken_away):
            found.append(
                check_report.Finding(
                    "error",
                    item.file,
                    item.line,
                    RULE,
                    message(item, hunk, removes, taken_away, base),
                    "",
                    f"{kind} {entity}".rstrip(),
                )
            )
    return found
