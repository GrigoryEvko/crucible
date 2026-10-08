"""history — a threshold or a ledger that grew stays visible, and a threshold rises only with a measurement.

CLAUDE.md section XV ("The budgets of the tree") gives the rule: change a
threshold only with a measurement, and give the numbers in the commit.  This
rule reads the git history and holds the rule after the commit lands.

WHAT THE RULE READS
    * Each commit that is not a merge and that changed the table of the
      thresholds or a ledger: its first parent, its subject, its body, and
      each such file at the commit and at the parent.  A merge brings a change
      that a commit of the other side made, and the rule reads that commit.
    * THE TABLE OF THE THRESHOLDS: utils/registry.txt, where a version holds
      it, else the old table utils/scripts/budgets.txt.  So the history of a
      threshold goes on across the move of the thresholds into the registry.
      A measured rule of the registry and a row of the old table with the same
      name are one history.
    * The ledgers utils/ledgers/*.txt, and the earlier files of EARLIER_FILES.
      A ledger that moved to the rows of assay.core.ledger counts the
      versions of its earlier files, because git does not see a move to a new
      format as a rename.  A state before the move counts the rows of each
      earlier file of the ledger, added.
    * Each path that a commit of the quarantine ledger or of its earlier file
      changed, and each file that git reports as moved in it.
    * The working-tree copy of each file, which can hold an edit that no
      commit holds yet.
    * The ledger of this rule, utils/ledgers/history.txt (policy admit): the
      rises of the past whose commit gives no measurement.  A row
      `utils/registry.txt | COLUMN | admit | COMMIT RULE | REASON` admits the
      rise of the warn or error threshold of RULE, and a row
      `utils/ledgers/quarantine.txt | raise | admit | COMMIT | REASON` admits
      a raise of the quarantine ledger.  COMMIT is the hash in 9 characters.

THE FINDINGS
    * A threshold (the warn or the error field of a measured rule) above its
      lowest committed value gives a warning on each run.  The warning names
      each commit that raised it after the newest commit at the lowest value.
      A value with another unit starts a new history of the rule.  The unit
      of a version is its first word, so "G instructions" of the old table and
      "G" of the registry are one unit.
    * A ledger whose row count is above its lowest committed count gives a
      warning on each run (assay.core.ledger.count_rows).  A ledger of a rule
      with the policy baseline holds the sums that --write writes again, and
      the rule does not count its rows.  The quarantine ledger has counts, so
      the rule reads it as the next item says.
    * A commit that raised a count of the quarantine ledger, and that changes
      neither the plugin of utils/tools/quarantine/ nor the rule table
      utils/rule-table.txt, gives an error, of the kind raise.  The rule table
      was utils/scripts/layer-rules.txt before it moved.  A raised count of
      the kind opted-out or region gives no error, because only `guard write
      --rule quarantine --admit` writes it.  Each raised count that stays above
      its count before the commit gives a warning on each run.
      read_quarantine_version() reads each version: the rows of
      assay.core.ledger, and the formats 1 to 4 of the earlier file
      utils/scripts/quarantine-ledger.txt.  The rows hold the counts of format
      4, so the commit of the move compares row by row, and the rules of the
      formats, the configurations and the moves stay.  Each version reads with
      the kind names of this time (current_key()).  The change of the names to
      format 2 of the report then gives no finding.  The first count of the
      kind region gives no finding too.
    * A commit that raised a threshold, and whose body holds no measurement of
      the rule, gives an error, of the kind of its field.
    A rise in the working tree gives a warning only, and the error comes when
    the rise lands without a measurement.

THE MEASUREMENT
    A measurement of a rule is a number with the unit of the rule: "s" for
    seconds, "G" for G instructions, "GB", "MB", "KB", "%", "operations",
    "elements", "links", or "x" for a multiple.  The number is none of the
    values that the rule had in a field at a commit or in the working tree.
    It counts in a sentence that holds the name of the rule as a word.  It
    also counts in each other sentence when it is above the old threshold
    and at most the new one: the measured value that the old threshold
    refused.  A squash landing makes a second commit with the same change.  A
    rise is measured when one commit of the history that made the same step
    (the rule, the field, the old and the new value) gives a measurement.

WHY THE RULE CANNOT BE MET BY ACCIDENT
    A number with no unit, a count of files, a date or a commit hash is not a
    number with the unit of the rule.  A body that only tells the change
    ("raise compile-cpu from 10 s to 20 s") fails, because the rule does not
    count a value that the rule ever had.  A body passes only with a quantity
    in the unit of the rule that no threshold of the rule ever was.

NOT APPLICABLE
    The rule does not apply, with the reason, when the tree is not a git work
    tree, or when the history is shallow: a shallow history does not hold the
    lowest values.

Complexity: O(c * r) for c commits and r measured rules, and linear in the size of the versions of the files.
"""

import math
import shutil
from dataclasses import dataclass, field
from pathlib import Path
from assay.core import git
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.meta.runner import MetaContext, NotApplicable

RULE = "history"
REGISTRY = rule_registry.REGISTRY_PATH
# The table of the thresholds before the registry held them.
OLD_TABLE = "utils/scripts/budgets.txt"
TABLES = (REGISTRY, OLD_TABLE)
LEDGER_DIRECTORY = rule_ledger.LEDGER_DIRECTORY
QUARANTINE_RULE = "quarantine"
QUARANTINE_LEDGER = rule_ledger.shown_path(QUARANTINE_RULE)
# The file of the quarantine ledger before it moved to the rows of assay.core.ledger.
EARLIER_QUARANTINE_LEDGER = "utils/scripts/quarantine-ledger.txt"
QUARANTINE_PATHS = (QUARANTINE_LEDGER, EARLIER_QUARANTINE_LEDGER)
# A commit that changes one of these paths can change what the quarantine plugin reports.  The rule table was
# utils/scripts/layer-rules.txt before it moved.
RULE_TABLE = "utils/rule-table.txt"
QUARANTINE_MACHINERY = ("utils/tools/quarantine/", RULE_TABLE, "utils/scripts/layer-rules.txt")
# The earlier files of each rule whose ledger moved to the rows of assay.core.ledger.  A state before the move holds
# the earlier files, and its row count is the sum of their row counts.
EARLIER_FILES = {
    "artifact-symbols": ("utils/ledgers/libstdcxx-symbols.txt", "utils/ledgers/no-throw-no-rtti.txt",
                         "utils/scripts/libstdcxx-symbols-ledger.txt", "utils/scripts/no-throw-no-rtti-allowlist.txt"),
    "banned-calls": ("utils/scripts/no-reinterpret-allowlist.txt", "utils/scripts/no-reserve-allowlist.txt",
                     "utils/scripts/no-spawn-process-allowlist.txt"),
    "brand-drain": ("utils/scripts/brand-drain.txt",),
    "compile-cpu": ("utils/scripts/compile-cpu-ledger.txt",),
    "compile-instructions": ("utils/scripts/compile-instructions-ledger.txt",),
    "compile-memory": ("utils/scripts/compile-memory-ledger.txt",),
    "contract-form": ("utils/scripts/contract-form-allowlist.txt",),
    "ctx-init-door": ("utils/scripts/ctx-init-door-allowlist.txt", "utils/scripts/ctx-bg-door-allowlist.txt"),
    "ctx-testing-boundary": ("utils/scripts/ctx-testing-boundary-allowlist.txt",),
    "detail-namespace": ("utils/scripts/detail-namespace-allowlist.txt",),
    "escape-doors": ("utils/scripts/escape-doors.txt",),
    "fixture-cpu": ("utils/scripts/fixture-cpu-ledger.txt",),
    "fixture-instructions": ("utils/scripts/fixture-instructions-ledger.txt",),
    "fixture-own-reason": ("utils/scripts/fixture-own-reason-ledger.txt",),
    "fullness-guard": ("utils/scripts/fullness-guard-allowlist.txt",),
    "function-size": ("utils/scripts/function-size-ledger.txt",),
    "gate-mutants": ("utils/tools/mutation/invalid-exemptions.txt",),
    "header-alone": ("utils/scripts/header-alone-ledger.txt",),
    "header-checks": ("utils/scripts/header-checks-ledger.txt",),
    "heavy-include": ("utils/scripts/heavy-includes-ledger.txt",),
    "history": ("utils/scripts/budget-history-ledger.txt",),
    "link-memory": ("utils/scripts/link-memory-ledger.txt",),
    "link-time": ("utils/scripts/link-time-ledger.txt",),
    "mint-floor": ("utils/scripts/mint-hs14-floor-allowlist.txt",),
    "mint-pattern": ("utils/scripts/mint-pattern-allowlist.txt",),
    "no-raw-array-member": ("utils/scripts/raw-array-member-allowlist.txt",),
    "no-unchecked-access": ("utils/scripts/unchecked-access-allowlist.txt",),
    "object-text": ("utils/scripts/object-text-ledger.txt",),
    "padded-list": ("utils/scripts/padded-lists-ledger.txt",),
    "process-wide-state": ("utils/scripts/process-wide-roster.txt",),
    "proof-routes": ("utils/scripts/proof-routes-allowlist.txt",),
    QUARANTINE_RULE: (EARLIER_QUARANTINE_LEDGER,),
    "start-lifetime": ("utils/scripts/start-lifetime-allowlist.txt",),
    "syscall-capability": ("utils/scripts/syscall-capability-allowlist.txt",),
    "test-memory": ("utils/scripts/test-memory-ledger.txt",),
    "test-time": ("utils/scripts/test-time-ledger.txt",),
    "trait-guard": ("utils/scripts/open-gate-reads.txt",),
    "use-after-move": ("utils/scripts/use-after-move-allowlist.txt",),
}
EARLIER_PATHS = {rule_ledger.shown_path(rule_id): paths for rule_id, paths in EARLIER_FILES.items()}
RAISE_KIND = "raise"
COLUMNS = ("warn", "error")
WORKING_TREE = "the working tree"
TOTAL = "(the total of the kind)"
SHOWN_RISES = 8
HEX_DIGITS = frozenset("0123456789abcdef")


@dataclass(frozen=True, slots=True)
class Row:
    """The two thresholds and the unit word of one rule in one version of the table."""

    warn: float
    error: float
    unit: str

    def value(self, column: str) -> float:
        """Return the threshold of one field."""
        return self.warn if column == "warn" else self.error


@dataclass(frozen=True, slots=True)
class Rise:
    """One commit, or the working tree, that raised a threshold or the row count of a ledger."""

    where: str
    subject: str
    old: float
    new: float
    sha: str = ""

    def described(self) -> str:
        """Return the rise as words for a message."""
        return f"{self.where} ({self.subject}) from {number_text(self.old)} to {number_text(self.new)}"


@dataclass(slots=True)
class History:
    """The states of the watched files, and each path that each commit of the quarantine ledger changed."""

    states: git.PathHistory
    ledgers: list[str] = field(default_factory=list)
    changes: dict[str, tuple[frozenset[str], dict[str, str]]] = field(default_factory=dict)

    def text(self, place: str, sha: str, paths: tuple[str, ...]) -> str | None:
        """Return the first of the paths that a state holds, at a commit ("at" or "before"), at HEAD or in the working tree."""
        for path in paths:
            if place == "at":
                data = self.states.at.get((sha, path))
            elif place == "before":
                data = self.states.before.get((sha, path))
            elif place == "head":
                data = self.states.head.get(path)
            else:
                data = self.states.working.get(path)
            if data is not None:
                return data.decode("utf-8", "replace")
        return None


def number_text(value: float) -> str:
    """Return a value with no exponent, and with no '.0' for a whole number."""
    return check_report.threshold_text(float(value))


def unit_word(unit: str) -> str:
    """Return the first word of a unit, which names the unit in a measurement."""
    words = unit.replace("\t", " ").split(" ")
    return next((word for word in words if word), unit)


def registry_rows(text: str | None) -> dict[str, Row]:
    """Read the measured rules of one version of the registry.  A line that does not read is skipped."""
    rows: dict[str, Row] = {}
    for line in (text or "").splitlines():
        try:
            parsed = rows_grammar.parse(f"{line}\n", REGISTRY)
        except rows_grammar.RowsError:
            continue
        if not parsed or parsed[0].words[0] != "rule" or len(parsed[0].words) < 2:
            continue
        values = dict(word.partition("=")[::2] for word in parsed[0].words[2:])
        try:
            warn, error = float(values["warn"]), float(values["error"])
        except (KeyError, ValueError):
            continue
        if math.isfinite(warn) and math.isfinite(error) and values.get("unit"):
            rows[parsed[0].words[1]] = Row(warn, error, unit_word(values["unit"]))
    return rows


def old_table_rows(text: str | None) -> dict[str, Row]:
    """Read the rows of one version of the old table: check | warn | error | unit | meaning.  A bad line is skipped."""
    rows: dict[str, Row] = {}
    for raw in (text or "").splitlines():
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        cells = [cell.strip() for cell in stripped.split("|")]
        if len(cells) != 5:
            continue
        try:
            warn, error = float(cells[1]), float(cells[2])
        except ValueError:
            continue
        if math.isfinite(warn) and math.isfinite(error):
            rows[cells[0]] = Row(warn, error, unit_word(cells[3]))
    return rows


def table_rows(history: History, place: str, sha: str = "") -> dict[str, Row] | None:
    """Return the thresholds of one state: of the registry when the state holds it, else of the old table, else None."""
    registry_text = history.text(place, sha, (REGISTRY,))
    if registry_text is not None:
        return registry_rows(registry_text)
    old_text = history.text(place, sha, (OLD_TABLE,))
    return old_table_rows(old_text) if old_text is not None else None


def is_word_character(character: str) -> bool:
    """Whether a character is a word character of a regular expression: a letter, a digit or '_'."""
    return character.isalnum() or character == "_"


def sentences(body: str) -> list[str]:
    """Divide a body at the white space after '.', '!' or '?', and at each other run of line breaks.

    The white space after the end of a sentence includes the line breaks and
    the spaces after them.  Complexity: linear in the length of the body.
    """
    parts: list[str] = []
    start = 0
    index = 0
    while index < len(body):
        ends_sentence = index > 0 and body[index - 1] in ".!?" and body[index].isspace()
        if not ends_sentence and body[index] != "\n":
            index += 1
            continue
        end = index
        while index < len(body) and (body[index].isspace() if ends_sentence else body[index] == "\n"):
            index += 1
        parts.append(body[start:end])
        start = index
    parts.append(body[start:])
    return parts


def names_rule(sentence: str, rule_id: str) -> bool:
    """Whether a sentence holds the name of a rule as a word: no word character and no '-' on either side."""
    start = sentence.find(rule_id)
    while start >= 0:
        before = sentence[start - 1] if start > 0 else ""
        after = sentence[start + len(rule_id):start + len(rule_id) + 1]
        if not (before and (is_word_character(before) or before == "-")) and not (
                after and (is_word_character(after) or after == "-")):
            return True
        start = sentence.find(rule_id, start + 1)
    return False


def quantities(sentence: str, word: str) -> list[float]:
    """Return each number with the unit word in a sentence.

    A number is digits with groups after '.' or ',', not after a word
    character or a '.'.  One white space character can come between the
    number and the unit.  A unit word that ends with a letter or a digit has
    no word character and no '%' after it.  Complexity: linear in the length
    of the sentence.
    """
    found: list[float] = []
    needs_tail = word[-1:].isalnum()
    index = 0
    while index < len(sentence):
        if not sentence[index].isdecimal() or (index > 0 and (is_word_character(sentence[index - 1])
                                                              or sentence[index - 1] == ".")):
            index += 1
            continue
        end = index
        while end < len(sentence) and sentence[end].isdecimal():
            end += 1
        while end + 1 < len(sentence) and sentence[end] in ".," and sentence[end + 1].isdecimal():
            end += 1
            while end < len(sentence) and sentence[end].isdecimal():
                end += 1
        unit_start = end + 1 if end < len(sentence) and sentence[end].isspace() else end
        after = unit_start + len(word)
        tail = sentence[after:after + 1]
        if (sentence.startswith(word, unit_start)
                and not (needs_tail and tail and (is_word_character(tail) or tail == "%"))):
            try:
                found.append(float(sentence[index:end].replace(",", "")))
            except ValueError:
                pass
            index = after
            continue
        index = end
    return found


def has_measurement(body: str, rule_id: str, unit: str, known: set[float], old: float, new: float) -> bool:
    """Whether a body holds a measurement of a rule for a rise from `old` to `new` (THE MEASUREMENT).

    Complexity: linear in the length of the body.
    """
    word = unit_word(unit)
    for sentence in sentences(body):
        has_name = names_rule(sentence, rule_id)
        for value in quantities(sentence, word):
            if any(math.isclose(value, item, rel_tol=1e-9, abs_tol=1e-12) for item in known):
                continue
            if has_name or old < value <= new:
                return True
    return False


def read_history(root: Path) -> History:
    """Read the commits of the watched files, each file at each such commit, its parent, HEAD and the working tree.

    Raises:
        NotApplicable: If the tree is not the top of a git work tree, or its history is shallow
    """
    try:
        if git.toplevel(root).resolve() != root.resolve():
            raise NotApplicable(f"{root} is not the top of a git work tree")
        if git.is_shallow(root):
            raise NotApplicable("the history is shallow, and a shallow history does not hold the lowest values")
        ledgers = sorted(git.tracked_files(root, (f"{LEDGER_DIRECTORY}/*.txt",), with_new=True))
        earlier = [path for ledger in ledgers for path in EARLIER_PATHS.get(ledger, ())]
        watched = list(dict.fromkeys([*TABLES, *ledgers, *earlier]))
        groups = [EARLIER_PATHS[ledger] for ledger in ledgers if len(EARLIER_PATHS.get(ledger, ())) > 1]
        states = git.path_history(root, watched, groups)
        changes = (git.changes(root, [*QUARANTINE_PATHS, *QUARANTINE_MACHINERY]) if QUARANTINE_LEDGER in ledgers
                   else {})
    except git.GitError as problem:
        raise NotApplicable(str(problem)) from None
    return History(states, ledgers, changes)


def rises_after_lowest(states: list[float | None], rises: list[tuple[int, Rise]], current: float | None,
                       working_rise: Rise | None) -> tuple[float, list[Rise]] | None:
    """Return the lowest committed value and the rises after the newest state at it, when the current value is above it.

    `states` holds the value at each commit, newest first, and None where no value applies.
    """
    committed = [value for value in states if value is not None]
    if current is None or not committed:
        return None
    lowest = min(committed)
    if current <= lowest:
        return None
    newest_lowest = next(index for index, value in enumerate(states) if value is not None and value == lowest)
    named = [rise for index, rise in rises if index < newest_lowest]
    if working_rise is not None:
        named.append(working_rise)
    return lowest, named


def threshold_findings(history: History, registry: rule_registry.Registry) -> list[check_report.Finding]:
    """Compare each threshold with its history, and each rise with the body of its commit."""
    findings: list[check_report.Finding] = []
    commits = history.states.commits
    current = table_rows(history, "working") or {}
    # A commit that changed a table has a state, also when the commit or its parent holds no table.
    tables = [(table_rows(history, "at", commit.sha) or {}) if any(path in commit.paths for path in TABLES)
              else None for commit in commits]
    parents = [(table_rows(history, "before", commit.sha) or {}) if any(path in commit.paths for path in TABLES)
               else None for commit in commits]
    last_committed = table_rows(history, "head") or {}
    bodies = {commit.sha: commit.body for commit in commits}
    for rule_id, row in sorted(current.items()):
        rule = registry.rules.get(rule_id)
        line = rule.line if rule is not None else 0
        display = check_report.unit_text(row.unit)
        known = {value for table in [*tables, *parents, current] if table
                 for other in [table.get(rule_id)] if other is not None for value in (other.warn, other.error)}
        for column in COLUMNS:
            states: list[float | None] = []
            rises: list[tuple[int, Rise]] = []
            for index, (commit, table, parent) in enumerate(zip(commits, tables, parents)):
                if table is None or parent is None:
                    states.append(None)
                    continue
                new_row, old_row = table.get(rule_id), parent.get(rule_id)
                states.append(new_row.value(column) if new_row is not None and new_row.unit == row.unit else None)
                if (new_row is None or old_row is None or new_row.unit != row.unit or old_row.unit != row.unit
                        or new_row.value(column) <= old_row.value(column)):
                    continue
                rises.append((index, Rise(commit.short, commit.subject, old_row.value(column), new_row.value(column),
                                          commit.sha)))
            measured = {(rise.old, rise.new) for _, rise in rises
                        if has_measurement(bodies[rise.sha], rule_id, row.unit, known, rise.old, rise.new)}
            for _, rise in rises:
                if (rise.old, rise.new) in measured:
                    continue
                findings.append(check_report.Finding(
                    "error", REGISTRY, line, RULE,
                    f"the commit {rise.described()} raised the {column} threshold of {rule_id}, and no commit of this "
                    f"step names a measurement: a number in {display} that no threshold of the rule was, in a sentence "
                    f"with the rule name or above the old threshold and at most the new one.  Measure the quantity, "
                    f"and add a row to {rule_ledger.shown_path(RULE)} with the measurement as its reason",
                    column, f"{rise.where} {rule_id}"))
            old_row = last_committed.get(rule_id)
            working_rise = (Rise(WORKING_TREE, "not committed", old_row.value(column), row.value(column))
                            if old_row is not None and old_row.unit == row.unit
                            and row.value(column) > old_row.value(column) else None)
            above = rises_after_lowest(states, rises, row.value(column), working_rise)
            if above is not None:
                lowest, named = above
                raised = "; ".join(rise.described() for rise in named) if named else "by no single commit"
                findings.append(check_report.Finding(
                    "warning", REGISTRY, line, RULE,
                    f"the {column} threshold of {rule_id} is {number_text(row.value(column))} {display}, above its "
                    f"lowest committed value {number_text(lowest)}.  Raised: {raised}.  Lower it again when a "
                    f"measurement permits", "above-lowest", rule_id))
    return findings


def ledger_rule(path: str) -> str:
    """Return the rule of a ledger file utils/ledgers/ID.txt."""
    return Path(path).name.removesuffix(".txt")


def ledger_rows(history: History, place: str, sha: str, path: str) -> int | None:
    """Return the row count of a ledger in one state: of its file, else the sum of its earlier files, else None."""
    own = history.text(place, sha, (path,))
    if own is not None:
        return rule_ledger.count_rows(own)
    counts = [rule_ledger.count_rows(history.text(place, sha, (item,))) for item in EARLIER_PATHS.get(path, ())]
    held = [count for count in counts if count is not None]
    return sum(held) if held else None


def ledger_findings(history: History, registry: rule_registry.Registry) -> list[check_report.Finding]:
    """Compare the row count of each ledger with its lowest committed count."""
    findings: list[check_report.Finding] = []
    for path in history.ledgers:
        rule = registry.rules.get(ledger_rule(path))
        if path == QUARANTINE_LEDGER or (rule is not None and rule.policy == rule_registry.Policy.BASELINE):
            continue
        paths = (path, *EARLIER_PATHS.get(path, ()))
        states: list[float | None] = []
        rises: list[tuple[int, Rise]] = []
        for index, commit in enumerate(history.states.commits):
            if not any(item in commit.paths for item in paths):
                states.append(None)
                continue
            new = ledger_rows(history, "at", commit.sha, path)
            old = ledger_rows(history, "before", commit.sha, path)
            states.append(float(new) if new is not None else None)
            if new is not None and old is not None and new > old:
                rises.append((index, Rise(commit.short, commit.subject, old, new, commit.sha)))
        committed = ledger_rows(history, "head", "", path)
        current = rule_ledger.count_rows(history.text("working", "", (path,)))
        working_rise = (Rise(WORKING_TREE, "not committed", committed, current)
                        if committed is not None and current is not None and current > committed else None)
        above = rises_after_lowest(states, rises, float(current) if current is not None else None, working_rise)
        if above is not None:
            lowest, named = above
            raised = "; ".join(rise.described() for rise in named) if named else "by no single commit"
            findings.append(check_report.Finding(
                "warning", path, 0, RULE,
                f"the ledger holds {current} rows, more than its lowest committed count {number_text(lowest)}.  Rows "
                f"added: {raised}.  Remove a row when its item is fixed", "ledger-grew", path))
    return findings


# ── The quarantine ledger ──────────────────────────────────────────

# The configuration, the file, the kind and the entity of one count of the quarantine ledger.
CountKey = tuple[str, str, str, str]
# The file, the kind and the entity of one count.
RowKey = tuple[str, str, str]
# The formats of the earlier file utils/scripts/quarantine-ledger.txt.  A format 1 version has no format row and
# counts directories.  A format 2 version counts files, a format 3 version also keys each count by its entity, and a
# format 4 version also counts the findings of the kind opted-out.  The rows of assay.core.ledger hold the counts of
# format 4, so a version of utils/ledgers/quarantine.txt reads as format 4.
FILE_FORMAT = 2
ENTITY_FORMAT = 3
OPTED_OUT_FORMAT = 4
EARLIER_FORMATS = (FILE_FORMAT, ENTITY_FORMAT, OPTED_OUT_FORMAT)
DIRECTORY_CONFIGURATION = "default"
OPTED_OUT = "opted-out"
REGION = "region"
# The kinds of a raised count that gives no error: only `guard write --rule quarantine --admit` writes such a count.
ADMITTED_KINDS = frozenset({OPTED_OUT, REGION})
# The kind names of the plugin before format 2 of its report, and the names of this time.
EARLIER_KINDS = {
    "std_entity": "std-entity", "std_object": "std-object", "c_library_object": "c-library-object",
    "raw_pointer_object": "raw-pointer-object", "raw_function_pointer": "raw-function-pointer",
    "c_array_object": "c-array-object", "raw_new_delete": "raw-new-delete", "c_library_call": "c-library-call",
    "foreign_symbol": "foreign-symbol", "compiler_builtin": "compiler-builtin", "inline_asm": "inline-asm",
    "assert_expansion": "assert-expansion", "layer_header": "layer-header", "door_header": "door-header",
    "upward_include": "upward-include", "contract_specifier": "contract-specifier", "opted_out": OPTED_OUT,
}
EARLIER_PENDING = "replace_pending:"
PENDING = "replace-pending:"
# The text between the reason of a region and the inner kind in the entity of an opted-out count.
REASON_SEPARATOR = " | "


def current_kind(kind: str) -> str:
    """Return the name of this time of a kind of the plugin, also of a kind of an earlier name."""
    if kind.startswith(EARLIER_PENDING):
        return PENDING + kind.removeprefix(EARLIER_PENDING)
    return EARLIER_KINDS.get(kind, kind)


def current_key(file: str, kind: str, entity: str) -> RowKey:
    """Return one count key with the kind names of this time, also the inner kind of an opted-out entity.

    The entity of an opted-out count is "REASON | KIND ENTITY", and the inner kind and its entity hold no separator.
    """
    kind = current_kind(kind)
    reason, separator, rest = entity.rpartition(REASON_SEPARATOR)
    if kind == OPTED_OUT and separator:
        inner, space, inner_entity = rest.partition(" ")
        entity = f"{reason}{separator}{current_kind(inner)}{space}{inner_entity}"
    return file, kind, entity


class VersionError(ValueError):
    """A version of the quarantine ledger does not have a format of the ledger."""


@dataclass(slots=True)
class QuarantineVersion:
    """The counts of one version of the quarantine ledger, in each of its formats.

    `configurations` keeps the order of the version, and its first name is the
    base.  A format 1 version has one configuration with the name "default".
    A count of format 1 or 2 has an empty entity.  `lines` gives the line of
    the row of each configuration and count.
    """

    format: int = OPTED_OUT_FORMAT
    configurations: dict[str, dict[str, str]] = field(default_factory=dict)
    counts: dict[RowKey, int] = field(default_factory=dict)
    deltas: dict[str, dict[RowKey, int]] = field(default_factory=dict)
    lines: dict[CountKey, int] = field(default_factory=dict)

    @property
    def base(self) -> str | None:
        """The name of the base configuration, or None for a version with no configuration."""
        return next(iter(self.configurations), None)

    def expected(self, name: str) -> dict[RowKey, int]:
        """Return the count of each file, kind and entity of one configuration, with no zero count.

        Complexity: linear in the rows of the base and of the configuration.
        """
        if name == self.base:
            return dict(self.counts)
        delta = self.deltas.get(name, {})
        counts = {key: self.counts.get(key, 0) + delta.get(key, 0) for key in set(self.counts) | set(delta)}
        return {key: count for key, count in counts.items() if count != 0}

    def line_of(self, name: str, key: RowKey) -> int:
        """Return the line of the row of one count: the difference row, else the base row, else the first row of its file.

        Complexity: O(1) for a count with a row, else linear in the rows.
        """
        exact = self.lines.get((name, *key)) or self.lines.get((self.base or "", *key), 0)
        if exact:
            return exact
        return min((line for (row_name, file, _kind, _entity), line in self.lines.items()
                    if file == key[0] and row_name in (name, self.base)), default=0)


def is_delta_word(text: str) -> bool:
    """Whether a word is the difference of an earlier format: '+' or '-', and a number above 0 with no leading 0."""
    return (len(text) >= 2 and text[0] in "+-" and "1" <= text[1] <= "9"
            and all("0" <= character <= "9" for character in text[2:]))


def read_rows_version(text: str, where: str) -> QuarantineVersion:
    """Read a version of utils/ledgers/quarantine.txt through assay.core.ledger.

    Raises:
        VersionError: If the text does not obey the rows of the ledger
    """
    try:
        ledger = rule_ledger.parse(text, where, QUARANTINE_RULE, rule_registry.Policy.COUNT)
    except rows_grammar.RowsError as problem:
        raise VersionError(str(problem)) from None
    version = QuarantineVersion(OPTED_OUT_FORMAT)
    for item in ledger.configurations:
        version.configurations[item.name] = dict(item.facts)
    base = ledger.base_name or ""
    for key, entry in ledger.base.items():
        row = current_key(key.path, key.kind, key.entity)
        version.counts[row] = int(entry.value)
        version.lines[(base, *row)] = entry.line
    for name, table in ledger.deltas.items():
        for key, entry in table.items():
            row = current_key(key.path, key.kind, key.entity)
            version.deltas.setdefault(name, {})[row] = int(entry.value)
            version.lines[(name, *row)] = entry.line
    return version


def read_earlier_version(rows: list[tuple[int, str]], where: str) -> QuarantineVersion:
    """Read the rows of a version of the earlier file utils/scripts/quarantine-ledger.txt, in the format 1 to 4.

    Raises:
        VersionError: If a row does not have its format, or a row has a second copy
    """
    version = QuarantineVersion(1 if rows else OPTED_OUT_FORMAT)
    if rows and rows[0][1].split()[0] == "format":
        number, row = rows.pop(0)
        words = row.split()
        if len(words) != 2 or words[1] not in {str(item) for item in EARLIER_FORMATS}:
            raise VersionError(f"{where}:{number}: the format row of the earlier file is 'format {OPTED_OUT_FORMAT}'")
        version.format = int(words[1])
    for number, row in rows:
        if version.format == 1:
            read_directory_row(version, number, row.split(), where)
        else:
            read_file_row(version, number, row, where)
    return version


def read_directory_row(version: QuarantineVersion, number: int, words: list[str], where: str) -> None:
    """Read one row of a format 1 version: a configuration row or a count of a directory and kind."""
    if words[0] == "configuration" and len(words) == 3:
        version.configurations.setdefault(DIRECTORY_CONFIGURATION, {})[words[1]] = words[2]
        return
    if len(words) != 3 or not words[2].isdigit():
        raise VersionError(f"{where}:{number}: a format 1 row is 'DIRECTORY KIND COUNT'")
    version.configurations.setdefault(DIRECTORY_CONFIGURATION, {})
    row = current_key(words[0], words[1], "")
    version.counts[row] = int(words[2])
    version.lines[(DIRECTORY_CONFIGURATION, *row)] = number


def read_file_row(version: QuarantineVersion, number: int, row: str, where: str) -> None:
    """Read one row of a format 2, 3 or 4 version.  The entity of a format 3 or 4 row is the rest of the line.

    Raises:
        VersionError: If the row does not have its format, or it has a second copy
    """
    words = row.split()
    if words[0] == "configuration":
        if len(words) != 4 or not rule_ledger.rows_grammar_name(words[1]):
            raise VersionError(f"{where}:{number}: a configuration row is 'configuration NAME KEY VALUE'")
        keys = version.configurations.setdefault(words[1], {})
        if words[2] in keys:
            raise VersionError(f"{where}:{number}: the configuration {words[1]} has a second row for {words[2]}")
        keys[words[2]] = words[3]
        return
    base = version.base
    is_delta = words[0] in version.configurations
    if version.format == FILE_FORMAT:
        fields = words if len(words) == (4 if is_delta else 3) else []
    else:
        fields = row.split(" ", 4 if is_delta else 3)
    if is_delta:
        if words[0] == base or len(fields) < 4 or "" in fields or not is_delta_word(fields[3]):
            raise VersionError(f"{where}:{number}: a difference row is 'NAME FILE KIND +N|-N ENTITY'")
        name, key, value = fields[0], (fields[1], fields[2], fields[4] if len(fields) == 5 else ""), int(fields[3])
        table = version.deltas.setdefault(name, {})
    else:
        if base is None or len(fields) < 3 or "" in fields or not fields[2].isdigit() or int(fields[2]) == 0:
            raise VersionError(f"{where}:{number}: a count row is 'FILE KIND COUNT ENTITY', after the configurations")
        name, key, value = base, (fields[0], fields[1], fields[3] if len(fields) == 4 else ""), int(fields[2])
        table = version.counts
    if key[2] != " ".join(key[2].split()):
        raise VersionError(f"{where}:{number}: the entity {key[2]!r} has more than one space between two words")
    key = current_key(*key)
    if key in table:
        raise VersionError(f"{where}:{number}: the count {key} has a second row in the configuration {name}")
    table[key] = value
    version.lines[(name, *key)] = number


def read_quarantine_version(text: str, where: str) -> QuarantineVersion:
    """Read one version of the quarantine ledger, in the rows of assay.core.ledger or in a format of the earlier file.

    The rows of assay.core.ledger start with the row 'format 1' and the row of the rule.  The earlier file had no
    format row in its format 1, and the rows 'format 2' to 'format 4' after it.

    Raises:
        VersionError: If the text does not have a format of the ledger, or a count of a configuration is below 0
    """
    rows = [(number, raw.strip()) for number, raw in enumerate(text.splitlines(), start=1)
            if raw.strip() and not raw.strip().startswith("#")]
    if rows and rows[0][1] == f"format {rule_ledger.FORMAT}":
        return read_rows_version(text, where)
    version = read_earlier_version(rows, where)
    for name in version.configurations:
        if any(count < 0 for count in version.expected(name).values()):
            raise VersionError(f"{where}: a difference of the configuration {name} makes a count below 0")
    return version


@dataclass(frozen=True, slots=True)
class LedgerRise:
    """One count of the quarantine ledger that a commit raised."""

    configuration: str
    file: str
    kind: str
    entity: str
    old: int
    new: int

    @property
    def key(self) -> tuple[str, str, str]:
        """The file, the kind and the entity of the count, as the quarantine ledger keys it."""
        return self.file, self.kind, self.entity

    def described(self) -> str:
        """Return the rise as words for a message."""
        what = f"{self.kind} {self.entity}" if self.entity else self.kind
        return f"{self.file} {what} in {self.configuration} from {self.old} to {self.new}"


def absolute_counts(ledger: QuarantineVersion, has_entity: bool = True) -> dict[CountKey, int]:
    """Return the count of each configuration, file, kind and entity of a ledger: the base count plus the difference."""
    counts: dict[CountKey, int] = {}
    for name in ledger.configurations:
        for (file, kind, entity), count in ledger.expected(name).items():
            key = (name, file, kind, entity if has_entity else "")
            counts[key] = counts.get(key, 0) + count
    return counts


def difference_of(ledger: QuarantineVersion, name: str, row: RowKey, has_entity: bool) -> int:
    """Return the difference of one row of a configuration from the base, or the sum for its file and kind."""
    deltas = ledger.deltas.get(name, {})
    if has_entity:
        return deltas.get(row, 0)
    return sum(delta for (file, kind, _entity), delta in deltas.items() if (file, kind) == row[:2])


def kind_totals(ledger: QuarantineVersion) -> dict[str, int]:
    """Return the sum of the base counts of each kind of a ledger."""
    totals: dict[str, int] = {}
    for (_file, kind, _entity), count in ledger.counts.items():
        totals[kind] = totals.get(kind, 0) + count
    return totals


def comparable(before: QuarantineVersion, after: QuarantineVersion) -> bool:
    """Whether two versions of the quarantine ledger compare row by row: both count files, or they have one format."""
    return before.format == after.format or min(before.format, after.format) >= FILE_FORMAT


def has_entities(before: QuarantineVersion, after: QuarantineVersion) -> bool:
    """Whether two comparable versions compare by entity."""
    return min(before.format, after.format) >= ENTITY_FORMAT


def ledger_rises(before: QuarantineVersion, after: QuarantineVersion, moves: dict[str, str],
                 peaks: dict[CountKey, int]) -> list[LedgerRise]:
    """Return each count that rose from one version of the quarantine ledger to the next.

    * Two versions of different formats compare the total of each kind of the
      base when one of them counts directories (format 1).  A version of
      format 2 and a version with entities compare the count of each file and
      kind, with an empty entity.
    * A configuration that the old version does not have is new coverage, and
      no rise.  The base keeps its history when its name changes.
    * A file that git moved keeps the rows of its old path.
    * A count of a configuration other than the base rises only when its
      difference from the base rises, and not when it comes back to a value of
      its history, `peaks`.
    * A count of opted-out is no rise when the old version does not count
      opted-out.
    * The first count of the kind region gives no finding: the ledger counts
      the regions from format 2 of the report of the plugin.

    Complexity: O(r log r) for r counts, and O(r * d) for d difference rows when the versions compare by file.
    """
    if not comparable(before, after):
        old_totals, new_totals = kind_totals(before), kind_totals(after)
        return [LedgerRise(after.base, TOTAL, kind, "", old_totals.get(kind, 0), count)
                for kind, count in sorted(new_totals.items()) if count > old_totals.get(kind, 0)]
    keyed = has_entities(before, after)
    counts_opted_out = before.format >= OPTED_OUT_FORMAT
    old_counts = {(after.base if name == before.base else name, file, kind, entity): count
                  for (name, file, kind, entity), count in absolute_counts(before, keyed).items()}
    counts_regions = any(kind == REGION for (_name, _file, kind, _entity) in old_counts)
    new_counts = absolute_counts(after, keyed)
    rises: list[LedgerRise] = []
    for key in sorted(new_counts):
        name, file, kind, entity = key
        new, old = new_counts[key], old_counts.get(key, 0)
        if (new <= old or (name not in before.configurations and name != after.base)
                or (kind == OPTED_OUT and not counts_opted_out) or (kind == REGION and not counts_regions)):
            continue
        source = moves.get(file)
        if (source is not None and old == 0 and new_counts.get((name, source, kind, entity), 0) == 0
                and old_counts.get((name, source, kind, entity), 0) >= new):
            continue
        row = (file, kind, entity)
        if name != after.base and (new <= peaks.get(key, 0) or difference_of(after, name, row, keyed)
                                   <= difference_of(before, name, row, keyed)):
            continue
        rises.append(LedgerRise(name, file, kind, entity, old, new))
    return rises


def rise_line(ledger: QuarantineVersion, rise: LedgerRise) -> int:
    """Return the line of the row of a rise in a ledger.  A rise of a file and kind gives the first row of its file."""
    if rise.entity or ledger.format < ENTITY_FORMAT:
        return int(ledger.line_of(rise.configuration, rise.key))
    lines = [line for (name, file, kind, _entity), line in ledger.lines.items()
             if (file, kind) == (rise.file, rise.kind) and name in (rise.configuration, ledger.base)]
    return min(lines, default=0)


def changes_machinery(paths: frozenset[str]) -> bool:
    """Whether a commit changed the quarantine plugin or the rule table."""
    return any(path == item or path.startswith(item) for path in paths for item in QUARANTINE_MACHINERY)


def quarantine_findings(history: History) -> list[check_report.Finding]:
    """Read each raise of the quarantine ledger in the history and in the working tree.

    * A commit that raised a count and that changes neither the plugin nor the
      rule table gives an error of the kind raise, which a row of the ledger of
      this rule can admit.  A raised count of opted-out or region gives no
      error.
    * A configuration other than the base is stale after a commit of the plugin
      or the rule table that did not write its rows.  A raise of a stale
      configuration is no error.
    * A raised count that stays above its value before the commit gives a
      warning on each run, which names the commit.
    * A raise in the working tree gives a warning only.

    Complexity: O(c * r) for c commits of the ledger, the plugin and the rule table, and r counts.
    """
    if QUARANTINE_LEDGER not in history.ledgers:
        return []
    parsed: dict[str, QuarantineVersion | None] = {}

    def parse(text: str | None, where: str) -> QuarantineVersion | None:
        """Read one version of the ledger, one time for each text, or None for no text or a malformed text."""
        if text is None:
            return None
        if text not in parsed:
            try:
                parsed[text] = read_quarantine_version(text, where)
            except VersionError:
                parsed[text] = None
        return parsed[text]

    # A state holds the ledger or, before the move to the rows of assay.core.ledger, its earlier file.
    path = QUARANTINE_PATHS
    findings: list[check_report.Finding] = []
    peaks: dict[CountKey, int] = {}
    standing: list[tuple[git.Commit, LedgerRise]] = []
    stale: set[str] = set()
    known: set[str] = set()
    last: QuarantineVersion | None = None
    by_sha = {commit.sha: commit for commit in history.states.commits
              if any(item in commit.paths for item in QUARANTINE_PATHS)}
    for sha, (paths, moves) in reversed(list(history.changes.items())):
        commit = by_sha.get(sha)
        if commit is None:
            stale |= known if changes_machinery(paths) else set()
            continue
        after = parse(history.text("at", commit.sha, path), f"{commit.short}:{QUARANTINE_LEDGER}")
        before = parse(history.text("before", commit.sha, path), f"{commit.short}^:{QUARANTINE_LEDGER}")
        if after is None:
            continue
        if before is None or not comparable(before, after):
            peaks, standing = {}, []
        elif before.format != after.format:
            peaks = {}
        last = after
        rises = ledger_rises(before, after, moves, peaks) if before is not None else []
        for key, count in absolute_counts(after).items():
            peaks[key] = max(peaks.get(key, 0), count)
        standing += [(commit, rise) for rise in rises if rise.file != TOTAL]
        later = {name for name in after.configurations if name != after.base}
        written = {name for name in later if before is None or after.deltas.get(name) != before.deltas.get(name)
                   or after.configurations[name] != before.configurations.get(name)}
        blocking = [rise for rise in rises if rise.kind not in ADMITTED_KINDS
                    and (rise.configuration not in stale or rise.configuration == after.base)]
        stale = (stale - written) | (later - written if changes_machinery(paths) else set())
        known = later
        if not blocking or changes_machinery(paths):
            continue
        shown = "; ".join(rise.described() for rise in blocking[:SHOWN_RISES])
        more = f"; and {len(blocking) - SHOWN_RISES} more" if len(blocking) > SHOWN_RISES else ""
        findings.append(check_report.Finding(
            "error", QUARANTINE_LEDGER, 0, RULE,
            f"the commit {commit.short} ({commit.subject}) raised {len(blocking)} counts of {QUARANTINE_LEDGER}, and "
            f"it changes neither the plugin of utils/tools/quarantine/ nor the rule table "
            f"{RULE_TABLE}: {shown}{more}.  A change can only lower a count.  Remove the new findings, "
            f"or add a row to {rule_ledger.shown_path(RULE)} that gives the commit and the reason", RAISE_KIND,
            commit.short))
    current = parse(history.text("working", "", path), QUARANTINE_LEDGER)
    if current is None or last is None or not comparable(last, current):
        return findings
    counts = absolute_counts(current)
    file_counts = absolute_counts(current, has_entity=False)
    for commit, rise in standing:
        value = (counts if rise.entity else file_counts).get((rise.configuration, *rise.key), 0)
        if value > rise.old:
            what = f"{rise.kind} {rise.entity}" if rise.entity else rise.kind
            findings.append(check_report.Finding(
                "warning", QUARANTINE_LEDGER, rise_line(current, rise), RULE,
                f"{rise.file} {what} in the configuration {rise.configuration} has {value} findings, above "
                f"{rise.old} before the commit {commit.short} ({commit.subject}) raised it to {rise.new}.  Lower it "
                f"again", "standing-raise", f"{commit.short} {rise.file}"))
    head = parse(history.text("head", "", path), f"HEAD:{QUARANTINE_LEDGER}")
    if head is not None and comparable(head, current):
        for rise in ledger_rises(head, current, {}, peaks):
            findings.append(check_report.Finding(
                "warning", QUARANTINE_LEDGER, rise_line(current, rise), RULE,
                f"{WORKING_TREE} (not committed) raises {rise.described()}.  The commit must change the plugin or the "
                f"rule table, or lower the count again", "working-raise", rise.file))
    return findings


def admission_findings(context: MetaContext) -> list[check_report.Finding]:
    """Refuse a row of the ledger of this rule whose key does not have the form of an admission."""
    rule = context.registry.rules.get(RULE)
    if rule is None:
        return []
    try:
        ledger = rule_ledger.read(rule, context.root)
    except (rows_grammar.RowsError, OSError):
        return []
    found: list[check_report.Finding] = []
    for key, entry in sorted(ledger.base.items()):
        words = key.entity.split(" ")
        commit = words[0] if words else ""
        is_commit = len(commit) == 9 and set(commit) <= HEX_DIGITS
        is_threshold = key.path == REGISTRY and key.kind in COLUMNS and len(words) == 2 and is_commit
        is_raise = key.path == QUARANTINE_LEDGER and key.kind == RAISE_KIND and len(words) == 1 and is_commit
        if not (is_threshold or is_raise):
            found.append(check_report.Finding(
                "error", rule_ledger.shown_path(RULE), entry.line, RULE,
                f"a row is '{REGISTRY} | warn|error | admit | COMMIT RULE | REASON' or '{QUARANTINE_LEDGER} | "
                f"{RAISE_KIND} | admit | COMMIT | REASON', with a commit of 9 hexadecimal characters", "row",
                str(entry.line)))
    return found


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each rise of a threshold or a ledger in the history of the tree, and each raise of the quarantine ledger."""
    history = read_history(context.root)
    registry = context.registry
    return (threshold_findings(history, registry) + quarantine_findings(history) + ledger_findings(history, registry)
            + admission_findings(context))


# ── The plants ─────────────────────────────────────────────────────

COMMITS = "commits.txt"
WORK = "work"


def plant_context(plant: Path, scratch: Path) -> MetaContext:
    """Build the history of a plant in a throwaway repository, and give its context.

    A plant holds commits.txt, whose rows are `commit N | SUBJECT | BODY`, and
    one directory N for each commit with each file of the tree at that
    commit.  A directory work, when it exists, is the working tree after the
    last commit.

    Raises:
        NotApplicable: If commits.txt does not obey its form
    """
    try:
        rows = rows_grammar.read(plant / COMMITS, f"{plant.name}/{COMMITS}")
    except (rows_grammar.RowsError, OSError) as problem:
        raise NotApplicable(f"the plant has no history: {problem}") from None
    scratch.mkdir(parents=True)
    git.init(scratch)
    for row in rows:
        if row.words[0] != "commit" or len(row.words) != 2 or len(row.cells) != 2:
            raise NotApplicable(f"{plant.name}/{COMMITS}:{row.line}: a row is 'commit N | SUBJECT | BODY'")
        replace_tree(scratch, plant / row.words[1])
        git.commit_all(scratch, row.cells[0], row.cells[1])
    if (plant / WORK).is_dir():
        replace_tree(scratch, plant / WORK)
    return MetaContext(scratch)


def replace_tree(root: Path, source: Path) -> None:
    """Make the files of a repository the files of a source directory, and keep the directory .git."""
    for entry in root.iterdir():
        if entry.name == ".git":
            continue
        if entry.is_dir():
            shutil.rmtree(entry)
        else:
            entry.unlink()
    shutil.copytree(source, root, dirs_exist_ok=True)
