"""The test of the status of the evidence (assay.loop.evidence.status).

`guard selftest tests.loop.evidence.test_status` runs the test.  Each case writes one line.  Exit 0 when each case
holds, else 1.

The cases hold the rows of planted items, the remaining work, and the judgment of each kind of row against each
target.  They also hold the JSON output and its refusals, and the table.
"""

import tempfile
from pathlib import Path

from assay.core import report as check_report
from assay.core import testing
from assay.loop.evidence import items as evidence_items
from assay.loop.evidence import status as evidence_status
from assay.loop.evidence import store as evidence_store
from assay.loop.evidence import targets

RULE = targets.STRESS.rule
BUDGETS = {RULE: check_report.Budget(RULE, 2.0, 5.0, "passes", "the passes of a test")}
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
        evidence_items.Item("no-such-rule", "other", "build", "key-other"),
    ]
    return evidence_status.rows_of(found, store, BUDGETS)


def row_cases(expect: testing.Expect) -> None:
    """Hold the rows of planted items, the remaining work and the judgment (THE ROWS, THE TARGETS, THE JUDGMENT)."""
    with tempfile.TemporaryDirectory(prefix="evidence-status-") as directory:
        rows = planted_rows(evidence_store.EvidenceStore(Path(directory)))
    by_item = {row.item: row for row in rows}
    expect(
        "a row comes only for an item of a rule of TARGETS with a budget",
        set(by_item) == {"done", "short", "failed", "loose"},
    )
    expect(
        "a row gives the evidence of the record at its key",
        by_item["done"].value == 5.0 and by_item["short"].value == 1.0,
    )
    expect("a row gives the wall time of one unit from its samples", by_item["done"].unit_wall_s == 0.25)
    expect(
        "the remaining work is the target less the evidence, and never less than 0",
        evidence_status.remaining(by_item["short"]) == 1.0
        and evidence_status.remaining(by_item["short"], True) == 4.0
        and evidence_status.remaining(by_item["done"], True) == 0.0,
    )
    nightly = {(finding.entity, finding.level, finding.kind) for finding in evidence_status.judged(rows, BUDGETS, "x")}
    expect(
        f"the nightly judgment gives an error for a failure and a warning for no key and for less evidence: {nightly}",
        nightly == {("failed", "error", "failure"), ("loose", "warning", "no-key"), ("short", "warning", "short")},
    )
    release = {(finding.entity, finding.kind) for finding in evidence_status.judged(rows, BUDGETS, "x", True)}
    expect(
        "the release judgment uses the release target, and an item at the nightly target has less evidence",
        ("short", "short") in release and ("done", "short") not in release,
    )
    expect("a row of a rule with no budget gives no finding", not evidence_status.judged(rows, {}, "x"))
    table = evidence_status.table(rows)
    expect(
        "the table gives each row and its state",
        "failure: a planted failure" in table
        and "no key:" in table
        and "at the two targets" in table
        and "1 more for the nightly target" in table,
    )


def json_cases(expect: testing.Expect) -> None:
    """Hold the JSON object of the rows: the rows through a file and back, and the refusals (THE OUTPUT)."""
    with tempfile.TemporaryDirectory(prefix="evidence-status-") as directory:
        root = Path(directory)
        rows = planted_rows(evidence_store.EvidenceStore(root / "store"))
        path = root / evidence_status.STATUS_FILE
        path.write_text(evidence_status.status_json(rows), encoding="utf-8")
        expect("the rows go through the JSON object of a file", evidence_status.read_status(path) == rows)
        for name, text in (
            ("a text that is not JSON", "{"),
            ("an object of a different format", '{"format": "other", "rows": []}'),
            ("a row with no number", f'{{"format": "{evidence_status.FORMAT}", "rows": [{{"rule": "r"}}]}}'),
        ):
            path.write_text(text, encoding="utf-8")
            try:
                evidence_status.read_status(path)
                expect(f"read_status refuses {name}", False)
            except evidence_status.StatusError:
                expect(f"read_status refuses {name}", True)


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(__name__.removeprefix("assay."), argv, (row_cases, json_cases))
