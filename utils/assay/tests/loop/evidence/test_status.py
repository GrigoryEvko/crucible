"""The test of the status of the evidence (assay.loop.evidence.status).

`guard selftest tests.loop.evidence.test_status` runs the test.  Each case writes one line.  Exit 0 when each case
holds, else 1.

The cases hold the rows of planted items with the targets of a planted class, the remaining work, and the judgment of
each kind of row.  They also hold the JSON output and the table.
"""

import json
import tempfile
from dataclasses import replace
from pathlib import Path

from assay.core import testing
from assay.loop.evidence import items as evidence_items
from assay.loop.evidence import status as evidence_status
from assay.loop.evidence import store as evidence_store
from assay.loop.evidence import targets

RULE = targets.STRESS.rule
CLASSES = (replace(targets.STRESS, nightly=2.0, release=5.0),)
FAILURE = evidence_store.Failure("2026-10-14T12:00:00Z", "a planted failure", ("ctest", "-R", "^two$"), ())


def planted_rows(store: evidence_store.EvidenceStore) -> list[evidence_status.Row]:
    """Return the rows of four planted items: at the targets, with less evidence, with a failure and with no key."""
    sample = evidence_store.Sample("2026-10-14T12:00:00Z", 1.0, 0.25)
    for _ in range(5):
        store.add(RULE, "done", "key-done", sample)
    store.add(RULE, "short", "key-short", sample)
    store.keep_failure(RULE, "failed", "key-failed", FAILURE)
    found = [
        evidence_items.Item(RULE, "done", "build", "key-done"),
        evidence_items.Item(RULE, "short", "build", "key-short"),
        evidence_items.Item(RULE, "failed", "build", "key-failed"),
        evidence_items.Item(RULE, "loose", "build", "", "the test has no key: script: a planted reason"),
        evidence_items.Item("no-such-class", "other", "build", "key-other"),
    ]
    return evidence_status.rows_of(found, store, CLASSES)


def row_cases(expect: testing.Expect) -> None:
    """Hold the rows of planted items, the remaining work and the judgment (THE ROWS, THE TARGETS, THE JUDGMENT)."""
    with tempfile.TemporaryDirectory(prefix="evidence-status-") as directory:
        rows = planted_rows(evidence_store.EvidenceStore(Path(directory)))
    by_item = {row.item: row for row in rows}
    expect("a row comes only for an item of a given class", set(by_item) == {"done", "short", "failed", "loose"})
    expect(
        "a row gives the evidence of the record at its key, and the unit and the targets of its class",
        by_item["done"].value == 5.0
        and by_item["short"].value == 1.0
        and (by_item["done"].unit, by_item["done"].nightly, by_item["done"].release) == ("passes", 2.0, 5.0),
    )
    expect("a row gives the wall time of one unit from its samples", by_item["done"].unit_wall_s == 0.25)
    expect(
        "the remaining work is the target less the evidence, and never less than 0",
        evidence_status.remaining(by_item["short"]) == 1.0
        and evidence_status.remaining(by_item["short"], True) == 4.0
        and evidence_status.remaining(by_item["done"], True) == 0.0,
    )
    found = {(finding.entity, finding.level, finding.kind) for finding in evidence_status.judged(rows, "x")}
    expect(
        f"the judgment gives an error for a kept failure, and no finding for no key or less evidence: {found}",
        found == {("failed", "error", "failure")},
    )
    table = evidence_status.table(rows)
    expect(
        "the table gives each row and its state",
        "failure: a planted failure" in table
        and "no key:" in table
        and "at the two targets" in table
        and "1 more for the nightly target" in table,
    )


def json_cases(expect: testing.Expect) -> None:
    """Hold the JSON object of the rows: its format, a row and the problems of the builds (THE OUTPUT)."""
    with tempfile.TemporaryDirectory(prefix="evidence-status-") as directory:
        rows = planted_rows(evidence_store.EvidenceStore(Path(directory)))
    found = json.loads(evidence_status.status_json(rows, ("a planted problem",)))
    by_item = {row["item"]: row for row in found["rows"]}
    expect(
        "the object gives its format, each row and the problems",
        found["format"] == evidence_status.FORMAT
        and set(by_item) == {"done", "short", "failed", "loose"}
        and found["problems"] == ["a planted problem"],
    )
    expect(
        "a row of the object gives the remaining work of the two targets and the kept failure",
        by_item["short"]["remaining_nightly"] == 1.0
        and by_item["short"]["remaining_release"] == 4.0
        and by_item["failed"]["failure"]["summary"] == "a planted failure",
    )


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(__name__.removeprefix("assay."), argv, (row_cases, json_cases))
