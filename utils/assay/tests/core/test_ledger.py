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
MAX_RULE = rule_registry.measured_rule("max-rule", 2, "s")
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
    expect("findings at their count give no finding: an admitted row gives no warning", judged == [])
    kept = rule_ledger.write(
        count,
        rule_ledger.empty(count),
        [finding("a.py"), finding("a.py")],
        mode=rule_ledger.WriteMode.ADMIT,
        reason="a planted reason",
    )
    expect(
        "a keep row, a count row with a reason, gives no finding either",
        rule_ledger.compare(count, kept, [finding("a.py"), finding("a.py")]) == [],
    )
    judged = rule_ledger.compare(count, ledger, [finding("a.py")] * 3)
    expect("a count above its row is an error at each finding", len(errors(judged)) == 3)
    judged = rule_ledger.compare(count, ledger, [finding("a.py")])
    expect("a count below its row gives no finding", judged == [])
    judged = rule_ledger.compare(count, ledger, [])
    expect("a count row with no finding of the build gives no finding", judged == [])
    judged = rule_ledger.compare(count, ledger, [*([finding("a.py")] * 2), finding("new.py")])
    expect(
        "a finding in a path that the ledger does not hold is an error",
        any(item.path == "new.py" and "A new path has no finding" in item.message for item in errors(judged)),
    )
    judged = rule_ledger.compare(count, ledger, [*([finding("a.py")] * 2), finding("a.py", level="warning")])
    expect(
        "a warning of the rule passes with no change, and it is the one finding",
        [item.level for item in judged] == ["warning"],
    )
    admit = table["admit-rule"]
    ledger = rule_ledger.write(
        admit,
        rule_ledger.empty(admit),
        [finding("a.py", rule_id="admit-rule")],
        mode=rule_ledger.WriteMode.ADMIT,
        reason="a planted reason",
    )
    judged = rule_ledger.compare(admit, ledger, [finding("a.py", rule_id="admit-rule")])
    expect("an admitted finding gives no finding", judged == [])
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


def prefix_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check the reason rows of a prefix: the reader, the longest prefix, the verdict and the write."""
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
        "reason_of() gives the reason of a row of its own and reads no prefix",
        ledger.reason_of(key("src/c.py", "kind", "")) == "an own reason"
        and ledger.reason_of(key("src/a.py", "kind", "")) == "",
    )
    expect(
        "a prefix with a final slash holds each path under it, and a prefix with no final slash only the equal path",
        any(item.holds("src/deep/b.py") for item in ledger.reasons)
        and any(item.holds("c.py") for item in ledger.reasons)
        and not any(item.holds("c.py.old") or item.holds("srcx/a.py") for item in ledger.reasons),
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
    expect("a reason row changes no verdict: the counts at their rows give no finding", judged == [])
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
    """Check the modes of a complete write, and the raise that a moved finding needs."""
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
    moved = [finding("new/a.py")] * 2
    expect(
        "the write reads no move: a finding of a new path needs the mode raise, and a partial write keeps the old row",
        refuses(lambda: rule_ledger.write(count, ledger, moved, is_partial=True), rule_ledger.LedgerError)
        and rule_ledger.write(count, ledger, moved, mode=rule_ledger.WriteMode.RAISE, is_partial=True).expected()
        == {rule_ledger.Key("a.py", "kind", ""): 2, rule_ledger.Key("new/a.py", "kind", ""): 2},
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


def merge_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check THE MERGE: the side that changed each row, the sum of two count changes of one key, and the conflicts."""
    count = table["count-rule"]
    key = rule_ledger.Key
    head = "format 1\nrule count-rule\n"
    base = f"{head}a.py | kind | 4 | - | -\nb.py | kind | 2 | - | -\nc.py | kind | 1 | - | -\n"
    ours = f"{head}a.py | kind | 6 | - | -\nb.py | kind | 1 | - | -\nc.py | kind | 1 | - | -\n"
    theirs = (
        f"{head}a.py | kind | 9 | - | -\nb.py | kind | 2 | - | -\nc.py | kind | 1 | - | -\nd.py | kind | 3 | - | -\n"
    )
    merged = rule_ledger.merge(count, (base, ours, theirs), "ledger.txt")
    parsed = rule_ledger.parse(merged.text or "", "ledger.txt", "count-rule", rule_registry.Policy.COUNT)
    expect(
        "two changes of one count merge to ours plus theirs less the base, a row that one side changed takes that "
        "side, and merge() gives each summed key",
        parsed.expected()
        == {
            key("a.py", "kind", ""): 11,
            key("b.py", "kind", ""): 1,
            key("c.py", "kind", ""): 1,
            key("d.py", "kind", ""): 3,
        }
        and merged.summed == (key("a.py", "kind", ""),),
    )
    falls = rule_ledger.merge(
        count,
        (base, f"{head}a.py | kind | 1 | - | -\nc.py | kind | 1 | - | -\n", f"{head}c.py | kind | 1 | - | -\n"),
        "ledger.txt",
    )
    expect(
        "a merged count of 0 or less removes the row, and a ledger of no row merges to no file",
        rule_ledger.merge(
            count, (base, f"{head}c.py | kind | 1 | - | -\n", f"{head}c.py | kind | 1 | - | -\n"), "ledger.txt"
        ).text
        == f"{head}c.py | kind | 1 | - | -\n"
        and falls.text == f"{head}c.py | kind | 1 | - | -\n"
        and rule_ledger.merge(count, (base, None, None), "ledger.txt").text is None,
    )
    reasoned = f"format 1\nrule count-rule\nreason d.py | a planted step\n{theirs.removeprefix(head)}"
    with_reason = rule_ledger.merge(count, (base, ours, reasoned), "ledger.txt")
    expect(
        "a reason row of theirs stays in the merged ledger",
        "reason d.py | a planted step" in (with_reason.text or ""),
    )
    reasons = f"{head}a.py | kind | 6 | - | one reason\nb.py | kind | 2 | - | -\nc.py | kind | 1 | - | -\n"
    other = f"{head}a.py | kind | 9 | - | a different reason\nb.py | kind | 2 | - | -\nc.py | kind | 1 | - | -\n"
    expect(
        "two different reasons of one key are a conflict",
        refuses(partial(rule_ledger.merge, count, (base, reasons, other), "ledger.txt"), rule_ledger.LedgerError),
    )
    admit = table["admit-rule"]
    admit_head = "format 1\nrule admit-rule\n"
    expect(
        "a ledger of the policy admit merges no sum: two different reasons of one key are a conflict",
        refuses(
            partial(
                rule_ledger.merge,
                admit,
                (
                    None,
                    f"{admit_head}a.py | kind | admit | - | ours\n",
                    f"{admit_head}a.py | kind | admit | - | theirs\n",
                ),
                "ledger.txt",
            ),
            rule_ledger.LedgerError,
        ),
    )
    expect(
        "ours in a form that the writer does not give is refused",
        refuses(
            partial(rule_ledger.merge, count, (base, ours.replace("a.py | kind", "a.py  | kind"), theirs), "x"),
            rule_ledger.LedgerError,
        ),
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
    merge_cases(expect, table)
    if failures:
        print(f"tests.core.test_ledger: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("tests.core.test_ledger: every case holds.")
    return 0
