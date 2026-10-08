"""The ledgers of the rules: utils/ledgers/RULE.txt, one reader and one writer for each policy.

THE FILE
    A ledger obeys the rows grammar of assay.core.rows:

        # The head comment.  The module keeps it on each write.
        format 1
        rule RULE
        configuration NAME KEY VALUE
        PATH | KIND | VALUE | ENTITY | REASON
        @NAME | PATH | KIND | DELTA | ENTITY | REASON

    The key of a row is PATH, KIND and ENTITY, and a key has one row.  The
    cell "-" is empty: a finding with no kind or no entity has "-" there.  A
    rule with the policy none has no ledger, and a ledger with no rows has no
    file.

    PATH is a path relative to the source root, a path of the build directory
    with the prefix @build/, or "-" for a row that names no file.  Key.path
    is "" for such a row, in a base row and in a difference row.  A head
    @NAME with no "/" starts a difference row, so the head of a base row of a
    file of the build directory, @build/PATH, is never a difference row.

THE POLICIES
    count     VALUE is a positive count.  A key with more findings than its
              row, or a file with a finding and no row, is an error.  A key
              with fewer findings than its row is an error too, until write()
              writes the ledger again, so the ledger keeps no slack.  REASON
              is optional.
    admit     VALUE is the word admit.  A finding with no row is an error.  A
              row that admits no finding is an error.  REASON is necessary.
    max       VALUE is a number in the unit of the rule.  The rule judges
              each value against its row, so compare() does not read this
              policy.  REASON is necessary.
    baseline  VALUE is a number, or BYTES/READERS.  The rule judges its sum
              against the row, so compare() does not read this policy.
    A row of the policy count or admit that admits a finding gives a warning
    on each run, one warning for each PATH, so each admitted item stays in
    the output of the rule.  The text of the warning tells the action of the
    policy: a count falls when its findings go, and an admit row goes with
    the finding that it admits.  A count row with a reason is a keep row: its
    reason tells why its findings stay.  The text of a keep row gives that
    reason as the cause, and it does not tell the reader to remove a finding.
    The text of a count row with no reason tells the reader to remove a
    finding and to lower its count.

A MODULE OUTSIDE THE ENGINES
    A legacy module that reads its own ledger gets its rule with rule_of(),
    reads the rows with read() and writes them with store().  A self-test
    gives read() and store() the path of a planted ledger.

THE CONFIGURATIONS
    A rule whose findings change with the build has configuration rows.  The
    first configuration is the base.  A row that starts with @NAME holds the
    difference of the configuration NAME from the base count, for the policy
    count.  So a change that lowers a count in each configuration writes only
    the base rows.  write() writes the rows of one configuration, and
    import_rows() puts the rows of a configuration that only a CI leg builds
    into the ledger, from the lines that rows_of_configuration() gives.

THE PART OF NO CONFIGURATION
    A base row of a count ledger can have the value admit.  Such a row
    belongs to no configuration, and no difference row names its key.  It
    admits the findings of its key in each configuration, as a row of the
    policy admit does, and its REASON is necessary.  A row that admits no
    finding of the build is an error.  A write removes such a row, keeps
    each other row with the value admit, and adds no such row.

THE WRITE
    write() gives the ledger of a set of findings in one mode:
      lower   the default.  It refuses a count above its row, and a finding
              with no row.  It lowers each count and removes each row that
              admits nothing.
      raise   it also writes a count above its row.  Use it only when the
              checker changed what it reports, and give the reason in the
              commit.
      admit   it also writes a count above its row, and a row for a finding
              with no row, each with the reason of the call.
    A file that git reports as moved (assay.core.git.moves) keeps the rows of
    its old path.

    A write of the base keeps the difference rows of each other
    configuration.  Each count of such a configuration then follows the
    change of its base count, and no count goes below 0.  With keep_others,
    each count of each other configuration stays the same, and the write
    calculates its difference rows again.

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
DELTA_MARK = "@"
COUNT_MAXIMUM = 10**9
# The head comment of a ledger that write() makes.
NEW_COMMENT = ("# The ledger of the rule {rule}.  `python3 utils/bin/guard explain {rule}` tells the rule and its "
               "policy,\n# and `python3 utils/bin/guard write --rule {rule}` writes this file.\n")
# The mark of each row that rows_of_configuration() prints, so that import_rows() finds it in a log.
ROW_MARK = "ledger-row: "


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
    """One row: its key, its value (a count, a difference, the word admit, a number or BYTES/READERS), its reason
    and its line."""

    key: Key
    value: int | float | str
    reason: str
    line: int = 0


@dataclass(frozen=True, slots=True)
class Configuration:
    """One configuration: its name and its facts, as KEY VALUE pairs."""

    name: str
    facts: tuple[tuple[str, str], ...]


@dataclass(frozen=True, slots=True)
class Ledger:
    """The rows of the ledger of one rule.

    `deltas` holds the difference rows of each configuration other than the
    base, for the policy count.
    """

    rule: str
    policy: Policy
    comment: str
    configurations: tuple[Configuration, ...] = ()
    base: Mapping[Key, Entry] = field(default_factory=dict)
    deltas: Mapping[str, Mapping[Key, Entry]] = field(default_factory=dict)

    @property
    def base_name(self) -> str | None:
        """The name of the base configuration, or None for a ledger with no configuration."""
        return self.configurations[0].name if self.configurations else None

    def configuration(self, name: str) -> Configuration | None:
        """Return the configuration with a name, or None."""
        return next((item for item in self.configurations if item.name == name), None)

    def expected(self, name: str | None = None) -> dict[Key, int]:
        """Return the count of each key of one configuration, with no zero count, for the policy count.

        A row with the value admit has no count (THE PART OF NO CONFIGURATION).  Complexity: linear in the rows of
        the base and of the configuration.
        """
        counts = base_counts(self.base)
        if name is None or name == self.base_name:
            return counts
        for key, entry in self.deltas.get(name, {}).items():
            counts[key] = counts.get(key, 0) + int(entry.value)
        return {key: count for key, count in counts.items() if count != 0}

    def admits(self) -> dict[Key, Entry]:
        """Return each base row with the value admit of a count ledger (THE PART OF NO CONFIGURATION)."""
        if self.policy != Policy.COUNT:
            return {}
        return {key: entry for key, entry in self.base.items() if entry.value == ADMIT_VALUE}

    def reason_of(self, key: Key, name: str | None = None) -> str:
        """Return the reason of the row of a key: of its difference row in a configuration, else of its base row.

        A difference row with no reason takes the reason of its base row, because the base row says why the key stays.
        The result is "" for a key with no row and for a row with no reason.
        """
        delta = self.deltas.get(name, {}).get(key) if name is not None else None
        base = self.base.get(key)
        return (delta.reason if delta is not None else "") or (base.reason if base is not None else "")

    def line_of(self, key: Key, name: str | None = None) -> int:
        """Return the line of the row of a key: the difference row, else the base row, else the first row of its path.

        The first row of its path is a base row or a difference row of the configuration, so a new entity of a file
        points at the file.  Complexity: O(1) for a key with a row, else linear in the rows.
        """
        delta_table = self.deltas.get(name, {}) if name is not None else {}
        entry = delta_table.get(key) or self.base.get(key)
        if entry is not None:
            return entry.line
        return min((item.line for item in [*self.base.values(), *delta_table.values()] if item.key.path == key.path),
                   default=0)


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
    """Return the count of each base row of a count ledger, with no row of the value admit."""
    return {key: int(entry.value) for key, entry in base.items() if entry.value != ADMIT_VALUE}


def difference_table(counts: Mapping[Key, int], base: Mapping[Key, int], old: Mapping[Key, Entry],
                     reason: str = "") -> dict[Key, Entry]:
    """Return the difference rows of a configuration with a count for each key, against the base counts.

    A row keeps the reason of the row of its key in `old`, and a new row gets
    `reason`.  A key with the base count has no row.  Complexity: linear in
    the keys.
    """
    return {key: Entry(key, counts.get(key, 0) - base.get(key, 0), old[key].reason if key in old else reason)
            for key in set(counts) | set(base) if counts.get(key, 0) != base.get(key, 0)}


def parse_value(policy: Policy, text: str, row: rows_grammar.Row) -> int | float | str:
    """Read the VALUE cell of a base row for a policy.

    A row of the policy count can also have the value admit (THE PART OF NO CONFIGURATION).

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
            raise row.error(f"the value of a row of the policy admit is the word {ADMIT_VALUE}, and this row has "
                            f"{text!r}")
        return ADMIT_VALUE
    if policy == Policy.MAX:
        return rows_grammar.decimal(text, row, "value")
    if policy == Policy.BASELINE:
        bytes_text, slash, readers_text = text.partition("/")
        if slash:
            rows_grammar.number(bytes_text, COUNT_MAXIMUM * COUNT_MAXIMUM, row, "byte count")
            rows_grammar.number(readers_text, COUNT_MAXIMUM, row, "reader count")
            return text
        return rows_grammar.decimal(text, row, "baseline")
    raise row.error(f"a rule with the policy {policy} has no ledger")


def parse_delta(text: str, row: rows_grammar.Row) -> int:
    """Read the DELTA cell of a difference row: +N or -N, with N above 0.

    Raises:
        rows_grammar.RowsError: If the cell does not have that form
    """
    if len(text) < 2 or text[0] not in "+-":
        raise row.error(f"a difference is +N or -N, and this row has {text!r}")
    magnitude = rows_grammar.number(text[1:], COUNT_MAXIMUM, row, "difference")
    if magnitude == 0:
        raise row.error("a difference row has a difference other than 0.  Remove the row")
    return magnitude if text[0] == "+" else -magnitude


def parse(text: str, shown: str, rule_id: str, policy: Policy) -> Ledger:
    """Read the text of the ledger of one rule.

    Raises:
        rows_grammar.RowsError: If a line does not obey the grammar or the schema, a key has a second row, or a
            count of a configuration is below 0
    """
    all_rows = rows_grammar.parse(text, shown)
    comment = rows_grammar.head_comment(text)
    if len(all_rows) < 2 or all_rows[0].words != ("format", FORMAT) or all_rows[1].words != ("rule", rule_id) \
            or all_rows[0].cells or all_rows[1].cells:
        raise rows_grammar.RowsError(shown, all_rows[0].line if all_rows else 1,
                                     f"a ledger starts with the rows 'format {FORMAT}' and 'rule {rule_id}'")
    configurations: list[Configuration] = []
    facts: dict[str, dict[str, str]] = {}
    base: dict[Key, Entry] = {}
    deltas: dict[str, dict[Key, Entry]] = {}
    is_header = True
    for row in all_rows[2:]:
        if row.words[0] == "configuration" and not row.cells:
            if not is_header or len(row.words) != 4 or not rows_grammar_name(row.words[1]):
                raise row.error("a configuration row is 'configuration NAME KEY VALUE', with a lowercase NAME, "
                                "before the first entry row")
            name = row.words[1]
            if name not in facts:
                facts[name] = {}
                configurations.append(Configuration(name, ()))
            if row.words[2] in facts[name]:
                raise row.error(f"the configuration {name} has a second row for {row.words[2]}")
            facts[name][row.words[2]] = row.words[3]
            continue
        is_header = False
        if len(row.words) != 1:
            raise row.error("an entry row is 'PATH | KIND | VALUE | ENTITY | REASON', and a path has no space")
        if is_delta_head(row.words[0]):
            name = row.words[0][1:]
            if policy != Policy.COUNT or name not in facts or name == configurations[0].name or len(row.cells) != 5:
                raise row.error("a difference row is '@NAME | PATH | KIND | +N|-N | ENTITY | REASON', of the policy "
                                "count, for a configuration other than the base")
            key = Key(row.cells[0], row.cells[1], row.cells[3])
            table = deltas.setdefault(name, {})
            if key in table:
                raise row.error(f"the key {key} has a second row in the configuration {name}")
            table[key] = Entry(key, parse_delta(row.cells[2], row), row.cells[4], row.line)
            continue
        if len(row.cells) != 4:
            raise row.error("an entry row is 'PATH | KIND | VALUE | ENTITY | REASON'")
        key = Key(path_of_head(row.words[0]), row.cells[0], row.cells[2])
        if key in base:
            raise row.error(f"the key {key} has a second row.  The first is at line {base[key].line}")
        reason = row.cells[3]
        value = parse_value(policy, row.cells[1], row)
        if (policy in (Policy.ADMIT, Policy.MAX) or value == ADMIT_VALUE) and not reason:
            words = f" with the value {ADMIT_VALUE}" if policy == Policy.COUNT else ""
            raise row.error(f"a row of the policy {policy}{words} gives a reason")
        base[key] = Entry(key, value, reason, row.line)
    ordered = tuple(Configuration(item.name, tuple(facts[item.name].items())) for item in configurations)
    ledger = Ledger(rule_id, policy, comment, ordered, base, deltas)
    admits = ledger.admits()
    for table in deltas.values():
        for key, entry in table.items():
            if key in admits:
                raise rows_grammar.RowsError(shown, entry.line, f"the difference row of {key} names the key of a row "
                                                                f"with the value {ADMIT_VALUE}, which belongs to no "
                                                                f"configuration")
    for item in ordered[1:]:
        if any(count < 0 for count in ledger.expected(item.name).values()):
            raise rows_grammar.RowsError(shown, 0, f"a difference of the configuration {item.name} makes a count "
                                                   f"below 0")
    return ledger


def is_delta_head(head: str) -> bool:
    """Whether the head of an entry row starts a difference row: @NAME with no "/" (THE FILE)."""
    return head.startswith(DELTA_MARK) and "/" not in head


def path_of_head(head: str) -> str:
    """Return the PATH of the head of a base row: "" for the word "-", as the cell "-" of a difference row is."""
    return "" if head == rows_grammar.EMPTY_CELL else head


def head_of_path(path: str) -> str:
    """Return the head word of the PATH of a base row: "-" for a row that names no file."""
    return path or rows_grammar.EMPTY_CELL


def rows_grammar_name(text: str) -> bool:
    """Whether a text is the name of a configuration: lowercase letters, digits, '_', '.' and '-', from a letter."""
    return bool(text) and "a" <= text[0] <= "z" and all(
        "a" <= character <= "z" or "0" <= character <= "9" or character in "_.-" for character in text)


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
    """Return the text of a ledger: the head comment, the header rows, the base rows and the difference rows, sorted.

    Complexity: O(r log r) for r rows.
    """
    header = [rows_grammar.render(["format", FORMAT]), rows_grammar.render(["rule", ledger.rule])]
    header += [rows_grammar.render(["configuration", item.name, key, value]) for item in ledger.configurations
               for key, value in item.facts]
    # The first item of a key puts each base row before each difference row.
    entries = [(("0", key.path, key.kind, key.entity),
                rows_grammar.render([head_of_path(key.path)], [key.kind, value_text(entry.value), key.entity,
                                                               entry.reason]))
               for key, entry in ledger.base.items()]
    entries += [(("1", name, key.path, key.kind, key.entity),
                 rows_grammar.render([f"{DELTA_MARK}{name}"], [key.path, key.kind, f"{int(entry.value):+d}",
                                                               key.entity, entry.reason]))
                for name, table in ledger.deltas.items() for key, entry in table.items()]
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
    if not ledger.base and not any(ledger.deltas.values()) and not ledger.configurations:
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


def admitted_text(rule: Rule, path: str, total: int, shown: str, by_admit: int = 0, by_reason: int = 0) -> str:
    """Return the message of the warning of one path with admitted findings, with the action of the policy.

    `by_admit` is the number of the findings that a row with the value admit admits (THE PART OF NO CONFIGURATION).
    `by_reason` is the number of the findings that keep rows count: count rows with a reason (THE POLICIES).
    """
    admit = (f"{path} has {total} findings that rows of the ledger admit: {shown}.  Each row gives its reason, and a "
             f"row goes in the change that removes its finding.  `python3 utils/bin/guard explain {rule.id}` gives the "
             f"rule")
    if rule.policy != Policy.COUNT or by_admit == total:
        return admit
    command = f"`python3 utils/bin/guard write --rule {rule.id}`"
    admit_part = (f".  Rows with the value admit admit {by_admit} of them, each with its reason, and such a row goes in "
                  f"the change that removes its finding") if by_admit else ""
    if by_admit + by_reason == total:
        return (f"{path} has {total} findings that rows of the ledger keep, each with its reason: {shown}.  The reason of "
                f"each row tells why its findings stay.  A count decreases only in the change that removes one of its "
                f"findings, and that change writes the ledger with {command}{admit_part}")
    counted = (f"{path} keeps {total} findings that the ledger counts: {shown}.  Remove a finding, and lower its count "
               f"in the same change with {command}")
    if by_reason:
        counted += f".  Keep rows count {by_reason} of them, and the reason of each such row tells why its findings stay"
    return counted + admit_part


def admitted_warnings(rule: Rule, admitted: Mapping[str, Counter[str]], lines: Mapping[str, int],
                      by_admit: Mapping[str, int] | None = None,
                      by_reason: Mapping[str, int] | None = None) -> list[check_report.Finding]:
    """Return one warning for each path with admitted findings: the number of findings of each kind.

    `by_admit` gives the number of the findings of each path that a row with the value admit admits, and `by_reason`
    the number that keep rows count.
    """
    findings: list[check_report.Finding] = []
    for path, kinds in sorted(admitted.items()):
        shown = ", ".join(f"{kind or 'no kind'} {count}" for kind, count in sorted(kinds.items()))
        findings.append(check_report.Finding(
            "warning", shown_path(rule.id), lines.get(path, 0), rule.id,
            admitted_text(rule, path, sum(kinds.values()), shown, (by_admit or {}).get(path, 0),
                          (by_reason or {}).get(path, 0)), "admitted", path))
    return findings


def compare(rule: Rule, ledger: Ledger, findings: Sequence[check_report.Finding],
            configuration: str | None = None) -> list[check_report.Finding]:
    """Judge the findings of a rule against its ledger (THE POLICIES).

    The errors of the rule are its violations.  A warning of the rule passes
    with no change.  Complexity: O(f log f + r) for f findings and r rows.

    Raises:
        LedgerError: If the policy of the rule is max or baseline, which the rule judges itself
    """
    if rule.policy in (Policy.MAX, Policy.BASELINE):
        raise LedgerError(f"the rule {rule.id} judges its ledger of the policy {rule.policy} itself")
    errors = [finding for finding in findings if finding.level == "error"]
    result = [finding for finding in findings if finding.level != "error"]
    if rule.policy == Policy.NONE:
        return result + errors
    shown = shown_path(rule.id)
    command = f"`python3 utils/bin/guard write --rule {rule.id}`"
    by_key: dict[Key, list[check_report.Finding]] = {}
    for finding in errors:
        by_key.setdefault(key_of(finding), []).append(finding)
    admitted: dict[str, Counter[str]] = {}
    lines: dict[str, int] = {}
    by_admit: Counter[str] = Counter()
    by_reason: Counter[str] = Counter()
    if rule.policy == Policy.COUNT:
        expected = ledger.expected(configuration)
        admits = ledger.admits()
        held = {key.path for key in expected} | {key.path for key in admits}
        for key in sorted(set(by_key) | set(expected) | set(admits)):
            measured, allowed = len(by_key.get(key, [])), expected.get(key, 0)
            line = ledger.line_of(key, configuration)
            if key in admits:
                result += admit_verdict(rule, key, admits[key], measured, admitted, lines)
                by_admit[key.path] += measured
            elif measured > allowed:
                said = ("and the ledger holds no row of the file.  A new file has no finding" if key.path not in held
                        else f"and the ledger admits {allowed}.  A change can only lower a count")
                result += [replace(finding, message=f"{finding.message}.  The file has {measured} findings of "
                                                    f"{described(key)}, {said}")
                           for finding in by_key[key]]
            elif measured < allowed:
                result.append(check_report.Finding(
                    "error", shown, line, rule.id,
                    f"{key.path} has {measured} findings of {described(key)}, and the ledger holds {allowed}.  Write "
                    f"the ledger again in the same commit: {command}", key.kind, key.entity))
            else:
                admitted.setdefault(key.path, Counter())[key.kind] += measured
                lines.setdefault(key.path, line)
                if ledger.reason_of(key, configuration):
                    by_reason[key.path] += measured
    else:
        for key, items in sorted(by_key.items()):
            if key not in ledger.base:
                result += items
        for key, entry in sorted(ledger.base.items()):
            result += admit_verdict(rule, key, entry, len(by_key.get(key, [])), admitted, lines)
    return result + admitted_warnings(rule, admitted, lines, by_admit, by_reason)


def admit_verdict(rule: Rule, key: Key, entry: Entry, measured: int, admitted: dict[str, Counter[str]],
                  lines: dict[str, int]) -> list[check_report.Finding]:
    """Judge the findings of the key of one row with the value admit, and return the error of a row with none.

    A row that admits findings adds them to `admitted` and its line to `lines`, for the warning of its path.
    """
    if measured == 0:
        return [check_report.Finding(
            "error", shown_path(rule.id), entry.line, rule.id,
            f"the row of {key.path} ({described(key)}) admits no finding.  Remove it with "
            f"`python3 utils/bin/guard write --rule {rule.id}`", key.kind, key.entity)]
    admitted.setdefault(key.path, Counter())[key.kind] += measured
    lines.setdefault(key.path, entry.line)
    return []


def moved_counts(expected: Mapping[Key, int], moved: Mapping[str, str]) -> dict[Key, int]:
    """Return the expected counts with the rows of each moved file also under its new path.

    A move counts only from a path that the ledger holds to a path that it does not hold.
    """
    held = {key.path for key in expected}
    renamed = {new: old for new, old in moved.items() if old in held and new not in held}
    result = dict(expected)
    for key, count in expected.items():
        for new, old in renamed.items():
            if old == key.path:
                result[Key(new, key.kind, key.entity)] = count
    return result


def moved_entries(table: Mapping[Key, Entry], moved: Mapping[str, str]) -> dict[Key, Entry]:
    """Return the rows of a table with each row of a moved file under its new path."""
    result: dict[Key, Entry] = {}
    for key, entry in table.items():
        new = next((new for new, old in moved.items() if old == key.path), None)
        target = Key(new, key.kind, key.entity) if new is not None else key
        if target not in result:
            result[target] = replace(entry, key=target)
    return result


def write(rule: Rule, ledger: Ledger, findings: Sequence[check_report.Finding], configuration: str | None = None,
          mode: WriteMode = WriteMode.LOWER, reason: str | None = None,
          moved: Mapping[str, str] | None = None, keep_others: bool = False) -> Ledger:
    """Return the ledger that admits the error findings of a rule (THE WRITE).

    Args:
        rule: The rule
        ledger: The ledger of the rule
        findings: The findings of the rule
        configuration: The name of the configuration of the build, or None for a rule with no configuration
        mode: The mode of the write
        reason: The reason of each new row and each raised count, necessary in the mode admit
        moved: The old path of each file that git reports as moved
        keep_others: When a write of the base keeps the count of each key of each other configuration

    Raises:
        LedgerError: If the mode refuses a finding, the policy has no write here, or a necessary reason is missing

    Complexity: O(f log f + r) for f findings and r rows.
    """
    counts = Counter(key_of(finding) for finding in findings if finding.level == "error")
    return write_counts(rule, ledger, counts, configuration, mode, reason, moved, keep_others)


def write_counts(rule: Rule, ledger: Ledger, counts: Mapping[Key, int], configuration: str | None = None,
                 mode: WriteMode = WriteMode.LOWER, reason: str | None = None,
                 moved: Mapping[str, str] | None = None, keep_others: bool = False) -> Ledger:
    """Return the ledger that admits the count of each key, as write() does for the error findings of a rule.

    A module that counts its findings itself, such as the rule quarantine,
    writes its ledger with this function.

    Raises:
        LedgerError: If the mode refuses a count, the policy has no write here, or a necessary reason is missing

    Complexity: O(k log k + r) for k counts and r rows.
    """
    if rule.policy in (Policy.NONE, Policy.MAX, Policy.BASELINE):
        raise LedgerError(f"the rule {rule.id} has the policy {rule.policy}, and its own module writes such a ledger")
    if mode == WriteMode.ADMIT and not reason:
        raise LedgerError("a write in the mode admit gives a reason with --reason TEXT")
    moved = dict(moved or {})
    counts = {key: count for key, count in counts.items() if count > 0}
    base = moved_entries(ledger.base, moved)
    deltas = {name: moved_entries(table, moved) for name, table in ledger.deltas.items()}
    if rule.policy == Policy.ADMIT:
        new_keys = sorted(key for key in counts if key not in base)
        if new_keys and mode != WriteMode.ADMIT:
            raise LedgerError(f"{len(new_keys)} findings of {rule.id} have no row, such as {new_keys[0].path} "
                              f"({described(new_keys[0])}).  Fix them, or admit them with --admit --reason TEXT")
        kept = {key: entry for key, entry in base.items() if key in counts}
        kept.update({key: Entry(key, ADMIT_VALUE, reason or "") for key in new_keys})
        return replace(ledger, base=kept)
    is_base = configuration is None or configuration == ledger.base_name or not ledger.configurations
    # The rows with the value admit belong to no configuration (THE PART OF NO CONFIGURATION).  A write keeps each one
    # that admits a finding, and their keys have no count.
    admits = {key: entry for key, entry in base.items() if entry.value == ADMIT_VALUE}
    kept_admits = {key: entry for key, entry in admits.items() if counts.get(key, 0) > 0}
    counts = {key: count for key, count in counts.items() if key not in admits}
    expected = moved_counts(ledger.expected(configuration), moved)
    rises = sorted(key for key, count in counts.items() if count > expected.get(key, 0))
    if rises and mode == WriteMode.LOWER:
        first = rises[0]
        raise LedgerError(f"{len(rises)} counts of {rule.id} rose, such as {first.path} ({described(first)}) from "
                          f"{expected.get(first, 0)} to {counts[first]}.  Remove the new findings.  Give --raise only "
                          f"when the checker changed what it reports, or --admit with a reason")
    if is_base:
        new_base = {key: Entry(key, count, base[key].reason if key in base else (reason or "")) for key, count in
                    counts.items()}
        return replace(ledger, base={**new_base, **kept_admits},
                       deltas=other_deltas(ledger, deltas, counts, moved, keep_others))
    # The difference rows are relative to the stored base rows, which keep the old path of a moved file until a write
    # of the base moves them.
    stored = base_counts(ledger.base)
    new_deltas = dict(deltas)
    new_deltas[configuration] = difference_table(counts, stored, deltas.get(configuration, {}), reason or "")
    return replace(ledger, base={**base_counts_rows(ledger.base), **kept_admits}, deltas=new_deltas)


def base_counts_rows(base: Mapping[Key, Entry]) -> dict[Key, Entry]:
    """Return each base row of a count ledger with no row of the value admit."""
    return {key: entry for key, entry in base.items() if entry.value != ADMIT_VALUE}


def other_deltas(ledger: Ledger, deltas: Mapping[str, Mapping[Key, Entry]], counts: Mapping[Key, int],
                 moved: Mapping[str, str], keep_others: bool) -> dict[str, dict[Key, Entry]]:
    """Return the difference rows of each other configuration after a write of the base counts `counts` (THE WRITE).

    With keep_others, each other configuration keeps its count of each key.
    Without keep_others, each other configuration keeps its difference rows.
    When a row gives a count below 0, the key gets the count 0 in that
    configuration.  Complexity: linear in the rows.
    """
    result: dict[str, dict[Key, Entry]] = {}
    for item in ledger.configurations[1:]:
        old = deltas.get(item.name, {})
        if keep_others:
            result[item.name] = difference_table(moved_counts(ledger.expected(item.name), moved), counts, old)
            continue
        table: dict[Key, Entry] = {}
        for key, entry in old.items():
            delta = max(int(entry.value), -counts.get(key, 0))
            if delta != 0:
                table[key] = replace(entry, value=delta)
        result[item.name] = table
    return result


def rows_of_configuration(ledger: Ledger, name: str, facts: Sequence[tuple[str, str]],
                          findings: Sequence[check_report.Finding]) -> list[str]:
    """Return the rows of a configuration other than the base, each with ROW_MARK, for import_rows().

    Raises:
        LedgerError: If the configuration is the base, or the policy is not count
    """
    return rows_of_counts(ledger, name, facts, Counter(key_of(finding) for finding in findings
                                                       if finding.level == "error"))


def rows_of_counts(ledger: Ledger, name: str, facts: Sequence[tuple[str, str]], counts: Mapping[Key, int]) -> list[str]:
    """Return the rows of a configuration other than the base for the count of each key, as rows_of_configuration().

    Raises:
        LedgerError: If the configuration is the base, or the policy is not count
    """
    if ledger.policy != Policy.COUNT or name == ledger.base_name:
        raise LedgerError("only a configuration other than the base of a count ledger has rows to import")
    admits = ledger.admits()
    table = difference_table({key: count for key, count in counts.items() if key not in admits},
                             base_counts(ledger.base), {})
    lines = [rows_grammar.render(["configuration", name, key, value]) for key, value in facts]
    lines += [rows_grammar.render([f"{DELTA_MARK}{name}"], [key.path, key.kind, f"{int(table[key].value):+d}",
                                                            key.entity, ""])
              for key in sorted(table)]
    return [f"{ROW_MARK}{line}" for line in lines]


def import_rows(ledger: Ledger, log_text: str) -> Ledger:
    """Put the rows of one configuration that rows_of_configuration() gave into the ledger, from the text of a log.

    A line of the log holds a row after ROW_MARK, and the text before the mark
    is the prefix of the CI log.  The rows replace the rows of the
    configuration.

    Raises:
        LedgerError: If the rows name no configuration or more than one, name the base, or do not obey the schema
    """
    found = [line.split(ROW_MARK, 1)[1] for line in log_text.splitlines() if ROW_MARK in line]
    text = "".join(f"{line}\n" for line in found)
    try:
        imported = rows_grammar.parse(text, "the log")
    except rows_grammar.RowsError as problem:
        raise LedgerError(str(problem)) from None
    names = {row.words[1] if row.words[0] == "configuration" else row.words[0][len(DELTA_MARK):] for row in imported}
    if len(names) != 1:
        raise LedgerError(f"the rows of the log name {len(names)} configurations, and an import takes one")
    name = names.pop()
    if name == ledger.base_name:
        raise LedgerError(f"the rows are of the base configuration {name}.  Write it with `guard write`")
    facts: dict[str, str] = {}
    table: dict[Key, Entry] = {}
    for row in imported:
        if row.words[0] == "configuration" and len(row.words) == 4:
            if facts.setdefault(row.words[2], row.words[3]) != row.words[3]:
                raise LedgerError(f"the key {row.words[2]} of the configuration {name} has two values")
        elif len(row.words) == 1 and len(row.cells) == 5:
            key = Key(row.cells[0], row.cells[1], row.cells[3])
            if key in table:
                raise LedgerError(f"the key {key} has two rows in the log")
            table[key] = Entry(key, parse_delta(row.cells[2], row), row.cells[4])
        else:
            raise LedgerError(f"the line {row.line} of the rows of the log is not a row of a configuration")
    configurations = [item for item in ledger.configurations if item.name != name]
    configurations.append(Configuration(name, tuple(facts.items())))
    deltas = {key: value for key, value in ledger.deltas.items() if key != name}
    deltas[name] = table
    result = replace(ledger, configurations=tuple(configurations), deltas=deltas)
    parse(render(result), "the import", result.rule, result.policy)
    return result


def count_rows(text: str | None) -> int | None:
    """Return the number of rows that one version of a ledger admits, or None for no text.

    A ledger of format 1 counts its entry rows and its difference rows, and
    not its header rows.  A ledger of an earlier format counts each line that
    is not empty and not a comment.  A version of format 1 that does not obey
    the grammar counts its lines that have a cell.
    """
    if text is None:
        return None
    lines = [line.strip(" \t") for line in text.splitlines()]
    rows = [line for line in lines if line and not line.startswith("#")]
    if rows[:1] != [f"format {FORMAT}"]:
        return len(rows)
    return sum(1 for line in rows if rows_grammar.SEPARATOR in line)


def history(rule: Rule, root: Path = REPO_ROOT, earlier: Iterable[str] = ()) -> list[tuple[str, str, int | None]]:
    """Return the row count of the ledger of a rule at each commit that changed it, newest first.

    Each item is the commit, its subject and the count after the commit.
    `earlier` gives the earlier paths of the ledger: a version of an earlier
    path counts at a commit where the ledger file does not exist.

    Raises:
        assay.core.git.GitError: If git fails
    """
    from assay.core import git

    paths = [shown_path(rule.id), *earlier]
    states = git.path_history(root, paths)
    result: list[tuple[str, str, int | None]] = []
    for commit in states.commits:
        text = next((data.decode("utf-8", "replace") for path in paths
                     if (data := states.at.get((commit.sha, path))) is not None), None)
        result.append((commit.sha, commit.subject, count_rows(text)))
    return result
