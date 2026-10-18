"""The test of the digest memo (assay.core.digests): the kept hashes, the settle period and the answers of a call.

`guard selftest tests.core.test_digests` runs the test.  Each case writes
one line, in a scratch directory.  Exit 0 when each case holds, else 1.
"""

import hashlib
import os
import tempfile
from pathlib import Path

from assay.core import digests, testing

# A settle period in the future: the memo then keeps the hash of a file that changed at the time of the case.
KEEP_NOW_NS = -1_000_000_000
# A hash that no file of a case has, which a kept entry can hold.
PLANTED = "f" * 64


def sha256(data: bytes) -> str:
    """Return the SHA-256 of `data` as the memo gives it."""
    return hashlib.sha256(data).hexdigest()


def kept_cases(expect: testing.Expect, scratch: Path) -> None:
    """Check that a kept hash comes back while the stat fields are the same, and goes when they change (THE MEMO)."""
    path = scratch / "kept.txt"
    path.write_bytes(b"first")
    first = digests.DigestMemo(settle_ns=KEEP_NOW_NS)
    expect("digest() gives the SHA-256 of the bytes of a file", first.digest(str(path)) == sha256(b"first"))
    entries = first.entries()
    entry = entries.get(str(path), [])
    expect(
        "entries() keeps the five stat fields and the hash of the file",
        entry == [*digests.stat_fields(os.stat(path)), sha256(b"first")],
    )
    planted = {str(path): [*entry[:5], PLANTED]}
    expect(
        "a kept entry with the same stat fields gives its hash with no read of the file",
        digests.DigestMemo(planted).digest(str(path)) == PLANTED,
    )
    path.write_bytes(b"second, longer")
    expect(
        "a kept entry whose stat fields changed gives the hash of the new bytes",
        digests.DigestMemo(planted).digest(str(path)) == sha256(b"second, longer"),
    )
    bad = {str(path): [*digests.stat_fields(os.stat(path)), 7]}
    expect(
        "an entry of another shape goes, and the memo reads the file",
        digests.DigestMemo(bad).digest(str(path)) == sha256(b"second, longer"),
    )


def settle_cases(expect: testing.Expect, scratch: Path) -> None:
    """Check that the memo keeps no hash of a file that changed in the settle period (THE MEMO)."""
    path = scratch / "fresh.txt"
    path.write_bytes(b"fresh")
    memo = digests.DigestMemo()
    expect("a file that changed now gets its hash", memo.digest(str(path)) == sha256(b"fresh"))
    expect("a file that changed in the settle period gets no kept entry", str(path) not in memo.entries())


def call_cases(expect: testing.Expect, scratch: Path) -> None:
    """Check the answers of one call, forget(), and the entries of a call (THE CALL AND THE KEPT HASHES)."""
    path = scratch / "call.txt"
    other = scratch / "other.txt"
    path.write_bytes(b"one")
    other.write_bytes(b"two")
    memo = digests.DigestMemo(settle_ns=KEEP_NOW_NS)
    before = memo.digest(str(path))
    path.write_bytes(b"one, changed")
    expect("an answer of a call stays until forget()", memo.digest(str(path)) == before)
    memo.forget()
    expect("after forget(), a changed file gives its new hash", memo.digest(str(path)) == sha256(b"one, changed"))
    kept = {**memo.entries(), **digests.DigestMemo(settle_ns=KEEP_NOW_NS).entries()}
    memo.digest(str(other))
    later = digests.DigestMemo({**kept, **memo.entries()}, settle_ns=KEEP_NOW_NS)
    later.digest(str(other))
    expect("entries() holds only the files that the call used", set(later.entries()) == {str(other)})
    expect("a missing path gives MISSING", memo.digest(str(scratch / "absent")) == digests.MISSING)
    expect("a directory gives DIRECTORY", memo.digest(str(scratch)) == digests.DIRECTORY)
    expect(
        "the memo keeps no entry for a missing path or a directory",
        not {str(scratch / "absent"), str(scratch)} & set(memo.entries()),
    )


def shape_cases(expect: testing.Expect) -> None:
    """Check is_entry() on the shape of a kept entry."""
    expect("five integers and a hash are an entry", digests.is_entry([1, 2, 3, 4, 5, PLANTED]))
    expect("a short list is no entry", not digests.is_entry([1, 2, 3, 4, PLANTED]))
    expect("a text stat field is no entry", not digests.is_entry([1, 2, "3", 4, 5, PLANTED]))
    expect("an integer hash is no entry", not digests.is_entry([1, 2, 3, 4, 5, 6]))


def memo_cases(expect: testing.Expect) -> None:
    """Run the cases that read files in a scratch directory."""
    with tempfile.TemporaryDirectory(prefix="test-digests-") as scratch:
        kept_cases(expect, Path(scratch))
        settle_cases(expect, Path(scratch))
        call_cases(expect, Path(scratch))


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (memo_cases, shape_cases))
