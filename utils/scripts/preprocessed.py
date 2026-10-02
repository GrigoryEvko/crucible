#!/usr/bin/env python3
"""One preprocessor run for each translation unit, shared by every guard, every build directory and every work tree.

check-proof-routes.py, check-start-lifetime.py, check-no-unchecked-access.py
and check-federation-admission.py read the output of the preprocessor over the
compile database.  Each guard gets that output from this module, so a
translation unit is preprocessed one time for all of them, and one time for
every build directory and every work tree that has the same unit.

THE STORE
    The store is the "preprocessed" cache of utils/scripts/cache_dir.py, by
    default ~/.cache/crucible/preprocessed.  The module runs the compiler of
    each database entry with -E and -MD.  It keeps only the part of the output
    that falls in files under the root.  It divides that part at each line
    marker into chunks.  A chunk is the text of one file from one line on, and
    it is stored one time under the SHA-256 of its text.  Many units include a
    header with the same macros, so the store holds that text one time.

THE KEY OF A UNIT
    A manifest holds the result of one unit.  Its name is a hash of these
    items:
      - The preprocessor command: the compile command with -E in place of its
        output and dependency-file flags, and with -fmacro-prefix-map, which
        makes each __FILE__ relative to the root
      - The directory of the command
      - The path, the size and the mtime of the compiler driver and of cc1plus
      - Each environment variable that changes what the preprocessor reads
    The root of the checkout in a path of the command and in the directory is
    replaced by a mark, so two work trees with the same unit share a name.  A
    -D value or another argument that holds the root is left as it is.

    A name can hold up to MAX_VARIANTS variants.  A variant lists each file
    that the unit read, from the -MD dependency file, with the SHA-256 of the
    file.  A variant is valid while each file it lists has the same contents,
    so a header that adds no line to the output still makes the variant stale
    when it changes.  Two work trees with different headers keep one variant
    each, and neither removes the other.

    The kept output of a variant can hold the root after all, for example
    through a -D value.  Then the variant goes under a second name that also
    hashes the root, and only the same root reads it.  So a variant under the
    shared name holds no text that depends on the root.

THE MANIFEST
    A manifest is the marshal encoding of (MANIFEST_TAG, variants).  A tag of
    another version, another marshal format or a damaged file counts as a
    miss.  A variant is a tuple of four items:
      - The files under the root that the unit read, by path relative to the
        root
      - The raw SHA-256 of each of those files, in one byte string
      - The name of a stored blob that lists the files outside the root that
        the unit read, by absolute path, with the raw SHA-256 of each.  Most
        units read the same system headers, so the store holds each list one
        time, and a run checks each list one time.
      - One record for each file under the root that the output holds, in
        the order of its first chunk: the path, the expansion key, the line
        of each chunk as unsigned 32-bit integers, and the raw SHA-256 of
        each chunk, each in one byte string.  The expansion key is the
        SHA-256 of the hexadecimal chunk names joined by newlines, so a guard
        reads it and does not calculate it.
    A variant is valid when each listed file has its recorded SHA-256, when
    its blob of outside files is valid in the same way, and when the store
    holds each of its chunks.  A run checks each distinct blob and each
    distinct record one time.

THE SHARDED FILL
    preprocessed.py --fill COMPILE_DB --shard K --of N reads or preprocesses
    each entry whose index is K modulo N.  ctest runs the N shards as setup
    tests of the guards that read the store, so each guard starts on a full
    store, and no one test pays for a cold store alone.  The fill is
    incremental per unit: a unit whose variant is valid costs one manifest
    read, and only a unit with a changed input runs the preprocessor.

EXPANSIONS AND RESULTS
    expansions() gives each distinct expansion of each file one time: the
    chunks of one file in one unit, in output order.  Two units that expand a
    file the same way give one expansion, so a guard reads a header that
    every unit includes one time and not one time for each unit.
    results() gives a guard a store of its results for each expansion, under
    a name that hashes the code of the guard and the parser.  A changed guard
    gets new names, so a result is never stale, and a warm run reads no text.
    text_results() gives a guard the same kind of results for the source files
    that it parses, under the SHA-256 of each file, with no compile database.
    map_batches() lets a guard calculate the results that the store does not
    hold in worker processes.

CONCURRENCY
    A cold run preprocesses its units in worker processes, at most `jobs` of
    them.  Two guards that run at the same time lock the manifest of a unit
    before they preprocess it, so the second one waits and then reads what
    the first one wrote.  The workers take the units in a random order and
    first pass over a unit that another process holds, so guards that start
    cold together divide the units between them.  Each write is atomic.
    Each process holds a shared lock on the store, and an eviction needs the
    exclusive lock, so an eviction never runs while a guard reads.

THE BOUND
    A file under units/ that is not a manifest of this version, or a lock
    with no manifest, goes first, at most GARBAGE_PER_TURN of them for each
    eviction.  A manifest and a result that no run used for
    cache_dir.MAX_AGE go next.  Then the oldest go until the store is under
    STORE_BYTES.  A chunk that no remaining manifest names goes with them.
    One process each day does this, when it can get the exclusive lock at
    once.  A process that gets the turn and not the lock, or that leaves
    garbage for the next turn, keeps the turn due, so the next run tries
    again.

FAILURE
    A unit that the preprocessor rejects gives a failure and no variant, so
    the next run tries it again.  A guard refuses its run on a failure,
    because a unit that it cannot read can hold a use.  A damaged manifest
    counts as a miss.  A damaged chunk raises DamagedStore and is removed, so
    the next run makes it again.

STAGED LAYER ROOTS
    A directory on the include path whose entries are all links into the
    include directory of the repository is a staged layer root of
    test/layer.  The pass replaces it with the include directory, because a
    layer fixture exists to fail under its root, and what it holds is still
    worth reading.  A directory that holds anything else is left alone.

WHAT THE KEY CANNOT SEE, STATED RATHER THAN IMPLIED
    - A new header that hides the header that a unit read, earlier on the
      include path.  The dependency file names only the files that the
      preprocessor opened.  This is the limit of the direct mode of ccache.
    - __DATE__, __TIME__ and __TIMESTAMP__.  The tree uses none of them.

Complexity: a cold run is linear in the total size of the preprocessed
output.  A warm run reads one manifest for each unit, one hash for each
distinct file that a unit read, and one check for each distinct blob and
each distinct record.
"""

from __future__ import annotations

import array
import contextlib
import fcntl
import hashlib
import json
import marshal
import multiprocessing
import os
import random
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import zlib
from collections.abc import Callable, Iterator
from concurrent.futures import ProcessPoolExecutor, ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

import cache_dir
from repo_root import REPO_ROOT

# Bumped when the store reads or writes the output or a manifest differently,
# so an older manifest is not reused.
STORE_VERSION = 3
# Bumped when a result of a guard no longer means what it meant.  It is apart
# from STORE_VERSION, because a new manifest format leaves each expansion key
# and each chunk text as it was.
RESULTS_VERSION = 2
# The name of the store in utils/scripts/cache_dir.py.
STORE_CACHE = "preprocessed"
# The size limit of the store.  One build directory of this tree takes about
# 300 MB.
STORE_BYTES = 4 << 30
# The variants that one manifest keeps, the most recent first.
MAX_VARIANTS = 8
# The environment variables that change what the preprocessor reads or what it writes.
ENVIRONMENT = ("CPATH", "C_INCLUDE_PATH", "CPLUS_INCLUDE_PATH", "OBJC_INCLUDE_PATH", "GCC_EXEC_PREFIX",
               "COMPILER_PATH", "SOURCE_DATE_EPOCH", "LANG", "LC_ALL", "LC_CTYPE")
# The flags whose value can be a path, attached to the flag.
PATH_FLAGS = ("-I", "-iquote", "-isystem", "-idirafter", "-iprefix", "-iwithprefix", "-iwithprefixbefore",
              "-include", "-imacros", "--sysroot=", "-B", "-fmacro-prefix-map=")
# The mark that stands for the root in the name of a manifest.
ROOT_MARK = "\u0000root\u0000"
# A cold run uses worker processes when at least this many units miss.
PARALLEL_MISSES = 4
# The suffix of a manifest file.
MANIFEST_SUFFIX = ".bin"
# The first item of each manifest.  A manifest with another tag is a miss.
MANIFEST_TAG = ("crucible-preprocessed", STORE_VERSION, marshal.version)
# The raw SHA-256 that a variant records for a file that does not exist.
MISSING = bytes(32)
# The array type code of a chunk line in a record: an unsigned 32-bit integer.
LINE_TYPE = "I"
# The files under units/ that one eviction removes as garbage, at most.
GARBAGE_PER_TURN = 50_000

# A line marker of the output: # line "file" flags, up to the end of its
# line.  It is matched only at the start of a line.
MARKER = re.compile(rb'# ([0-9]+) "((?:[^"\\]|\\.)*)"[^\n]*')
# A line marker after the newline that ends the line before it.  The literal
# prefix lets the regular expression engine find each candidate in C.
MARKER_AFTER_NEWLINE = re.compile(rb'\n' + MARKER.pattern)

# A record of one file of one unit: (path, expansion key, lines, chunk digests).
Record = tuple[str, str, bytes, bytes]
# A variant: (names under the root, their digests, outside blob, records).
Variant = tuple[tuple[str, ...], bytes, str, tuple[Record, ...]]


@dataclass(frozen=True)
class Chunk:
    """The text of one file under the root from one line on, as it appears in one unit."""

    path: str
    line: int
    digest: str


@dataclass
class Unit:
    """One compile-database entry after the preprocessed pass.

    records holds one record for each file under the root that the output
    holds, in the order of its first chunk.  inside names each file under the
    root that the unit read, from the -MD dependency file, so a header that
    adds no line to the output is still listed.
    """

    file: str
    records: tuple[Record, ...] = ()
    failure: str | None = None
    from_cache: bool = False
    inside: tuple[str, ...] = ()

    @property
    def dependencies(self) -> frozenset[str]:
        """Return each file under the root that the unit read."""
        return frozenset(self.inside)

    @property
    def chunks(self) -> list[Chunk]:
        """Return the chunks of the unit, grouped by file, in the order of the first chunk of each file."""
        return [chunk for record in self.records for chunk in record_chunks(record)]


@dataclass(frozen=True)
class Expansion:
    """One distinct expansion of one file: its chunks in output order.

    key hashes the digests of the chunks, so it names the text.  Two
    expansions with the same text and other lines have the same key.
    """

    path: str
    key: str
    chunks: tuple[Chunk, ...]


@dataclass(frozen=True)
class Plan:
    """What a worker needs to fill the manifest of one unit."""

    file: str
    run_argv: tuple[str, ...]
    directory: str
    shared_key: str
    rooted_key: str
    store: str
    root: str


class PreprocessError(RuntimeError):
    """A unit that the preprocessor rejected, so a guard cannot read all of the tree."""


class DamagedStore(PreprocessError):
    """A chunk of the store does not match its name.  The store removed it, and the next run makes it again."""


def record_chunks(record: Record) -> tuple[Chunk, ...]:
    """Return the chunks that one record describes, in output order.

    Args:
        record: (path, expansion key, lines, chunk digests)

    Returns:
        One Chunk for each line of the record
    """
    path, _key, lines, digests = record
    return tuple(Chunk(path, line, digests[32 * index:32 * index + 32].hex())
                 for index, line in enumerate(array.array(LINE_TYPE, lines)))


def preprocess_argv(argv: list[str]) -> list[str]:
    """Return the compile command with -E, and without its output and dependency-file flags.

    Args:
        argv: The compile command of one database entry

    Returns:
        The command that preprocesses the same source with the same flags
    """
    out: list[str] = []
    skips_value = False
    for flag in argv:
        if skips_value:
            skips_value = False
        elif flag in _DROPPED_FLAGS:
            pass
        elif flag in _DROPPED_WITH_VALUE:
            skips_value = True
        elif not flag.startswith(_DROPPED_PREFIXES):
            out.append(flag)
    out.append("-E")
    return out


# The flags that preprocess_argv drops, the flags that it drops with the
# argument after them, and the prefixes of the attached forms of the second
# set.
_DROPPED_FLAGS = frozenset({"-c", "-MD", "-MMD", "-MP"})
_DROPPED_WITH_VALUE = frozenset({"-o", "-MF", "-MT", "-MQ"})
_DROPPED_PREFIXES = ("-o", "-MF", "-MT", "-MQ")


def is_staged_root(directory: Path, include: Path) -> bool:
    """Return True for a staged layer root: a directory whose entries are all links into the include directory.

    test/layer stages one include root per layer, which links only to the
    include directories of the layers below it.  A negative fixture of that
    directory includes a higher layer on purpose and fails under its root.

    Args:
        directory: A directory on an include path
        include: The include directory of the repository

    Returns:
        Whether each entry is a symbolic link to the directory of the same
        name in the include directory, and there is at least one
    """
    if not directory.is_dir():
        return False
    entries = list(directory.iterdir())
    return bool(entries) and all(entry.is_symlink() and entry.resolve() == (include / entry.name).resolve()
                                 for entry in entries)


def widen_staged_roots(argv: list[str], directory: str, root: Path,
                       staged: dict[str, bool] | None = None) -> list[str]:
    """Replace each staged layer root on the include path with the include directory of the repository.

    The preprocessed pass reads what a unit holds.  A layer fixture exists to
    fail under its staged root, and a route in it is still worth reading, so
    the pass gives it the include path that every other unit gets.

    Args:
        argv: The compile command of one database entry
        directory: The directory the command runs in
        root: The repository root
        staged: The verdict on each include directory that this run has seen,
            or None for no memory

    Returns:
        The command with each staged root replaced
    """
    include = root / "include"
    out: list[str] = []
    count = len(argv)
    index = 0
    while index < count:
        flag = argv[index]
        index += 1
        if not flag.startswith("-I"):
            out.append(flag)
            continue
        if flag == "-I":
            if index == count:
                out.append(flag)
                continue
            value = argv[index]
            index += 1
        else:
            value = flag[2:]
        target = os.path.join(directory, value)
        verdict = None if staged is None else staged.get(target)
        if verdict is None:
            verdict = is_staged_root(Path(target), include)
            if staged is not None:
                staged[target] = verdict
        out.append("-I" + (str(include) if verdict else value))
    return out


def without_root(text: str, root: str) -> str:
    """Replace the root by ROOT_MARK when it starts a path: the whole argument, or the value of a path flag.

    A root inside any other argument, such as a -D value, stays, so the name
    of the manifest depends on the root there.

    Args:
        text: One argument, or the directory of the command
        root: The root of the checkout, resolved

    Returns:
        The argument with the root replaced where it starts a path
    """
    if root not in text:
        return text
    for flag in ("", *PATH_FLAGS):
        if text.startswith(flag):
            value = text[len(flag):]
            if value == root or value.startswith(root + "/"):
                return flag + ROOT_MARK + value[len(root):]
    return text


_COMPILERS: dict[str, list] = {}


def compiler_identity(argv: list[str], directory: str) -> list:
    """Return the path, the size and the mtime of the compiler driver and of its cc1plus.

    The answer for each driver is calculated one time for each process.

    Args:
        argv: The compile command of one database entry
        directory: The directory the command runs in

    Returns:
        The identity, or the bare name when the binary cannot be found
    """
    binary = argv[0] if os.path.isabs(argv[0]) else (
        os.path.join(directory, argv[0]) if os.sep in argv[0] else (shutil.which(argv[0]) or argv[0]))
    if binary in _COMPILERS:
        return _COMPILERS[binary]
    identity: list = []
    for program in (binary, _cc1plus(binary)):
        try:
            info = os.stat(os.path.realpath(program))
            identity += [os.path.realpath(program), info.st_size, info.st_mtime_ns]
        except (OSError, TypeError):
            identity += [program]
    _COMPILERS[binary] = identity
    return identity


def _cc1plus(binary: str) -> str | None:
    """Return the cc1plus that a driver runs, or None when the driver does not say."""
    try:
        found = subprocess.run([binary, "-print-prog-name=cc1plus"], capture_output=True, text=True,
                               timeout=60).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return None
    return found if os.path.isabs(found) else None


def _resolved(path: str, real: dict[str, str] | None) -> str:
    """Return the resolved path of a file name, from the memory of this run when it holds the name."""
    if real is None:
        return os.path.realpath(path)
    found = real.get(path)
    if found is None:
        found = real[path] = os.path.realpath(path)
    return found


def under_root(name: str, directory: str, prefix: str, real: dict[str, str] | None = None) -> str | None:
    """Return the root-relative path of a file name from the output, or None outside the root.

    Args:
        name: The file name, as a line marker or a dependency file spells it
        directory: The directory the command ran in
        prefix: The resolved root and a trailing separator
        real: The resolved path of each name that this run has seen, or None
            for no memory

    Returns:
        The path relative to the root, or None
    """
    if name.startswith("<"):
        return None
    # The root is resolved, so the name is resolved too: a database entry
    # whose directory goes through a symbolic link names files under the
    # root by another path, and a plain join would put them outside it.
    absolute = _resolved(os.path.join(directory, name), real)
    return absolute[len(prefix):] if absolute.startswith(prefix) else None


def dependency_names(text: str, directory: str, real: dict[str, str] | None = None) -> list[str]:
    """Return each file that a make-style dependency file names, resolved, in order and without repeats."""
    body = text.replace("\\\n", " ").split(":", 1)[-1]
    found: dict[str, None] = {}
    for name in re.split(r"(?<!\\)\s+", body):
        if name:
            found[_resolved(os.path.join(directory, name.replace("\\ ", " ")), real)] = None
    return list(found)


def split_command(command: str) -> list[str]:
    """Split a compile command as a POSIX shell does.

    A command with no quote and no backslash splits at white space, which is
    what shlex.split gives for it, at a small part of the cost.

    Args:
        command: The command of one database entry

    Returns:
        The arguments
    """
    if "'" in command or '"' in command or "\\" in command:
        return shlex.split(command)
    return command.split()


class _Files:
    """The raw SHA-256 of each file and the resolved path of each name that a run reads, each found one time."""

    def __init__(self, root: str) -> None:
        """Start an empty memory for one root."""
        self.prefix = root + os.sep
        self.raw: dict[str, bytes] = {}
        self.real: dict[str, str] = {}
        self.lock = threading.Lock()

    def name_of(self, absolute: str) -> str:
        """Return the name that a variant gives a file: relative under the root, absolute outside it."""
        return absolute[len(self.prefix):] if absolute.startswith(self.prefix) else absolute

    def raw_digest(self, name: str) -> bytes:
        """Return the raw SHA-256 of a file by its variant name, or MISSING when the file does not exist."""
        known = self.raw.get(name)
        if known is not None:
            return known
        path = name if name.startswith("/") else self.prefix + name
        try:
            with open(path, "rb") as stream:
                found = hashlib.file_digest(stream, "sha256").digest()
        except OSError:
            found = MISSING
        with self.lock:
            self.raw[name] = found
        return found

    def digest(self, name: str) -> str | None:
        """Return the hexadecimal SHA-256 of a file by its variant name, or None when the file does not exist."""
        found = self.raw_digest(name)
        return None if found == MISSING else found.hex()


def _chunk_names(directory: Path) -> set[str]:
    """Return the name of each chunk file of a store, with no stat call: one listing of each fan-out directory."""
    names: set[str] = set()
    with contextlib.suppress(FileNotFoundError):
        for fan in os.scandir(directory):
            if fan.is_dir(follow_symlinks=False):
                with contextlib.suppress(FileNotFoundError), os.scandir(fan.path) as listing:
                    names.update(item.name for item in listing if not item.name.startswith("."))
    return names


class _Chunks:
    """The names of the chunks in the store, listed one time or found one at a time, and kept up to date."""

    def __init__(self, directory: Path, listed: bool = True) -> None:
        """Bind the memory to the chunk directory of a store.

        Args:
            directory: The chunk directory
            listed: List the directory at the first question.  A worker that
                asks about few chunks finds each one with a stat instead.
        """
        self.directory = directory
        self.known: set[str] | None = None if listed else set()
        self.lock = threading.Lock()

    def has(self, digest: str) -> bool:
        """Return whether the store holds a chunk."""
        known = self.known
        if known is None:
            with self.lock:
                if self.known is None:
                    self.known = _chunk_names(self.directory)
                known = self.known
        if digest in known:
            return True
        if (self.directory / digest[:2] / digest).is_file():
            self.add(digest)
            return True
        return False

    def add(self, digest: str) -> None:
        """Record a chunk that this process wrote or found."""
        with self.lock:
            if self.known is not None:
                self.known.add(digest)


class _Reader:
    """What one process learns during one run: file hashes, chunk names, and the verdict on each blob and record."""

    def __init__(self, root: str, store: Path, listed: bool = True) -> None:
        """Start an empty memory for one root and one store.

        Args:
            root: The resolved root
            store: The directory of the store
            listed: Whether the chunk directory is listed at the first question
        """
        self.files = _Files(root)
        self.chunks = _Chunks(store / "chunks", listed)
        self.store = store
        self.outside: dict[str, bool] = {}
        self.present: dict[bytes, bool] = {}

    def matches(self, names: tuple[str, ...], digests: bytes) -> bool:
        """Return whether each named file has the raw SHA-256 at its place in digests."""
        return b"".join(map(self.files.raw_digest, names)) == digests

    def outside_holds(self, digest: str) -> bool:
        """Return whether each file of a stored blob of outside files has its recorded SHA-256."""
        known = self.outside.get(digest)
        if known is None:
            try:
                names, digests = marshal.loads(blob_bytes(self.store, digest))
                known = self.matches(names, digests)
            except (OSError, ValueError, EOFError, TypeError, DamagedStore):
                known = False
            self.outside[digest] = known
        return known

    def has_chunks(self, record: Record) -> bool:
        """Return whether the store holds each chunk of a record."""
        digests = record[3]
        known = self.present.get(digests)
        if known is None:
            known = all(self.chunks.has(digests[at:at + 32].hex()) for at in range(0, len(digests), 32))
            self.present[digests] = known
        return known

    def valid_variant(self, variants: tuple) -> Variant | None:
        """Return the first variant whose files have their recorded contents and whose chunks all exist."""
        for variant in variants:
            try:
                names, digests, outside, records = variant
                if self.matches(names, digests) and self.outside_holds(outside) \
                        and all(map(self.has_chunks, records)):
                    return variant
            except (TypeError, ValueError, IndexError):
                continue
        return None


def _manifest_path(store: Path, key: str) -> Path:
    """Return the path of the manifest of one key."""
    return store / "units" / key[:2] / f"{key}{MANIFEST_SUFFIX}"


def _read_variants(manifest: Path) -> tuple:
    """Return the variants of a manifest, or none when it is absent, damaged or of another tag."""
    try:
        tag, variants = marshal.loads(manifest.read_bytes())
    except (OSError, ValueError, EOFError, TypeError):
        return ()
    return variants if tag == MANIFEST_TAG and isinstance(variants, tuple) else ()


def _unit_of(file: str, variant: Variant, from_cache: bool) -> Unit:
    """Return the Unit that a valid variant describes."""
    return Unit(file, variant[3], None, from_cache, variant[0])


def _chunk_path(store: Path, digest: str) -> Path:
    """Return the path of one chunk or blob of a store."""
    return store / "chunks" / digest[:2] / digest


def blob_bytes(store: Path, digest: str) -> bytes:
    """Return the bytes of one chunk or blob of a store, read from its file.

    Args:
        store: The directory of the store
        digest: The SHA-256 of the bytes

    Returns:
        The bytes

    Raises:
        FileNotFoundError: If the store lost the chunk
        DamagedStore: If the chunk does not match its name
    """
    path = _chunk_path(store, digest)
    try:
        data = zlib.decompress(path.read_bytes())
        if hashlib.sha256(data).hexdigest() != digest:
            raise ValueError("the bytes do not match their name")
    except (zlib.error, ValueError) as exc:
        path.unlink(missing_ok=True)
        raise DamagedStore(f"the chunk {path} of the preprocessed store is damaged ({exc}).  The store removed "
                           f"it.  Run the guard again, and the store makes the chunk again.") from exc
    return data


def chunk_text(store: Path, digest: str) -> str:
    """Return the text of one chunk of a store, read from its file.

    Args:
        store: The directory of the store
        digest: The SHA-256 of the chunk text

    Returns:
        The text

    Raises:
        FileNotFoundError: If the store lost the chunk
        DamagedStore: If the chunk does not match its name
    """
    return blob_bytes(store, digest).decode()


def _write_blob(store: Path, data: bytes, chunks: _Chunks) -> str:
    """Store bytes under their hash, when the store does not hold them yet, and return the hash."""
    digest = hashlib.sha256(data).hexdigest()
    if not chunks.has(digest):
        cache_dir.write_atomic(_chunk_path(store, digest), zlib.compress(data, 1))
        chunks.add(digest)
    return digest


def _markers(output: bytes) -> list[tuple[int, int, bytes, bytes]]:
    """Return each line marker of the output, in order: (start of its line, start of its text, line, file name).

    A line marker starts a line.  The text of a marker starts after the
    newline that ends the marker, and it ends where the line of the next
    marker starts, so it holds the newline of its last line.

    Complexity: linear in the length of the output.
    """
    end = len(output)
    found: list[tuple[int, int, bytes, bytes]] = []
    head = MARKER.match(output)
    if head is not None:
        found.append((0, min(head.end() + 1, end), head.group(1), head.group(2)))
    for match in MARKER_AFTER_NEWLINE.finditer(output):
        found.append((match.start() + 1, min(match.end() + 1, end), match.group(1), match.group(2)))
    return found


# The value that a memory of names finds for a name that it has not resolved yet.
_ABSENT = object()


def _file_records(output: bytes, directory: str, root: str, store: Path,
                  reader: _Reader) -> tuple[tuple[Record, ...], bool]:
    """Divide the output at its line markers, store each part under the root, and say if one holds the root.

    A part is stored as its bytes when they are valid UTF-8, and as its text
    with each invalid sequence replaced otherwise, so the name of a chunk is
    the SHA-256 of its UTF-8 text.

    Returns:
        One record for each file under the root, in the order of its first
        chunk, and whether the text of one chunk holds the root, which makes
        the variant depend on the root
    """
    prefix = root + os.sep
    root_bytes = root.encode()
    grouped: dict[str, tuple[list[int], list[str]]] = {}
    holds_root = False
    markers = _markers(output)
    names: dict[bytes, object] = {}
    for index, (_line_start, text_start, line, name) in enumerate(markers):
        path = names.get(name, _ABSENT)
        if path is _ABSENT:
            spelled = name.decode(errors="replace").replace('\\"', '"').replace("\\\\", "\\")
            path = names[name] = under_root(spelled, directory, prefix, reader.files.real)
        if path is None:
            continue
        end = markers[index + 1][0] if index + 1 < len(markers) else len(output)
        raw = output[text_start:end]
        if not raw:
            continue
        try:
            raw.decode()
            data = raw
        except UnicodeDecodeError:
            data = raw.decode(errors="replace").encode()
        holds_root = holds_root or root_bytes in data
        lines, digests = grouped.setdefault(str(path), ([], []))
        lines.append(int(line))
        digests.append(_write_blob(store, data, reader.chunks))
    records = tuple((path, hashlib.sha256("\n".join(digests).encode()).hexdigest(),
                     array.array(LINE_TYPE, lines).tobytes(), bytes.fromhex("".join(digests)))
                    for path, (lines, digests) in grouped.items())
    return records, holds_root


# The memory of one worker process, made by _start_worker.
_WORKER: dict = {}


def _start_worker(root: str, store: str) -> None:
    """Give a worker process its own memory of hashes, names and chunks for one run."""
    _WORKER["reader"] = _Reader(root, Path(store), listed=False)


def _fill_in_worker(plan: Plan, wait: bool) -> tuple[Unit, bool] | None:
    """Fill the manifest of one unit in a worker process."""
    return fill(plan, _WORKER["reader"], wait)


def fill(plan: Plan, reader: _Reader, wait: bool = True) -> tuple[Unit, bool] | None:
    """Read one unit from a valid variant, or preprocess it and add its variant to its manifest.

    The lock of the shared name is held during the work, so a second guard
    that wants the same unit waits, then reads the variant that the first
    one wrote.

    Args:
        plan: The unit and its two names
        reader: The memory of this process
        wait: When false, give up at once on a unit that another process holds

    Returns:
        The unit and whether this call ran the preprocessor, or None when
        wait is false and another process holds the unit
    """
    store = Path(plan.store)
    shared = _manifest_path(store, plan.shared_key)
    rooted = _manifest_path(store, plan.rooted_key)
    shared.parent.mkdir(parents=True, exist_ok=True)
    with open(shared.with_suffix(".lock"), "a") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX if wait else fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return None
        for manifest in (shared, rooted):
            variant = reader.valid_variant(_read_variants(manifest))
            if variant is not None:
                return _unit_of(plan.file, variant, True), False
        descriptor, depfile = tempfile.mkstemp(prefix="preprocessed-", suffix=".d")
        os.close(descriptor)
        try:
            result = subprocess.run([*plan.run_argv, "-MD", "-MF", depfile], cwd=plan.directory,
                                    capture_output=True)
            names = dependency_names(Path(depfile).read_text(errors="replace"), plan.directory, reader.files.real)
        finally:
            os.unlink(depfile)
        if result.returncode != 0:
            first = result.stderr.decode(errors="replace").strip().splitlines()[:1]
            failure = f"{plan.file}: {first[0] if first else 'exit ' + str(result.returncode)}"
            return Unit(plan.file, (), failure), True
        records, holds_root = _file_records(result.stdout, plan.directory, plan.root, store, reader)
        for record in records:
            reader.present[record[3]] = True
        files = reader.files
        inside = tuple(files.name_of(name) for name in names if name.startswith(files.prefix))
        outside = tuple(name for name in names if not name.startswith(files.prefix))
        blob = _write_blob(store, marshal.dumps((outside, b"".join(map(files.raw_digest, outside)))), reader.chunks)
        reader.outside[blob] = True
        variant: Variant = (inside, b"".join(map(files.raw_digest, inside)), blob, records)
        target = rooted if holds_root else shared
        target.parent.mkdir(parents=True, exist_ok=True)
        others = tuple(old for old in _read_variants(target) if not isinstance(old, tuple) or old[:3] != variant[:3])
        cache_dir.write_atomic(target, marshal.dumps((MANIFEST_TAG, (variant, *others)[:MAX_VARIANTS])))
        return _unit_of(plan.file, variant, False), True


class Results:
    """The results of one guard for each expansion or source text, under names that hash the code of the guard.

    A result is a JSON value.  get() gives None for a result that the store
    does not hold or that is damaged.
    """

    def __init__(self, directory: Path) -> None:
        """Bind the results to their directory."""
        self.directory = directory

    def _path(self, key: str) -> Path:
        """Return the path of the result of one key."""
        return self.directory / key[:2] / f"{key}.json"

    def get(self, key: str) -> object | None:
        """Return the stored result of one key, or None."""
        path = self._path(key)
        try:
            data = json.loads(path.read_bytes())
            cache_dir.mark_used(path, path.stat().st_mtime)
            return data["value"]
        except (OSError, ValueError, KeyError, TypeError):
            return None

    def put(self, key: str, value: object) -> None:
        """Store the result of one key."""
        cache_dir.write_atomic(self._path(key), json.dumps({"value": value}).encode())


def _results_in(store: Path, guard: str, identity: tuple[object, ...]) -> Results:
    """Return the results of one guard in a store directory, under a name that hashes the guard and its inputs."""
    directory = store / "results" / f"{guard}-{code_identity(*identity)[:24]}"
    directory.mkdir(parents=True, exist_ok=True)
    return Results(directory)


def text_results(guard: str, *identity: object) -> Results | None:
    """Return the results of one guard for the source texts it reads, or None when the caches are off.

    A guard keys each result by the SHA-256 of the one text that it reads,
    so the result needs no compile database.  The results live in the store
    and share its bound.

    Args:
        guard: The name of the results
        identity: The parts that code_identity() hashes: the guard script,
            the parser identity and any other input

    Returns:
        The results store, or None
    """
    directory = cache_dir.cache_root(STORE_CACHE)
    return None if directory is None else _results_in(directory, guard, identity)


def map_batches(function: Callable[[list], list], items: list, jobs: int, batch: int = 32) -> list:
    """Apply a function to batches of items, in worker processes when there are many, and return the results in order.

    A worker is a fork of this process when the process has one thread,
    else a child of a fork server.  So the function must be a module-level
    function.  The function gives one result for each item of its batch.

    Args:
        function: Maps one batch of items to one result for each item
        items: The items, each small and picklable
        jobs: The largest number of worker processes
        batch: The number of items in one task

    Returns:
        The results, in the order of the items
    """
    if len(items) < 2 * batch or jobs <= 1:
        return function(items)
    batches = [items[start:start + batch] for start in range(0, len(items), batch)]
    method = "fork" if threading.active_count() == 1 else "forkserver"
    found: list = []
    with ProcessPoolExecutor(min(jobs, len(batches)), mp_context=multiprocessing.get_context(method)) as pool:
        for part in pool.map(function, batches):
            found.extend(part)
    return found


def code_identity(*parts: object) -> str:
    """Return a hash of the code and the data that a guard result depends on.

    A Path part contributes the bytes of the file, and any other part its text.

    Args:
        parts: The guard script, the parser identity, and any other input

    Returns:
        The hexadecimal SHA-256
    """
    digest = hashlib.sha256(f"results-{RESULTS_VERSION}".encode())
    for part in parts:
        digest.update(b"\0")
        digest.update(hashlib.sha256(part.read_bytes() if isinstance(part, Path) else str(part).encode()).digest())
    return digest.hexdigest()


class Store:
    """The shared preprocessed pass over one compile database."""

    def __init__(self, compile_db: Path, root: Path, jobs: int = 0) -> None:
        """Open the store for a compile database.

        Args:
            compile_db: The compile_commands.json file
            root: The repository root, whose files the store keeps
            jobs: The number of parallel preprocessor runs, or 0 for the
                number of processors, at most 16
        """
        self.compile_db = compile_db
        self.root = root.resolve()
        self.prefix = str(self.root) + os.sep
        found = cache_dir.cache_root(STORE_CACHE)
        # With the caches off, the store lives in a private directory for this process only.
        self._private = tempfile.TemporaryDirectory(prefix="preprocessed-") if found is None else None
        self.directory = found if found is not None else Path(self._private.name)
        for part in ("units", "chunks", "results"):
            (self.directory / part).mkdir(parents=True, exist_ok=True)
        self.jobs = jobs or min(16, os.cpu_count() or 1)
        self._reader = _Reader(str(self.root), self.directory)
        self._staged: dict[str, bool] = {}
        self._unrooted: dict[str, str] = {}
        self._environment = [(name, os.environ.get(name)) for name in ENVIRONMENT]
        self._texts: dict[str, str] = {}
        self._lock = threading.Lock()
        self._shared_lock = self._hold_shared()
        # The number of preprocessor runs this store started, for the self-test.
        self.runs = 0
        # The units of the last pass, and how many of them came from the store.
        self.unit_count = 0
        self.cached_count = 0
        # The units of the last pass that failed, and their failures, one line each.
        self.failed: list[Unit] = []
        self.failures: list[str] = []

    def _hold_shared(self):
        """Take the shared lock of the store, which keeps an eviction out while this process reads."""
        holder = open(self.directory / "store.lock", "a")
        fcntl.flock(holder, fcntl.LOCK_SH)
        return holder

    def file_hash(self, relative: str) -> str | None:
        """Return the SHA-256 of a file under the root, calculated one time for each run.

        Args:
            relative: The path relative to the root

        Returns:
            The hash, or None when the file no longer exists
        """
        return self._reader.files.digest(relative)

    def under_root(self, name: str, directory: str) -> str | None:
        """Return the root-relative path of a file name from the output, or None outside the root."""
        return under_root(name, directory, self.prefix, self._reader.files.real)

    def text(self, digest: str) -> str:
        """Return the text of a chunk.

        Args:
            digest: The SHA-256 of the chunk text

        Returns:
            The text

        Raises:
            FileNotFoundError: If the store lost the chunk
            DamagedStore: If the chunk does not match its name
        """
        with self._lock:
            known = self._texts.get(digest)
        if known is not None:
            return known
        text = chunk_text(self.directory, digest)
        with self._lock:
            self._texts[digest] = text
        return text

    def map_batches(self, function: Callable[[list], list], items: list, batch: int = 32) -> list:
        """Apply a function to batches of items with at most `jobs` workers, as the module function map_batches does.

        Args:
            function: Maps one batch of items to one result for each item
            items: The items, each small and picklable
            batch: The number of items in one task

        Returns:
            The results, in the order of the items
        """
        return map_batches(function, items, self.jobs, batch)

    def _plan(self, entry: dict) -> Plan:
        """Return the plan of one database entry: its preprocessor command and its two manifest names.

        The name hashes one JSON text of the key items.  The rooted name
        hashes that text, a NUL and the root.  JSON has no raw NUL and a path
        has none, so the two names never meet.
        """
        argv = entry["arguments"] if "arguments" in entry else split_command(entry["command"])
        directory = entry["directory"]
        root = str(self.root)
        run_argv = preprocess_argv(widen_staged_roots(argv, directory, self.root, self._staged))
        run_argv[-1:-1] = [f"-fmacro-prefix-map={root}/="]
        unrooted = self._unrooted
        arguments: list[str] = []
        for argument in run_argv:
            found = unrooted.get(argument)
            if found is None:
                found = unrooted[argument] = without_root(argument, root)
            arguments.append(found)
        identity = json.dumps([STORE_VERSION, arguments, without_root(directory, root),
                               compiler_identity(argv, directory), self._environment]).encode()
        return Plan(entry["file"], tuple(run_argv), directory, hashlib.sha256(identity).hexdigest(),
                    hashlib.sha256(identity + b"\0" + root.encode()).hexdigest(), str(self.directory), root)

    def _lookup(self, plan: Plan) -> Unit | None:
        """Return the unit of a plan from a valid variant, or None when the unit misses."""
        for key in (plan.shared_key, plan.rooted_key):
            manifest = _manifest_path(self.directory, key)
            variant = self._reader.valid_variant(_read_variants(manifest))
            if variant is not None:
                with contextlib.suppress(OSError):
                    cache_dir.mark_used(manifest, manifest.stat().st_mtime)
                return _unit_of(plan.file, variant, True)
        return None

    def units(self) -> Iterator[Unit]:
        """Yield each database entry after the preprocessed pass, in database order.

        Yields:
            One Unit for each entry
        """
        units = self._resolve(json.loads(self.compile_db.read_text()))
        self._evict()
        yield from units

    def fill_shard(self, shard: int, count: int) -> list[Unit]:
        """Read or preprocess the units of one shard of the database: each entry whose index is shard modulo count.

        The shards of one database are disjoint and together hold every
        entry, so tests that fill the shards at the same time fill the store
        for each guard that reads it after them.

        Args:
            shard: The shard, from 0
            count: The number of shards

        Returns:
            The units of the shard, in database order
        """
        entries = json.loads(self.compile_db.read_text())
        return self._resolve(entries[shard::count])

    def _resolve(self, entries: list[dict]) -> list[Unit]:
        """Return the unit of each entry, from a valid variant or from the preprocessor, and record the counts."""
        plans = [self._plan(entry) for entry in entries]
        found: list[Unit | None] = [self._lookup(plan) for plan in plans]
        misses = [index for index, unit in enumerate(found) if unit is None]
        for index, (unit, ran) in zip(misses, self._fill_all([plans[index] for index in misses]), strict=True):
            found[index] = unit
            self.runs += ran
        units = [unit for unit in found if unit is not None]
        self.unit_count = len(units)
        self.cached_count = sum(unit.from_cache for unit in units)
        self.failed = [unit for unit in units if unit.failure is not None]
        self.failures = [unit.failure for unit in self.failed if unit.failure is not None]
        return units

    def _fill_all(self, plans: list[Plan]) -> list[tuple[Unit, bool]]:
        """Fill the manifests of the units that missed, in worker processes when there are several.

        The workers take the units in a random order, and they first pass
        over each unit that another process holds.  Two guards that start
        cold at the same time therefore divide the units between them, and
        each one then reads what the other one wrote.
        """
        if not plans:
            return []
        if len(plans) < PARALLEL_MISSES or self.jobs <= 1:
            return [fill(plan, self._reader) for plan in plans]
        order = random.sample(range(len(plans)), len(plans))
        done: list[tuple[Unit, bool] | None] = [None] * len(plans)
        method = "fork" if threading.active_count() == 1 else "forkserver"
        with ProcessPoolExecutor(min(self.jobs, len(plans)), mp_context=multiprocessing.get_context(method),
                                 initializer=_start_worker, initargs=(str(self.root), str(self.directory))) as pool:
            for index, result in zip(order, pool.map(_fill_in_worker, [plans[index] for index in order],
                                                     [False] * len(order), chunksize=4), strict=True):
                done[index] = result
            held = [index for index in order if done[index] is None]
            for index, result in zip(held, pool.map(_fill_in_worker, [plans[index] for index in held],
                                                    [True] * len(held), chunksize=4), strict=True):
                done[index] = result
        filled = [result for result in done if result is not None]
        for unit, _ran in filled:
            for chunk in unit.chunks:
                self._reader.chunks.add(chunk.digest)
        return filled

    def expansions(self) -> Iterator[Expansion]:
        """Yield each distinct expansion of each file under the root, over every unit.

        The order follows the compile database and then the order of the
        first chunk of each file, so a report is stable.  A unit that failed
        gives no expansion, and its failure is in self.failures after the
        pass.

        Yields:
            One Expansion for each distinct (path, key, lines)
        """
        seen: set[tuple[str, str, bytes]] = set()
        for unit in self.units():
            for record in unit.records:
                marker = record[:3]
                if marker in seen:
                    continue
                seen.add(marker)
                yield Expansion(record[0], record[1], record_chunks(record))

    def results(self, guard: str, *identity: object) -> Results:
        """Return the store of one guard's results, under a name that hashes the guard and its inputs.

        Args:
            guard: The name of the guard
            identity: The parts that code_identity() hashes: the guard
                script, the parser identity and any other input

        Returns:
            The results store
        """
        return _results_in(self.directory, guard, identity)

    def _evict(self) -> None:
        """Hold the store under its size and age limits, when the turn is due and no other process reads.

        A process that gets the turn and not the exclusive lock, or that
        leaves garbage for the next turn, gives the stamp its last time
        again, so the turn stays due for the next run.
        """
        stamp = self.directory / "evicted"
        try:
            last = stamp.stat().st_mtime
        except FileNotFoundError:
            last = None
        with cache_dir.eviction_turn(self.directory) as has_turn:
            if not has_turn:
                return
            self._shared_lock.close()
            is_done = False
            try:
                with open(self.directory / "store.lock", "a") as exclusive:
                    try:
                        fcntl.flock(exclusive, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    except BlockingIOError:
                        return
                    is_done = evict_store(self.directory, STORE_BYTES)
            finally:
                if not is_done:
                    if last is None:
                        stamp.unlink(missing_ok=True)
                    else:
                        os.utime(stamp, (last, last))
                self._shared_lock = self._hold_shared()


def _remove_garbage(units: Path, limit: int) -> bool:
    """Remove each file under units/ that is not a manifest of this version, and each lock with no manifest.

    The caller holds the exclusive lock of the store, so no process fills a
    unit and a lock with no manifest is free.

    Args:
        units: The units directory of a store
        limit: The largest number of files to remove

    Returns:
        Whether no garbage remains
    """
    removed = 0
    with contextlib.suppress(FileNotFoundError):
        for fan in os.scandir(units):
            if not fan.is_dir(follow_symlinks=False):
                continue
            with os.scandir(fan.path) as listing:
                names = {item.name for item in listing}
            for name in names:
                if name.endswith(MANIFEST_SUFFIX) or name.startswith("."):
                    continue
                if name.endswith(".lock") and f"{name[:-len('.lock')]}{MANIFEST_SUFFIX}" in names:
                    continue
                if removed >= limit:
                    return False
                with contextlib.suppress(FileNotFoundError):
                    os.unlink(os.path.join(fan.path, name))
                removed += 1
    return True


def evict_store(directory: Path, max_bytes: int, max_age: float = cache_dir.MAX_AGE,
                now: float | None = None, garbage_limit: int = GARBAGE_PER_TURN) -> bool:
    """Remove the garbage, the old manifests and the old results of a store, then each chunk that no manifest names.

    The caller holds the exclusive lock of the store.  Complexity: one read
    of each remaining manifest, and one listing of each entry.

    Args:
        directory: The directory of the store
        max_bytes: The size limit
        max_age: The age limit of an entry with no use
        now: The time of the eviction, or None for the clock
        garbage_limit: The largest number of garbage files to remove

    Returns:
        Whether no garbage remains for the next turn
    """
    moment = time.time() if now is None else now
    is_clean = _remove_garbage(directory / "units", garbage_limit)

    def owners() -> list[cache_dir.Entry]:
        """Return each manifest and each result of the store."""
        results = [entry for folder in (directory / "results").iterdir() if folder.is_dir()
                   for entry in cache_dir.entries(folder, ".json")]
        return cache_dir.entries(directory / "units", MANIFEST_SUFFIX) + results

    def drop(chosen: list[cache_dir.Entry]) -> int:
        """Remove manifests and results, then each chunk that no remaining manifest names.

        Returns:
            The bytes that the store still holds
        """
        cache_dir.remove(chosen)
        for entry in chosen:
            entry.path.with_suffix(".lock").unlink(missing_ok=True)
        named: set[str] = set()
        for manifest in cache_dir.entries(directory / "units", MANIFEST_SUFFIX):
            for variant in _read_variants(manifest.path):
                with contextlib.suppress(TypeError, ValueError, IndexError):
                    named.add(variant[2])
                    for record in variant[3]:
                        digests = record[3]
                        named.update(digests[at:at + 32].hex() for at in range(0, len(digests), 32))
        chunks = cache_dir.entries(directory / "chunks")
        cache_dir.remove([entry for entry in chunks if entry.path.name not in named])
        return (sum(entry.size for entry in owners())
                + sum(entry.size for entry in chunks if entry.path.name in named))

    held = drop([entry for entry in owners() if moment - entry.used > max_age])
    # While the store is over its size, each round removes the older half of
    # the manifests and results, so the loop ends.
    while held > max_bytes:
        remaining = sorted(owners(), key=lambda entry: entry.used)
        if not remaining:
            break
        held = drop(remaining[:max(1, len(remaining) // 2)])
    return is_clean


def files_of(unit: Unit) -> dict[str, tuple[str, list[Chunk]]]:
    """Return the chunks of each file of a unit in output order, with a key for the list.

    The key is a hash of the file's chunk list, so two units that expand a
    file the same way give the same key, and a guard can reuse its result
    without reading the text again.

    Args:
        unit: One unit

    Returns:
        For each path, the key and the chunks
    """
    return {record[0]: (record[1], list(record_chunks(record))) for record in unit.records}


def joined(store: Store, chunks: list[Chunk] | tuple[Chunk, ...]) -> str:
    """Return the text of a file's chunks, joined in output order.

    Args:
        store: The store that holds the chunks
        chunks: The chunks of one file of one unit

    Returns:
        The joined text
    """
    return "\n".join(store.text(c.digest) for c in chunks)


def expanded_files(store: Store) -> Iterator[tuple[str, str]]:
    """Yield the macro-expanded text of each file under the root, one time for each distinct expansion.

    Two units that expand a file the same way give one text, because the key
    of files_of() is equal.  A file that two units expand differently gives
    two texts.  The order follows the compile database and then the output,
    so a report is stable.

    Args:
        store: The store over one compile database

    Yields:
        (path relative to the root, joined text)

    Raises:
        PreprocessError: After the last text, if a unit failed, because a
            guard that cannot read one unit cannot prove the tree clean
    """
    seen: set[tuple[str, str]] = set()
    for expansion in store.expansions():
        if (expansion.path, expansion.key) in seen:
            continue
        seen.add((expansion.path, expansion.key))
        yield expansion.path, joined(store, expansion.chunks)
    if store.failures:
        raise PreprocessError(
            f"{len(store.failures)} unit(s) failed to preprocess, so the expanded tree is incomplete:\n  "
            + "\n  ".join(store.failures[:10])
        )


def _self_test_squares(batch: list[int]) -> list[int]:
    """Return the square of each number of a batch, the worker function of the map_batches case."""
    return [number * number for number in batch]


def self_test() -> int:
    """Preprocess scratch trees in a scratch store and prove the store's verdicts.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, ok: bool) -> None:
        """Record one case."""
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    compiler = os.environ.get("CXX") or shutil.which("c++") or shutil.which("g++")
    if compiler is None:
        print("preprocessed --self-test: no C++ compiler to run", file=sys.stderr)
        return 2
    with cache_dir.scratch_root() as caches, tempfile.TemporaryDirectory() as scratch:
        _self_test_cases(expect, compiler, caches / STORE_CACHE, Path(scratch))
    if failures:
        print(f"preprocessed --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print("preprocessed --self-test: every case passes.")
    return 0


def _self_test_cases(expect, compiler: str, store_dir: Path, scratch: Path) -> None:
    """Run each case of the self-test against one scratch store.

    Args:
        expect: The recorder of the self-test
        compiler: The C++ compiler that the scratch databases name
        store_dir: The directory of the scratch store
        scratch: A scratch directory for the trees
    """
    root = scratch / "first"
    root.mkdir()
    (root / "shared.h").write_text("#pragma once\nstruct Shared { int a; };\n")
    (root / "quiet.h").write_text("#pragma once\n#define QUIET 1\n")
    (root / "a.cpp").write_text('#include "shared.h"\n#include "quiet.h"\nint a_only;\n'
                                'const char* where = __FILE__;\n')
    (root / "b.cpp").write_text('#include "shared.h"\nint b_only;\n')
    (root / "broken.cpp").write_text('#include "missing.h"\n')
    (root / "rooted.cpp").write_text("const char* home = HOME_PATH;\n")
    outside = scratch / "outside"
    outside.mkdir()
    (outside / "system.h").write_text("#pragma once\n#define SYSTEM_VALUE 1\n")
    (root / "c.cpp").write_text('#include <system.h>\nint c_value = SYSTEM_VALUE;\n')
    (root / "d.cpp").write_text('#include <system.h>\nint d_value = SYSTEM_VALUE;\n')
    database = root / "build" / "compile_commands.json"
    database.parent.mkdir()

    def write_db(base: Path, files: list[str], flags: str = "") -> Path:
        """Write a compile database for the named sources of one tree, and return its path."""
        target = base / "build" / "compile_commands.json"
        target.write_text(json.dumps([{"directory": str(base), "file": name,
                                       "command": f"{compiler} -std=c++20 {flags} -c {base / name} -o {name}.o"}
                                      for name in files]))
        return target

    def chunk_files() -> list[Path]:
        """Return each chunk file of the scratch store."""
        return [path for path in (store_dir / "chunks").rglob("*") if path.is_file() and not path.name.startswith(".")]

    samples = ["g++ -I/a  -DX=1\t-c a.cpp -o a.o", 'g++ -DP=\\"/x y\\" -c a.cpp', "g++ '-Dq=a b' -c a.cpp",
               "g++ -DE=a\\ b -c a.cpp"]
    expect("split_command splits each command as shlex.split does",
           all(split_command(sample) == shlex.split(sample) for sample in samples))
    sample_output = (b'# 0 "x.cpp"\nint text_zero;\n# 1 "x.h" 1 3\n# 3 "x.h"\n#notmarker\n# not a marker\n'
                     b'# 9 "x.h"\nint nine; # 5 "inside.h"\n# 2 "x.cpp" 2\n# 4 "x.cpp"')
    scanned = _markers(sample_output)
    by_line_start = [(line, name, sample_output[text:scanned[index + 1][0] if index + 1 < len(scanned) else None])
                     for index, (_start, text, line, name) in enumerate(scanned)]
    by_multiline = list(re.finditer(b"^" + MARKER.pattern + b"\n?", sample_output, re.M))
    expect("the scan of line markers finds each marker at the start of a line, and each text up to the next one",
           [(line, name) for line, name, _text in by_line_start]
           == [(b"0", b"x.cpp"), (b"1", b"x.h"), (b"3", b"x.h"), (b"9", b"x.h"), (b"2", b"x.cpp"), (b"4", b"x.cpp")]
           == [(m.group(1), m.group(2)) for m in by_multiline]
           and [text for _line, _name, text in by_line_start]
           == [sample_output[m.end():by_multiline[index + 1].start() if index + 1 < len(by_multiline) else None]
               for index, m in enumerate(by_multiline)])
    write_db(root, ["a.cpp", "b.cpp"])
    cold = list(Store(database, root).units())
    expect("a cold run preprocesses each unit", [u.from_cache for u in cold] == [False, False])
    expect("a cold run has no failure", all(u.failure is None for u in cold))
    expect("the store is under the cache root and not beside the compile database",
           (store_dir / "units").is_dir() and not (database.parent / "preprocessed-cache").exists())
    shared = {c.digest for u in cold for c in u.chunks if c.path == "shared.h"}
    expect("a header included the same way is stored one time", len(shared) == 1)
    blobs = {variant[2] for manifest in (store_dir / "units").rglob(f"*{MANIFEST_SUFFIX}")
             for variant in _read_variants(manifest)}
    expect("the store holds each distinct chunk and each distinct blob of outside files one time",
           len(chunk_files()) == len({c.digest for u in cold for c in u.chunks} | blobs))
    store = Store(database, root)
    files = files_of(cold[0])
    expect("the joined text of a file holds its code", "struct Shared" in joined(store, files["shared.h"][1]))
    expect("two units that expand a file the same way give one key",
           files["shared.h"][0] == files_of(cold[1])["shared.h"][0])
    expect("a record keeps the expansion key of its chunk names",
           all(key == hashlib.sha256("\n".join(c.digest for c in chunks).encode()).hexdigest()
               for key, chunks in files.values()))
    lines = [c.line + store.text(c.digest).split("a_only")[0].count("\n")
             for c in cold[0].chunks if c.path == "a.cpp" and "a_only" in store.text(c.digest)]
    expect("a chunk's line gives the source line of its text", lines == [3])
    own = joined(store, files["a.cpp"][1])
    expect("__FILE__ expands to the path relative to the root", '"a.cpp"' in own and str(root) not in own)
    expect("a unit lists each header it read, one that adds no line too",
           {"shared.h", "quiet.h"} <= cold[0].dependencies and "quiet.h" not in cold[1].dependencies)
    link = scratch / "first-link"
    link.symlink_to(root, target_is_directory=True)
    database.write_text(json.dumps([{"directory": str(link), "file": "a.cpp",
                                     "command": f"{compiler} -std=c++20 -c a.cpp -o a.o"}]))
    linked = list(Store(database, root).units())
    expect("an entry whose directory is a symbolic link still lists its dependencies and chunks",
           linked[0].failure is None and {"shared.h", "quiet.h"} <= linked[0].dependencies
           and any(chunk.path == "a.cpp" for chunk in linked[0].chunks))
    link.unlink()

    write_db(root, ["a.cpp", "b.cpp"])
    expanded = list(expanded_files(Store(database, root)))
    expect("expanded_files gives a file two units expand the same way one time",
           [path for path, _text in expanded].count("shared.h") == 1
           and {"a.cpp", "b.cpp"} <= {path for path, _text in expanded})
    distinct = Store(database, root)
    listed = list(distinct.expansions())
    expect("expansions gives each distinct expansion one time",
           sorted(e.path for e in listed) == ["a.cpp", "b.cpp", "quiet.h", "shared.h"])
    warm_store = Store(database, root)
    warm = list(warm_store.units())
    expect("a warm run reads each manifest", [u.from_cache for u in warm] == [True, True]
           and warm_store.runs == 0 and warm_store.cached_count == 2)
    expect("a warm run gives the same chunks", [u.chunks for u in warm] == [u.chunks for u in cold])
    expect("a warm run gives the same dependencies", [u.dependencies for u in warm] == [u.dependencies for u in cold])

    # A second checkout of the same tree reads the variants of the first.
    second = scratch / "second"
    shutil.copytree(root, second, ignore=shutil.ignore_patterns("build"))
    (second / "build").mkdir()
    shared_units = list(Store(write_db(second, ["a.cpp", "b.cpp"]), second).units())
    expect("a second work tree with the same files reads the variants of the first",
           [u.from_cache for u in shared_units] == [True, True]
           and [u.chunks for u in shared_units] == [u.chunks for u in cold])
    write_db(root, ["rooted.cpp"], f'-DHOME_PATH=\\"{root}/home\\"')
    rooted_plan = Store(database, root)._plan(json.loads(database.read_text())[0])
    rooted_first = list(Store(database, root).units())
    expect("a variant whose text holds the root goes under the name that hashes the root",
           _manifest_path(store_dir, rooted_plan.rooted_key).is_file()
           and not _manifest_path(store_dir, rooted_plan.shared_key).exists())
    marker = f'# 1 "{root}/x.h"\n'.encode()
    probe = _Reader(str(root), store_dir)
    expect("the text of a chunk that holds the root marks its variant",
           _file_records(marker + f"const char* p = \"{root}/y\";\n".encode(), str(root), str(root), store_dir,
                         probe)[1]
           and not _file_records(marker + b"int clean;\n", str(root), str(root), store_dir, probe)[1])
    invalid = _file_records(marker + b"int bad = '\xff';\n", str(root), str(root), store_dir, probe)[0]
    expect("a chunk that is not UTF-8 is stored as its text with the invalid byte replaced",
           chunk_text(store_dir, record_chunks(invalid[0])[0].digest) == "int bad = '\ufffd';\n")
    second_db = write_db(second, ["rooted.cpp"], f'-DHOME_PATH=\\"{second}/home\\"')
    rooted_store = Store(second_db, second)
    rooted_second = list(rooted_store.units())
    text = joined(rooted_store, files_of(rooted_second[0])["rooted.cpp"][1])
    expect("a unit whose text holds its root is not shared with another root",
           rooted_first[0].failure is None and not rooted_second[0].from_cache
           and f"{second}/home" in text and str(root) not in text)
    again = list(Store(second_db, second).units())
    expect("the same root reads a unit whose text holds the root", again[0].from_cache)

    write_db(root, ["a.cpp", "b.cpp"])
    time.sleep(0.01)
    (root / "quiet.h").write_text("#pragma once\n#define QUIET 2\n")
    changed = list(Store(database, root).units())
    expect("a changed header that adds no line makes its units stale",
           [u.from_cache for u in changed] == [False, True])
    (root / "quiet.h").write_text("#pragma once\n#define QUIET 1\n")
    reverted = list(Store(database, root).units())
    expect("a manifest keeps the old variant, so the old header reads it again",
           [u.from_cache for u in reverted] == [True, True])
    saved_cpath = os.environ.get("CPATH")
    os.environ["CPATH"] = str(scratch / "elsewhere")
    try:
        moved = list(Store(database, root).units())
    finally:
        if saved_cpath is None:
            os.environ.pop("CPATH")
        else:
            os.environ["CPATH"] = saved_cpath
    expect("an include path from the environment is part of the name", [u.from_cache for u in moved] == [False, False])
    plain, empty = (Store(database, root)._plan({"directory": str(root), "file": "a.cpp", "arguments": arguments})
                    for arguments in ([compiler, "-DX", "-c", "a.cpp"], [compiler, "-DX", "", "-c", "a.cpp"]))
    expect("an empty argument is part of the name", plain.shared_key != empty.shared_key)

    # The files outside the root: one stored list for the units that read the
    # same ones, and a change to one of them makes each of its units stale.
    write_db(root, ["c.cpp", "d.cpp"], f"-isystem {outside}")
    system_units = list(Store(database, root).units())
    system_blobs = [{variant[2] for variant in _read_variants(_manifest_path(store_dir, plan.shared_key))}
                    for plan in (Store(database, root)._plan(entry) for entry in json.loads(database.read_text()))]
    expect("units that read the same files outside the root name one stored list of them",
           all(u.failure is None for u in system_units) and len(system_blobs[0] | system_blobs[1]) == 1)
    (outside / "system.h").write_text("#pragma once\n#define SYSTEM_VALUE 2\n")
    system_changed = list(Store(database, root).units())
    expect("a changed file outside the root makes each unit that read it stale",
           [u.from_cache for u in system_changed] == [False, False])
    system_text = list(Store(database, root).expansions())
    expect("the units that a changed outside file made stale give the new text",
           any("int c_value = 2 ;" in " ".join(joined(Store(database, root), e.chunks).split()) for e in system_text))

    (root / "quiet.h").write_text("#pragma once\n#define QUIET 3\n")
    (root / "shared.h").write_text("#pragma once\nstruct Shared { int a; int b; };\n")
    write_db(root, ["a.cpp", "b.cpp"])
    first, second_run = Store(database, root), Store(database, root)
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(lambda each: list(each.units()), (first, second_run)))
    expect("two guards that run at the same time preprocess each unit one time",
           first.runs + second_run.runs == 2)

    many = [f"many{index}.cpp" for index in range(PARALLEL_MISSES + 2)]
    for index, name in enumerate(many):
        (root / name).write_text(f'#include "shared.h"\nint many_{index};\n')
    write_db(root, many)
    in_workers = Store(database, root, jobs=4)
    parallel = list(in_workers.units())
    inline = list(Store(database, root, jobs=1).units())
    expect("worker processes fill the units that miss, and a second run reads them",
           in_workers.runs == len(many) and all(u.failure is None for u in parallel)
           and [u.chunks for u in parallel] == [u.chunks for u in inline] and all(u.from_cache for u in inline))
    for index, name in enumerate(many):
        (root / name).write_text(f'#include "shared.h"\nint many_{index}_again;\n')
    together = [Store(database, root, jobs=3), Store(database, root, jobs=3)]
    with ThreadPoolExecutor(max_workers=2) as pool:
        both = list(pool.map(lambda each: list(each.units()), together))
    expect("two guards with worker processes that start cold together preprocess each unit one time",
           sum(each.runs for each in together) == len(many)
           and [unit.chunks for unit in both[0]] == [unit.chunks for unit in both[1]]
           and all(unit.failure is None for unit in both[0]))
    for index, name in enumerate(many):
        (root / name).write_text(f'#include "shared.h"\nint many_{index}_sharded;\n')
    fillers = [Store(database, root, jobs=2) for _shard in range(3)]
    shards = [filler.fill_shard(shard, 3) for shard, filler in enumerate(fillers)]
    after = Store(database, root)
    reread = list(after.units())
    expect("the shards of a database are disjoint, hold each entry, and fill the store for a guard after them",
           sorted(unit.file for part in shards for unit in part) == sorted(many)
           and sum(filler.runs for filler in fillers) == len(many) and after.runs == 0
           and all(unit.from_cache for unit in reread))
    squares = Store(database, root, jobs=4)
    expect("map_batches gives one result for each item, in order, in worker processes and inline",
           squares.map_batches(_self_test_squares, list(range(100)), batch=8) == [n * n for n in range(100)]
           and squares.map_batches(_self_test_squares, [3, 4], batch=8) == [9, 16])

    write_db(root, ["a.cpp", "broken.cpp"])
    broken = list(Store(database, root).units())
    expect("a unit the preprocessor rejects gives a failure", broken[1].failure is not None)
    retried = list(Store(database, root).units())
    expect("a rejected unit is tried again", retried[1].failure is not None and not retried[1].from_cache)
    partial: list[str] = []
    refused = ""
    try:
        for path, _text in expanded_files(Store(database, root)):
            partial.append(path)
    except PreprocessError as exc:
        refused = str(exc)
    expect("expanded_files gives the readable units, then refuses the run on a rejected one",
           "a.cpp" in partial and "broken.cpp" in refused)

    write_db(root, ["a.cpp"])
    store = Store(database, root)
    plan = store._plan(json.loads(database.read_text())[0])
    for manifest in (store.directory / "units").rglob(f"*{MANIFEST_SUFFIX}"):
        manifest.write_bytes(b"{not marshal")
    damaged = list(Store(database, root).units())
    expect("a damaged manifest counts as a miss", not damaged[0].from_cache and damaged[0].failure is None)
    victim = damaged[0].chunks[0].digest
    (store.directory / "chunks" / victim[:2] / victim).unlink()
    lost = list(Store(database, root).units())
    expect("a manifest that names a lost chunk counts as a miss, and the run makes the chunk again",
           not lost[0].from_cache and (store.directory / "chunks" / victim[:2] / victim).is_file())
    (store.directory / "chunks" / victim[:2] / victim).write_bytes(zlib.compress(b"other text"))
    reader = Store(database, root)
    raised = False
    try:
        reader.text(victim)
    except DamagedStore:
        raised = True
    remade = list(Store(database, root).units())
    expect("a damaged chunk raises, goes away, and the next run makes it again",
           raised and not remade[0].from_cache and Store(database, root).text(victim))
    expect("the plan of a unit names two manifests", plan.shared_key != plan.rooted_key)
    retagged = _manifest_path(store_dir, plan.shared_key)
    _tag, kept_variants = marshal.loads(retagged.read_bytes())
    retagged.write_bytes(marshal.dumps((("crucible-preprocessed", STORE_VERSION - 1, marshal.version),
                                        kept_variants)))
    expect("a manifest of another version counts as a miss", not list(Store(database, root).units())[0].from_cache)

    results = Store(database, root).results("probe", Path(__file__), "parser-1")
    results.put("k" * 64, {"rows": [1, 2]})
    expect("a result reads back under the same code", results.get("k" * 64) == {"rows": [1, 2]})
    other = Store(database, root).results("probe", Path(__file__), "parser-2")
    expect("another parser identity gives other results", other.get("k" * 64) is None)
    (results.directory / "kk" / f"{'k' * 64}.json").write_text("{")
    expect("a damaged result counts as a miss", results.get("k" * 64) is None)

    old = time.time() - 2 * cache_dir.MAX_AGE
    write_db(root, ["b.cpp"])
    list(Store(database, root).units())
    kept_store = Store(database, root)
    kept = [kept_store._plan(entry) for entry in json.loads(database.read_text())]
    for manifest in (store_dir / "units").rglob(f"*{MANIFEST_SUFFIX}"):
        if manifest.stem not in {kept[0].shared_key, kept[0].rooted_key}:
            os.utime(manifest, (old, old))
    results.put("r" * 64, [1])
    os.utime(results.directory / "rr" / f"{'r' * 64}.json", (old, old))
    named_before = len(chunk_files())
    garbage = [store_dir / "units" / "ab" / "abandoned.json", store_dir / "units" / "ab" / "orphan.lock"]
    for path in garbage:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("{}")

    def garbage_count() -> int:
        """Return the number of files under units/ that are not a manifest or the lock of one."""
        names = {path.name for path in (store_dir / "units").rglob("*") if path.is_file()}
        return sum(1 for name in names if not name.endswith(MANIFEST_SUFFIX)
                   and not (name.endswith(".lock") and f"{name[:-len('.lock')]}{MANIFEST_SUFFIX}" in names))

    before = garbage_count()
    expect("an eviction with a small garbage limit removes part of the garbage and says that some remains",
           before >= 2 and not evict_store(store_dir, STORE_BYTES, garbage_limit=1) and garbage_count() == before - 1)
    expect("the next eviction removes the rest of the garbage", evict_store(store_dir, STORE_BYTES)
           and not any(p.exists() for p in garbage))
    left = list(Store(database, root).units())
    expect("an eviction removes old manifests, old results and their chunks, and keeps the used ones",
           left[0].from_cache and len(chunk_files()) < named_before and results.get("r" * 64) is None)

    # The stores above still hold their shared locks, so the turn cases use
    # a store of their own.
    with cache_dir.scratch_root() as turn_caches:
        list(Store(database, root).units())
        stamp = turn_caches / STORE_CACHE / "evicted"
        stale = time.time() - 2 * cache_dir.EVICT_INTERVAL
        os.utime(stamp, (stale, stale))
        with open(turn_caches / STORE_CACHE / "store.lock", "a") as reader_lock:
            fcntl.flock(reader_lock, fcntl.LOCK_SH)
            list(Store(database, root).units())
        expect("a turn that cannot get the store from another reader stays due",
               abs(stamp.stat().st_mtime - stale) < 1.0)
        list(Store(database, root).units())
        expect("the next run with no other reader takes the turn", time.time() - stamp.stat().st_mtime < 60)
    evict_store(store_dir, 1)
    expect("a size limit removes manifests until the store fits",
           not list((store_dir / "units").rglob(f"*{MANIFEST_SUFFIX}")))

    saved_root = os.environ[cache_dir.ROOT_VARIABLE]
    os.environ[cache_dir.ROOT_VARIABLE] = "off"
    try:
        private = Store(database, root)
        off = list(private.units())
        expect("with the caches off the store works in a private directory",
               off[0].failure is None and not off[0].from_cache and store_dir not in private.directory.parents)
    finally:
        os.environ[cache_dir.ROOT_VARIABLE] = saved_root

    for layer in ("lower", "upper"):
        (root / "include" / layer).mkdir(parents=True)
        (root / "include" / layer / f"{layer}.h").write_text(f"#pragma once\nint {layer}_name;\n")
    stage = root / "build" / "stage"
    stage.mkdir()
    (stage / "lower").symlink_to(root / "include" / "lower")
    real = root / "build" / "real"
    (real / "lower").mkdir(parents=True)
    (real / "lower" / "lower.h").write_text("#pragma once\nint lower_copy;\n")
    (root / "fixture.cpp").write_text("#include <lower/lower.h>\n#include <upper/upper.h>\n")
    database.write_text(json.dumps([
        {"directory": str(root), "file": "fixture.cpp",
         "command": f"{compiler} -std=c++20 -Ibuild/stage -c fixture.cpp -o fixture.o"},
        {"directory": str(root), "file": "fixture.cpp",
         "command": f"{compiler} -std=c++20 -I build/real -c fixture.cpp -o fixture.o"}]))
    staged, plain_unit = list(Store(database, root).units())
    expect("a staged layer root reads with the include directory, so a layer fixture is read",
           staged.failure is None and any(c.path == "include/upper/upper.h" for c in staged.chunks))
    expect("a directory that holds a file is not a staged root, and keeps its include path",
           plain_unit.failure is not None)


def fill_main(compile_db: Path, shard: int, count: int) -> int:
    """Fill one shard of the store for a compile database, and print one line about it.

    A unit that the preprocessor rejects gets no variant, so each guard that
    reads it tries it again and reports it.  The fill itself does not fail.

    Returns:
        0, or 2 when the arguments or the database are wrong
    """
    if not compile_db.is_file() or not 0 <= shard < count:
        print(f"preprocessed --fill: {compile_db} does not exist, or shard {shard} is not in 0 to {count - 1}",
              file=sys.stderr)
        return 2
    begin = time.monotonic()
    store = Store(compile_db, REPO_ROOT)
    units = store.fill_shard(shard, count)
    print(f"preprocessed --fill: shard {shard} of {count}: {len(units)} unit(s), {store.cached_count} from the "
          f"store, {store.runs} preprocessed, {len(store.failures)} rejected, in {time.monotonic() - begin:.1f} s")
    return 0


if __name__ == "__main__":
    arguments = sys.argv[1:]
    if arguments == ["--self-test"]:
        sys.exit(self_test())
    if len(arguments) == 6 and arguments[0] == "--fill" and arguments[2] == "--shard" and arguments[4] == "--of" \
            and arguments[3].isdigit() and arguments[5].isdigit():
        sys.exit(fill_main(Path(arguments[1]).resolve(), int(arguments[3]), int(arguments[5])))
    print("usage: preprocessed.py --self-test | --fill COMPILE_DB --shard K --of N", file=sys.stderr)
    sys.exit(2)
