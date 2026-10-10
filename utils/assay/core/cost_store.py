"""cost_store — the cost of each compile that ran the compiler, for a ccache hit of the same object.

THE STORE
    On a ccache hit the compiler does not run, and the step has no cost of
    its own (assay.core.record).  `loop step` (utils/assay/loop/step.py)
    writes the cost block of each compile through ccache that ran the
    compiler into this store.  A hit of the same object reads the block, and
    its record keeps the block as last_cost.  A rule that adds the cost of
    each unit, such as instruction-total, then reads the cost of the compile
    that made each object.  That compile can be in a different build
    directory or in a different work tree.

    The store is the cache STORE_NAME in the root of the caches
    (assay.core.cache_root).  It is off when the caches are off.  An entry is
    the file KEY in the directory of the first two digits of KEY.  It holds
    one line: the cost block, as assay.core.record writes it.  A write goes to
    a temporary file with a name that starts with a dot, and a rename gives
    the file its name.  A reader then sees all of an entry or none of it.

    A lookup touches an entry when its last touch is older than
    TOUCH_INTERVAL of assay.core.cache_root.  hold_bound() keeps the store in
    the byte limit of its row of LIMITS of assay.core.cache, and `guard
    check` takes that turn.  The entries with the oldest use go first.  The
    store has no age limit, because ccache keeps an object with no age
    limit.  A hit of an object with no entry has no count.

THE KEY
    A hit gives the object that ccache stored at the miss.  The key holds the
    SHA-256 of the bytes of the object, and a hit and its miss then give one
    digest.  The bytes alone do not identify one compile.  The tree compiles
    with -gno-record-gcc-switches (cmake/Ccache.cmake), and an object does
    not hold the options of its compile.  One source with two sets of options
    can give the same bytes and two different counts.  An example is the
    units of test/layer/admitted_flag_matrix.cpp, one unit for each flag set.
    The key then also holds each word of the command of the step.  These
    words are the ccache words with the identity of the compiler, the
    compiler and its options.

    A path of the build directory or of the source root is different in two
    build directories or two work trees.  ccache gives a hit across them,
    because it writes each path in its base_dir from the build directory.
    In each word of the key, BUILD_MARK replaces the build directory and
    ROOT_MARK replaces the source root (relocated()).  A path is such a root
    when no character follows it in the word, or when the character after it
    is in AFTER.  The text before it, from the last character of BEFORE or
    from the start of the word, must hold no "/".  A root can then follow an
    option such as -I or a separator such as "=".  A path that only holds the
    text of a root at its end is no root.

    Two compiles with one command and the same bytes have one key.  Their
    headers can then be different only in a part that the object does not
    show.  An example is a static_assert on a line that does not move.  The
    last of the two compiles writes the entry.  The difference of the two
    counts is the work of that part.

THE COST
    The key reads each byte of the object one time.  The module imports
    hashlib only in key_of(), and a link imports nothing of this module.  On
    2026-10-10, with -S, the import of hashlib took 2.4 ms.  A digest of
    10.24 MB, the largest object of the Debug build, took 5.6 ms.

`guard selftest tests.core.test_cost_store` does a test of the module.
"""

import os

from assay.core import cache_root

# The names are only for the types, and a step meter does not pay their imports.
TYPE_CHECKING = False
if TYPE_CHECKING:
    from collections.abc import Sequence

# The name of the store in the root of the caches, and its row of LIMITS of assay.core.cache.
STORE_NAME = "compile-cost"
# Changed when a key or an entry no longer means what it meant.
FORMAT = "crucible-compile-cost 1"
# The marks of the build directory and of the source root in a word of a key (THE KEY).
BUILD_MARK = "<build>"
ROOT_MARK = "<root>"
# The characters that can come before a root in a word, and after it (THE KEY).
BEFORE = frozenset("=:,")
AFTER = frozenset("/=:,")
# The bytes of one read of the object.
CHUNK = 1 << 20


def store_directory() -> str | None:
    """Return the directory of the store, or None when the caches are off.  The function makes no directory."""
    base = cache_root.base_root()
    return None if base is None else os.path.join(base, STORE_NAME)


def relocated(word: str, roots: Sequence[tuple[str, str]]) -> str:
    """Return a word of a command with each path of a root as the mark of the root (THE KEY).

    Args:
        word: The word
        roots: Each root path with its mark, the longest path first

    Complexity: O(w * w * r) for a word of w characters and r roots, because each match reads the text before it.
    The words of a command are short.
    """
    for path, mark in roots:
        pieces: list[str] = []
        start = 0
        position = word.find(path)
        while position >= 0:
            end = position + len(path)
            head = word[:position]
            token = head[max(head.rfind(character) for character in BEFORE) + 1 :]
            is_root = "/" not in token and (end == len(word) or word[end] in AFTER)
            if is_root:
                pieces += [word[start:position], mark]
                start = end
            position = word.find(path, end if is_root else position + 1)
        word = "".join(pieces) + word[start:]
    return word


def key_of(object_path: str, command: Sequence[str], build_dir: str | None, source_root: str) -> str | None:
    """Return the key of the compile that made an object, or None when the object cannot be read (THE KEY).

    Args:
        object_path: The object
        command: The command of the step, from the first word after the launcher
        build_dir: The build directory of the object, or None when the object is in none
        source_root: The source root of the build

    Complexity: linear in the bytes of the object and the length of the command.
    """
    import hashlib

    digest = hashlib.sha256()
    try:
        descriptor = os.open(object_path, os.O_RDONLY)
    except OSError:
        return None
    try:
        while chunk := os.read(descriptor, CHUNK):
            digest.update(chunk)
    except OSError:
        return None
    finally:
        os.close(descriptor)
    given = ((build_dir, BUILD_MARK), (source_root, ROOT_MARK))
    roots = sorted(
        ((os.path.normpath(path), mark) for path, mark in given if path and os.path.normpath(path) != os.sep),
        key=lambda item: -len(item[0]),
    )
    material = "\0".join((FORMAT, digest.hexdigest(), *(relocated(word, roots) for word in command)))
    return hashlib.sha256(material.encode("utf-8", "surrogateescape")).hexdigest()


def entry_path(directory: str, key: str) -> str:
    """Return the file of the entry of a key in the store directory."""
    return os.path.join(directory, key[:2], key)


def put(key: str, block: str) -> None:
    """Write the cost block of the compile of a key into the store, through a temporary file and a rename.

    The call writes nothing when the caches are off.

    Raises:
        OSError: If the entry cannot be written
    """
    directory = store_directory()
    if directory is None:
        return
    target = entry_path(directory, key)
    staging = os.path.join(os.path.dirname(target), f".{key}.{os.getpid()}.tmp")
    flags = os.O_WRONLY | os.O_CREAT | os.O_TRUNC
    try:
        descriptor = os.open(staging, flags, 0o666)
    except FileNotFoundError:
        os.makedirs(os.path.dirname(target), exist_ok=True)
        descriptor = os.open(staging, flags, 0o666)
    try:
        try:
            view = memoryview(f"{block}\n".encode())
            while view:
                view = view[os.write(descriptor, view) :]
        finally:
            os.close(descriptor)
        os.replace(staging, target)
    except BaseException:
        try:
            os.unlink(staging)
        except OSError:
            pass
        raise


def get(key: str, used_at: float) -> str | None:
    """Return the cost block of the compile of a key, or None when the store holds none.

    The lookup touches the entry when its last touch is older than
    TOUCH_INTERVAL before the time of the use (THE STORE).

    Args:
        key: The key
        used_at: The time of the use, in seconds since the epoch: the end of the step of the hit
    """
    directory = store_directory()
    if directory is None:
        return None
    path = entry_path(directory, key)
    try:
        with open(path, encoding="utf-8") as handle:
            block = handle.read().strip()
        touched = os.stat(path).st_mtime
    except OSError, UnicodeDecodeError:
        return None
    if used_at - touched > cache_root.TOUCH_INTERVAL:
        try:
            os.utime(path)
        except OSError:
            pass
    return block or None


def hold_bound() -> bool:
    """Keep the store in the byte limit of its row of LIMITS of assay.core.cache, at its turn (THE STORE).

    Returns:
        True when this process took the turn.  With the caches off, or with no store, False
    """
    from pathlib import Path

    from assay.core import cache

    directory = store_directory()
    if directory is None or not os.path.isdir(directory):
        return False
    return cache.hold_bound(Path(directory), cache.LIMITS[STORE_NAME], max_age=float("inf"))
