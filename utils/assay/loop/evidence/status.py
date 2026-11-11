"""status — the evidence of each item of the sampled checks against its target, and the remaining work.

THE ROWS
    rows_of() gives one row for each item (assay.loop.evidence.items): the
    name of its class, its kind of evidence, its unit, its name, its build
    and its key.  The row also gives the evidence of the record of the item
    at its key (assay.loop.evidence.store), and the two targets of the class.
    It gives the wall time of one unit of evidence and the kept failure too.
    An item with no key gives the reason, and its evidence is 0.

THE TARGETS
    Each class has a nightly target and a release target
    (assay.loop.evidence.targets).  remaining() gives the units of evidence
    that are necessary for one of the two targets.  The wall time of one unit
    comes from the samples of the record.  An entry can then estimate the
    time of the remaining work.

THE JUDGMENT
    judged() gives an error for each item with a kept failure, with the name
    of its class.  The failure stays until its key changes
    (assay.loop.evidence.store, THE FAILURE).  An item with no key, or with
    less evidence than a target, gives no finding.  table() shows it.

THE OUTPUT
    status_json() gives the rows as one JSON object, with FORMAT, the list
    "rows" and the list "problems" of the builds that give no items.  An
    entry reads that object to schedule the remaining work in a time budget.
    table() gives the rows as text.

`guard selftest tests.loop.evidence.test_status` does a test of the module.
"""

import json
from collections.abc import Sequence
from dataclasses import dataclass

from assay.core import report as check_report
from assay.loop.evidence import items as evidence_items
from assay.loop.evidence import store as evidence_store
from assay.loop.evidence import targets

# The format of the status object (THE OUTPUT).
FORMAT = "crucible-evidence-status 1"


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
    classes: Sequence[targets.Target] = targets.TARGETS,
) -> list[Row]:
    """Return a row for each item of a class of `classes`, with the targets of that class (THE ROWS).

    Complexity: one read of the store for each item with a key.
    """
    by_name = {target.rule: target for target in classes}
    rows: list[Row] = []
    for item in found:
        target = by_name.get(item.rule)
        if target is None:
            continue
        record = store.read(item.rule, item.key) if item.key else None
        rows.append(
            Row(
                item.rule,
                str(target.kind),
                target.unit,
                item.name,
                item.build,
                item.key,
                item.reason,
                0.0 if record is None else record.value,
                target.nightly,
                target.release,
                None if record is None else record.unit_wall_s,
                None if record is None else record.failure,
            )
        )
    return rows


def judged(rows: Sequence[Row], place: str) -> list[check_report.Finding]:
    """Return an error for each row with a kept failure (THE JUDGMENT).

    Complexity: linear in the rows.
    """
    found: list[check_report.Finding] = []
    for row in rows:
        if row.failure is None:
            continue
        rerun = " ".join(row.failure.rerun)
        message = (
            f"the item {row.item} of {row.rule} has a failure at its key since {row.failure.at}: {row.failure.summary}."
            f"  The failure stays until the key changes.  Run it again with: {rerun}"
        )
        found.append(check_report.Finding("error", place, 0, row.rule, message, "failure", row.item))
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


def table(rows: Sequence[Row]) -> str:
    """Return the rows as a text table: the class, the item, the evidence, the targets and the state."""
    lines = [("class", "item", "evidence", "nightly", "release", "state")]
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
