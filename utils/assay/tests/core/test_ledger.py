"""The test of the ledgers (assay.core.ledger): the reader, the policies, the writes and the reduce.

`guard selftest tests.core.test_ledger` runs the test.  Each case prints one
line.  Exit 0 when each case holds, else 1.
"""

import sys
import tempfile
from collections.abc import Callable
from dataclasses import replace
from functools import partial
from pathlib import Path

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
    text = "# the head\nformat 1\nrule count-rule\na.py | kind | 2 | - | -\nb.py | kind | 1 | name | a reason\n"
    ledger = rule_ledger.parse(text, "ledger.txt", "count-rule", rule_registry.Policy.COUNT)
    expect(
        "a ledger reads its rows, and expected() gives the count of each key",
        ledger.expected() == {rule_ledger.Key("a.py", "kind", ""): 2, rule_ledger.Key("b.py", "kind", "name"): 1},
    )
    expect("render() gives the same text again", rule_ledger.render(ledger) == text)
    bad = {
        "a second row of a key": text.replace("b.py | kind | 1 | name", "a.py | kind | 1 | -"),
        "a count of 0": text.replace("a.py | kind | 2", "a.py | kind | 0"),
        "a wrong rule row": text.replace("rule count-rule", "rule other-rule"),
        "a configuration row": text.replace("rule count-rule\n", "rule count-rule\nconfiguration base kind base\n"),
        "a difference row": text + "@arm | a.py | kind | -1 | - | -\n",
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
        "a row of a file of the build directory reads and renders again",
        built.base.get(rule_ledger.Key("@build/gen/table.c", "kind", "")) is not None
        and without_lines(
            rule_ledger.parse(rule_ledger.render(built), "again.txt", "count-rule", rule_registry.Policy.COUNT)
        )
        == without_lines(built),
    )
    nameless = rule_ledger.parse(
        text + "- | kind | 1 | name | -\n", "ledger.txt", "count-rule", rule_registry.Policy.COUNT
    )
    expect(
        "the PATH '-' is the empty path, and it renders as '-' again",
        rule_ledger.Key("", "kind", "name") in nameless.base
        and "\n- | kind | 1 | name | -\n" in rule_ledger.render(nameless),
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
        rule_ledger.rule_of("quarantine").id == "quarantine"
        and refuses(lambda: rule_ledger.rule_of("no-such-rule"), rule_ledger.LedgerError),
    )


def without_lines(ledger: rule_ledger.Ledger) -> rule_ledger.Ledger:
    """Return a ledger with the line of each row at 0, so that two texts with the same rows in another order compare equal."""
    return replace(ledger, base={key: replace(entry, line=0) for key, entry in ledger.base.items()})


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
        and "lowers its count" in judged[0].message
        and "guard write --rule count-rule" in judged[0].message,
    )
    keep_cases(expect, count)
    judged = rule_ledger.compare(count, ledger, [finding("a.py")] * 3)
    expect("a count above its row is an error at each finding", len(errors(judged)) == 3)
    judged = rule_ledger.compare(count, ledger, [finding("a.py")])
    expect(
        "a count below its row gives no error, and the warning of its path counts the findings of the build",
        not errors(judged) and len(judged) == 1 and "keeps 1 findings" in judged[0].message,
    )
    judged = rule_ledger.compare(count, ledger, [])
    expect("a count row with no finding of the build gives no finding", judged == [])
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
    expect("a row that admits no finding gives no finding", judged == [])
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


def prefix_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check the reason rows of a prefix: the reader, the longest prefix, the warning and the write."""
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
        "a complete write removes each reason row that no count row of its result holds, and keeps the others",
        sorted(item.prefix for item in lowered.reasons) == ["c.py", "src/"],
    )


def write_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check the modes of a complete write and the move of a file."""
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
        "a complete write lowers a count and removes a row with no finding",
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


def partial_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check the write of the counts of one build: it lowers no row and removes no row (THE WRITE)."""
    count, admit = table["count-rule"], table["admit-rule"]
    text = "format 1\nrule count-rule\na.py | kind | 2 | - | -\nb.py | kind | 1 | - | the arm of a different build\n"
    ledger = rule_ledger.parse(text, "ledger.txt", "count-rule", rule_registry.Policy.COUNT)
    kept = rule_ledger.write(count, ledger, [finding("a.py")], is_partial=True)
    expect(
        "a partial write keeps a row above the count of the build, and a row that the build does not give",
        kept.expected() == ledger.expected() and without_lines(kept) == without_lines(ledger),
    )
    expect(
        "a partial write refuses a count above its row and a new key in the mode lower",
        refuses(
            lambda: rule_ledger.write(count, ledger, [finding("a.py")] * 3, is_partial=True), rule_ledger.LedgerError
        )
        and refuses(
            lambda: rule_ledger.write(count, ledger, [finding("c.py")], is_partial=True), rule_ledger.LedgerError
        ),
    )
    raised = rule_ledger.write(
        count, ledger, [finding("a.py")] * 3 + [finding("c.py")], mode=rule_ledger.WriteMode.RAISE, is_partial=True
    )
    expect(
        "a partial write with --raise raises a row and adds a key, and it keeps each other row",
        raised.expected()
        == {
            rule_ledger.Key("a.py", "kind", ""): 3,
            rule_ledger.Key("b.py", "kind", ""): 1,
            rule_ledger.Key("c.py", "kind", ""): 1,
        },
    )
    admitted = rule_ledger.write(
        admit,
        rule_ledger.empty(admit),
        [finding("a.py", rule_id="admit-rule"), finding("b.py", rule_id="admit-rule")],
        mode=rule_ledger.WriteMode.ADMIT,
        reason="a planted reason",
    )
    expect(
        "a partial write of the policy admit keeps a row that admits no finding of the build",
        rule_ledger.write(admit, admitted, [finding("a.py", rule_id="admit-rule")], is_partial=True).base.keys()
        == admitted.base.keys()
        and rule_ledger.write(admit, admitted, [finding("a.py", rule_id="admit-rule")]).base.keys()
        == {rule_ledger.Key("a.py", "kind", "")},
    )


def reduce_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check THE REDUCE: the largest count of each key over the builds, and the complete write of it."""
    count = table["count-rule"]
    key = rule_ledger.Key
    first = {key("a.py", "kind", ""): 1, key("arm.h", "kind", ""): 0, key("b.py", "kind", ""): 2}
    second = {key("a.py", "kind", ""): 3, key("arm.h", "kind", ""): 1}
    expect(
        "largest_counts() gives the largest count of each key over two builds, with no zero count",
        rule_ledger.largest_counts([first, second])
        == {key("a.py", "kind", ""): 3, key("arm.h", "kind", ""): 1, key("b.py", "kind", ""): 2}
        and rule_ledger.largest_counts([]) == {},
    )
    text = (
        "format 1\nrule count-rule\na.py | kind | 4 | - | -\narm.h | kind | 1 | - | -\nb.py | kind | 2 | - | -\n"
        "gone.py | kind | 1 | - | -\n"
    )
    ledger = rule_ledger.parse(text, "ledger.txt", "count-rule", rule_registry.Policy.COUNT)
    reduced = rule_ledger.write_counts(count, ledger, rule_ledger.largest_counts([first, second]))
    expect(
        "the reduce lowers a row to the largest count, keeps the count of an arm of one build, and removes a key "
        "that no build gives",
        reduced.expected() == {key("a.py", "kind", ""): 3, key("arm.h", "kind", ""): 1, key("b.py", "kind", ""): 2},
    )
    alone = rule_ledger.write_counts(count, ledger, first, is_partial=True)
    expect(
        "the write of one of the two builds lowers no row, because it cannot see the arm of the other build",
        alone.expected() == ledger.expected(),
    )


def admit_row_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check the rows with the value admit of a count ledger (AN ADMIT ROW OF A COUNT LEDGER)."""
    count = table["count-rule"]
    text = "format 1\nrule count-rule\na.py | kind | 1 | - | -\nb.py | kind | admit | name | a planted reason\n"
    ledger = rule_ledger.parse(text, "ledger.txt", "count-rule", rule_registry.Policy.COUNT)
    admit_key = rule_ledger.Key("b.py", "kind", "name")
    expect(
        "a count ledger reads a row with the value admit, which has no count",
        list(ledger.admits()) == [admit_key]
        and admit_key not in ledger.expected()
        and rule_ledger.render(ledger) == text,
    )
    expect(
        "a count ledger with a row with the value admit and no reason is refused",
        refuses(
            partial(
                rule_ledger.parse,
                text.replace("a planted reason", "-"),
                "ledger.txt",
                "count-rule",
                rule_registry.Policy.COUNT,
            ),
            rows_grammar.RowsError,
        ),
    )
    found = [finding("a.py"), finding("b.py", entity="name"), finding("b.py", entity="name")]
    expect(
        "a row with the value admit admits each finding of its key",
        not errors(rule_ledger.compare(count, ledger, found)),
    )
    judged = rule_ledger.compare(count, ledger, [finding("a.py")])
    expect(
        "a row with the value admit that admits no finding of the build gives no finding",
        not errors(judged) and all("b.py" not in item.message for item in judged),
    )
    written = rule_ledger.write(count, ledger, [finding("a.py")])
    expect(
        "a complete write removes a row with the value admit that admits no finding",
        written.admits() == {} and written.expected() == {rule_ledger.Key("a.py", "kind", ""): 1},
    )
    written = rule_ledger.write(count, ledger, found)
    expect(
        "a complete write keeps a row with the value admit that admits a finding, with its value and its reason",
        written.admits()[admit_key].reason == "a planted reason" and written.base[admit_key].value == "admit",
    )
    expect(
        "a partial write keeps a row with the value admit that admits no finding of the build",
        admit_key in rule_ledger.write(count, ledger, [finding("a.py")], is_partial=True).admits(),
    )


def move_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check THE MOVE: an increase that a fall of a file that the change moved lines from pays, in each policy."""
    count, admit = table["count-rule"], table["admit-rule"]
    text = (
        "format 1\nrule count-rule\n"
        "split.h | kind | 3 | vector | the reason of the split header\nother.h | kind | 1 | vector | -\n"
    )
    ledger = rule_ledger.parse(text, "ledger.txt", "count-rule", rule_registry.Policy.COUNT)
    flows = {("split.h", "part_a.h"), ("split.h", "part_b.h")}
    split = [
        *[finding("part_a.h", entity="vector")] * 2,
        finding("part_b.h", entity="vector"),
        finding("other.h", entity="vector"),
    ]
    expect(
        "with no flow, the mode lower refuses the rows of the new files of a split",
        refuses(lambda: rule_ledger.write(count, ledger, split), rule_ledger.LedgerError),
    )
    written = rule_ledger.write(count, ledger, split, flows=flows)
    expect(
        "a fall of the divided file pays the increases of the two new files, and the mode lower writes them",
        written.expected()
        == {
            rule_ledger.Key("part_a.h", "kind", "vector"): 2,
            rule_ledger.Key("part_b.h", "kind", "vector"): 1,
            rule_ledger.Key("other.h", "kind", "vector"): 1,
        },
    )
    expect(
        "a new row of a moved finding gets the reason of the row of its fall",
        written.base[rule_ledger.Key("part_a.h", "kind", "vector")].reason == "the reason of the split header",
    )
    expect(
        "a fall pays one time: the same split with one more finding is refused",
        refuses(
            lambda: rule_ledger.write(count, ledger, [*split, finding("part_b.h", entity="vector")], flows=flows),
            rule_ledger.LedgerError,
        ),
    )
    expect(
        "a fall pays only an increase of the same kind and entity",
        refuses(
            lambda: rule_ledger.write(count, ledger, [finding("part_a.h", entity="map")], flows=flows),
            rule_ledger.LedgerError,
        ),
    )
    expect(
        "a fall of a file that the change moved no line from pays nothing, and a file that keeps its count pays nothing",
        refuses(
            lambda: rule_ledger.write(
                count,
                ledger,
                [finding("part_a.h", entity="vector"), finding("other.h", entity="vector")],
                flows={("other.h", "part_a.h")},
            ),
            rule_ledger.LedgerError,
        ),
    )
    partial_split = rule_ledger.write(count, ledger, split, flows=flows, is_partial=True)
    expect(
        "a partial write writes the paid increases with no --raise, and it keeps the row of the divided file",
        partial_split.expected()
        == {
            rule_ledger.Key("split.h", "kind", "vector"): 3,
            rule_ledger.Key("part_a.h", "kind", "vector"): 2,
            rule_ledger.Key("part_b.h", "kind", "vector"): 1,
            rule_ledger.Key("other.h", "kind", "vector"): 1,
        },
    )
    expect(
        "move_payments gives each part of a fall that pays an increase, in the sequence of the paths",
        rule_ledger.move_payments(ledger.expected(), written.expected(), flows)
        == (
            rule_ledger.Payment(
                rule_ledger.Key("split.h", "kind", "vector"), rule_ledger.Key("part_a.h", "kind", "vector"), 2
            ),
            rule_ledger.Payment(
                rule_ledger.Key("split.h", "kind", "vector"), rule_ledger.Key("part_b.h", "kind", "vector"), 1
            ),
        ),
    )
    admitted = rule_ledger.write(
        admit,
        rule_ledger.empty(admit),
        [finding("split.h", entity="door", rule_id="admit-rule")],
        mode=rule_ledger.WriteMode.ADMIT,
        reason="the reason of the door",
    )
    moved_door = [finding("part_a.h", entity="door", rule_id="admit-rule")]
    expect(
        "the policy admit refuses a new row with no flow",
        refuses(lambda: rule_ledger.write(admit, admitted, moved_door), rule_ledger.LedgerError),
    )
    expect(
        "the policy admit writes a moved row with the reason of the row of its fall",
        rule_ledger.write(admit, admitted, moved_door, flows=flows).base
        == {
            rule_ledger.Key("part_a.h", "kind", "door"): rule_ledger.Entry(
                rule_ledger.Key("part_a.h", "kind", "door"), rule_ledger.ADMIT_VALUE, "the reason of the door"
            )
        },
    )


def before_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check THE LEDGER BEFORE THE CHANGE: a fall pays one time, also after a partial write kept its row."""
    count = table["count-rule"]
    text = "format 1\nrule count-rule\nsplit.h | kind | 1 | vector | -\n"
    ledger = rule_ledger.parse(text, "ledger.txt", "count-rule", rule_registry.Policy.COUNT)
    flows = {("split.h", "part_a.h"), ("split.h", "part_b.h")}
    first = rule_ledger.write(
        count, ledger, [finding("part_a.h", entity="vector")], flows=flows, before=ledger, is_partial=True
    )
    expect(
        "a partial write of a move keeps the row of the source and writes the paid row",
        first.expected()
        == {rule_ledger.Key("split.h", "kind", "vector"): 1, rule_ledger.Key("part_a.h", "kind", "vector"): 1},
    )
    second_found = [finding("part_a.h", entity="vector"), finding("part_b.h", entity="vector")]
    expect(
        "a second write of the change pays against the ledger before the change, so the one fall pays one time",
        refuses(
            lambda: rule_ledger.write(count, first, second_found, flows=flows, before=ledger, is_partial=True),
            rule_ledger.LedgerError,
        ),
    )
    expect(
        "with no ledger before the change, the kept row of the source pays again, which the ledger before prevents",
        rule_ledger.write(count, first, second_found, flows=flows, is_partial=True)
        .expected()
        .get(rule_ledger.Key("part_b.h", "kind", "vector"))
        == 1,
    )
    again = rule_ledger.write(
        count, first, [finding("part_a.h", entity="vector")], flows=flows, before=ledger, is_partial=True
    )
    expect("the same write again gives the same ledger", rule_ledger.render(again) == rule_ledger.render(first))
    read_back = rule_ledger.ledger_before(count, "abc123", text.encode())
    expect(
        "ledger_before reads the text at the base: None with no base or a bad text, an empty ledger with no file",
        read_back is not None
        and read_back.expected() == ledger.expected()
        and rule_ledger.ledger_before(count, None, text.encode()) is None
        and rule_ledger.ledger_before(count, "abc123", b"format 9\n") is None
        and rule_ledger.ledger_before(count, "abc123", None) == rule_ledger.empty(count),
    )


def namespace_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check the second half of THE MOVE: a move of code to a different namespace of the tree changes no finding."""
    old = "foundation::algebra::transition::read(const foundation::algebra::transition::node&): std::meta::info"
    new = "fixy::session::transition::read(const fixy::session::transition::node&): std::meta::info"
    expect(
        "the qualifiers of the tree start at each root namespace, and a name of the library starts none",
        rule_ledger.tree_qualifiers("::fixy::session::read(std::meta::info)") == {"fixy::", "fixy::session::"}
        and rule_ledger.tree_qualifiers("std::vector") == frozenset()
        and rule_ledger.tree_qualifiers("my_fixy::read") == frozenset(),
    )
    expect(
        "one replacement of a qualifier at each place gives an entity that agrees",
        rule_ledger.entities_agree(old, new) and rule_ledger.entities_agree(new, old),
    )
    expect(
        "two replacements, a different last name and a namespace of the library give no agreement",
        not rule_ledger.entities_agree(old, new.replace("const fixy::session::", "const crucible::"))
        and not rule_ledger.entities_agree(old, new.replace("::read(", "::write("))
        and not rule_ledger.entities_agree("std::vector", "boost::vector"),
    )
    count, admit = table["count-rule"], table["admit-rule"]
    text = (
        "format 1\nrule count-rule\n"
        f"old.h | kind | 1 | {old} | the reason of the walk\nuser.h | kind | 1 | ::{old} | -\n"
    )
    ledger = rule_ledger.parse(text, "ledger.txt", "count-rule", rule_registry.Policy.COUNT)
    moved = [finding("new.h", entity=new), finding("user.h", entity=f"::{new}")]
    expect(
        "with no flow, the entity in the new file has no row, and the mode lower refuses it",
        refuses(lambda: rule_ledger.write(count, ledger, moved), rule_ledger.LedgerError),
    )
    written = rule_ledger.write(count, ledger, moved, flows={("old.h", "new.h")})
    expect(
        "a fall pays an entity that agrees along a pair, and in its own file with no pair",
        written.expected()
        == {rule_ledger.Key("new.h", "kind", new): 1, rule_ledger.Key("user.h", "kind", f"::{new}"): 1}
        and written.base[rule_ledger.Key("new.h", "kind", new)].reason == "the reason of the walk",
    )
    expect(
        "an entity that does not agree stays a new row",
        refuses(
            lambda: rule_ledger.write(
                count, ledger, [finding("new.h", entity="std::vector")], flows={("old.h", "new.h")}
            ),
            rule_ledger.LedgerError,
        ),
    )
    admitted = rule_ledger.write(
        admit,
        rule_ledger.empty(admit),
        [finding("old.h", entity=old, rule_id="admit-rule")],
        mode=rule_ledger.WriteMode.ADMIT,
        reason="the reason of the walk",
    )
    expect(
        "the policy admit moves a row to an entity that agrees, with its reason",
        rule_ledger.write(
            admit, admitted, [finding("new.h", entity=new, rule_id="admit-rule")], flows={("old.h", "new.h")}
        ).base
        == {
            rule_ledger.Key("new.h", "kind", new): rule_ledger.Entry(
                rule_ledger.Key("new.h", "kind", new), rule_ledger.ADMIT_VALUE, "the reason of the walk"
            )
        },
    )


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
    partial_cases(expect, table)
    reduce_cases(expect, table)
    admit_row_cases(expect, table)
    move_cases(expect, table)
    before_cases(expect, table)
    namespace_cases(expect, table)
    if failures:
        print(f"tests.core.test_ledger: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("tests.core.test_ledger: every case holds.")
    return 0
