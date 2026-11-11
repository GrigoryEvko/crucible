"""The ledgers of the rules: utils/ledgers/RULE.txt, one reader and one writer for each policy.

THE FILE
    A ledger obeys the rows grammar of assay.core.rows:

        # The head comment (THE HEAD COMMENT).
        format 1
        rule RULE
        reason PREFIX | REASON
        PATH | KIND | VALUE | ENTITY | REASON

    The key of a row is PATH, KIND and ENTITY, and a key has one row.  The
    cell "-" is empty: a finding with no kind or no entity has "-" there.  A
    rule with the policy none has no ledger, and a ledger with no rows has no
    file.  A ledger holds one set of rows, and these rows judge each build of
    its rule.

    PATH is a path relative to the source root, a path of the build directory
    with the prefix @build/, or "-" for a row that names no file.  Key.path
    is "" for such a row.

THE HEAD COMMENT
    The writer of a ledger owns its head comment.  A writer with a head
    text of its own gives the ledger that text on each write, with
    replace(ledger, comment=TEXT).  A head with an incorrect fact then
    goes at the next write.  Each other write keeps the head of
    the file, and a new ledger gets NEW_COMMENT.  Examples of a writer with
    a head text are the writers of the measured rules
    (assay.guard.engines.buildsys.cost_budgets) and of the rule
    instruction-total (assay.guard.engines.buildsys.cost_totals).

THE POLICIES
    A ledger fails only on a growth.  A value above its row is an error, and a
    value below its row passes.  A complete write lowers such a row (THE
    WRITE), and the landing gate makes the complete write of each counted
    ledger (THE REDUCE of assay.loop.gate).
    count     VALUE is a positive count.  A key with more findings than its
              row, or a file with a finding and no row, is an error.  A key
              with fewer findings than its row gives no finding.  REASON is
              optional.
    admit     VALUE is the word admit.  A finding with no row is an error.  A
              row that admits no finding gives no finding.  REASON is
              necessary.
    max       VALUE is a number in the unit of the rule.  The rule judges
              each value against its row, so compare() does not read this
              policy.  REASON is necessary.
    baseline  VALUE is a number, or BYTES/READERS.  A number with only
              digits reads as an int, which is accurate to 10^18.  A number
              with a decimal point reads as a float.  The rule judges its sum
              against the row, so compare() does not read this policy.
    A finding that a row of the policy count or admit admits gives no
    output.  `guard list --path FILE` prints the rows of a file.  A count row
    with a reason is a keep row: its reason tells why its findings stay.

THE REASON OF A PREFIX
    A row `reason PREFIX | REASON` gives the reason of each count row of a
    path that PREFIX holds, in a ledger of the policy count.  A PREFIX with
    "/" as its last character holds each path that starts with it.  Each
    other PREFIX holds only the path that is equal to it.  When two rows hold
    a path, the longer PREFIX gives the reason.  The reason of a row of its
    own comes before the reason of a prefix (kept_reason()).

    A count row that a reason row holds is a keep row, and the policy count
    applies to its counts.  The REASON names the step of the plan that
    removes the findings.  The reason rows come after the row of the rule
    and before the first entry row.  A reason row that holds no count row is
    an error.  A write removes each reason row that holds no count row of its
    result.  The reason rows are header rows.

A MODULE OUTSIDE THE ENGINES
    A legacy module that reads its own ledger gets its rule with rule_of(),
    reads the rows with read() and writes them with store().  A self-test
    gives read() and store() the path of a planted ledger.

AN ADMIT ROW OF A COUNT LEDGER
    A row of a count ledger can have the value admit.  It admits each
    finding of its key, as a row of the policy admit does, and its REASON is
    necessary.  A row that admits no finding of the build gives no finding.
    A complete write removes such a row.  A write keeps each other row with
    the value admit, and it adds no such row.

THE WRITE
    write() gives the ledger of a set of findings in one mode:
      lower   the default.  It refuses a count above its row, and a finding
              with no row.
      raise   it also writes a count above its row.  Use it only when the
              checker changed what it reports, and give the reason in the
              commit.
      admit   it also writes a count above its row, and a row for a finding
              with no row, each with the reason of the call.
    The counts of a write are complete or partial.  The counts of a rule that
    reads the tree are complete.  The largest count of each key over the
    builds of a reduce is complete too (THE REDUCE).  A complete write lowers
    each count to its new value, and it removes each row that admits nothing.

    The counts of one build are partial, because one build cannot see the
    findings of a different build.  A partial write lowers no row, and it
    removes no row.  A count of the build above its row replaces the row only
    when the mode permits the increase.  So only the landing gate lowers the
    rows of a ledger that a build gives.

    A write reads no git history.  A change that moves a finding to a key
    with no row, such as a different file, raises the count of that key.
    Write it with the mode raise for the policy count, or with the mode
    admit and a reason for the policy admit.  Give the reason in the commit.

THE REDUCE
    The count of a key of a ledger that a build gives is the largest count of
    the key over the configurations of the landing gate.  The gate gives one
    write a build of each such configuration (assay.loop.gate).  The write
    takes the largest count of each key over these builds (largest_counts()),
    and it is a complete write.  It lowers each row above that count, and it
    removes each row whose largest count is 0.

`guard write --rule ID` writes the ledger of a rule.  `guard selftest
tests.core.test_ledger` does a test of the module.
"""

import enum
from collections import Counter
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass, field, replace
from pathlib import Path

from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT
from assay.core.registry import Policy, Rule

FORMAT = "1"
LEDGER_DIRECTORY = "utils/ledgers"
ADMIT_VALUE = "admit"
COUNT_MAXIMUM = 10**9
# The head comment of a ledger that write() makes.
NEW_COMMENT = (
    "# The ledger of the rule {rule}.  `python3 utils/bin/guard explain {rule}` tells the rule and its "
    "policy,\n# and `python3 utils/bin/guard write --rule {rule}` writes this file.\n"
)
# The first word of a row that gives the reason of each count row that a prefix holds (THE REASON OF A PREFIX).
REASON_WORD = "reason"


class LedgerError(ValueError):
    """A ledger does not obey its schema, or a write refuses its findings."""


class WriteMode(enum.StrEnum):
    """The mode of a write (THE WRITE)."""

    LOWER = "lower"
    RAISE = "raise"
    ADMIT = "admit"


@dataclass(frozen=True, slots=True, order=True)
class Key:
    """The key of one ledger row: a path, a kind and an entity, each "" when it is empty."""

    path: str
    kind: str
    entity: str


@dataclass(frozen=True, slots=True)
class Entry:
    """One row: its key, its value (a count, the word admit, a number or BYTES/READERS), its reason and its line."""

    key: Key
    value: int | float | str
    reason: str
    line: int = 0


@dataclass(frozen=True, slots=True)
class PrefixReason:
    """One row `reason PREFIX | REASON`: the prefix, the reason and the line (THE REASON OF A PREFIX)."""

    prefix: str
    reason: str
    line: int = 0

    def holds(self, path: str) -> bool:
        """Return True when the prefix holds a path (THE REASON OF A PREFIX)."""
        return path.startswith(self.prefix) if self.prefix.endswith("/") else path == self.prefix


@dataclass(frozen=True, slots=True)
class Ledger:
    """The rows of the ledger of one rule.

    `base` holds the entry rows by their keys, and `reasons` holds the reason rows (THE REASON OF A PREFIX).
    """

    rule: str
    policy: Policy
    comment: str
    base: Mapping[Key, Entry] = field(default_factory=dict)
    reasons: tuple[PrefixReason, ...] = ()

    def expected(self) -> dict[Key, int]:
        """Return the count of each row of a count ledger, with no row of the value admit (AN ADMIT ROW OF A COUNT
        LEDGER).  Complexity: linear in the rows."""
        return base_counts(self.base)

    def admits(self) -> dict[Key, Entry]:
        """Return each row with the value admit of a count ledger (AN ADMIT ROW OF A COUNT LEDGER)."""
        if self.policy != Policy.COUNT:
            return {}
        return {key: entry for key, entry in self.base.items() if entry.value == ADMIT_VALUE}

    def reason_of(self, key: Key) -> str:
        """Return the reason of the row of a key, or "" for a key with no row and for a row with no reason."""
        entry = self.base.get(key)
        return entry.reason if entry is not None else ""

    def prefix_reason(self, path: str) -> PrefixReason | None:
        """Return the reason row with the longest prefix that holds a path, or None (THE REASON OF A PREFIX).

        Complexity: linear in the reason rows.
        """
        holding = [item for item in self.reasons if item.holds(path)]
        return max(holding, key=lambda item: len(item.prefix)) if holding else None

    def kept_reason(self, key: Key) -> str:
        """Return the reason that keeps the findings of a key: of its own row (reason_of()), else of a prefix."""
        own = self.reason_of(key)
        if own:
            return own
        found = self.prefix_reason(key.path)
        return found.reason if found is not None else ""

    def count_keys(self) -> set[Key]:
        """Return the key of each count row: each row with a count of a count ledger."""
        return set(base_counts(self.base)) if self.policy == Policy.COUNT else set()

    def idle_reasons(self) -> list[PrefixReason]:
        """Return each reason row that holds no count row (THE REASON OF A PREFIX).

        Complexity: linear in the reason rows times the count rows.
        """
        paths = {key.path for key in self.count_keys()}
        return [item for item in self.reasons if not any(item.holds(path) for path in paths)]

    def line_of(self, key: Key) -> int:
        """Return the line of the row of a key, else the line of the first row of its path, else 0.

        A new entity of a file then points at the file.  Complexity: O(1) for a key with a row, else linear in the rows.
        """
        entry = self.base.get(key)
        if entry is not None:
            return entry.line
        return min((item.line for item in self.base.values() if item.key.path == key.path), default=0)


def ledger_path(rule_id: str, root: Path = REPO_ROOT) -> Path:
    """Return the file of the ledger of a rule."""
    return root / LEDGER_DIRECTORY / f"{rule_id}.txt"


def shown_path(rule_id: str) -> str:
    """Return the file of the ledger of a rule, relative to the source root."""
    return f"{LEDGER_DIRECTORY}/{rule_id}.txt"


def empty(rule: Rule) -> Ledger:
    """Return the ledger of a rule with no rows and the head comment of a new file."""
    return Ledger(rule.id, rule.policy, NEW_COMMENT.format(rule=rule.id))


def base_counts(base: Mapping[Key, Entry]) -> dict[Key, int]:
    """Return the count of each row of a count ledger, with no row of the value admit."""
    return {key: int(entry.value) for key, entry in base.items() if entry.value != ADMIT_VALUE}


def parse_value(policy: Policy, text: str, row: rows_grammar.Row) -> int | float | str:
    """Read the VALUE cell of an entry row for a policy.

    A row of the policy count can also have the value admit (AN ADMIT ROW OF A COUNT LEDGER).

    Raises:
        rows_grammar.RowsError: If the value does not have the form of the policy
    """
    if policy == Policy.COUNT:
        if text == ADMIT_VALUE:
            return ADMIT_VALUE
        count = rows_grammar.number(text, COUNT_MAXIMUM, row, "count")
        if count == 0:
            raise row.error("a count row has a count above 0.  Remove a row with no finding")
        return count
    if policy == Policy.ADMIT:
        if text != ADMIT_VALUE:
            raise row.error(
                f"the value of a row of the policy admit is the word {ADMIT_VALUE}, and this row has {text!r}"
            )
        return ADMIT_VALUE
    if policy == Policy.MAX:
        return rows_grammar.decimal(text, row, "value")
    if policy == Policy.BASELINE:
        bytes_text, slash, readers_text = text.partition("/")
        if slash:
            rows_grammar.number(bytes_text, COUNT_MAXIMUM * COUNT_MAXIMUM, row, "byte count")
            rows_grammar.number(readers_text, COUNT_MAXIMUM, row, "reader count")
            return text
        if text.isascii() and text.isdigit():
            # A sum of bytes or instructions is an integer.  A float holds an integer accurately only to 2^53.
            return rows_grammar.number(text, COUNT_MAXIMUM * COUNT_MAXIMUM, row, "baseline")
        return rows_grammar.decimal(text, row, "baseline")
    raise row.error(f"a rule with the policy {policy} has no ledger")


def parse(text: str, shown: str, rule_id: str, policy: Policy) -> Ledger:
    """Read the text of the ledger of one rule.

    Raises:
        rows_grammar.RowsError: If a line does not obey the grammar or the schema, or a key has a second row
    """
    all_rows = rows_grammar.parse(text, shown)
    comment = rows_grammar.head_comment(text)
    if (
        len(all_rows) < 2
        or all_rows[0].words != ("format", FORMAT)
        or all_rows[1].words != ("rule", rule_id)
        or all_rows[0].cells
        or all_rows[1].cells
    ):
        raise rows_grammar.RowsError(
            shown,
            all_rows[0].line if all_rows else 1,
            f"a ledger starts with the rows 'format {FORMAT}' and 'rule {rule_id}'",
        )
    base: dict[Key, Entry] = {}
    reasons: dict[str, PrefixReason] = {}
    is_header = True
    for row in all_rows[2:]:
        if len(row.words) == 2 and row.words[0] == REASON_WORD:
            reasons_row(row, policy, is_header, reasons)
            continue
        is_header = False
        if len(row.words) != 1 or len(row.cells) != 4:
            raise row.error("an entry row is 'PATH | KIND | VALUE | ENTITY | REASON', and a path has no space")
        key = Key(path_of_head(row.words[0]), row.cells[0], row.cells[2])
        if key in base:
            raise row.error(f"the key {key} has a second row.  The first is at line {base[key].line}")
        reason = row.cells[3]
        value = parse_value(policy, row.cells[1], row)
        if (policy in (Policy.ADMIT, Policy.MAX) or value == ADMIT_VALUE) and not reason:
            words = f" with the value {ADMIT_VALUE}" if policy == Policy.COUNT else ""
            raise row.error(f"a row of the policy {policy}{words} gives a reason")
        base[key] = Entry(key, value, reason, row.line)
    ledger = Ledger(rule_id, policy, comment, base, tuple(reasons.values()))
    idle = ledger.idle_reasons()
    if idle:
        raise rows_grammar.RowsError(
            shown,
            idle[0].line,
            f"the reason row of {idle[0].prefix} holds no count row.  Remove it, or write the ledger again with "
            f"`python3 utils/bin/guard write --rule {rule_id}`",
        )
    return ledger


def reasons_row(row: rows_grammar.Row, policy: Policy, is_header: bool, reasons: dict[str, PrefixReason]) -> None:
    """Read one row `reason PREFIX | REASON` into `reasons` (THE REASON OF A PREFIX).

    Raises:
        rows_grammar.RowsError: If the row does not obey THE REASON OF A PREFIX.  The ledger must have the policy
            count, and the row must come before the first entry row.  It must have one reason, and a new prefix
    """
    if policy != Policy.COUNT:
        raise row.error(f"only a ledger of the policy count has a reason row, and this ledger has the policy {policy}")
    if not is_header:
        raise row.error("a reason row comes after the row of the rule and before the first entry row")
    if len(row.cells) != 1 or not row.cells[0]:
        raise row.error("a reason row is 'reason PREFIX | REASON', with a reason")
    prefix = row.words[1]
    if prefix in reasons:
        raise row.error(f"the prefix {prefix} has a second reason row.  The first is at line {reasons[prefix].line}")
    reasons[prefix] = PrefixReason(prefix, row.cells[0], row.line)


def path_of_head(head: str) -> str:
    """Return the PATH of the head of an entry row: "" for the word "-"."""
    return "" if head == rows_grammar.EMPTY_CELL else head


def head_of_path(path: str) -> str:
    """Return the head word of the PATH of an entry row: "-" for a row that names no file."""
    return path or rows_grammar.EMPTY_CELL


def rule_of(rule_id: str) -> Rule:
    """Return the rule of the registry with an ID, for a legacy module that reads and writes its own ledger.

    Raises:
        LedgerError: If the registry has no such rule, or the rule has the policy none
        rows_grammar.RowsError: If the registry does not obey its schema
        OSError: If the registry cannot be read
    """
    from assay.core import registry as rule_registry

    rule = rule_registry.load().rules.get(rule_id)
    if rule is None:
        raise LedgerError(f"the registry {rule_registry.REGISTRY_PATH} has no rule {rule_id}")
    if rule.policy == Policy.NONE:
        raise LedgerError(f"the rule {rule_id} has the policy none, and such a rule has no ledger")
    return rule


def read(rule: Rule, root: Path = REPO_ROOT, file: Path | None = None) -> Ledger:
    """Read the ledger of a rule, or give an empty ledger when the rule has no file.

    Args:
        rule: The rule
        root: The source root of the file utils/ledgers/RULE.txt
        file: The file to read in place of that file, for a planted ledger of a self-test

    Raises:
        rows_grammar.RowsError: If the file does not obey its schema
        OSError: If the file exists and cannot be read
    """
    path = file if file is not None else ledger_path(rule.id, root)
    if not path.is_file():
        return empty(rule)
    shown = shown_path(rule.id) if file is None else str(file)
    return parse(path.read_text(encoding="utf-8"), shown, rule.id, rule.policy)


def value_text(value: int | float | str) -> str:
    """Return a value as the text of its cell, with no '.0' for a whole number."""
    if isinstance(value, float):
        return str(int(value)) if value.is_integer() else repr(value)
    return str(value)


def render(ledger: Ledger) -> str:
    """Return the text of a ledger: the head comment, the header rows and the entry rows, sorted.

    Complexity: O(r log r) for r rows.
    """
    header = [rows_grammar.render(["format", FORMAT]), rows_grammar.render(["rule", ledger.rule])]
    header += [
        rows_grammar.render([REASON_WORD, item.prefix], [item.reason])
        for item in sorted(ledger.reasons, key=lambda item: item.prefix)
    ]
    entries = [
        (
            (key.path, key.kind, key.entity),
            rows_grammar.render(
                [head_of_path(key.path)], [key.kind, value_text(entry.value), key.entity, entry.reason]
            ),
        )
        for key, entry in ledger.base.items()
    ]
    return rows_grammar.compose(ledger.comment, header, entries)


def store(ledger: Ledger, root: Path = REPO_ROOT, file: Path | None = None) -> bool:
    """Write a ledger, or remove its file when it has no row.  Return True when the file changed.

    Args:
        ledger: The ledger
        root: The source root of the file utils/ledgers/RULE.txt
        file: The file to write in place of that file, for a planted ledger of a self-test

    Raises:
        OSError: If the file cannot be written or removed
    """
    path = file if file is not None else ledger_path(ledger.rule, root)
    if not ledger.base:
        if path.exists():
            path.unlink()
            return True
        return False
    data = render(ledger).encode("utf-8")
    try:
        if path.read_bytes() == data:
            return False
    except FileNotFoundError:
        pass
    from assay.core import cache

    cache.write_atomic(path, data)
    return True


def key_of(finding: check_report.Finding) -> Key:
    """Return the ledger key of a finding."""
    return Key(finding.path, finding.kind, finding.entity)


def described(key: Key) -> str:
    """Return the kind and the entity of a key as words for a message."""
    parts = [part for part in (key.kind, key.entity) if part]
    return " ".join(parts) if parts else "the finding"


def compare(rule: Rule, ledger: Ledger, findings: Sequence[check_report.Finding]) -> list[check_report.Finding]:
    """Judge the findings of a rule against its ledger (THE POLICIES).

    The errors of the rule are its violations.  A warning of the rule passes
    with no change.  A finding that a row admits, a count below its row and a
    row that admits no finding give no finding (THE POLICIES).  Complexity:
    O(f log f + r) for f findings and r rows.

    Raises:
        LedgerError: If the policy of the rule is max or baseline, which the rule judges itself
    """
    if rule.policy in (Policy.MAX, Policy.BASELINE):
        raise LedgerError(f"the rule {rule.id} judges its ledger of the policy {rule.policy} itself")
    errors = [finding for finding in findings if finding.level == "error"]
    result = [finding for finding in findings if finding.level != "error"]
    if rule.policy == Policy.NONE:
        return result + errors
    by_key: dict[Key, list[check_report.Finding]] = {}
    for finding in errors:
        by_key.setdefault(key_of(finding), []).append(finding)
    if rule.policy != Policy.COUNT:
        return result + [item for key, items in sorted(by_key.items()) if key not in ledger.base for item in items]
    expected = ledger.expected()
    admits = ledger.admits()
    held = {key.path for key in expected} | {key.path for key in admits}
    for key, items in sorted(by_key.items()):
        allowed = expected.get(key, 0)
        if key in admits or len(items) <= allowed:
            continue
        said = (
            "and the ledger holds no row of the path.  A new path has no finding"
            if key.path not in held
            else f"and the ledger admits {allowed}.  A change can only lower a count"
        )
        result += [
            replace(
                finding, message=f"{finding.message}.  The path has {len(items)} findings of {described(key)}, {said}"
            )
            for finding in items
        ]
    return result


def largest_counts(builds: Iterable[Mapping[Key, int]]) -> dict[Key, int]:
    """Return the largest count of each key over the counts of some builds, with no zero count (THE REDUCE).

    Complexity: linear in the counts of the builds.
    """
    largest: dict[Key, int] = {}
    for counts in builds:
        for key, count in counts.items():
            if count > largest.get(key, 0):
                largest[key] = count
    return largest


def write(
    rule: Rule,
    ledger: Ledger,
    findings: Sequence[check_report.Finding],
    mode: WriteMode = WriteMode.LOWER,
    reason: str | None = None,
    is_partial: bool = False,
) -> Ledger:
    """Return the ledger that admits the error findings of a rule (THE WRITE).

    Args:
        rule: The rule
        ledger: The ledger of the rule
        findings: The findings of the rule
        mode: The mode of the write
        reason: The reason of each new row and each raised count, necessary in the mode admit
        is_partial: The findings are those of one build, and the write lowers no row (THE WRITE)

    Raises:
        LedgerError: If the mode refuses a finding, the policy has no write here, or a necessary reason is missing

    Complexity: O(f log f + r) for f findings and r rows.
    """
    counts = Counter(key_of(finding) for finding in findings if finding.level == "error")
    return write_counts(rule, ledger, counts, mode, reason, is_partial)


def write_counts(
    rule: Rule,
    ledger: Ledger,
    counts: Mapping[Key, int],
    mode: WriteMode = WriteMode.LOWER,
    reason: str | None = None,
    is_partial: bool = False,
) -> Ledger:
    """Return the ledger that admits the count of each key, as write() does for the error findings of a rule.

    A module that counts its findings itself, such as the rule quarantine,
    writes its ledger with this function.  A partial write keeps each row,
    and it raises a row only in the mode raise or admit (THE WRITE).

    Raises:
        LedgerError: If the mode refuses a count, the policy has no write here, or a necessary reason is missing

    Complexity: O(k log k + r) for k counts and r rows.
    """
    if rule.policy in (Policy.NONE, Policy.MAX, Policy.BASELINE):
        raise LedgerError(f"the rule {rule.id} has the policy {rule.policy}, and its own module writes such a ledger")
    if mode == WriteMode.ADMIT and not reason:
        raise LedgerError("a write in the mode admit gives a reason with --reason TEXT")
    counts = {key: count for key, count in counts.items() if count > 0}
    base = ledger.base
    if rule.policy == Policy.ADMIT:
        new_keys = sorted(key for key in counts if key not in base)
        if new_keys and mode != WriteMode.ADMIT:
            raise LedgerError(
                f"{len(new_keys)} findings of {rule.id} have no row, such as {new_keys[0].path} "
                f"({described(new_keys[0])}).  Fix them, or admit them with --admit --reason TEXT"
            )
        kept = {key: entry for key, entry in base.items() if is_partial or key in counts}
        kept.update({key: Entry(key, ADMIT_VALUE, reason or "") for key in new_keys})
        return needed_reasons(replace(ledger, base=kept))
    # The rows with the value admit have no count (AN ADMIT ROW OF A COUNT LEDGER).  A write adds no such row, and a
    # complete write keeps each one that admits a finding.
    admits = {key: entry for key, entry in base.items() if entry.value == ADMIT_VALUE}
    kept_admits = {key: entry for key, entry in admits.items() if is_partial or counts.get(key, 0) > 0}
    counts = {key: count for key, count in counts.items() if key not in admits}
    expected = base_counts(base)
    rises = sorted(key for key, count in counts.items() if count > expected.get(key, 0))
    if rises and mode == WriteMode.LOWER:
        first = rises[0]
        raise LedgerError(
            f"{len(rises)} counts of {rule.id} rose, such as {first.path} ({described(first)}) from "
            f"{expected.get(first, 0)} to {counts[first]}.  Remove the new findings.  Give --raise only when the "
            f"checker changed what it reports or a change moved a finding to a key with no row, or --admit with a "
            f"reason"
        )
    written = {
        key: Entry(key, count, base[key].reason if key in base else (reason or "")) for key, count in counts.items()
    }
    if is_partial:
        for key, entry in base.items():
            if key in admits:
                continue
            if key not in written or int(entry.value) > int(written[key].value):
                written[key] = entry
    return needed_reasons(replace(ledger, base={**written, **kept_admits}))


def needed_reasons(ledger: Ledger) -> Ledger:
    """Return the ledger with no reason row that holds no count row (THE REASON OF A PREFIX)."""
    idle = ledger.idle_reasons()
    return replace(ledger, reasons=tuple(item for item in ledger.reasons if item not in idle)) if idle else ledger
