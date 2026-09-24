#!/usr/bin/env python3
"""One preprocessor run for each translation unit, shared by the guards that read preprocessed output.

scripts/check-proof-routes.py and scripts/check-start-lifetime.sh read the
output of the preprocessor over the compile database.  Each guard gets that
output from this module, so a translation unit is preprocessed one time for
the two guards.

THE STORE
    The store lives in preprocessed-cache/ beside the compile database.
    The module runs the compiler of each database entry with -E and -MD.
    It keeps only the part of the output that falls in files under the root.
    It divides that part at each line marker into chunks.  A chunk is the text
    of one file from one line on, and it is stored one time under the SHA-256
    of its text.  Many units include a header with the same macros, so the
    store holds that text one time.

    A unit manifest names the chunks of the unit in output order.  It also
    names each file under the root that the unit read, with the SHA-256 of
    the file.  The -MD dependency file gives that list, so a header that adds
    no line to the output still makes the manifest stale when it changes.
    A manifest is valid while its command, its directory, the compiler binary
    and each file it names are unchanged.  The key of a manifest is a hash of
    the first three items.

FAILURE
    A unit that the preprocessor rejects gives a failure and no manifest,
    so the next run tries it again.  A guard refuses its run on a failure,
    because a unit that it cannot read can hold a use.

STAGED LAYER ROOTS
    A directory on the include path whose entries are all links into the
    include directory of the repository is a staged layer root of
    test/layer.  The pass replaces it with the include directory, because a
    layer fixture exists to fail under its root, and what it holds is still
    worth reading.  A directory that holds anything else is left alone.

Complexity: a cold run is linear in the total size of the preprocessed
output.  A warm run reads one manifest for each unit and one hash for each
file under the root.
"""

from __future__ import annotations

import fcntl
import hashlib
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
import threading
import time
import zlib
from collections.abc import Iterator
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

# Bumped when the store reads or writes the output differently, so an older
# manifest is not reused.
STORE_VERSION = 1

# A line marker of the output: # line "file" flags.
MARKER = re.compile(rb'^# ([0-9]+) "((?:[^"\\]|\\.)*)"[^\n]*\n?', re.M)


@dataclass(frozen=True)
class Chunk:
    """The text of one file under the root from one line on, as it appears in one unit."""

    path: str
    line: int
    digest: str


@dataclass
class Unit:
    """One compile-database entry after the preprocessed pass."""

    file: str
    chunks: list[Chunk] = field(default_factory=list)
    failure: str | None = None
    from_cache: bool = False


def preprocess_argv(argv: list[str]) -> list[str]:
    """Return the compile command with -E, and without its output and dependency-file flags.

    Args:
        argv: The compile command of one database entry

    Returns:
        The command that preprocesses the same source with the same flags
    """
    out: list[str] = []
    index = 0
    while index < len(argv):
        flag = argv[index]
        if flag in ("-c", "-MD", "-MMD", "-MP"):
            index += 1
        elif flag in ("-o", "-MF", "-MT", "-MQ"):
            index += 2
        elif flag.startswith(("-o", "-MF", "-MT", "-MQ")):
            index += 1
        else:
            out.append(flag)
            index += 1
    return out + ["-E"]


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


def widen_staged_roots(argv: list[str], directory: str, root: Path) -> list[str]:
    """Replace each staged layer root on the include path with the include directory of the repository.

    The preprocessed pass reads what a unit holds.  A layer fixture exists to
    fail under its staged root, and a route in it is still worth reading, so
    the pass gives it the include path that every other unit gets.

    Args:
        argv: The compile command of one database entry
        directory: The directory the command runs in
        root: The repository root

    Returns:
        The command with each staged root replaced
    """
    include = root / "include"
    out: list[str] = []
    index = 0
    while index < len(argv):
        flag = argv[index]
        if flag == "-I" and index + 1 < len(argv):
            value, step = argv[index + 1], 2
        elif flag.startswith("-I") and len(flag) > 2:
            value, step = flag[2:], 1
        else:
            out.append(flag)
            index += 1
            continue
        target = Path(directory) / value
        out.append("-I" + (str(include) if is_staged_root(target, include) else value))
        index += step
    return out


def compiler_identity(argv: list[str], directory: str) -> list:
    """Return the resolved path, the size and the modification time of the compiler binary.

    Args:
        argv: The compile command of one database entry
        directory: The directory the command runs in

    Returns:
        The identity, or the bare name when the binary cannot be found
    """
    binary = argv[0] if os.path.isabs(argv[0]) else (
        os.path.join(directory, argv[0]) if os.sep in argv[0] else next(
            (os.path.join(d, argv[0]) for d in os.environ.get("PATH", "").split(os.pathsep)
             if os.path.isfile(os.path.join(d, argv[0]))), argv[0]))
    try:
        info = os.stat(os.path.realpath(binary))
        return [os.path.realpath(binary), info.st_size, info.st_mtime_ns]
    except OSError:
        return [binary]


class Store:
    """The shared preprocessed pass over one compile database."""

    def __init__(self, compile_db: Path, root: Path, jobs: int = 0) -> None:
        """Open the store beside a compile database.

        Args:
            compile_db: The compile_commands.json file
            root: The repository root, whose files the store keeps
            jobs: The number of parallel preprocessor runs, or 0 for the
                number of processors, at most 16
        """
        self.compile_db = compile_db
        self.root = root.resolve()
        self.prefix = str(self.root) + os.sep
        self.directory = compile_db.parent / "preprocessed-cache"
        (self.directory / "units").mkdir(parents=True, exist_ok=True)
        (self.directory / "chunks").mkdir(parents=True, exist_ok=True)
        self.jobs = jobs or min(16, os.cpu_count() or 1)
        self._hashes: dict[str, str | None] = {}
        self._texts: dict[str, str] = {}
        self._lock = threading.Lock()
        # The number of preprocessor runs this store started, for the self-test.
        self.runs = 0

    def file_hash(self, relative: str) -> str | None:
        """Return the SHA-256 of a file under the root, computed one time for each run.

        Args:
            relative: The path relative to the root

        Returns:
            The hash, or None when the file no longer exists
        """
        with self._lock:
            if relative in self._hashes:
                return self._hashes[relative]
        path = self.root / relative
        digest = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
        with self._lock:
            self._hashes[relative] = digest
        return digest

    def under_root(self, name: str, directory: str) -> str | None:
        """Return the root-relative path of a file name from the output, or None outside the root.

        Args:
            name: The file name, as a line marker or a dependency file spells it
            directory: The directory the command ran in

        Returns:
            The path relative to the root, or None
        """
        if name.startswith("<"):
            return None
        absolute = os.path.normpath(os.path.join(directory, name))
        return absolute[len(self.prefix):] if absolute.startswith(self.prefix) else None

    def text(self, digest: str) -> str:
        """Return the text of a chunk.

        Args:
            digest: The SHA-256 of the chunk text

        Returns:
            The text

        Raises:
            FileNotFoundError: If the store lost the chunk, which makes the
                caller's manifest unusable
        """
        with self._lock:
            known = self._texts.get(digest)
        if known is not None:
            return known
        data = zlib.decompress((self.directory / "chunks" / digest[:2] / digest).read_bytes()).decode()
        with self._lock:
            self._texts[digest] = data
        return data

    def _put(self, text: str) -> str:
        """Store a chunk text under its hash and return the hash."""
        digest = hashlib.sha256(text.encode()).hexdigest()
        target = self.directory / "chunks" / digest[:2] / digest
        if not target.is_file():
            target.parent.mkdir(exist_ok=True)
            staging = target.with_suffix(f".{os.getpid()}.{threading.get_ident()}.tmp")
            staging.write_bytes(zlib.compress(text.encode(), 6))
            os.replace(staging, target)
        with self._lock:
            self._texts.setdefault(digest, text)
        return digest

    def _chunks(self, output: bytes, directory: str) -> list[Chunk]:
        """Divide the output at its line markers and store each part that falls under the root."""
        found: list[Chunk] = []
        markers = list(MARKER.finditer(output))
        names: dict[bytes, str | None] = {}
        for index, marker in enumerate(markers):
            name = marker.group(2)
            if name not in names:
                spelled = name.decode(errors="replace").replace('\\"', '"').replace("\\\\", "\\")
                names[name] = self.under_root(spelled, directory)
            path = names[name]
            if path is None:
                continue
            end = markers[index + 1].start() if index + 1 < len(markers) else len(output)
            text = output[marker.end():end].decode(errors="replace")
            if text:
                found.append(Chunk(path, int(marker.group(1)), self._put(text)))
        return found

    def _dependencies(self, text: str, directory: str) -> set[str]:
        """Return the files under the root that a make-style dependency file names."""
        body = text.replace("\\\n", " ").split(":", 1)[-1]
        found: set[str] = set()
        for name in re.split(r"(?<!\\)\s+", body):
            if name and (path := self.under_root(name.replace("\\ ", " "), directory)) is not None:
                found.add(path)
        return found

    def _cached(self, manifest: Path) -> list[Chunk] | None:
        """Return the chunks of a valid manifest, or None when it is absent or stale."""
        if not manifest.is_file():
            return None
        try:
            cached = json.loads(manifest.read_text())
            if not all(self.file_hash(path) == digest for path, digest in cached["dependencies"].items()):
                return None
            chunks = [Chunk(*item) for item in cached["chunks"]]
        except (OSError, ValueError, KeyError, TypeError):
            return None
        if all((self.directory / "chunks" / c.digest[:2] / c.digest).is_file() for c in chunks):
            return chunks
        return None

    def _unit(self, entry: dict) -> Unit:
        """Read one entry from its valid manifest, or preprocess it and write the manifest.

        Two guards that run at the same time lock the manifest of a unit
        before they preprocess it, so the second one waits and then reads
        what the first one wrote.
        """
        argv = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
        directory = entry["directory"]
        run_argv = preprocess_argv(widen_staged_roots(argv, directory, self.root))
        identity = json.dumps([STORE_VERSION, run_argv, directory, compiler_identity(argv, directory)])
        manifest = self.directory / "units" / (hashlib.sha256(identity.encode()).hexdigest() + ".json")
        if (chunks := self._cached(manifest)) is not None:
            return Unit(entry["file"], chunks, None, True)
        with open(manifest.with_suffix(".lock"), "a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            if (chunks := self._cached(manifest)) is not None:
                return Unit(entry["file"], chunks, None, True)
            return self._preprocess(entry, run_argv, directory, manifest)

    def _preprocess(self, entry: dict, run_argv: list[str], directory: str, manifest: Path) -> Unit:
        """Run the preprocessor for one entry, store its chunks and write its manifest."""
        with self._lock:
            self.runs += 1
        descriptor, depfile = tempfile.mkstemp(prefix="preprocessed-", suffix=".d")
        os.close(descriptor)
        try:
            result = subprocess.run(run_argv + ["-MD", "-MF", depfile], cwd=directory, capture_output=True)
            dependencies = self._dependencies(Path(depfile).read_text(errors="replace"), directory)
        finally:
            os.unlink(depfile)
        if result.returncode != 0:
            first = result.stderr.decode(errors="replace").strip().splitlines()[:1]
            return Unit(entry["file"], [], f"{entry['file']}: {first[0] if first else 'exit ' + str(result.returncode)}")
        chunks = self._chunks(result.stdout, directory)
        record = {"chunks": [[c.path, c.line, c.digest] for c in chunks],
                  "dependencies": {path: self.file_hash(path) for path in sorted(dependencies)}}
        staging = manifest.with_suffix(f".{os.getpid()}.{threading.get_ident()}.tmp")
        staging.write_text(json.dumps(record))
        os.replace(staging, manifest)
        return Unit(entry["file"], chunks)

    def units(self) -> Iterator[Unit]:
        """Yield each database entry after the preprocessed pass, in database order.

        Yields:
            One Unit for each entry
        """
        entries = json.loads(self.compile_db.read_text())
        with ThreadPoolExecutor(max_workers=self.jobs) as pool:
            yield from pool.map(self._unit, entries)


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
    by_path: dict[str, list[Chunk]] = {}
    for chunk in unit.chunks:
        by_path.setdefault(chunk.path, []).append(chunk)
    return {path: (hashlib.sha256("\n".join(c.digest for c in chunks).encode()).hexdigest(), chunks)
            for path, chunks in by_path.items()}


def joined(store: Store, chunks: list[Chunk]) -> str:
    """Return the text of a file's chunks, joined in output order.

    Args:
        store: The store that holds the chunks
        chunks: The chunks of one file of one unit

    Returns:
        The joined text
    """
    return "\n".join(store.text(c.digest) for c in chunks)


def self_test() -> int:
    """Preprocess a scratch tree two times and prove the store's verdicts.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, ok: bool) -> None:
        """Record one case."""
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    compiler = os.environ.get("CXX") or next(
        (c for c in ("c++", "g++") if any(os.path.isfile(os.path.join(d, c))
                                          for d in os.environ.get("PATH", "").split(os.pathsep))), None)
    if compiler is None:
        print("preprocessed --self-test: no C++ compiler to run", file=sys.stderr)
        return 2
    with tempfile.TemporaryDirectory() as scratch:
        root = Path(scratch)
        (root / "shared.h").write_text("#pragma once\nstruct Shared { int a; };\n")
        (root / "quiet.h").write_text("#pragma once\n#define QUIET 1\n")
        (root / "a.cpp").write_text('#include "shared.h"\n#include "quiet.h"\nint a_only;\n')
        (root / "b.cpp").write_text('#include "shared.h"\nint b_only;\n')
        (root / "broken.cpp").write_text('#include "missing.h"\n')
        database = root / "build" / "compile_commands.json"
        database.parent.mkdir()

        def write_db(files: list[str]) -> None:
            """Write a compile database for the named sources."""
            database.write_text(json.dumps([{"directory": str(root), "file": name,
                                             "command": f"{compiler} -std=c++20 -c {name} -o {name}.o"}
                                            for name in files]))

        write_db(["a.cpp", "b.cpp"])
        cold = list(Store(database, root).units())
        expect("a cold run preprocesses each unit", [u.from_cache for u in cold] == [False, False])
        expect("a cold run has no failure", all(u.failure is None for u in cold))
        shared = {c.digest for u in cold for c in u.chunks if c.path == "shared.h"}
        expect("a header included the same way is stored one time", len(shared) == 1)
        stored = list((database.parent / "preprocessed-cache" / "chunks").rglob("*"))
        expect("the store holds each distinct chunk one time",
               len([p for p in stored if p.is_file()]) == len({c.digest for u in cold for c in u.chunks}))
        store = Store(database, root)
        files = files_of(cold[0])
        expect("the joined text of a file holds its code", "struct Shared" in joined(store, files["shared.h"][1]))
        expect("two units that expand a file the same way give one key",
               files["shared.h"][0] == files_of(cold[1])["shared.h"][0])
        lines = [c.line + store.text(c.digest).split("a_only")[0].count("\n")
                 for c in cold[0].chunks if c.path == "a.cpp" and "a_only" in store.text(c.digest)]
        expect("a chunk's line gives the source line of its text", lines == [3])
        warm = list(Store(database, root).units())
        expect("a warm run reads each manifest", [u.from_cache for u in warm] == [True, True])
        expect("a warm run gives the same chunks", [u.chunks for u in warm] == [u.chunks for u in cold])
        time.sleep(0.01)
        (root / "quiet.h").write_text("#pragma once\n#define QUIET 2\n")
        changed = list(Store(database, root).units())
        expect("a changed header that adds no line makes its units stale",
               [u.from_cache for u in changed] == [False, True])
        (root / "quiet.h").write_text("#pragma once\n#define QUIET 3\n")
        (root / "shared.h").write_text("#pragma once\nstruct Shared { int a; int b; };\n")
        first, second = Store(database, root), Store(database, root)
        with ThreadPoolExecutor(max_workers=2) as pool:
            list(pool.map(lambda store: list(store.units()), (first, second)))
        expect("two guards that run at the same time preprocess each unit one time",
               first.runs + second.runs == 2)
        write_db(["a.cpp", "broken.cpp"])
        broken = list(Store(database, root).units())
        expect("a unit the preprocessor rejects gives a failure", broken[1].failure is not None)
        again = list(Store(database, root).units())
        expect("a rejected unit is tried again", again[1].failure is not None and not again[1].from_cache)
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
        staged, plain = list(Store(database, root).units())
        expect("a staged layer root reads with the include directory, so a layer fixture is read",
               staged.failure is None and any(c.path == "include/upper/upper.h" for c in staged.chunks))
        expect("a directory that holds a file is not a staged root, and keeps its include path",
               plain.failure is not None)
    if failures:
        print(f"preprocessed --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print("preprocessed --self-test: every case passes.")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] != ["--self-test"]:
        print("usage: preprocessed.py --self-test", file=sys.stderr)
        sys.exit(2)
    sys.exit(self_test())
