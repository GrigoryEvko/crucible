#!/usr/bin/env python3
"""run-affected-tests: build, then run each test whose inputs changed since its last pass.

The inner loop of a developer is "edit, build, test".  A full ctest run of the
Debug build runs about 2,500 tests.  After one edit, most test executables are
the same as at their last pass, and a second run of such an executable proves
nothing new.  This script builds the tree, and then it runs only these tests:

- Each negative-compile fixture.  The result store of test/neg_compile_driver.py
  decides if the fixture compiles again.
- Each script test: a guard, a self-test, or another test whose program is not
  an executable of the build tree.  These tests read the source tree, so they
  always run.
- Each executable test whose fingerprint is not the fingerprint of its last
  pass.

THE EXECUTABLE TEST
    The program of an executable test is an ELF file of the build tree, after
    the wrappers: the test launcher of the tree (`python3 -S
    utils/scripts/test-launcher.py --warnings-dir DIR`, which CMake puts in
    front of each executable target), `cmake -E env` and `valgrind`.  Its
    fingerprint is a hash of these items:
    - The ctest record of the test: the command and each property.
    - The bytes of each file of the build tree that the command names.
    - The bytes of each file that an ELF file of the command names as an
      absolute path, in an allocated section that holds no code.  Such a name
      is a shared library, a data file, or a directory of the run path.  A
      named directory of the build tree adds each shared library in it.  A
      named file of the source tree with a C or C++ suffix is the text of a
      diagnostic, for example from __FILE__, and it is not an input.
    - For a test behind the test launcher: the bytes of each .py and .txt
      file in the directory of the launcher, and of build-kind.txt in the
      build tree.  The launcher reads its modules, its budget table and its
      ledgers there, and the result of a test depends on them.
    The scan of an ELF file reads its section headers, so it finds a name in
    .rodata, .data or .dynstr, and it does not read the code or the debug
    information.

THE TESTS THAT ALWAYS RUN
    The script cannot see each input of these tests, so they always run:
    - A test whose command, environment or working directory names a path of
      the source tree that is not in the build tree.
    - A test with the property FIXTURES_REQUIRED, FIXTURES_SETUP,
      FIXTURES_CLEANUP or REQUIRED_FILES.
    - A test whose ELF file names a directory of the source tree, or a path of
      the source tree that is not a file.
    - A test whose ELF file the script cannot read as an ELF64 file.

THE LOOP HISTORY
    After each call that builds or runs tests, the script appends one line to
    BUILD_DIR/loop-history/history.jsonl: the commit, the job count, the host
    load, the wall time, the CPU time and the critical path of the build, the
    compiles and the links that ran, and the wall time and the CPU time of
    the tests.  `python3 utils/scripts/loop_history.py BUILD_DIR` prints the
    last lines as a table.  utils/scripts/loop_history.py gives the rules.

THE RECORD
    After an executable test passes, the script records its fingerprint in
    BUILD_DIR/affected-tests/state.json.  A test that fails, a test that ctest
    does not run, and a test whose inputs changed while it ran get no record,
    and they run on the next call.  The script never reports a test as passed
    that it did not run.  It prints the number of tests that it skipped and
    the reason, and it writes their names to BUILD_DIR/affected-tests/skipped.txt.
    A lock on BUILD_DIR/affected-tests/lock keeps two calls on one build
    directory apart.

    The script assumes that an executable test reads no file of the source
    tree, unless its command, its environment or its ELF file names that
    file.  The tests of this tree write their scratch files in the build tree
    or in /tmp, and they read /proc and /sys.

The script is for the inner loop only.  CI and the checks before a landing
run the full suite.

Usage:
    run-affected-tests.py BUILD_DIR [-j N] [--no-build] [--all] [--plan] [--ctest CTEST] [-- CTEST_ARGUMENT...]
    run-affected-tests.py --self-test [--ctest CTEST]

--no-build  Do not build first.  Then a stale executable can look unchanged.
--all       Run each executable test, and record each pass.
--plan      Print what the script would run, and run nothing.
--ctest     The ctest program.  The default is the ctest in PATH.  The build
            uses the cmake in the same directory.

Exit 0 when each test that ran passed, 1 when the build or a test failed, 2 on
a usage error, a build directory that the script cannot read, or a failed
self-test.
"""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import mmap
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ElementTree
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
import loop_history  # noqa: E402

# ── Constants ──────────────────────────────────────────────────────────────

STATE_VERSION = 1
STATE_DIRECTORY = "affected-tests"
# --ctest puts its value here, so that each child process of the self-test
# uses the same ctest.
CTEST_VARIABLE = "CRUCIBLE_AFFECTED_TESTS_CTEST"
FIXTURE_DRIVER = "neg_compile_driver.py"
TEST_LAUNCHER = "test-launcher.py"
LAUNCHER_INPUT_SUFFIXES = (".py", ".txt")
BUILD_KIND_FILE = "build-kind.txt"
ALWAYS_RUN_PROPERTIES = ("FIXTURES_REQUIRED", "FIXTURES_SETUP", "FIXTURES_CLEANUP", "REQUIRED_FILES")
ENVIRONMENT_PROPERTIES = ("ENVIRONMENT", "ENVIRONMENT_MODIFICATION")
# A name in an ELF file with one of these suffixes is the text of a
# diagnostic, such as __FILE__ or std::source_location, and not a data file.
DIAGNOSTIC_SUFFIXES = (".h", ".hh", ".hpp", ".hxx", ".c", ".cc", ".cpp", ".cxx", ".inc", ".ipp", ".tpp")
SHARED_LIBRARY = re.compile(r"\.so(\.[0-9]+)*$")
PATH_BYTES = rb"[A-Za-z0-9_./+@-]"
ELF_MAGIC = b"\x7fELF"
ELF_CLASS_64 = 2
ELF_DATA_LITTLE = 1
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
SHT_NOBITS = 8
ELF_HEADER_SIZE = 64
SECTION_HEADER_SIZE = 64

KIND_FIXTURE = "fixture"
KIND_SCRIPT = "script"
KIND_ALWAYS = "always"
KIND_EXECUTABLE = "executable"

# The reason of each decision, as the summary prints it.
REASON_FIXTURE = "the result store of test/neg_compile_driver.py decides"
REASON_SCRIPT = "a script test reads the source tree"
REASON_CHANGED = "an input changed since the last pass"
REASON_NO_RECORD = "no pass on record"
REASON_ALL = "--all"
REASON_SAME = "each input is the same as at the last pass"


# ── Exceptions ─────────────────────────────────────────────────────────────


class RunnerError(Exception):
    """A condition that stops the script: a usage error or a build directory that it cannot read."""


# ── Helpers ────────────────────────────────────────────────────────────────


@dataclass(frozen=True)
class Trees:
    """The source tree and the build tree, as real absolute paths without a final slash."""

    source_root: str
    build_root: str

    def is_in_build(self, path: str) -> bool:
        """Return True when `path` is the build tree or a path in it."""
        return path == self.build_root or path.startswith(self.build_root + "/")

    def is_source_only(self, path: str) -> bool:
        """Return True when `path` is in the source tree and not in the build tree."""
        in_source = path == self.source_root or path.startswith(self.source_root + "/")
        return in_source and not self.is_in_build(path)


@dataclass(frozen=True)
class TestRecord:
    """One test from `ctest --show-only=json-v1`."""

    name: str
    command: tuple[str, ...]
    properties: dict[str, object]
    canonical: str


@dataclass
class Decision:
    """What the script does with one test, and why."""

    test: TestRecord
    kind: str
    reason: str
    fingerprint: str = ""
    is_skipped: bool = False


@dataclass
class Scan:
    """The names that one ELF file holds: input files, input directories, or a reason to always run."""

    files: list[str] = field(default_factory=list)
    directories: list[str] = field(default_factory=list)
    always_reason: str = ""

    def to_json(self) -> dict[str, object]:
        """Return the scan as a JSON object for the state file."""
        return {"files": self.files, "directories": self.directories, "always": self.always_reason}

    @staticmethod
    def from_json(value: object) -> Scan | None:
        """Return the scan of a JSON object, or None when the object is not a scan."""
        if not isinstance(value, dict):
            return None
        files, directories, always = value.get("files"), value.get("directories"), value.get("always")
        if not (isinstance(files, list) and isinstance(directories, list) and isinstance(always, str)):
            return None
        if not all(isinstance(item, str) for item in files + directories):
            return None
        return Scan(files=files, directories=directories, always_reason=always)


def real(path: str) -> str:
    """Return the real absolute path of `path`, without a final slash."""
    return os.path.realpath(path).rstrip("/") or "/"


def read_trees(build_dir: Path) -> Trees:
    """Return the trees of a configured build directory.

    The source tree comes from CMAKE_HOME_DIRECTORY in CMakeCache.txt.
    """
    cache = build_dir / "CMakeCache.txt"
    if not cache.is_file():
        raise RunnerError(f"{build_dir} holds no CMakeCache.txt.  Configure it first, for example "
                          f"`cmake --preset default`, and give its path.")
    for line in cache.read_text(errors="replace").splitlines():
        if line.startswith("CMAKE_HOME_DIRECTORY:"):
            source = line.split("=", 1)[1].strip()
            if not Path(source).is_dir():
                raise RunnerError(f"CMakeCache.txt of {build_dir} names the source tree {source}, "
                                  f"which does not exist.  Configure the build directory again.")
            return Trees(source_root=real(source), build_root=real(str(build_dir)))
    raise RunnerError(f"CMakeCache.txt of {build_dir} holds no CMAKE_HOME_DIRECTORY.  Configure the build "
                      f"directory again.")


def ctest_program() -> str:
    """Return the path of ctest from --ctest, or from PATH, or stop when there is none."""
    program = os.environ.get(CTEST_VARIABLE) or shutil.which("ctest")
    if program is None:
        raise RunnerError("ctest is not in PATH.  Put the ctest of the CMake that configured the build in PATH, "
                          "or give it with --ctest.")
    return program


def list_tests(build_dir: Path) -> list[TestRecord]:
    """Return each test of the build directory, from `ctest --show-only=json-v1`."""
    result = subprocess.run([ctest_program(), "--test-dir", str(build_dir), "--show-only=json-v1"],
                            capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise RunnerError(f"`ctest --show-only=json-v1` failed with exit code {result.returncode}:\n"
                          f"{result.stderr.strip()}")
    try:
        data = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RunnerError(f"`ctest --show-only=json-v1` did not give JSON: {error}") from error
    tests: list[TestRecord] = []
    for entry in data.get("tests", []):
        properties = {item["name"]: item["value"] for item in entry.get("properties", [])}
        command = tuple(entry.get("command") or ())
        canonical = json.dumps({"name": entry["name"], "command": command, "properties": properties},
                               sort_keys=True)
        tests.append(TestRecord(name=entry["name"], command=command, properties=properties, canonical=canonical))
    return tests


def launcher_prefix(command: tuple[str, ...]) -> tuple[int, str]:
    """Return the index after the test launcher prefix and the path of the launcher, or (0, "") with no launcher.

    The prefix is a Python interpreter, its options, the launcher script and
    an optional `--warnings-dir DIR`.
    """
    if not command or not os.path.basename(command[0]).startswith("python"):
        return 0, ""
    index = 1
    while index < len(command) and command[index].startswith("-"):
        index += 1
    if index >= len(command) or os.path.basename(command[index]) != TEST_LAUNCHER:
        return 0, ""
    script = command[index]
    index += 1
    if index < len(command) and command[index] == "--warnings-dir":
        index += 2
    return index, script


def program_index(command: tuple[str, ...]) -> int | None:
    """Return the index of the program after the test launcher, `cmake -E env` and `valgrind`, or None."""
    if not command:
        return None
    index, _launcher = launcher_prefix(command)
    if index >= len(command):
        return None
    name = os.path.basename(command[index])
    if name == "cmake" and command[index + 1:index + 3] == ("-E", "env"):
        index += 3
        while index < len(command):
            item = command[index]
            if item == "--modify":
                index += 2
            elif item.startswith("--") or ("=" in item and not item.startswith("/")):
                index += 1
            else:
                break
    elif name == "valgrind":
        index += 1
        while index < len(command) and command[index].startswith("-"):
            index += 1
    return index if index < len(command) else None


def source_paths_in(text: str, trees: Trees) -> list[str]:
    """Return each absolute path in `text` that is in the source tree and not in the build tree."""
    found: list[str] = []
    for match in re.finditer(re.escape(trees.source_root) + r"(?:/[^\s:;=,\"']*)?", text):
        candidate = os.path.normpath(match.group(0))
        if trees.is_source_only(candidate):
            found.append(candidate)
    return found


def is_elf(path: str) -> bool:
    """Return True when `path` is a regular file that starts with the ELF magic."""
    try:
        with open(path, "rb") as handle:
            return handle.read(4) == ELF_MAGIC
    except OSError:
        return False


def data_sections(data: mmap.mmap | bytes) -> list[tuple[int, int]] | None:
    """Return the byte span of each allocated section that holds no code, or None for a file it cannot read.

    The file must be an ELF64 file in little-endian order with a section
    header table.  O(number of sections).
    """
    if len(data) < ELF_HEADER_SIZE or data[:4] != ELF_MAGIC:
        return None
    if data[4] != ELF_CLASS_64 or data[5] != ELF_DATA_LITTLE:
        return None
    (table_offset,) = struct.unpack_from("<Q", data, 0x28)
    entry_size, count, _names = struct.unpack_from("<HHH", data, 0x3A)
    if table_offset == 0 or entry_size < SECTION_HEADER_SIZE:
        return None
    if count == 0:
        # Extended numbering: the size field of section 0 holds the count.
        if table_offset + SECTION_HEADER_SIZE > len(data):
            return None
        (count,) = struct.unpack_from("<Q", data, table_offset + 0x20)
    if table_offset + count * entry_size > len(data):
        return None
    spans: list[tuple[int, int]] = []
    for index in range(count):
        base = table_offset + index * entry_size
        _name, kind, flags, _address, offset, size = struct.unpack_from("<IIQQQQ", data, base)
        if not flags & SHF_ALLOC or flags & SHF_EXECINSTR or kind == SHT_NOBITS:
            continue
        if offset + size > len(data):
            return None
        spans.append((offset, offset + size))
    return spans


def name_pattern(trees: Trees) -> re.Pattern[bytes]:
    """Return the pattern of an absolute path of the source tree or of the build tree, as bytes."""
    roots = sorted({trees.source_root, trees.build_root}, key=len, reverse=True)
    alternatives = b"|".join(re.escape(root.encode()) for root in roots)
    return re.compile(b"(?<!" + PATH_BYTES + b")(?:" + alternatives + b")(?:/" + PATH_BYTES + b"*)?(?!"
                      + PATH_BYTES + b")")


def scan_elf(path: str, trees: Trees) -> Scan:
    """Return the input names of one ELF file.  O(size of its data sections)."""
    try:
        with open(path, "rb") as handle:
            if os.fstat(handle.fileno()).st_size == 0:
                return Scan(always_reason=f"{path} is empty")
            with mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ) as mapped:
                spans = data_sections(mapped)
                if spans is None:
                    return Scan(always_reason=f"{path} is not an ELF64 file with a section header table")
                pattern = name_pattern(trees)
                names = {match.group(0).decode("utf-8", "surrogateescape")
                         for start, end in spans for match in pattern.finditer(mapped, start, end)}
    except OSError as error:
        return Scan(always_reason=f"the script cannot read {path}: {error.strerror}")
    scan = Scan()
    for name in sorted(names):
        candidate = os.path.normpath(name)
        if trees.is_in_build(candidate):
            (scan.directories if os.path.isdir(candidate) else scan.files).append(candidate)
        elif candidate.endswith(DIAGNOSTIC_SUFFIXES):
            continue
        elif os.path.isfile(candidate):
            scan.files.append(candidate)
        else:
            return Scan(always_reason=f"{path} names {candidate}, which is not a file of the source tree")
    return scan


class DigestCache:
    """The SHA-256 of each file, computed one time for each call of the script."""

    def __init__(self) -> None:
        """Make an empty cache."""
        self._digests: dict[str, str] = {}

    def digest(self, path: str) -> str:
        """Return the SHA-256 of the bytes of `path`, or a word for a path that is not a regular file."""
        if path not in self._digests:
            try:
                with open(path, "rb") as handle:
                    self._digests[path] = hashlib.file_digest(handle, "sha256").hexdigest()
            except FileNotFoundError:
                self._digests[path] = "missing"
            except IsADirectoryError:
                self._digests[path] = "directory"
            except OSError as error:
                self._digests[path] = f"unreadable: {error.strerror}"
        return self._digests[path]

    def forget(self) -> None:
        """Forget each digest, so that the next call reads each file again."""
        self._digests.clear()


def shared_libraries(directory: str) -> list[str]:
    """Return each shared library directly in `directory`, sorted."""
    try:
        names = os.listdir(directory)
    except OSError:
        return []
    return sorted(os.path.join(directory, name) for name in names if SHARED_LIBRARY.search(name))


# ── The state file ─────────────────────────────────────────────────────────


def empty_state(trees: Trees) -> dict[str, object]:
    """Return a state with no record."""
    return {"version": STATE_VERSION, "source": trees.source_root, "build": trees.build_root, "passed": {},
            "scans": {}}


def load_state(path: Path, trees: Trees) -> tuple[dict[str, object], str]:
    """Return the state and a note.  A damaged state, or the state of other trees, gives an empty state."""
    if not path.exists():
        return empty_state(trees), "no state file: each executable test runs"
    try:
        state = json.loads(path.read_text())
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        return empty_state(trees), f"the state file {path} is damaged ({error}): each executable test runs"
    is_valid = (isinstance(state, dict) and state.get("version") == STATE_VERSION
                and isinstance(state.get("passed"), dict) and isinstance(state.get("scans"), dict)
                and all(isinstance(key, str) and isinstance(value, str) for key, value in state["passed"].items()))
    if not is_valid:
        return empty_state(trees), f"the state file {path} is not in format {STATE_VERSION}: each executable test runs"
    if state.get("source") != trees.source_root or state.get("build") != trees.build_root:
        return empty_state(trees), f"the state file {path} belongs to other trees: each executable test runs"
    return state, ""


def save_state(path: Path, state: dict[str, object]) -> None:
    """Write the state through a temporary file and a rename, so that a reader never sees half of it."""
    temporary = path.with_name(f".{path.name}.{os.getpid()}")
    temporary.write_text(json.dumps(state, sort_keys=True))
    os.replace(temporary, path)


# ── The decisions ──────────────────────────────────────────────────────────


class Planner:
    """Decides for each test if it runs, and computes the fingerprint of each executable test."""

    def __init__(self, trees: Trees, state: dict[str, object]) -> None:
        """Keep the trees and the state, and start with an empty digest cache."""
        self.trees = trees
        self.state = state
        self.digests = DigestCache()
        self.used_scans: dict[str, object] = {}
        self.launcher_files: dict[str, list[str]] = {}

    def scan(self, path: str) -> Scan:
        """Return the scan of an ELF file, from the state when the state holds the scan of the same bytes."""
        digest = self.digests.digest(path)
        scans = self.state["scans"]
        assert isinstance(scans, dict)
        stored = Scan.from_json(scans.get(digest))
        result = stored if stored is not None else scan_elf(path, self.trees)
        self.used_scans[digest] = result.to_json()
        return result

    def classify(self, test: TestRecord) -> Decision:
        """Return the kind of a test and, for an executable test, its fingerprint."""
        trees = self.trees
        if any(os.path.basename(item) == FIXTURE_DRIVER for item in test.command):
            return Decision(test, KIND_FIXTURE, REASON_FIXTURE)
        index = program_index(test.command)
        if index is None:
            return Decision(test, KIND_SCRIPT, REASON_SCRIPT)
        program = real(test.command[index]) if os.path.isabs(test.command[index]) else test.command[index]
        if not os.path.isabs(program) or not trees.is_in_build(program):
            return Decision(test, KIND_SCRIPT, REASON_SCRIPT)
        if os.path.exists(program) and not is_elf(program):
            return Decision(test, KIND_SCRIPT, REASON_SCRIPT)
        start, launcher = launcher_prefix(test.command)
        always = self.always_reason(test, start)
        if always:
            return Decision(test, KIND_ALWAYS, always)
        files = sorted({real(item) for item in test.command[start:]
                        if os.path.isabs(item) and trees.is_in_build(real(item)) and not os.path.isdir(item)})
        inputs: set[str] = set(files) | {program}
        if launcher:
            inputs.update(self.launcher_inputs(launcher))
        directories: set[str] = set()
        for item in sorted(inputs):
            if not is_elf(item):
                continue
            scan = self.scan(item)
            if scan.always_reason:
                return Decision(test, KIND_ALWAYS, scan.always_reason)
            inputs.update(scan.files)
            directories.update(scan.directories)
        return Decision(test, KIND_EXECUTABLE, "", fingerprint=self.fingerprint(test, inputs, directories))

    def launcher_inputs(self, launcher: str) -> list[str]:
        """Return the files that the test launcher reads: each .py and .txt file beside it, and build-kind.txt.

        O(number of files in the directory of the launcher), one time for each call of the script.
        """
        if launcher not in self.launcher_files:
            directory = os.path.dirname(real(launcher))
            try:
                names = sorted(os.listdir(directory))
            except OSError:
                names = []
            files = [os.path.join(directory, name) for name in names if name.endswith(LAUNCHER_INPUT_SUFFIXES)]
            self.launcher_files[launcher] = [real(launcher), *files,
                                             os.path.join(self.trees.build_root, BUILD_KIND_FILE)]
        return self.launcher_files[launcher]

    def always_reason(self, test: TestRecord, start: int) -> str:
        """Return why an executable test always runs, or an empty string.

        `start` is the index after the test launcher prefix.  The launcher is
        a file of the source tree by design, and its files are inputs of the
        fingerprint, so the check of the command starts after it.
        """
        for name in ALWAYS_RUN_PROPERTIES:
            if name in test.properties:
                return f"the test has the property {name}"
        for item in test.command[start:]:
            found = source_paths_in(item, self.trees)
            if found:
                return f"the command names {found[0]} of the source tree"
        for name in ENVIRONMENT_PROPERTIES:
            values = test.properties.get(name) or []
            for value in values if isinstance(values, list) else [values]:
                found = source_paths_in(str(value), self.trees)
                if found:
                    return f"the property {name} names {found[0]} of the source tree"
        directory = test.properties.get("WORKING_DIRECTORY")
        if isinstance(directory, str) and self.trees.is_source_only(real(directory)):
            return f"the working directory {directory} is in the source tree"
        return ""

    def fingerprint(self, test: TestRecord, inputs: set[str], directories: set[str]) -> str:
        """Return the SHA-256 of the record of a test and of the bytes of each of its inputs."""
        files = set(inputs)
        for directory in directories:
            files.update(shared_libraries(directory))
        payload = json.dumps([test.canonical, sorted((path, self.digests.digest(path)) for path in files),
                              sorted(directories)])
        return hashlib.sha256(payload.encode("utf-8", "surrogateescape")).hexdigest()

    def decide(self, tests: list[TestRecord], run_all: bool) -> list[Decision]:
        """Return the decision for each test."""
        passed = self.state["passed"]
        assert isinstance(passed, dict)
        decisions: list[Decision] = []
        for test in tests:
            decision = self.classify(test)
            if decision.kind == KIND_EXECUTABLE:
                recorded = passed.get(test.name)
                if run_all:
                    decision.reason = REASON_ALL
                elif recorded is None:
                    decision.reason = REASON_NO_RECORD
                elif recorded == decision.fingerprint:
                    decision.reason = REASON_SAME
                    decision.is_skipped = True
                else:
                    decision.reason = REASON_CHANGED
            decisions.append(decision)
        return decisions


# ── The run ────────────────────────────────────────────────────────────────


def read_junit(path: Path) -> dict[str, str]:
    """Return the status of each test in a JUnit report of ctest: run, fail, notrun or disabled."""
    statuses: dict[str, str] = {}
    root = ElementTree.parse(path).getroot()
    for case in root.iter("testcase"):
        name = case.get("name")
        if name is not None:
            statuses[name] = case.get("status") or ("fail" if case.find("failure") is not None else "run")
    return statuses


def print_plan(decisions: list[Decision], build_dir: Path) -> None:
    """Print one line for each kind and reason, with its count."""
    groups = Counter(("skip" if decision.is_skipped else "run", decision.kind, decision.reason)
                     for decision in decisions)
    print(f"run-affected-tests: {len(decisions)} tests in {build_dir}")
    order = {KIND_FIXTURE: 0, KIND_SCRIPT: 1, KIND_ALWAYS: 2, KIND_EXECUTABLE: 3}
    for (action, kind, reason), count in sorted(groups.items(), key=lambda item: (item[0][0], order[item[0][1]])):
        print(f"  {action:4} {count:5}  {kind}: {reason}")


def write_list(path: Path, names: list[str]) -> None:
    """Write one test name on each line of `path`."""
    path.write_text("".join(f"{name}\n" for name in names))


def run_tests(build_dir: Path, names: list[str], state_dir: Path, jobs: int,
              ctest_arguments: list[str]) -> tuple[int, dict[str, str]]:
    """Run the named tests with ctest, and return its exit code and the status of each test."""
    selection = state_dir / "run.txt"
    report = state_dir / "junit.xml"
    write_list(selection, names)
    report.unlink(missing_ok=True)
    command = [ctest_program(), "--test-dir", str(build_dir), "--tests-from-file", str(selection),
               "--output-junit", str(report), f"-j{jobs}", *ctest_arguments]
    status = subprocess.run(command, check=False).returncode
    if not report.is_file():
        return (status or 2), {}
    try:
        return status, read_junit(report)
    except ElementTree.ParseError:
        return (status or 2), {}


def build(build_dir: Path, jobs: int) -> int:
    """Build the tree with the cmake beside ctest, and return the exit code of the build."""
    sibling = Path(ctest_program()).with_name("cmake")
    cmake = str(sibling) if sibling.is_file() else (shutil.which("cmake") or "cmake")
    return subprocess.run([cmake, "--build", str(build_dir), f"-j{jobs}"], check=False).returncode


def update_state(state: dict[str, object], planner: Planner, decisions: list[Decision],
                 statuses: dict[str, str], tests: list[TestRecord]) -> None:
    """Record the fingerprint of each executable test that passed with unchanged inputs, and drop the rest."""
    passed = state["passed"]
    assert isinstance(passed, dict)
    names = {test.name for test in tests}
    for name in list(passed):
        if name not in names:
            del passed[name]
    planner.digests.forget()
    for decision in decisions:
        if decision.kind != KIND_EXECUTABLE or decision.is_skipped:
            if decision.kind != KIND_EXECUTABLE:
                passed.pop(decision.test.name, None)
            continue
        after = planner.classify(decision.test)
        is_same = after.kind == KIND_EXECUTABLE and after.fingerprint == decision.fingerprint
        if statuses.get(decision.test.name) == "run" and is_same:
            passed[decision.test.name] = decision.fingerprint
        else:
            passed.pop(decision.test.name, None)
    state["scans"] = planner.used_scans


def save_history(recorder: loop_history.Recorder, build_dir: Path) -> None:
    """Append the line of this call to the loop history, and print the times of the call in one line."""
    problem = recorder.save()
    if problem:
        print(f"run-affected-tests: the loop history has a problem: {problem}")
    parts = []
    for name, part in (("build", recorder.line.get("build")), ("tests", recorder.line.get("tests"))):
        if isinstance(part, dict) and "wall_s" in part:
            path = f", critical path {part['critical_path_s']} s" if "critical_path_s" in part else ""
            parts.append(f"{name} {part['wall_s']:.1f} s with {part['cpu_s']:.0f} s CPU{path}")
    if parts:
        print(f"run-affected-tests: {'; '.join(parts)}.  `python3 utils/scripts/loop_history.py {build_dir}` prints "
              f"the history.")
    try:
        budgets = check_report.read_budgets()
        for finding in loop_history.judge_line(recorder.line, budgets, str(loop_history.history_path(build_dir))):
            print(finding.text())
    except (OSError, ValueError) as problem:
        print(f"run-affected-tests: the budget table cannot be read, so the line of the loop history has no "
              f"warnings: {problem}")


def run(build_dir: Path, jobs: int, should_build: bool, run_all: bool, plan_only: bool,
        ctest_arguments: list[str]) -> int:
    """Build, decide, run and record.  Return the exit code of the script."""
    if not build_dir.is_dir():
        raise RunnerError(f"{build_dir} is not a directory.  Give the path of a configured build directory.")
    trees = read_trees(build_dir)
    state_dir = build_dir / STATE_DIRECTORY
    state_dir.mkdir(exist_ok=True)
    with open(state_dir / "lock", "w") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        recorder = loop_history.Recorder(build_dir, Path(trees.source_root), jobs)
        if should_build and not plan_only:
            with recorder.start_build():
                status = build(build_dir, jobs)
            recorder.finish_build(status)
            if status != 0:
                print(f"run-affected-tests: the build failed with exit code {status}.  No test ran.")
                save_history(recorder, build_dir)
                return 1
        elif not should_build:
            print("run-affected-tests: --no-build: the script does not make sure that each executable is current.")
        state_path = state_dir / "state.json"
        state, note = load_state(state_path, trees)
        if note:
            print(f"run-affected-tests: {note}")
        tests = list_tests(build_dir)
        planner = Planner(trees, state)
        decisions = planner.decide(tests, run_all)
        skipped = [decision.test.name for decision in decisions if decision.is_skipped]
        selected = [decision.test.name for decision in decisions if not decision.is_skipped]
        write_list(state_dir / "skipped.txt", skipped)
        print_plan(decisions, build_dir)
        if plan_only:
            return 0
        status, statuses = 0, {}
        if selected:
            with recorder.test_timer:
                status, statuses = run_tests(build_dir, selected, state_dir, jobs, ctest_arguments)
        if selected and not statuses:
            print(f"run-affected-tests: ctest gave no JUnit report (exit code {status}).  The script records no pass.")
            recorder.finish_tests(status or 2, {"selected": len(selected), "skipped": len(skipped)})
            save_history(recorder, build_dir)
            return 2
        update_state(state, planner, decisions, statuses, tests)
        save_state(state_path, state)
        counts = Counter(statuses.get(name, "notrun") for name in selected)
        recorder.finish_tests(status, {"selected": len(selected), "passed": counts["run"], "failed": counts["fail"],
                                       "not_run": counts["notrun"] + counts["disabled"], "skipped": len(skipped)})
        print(f"run-affected-tests: {len(selected)} tests selected: {counts['run']} passed, {counts['fail']} failed, "
              f"{counts['notrun'] + counts['disabled']} did not run.")
        print(f"run-affected-tests: {len(skipped)} tests skipped, because {REASON_SAME} "
              f"({state_dir / 'skipped.txt'}).  The script reports no skipped test as passed.")
        if status != 0 and counts["fail"] == 0 and counts["notrun"] == 0:
            print(f"run-affected-tests: ctest exited with status {status} after its tests.  A command of "
                  f"CTestCustom.cmake in the build directory failed, for example check-test-time.py: read its "
                  f"lines above.")
        save_history(recorder, build_dir)
        return 0 if status == 0 and counts["fail"] == 0 else 1


# ── Self-test ──────────────────────────────────────────────────────────────


def synthetic_elf(allocated: bytes, other: bytes = b"", executable: bytes = b"") -> bytes:
    """Return an ELF64 file with one allocated data section, one code section and one section that is not allocated.

    The file is not a program.  The scan reads only its section headers.
    """
    body = allocated + executable + other
    table = ELF_HEADER_SIZE + len(body)
    header = bytearray(ELF_HEADER_SIZE)
    header[:4] = ELF_MAGIC
    header[4], header[5], header[6] = ELF_CLASS_64, ELF_DATA_LITTLE, 1
    struct.pack_into("<HHIQQQIHHHHHH", header, 16, 3, 0x3E, 1, 0, 0, table, 0, ELF_HEADER_SIZE, 0, 0,
                     SECTION_HEADER_SIZE, 4, 0)

    def section(kind: int, flags: int, offset: int, size: int) -> bytes:
        return struct.pack("<IIQQQQIIQQ", 0, kind, flags, 0, offset, size, 0, 0, 1, 0)

    sections = section(0, 0, 0, 0)
    sections += section(1, SHF_ALLOC, ELF_HEADER_SIZE, len(allocated))
    sections += section(1, SHF_ALLOC | SHF_EXECINSTR, ELF_HEADER_SIZE + len(allocated), len(executable))
    sections += section(1, 0, ELF_HEADER_SIZE + len(allocated) + len(executable), len(other))
    return bytes(header) + body + sections


class SelfTest:
    """A fake source tree and build tree, with a CTestTestfile.cmake written by hand."""

    def __init__(self, root: Path) -> None:
        """Make the trees under `root`."""
        self.source_dir = root / "src"
        self.build_dir = self.source_dir / "build"
        (self.build_dir / "bin").mkdir(parents=True)
        (self.build_dir / "data").mkdir()
        (self.build_dir / "lib").mkdir()
        (self.source_dir / "data").mkdir()
        (self.source_dir / "test").mkdir()
        (self.build_dir / "CMakeCache.txt").write_text(f"CMAKE_HOME_DIRECTORY:INTERNAL={self.source_dir}\n")
        self.golden = self.source_dir / "data" / "golden.csv"
        self.golden.write_text("1,2\n")
        self.library = self.build_dir / "lib" / "libnamed.so"
        self.library.write_bytes(b"library one")
        true_program, false_program = shutil.which("true"), shutil.which("false")
        if true_program is None or false_program is None or not is_elf(true_program) or not is_elf(false_program):
            raise RunnerError("the self-test needs `true` and `false` as ELF programs in PATH")
        for name in ("ok", "ok_named", "ok_source_dir", "ok_fixtures", "ok_environment", "ok_diagnostic",
                     "ok_launched"):
            shutil.copyfile(true_program, self.build_dir / "bin" / name)
            (self.build_dir / "bin" / name).chmod(0o755)
        # A stand-in for the test launcher of the tree: it drops its own
        # arguments and runs the test.  The budget table beside it is one of
        # the files that the launched test depends on.
        (self.source_dir / "scripts").mkdir()
        self.launcher = self.source_dir / "scripts" / TEST_LAUNCHER
        self.launcher.write_text("import os, sys\narguments = sys.argv[1:]\n"
                                 "if arguments[:1] == ['--warnings-dir']:\n    arguments = arguments[2:]\n"
                                 "os.execv(arguments[0], arguments)\n")
        self.budgets = self.source_dir / "scripts" / "budgets.txt"
        self.budgets.write_text("test-time | warning 5 | error 10\n")
        shutil.copyfile(false_program, self.build_dir / "bin" / "bad")
        (self.build_dir / "bin" / "bad").chmod(0o755)
        build_text, source_text = str(self.build_dir).encode(), str(self.source_dir).encode()
        (self.build_dir / "data" / "named.elf").write_bytes(synthetic_elf(
            allocated=b"\0" + source_text + b"/data/golden.csv\0" + build_text + b"/lib\0",
            other=b"\0" + source_text + b"/data/not_read_from_debug_info.csv\0",
            executable=b"\0" + source_text + b"/data/not_read_from_code.csv\0"))
        (self.build_dir / "data" / "source_dir.elf").write_bytes(synthetic_elf(
            allocated=b"\0" + source_text + b"/data\0"))
        (self.build_dir / "data" / "diagnostic.elf").write_bytes(synthetic_elf(
            allocated=b"\0" + source_text + b"/test/test_example.cpp\0"))
        self.extra_argument = ""
        self.write_tests()

    def write_tests(self) -> None:
        """Write the CTestTestfile.cmake of the fake build tree."""
        bin_dir, data = self.build_dir / "bin", self.build_dir / "data"
        lines = [
            f'add_test([=[exe_ok]=] "{bin_dir}/ok"{self.extra_argument})',
            f'add_test([=[exe_named]=] "{bin_dir}/ok_named" "{data}/named.elf")',
            f'add_test([=[exe_source_dir]=] "{bin_dir}/ok_source_dir" "{data}/source_dir.elf")',
            f'add_test([=[exe_diagnostic]=] "{bin_dir}/ok_diagnostic" "{data}/diagnostic.elf")',
            f'add_test([=[exe_bad]=] "{bin_dir}/bad")',
            f'add_test([=[exe_fixtures]=] "{bin_dir}/ok_fixtures")',
            f'set_tests_properties([=[exe_fixtures]=] PROPERTIES FIXTURES_REQUIRED "trees")',
            f'add_test([=[setup_trees]=] "{shutil.which("true")}")',
            f'set_tests_properties([=[setup_trees]=] PROPERTIES FIXTURES_SETUP "trees")',
            f'add_test([=[exe_environment]=] "{bin_dir}/ok_environment")',
            f'set_tests_properties([=[exe_environment]=] PROPERTIES ENVIRONMENT "GOLDEN={self.golden}")',
            f'add_test([=[script_guard]=] "{shutil.which("true")}" "{self.source_dir}/test/check.py")',
            f'add_test([=[neg_fixture]=] "{shutil.which("true")}" "{self.source_dir}/test/{FIXTURE_DRIVER}")',
            f'add_test([=[disabled_test]=] "{bin_dir}/ok")',
            f'set_tests_properties([=[disabled_test]=] PROPERTIES DISABLED TRUE)',
            f'add_test([=[exe_launched]=] "{sys.executable}" "-S" "{self.launcher}" "--warnings-dir" '
            f'"{self.build_dir}/check-warnings" "{bin_dir}/ok_launched")',
        ]
        (self.build_dir / "CTestTestfile.cmake").write_text("\n".join(lines) + "\n")

    def call(self, *extra: str) -> tuple[int, str]:
        """Run the script on the fake build tree without a build, and return its exit code and output."""
        result = subprocess.run([sys.executable, str(Path(__file__).resolve()), str(self.build_dir), "--no-build",
                                 "-j4", *extra], capture_output=True, text=True, check=False)
        return result.returncode, result.stdout + result.stderr

    def skipped(self) -> list[str]:
        """Return the names in skipped.txt."""
        return (self.build_dir / STATE_DIRECTORY / "skipped.txt").read_text().split()


def self_test() -> int:
    """Run each plant, and return 0 when each one gives the expected result."""
    failures: list[str] = []

    def expect(label: str, condition: bool, output: str = "") -> None:
        if not condition:
            failures.append(f"{label}\n{output}")

    # The scan of an ELF file: each kind of section, and a file that is not an ELF64 file.
    trees = Trees(source_root="/s", build_root="/s/b")
    spans = data_sections(synthetic_elf(allocated=b"A", other=b"B", executable=b"C"))
    expect("data_sections finds one allocated data section", spans == [(ELF_HEADER_SIZE, ELF_HEADER_SIZE + 1)])
    expect("data_sections refuses a file that is not ELF", data_sections(b"#!/bin/sh\n" + b"\0" * 80) is None)
    damaged = bytearray(synthetic_elf(allocated=b"A"))
    struct.pack_into("<Q", damaged, 0x28, len(damaged) + 100)
    expect("data_sections refuses a section table outside the file", data_sections(bytes(damaged)) is None)
    expect("program_index skips cmake -E env", program_index(("/x/cmake", "-E", "env", "A=1", "/b/t")) == 4)
    expect("program_index skips valgrind options", program_index(("/v/valgrind", "-q", "/b/t", "x")) == 2)
    launched = ("/usr/bin/python3", "-S", f"/s/scripts/{TEST_LAUNCHER}", "--warnings-dir", "/s/b/w", "/s/b/t")
    expect("program_index skips the test launcher", program_index(launched) == 5)
    expect("program_index skips the test launcher and then cmake -E env",
           program_index(launched[:5] + ("/x/cmake", "-E", "env", "A=1", "/s/b/t")) == 9)
    expect("a Python script is not a test launcher", program_index(("/usr/bin/python3", "/s/check.py")) == 0)
    expect("source_paths_in ignores the build tree", source_paths_in("/s/b/x /s/y", trees) == ["/s/y"])

    with tempfile.TemporaryDirectory(prefix="affected-tests-") as directory:
        fake = SelfTest(Path(directory))
        status, output = fake.call()
        expect("the first call runs each test, and exe_bad fails", status == 1 and fake.skipped() == [], output)
        expect("the first call says that no state file exists", "no state file" in output, output)
        history = loop_history.read_lines(fake.build_dir)
        expect("a call appends one line to the loop history, with the tests and with no build after --no-build",
               len(history) == 1 and history[0]["build"] is None and history[0]["tests"]["failed"] == 1
               and history[0]["tests"]["selected"] == history[0]["tests"]["passed"] + 1 + history[0]["tests"]["not_run"]
               and history[0]["jobs"] == 4, output)

        status, output = fake.call()
        expect("an unchanged tree skips exactly the passed executable tests",
               status == 1 and sorted(fake.skipped()) == ["exe_diagnostic", "exe_launched", "exe_named", "exe_ok"],
               output)
        expect("the summary gives the count of skipped tests and the reason",
               "4 tests skipped, because each input is the same as at the last pass" in output, output)

        fake.budgets.write_text("test-time | warning 5 | error 20\n")
        status, output = fake.call()
        expect("a changed file beside the test launcher makes a launched test run", "exe_launched" not in
               fake.skipped() and "exe_ok" in fake.skipped(), output)
        status, output = fake.call()
        expect("the launched test is skipped again after its pass", "exe_launched" in fake.skipped(), output)

        with open(fake.build_dir / "bin" / "ok", "ab") as handle:
            handle.write(b"\0")
        status, output = fake.call()
        expect("a changed executable runs again", "exe_ok" not in fake.skipped(), output)
        expect("another test stays skipped", "exe_named" in fake.skipped(), output)

        shutil.copyfile(fake.build_dir / "bin" / "ok", fake.build_dir / "bin" / "ok.copy")
        os.replace(fake.build_dir / "bin" / "ok.copy", fake.build_dir / "bin" / "ok")
        (fake.build_dir / "bin" / "ok").chmod(0o755)
        status, output = fake.call()
        expect("an executable with the same bytes and a new time stays skipped", "exe_ok" in fake.skipped(), output)

        fake.golden.write_text("1,3\n")
        status, output = fake.call()
        expect("a changed data file that the ELF file names makes its test run", "exe_named" not in fake.skipped(),
               output)

        status, output = fake.call()
        expect("the test is skipped again after its pass", "exe_named" in fake.skipped(), output)
        fake.library.write_bytes(b"library two")
        status, output = fake.call()
        expect("a changed shared library in a named build directory makes the test run",
               "exe_named" not in fake.skipped(), output)

        fake.extra_argument = ' "--flag"'
        fake.write_tests()
        status, output = fake.call()
        expect("a changed test record makes the test run", "exe_ok" not in fake.skipped(), output)

        for name in ("exe_source_dir", "exe_fixtures", "exe_environment", "script_guard", "neg_fixture", "exe_bad"):
            expect(f"{name} runs on each call", name not in fake.skipped(), output)
        expect("a source directory in an ELF file makes the test always run",
               "always: " in output and "which is not a file of the source tree" in output, output)

        state_path = fake.build_dir / STATE_DIRECTORY / "state.json"
        state_path.write_text("{damaged")
        status, output = fake.call()
        expect("a damaged state file makes each executable test run", fake.skipped() == [], output)
        expect("the script names the damaged state file", "is damaged" in output, output)

        status, output = fake.call()
        (fake.build_dir / "bin" / "ok").unlink()
        status, output = fake.call()
        expect("a missing executable runs and fails", "exe_ok" not in fake.skipped() and status == 1, output)
        state = json.loads(state_path.read_text())
        expect("a failed test gets no record", "exe_ok" not in state["passed"], output)
        expect("a disabled test gets no record", "disabled_test" not in state["passed"], output)

        lines_before = len(loop_history.read_lines(fake.build_dir))
        status, output = fake.call("--plan")
        expect("--plan runs no test", status == 0 and "tests selected" not in output, output)
        expect("--plan appends no line to the loop history",
               len(loop_history.read_lines(fake.build_dir)) == lines_before, output)

    status, output = subprocess.run([sys.executable, str(Path(__file__).resolve()), "/nonexistent/build"],
                                    capture_output=True, text=True, check=False).returncode, ""
    expect("a missing build directory gives exit code 2", status == 2, output)

    for failure in failures:
        print(f"run-affected-tests --self-test: FAILED: {failure}")
    if failures:
        print(f"run-affected-tests --self-test: {len(failures)} case(s) did not hold.")
        return 2
    print("run-affected-tests --self-test: every case passes.")
    return 0


# ── Main ───────────────────────────────────────────────────────────────────


def main(arguments: list[str]) -> int:
    """Parse the arguments and run the script."""
    # Each argument after "--" goes to ctest unchanged.
    separator = arguments.index("--") if "--" in arguments else len(arguments)
    ctest_arguments = arguments[separator + 1:]
    parser = argparse.ArgumentParser(description="Build, then run each test whose inputs changed.")
    parser.add_argument("build_dir", type=Path, nargs="?")
    parser.add_argument("-j", "--jobs", type=int, default=os.cpu_count() or 1)
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--plan", action="store_true")
    parser.add_argument("--ctest", help="the ctest program; the default is the ctest in PATH")
    parser.add_argument("--self-test", action="store_true")
    options = parser.parse_args(arguments[:separator])
    if options.ctest:
        os.environ[CTEST_VARIABLE] = options.ctest
    if options.self_test:
        try:
            return self_test()
        except RunnerError as error:
            print(f"run-affected-tests --self-test: {error}")
            return 2
    if options.build_dir is None or options.jobs < 1:
        print("run-affected-tests: give a build directory, and give -j a positive number.")
        return 2
    try:
        return run(options.build_dir.resolve(), options.jobs, not options.no_build, options.all, options.plan,
                   ctest_arguments)
    except RunnerError as error:
        print(f"run-affected-tests: {error}")
        return 2


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
