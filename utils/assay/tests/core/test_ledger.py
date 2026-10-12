"""The test of the ledgers (assay.core.ledger): the reader, the policies, the writes, the configurations and the history.

`guard selftest tests.core.test_ledger` runs the test.  Each case prints one
line.  Exit 0 when each case holds, else 1.
"""

import sys
import tempfile
from collections.abc import Callable, Mapping
from dataclasses import replace
from functools import partial
from pathlib import Path

from assay.core import git
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar

COUNT_RULE = "rule count-rule engine=meta scope=tracked ledger=count doc=XV | a planted refusal | fix it"
ADMIT_RULE = "rule admit-rule engine=meta scope=tracked ledger=admit doc=XV | a planted refusal | fix it"
MAX_RULE = rule_registry.measured_rule("max-rule", 1, 2, "s")
Expect = Callable[[str, bool], None]


def rules() -> dict[str, rule_registry.Rule]:
    """Return the three planted rules."""
    registry, problems = rule_registry.examine(rule_registry.planted([COUNT_RULE, ADMIT_RULE, MAX_RULE]))
    assert not problems, problems
    return dict(registry.rules)


def finding(
    path: str, kind: str = "kind", entity: str = "", level: str = "error", rule_id: str = "count-rule"
) -> check_report.Finding:
    """Return one planted finding."""
    return check_report.Finding(level, path, 1, rule_id, "a planted finding", kind, entity)


def refuses(action: Callable[[], object], error: type[Exception]) -> bool:
    """Whether an action raises the error."""
    try:
        action()
    except error:
        return True
    return False


def errors(found: list[check_report.Finding]) -> list[check_report.Finding]:
    """Return the errors of a list of findings."""
    return [item for item in found if item.level == "error"]


def reader_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check the reader and the writer of the file."""
    text = (
        "# the head\nformat 1\nrule count-rule\nconfiguration base machine x86_64\nconfiguration arm machine "
        "aarch64\na.py | kind | 2 | - | -\nb.py | kind | 1 | name | a reason\n@arm | a.py | kind | -1 | - | -\n"
    )
    ledger = rule_ledger.parse(text, "ledger.txt", "count-rule", rule_registry.Policy.COUNT)
    expect(
        "a ledger reads its configurations, its base rows and its difference rows",
        ledger.base_name == "base" and len(ledger.base) == 2 and len(ledger.deltas["arm"]) == 1,
    )
    expect(
        "expected() adds the differences of a configuration",
        ledger.expected("arm") == {rule_ledger.Key("a.py", "kind", ""): 1, rule_ledger.Key("b.py", "kind", "name"): 1}
        and ledger.expected()[rule_ledger.Key("a.py", "kind", "")] == 2,
    )
    expect("render() gives the same text again", rule_ledger.render(ledger) == text)
    bad = {
        "a second row of a key": text.replace("b.py | kind | 1 | name", "a.py | kind | 1 | -"),
        "a count of 0": text.replace("a.py | kind | 2", "a.py | kind | 0"),
        "a difference that makes a count below 0": text.replace("-1 | - | -", "-3 | - | -"),
        "a wrong rule row": text.replace("rule count-rule", "rule other-rule"),
        "a configuration after an entry row": text + "configuration late machine x\n",
    }
    for name, planted in bad.items():
        expect(
            f"a ledger with {name} is refused",
            refuses(
                lambda planted=planted: rule_ledger.parse(
                    planted, "ledger.txt", "count-rule", rule_registry.Policy.COUNT
                ),
                rows_grammar.RowsError,
            ),
        )
    admit_text = "format 1\nrule admit-rule\na.py | kind | admit | - | -\n"
    expect(
        "a row of the policy admit with no reason is refused",
        refuses(
            lambda: rule_ledger.parse(admit_text, "ledger.txt", "admit-rule", rule_registry.Policy.ADMIT),
            rows_grammar.RowsError,
        ),
    )
    built = rule_ledger.parse(
        text + "@build/gen/table.c | kind | 1 | - | -\n", "ledger.txt", "count-rule", rule_registry.Policy.COUNT
    )
    expect(
        "a row of a file of the build directory is a base row, and @NAME with no slash is a difference row",
        built.base.get(rule_ledger.Key("@build/gen/table.c", "kind", "")) is not None
        and len(built.deltas["arm"]) == 1
        and without_lines(
            rule_ledger.parse(rule_ledger.render(built), "again.txt", "count-rule", rule_registry.Policy.COUNT)
        )
        == without_lines(built),
    )
    nameless = rule_ledger.parse(
        text + "- | kind | 1 | name | -\n@arm | - | kind | +1 | name | -\n",
        "ledger.txt",
        "count-rule",
        rule_registry.Policy.COUNT,
    )
    expect(
        "the PATH '-' is the empty path in a base row and in a difference row, and it renders as '-' again",
        rule_ledger.Key("", "kind", "name") in nameless.base
        and rule_ledger.Key("", "kind", "name") in nameless.deltas["arm"]
        and "\n- | kind | 1 | name | -\n" in rule_ledger.render(nameless),
    )
    expect(
        "count_rows() counts the entry rows of format 1 and each line of an earlier format",
        rule_ledger.count_rows(text) == 3
        and rule_ledger.count_rows("# head\na | b\nc | d\n") == 2
        and rule_ledger.count_rows(None) is None,
    )
    expect(
        "format_row() gives the format row, an empty text for an earlier format, and None for no row",
        rule_ledger.format_row(text) == "format 1"
        and rule_ledger.format_row("# head\na | b\n") == ""
        and rule_ledger.format_row("# head only\n") is None
        and rule_ledger.format_row(None) is None,
    )
    expect(
        "version_keys() gives the key of each base row and difference row of format 1, and each line of an earlier "
        "format",
        rule_ledger.version_keys(text) == {("a.py", "kind", ""), ("b.py", "kind", "name"), ("@arm", "a.py", "kind", "")}
        and rule_ledger.version_keys("# head\na | b\n") == {("a | b",)}
        and rule_ledger.version_keys(None) == frozenset(),
    )
    reasoned = text + "lib/a.a | throw | admit | helper | a reason row\n"
    admits_only = "format 1\nrule admit-rule\nc.py | kind | admit | name | a reason\n"
    expect(
        "counted_keys() gives the keys of the rows with a count, and each key of a version with no such row",
        rule_ledger.counted_keys(reasoned) == rule_ledger.version_keys(text)
        and ("lib/a.a", "throw", "helper") in rule_ledger.version_keys(reasoned)
        and rule_ledger.counted_keys(admits_only) == {("c.py", "kind", "name")}
        and rule_ledger.counted_keys(None) == frozenset(),
    )
    expect(
        "key_path() gives the PATH of a base row and of a difference row, and '' for no file and an earlier line",
        [rule_ledger.key_path(key) for key in sorted(rule_ledger.version_keys(text))] == ["a.py", "a.py", "b.py"]
        and rule_ledger.key_path(("-", "kind", "name")) == ""
        and rule_ledger.key_path(("@arm", "-", "kind", "name")) == ""
        and rule_ledger.key_path(("a | b",)) == "",
    )
    with tempfile.TemporaryDirectory(prefix="test-ledger-") as scratch:
        root = Path(scratch)
        (root / rule_ledger.LEDGER_DIRECTORY).mkdir(parents=True)
        changed = rule_ledger.store(ledger, root)
        again = rule_ledger.store(ledger, root)
        read_back = rule_ledger.read(table["count-rule"], root)
        removed = rule_ledger.store(rule_ledger.empty(table["count-rule"]), root)
        expect(
            "store() writes a file, keeps the same bytes, and removes the file of a ledger with no row",
            changed
            and not again
            and read_back == ledger
            and removed
            and not rule_ledger.ledger_path("count-rule", root).exists(),
        )
        planted = root / "planted" / "ledger.txt"
        planted.parent.mkdir()
        stored = rule_ledger.store(ledger, file=planted)
        planted_back = rule_ledger.read(table["count-rule"], file=planted)
        expect(
            "read() and store() take the file of a planted ledger, and the reader names that file",
            stored
            and planted_back.base == ledger.base
            and not rule_ledger.ledger_path("count-rule", root).exists()
            and rule_ledger.read(table["count-rule"], file=root / "absent.txt").base == {},
        )
    expect(
        "rule_of() gives a rule of the registry and refuses an unknown rule",
        rule_ledger.rule_of("history").id == "history"
        and refuses(lambda: rule_ledger.rule_of("no-such-rule"), rule_ledger.LedgerError),
    )


def without_lines(ledger: rule_ledger.Ledger) -> rule_ledger.Ledger:
    """Return a ledger with the line of each row at 0, so that two texts with the same rows in another order compare equal."""

    def table(entries: Mapping[rule_ledger.Key, rule_ledger.Entry]) -> dict[rule_ledger.Key, rule_ledger.Entry]:
        return {key: replace(entry, line=0) for key, entry in entries.items()}

    return replace(
        ledger, base=table(ledger.base), deltas={name: table(entries) for name, entries in ledger.deltas.items()}
    )


def policy_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check compare() for the policies count and admit."""
    count = table["count-rule"]
    ledger = rule_ledger.write(
        count, rule_ledger.empty(count), [finding("a.py"), finding("a.py")], mode=rule_ledger.WriteMode.RAISE
    )
    judged = rule_ledger.compare(count, ledger, [finding("a.py"), finding("a.py")])
    expect(
        "findings at their count give one warning for each path, and no error",
        not errors(judged) and len(judged) == 1 and "keeps 2 findings" in judged[0].message,
    )
    expect(
        "the warning of a count row with no reason tells the reader to remove a finding and to lower its count",
        "Remove a finding" in judged[0].message
        and "lower its count" in judged[0].message
        and "guard write --rule count-rule" in judged[0].message,
    )
    keep_cases(expect, count)
    judged = rule_ledger.compare(count, ledger, [finding("a.py")] * 3)
    expect("a count above its row is an error at each finding", len(errors(judged)) == 3)
    judged = rule_ledger.compare(count, ledger, [finding("a.py")])
    expect(
        "a count below its row is an error at the ledger, which asks for a write",
        len(errors(judged)) == 1 and errors(judged)[0].path == rule_ledger.shown_path("count-rule"),
    )
    judged = rule_ledger.compare(count, ledger, [*([finding("a.py")] * 2), finding("new.py")])
    expect(
        "a finding in a file that the ledger does not hold is an error",
        any(item.path == "new.py" and "A new file has no finding" in item.message for item in errors(judged)),
    )
    judged = rule_ledger.compare(count, ledger, [*([finding("a.py")] * 2), finding("a.py", level="warning")])
    expect("a warning of the rule passes with no change", sum(item.level == "warning" for item in judged) == 2)
    admit = table["admit-rule"]
    ledger = rule_ledger.write(
        admit,
        rule_ledger.empty(admit),
        [finding("a.py", rule_id="admit-rule")],
        mode=rule_ledger.WriteMode.ADMIT,
        reason="a planted reason",
    )
    judged = rule_ledger.compare(admit, ledger, [finding("a.py", rule_id="admit-rule")])
    expect("an admitted finding gives a warning", not errors(judged) and len(judged) == 1)
    expect(
        "the warning of the policy admit tells that each row gives its reason, and asks for no removal",
        "Each row gives its reason" in judged[0].message
        and "Remove" not in judged[0].message
        and "lower" not in judged[0].message,
    )
    judged = rule_ledger.compare(admit, ledger, [])
    expect(
        "a row that admits no finding is an error", any("admits no finding" in item.message for item in errors(judged))
    )
    judged = rule_ledger.compare(admit, ledger, [finding("b.py", rule_id="admit-rule")])
    expect("a finding with no row is an error", any(item.path == "b.py" for item in errors(judged)))
    expect(
        "compare() refuses the policy max, which the rule judges itself",
        refuses(
            lambda: rule_ledger.compare(table["max-rule"], rule_ledger.empty(table["max-rule"]), []),
            rule_ledger.LedgerError,
        ),
    )
    baseline = rule_ledger.parse(
        "format 1\nrule base-rule\n- | k | 9007199254740993 | sum | -\n- | k | 2.5 | part | -\n- | k | 7/3 | h | -\n",
        "ledger.txt",
        "base-rule",
        rule_registry.Policy.BASELINE,
    )
    values = {key.entity: entry.value for key, entry in baseline.base.items()}
    expect(
        "a baseline of digits reads as an accurate int above 2^53, a decimal as a float, and BYTES/READERS as text",
        values == {"sum": 9007199254740993, "part": 2.5, "h": "7/3"} and isinstance(values["sum"], int),
    )
    configured = rule_ledger.parse(
        "format 1\nrule base-rule\nconfiguration debug kind debug\nconfiguration release kind release\n"
        "- | debug | 7/3 | h | -\n- | release | 8/3 | h | -\n",
        "ledger.txt",
        "base-rule",
        rule_registry.Policy.BASELINE,
    )
    expect(
        "a baseline ledger reads two configurations with rows that hold no count, and a difference row stays refused",
        [item.name for item in configured.configurations] == ["debug", "release"]
        and len(configured.base) == 2
        and refuses(
            lambda: rule_ledger.parse(
                "format 1\nrule base-rule\nconfiguration debug kind debug\nconfiguration release kind release\n"
                "@release | - | debug | +1 | h | -\n",
                "ledger.txt",
                "base-rule",
                rule_registry.Policy.BASELINE,
            ),
            rows_grammar.RowsError,
        ),
    )


def keep_cases(expect: Expect, count: rule_registry.Rule) -> None:
    """Check the warning of a keep row: a count row with a reason, which tells why its findings stay."""
    kept = rule_ledger.write(
        count,
        rule_ledger.empty(count),
        [finding("a.py"), finding("a.py")],
        mode=rule_ledger.WriteMode.ADMIT,
        reason="a planted reason",
    )
    judged = rule_ledger.compare(count, kept, [finding("a.py"), finding("a.py")])
    message = judged[0].message if len(judged) == 1 else ""
    expect(
        "the warning of a keep row gives its reason as the cause, and it asks for no removal",
        not errors(judged)
        and "each with its reason" in message
        and "tells why its findings stay" in message
        and "Remove" not in message
        and "guard write --rule count-rule" in message,
    )
    mixed = rule_ledger.write(
        count, kept, [finding("a.py"), finding("a.py"), finding("a.py", kind="other")], mode=rule_ledger.WriteMode.RAISE
    )
    judged = rule_ledger.compare(count, mixed, [finding("a.py"), finding("a.py"), finding("a.py", kind="other")])
    message = judged[0].message if len(judged) == 1 else ""
    expect(
        "a path with a keep row and a count row with no reason asks for a removal, and names the keep rows",
        not errors(judged) and "Remove a finding" in message and "Keep rows count 2 of them" in message,
    )
    text = (
        "format 1\nrule count-rule\nconfiguration base machine x86_64\nconfiguration arm machine aarch64\n"
        "a.py | kind | 2 | - | a planted reason\n@arm | a.py | kind | +1 | - | -\n"
    )
    based = rule_ledger.parse(text, "ledger.txt", "count-rule", rule_registry.Policy.COUNT)
    judged = rule_ledger.compare(count, based, [finding("a.py")] * 3, "arm")
    message = judged[0].message if len(judged) == 1 else ""
    expect(
        "a difference row with no reason takes the reason of its base row, so its key is a keep row",
        not errors(judged) and "each with its reason" in message and "Remove" not in message,
    )


def prefix_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check the reason rows of a prefix: the reader, the longest prefix, the warning, the write and the row count."""
    count = table["count-rule"]
    head = "format 1\nrule count-rule\n"
    text = (
        f"{head}reason src/ | the step of a planted plan removes them\n"
        "reason src/deep/ | a longer prefix gives this reason\nreason c.py | one file\n"
        "src/a.py | kind | 2 | - | -\nsrc/deep/b.py | kind | 1 | - | -\nsrc/c.py | kind | 1 | - | an own reason\n"
        "c.py | kind | 1 | - | -\nother.py | kind | 1 | - | -\n"
    )
    ledger = rule_ledger.parse(text, "ledger.txt", "count-rule", rule_registry.Policy.COUNT)
    key = rule_ledger.Key
    expect(
        "a reason row gives the reason of a count row under its prefix, and the longer prefix decides",
        ledger.kept_reason(key("src/a.py", "kind", "")) == "the step of a planted plan removes them"
        and ledger.kept_reason(key("src/deep/b.py", "kind", "")) == "a longer prefix gives this reason"
        and ledger.kept_reason(key("c.py", "kind", "")) == "one file"
        and ledger.kept_reason(key("other.py", "kind", "")) == "",
    )
    expect(
        "the reason of a row of its own comes before the reason of a prefix, and reason_of() reads no prefix",
        ledger.kept_reason(key("src/c.py", "kind", "")) == "an own reason"
        and ledger.reason_of(key("src/a.py", "kind", "")) == "",
    )
    expect(
        "a prefix with no final slash holds only the equal path",
        ledger.prefix_reason("c.py.old") is None and ledger.prefix_reason("srcx/a.py") is None,
    )
    again = rule_ledger.parse(rule_ledger.render(ledger), "again", "count-rule", rule_registry.Policy.COUNT)
    expect(
        "render() writes the reason rows sorted after the header, and parse() reads the same rows again",
        [(item.prefix, item.reason) for item in again.reasons]
        == sorted((item.prefix, item.reason) for item in ledger.reasons)
        and again.base.keys() == ledger.base.keys(),
    )
    expect(
        "count_rows() does not count a reason row",
        rule_ledger.count_rows(text) == 5,
    )
    judged = rule_ledger.compare(
        count,
        ledger,
        [finding("src/a.py"), finding("src/a.py")]
        + [finding(path) for path in ("src/deep/b.py", "src/c.py", "c.py", "other.py")],
    )
    by_path = {item.entity: item.message for item in judged if item.level == "warning"}
    expect(
        "the warning of a path that a reason row holds names that row and its reason, as a keep row",
        not errors(judged)
        and "each with its reason" in by_path.get("src/a.py", "")
        and "`reason src/` at line 3 gives the reason: the step of a planted plan removes them" in by_path["src/a.py"]
        and "Remove a finding" in by_path.get("other.py", ""),
    )
    state, tail = rule_ledger.kept_words(ledger, [key("src/a.py", "kind", ""), key("src/deep/b.py", "kind", "")])
    expect(
        "kept_words() tells that each key has a reason, and the lines of two reason rows",
        state == ", each with its reason" and "lines 3, 4" in tail,
    )
    expect(
        "kept_words() gives no words for a group with a key that has no reason",
        rule_ledger.kept_words(ledger, [key("other.py", "kind", ""), key("src/a.py", "kind", "")]) == ("", ""),
    )
    for name, bad in (
        ("a reason row that holds no count row", f"{head}reason lib/ | idle\nsrc/a.py | kind | 1 | - | -\n"),
        ("a reason row after an entry row", f"{head}src/a.py | kind | 1 | - | -\nreason src/ | late\n"),
        ("a reason row with no reason", f"{head}reason src/ | -\nsrc/a.py | kind | 1 | - | -\n"),
        (
            "a second reason row of one prefix",
            f"{head}reason src/ | one\nreason src/ | two\nsrc/a.py | kind | 1 | - | -\n",
        ),
        (
            "a configuration row after a reason row",
            f"{head}reason src/ | one\nconfiguration base machine x86_64\nsrc/a.py | kind | 1 | - | -\n",
        ),
    ):
        expect(
            f"the reader refuses {name}",
            refuses(
                partial(rule_ledger.parse, bad, "l.txt", "count-rule", rule_registry.Policy.COUNT),
                rows_grammar.RowsError,
            ),
        )
    expect(
        "the reader refuses a reason row in a ledger of the policy admit",
        refuses(
            lambda: rule_ledger.parse(
                "format 1\nrule admit-rule\nreason src/ | one\nsrc/a.py | k | admit | - | r\n",
                "l.txt",
                "admit-rule",
                rule_registry.Policy.ADMIT,
            ),
            rows_grammar.RowsError,
        ),
    )
    lowered = rule_ledger.write(
        count, ledger, [finding("src/a.py")] + [finding(path) for path in ("src/c.py", "c.py", "other.py")]
    )
    expect(
        "a write removes each reason row that no count row of its result holds, and keeps the others",
        sorted(item.prefix for item in lowered.reasons) == ["c.py", "src/"],
    )


def write_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check the modes of write(), the moves, the configurations and the import of the rows of a configuration."""
    count = table["count-rule"]
    ledger = rule_ledger.write(
        count, rule_ledger.empty(count), [finding("a.py")] * 2, mode=rule_ledger.WriteMode.ADMIT, reason="first"
    )
    expect(
        "the mode lower refuses a count above its row",
        refuses(lambda: rule_ledger.write(count, ledger, [finding("a.py")] * 3), rule_ledger.LedgerError),
    )
    lowered = rule_ledger.write(count, ledger, [finding("a.py")])
    expect(
        "the mode lower lowers a count and removes a row with no finding",
        lowered.expected() == {rule_ledger.Key("a.py", "kind", ""): 1}
        and rule_ledger.write(count, ledger, []).base == {},
    )
    raised = rule_ledger.write(count, ledger, [finding("a.py")] * 3, mode=rule_ledger.WriteMode.RAISE)
    expect("the mode raise writes a count above its row", raised.expected()[rule_ledger.Key("a.py", "kind", "")] == 3)
    expect(
        "the mode admit needs a reason",
        refuses(
            lambda: rule_ledger.write(count, ledger, [finding("b.py")], mode=rule_ledger.WriteMode.ADMIT),
            rule_ledger.LedgerError,
        ),
    )
    moved = rule_ledger.write(count, ledger, [finding("new/a.py")] * 2, moved={"new/a.py": "a.py"})
    expect(
        "a file that git moved keeps the rows of its old path",
        moved.expected() == {rule_ledger.Key("new/a.py", "kind", ""): 2},
    )
    based = rule_ledger.Ledger(
        "count-rule",
        count.policy,
        "# head\n",
        (
            rule_ledger.Configuration("base", (("machine", "x86_64"),)),
            rule_ledger.Configuration("arm", (("machine", "aarch64"),)),
        ),
        ledger.base,
    )
    armed = rule_ledger.write(
        count, based, [finding("a.py")] * 3, configuration="arm", mode=rule_ledger.WriteMode.RAISE
    )
    expect(
        "a write of a configuration other than the base writes only its difference rows",
        armed.base == based.base and armed.expected("arm")[rule_ledger.Key("a.py", "kind", "")] == 3,
    )
    shifted = rule_ledger.write(
        count, based, [finding("new/a.py")] * 2, configuration="arm", moved={"new/a.py": "a.py"}
    )
    expect(
        "a write of a later configuration after a move, before the write of the base, keeps the base rows and "
        "gives its counts at the new path",
        shifted.base == based.base
        and shifted.expected("arm") == {rule_ledger.Key("new/a.py", "kind", ""): 2}
        and shifted.expected() == {rule_ledger.Key("a.py", "kind", ""): 2},
    )
    lines = rule_ledger.rows_of_configuration(based, "arm", [("machine", "aarch64")], [finding("a.py")] * 4)
    imported = rule_ledger.import_rows(based, "".join(f"2026-10-08 log: {line}\n" for line in lines))
    expect(
        "import_rows() puts the rows of a configuration from a log into the ledger",
        imported.expected("arm")[rule_ledger.Key("a.py", "kind", "")] == 4,
    )
    expect(
        "import_rows() refuses the rows of the base",
        refuses(
            lambda: rule_ledger.import_rows(based, f"{rule_ledger.ROW_MARK}configuration base machine x\n"),
            rule_ledger.LedgerError,
        ),
    )


def no_configuration_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check the rows with the value admit of a count ledger, and a write of the base with a second configuration."""
    count = table["count-rule"]
    text = (
        "format 1\nrule count-rule\nconfiguration base machine x86_64\nconfiguration arm machine aarch64\n"
        "a.py | kind | 1 | - | -\nb.py | kind | admit | name | a planted reason\n@arm | a.py | kind | -1 | - | -\n"
    )
    ledger = rule_ledger.parse(text, "ledger.txt", "count-rule", rule_registry.Policy.COUNT)
    admit_key = rule_ledger.Key("b.py", "kind", "name")
    expect(
        "a count ledger reads a row with the value admit, which has no count",
        list(ledger.admits()) == [admit_key]
        and admit_key not in ledger.expected()
        and admit_key not in ledger.expected("arm")
        and rule_ledger.render(ledger) == text,
    )
    for name, planted in {
        "a row with the value admit and no reason": text.replace("a planted reason", "-"),
        "a difference row of a row with the value admit": text + "@arm | b.py | kind | +1 | name | -\n",
    }.items():
        expect(
            f"a count ledger with {name} is refused",
            refuses(
                partial(rule_ledger.parse, planted, "ledger.txt", "count-rule", rule_registry.Policy.COUNT),
                rows_grammar.RowsError,
            ),
        )
    found = [finding("a.py"), finding("b.py", entity="name"), finding("b.py", entity="name")]
    judged = rule_ledger.compare(count, ledger, found, "base")
    expect("a row with the value admit admits each finding of its key in the base", not errors(judged))
    judged = rule_ledger.compare(count, ledger, [finding("b.py", entity="name")], "arm")
    expect("a row with the value admit admits each finding of its key in another configuration", not errors(judged))
    judged = rule_ledger.compare(count, ledger, [finding("a.py")], "base")
    expect(
        "a row with the value admit that admits no finding of the build is an error",
        [item.message.split(" (")[0] for item in errors(judged)] == ["the row of b.py"],
    )
    written = rule_ledger.write(count, ledger, [finding("a.py")], "base")
    expect(
        "a write removes a row with the value admit that admits no finding of the build",
        written.admits() == {} and written.expected() == {rule_ledger.Key("a.py", "kind", ""): 1},
    )
    written = rule_ledger.write(count, ledger, found, "base")
    expect(
        "a write keeps a row with the value admit that admits a finding, with its value and its reason",
        written.admits()[admit_key].reason == "a planted reason" and written.base[admit_key].value == "admit",
    )
    written = rule_ledger.write(count, ledger, [finding("b.py", entity="name")], "arm")
    expect(
        "a write of another configuration keeps a row with the value admit that admits a finding",
        admit_key in written.admits() and written.expected("arm") == {},
    )
    lowered = rule_ledger.write(count, ledger, [finding("b.py", entity="name")], "base")
    expect(
        "a write of the base gives no count below 0 to another configuration",
        lowered.expected("arm") == {}
        and lowered.deltas["arm"] == {}
        and rule_ledger.parse(
            rule_ledger.render(lowered), "again.txt", "count-rule", rule_registry.Policy.COUNT
        ).expected("arm")
        == {},
    )
    two = rule_ledger.parse(
        text.replace("@arm | a.py | kind | -1 | - | -\n", "@arm | c.py | kind | +1 | - | -\n"),
        "ledger.txt",
        "count-rule",
        rule_registry.Policy.COUNT,
    )
    kept = rule_ledger.write(
        count, two, [finding("d.py"), finding("b.py", entity="name")], "base", mode=rule_ledger.WriteMode.RAISE
    )
    expect(
        "a write of the base that raises a base count keeps the count of each key of the other configuration",
        kept.expected("arm") == two.expected("arm") and kept.expected() == {rule_ledger.Key("d.py", "kind", ""): 1},
    )
    lines = rule_ledger.rows_of_configuration(ledger, "arm", [("machine", "aarch64")], [finding("b.py", entity="name")])
    expect(
        "the rows of a configuration hold no row of a key with the value admit",
        not any("b.py" in line for line in lines) and any("a.py | kind | -1" in line for line in lines),
    )


def other_configuration_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check that a write of the base changes no count of a different configuration (THE WRITE of assay.core.ledger)."""
    count = table["count-rule"]
    text = (
        "format 1\nrule count-rule\nconfiguration base machine x86_64\nconfiguration arm machine aarch64\n"
        "a.py | kind | 3 | same | -\nb.py | kind | 3 | partial | -\nc.py | kind | 3 | none | -\n"
        "d.py | kind | 1 | gone | -\n@arm | b.py | kind | -1 | partial | -\n@arm | c.py | kind | -3 | none | -\n"
    )
    ledger = rule_ledger.parse(text, "ledger.txt", "count-rule", rule_registry.Policy.COUNT)
    found = [finding(path, entity=entity) for path, entity in (("a.py", "same"), ("b.py", "partial"), ("c.py", "none"))]
    lowered = rule_ledger.write(count, ledger, found * 2, "base")
    expect(
        "a write of the base that lowers base counts keeps the count of each key of the other configuration",
        lowered.expected() == {key: 2 for key in map(rule_ledger.key_of, found)}
        and lowered.expected("arm") == ledger.expected("arm"),
    )
    expect(
        "the difference of a key in the other configuration changes by the opposite of the change of its base count",
        {key.path: int(entry.value) for key, entry in lowered.deltas["arm"].items()}
        == {"a.py": 1, "c.py": -2, "d.py": 1},
    )
    again = rule_ledger.parse(rule_ledger.render(lowered), "again.txt", "count-rule", rule_registry.Policy.COUNT)
    expect(
        "the written ledger reads again with the same counts in each configuration",
        again.expected() == lowered.expected() and again.expected("arm") == ledger.expected("arm"),
    )
    later = rule_ledger.write(count, lowered, found[:2] * 2, "arm")
    expect(
        "a write of the other configuration then records its own fall, with no --raise",
        later.expected("arm") == {key: 2 for key in map(rule_ledger.key_of, found[:2])}
        and {key.path: int(entry.value) for key, entry in later.deltas["arm"].items()} == {"c.py": -2}
        and later.base == lowered.base,
    )
    moved = rule_ledger.write(
        count, ledger, [finding("new/a.py", entity="same")] * 3, "base", moved={"new/a.py": "a.py"}
    )
    expect(
        "a write of the base after a move gives the count of the old path to the new path in the other configuration",
        moved.expected("arm").get(rule_ledger.Key("new/a.py", "kind", "same")) == 3
        and rule_ledger.Key("a.py", "kind", "same") not in moved.expected("arm"),
    )


def history_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check history(): the row count at each commit, also of an earlier path."""
    with tempfile.TemporaryDirectory(prefix="test-ledger-history-") as scratch:
        root = Path(scratch)
        git.init(root)
        old = root / "utils" / "scripts" / "count-rule-ledger.txt"
        old.parent.mkdir(parents=True)
        old.write_text("# head\na | 1\nb | 2\n", encoding="utf-8")
        git.commit_all(root, "Add the old ledger")
        old.unlink()
        ledger = rule_ledger.write(
            table["count-rule"],
            rule_ledger.empty(table["count-rule"]),
            [finding("a.py")],
            mode=rule_ledger.WriteMode.ADMIT,
            reason="moved",
        )
        rule_ledger.ledger_path("count-rule", root).parent.mkdir(parents=True)
        rule_ledger.store(ledger, root)
        git.commit_all(root, "Move the ledger")
        counts = [
            count
            for _, _, count in rule_ledger.history(table["count-rule"], root, ["utils/scripts/count-rule-ledger.txt"])
        ]
        expect("history() gives the count at each commit, newest first, also of the earlier path", counts == [1, 2])


def main(argv: list[str]) -> int:
    """Run each case, and print one line for each.

    Returns:
        0 when each case holds, 1 when a case fails, 2 for a usage error
    """
    if argv:
        print("usage: guard selftest tests.core.test_ledger", file=sys.stderr)
        return 2
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    table = rules()
    reader_cases(expect, table)
    policy_cases(expect, table)
    prefix_cases(expect, table)
    write_cases(expect, table)
    no_configuration_cases(expect, table)
    other_configuration_cases(expect, table)
    history_cases(expect, table)
    if failures:
        print(f"tests.core.test_ledger: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("tests.core.test_ledger: every case holds.")
    return 0
