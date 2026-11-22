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

from assay.core import git
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


def reason_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check the reason of a raised row, and the write of the reason of named rows (THE REASON OF A ROW)."""
    count = table["count-rule"]
    two = [finding("a.py"), finding("a.py"), finding("b.py"), finding("b.py")]
    ledger = rule_ledger.write(count, rule_ledger.empty(count), two, mode=rule_ledger.WriteMode.ADMIT, reason="first")
    a_key = rule_ledger.Key("a.py", "kind", "")
    b_key = rule_ledger.Key("b.py", "kind", "")
    raised = rule_ledger.write(
        count, ledger, two + [finding("a.py")], mode=rule_ledger.WriteMode.RAISE, reason="a moved finding"
    )
    expect(
        "a raise with a reason gives the raised row that reason, and the row that holds keeps its own",
        raised.base[a_key].value == 3
        and raised.base[a_key].reason == "a moved finding"
        and raised.base[b_key].reason == "first",
    )
    unexplained = rule_ledger.write(count, ledger, two + [finding("a.py")], mode=rule_ledger.WriteMode.RAISE)
    expect("a raise with no reason keeps the reason of the raised row", unexplained.base[a_key].reason == "first")
    per_row = rule_ledger.write(
        count,
        raised,
        two + [finding("a.py")] + [finding("b.py")] * 2,
        mode=rule_ledger.WriteMode.RAISE,
        reason="a second moved finding",
    )
    expect(
        "two raises with two reasons keep the reason of each row",
        per_row.base[a_key].reason == "a moved finding" and per_row.base[b_key].reason == "a second moved finding",
    )
    renamed = rule_ledger.reasoned(ledger, rule_ledger.RowReason(("b.py",), "", "a corrected reason"))
    expect(
        "a write of the reason of a count row changes its reason and not its count, and keeps each other row",
        renamed.base[b_key].reason == "a corrected reason"
        and renamed.expected() == ledger.expected()
        and renamed.base[a_key] == ledger.base[a_key],
    )
    expect(
        "an item that names no row, a kind that no named row has, and a request with no reason are refused",
        refuses(
            lambda: rule_ledger.reasoned(ledger, rule_ledger.RowReason(("c.py",), "", "x")), rule_ledger.LedgerError
        )
        and refuses(
            lambda: rule_ledger.reasoned(ledger, rule_ledger.RowReason(("b.py",), "other", "x")),
            rule_ledger.LedgerError,
        )
        and refuses(
            lambda: rule_ledger.reasoned(ledger, rule_ledger.RowReason(("b.py",), "", "")), rule_ledger.LedgerError
        ),
    )
    shared_text = (
        "format 1\nrule count-rule\n"
        "a.py | kind | 2 | namespace-scope: n::detail | the reason of a\n"
        "a.py | kind | 1 | namespace-scope: m::detail | the second reason of a\n"
        "b.py | kind | 3 | namespace-scope: n::detail | the reason of b\n"
    )
    shared = rule_ledger.parse(shared_text, "ledger.txt", "count-rule", rule_registry.Policy.COUNT)
    expect(
        "an entity that names rows of two files and a path that names two rows are refused, so no reason of a "
        "different row changes",
        refuses(
            lambda: rule_ledger.reasoned(shared, rule_ledger.RowReason(("namespace-scope: n::detail",), "", "x")),
            rule_ledger.LedgerError,
        )
        and refuses(
            lambda: rule_ledger.reasoned(shared, rule_ledger.RowReason(("a.py",), "kind", "x")),
            rule_ledger.LedgerError,
        ),
    )
    one_key = rule_ledger.Key("a.py", "kind", "namespace-scope: n::detail")
    named = rule_ledger.reasoned(
        shared, rule_ledger.RowReason(("a.py | namespace-scope: n::detail",), "", "a corrected reason")
    )
    expect(
        "an item 'PATH | ENTITY' changes the reason of that one row, and the row of the same entity in a different "
        "file and the row of the same file with a different entity keep their reasons",
        named.base[one_key].reason == "a corrected reason"
        and {key: entry for key, entry in named.base.items() if key != one_key}
        == {key: entry for key, entry in shared.base.items() if key != one_key},
    )
    maximum = table["max-rule"]
    test_key = rule_ledger.Key("", "test", "slow_test")
    output_key = rule_ledger.Key("@build/test/slow_test", "test", "")
    kept = replace(
        rule_ledger.empty(maximum),
        base={
            test_key: rule_ledger.Entry(test_key, 3.5, "a stale reason"),
            output_key: rule_ledger.Entry(output_key, 2.5, "a reason of an output"),
        },
    )
    corrected = rule_ledger.reasoned(kept, rule_ledger.RowReason(("slow_test",), "test", "the measured cause"))
    expect(
        "a write of the reason of a kept row of a max ledger names a row with no path by its entity, and changes the "
        "reason and not the value",
        corrected.base[test_key] == rule_ledger.Entry(test_key, 3.5, "the measured cause")
        and corrected.base[output_key] == kept.base[output_key],
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
    equal_sides = (base, ours.replace("a.py | kind | 6", "a.py | kind | 5"), theirs.replace("kind | 9", "kind | 5"))
    agreed = rule_ledger.merge(count, equal_sides, "ledger.txt")
    added = rule_ledger.merge(count, equal_sides, "ledger.txt", rule_ledger.EqualChange.SUM)
    expect(
        "two sides that raise one count to one value agree in the mode AGREE, and sum to the base plus the two raises "
        "in the mode SUM, as a summed key",
        "a.py | kind | 5 | - | -" in (agreed.text or "")
        and agreed.summed == ()
        and "a.py | kind | 6 | - | -" in (added.text or "")
        and added.summed == (key("a.py", "kind", ""),),
    )
    joined = rule_ledger.merge(count, (base, reasons, other), "ledger.txt", rule_ledger.EqualChange.SUM)
    same_reason = rule_ledger.merge(
        count, (base, reasons, reasons.replace("kind | 6", "kind | 7")), "x", rule_ledger.EqualChange.SUM
    )
    expect(
        "in the mode SUM, two reasons of one summed key join, ours first, and one changed reason stays",
        "a.py | kind | 11 | - | one reason.  a different reason" in (joined.text or "")
        and "a.py | kind | 9 | - | one reason" in (same_reason.text or ""),
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
    removed = rule_ledger.merge(count, (base, f"{head}b.py | kind | 2 | - | -\nc.py | kind | 1 | - | -\n", theirs), "x")
    expect(
        "a count row that one side removes and the other side changes merges by the sum, with the count 0 for the "
        "removed row, and it is a summed key",
        "a.py | kind | 5 | - | -" in (removed.text or "") and key("a.py", "kind", "") in removed.summed,
    )
    rows = [line.split(" | ")[0] for line in (merged.text or "").splitlines()[2:]]
    expect(
        "the merged ledger has its rows in the sorted sequence of render(), with the rows of the two sides between "
        "each other",
        merged.text == rule_ledger.render(parsed) and rows == ["a.py", "b.py", "c.py", "d.py"],
    )
    max_merge_cases(expect, table)


def max_text(rows: dict[str, tuple[str, str]]) -> str:
    """Return the planted ledger of the rule max-rule: the value and the reason of each path, sorted."""
    entries = "".join(f"{path} | kind | {value} | - | {why}\n" for path, (value, why) in sorted(rows.items()))
    return f"format 1\nrule max-rule\n{entries}"


def max_merge_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check THE MERGE of a max ledger: the larger of two raised rows, and a conflict for a row that one side lowers."""
    rule = table["max-rule"]
    kept = {"b.o": ("20", "base b"), "c.o": ("30", "base c")}
    base = max_text({"a.o": ("10", "base a"), **kept})
    ours = max_text({"a.o": ("12", "ours a"), "b.o": ("25", "ours b"), "c.o": ("30", "base c")})
    theirs = max_text(
        {"a.o": ("11", "theirs a"), "b.o": ("27", "theirs b"), "c.o": ("31", "theirs c"), "d.o": ("5", "d")}
    )
    merged = rule_ledger.merge(rule, (base, ours, theirs), "ledger.txt")
    wanted = {"a.o": ("12", "ours a"), "b.o": ("27", "theirs b"), "c.o": ("31", "theirs c"), "d.o": ("5", "d")}
    expect(
        "two raises of one row of a max ledger merge to the larger value with the reason of its side, a row that "
        "one side changed takes that side, and no key of a max ledger is a summed key",
        merged.text == max_text(wanted) and merged.summed == (),
    )
    tie = rule_ledger.merge(
        rule, (base, max_text({"a.o": ("12", "ours a"), **kept}), max_text({"a.o": ("12", "theirs a"), **kept})), "x"
    )
    expect(
        "two raises of one row to the same value take the reason of ours",
        "a.o | kind | 12 | - | ours a" in (tie.text or ""),
    )
    reasons = (base, max_text({"a.o": ("10", "ours a"), **kept}), max_text({"a.o": ("10", "theirs a"), **kept}))
    expect(
        "two changes of only the reason of one max row, with no raise, are a conflict, as in the other policies",
        refuses(partial(rule_ledger.merge, rule, reasons, "x"), rule_ledger.LedgerError),
    )
    added = rule_ledger.merge(rule, (None, max_text({"a.o": ("3", "ours")}), max_text({"a.o": ("4", "theirs")})), "x")
    expect("two new rows of one key merge to the larger value", added.text == max_text({"a.o": ("4", "theirs")}))
    lowered = max_text({"a.o": ("9", "a fall"), **kept})
    removed = max_text(kept)
    expect(
        "a row of a max ledger that one side lowers below the base and the other side raises is a conflict",
        refuses(partial(rule_ledger.merge, rule, (base, lowered, theirs), "x"), rule_ledger.LedgerError),
    )
    expect(
        "a row of a max ledger that one side removes and the other side changes is a conflict, on each side",
        refuses(partial(rule_ledger.merge, rule, (base, removed, theirs), "x"), rule_ledger.LedgerError)
        and refuses(partial(rule_ledger.merge, rule, (base, theirs, removed), "x"), rule_ledger.LedgerError),
    )
    expect(
        "a row of a max ledger that the two sides remove goes, and a row that one side removes and the other keeps goes",
        rule_ledger.merge(rule, (base, removed, removed), "x").text == removed
        and rule_ledger.merge(rule, (base, removed, base), "x").text == removed,
    )


def count_text(counts: dict[str, int]) -> str:
    """Return the planted ledger of the rule count-rule with the count of each path, sorted."""
    entries = "".join(f"{path} | kind | {count} | - | -\n" for path, count in sorted(counts.items()))
    return f"format 1\nrule count-rule\n{entries}"


def rebased(work: Path, ledgers: tuple[str, str, str], driver: str) -> tuple[int, str]:
    """Rebase a planted branch onto a planted main, and return the exit of the rebase and the ledger of the work tree.

    The three texts are the ledger of count-rule in the base, on main and on the branch.  The repository has the
    attribute merge=ledger for its ledgers, and the configuration of the driver is `driver`, or none for "".
    """
    root = work / "repo"
    root.mkdir()
    git.init(root)
    ledger_file = root / rule_ledger.shown_path("count-rule")
    ledger_file.parent.mkdir(parents=True)
    (root / rule_registry.REGISTRY_PATH).write_text(rule_registry.planted([COUNT_RULE]), encoding="utf-8")
    (root / ".gitattributes").write_text("utils/ledgers/*.txt merge=ledger\n", encoding="utf-8")
    ledger_file.write_text(ledgers[0], encoding="utf-8")
    base = git.commit_all(root, "Plant the base")
    main_branch = git.run(root, "rev-parse", "--abbrev-ref", "HEAD").decode().strip()
    ledger_file.write_text(ledgers[2], encoding="utf-8")
    git.run(root, "add", "-A")
    tree = git.run(root, "write-tree").decode().strip()
    topic = git.run(root, "commit-tree", tree, "-p", base, "-m", "Change the branch", extra=git.SCRATCH_IDENTITY)
    git.run(root, "update-ref", "refs/heads/topic", topic.decode().strip())
    ledger_file.write_text(ledgers[1], encoding="utf-8")
    git.commit_all(root, "Change main")
    configured = ("-c", f"merge.ledger.driver={driver}") if driver else ()
    completed = git.run_any_exit(root, *configured, "rebase", main_branch, "topic", extra=git.SCRATCH_IDENTITY)
    return completed.exit, ledger_file.read_text(encoding="utf-8")


def driver_cases(expect: Expect) -> None:
    """Check the merge driver of git in a planted rebase (THE DRIVER of assay.guard.merge_ledger)."""
    guard = Path(__file__).resolve().parents[3] / "bin" / "guard"
    driver = f"{sys.executable} {guard} merge-ledger %O %A %B %P %S %X %Y"
    base = count_text({"a.py": 4, "b.py": 2, "c.py": 1})
    adjacent = (base, count_text({"a.py": 5, "b.py": 2, "c.py": 1}), count_text({"a.py": 4, "b.py": 3, "c.py": 1}))
    with tempfile.TemporaryDirectory(prefix="ledger-driver-") as work:
        status, text = rebased(Path(work), adjacent, driver)
        expect(
            "in a rebase with the driver, changes of adjacent keys merge by the keys, and the rebase ends",
            status == 0 and text == count_text({"a.py": 5, "b.py": 3, "c.py": 1}),
        )
    with tempfile.TemporaryDirectory(prefix="ledger-driver-") as work:
        status, text = rebased(Path(work), adjacent, "")
        expect(
            "with the attribute and no configuration of the driver, git merges the ledger by its lines, and the "
            "adjacent changes conflict",
            status != 0 and "<<<<<<<" in text,
        )
    summed = (base, count_text({"a.py": 5, "b.py": 2, "c.py": 1}), count_text({"a.py": 7, "b.py": 2, "c.py": 1}))
    with tempfile.TemporaryDirectory(prefix="ledger-driver-") as work:
        status, text = rebased(Path(work), summed, driver)
        expect(
            "the driver refuses a key whose counts merge by a sum, and it leaves the conflict markers of git",
            status != 0 and "<<<<<<<" in text and "a.py | kind | 7" in text and "a.py | kind | 5" in text,
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
    reason_cases(expect, table)
    partial_cases(expect, table)
    reduce_cases(expect, table)
    admit_row_cases(expect, table)
    merge_cases(expect, table)
    driver_cases(expect)
    if failures:
        print(f"tests.core.test_ledger: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("tests.core.test_ledger: every case holds.")
    return 0
