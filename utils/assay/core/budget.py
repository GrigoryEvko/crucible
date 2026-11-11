"""The judge of the measured value of one item against a measured rule and the ledger row of the policy max.

THE VERDICT
    verdict() compares the value of one item with the limit of its kind in a
    measured rule (assay.core.registry).  It also reads the kept value of the
    ledger row of the kind and the item, when the ledger has a row that
    judges the build:
      ERROR     the value is above the limit, and no row keeps it
      ADMITTED  a row keeps the item.  The value is above the limit, and it
                is not more than the kept value with the noise of its unit.
                The judge gives no finding
      RISEN     a row keeps the item, and the value is more than the kept
                value with the noise of its unit.  The judge gives an error
                that names the row.  A row then cannot hide a drift
    NO_FINDING is the verdict of each other value.  A value at or below the
    limit gives no finding, also when a row keeps its item.  A ledger fails
    only on a growth (THE POLICIES of assay.core.ledger).  A row above its
    value passes.  A value that is equal to the limit or to the kept value
    with its noise is not more than it.  Each judge of a measured value
    calls verdict(): judge() of this module, check_report.classify(), and
    the judges of the step meters.

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
    a header are the same in two builds of one tree.  A message gives a unit
    with its display text, such as "G instructions" for G (UNIT_TEXTS, and
    unit_text() of assay.core.report).  noise_of() reads the unit or its
    display text.  A judge of a check_report.Budget then gets the noise of its
    unit too.

THE JUDGE
    judge() gives the finding of one verdict for a check of the engines: an
    error for ERROR and RISEN, and no finding for each other verdict.

THE SCOPE
    The measured rules of a compile, a link, an object and a test do not
    judge the outputs of the directory bench of a build: its objects, its
    links and its tests.  A bench measures a hot path on purpose, so its
    cost follows from what it measures, and only a bench build makes it.
    is_judged() reads an output relative to the build directory, and each
    judge of such a rule calls it: the rules of cost_budgets, `loop step`
    and `loop test-step`.  An output that is not in the build directory is
    judged.

THE ROWS
    The ledger of a rule of the policy max holds at most one row for each
    kind and item, and each row judges each build of the tree.  THE REGISTRY
    AND THE LEDGERS of assay.core.meter gives the row.  read_rows() gives the
    rows of the kinds that a judge reads, and row_of() gives the row of one
    kind and item, with no finding.  A row whose item a build does not hold
    gives no finding.

THE WRITE
    raised_ledger() gives the new ledger of `guard write` from the measures
    of a build.  The kind of the limit of a measure is the kind of its row.
    It writes a row only for an item above the limit that no
    row keeps: a new row, or a higher kept value of a row whose item rose
    above it with its noise (THE VERDICT).  Such a row needs the reason of
    the call, and a call with no reason writes nothing.  The write removes
    no row and lowers no kept value.  A build that does not hold an item,
    or holds it with a lower value, then keeps the row of a different build.
    A person removes a row whose item stays at or below the limit.

THE STEP METERS
    `loop step`, `loop test-step` and `loop fixture` judge each step with
    verdict() and row_of(), and they give their own messages.  The import
    closure of a step meter does not hold assay.core.report, which imports
    argparse and re.  This module then imports only
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

    from assay.core import ledger as rule_ledger
    from assay.core import report as check_report

# The verdicts of one value (THE VERDICT).
NO_FINDING = ""
ERROR = "error"
ADMITTED = "admitted"
RISEN = "risen"
# The noise of each unit of a measured rule, as a part of the kept value of a row (THE NOISE).
NOISE = {"s": 1.0, "GB": 0.1, "G": 0.02}
# The display text of a unit of a measured rule, when it is not the unit itself.  G is 10^9 instructions.
UNIT_TEXTS = {"G": "G instructions"}
# The directory of a build whose outputs no measured rule judges (THE SCOPE).
BENCH_DIRECTORY = "bench"


def is_judged(output: str) -> bool:
    """Tell if the measured rules judge an output, from its path relative to the build directory (THE SCOPE).

    Args:
        output: The output relative to the build directory, with '/' between its parts.  An absolute path or a path
            that starts with '..' names an output outside the build directory

    Returns:
        False for an output in the directory bench of the build, else True
    """
    return output.split("/", 1)[0] != BENCH_DIRECTORY


class Measure(Value):
    """The measured value of one item, with the words that tell it.

    `item` is the item of the ledger key (assay.core.meter, item_of()).
    `path` is the position of the finding.  `said` tells the value, for example
    "the test t took 3.10 s of wall time".  `kind` is the kind of the item,
    which selects its limit, such as fixy for the rule header-alone, and ""
    for a rule with one limit.
    """

    __match_args__ = ("item", "path", "value", "said", "kind")
    item: str
    path: str
    value: float
    said: str
    kind: str

    def __init__(self, item: str, path: str, value: float, said: str, kind: str = "") -> None:
        """Hold the item, the position, the value, the words and the kind of the item."""
        super().__init__(item, path, value, said, kind)


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
    """The ledger rows of one rule of the policy max (THE ROWS).

    `admitted` holds the row of each kind and item that a row keeps.
    """

    __match_args__ = ("shown", "admitted", "problems")
    shown: str
    admitted: Mapping[tuple[str, str], Admission]
    problems: tuple[check_report.Finding, ...]

    def __init__(
        self, shown: str, admitted: Mapping[tuple[str, str], Admission], problems: tuple[check_report.Finding, ...] = ()
    ) -> None:
        """Hold the shown ledger, the row of each item and the problems of the file."""
        super().__init__(shown, admitted, problems)


def noise_of(unit: str) -> float:
    """Return the noise of a unit or of its display text, as a part of the kept value of a row (THE NOISE)."""
    shown = {text: name for name, text in UNIT_TEXTS.items()}
    return NOISE.get(shown.get(unit, unit), 0.0)


def kept_value(value: float) -> float:
    """Return the value that a write puts in a row: the measure with three decimals, rounded up.

    A kept value below its measure gives RISEN in the next run of a unit with no noise.  The difference of 1e-9 keeps
    a measure with three decimals at its own value.
    """
    import math

    return math.ceil(value * 1000.0 - 1e-9) / 1000.0


def verdict(value: float, limit: float, kept: float | None, noise: float = 0.0) -> str:
    """Return the verdict of one value against the limit and the ledger row of its item (THE VERDICT).

    Args:
        value: The value, in the unit of the measured rule
        limit: The limit of the rule
        kept: The kept value of the row that keeps the item for the build, or None for no row
        noise: The noise of the unit of the rule (noise_of())
    """
    if value <= limit:
        return NO_FINDING
    if kept is None:
        return ERROR
    return ADMITTED if value <= kept * (1.0 + noise) else RISEN


def forms_of(shape: str | Mapping[str, str]) -> Mapping[str, str]:
    """Return the form of the item of each kind: a mapping as it is, or the one kind "" of a rule with one limit."""
    return {"": shape} if isinstance(shape, str) else shape


def row_of(rule_id: str, kind: str, shape: str, ledger: str, item: str) -> tuple[Admission | None, str, str]:
    """Return the row of one kind and item from the ledger of a rule of the policy max (THE ROWS).

    Args:
        rule_id: The ID of the rule
        kind: The kind of the item, or "" for a rule with one limit
        shape: The shape of the item of a row: ITEM_OUTPUT, ITEM_FILE or ITEM_NAME of assay.core.meter
        ledger: The file of the ledger.  The file is not there when the rule has no row
        item: The item

    Returns:
        The row or None, the ledger as a message shows it, and the first problem of the ledger as `LINE: TEXT`, or an
        empty text.  A ledger that cannot be read gives no row and the problem at the line 0
    """
    from assay.core import meter as cost_meter

    try:
        rows, problems, shown = cost_meter.read_ledger_rows(rule_id, {kind: shape}, ledger)
    except OSError as error:
        return None, cost_meter.shown_ledger(ledger), f"0: the ledger cannot be read ({error})"
    found = rows.get((kind, item))
    problem = f"{problems[0][0]}: {problems[0][1]}" if problems else ""
    return (None if found is None else Admission(*found)), shown, problem


def read_rows(rule_id: str, shape: str | Mapping[str, str], ledger: Path) -> Rows:
    """Read the rows of the ledger of a rule of the policy max, for the kinds that a judge reads (THE ROWS).

    Complexity: linear in the rows.

    Args:
        rule_id: The ID of the rule
        shape: The shape of the item of a row of a rule with one limit: ITEM_OUTPUT, ITEM_FILE or ITEM_NAME of
            assay.core.meter.  For a rule with one limit for each kind, the shape of the item of each kind that the
            judge reads
        ledger: The file of the ledger.  The file is not there when the rule has no row

    Returns:
        The rows.  Also an error for each row that does not have the shape of its kind or has a kind that the rule
        does not have, and an error for a file that cannot be read
    """
    from assay.core import meter as cost_meter
    from assay.core import report as check_report

    try:
        rows, problems, shown = cost_meter.read_ledger_rows(rule_id, forms_of(shape), str(ledger))
    except OSError as problem:
        shown = cost_meter.shown_ledger(str(ledger))
        unread = check_report.Finding("error", shown, 0, rule_id, f"the ledger cannot be read ({problem})")
        return Rows(shown, {}, (unread,))
    admitted = {key: Admission(line, value, reason) for key, (line, value, reason) in rows.items()}
    errors = tuple(check_report.Finding("error", shown, line, rule_id, text) for line, text in problems)
    return Rows(shown, admitted, errors)


def raised_ledger(
    ledger: rule_ledger.Ledger,
    shape: str | Mapping[str, str],
    measures: Iterable[tuple[str, float, check_report.Budget]],
    reason: str | None,
) -> rule_ledger.Ledger:
    """Return the ledger of a rule of the policy max with a row for each measure that no row keeps (THE WRITE).

    The kind of the limit of a measure (check_report.Budget) is the kind of its row.  Complexity: linear in the
    measures and the rows.

    Args:
        ledger: The ledger at this time
        shape: The shape of the item of a row, as read_rows() takes it
        measures: The item, the value and the limit of each measure of the build
        reason: The reason of each new row and each higher kept value, or None

    Raises:
        rule_ledger.LedgerError: If a measure needs a row and the call gives no reason
    """
    from dataclasses import replace

    from assay.core import ledger as rule_ledger
    from assay.core import meter as cost_meter

    forms = forms_of(shape)
    base = dict(ledger.base)
    unexplained: list[str] = []
    for item, value, budget in measures:
        path, entity = cost_meter.item_cells(item, forms[budget.kind])
        key = rule_ledger.Key(path, budget.kind, entity)
        earlier = base.get(key)
        kept = None if earlier is None else float(earlier.value)
        if verdict(value, budget.limit, kept, noise_of(budget.unit)) not in (ERROR, RISEN):
            continue
        if not reason:
            unexplained.append(f"{item} of the kind {budget.kind}" if budget.kind else item)
            continue
        base[key] = rule_ledger.Entry(key, max(kept or 0.0, kept_value(value)), reason)
    if unexplained:
        raise rule_ledger.LedgerError(
            f"{len(unexplained)} items are more than the limit, and no row keeps them up to their values, such as "
            f"{unexplained[0]}.  Give the reason of their rows with --reason"
        )
    return replace(ledger, base=base)


def judge(rule_id: str, budget: check_report.Budget, measure: Measure, rows: Rows) -> check_report.Finding | None:
    """Judge the value of one item (THE JUDGE).

    Args:
        rule_id: The ID of the rule of the finding
        budget: The limit of the kind of the item (measure.kind)
        measure: The value of the item
        rows: The ledger rows that judge the build

    Returns:
        The error of a value above the limit that no row keeps, or None
    """
    row = rows.admitted.get((measure.kind, measure.item))
    noise = noise_of(budget.unit)
    found = verdict(measure.value, budget.limit, None if row is None else row.value, noise)
    if found in (NO_FINDING, ADMITTED):
        return None
    from assay.core import report as check_report

    of_kind = f" of the kind {measure.kind}" if measure.kind else ""
    above = f"{measure.said}, above the limit{of_kind} {budget.limit:g} {budget.unit}"
    if row is None:
        return check_report.judged("error", measure.path, 0, rule_id, above)
    kept = f"{row.value:g} {budget.unit} with the noise {noise:.0%} of the unit {budget.unit}"
    return check_report.judged(
        "error",
        measure.path,
        0,
        rule_id,
        f"{above}, and more than the row {rows.shown}:{row.line} keeps: {kept}.  Make the item smaller.  When the "
        f"growth is wanted, measure it again and write the row with `python3 utils/bin/guard write --rule {rule_id} "
        f"--build-dir DIR --reason TEXT`",
    )
