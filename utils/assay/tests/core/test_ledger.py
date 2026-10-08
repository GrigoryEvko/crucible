"""The test of the ledgers (assay.core.ledger): the reader, the policies, the writes, the configurations and the history.

`guard selftest tests.core.test_ledger` runs the test.  Each case prints one
line.  Exit 0 when each case holds, else 1.
"""

import sys
import tempfile
from collections.abc import Callable, Mapping
from dataclasses import replace
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


def finding(path: str, kind: str = "kind", entity: str = "", level: str = "error",
            rule_id: str = "count-rule") -> check_report.Finding:
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
    text = ("# the head\nformat 1\nrule count-rule\nconfiguration base machine x86_64\nconfiguration arm machine "
            "aarch64\na.py | kind | 2 | - | -\nb.py | kind | 1 | name | a reason\n@arm | a.py | kind | -1 | - | -\n")
    ledger = rule_ledger.parse(text, "ledger.txt", "count-rule", rule_registry.Policy.COUNT)
    expect("a ledger reads its configurations, its base rows and its difference rows",
           ledger.base_name == "base" and len(ledger.base) == 2 and len(ledger.deltas["arm"]) == 1)
    expect("expected() adds the differences of a configuration",
           ledger.expected("arm") == {rule_ledger.Key("a.py", "kind", ""): 1, rule_ledger.Key("b.py", "kind", "name"): 1}
           and ledger.expected()[rule_ledger.Key("a.py", "kind", "")] == 2)
    expect("render() gives the same text again", rule_ledger.render(ledger) == text)
    bad = {
        "a second row of a key": text.replace("b.py | kind | 1 | name", "a.py | kind | 1 | -"),
        "a count of 0": text.replace("a.py | kind | 2", "a.py | kind | 0"),
        "a difference that makes a count below 0": text.replace("-1 | - | -", "-3 | - | -"),
        "a wrong rule row": text.replace("rule count-rule", "rule other-rule"),
        "a configuration after an entry row": text + "configuration late machine x\n",
    }
    for name, planted in bad.items():
        expect(f"a ledger with {name} is refused",
               refuses(lambda planted=planted: rule_ledger.parse(planted, "ledger.txt", "count-rule",
                                                                 rule_registry.Policy.COUNT), rows_grammar.RowsError))
    admit_text = "format 1\nrule admit-rule\na.py | kind | admit | - | -\n"
    expect("a row of the policy admit with no reason is refused",
           refuses(lambda: rule_ledger.parse(admit_text, "ledger.txt", "admit-rule", rule_registry.Policy.ADMIT),
                   rows_grammar.RowsError))
    built = rule_ledger.parse(text + "@build/gen/table.c | kind | 1 | - | -\n", "ledger.txt", "count-rule",
                              rule_registry.Policy.COUNT)
    expect("a row of a file of the build directory is a base row, and @NAME with no slash is a difference row",
           built.base.get(rule_ledger.Key("@build/gen/table.c", "kind", "")) is not None
           and len(built.deltas["arm"]) == 1
           and without_lines(rule_ledger.parse(rule_ledger.render(built), "again.txt", "count-rule",
                                               rule_registry.Policy.COUNT)) == without_lines(built))
    nameless = rule_ledger.parse(text + "- | kind | 1 | name | -\n@arm | - | kind | +1 | name | -\n", "ledger.txt",
                                 "count-rule", rule_registry.Policy.COUNT)
    expect("the PATH '-' is the empty path in a base row and in a difference row, and it renders as '-' again",
           rule_ledger.Key("", "kind", "name") in nameless.base and rule_ledger.Key("", "kind", "name") in
           nameless.deltas["arm"] and "\n- | kind | 1 | name | -\n" in rule_ledger.render(nameless))
    expect("count_rows() counts the entry rows of format 1 and each line of an earlier format",
           rule_ledger.count_rows(text) == 3 and rule_ledger.count_rows("# head\na | b\nc | d\n") == 2
           and rule_ledger.count_rows(None) is None)
    with tempfile.TemporaryDirectory(prefix="test-ledger-") as scratch:
        root = Path(scratch)
        (root / rule_ledger.LEDGER_DIRECTORY).mkdir(parents=True)
        changed = rule_ledger.store(ledger, root)
        again = rule_ledger.store(ledger, root)
        read_back = rule_ledger.read(table["count-rule"], root)
        removed = rule_ledger.store(rule_ledger.empty(table["count-rule"]), root)
        expect("store() writes a file, keeps the same bytes, and removes the file of a ledger with no row",
               changed and not again and read_back == ledger and removed
               and not rule_ledger.ledger_path("count-rule", root).exists())
        planted = root / "planted" / "ledger.txt"
        planted.parent.mkdir()
        stored = rule_ledger.store(ledger, file=planted)
        planted_back = rule_ledger.read(table["count-rule"], file=planted)
        expect("read() and store() take the file of a planted ledger, and the reader names that file",
               stored and planted_back.base == ledger.base and not rule_ledger.ledger_path("count-rule", root).exists()
               and rule_ledger.read(table["count-rule"], file=root / "absent.txt").base == {})
    expect("rule_of() gives a rule of the registry and refuses an unknown rule",
           rule_ledger.rule_of("history").id == "history"
           and refuses(lambda: rule_ledger.rule_of("no-such-rule"), rule_ledger.LedgerError))


def without_lines(ledger: rule_ledger.Ledger) -> rule_ledger.Ledger:
    """Return a ledger with the line of each row at 0, so that two texts with the same rows in another order compare equal."""
    def table(entries: Mapping[rule_ledger.Key, rule_ledger.Entry]) -> dict[rule_ledger.Key, rule_ledger.Entry]:
        return {key: replace(entry, line=0) for key, entry in entries.items()}

    return replace(ledger, base=table(ledger.base),
                   deltas={name: table(entries) for name, entries in ledger.deltas.items()})


def policy_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check compare() for the policies count and admit."""
    count = table["count-rule"]
    ledger = rule_ledger.write(count, rule_ledger.empty(count), [finding("a.py"), finding("a.py")],
                               mode=rule_ledger.WriteMode.ADMIT, reason="a planted reason")
    judged = rule_ledger.compare(count, ledger, [finding("a.py"), finding("a.py")])
    expect("findings at their count give one warning for each path, and no error",
           not errors(judged) and len(judged) == 1 and "keeps 2 findings" in judged[0].message)
    expect("the warning of the policy count tells the reader to remove a finding and to lower its count",
           "lower its count" in judged[0].message and "guard write --rule count-rule" in judged[0].message)
    judged = rule_ledger.compare(count, ledger, [finding("a.py")] * 3)
    expect("a count above its row is an error at each finding", len(errors(judged)) == 3)
    judged = rule_ledger.compare(count, ledger, [finding("a.py")])
    expect("a count below its row is an error at the ledger, which asks for a write",
           len(errors(judged)) == 1 and errors(judged)[0].path == rule_ledger.shown_path("count-rule"))
    judged = rule_ledger.compare(count, ledger, [*([finding("a.py")] * 2), finding("new.py")])
    expect("a finding in a file that the ledger does not hold is an error",
           any(item.path == "new.py" and "A new file has no finding" in item.message for item in errors(judged)))
    judged = rule_ledger.compare(count, ledger, [*([finding("a.py")] * 2), finding("a.py", level="warning")])
    expect("a warning of the rule passes with no change", sum(item.level == "warning" for item in judged) == 2)
    admit = table["admit-rule"]
    ledger = rule_ledger.write(admit, rule_ledger.empty(admit), [finding("a.py", rule_id="admit-rule")],
                               mode=rule_ledger.WriteMode.ADMIT, reason="a planted reason")
    judged = rule_ledger.compare(admit, ledger, [finding("a.py", rule_id="admit-rule")])
    expect("an admitted finding gives a warning", not errors(judged) and len(judged) == 1)
    expect("the warning of the policy admit tells that each row gives its reason, and asks for no removal",
           "Each row gives its reason" in judged[0].message and "Remove" not in judged[0].message
           and "lower" not in judged[0].message)
    judged = rule_ledger.compare(admit, ledger, [])
    expect("a row that admits no finding is an error", any("admits no finding" in item.message
                                                           for item in errors(judged)))
    judged = rule_ledger.compare(admit, ledger, [finding("b.py", rule_id="admit-rule")])
    expect("a finding with no row is an error", any(item.path == "b.py" for item in errors(judged)))
    expect("compare() refuses the policy max, which the rule judges itself",
           refuses(lambda: rule_ledger.compare(table["max-rule"], rule_ledger.empty(table["max-rule"]), []),
                   rule_ledger.LedgerError))


def write_cases(expect: Expect, table: dict[str, rule_registry.Rule]) -> None:
    """Check the modes of write(), the moves, the configurations and the import of the rows of a configuration."""
    count = table["count-rule"]
    ledger = rule_ledger.write(count, rule_ledger.empty(count), [finding("a.py")] * 2,
                               mode=rule_ledger.WriteMode.ADMIT, reason="first")
    expect("the mode lower refuses a count above its row",
           refuses(lambda: rule_ledger.write(count, ledger, [finding("a.py")] * 3), rule_ledger.LedgerError))
    lowered = rule_ledger.write(count, ledger, [finding("a.py")])
    expect("the mode lower lowers a count and removes a row with no finding",
           lowered.expected() == {rule_ledger.Key("a.py", "kind", ""): 1}
           and rule_ledger.write(count, ledger, []).base == {})
    raised = rule_ledger.write(count, ledger, [finding("a.py")] * 3, mode=rule_ledger.WriteMode.RAISE)
    expect("the mode raise writes a count above its row", raised.expected()[rule_ledger.Key("a.py", "kind", "")] == 3)
    expect("the mode admit needs a reason",
           refuses(lambda: rule_ledger.write(count, ledger, [finding("b.py")], mode=rule_ledger.WriteMode.ADMIT),
                   rule_ledger.LedgerError))
    moved = rule_ledger.write(count, ledger, [finding("new/a.py")] * 2, moved={"new/a.py": "a.py"})
    expect("a file that git moved keeps the rows of its old path",
           moved.expected() == {rule_ledger.Key("new/a.py", "kind", ""): 2})
    based = rule_ledger.Ledger("count-rule", count.policy, "# head\n",
                               (rule_ledger.Configuration("base", (("machine", "x86_64"),)),
                                rule_ledger.Configuration("arm", (("machine", "aarch64"),))), ledger.base)
    armed = rule_ledger.write(count, based, [finding("a.py")] * 3, configuration="arm",
                              mode=rule_ledger.WriteMode.RAISE)
    expect("a write of a configuration other than the base writes only its difference rows",
           armed.base == based.base and armed.expected("arm")[rule_ledger.Key("a.py", "kind", "")] == 3)
    shifted = rule_ledger.write(count, based, [finding("new/a.py")] * 2, configuration="arm", moved={"new/a.py": "a.py"})
    expect("a write of a later configuration after a move, before the write of the base, keeps the base rows and "
           "gives its counts at the new path",
           shifted.base == based.base and shifted.expected("arm") == {rule_ledger.Key("new/a.py", "kind", ""): 2}
           and shifted.expected() == {rule_ledger.Key("a.py", "kind", ""): 2})
    lines = rule_ledger.rows_of_configuration(based, "arm", [("machine", "aarch64")], [finding("a.py")] * 4)
    imported = rule_ledger.import_rows(based, "".join(f"2026-10-08 log: {line}\n" for line in lines))
    expect("import_rows() puts the rows of a configuration from a log into the ledger",
           imported.expected("arm")[rule_ledger.Key("a.py", "kind", "")] == 4)
    expect("import_rows() refuses the rows of the base",
           refuses(lambda: rule_ledger.import_rows(based, f"{rule_ledger.ROW_MARK}configuration base machine x\n"),
                   rule_ledger.LedgerError))


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
        ledger = rule_ledger.write(table["count-rule"], rule_ledger.empty(table["count-rule"]), [finding("a.py")],
                                   mode=rule_ledger.WriteMode.ADMIT, reason="moved")
        rule_ledger.ledger_path("count-rule", root).parent.mkdir(parents=True)
        rule_ledger.store(ledger, root)
        git.commit_all(root, "Move the ledger")
        counts = [count for _, _, count in rule_ledger.history(table["count-rule"], root,
                                                               ["utils/scripts/count-rule-ledger.txt"])]
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
    write_cases(expect, table)
    history_cases(expect, table)
    if failures:
        print(f"tests.core.test_ledger: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("tests.core.test_ledger: every case holds.")
    return 0
