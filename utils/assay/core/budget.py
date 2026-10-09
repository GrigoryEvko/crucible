"""The judge of the measured value of one item against a measured rule and the ledger row of the policy max.

THE VERDICT
    verdict() compares the value of one item with the two thresholds of a
    measured rule (assay.core.registry).  It also reads the kept value of the
    ledger row of the item, when the ledger has a row that judges the build:
      ERROR     the value is over the error threshold, and no row keeps it
      WARNING   the value is over the warning threshold only, and no row
                keeps it
      ADMITTED  a row keeps the item.  The value is over the warning
                threshold, and it is not more than the kept value with the
                noise of its unit.  The judge gives a warning that names the
                row and its reason
      RISEN     a row keeps the item, and the value is more than the kept
                value with the noise of its unit.  The judge gives the level
                of a value with no row: an error over the error threshold,
                else a warning.  A row then cannot hide a drift
      UNNEEDED  a row keeps the item, and the value with the noise of its
                unit is at or below the warning threshold.  The row keeps
                nothing, and the judge gives an error that tells to remove the
                row
    NO_FINDING is the verdict of each other value.  A row gives no finding
    when its item is at or below the warning threshold and the item with its
    noise is above it.  The noise of a time then does not give a kept test an
    error in one run and no error in the next.  A value that is equal to a threshold
    or to the kept value with its noise is not more than it.  Each judge of a
    measured value calls verdict(): judge() of this module,
    check_report.classify(), and the judges of the step meters.

THE NOISE
    The kept value of a row is the value of the item at the last write of the
    row.  Two measures of one item are not always equal.  A row then keeps
    its item up to the kept value times one plus the noise of the unit of the
    rule (NOISE, noise_of()):
      s    1.0   A wall time or a CPU time changes with the load of the host.
                 On 2026-10-09, the slow tests of five builds took up to 77 %
                 more time than their lowest time
      GB   0.1   A peak memory changes with the allocator and the sanitizer
      G    0.02  An instruction count changes by less than 0.1 % between two
                 compiles of one unit
    Each other unit has no noise.  The bytes of machine code and the bytes of
    a header are the same in two builds of one tree.

THE JUDGE
    judge() gives the finding of one verdict for a check of the engines.  On a
    GitHub runner, an error of a time rule is a warning (assay.core.meter, A
    CI RUNNER), through check_report.judged().

THE ROWS
    The ledger of a rule of the policy max holds one row for each item and
    each build kind or configuration.  THE REGISTRY AND THE LEDGERS of
    assay.core.meter gives the row.  The name of a configuration is the build kind with the words of
    its options (assay.core.configuration, THE CONFIGURATION).  read_rows()
    gives the rows that judge one build.  They are the rows of the name of its
    configuration, and the rows of its build kind for each other item.
    row_of() gives the row of one item in the same sequence, with no finding.
    A build with no option has the build kind as the name of its
    configuration.

    unused() gives a warning for each row of the name of the configuration of
    the build with an item that the build does not hold.  The bench and fuzz
    build then gives no warning for a row of its build kind.  A bench object
    gets a row of the name of the bench and fuzz build.  The build of the
    build kind gives no warning for that row.  Such a row stays until no build
    of its configuration makes the item.

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
RISEN = "risen"
UNNEEDED = "unneeded"
# The noise of each unit of a measured rule, as a part of the kept value of a row (THE NOISE).
NOISE = {"s": 1.0, "GB": 0.1, "G": 0.02}


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
    """One ledger row of the policy max: its line, the kept value at the last write, and its reason."""

    __match_args__ = ("line", "value", "reason")
    line: int
    value: float
    reason: str

    def __init__(self, line: int, value: float, reason: str) -> None:
        """Hold the line, the kept value and the reason of one row."""
        super().__init__(line, value, reason)


class Rows(Value):
    """The ledger rows of one rule of the policy max that judge one build (THE ROWS).

    `kind` is the name of the configuration of the build.  `admitted` holds the
    row of each item that a row keeps.  `own` holds each item of a row of that
    name, which unused() reads.
    """

    __match_args__ = ("shown", "kind", "admitted", "problems", "own")
    shown: str
    kind: str
    admitted: Mapping[str, Admission]
    problems: tuple[check_report.Finding, ...]
    own: frozenset[str]

    def __init__(
        self,
        shown: str,
        kind: str,
        admitted: Mapping[str, Admission],
        problems: tuple[check_report.Finding, ...] = (),
        own: frozenset[str] | None = None,
    ) -> None:
        """Hold the shown ledger, the name, the row of each item, the problems of the file and the items of the name.

        `own` None makes each item of `admitted` an item of the name.
        """
        super().__init__(shown, kind, admitted, problems, frozenset(admitted) if own is None else own)


def noise_of(unit: str) -> float:
    """Return the noise of a unit, as a part of the kept value of a row (THE NOISE)."""
    return NOISE.get(unit, 0.0)


def kept_value(value: float) -> float:
    """Return the value that a write puts in a row: the measure with three decimals, rounded up.

    A kept value below its measure gives RISEN in the next run of a unit with no noise.  The difference of 1e-9 keeps
    a measure with three decimals at its own value.
    """
    import math

    return math.ceil(value * 1000.0 - 1e-9) / 1000.0


def verdict(value: float, warning: float, error: float, kept: float | None, noise: float = 0.0) -> str:
    """Return the verdict of one value against two thresholds and the ledger row of its item (THE VERDICT).

    Args:
        value: The value, in the unit of the measured rule
        warning: The warning threshold
        error: The error threshold
        kept: The kept value of the row that keeps the item for the build, or None for no row
        noise: The noise of the unit of the rule (noise_of())
    """
    if kept is None:
        if value > error:
            return ERROR
        return WARNING if value > warning else NO_FINDING
    if value * (1.0 + noise) <= warning:
        return UNNEEDED
    if value <= warning:
        return NO_FINDING
    return ADMITTED if value <= kept * (1.0 + noise) else RISEN


def risen_level(value: float, error: float) -> check_report.Level:
    """Return the level of a RISEN verdict: the level of the value with no row (THE VERDICT)."""
    return "error" if value > error else "warning"


def lookup(
    rows: Mapping[tuple[str, str], tuple[int, float, str]], kind: str, item: str, configuration: str | None
) -> tuple[int, float, str] | None:
    """Return the row of one item: of the name of the configuration, else of the build kind (THE ROWS)."""
    if configuration:
        found = rows.get((configuration, item))
        if found is not None:
            return found
    return rows.get((kind, item))


def row_of(
    rule_id: str, shape: str, ledger: str, kind: str, item: str, configuration: str | None = None
) -> tuple[Admission | None, str, str]:
    """Return the row of one item that judges a build, from the ledger of a rule of the policy max (THE ROWS).

    Args:
        rule_id: The ID of the rule
        shape: The shape of the item of a row: ITEM_OUTPUT, ITEM_FILE or ITEM_NAME of assay.core.meter
        ledger: The file of the ledger.  The file is not there when the rule has no row
        kind: The kind of the build
        item: The item
        configuration: The name of the configuration of the build, or None for the build kind

    Returns:
        The row or None, the ledger as a message shows it, and the first problem of the ledger as `LINE: TEXT`, or an
        empty text.  A ledger that cannot be read gives no row and the problem at the line 0
    """
    from assay.core import meter as cost_meter

    try:
        rows, problems, shown = cost_meter.read_kind_rows(rule_id, shape, ledger)
    except OSError as error:
        return None, cost_meter.shown_ledger(ledger), f"0: the ledger cannot be read ({error})"
    found = lookup(rows, kind, item, configuration)
    problem = f"{problems[0][0]}: {problems[0][1]}" if problems else ""
    return (None if found is None else Admission(*found)), shown, problem


def read_rows(rule_id: str, shape: str, ledger: Path, kind: str, configuration: str | None = None) -> Rows:
    """Read the rows that judge one build from the ledger of a rule of the policy max (THE ROWS).

    Complexity: linear in the rows.

    Args:
        rule_id: The ID of the rule
        shape: The shape of the item of a row: ITEM_OUTPUT, ITEM_FILE or ITEM_NAME of assay.core.meter
        ledger: The file of the ledger.  The file is not there when the rule has no row
        kind: The kind of the build
        configuration: The name of the configuration of the build, or None for the build kind

    Returns:
        The rows of the build.  Also an error for each row that does not have the shape of the rule, and an error for
        a file that cannot be read
    """
    from assay.core import meter as cost_meter
    from assay.core import report as check_report

    name = configuration or kind
    try:
        rows, problems, shown = cost_meter.read_kind_rows(rule_id, shape, str(ledger))
    except OSError as problem:
        shown = cost_meter.shown_ledger(str(ledger))
        return Rows(
            shown,
            name,
            {},
            (check_report.Finding("error", shown, 0, rule_id, f"the ledger cannot be read ({problem})"),),
        )
    admitted = {
        item: Admission(line, value, reason)
        for (row_kind, item), (line, value, reason) in rows.items()
        if row_kind == name
    }
    own = frozenset(admitted)
    for (row_kind, item), (line, value, reason) in rows.items():
        if row_kind == kind and item not in admitted:
            admitted[item] = Admission(line, value, reason)
    errors = tuple(check_report.Finding("error", shown, line, rule_id, text) for line, text in problems)
    return Rows(shown, name, admitted, errors, own)


def judge(rule_id: str, budget: check_report.Budget, measure: Measure, rows: Rows) -> check_report.Finding | None:
    """Judge the value of one item (THE JUDGE).

    Args:
        rule_id: The ID of the rule of the finding
        budget: The thresholds: of the rule, or of the held rule that measure.threshold_rule names
        measure: The value of the item
        rows: The ledger rows that judge the build

    Returns:
        The finding, or None when the value is not more than a threshold and no row keeps the item
    """
    row = rows.admitted.get(measure.item)
    noise = noise_of(budget.unit)
    found = verdict(measure.value, budget.warn, budget.error, None if row is None else row.value, noise)
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
    over = "error" if measure.value > budget.error else "warning"
    limit = budget.error if over == "error" else budget.warn
    kept = f"{row.value:g} {budget.unit} with the noise {noise:.0%} of the unit {budget.unit}"
    if found == ADMITTED:
        return check_report.Finding(
            "warning",
            measure.path,
            0,
            rule_id,
            f"{measure.said}, over the {over} threshold{of_rule} {limit:g} {budget.unit}.  The row {rows.shown}:"
            f"{row.line} keeps it up to {kept}: {row.reason}",
        )
    if found == RISEN:
        return check_report.judged(
            risen_level(measure.value, budget.error),
            measure.path,
            0,
            rule_id,
            f"{measure.said}, over the {over} threshold{of_rule} {limit:g} {budget.unit}, and more than the row "
            f"{rows.shown}:{row.line} keeps: {kept}.  Make the item smaller.  When the growth is wanted, measure it "
            f"again and write the row with `python3 utils/bin/guard write --rule {rule_id}`",
        )
    return check_report.judged(
        "error",
        rows.shown,
        row.line,
        rule_id,
        f"the row keeps {measure.item} in {rows.kind}, and it is at {measure.value:.2f} {budget.unit}.  With the "
        f"noise {noise:.0%} of the unit, it stays at or under the warning threshold{of_rule} {budget.warn:g} "
        f"{budget.unit}.  Remove the row",
    )


def unused(rule_id: str, rows: Rows, held: Iterable[str]) -> list[check_report.Finding]:
    """Give a warning for each row of the name of the configuration with an item that the build does not hold.

    Complexity: linear in the rows and the held items.

    Args:
        rule_id: The ID of the rule
        rows: The ledger rows that judge the build
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
            f"the build of {rows.kind} holds no {item}.  When no build of {rows.kind} makes it, remove the row",
        )
        for item, row in sorted(rows.admitted.items(), key=lambda pair: pair[1].line)
        if item in rows.own and item not in present
    ]
