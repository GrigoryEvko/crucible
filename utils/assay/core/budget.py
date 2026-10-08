"""The judge of the measured value of one item against a measured rule and the ledger row of the policy max.

THE JUDGE
    judge() compares the value of one item with the two thresholds of a
    measured rule (assay.core.registry).  It also reads the ledger row that
    admits the item for the kind of the build:
      * A value over the error threshold with no row is an error.
      * A value over the error threshold with a row is a warning.  The
        warning names the row and its reason.
      * A row of an item with a value at or below the error threshold is an
        error at the row.  The row admits nothing, and the action is to
        remove it.
      * A value over the warning threshold is a warning.
    A value that is equal to a threshold is not more than it.  On a GitHub
    runner, an error of a time rule is a warning (assay.core.meter, A CI
    RUNNER), through check_report.judged().

THE ROWS
    The ledger of a rule of the policy max holds one row for each kind and
    item (assay.core.meter, THE REGISTRY AND THE LEDGERS).  read_rows() gives
    the rows of one kind of build, with the problems of the file as errors.
    unused() gives a warning for each row of the kind with an item that the
    build does not hold.  Such a row stays until no build of its kind makes
    the item.

THE STEP METERS
    `loop step`, `loop test-step` and `loop fixture` judge their steps with
    functions of their own.  The import closure of a step meter does not hold
    assay.core.report, which imports argparse and re (the rule
    import-closure).  A step meter cannot give a Finding for this reason.

`guard selftest tests.core.test_budget` does a test of the module.
"""

from collections.abc import Iterable, Mapping
from dataclasses import dataclass
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import report as check_report


@dataclass(frozen=True, slots=True)
class Measure:
    """The measured value of one item, with the words that tell it.

    `item` is the item of the ledger key (assay.core.meter, item_of()).
    `path` is the position of the finding.  `said` tells the value, for example
    "the test t took 3.10 s of wall time".  `threshold_rule` names the held
    rule that gives the thresholds, when it is not the rule of the finding,
    such as header-alone-fixy for the rule header-alone.
    """

    item: str
    path: str
    value: float
    said: str
    threshold_rule: str | None = None


@dataclass(frozen=True, slots=True)
class Admission:
    """One ledger row of the policy max: its line, the value at the last write, and its reason."""

    line: int
    value: float
    reason: str


@dataclass(frozen=True, slots=True)
class Rows:
    """The ledger of one rule of the policy max for one kind of build (THE ROWS)."""

    shown: str
    kind: str
    admitted: Mapping[str, Admission]
    problems: tuple[check_report.Finding, ...] = ()


def read_rows(rule_id: str, shape: str, ledger: Path, kind: str) -> Rows:
    """Read the rows of one kind of build from the ledger of a rule of the policy max.

    Args:
        rule_id: The ID of the rule
        shape: The shape of the item of a row: ITEM_OUTPUT, ITEM_FILE or ITEM_NAME of assay.core.meter
        ledger: The file of the ledger.  The file is not there when the rule has no row
        kind: The kind of the build

    Returns:
        The rows of the kind.  Also an error for each row that does not have the shape of the rule, and an error for
        a file that cannot be read
    """
    try:
        rows, problems, shown = cost_meter.read_kind_rows(rule_id, shape, str(ledger))
    except OSError as problem:
        shown = cost_meter.shown_ledger(str(ledger))
        return Rows(
            shown,
            kind,
            {},
            (check_report.Finding("error", shown, 0, rule_id, f"the ledger cannot be read ({problem})"),),
        )
    admitted = {
        item: Admission(line, value, reason)
        for (row_kind, item), (line, value, reason) in rows.items()
        if row_kind == kind
    }
    errors = tuple(check_report.Finding("error", shown, line, rule_id, text) for line, text in problems)
    return Rows(shown, kind, admitted, errors)


def judge(rule_id: str, budget: check_report.Budget, measure: Measure, rows: Rows) -> check_report.Finding | None:
    """Judge the value of one item (THE JUDGE).

    Args:
        rule_id: The ID of the rule of the finding
        budget: The thresholds: of the rule, or of the held rule that measure.threshold_rule names
        measure: The value of the item
        rows: The ledger rows of the kind of the build

    Returns:
        The finding, or None when the value is not more than a threshold and no row admits the item
    """
    level = check_report.classify(measure.value, budget)
    of_rule = f" of the measured rule {measure.threshold_rule}" if measure.threshold_rule else ""
    row = rows.admitted.get(measure.item)
    if row is None:
        if level is None:
            return None
        limit = budget.error if level == "error" else budget.warn
        return check_report.judged(
            level,
            measure.path,
            0,
            rule_id,
            f"{measure.said}, over the {level} threshold{of_rule} {limit:g} {budget.unit}",
        )
    if level == "error":
        return check_report.Finding(
            "warning",
            measure.path,
            0,
            rule_id,
            f"{measure.said}, over the error threshold{of_rule} {budget.error:g} {budget.unit}.  The row "
            f"{rows.shown}:{row.line} admits it (value {row.value:g} at the last write): {row.reason}",
        )
    return check_report.judged(
        "error",
        rows.shown,
        row.line,
        rule_id,
        f"the row admits {measure.item} in the kind {rows.kind}, and it is at {measure.value:.2f} {budget.unit}, at "
        f"or under the error threshold{of_rule} {budget.error:g} {budget.unit}.  Remove the row",
    )


def unused(rule_id: str, rows: Rows, held: Iterable[str]) -> list[check_report.Finding]:
    """Give a warning for each row of the kind with an item that the build does not hold (THE ROWS).

    Complexity: linear in the rows and the held items.

    Args:
        rule_id: The ID of the rule
        rows: The ledger rows of the kind of the build
        held: Each item that the build holds, also an item that the rule does not judge, such as a ccache hit
    """
    present = frozenset(held)
    return [
        check_report.Finding(
            "warning",
            rows.shown,
            row.line,
            rule_id,
            f"the build of kind {rows.kind} holds no {item}.  When no build of that kind makes it, remove the row",
        )
        for item, row in sorted(rows.admitted.items(), key=lambda pair: pair[1].line)
        if item not in present
    ]
