"""The test of the judge of a measured value against a rule of the policy max (assay.core.budget).

`guard selftest tests.core.test_budget` runs the test.  Each case judges one
value against planted thresholds and planted rows.  Exit 0 when each case
holds, else 1.
"""

import tempfile
from pathlib import Path

from assay.core import budget as cost_budget
from assay.core import meter as cost_meter
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
    cases = {
        (2.0, False): cost_budget.NO_FINDING,
        (3.0, False): cost_budget.WARNING,
        (4.0, False): cost_budget.WARNING,
        (4.5, False): cost_budget.ERROR,
        (4.5, True): cost_budget.ADMITTED,
        (4.0, True): cost_budget.UNNEEDED,
        (1.0, True): cost_budget.UNNEEDED,
    }
    for (value, is_admitted), wanted in cases.items():
        expect(
            f"the value {value} with{'' if is_admitted else ' no'} row gives the verdict {wanted or 'none'}",
            cost_budget.verdict(value, 2.0, 4.0, is_admitted) == wanted,
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
            "compile-memory", BUDGET, cost_budget.Measure("unit.o", "@build/unit.o", 3.0, "s"), rows_of({"unit.o": 5.2})
        )
        expect(
            "a row of an item at or under the error threshold gives an error at the row",
            spare is not None and spare.level == "error" and spare.line == 3 and "Remove the row" in spare.message,
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


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (verdict_cases, row_cases, judge_cases, rows_cases))
