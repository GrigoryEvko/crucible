"""store — the evidence of each sampled check at each key, in one store that each work tree shares.

THE STORE
    The store is the cache STORE_NAME in the root of the caches
    (assay.core.cache).  Each build directory and each work tree of the host
    reads and adds to it, as they share the store test-results of `loop
    test` (assay.loop.affected, THE SHARED STORE).  The rules of the key are
    the rules of that store: a key holds no path of the source root or of a
    build directory (assay.loop.evidence.items, THE KEYS).

    An entry is the record of one rule and one key.  NAME is the SHA-256 of
    FORMAT, the rule and the key.  The file of the entry is NAME in the
    directory of the first two digits of NAME.  A new key then gets a new
    entry.  The evidence of a key does not apply to a different key.  With
    the caches off, the store holds nothing and writes nothing.

THE RECORD
    A record holds the evidence of one item at one key:
      value    the evidence that the runs added, in the unit of the rule:
               passes, CPU seconds, valid measurements or runs
      samples  one sample for each unit of work that added evidence: its
               time, its quantity, its wall time and its facts.  The record
               keeps the last SAMPLE_LIMIT samples, and the value keeps the
               quantity of each sample
      failure  the first failure at the key, or none
    The record is one JSON object.  record_of() refuses an object of a
    different shape, and the store then reads no record for the key.

THE FAILURE
    A failure is evidence too.  A race that one repetition saw, a crash of a
    search and a mutant that no test kills stay red until the key changes.
    keep_failure() keeps the first failure of a key.  A subsequent failure or
    a subsequent pass does not change it.  The failure holds the command that
    runs it again and its facts, such as the seed, the input or the
    repetition.

THE WRITE
    add() and keep_failure() read, change and write one record while they
    hold the lock UPDATE_LOCK of the store.  The sample of one runner then
    stays when a different runner adds to the same record.  A write goes
    through a temporary file and a rename (assay.core.cache, THE ENTRIES).  After each run, `loop evidence run` keeps the store in its row
    of LIMITS of assay.core.cache.  An entry with no use for the age limit of
    that module goes, and its key then has no evidence.

`guard selftest tests.loop.evidence.test_store` does a test of the module.
"""

import contextlib
import fcntl
import hashlib
import json
from collections.abc import Iterator, Mapping, Sequence
from dataclasses import dataclass, replace
from pathlib import Path

from assay.core import cache
from assay.loop import history as loop_history

# The name of the store in the root of the caches and in LIMITS of assay.core.cache, and the format of a record.
STORE_NAME = "evidence"
FORMAT = "crucible-evidence 1"
# The samples that a record keeps (THE RECORD), and the lock of a change of a record (THE WRITE).
SAMPLE_LIMIT = 256
UPDATE_LOCK = "update.lock"


@dataclass(frozen=True, slots=True)
class Sample:
    """One unit of work that added evidence: its time, its quantity in the unit of the rule, its wall time and its facts."""

    at: str
    quantity: float
    wall_s: float
    facts: tuple[tuple[str, str], ...] = ()


@dataclass(frozen=True, slots=True)
class Failure:
    """The first failure at a key: its time, its summary, the command that runs it again and its facts."""

    at: str
    summary: str
    rerun: tuple[str, ...]
    facts: tuple[tuple[str, str], ...] = ()


@dataclass(frozen=True, slots=True)
class Record:
    """The evidence of one item at one key (THE RECORD)."""

    rule: str
    item: str
    key: str
    value: float = 0.0
    samples: tuple[Sample, ...] = ()
    failure: Failure | None = None

    @property
    def unit_wall_s(self) -> float | None:
        """The wall time of one unit of evidence in the kept samples, or None when the samples hold no quantity."""
        quantity = sum(sample.quantity for sample in self.samples)
        return None if quantity <= 0 else sum(sample.wall_s for sample in self.samples) / quantity


def now() -> str:
    """Return the time of this moment in ISO 8601, in UTC, to the second (assay.loop.history)."""
    return loop_history.iso_now()


def facts_json(facts: Sequence[tuple[str, str]]) -> list[list[str]]:
    """Return the facts of a sample or a failure as JSON pairs."""
    return [[name, value] for name, value in facts]


def record_json(record: Record) -> str:
    """Return the JSON text of a record (THE RECORD)."""
    failure = record.failure
    return json.dumps(
        {
            "format": FORMAT,
            "rule": record.rule,
            "item": record.item,
            "key": record.key,
            "value": record.value,
            "samples": [
                {"at": item.at, "quantity": item.quantity, "wall_s": item.wall_s, "facts": facts_json(item.facts)}
                for item in record.samples
            ],
            "failure": None
            if failure is None
            else {
                "at": failure.at,
                "summary": failure.summary,
                "rerun": list(failure.rerun),
                "facts": facts_json(failure.facts),
            },
        },
        sort_keys=True,
    )


def facts_of(value: object) -> tuple[tuple[str, str], ...] | None:
    """Return the facts of a JSON list of pairs of texts, or None when the value has a different shape."""
    if not isinstance(value, list):
        return None
    pairs = [item for item in value if isinstance(item, list) and len(item) == 2]
    if len(pairs) != len(value) or not all(isinstance(part, str) for pair in pairs for part in pair):
        return None
    return tuple((str(pair[0]), str(pair[1])) for pair in pairs)


def number_of(value: object) -> float | None:
    """Return a JSON number that is not a boolean as a float, or None."""
    return float(value) if isinstance(value, (int, float)) and not isinstance(value, bool) else None


def sample_of(value: object) -> Sample | None:
    """Return the sample of one JSON object of the field samples, or None when the object has a different shape."""
    if not isinstance(value, dict):
        return None
    at, quantity, wall_s = value.get("at"), number_of(value.get("quantity")), number_of(value.get("wall_s"))
    facts = facts_of(value.get("facts"))
    if not isinstance(at, str) or quantity is None or wall_s is None or facts is None:
        return None
    return Sample(at, quantity, wall_s, facts)


def failure_of(value: object) -> Failure | None:
    """Return the failure of the JSON object of the field failure, or None when the object has a different shape."""
    if not isinstance(value, dict):
        return None
    at, summary, rerun, facts = value.get("at"), value.get("summary"), value.get("rerun"), facts_of(value.get("facts"))
    if not isinstance(at, str) or not isinstance(summary, str) or facts is None:
        return None
    if not isinstance(rerun, list) or not all(isinstance(word, str) for word in rerun):
        return None
    return Failure(at, summary, tuple(str(word) for word in rerun), facts)


def record_of(text: str) -> Record | None:
    """Return the record of a JSON text, or None when the text does not have the shape of THE RECORD.

    Complexity: linear in the length of the text.
    """
    try:
        found = json.loads(text)
    except ValueError:
        return None
    if not isinstance(found, dict) or found.get("format") != FORMAT:
        return None
    rule, item, key, value = found.get("rule"), found.get("item"), found.get("key"), number_of(found.get("value"))
    raw_samples = found.get("samples")
    if not (isinstance(rule, str) and isinstance(item, str) and isinstance(key, str)) or value is None:
        return None
    samples = [sample_of(part) for part in raw_samples] if isinstance(raw_samples, list) else None
    if samples is None or any(sample is None for sample in samples):
        return None
    raw_failure = found.get("failure")
    failure = failure_of(raw_failure)
    if raw_failure is not None and failure is None:
        return None
    return Record(rule, item, key, value, tuple(sample for sample in samples if sample is not None), failure)


def entry_name(rule: str, key: str) -> str:
    """Return the name of the entry of a rule and a key (THE STORE)."""
    return hashlib.sha256(f"{FORMAT}\n{rule}\n{key}".encode()).hexdigest()


class EvidenceStore:
    """The evidence of each rule and key that each build directory and each work tree of the host share (THE STORE)."""

    def __init__(self, directory: Path | None = None) -> None:
        """Use a directory and make it, or find the directory of the store in the root of the caches and make it."""
        if directory is not None:
            directory.mkdir(parents=True, exist_ok=True)
        self.directory = directory if directory is not None else cache.cache_root(STORE_NAME)

    def entry(self, rule: str, key: str) -> Path | None:
        """Return the file of the entry of a rule and a key, or None when the caches are off."""
        if self.directory is None:
            return None
        name = entry_name(rule, key)
        return self.directory / name[:2] / name

    def read(self, rule: str, key: str) -> Record | None:
        """Return the record of a rule and a key, and touch its entry, or None when the store holds no record.

        Complexity: one open of one file.
        """
        path = self.entry(rule, key)
        if path is None:
            return None
        try:
            used = path.stat().st_mtime
            found = record_of(path.read_text(encoding="utf-8"))
        except OSError, UnicodeDecodeError:
            return None
        if found is None or found.rule != rule or found.key != key:
            return None
        cache.mark_used(path, used)
        return found

    def records(self, keys: Mapping[str, Sequence[str]]) -> dict[tuple[str, str], Record]:
        """Return the record of each rule and key that the store holds, by its rule and its key.

        Complexity: one open of one file for each key.
        """
        found: dict[tuple[str, str], Record] = {}
        for rule, listed in keys.items():
            for key in listed:
                record = self.read(rule, key)
                if record is not None:
                    found[(rule, key)] = record
        return found

    @contextlib.contextmanager
    def locked(self) -> Iterator[None]:
        """Hold the lock UPDATE_LOCK of the store for the block (THE WRITE).  With the caches off, hold nothing."""
        if self.directory is None:
            yield
            return
        with open(self.directory / UPDATE_LOCK, "a") as handle:
            fcntl.flock(handle, fcntl.LOCK_EX)
            yield

    def write(self, record: Record) -> None:
        """Write a record through a temporary file and a rename.  A failure of the write changes nothing."""
        path = self.entry(record.rule, record.key)
        if path is not None:
            with contextlib.suppress(OSError):
                cache.write_atomic(path, record_json(record).encode("utf-8"))

    def add(self, rule: str, item: str, key: str, sample: Sample) -> Record:
        """Add one sample to the record of a rule and a key, and return the record that holds the sample.

        The value gets the quantity of the sample, and the record keeps the last SAMPLE_LIMIT samples.  A kept failure
        stays (THE FAILURE).
        """
        with self.locked():
            earlier = self.read(rule, key) or Record(rule, item, key)
            record = replace(
                earlier,
                item=item,
                value=earlier.value + sample.quantity,
                samples=(*earlier.samples, sample)[-SAMPLE_LIMIT:],
            )
            self.write(record)
        return record

    def keep_failure(self, rule: str, item: str, key: str, failure: Failure) -> Record:
        """Keep the first failure of a rule and a key, and return the record (THE FAILURE).

        A record with a failure keeps that failure, and the new failure changes nothing.
        """
        with self.locked():
            earlier = self.read(rule, key) or Record(rule, item, key)
            if earlier.failure is not None:
                return earlier
            record = replace(earlier, item=item, failure=failure)
            self.write(record)
        return record
