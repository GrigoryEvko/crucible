"""status — the evidence of each item of the sampled checks against its target, and the remaining work.

THE ROWS
    rows_of() gives one row for each item (assay.loop.evidence.items): its
    rule, its kind of evidence, its unit, its name, its build and its key.
    The row also gives the evidence of the record of the item at its key
    (assay.loop.evidence.store), and the targets of the rule.  It gives the
    wall time of one unit of evidence and the kept failure too.  An item with
    no key gives the reason, and its evidence is 0.

THE TARGETS
    The warning threshold of the rule of an item is its target for the
    nightly entry (assay.loop.evidence.targets).  The error threshold is its
    target for the release entry.  remaining() gives the units of evidence
    that are necessary for one of the two targets.  The wall time of one unit
    comes from the samples of the record.  An entry can then estimate the
    time of the remaining work.

THE JUDGMENT
    judged() gives these findings, each with the rule of the item:
      * An error for an item with a kept failure: the failure stays until
        its key changes (assay.loop.evidence.store, THE FAILURE)
      * A warning for an item with no key
      * A warning for an item with less evidence than its target: the
        nightly target, or with is_release the release target.
    `loop evidence status` judges the rows of the tree.  The witness of each
    rule of TARGETS judges the planted rows of the file STATUS_FILE of a
    planted build with the same function (assay.guard.engines.buildsys.held).

THE OUTPUT
    status_json() gives the rows as one JSON object, with FORMAT, the list
    "rows" and the list "problems" of the builds that give no items.  An
    entry reads that object to schedule the remaining work in a time budget.
    read_status() reads the rows of such an object from a file, and table()
    gives the rows as text.

`guard selftest tests.loop.evidence.test_status` does a test of the module.
"""

import json
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import report as check_report
from assay.loop.evidence import items as evidence_items
from assay.loop.evidence import store as evidence_store
from assay.loop.evidence import targets

# The format of the status object (THE OUTPUT), and the file of the planted rows of a witness.
FORMAT = "crucible-evidence-status 1"
STATUS_FILE = "evidence-status.json"


class StatusError(ValueError):
    """A status object does not have the shape of THE OUTPUT."""


@dataclass(frozen=True, slots=True)
class Row:
    """The evidence of one item against its targets (THE ROWS)."""

    rule: str
    kind: str
    unit: str
    item: str
    build: str
    key: str
    reason: str
    value: float
    nightly: float
    release: float
    unit_wall_s: float | None = None
    failure: evidence_store.Failure | None = None


def remaining(row: Row, is_release: bool = False) -> float:
    """Return the units of evidence that are necessary for the nightly or the release target of a row (THE TARGETS)."""
    return max(0.0, (row.release if is_release else row.nightly) - row.value)


def rows_of(
    found: Sequence[evidence_items.Item],
    store: evidence_store.EvidenceStore,
    budgets: Mapping[str, check_report.Budget],
) -> list[Row]:
    """Return a row for each item with a rule of TARGETS in the budgets (THE ROWS).

    Complexity: one read of the store for each item with a key.
    """
    kinds = {target.rule: target.kind for target in targets.TARGETS}
    rows: list[Row] = []
    for item in found:
        budget = budgets.get(item.rule)
        if budget is None or item.rule not in kinds:
            continue
        record = store.read(item.rule, item.key) if item.key else None
        rows.append(
            Row(
                item.rule,
                str(kinds[item.rule]),
                budget.unit,
                item.name,
                item.build,
                item.key,
                item.reason,
                0.0 if record is None else record.value,
                budget.warn,
                budget.error,
                None if record is None else record.unit_wall_s,
                None if record is None else record.failure,
            )
        )
    return rows


def judged(
    rows: Sequence[Row], budgets: Mapping[str, check_report.Budget], place: str, is_release: bool = False
) -> list[check_report.Finding]:
    """Return the findings of the rows against the targets of the budgets (THE JUDGMENT).

    A row of a rule with no budget gives no finding.  Complexity: linear in the rows.
    """
    found: list[check_report.Finding] = []
    for row in rows:
        budget = budgets.get(row.rule)
        if budget is None:
            continue
        what = f"the item {row.item} of {row.rule}"
        if row.failure is not None:
            rerun = " ".join(row.failure.rerun)
            message = (
                f"{what} has a failure at its key since {row.failure.at}: {row.failure.summary}.  The failure stays "
                f"until the key changes.  Run it again with: {rerun}"
            )
            found.append(check_report.Finding("error", place, 0, row.rule, message, "failure", row.item))
            continue
        if not row.key:
            message = f"{what} has no key.  No run adds evidence to it: {row.reason}"
            found.append(check_report.Finding("warning", place, 0, row.rule, message, "no-key", row.item))
            continue
        target = budget.error if is_release else budget.warn
        if row.value < target:
            entry = "release" if is_release else "nightly"
            message = (
                f"{what} has {row.value:g} {budget.unit} of evidence at its key, less than the {entry} target "
                f"{target:g} {budget.unit}.  Run `loop evidence run {row.kind}`"
            )
            found.append(check_report.Finding("warning", place, 0, row.rule, message, "short", row.item))
    return found


def failure_json(failure: evidence_store.Failure | None) -> object:
    """Return a failure as a JSON object, or None."""
    if failure is None:
        return None
    return {
        "at": failure.at,
        "summary": failure.summary,
        "rerun": list(failure.rerun),
        "facts": evidence_store.facts_json(failure.facts),
    }


def status_json(rows: Sequence[Row], problems: Sequence[str] = ()) -> str:
    """Return the rows, and the problem of each build that gives no items, as one JSON object (THE OUTPUT)."""
    return json.dumps(
        {
            "format": FORMAT,
            "problems": list(problems),
            "rows": [
                {
                    "rule": row.rule,
                    "kind": row.kind,
                    "unit": row.unit,
                    "item": row.item,
                    "build": row.build,
                    "key": row.key,
                    "reason": row.reason,
                    "value": row.value,
                    "nightly": row.nightly,
                    "release": row.release,
                    "remaining_nightly": remaining(row),
                    "remaining_release": remaining(row, True),
                    "unit_wall_s": row.unit_wall_s,
                    "failure": failure_json(row.failure),
                }
                for row in rows
            ],
        },
        indent=1,
        sort_keys=True,
    )


def row_of(value: object) -> Row:
    """Return the row of one JSON object of the list "rows".

    Raises:
        StatusError: If the object does not have the shape of a row
    """
    if not isinstance(value, dict):
        raise StatusError("a row is not a JSON object")
    texts = [value.get(name) for name in ("rule", "kind", "unit", "item", "build", "key", "reason")]
    numbers = [evidence_store.number_of(value.get(name)) for name in ("value", "nightly", "release")]
    wall = value.get("unit_wall_s")
    if not all(isinstance(text, str) for text in texts) or any(number is None for number in numbers):
        raise StatusError(f"the row {value.get('item')!r} has no text or no number of a field")
    if wall is not None and evidence_store.number_of(wall) is None:
        raise StatusError(f"the field unit_wall_s of the row {value.get('item')!r} is not a number")
    failure = evidence_store.failure_of(value.get("failure"))
    if value.get("failure") is not None and failure is None:
        raise StatusError(f"the failure of the row {value.get('item')!r} does not have the shape of a failure")
    rule, kind, unit, item, build, key, reason = (str(text) for text in texts)
    count, nightly, release = (float(number or 0.0) for number in numbers)
    return Row(
        rule, kind, unit, item, build, key, reason, count, nightly, release, evidence_store.number_of(wall), failure
    )


def read_status(path: Path) -> list[Row]:
    """Return the rows of a status object in a file (THE OUTPUT).

    Raises:
        StatusError: If the file cannot be read, or it does not hold a status object
    """
    try:
        found = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as problem:
        raise StatusError(f"{path} does not hold a JSON status: {problem}") from None
    if not isinstance(found, dict) or found.get("format") != FORMAT or not isinstance(found.get("rows"), list):
        raise StatusError(f"{path} does not hold an object of the format {FORMAT!r} with a list rows")
    return [row_of(item) for item in found["rows"]]


def table(rows: Sequence[Row]) -> str:
    """Return the rows as a text table: the rule, the item, the evidence, the targets and the state."""
    lines = [("rule", "item", "evidence", "nightly", "release", "state")]
    for row in rows:
        if row.failure is not None:
            state = f"failure: {row.failure.summary}"
        elif not row.key:
            state = f"no key: {row.reason}"
        elif remaining(row, True) <= 0:
            state = "at the two targets"
        elif remaining(row) <= 0:
            state = f"{remaining(row, True):g} more for the release target"
        else:
            state = f"{remaining(row):g} more for the nightly target"
        lines.append((row.rule, row.item, f"{row.value:g} {row.unit}", f"{row.nightly:g}", f"{row.release:g}", state))
    widths = [max(len(line[index]) for line in lines) for index in range(len(lines[0]) - 1)]
    return "\n".join(
        "  ".join([*(cell.ljust(width) for cell, width in zip(line[:-1], widths, strict=True)), line[-1]])
        for line in lines
    )
