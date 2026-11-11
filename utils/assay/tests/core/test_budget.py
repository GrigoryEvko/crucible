"""The test of the judge of a measured value against a rule of the policy max (assay.core.budget).

`guard selftest tests.core.test_budget` runs the test.  Each case judges one
value against a planted limit and planted rows.  Exit 0 when each case
holds, else 1.
"""

import os
import tempfile
from pathlib import Path

from assay.core import budget as cost_budget
from assay.core import cache, cache_root, testing
from assay.core import meter as cost_meter
from assay.core import registry as rule_registry
from assay.core import report as check_report

BUDGET = check_report.Budget("compile-memory", 4.0, "GB", "a planted quantity")
UNSETTLED_NS = 3_600_000_000_000


def rows_of(admitted: dict[str, float]) -> cost_budget.Rows:
    """Return planted rows of the kind k, each with its value, at the lines that start at line 3."""
    return cost_budget.Rows(
        "utils/ledgers/compile-memory.txt",
        "k",
        {
            item: cost_budget.Admission(line, value, "a planted reason")
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
            rule_registry.planted([rule_registry.measured_rule("compile-instructions", 110, "G", "a count")]),
            encoding="utf-8",
        )
        counted = check_report.read_budgets(table)["compile-instructions"]
    count_rows = cost_budget.Rows(
        "utils/ledgers/compile-instructions.txt", "k", {"u": cost_budget.Admission(3, 165.712, "r")}
    )
    grown, drifted = (
        cost_budget.judge("compile-instructions", counted, cost_budget.Measure("u", "u", value, "said"), count_rows)
        for value in (165.8, 170.0)
    )
    expect(
        "a rule with the unit G that read_budgets() gives has the noise of an instruction count.  A count 0.05 % over "
        "its row gives no finding, and a count 2.6 % over it is an error",
        counted.unit == cost_budget.UNIT_TEXTS["G"]
        and cost_budget.noise_of(counted.unit) == cost_budget.NOISE["G"]
        and level_of(grown) == "none"
        and level_of(drifted) == "error",
    )
    expect(
        "check_report.classify() agrees with verdict() for each value with no row",
        [check_report.classify(value, BUDGET) for value in (2.0, 4.0, 4.5)] == [None, None, "error"],
    )


def row_cases(expect: testing.Expect) -> None:
    """Give the row of one item and kind, the shown ledger and the first problem of the ledger (THE ROWS)."""
    with tempfile.TemporaryDirectory(prefix="test-budget-") as scratch:
        ledger = str(Path(scratch) / "compile-memory.txt")
        found, _, problem = cost_budget.row_of("compile-memory", cost_meter.ITEM_OUTPUT, ledger, "k", "unit.o")
        expect("a ledger that is not there gives no row and no problem", found is None and not problem)
        cost_meter.write_kind_rows(
            "compile-memory",
            cost_meter.ITEM_OUTPUT,
            ledger,
            {("k", "unit.o"): (5.0, "a planted reason")},
            "# a planted ledger\n",
        )
        found, shown, problem = cost_budget.row_of("compile-memory", cost_meter.ITEM_OUTPUT, ledger, "k", "unit.o")
        expect(
            "row_of() gives the row of the item and the kind, and the ledger as a message shows it",
            found is not None
            and (found.value, found.reason) == (5.0, "a planted reason")
            and shown.endswith("compile-memory.txt")
            and not problem,
        )
        other, _, _ = cost_budget.row_of("compile-memory", cost_meter.ITEM_OUTPUT, ledger, "other", "unit.o")
        expect("row_of() gives no row of a different kind", other is None)
        with open(ledger, "a", encoding="utf-8") as handle:
            handle.write("- | k | 1.0 | - | a row with no item\n")
        _, _, problem = cost_budget.row_of("compile-memory", cost_meter.ITEM_OUTPUT, ledger, "k", "unit.o")
        expect("row_of() gives the first problem of the ledger with its line", problem.split(":")[0].isdigit())


def judge_cases(expect: testing.Expect) -> None:
    """Judge one value against the limit and the rows (THE JUDGE)."""
    empty = rows_of({})
    measure = cost_budget.Measure("unit.o", "@build/unit.o", 5.0, "the compile took 5.00 GB")
    with check_report.github_actions(False):
        expect(
            "a value at the limit gives no finding, and a value below it gives none",
            cost_budget.judge("compile-memory", BUDGET, cost_budget.Measure("u", "u", 4.0, "said"), empty) is None
            and cost_budget.judge("compile-memory", BUDGET, cost_budget.Measure("u", "u", 3.0, "said"), empty) is None,
        )
        found = cost_budget.judge("compile-memory", BUDGET, measure, empty)
        expect(
            "a value above the limit with no row gives an error at the item",
            found is not None
            and found.level == "error"
            and found.path == "@build/unit.o"
            and "above the limit 4 GB" in found.message,
        )
        admitted = cost_budget.judge("compile-memory", BUDGET, measure, rows_of({"unit.o": 5.2}))
        expect("a row of the item above the limit keeps it, and gives no finding", admitted is None)
        noisy = cost_budget.judge(
            "compile-memory", BUDGET, cost_budget.Measure("unit.o", "u", 5.7, "s"), rows_of({"unit.o": 5.2})
        )
        expect("a row keeps its item up to the kept value with the noise of its unit", noisy is None)
        spare = cost_budget.judge(
            "compile-memory", BUDGET, cost_budget.Measure("unit.o", "@build/unit.o", 1.5, "s"), rows_of({"unit.o": 5.2})
        )
        expect(
            "a row of an item at or under the limit gives no finding, because a ledger fails only on a growth",
            spare is None,
        )
        risen = cost_budget.judge(
            "compile-memory", BUDGET, cost_budget.Measure("unit.o", "u", 6.0, "s"), rows_of({"unit.o": 5.2})
        )
        expect(
            "a value over the kept value with its noise is an error that names the row",
            risen is not None
            and risen.level == "error"
            and "more than the row" in risen.message
            and "compile-memory.txt:3" in risen.message,
        )
        held = cost_budget.judge(
            "header-alone",
            BUDGET,
            cost_budget.Measure("h", "h", 5.0, "said", threshold_rule="header-alone-fixy"),
            empty,
        )
        expect(
            "a held rule of the limit is named in the message",
            held is not None and held.check == "header-alone" and "header-alone-fixy" in held.message,
        )
    with check_report.github_actions(True):
        sized = cost_budget.judge("compile-memory", BUDGET, measure, empty)
        expect("on a GitHub runner an error of a memory rule stays an error", level_of(sized) == "error")


def rows_cases(expect: testing.Expect) -> None:
    """Read the rows of one kind (THE ROWS)."""
    with tempfile.TemporaryDirectory(prefix="test-budget-") as scratch:
        ledger = Path(scratch) / "compile-memory.txt"
        rows = cost_budget.read_rows("compile-memory", cost_meter.ITEM_OUTPUT, ledger, "k")
        expect("a ledger that does not exist has no row and no problem", not rows.admitted and not rows.problems)
        cost_meter.write_kind_rows(
            "compile-memory",
            cost_meter.ITEM_OUTPUT,
            str(ledger),
            {("k", "unit.o"): (5.0, "a planted reason"), ("other", "unit.o"): (6.0, "other")},
            "# a planted ledger\n",
        )
        rows = cost_budget.read_rows("compile-memory", cost_meter.ITEM_OUTPUT, ledger, "k")
        expect(
            "read_rows() gives only the rows of the kind",
            list(rows.admitted) == ["unit.o"] and rows.admitted["unit.o"].value == 5.0,
        )
        cost_meter.write_kind_rows(
            "compile-memory",
            cost_meter.ITEM_OUTPUT,
            str(ledger),
            {
                ("k", "unit.o"): (5.0, "a planted reason"),
                ("k", "base.o"): (4.5, "a row of the build kind"),
                ("k-bench", "unit.o"): (7.0, "a row of the bench configuration"),
                ("k-bench", "bench.o"): (8.0, "a bench object"),
            },
            "# a planted ledger\n",
        )
        bench = cost_budget.read_rows("compile-memory", cost_meter.ITEM_OUTPUT, ledger, "k", "k-bench")
        expect(
            "the rows of the name of the configuration come first, then the rows of the build kind for other items",
            bench.kind == "k-bench"
            and bench.admitted["unit.o"].value == 7.0
            and bench.admitted["base.o"].value == 4.5
            and bench.own == frozenset({"unit.o", "bench.o"}),
        )
        plain = cost_budget.read_rows("compile-memory", cost_meter.ITEM_OUTPUT, ledger, "k", "k")
        expect(
            "a build with no option reads only the rows of its build kind",
            set(plain.admitted) == {"unit.o", "base.o"},
        )
        found, _, _ = cost_budget.row_of(
            "compile-memory", cost_meter.ITEM_OUTPUT, str(ledger), "k", "unit.o", "k-bench"
        )
        expect("row_of() gives the row of the configuration first", found is not None and found.value == 7.0)


def memo_cases(expect: testing.Expect) -> None:
    """Hold the cache of the rows of a ledger (THE MEMO OF THE ROWS of assay.core.meter)."""
    rule, form = "compile-memory", cost_meter.ITEM_OUTPUT
    with tempfile.TemporaryDirectory(prefix="test-budget-") as scratch:
        ledger = str(Path(scratch) / "compile-memory.txt")
        cost_meter.write_kind_rows(rule, form, ledger, {("k", "unit.o"): (5.0, "a planted reason")}, "# planted\n")
        with cache.scratch_root():
            memo = cost_meter.rows_memo(rule, form, ledger, UNSETTLED_NS)
            first, _, _ = cost_meter.read_kind_rows(rule, form, ledger, UNSETTLED_NS)
            expect(
                "a ledger that changed in the settle period gives its rows, and no entry.  The case gives a settle "
                "period of an hour, so that the load of the host cannot settle the ledger before the read",
                memo is not None
                and not memo[1]
                and first == {("k", "unit.o"): (4, 5.0, "a planted reason")}
                and not os.path.exists(memo[0]),
            )
            settled = testing.wait_until(lambda: (cost_meter.rows_memo(rule, form, ledger) or ("", False))[1], 2.0)
            entry, _ = cost_meter.rows_memo(rule, form, ledger) or ("", False)
            cost_meter.read_kind_rows(rule, form, ledger)
            expect("a settled ledger gets an entry of its rows", settled and os.path.isfile(entry))
            rows_cache = cache.cache_root(cost_meter.ROWS_CACHE)
            expect(
                "the entry is in the cache that loop test holds in its limit",
                rows_cache is not None and Path(entry).is_relative_to(rows_cache),
            )
            lined = {("z", "first.o"): (4, 1.0, "the first line"), ("a", "second.o"): (5, 2.0, "the second line")}
            cost_meter.write_rows_memo(entry, lined, [])
            kept = cost_meter.read_rows_memo(entry)
            expect(
                "an entry keeps the rows in the sequence of their lines, as the parse gives them",
                kept is not None and list(kept[0]) == [("z", "first.o"), ("a", "second.o")],
            )
            cost_meter.write_rows_memo(entry, {("k", "unit.o"): (3, 9.0, "the planted entry")}, [])
            held, _, _ = cost_meter.read_kind_rows(rule, form, ledger)
            expect("a read takes the rows of the entry with no parse", held[("k", "unit.o")][1] == 9.0)
            cost_meter.write_kind_rows(rule, form, ledger, {("k", "unit.o"): (6.0, "a new reason")}, "# planted\n")
            fresh, _, _ = cost_meter.read_kind_rows(rule, form, ledger)
            expect("a changed ledger gets a new key, and its rows", fresh[("k", "unit.o")][1:] == (6.0, "a new reason"))
        with testing.environment_variable(cache_root.ROOT_VARIABLE, cache_root.OFF):
            expect("with the caches off, no entry", cost_meter.rows_memo(rule, form, ledger) is None)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."), argv, (verdict_cases, row_cases, judge_cases, rows_cases, memo_cases)
    )
