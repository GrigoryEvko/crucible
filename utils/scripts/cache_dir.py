#!/usr/bin/env python3
"""The directory of the content-keyed caches that the guards share, and the bound on each cache.

THE ROOT
    Each cache is a subdirectory of one root.  The root is $CRUCIBLE_CACHE_DIR
    when it is set, else $XDG_CACHE_HOME/crucible, else ~/.cache/crucible.
    The value "off" turns every cache off.  The root is outside each checkout
    and each build directory, so every guard, every build directory and every
    work tree uses what one of them calculated.  The result store of the
    negative fixtures, in test/neg_compile_store.py, is the cache "neg".

THE TOOLS
    The pinned tools (the tree-sitter kit and ast-grep) live in the tools
    subdirectory of the default root, and utils/scripts/tools_root.sh owns
    them.  $CRUCIBLE_CACHE_DIR does not move them, because a self-test points
    it at a scratch directory and "off" turns each cache off.  No eviction of
    this module reads that subdirectory.

THE LIMITS
    LIMITS gives the size limit of each subdirectory of the root, in
    allocated bytes.  It is the one place that declares a limit.  The
    eviction of each cache reads its row, and utils/scripts/check-cache-size.py
    reports a subdirectory over its row.  A size in this module is the space
    that a file takes on the disk (st_blocks), so a lock or a small file
    counts with its block.

THE ENTRIES
    An entry is a file whose name is the SHA-256 of each input that its
    contents depend on.  A changed input gives a different name, so an entry
    is never stale.  A write goes to a temporary file in the directory of the
    entry and then renames it.  A reader in another process sees all of the
    entry or none of it.  The name of a temporary file starts with a dot.

THE BOUND
    The mtime of an entry is the time of its last use.  A run that uses an
    entry touches it when the mtime is older than TOUCH_INTERVAL, so a warm run
    writes almost nothing.  An entry with no use for its age limit goes first.
    Then the entries with the oldest use go until the cache is under EVICT_TO
    of its size limit.  A temporary file or an empty directory with no change
    for STALE_AFTER goes too: a writer that stopped left it.

THE TURN
    One process at a time evicts a cache: the process that gets the eviction
    lock at once.  The turn is due when the last eviction is older than
    EVICT_INTERVAL, or when a sample of the cache is over its size limit.  A
    process takes that sample at most one time for each PROBE_INTERVAL.  So a
    cache that grows fast gets its turn within PROBE_INTERVAL, and a cache
    that grows slowly gets one turn each day for its age limit.

THE SAMPLE
    estimated_bytes() gives the size of a directory with no walk of each file.
    A fan-out directory is a directory whose subdirectories are named with
    two hexadecimal digits, as the first two digits of a SHA-256 name.  The
    names divide the entries evenly between those subdirectories.  So a
    sample of SAMPLE_FANS of them, scaled, gives the size of all of them.
    When the sample shows FULL_WALK files or fewer, the function walks each
    file, because a few large files in a small cache make a sample noisy.

Complexity: an eviction lists each entry one time, O(n log n) for n entries
because of the sort.  A sample stats about n * SAMPLE_FANS / 256 files.  A
lookup is one open of one file.

The driver of each negative fixture imports this module on each run, so the
module imports at load time only what cache_root() uses.

python3 cache_dir.py --evict NAME holds the cache NAME under its row of
LIMITS, when its turn is due.  A shell guard uses it for a cache whose
entries are files one level below a directory of the cache.
"""

from __future__ import annotations

import contextlib
import fcntl
import os
import sys
import time
from collections.abc import Iterator
from pathlib import Path

# The environment variable that moves the root, or turns each cache off with "off".
ROOT_VARIABLE = "CRUCIBLE_CACHE_DIR"
# A run touches an entry that it uses when the last touch is older than this.
TOUCH_INTERVAL = 3600.0
# The time between two evictions of a cache whose sample stays under its limit.
EVICT_INTERVAL = 86400.0
# The time between two samples of the size of one cache.
PROBE_INTERVAL = 120.0
# The age limit of an entry with no use.
MAX_AGE = 14 * 86400.0
# A temporary file or an empty directory with no change for this period is the remains of a writer that stopped.
STALE_AFTER = 3600.0
# An eviction over the size limit removes entries until the cache is under this part of the limit.
EVICT_TO = 0.75
# The fan-out subdirectories that one sample reads in each fan-out directory.
SAMPLE_FANS = 16
# A sample that shows this many files or fewer gives way to a walk of each file.
FULL_WALK = 60_000
# The size limit of each subdirectory of the root, in allocated bytes (THE LIMITS).
LIMITS: dict[str, int] = {
    "preprocessed": 4 << 30,
    "tsast": 2 << 30,
    "neg": 1 << 30,
    # The pinned toolchain and the checkouts of the proofs are about 1.1 GB of it.
    "session_oracle": 2 << 30,
    "atom-roster": 512 << 20,
    "clang-format": 64 << 20,
    "padded-list": 16 << 20,
    "hwledger": 16 << 20,
    "tools": 512 << 20,
}


class Entry:
    """One file of a cache, with its allocated size and the time of its last use."""

    __slots__ = ("path", "size", "used")

    def __init__(self, path: Path, size: int, used: float) -> None:
        """Hold the path of the file, its allocated size in bytes and the time of its last use."""
        self.path = path
        self.size = size
        self.used = used


def base_root() -> Path | None:
    """Return the root of the caches, or None when $CRUCIBLE_CACHE_DIR is "off".  The function makes no directory."""
    chosen = os.environ.get(ROOT_VARIABLE, "")
    if chosen == "off":
        return None
    if chosen:
        return Path(chosen)
    home = os.environ.get("XDG_CACHE_HOME", "")
    return (Path(home) if home else Path.home() / ".cache") / "crucible"


def cache_root(name: str) -> Path | None:
    """Return the directory of one named cache, and make it, or return None when the caches are off.

    Args:
        name: The name of the cache, one path segment

    Returns:
        The directory, or None when $CRUCIBLE_CACHE_DIR is "off"
    """
    base = base_root()
    if base is None:
        return None
    directory = base / name
    directory.mkdir(parents=True, exist_ok=True)
    return directory


@contextlib.contextmanager
def scratch_root() -> Iterator[Path]:
    """Move the root of the caches to a scratch directory for the block, so a self-test starts cold and leaves nothing.

    Yields:
        The scratch root
    """
    import tempfile

    saved = os.environ.get(ROOT_VARIABLE)
    with tempfile.TemporaryDirectory(prefix="crucible-cache-") as scratch:
        os.environ[ROOT_VARIABLE] = scratch
        try:
            yield Path(scratch)
        finally:
            if saved is None:
                os.environ.pop(ROOT_VARIABLE, None)
            else:
                os.environ[ROOT_VARIABLE] = saved


def write_atomic(target: Path, data: bytes) -> None:
    """Write the bytes of one entry, so that a reader sees all of them or none.

    An eviction removes an empty directory with no change for STALE_AFTER.
    When it removes the directory between the mkdir and the write, the write
    makes the directory again, at most three times.

    Args:
        target: The path of the entry
        data: The contents
    """
    import threading

    staging = target.with_name(f".{target.name}.{os.getpid()}.{threading.get_ident()}.tmp")
    for attempt in range(3):
        target.parent.mkdir(parents=True, exist_ok=True)
        try:
            staging.write_bytes(data)
            break
        except FileNotFoundError:
            if attempt == 2:
                raise
    os.replace(staging, target)


def mark_used(path: Path, used: float, now: float | None = None) -> bool:
    """Touch an entry that a run used, when its last touch is older than TOUCH_INTERVAL.

    Args:
        path: The entry
        used: Its mtime, as the caller read it
        now: The time of the use, or None for the clock

    Returns:
        Whether the function touched the entry
    """
    moment = time.time() if now is None else now
    if moment - used <= TOUCH_INTERVAL:
        return False
    try:
        os.utime(path, (moment, moment))
    except OSError:
        return False
    return True


def allocated(info: os.stat_result) -> int:
    """Return the bytes that a file takes on the disk, from its stat fields."""
    return info.st_blocks * 512


def entries(directory: Path, suffix: str = "") -> list[Entry]:
    """Return each entry of a cache that stores its entries one level below fan-out directories.

    A temporary file of a write in progress is not an entry.

    Args:
        directory: The directory of the fan-out directories
        suffix: Keep only the names that end with this suffix

    Returns:
        The entries, in no particular order
    """
    found: list[Entry] = []
    with contextlib.suppress(FileNotFoundError):
        for fan in os.scandir(directory):
            if not fan.is_dir(follow_symlinks=False):
                continue
            with contextlib.suppress(FileNotFoundError):
                for item in os.scandir(fan.path):
                    if item.name.startswith(".") or not item.name.endswith(suffix):
                        continue
                    with contextlib.suppress(FileNotFoundError):
                        info = item.stat(follow_symlinks=False)
                        found.append(Entry(Path(item.path), allocated(info), info.st_mtime))
    return found


def victims(candidates: list[Entry], max_bytes: int, max_age: float = MAX_AGE,
            now: float | None = None) -> list[Entry]:
    """Return the entries that an eviction removes: each one past the age limit, then the oldest past the size limit.

    Over the size limit, the oldest entries go until the rest is under
    EVICT_TO of the limit, so the next eviction is not due at once.

    Complexity: O(n log n) for n entries.

    Args:
        candidates: Every entry of the cache
        max_bytes: The size limit of the cache
        max_age: The age limit of an entry with no use
        now: The time of the eviction, or None for the clock

    Returns:
        The entries to remove
    """
    moment = time.time() if now is None else now
    ordered = sorted(candidates, key=lambda entry: entry.used)
    chosen = [entry for entry in ordered if moment - entry.used > max_age]
    kept = ordered[len(chosen):]
    total = sum(entry.size for entry in kept)
    if total <= max_bytes:
        return chosen
    goal = EVICT_TO * max_bytes
    for entry in kept:
        if total <= goal:
            break
        chosen.append(entry)
        total -= entry.size
    return chosen


def remove(chosen: list[Entry]) -> int:
    """Remove entries, and return the number of bytes that the removal freed."""
    freed = 0
    for entry in chosen:
        with contextlib.suppress(FileNotFoundError):
            entry.path.unlink()
            freed += entry.size
    return freed


def remove_stale(directory: Path, now: float | None = None) -> int:
    """Remove each temporary file and each empty subdirectory one level below a cache that had no change for STALE_AFTER.

    A writer that stopped leaves its temporary file, and an eviction leaves
    an empty directory.  A change in the last STALE_AFTER shows a writer that
    can still use the file or the directory.  write_atomic() makes a removed
    directory again.

    Args:
        directory: The directory of the fan-out directories of a cache
        now: The time of the removal, or None for the clock

    Returns:
        The number of files and directories that the function removed
    """
    expired = (time.time() if now is None else now) - STALE_AFTER
    removed = 0
    with contextlib.suppress(FileNotFoundError):
        for fan in os.scandir(directory):
            if not fan.is_dir(follow_symlinks=False):
                continue
            is_empty = True
            with contextlib.suppress(FileNotFoundError):
                for item in os.scandir(fan.path):
                    is_empty = False
                    if not item.name.startswith("."):
                        continue
                    with contextlib.suppress(FileNotFoundError):
                        if item.stat(follow_symlinks=False).st_mtime < expired:
                            os.unlink(item.path)
                            removed += 1
            with contextlib.suppress(OSError):
                if is_empty and fan.stat(follow_symlinks=False).st_mtime < expired:
                    os.rmdir(fan.path)
                    removed += 1
    return removed


def _is_fan(name: str) -> bool:
    """Return whether a directory name is two lowercase hexadecimal digits, the name of a fan-out directory."""
    return len(name) == 2 and all(character in "0123456789abcdef" for character in name)


def _walk(directory: str, sample: int | None, rng) -> tuple[float, float]:
    """Return the estimated allocated bytes and files under a directory, with a sample of each fan-out level or none.

    Args:
        directory: The directory
        sample: The fan-out subdirectories to read in each fan-out directory, or None for each one
        rng: The random generator that chooses the sample

    Returns:
        (bytes, files)
    """
    size = 0.0
    files = 0.0
    try:
        with os.scandir(directory) as listing:
            items = list(listing)
    except OSError:
        return 0.0, 0.0
    fans: list[os.DirEntry] = []
    others: list[os.DirEntry] = []
    for item in items:
        try:
            if item.is_dir(follow_symlinks=False):
                (fans if _is_fan(item.name) else others).append(item)
                continue
            size += allocated(item.stat(follow_symlinks=False))
            files += 1
        except OSError:
            continue
    scale = 1.0
    if sample is not None and len(fans) > sample:
        scale = len(fans) / sample
        fans = rng.sample(fans, sample)
    for item in fans:
        part_size, part_files = _walk(item.path, sample, rng)
        size += scale * part_size
        files += scale * part_files
    for item in others:
        part_size, part_files = _walk(item.path, sample, rng)
        size += part_size
        files += part_files
    return size, files


def estimated_bytes(directory: Path, rng=None, full_walk: int = FULL_WALK) -> int:
    """Return the allocated bytes under a directory, from a sample of its fan-out directories (THE SAMPLE).

    Complexity: about n * SAMPLE_FANS / 256 stat calls for n files under fan-out
    directories, and n calls when the sample shows full_walk files or fewer.

    Args:
        directory: The directory
        rng: The random generator of the sample, or None for a new one
        full_walk: The file count of the sample at or under which the function walks each file

    Returns:
        The estimated bytes.  A missing directory has 0
    """
    import random

    chooser = random.Random() if rng is None else rng
    size, files = _walk(str(directory), SAMPLE_FANS, chooser)
    if files <= full_walk:
        size, _files = _walk(str(directory), None, chooser)
    return int(size)


@contextlib.contextmanager
def eviction_turn(directory: Path, interval: float = EVICT_INTERVAL, max_bytes: int | None = None,
                  stamp_name: str = "evicted") -> Iterator[bool]:
    """Yield True when this process holds the eviction turn of a cache, else False (THE TURN).

    The turn is due when the stamp is older than the interval, or when
    max_bytes is given and a sample of the directory is over it.  A process
    samples only when the last sample of the cache is older than
    PROBE_INTERVAL.  The process gets the turn only when no other process
    holds the eviction lock, and it does not wait for that lock.  It touches
    the stamp when it gets the turn.

    Args:
        directory: The directory of the cache
        interval: The time between two evictions of a cache under its limit
        max_bytes: The size limit of the cache, or None for no sample
        stamp_name: The name of the stamp of the last eviction in the directory
    """
    stamp = directory / stamp_name
    now = time.time()
    try:
        is_due = now - stamp.stat().st_mtime >= interval
    except FileNotFoundError:
        is_due = True
    if not is_due and max_bytes is not None:
        probed = directory / f"{stamp_name}.probed"
        try:
            is_probe_due = now - probed.stat().st_mtime >= PROBE_INTERVAL
        except FileNotFoundError:
            is_probe_due = True
        if is_probe_due:
            probed.touch()
            is_due = estimated_bytes(directory) > max_bytes
    if not is_due:
        yield False
        return
    with open(directory / "evict.lock", "a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            has_turn = True
        except BlockingIOError:
            has_turn = False
        if has_turn:
            stamp.touch()
        yield has_turn


def hold_bound(directory: Path, max_bytes: int, max_age: float = MAX_AGE) -> bool:
    """Hold a cache whose entries are files one level below its subdirectories under its limits, when its turn is due.

    An entry is read whole, so its removal leaves no reader with a part of it.

    Args:
        directory: The directory of the cache
        max_bytes: The size limit of the cache
        max_age: The age limit of an entry with no use

    Returns:
        Whether this process took the turn
    """
    with eviction_turn(directory, max_bytes=max_bytes) as has_turn:
        if has_turn:
            remove(victims(entries(directory), max_bytes, max_age))
            remove_stale(directory)
        return has_turn


def evict_main(name: str) -> int:
    """Hold the cache NAME under its row of LIMITS when its turn is due, for a shell guard.

    Returns:
        0, or 2 when LIMITS has no row for the name
    """
    if name not in LIMITS:
        print(f"cache_dir.py --evict: the cache {name!r} has no row in LIMITS.  Add one to utils/scripts/cache_dir.py.",
              file=sys.stderr)
        return 2
    directory = cache_root(name)
    if directory is not None:
        hold_bound(directory, LIMITS[name])
    return 0


def self_test() -> int:
    """Do a test of the sample, the turn, the bound and the command line on scratch caches.

    Returns:
        0 when every case holds, else 2
    """
    import random
    import subprocess

    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        """Record one case."""
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    def exact(directory: Path) -> int:
        """Return the allocated bytes of each file under a directory, from a walk of each file."""
        return sum(allocated(path.lstat()) for path in directory.rglob("*") if path.is_file())

    rng = random.Random(42)
    with scratch_root() as root:
        fanned = root / "fanned"
        for fan in range(256):
            for index in range(1 + rng.randrange(8)):
                path = fanned / f"{fan:02x}" / f"{index:064x}"
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(bytes(4096 * (1 + rng.randrange(4))))
        total = exact(fanned)
        sampled = estimated_bytes(fanned, random.Random(7), full_walk=0)
        expect("a sample of 16 of 256 fan-out directories gives the size within 25 percent",
               abs(sampled - total) <= total // 4 and sampled != total)
        expect("a sample that shows few files gives way to a walk of each file", estimated_bytes(fanned) == total)
        expect("a missing directory has no size", estimated_bytes(root / "missing") == 0)

        stamp = fanned / "evicted"
        stamp.touch()
        probed = fanned / "evicted.probed"
        probed.touch()
        with eviction_turn(fanned, max_bytes=total // 2) as has_turn:
            expect("a recent turn and a recent sample give no turn, also over the limit", not has_turn)
        stale = time.time() - 2 * PROBE_INTERVAL
        os.utime(probed, (stale, stale))
        with eviction_turn(fanned, max_bytes=2 * total) as has_turn:
            expect("a due sample under the limit gives no turn", not has_turn)
        expect("a sample touches its stamp", time.time() - probed.stat().st_mtime < 60)
        os.utime(probed, (stale, stale))
        with eviction_turn(fanned, max_bytes=total // 2) as has_turn:
            expect("a due sample over the limit gives the turn, and the turn touches the stamp",
                   has_turn and time.time() - stamp.stat().st_mtime < 60)

        everything = entries(fanned)
        old = time.time() - 86400
        for offset, entry in enumerate(everything):
            os.utime(entry.path, (old + offset, old + offset))
        newest = max(everything, key=lambda entry: entry.path.stat().st_mtime).path
        dead = fanned / "00" / ".writer.1.2.tmp"
        dead.write_bytes(b"x")
        os.utime(dead, (old, old))
        live = fanned / "01" / ".writer.3.4.tmp"
        live.write_bytes(b"x")
        empty = fanned / "zz"
        empty.mkdir()
        os.utime(empty, (old, old))
        os.utime(stamp, (old, old))
        expect("hold_bound takes a due turn", hold_bound(fanned, total // 2))
        kept = sum(entry.size for entry in entries(fanned))
        expect("hold_bound leaves the entries under EVICT_TO of the limit, and keeps the entry of the newest use",
               kept <= EVICT_TO * (total // 2) and newest.exists())
        expect("hold_bound removes an old temporary file and an old empty directory, and keeps a new temporary file",
               not dead.exists() and not empty.exists() and live.exists())
        expect("write_atomic makes the directory of an entry", write_atomic(fanned / "new" / "entry", b"y") is None
               and (fanned / "new" / "entry").read_bytes() == b"y")

        script = str(Path(__file__).resolve())
        refused = subprocess.run([sys.executable, script, "--evict", "no-such-cache"], capture_output=True, text=True)
        expect("--evict refuses a cache with no row of LIMITS", refused.returncode == 2 and "LIMITS" in refused.stderr)
        roster = root / "atom-roster" / ("a" * 64)
        roster.mkdir(parents=True)
        (roster / "variant.log").write_bytes(bytes(8192))
        os.utime(roster / "variant.log", (time.time() - 2 * MAX_AGE, time.time() - 2 * MAX_AGE))
        done = subprocess.run([sys.executable, script, "--evict", "atom-roster"], capture_output=True, text=True,
                              env={**os.environ, ROOT_VARIABLE: str(root)})
        expect("--evict holds a cache of LIMITS under its limits", done.returncode == 0
               and not (roster / "variant.log").exists())
    expect("each row of LIMITS is a positive size", all(limit > 0 for limit in LIMITS.values()))
    saved = os.environ.get(ROOT_VARIABLE)
    os.environ[ROOT_VARIABLE] = "off"
    try:
        expect("with the caches off, no root and no cache", base_root() is None and cache_root("x") is None)
    finally:
        if saved is None:
            os.environ.pop(ROOT_VARIABLE, None)
        else:
            os.environ[ROOT_VARIABLE] = saved
    if failures:
        print(f"cache_dir --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("cache_dir --self-test: every case holds.")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    if len(sys.argv) == 3 and sys.argv[1] == "--evict":
        sys.exit(evict_main(sys.argv[2]))
    print("usage: cache_dir.py --self-test | --evict NAME", file=sys.stderr)
    sys.exit(2)
