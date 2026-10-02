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
        makes each __FILE__ relative to the root.  The command has no plugin
        flag and no -fdebug-prefix-map (THE FLAGS THAT THE PASS DROPS)
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

THE FLAGS THAT THE PASS DROPS
    A flag of these two families holds the root of the work tree, or the
    stamp that changes with each change of the plugin sources.  The output of
    -E does not depend on such a flag.  So the run and the name have no such
    flag, and two work trees and two versions of a plugin share the name of
    a unit.
      - The plugin flags (-fplugin=, -fplugin-arg-).  A plugin of the tree
        registers its pragmas without macro expansion, and GCC registers no
        such pragma under -E.  The run loads no plugin, so its output cannot
        depend on a plugin.
      - -fdebug-prefix-map, which changes the paths in the debug information
        only.

THE SETTLE PERIOD
    A variant records each file as the preprocessor read it, so a file that
    changes during the run must not get a variant.  After the run, the store
    reads the change time of each file that the unit read.  When one changed
    in the SETTLE_NS before the start of the run or later, or when one
    changed while the store hashed it, the unit gets no variant, and this
    run uses its output only.  A file system with coarse timestamps gives a
    change a time up to one tick early, and the period is much longer than a
    tick.  The result store of the negative fixtures uses the same period.
    A store can take a latest start time for its runs.  A run then takes the
    earlier of that time and the clock, so the bound can only make the store
    refuse more variants.  The self-test uses it.

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
    No reader waits for an eviction, and no eviction waits for a reader
    (THE GENERATIONS).

THE BOUND
    The store stays under its row of cache_dir.LIMITS.  The eviction turn is
    due each day, and also when a sample of the store is over the limit
    (utils/scripts/cache_dir.py, THE TURN).  An eviction first removes the
    garbage: each file under units/ that is not a manifest of this version,
    and each lock with no manifest.  Then it removes each manifest, result
    and chunk with no use for cache_dir.MAX_AGE.  Then the entries with the
    oldest use go until the store is under cache_dir.EVICT_TO of its limit.
    A turn takes at most EVICT_SECONDS.  A turn that leaves work keeps the
    turn due, so the next run continues the work.

    The mtime of each entry is the time of its last use.  A lookup that
    touches a manifest also touches each chunk of its variant, and a fill
    touches each chunk that it finds in the store.  So the chunks of a used
    manifest are not older than the manifest.  A lock with no manifest and a
    temporary file are garbage only after cache_dir.STALE_AFTER with no
    change, because a fill in progress can hold them.

THE GENERATIONS
    A manifest and a result are read whole, so no reader needs one after it
    read it, and an eviction removes them at once.  A reader can read a chunk
    long after its lookup.  So an eviction moves each chunk that it removes
    to retired/G/, and it deletes retired/G/ only when no reader that can
    need those chunks is alive:
      - Each reader holds a shared lock of generation.lock for its whole run.
      - An eviction that moves chunks first links generation.lock to
        retired/G.lock.  Then it moves the chunks.  Then it puts a new file
        at generation.lock, so each later reader locks a new generation.
      - An eviction deletes retired/G/ and retired/G.lock only after it got
        the exclusive lock of retired/G.lock at once, and after it deleted
        each older generation.  So the oldest live reader keeps its own
        generation and each later one.
      - A reader that does not find a chunk under chunks/ reads it from
        retired/, the newest generation first.  A lookup reads only chunks/,
        so a reader of a later generation never needs a retired chunk.

THE WORK TREES OF OTHER VERSIONS
    Each work tree runs its own copy of this module against the one store.
    A copy that keeps no generation lock holds a shared lock of store.lock
    while it reads.  It removes chunks only under the exclusive lock of
    store.lock, and only when the stamp `evicted` is one day old.  So:
      - An eviction moves chunks only while it holds the exclusive lock of
        store.lock, and it does not wait for that lock.
      - Each reader keeps the stamp `evicted` younger than one day, so such
        a copy never takes an eviction turn.  The turn of this module has
        the stamp TURN_STAMP.

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
# The name of the store in utils/scripts/cache_dir.py.  Its row of
# cache_dir.LIMITS is the size limit of the store.
STORE_CACHE = "preprocessed"
# The lock that each reader holds, shared, for its whole run (THE GENERATIONS).
GENERATION_LOCK = "generation.lock"
# The directory of the retired chunks and of the locks of their generations.
RETIRED = "retired"
# The lock and the stamp of a copy of this module that keeps no generation
# lock (THE WORK TREES OF OTHER VERSIONS).
SHARED_LOCK = "store.lock"
SHARED_STAMP = "evicted"
# The stamp of the eviction turn of this module.
TURN_STAMP = "turn"
# The longest time of one eviction turn, in seconds.  A turn that leaves work keeps the turn due.
EVICT_SECONDS = 15.0
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
# A unit gets no variant when a file that it read changed in this period
# before the start of the preprocessor run, or later (THE SETTLE PERIOD).
SETTLE_NS = 1_000_000_000

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
    """What a worker needs to fill the manifest of one unit.

    ``latest_start_ns`` is the latest start time of the preprocessor run, in
    nanoseconds since the epoch, or None for the clock (THE SETTLE PERIOD).
    """

    file: str
    run_argv: tuple[str, ...]
    directory: str
    shared_key: str
    rooted_key: str
    store: str
    root: str
    latest_start_ns: int | None = None


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
    """Return the compile command with -E, and without its output, dependency-file, plugin and debug-map flags.

    The module text (THE FLAGS THAT THE PASS DROPS) tells why the output of
    -E does not depend on a plugin flag or on -fdebug-prefix-map.

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
# set, of the plugin flags and of the debug maps.
_DROPPED_FLAGS = frozenset({"-c", "-MD", "-MMD", "-MP"})
_DROPPED_WITH_VALUE = frozenset({"-o", "-MF", "-MT", "-MQ"})
_DROPPED_PREFIXES = ("-o", "-MF", "-MT", "-MQ", "-fplugin=", "-fplugin-arg-", "-fdebug-prefix-map=")


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
        self.verified: dict[str, tuple[tuple[int, int, int, int], bytes]] = {}
        self.lock = threading.Lock()

    def verified_digest(self, name: str, settled_ns: int) -> bytes | None:
        """Return the raw SHA-256 of a file that did not change since settled_ns, or None.

        The file must have a change time before settled_ns, and the same stat
        fields before and after the hash.  The memory keeps the digest of each
        such file with its stat fields.  A file with a change time before
        settled_ns that changes again gets a later change time, so equal
        fields mean equal contents.

        Args:
            name: The variant name of the file
            settled_ns: The latest change time, in nanoseconds, that a file can have

        Returns:
            The digest, or None when the file changed after settled_ns, changed
            during the hash, or cannot be read
        """
        path = name if name.startswith("/") else self.prefix + name
        try:
            before = os.stat(path)
        except OSError:
            return None
        if before.st_ctime_ns >= settled_ns:
            return None
        fields = (before.st_ino, before.st_size, before.st_mtime_ns, before.st_ctime_ns)
        known = self.verified.get(name)
        if known is not None and known[0] == fields:
            return known[1]
        try:
            with open(path, "rb") as stream:
                found = hashlib.file_digest(stream, "sha256").digest()
            after = os.stat(path)
        except OSError:
            return None
        if (after.st_ino, after.st_size, after.st_mtime_ns, after.st_ctime_ns) != fields:
            return None
        with self.lock:
            self.verified[name] = (fields, found)
        return found

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
        self.touched: set[str] = set()
        self.lock = threading.Lock()

    def touch(self, digest: str) -> None:
        """Give a chunk that this process uses the time of its use, one time for each process (THE BOUND)."""
        with self.lock:
            if digest in self.touched:
                return
            self.touched.add(digest)
        with contextlib.suppress(OSError):
            os.utime(self.directory / digest[:2] / digest)

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

    def touch_variant(self, variant: Variant) -> None:
        """Give each chunk of a variant and its blob of outside files the time of a use (THE BOUND)."""
        self.chunks.touch(variant[2])
        for record in variant[3]:
            digests = record[3]
            for at in range(0, len(digests), 32):
                self.chunks.touch(digests[at:at + 32].hex())

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


def _retired_path(store: Path, digest: str) -> Path | None:
    """Return the path of a retired chunk, from the newest generation that holds it, or None (THE GENERATIONS)."""
    try:
        generations = sorted((name for name in os.listdir(store / RETIRED) if name.isdigit()), key=int, reverse=True)
    except FileNotFoundError:
        return None
    for generation in generations:
        path = store / RETIRED / generation / digest
        if path.is_file():
            return path
    return None


def blob_bytes(store: Path, digest: str) -> bytes:
    """Return the bytes of one chunk or blob of a store, from chunks/ or else from a retired generation.

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
        raw = path.read_bytes()
    except FileNotFoundError:
        retired = _retired_path(store, digest)
        if retired is None:
            raise
        path = retired
        raw = path.read_bytes()
    try:
        data = zlib.decompress(raw)
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
    """Store bytes under their hash, or touch the chunk when the store holds them, and return the hash."""
    digest = hashlib.sha256(data).hexdigest()
    if chunks.has(digest):
        chunks.touch(digest)
    else:
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
    one wrote.  A unit that read a file that changed in the settle period
    gets no variant (THE SETTLE PERIOD).

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
        started_ns = time.time_ns()
        if plan.latest_start_ns is not None:
            started_ns = min(started_ns, plan.latest_start_ns)
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
        files = reader.files
        inside = tuple(files.name_of(name) for name in names if name.startswith(files.prefix))
        outside = tuple(name for name in names if not name.startswith(files.prefix))
        settled_ns = started_ns - SETTLE_NS
        inside_digests = [files.verified_digest(name, settled_ns) for name in inside]
        outside_digests = [files.verified_digest(name, settled_ns) for name in outside]
        if None in inside_digests or None in outside_digests:
            return Unit(plan.file, records, None, False, inside), True
        for record in records:
            reader.present[record[3]] = True
        blob = _write_blob(store, marshal.dumps((outside, b"".join(outside_digests))), reader.chunks)
        reader.outside[blob] = True
        variant: Variant = (inside, b"".join(inside_digests), blob, records)
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

    def __init__(self, compile_db: Path, root: Path, jobs: int = 0, latest_start_ns: int | None = None) -> None:
        """Open the store for a compile database.

        Args:
            compile_db: The compile_commands.json file
            root: The repository root, whose files the store keeps
            jobs: The number of parallel preprocessor runs, or 0 for the
                number of processors, at most 16
            latest_start_ns: The latest start time of a preprocessor run, in
                nanoseconds since the epoch, or None for the clock (THE
                SETTLE PERIOD)
        """
        self.compile_db = compile_db
        self.latest_start_ns = latest_start_ns
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
        self._generation_lock = self._hold_generation()
        self._keep_shared_stamp()
        # The number of preprocessor runs this store started, for the self-test.
        self.runs = 0
        # The units of the last pass, and how many of them came from the store.
        self.unit_count = 0
        self.cached_count = 0
        # The units of the last pass that failed, and their failures, one line each.
        self.failed: list[Unit] = []
        self.failures: list[str] = []

    def _hold_generation(self):
        """Take the shared lock of the current generation, which keeps each chunk that this process can read (THE GENERATIONS)."""
        holder = open(self.directory / GENERATION_LOCK, "a")
        fcntl.flock(holder, fcntl.LOCK_SH)
        return holder

    def _keep_shared_stamp(self) -> None:
        """Keep the stamp SHARED_STAMP younger than one day (THE WORK TREES OF OTHER VERSIONS)."""
        stamp = self.directory / SHARED_STAMP
        try:
            cache_dir.mark_used(stamp, stamp.stat().st_mtime)
        except FileNotFoundError:
            stamp.touch()

    def close(self) -> None:
        """Release the generation lock, so an eviction can delete the generations that this process kept.

        The store reads no chunk after this call.
        """
        self._generation_lock.close()

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
                    hashlib.sha256(identity + b"\0" + root.encode()).hexdigest(), str(self.directory), root,
                    self.latest_start_ns)

    def _lookup(self, plan: Plan) -> Unit | None:
        """Return the unit of a plan from a valid variant, or None when the unit misses."""
        for key in (plan.shared_key, plan.rooted_key):
            manifest = _manifest_path(self.directory, key)
            variant = self._reader.valid_variant(_read_variants(manifest))
            if variant is not None:
                with contextlib.suppress(OSError):
                    if cache_dir.mark_used(manifest, manifest.stat().st_mtime):
                        self._reader.touch_variant(variant)
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
        """Hold the store under its limits when its turn is due (THE BOUND).

        A turn that leaves work gives the stamp its last time again, so the
        turn stays due for the next run.
        """
        stamp = self.directory / TURN_STAMP
        try:
            last = stamp.stat().st_mtime
        except FileNotFoundError:
            last = None
        limit = cache_dir.LIMITS[STORE_CACHE]
        with cache_dir.eviction_turn(self.directory, max_bytes=limit, stamp_name=TURN_STAMP) as has_turn:
            if not has_turn:
                return
            is_done = False
            try:
                is_done = evict_store(self.directory, limit)
            finally:
                if not is_done:
                    if last is None:
                        stamp.unlink(missing_ok=True)
                    else:
                        with contextlib.suppress(OSError):
                            os.utime(stamp, (last, last))


def _remove_garbage(directory: Path, moment: float, deadline: float) -> bool:
    """Remove the garbage of a store until a deadline, and return whether none remains (THE BOUND).

    The garbage is each file under units/ that is not a manifest of this
    version or the lock of one, and each temporary file of the store.  A lock
    with no manifest, a temporary file and an empty directory of the results
    go only after cache_dir.STALE_AFTER with no change.  The function looks
    at the clock after each directory of units/ where it removed a file.

    Complexity: one listing of each directory of the store, and one stat of
    each lock with no manifest and of each temporary file.

    Args:
        directory: The directory of the store
        moment: The time of the eviction
        deadline: The value of time.monotonic() at which the function stops

    Returns:
        Whether no garbage remains
    """
    expired = moment - cache_dir.STALE_AFTER
    with contextlib.suppress(FileNotFoundError):
        for fan in os.scandir(directory / "units"):
            if not fan.is_dir(follow_symlinks=False):
                continue
            try:
                with os.scandir(fan.path) as listing:
                    names = {item.name for item in listing}
            except FileNotFoundError:
                continue
            removed = 0
            for name in names:
                is_lock = name.endswith(".lock")
                if name.endswith(MANIFEST_SUFFIX) or (is_lock and f"{name[:-5]}{MANIFEST_SUFFIX}" in names):
                    continue
                path = os.path.join(fan.path, name)
                with contextlib.suppress(FileNotFoundError):
                    if (is_lock or name.startswith(".")) and os.stat(path).st_mtime >= expired:
                        continue
                    os.unlink(path)
                    removed += 1
            if removed and time.monotonic() >= deadline:
                return False
    cache_dir.remove_stale(directory / "chunks", moment)
    results = directory / "results"
    with contextlib.suppress(FileNotFoundError):
        for folder in os.scandir(results):
            if folder.is_dir(follow_symlinks=False):
                cache_dir.remove_stale(Path(folder.path), moment)
    cache_dir.remove_stale(results, moment)
    return True


def _finalize_retired(directory: Path) -> None:
    """Delete each retired generation that no live reader keeps, the oldest first (THE GENERATIONS).

    The function stops at the first generation whose lock a reader holds,
    because that reader can need each later generation too.
    """
    retired = directory / RETIRED
    try:
        names = os.listdir(retired)
    except FileNotFoundError:
        return
    for generation in sorted({int(name.split(".")[0]) for name in names if name.split(".")[0].isdigit()}):
        lock = retired / f"{generation}.lock"
        if not lock.exists():
            shutil.rmtree(retired / str(generation), ignore_errors=True)
            continue
        with open(lock, "a") as holder:
            try:
                fcntl.flock(holder, fcntl.LOCK_EX | fcntl.LOCK_NB)
            except BlockingIOError:
                return
            shutil.rmtree(retired / str(generation), ignore_errors=True)
            lock.unlink(missing_ok=True)


def _retire(directory: Path, chunks: list[cache_dir.Entry]) -> bool:
    """Move chunks to a new retired generation, and give the next readers a new generation lock (THE GENERATIONS).

    The move holds the exclusive lock of SHARED_LOCK, which the function
    takes at once or not at all (THE WORK TREES OF OTHER VERSIONS).

    Args:
        directory: The directory of the store
        chunks: The chunks to retire

    Returns:
        Whether the function moved the chunks
    """
    with open(directory / SHARED_LOCK, "a") as shared:
        try:
            fcntl.flock(shared, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return False
        generation = str(time.time_ns())
        retired = directory / RETIRED
        retired.mkdir(exist_ok=True)
        current = directory / GENERATION_LOCK
        current.touch()
        os.link(current, retired / f"{generation}.lock")
        target = retired / generation
        target.mkdir()
        for entry in chunks:
            with contextlib.suppress(FileNotFoundError):
                os.rename(entry.path, target / entry.path.name)
        fresh = directory / f".{GENERATION_LOCK}.{os.getpid()}.tmp"
        fresh.touch()
        os.replace(fresh, current)
    return True


def evict_store(directory: Path, max_bytes: int, max_age: float = cache_dir.MAX_AGE,
                now: float | None = None, seconds: float = EVICT_SECONDS) -> bool:
    """Hold a store under its limits: remove the garbage, then the entries past the age limit, then the oldest (THE BOUND).

    A manifest and a result go at once.  A chunk goes to a retired
    generation, which a later eviction deletes when no reader keeps it (THE
    GENERATIONS).  The caller holds the eviction turn of the store.

    Complexity: one listing of each entry and a sort, O(n log n) for n entries.

    Args:
        directory: The directory of the store
        max_bytes: The size limit
        max_age: The age limit of an entry with no use
        now: The time of the eviction, or None for the clock
        seconds: The longest time of the eviction

    Returns:
        Whether the eviction left no work for the next turn
    """
    moment = time.time() if now is None else now
    deadline = time.monotonic() + seconds
    _finalize_retired(directory)
    if not _remove_garbage(directory, moment, deadline):
        return False
    owned = cache_dir.entries(directory / "units", MANIFEST_SUFFIX)
    with contextlib.suppress(FileNotFoundError):
        for folder in os.scandir(directory / "results"):
            if folder.is_dir(follow_symlinks=False):
                owned += cache_dir.entries(Path(folder.path), ".json")
    chunks = cache_dir.entries(directory / "chunks")
    chunk_ids = set(map(id, chunks))
    chosen = cache_dir.victims(owned + chunks, max_bytes, max_age, moment)
    retiring = [entry for entry in chosen if id(entry) in chunk_ids]
    removing = [entry for entry in chosen if id(entry) not in chunk_ids]
    is_done = not retiring or _retire(directory, retiring)
    for start in range(0, len(removing), 1024):
        if time.monotonic() >= deadline:
            return False
        cache_dir.remove(removing[start:start + 1024])
    return is_done


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
    (root / "watched.h").write_text("#pragma once\n#define WATCHED 1\n")
    (root / "watch.cpp").write_text('#include "watched.h"\nint watch_only;\n')
    (root / "saved.h").write_text("int seen_old;\n")
    (root / "saving.cpp").write_text('#include "saved.h"\n')
    (root / "plugged.h").write_text("int plugged_old;\n")
    (root / "plugged.cpp").write_text('#include "plugged.h"\nint plugged_unit;\n')
    for name in ("duo_a", "duo_b"):
        (root / f"{name}.cpp").write_text(f'#include "shared.h"\nint {name};\n')
    sets = {stage: [f"{stage}{index}.cpp" for index in range(PARALLEL_MISSES + 2)]
            for stage in ("many", "again", "sharded")}
    for stage, names in sets.items():
        for index, name in enumerate(names):
            (root / name).write_text(f'#include "shared.h"\nint {stage}_{index};\n')
    database = root / "build" / "compile_commands.json"
    database.parent.mkdir()
    # A second checkout of the same tree, made with the first.
    second = scratch / "second"
    shutil.copytree(root, second, ignore=shutil.ignore_patterns("build"))
    (second / "build").mkdir()
    # A unit gets a variant only when each file that it read is older than
    # the settle period, so the cases that read a stored variant start after it.
    time.sleep(SETTLE_NS / 1e9 + 0.1)

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
    shared_units = list(Store(write_db(second, ["a.cpp", "b.cpp"]), second).units())
    expect("a second work tree with the same files reads the variants of the first",
           [u.from_cache for u in shared_units] == [True, True]
           and [u.chunks for u in shared_units] == [u.chunks for u in cold])

    # The plugin flags and the debug maps of a work tree hold its root, and a
    # new stamp comes with each change of the plugin sources.  The plugin
    # file does not exist, so a run that loaded it would fail.
    def plugin_flags(base: Path, stamp: str) -> str:
        """Return the plugin flags and the debug maps that a build directory under ``base`` gives."""
        return (f"-fplugin={base}/build/quarantine/absent_plugin.so -fplugin-arg-absent_plugin-root={base} "
                f"-fplugin-arg-absent_plugin-build={base}/build -fplugin-arg-absent_plugin-stamp={stamp} "
                f"-fdebug-prefix-map={base}=.. -fdebug-prefix-map={base}/build=.")

    plugged_first = list(Store(write_db(root, ["plugged.cpp"], plugin_flags(root, "1")), root).units())
    expect("the preprocessor run loads no plugin, so a plugin that does not exist fails no unit",
           plugged_first[0].failure is None and not plugged_first[0].from_cache)
    plugged_store = Store(write_db(second, ["plugged.cpp"], plugin_flags(second, "2")), second)
    plugged_second = list(plugged_store.units())
    expect("a work tree with its own plugin flags, debug maps and stamp reads the variant of the first",
           plugged_second[0].from_cache and plugged_store.runs == 0
           and plugged_second[0].chunks == plugged_first[0].chunks)
    (second / "plugged.h").write_text("int plugged_new;\n")
    plugged_store = Store(write_db(second, ["plugged.cpp"], plugin_flags(second, "3")), second)
    plugged_changed = list(plugged_store.units())
    expect("with the plugin flags out of the name, a changed header still makes the unit stale",
           not plugged_changed[0].from_cache
           and "plugged_new" in joined(plugged_store, files_of(plugged_changed[0]).get("plugged.h", ("", []))[1]))
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

    write_db(root, ["watch.cpp", "b.cpp"])
    list(Store(database, root).units())
    (root / "watched.h").write_text("#pragma once\n#define WATCHED 2\n")
    changed = list(Store(database, root).units())
    expect("a changed header that adds no line makes its units stale",
           [u.from_cache for u in changed] == [False, True])
    (root / "watched.h").write_text("#pragma once\n#define WATCHED 1\n")
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

    # A file that changes during the run, and a file that changed in the
    # settle period before it, give the unit no variant.
    wrapper = scratch / "cxx-saves-a-header"
    wrapper.write_text(f'#!/bin/bash\n"{compiler}" "$@"\nstatus=$?\n'
                       f'case " $* " in *" -E "*) [ -e "{root}/saved" ] || '
                       f'{{ printf "int seen_new;\\n" > "{root}/saved.h"; : > "{root}/saved"; }} ;; esac\n'
                       f'exit $status\n')
    wrapper.chmod(0o755)
    database.write_text(json.dumps([{"directory": str(root), "file": "saving.cpp",
                                     "command": f"{wrapper} -c saving.cpp -o saving.o"}]))
    list(Store(database, root).units())
    saving_store = Store(database, root)
    saving = list(saving_store.units())
    expect("a file that changes during the run gives the unit no variant, so the next run reads the new text",
           not saving[0].from_cache and "seen_new" in joined(saving_store, files_of(saving[0])["saved.h"][1]))
    # The run takes the change time of the new header as its latest start,
    # so the change is in the settle period, also on a slow host.
    (root / "fresh.h").write_text("int fresh_value;\n")
    (root / "fresh.cpp").write_text('#include "fresh.h"\n')
    write_db(root, ["fresh.cpp"])
    list(Store(database, root, latest_start_ns=(root / "fresh.h").stat().st_ctime_ns).units())
    expect("a file that changed less than the settle period before the run gives the unit no variant",
           not list(Store(database, root).units())[0].from_cache)

    write_db(root, ["duo_a.cpp", "duo_b.cpp"])
    first, second_run = Store(database, root), Store(database, root)
    with ThreadPoolExecutor(max_workers=2) as pool:
        list(pool.map(lambda each: list(each.units()), (first, second_run)))
    expect("two guards that run at the same time preprocess each unit one time",
           first.runs + second_run.runs == 2)

    many = sets["many"]
    write_db(root, many)
    in_workers = Store(database, root, jobs=4)
    parallel = list(in_workers.units())
    inline = list(Store(database, root, jobs=1).units())
    expect("worker processes fill the units that miss, and a second run reads them",
           in_workers.runs == len(many) and all(u.failure is None for u in parallel)
           and [u.chunks for u in parallel] == [u.chunks for u in inline] and all(u.from_cache for u in inline))
    write_db(root, sets["again"])
    together = [Store(database, root, jobs=3), Store(database, root, jobs=3)]
    with ThreadPoolExecutor(max_workers=2) as pool:
        both = list(pool.map(lambda each: list(each.units()), together))
    expect("two guards with worker processes that start cold together preprocess each unit one time",
           sum(each.runs for each in together) == len(sets["again"])
           and [unit.chunks for unit in both[0]] == [unit.chunks for unit in both[1]]
           and all(unit.failure is None for unit in both[0]))
    write_db(root, sets["sharded"])
    fillers = [Store(database, root, jobs=2) for _shard in range(3)]
    shards = [filler.fill_shard(shard, 3) for shard, filler in enumerate(fillers)]
    after = Store(database, root)
    reread = list(after.units())
    expect("the shards of a database are disjoint, hold each entry, and fill the store for a guard after them",
           sorted(unit.file for part in shards for unit in part) == sorted(sets["sharded"])
           and sum(filler.runs for filler in fillers) == len(sets["sharded"]) and after.runs == 0
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
    limit = cache_dir.LIMITS[STORE_CACHE]
    write_db(root, ["b.cpp"])
    list(Store(database, root).units())
    # Each manifest and chunk gets an old use.  The next lookup of b.cpp
    # touches its manifest and each chunk of its variant.
    for path in [*(store_dir / "units").rglob(f"*{MANIFEST_SUFFIX}"), *chunk_files()]:
        os.utime(path, (old, old))
    results.put("r" * 64, [1])
    os.utime(results.directory / "rr" / f"{'r' * 64}.json", (old, old))
    used = list(Store(database, root).units())
    named_before = len(chunk_files())
    garbage = [store_dir / "units" / fan / f"{name}-{fan}{suffix}" for fan in ("ab", "cd")
               for name, suffix in (("abandoned", ".json"), ("orphan", ".lock"))]
    young_lock = store_dir / "units" / "ef" / "filling.lock"
    for path in [*garbage, young_lock]:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text("{}")
        if path != young_lock:
            os.utime(path, (old, old))

    def garbage_count() -> int:
        """Return the number of files under units/ that are not a manifest or the lock of one."""
        names = {path.name for path in (store_dir / "units").rglob("*") if path.is_file()}
        return sum(1 for name in names if not name.endswith(MANIFEST_SUFFIX)
                   and not (name.endswith(".lock") and f"{name[:-len('.lock')]}{MANIFEST_SUFFIX}" in names))

    before = garbage_count()
    expect("an eviction past its deadline removes the garbage of one directory and says that some remains",
           used[0].from_cache and not evict_store(store_dir, limit, seconds=0) and garbage_count() == before - 2)
    expect("the next eviction removes the rest of the garbage, and keeps a young lock that a fill can hold",
           evict_store(store_dir, limit) and not any(path.exists() for path in garbage) and young_lock.exists())
    left = list(Store(database, root).units())
    expect("an eviction removes old manifests, old results and old chunks, and keeps the ones that a lookup used",
           left[0].from_cache and len(chunk_files()) < named_before and results.get("r" * 64) is None)

    # Each store above holds its generation, so these cases use a store of their own.
    with cache_dir.scratch_root() as turn_caches:
        _self_test_turns(expect, turn_caches / STORE_CACHE, root, write_db)

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


def _self_test_turns(expect, store_dir: Path, root: Path, write_db) -> None:
    """Run the cases of the eviction turn and of the generations against a store that no other case holds.

    Args:
        expect: The recorder of the self-test
        store_dir: The directory of the store, under a scratch root of the caches
        root: The scratch tree of the sources
        write_db: Writes the compile database of the named sources of a tree, and returns its path
    """
    database = root / "build" / "compile_commands.json"

    def files_under(part: str) -> list[Path]:
        """Return each file under one directory of the store whose name does not start with a dot."""
        return [path for path in (store_dir / part).rglob("*") if path.is_file() and not path.name.startswith(".")]

    def plant_garbage(count: int) -> list[Path]:
        """Plant manifests of an earlier format and locks with no manifest, each with an old change, and return them."""
        planted: list[Path] = []
        stale = time.time() - 2 * cache_dir.STALE_AFTER
        for index in range(count):
            key = hashlib.sha256(f"garbage-{index}-{time.time_ns()}".encode()).hexdigest()
            for name, data in ((f"{key}.json", bytes(8192)), (f"{key}.lock", b"")):
                path = store_dir / "units" / key[:2] / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(data)
                os.utime(path, (stale, stale))
                planted.append(path)
        return planted

    def turn_with_limit(small: int, store: Store | None = None) -> None:
        """Run the eviction turn of one lookup pass with a small size limit of the store."""
        saved = cache_dir.LIMITS[STORE_CACHE]
        cache_dir.LIMITS[STORE_CACHE] = small
        try:
            list((store if store is not None else Store(database, root)).units())
        finally:
            cache_dir.LIMITS[STORE_CACHE] = saved

    (root / "unsettled.h").write_text("int unsettled_value;\n")
    (root / "unsettled.cpp").write_text('#include "unsettled.h"\n')
    write_db(root, ["unsettled.cpp"])
    # The change time of the new header is the latest start, so the unit gets no variant.
    evicting = Store(database, root, latest_start_ns=(root / "unsettled.h").stat().st_ctime_ns)
    unsettled = list(evicting.units())
    evict_store(store_dir, 1)
    try:
        readable = "unsettled_value" in joined(evicting, files_of(unsettled[0])["unsettled.h"][1])
    except FileNotFoundError:
        readable = False
    expect("an eviction keeps each chunk that a reader of its generation reads, also of a unit that got no variant",
           (store_dir / TURN_STAMP).is_file() and not unsettled[0].from_cache and readable and not files_under("chunks"))
    evicting.close()

    # Another reader is alive, and the store holds garbage over its limit.
    # One turn removes the garbage, and the other reader still reads its text.
    write_db(root, ["b.cpp"])
    other = Store(database, root)
    other_units = list(other.units())
    garbage = plant_garbage(64)
    small = 256 << 10
    over = cache_dir.estimated_bytes(store_dir)
    stale_turn = time.time() - 2 * cache_dir.EVICT_INTERVAL
    os.utime(store_dir / TURN_STAMP, (stale_turn, stale_turn))
    turn_with_limit(small)
    expect("one eviction turn with another reader alive removes the garbage and holds the store under its limit",
           over > 2 * small and not any(path.exists() for path in garbage)
           and cache_dir.estimated_bytes(store_dir) <= small
           and "struct Shared" in joined(other, files_of(other_units[0])["shared.h"][1]))

    # A recent turn, and a store that grows over its limit: the turn waits
    # for the next sample of the size, and then it is due.
    os.utime(store_dir / TURN_STAMP)
    (store_dir / f"{TURN_STAMP}.probed").touch()
    garbage = plant_garbage(64)
    turn_with_limit(small)
    expect("a store over its limit after a recent turn and a recent sample waits for the next sample",
           all(path.exists() for path in garbage))
    stale_probe = time.time() - 2 * cache_dir.PROBE_INTERVAL
    os.utime(store_dir / f"{TURN_STAMP}.probed", (stale_probe, stale_probe))
    turn_with_limit(small)
    expect("a store over its limit takes its turn at the next sample, also after a recent turn",
           not any(path.exists() for path in garbage) and cache_dir.estimated_bytes(store_dir) <= small)

    # The generations: a reader of an earlier generation reads the chunks
    # that an eviction retired, and a reader of a later one does not need them.
    write_db(root, ["a.cpp"])
    holder = Store(database, root)
    held = list(holder.units())
    evict_store(store_dir, 1)
    expect("a size limit removes each manifest and retires each chunk",
           not list((store_dir / "units").rglob(f"*{MANIFEST_SUFFIX}")) and not files_under("chunks"))
    expect("a reader that holds an earlier generation reads a chunk that an eviction retired",
           "struct Shared" in joined(holder, files_of(held[0])["shared.h"][1]))
    newer = Store(database, root)
    renewed = list(newer.units())
    expect("a reader of a later generation misses the retired chunks, and preprocesses its unit again",
           not renewed[0].from_cache and "struct Shared" in joined(newer, files_of(renewed[0])["shared.h"][1]))
    evict_store(store_dir, cache_dir.LIMITS[STORE_CACHE])
    expect("an eviction keeps each retired generation that a live reader holds", bool(files_under(RETIRED)))
    holder.close()
    other.close()
    evict_store(store_dir, cache_dir.LIMITS[STORE_CACHE])
    expect("an eviction deletes each retired generation after its readers closed", not files_under(RETIRED))
    newer.close()

    # A copy of this module that keeps no generation lock reads under a shared lock of store.lock.
    with open(store_dir / SHARED_LOCK, "a") as shared:
        fcntl.flock(shared, fcntl.LOCK_SH)
        is_done = evict_store(store_dir, 1)
        expect("an eviction retires no chunk while a reader holds store.lock, and says that work remains",
               not is_done and bool(files_under("chunks")) and not list((store_dir / "units").rglob(f"*{MANIFEST_SUFFIX}")))
    expect("the next eviction with no such reader retires the chunks",
           evict_store(store_dir, 1) and not files_under("chunks"))
    os.utime(store_dir / SHARED_STAMP, (stale_turn, stale_turn))
    Store(database, root).close()
    expect("each reader keeps the stamp of a copy that keeps no generation lock younger than one day",
           time.time() - (store_dir / SHARED_STAMP).stat().st_mtime < cache_dir.EVICT_INTERVAL)


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
