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
      earlier file of the ledger, added.  A commit that changed one file of a
      ledger reads each file of the ledger at the commit and at its parent.
    * Each path that a commit of the quarantine ledger or of its earlier file
      changed, and each file that git reports as moved in it.
    * The working-tree copy of each file, which can hold an edit that no
      commit holds yet.
    * The moved lines of a commit that increased a count of a ledger, in the
      files of its increases and its falls (THE MOVE).
    * The ledger of this rule, utils/ledgers/history.txt (policy admit): the
      rises of the past whose commit gives no measurement.  A row
      `utils/registry.txt | COLUMN | admit | COMMIT RULE | REASON` admits the
      rise of the warn or error threshold of RULE, and a row
      `utils/ledgers/quarantine.txt | raise | admit | COMMIT | REASON` admits
      a raise of the quarantine ledger.  A row with the kind region-reason
      admits an admitted count with no reason (THE ADMITTED COUNTS).  A row
      `utils/ledgers/RULE.txt | rebase | admit | COMMIT | REASON` names a
      commit that changed the key or the format of a ledger, or a commit that
      made the scope of the rule larger (THE REBASE).  A row
      `utils/ledgers/RULE.txt | grow | admit | COMMIT | REASON` admits the
      growth of a ledger in one commit (THE GROWTH).  A row
      `utils/registry.txt | keep | admit | RULE COLUMN | REASON` keeps a
      threshold above its lowest value (THE KEPT THRESHOLDS).  COMMIT is the
      hash in 9 characters.

THE FINDINGS
    * A threshold (the warn or the error field of a measured rule) above its
      lowest committed value gives a warning on each run.  A `keep` row can
      admit it (THE KEPT THRESHOLDS).  The warning names
      each commit that raised it after the newest commit at the lowest value.
      A value with another unit starts a new history of the rule.  The unit
      of a version is its first word, so "G instructions" of the old table and
      "G" of the registry are one unit.
    * A ledger whose row count is above its lowest committed count gives a
      warning on each run (assay.core.ledger.count_rows).  A `grow` row can
      admit the growth of one commit (THE GROWTH).  A ledger of a rule
      with the policy baseline holds the sums that --write writes again, and
      the rule does not count its rows.  The rule does not count the rows of
      its own ledger.  Each row admits an event of the history, and an event
      of the history cannot change.  Each row gives the warning of an admitted
      finding.  The quarantine ledger has counts, so the rule reads it as the
      next item says.
    * A commit that raised a count of the quarantine ledger, and that changes
      neither the plugin of utils/tools/quarantine/ nor the rule table
      utils/rule-table.txt, gives an error, of the kind raise.  The rule table
      was utils/scripts/layer-rules.txt before it moved.  An increase that a
      move pays is no increase (THE MOVE).  A raised count of
      the kind opted-out or region is no error of the kind raise.  Only
      `guard write --rule quarantine --admit` writes it (THE ADMITTED COUNTS).
      Each raised count that stays above its count before the commit gives a
      warning on each run.  A count of the kind opted-out or region gives no
      such warning when its commit gave the reason of its region (THE
      ADMITTED COUNTS).  A `grow` row can admit the other counts that one
      commit raised (THE GROWTH).
      read_quarantine_version() reads each version: the rows of
      assay.core.ledger, and the formats 1 to 4 of the earlier file
      utils/scripts/quarantine-ledger.txt.  The rows hold the counts of format
      4, so the commit of the move compares row by row, and the rules of the
      formats, the configurations and the moves stay.  Each version reads with
      the kind names and the include keys of this time (current_key()).  The
      change of the names to format 2 of the report then gives no finding.  The
      change of an include key to a key with no layer gives no finding too.
      The first count of the kind region gives no finding.
    * A commit that raised a threshold, and whose body holds no measurement of
      the rule, gives an error, of the kind of its field.
    A rise in the working tree gives a warning only, and the error comes when
    the rise lands without a measurement.

THE MOVE
    A change can move lines from one file to a different file, for example
    when it divides a header into two new headers.  The counts of the
    findings then fall in the first file and increase in the new files.  The
    count in all the files of the change does not increase.  A commit can
    move a line from A to B (THE MOVED LINES of assay.core.git, THE MOVE of
    assay.core.ledger).  A fall of the key (A, KIND, ENTITY) then pays an
    increase of the key (B, KIND, ENTITY) in the same configuration.  An
    entity that agrees after a move to a different namespace of the tree
    pays too, also in one file.  The
    rule reads the moved lines of a commit only in the files of its
    increases and its falls.
    * The quarantine ledger: a paid increase gives no error of the kind
      raise and no warning.  The rule judges each part of an increase that
      no fall pays as before.  When a move pays each increase of a commit, a
      `grow` row of the commit admits nothing.
    * Each other ledger: the rows that a move adds are no growth, as the
      rows of a `grow` row are.  The rule counts the rows of the ledger at
      the commit again, with each paid increase back in the file of its
      fall.  The difference of the two row counts is the rows that the move
      adds.  The rule reads them for each increase after the newest lowest
      state of the ledger, when the ledger is above that state.
    No row of the ledger of this rule is necessary for a move.

THE ADMITTED COUNTS
    An opt-out region covers the smallest span that its reason makes
    necessary (CLAUDE.md section XXII, R7).  A region that covers more raises
    a count of the kind opted-out, and a new region raises a count of the kind
    region.  Only `guard write --rule quarantine --admit` writes such a count.
    A commit that raised one gives an error of the kind region-reason.  The
    error does not come when its subject or its body holds the reason of the
    region, as the region spells it.  The reason starts with its class.  The message then gives the
    class and the reason.  The white space between the words of the message
    can be different.  A commit that changes the plugin or the rule table
    gives no such error, because the plugin can then find more in a region.
    A row `utils/ledgers/quarantine.txt | region-reason | admit | COMMIT |
    REASON` admits the error of one commit.  When the commit gave the reason
    of its region, its count of the kind opted-out or region gives no
    warning.  The reason is then in the row of the region in the quarantine
    ledger, and in the message of the commit.

THE GROWTH
    A `grow` row names one commit and one ledger.  The growth of the ledger in
    that commit is necessary.  Examples are a new door claim, a probe of the
    state after a move, and a design row that must stay.  REASON tells why.
    The row admits the growth of that one commit only, as a rebase row counts
    it.  A growth of a different commit gives its warning again.  The row
    holds each row of its commit that a rebase row at a larger scope of the
    commit cannot hold.  A commit that made the file of a ledger with no file
    at its parent added each row of the file.  The two rows then hold
    different rows of the ledger:
    * Each other ledger: the lowest count before the commit increases by the
      rows that the commit added, less the rows of a larger scope.
    * The quarantine ledger: each count that the commit raised gives no
      warning.  At a larger scope, these are only the counts of files that
      the commit changed.
    A `grow` row is an error, of the kind row, in two cases.  Its commit
    added no such row to the ledger, or the row names a ledger that takes no
    rebase row.  A `grow` row admits one error of the kind `grow`.  The rule
    gives that error only when the row changes the result, as for a rebase
    row.  The rule refuses a `grow` row that changes no result, because the
    row admits nothing.

    with_admissions() writes the rows of a commit, with the commit in
    SHORT_HASH characters.  The writer of the growth of a ledger at a commit
    of the branch is this command:
      `guard write --rule history --growth RULE COMMIT --reason TEXT`
    growth_kinds() gives its rows: a rebase row when the commit is a rebase
    commit of the ledger (THE REBASE), and a `grow` row when grow_cause()
    gives rows that a rebase row cannot hold.  The landing of a change writes
    the `grow` rows of its first commit in a second commit, with this
    command (assay.loop.land, THE GROWTH):
      `loop land --grow RULE REASON`

THE KEPT THRESHOLDS
    A `keep` row names a measured rule and one field of it.  REASON gives the
    measurement that keeps the threshold above its lowest committed value.
    The warning of that threshold then becomes an error of the kind `keep`,
    which the row admits.  When the threshold falls to its lowest value, the
    row admits nothing, and the rule refuses it.

THE REBASE
    A rebase row names a commit at which the rows of a ledger do not compare
    with its history.  The commit changed the ledger or an earlier file of
    it, and change_cause() gives one of two causes.  The code of the guards
    is each file of utils/ that is not in utils/ledgers/ or utils/plants/.

    A change of the key or the format.  The items of the ledger stay the
    same, and their row count changes.  One of these facts holds at the
    commit:
    * The set of the files of the ledger changed, and the ledger had a file
      before and after the commit.  Then two ledgers joined, or the ledger
      moved to a different file.
    * The format row of a file of the ledger changed.
    * No counted key of the ledger stayed (assay.core.ledger.counted_keys),
      and the commit changed the code of the guards.  A counted key is the
      key of a row with a count.  A row with the value admit holds a reason
      and no count, and such a row can stay.
    The lowest count of the ledger then starts again at that commit.

    A larger scope.  The rule reads items that it did not read before, such
    as a file, an archive, a configuration or a kind.  The commit changed the
    code of the guards, and it added a row of a file that it did not change.
    A row of a file that the commit changed can come from new code.  Such a
    row does not show a larger scope.  No commit changes a path of the build
    directory, and a row of such a path shows a larger scope.  A commit that
    only raises the counts of keys that the ledger held does not make the
    scope larger.  The rows that the commit added in files that it did not
    change do not count as growth.  Its other rows count as growth:
    * Each ledger other than the quarantine ledger: the lowest count before
      the commit increases by those rows.  The increase is at most the rows
      that the commit added in total.  The growth of each other commit gives
      the warning.
    * The quarantine ledger: each count that the commit raised in a file that
      it did not change gives no warning.
    The rule reads each format of the quarantine ledger with the kind names
    of this time (current_key()).  A change of its key or its format is then
    no cause of a rebase.  The quarantine ledger takes a rebase row only at a
    larger scope, and its keys compare after current_key().  For each other
    ledger, a change of the key or the format comes first.

    A rebase row is an error, of the kind row, in each of these cases:
    * Its commit is not such a commit.
    * It names the ledger of this rule, a ledger of the policy baseline, or
      a file that is not a ledger of the tree.
    A rebase row admits one error of the kind rebase.  The rule gives that
    error only when the row changes the result.  For each ledger other than
    the quarantine ledger, the ledger is then above its lowest count without
    the row.  That count is lower than its lowest count with the row.  For
    the quarantine ledger, a count that the row does not count as growth then
    stays above its value before the commit.  The warning of the admitted
    finding stays on each run.  A row that changes no result admits nothing,
    and the rule refuses it.  A row at a commit before the newest change of
    the key or the format of its ledger changes no result.

THE MEASUREMENT
    A measurement of a rule is a number with the unit of the rule: "s" for
    seconds, "G" for G instructions, "GB", "MB", "KB", "bytes", "%",
    "operations", "elements", "links", "jobs", "threads", "passes", "runs", or
    "x" for a multiple.  The number is none of the values that the rule had in a field
    at a commit or in the working tree.
    It counts in a sentence that holds the name of the rule as a word.  It
    also counts in each other sentence when it is above the old threshold
    and at most the new one: the measured value that the old threshold
    refused.  A squash landing makes a second commit with the same change.  A
    rise is measured when one commit of the history that made the same step
    (the rule, the field, the old and the new value) gives a measurement.

WHY THE RULE CANNOT BE MET BY ACCIDENT
    A number with no unit, a count of files, a date or a commit hash is not a
    number with the unit of the rule.  A body that only tells the change
    ("raise link-time from 2 s to 5 s") fails, because the rule does not
    count a value that the rule ever had.  A body passes only with a quantity
    in the unit of the rule that no threshold of the rule ever was.

NOT APPLICABLE
    The rule does not apply, with the reason, when the tree is not a git work
    tree, or when the history is shallow: a shallow history does not hold the
    lowest values.

THE CACHE
    Git names a version of a file and a commit by their bytes, and their
    bytes do not change.  Each result that a version or a commit gives is
    then the same on each run.  The cache "history" of assay.core.cache
    (HistoryStore) keeps these results:
    * The version of the quarantine ledger that a text gives, by the SHA-256
      of the text.  A text that does not read keeps the value None.
    * The thresholds of a version of a table of TABLES.
    * The pairs of files of THE MOVE of a commit and a set of paths.
    * The cause of a rebase row or a `grow` row at a commit (row_cause()).
    * The state of the walk through the quarantine ledger (THE CHECKPOINT).
    An entry is the mark ENTRY_MARK, a digest of its payload and the payload,
    compressed JSON.  A damaged entry, an entry of a different shape and a
    write that fails give no result.  The run then calculates the result
    again.  With the caches off, the rule calculates each result.

THE IDENTITY
    The name of an entry holds a digest of the code that gives it.  The code
    is each module of the package that this module can import, also in a
    function, and the versions of git and Python.  A change of one of them
    gives different names, and an entry does not become stale.  The walk of
    the imports costs 0.3 s.  The cache then keeps a record of the closure
    with the digest of each module (CLOSURE_RECORD).  The walk runs again
    only when the record does not agree with the files.

THE CHECKPOINT
    The walk through the commits of the quarantine ledger keeps a state: the
    peaks, the raised counts, the stale configurations and the errors.  The
    state after a commit is a function of the commits up to it.  The hash of
    a commit names its message, its first parent and its files.  The key of a
    state is a digest of the identity and of each step up to it.  A step gives
    its hash, its paths and its moved files.

    A run continues from the newest state that the cache holds, reads the
    newer commits, and writes the state after the last commit.  A history with
    a different sequence of commits gives different keys.

THE COLD RUN
    A run with no usable entry reads each version of the quarantine ledger in
    at most POOL_WORKERS worker processes, and the workers write the entries.
    The moves of the commits before a commit do not change its raised counts.
    The walk then reads the raised counts of each new commit first.  Then it
    reads the diffs of THE MOVE in threads, and then the payments and the
    errors.  The changed paths of the rebase rows also come from threads.

Complexity: O(c * r) for c commits and r measured rules, and linear in the size of the versions of the files.  A
run reads only the commits after the newest state of the cache, and the changed rows of each one.
"""

import ast
import enum
import hashlib
import json
import math
import shutil
import sys
import threading
import zlib
from collections.abc import Callable, Iterable, Mapping, Sequence
from contextlib import suppress
from dataclasses import dataclass, field, replace
from functools import partial
from pathlib import Path

from assay.core import cache, git, proc
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT
from assay.guard.engines.meta.runner import MetaContext, NotApplicable
from assay.guard.engines.plugin.quarantine import include_key

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
    "artifact-symbols": (
        "utils/ledgers/libstdcxx-symbols.txt",
        "utils/ledgers/no-throw-no-rtti.txt",
        "utils/scripts/libstdcxx-symbols-ledger.txt",
        "utils/scripts/no-throw-no-rtti-allowlist.txt",
    ),
    "banned-calls": (
        "utils/scripts/no-reinterpret-allowlist.txt",
        "utils/scripts/no-reserve-allowlist.txt",
        "utils/scripts/no-spawn-process-allowlist.txt",
    ),
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
OWN_LEDGER = rule_ledger.shown_path(RULE)
RAISE_KIND = "raise"
REASON_KIND = "region-reason"
REBASE_KIND = "rebase"
GROW_KIND = "grow"
KEEP_KIND = "keep"
# The length of the commit in the entity of a row of the ledger of this rule.
SHORT_HASH = 9
ROW_KIND = "row"
# A commit that changes a path of the code of the guards can change the keys of a ledger (THE REBASE).
GUARD_CODE = "utils/"
NOT_GUARD_CODE = (f"{LEDGER_DIRECTORY}/", "utils/plants/")
COLUMNS = ("warn", "error")
WORKING_TREE = "the working tree"
TOTAL = "(the total of the kind)"
SHOWN_RISES = 8
HEX_DIGITS = frozenset("0123456789abcdef")


class Missing(enum.Enum):
    """The result of a lookup in the cache that holds no usable entry, apart from an entry that holds None."""

    TOKEN = enum.auto()


MISSING = Missing.TOKEN


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


class Change(enum.StrEnum):
    """The cause of a rebase (THE REBASE), or a growth that a `grow` row admits (THE GROWTH)."""

    KEY = "key"
    SCOPE = "scope"
    GROW = "grow"


@dataclass(frozen=True, slots=True)
class Cause:
    """How a commit changed what a ledger holds (THE REBASE and THE GROWTH).

    `rows` is the number of rows that a larger scope or a `grow` row does not
    count as growth, for each ledger other than the quarantine ledger.  For a
    `grow` row, it is each row that the commit added, less the rows of a
    larger scope.  `changed` holds each path that the commit changed.
    `is_scope` tells, for a `grow` row, that the commit also made the scope of
    the rule larger.  The row of the quarantine ledger then holds only the
    counts of files that the commit changed.
    """

    change: Change
    text: str
    rows: int = 0
    changed: frozenset[str] = frozenset()
    is_scope: bool = False


@dataclass(frozen=True, slots=True)
class Rebase:
    """A rebase row that names a rebase commit (THE REBASE), or a `grow` row (THE GROWTH): its commit, the index of
    the commit in the history, and the cause."""

    commit: git.Commit
    index: int
    cause: Cause

    @property
    def kind(self) -> str:
        """The kind of the error that the row admits: `grow` for a `grow` row, else rebase."""
        return GROW_KIND if self.cause.change is Change.GROW else REBASE_KIND


@dataclass(frozen=True, slots=True)
class Rebased:
    """The states of a ledger and its `Rise` records after its rebase rows (THE REBASE), and the rows that apply.

    `states` holds the row count at each commit, newest first, and None where
    no count applies.  `rises` holds the `Rise` records that stay growth.
    """

    states: tuple[float | None, ...]
    rises: tuple[tuple[int, Rise], ...]
    applied: tuple[Rebase, ...]

    @property
    def lowest(self) -> float | None:
        """The lowest count of the states, or None when no state has a count."""
        held = [value for value in self.states if value is not None]
        return min(held) if held else None


@dataclass(slots=True)
class History:
    """The states of the watched files, and each path that each commit of the quarantine ledger changed.

    `root` is the tree of the history, and `moved` keeps the pairs of files of THE MOVE of each commit and set of
    paths.  A history with no root has no move.  `store` is the cache of the results that a text or a commit gives
    (THE CACHE), and `versions`, `row_counts` and `tables` keep the reads of one run, by text.
    """

    states: git.PathHistory
    ledgers: list[str] = field(default_factory=list)
    changes: dict[str, tuple[frozenset[str], dict[str, str]]] = field(default_factory=dict)
    root: Path | None = None
    moved: dict[tuple[str, tuple[str, ...]], frozenset[tuple[str, str]]] = field(default_factory=dict)
    store: HistoryStore | None = None
    versions: dict[str, QuarantineVersion | None] = field(default_factory=dict)
    row_counts: dict[str, int | None] = field(default_factory=dict)
    tables: dict[tuple[str, str], dict[str, Row]] = field(default_factory=dict)

    def flows(self, sha: str, paths: Sequence[str]) -> frozenset[tuple[str, str]]:
        """Return each pair of files (A, B) of the paths where a commit moved a line from A to B (THE MOVE).

        The commit "" is the working tree against HEAD.  A history with no root, a root commit and a git command that
        does not succeed give no pair.  The cache keeps the pairs of a commit, because its hash names its diff.
        Complexity: one diff of the paths for each commit and set of paths that the cache does not hold.
        """
        if self.root is None or not paths:
            return frozenset()
        key = (sha, tuple(sorted(set(paths))))
        if key not in self.moved:
            stored = self.store.flows(key) if self.store is not None and sha else None
            if stored is not None:
                self.moved[key] = stored
                return stored
            try:
                found = (
                    git.diff_moves(self.root, f"{sha}^1", sha, key[1])
                    if sha
                    else git.diff_moves(self.root, "HEAD", None, key[1])
                )
                self.moved[key] = found.flows
                if self.store is not None and sha:
                    self.store.keep_flows(key, found.flows)
            except git.GitError:
                self.moved[key] = frozenset()
        return self.moved[key]

    def fill_flows(self, keys: Iterable[tuple[str, Sequence[str]]]) -> None:
        """Read the pairs of THE MOVE of each commit and set of paths that no cache holds, with the diffs in threads.

        flows() then gives each of them with no diff.  The function fills no key of the working tree ("") and no
        empty set of paths.
        Complexity: one diff of each key that the caches do not hold, divided between the threads.
        """
        root = self.root
        if root is None:
            return
        pending: dict[tuple[str, tuple[str, ...]], None] = {}
        for sha, paths in keys:
            key = (sha, tuple(sorted(set(paths))))
            if not sha or not paths or key in self.moved or key in pending:
                continue
            stored = self.store.flows(key) if self.store is not None else None
            if stored is None:
                pending[key] = None
            else:
                self.moved[key] = stored
        if not pending:
            return
        from concurrent.futures import ThreadPoolExecutor

        with ThreadPoolExecutor(min(proc.DEFAULT_JOBS, len(pending))) as pool:
            for key, found in zip(pending, pool.map(partial(moved_pairs, root), pending)):
                self.moved[key] = frozenset() if found is None else found
                if found is not None and self.store is not None:
                    self.store.keep_flows(key, found)

    def quarantine_version(self, text: str | None) -> QuarantineVersion | None:
        """Return one version of the quarantine ledger, or None for no text and for a text that does not read.

        Each caller ignores the text of a VersionError, and the read keys on the text alone.  The store keeps the
        version of a text across runs (THE CACHE).  Complexity: one read of each text that the store does not hold.
        """
        if text is None:
            return None
        if text not in self.versions:
            stored = self.store.version(text) if self.store is not None else MISSING
            if stored is MISSING:
                try:
                    read: QuarantineVersion | None = read_quarantine_version(text, QUARANTINE_LEDGER)
                except VersionError:
                    read = None
                if self.store is not None:
                    self.store.keep_version(text, read)
            else:
                read = stored
            self.versions[text] = read
        return self.versions[text]

    def table(self, path: str, text: str) -> dict[str, Row]:
        """Return the thresholds of one version of a table of TABLES, one time for each text.

        The store keeps the thresholds of a text across runs (THE CACHE).  Complexity: one read of each text that the
        store does not hold.
        """
        key = (path, text)
        if key not in self.tables:
            stored = self.store.table(path, text) if self.store is not None else None
            if stored is None:
                stored = registry_rows(text) if path == REGISTRY else old_table_rows(text)
                if self.store is not None:
                    self.store.keep_table(path, text, stored)
            self.tables[key] = stored
        return self.tables[key]

    def row_count(self, text: str | None) -> int | None:
        """Return the row count of one version of a ledger (assay.core.ledger.count_rows()), one time for each text."""
        if text is None:
            return None
        if text not in self.row_counts:
            self.row_counts[text] = rule_ledger.count_rows(text)
        return self.row_counts[text]

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


def moved_pairs(root: Path, key: tuple[str, tuple[str, ...]]) -> frozenset[tuple[str, str]] | None:
    """Return the pairs of files of THE MOVE of a commit and a set of paths, or None when git fails."""
    try:
        return git.diff_moves(root, f"{key[0]}^1", key[0], key[1]).flows
    except git.GitError:
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
        except KeyError, ValueError:
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
        return history.table(REGISTRY, registry_text)
    old_text = history.text(place, sha, (OLD_TABLE,))
    return history.table(OLD_TABLE, old_text) if old_text is not None else None


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
        after = sentence[start + len(rule_id) : start + len(rule_id) + 1]
        if not (before and (is_word_character(before) or before == "-")) and not (
            after and (is_word_character(after) or after == "-")
        ):
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
        if not sentence[index].isdecimal() or (
            index > 0 and (is_word_character(sentence[index - 1]) or sentence[index - 1] == ".")
        ):
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
        tail = sentence[after : after + 1]
        if sentence.startswith(word, unit_start) and not (
            needs_tail and tail and (is_word_character(tail) or tail == "%")
        ):
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

    A thread opens the cache and reads the changes of the quarantine ledger while git reads the states of the
    files (THE CACHE).

    Raises:
        NotApplicable: If the tree is not the top of a git work tree, or its history is shallow
    """
    try:
        if git.toplevel(root).resolve() != root.resolve():
            raise NotApplicable(f"{root} is not the top of a git work tree")
        if git.is_shallow(root):
            raise NotApplicable("the history is shallow, and a shallow history does not hold the lowest values")
        ledgers = sorted(git.tracked_files(root, (f"{LEDGER_DIRECTORY}/*.txt",), with_new=True))
        side = SideRead(root, QUARANTINE_LEDGER in ledgers)
        reader = threading.Thread(target=side.run, name="history-side")
        reader.start()
        try:
            earlier = [path for ledger in ledgers for path in EARLIER_PATHS.get(ledger, ())]
            watched = list(dict.fromkeys([*TABLES, *ledgers, *earlier]))
            groups = [(ledger, *EARLIER_PATHS[ledger]) for ledger in ledgers if ledger in EARLIER_PATHS]
            states = git.path_history(root, watched, groups)
        finally:
            reader.join()
        if side.problem is not None:
            raise side.problem
    except git.GitError as problem:
        raise NotApplicable(str(problem)) from None
    return History(states, ledgers, side.changes, root, store=side.store)


class SideRead:
    """The part of read_history() that a thread reads: the store of the run, and the changes of the quarantine ledger.

    `problem` holds the failure of git in the thread, and read_history() raises it.
    """

    __slots__ = ("changes", "needs_changes", "problem", "root", "store")

    def __init__(self, root: Path, needs_changes: bool) -> None:
        """Hold the tree, and tell if the tree holds the quarantine ledger.  The walk reads the changes of it."""
        self.root = root
        self.needs_changes = needs_changes
        self.store: HistoryStore | None = None
        self.changes: dict[str, tuple[frozenset[str], dict[str, str]]] = {}
        self.problem: git.GitError | None = None

    def run(self) -> None:
        """Open the store, and read the changes of the quarantine ledger, the plugin and the rule table."""
        self.store = HistoryStore.open(self.root)
        try:
            if self.needs_changes:
                self.changes = git.changes(self.root, [*QUARANTINE_PATHS, *QUARANTINE_MACHINERY])
        except git.GitError as problem:
            self.problem = problem


def rises_after_lowest(
    states: list[float | None], rises: list[tuple[int, Rise]], current: float | None, working_rise: Rise | None
) -> tuple[float, list[Rise]] | None:
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


def threshold_findings(
    history: History, registry: rule_registry.Registry, kept: frozenset[tuple[str, str]] = frozenset()
) -> list[check_report.Finding]:
    """Compare each threshold with its history, and each rise with the body of its commit.

    `kept` holds the rule and the field of each `keep` row (THE KEPT THRESHOLDS).  A threshold above its lowest value
    with a `keep` row gives no warning.  It gives an error of the kind `keep`, which the row admits.
    """
    findings: list[check_report.Finding] = []
    commits = history.states.commits
    current = table_rows(history, "working") or {}
    # A commit that changed a table has a state, also when the commit or its parent holds no table.
    tables = [
        (table_rows(history, "at", commit.sha) or {}) if any(path in commit.paths for path in TABLES) else None
        for commit in commits
    ]
    parents = [
        (table_rows(history, "before", commit.sha) or {}) if any(path in commit.paths for path in TABLES) else None
        for commit in commits
    ]
    last_committed = table_rows(history, "head") or {}
    bodies = {commit.sha: commit.body for commit in commits}
    for rule_id, row in sorted(current.items()):
        rule = registry.rules.get(rule_id)
        line = rule.line if rule is not None else 0
        display = check_report.unit_text(row.unit)
        known = {
            value
            for table in [*tables, *parents, current]
            if table
            for other in [table.get(rule_id)]
            if other is not None
            for value in (other.warn, other.error)
        }
        for column in COLUMNS:
            states: list[float | None] = []
            rises: list[tuple[int, Rise]] = []
            for index, (commit, table, parent) in enumerate(zip(commits, tables, parents)):
                if table is None or parent is None:
                    states.append(None)
                    continue
                new_row, old_row = table.get(rule_id), parent.get(rule_id)
                states.append(new_row.value(column) if new_row is not None and new_row.unit == row.unit else None)
                if (
                    new_row is None
                    or old_row is None
                    or new_row.unit != row.unit
                    or old_row.unit != row.unit
                    or new_row.value(column) <= old_row.value(column)
                ):
                    continue
                rises.append(
                    (
                        index,
                        Rise(commit.short, commit.subject, old_row.value(column), new_row.value(column), commit.sha),
                    )
                )
            measured = {
                (rise.old, rise.new)
                for _, rise in rises
                if has_measurement(bodies[rise.sha], rule_id, row.unit, known, rise.old, rise.new)
            }
            for _, rise in rises:
                if (rise.old, rise.new) in measured:
                    continue
                findings.append(
                    check_report.Finding(
                        "error",
                        REGISTRY,
                        line,
                        RULE,
                        f"the commit {rise.described()} raised the {column} threshold of {rule_id}, and no commit of this "
                        f"step names a measurement: a number in {display} that no threshold of the rule was, in a sentence "
                        f"with the rule name or above the old threshold and at most the new one.  Measure the quantity, "
                        f"and add a row to {rule_ledger.shown_path(RULE)} with the measurement as its reason",
                        column,
                        f"{rise.where} {rule_id}",
                    )
                )
            old_row = last_committed.get(rule_id)
            working_rise = (
                Rise(WORKING_TREE, "not committed", old_row.value(column), row.value(column))
                if old_row is not None and old_row.unit == row.unit and row.value(column) > old_row.value(column)
                else None
            )
            above = rises_after_lowest(states, rises, row.value(column), working_rise)
            if above is not None:
                lowest, named = above
                raised = "; ".join(rise.described() for rise in named) if named else "by no single commit"
                said = (
                    f"the {column} threshold of {rule_id} is {number_text(row.value(column))} {display}, above its "
                    f"lowest committed value {number_text(lowest)}.  Raised: {raised}"
                )
                if (rule_id, column) in kept:
                    findings.append(
                        check_report.Finding(
                            "error",
                            REGISTRY,
                            line,
                            RULE,
                            f"{said}.  A keep row of {OWN_LEDGER} gives the measurement that keeps it",
                            KEEP_KIND,
                            f"{rule_id} {column}",
                        )
                    )
                else:
                    findings.append(
                        check_report.Finding(
                            "warning",
                            REGISTRY,
                            line,
                            RULE,
                            f"{said}.  Lower it again when a measurement permits, or add a keep row to {OWN_LEDGER} that "
                            f"gives the measurement",
                            "above-lowest",
                            rule_id,
                        )
                    )
    return findings


def ledger_rule(path: str) -> str:
    """Return the rule of a ledger file utils/ledgers/ID.txt."""
    return Path(path).name.removesuffix(".txt")


def ledger_rows(history: History, place: str, sha: str, path: str) -> int | None:
    """Return the row count of a ledger in one state: of its file, else the sum of its earlier files, else None."""
    own = history.text(place, sha, (path,))
    if own is not None:
        return history.row_count(own)
    counts = [history.row_count(history.text(place, sha, (item,))) for item in EARLIER_PATHS.get(path, ())]
    held = [count for count in counts if count is not None]
    return sum(held) if held else None


def counts_rows(path: str, registry: rule_registry.Registry) -> bool:
    """Return True when the rule compares the row count of a ledger with its history.

    The quarantine ledger, the ledger of this rule and a ledger of the policy baseline give False.
    """
    rule = registry.rules.get(ledger_rule(path))
    is_baseline = rule is not None and rule.policy == rule_registry.Policy.BASELINE
    return path not in (QUARANTINE_LEDGER, OWN_LEDGER) and not is_baseline


def rebased(states: Sequence[float | None], rises: Sequence[tuple[int, Rise]], rows: Sequence[Rebase]) -> Rebased:
    """Return the states of a ledger and its `Rise` records after some of its rebase rows and `grow` rows.

    The newest change of the key or the format keeps the states and the
    records at that commit and after it (THE REBASE).  A larger scope or a
    `grow` row after it increases each older state by the rows that it does
    not count as growth.  Its own record decreases by the same rows (THE
    GROWTH).
    Complexity: O(s * r) for s states and r rows.
    """
    end = min((row.index + 1 for row in rows if row.cause.change is Change.KEY), default=len(states))
    shifted = list(states[:end])
    named = [item for item in rises if item[0] < end]
    applied = tuple(sorted((row for row in rows if row.index < end), key=lambda row: row.index))
    for row in applied:
        if row.cause.change is Change.KEY:
            continue
        for index in range(row.index + 1, end):
            state = shifted[index]
            if state is not None:
                shifted[index] = state + row.cause.rows
        named = [
            (index, replace(rise, old=rise.old + row.cause.rows) if index == row.index else rise)
            for index, rise in named
        ]
        named = [(index, rise) for index, rise in named if rise.new > rise.old]
    return Rebased(tuple(shifted), tuple(named), applied)


def changes_result(value: float | None, without: Rebased, full: Rebased) -> bool:
    """Return True when a rebase row changes the result (THE REBASE).

    The ledger is then above its lowest count without the row.  That count is lower than its lowest count with each
    row.
    """
    lowest_without, lowest_with = without.lowest, full.lowest
    return (
        value is not None
        and lowest_without is not None
        and value > lowest_without
        and (lowest_with is None or lowest_with > lowest_without)
    )


def rebase_text(row: Rebase, path: str) -> str:
    """Return the sentences that tell the cause of a rebase row or a `grow` row and its effect on the ledger."""
    if row.cause.change is Change.KEY:
        return (
            f"The commit changed the key or the format of the ledger: {row.cause.text}.  The lowest count starts "
            f"again at that commit"
        )
    if row.cause.change is Change.GROW:
        if path == QUARANTINE_LEDGER:
            return f"A grow row admits the growth of the commit: {row.cause.text}.  Each such count gives no warning"
        return (
            f"A grow row admits the growth of the commit: {row.cause.text}.  These {row.cause.rows} rows do not "
            f"count as growth"
        )
    scope = f"The commit made the scope of the rule larger: {row.cause.text}"
    if path == QUARANTINE_LEDGER:
        return f"{scope}.  Each count that it raised in a file that it did not change gives no warning"
    return (
        f"{scope}.  {row.cause.rows} rows of its growth do not count as growth: the rows of files that it did not "
        f"change, and at most the rows that it added in total"
    )


def since_text(applied: Sequence[Rebase]) -> str:
    """Return the words that name the rebase rows and the `grow` rows that apply to a ledger, or '' for none."""
    parts: list[str] = []
    for kind, label in ((REBASE_KIND, "rebase"), (GROW_KIND, "admitted growth")):
        shorts = [row.commit.short for row in applied if row.kind == kind]
        if shorts:
            parts.append(f"the {label}{'s' if kind == REBASE_KIND and len(shorts) > 1 else ''} at {', '.join(shorts)}")
    return f" after {' and '.join(parts)}" if parts else ""


def configuration_counts(ledger: rule_ledger.Ledger) -> dict[str, dict[rule_ledger.Key, int]]:
    """Return the count of each key in each configuration of a version of a ledger, with the base first.

    A ledger of the policy admit counts one for each row, and a ledger with no configuration has one table with the
    name "".  A row with the value admit of a count ledger has no count.
    """
    if ledger.policy == rule_registry.Policy.ADMIT:
        return {"": dict.fromkeys(ledger.base, 1)}
    if not ledger.configurations:
        return {"": ledger.expected()}
    return {item.name: ledger.expected(item.name) for item in ledger.configurations}


def counted_rows(tables: Mapping[str, Mapping[rule_ledger.Key, int]]) -> int:
    """Return the row count of a ledger with the counts of each configuration.

    The first table gives a base row for each count.  Each other table gives a difference row for each count that is
    not the base count.
    """
    names = list(tables)
    if not names:
        return 0
    base = tables[names[0]]
    rows = sum(1 for count in base.values() if count > 0)
    for name in names[1:]:
        table = tables[name]
        rows += sum(1 for key in set(table) | set(base) if table.get(key, 0) != base.get(key, 0))
    return rows


def moved_rows(history: History, commit: git.Commit, path: str, registry: rule_registry.Registry) -> int:
    """Return the rows that a commit added to a ledger with a move (THE MOVE).

    The function counts the rows of the ledger at the commit again, with each paid increase back in the file of its
    fall.  The result is the difference of the two row counts.  A ledger of a policy other than count or admit, and a
    version that does not read, give 0.
    Complexity: one diff of the files with a changed count, and linear in the rows of the two versions.
    """
    rule = registry.rules.get(ledger_rule(path))
    if rule is None or rule.policy not in (rule_registry.Policy.COUNT, rule_registry.Policy.ADMIT):
        return 0
    old_text, new_text = history.text("before", commit.sha, (path,)), history.text("at", commit.sha, (path,))
    if old_text is None or new_text is None:
        return 0
    try:
        before = rule_ledger.parse(old_text, f"{commit.short}^:{path}", rule.id, rule.policy)
        after = rule_ledger.parse(new_text, f"{commit.short}:{path}", rule.id, rule.policy)
    except rows_grammar.RowsError:
        return 0
    old_tables, new_tables = configuration_counts(before), configuration_counts(after)
    files = sorted(
        {
            key.path
            for name, table in new_tables.items()
            if name in old_tables
            for key in set(table) | set(old_tables[name])
            if key.path and table.get(key, 0) != old_tables[name].get(key, 0)
        }
    )
    flows = history.flows(commit.sha, files)
    if not flows:
        return 0
    returned: dict[str, dict[rule_ledger.Key, int]] = {}
    for name, table in new_tables.items():
        counts = dict(table)
        for payment in rule_ledger.move_payments(old_tables[name], table, flows) if name in old_tables else ():
            counts[payment.target] -= payment.count
            counts[payment.source] = counts.get(payment.source, 0) + payment.count
        returned[name] = {key: count for key, count in counts.items() if count}
    return max(0, counted_rows(new_tables) - counted_rows(returned))


def moved_growth(
    history: History, registry: rule_registry.Registry, path: str, rebased_rows: Rebased
) -> dict[int, int]:
    """Return the rows that a move added to each increase after the newest lowest state of a ledger (THE MOVE).

    `rebased_rows` holds the states and the increases after the rebase rows and the `grow` rows.  A move only
    increases an older state.  The newest lowest state with the moves is then not older than the one without them.
    The increases before that state then hold each increase that a move can change.  The rows of an increase are at
    most its growth after the rows of the ledger of this rule.
    """
    lowest = rebased_rows.lowest
    if lowest is None:
        return {}
    newest = next(index for index, value in enumerate(rebased_rows.states) if value is not None and value == lowest)
    commits = history.states.commits
    found: dict[int, int] = {}
    for index, rise in rebased_rows.rises:
        if (
            index < newest
            and (rows := min(moved_rows(history, commits[index], path, registry), int(rise.new - rise.old))) > 0
        ):
            found[index] = rows
    return found


def after_moves(
    states: Sequence[float | None], rises: Sequence[tuple[int, Rise]], moved: Mapping[int, int]
) -> tuple[list[float | None], list[tuple[int, Rise]]]:
    """Return the states and the increases of a ledger with the rows of each move out of its growth (THE MOVE).

    A move at a commit increases each older state by its rows.  The increase of its own commit decreases by the same
    rows, as a `grow` row does (THE GROWTH).  Complexity: O(s * m) for s states and m moves.
    """
    shifted = list(states)
    named = list(rises)
    for index, rows in sorted(moved.items()):
        for older in range(index + 1, len(shifted)):
            state = shifted[older]
            if state is not None:
                shifted[older] = state + rows
        named = [(item, replace(rise, old=rise.old + rows) if item == index else rise) for item, rise in named]
    return shifted, [(item, rise) for item, rise in named if rise.new > rise.old]


def ledger_findings(
    history: History, registry: rule_registry.Registry, rebases: Mapping[str, Sequence[Rebase]]
) -> list[check_report.Finding]:
    """Compare the row count of each ledger with its lowest committed count, after the rebase rows and the `grow` rows.

    Each rebase row or `grow` row that changes the result gives an error of the kind rebase or `grow`, which the row
    admits.

    Complexity: O(c * l * r^2) for c commits, l ledgers and r rebase rows of one ledger.  The reads are linear in the
    bytes of the versions of the ledgers.
    """
    findings: list[check_report.Finding] = []
    for path in history.ledgers:
        if not counts_rows(path, registry):
            continue
        paths = (path, *EARLIER_PATHS.get(path, ()))
        states: list[float | None] = []
        rises: list[tuple[int, Rise]] = []
        for index, commit in enumerate(history.states.commits):
            if not any(item in commit.paths for item in paths):
                states.append(None)
                continue
            new = ledger_rows(history, "at", commit.sha, path)
            old = ledger_rows(history, "before", commit.sha, path) or 0
            states.append(float(new) if new is not None else None)
            if new is not None and new > old:
                rises.append((index, Rise(commit.short, commit.subject, old, new, commit.sha)))
        committed = ledger_rows(history, "head", "", path)
        current = history.row_count(history.text("working", "", (path,)))
        working_rise = (
            Rise(WORKING_TREE, "not committed", committed, current)
            if committed is not None and current is not None and current > committed
            else None
        )
        value = float(current) if current is not None else None
        rows = list(rebases.get(path, ()))
        full = rebased(states, rises, rows)
        if rises_after_lowest(list(full.states), list(full.rises), value, working_rise) is not None:
            moves = moved_growth(history, registry, path, full)
            if moves:
                states, rises = after_moves(states, rises, moves)
                full = rebased(states, rises, rows)
        for row in rows:
            without = rebased(states, rises, [other for other in rows if other is not row])
            if not changes_result(value, without, full):
                continue
            findings.append(
                check_report.Finding(
                    "error",
                    path,
                    0,
                    RULE,
                    f"the ledger holds {current} rows, more than its lowest committed count "
                    f"{number_text(without.lowest or 0)} without the {row.kind} row at {row.commit.short} "
                    f"({row.commit.subject}).  {rebase_text(row, path)}.  A row of {OWN_LEDGER} gives the reason",
                    row.kind,
                    row.commit.short,
                )
            )
        above = rises_after_lowest(list(full.states), list(full.rises), value, working_rise)
        if above is not None:
            lowest, named = above
            raised = "; ".join(rise.described() for rise in named) if named else "by no single commit"
            since = since_text(full.applied)
            findings.append(
                check_report.Finding(
                    "warning",
                    path,
                    0,
                    RULE,
                    f"the ledger holds {current} rows, more than its lowest committed count {number_text(lowest)}{since}.  "
                    f"Rows added: {raised}.  Remove a row when its item is fixed",
                    "ledger-grew",
                    path,
                )
            )
    return findings


def is_guard_code(path: str) -> bool:
    """Return True for a file of the code of the guards: a file of utils/ that is not a ledger and not a plant."""
    return path.startswith(GUARD_CODE) and not path.startswith(NOT_GUARD_CODE)


def key_change(before: Mapping[str, str], after: Mapping[str, str], changed: frozenset[str]) -> str | None:
    """Return how a commit changed the key or the format of a ledger, or None when it did not change them (THE REBASE).

    `before` and `after` hold the text of each file of the ledger at the parent and at the commit.  Complexity: linear
    in the bytes of the texts.
    """
    if set(before) != set(after):
        return (
            f"the files of the ledger changed from {', '.join(sorted(before))} to {', '.join(sorted(after))}, so "
            f"two ledgers joined or the ledger moved to another file"
        )
    if any(rule_ledger.format_row(before[item]) != rule_ledger.format_row(after[item]) for item in before):
        return "the format row of the ledger changed"
    old_keys = frozenset().union(*(rule_ledger.counted_keys(text) for text in before.values()))
    new_keys = frozenset().union(*(rule_ledger.counted_keys(text) for text in after.values()))
    if old_keys and new_keys and not old_keys & new_keys and any(is_guard_code(item) for item in changed):
        return (
            f"no counted key of the {len(old_keys)} rows stayed in the {len(new_keys)} rows, and the commit changed "
            f"the code of the guards"
        )
    return None


def quarantine_keys(history: History, text: str) -> frozenset[tuple[str, ...]]:
    """Return the keys of one version of the quarantine ledger, with the kind names and the include keys of this time.

    A base count gives its file, kind and entity, and a difference row gives
    @NAME before them, as assay.core.ledger.keyed_rows() gives the keys.  A
    version that does not read gives no key.  Complexity: linear in the rows.
    """
    version = history.quarantine_version(text)
    if version is None:
        return frozenset()
    keys: set[tuple[str, ...]] = {key for key in version.counts}
    keys.update((f"{rule_ledger.DELTA_MARK}{name}", *key) for name, table in version.deltas.items() for key in table)
    return frozenset(keys)


def change_cause(history: History, commit: git.Commit, path: str, changed: frozenset[str]) -> Cause | None:
    """Return how a commit changed what a ledger holds, or None when it is no rebase commit (THE REBASE).

    `changed` holds each path that the commit changed.  Complexity: linear in the bytes of the versions of the ledger
    at the commit and at its parent, and O(k log k) for the k new keys.
    """
    paths = (path, *EARLIER_PATHS.get(path, ()))
    before = {item: text for item in paths if (text := history.text("before", commit.sha, (item,))) is not None}
    after = {item: text for item in paths if (text := history.text("at", commit.sha, (item,))) is not None}
    if not before or not after:
        return None
    if path != QUARANTINE_LEDGER:
        text = key_change(before, after, changed)
        if text is not None:
            return Cause(Change.KEY, text, 0, changed)
    if not any(is_guard_code(item) for item in changed):
        return None
    read: Callable[[str], frozenset[tuple[str, ...]]] = (
        partial(quarantine_keys, history) if path == QUARANTINE_LEDGER else rule_ledger.version_keys
    )
    old_keys = frozenset().union(*(read(text) for text in before.values()))
    new_keys = frozenset().union(*(read(text) for text in after.values()))
    outside = sorted(key for key in new_keys - old_keys if (file := rule_ledger.key_path(key)) and file not in changed)
    if not outside:
        return None
    added = sum(history.row_count(text) or 0 for text in after.values()) - sum(
        history.row_count(text) or 0 for text in before.values()
    )
    rows = 0 if path == QUARANTINE_LEDGER else max(0, min(len(outside), added))
    return Cause(
        Change.SCOPE,
        f"it changed the code of the guards, and it added {len(outside)} rows of files that it "
        f"did not change, such as {rule_ledger.key_path(outside[0])}",
        rows,
        changed,
    )


def quarantine_rises(history: History, commit: git.Commit) -> list[LedgerRise]:
    """Return each count of the quarantine ledger that a commit raised, as quarantine_findings() reads the commit.

    A version that does not read, or two versions that do not compare row by row, give no raised count.  Complexity:
    linear in the rows of the two versions.
    """
    before = history.quarantine_version(history.text("before", commit.sha, QUARANTINE_PATHS) or "")
    after = history.quarantine_version(history.text("at", commit.sha, QUARANTINE_PATHS) or "")
    if (
        before is None
        or after is None
        or not before.configurations
        or not after.configurations
        or not comparable(before, after)
    ):
        return []
    renames = Renames(history.changes.get(commit.sha, (frozenset(), {}))[1])
    rises = paid_rises(history, commit.sha, before, after, ledger_rises(before, after, renames, Peaks()))
    return [rise for rise in rises if rise.file != TOTAL]


def grow_cause(history: History, commit: git.Commit, path: str, changed: frozenset[str]) -> Cause | None:
    """Return the growth of a ledger that a `grow` row at a commit admits, or None when it admits none (THE GROWTH).

    The growth holds each row of the commit that a rebase row at a larger
    scope of the commit cannot hold.  A rebase row at a change of the key or
    the format of the ledger starts its lowest count again at the commit, and
    a `grow` row of that commit then admits nothing.
    Complexity: linear in the bytes of the versions of the ledger at the commit and at its parent.
    """
    cause = change_cause(history, commit, path, changed)
    is_scope = cause is not None and cause.change is Change.SCOPE
    if path == QUARANTINE_LEDGER:
        rises = [rise for rise in quarantine_rises(history, commit) if not is_scope or rise.file in changed]
        if not rises:
            return None
        where = " in files that it changed" if is_scope else ""
        return Cause(
            Change.GROW, f"it raised {len(rises)} counts{where}, such as {rises[0].described()}", 0, changed, is_scope
        )
    new = ledger_rows(history, "at", commit.sha, path)
    if new is None:
        return None
    # The reads of a commit hold each file of the ledger.  A parent with no text then has no file of the ledger.
    old = ledger_rows(history, "before", commit.sha, path) or 0
    scope_rows = cause.rows if cause is not None and is_scope else 0
    rows = new - old - scope_rows
    if rows <= 0:
        return None
    where = f", besides the {scope_rows} rows of a larger scope" if scope_rows else ""
    return Cause(Change.GROW, f"it added {rows} rows{where}", rows, changed, is_scope)


def takes_rebase(path: str, registry: rule_registry.Registry) -> bool:
    """Return True when a ledger takes a rebase row: the quarantine ledger, and each ledger of counts_rows()."""
    return path == QUARANTINE_LEDGER or counts_rows(path, registry)


def fetch_changed_paths(root: Path, shas: Iterable[str]) -> dict[str, frozenset[str]]:
    """Return each path that each commit changed (git.changed_paths()), with the git commands in threads.

    Raises:
        NotApplicable: If git fails
    """
    pending = list(dict.fromkeys(shas))
    if not pending:
        return {}
    from concurrent.futures import ThreadPoolExecutor

    try:
        with ThreadPoolExecutor(min(proc.DEFAULT_JOBS, len(pending))) as pool:
            return dict(zip(pending, pool.map(partial(git.changed_paths, root), pending)))
    except git.GitError as failure:
        raise NotApplicable(str(failure)) from None


def row_cause(
    history: History, root: Path, kind: str, commit: git.Commit, path: str, fetched: dict[str, frozenset[str]]
) -> Cause | None:
    """Return the cause of a rebase row (change_cause()) or a `grow` row (grow_cause()) at a commit of a ledger.

    The cause is a function of the commit, because its hash names its files, its parent and each path that it
    changed.  The cache keeps it (THE CACHE).  `fetched` gives the changed paths of some commits, and the function
    adds the paths that it reads.

    Raises:
        NotApplicable: If git fails
    """
    stored = history.store.cause(kind, commit.sha, path) if history.store is not None else MISSING
    if not isinstance(stored, Missing):
        return stored
    if commit.sha not in fetched:
        fetched.update(fetch_changed_paths(root, [commit.sha]))
    changed = fetched[commit.sha]
    cause = (
        grow_cause(history, commit, path, changed)
        if kind == GROW_KIND
        else change_cause(history, commit, path, changed)
    )
    if history.store is not None:
        history.store.keep_cause(kind, commit.sha, path, cause)
    return cause


def uncached_rows(
    history: History, ledger: rule_ledger.Ledger, registry: rule_registry.Registry, index_of: Mapping[str, int]
) -> list[str]:
    """Return the commit of each rebase row and each `grow` row with no cause in the cache, in the sequence of the rows.

    rebase_findings() then reads the changed paths of these commits in threads.  A row that names no such commit of
    its ledger reads no changed path.
    """
    shas: list[str] = []
    for key in sorted(ledger.base):
        index = index_of.get(key.entity)
        if key.kind not in (REBASE_KIND, GROW_KIND) or index is None or key.path not in history.ledgers:
            continue
        commit = history.states.commits[index]
        paths = (key.path, *EARLIER_PATHS.get(key.path, ()))
        is_cached = history.store is not None and history.store.has_cause(key.kind, commit.sha, key.path)
        if takes_rebase(key.path, registry) and any(item in commit.paths for item in paths) and not is_cached:
            shas.append(commit.sha)
    return shas


def rebase_findings(
    history: History, ledger: rule_ledger.Ledger | None, registry: rule_registry.Registry, root: Path
) -> tuple[dict[str, list[Rebase]], list[check_report.Finding]]:
    """Read the rebase rows (THE REBASE) and the `grow` rows (THE GROWTH) of the ledger of this rule.

    Returns the rows of each ledger that name a rebase commit or a growth,
    newest first.  It also returns an error of the kind row for each row that
    names no such commit.

    Raises:
        NotApplicable: If git fails

    Complexity: one `git diff-tree` for each row, and linear in the versions of the ledger.
    """
    applied: dict[str, list[Rebase]] = {}
    errors: list[check_report.Finding] = []
    if ledger is None:
        return applied, errors
    index_of = {commit.short: index for index, commit in enumerate(history.states.commits)}
    fetched = fetch_changed_paths(root, uncached_rows(history, ledger, registry, index_of))
    for key, entry in sorted(ledger.base.items()):
        if key.kind not in (REBASE_KIND, GROW_KIND):
            continue
        paths = (key.path, *EARLIER_PATHS.get(key.path, ()))
        index = index_of.get(key.entity)
        commit = history.states.commits[index] if index is not None else None
        cause: Cause | None = None
        if key.path not in history.ledgers:
            problem = f"the tree holds no ledger {key.path}"
        elif not takes_rebase(key.path, registry):
            problem = (
                f"the rule does not compare the row count of {key.path} with its history.  The ledger of this "
                f"rule and a ledger of the policy baseline have no rebase and no admitted growth"
            )
        elif commit is None or not any(item in commit.paths for item in paths):
            problem = f"the commit {key.entity} did not change {key.path} or an earlier file of it"
        else:
            cause = row_cause(history, root, key.kind, commit, key.path, fetched)
            if key.kind == GROW_KIND:
                problem = (
                    f"the commit {commit.short} ({commit.subject}) did not grow {key.path} with rows that a "
                    f"rebase row at a larger scope cannot hold"
                )
            else:
                problem = (
                    f"the commit {commit.short} ({commit.subject}) did not change the key or the format of "
                    f"{key.path}, and it did not make the scope of the rule larger"
                )
        if index is None or commit is None or cause is None:
            told = (
                "A grow row names a commit that added rows to a ledger or raised a count of the quarantine ledger"
                if key.kind == GROW_KIND
                else "A rebase row names a commit that changed the key or the format of a ledger, or a commit that "
                "changed the code of the guards and added rows of files that it did not change"
            )
            errors.append(
                check_report.Finding(
                    "error",
                    OWN_LEDGER,
                    entry.line,
                    RULE,
                    f"{problem}.  {told}.  Remove the row, or name such a commit",
                    ROW_KIND,
                    str(entry.line),
                )
            )
            continue
        applied.setdefault(key.path, []).append(Rebase(commit, index, cause))
    for rows in applied.values():
        rows.sort(key=lambda row: row.index)
    return applied, errors


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
# The kinds of a raised count that only `guard write --rule quarantine --admit` writes.  Such a raise is no error of
# the kind raise.  A commit gives an error of the kind region-reason when its message does not give the reason of
# the region (THE ADMITTED COUNTS).
ADMITTED_KINDS = frozenset({OPTED_OUT, REGION})
# The kind names of the plugin before format 2 of its report, and the names of this time.
EARLIER_KINDS = {
    "std_entity": "std-entity",
    "std_object": "std-object",
    "c_library_object": "c-library-object",
    "raw_pointer_object": "raw-pointer-object",
    "raw_function_pointer": "raw-function-pointer",
    "c_array_object": "c-array-object",
    "raw_new_delete": "raw-new-delete",
    "c_library_call": "c-library-call",
    "foreign_symbol": "foreign-symbol",
    "compiler_builtin": "compiler-builtin",
    "inline_asm": "inline-asm",
    "assert_expansion": "assert-expansion",
    "layer_header": "layer-header",
    "door_header": "door-header",
    "upward_include": "upward-include",
    "contract_specifier": "contract-specifier",
    "opted_out": OPTED_OUT,
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
    """Return one count key with the kind names and the include keys of this time, also in an opted-out entity.

    The entity of an opted-out count is "REASON | KIND ENTITY", and the inner kind and its entity hold no separator.
    An include entity of an earlier version also holds the layers of its file.  include_key() of the rule quarantine
    removes them.  A version before the change of the key then compares with a version after it.
    """
    kind = current_kind(kind)
    reason, separator, rest = entity.rpartition(REASON_SEPARATOR)
    if kind == OPTED_OUT and separator:
        inner, space, inner_entity = rest.partition(" ")
        inner = current_kind(inner)
        entity = f"{reason}{separator}{inner}{space}{include_key(inner, inner_entity)}"
    else:
        entity = include_key(kind, entity)
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
        return min(
            (
                line
                for (row_name, file, _kind, _entity), line in self.lines.items()
                if file == key[0] and row_name in (name, self.base)
            ),
            default=0,
        )


def is_delta_word(text: str) -> bool:
    """Whether a word is the difference of an earlier format: '+' or '-', and a number above 0 with no leading 0."""
    return (
        len(text) >= 2
        and text[0] in "+-"
        and "1" <= text[1] <= "9"
        and all("0" <= character <= "9" for character in text[2:])
    )


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
    rows = [
        (number, raw.strip())
        for number, raw in enumerate(text.splitlines(), start=1)
        if raw.strip() and not raw.strip().startswith("#")
    ]
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


def renamed_counts(before: QuarantineVersion, after: QuarantineVersion, has_entity: bool = True) -> dict[CountKey, int]:
    """Return the absolute counts of the earlier version, and give its base the name of the base of the next version.

    The base keeps its history when its name changes.
    """
    base = after.base or ""
    return {
        (base if name == before.base else name, file, kind, entity): count
        for (name, file, kind, entity), count in absolute_counts(before, has_entity).items()
    }


# THE CHANGED ROWS
#     absolute_counts() gives each count of each configuration: 55,175 counts
#     for the version of 2026-10-17.  A commit changes a small number of them.
#     Two versions with the same base name and with entities compare only the
#     rows that they change.  The absolute count of a row in a configuration
#     is its base count.  A configuration that is not the base adds its
#     difference (version_count()).
#
#     The changed rows of a configuration are the base rows and its difference
#     rows that change.  Each other row has the same count in the two
#     versions.  ledger_rises() and paid_rises() read only the changed rows,
#     in the sequence of absolute_counts(), and their results stay the same.
#     Two other versions compare each count.


def dict_count(counts: Mapping[CountKey, int], key: CountKey) -> int:
    """Return the count of a key in a table of absolute_counts(), or 0 for a key that the table does not hold."""
    return counts.get(key, 0)


def version_count(version: QuarantineVersion, name: str, row: RowKey) -> int:
    """Return the count of one row of one configuration of a version, as absolute_counts() gives it, else 0.

    Complexity: O(1).
    """
    if name not in version.configurations:
        return 0
    count = version.counts.get(row, 0)
    if name == version.base:
        return count
    return count + version.deltas.get(name, {}).get(row, 0)


def is_counted(version: QuarantineVersion, name: str, row: RowKey) -> bool:
    """Tell if absolute_counts() of a version holds the row of a configuration.

    The base holds each of its rows, also with the count 0.  Each other configuration holds a row with a count of not 0.
    Complexity: O(1).
    """
    if name not in version.configurations:
        return False
    if name == version.base:
        return row in version.counts
    return version_count(version, name, row) != 0


def changed_base_rows(before: QuarantineVersion, after: QuarantineVersion) -> set[RowKey]:
    """Return each base row that one of two versions does not hold with the same count (THE CHANGED ROWS).

    Complexity: linear in the base rows of the two versions.
    """
    return {row for row, _ in before.counts.items() ^ after.counts.items()}


def changed_rows(before: QuarantineVersion, after: QuarantineVersion, name: str, base_rows: set[RowKey]) -> set[RowKey]:
    """Return each row of a configuration with a count that can change between two versions with one base.

    `base_rows` holds the result of changed_base_rows().  Each other row has the same count in the two versions (THE
    CHANGED ROWS).  Complexity: linear in the difference rows of the configuration in the two versions.
    """
    if name == after.base:
        return base_rows
    old, new = before.deltas.get(name, {}), after.deltas.get(name, {})
    return base_rows | {row for row, _ in old.items() ^ new.items()}


def has_shared_base(before: QuarantineVersion, after: QuarantineVersion) -> bool:
    """Tell if two versions compare on their changed rows: they have entities and one base name (THE CHANGED ROWS)."""
    return has_entities(before, after) and before.base is not None and before.base == after.base


def changed_counts(
    before: QuarantineVersion, after: QuarantineVersion, names: Iterable[str]
) -> tuple[dict[CountKey, int], dict[CountKey, int]]:
    """Return the counts of the changed rows of some configurations in two versions with one base (THE CHANGED ROWS).

    The two tables hold a key only where absolute_counts() of the version holds it.  Each key that the two
    tables of absolute_counts() hold with different counts is in the result.  Complexity: linear in the changed rows.
    """
    base_rows = changed_base_rows(before, after)
    old_counts: dict[CountKey, int] = {}
    new_counts: dict[CountKey, int] = {}
    for name in names:
        for row in changed_rows(before, after, name, base_rows):
            key = (name, row[0], row[1], row[2])
            if is_counted(before, name, row):
                old_counts[key] = version_count(before, name, row)
            if is_counted(after, name, row):
                new_counts[key] = version_count(after, name, row)
    return old_counts, new_counts


def has_region_count(version: QuarantineVersion) -> bool:
    """Tell if absolute_counts() of a version holds a count of the kind region.

    Complexity: linear in the rows of the version.
    """
    base = version.base
    if base is None:
        return False
    if any(kind == REGION for _file, kind, _entity in version.counts):
        return True
    return any(
        row[1] == REGION and version_count(version, name, row) != 0
        for name in version.configurations
        if name != base
        for row in version.deltas.get(name, {})
    )


class Peaks:
    """The largest count of each configuration and row in the versions of the quarantine ledger that the walk read.

    The walk starts the peaks again at a version that does not compare with
    the version before it.  A key that no version holds gives 0, and a count
    below 0 also gives 0.  A key then has max(0, the largest count), as a
    fold of absolute_counts() of each version gives it.  fold() reads only the
    rows that changed after the last version that it read, when the two
    versions have one base name.  Each other row keeps its count and its peak.
    """

    __slots__ = ("last", "values")

    def __init__(self, values: dict[CountKey, int] | None = None) -> None:
        """Hold the peaks of each key, or no peak, and no last version."""
        self.values: dict[CountKey, int] = {} if values is None else values
        self.last: QuarantineVersion | None = None

    def get(self, key: CountKey, default: int = 0) -> int:
        """Return the peak of one key, or the default for a key with no count above 0."""
        return self.values.get(key, default)

    def fold(self, version: QuarantineVersion) -> None:
        """Add the counts of one version to the peaks.

        Complexity: linear in the rows that changed after the last version, else linear in the counts of the version.
        """
        last = self.last
        is_shared = last is not None and last.base is not None and last.base == version.base
        base_rows = changed_base_rows(last, version) if last is not None and is_shared else set()
        values = self.values
        for name in version.configurations:
            if last is not None and is_shared and name in last.configurations:
                rows: Iterable[RowKey] = changed_rows(last, version, name, base_rows)
            elif name == version.base:
                rows = version.counts
            else:
                rows = version.counts.keys() | version.deltas.get(name, {}).keys()
            for row in rows:
                count = version_count(version, name, row)
                key = (name, row[0], row[1], row[2])
                if count > values.get(key, 0):
                    values[key] = count
        self.last = version


def move_counts(
    before: QuarantineVersion, after: QuarantineVersion, counted: Sequence[LedgerRise]
) -> tuple[dict[CountKey, int], dict[CountKey, int]]:
    """Return the counts of two versions that paid_rises() compares.

    They are the changed rows of the configurations of the raised counts, else each count (THE CHANGED ROWS).
    """
    if has_shared_base(before, after):
        return changed_counts(before, after, {rise.configuration for rise in counted})
    return renamed_counts(before, after), absolute_counts(after)


def move_files(
    counted: Sequence[LedgerRise], old_counts: Mapping[CountKey, int], new_counts: Mapping[CountKey, int]
) -> list[str]:
    """Return the sorted files of the diff of paid_rises().

    Each raised count gives its file.  A count that decreases in the configuration and the kind of a raised count also
    gives its file.
    """
    wanted = {(rise.configuration, rise.kind) for rise in counted}
    files = {rise.file for rise in counted} | {
        file
        for (name, file, kind, entity), count in old_counts.items()
        if (name, kind) in wanted and count > new_counts.get((name, file, kind, entity), 0)
    }
    return sorted(files)


def flow_paths(before: QuarantineVersion | None, after: QuarantineVersion, rises: Sequence[LedgerRise]) -> list[str]:
    """Return the files of the diff that paid_rises() reads for a commit, or [] when it reads no diff."""
    counted = [rise for rise in rises if rise.file != TOTAL]
    if before is None or not counted or not has_entities(before, after):
        return []
    return move_files(counted, *move_counts(before, after, counted))


def paid_rises(
    history: History, sha: str, before: QuarantineVersion, after: QuarantineVersion, rises: Sequence[LedgerRise]
) -> list[LedgerRise]:
    """Return the increases of a commit less the part that a fall of a file that the commit moved lines from pays.

    The fall and the increase are in the same configuration and have the same kind.  Their entities are equal or agree
    (THE MOVE).  The counts of each increase and each fall are the absolute counts of the configuration.
    An increase that a fall pays in full goes.  The commit "" is the working tree.  Complexity: one diff of the files
    of the increases and the falls, and linear in the counts of the two versions.
    """
    counted = [rise for rise in rises if rise.file != TOTAL]
    if not counted or not has_entities(before, after):
        return list(rises)
    old_counts, new_counts = move_counts(before, after, counted)
    flows = history.flows(sha, move_files(counted, old_counts, new_counts))
    paid: dict[str, dict[rule_ledger.Key, int]] = {}
    for name in {rise.configuration for rise in counted}:
        old_keys = {
            rule_ledger.Key(file, kind, entity): count
            for (row_name, file, kind, entity), count in old_counts.items()
            if row_name == name
        }
        new_keys = {
            rule_ledger.Key(file, kind, entity): count
            for (row_name, file, kind, entity), count in new_counts.items()
            if row_name == name
        }
        paid[name] = rule_ledger.paid_counts(old_keys, new_keys, flows)
    kept: list[LedgerRise] = []
    for rise in rises:
        amount = paid.get(rise.configuration, {}).get(rule_ledger.Key(rise.file, rise.kind, rise.entity), 0)
        if amount == 0:
            kept.append(rise)
        elif amount < rise.new - rise.old:
            kept.append(replace(rise, old=rise.old + amount))
    return kept


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
    return formats_compare(before.format, after.format)


def formats_compare(first: int, second: int) -> bool:
    """Tell if versions of two formats compare row by row: the two count files, or they have one format."""
    return first == second or min(first, second) >= FILE_FORMAT


def has_entities(before: QuarantineVersion, after: QuarantineVersion) -> bool:
    """Whether two comparable versions compare by entity."""
    return min(before.format, after.format) >= ENTITY_FORMAT


@dataclass(frozen=True, slots=True)
class Renames:
    """The files that git reports as moved in one commit: the path before the commit of each file, by its new path."""

    previous: Mapping[str, str] = field(default_factory=dict)

    def source(self, path: str) -> str | None:
        """Return the path before the commit of a moved file, or None for a file that the commit did not move."""
        return self.previous.get(path)


def ledger_rises(
    before: QuarantineVersion, after: QuarantineVersion, renames: Renames, peaks: Peaks
) -> list[LedgerRise]:
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
    * The function reads only the changed rows of two versions with one base
      name and with entities (THE CHANGED ROWS).  Each other row has the same
      count, and no count of it increases.

    Complexity: O(c log c) for c changed rows of two versions with one base.  Else O(r log r) for r counts, and
    O(r * d) for d difference rows when the versions compare by file.
    """
    if not comparable(before, after):
        old_totals, new_totals = kind_totals(before), kind_totals(after)
        return [
            LedgerRise(after.base, TOTAL, kind, "", old_totals.get(kind, 0), count)
            for kind, count in sorted(new_totals.items())
            if count > old_totals.get(kind, 0)
        ]
    keyed = has_entities(before, after)
    counts_opted_out = before.format >= OPTED_OUT_FORMAT
    new_of: Callable[[CountKey], int]
    old_of: Callable[[CountKey], int]
    if has_shared_base(before, after):
        base_rows = changed_base_rows(before, after)
        keys = sorted(
            (name, row[0], row[1], row[2])
            for name in after.configurations
            if name == after.base or name in before.configurations
            for row in changed_rows(before, after, name, base_rows)
            if is_counted(after, name, row)
        )
        counts_regions = has_region_count(before)

        def new_of(key: CountKey) -> int:
            """Return the count of a key in the new version, or 0."""
            return version_count(after, key[0], (key[1], key[2], key[3]))

        def old_of(key: CountKey) -> int:
            """Return the count of a key in the version before the commit, or 0."""
            return version_count(before, key[0], (key[1], key[2], key[3]))
    else:
        old_counts = renamed_counts(before, after, keyed)
        counts_regions = any(kind == REGION for (_name, _file, kind, _entity) in old_counts)
        new_counts = absolute_counts(after, keyed)
        keys = sorted(new_counts)
        new_of = partial(dict_count, new_counts)
        old_of = partial(dict_count, old_counts)
    rises: list[LedgerRise] = []
    for key in keys:
        name, file, kind, entity = key
        new, old = new_of(key), old_of(key)
        if (
            new <= old
            or (name not in before.configurations and name != after.base)
            or (kind == OPTED_OUT and not counts_opted_out)
            or (kind == REGION and not counts_regions)
        ):
            continue
        source = renames.source(file)
        if (
            source is not None
            and old == 0
            and new_of((name, source, kind, entity)) == 0
            and old_of((name, source, kind, entity)) >= new
        ):
            continue
        row = (file, kind, entity)
        if name != after.base and (
            new <= peaks.get(key, 0)
            or difference_of(after, name, row, keyed) <= difference_of(before, name, row, keyed)
        ):
            continue
        rises.append(LedgerRise(name, file, kind, entity, old, new))
    return rises


def rise_line(ledger: QuarantineVersion, rise: LedgerRise) -> int:
    """Return the line of the row of a rise in a ledger.  A rise of a file and kind gives the first row of its file."""
    if rise.entity or ledger.format < ENTITY_FORMAT:
        return int(ledger.line_of(rise.configuration, rise.key))
    lines = [
        line
        for (name, file, kind, _entity), line in ledger.lines.items()
        if (file, kind) == (rise.file, rise.kind) and name in (rise.configuration, ledger.base)
    ]
    return min(lines, default=0)


def changes_machinery(paths: frozenset[str]) -> bool:
    """Whether a commit changed the quarantine plugin or the rule table."""
    return any(path == item or path.startswith(item) for path in paths for item in QUARANTINE_MACHINERY)


def region_reason(rise: LedgerRise) -> str:
    """Return the reason of the region of a raised count of the kind opted-out or region, with one space between words.

    The entity of a region count is the reason, and the entity of an opted-out count is "REASON | KIND ENTITY".
    """
    reason = rise.entity if rise.kind == REGION else rise.entity.rpartition(REASON_SEPARATOR)[0]
    return " ".join(reason.split())


def gives_reason(commit: git.Commit, reason: str) -> bool:
    """Tell if the subject or the body of a commit holds the reason of a region.  The white space can be different.

    The reason starts with its class (CLAUDE.md section XXII, R7).  The message then gives the class too.
    Complexity: linear in the length of the message.
    """
    message = " ".join(f"{commit.subject}\n{commit.body}".split())
    return bool(reason) and reason in message


# One commit of the walk: its hash, each path that it changed, and the files that it moved.
Step = tuple[str, frozenset[str], Renames]


@dataclass(frozen=True, slots=True)
class QuarantineWalk:
    """The state of the walk of quarantine_findings() through the commits of the history (THE CHECKPOINT).

    `findings` holds the errors of the commits that the walk read, and
    `standing` each raised count with its commit.  `stale` and `known` hold
    the configurations other than the base.  `last_format` is the format of
    the last version that the walk read, or None.  Each step of the walk gives
    a new state, and the states share one object `peaks` until the peaks
    start again.  A copy of each peak at each commit costs more than the
    changed rows (Peaks).
    """

    findings: tuple[check_report.Finding, ...] = ()
    peaks: Peaks = field(default_factory=Peaks)
    standing: tuple[tuple[git.Commit, LedgerRise], ...] = ()
    stale: frozenset[str] = frozenset()
    known: frozenset[str] = frozenset()
    last_format: int | None = None

    def prepare(
        self, history: History, commit: git.Commit | None, paths: frozenset[str], renames: Renames
    ) -> tuple[QuarantineWalk, PreparedStep | None]:
        """Read the part of one commit that reads no diff: the peaks, the configurations and the raised counts.

        The function gives the state after this part, and the step that complete() reads.  A commit of only the plugin
        or the rule table gives no step.  A commit with a version that does not read also gives no step.  The moves of
        the commits before a commit do not change its raised counts.  complete() can then read the diffs of each commit
        after this part (THE COLD RUN).  Complexity: linear in the changed rows of the two versions of the commit (THE
        CHANGED ROWS).
        """
        is_machinery = changes_machinery(paths)
        if commit is None:
            return (replace(self, stale=self.stale | self.known) if is_machinery else self), None
        after = history.quarantine_version(history.text("at", commit.sha, QUARANTINE_PATHS))
        before = history.quarantine_version(history.text("before", commit.sha, QUARANTINE_PATHS))
        if after is None:
            return self, None
        resets = before is None or not comparable(before, after)
        starts_peaks = resets or (before is not None and before.format != after.format)
        peaks = Peaks() if starts_peaks else self.peaks
        rises = ledger_rises(before, after, renames, peaks) if before is not None else []
        peaks.fold(after)
        later = frozenset(name for name in after.configurations if name != after.base)
        written = frozenset(
            name
            for name in later
            if before is None
            or after.deltas.get(name) != before.deltas.get(name)
            or after.configurations[name] != before.configurations.get(name)
        )
        stale = (self.stale - written) | (later - written if is_machinery else frozenset())
        walk = replace(self, peaks=peaks, stale=stale, known=later, last_format=after.format)
        return walk, PreparedStep(commit, paths, before, after, rises, resets, self.stale)

    def complete(self, history: History, step: PreparedStep) -> QuarantineWalk:
        """Read the remaining part of one commit after prepare(): the moves, the raised counts that stay, and the errors.

        The function gives the state after the commit.
        """
        commit, after = step.commit, step.after
        rises = (
            paid_rises(history, commit.sha, step.before, after, step.rises) if step.before is not None else step.rises
        )
        kept = () if step.resets else self.standing
        standing = (*kept, *((commit, rise) for rise in rises if rise.file != TOTAL))
        judged = [rise for rise in rises if rise.configuration not in step.stale or rise.configuration == after.base]
        blocking = [rise for rise in judged if rise.kind not in ADMITTED_KINDS]
        unreasoned = [
            rise for rise in judged if rise.kind in ADMITTED_KINDS and not gives_reason(commit, region_reason(rise))
        ]
        if changes_machinery(step.paths):
            return replace(self, standing=standing)
        found: list[check_report.Finding] = []
        if blocking:
            shown = "; ".join(rise.described() for rise in blocking[:SHOWN_RISES])
            more = f"; and {len(blocking) - SHOWN_RISES} more" if len(blocking) > SHOWN_RISES else ""
            found.append(
                check_report.Finding(
                    "error",
                    QUARANTINE_LEDGER,
                    0,
                    RULE,
                    f"the commit {commit.short} ({commit.subject}) raised {len(blocking)} counts of {QUARANTINE_LEDGER}, "
                    f"and it changes neither the plugin of utils/tools/quarantine/ nor the rule table "
                    f"{RULE_TABLE}: {shown}{more}.  A change can only lower a count.  Remove the new findings, "
                    f"or add a row to {rule_ledger.shown_path(RULE)} that gives the commit and the reason",
                    RAISE_KIND,
                    commit.short,
                )
            )
        if unreasoned:
            reasons = sorted({region_reason(rise) for rise in unreasoned})
            shown = "; ".join(rise.described() for rise in unreasoned[:SHOWN_RISES])
            more = f"; and {len(unreasoned) - SHOWN_RISES} more" if len(unreasoned) > SHOWN_RISES else ""
            found.append(
                check_report.Finding(
                    "error",
                    QUARANTINE_LEDGER,
                    0,
                    RULE,
                    f"the commit {commit.short} ({commit.subject}) raised {len(unreasoned)} counts of opted-out or "
                    f"region of {QUARANTINE_LEDGER} through `guard write --admit`, and its message does not give the "
                    f"reason of the region: {shown}{more}.  A region only shrinks.  Give each reason in the message, "
                    f"with its class, as the region spells it ({'; '.join(repr(reason) for reason in reasons)}), or "
                    f"add a row to {rule_ledger.shown_path(RULE)} that gives the commit and the reason",
                    REASON_KIND,
                    commit.short,
                )
            )
        return replace(self, standing=standing, findings=(*self.findings, *found))


@dataclass(frozen=True, slots=True)
class PreparedStep:
    """One commit of the quarantine ledger after QuarantineWalk.prepare(): its versions and its raised counts.

    `resets` tells that the commit starts the raised counts again, and `stale` holds the stale configurations before
    the commit.
    """

    commit: git.Commit
    paths: frozenset[str]
    before: QuarantineVersion | None
    after: QuarantineVersion
    rises: list[LedgerRise]
    resets: bool
    stale: frozenset[str]


@dataclass(frozen=True, slots=True)
class WalkPlan:
    """The commits of the walk, oldest first, the key of the state after each one, and the state that the walk continues.

    The walk reads the commits from the index `start` onward, with `walk` as the state after the commits before it
    (THE CHECKPOINT).
    """

    steps: tuple[Step, ...]
    keys: tuple[bytes, ...]
    start: int = 0
    walk: QuarantineWalk = field(default_factory=QuarantineWalk)


def quarantine_commits(history: History) -> dict[str, git.Commit]:
    """Return each commit of the history that changed the quarantine ledger or its earlier file, by its hash."""
    return {
        commit.sha: commit
        for commit in history.states.commits
        if any(item in commit.paths for item in QUARANTINE_PATHS)
    }


def step_keys(identity: bytes, steps: Sequence[Step]) -> tuple[bytes, ...]:
    """Return the key of the state after each step: a digest of the identity and of each step up to it.

    The state of the walk after a step is a function of the steps up to it.  The hash of a commit names its message,
    its first parent and its files (THE CHECKPOINT).  Complexity: linear in the steps.
    """
    digest = hashlib.sha256(identity)
    keys: list[bytes] = []
    for sha, paths, renames in steps:
        digest.update(json.dumps([sha, sorted(paths), sorted(renames.previous.items())]).encode())
        keys.append(digest.copy().digest())
    return tuple(keys)


def plan_walk(history: History) -> WalkPlan:
    """Return the walk of the quarantine ledger from the newest state that the cache holds, and read its versions.

    The function reads the versions of the remaining steps, of HEAD and of the
    working tree.  Worker processes read the versions that the cache does not
    hold (THE COLD RUN).  Complexity: one lookup for each step from the newest
    one, until a hit.
    """
    if QUARANTINE_LEDGER not in history.ledgers:
        return WalkPlan((), ())
    steps = tuple((sha, paths, Renames(moves)) for sha, (paths, moves) in reversed(list(history.changes.items())))
    store = history.store
    keys = step_keys(store.identity if store is not None else b"", steps)
    start, walk = 0, QuarantineWalk()
    if store is not None:
        commits = quarantine_commits(history)
        for index in range(len(steps) - 1, -1, -1):
            restored = store.checkpoint(keys[index], commits)
            if restored is not None:
                start, walk = index + 1, restored
                break
    texts = [history.text(place, sha, QUARANTINE_PATHS) for sha, _, _ in steps[start:] for place in ("before", "at")]
    texts += [history.text("head", "", QUARANTINE_PATHS), history.text("working", "", QUARANTINE_PATHS)]
    fill_versions(history, [text for text in texts if text is not None])
    return WalkPlan(steps, keys, start, walk)


def read_pooled_version(task: tuple[str, str | None, bytes]) -> QuarantineVersion | None:
    """Read one version of the quarantine ledger in a worker process, and write its entry when the cache is on.

    `task` holds the text, the directory of the cache or None, and the identity of the store.
    """
    text, directory, identity = task
    try:
        version: QuarantineVersion | None = read_quarantine_version(text, QUARANTINE_LEDGER)
    except VersionError:
        version = None
    if directory is not None:
        HistoryStore(Path(directory), identity).keep_version(text, version)
    return version


def fill_versions(history: History, texts: Iterable[str]) -> None:
    """Read each version of the quarantine ledger of the texts that no cache holds, in worker processes (THE COLD RUN).

    Fewer than POOL_FLOOR such texts stay for quarantine_version() in this process.  Complexity: one lookup of each
    text, and one read of each text that the cache does not hold, divided between the workers.
    """
    pending: list[str] = []
    for text in dict.fromkeys(texts):
        if text in history.versions:
            continue
        stored = history.store.version(text) if history.store is not None else MISSING
        if stored is MISSING:
            pending.append(text)
        else:
            history.versions[text] = stored
    if len(pending) < POOL_FLOOR:
        return
    import multiprocessing
    from concurrent.futures import ProcessPoolExecutor

    store = history.store
    tasks = [
        (text, None if store is None else str(store.directory), b"" if store is None else store.identity)
        for text in pending
    ]
    workers = min(proc.DEFAULT_JOBS, POOL_WORKERS, len(tasks))
    with ProcessPoolExecutor(workers, mp_context=multiprocessing.get_context("forkserver")) as pool:
        for text, version in zip(pending, pool.map(read_pooled_version, tasks)):
            history.versions[text] = version


def quarantine_findings(
    history: History, rebases: Sequence[Rebase] = (), plan: WalkPlan | None = None
) -> list[check_report.Finding]:
    """Read each raise of the quarantine ledger in the history and in the working tree.

    * A commit that raised a count and that changes neither the plugin nor the
      rule table gives an error of the kind raise, which a row of the ledger of
      this rule can admit.  A raised count of opted-out or region gives no
      error.
    * A configuration other than the base is stale after a commit of the plugin
      or the rule table that did not write its rows.  A raise of a stale
      configuration is no error.
    * A raised count that stays above its value before the commit gives a
      warning on each run, which names the commit.  When the commit gave the
      reason of its region, a raised count of the kind opted-out or region
      gives no warning (THE ADMITTED COUNTS).
    * A rebase row of the quarantine ledger names a commit that made the scope
      of the rule larger (THE REBASE).  Each count that the commit raised in a
      file that it did not change then gives no warning.  The row admits one
      error of the kind rebase when such a count stays above its value before
      the commit.
    * A `grow` row of the quarantine ledger admits the other counts that one
      commit raised (THE GROWTH), with one error of the kind `grow`.
    * A raise in the working tree gives a warning only.

    Complexity: O(c * r) for c commits of the ledger, the plugin and the rule table, and r counts.
    """
    if QUARANTINE_LEDGER not in history.ledgers:
        return []
    plan = plan_walk(history) if plan is None else plan
    walk = plan.walk
    by_sha = quarantine_commits(history)
    prepared: list[PreparedStep] = []
    for sha, paths, renames in plan.steps[plan.start :]:
        walk, step = walk.prepare(history, by_sha.get(sha), paths, renames)
        if step is not None:
            prepared.append(step)
    history.fill_flows((item.commit.sha, flow_paths(item.before, item.after, item.rises)) for item in prepared)
    for item in prepared:
        walk = walk.complete(history, item)
    if history.store is not None and plan.start < len(plan.steps):
        history.store.keep_checkpoint(plan.keys[-1], walk)
    findings = list(walk.findings)
    path = QUARANTINE_PATHS
    current = history.quarantine_version(history.text("working", "", path))
    if current is None or walk.last_format is None or not formats_compare(walk.last_format, current.format):
        return findings
    file_counts: dict[CountKey, int] | None = None
    by_kind = {kind: {row.commit.sha: row for row in rebases if row.kind == kind} for kind in (REBASE_KIND, GROW_KIND)}
    not_growth: dict[tuple[str, str], int] = {}
    for commit, rise in walk.standing:
        if rise.entity:
            value = version_count(current, rise.configuration, rise.key)
        else:
            file_counts = absolute_counts(current, has_entity=False) if file_counts is None else file_counts
            value = file_counts.get((rise.configuration, *rise.key), 0)
        if value <= rise.old or (rise.kind in ADMITTED_KINDS and gives_reason(commit, region_reason(rise))):
            continue
        rebase, grow = by_kind[REBASE_KIND].get(commit.sha), by_kind[GROW_KIND].get(commit.sha)
        holder = (
            rebase
            if rebase is not None and rise.file not in rebase.cause.changed
            else grow
            if grow is not None and (not grow.cause.is_scope or rise.file in grow.cause.changed)
            else None
        )
        if holder is not None:
            not_growth[(commit.sha, holder.kind)] = not_growth.get((commit.sha, holder.kind), 0) + 1
            continue
        what = f"{rise.kind} {rise.entity}" if rise.entity else rise.kind
        findings.append(
            check_report.Finding(
                "warning",
                QUARANTINE_LEDGER,
                rise_line(current, rise),
                RULE,
                f"{rise.file} {what} in the configuration {rise.configuration} has {value} findings, above "
                f"{rise.old} before the commit {commit.short} ({commit.subject}) raised it to {rise.new}.  Lower it "
                f"again, or add a grow row to {OWN_LEDGER} that gives the reason",
                "standing-raise",
                f"{commit.short} {rise.file}",
            )
        )
    for (sha, kind), count in sorted(not_growth.items()):
        row = by_kind[kind][sha]
        where = "in files that it did not change" if kind == REBASE_KIND else "that a grow row holds"
        findings.append(
            check_report.Finding(
                "error",
                QUARANTINE_LEDGER,
                0,
                RULE,
                f"the commit {row.commit.short} ({row.commit.subject}) raised {count} counts of {QUARANTINE_LEDGER} "
                f"{where}, and each one stays above its value before the commit.  "
                f"{rebase_text(row, QUARANTINE_LEDGER)}.  A row of {OWN_LEDGER} gives the reason",
                kind,
                row.commit.short,
            )
        )
    head = history.quarantine_version(history.text("head", "", path))
    if head is not None and comparable(head, current):
        for rise in paid_rises(history, "", head, current, ledger_rises(head, current, Renames(), walk.peaks)):
            findings.append(
                check_report.Finding(
                    "warning",
                    QUARANTINE_LEDGER,
                    rise_line(current, rise),
                    RULE,
                    f"{WORKING_TREE} (not committed) raises {rise.described()}.  The commit must change the plugin or the "
                    f"rule table, or lower the count again",
                    "working-raise",
                    rise.file,
                )
            )
    return findings


def own_ledger(context: MetaContext) -> rule_ledger.Ledger | None:
    """Return the ledger of this rule in the tree, or None when the registry of the tree has no such rule.

    A ledger that does not read gives None here, and the run of the rule reports it (runner.judged()).
    """
    rule = context.registry.rules.get(RULE)
    if rule is None:
        return None
    try:
        return rule_ledger.read(rule, context.root)
    except rows_grammar.RowsError, OSError:
        return None


def admission_findings(ledger: rule_ledger.Ledger | None) -> list[check_report.Finding]:
    """Refuse a row of the ledger of this rule whose key does not have the form of an admission or a rebase."""
    if ledger is None:
        return []
    found: list[check_report.Finding] = []
    for key, entry in sorted(ledger.base.items()):
        words = key.entity.split(" ")
        commit = words[0] if words else ""
        is_commit = len(commit) == SHORT_HASH and set(commit) <= HEX_DIGITS
        is_threshold = key.path == REGISTRY and key.kind in COLUMNS and len(words) == 2 and is_commit
        is_raise = (
            key.path == QUARANTINE_LEDGER and key.kind in (RAISE_KIND, REASON_KIND) and len(words) == 1 and is_commit
        )
        is_rebase = (
            key.kind in (REBASE_KIND, GROW_KIND)
            and key.path.startswith(f"{LEDGER_DIRECTORY}/")
            and key.path.endswith(".txt")
            and len(words) == 1
            and is_commit
        )
        is_kept = key.path == REGISTRY and key.kind == KEEP_KIND and len(words) == 2 and words[1] in COLUMNS
        if not (is_threshold or is_raise or is_rebase or is_kept):
            found.append(
                check_report.Finding(
                    "error",
                    OWN_LEDGER,
                    entry.line,
                    RULE,
                    f"a row is '{REGISTRY} | warn|error | admit | COMMIT RULE | REASON', '{REGISTRY} | {KEEP_KIND} | "
                    f"admit | RULE warn|error | REASON', '{QUARANTINE_LEDGER} | {RAISE_KIND}|{REASON_KIND} | admit | "
                    f"COMMIT | REASON' or '{LEDGER_DIRECTORY}/RULE.txt | {REBASE_KIND}|{GROW_KIND} | admit | COMMIT | "
                    f"REASON', with a commit of {SHORT_HASH} hexadecimal characters",
                    ROW_KIND,
                    str(entry.line),
                )
            )
    return found


def with_admissions(
    ledger: rule_ledger.Ledger, commit: str, reasons: Mapping[tuple[str, str], str]
) -> rule_ledger.Ledger:
    """Return the ledger of this rule with one row at a commit for each rule and kind of `reasons`, with its reason.

    A kind is REBASE_KIND or GROW_KIND.  The row names the ledger of the rule and the commit in SHORT_HASH
    characters.  A row with the same key gets the new reason.
    """
    rows: dict[rule_ledger.Key, rule_ledger.Entry] = {}
    for (rule_id, kind), reason in reasons.items():
        key = rule_ledger.Key(rule_ledger.shown_path(rule_id), kind, commit[:SHORT_HASH])
        rows[key] = rule_ledger.Entry(key, rule_ledger.ADMIT_VALUE, reason)
    return replace(ledger, base={**ledger.base, **rows})


def growth_kinds(root: Path, registry: rule_registry.Registry, rule_id: str, commit: str) -> tuple[str, ...]:
    """Return the kinds of the rows that admit the growth of the ledger of a rule at a commit (THE GROWTH).

    A rebase row comes first when change_cause() gives a cause: a change of the key or the format, or a larger scope
    (THE REBASE).  A `grow` row holds the rows that a rebase row at a larger scope cannot hold.  A ledger
    that takes no rebase row, and a commit that did not change the ledger, give no kind.  Complexity: one read of the
    history (read_history()), and linear in the versions of the ledger at the commit and at its parent.

    Raises:
        NotApplicable: If the tree is not the top of a git work tree, its history is shallow, or git fails
    """
    path = rule_ledger.shown_path(rule_id)
    if not takes_rebase(path, registry):
        return ()
    history = read_history(root)
    found = next((item for item in history.states.commits if item.sha == commit), None)
    if found is None:
        return ()
    fetched: dict[str, frozenset[str]] = {}
    cause = row_cause(history, root, REBASE_KIND, found, path, fetched)
    kinds = [REBASE_KIND] if cause is not None else []
    if (cause is None or cause.change is Change.SCOPE) and row_cause(
        history, root, GROW_KIND, found, path, fetched
    ) is not None:
        kinds.append(GROW_KIND)
    return tuple(kinds)


def kept_thresholds(ledger: rule_ledger.Ledger | None) -> frozenset[tuple[str, str]]:
    """Return the rule and the field of each keep row of the ledger of this rule (THE KEPT THRESHOLDS)."""
    if ledger is None:
        return frozenset()
    return frozenset(
        (words[0], words[1])
        for key in ledger.base
        if key.path == REGISTRY and key.kind == KEEP_KIND
        for words in [key.entity.split(" ")]
        if len(words) == 2 and words[1] in COLUMNS
    )


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each rise of a threshold or a ledger in the history of the tree, and each raise of the quarantine ledger."""
    history = read_history(context.root)
    registry = context.registry
    ledger = own_ledger(context)
    plan = plan_walk(history)
    rebases, refused = rebase_findings(history, ledger, registry, context.root)
    return (
        threshold_findings(history, registry, kept_thresholds(ledger))
        + quarantine_findings(history, rebases.get(QUARANTINE_LEDGER, ()), plan)
        + ledger_findings(history, registry, rebases)
        + admission_findings(ledger)
        + refused
    )


# ── The cache ──────────────────────────────────────────────────────

CACHE_NAME = "history"
# The version of the format of the entries.  Change it when the shape of an entry changes and no source changes.
CACHE_FORMAT = b"history-entries-1"
# The first bytes of an entry, before the digest of its payload.
ENTRY_MARK = b"HS1"
DIGEST_SIZE = 16
# The record of the import closure of this module in the directory of the cache (THE IDENTITY).
CLOSURE_RECORD = "closure.json"
PACKAGE = "assay"
VERSION_ENTRY = "version"
FLOWS_ENTRY = "flows"
CAUSE_ENTRY = "cause"
TABLE_ENTRY = "table"
WALK_ENTRY = "walk"
# A cold run uses worker processes when it reads this number of versions or more (THE COLD RUN).
POOL_FLOOR = 4
# The largest number of the worker processes of a cold run.  On 2026-10-17, 16 workers read the 83 versions of the
# tree in 0.66 s, and 32 workers took 0.67 s.  This process takes each version from the workers one at a time.
POOL_WORKERS = 16


@dataclass(frozen=True, slots=True)
class ImportClosure:
    """The modules of the package that a module can import, and each imported name that names no file (THE IDENTITY).

    `digests` gives the SHA-256 of the bytes of each module, by its name.  `absent` holds each name that names no file,
    sorted.  The record of the closure in the cache holds the two parts.
    """

    digests: Mapping[str, str]
    absent: tuple[str, ...]


def module_file(base: Path, name: str) -> Path | None:
    """Return the file of a module of the package, or None when no file of the package has the name."""
    stem = base.joinpath(*name.split("."))
    for candidate in (stem.with_suffix(".py"), stem / "__init__.py"):
        if candidate.is_file():
            return candidate
    return None


def import_closure(base: Path, start: str) -> ImportClosure:
    """Return the SHA-256 of each module of the package that a module can import, and each name that names no file.

    The walk reads each import of a module, also an import in a function and
    a relative import.  A name `from X import Y` gives the module X and the
    candidate X.Y, which is a module only when a file has its name.

    Raises:
        SyntaxError: If a module of the closure does not parse
        ValueError: If a module of the closure holds a null byte

    Complexity: one parse of each module of the closure.
    """
    found: dict[str, str] = {}
    absent: set[str] = set()
    pending = [start]
    while pending:
        name = pending.pop()
        if name in found or name in absent:
            continue
        file = module_file(base, name)
        if file is None:
            absent.add(name)
            continue
        data = file.read_bytes()
        found[name] = hashlib.sha256(data).hexdigest()
        package = name if file.name == "__init__.py" else name.rpartition(".")[0]
        for node in ast.walk(ast.parse(data)):
            if isinstance(node, ast.Import):
                pending += [alias.name for alias in node.names if alias.name.split(".")[0] == PACKAGE]
            elif isinstance(node, ast.ImportFrom):
                module = absolute_module(package, node.level, node.module)
                if module is not None and module.split(".")[0] == PACKAGE:
                    pending += [module, *(f"{module}.{alias.name}" for alias in node.names)]
    return ImportClosure(found, tuple(sorted(absent)))


def absolute_module(package: str, level: int, module: str | None) -> str | None:
    """Return the absolute name of the module of a `from` import in a package, or None above the top of the package.

    `level` is the number of dots of a relative import, and 0 for an absolute import.
    """
    if level == 0:
        return module
    parts = package.split(".") if package else []
    if level - 1 > len(parts):
        return None
    named = [*parts[: len(parts) - (level - 1)], *([module] if module else [])]
    return ".".join(named) or None


def recorded_closure(base: Path, record: Path) -> ImportClosure | None:
    """Return the recorded closure, or None when a module changed or a file has a missing name.

    The closure is a function of the bytes of its modules and of the names that
    name no file.  An unchanged record then gives the closure of this time.
    Complexity: one read and one hash of each module of the record.
    """
    try:
        listed = json.loads(record.read_bytes())
        modules, absent = listed["modules"], listed["absent"]
        for name, digest in modules.items():
            file = module_file(base, name)
            if file is None or hashlib.sha256(file.read_bytes()).hexdigest() != digest:
                return None
        if any(module_file(base, name) is not None for name in absent):
            return None
        digests = {str(name): str(digest) for name, digest in modules.items()}
        return ImportClosure(digests, tuple(str(name) for name in absent))
    except OSError, ValueError, TypeError, KeyError, AttributeError:
        return None


def code_identity(directory: Path) -> bytes:
    """Return the digest of the code that gives each entry: the modules of the import closure of this module.

    The record of the closure in the cache saves the walk of THE IDENTITY when no module changes.

    Raises:
        SyntaxError: If a module of the closure does not parse, for example a file that an editor writes
        ValueError: If a module holds a null byte, or the walk finds no file of this module
        OSError: If a module cannot be read
    """
    base = REPO_ROOT / "utils"
    record = directory / CLOSURE_RECORD
    closure = recorded_closure(base, record)
    if closure is None:
        closure = import_closure(base, __name__)
        if __name__ not in closure.digests:
            raise ValueError(f"the walk of the imports finds no file of the module {__name__} under {base}")
        listed = {"modules": dict(closure.digests), "absent": list(closure.absent)}
        with suppress(OSError):
            cache.write_atomic(record, json.dumps(listed, sort_keys=True).encode())
    digest = hashlib.sha256(CACHE_FORMAT)
    for name in sorted(closure.digests):
        digest.update(f"{name}\0{closure.digests[name]}\0".encode())
    return digest.digest()


def encode_entry(value: object) -> bytes:
    """Return the bytes of an entry: the mark, the digest of the payload, and the compressed JSON of the value."""
    payload = zlib.compress(json.dumps(value, separators=(",", ":")).encode(), 1)
    return ENTRY_MARK + hashlib.blake2b(payload, digest_size=DIGEST_SIZE).digest() + payload


def decode_entry(data: bytes) -> object | Missing:
    """Return the value of the bytes of an entry, or MISSING for bytes that a write of encode_entry() did not give."""
    start = len(ENTRY_MARK) + DIGEST_SIZE
    payload = data[start:]
    if (
        not data.startswith(ENTRY_MARK)
        or hashlib.blake2b(payload, digest_size=DIGEST_SIZE).digest() != data[len(ENTRY_MARK) : start]
    ):
        return MISSING
    try:
        value: object = json.loads(zlib.decompress(payload))
    except ValueError, zlib.error:
        return MISSING
    return value


def version_plain(version: QuarantineVersion) -> list[object]:
    """Return a version of the quarantine ledger as JSON values, in the sequence of each table of the version."""
    return [
        version.format,
        [[name, list(facts.items())] for name, facts in version.configurations.items()],
        [[*row, count] for row, count in version.counts.items()],
        [[name, [[*row, count] for row, count in table.items()]] for name, table in version.deltas.items()],
        [[*key, line] for key, line in version.lines.items()],
    ]


def plain_version(value: object) -> QuarantineVersion:
    """Return the version that version_plain() gave as JSON values.

    Raises:
        ValueError: If the value does not have the shape of version_plain()
        TypeError: If a part of the value has a different type
    """
    if not isinstance(value, list) or len(value) != 5 or not isinstance(value[0], int):
        raise ValueError("the entry does not hold a version")
    number, configurations, counts, deltas, lines = value
    return QuarantineVersion(
        number,
        {str(name): {str(key): str(fact) for key, fact in facts} for name, facts in configurations},
        {(file, kind, entity): int(count) for file, kind, entity, count in counts},
        {
            str(name): {(file, kind, entity): int(count) for file, kind, entity, count in table}
            for name, table in deltas
        },
        {(name, file, kind, entity): int(line) for name, file, kind, entity, line in lines},
    )


class HistoryStore:
    """The cache "history" of assay.core.cache for one run (THE CACHE).

    The name of an entry is the SHA-256 of the identity of the code (THE
    IDENTITY), the kind of the entry and its key.  A changed input gives a
    different name, and an entry does not become stale.
    """

    __slots__ = ("directory", "identity")

    def __init__(self, directory: Path, identity: bytes) -> None:
        """Hold the directory of the cache and the identity of the code and of git."""
        self.directory = directory
        self.identity = identity

    @classmethod
    def open(cls, root: Path) -> HistoryStore | None:
        """Return the store of a run in a git work tree, or None when the caches are off or the cache cannot open.

        The cache does not open when git gives no version, or when its directory cannot be made.  A module of THE
        IDENTITY that does not parse also stops it.  The call keeps the cache in the limit of its row of LIMITS when
        its turn is due.
        """
        try:
            directory = cache.cache_root(CACHE_NAME)
            if directory is None:
                return None
            version = git.version(root).encode()
            cache.hold_bound(directory, cache.LIMITS[CACHE_NAME])
            python = f"{sys.version_info[0]}.{sys.version_info[1]}".encode()
            return cls(directory, hashlib.sha256(code_identity(directory) + version + b"\0" + python).digest())
        except OSError, ValueError, SyntaxError, git.GitError:
            return None

    def entry(self, kind: str, key: bytes) -> Path:
        """Return the path of the entry of one kind and key."""
        digest = hashlib.sha256(self.identity)
        digest.update(kind.encode() + b"\0" + key)
        name = digest.hexdigest()
        return self.directory / name[:2] / name

    def load(self, kind: str, key: bytes) -> object | Missing:
        """Return the value of an entry, or MISSING when the cache holds no entry or a damaged one."""
        entry = self.entry(kind, key)
        try:
            used = entry.stat().st_mtime
            value = decode_entry(entry.read_bytes())
        except OSError:
            return MISSING
        if value is not MISSING:
            cache.mark_used(entry, used)
        return value

    def has(self, kind: str, key: bytes) -> bool:
        """Tell if the cache holds an entry of one kind and key, with no read of it."""
        return self.entry(kind, key).is_file()

    def save(self, kind: str, key: bytes, value: object) -> None:
        """Write an entry.  A write that fails writes no entry, and a subsequent run calculates the value again."""
        with suppress(OSError):
            cache.write_atomic(self.entry(kind, key), encode_entry(value))

    def version(self, text: str) -> QuarantineVersion | None | Missing:
        """Return the version of the quarantine ledger that a text gives, None for a text that does not read, or MISSING."""
        value = self.load(VERSION_ENTRY, text_key(text))
        if value is MISSING or value is None:
            return value
        try:
            return plain_version(value)
        except ValueError, TypeError:
            return MISSING

    def keep_version(self, text: str, version: QuarantineVersion | None) -> None:
        """Write the version of a text, or None for a text that does not read."""
        self.save(VERSION_ENTRY, text_key(text), None if version is None else version_plain(version))

    def flows(self, key: tuple[str, tuple[str, ...]]) -> frozenset[tuple[str, str]] | None:
        """Return the pairs of files of THE MOVE of a commit and its set of paths, or None for no usable entry."""
        value = self.load(FLOWS_ENTRY, json.dumps([key[0], list(key[1])]).encode())
        if not isinstance(value, list):
            return None
        try:
            return frozenset((str(source), str(target)) for source, target in value)
        except ValueError, TypeError:
            return None

    def keep_flows(self, key: tuple[str, tuple[str, ...]], flows: frozenset[tuple[str, str]]) -> None:
        """Write the pairs of files of THE MOVE of a commit and its set of paths."""
        self.save(FLOWS_ENTRY, json.dumps([key[0], list(key[1])]).encode(), sorted(flows))

    def cause(self, kind: str, sha: str, path: str) -> Cause | None | Missing:
        """Return the cause of a rebase row or a `grow` row at a commit, None for no cause, or MISSING."""
        value = self.load(CAUSE_ENTRY, json.dumps([kind, sha, path]).encode())
        if value is MISSING or value is None:
            return value
        if not isinstance(value, list) or len(value) != 5 or not isinstance(value[3], list):
            return MISSING
        try:
            return Cause(
                Change(value[0]),
                str(value[1]),
                int(value[2]),
                frozenset(str(item) for item in value[3]),
                bool(value[4]),
            )
        except ValueError, TypeError:
            return MISSING

    def keep_cause(self, kind: str, sha: str, path: str, cause: Cause | None) -> None:
        """Write the cause of a rebase row or a `grow` row at a commit, or None for no cause."""
        value = (
            None
            if cause is None
            else [cause.change.value, cause.text, cause.rows, sorted(cause.changed), cause.is_scope]
        )
        self.save(CAUSE_ENTRY, json.dumps([kind, sha, path]).encode(), value)

    def table(self, path: str, text: str) -> dict[str, Row] | None:
        """Return the thresholds of one version of a table of TABLES, or None for no usable entry."""
        value = self.load(TABLE_ENTRY, path.encode() + b"\0" + text_key(text))
        if not isinstance(value, dict):
            return None
        try:
            return {str(rule): Row(float(warn), float(error), str(unit)) for rule, (warn, error, unit) in value.items()}
        except ValueError, TypeError:
            return None

    def keep_table(self, path: str, text: str, rows: Mapping[str, Row]) -> None:
        """Write the thresholds of one version of a table of TABLES."""
        value = {rule: [row.warn, row.error, row.unit] for rule, row in rows.items()}
        self.save(TABLE_ENTRY, path.encode() + b"\0" + text_key(text), value)

    def has_cause(self, kind: str, sha: str, path: str) -> bool:
        """Tell if the cache holds an entry of the cause of a row at a commit, with no read of it."""
        return self.has(CAUSE_ENTRY, json.dumps([kind, sha, path]).encode())

    def checkpoint(self, key: bytes, commits: Mapping[str, git.Commit]) -> QuarantineWalk | None:
        """Return the state of the walk after the steps of a key, or None for no usable entry (THE CHECKPOINT).

        `commits` gives each commit of the quarantine ledger by its hash.  A state that names a different commit is no
        usable entry.
        """
        if not self.has(WALK_ENTRY, key):
            return None
        value = self.load(WALK_ENTRY, key)
        if value is MISSING:
            return None
        try:
            return plain_walk(value, commits)
        except ValueError, TypeError, KeyError:
            return None

    def keep_checkpoint(self, key: bytes, walk: QuarantineWalk) -> None:
        """Write the state of the walk after the steps of a key."""
        self.save(WALK_ENTRY, key, walk_plain(walk))


def walk_plain(walk: QuarantineWalk) -> list[object]:
    """Return a state of the walk as JSON values, in the sequence of each field of the state.

    The findings and the raised counts keep their sequence.  The peaks keep their values and no last version.  The
    next fold then reads each count of its version, and the values stay as an unbroken walk gives them (Peaks).
    """
    return [
        [
            [item.level, item.path, item.line, item.check, item.message, item.kind, item.entity]
            for item in walk.findings
        ],
        [[*key, value] for key, value in walk.peaks.values.items()],
        [
            [commit.sha, rise.configuration, rise.file, rise.kind, rise.entity, rise.old, rise.new]
            for commit, rise in walk.standing
        ],
        sorted(walk.stale),
        sorted(walk.known),
        walk.last_format,
    ]


def plain_walk(value: object, commits: Mapping[str, git.Commit]) -> QuarantineWalk:
    """Return the state of the walk that walk_plain() gave as JSON values.

    Raises:
        ValueError: If the value does not have the shape of walk_plain(), or a finding has a field that is not valid
        TypeError: If a part of the value has a different type
        KeyError: If a finding has a level that is not known, or a raised count names a commit that `commits` does not
            hold
    """
    if not isinstance(value, list) or len(value) != 6:
        raise ValueError("the entry does not hold a state of the walk")
    findings, peaks, standing, stale, known, last_format = value
    if not (
        isinstance(findings, list)
        and isinstance(peaks, list)
        and isinstance(standing, list)
        and isinstance(stale, list)
        and isinstance(known, list)
    ):
        raise TypeError("the entry does not hold a state of the walk")
    if last_format is not None and not isinstance(last_format, int):
        raise TypeError("the format of the last version is not a number")
    levels = {level: level for level in check_report.LEVELS}
    return QuarantineWalk(
        tuple(
            check_report.Finding(levels[level], str(where), int(line), str(check), str(message), str(kind), str(entity))
            for level, where, line, check, message, kind, entity in findings
        ),
        Peaks(
            {(str(name), str(file), str(kind), str(entity)): int(count) for name, file, kind, entity, count in peaks}
        ),
        tuple(
            (commits[sha], LedgerRise(str(name), str(file), str(kind), str(entity), int(old), int(new)))
            for sha, name, file, kind, entity, old, new in standing
        ),
        frozenset(str(name) for name in stale),
        frozenset(str(name) for name in known),
        last_format,
    )


def text_key(text: str) -> bytes:
    """Return the key of a text in the cache: the SHA-256 of its UTF-8 bytes."""
    return hashlib.sha256(text.encode("utf-8", "surrogatepass")).digest()


# ── The plants ─────────────────────────────────────────────────────

COMMITS = "commits.txt"
WORK = "work"
# The entity "{commit N}" of a row of the ledger of this rule in the working tree of a plant names commit N.
COMMIT_NAME_START = "{commit "
COMMIT_NAME_END = "}"


def plant_context(plant: Path, scratch: Path) -> MetaContext:
    """Build the history of a plant in a throwaway repository, and give its context.

    A plant holds commits.txt, whose rows are `commit N | SUBJECT | BODY`, and
    one directory N for each commit with each file of the tree at that
    commit.  A directory work, when it exists, is the working tree after the
    last commit.  A plant cannot know the hash of a commit.  The ledger of
    this rule in the directory work names commit N with the entity
    "{commit N}" (name_commits()).

    Raises:
        NotApplicable: If commits.txt does not obey its form, or a ledger names a commit that the plant does not have
    """
    try:
        rows = rows_grammar.read(plant / COMMITS, f"{plant.name}/{COMMITS}")
    except (rows_grammar.RowsError, OSError) as problem:
        raise NotApplicable(f"the plant has no history: {problem}") from None
    scratch.mkdir(parents=True)
    git.init(scratch)
    hashes: dict[str, str] = {}
    for row in rows:
        if row.words[0] != "commit" or len(row.words) != 2 or len(row.cells) != 2:
            raise NotApplicable(f"{plant.name}/{COMMITS}:{row.line}: a row is 'commit N | SUBJECT | BODY'")
        replace_tree(scratch, plant / row.words[1])
        hashes[row.words[1]] = git.commit_all(scratch, row.cells[0], row.cells[1])
    if (plant / WORK).is_dir():
        replace_tree(scratch, plant / WORK)
        name_commits(scratch, hashes)
    return MetaContext(scratch)


def name_commits(root: Path, hashes: Mapping[str, str]) -> None:
    """Give each entity "{commit N}" of the ledger of this rule in a planted tree the hash of commit N in 9 characters.

    Raises:
        NotApplicable: If the ledger does not read, or an entity names a commit that the plant does not have
    """
    if not (root / OWN_LEDGER).is_file():
        return
    try:
        ledger = rule_ledger.read(rule_ledger.rule_of(RULE), root)
    except rows_grammar.RowsError as problem:
        raise NotApplicable(f"the planted ledger does not read: {problem}") from None
    base: dict[rule_ledger.Key, rule_ledger.Entry] = {}
    for key, entry in ledger.base.items():
        name = key.entity.removeprefix(COMMIT_NAME_START).removesuffix(COMMIT_NAME_END)
        if not key.entity.startswith(COMMIT_NAME_START) or not key.entity.endswith(COMMIT_NAME_END):
            base[key] = entry
            continue
        if name not in hashes:
            raise NotApplicable(f"the planted ledger names the commit {name}, and the plant has no such commit")
        named = replace(key, entity=hashes[name][:9])
        base[named] = replace(entry, key=named)
    rule_ledger.store(replace(ledger, base=base), root)


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
