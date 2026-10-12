"""The test of the judge of a measured value against a rule of the policy max (assay.core.budget).

`guard selftest tests.core.test_budget` runs the test.  Each case judges one
value against planted thresholds and planted rows.  Exit 0 when each case
holds, else 1.
"""

import tempfile
from pathlib import Path

from assay.core import budget as cost_budget
from assay.core import meter as cost_meter
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import testing

BUDGET = check_report.Budget("compile-memory", 2.0, 4.0, "GB", "a planted quantity")
SPACE = check_report.Budget("compile-cpu", 10.0, 20.0, "s", "a planted time")


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
    """Give the verdict of one value against two thresholds and a row (THE VERDICT)."""
    cases: dict[tuple[float, float | None], str] = {
        (2.0, None): cost_budget.NO_FINDING,
        (3.0, None): cost_budget.WARNING,
        (4.0, None): cost_budget.WARNING,
        (4.5, None): cost_budget.ERROR,
        (4.5, 5.0): cost_budget.ADMITTED,
        (5.5, 5.0): cost_budget.ADMITTED,
        (5.6, 5.0): cost_budget.RISEN,
        (3.0, 3.0): cost_budget.ADMITTED,
        (3.5, 3.0): cost_budget.RISEN,
        (2.0, 3.0): cost_budget.UNNEEDED,
        (1.0, 5.0): cost_budget.UNNEEDED,
        (1.9, 5.0): cost_budget.NO_FINDING,
    }
    for (value, kept), wanted in cases.items():
        expect(
            f"the value {value} with the kept value {kept} and a noise of 10 % gives the verdict {wanted or 'none'}",
            cost_budget.verdict(value, 2.0, 4.0, kept, 0.1 if kept != 3.0 else 0.0) == wanted,
        )
    expect(
        "a value equal to the kept value with its noise is not more than it",
        cost_budget.verdict(4.4, 2.0, 4.0, 4.0, 0.1) == cost_budget.ADMITTED,
    )
    expect(
        "a row of a time keeps a test near the warning threshold with no finding, and goes when the time halves",
        cost_budget.verdict(4.0, 5.0, 30.0, 6.0, 1.0) == cost_budget.NO_FINDING
        and cost_budget.verdict(2.5, 5.0, 30.0, 6.0, 1.0) == cost_budget.UNNEEDED,
    )
    expect(
        "the level of a RISEN verdict is the level of a value with no row",
        cost_budget.risen_level(4.5, 4.0) == cost_budget.ERROR and cost_budget.risen_level(3.5, 4.0) == "warning",
    )
    expect(
        "the noise of a time, a memory and an instruction count is that of NOISE, and another unit has none",
        [cost_budget.noise_of(unit) for unit in ("s", "GB", "G", "KB")] == [1.0, 0.1, 0.02, 0.0],
    )
    with tempfile.TemporaryDirectory(prefix="test-budget-noise-") as scratch:
        table = Path(scratch) / "registry.txt"
        table.write_text(
            rule_registry.planted([rule_registry.measured_rule("compile-instructions", 55, 110, "G", "a count")]),
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
        "its row is admitted, and a count 2.6 % over it is an error",
        counted.unit == cost_budget.UNIT_TEXTS["G"]
        and cost_budget.noise_of(counted.unit) == cost_budget.NOISE["G"]
        and level_of(grown) == "warning"
        and level_of(drifted) == "error",
    )
    budget = check_report.Budget("compile-memory", 2.0, 4.0, "GB", "a planted quantity")
    expect(
        "check_report.classify() agrees with verdict() for each value with no row",
        [check_report.classify(value, budget) for value in (2.0, 3.0, 4.5)] == [None, "warning", "error"],
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
    """Judge one value against the thresholds and the rows (THE JUDGE)."""
    empty = rows_of({})
    measure = cost_budget.Measure("unit.o", "@build/unit.o", 5.0, "the compile took 5.00 GB")
    with check_report.github_actions(False):
        expect(
            "a value at a threshold gives no finding",
            cost_budget.judge("compile-memory", BUDGET, cost_budget.Measure("u", "u", 2.0, "said"), empty) is None,
        )
        expect(
            "a value over the warning threshold gives a warning",
            level_of(cost_budget.judge("compile-memory", BUDGET, cost_budget.Measure("u", "u", 3.0, "said"), empty))
            == "warning",
        )
        found = cost_budget.judge("compile-memory", BUDGET, measure, empty)
        expect(
            "a value over the error threshold with no row gives an error at the item",
            found is not None
            and found.level == "error"
            and found.path == "@build/unit.o"
            and "over the error threshold 4 GB" in found.message,
        )
        admitted = cost_budget.judge("compile-memory", BUDGET, measure, rows_of({"unit.o": 5.2}))
        expect(
            "a row of the item over the error threshold gives a warning that names the row",
            admitted is not None and admitted.level == "warning" and "compile-memory.txt:3" in admitted.message,
        )
        spare = cost_budget.judge(
            "compile-memory", BUDGET, cost_budget.Measure("unit.o", "@build/unit.o", 1.5, "s"), rows_of({"unit.o": 5.2})
        )
        expect(
            "a row of an item at or under the warning threshold gives an error at the row",
            spare is not None and spare.level == "error" and spare.line == 3 and "Remove the row" in spare.message,
        )
        kept = cost_budget.judge(
            "compile-memory", BUDGET, cost_budget.Measure("unit.o", "@build/unit.o", 3.0, "s"), rows_of({"unit.o": 3.0})
        )
        expect(
            "a row of an item over the warning threshold only keeps it, with a warning that names the row and its value",
            kept is not None and kept.level == "warning" and "keeps it up to 3 GB with the noise 10%" in kept.message,
        )
        risen = cost_budget.judge(
            "compile-memory", BUDGET, cost_budget.Measure("unit.o", "u", 6.0, "s"), rows_of({"unit.o": 5.2})
        )
        expect(
            "a value over the kept value with its noise and over the error threshold is an error that names the row",
            risen is not None and risen.level == "error" and "more than the row" in risen.message,
        )
        drift = cost_budget.judge(
            "compile-memory", BUDGET, cost_budget.Measure("unit.o", "u", 3.5, "s"), rows_of({"unit.o": 3.0})
        )
        expect(
            "a value over the kept value with its noise and under the error threshold is a warning again",
            drift is not None and drift.level == "warning" and "more than the row" in drift.message,
        )
        held = cost_budget.judge(
            "header-alone",
            BUDGET,
            cost_budget.Measure("h", "h", 3.0, "said", threshold_rule="header-alone-fixy"),
            empty,
        )
        expect(
            "a held rule of the thresholds is named in the message",
            held is not None and held.check == "header-alone" and "header-alone-fixy" in held.message,
        )
    with check_report.github_actions(True):
        timed = cost_budget.judge("compile-cpu", SPACE, cost_budget.Measure("u", "u", 30.0, "said"), empty)
        expect("on a GitHub runner an error of a time rule is a warning", level_of(timed) == "warning")
        sized = cost_budget.judge("compile-memory", BUDGET, measure, empty)
        expect("on a GitHub runner an error of a memory rule stays an error", level_of(sized) == "error")


def rows_cases(expect: testing.Expect) -> None:
    """Read the rows of one kind, and give a warning for a row with no item (THE ROWS)."""
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
        stale = cost_budget.unused("compile-memory", rows, ["main.o"])
        expect(
            "a row whose item the build does not hold gives a warning",
            len(stale) == 1 and stale[0].level == "warning" and "holds no unit.o" in stale[0].message,
        )
        expect(
            "a row whose item the build holds gives no warning",
            cost_budget.unused("compile-memory", rows, ["unit.o"]) == [],
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
        expect(
            "unused() gives no warning for a row of the build kind in a build with options",
            [item.line for item in cost_budget.unused("compile-memory", bench, ["unit.o"])]
            == [bench.admitted["bench.o"].line],
        )
        plain = cost_budget.read_rows("compile-memory", cost_meter.ITEM_OUTPUT, ledger, "k", "k")
        expect(
            "a build with no option reads only the rows of its build kind, and gives no warning for a bench row",
            set(plain.admitted) == {"unit.o", "base.o"}
            and cost_budget.unused("compile-memory", plain, ["unit.o", "base.o"]) == [],
        )
        found, _, _ = cost_budget.row_of(
            "compile-memory", cost_meter.ITEM_OUTPUT, str(ledger), "k", "unit.o", "k-bench"
        )
        expect("row_of() gives the row of the configuration first", found is not None and found.value == 7.0)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (verdict_cases, row_cases, judge_cases, rows_cases))
