"""loop test: build, then run each test whose inputs changed since its last pass.

The inner loop of a developer is "edit, build, test".  A full ctest run of the
Debug build runs about 2,500 tests.  After one edit, most test executables are
the same as at their last pass, and a second run of such an executable proves
nothing new.  This subcommand builds the tree, and then it runs only these tests:

- Each negative-compile fixture.  The result store of `loop fixture` decides if
  the fixture compiles again.
- Each script test: a guard, a self-test, or another test whose program is not
  an executable of the build tree.  These tests read the source tree, so they
  always run.
- Each executable test whose fingerprint is not the fingerprint of its last
  pass.

THE EXECUTABLE TEST
    The program of an executable test is an ELF file of the build tree, after
    the wrappers: the test launcher of the tree (`python3 -S utils/bin/loop
    test-step` and its options, which CMake puts in front of each executable
    target, and crucible_script_test() in front of each script test), `cmake
    -E env` and `valgrind`.  Its fingerprint is a hash of these items:
    - The ctest record of the test: the command and each property.
    - The bytes of each file of the build tree that the command names.
    - The bytes of each file that an ELF file of the command names as an
      absolute path, in an allocated section that holds no code.  Such a name
      is a shared library, a data file, or a directory of the run path.  A
      named directory of the build tree adds each shared library in it.  A
      named file of the source tree with a C or C++ suffix is the text of a
      diagnostic, for example from __FILE__, and it is not an input.
    - For a test behind the test launcher: the bytes of the master loop, of
      each .py file of utils/assay/core and utils/assay/loop, of
      utils/registry.txt, of the ledger utils/ledgers/RULE.txt of each rule
      of the launcher, and of build-kind.txt in the build tree.  The launcher
      reads its modules, the registry and its ledgers there, and the result
      of a test depends on them.  A ledger that does not exist is an input
      too, so a new ledger makes the test run.
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
    the tests.  `loop history BUILD_DIR` prints the last lines as a table.
    utils/assay/loop/history.py gives the rules.

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

THE PROGRAMS
    The script builds with the cmake and runs the tests with the ctest that
    the CMake cache of BUILD_DIR names (utils/assay/setup/pins.py).  It runs
    no cmake and no ctest of PATH, because PATH can hold a ctest of another
    version.

Usage:
    loop test BUILD_DIR [-j N] [--no-build] [--all] [--plan] [-- CTEST_ARGUMENT...]
    guard selftest loop.affected [--ctest CTEST]

--no-build  Do not build first.  Then a stale executable can look unchanged.
--all       Run each executable test, and record each pass.
--plan      Print what the script would run, and run nothing.
--ctest     The ctest of the fake build tree of the self-test.  The default
            is the ctest of PATH, when it gives the pinned version.

Exit 0 when each test that ran passed, 1 when the build or a test failed, 2 on
a usage error, a build directory that the script cannot read, or a failed
self-test.
"""

from __future__ import annotations

import argparse
import fcntl
import hashlib
import json
import os
import re
import shutil
import struct
import sys
import tempfile
from collections import Counter
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import proc
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.paths import LOOP
from assay.loop import history as loop_history
from assay.loop import teststep as test_launcher
from assay.readers import cmake
from assay.readers import ctest as ctest_reader
from assay.readers import elf as elf_file
from assay.setup import pins as cmake_pin

# ── Constants ──────────────────────────────────────────────────────────────

STATE_VERSION = 1
STATE_DIRECTORY = "affected-tests"
# The file name of the master loop, and its subcommands that ctest starts: the
# driver of a negative fixture and the test launcher.
LOOP_MASTER = "loop"
FIXTURE_SUBCOMMAND = "fixture"
LAUNCHER_SUBCOMMAND = "test-step"
# The files that the test launcher reads: each directory relative to utils/
# (the parent of the directory of the master), and the suffix of its files.
LAUNCHER_INPUT_DIRECTORIES = (("assay/core", ".py"), ("assay/loop", ".py"))
# The other files that the test launcher reads, relative to utils/: the registry of the measured rules, and the
# ledger of each rule of the launcher.
LAUNCHER_INPUT_FILES = ("registry.txt", *(f"ledgers/{rule}.txt" for rule in test_launcher.ROWS))
BUILD_KIND_FILE = "build-kind.txt"
ALWAYS_RUN_PROPERTIES = ("FIXTURES_REQUIRED", "FIXTURES_SETUP", "FIXTURES_CLEANUP", "REQUIRED_FILES")
ENVIRONMENT_PROPERTIES = ("ENVIRONMENT", "ENVIRONMENT_MODIFICATION")
# A name in an ELF file with one of these suffixes is the text of a
# diagnostic, such as __FILE__ or std::source_location, and not a data file.
DIAGNOSTIC_SUFFIXES = (".h", ".hh", ".hpp", ".hxx", ".c", ".cc", ".cpp", ".cxx", ".inc", ".ipp", ".tpp")
SHARED_LIBRARY = re.compile(r"\.so(\.[0-9]+)*$")
PATH_BYTES = rb"[A-Za-z0-9_./+@-]"

KIND_FIXTURE = "fixture"
KIND_SCRIPT = "script"
KIND_ALWAYS = "always"
KIND_EXECUTABLE = "executable"

# The reason of each decision, as the summary prints it.
REASON_FIXTURE = "the result store of `loop fixture` decides"
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


@dataclass
class Decision:
    """What the script does with one test, and why."""

    test: ctest_reader.TestEntry
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

    The source tree comes from CMAKE_HOME_DIRECTORY of the CMake cache (assay.readers.cmake).
    """
    if not cmake.is_configured(build_dir):
        raise RunnerError(f"{build_dir} is not a configured build directory.  Configure it first, for example "
                          f"`cmake --preset default`, and give its path.")
    try:
        found = cmake.cache(build_dir).get("CMAKE_HOME_DIRECTORY")
    except cmake.CmakeError as problem:
        raise RunnerError(f"the CMake cache of {build_dir} cannot be read: {problem}") from None
    if found is None:
        raise RunnerError(f"the CMake cache of {build_dir} holds no CMAKE_HOME_DIRECTORY.  {cmake.AGAIN}")
    source = found.value.strip()
    if not Path(source).is_dir():
        raise RunnerError(f"the CMake cache of {build_dir} names the source tree {source}, which does not exist.  "
                          f"{cmake.AGAIN}")
    return Trees(source_root=real(source), build_root=real(str(build_dir)))


def build_program(build_dir: Path, name: str) -> str:
    """Return the cmake or the ctest that the CMake cache of the build directory names, or stop."""
    try:
        return cmake_pin.configured_program(build_dir, name)
    except cmake_pin.PinError as error:
        raise RunnerError(str(error)) from None


def list_tests(build_dir: Path) -> tuple[ctest_reader.TestEntry, ...]:
    """Return each test of the build directory (assay.readers.ctest)."""
    try:
        return ctest_reader.tests(build_dir)
    except ctest_reader.CtestError as problem:
        raise RunnerError(str(problem)) from None


def canonical(test: ctest_reader.TestEntry) -> str:
    """Return the record of a test as one JSON text: its name, its command and each property."""
    return json.dumps({"name": test.name, "command": test.command, "properties": dict(test.properties)},
                      sort_keys=True)


def starts_subcommand(command: tuple[str, ...], index: int, subcommand: str) -> bool:
    """Return True when the word at `index` is the master loop and the next word is `subcommand`."""
    return (index + 1 < len(command) and os.path.basename(command[index]) == LOOP_MASTER
            and command[index + 1] == subcommand)


def is_fixture(command: tuple[str, ...]) -> bool:
    """Return True when the command starts the driver of a negative fixture, `loop fixture`.  O(length of command)."""
    return any(starts_subcommand(command, index, FIXTURE_SUBCOMMAND) for index in range(len(command)))


def launcher_prefix(command: tuple[str, ...]) -> tuple[int, str]:
    """Return the index after the test launcher prefix and the path of the master, or (0, "") with no launcher.

    The prefix is a Python interpreter, its options, the master loop, the
    subcommand test-step and its options with their values
    (assay.loop.teststep.OPTIONS).
    """
    if not command or not os.path.basename(command[0]).startswith("python"):
        return 0, ""
    index = 1
    while index < len(command) and command[index].startswith("-"):
        index += 1
    if not starts_subcommand(command, index, LAUNCHER_SUBCOMMAND):
        return 0, ""
    master = command[index]
    index += 2
    while index < len(command) and command[index] in test_launcher.OPTIONS:
        index += 2
    return index, master


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


def data_sections(elf: elf_file.ElfFile) -> list[tuple[int, int]]:
    """Return the byte span of each allocated section that holds no code.  O(number of sections).

    Raises:
        elf_file.ElfError: If the file has no section header table, or a data section is outside the file
    """
    if not elf.sections:
        raise elf_file.ElfError(f"{elf.where}: the file has no section header table")
    return [elf.span(section) for section in elf.sections if section.is_data]


def name_pattern(trees: Trees) -> re.Pattern[bytes]:
    """Return the pattern of an absolute path of the source tree or of the build tree, as bytes."""
    roots = sorted({trees.source_root, trees.build_root}, key=len, reverse=True)
    alternatives = b"|".join(re.escape(root.encode()) for root in roots)
    return re.compile(b"(?<!" + PATH_BYTES + b")(?:" + alternatives + b")(?:/" + PATH_BYTES + b"*)?(?!"
                      + PATH_BYTES + b")")


def scan_elf(path: str, trees: Trees) -> Scan:
    """Return the input names of one ELF file.  O(size of its data sections)."""
    try:
        with elf_file.ElfFile.mapped(path) as elf:
            spans = data_sections(elf)
            pattern = name_pattern(trees)
            names = {match.group(0).decode("utf-8", "surrogateescape")
                     for start, end in spans for match in pattern.finditer(elf.buffer, start, end)}
    except elf_file.ElfError as error:
        return Scan(always_reason=f"{path} is not an ELF64 file with a section header table: {error}")
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

    def classify(self, test: ctest_reader.TestEntry) -> Decision:
        """Return the kind of a test and, for an executable test, its fingerprint."""
        trees = self.trees
        if is_fixture(test.command):
            return Decision(test, KIND_FIXTURE, REASON_FIXTURE)
        index = program_index(test.command)
        if index is None:
            return Decision(test, KIND_SCRIPT, REASON_SCRIPT)
        program = real(test.command[index]) if os.path.isabs(test.command[index]) else test.command[index]
        if not os.path.isabs(program) or not trees.is_in_build(program):
            return Decision(test, KIND_SCRIPT, REASON_SCRIPT)
        if os.path.exists(program) and not elf_file.is_elf(program):
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
            if not elf_file.is_elf(item):
                continue
            scan = self.scan(item)
            if scan.always_reason:
                return Decision(test, KIND_ALWAYS, scan.always_reason)
            inputs.update(scan.files)
            directories.update(scan.directories)
        return Decision(test, KIND_EXECUTABLE, "", fingerprint=self.fingerprint(test, inputs, directories))

    def launcher_inputs(self, launcher: str) -> list[str]:
        """Return the files that the test launcher reads: the master, its modules, its data files and build-kind.txt.

        LAUNCHER_INPUT_DIRECTORIES gives the directories of the modules, and
        LAUNCHER_INPUT_FILES gives the registry and the ledgers.
        O(number of files in those directories), one time for each call of the
        script.
        """
        if launcher not in self.launcher_files:
            utils = os.path.dirname(os.path.dirname(real(launcher)))
            files: list[str] = []
            for relative, suffix in LAUNCHER_INPUT_DIRECTORIES:
                directory = os.path.join(utils, relative)
                try:
                    names = sorted(os.listdir(directory))
                except OSError:
                    names = []
                files.extend(os.path.join(directory, name) for name in names if name.endswith(suffix))
            files.extend(os.path.join(utils, name) for name in LAUNCHER_INPUT_FILES)
            self.launcher_files[launcher] = [real(launcher), *files,
                                             os.path.join(self.trees.build_root, BUILD_KIND_FILE)]
        return self.launcher_files[launcher]

    def always_reason(self, test: ctest_reader.TestEntry, start: int) -> str:
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
            for value in test.words_of(name):
                found = source_paths_in(value, self.trees)
                if found:
                    return f"the property {name} names {found[0]} of the source tree"
        for directory in test.words_of("WORKING_DIRECTORY"):
            if self.trees.is_source_only(real(directory)):
                return f"the working directory {directory} is in the source tree"
        return ""

    def fingerprint(self, test: ctest_reader.TestEntry, inputs: set[str], directories: set[str]) -> str:
        """Return the SHA-256 of the record of a test and of the bytes of each of its inputs."""
        files = set(inputs)
        for directory in directories:
            files.update(shared_libraries(directory))
        payload = json.dumps([canonical(test), sorted((path, self.digests.digest(path)) for path in files),
                              sorted(directories)])
        return hashlib.sha256(payload.encode("utf-8", "surrogateescape")).hexdigest()

    def decide(self, tests: tuple[ctest_reader.TestEntry, ...], run_all: bool) -> list[Decision]:
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


def print_plan(decisions: list[Decision], build_dir: Path) -> None:
    """Print one line for each kind and reason, with its count."""
    groups = Counter(("skip" if decision.is_skipped else "run", decision.kind, decision.reason)
                     for decision in decisions)
    print(f"loop test: {len(decisions)} tests in {build_dir}")
    order = {KIND_FIXTURE: 0, KIND_SCRIPT: 1, KIND_ALWAYS: 2, KIND_EXECUTABLE: 3}
    for (action, kind, reason), count in sorted(groups.items(), key=lambda item: (item[0][0], order[item[0][1]])):
        print(f"  {action:4} {count:5}  {kind}: {reason}")


def write_list(path: Path, names: list[str]) -> None:
    """Write one test name on each line of `path`."""
    path.write_text("".join(f"{name}\n" for name in names))


def run_inherited(command: list[str]) -> int:
    """Run a build or a test run with the standard streams of this process, and return its exit code.

    A command that runs past the limit BUILD_S of assay.core.proc gives the exit code of its end on the signal of the
    limit.

    Raises:
        RunnerError: If the program cannot start
    """
    try:
        return proc.run(proc.Command(command, timeout_s=proc.BUILD_S, stdout=proc.Output.INHERIT,
                                     stderr=proc.Output.INHERIT)).exit
    except proc.TimedOut as problem:
        print(f"loop test: {problem}")
        return problem.completed.exit if problem.completed is not None else 2
    except proc.CannotStart as problem:
        raise RunnerError(str(problem)) from None


def run_tests(build_dir: Path, names: list[str], state_dir: Path, jobs: int,
              ctest_arguments: list[str]) -> tuple[int, dict[str, ctest_reader.TestStatus]]:
    """Run the named tests with ctest, and return its exit code and the status of each test of its JUnit report."""
    selection = state_dir / "run.txt"
    report = state_dir / "junit.xml"
    write_list(selection, names)
    report.unlink(missing_ok=True)
    command = [build_program(build_dir, "ctest"), "--test-dir", str(build_dir), "--tests-from-file", str(selection),
               "--output-junit", str(report), f"-j{jobs}", *ctest_arguments]
    status = run_inherited(command)
    if not report.is_file():
        return (status or 2), {}
    try:
        return status, ctest_reader.results(report)
    except ctest_reader.CtestError as problem:
        check_report.write_line(f"loop test: {problem}")
        return (status or 2), {}


def build(build_dir: Path, jobs: int) -> int:
    """Build the tree with the cmake of the build directory, and return the exit code of the build."""
    return run_inherited([build_program(build_dir, "cmake"), "--build", str(build_dir), f"-j{jobs}"])


def update_state(state: dict[str, object], planner: Planner, decisions: list[Decision],
                 statuses: dict[str, ctest_reader.TestStatus], tests: tuple[ctest_reader.TestEntry, ...]) -> None:
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
        if statuses.get(decision.test.name) == ctest_reader.TestStatus.RUN and is_same:
            passed[decision.test.name] = decision.fingerprint
        else:
            passed.pop(decision.test.name, None)
    state["scans"] = planner.used_scans


def save_history(recorder: loop_history.Recorder, build_dir: Path) -> None:
    """Append the line of this call to the loop history, and print the times of the call in one line."""
    problem = recorder.save()
    if problem:
        print(f"loop test: the loop history has a problem: {problem}")
    parts = []
    for name, part in (("build", recorder.line.get("build")), ("tests", recorder.line.get("tests"))):
        if isinstance(part, dict) and "wall_s" in part:
            path = f", critical path {part['critical_path_s']} s" if "critical_path_s" in part else ""
            parts.append(f"{name} {part['wall_s']:.1f} s with {part['cpu_s']:.0f} s CPU{path}")
    if parts:
        print(f"loop test: {'; '.join(parts)}.  `loop history {build_dir}` prints the history.")
    try:
        budgets = check_report.read_budgets()
        for finding in loop_history.judge_line(recorder.line, budgets, str(loop_history.history_path(build_dir))):
            print(finding.text())
    except (OSError, ValueError) as problem:
        print(f"loop test: the registry cannot be read, so the line of the loop history has no "
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
                print(f"loop test: the build failed with exit code {status}.  No test ran.")
                save_history(recorder, build_dir)
                return 1
        elif not should_build:
            print("loop test: --no-build: the script does not make sure that each executable is current.")
        state_path = state_dir / "state.json"
        state, note = load_state(state_path, trees)
        if note:
            print(f"loop test: {note}")
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
            print(f"loop test: ctest gave no JUnit report (exit code {status}).  The script records no pass.")
            recorder.finish_tests(status or 2, {"selected": len(selected), "skipped": len(skipped)})
            save_history(recorder, build_dir)
            return 2
        update_state(state, planner, decisions, statuses, tests)
        save_state(state_path, state)
        counts = Counter(statuses.get(name, ctest_reader.TestStatus.NOTRUN) for name in selected)
        passed, failed = counts[ctest_reader.TestStatus.RUN], counts[ctest_reader.TestStatus.FAIL]
        not_run = counts[ctest_reader.TestStatus.NOTRUN] + counts[ctest_reader.TestStatus.DISABLED]
        recorder.finish_tests(status, {"selected": len(selected), "passed": passed, "failed": failed,
                                       "not_run": not_run, "skipped": len(skipped)})
        print(f"loop test: {len(selected)} tests selected: {passed} passed, {failed} failed, {not_run} did not run.")
        print(f"loop test: {len(skipped)} tests skipped, because {REASON_SAME} "
              f"({state_dir / 'skipped.txt'}).  The script reports no skipped test as passed.")
        if status != 0 and failed == 0 and counts[ctest_reader.TestStatus.NOTRUN] == 0:
            print(f"loop test: ctest exited with status {status} after its tests, and no test failed: read its "
                  f"lines above.")
        save_history(recorder, build_dir)
        return 0 if status == 0 and failed == 0 else 1


# ── Self-test ──────────────────────────────────────────────────────────────


def synthetic_elf(allocated: bytes, other: bytes = b"", executable: bytes = b"") -> bytes:
    """Return an ELF64 file with one allocated data section, one code section and one section that is not allocated.

    The file is not a program.  The scan reads only its section headers.
    """
    return elf_file.make_file([elf_file.Planted(b".data", allocated, flags=elf_file.SHF_ALLOC),
                               elf_file.Planted(b".text", executable,
                                                flags=elf_file.SHF_ALLOC | elf_file.SHF_EXECINSTR),
                               elf_file.Planted(b".comment", other)])


class SelfTest:
    """A fake source tree and build tree, with a CTestTestfile.cmake written by hand."""

    def __init__(self, root: Path, ctest: str) -> None:
        """Make the trees under `root`.  The cache of the fake build tree names `ctest`."""
        self.source_dir = root / "src"
        self.build_dir = self.source_dir / "build"
        (self.build_dir / "bin").mkdir(parents=True)
        (self.build_dir / "data").mkdir()
        (self.build_dir / "lib").mkdir()
        (self.source_dir / "data").mkdir()
        (self.source_dir / "test").mkdir()
        cmake.cache_file(self.build_dir).write_text("")
        cmake.plant(self.build_dir, cache={"CMAKE_HOME_DIRECTORY": str(self.source_dir), "CMAKE_CTEST_COMMAND": ctest})
        self.golden = self.source_dir / "data" / "golden.csv"
        self.golden.write_text("1,2\n")
        self.library = self.build_dir / "lib" / "libnamed.so"
        self.library.write_bytes(b"library one")
        true_program, false_program = shutil.which("true"), shutil.which("false")
        if (true_program is None or false_program is None or not elf_file.is_elf(true_program)
                or not elf_file.is_elf(false_program)):
            raise RunnerError("the self-test needs `true` and `false` as ELF programs in PATH")
        for name in ("ok", "ok_named", "ok_source_dir", "ok_fixtures", "ok_environment", "ok_diagnostic",
                     "ok_launched"):
            shutil.copyfile(true_program, self.build_dir / "bin" / name)
            (self.build_dir / "bin" / name).chmod(0o755)
        # A stand-in for the master loop of the tree: its subcommand test-step
        # drops its own arguments and runs the test.  The registry in utils/
        # is one of the files that the launched test depends on.
        (self.source_dir / "utils" / "bin").mkdir(parents=True)
        (self.source_dir / "utils" / "ledgers").mkdir()
        self.launcher = self.source_dir / "utils" / "bin" / LOOP_MASTER
        self.launcher.write_text("import os, sys\narguments = sys.argv[2:]\n"
                                 f"while arguments[:1] and arguments[0] in {test_launcher.OPTIONS!r}:\n"
                                 "    arguments = arguments[2:]\n"
                                 "os.execv(arguments[0], arguments)\n")
        self.registry = self.source_dir / "utils" / "registry.txt"
        self.plant_registry(5, 10)
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

    def plant_registry(self, warn: float, error: float) -> None:
        """Write the registry of the fake source tree, with the measured rule test-time."""
        self.registry.write_text(rule_registry.planted([rule_registry.measured_rule("test-time", warn, error, "s")]))

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
            (f'add_test([=[neg_fixture]=] "{shutil.which("true")}" "{self.source_dir}/utils/bin/{LOOP_MASTER}" '
             f'"{FIXTURE_SUBCOMMAND}")'),
            f'add_test([=[disabled_test]=] "{bin_dir}/ok")',
            f'set_tests_properties([=[disabled_test]=] PROPERTIES DISABLED TRUE)',
            (f'add_test([=[exe_launched]=] "{sys.executable}" "-S" "{self.launcher}" "{LAUNCHER_SUBCOMMAND}" '
             f'"--warnings-dir" "{self.build_dir}/check-warnings" "--build-dir" "{self.build_dir}" '
             f'"{bin_dir}/ok_launched")'),
            (f'add_test([=[script_launched]=] "{sys.executable}" "-S" "{self.launcher}" "{LAUNCHER_SUBCOMMAND}" '
             f'"--build-dir" "{self.build_dir}" "--name" "script_launched" "{shutil.which("true")}" '
             f'"{self.source_dir}/test/check.py")'),
        ]
        (self.build_dir / "CTestTestfile.cmake").write_text("\n".join(lines) + "\n")

    def call(self, *extra: str, path: str | None = None) -> tuple[int, str]:
        """Run the script on the fake build tree without a build, and return its exit code and output.

        With `path`, the script runs with that PATH.
        """
        environment = proc.environment({"PATH": path}) if path is not None else None
        result = proc.run(proc.Command([sys.executable, str(LOOP), "test", str(self.build_dir), "--no-build", "-j4",
                                        *extra], timeout_s=proc.TOOL_S, env=environment))
        return result.exit, result.out + result.err

    def skipped(self) -> list[str]:
        """Return the names in skipped.txt."""
        return (self.build_dir / STATE_DIRECTORY / "skipped.txt").read_text().split()


def self_test(ctest: str) -> int:
    """Run each plant with `ctest` as the ctest of the fake build tree, and return 0 when each one holds."""
    failures: list[str] = []

    def expect(label: str, condition: bool, output: str = "") -> None:
        if not condition:
            failures.append(f"{label}\n{output}")

    # The scan of an ELF file: each kind of section, and a file that is not an ELF64 file.
    trees = Trees(source_root="/s", build_root="/s/b")
    def is_refused(data: bytes) -> bool:
        try:
            data_sections(elf_file.ElfFile(data, "planted"))
        except elf_file.ElfError:
            return True
        return False

    spans = data_sections(elf_file.ElfFile(synthetic_elf(allocated=b"A", other=b"B", executable=b"C"), "planted"))
    expect("data_sections finds one allocated data section",
           spans == [(elf_file.HEADER_SIZE, elf_file.HEADER_SIZE + 1)])
    expect("data_sections refuses a file that is not ELF", is_refused(b"#!/bin/sh\n" + b"\0" * 80))
    damaged = bytearray(synthetic_elf(allocated=b"A"))
    struct.pack_into("<Q", damaged, 0x28, len(damaged) + 100)
    expect("data_sections refuses a section table outside the file", is_refused(bytes(damaged)))
    no_table = bytearray(synthetic_elf(allocated=b"A"))
    struct.pack_into("<Q", no_table, 0x28, 0)
    expect("data_sections refuses a file with no section header table", is_refused(bytes(no_table)))
    expect("program_index skips cmake -E env", program_index(("/x/cmake", "-E", "env", "A=1", "/b/t")) == 4)
    expect("program_index skips valgrind options", program_index(("/v/valgrind", "-q", "/b/t", "x")) == 2)
    launched = ("/usr/bin/python3", "-S", f"/s/utils/bin/{LOOP_MASTER}", LAUNCHER_SUBCOMMAND, "--warnings-dir",
                "/s/b/w", "/s/b/t")
    expect("program_index skips the test launcher", program_index(launched) == 6)
    expect("program_index skips the test launcher and then cmake -E env",
           program_index(launched[:6] + ("/x/cmake", "-E", "env", "A=1", "/s/b/t")) == 10)
    expect("program_index skips each option of the test launcher",
           program_index(launched[:6] + ("--build-dir", "/s/b", "--name", "a_test", "/usr/bin/python3",
                                         "/s/check.py")) == 10)
    expect("a Python script is not a test launcher", program_index(("/usr/bin/python3", "/s/check.py")) == 0)
    expect("another subcommand of the master is not a test launcher",
           program_index(("/usr/bin/python3", f"/s/utils/bin/{LOOP_MASTER}", FIXTURE_SUBCOMMAND, "/s/b/t")) == 0)
    expect("the fixture driver is the master loop with the subcommand fixture",
           is_fixture(("/usr/bin/python3", "-S", f"/s/utils/bin/{LOOP_MASTER}", FIXTURE_SUBCOMMAND, "/s/b"))
           and not is_fixture(launched))
    expect("source_paths_in ignores the build tree", source_paths_in("/s/b/x /s/y", trees) == ["/s/y"])

    with tempfile.TemporaryDirectory(prefix="affected-tests-") as directory:
        fake = SelfTest(Path(directory), ctest)
        # A ctest of PATH that fails on each call.  The script runs the ctest of the cache.
        wrong = Path(directory) / "wrong-path"
        wrong.mkdir()
        (wrong / "ctest").write_text("#!/bin/sh\necho 'the ctest of PATH ran'\nexit 3\n")
        (wrong / "ctest").chmod(0o755)
        status, output = fake.call(path=f"{wrong}:{os.environ.get('PATH', '')}")
        expect("the first call runs each test with the ctest of the cache, and exe_bad fails",
               status == 1 and fake.skipped() == [] and "the ctest of PATH ran" not in output, output)
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

        fake.plant_registry(5, 20)
        status, output = fake.call()
        expect("a changed registry of the test launcher makes a launched test run", "exe_launched" not in
               fake.skipped() and "exe_ok" in fake.skipped(), output)
        status, output = fake.call()
        expect("the launched test is skipped again after its pass", "exe_launched" in fake.skipped(), output)
        (fake.source_dir / "utils" / "ledgers" / "test-time.txt").write_text(
            "# a planted ledger\nformat 1\nrule test-time\n- | x86_64-debug-asan | 9 | exe_other | a planted reason\n")
        status, output = fake.call()
        expect("a new ledger of the test launcher makes a launched test run", "exe_launched" not in
               fake.skipped() and "exe_ok" in fake.skipped(), output)
        status, output = fake.call()
        (fake.source_dir / "utils" / "ledgers" / "history.txt").write_text("# a ledger of a different rule\n")
        status, output = fake.call()
        expect("a ledger of a rule that the test launcher does not read leaves a launched test skipped",
               "exe_launched" in fake.skipped(), output)

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

        for name in ("exe_source_dir", "exe_fixtures", "exe_environment", "script_guard", "neg_fixture", "exe_bad",
                     "script_launched"):
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

        cmake.plant(fake.build_dir, cache={"CMAKE_HOME_DIRECTORY": str(fake.source_dir)})
        status, output = fake.call()
        expect("a cache that names no ctest gives exit code 2 and the reason",
               status == 2 and "has no entry CMAKE_CTEST_COMMAND" in output, output)

    status, output = proc.run(proc.Command([sys.executable, str(LOOP), "test", "/nonexistent/build"],
                                           timeout_s=proc.TOOL_S)).exit, ""
    expect("a missing build directory gives exit code 2", status == 2, output)

    for failure in failures:
        print(f"loop test --self-test: FAILED: {failure}")
    if failures:
        print(f"loop test --self-test: {len(failures)} case(s) did not hold.")
        return 2
    print("loop test --self-test: every case passes.")
    return 0


# ── Main ───────────────────────────────────────────────────────────────────


def main(argv: list[str]) -> int:
    """Parse the arguments and run the subcommand or the self-test.

    Args:
        argv: The words after `loop test`, or --self-test and its words

    Returns:
        The exit status of the run or of the self-test
    """
    arguments = argv
    # Each argument after "--" goes to ctest unchanged.
    separator = arguments.index("--") if "--" in arguments else len(arguments)
    ctest_arguments = arguments[separator + 1:]
    parser = argparse.ArgumentParser(prog="loop test", description="Build, then run each test whose inputs changed.")
    parser.add_argument("build_dir", type=Path, nargs="?")
    parser.add_argument("-j", "--jobs", type=int, default=96)
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--plan", action="store_true")
    parser.add_argument("--ctest", help="the ctest of the fake build tree of --self-test; the default is the ctest "
                                        "of PATH, when it gives the pinned version")
    parser.add_argument("--self-test", action="store_true")
    options = parser.parse_args(arguments[:separator])
    if options.self_test:
        try:
            return self_test(options.ctest or cmake_pin.pinned_program("ctest"))
        except (RunnerError, cmake_pin.PinError) as error:
            print(f"loop test --self-test: {error}")
            return 2
    if options.ctest:
        print("loop test: --ctest is only for --self-test.  The script runs the ctest of the build "
              "directory.")
        return 2
    if options.build_dir is None or options.jobs < 1:
        print("loop test: give a build directory, and give -j a positive number.")
        return 2
    try:
        return run(options.build_dir.resolve(), options.jobs, not options.no_build, options.all, options.plan,
                   ctest_arguments)
    except RunnerError as error:
        print(f"loop test: {error}")
        return 2
