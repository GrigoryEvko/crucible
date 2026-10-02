#!/usr/bin/env python3
"""The result store of the units of the session-oracle self-test.

THE UNITS
    The self-test builds two kinds of unit.  A static unit is one source
    file.  Its result is the exit code and the output of a compile with
    -fsyntax-only.  A program is the source files of one runtime test.  Its
    result is the first compile of a source that fails, else the link when it
    fails, else the run of the program.  A result holds the exit code, the
    standard output and the standard error.  The caller judges the result,
    so the store keeps no verdict.

THE STORE
    The store is the "session_oracle" cache of utils/scripts/cache_dir.py.
    It is a result store of test/neg_compile_store.py, with the key, the
    entry format, the check of the inputs, the settle period and the
    eviction of that file.  The module text of that file tells why a stored
    result of a compile is the result of the compile.  A memo of the hash
    of each dependency, with its stat fields, serves the lookups of one
    caller (DIGEST_MEMO_NAME).  When the caches are off, each unit builds
    again.

THE SOURCES
    The command of a unit names its sources, and the key holds the command.
    So each unit writes its sources to sources/DIGEST/ in the store, where
    DIGEST is the SHA-256 of the names and the texts of the sources.  Before
    each lookup, the unit writes each source whose bytes are not its text.
    So the key names the bytes of each source, and a source is not a
    dependency of a result.  A new source then does not wait for the settle
    period.  An include of a source looks in the directory of the source
    first.  So a lookup and a store make sure that the directory holds only
    the sources and temporary files, whose names start with a dot and which
    no include names.  A run gives each directory that it uses a new time
    when the last one is older than cache_dir.TOUCH_INTERVAL.  One run each
    day removes each directory with no use for cache_dir.MAX_AGE.

THE KEY OF A STATIC UNIT
    compile_key of the store hashes the identity of the compiler, the
    command, the working directory, the environment of the compiler and the
    bytes of the store module.  The key of the unit hashes that hash with
    FORMAT and the bytes of this module.  The inputs of a result are the
    inputs of the store: the bytes of each file that the compile read, the
    names under each search root, and the state of each probed path.  The
    sources are not inputs, because the key names their bytes.

THE KEY OF A PROGRAM
    The key hashes the compile key of each source, the link and the run:
      - The link command, with a placeholder for each object file and for
        the program.  The path and the bytes of collect2, of the linker, of
        the LTO plugin and of lto-wrapper, and the shared objects that the
        loader maps into collect2, into the linker and into the plugin.
      - The bytes of the configuration of the loader, of its cache and of
        its preload list.  The linker reads the configuration for the shared
        objects that a shared object needs.  These files are under /etc,
        which is not a search root.
      - The arguments of the run, and each environment variable of the C
        library (RUN_ENVIRONMENT, RUN_ENVIRONMENT_PREFIXES).
    The inputs of a result of a program are the inputs of each compile, and
    these inputs:
      - Each file that the linker opened, from its --verbose report, and
        each path that it tried and did not find.
      - Each path that the loader tried for the program and each shared
        object that it maps, from a list of the loader under LD_DEBUG=libs.
    A file that exists is a dependency, and its bytes are an input.  A path
    that does not exist is a probed path, and a file at that path later
    makes the result a miss.  The object files and the program are outputs,
    so they are not inputs.  The program runs in an empty directory.

WHAT THE STORE DOES NOT STORE
    - A result whose input changed in the settle period, or whose compiler
      or linker identity changed during the unit.
    - A compile or a link whose exit code is not 0 or 1, because a signal or
      an internal error can give a different result on the next run.
    - A run that ended by a signal or by its time limit, and a run whose
      result the caller calls unstable.  The run of a program can depend on
      time, and the caller knows which outcome does.

The store has its own self-test: unit_store.py --self-test --cxx CXX.
Complexity: a lookup hashes each dependency one time, O(bytes of the
dependencies).  A unit that misses adds one search query and one key
computation to its build.
"""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import json
import os
import re
import shutil
import stat
import subprocess
import sys
import tempfile
import threading
import time
from collections.abc import Callable, Iterator, Sequence
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

sys.dont_write_bytecode = True
_HERE = Path(__file__).resolve().parent
_REPO = _HERE.parents[2]
sys.path.insert(0, str(_REPO / "utils" / "scripts"))
sys.path.insert(0, str(_REPO / "test"))

import cache_dir  # noqa: E402
import neg_compile_store as compile_store  # noqa: E402

# The name of the store in utils/scripts/cache_dir.py.
STORE_NAME = "session_oracle"
# Changed when a key or a stored result no longer means what it meant.
FORMAT = "crucible-session-oracle-units 1"
# The size limit of the entries of the store.
LIMIT_BYTES = 256 << 20
# The file name of the digest memo of one caller, in the memo directory of the store.
DIGEST_MEMO_NAME = "units-{}.digests"
# The time limit of the run of a program, in seconds.
RUN_TIMEOUT_S = 600
# The environment variables that the C library reads in the run of a program.
RUN_ENVIRONMENT = frozenset({"LANG", "TZ"})
RUN_ENVIRONMENT_PREFIXES = ("LD_", "LC_", "GLIBC_", "MALLOC_")
# The programs of a link that the compiler driver finds.  The loader of the
# linker also maps the shared objects of the LTO plugin.
LINK_PROGRAMS = ("collect2", "ld", "liblto_plugin.so", "lto-wrapper")
# The configuration of the loader, its cache, and the preload list that it reads in each process.
LOADER_CONFIG = Path("/etc/ld.so.conf")
LOADER_CONFIG_DIRECTORY = Path("/etc/ld.so.conf.d")
LOADER_CACHE = Path("/etc/ld.so.cache")
PRELOAD_LIST = Path("/etc/ld.so.preload")
_ATTEMPT = re.compile(r"^attempt to open (.+) (succeeded|failed)$", re.MULTILINE)


@dataclass(frozen=True, slots=True)
class UnitResult:
    """The exit code, the standard output and the standard error of a unit, and whether it came from the store."""

    returncode: int
    stdout: str
    stderr: str
    is_stored: bool = False


def _always_stable(result: UnitResult) -> bool:
    """Return True, because the result of each run stays the same on the next run with the same inputs."""
    return True


@dataclass(slots=True)
class Unit:
    """One unit before its lookup and its build: its sources, its commands, the arguments of its run and its key.

    ``kind`` is "static" or "program".  ``work`` is the scratch directory of
    the outputs of the unit.  A static unit has one compile and no link.
    ``key`` is None when the store is off or a part of the key cannot be
    found.
    """

    kind: str
    sources: list[Path]
    work: Path
    compiles: list[list[str]]
    link: list[str]
    arguments: tuple[str, ...]
    is_stable: Callable[[UnitResult], bool] = _always_stable
    key: str | None = None

    @property
    def directory(self) -> Path:
        """Return the directory of the sources, the working directory of each build of the unit."""
        return self.sources[0].parent

    def holds_only_sources(self) -> bool:
        """Return True when the directory of the sources holds the sources and no other name but a temporary one.

        cache_dir.write_atomic gives a temporary file a name that starts with
        a dot, and no include of a unit names such a file.  O(names).
        """
        try:
            names = {name for name in os.listdir(self.directory) if not name.startswith(".")}
        except OSError:
            return False
        return names == {source.name for source in self.sources}


@dataclass(slots=True)
class _Inputs:
    """The inputs of a result: each file read, each probed path or missing header, and each search directory."""

    dependencies: list[str] = field(default_factory=list)
    missing: list[str] = field(default_factory=list)
    named: list[str] = field(default_factory=list)

    def add(self, other: _Inputs) -> None:
        """Add the inputs of ``other``."""
        self.dependencies += other.dependencies
        self.missing += other.missing
        self.named += other.named


def _digest(data: bytes) -> str:
    """Return the SHA-256 of ``data`` as hexadecimal text."""
    return hashlib.sha256(data).hexdigest()


def _encode(stdout: str, stderr: str) -> str:
    """Return the text that a stored result keeps for the two outputs of a unit."""
    return json.dumps({"stdout": stdout, "stderr": stderr})


def _decode(text: str) -> tuple[str, str] | None:
    """Return the two outputs that ``_encode`` gave, or None for a text that it did not give."""
    try:
        value = json.loads(text)
    except ValueError:
        return None
    if not (isinstance(value, dict) and isinstance(value.get("stdout"), str) and isinstance(value.get("stderr"), str)):
        return None
    return value["stdout"], value["stderr"]


def _is_under(path: str, directory: Path) -> bool:
    """Return True when ``path`` is ``directory`` or a path inside it."""
    real = os.path.realpath(path)
    base = os.path.realpath(directory)
    return real == base or real.startswith(base + os.sep)


def parse_link_report(text: str, directory: Path, outputs: Path) -> _Inputs:
    """Return the inputs of a link from the --verbose report of GNU ld.

    Each file that the linker opened is a dependency, and each path that it
    tried and did not find is a probed path.  A path under ``outputs`` is an
    output of the unit and not an input.  A relative path is relative to
    ``directory``.  O(length of the report).
    """
    inputs = _Inputs()
    for match in _ATTEMPT.finditer(text):
        path = os.path.join(directory, match.group(1))
        if _is_under(path, outputs):
            continue
        (inputs.dependencies if match.group(2) == "succeeded" else inputs.missing).append(path)
    return inputs


def _loader_identity(memo: Path) -> list[object]:
    """Return the path and the bytes of each file of the loader configuration, of its cache and of its preload list.

    The linker reads the configuration for the shared objects that a shared
    object needs, and the loader reads the cache and the preload list.  The
    files are under /etc, which is not a search root, so they go into the
    key.  A file that does not exist has no bytes.
    """
    paths = [LOADER_CONFIG, *sorted(LOADER_CONFIG_DIRECTORY.glob("*.conf")), LOADER_CACHE, PRELOAD_LIST]
    identity: list[object] = []
    for path in paths:
        try:
            identity.append([str(path), compile_store.file_digest(str(path), memo)])
        except OSError:
            identity.append([str(path), None])
    return identity


class UnitStore:
    """Build and run the units of the self-test, and keep their results in the store.

    One instance serves the units of one run, from many threads.  ``scratch``
    holds the outputs of each build: the dependency files, the object files
    and the programs.
    """

    def __init__(self, cxx: str, flags: Sequence[str], link_flags: Sequence[str], scratch: Path,
                 store_root: Path | None = None, caller: str | None = None) -> None:
        """Use the compiler ``cxx`` with the compile flags ``flags`` and the link flags ``link_flags``.

        ``store_root`` is the directory of the store.  Without it, the store
        is the cache STORE_NAME of cache_dir, and with the caches off there
        is no store.  ``caller`` names the digest memo of the lookups, and
        save_memo writes it.  Without it, the lookups use no memo.
        """
        self.cxx = shutil.which(cxx) or cxx
        self.flags = list(flags)
        self.link_flags = list(link_flags)
        self.scratch = scratch
        self.environment = compile_store.compile_environment(os.environ)
        self.module = _digest(Path(__file__).read_bytes())
        root = store_root if store_root is not None else cache_dir.cache_root(STORE_NAME)
        self.store: compile_store.ResultStore | None = None
        if root is not None:
            root = root.resolve()
            self.store = compile_store.ResultStore(root, LIMIT_BYTES)
            self.store.entries.mkdir(parents=True, exist_ok=True)
            self.store.memo.mkdir(parents=True, exist_ok=True)
        self.sources_root = (root if root is not None else scratch.resolve()) / "sources"
        memo_path = (self.store.memo / DIGEST_MEMO_NAME.format(caller)
                     if self.store is not None and caller is not None else None)
        # The units have no build directory, so no root takes the place of a
        # mark, and the store keys each path as it is.
        self.roots = compile_store.Roots(None, None)
        self.digests = compile_store.DigestMemo(memo_path, self.roots)
        self._lock = threading.Lock()
        self._program_paths: dict[str, str | None] = {}

    def save_memo(self) -> None:
        """Write the digest memo of the lookups of this run.  A failure to write changes nothing."""
        self.digests.save()

    # ── The sources ─────────────────────────────────────────────────

    def sources(self, files: Sequence[tuple[str, str]]) -> list[Path]:
        """Write the sources of one unit, ``(name, text)`` pairs, to their directory, and return their paths.

        A source that holds its text is not written again, and a source with
        other bytes is written again.  A file of the directory that is not a
        source makes the unit miss (Unit.holds_only_sources).
        """
        digest = _digest(json.dumps([list(pair) for pair in files]).encode("utf-8"))
        directory = self.sources_root / digest[:2] / digest
        directory.mkdir(parents=True, exist_ok=True)
        paths: list[Path] = []
        for name, text in files:
            path = directory / name
            data = text.encode("utf-8")
            try:
                is_current = path.read_bytes() == data
            except OSError:
                is_current = False
            if not is_current:
                cache_dir.write_atomic(path, data)
            paths.append(path)
        with contextlib.suppress(OSError):
            cache_dir.mark_used(directory, directory.stat().st_mtime)
        return paths

    def sweep_sources(self) -> int:
        """Remove each source directory with no use for cache_dir.MAX_AGE, when the turn of this store is due.

        Return the count of removed directories.  O(directories).
        """
        if self.store is None or not self.sources_root.is_dir():
            return 0
        removed = 0
        with cache_dir.eviction_turn(self.sources_root) as has_turn:
            if not has_turn:
                return 0
            expired = time.time() - cache_dir.MAX_AGE
            for directory in self.sources_root.glob("*/*"):
                with contextlib.suppress(OSError):
                    if directory.is_dir() and directory.stat().st_mtime < expired:
                        shutil.rmtree(directory)
                        removed += 1
        return removed

    # ── The keys ────────────────────────────────────────────────────

    def _key(self, kind: str, parts: list[object]) -> str:
        """Return the key of one unit from the key parts of its builds."""
        return _digest(json.dumps({"format": FORMAT, "module": self.module, "kind": kind, "parts": parts},
                                  sort_keys=True).encode("ascii"))

    def _compile_key(self, key_argv: list[str], directory: Path, source: Path) -> str | None:
        """Return the key of one compile from the store, or None.

        ``key_argv`` ends with -MF and the dependency file, and a placeholder
        stands for each other output of the compile.  The source and the
        dependency file are not files of the command for the key: the path
        of the source names its bytes (THE SOURCES), and the dependency file
        is an output.
        """
        if self.store is None:
            return None
        protected = {os.path.realpath(source), os.path.realpath(key_argv[-1])}
        key, _reason = compile_store.compile_key(key_argv, directory, protected, self.store.memo, self.environment,
                                                 self.roots)
        return key

    def _program_path(self, program: str, directory: Path) -> str | None:
        """Return the path of the program or the file that the compiler driver finds as ``program``, or None.

        The answer depends on the driver and on PATH, which stay the same in
        one run, so one query of each program serves the run.
        """
        with self._lock:
            if program in self._program_paths:
                return self._program_paths[program]
        found = subprocess.run([self.cxx, f"-print-prog-name={program}"], cwd=directory, env=self.environment,
                               capture_output=True, text=True, check=False).stdout.strip()
        if found and not os.path.isabs(found):
            found = shutil.which(found, path=self.environment.get("PATH")) or ""
        path = found if found and os.path.isfile(found) else None
        with self._lock:
            self._program_paths[program] = path
        return path

    def _link_identity(self, directory: Path) -> list[object] | None:
        """Return the identity of the link programs, or None when one of them cannot be found or read."""
        assert self.store is not None
        paths: dict[str, str] = {}
        for program in LINK_PROGRAMS:
            path = self._program_path(program, directory)
            if path is None:
                return None
            paths[program] = path
        hosts = {"collect2": paths["collect2"], "ld": paths["ld"], "liblto_plugin.so": paths["ld"]}
        identity: list[object] = []
        try:
            for program, path in paths.items():
                identity.append([program, path, compile_store.file_digest(path, self.store.memo)])
            for program, host in hosts.items():
                interpreter = compile_store.elf_interpreter(host)
                if interpreter is None:
                    identity.append([program, "static"])
                    continue
                objects, _reason = compile_store.shared_objects(paths[program], interpreter, directory,
                                                                self.environment, self.store.memo)
                if objects is None:
                    return None
                identity.append([program, interpreter,
                                 [[name, compile_store.file_digest(name, self.store.memo)] for name in objects]])
        except OSError:
            return None
        return identity

    @staticmethod
    def _run_identity(arguments: Sequence[str]) -> list[object]:
        """Return the arguments and the environment variables of the C library for the run of a program."""
        environment = sorted([name, value] for name, value in os.environ.items()
                             if name in RUN_ENVIRONMENT or name.startswith(RUN_ENVIRONMENT_PREFIXES))
        return [list(arguments), environment]

    # ── One compile ─────────────────────────────────────────────────

    def _compile(self, argv: list[str], directory: Path, source: Path,
                 work: Path) -> tuple[subprocess.CompletedProcess[str], _Inputs | None]:
        """Run one compile, and return its result and its inputs, or None when the inputs are not known.

        ``argv`` ends with -MF and the dependency file.  ``work`` is a
        directory of this compile alone.  After a fatal error, GCC writes no
        dependency file, and a preprocess with -M -MG gives the dependencies
        and the missing headers.
        """
        depfile = Path(argv[-1])
        depfile.unlink(missing_ok=True)
        proc = subprocess.run(argv, cwd=directory, env=self.environment, capture_output=True, text=True,
                              errors="surrogateescape", check=False)
        if self.store is None:
            return proc, None
        inputs = _Inputs()
        try:
            text: str | None = depfile.read_text(errors="surrogateescape")
        except OSError:
            text = None
        if text is not None:
            parsed = compile_store.parse_dependency_file(text)
            if parsed is None:
                return proc, None
            inputs.dependencies = list(dict.fromkeys(os.path.join(directory, name) for name in parsed))
        else:
            present, missing, _reason = compile_store.dependency_pass(argv, directory, work, self.environment)
            if present is None:
                return proc, None
            inputs.dependencies, inputs.missing = present, missing
        named, _reason = compile_store.search_directories(argv, directory, source, work, self.environment)
        if named is None:
            return proc, None
        inputs.named = named
        return proc, inputs

    def _record(self, key: str, result: UnitResult, inputs: _Inputs, started_ns: int) -> None:
        """Store ``result`` with its inputs.  The store refuses it when an input changed in the settle period."""
        assert self.store is not None
        stored = compile_store.CompileResult(result.returncode, _encode(result.stdout, result.stderr), 0.0, 0.0,
                                             None)
        self.store.record(key, stored, list(dict.fromkeys(inputs.dependencies)), list(dict.fromkeys(inputs.missing)),
                          list(dict.fromkeys(inputs.named)), started_ns, self.roots)

    def _lookup(self, key: str) -> UnitResult | None:
        """Return the stored result of ``key``, or None."""
        assert self.store is not None
        stored, _reason = self.store.lookup(key, self.digests, self.roots)
        outputs = _decode(stored.output) if stored is not None else None
        if stored is None or outputs is None:
            return None
        return UnitResult(stored.returncode, outputs[0], outputs[1], True)

    # ── The units ───────────────────────────────────────────────────

    def static_unit(self, files: Sequence[tuple[str, str]]) -> Unit:
        """Return the unit of a compile with -fsyntax-only of one source, ``(name, text)``, with its key."""
        if len(files) != 1:
            raise ValueError(f"a static unit has one source, and this one has {len(files)}")
        sources = self.sources(files)
        work = Path(tempfile.mkdtemp(prefix="static_", dir=self.scratch))
        argv = [self.cxx, *self.flags, "-fsyntax-only", str(sources[0]), "-MD", "-MF", str(work / "unit.d")]
        unit = Unit("static", sources, work, [argv], [], ())
        unit.key = self._unit_key(unit)
        return unit

    def program_unit(self, files: Sequence[tuple[str, str]], arguments: Sequence[str] = (),
                     is_stable: Callable[[UnitResult], bool] = _always_stable) -> Unit:
        """Return the unit of a program, with its key.

        ``files`` holds the ``(name, text)`` of each source, ``arguments`` the
        arguments of the run.  ``is_stable`` tells whether the result of a run
        stays the same on the next run with the same inputs.
        """
        sources = self.sources(files)
        work = Path(tempfile.mkdtemp(prefix="program_", dir=self.scratch))
        objects = [work / f"{index}.o" for index in range(len(sources))]
        compiles = [[self.cxx, *self.flags, "-c", str(source), "-o", str(obj), "-MD", "-MF", str(work / f"{index}.d")]
                    for index, (source, obj) in enumerate(zip(sources, objects, strict=True))]
        link = [self.cxx, "-fcontracts", *map(str, objects), "-o", str(work / "program"), *self.link_flags,
                "-Wl,--verbose"]
        unit = Unit("program", sources, work, compiles, link, tuple(arguments), is_stable)
        unit.key = self._unit_key(unit)
        return unit

    def _unit_key(self, unit: Unit) -> str | None:
        """Return the key of ``unit``, or None when the store is off or one part of the key cannot be found.

        A placeholder stands for each object file and for the program, which
        are outputs in the scratch directory of this run.
        """
        if self.store is None:
            return None
        placeholders = {str(unit.work / f"{index}.o"): f"<object {index}>" for index in range(len(unit.sources))}
        placeholders[str(unit.work / "program")] = "<program>"
        parts: list[object] = []
        for command, source in zip(unit.compiles, unit.sources, strict=True):
            compile_key = self._compile_key([placeholders.get(arg, arg) for arg in command], unit.directory, source)
            if compile_key is None:
                return None
            parts.append(compile_key)
        if unit.kind == "program":
            identity = self._link_identity(unit.directory)
            if identity is None:
                return None
            parts += [[placeholders.get(arg, arg) for arg in unit.link], identity, _loader_identity(self.store.memo),
                      self._run_identity(unit.arguments)]
        return self._key(unit.kind, parts)

    def stored(self, unit: Unit) -> UnitResult | None:
        """Return the stored result of ``unit``, or None when the store holds no result with the same inputs."""
        if unit.key is None or not unit.holds_only_sources():
            return None
        return self._lookup(unit.key)

    def build(self, unit: Unit) -> UnitResult:
        """Build ``unit``, store its result when the store can keep it, and return the result.

        The key names the bytes of each source (THE SOURCES), so the sources
        are not dependencies of the result, and a new source does not wait
        for the settle period.
        """
        started_ns = time.time_ns()
        if unit.kind == "static":
            proc, inputs = self._compile(unit.compiles[0], unit.directory, unit.sources[0], unit.work)
            result = UnitResult(proc.returncode, proc.stdout, proc.stderr)
            is_storable = proc.returncode in (0, 1)
        else:
            result, inputs, is_storable = self._build_program(unit)
        if inputs is not None:
            inputs.dependencies = [path for path in inputs.dependencies if not _is_under(path, unit.directory)]
        # A changed compiler or linker identity, or a foreign file beside the sources, stores nothing.
        if (unit.key is not None and inputs is not None and is_storable and unit.holds_only_sources()
                and self._unit_key(unit) == unit.key):
            self._record(unit.key, result, inputs, started_ns)
        return result

    def static(self, files: Sequence[tuple[str, str]]) -> UnitResult:
        """Return the result of a compile with -fsyntax-only of one source, from the store or from a build."""
        unit = self.static_unit(files)
        stored = self.stored(unit)
        return stored if stored is not None else self.build(unit)

    def program(self, files: Sequence[tuple[str, str]], arguments: Sequence[str] = (),
                is_stable: Callable[[UnitResult], bool] = _always_stable) -> UnitResult:
        """Return the result of a program, from the store or from a build (program_unit)."""
        unit = self.program_unit(files, arguments, is_stable)
        stored = self.stored(unit)
        return stored if stored is not None else self.build(unit)

    def _build_program(self, unit: Unit) -> tuple[UnitResult, _Inputs | None, bool]:
        """Compile, link and run a program.  Return its result, its inputs or None, and whether it can be stored.

        The result is the first compile that fails, else the link when it
        fails, else the run.
        """
        directory, work, compiles, link = unit.directory, unit.work, unit.compiles, unit.link
        program = work / "program"
        scratches = [work / f"compile_{index}" for index in range(len(compiles))]
        for scratch in scratches:
            scratch.mkdir()
        with ThreadPoolExecutor(max_workers=len(compiles)) as pool:
            built = list(pool.map(lambda job: self._compile(job[0], directory, job[1], job[2]),
                                  zip(compiles, unit.sources, scratches, strict=True)))
        inputs: _Inputs | None = _Inputs()
        for _proc, compiled in built:
            if compiled is None or inputs is None:
                inputs = None
            else:
                inputs.add(compiled)
        failed = next((proc for proc, _ in built if proc.returncode != 0), None)
        if failed is not None:
            is_storable = all(proc.returncode in (0, 1) for proc, _ in built)
            return UnitResult(failed.returncode, failed.stdout, failed.stderr), inputs, is_storable
        linked = subprocess.run(link, cwd=directory, env=self.environment, capture_output=True, text=True,
                                errors="surrogateescape", check=False)
        if inputs is not None:
            inputs.add(parse_link_report(linked.stdout, directory, work))
        if linked.returncode != 0:
            return UnitResult(linked.returncode, linked.stdout, linked.stderr), inputs, linked.returncode == 1
        if inputs is not None:
            loaded = self._loaded(program, directory)
            if loaded is None:
                inputs = None
            else:
                inputs.add(loaded)
        empty = work / "run"
        empty.mkdir()
        try:
            ran = subprocess.run([str(program), *unit.arguments], cwd=empty, capture_output=True, text=True,
                                 errors="surrogateescape", timeout=RUN_TIMEOUT_S, check=False)
        except subprocess.TimeoutExpired:
            return UnitResult(124, "", f"the run of the program took more than {RUN_TIMEOUT_S} s"), inputs, False
        result = UnitResult(ran.returncode, ran.stdout, ran.stderr)
        return result, inputs, ran.returncode >= 0 and unit.is_stable(result)

    def _loaded(self, program: Path, directory: Path) -> _Inputs | None:
        """Return each path that the loader tries for ``program``, or None.

        A tried path that exists is a dependency, and one that does not is a
        probed path.  The loader lists the objects under LD_DEBUG=libs.  The
        key holds the cache of the loader (_loader_identity), so a list that
        names a different cache gives None.
        """
        interpreter = compile_store.elf_interpreter(str(program))
        if interpreter is None:
            return _Inputs()
        environment = {name: value for name, value in os.environ.items() if name != "LD_DEBUG_OUTPUT"}
        environment["LD_DEBUG"] = "libs"
        proc = subprocess.run([interpreter, "--list", str(program)], cwd=directory, env=environment,
                              capture_output=True, text=True, errors="surrogateescape", check=False)
        parsed = compile_store.parse_loader_list(proc.stdout, proc.stderr, directory) if proc.returncode == 0 else None
        if parsed is None:
            return None
        objects, tried, caches = parsed
        if any(cache != str(LOADER_CACHE) for cache in caches):
            return None
        inputs = _Inputs(dependencies=[interpreter, *objects])
        for path in tried:
            (inputs.dependencies if os.path.isfile(path) else inputs.missing).append(path)
        return inputs


# ── The self-test ───────────────────────────────────────────────────


@contextlib.contextmanager
def _environment(name: str, value: str) -> Iterator[None]:
    """Set one environment variable for the block."""
    saved = os.environ.get(name)
    os.environ[name] = value
    try:
        yield
    finally:
        if saved is None:
            os.environ.pop(name, None)
        else:
            os.environ[name] = saved


def _counting_compiler(cxx: str, directory: Path) -> tuple[str, Path]:
    """Return a compiler that runs ``cxx`` and logs its arguments, and the log.

    A line of the log that holds no query of the driver, no search query and
    no dependency pass is one compile or one link.
    """
    log = directory / "runs.log"
    wrapper = directory / "counted-cxx"
    wrapper.write_text(f'#!/bin/sh\necho "$*" >> "{log}"\nexec "{cxx}" "$@"\n', encoding="utf-8")
    wrapper.chmod(wrapper.stat().st_mode | stat.S_IXUSR)
    return str(wrapper), log


def _builds(log: Path) -> int:
    """Return the count of compiles and links in the log of ``_counting_compiler``."""
    try:
        lines = log.read_text(encoding="utf-8").splitlines()
    except OSError:
        return 0
    return sum(" -print-" not in f" {line}" and " -E " not in f" {line} " and " -MG" not in line for line in lines)


def _build_library(cxx: str, directory: Path, value: int) -> bool:
    """Build directory/libvalue.so, whose library_value returns ``value``.  Return True when it builds."""
    source = directory / "value.cpp"
    source.write_text(f"int library_value() {{ return {value}; }}\n", encoding="utf-8")
    return subprocess.run([cxx, "-shared", "-fPIC", str(source), "-o", str(directory / "libvalue.so")],
                          capture_output=True, text=True, check=False).returncode == 0


def self_test(cxx: str) -> int:
    """Prove each rule of the store on scratch units in a scratch store.  Return 0 when each case holds, else 1."""
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="unit_store_selftest_") as tmp:
        _self_test_cases(expect, cxx, Path(tmp))
    if failures:
        print(f"unit_store --self-test: {len(failures)} case(s) failed", file=sys.stderr)
        return 1
    print("unit_store --self-test: every case passes.")
    return 0


def _self_test_cases(expect: Callable[[str, bool], None], cxx: str, root: Path) -> None:
    """Run each case of the self-test under ``root``.

    Each file starts outside the settle period after one wait.  A change
    after the wait is in the settle period, so a case after a change shows
    a miss or a result whose inputs are the same as stored ones.
    """
    include, early, library, early_library, scratch = (root / name for name in
                                                       ("include", "early", "library", "early_library", "scratch"))
    for directory in (include, early, library, early_library, scratch):
        directory.mkdir()
    (include / "probe.h").write_text("#pragma once\ninline constexpr int probe_value = 1;\n")
    expect("the scratch library builds", _build_library(cxx, library, 7))
    static = [("unit.cpp", '#include "probe.h"\nstatic_assert(probe_value == 1, "the probe value");\n'
                           "int main() { return 0; }\n")]
    planted = [("planted.cpp", '#include "probe.h"\nstatic_assert(probe_value == 2, "the planted probe value");\n')]
    program = [("main.cpp", '#include <cstdio>\n#include "probe.h"\nint library_value();\n'
                            'int main() { std::printf("value %d\\n", library_value() + probe_value); return 0; }\n')]
    flags = ["-std=c++20", f"-I{early}", f"-I{include}"]
    link_flags = [f"-L{early_library}", f"-L{library}", "-lvalue", f"-Wl,-rpath,{early_library}:{library}"]
    store_root = root / "store"
    counted, log = _counting_compiler(cxx, root)

    def unit_store(unit_flags: Sequence[str] = tuple(flags)) -> UnitStore:
        return UnitStore(counted, unit_flags, link_flags, scratch, store_root)

    first = unit_store()
    fresh = first.static(static)
    first.static(planted)
    first.program(program)
    expect("a unit whose inputs changed in the settle period builds, and the store keeps no result",
           fresh.returncode == 0 and not unit_store().static(static).is_stored)
    time.sleep(compile_store._SETTLE_NS / 1e9 + 0.2)

    cold = unit_store().static(static)
    before = _builds(log)
    warm = unit_store().static(static)
    expect("a warm static unit comes from the store, and no compile runs",
           not cold.is_stored and cold.returncode == 0 and warm.is_stored and warm.returncode == 0
           and _builds(log) == before)
    unit_store().static(planted)
    planted_warm = unit_store().static(planted)
    expect("a failed compile comes from the store with its error output",
           planted_warm.is_stored and planted_warm.returncode == 1 and "the planted probe value" in planted_warm.stderr)
    renamed = [("renamed.cpp", static[0][1])]
    renamed_first = unit_store().static(renamed)
    expect("a new source does not wait for the settle period: its first build stores the result",
           not renamed_first.is_stored and unit_store().static(renamed).is_stored)
    directory = unit_store().sources(static)[0].parent
    (directory / "probe.h").write_text("#pragma once\ninline constexpr int probe_value = 4;\n")
    beside = unit_store().static(static)
    expect("a file beside the sources makes the unit miss, and the include finds it",
           not beside.is_stored and beside.returncode == 1)
    (directory / "probe.h").unlink()
    (directory / "unit.cpp").write_text("int damaged_source;\n")
    rewritten = unit_store().static(static)
    expect("a source with other bytes is written again before the lookup, so the stored result holds",
           rewritten.is_stored and rewritten.returncode == 0
           and (directory / "unit.cpp").read_text(encoding="utf-8") == static[0][1])
    unit_store().program(program)
    before = _builds(log)
    program_warm = unit_store().program(program)
    expect("a warm program comes from the store with the output of its run, and no compile or link runs",
           program_warm.is_stored and program_warm.stdout == "value 8\n" and _builds(log) == before)
    unstable = [unit_store().program(program, ["unstable"], is_stable=lambda result: False) for _ in range(2)]
    expect("a run that the caller calls unstable is not stored",
           [result.is_stored for result in unstable] == [False, False] and unstable[1].stdout == "value 8\n")
    expect("the arguments of the run are part of the key", not unit_store().program(program, ["other"]).is_stored)
    expect("a new compile flag gives a new key", not unit_store([*flags, "-DEXTRA_FLAG=1"]).static(static).is_stored)
    with ThreadPoolExecutor(max_workers=4) as pool:
        parallel = list(pool.map(lambda files: unit_store().static(files), [static, static, planted, planted]))
    expect("units in many threads give the stored results",
           [(result.returncode, result.is_stored) for result in parallel] == [(0, True), (0, True), (1, True),
                                                                               (1, True)])

    (include / "probe.h").write_text("#pragma once\ninline constexpr int probe_value = 2;\n")
    changed = unit_store().static(static)
    expect("a changed header makes the unit miss, so the store gives no stale result",
           not changed.is_stored and changed.returncode == 1 and "the probe value" in changed.stderr)
    changed_program = unit_store().program(program)
    expect("a changed header makes the program miss", not changed_program.is_stored
           and changed_program.stdout == "value 9\n")
    (include / "probe.h").write_text("#pragma once\ninline constexpr int probe_value = 1;\n")
    expect("the header with the stored bytes reads the stored result again", unit_store().static(static).is_stored)

    (early / "probe.h").write_text("#pragma once\ninline constexpr int probe_value = 3;\n")
    shadowed = unit_store().static(static)
    expect("a header that a directory earlier on the search path now holds makes the unit miss",
           not shadowed.is_stored and shadowed.returncode == 1)
    (early / "probe.h").unlink()

    expect("a changed library builds", _build_library(cxx, library, 9))
    relinked = unit_store().program(program)
    expect("a changed library that the link and the run read makes the program miss",
           not relinked.is_stored and relinked.stdout == "value 10\n")
    expect("a library in a directory that the linker and the loader tried first builds",
           _build_library(cxx, early_library, 11))
    probed = unit_store().program(program)
    expect("a library on a path that the linker and the loader tried and did not find makes the program miss",
           not probed.is_stored and probed.stdout == "value 12\n")

    with _environment(cache_dir.ROOT_VARIABLE, "off"):
        disabled = UnitStore(cxx, flags, link_flags, scratch)
        result = disabled.static(static)
    expect("with the caches off, a unit builds and there is no store",
           disabled.store is None and result.returncode == 0 and not result.is_stored)


def main(argv: list[str]) -> int:
    """Run the self-test."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--self-test", action="store_true", required=True, help="prove each rule of the store")
    parser.add_argument("--cxx", required=True, help="the compiler of the scratch units")
    args = parser.parse_args(argv)
    return self_test(args.cxx)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
