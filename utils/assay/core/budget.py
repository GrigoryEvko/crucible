"""The judge of the measured value of one item against a measured rule and the ledger row of the policy max.

THE VERDICT
    verdict() compares the value of one item with the two thresholds of a
    measured rule (assay.core.registry).  It also reads if a ledger row
    admits the item for the kind of the build:
      ERROR     the value is over the error threshold, and no row admits it
      ADMITTED  the value is over the error threshold, and a row admits it.
                The judge gives a warning that names the row and its reason
      UNNEEDED  a row admits the item, and the value is at or below the error
                threshold.  The row admits nothing, and the judge gives an
                error that tells to remove the row
      WARNING   the value is over the warning threshold only
    NO_FINDING is the verdict of each other value.  A value that is equal to a
    threshold is not more than it.  Each judge of a measured value calls
    verdict(): judge() of this module, check_report.classify(), and the judges
    of the step meters.

THE JUDGE
    judge() gives the finding of one verdict for a check of the engines.  On a
    GitHub runner, an error of a time rule is a warning (assay.core.meter, A
    CI RUNNER), through check_report.judged().

THE ROWS
    The ledger of a rule of the policy max holds one row for each kind and
    item (assay.core.meter, THE REGISTRY AND THE LEDGERS).  read_rows() gives
    the rows of one kind of build, with the problems of the file as errors.
    row_of() gives the row of one item and kind, with no finding.  unused()
    gives a warning for each row of the kind with an item that the build does
    not hold.  Such a row stays until no build of its kind makes the item.

THE STEP METERS
    `loop step`, `loop test-step` and `loop fixture` judge each step with
    verdict() and row_of(), and they give their own messages.  The import
    closure of a step meter does not hold assay.core.report, which imports
    argparse and re (the rule import-closure).  This module then imports only
    assay.core.value at its import.  It imports assay.core.meter and
    assay.core.report in a function that gives a finding or reads a ledger.

`guard selftest tests.core.test_budget` does a test of the module.
"""

from assay.core.value import Value

# The names are only for the types, and a step meter does not pay their imports.
TYPE_CHECKING = False
if TYPE_CHECKING:
    from collections.abc import Iterable, Mapping
    from pathlib import Path

    from assay.core import report as check_report

# The verdicts of one value (THE VERDICT).
NO_FINDING = ""
WARNING = "warning"
ERROR = "error"
ADMITTED = "admitted"
UNNEEDED = "unneeded"


class Measure(Value):
    """The measured value of one item, with the words that tell it.

    `item` is the item of the ledger key (assay.core.meter, item_of()).
    `path` is the position of the finding.  `said` tells the value, for example
    "the test t took 3.10 s of wall time".  `threshold_rule` names the held
    rule that gives the thresholds, when it is not the rule of the finding,
    such as header-alone-fixy for the rule header-alone.
    """

    __match_args__ = ("item", "path", "value", "said", "threshold_rule")
    item: str
    path: str
    value: float
    said: str
    threshold_rule: str | None

    def __init__(self, item: str, path: str, value: float, said: str, threshold_rule: str | None = None) -> None:
        """Hold the item, the position, the value, the words and the held rule of the thresholds."""
        super().__init__(item, path, value, said, threshold_rule)


class Admission(Value):
    """One ledger row of the policy max: its line, the value at the last write, and its reason."""

    __match_args__ = ("line", "value", "reason")
    line: int
    value: float
    reason: str

    def __init__(self, line: int, value: float, reason: str) -> None:
        """Hold the line, the value and the reason of one row."""
        super().__init__(line, value, reason)


class Rows(Value):
    """The ledger of one rule of the policy max for one kind of build (THE ROWS)."""

    __match_args__ = ("shown", "kind", "admitted", "problems")
    shown: str
    kind: str
    admitted: Mapping[str, Admission]
    problems: tuple[check_report.Finding, ...]

    def __init__(
        self,
        shown: str,
        kind: str,
        admitted: Mapping[str, Admission],
        problems: tuple[check_report.Finding, ...] = (),
    ) -> None:
        """Hold the shown ledger, the kind, the row of each admitted item and the problems of the file."""
        super().__init__(shown, kind, admitted, problems)


def verdict(value: float, warning: float, error: float, is_admitted: bool) -> str:
    """Return the verdict of one value against two thresholds and the ledger row of its item (THE VERDICT).

    Args:
        value: The value, in the unit of the measured rule
        warning: The warning threshold
        error: The error threshold
        is_admitted: True when a ledger row admits the item for the kind of the build
    """
    if is_admitted:
        return ADMITTED if value > error else UNNEEDED
    if value > error:
        return ERROR
    return WARNING if value > warning else NO_FINDING


def row_of(rule_id: str, shape: str, ledger: str, kind: str, item: str) -> tuple[Admission | None, str, str]:
    """Return the row of one item and kind in the ledger of a rule of the policy max, with no finding (THE ROWS).

    Args:
        rule_id: The ID of the rule
        shape: The shape of the item of a row: ITEM_OUTPUT, ITEM_FILE or ITEM_NAME of assay.core.meter
        ledger: The file of the ledger.  The file is not there when the rule has no row
        kind: The kind of the build
        item: The item

    Returns:
        The row or None, the ledger as a message shows it, and the first problem of the ledger as `LINE: TEXT`, or an
        empty text.  A ledger that cannot be read gives no row and the problem at the line 0
    """
    from assay.core import meter as cost_meter

    try:
        rows, problems, shown = cost_meter.read_kind_rows(rule_id, shape, ledger)
    except OSError as error:
        return None, cost_meter.shown_ledger(ledger), f"0: the ledger cannot be read ({error})"
    found = rows.get((kind, item))
    problem = f"{problems[0][0]}: {problems[0][1]}" if problems else ""
    return (None if found is None else Admission(*found)), shown, problem


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
    from assay.core import meter as cost_meter
    from assay.core import report as check_report

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
    row = rows.admitted.get(measure.item)
    found = verdict(measure.value, budget.warn, budget.error, row is not None)
    if found == NO_FINDING:
        return None
    from assay.core import report as check_report

    of_rule = f" of the measured rule {measure.threshold_rule}" if measure.threshold_rule else ""
    if row is None:
        limit = budget.error if found == ERROR else budget.warn
        return check_report.judged(
            "error" if found == ERROR else "warning",
            measure.path,
            0,
            rule_id,
            f"{measure.said}, over the {found} threshold{of_rule} {limit:g} {budget.unit}",
        )
    if found == ADMITTED:
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
    from assay.core import report as check_report

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
