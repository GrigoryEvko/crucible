"""The test of the store of the evidence (assay.loop.evidence.store).

`guard selftest tests.loop.evidence.test_store` runs the test.  Each case writes one line.  Exit 0 when each case holds,
else 1.

The cases hold the record format, the samples that add to a record, and the first failure of a key.  They also hold
the separation of two keys and of two rules, a damaged entry, and the store with the caches off.
"""

import tempfile
from pathlib import Path

from assay.core import cache, testing
from assay.core import cache_root as root_rule
from assay.loop.evidence import store as evidence_store

RULE = "planted-passes"
SAMPLE = evidence_store.Sample("2026-10-14T12:00:00Z", 1.0, 0.5, (("round", "1"),))
FAILURE = evidence_store.Failure("2026-10-14T12:01:00Z", "a planted failure", ("true",), (("repetition", "2"),))


def format_cases(expect: testing.Expect) -> None:
    """Hold the JSON text of a record: a record through its text and back, and the refusal of a different shape."""
    record = evidence_store.Record(RULE, "one", "key-a", 2.0, (SAMPLE, SAMPLE), FAILURE)
    expect(
        "a record goes through its JSON text with each field",
        evidence_store.record_of(evidence_store.record_json(record)) == record,
    )
    expect(
        "a record with no samples has no wall time of one unit",
        evidence_store.Record(RULE, "x", "k").unit_wall_s is None,
    )
    expect(
        "the wall time of one unit is the wall time of the samples for each unit of quantity", record.unit_wall_s == 0.5
    )
    empty = evidence_store.record_json(evidence_store.Record(RULE, "i", "k"))
    for name, text in (
        ("a text that is not JSON", "{"),
        ("a record of a different format", '{"format": "other"}'),
        (
            "a record with no samples",
            f'{{"format": "{evidence_store.FORMAT}", "rule": "r", "item": "i", "key": "k", "value": 1}}',
        ),
        ("a record with a damaged failure", empty.replace('"failure": null', '"failure": 3')),
    ):
        expect(f"record_of refuses {name}", evidence_store.record_of(text) is None)


def store_cases(expect: testing.Expect) -> None:
    """Hold the samples and the failures of a store in a scratch directory (THE WRITE, THE FAILURE)."""
    with tempfile.TemporaryDirectory(prefix="evidence-store-") as directory:
        store = evidence_store.EvidenceStore(Path(directory))
        expect("a store with no entry reads no record", store.read(RULE, "key-a") is None)
        store.add(RULE, "one", "key-a", SAMPLE)
        added = store.add(RULE, "one", "key-a", SAMPLE)
        expect("each add() adds its quantity and its sample", added.value == 2.0 and len(added.samples) == 2)
        expect("the store reads the record of the additions", store.read(RULE, "key-a") == added)
        expect("a different key has no evidence", store.read(RULE, "key-b") is None)
        expect("a different rule with the same key has no evidence", store.read("other-passes", "key-a") is None)
        kept = store.keep_failure(RULE, "one", "key-a", FAILURE)
        later = store.keep_failure(RULE, "one", "key-a", evidence_store.Failure("later", "a second failure", ()))
        expect(
            "the first failure of a key stays, and a later failure changes nothing",
            kept.failure == later.failure == FAILURE,
        )
        after = store.add(RULE, "one", "key-a", SAMPLE)
        expect(
            "a pass after a failure adds evidence, and the failure stays",
            after.value == 3.0 and after.failure == FAILURE,
        )
        for _ in range(evidence_store.SAMPLE_LIMIT + 5):
            last = store.add(RULE, "many", "key-many", SAMPLE)
        expect(
            "a record keeps the last SAMPLE_LIMIT samples, and its value keeps each quantity",
            len(last.samples) == evidence_store.SAMPLE_LIMIT and last.value == evidence_store.SAMPLE_LIMIT + 5,
        )
        path = store.entry(RULE, "key-a")
        if path is not None:
            path.write_text("{damaged", encoding="utf-8")
        expect("a damaged entry gives no record", store.read(RULE, "key-a") is None)
        expect(
            "records() gives the record of each key that the store holds",
            set(store.records({RULE: ["key-many", "key-a", "key-none"]})) == {(RULE, "key-many")},
        )


def off_cases(expect: testing.Expect) -> None:
    """Hold the store with the caches off: it reads nothing and writes nothing (THE STORE)."""
    with testing.environment_variable(root_rule.ROOT_VARIABLE, root_rule.OFF):
        store = evidence_store.EvidenceStore()
        record = store.add(RULE, "one", "key-a", SAMPLE)
        expect(
            "with the caches off, an addition gives its record and the store keeps nothing",
            store.directory is None and record.value == 1.0 and store.read(RULE, "key-a") is None,
        )
    expect("the limit of the store is a row of LIMITS", evidence_store.STORE_NAME in cache.LIMITS)


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(__name__.removeprefix("assay."), argv, (format_cases, store_cases, off_cases))
