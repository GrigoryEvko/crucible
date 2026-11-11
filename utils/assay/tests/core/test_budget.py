"""The test of the judge of a measured value against a rule of the policy max (assay.core.budget).

`guard selftest tests.core.test_budget` runs the test.  Each case judges one
value against a planted limit and planted rows.  Exit 0 when each case
holds, else 1.  The cases of a ledger read the registry of the tree: the rule
static-tls has one limit, and the rule header-alone has one limit for each
layer of include/.
"""

import os
import tempfile
from dataclasses import replace
from pathlib import Path

from assay.core import budget as cost_budget
from assay.core import cache, cache_root, testing
from assay.core import ledger as rule_ledger
from assay.core import meter as cost_meter
from assay.core import registry as rule_registry
from assay.core import report as check_report

BUDGET = check_report.Budget("step-memory", 4.0, "GB", "a planted quantity", "compile")
UNSETTLED_NS = 3_600_000_000_000
# A rule of the tree with one limit, and a rule of the tree with one limit for each kind, with the form of their items.
SCALAR_RULE = "static-tls"
SCALAR_FORMS = {"": cost_meter.ITEM_OUTPUT}
KEYED_RULE = "header-alone"
KEYED_FORMS = {"fixy": cost_meter.ITEM_FILE, "crucible": cost_meter.ITEM_FILE}


def rows_of(admitted: dict[str, float], kind: str = "compile") -> cost_budget.Rows:
    """Return planted rows of one kind, each with its value, at the lines that start at line 3."""
    return cost_budget.Rows(
        "utils/ledgers/step-memory.txt",
        {
            (kind, item): cost_budget.Admission(line, value, "a planted reason")
            for line, (item, value) in enumerate(admitted.items(), start=3)
        },
    )


def level_of(found: check_report.Finding | None) -> str:
    """Return the level of a finding, or "none"."""
    return "none" if found is None else found.level


def verdict_cases(expect: testing.Expect) -> None:
    """Give the verdict of one value against the limit and a row (THE VERDICT)."""
    cases: dict[tuple[float, float | None], str] = {
        (2.0, None): cost_budget.NO_FINDING,
        (4.0, None): cost_budget.NO_FINDING,
        (4.5, None): cost_budget.ERROR,
        (4.5, 5.0): cost_budget.ADMITTED,
        (5.5, 5.0): cost_budget.ADMITTED,
        (5.6, 5.0): cost_budget.RISEN,
        (3.0, 5.0): cost_budget.NO_FINDING,
        (4.0, 5.0): cost_budget.NO_FINDING,
    }
    for (value, kept), wanted in cases.items():
        expect(
            f"the value {value} with the kept value {kept} and a noise of 10 % gives the verdict {wanted or 'none'}",
            cost_budget.verdict(value, 4.0, kept, 0.1) == wanted,
        )
    expect(
        "a value equal to the kept value with its noise is not more than it",
        cost_budget.verdict(4.4, 4.0, 4.0, 0.1) == cost_budget.ADMITTED,
    )
    expect(
        "a row gives no finding for a value at or below the limit, also when the value halves, because a ledger "
        "fails only on a growth",
        cost_budget.verdict(4.0, 4.0, 6.0, 1.0) == cost_budget.NO_FINDING
        and cost_budget.verdict(2.5, 4.0, 6.0, 1.0) == cost_budget.NO_FINDING,
    )
    expect(
        "the noise of a time, a memory and an instruction count is that of NOISE, and another unit has none",
        [cost_budget.noise_of(unit) for unit in ("s", "GB", "G", "KB")] == [1.0, 0.1, 0.02, 0.0],
    )
    with tempfile.TemporaryDirectory(prefix="test-budget-noise-") as scratch:
        table = Path(scratch) / "registry.txt"
        table.write_text(
            rule_registry.planted(
                [rule_registry.measured_rule("planted-cost", {"compile": 110, "test": 500}, "G", "a count")]
            ),
            encoding="utf-8",
        )
        counted = check_report.budget_of(rule_registry.load(table, str(table)).rules["planted-cost"], "compile")
    count_rows = cost_budget.Rows(
        "utils/ledgers/planted-cost.txt", {("compile", "u"): cost_budget.Admission(3, 165.712, "r")}
    )
    grown, drifted = (
        cost_budget.judge("planted-cost", counted, cost_budget.Measure("u", "u", value, "said", "compile"), count_rows)
        for value in (165.8, 170.0)
    )
    expect(
        "the limit of a kind with the unit G has the noise of an instruction count.  A count 0.05 % over its row "
        "gives no finding, and a count 2.6 % over it is an error",
        counted.unit == cost_budget.UNIT_TEXTS["G"]
        and counted.limit == 110.0
        and cost_budget.noise_of(counted.unit) == cost_budget.NOISE["G"]
        and level_of(grown) == "none"
        and level_of(drifted) == "error",
    )
    expect(
        "check_report.classify() agrees with verdict() for each value with no row",
        [check_report.classify(value, BUDGET) for value in (2.0, 4.0, 4.5)] == [None, None, "error"],
    )


def row_cases(expect: testing.Expect) -> None:
    """Give the row of one kind and item, the shown ledger and the first problem of the ledger (THE ROWS)."""
    with tempfile.TemporaryDirectory(prefix="test-budget-") as scratch:
        ledger = str(Path(scratch) / f"{SCALAR_RULE}.txt")
        found, _, problem = cost_budget.row_of(SCALAR_RULE, "", cost_meter.ITEM_OUTPUT, ledger, "unit.o")
        expect("a ledger that is not there gives no row and no problem", found is None and not problem)
        cost_meter.write_ledger_rows(
            SCALAR_RULE, SCALAR_FORMS, ledger, {("", "unit.o"): (5.0, "a planted reason")}, "# a planted ledger\n"
        )
        expect(
            "write_ledger_rows() gives each row of a rule with one limit an empty KIND cell",
            "@build/unit.o | - | 5 | - | a planted reason" in Path(ledger).read_text(encoding="utf-8"),
        )
        found, shown, problem = cost_budget.row_of(SCALAR_RULE, "", cost_meter.ITEM_OUTPUT, ledger, "unit.o")
        expect(
            "row_of() gives the row of the item, and the ledger as a message shows it",
            found is not None
            and (found.value, found.reason) == (5.0, "a planted reason")
            and shown.endswith(f"{SCALAR_RULE}.txt")
            and not problem,
        )
        other, _, _ = cost_budget.row_of(SCALAR_RULE, "", cost_meter.ITEM_OUTPUT, ledger, "other.o")
        expect("row_of() gives no row of a different item", other is None)
        with open(ledger, "a", encoding="utf-8") as handle:
            handle.write("- | - | 1.0 | - | a row with no item\n")
        _, _, problem = cost_budget.row_of(SCALAR_RULE, "", cost_meter.ITEM_OUTPUT, ledger, "unit.o")
        expect("row_of() gives the first problem of the ledger with its line", problem.split(":")[0].isdigit())
        keyed = str(Path(scratch) / f"{KEYED_RULE}.txt")
        cost_meter.write_ledger_rows(
            KEYED_RULE,
            KEYED_FORMS,
            keyed,
            {("fixy", "include/fixy/A.h"): (7.0, "the fixy row"), ("crucible", "include/fixy/A.h"): (8.0, "a second")},
            "# a planted ledger\n",
        )
        text = Path(keyed).read_text(encoding="utf-8")
        fixy, _, fixy_problem = cost_budget.row_of(KEYED_RULE, "fixy", cost_meter.ITEM_FILE, keyed, "include/fixy/A.h")
        crucible, _, _ = cost_budget.row_of(KEYED_RULE, "crucible", cost_meter.ITEM_FILE, keyed, "include/fixy/A.h")
        missing, _, _ = cost_budget.row_of(KEYED_RULE, "foundation", cost_meter.ITEM_FILE, keyed, "include/fixy/A.h")
        expect(
            "write_ledger_rows() gives the KIND cell of each row its kind, and row_of() gives the row of the kind of "
            "the call.  Two kinds of the same item have two rows",
            "include/fixy/A.h | fixy | 7 | - | the fixy row" in text
            and fixy is not None
            and fixy.value == 7.0
            and not fixy_problem
            and crucible is not None
            and crucible.value == 8.0
            and missing is None,
        )


def judge_cases(expect: testing.Expect) -> None:
    """Judge one value against the limit of its kind and the rows (THE JUDGE)."""
    empty = rows_of({})
    measure = cost_budget.Measure("unit.o", "@build/unit.o", 5.0, "the compile took 5.00 GB", "compile")
    with check_report.github_actions(False):
        small = (cost_budget.Measure("u", "u", value, "said", "compile") for value in (4.0, 3.0))
        expect(
            "a value at the limit gives no finding, and a value below it gives none",
            all(cost_budget.judge("step-memory", BUDGET, item, empty) is None for item in small),
        )
        found = cost_budget.judge("step-memory", BUDGET, measure, empty)
        expect(
            "a value above the limit with no row gives an error at the item, which names the kind of its limit",
            found is not None
            and found.level == "error"
            and found.path == "@build/unit.o"
            and "above the limit of the kind compile 4 GB" in found.message,
        )
        admitted = cost_budget.judge("step-memory", BUDGET, measure, rows_of({"unit.o": 5.2}))
        expect("a row of the kind and the item above the limit keeps it, and gives no finding", admitted is None)
        other_kind = cost_budget.judge("step-memory", BUDGET, measure, rows_of({"unit.o": 5.2}, "link"))
        expect("a row of a different kind of the same item keeps nothing", level_of(other_kind) == "error")
        noisy = cost_budget.judge(
            "step-memory",
            BUDGET,
            cost_budget.Measure("unit.o", "u", 5.7, "s", "compile"),
            rows_of({"unit.o": 5.2}),
        )
        expect("a row keeps its item up to the kept value with the noise of its unit", noisy is None)
        spare = cost_budget.judge(
            "step-memory",
            BUDGET,
            cost_budget.Measure("unit.o", "@build/unit.o", 1.5, "s", "compile"),
            rows_of({"unit.o": 5.2}),
        )
        expect(
            "a row of an item at or under the limit gives no finding, because a ledger fails only on a growth",
            spare is None,
        )
        risen = cost_budget.judge(
            "step-memory",
            BUDGET,
            cost_budget.Measure("unit.o", "u", 6.0, "s", "compile"),
            rows_of({"unit.o": 5.2}),
        )
        expect(
            "a value over the kept value with its noise is an error that names the row",
            risen is not None
            and risen.level == "error"
            and "more than the row" in risen.message
            and "step-memory.txt:3" in risen.message,
        )
        scalar = check_report.Budget(SCALAR_RULE, 4.0, "bytes", "a planted quantity")
        plain = cost_budget.judge(SCALAR_RULE, scalar, cost_budget.Measure("u", "u", 5.0, "said"), rows_of({}, ""))
        expect(
            "the error of a rule with one limit names no kind",
            plain is not None and "above the limit 4 bytes" in plain.message,
        )
    with check_report.github_actions(True):
        sized = cost_budget.judge("step-memory", BUDGET, measure, empty)
        expect("on a GitHub runner an error of a memory rule stays an error", level_of(sized) == "error")


def rows_cases(expect: testing.Expect) -> None:
    """Read the rows of a ledger, and refuse a row of a kind that the rule does not have (THE ROWS)."""
    with tempfile.TemporaryDirectory(prefix="test-budget-") as scratch:
        ledger = Path(scratch) / f"{SCALAR_RULE}.txt"
        rows = cost_budget.read_rows(SCALAR_RULE, cost_meter.ITEM_OUTPUT, ledger)
        expect("a ledger that does not exist has no row and no problem", not rows.admitted and not rows.problems)
        cost_meter.write_ledger_rows(
            SCALAR_RULE,
            SCALAR_FORMS,
            str(ledger),
            {("", "unit.o"): (5.0, "a planted reason"), ("", "base.o"): (4.5, "a second row")},
            "# a planted ledger\n",
        )
        rows = cost_budget.read_rows(SCALAR_RULE, cost_meter.ITEM_OUTPUT, ledger)
        expect(
            "read_rows() gives the row of each item with the kind ''",
            set(rows.admitted) == {("", "unit.o"), ("", "base.o")}
            and rows.admitted[("", "unit.o")].value == 5.0
            and not rows.problems,
        )
        ledger.write_text(
            f"# a planted ledger\nformat 1\nrule {SCALAR_RULE}\n@build/unit.o | x86_64-debug-asan | 5 | - | a kind\n",
            encoding="utf-8",
        )
        kinded = cost_budget.read_rows(SCALAR_RULE, cost_meter.ITEM_OUTPUT, ledger)
        expect(
            "a row with a KIND of a rule with one limit is an error at its line, and it keeps nothing",
            not kinded.admitted
            and [problem.line for problem in kinded.problems] == [4]
            and "@build/OUTPUT | - | VALUE | - | REASON" in kinded.problems[0].message,
        )
        keyed = Path(scratch) / f"{KEYED_RULE}.txt"
        keyed.write_text(
            f"# a planted ledger\nformat 1\nrule {KEYED_RULE}\n"
            "include/fixy/A.h | fixy | 7 | - | a fixy row\n"
            "include/foundation/B.h | foundation | 4 | - | a row of a kind that the judge does not read\n"
            "include/fixy/C.h | other | 7 | - | a row of a kind that the rule does not have\n"
            "@build/include/fixy/D.h | crucible | 7 | - | a row of the wrong form\n",
            encoding="utf-8",
        )
        read = cost_budget.read_rows(KEYED_RULE, KEYED_FORMS, keyed)
        expect(
            "read_rows() of a rule with kinds gives the rows of the kinds of the call, skips a row of a different "
            "kind of the rule, and gives an error for a row of a kind that the rule does not have and for a row of "
            "the wrong form",
            set(read.admitted) == {("fixy", "include/fixy/A.h")}
            and [problem.line for problem in read.problems] == [6, 7]
            and "the kind of a row of the rule header-alone is one of" in read.problems[0].message
            and "FILE | crucible | VALUE | - | REASON" in read.problems[1].message,
        )


def write_cases(expect: testing.Expect) -> None:
    """Write a row only for an item above the limit that no row keeps, and remove and lower no row (THE WRITE)."""
    scalar = check_report.Budget(SCALAR_RULE, 4.0, "GB", "a planted quantity")
    kept_key = rule_ledger.Key("@build/kept.o", "", "")
    gone_key = rule_ledger.Key("@build/gone.o", "", "")
    ledger = replace(
        rule_ledger.empty(rule_ledger.rule_of(SCALAR_RULE)),
        base={
            kept_key: rule_ledger.Entry(kept_key, 6.0, "a kept row"),
            gone_key: rule_ledger.Entry(gone_key, 7.0, "an item that this build does not hold"),
        },
    )
    measures = [
        ("small.o", 1.0, scalar),
        ("kept.o", 5.0, scalar),
        ("noisy.o", 4.0, scalar),
    ]
    written = cost_budget.raised_ledger(ledger, cost_meter.ITEM_OUTPUT, measures, None)
    expect(
        "a write with no item that needs a row and no reason keeps each row, also the row of an item that the build "
        "does not hold, and lowers no kept value",
        written.base == ledger.base,
    )
    try:
        cost_budget.raised_ledger(ledger, cost_meter.ITEM_OUTPUT, [("new.o", 5.0, scalar)], None)
        refused = False
    except rule_ledger.LedgerError as problem:
        refused = "--reason" in str(problem)
    expect("a new row with no reason is refused, and the message names --reason", refused)
    try:
        cost_budget.raised_ledger(ledger, cost_meter.ITEM_OUTPUT, [("kept.o", 7.0, scalar)], None)
        risen_refused = False
    except rule_ledger.LedgerError:
        risen_refused = True
    expect("a higher kept value with no reason is refused", risen_refused)
    raised = cost_budget.raised_ledger(
        ledger, cost_meter.ITEM_OUTPUT, [("kept.o", 7.0, scalar), ("new.o", 5.0001, scalar)], "a planted reason"
    )
    new_key = rule_ledger.Key("@build/new.o", "", "")
    expect(
        "a write with a reason gives a new row and a higher kept value the reason, rounds the value up, and keeps "
        "each other row",
        raised.base[kept_key].value == 7.0
        and raised.base[kept_key].reason == "a planted reason"
        and raised.base[new_key].value == 5.001
        and raised.base[gone_key] == ledger.base[gone_key],
    )
    lower = cost_budget.raised_ledger(ledger, cost_meter.ITEM_OUTPUT, [("kept.o", 4.5, scalar)], "a reason")
    expect("a measure under the kept value lowers no row", lower.base[kept_key].value == 6.0)
    fixy = check_report.Budget(KEYED_RULE, 6.5, "MB", "a planted quantity", "fixy")
    keyed = cost_budget.raised_ledger(
        rule_ledger.empty(rule_ledger.rule_of(KEYED_RULE)), KEYED_FORMS, [("include/fixy/A.h", 7.0, fixy)], "a reason"
    )
    expect(
        "a write gives the row the kind of the limit of its measure",
        set(keyed.base) == {rule_ledger.Key("include/fixy/A.h", "fixy", "")},
    )


def memo_cases(expect: testing.Expect) -> None:
    """Hold the cache of the rows of a ledger (THE MEMO OF THE ROWS of assay.core.meter)."""
    rule, forms = SCALAR_RULE, SCALAR_FORMS
    with tempfile.TemporaryDirectory(prefix="test-budget-") as scratch:
        ledger = str(Path(scratch) / f"{SCALAR_RULE}.txt")
        cost_meter.write_ledger_rows(rule, forms, ledger, {("", "unit.o"): (5.0, "a planted reason")}, "# planted\n")
        with cache.scratch_root():
            memo = cost_meter.rows_memo(rule, forms, ledger, UNSETTLED_NS)
            first, _, _ = cost_meter.read_ledger_rows(rule, forms, ledger, UNSETTLED_NS)
            expect(
                "a ledger that changed in the settle period gives its rows, and no entry.  The case gives a settle "
                "period of an hour, so that the load of the host cannot settle the ledger before the read",
                memo is not None
                and not memo[1]
                and first == {("", "unit.o"): (4, 5.0, "a planted reason")}
                and not os.path.exists(memo[0]),
            )
            settled = testing.wait_until(lambda: (cost_meter.rows_memo(rule, forms, ledger) or ("", False))[1], 2.0)
            entry, _ = cost_meter.rows_memo(rule, forms, ledger) or ("", False)
            cost_meter.read_ledger_rows(rule, forms, ledger)
            expect("a settled ledger gets an entry of its rows", settled and os.path.isfile(entry))
            rows_cache = cache.cache_root(cost_meter.ROWS_CACHE)
            expect(
                "the entry is in the cache that loop test holds in its limit",
                rows_cache is not None and Path(entry).is_relative_to(rows_cache),
            )
            other_forms = cost_meter.rows_memo(rule, {"": cost_meter.ITEM_NAME}, ledger)
            expect(
                "the forms of the kinds of the call are a part of the key of an entry",
                other_forms is not None and other_forms[0] != entry,
            )
            lined = {("", "first.o"): (4, 1.0, "the first line"), ("", "second.o"): (5, 2.0, "the second line")}
            cost_meter.write_rows_memo(
                entry, {("", "second.o"): lined[("", "second.o")], ("", "first.o"): lined[("", "first.o")]}, []
            )
            kept = cost_meter.read_rows_memo(entry)
            expect(
                "an entry keeps the rows in the sequence of their lines, as the parse gives them, with the kind of "
                "each row",
                kept is not None and list(kept[0]) == [("", "first.o"), ("", "second.o")],
            )
            cost_meter.write_rows_memo(entry, {("", "unit.o"): (3, 9.0, "the planted entry")}, [])
            held, _, _ = cost_meter.read_ledger_rows(rule, forms, ledger)
            expect("a read takes the rows of the entry with no parse", held[("", "unit.o")][1] == 9.0)
            cost_meter.write_ledger_rows(rule, forms, ledger, {("", "unit.o"): (6.0, "a new reason")}, "# planted\n")
            fresh, _, _ = cost_meter.read_ledger_rows(rule, forms, ledger)
            expect("a changed ledger gets a new key, and its rows", fresh[("", "unit.o")][1:] == (6.0, "a new reason"))
        with testing.environment_variable(cache_root.ROOT_VARIABLE, cache_root.OFF):
            expect("with the caches off, no entry", cost_meter.rows_memo(rule, forms, ledger) is None)


def scope_cases(expect: testing.Expect) -> None:
    """Tell which outputs of a build the measured rules judge (THE SCOPE)."""
    cases = {
        "bench/CMakeFiles/bench_x.dir/bench_x.cpp.o": False,
        "bench/bench_x": False,
        "test/CMakeFiles/test_x.dir/test_x.cpp.o": True,
        "benchmarks/x.o": True,
        "test/bench/x.o": True,
        "/elsewhere/bench/x.o": True,
        "../bench/x.o": True,
    }
    for output, wanted in cases.items():
        expect(f"is_judged({output!r}) is {wanted}", cost_budget.is_judged(output) is wanted)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (verdict_cases, row_cases, judge_cases, rows_cases, write_cases, memo_cases, scope_cases),
    )
