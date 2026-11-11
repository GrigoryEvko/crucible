"""digests — the SHA-256 of a file, kept with the stat fields of the file.  An unchanged file then costs one stat.

THE MEMO
    A DigestMemo gives the SHA-256 of the bytes of a file.  It keeps each
    hash with the stat fields of the file at the time of the hash
    (stat_fields()).  These fields are the device, the inode, the length,
    the change time of the bytes and the change time of the inode.  A
    subsequent call gives the kept hash while the five fields are the same,
    and it reads no byte of the file.

    The memo keeps a hash only for a file with no change in SETTLE_NS before
    the hash (assay.core.meter.has_settled()).  Each subsequent change of the
    file gets a change time of the inode after the kept one.  The kernel
    gives this sequence also with a clock of a coarse tick.  A change in the
    same tick as the hash then cannot hide behind the kept fields.  A file
    that changed in the settle period gets its hash, and the next call reads
    it again.

THE CALL AND THE KEPT HASHES
    The answers of one call stay until forget().  A caller that reads the
    same file before and after a step, such as a run of the tests, calls
    forget() between them.  Each answer after forget() then compares the
    stat fields again, and only a changed file costs a new hash.  entries()
    gives the kept hash of each file of the current call, for the next call
    of the caller.  The memo then holds only the files that a call used, and
    its dimension has a limit.

    A path that names no file gives MISSING, a directory gives DIRECTORY,
    and a file that cannot be read gives UNREADABLE and the reason.  The
    memo keeps no such answer between calls.

Complexity: one stat for each file of a call, and one read of each file that changed or has no kept hash.

`guard selftest tests.core.test_digests` does a test of the module.
"""

import hashlib
import os
import stat
from collections.abc import Mapping, Sequence

# A file with a change in this period before its hash gets no kept hash (THE MEMO).
SETTLE_NS = 50_000_000
# The answers for a path that names no regular file (THE CALL AND THE KEPT HASHES).
MISSING = "missing"
DIRECTORY = "directory"
UNREADABLE = "unreadable"
# The fields of a kept entry: the five stat fields, then the hash.
ENTRY_FIELDS = 6

type Entry = list[int | str]


def stat_fields(status: os.stat_result) -> list[int]:
    """Return the fields of a stat result that change when the file changes (THE MEMO)."""
    return [status.st_dev, status.st_ino, status.st_size, status.st_mtime_ns, status.st_ctime_ns]


def is_entry(value: object) -> bool:
    """Tell if a value has the shape of a kept entry: five integer stat fields, then a hash."""
    if not isinstance(value, list) or len(value) != ENTRY_FIELDS:
        return False
    return all(isinstance(item, int) for item in value[:5]) and isinstance(value[5], str)


class DigestMemo:
    """The SHA-256 of each file that a caller reads, with the kept hashes of an earlier call (THE MEMO)."""

    __slots__ = ("_answers", "_kept", "_settle_ns", "_used")

    def __init__(self, kept: Mapping[str, Sequence[object]] | None = None, settle_ns: int = SETTLE_NS) -> None:
        """Start with the kept entries of an earlier call (entries()), or with none.  An entry of a different shape goes.

        `settle_ns` is the settle period of THE MEMO.  A test gives a period that lets it keep a new file.
        """
        self._settle_ns = settle_ns
        self._kept: dict[str, Entry] = {}
        for path, value in (kept or {}).items():
            listed = list(value)
            if is_entry(listed):
                self._kept[path] = listed  # type: ignore[assignment]
        self._used: dict[str, Entry] = {}
        self._answers: dict[str, str] = {}

    def digest(self, path: str) -> str:
        """Return the SHA-256 of the bytes of a file, or MISSING, DIRECTORY or the UNREADABLE reason.

        Complexity: one stat on the first call for a path in a call.  A file with no kept hash of the same stat
        fields also costs one read.
        """
        found = self._answers.get(path)
        if found is None:
            found = self._answers[path] = self._read(path)
        return found

    def _read(self, path: str) -> str:
        """Return the answer for a path in a new call, from a kept entry when its stat fields are the same.

        The function imports the meter of the clock here.  The fixture driver then imports this module for
        stat_fields() with no meter.
        """
        from assay.core import meter

        try:
            status = os.stat(path)
        except FileNotFoundError, NotADirectoryError:
            return MISSING
        except OSError as error:
            return f"{UNREADABLE}: {error.strerror}"
        if stat.S_ISDIR(status.st_mode):
            return DIRECTORY
        fields = stat_fields(status)
        kept = self._used.get(path) or self._kept.get(path)
        if kept is not None and kept[:5] == fields:
            self._used[path] = kept
            return str(kept[5])
        try:
            with open(path, "rb") as handle:
                digest = hashlib.file_digest(handle, "sha256").hexdigest()
            after = os.stat(path)
        except OSError as error:
            return f"{UNREADABLE}: {error.strerror}"
        if stat_fields(after) == fields and meter.has_settled(after.st_ctime_ns, self._settle_ns):
            self._used[path] = [*fields, digest]
        return digest

    def forget(self) -> None:
        """Drop the answers of this call.  The kept hashes stay, and the next answer for a path compares its stat."""
        self._answers.clear()

    def entries(self) -> dict[str, Entry]:
        """Return the kept entry of each file of this call, for the next call (THE CALL AND THE KEPT HASHES)."""
        return dict(self._used)
