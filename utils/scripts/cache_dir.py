#!/usr/bin/env python3
"""The directory of the content-keyed caches that the guards share, and the bound on each cache.

THE ROOT
    Each cache is a subdirectory of one root.  The root is $CRUCIBLE_CACHE_DIR
    when it is set, else $XDG_CACHE_HOME/crucible, else ~/.cache/crucible.
    The value "off" turns every cache off.  The root is outside each checkout
    and each build directory, so every guard, every build directory and every
    work tree uses what one of them calculated.

THE TOOLS
    The pinned tools (the tree-sitter kit and ast-grep) live in the tools
    subdirectory of the default root, and utils/scripts/tools_root.sh owns
    them.  $CRUCIBLE_CACHE_DIR does not move them, because a self-test points
    it at a scratch directory and "off" turns each cache off.  No eviction of
    this module reads that subdirectory.

THE ENTRIES
    An entry is a file whose name is the SHA-256 of each input that its
    contents depend on.  A changed input gives a different name, so an entry
    is never stale.  A write goes to a temporary file in the directory of the
    entry and then renames it.  A reader in another process sees all of the
    entry or none of it.

THE BOUND
    The mtime of an entry is the time of its last use.  A run that uses an
    entry touches it when the mtime is older than TOUCH_INTERVAL, so a warm run
    writes almost nothing.  An entry with no use for its age limit goes first.
    Then the entries with the oldest use go until the cache is below its size
    limit.  One process for each EVICT_INTERVAL does this work, and a process
    that cannot get the eviction lock at once does not wait for it.

Complexity: an eviction lists each entry one time, O(n log n) for n entries
because of the sort.  A lookup is one open of one file.
"""

from __future__ import annotations

import contextlib
import fcntl
import os
import tempfile
import threading
import time
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path

# The environment variable that moves the root, or turns each cache off with "off".
ROOT_VARIABLE = "CRUCIBLE_CACHE_DIR"
# A run touches an entry that it uses when the last touch is older than this.
TOUCH_INTERVAL = 3600.0
# One eviction for each interval, for each cache.
EVICT_INTERVAL = 86400.0
# The age limit of an entry with no use.
MAX_AGE = 14 * 86400.0


@dataclass(frozen=True)
class Entry:
    """One file of a cache, with its size and the time of its last use."""

    path: Path
    size: int
    used: float


def cache_root(name: str) -> Path | None:
    """Return the directory of one named cache, and make it, or return None when the caches are off.

    Args:
        name: The name of the cache, one path segment

    Returns:
        The directory, or None when $CRUCIBLE_CACHE_DIR is "off"
    """
    chosen = os.environ.get(ROOT_VARIABLE, "")
    if chosen == "off":
        return None
    if chosen:
        base = Path(chosen)
    else:
        home = os.environ.get("XDG_CACHE_HOME", "")
        base = (Path(home) if home else Path.home() / ".cache") / "crucible"
    directory = base / name
    directory.mkdir(parents=True, exist_ok=True)
    return directory


@contextlib.contextmanager
def scratch_root() -> Iterator[Path]:
    """Move the root of the caches to a scratch directory for the block, so a self-test starts cold and leaves nothing.

    Yields:
        The scratch root
    """
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

    Args:
        target: The path of the entry
        data: The contents
    """
    target.parent.mkdir(parents=True, exist_ok=True)
    staging = target.with_name(f".{target.name}.{os.getpid()}.{threading.get_ident()}.tmp")
    staging.write_bytes(data)
    os.replace(staging, target)


def mark_used(path: Path, used: float, now: float | None = None) -> None:
    """Touch an entry that a run used, when its last touch is older than TOUCH_INTERVAL.

    Args:
        path: The entry
        used: Its mtime, as the caller read it
        now: The time of the use, or None for the clock
    """
    moment = time.time() if now is None else now
    if moment - used > TOUCH_INTERVAL:
        with contextlib.suppress(OSError):
            os.utime(path, (moment, moment))


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
                        found.append(Entry(Path(item.path), info.st_size, info.st_mtime))
    return found


def victims(candidates: list[Entry], max_bytes: int, max_age: float = MAX_AGE,
            now: float | None = None) -> list[Entry]:
    """Return the entries that an eviction removes: each one past the age limit, then the oldest past the size limit.

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
    for entry in kept:
        if total <= max_bytes:
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


@contextlib.contextmanager
def eviction_turn(directory: Path, interval: float = EVICT_INTERVAL) -> Iterator[bool]:
    """Yield True when this process holds the eviction turn of a cache, else False.

    The turn is due when the last eviction is older than the interval.  The
    process gets the turn only when no other process holds the eviction lock,
    and it does not wait for that lock.

    Args:
        directory: The directory of the cache
        interval: The time between two evictions
    """
    stamp = directory / "evicted"
    try:
        is_due = time.time() - stamp.stat().st_mtime >= interval
    except FileNotFoundError:
        is_due = True
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
