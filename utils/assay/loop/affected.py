"""loop test: build, then run each test whose inputs changed since its last pass.

The inner loop of a developer is "edit, build, test".  A full ctest run of the
Debug build runs about 2,500 tests.  After one edit, most test executables are
the same as at their last pass, and a second run of such an executable proves
nothing new.  This subcommand builds the tree, and then it runs only these tests:

- Each negative-compile fixture.  The result store of `loop fixture` decides if
  the fixture compiles again.
- Each test of the master guard with changed inputs (THE TESTS OF THE MASTER
  GUARD).
- Each other script test: a self-test of a module, or another test whose
  program is not an executable of the build tree.  These tests read the source
  tree, and they always run.
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
      each module of the import closure of assay.loop.teststep, of the
      ledger utils/ledgers/RULE.txt of each rule of the launcher, and of
      build-kind.txt and build-configuration.txt in the build tree.  Also
      the text of the row of each rule of the launcher in
      utils/registry.txt.  The launcher reads these files and rows, and the
      result of a test changes with them.  A ledger that the tree does not
      hold is an input too.  A new ledger then makes the test run.
    - The environment of the test (THE ENVIRONMENT).
    - The key parts of the read set of the test (THE READ SET), or a mark
      that the test has no read set.
    The scan of an ELF file reads its section headers, so it finds a name in
    .rodata, .data or .dynstr, and it does not read the code or the debug
    information.

THE READ SET
    The test launcher can trace a run of a test with a program in the build
    tree.  It then writes the read set of the run (assay.loop.readset).  The
    read set holds each file that the test and its children opened for read,
    ran or listed.  It also holds a file that the test opened by a path that
    it made at run time.  The fingerprint holds these items of the key parts:
    - The bytes of each key part that is a file
    - The names in each key part that is a directory
    - The mark "missing" of each key part with no file.
    A read set is usable when it is settled, and its command is the command
    of the test after the launcher.  Its run must also have the status 0,
    unless the property PASS_REGULAR_EXPRESSION or WILL_FAIL gives the
    verdict.  The launcher traces each run with a different status again.
    The read set of such a test then comes from its last run.  A test with a
    read set that is not usable always runs, and the launcher then traces it
    again.  A test with no read set has the fingerprint of the
    other items only.  After a run with the status 0, the script records the
    fingerprint with the read set of that run.  It records nothing when a key
    part has a different signature than at the end of the run.  The file then
    changed after the test read it.

THE TESTS OF THE MASTER GUARD
    A test can start the master guard of the source tree with a Python
    interpreter.  assay.loop.inputs gives the inputs of two kinds of such a
    test.  A rule test runs `guard check --rule ID`, `guard witness --rule
    ID` or `guard witness --engine plugin`.  A group test runs `guard
    selftest GROUP`.  The inputs are the modules of the import closure, the
    ledger, the plants, the files of the scope, the rule table and the row
    of the rule.  The files that the search of the module of the rule reads
    are inputs too.  Only a test that can read the history gets the HEAD
    commit.  Only a test that can list the files of the tree gets the list
    of the files, and each .gitignore and .gitattributes (assay.loop.inputs,
    THE TEXTS OF GIT).

    The fingerprint is a hash of the ctest record of the test, of these
    inputs and of the inputs of the test launcher.  It also holds the bytes
    of each program and each file of the build tree that the command or the
    environment names.  A change of one plant then runs the tests of its
    rule.  It also runs the tests of the rules that read each plant:
    prose-bans and rule-witness.  A rule test that reads a build, and a test
    of the master with other words, run on each call.

    ctest gets the option `--fixture-exclude-setup tsast_trees`.  A test
    with the fixture of the parse trees then reads the tree cache of the
    last fill.  The parts of the rule parse-clean run only when a C++ file
    changes.

THE ENVIRONMENT
    The test launcher gives each test a clean environment
    (assay.loop.teststep, THE ENVIRONMENT).  The fingerprint of a test
    behind the launcher holds the clean environment that the launcher makes
    from the environment of this script.  ctest gives the test the
    environment of this script, and the property ENVIRONMENT of the test is
    in its record.  A variable of the shell that the launcher drops then
    changes no fingerprint.  The fingerprint of a test with no launcher holds
    each variable of the environment of this script.

THE TESTS THAT ALWAYS RUN
    The script cannot see each input of these tests, so they always run:
    - A test with the property FIXTURES_REQUIRED, FIXTURES_SETUP,
      FIXTURES_CLEANUP or REQUIRED_FILES.
    - A test whose ELF file the script cannot read as an ELF64 file.
    - A test with a read set that is not usable (THE READ SET).
    These tests always run too, unless they have a usable read set:
    - A test whose command, environment or working directory names a path of
      the source tree that is not in the build tree.
    - A test whose ELF file names a directory of the source tree, or a path of
      the source tree that is not a file.
    The read set holds each file of the source tree that such a test opened,
    and the key of the test then holds those files.

THE PARSE BUILDS
    After the build, the script runs `loop syntax` on each configured parse
    build that the change of the tree makes necessary (assay.loop.syntax, THE
    CHANGE OF AN ARM).  The parse of a parse build is a build, and its time
    goes into the build time of THE LOOP HISTORY.  The tests
    guard_parse_x86_64_v3 and guard_parse_aarch64 then only judge the
    reports.  A unit that does not parse stops the call before the tests, as
    a build with an error does.  For a parse build that is not configured, the script
    prints the command that configures it.

THE LOOP HISTORY
    After each call that builds or runs tests, the script appends one line to
    BUILD_DIR/loop-history/history.jsonl: the commit, the job count, the host
    load, the wall time, the CPU time and the critical path of the build, the
    compiles and the links that ran, and the wall time and the CPU time of
    the tests.  `loop history BUILD_DIR` prints the last lines as a table.
    utils/assay/loop/history.py gives the rules.

THE RECORD
    After a test of FINGERPRINTED passes, the script records its fingerprint
    in BUILD_DIR/affected-tests/state.json.  A test that ctest does not run
    gets no record, and a test whose inputs changed while it ran gets none.
    The two run on the next call.  A test that fails gets its fingerprint in
    the failures of the state.  The script never reports a test as passed that it did not
    run.  It prints the number of tests that it skipped and the reason, and
    it writes their names to BUILD_DIR/affected-tests/skipped.txt.  A lock on
    BUILD_DIR/affected-tests/lock keeps two calls on one build directory
    apart.

THE SHARED STORE
    The result of each run with the status 0 also goes into one store: the
    cache STORE_NAME of assay.core.cache.  The warm trees of the gate of `loop land`, the copy of
    `loop gauge` and each build directory of a developer then share them.  A
    test of FINGERPRINTED with no such run in its own state is skipped when
    the store holds a result for its shared key.  These are the rules:
      * The shared key is the fingerprint with ROOT_MARK for the source root
        and BUILD_MARK for the build directory.  The marks replace the roots
        in each word of the record, each path and each text (relocated() of
        assay.core.cost_store).  The files build-kind.txt and
        build-configuration.txt are not in the key, because only the judge of
        the budget of the launcher reads them.
      * An entry is the file KEY in the directory of the first two digits of
        KEY.  It holds the format, the verdict and the SHA-256 of the record of
        the run (assay.core.record).  The script writes an entry only after a
        run with the status 0 and with unchanged inputs.  It removes the entry
        of a test after a run with a different status.
      * A test runs again when its last run in this build directory had a
        status that is not 0 with the same fingerprint.  It runs also when the
        store holds a result for its key.  A failure of this build directory
        then stays in the output.
      * A skip through the store writes no record of the test into this build
        directory.  The judges of the budgets, such as the rule test-time, read
        the records of their own build directory.  A test with no record keeps
        its row.
      * --all runs each test, and it writes the store after each run with the
        status 0.
      * hold_bound() of assay.core.cache keeps the store in its row of LIMITS
        after each call.
    The judge of the launcher is lenient in a build of a kind with tsan.  A
    ledger row keeps a test only in its configuration.  The result of such a
    build then stands for each build directory with the same key.  The key of
    an executable test holds the bytes of its program.  The build writes the
    paths of the checkout into the program.  Two work trees then seldom share
    the result of an executable test.

    With no read set, the script assumes that an executable test reads no
    file of the source tree, unless its command, its environment or its ELF
    file names that file.  The tests of this tree write their scratch files
    in the build tree or in /tmp, and they read /proc and /sys.

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
import contextlib
import fcntl
import hashlib
import io
import json
import os
import re
import shutil
import struct
import sys
import tempfile
from collections import Counter
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import cache, cost_store, git, proc
from assay.core import cache_root as cache_rule
from assay.core import meter as cost_meter
from assay.core import record as cost_record
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.paths import LOOP
from assay.loop import history as loop_history
from assay.loop import inputs as guard_inputs
from assay.loop import readset
from assay.loop import teststep as test_launcher
from assay.readers import cmake
from assay.readers import ctest as ctest_reader
from assay.readers import elf as elf_file
from assay.setup import pins as cmake_pin

# ── Constants ──────────────────────────────────────────────────────────────

STATE_VERSION = 3
STATE_DIRECTORY = "affected-tests"
# The name of the shared store in the root of the caches and in LIMITS of assay.core.cache.  Then the format of an
# entry and of a key, and the verdict of an entry (THE SHARED STORE).
STORE_NAME = "test-results"
STORE_FORMAT = "crucible-test-result 1"
VERDICT_PASSED = "passed"
# The file name of the master loop, and its subcommands that ctest starts: the
# driver of a negative fixture and the test launcher.
LOOP_MASTER = "loop"
FIXTURE_SUBCOMMAND = "fixture"
LAUNCHER_SUBCOMMAND = "test-step"
# The master of the checks.  A test that runs it can get its inputs from assay.loop.inputs.
GUARD_MASTER = "guard"
# The module of the test launcher, and the ledger of each rule of the launcher, relative to the root of its tree.
LAUNCHER_MODULE = "assay.loop.teststep"
LAUNCHER_LEDGERS = tuple(f"utils/ledgers/{rule}.txt" for rule in test_launcher.ROWS)
# The files of the build tree that the judge of the launcher reads: the build kind and the name of the configuration.
JUDGE_FILES = (cost_meter.KIND_FILE, cost_meter.CONFIGURATION_FILE)
# The start of the text of the environment of a test in its fingerprint (environment_text()).
ENVIRONMENT_TEXT = "environment"
ALWAYS_RUN_PROPERTIES = ("FIXTURES_REQUIRED", "FIXTURES_SETUP", "FIXTURES_CLEANUP", "REQUIRED_FILES")
# The properties of a fixture.  A test of the master guard can hold the fixture of the parse trees.
FIXTURE_PROPERTIES = ("FIXTURES_REQUIRED", "FIXTURES_SETUP")
ENVIRONMENT_PROPERTIES = ("ENVIRONMENT", "ENVIRONMENT_MODIFICATION")
# The words in uppercase of a boolean property of ctest, such as WILL_FAIL, that give the value yes.
TRUE_WORDS = frozenset({"1", "ON", "YES", "TRUE", "Y"})
# A name in an ELF file with one of these suffixes is the text of a
# diagnostic, such as __FILE__ or std::source_location, and not a data file.
DIAGNOSTIC_SUFFIXES = (".h", ".hh", ".hpp", ".hxx", ".c", ".cc", ".cpp", ".cxx", ".inc", ".ipp", ".tpp")
SHARED_LIBRARY = re.compile(r"\.so(\.[0-9]+)*$")
PATH_BYTES = rb"[A-Za-z0-9_./+@-]"

KIND_FIXTURE = "fixture"
KIND_SCRIPT = "script"
KIND_ALWAYS = "always"
KIND_GUARD = "guard"
KIND_EXECUTABLE = "executable"
# The kinds of a test that a fingerprint judges.
FINGERPRINTED = (KIND_GUARD, KIND_EXECUTABLE)

# The reason of each decision, as the summary prints it.
REASON_FIXTURE = "the result store of `loop fixture` decides"
REASON_SCRIPT = "a script test reads the source tree"
REASON_CHANGED = "an input changed since the last pass"
REASON_NO_RECORD = "no pass on record"
REASON_ALL = "--all"
REASON_SAME = "each input is the same as at the last pass"
REASON_STORE = "the shared store holds a pass with the same inputs"
# The reason of each skip in the summary after the tests: REASON_SAME or REASON_STORE.
REASON_SKIPPED = "the state of this build directory or the shared store holds a pass with the same inputs"
REASON_FAILED = "the test failed in this build directory with the same inputs"
REASON_READS = "the read set of the last traced run is not usable, and the launcher traces the test again"


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
    """What the script does with one test, and why.

    `static` is the fingerprint with no read set (THE READ SET).  An executable test and a test of the master guard
    have it.  A test that always runs for a reason that a read set can lift has it too.  `reads` is the usable read set
    of an executable test, or None.
    """

    test: ctest_reader.TestEntry
    kind: str
    reason: str
    fingerprint: str = ""
    shared: str = ""
    is_skipped: bool = False
    static: str = ""
    reads: readset.ReadSet | None = None


@dataclass
class Scan:
    """The names that one ELF file holds: input files, input directories, and the reasons to always run.

    `always_reason` holds a reason that no read set lifts, and `source_reason` holds a name of the source tree that
    is not a file, which a usable read set lifts (THE TESTS THAT ALWAYS RUN).
    """

    files: list[str] = field(default_factory=list)
    directories: list[str] = field(default_factory=list)
    always_reason: str = ""
    source_reason: str = ""

    def to_json(self) -> dict[str, object]:
        """Return the scan as a JSON object for the state file."""
        return {
            "files": self.files,
            "directories": self.directories,
            "always": self.always_reason,
            "source": self.source_reason,
        }

    @staticmethod
    def from_json(value: object) -> Scan | None:
        """Return the scan of a JSON object, or None when the object is not a scan."""
        if not isinstance(value, dict):
            return None
        files, directories = value.get("files"), value.get("directories")
        always, source = value.get("always"), value.get("source")
        if not (isinstance(files, list) and isinstance(directories, list)):
            return None
        if not (isinstance(always, str) and isinstance(source, str)):
            return None
        if not all(isinstance(item, str) for item in files + directories):
            return None
        return Scan(files=files, directories=directories, always_reason=always, source_reason=source)


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


def env_assignments(command: tuple[str, ...], start: int, index: int) -> list[str]:
    """Return each VAR=VALUE word of a `cmake -E env` prefix between the test launcher at `start` and the program.

    Complexity: O(index - start).
    """
    if index - start < 3 or os.path.basename(command[start]) != "cmake" or command[start + 1:start + 3] != ("-E", "env"):
        return []
    return [word for word in command[start + 3:index] if "=" in word and not word.startswith("-")]


def source_paths_in(text: str, trees: Trees) -> list[str]:
    """Return each absolute path in `text` that is in the source tree and not in the build tree."""
    found: list[str] = []
    for match in re.finditer(re.escape(trees.source_root) + r"(?:/[^\s:;=,\"']*)?", text):
        candidate = os.path.normpath(match.group(0))
        if trees.is_source_only(candidate):
            found.append(candidate)
    return found


def data_sections(member: elf_file.Member) -> list[tuple[int, int]]:
    """Return the byte span of each allocated section that holds no code.  O(number of sections).

    Raises:
        elf_file.ElfError: If the file has no section header table, or a data section is outside the file
    """
    if not member.sections():
        raise elf_file.ElfError(f"{member.where}: the file has no section header table")
    return [member.span(section) for section in member.sections() if section.is_data]


def name_pattern(trees: Trees) -> re.Pattern[bytes]:
    """Return the pattern of an absolute path of the source tree or of the build tree, as bytes."""
    roots = sorted({trees.source_root, trees.build_root}, key=len, reverse=True)
    alternatives = b"|".join(re.escape(root.encode()) for root in roots)
    return re.compile(b"(?<!" + PATH_BYTES + b")(?:" + alternatives + b")(?:/" + PATH_BYTES + b"*)?(?!"
                      + PATH_BYTES + b")")


def scan_elf(path: str, trees: Trees) -> Scan:
    """Return the input names of one ELF file.  O(size of its data sections)."""
    try:
        with elf_file.mapped(path) as member:
            spans = data_sections(member)
            pattern = name_pattern(trees)
            names = {match.group(0).decode("utf-8", "surrogateescape")
                     for start, end in spans for match in pattern.finditer(member.buffer, start, end)}
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
        elif not scan.source_reason:
            scan.source_reason = f"{path} names {candidate}, which is not a file of the source tree"
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


def environment_text(is_launched: bool) -> str:
    """Return the text of the environment that a test gets, for its fingerprint (THE ENVIRONMENT).

    Complexity: O(n log n) for n variables.
    """
    environment = proc.environment()
    shown = test_launcher.canonical_environment(environment) if is_launched else tuple(sorted(environment.items()))
    return f"{ENVIRONMENT_TEXT} {json.dumps(shown)}"


def shared_libraries(directory: str) -> list[str]:
    """Return each shared library directly in `directory`, sorted."""
    try:
        names = os.listdir(directory)
    except OSError:
        return []
    return sorted(os.path.join(directory, name) for name in names if SHARED_LIBRARY.search(name))


# ── The shared store ───────────────────────────────────────────────────────


class ResultStore:
    """The results of the runs with the status 0 that each build directory and each work tree share (THE SHARED STORE).

    With the caches off, the store holds nothing and writes nothing.
    """

    def __init__(self) -> None:
        """Find the directory of the store in the root of the caches, and make it."""
        self.directory = cache.cache_root(STORE_NAME)

    def entry(self, key: str) -> Path | None:
        """Return the file of the entry of a key, or None when the caches are off."""
        return None if self.directory is None else self.directory / key[:2] / key

    def holds_pass(self, key: str) -> bool:
        """Return True when the store holds the result of a run with the status 0 for a key, and touch the entry.

        Complexity: one open of one file.
        """
        path = self.entry(key)
        if path is None:
            return False
        try:
            used = path.stat().st_mtime
            found = json.loads(path.read_bytes())
        except (OSError, ValueError):
            return False
        is_pass = isinstance(found, dict) and found.get("format") == STORE_FORMAT and found.get("verdict") == (
            VERDICT_PASSED
        )
        if is_pass:
            cache.mark_used(path, used)
        return is_pass

    def put(self, key: str, record_digest: str) -> None:
        """Write the result of a run with the status 0 for a key, with the SHA-256 of the record of the run.

        A failure of the write changes nothing.
        """
        path = self.entry(key)
        if path is None:
            return
        text = json.dumps({"format": STORE_FORMAT, "verdict": VERDICT_PASSED, "record": record_digest})
        with contextlib.suppress(OSError):
            cache.write_atomic(path, text.encode("ascii"))

    def remove(self, key: str) -> None:
        """Remove the entry of a key, when the store holds one."""
        path = self.entry(key)
        if path is not None:
            path.unlink(missing_ok=True)

    def hold_bound(self) -> bool:
        """Keep the store in its row of LIMITS of assay.core.cache, at its turn.  Return True when this call evicted."""
        return self.directory is not None and cache.hold_bound(self.directory, cache.LIMITS[STORE_NAME])


def text_digest(text: str) -> str:
    """Return the SHA-256 of a text, with each byte of a surrogate escape kept."""
    return hashlib.sha256(text.encode("utf-8", "surrogateescape")).hexdigest()


def record_digest(build_root: str, name: str) -> str:
    """Return the SHA-256 of the record of the last run of a test (assay.core.record), or "" with no record."""
    try:
        with open(cost_record.test_record_file(build_root, name), "rb") as handle:
            return hashlib.file_digest(handle, "sha256").hexdigest()
    except OSError:
        return ""


# ── The state file ─────────────────────────────────────────────────────────


def empty_state(trees: Trees) -> dict[str, object]:
    """Return a state with no record."""
    return {"version": STATE_VERSION, "source": trees.source_root, "build": trees.build_root, "passed": {},
            "failed": {}, "scans": {}}


def load_state(path: Path, trees: Trees) -> tuple[dict[str, object], str]:
    """Return the state and a note.  A damaged state, or the state of other trees, gives an empty state."""
    if not path.exists():
        return empty_state(trees), "no state file: each executable test runs"
    try:
        state = json.loads(path.read_text())
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        return empty_state(trees), f"the state file {path} is damaged ({error}): each executable test runs"
    is_valid = (isinstance(state, dict) and state.get("version") == STATE_VERSION
                and isinstance(state.get("scans"), dict)
                and all(isinstance(state.get(part), dict)
                        and all(isinstance(key, str) and isinstance(value, str) for key, value in state[part].items())
                        for part in ("passed", "failed")))
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

    def __init__(self, trees: Trees, state: dict[str, object], store: ResultStore) -> None:
        """Keep the trees, the state and the shared store, and start with an empty digest cache."""
        self.trees = trees
        self.state = state
        self.store = store
        self.digests = DigestCache()
        self.used_scans: dict[str, object] = {}
        self.launcher_files: dict[str, tuple[list[str], tuple[str, ...]]] = {}
        self.derivers: dict[str, guard_inputs.Deriver] = {}
        # The roots of the shared key, the longest first.  A build directory in the source tree then gets its own mark.
        given = ((trees.build_root, cost_store.BUILD_MARK), (trees.source_root, cost_store.ROOT_MARK))
        self.marks = tuple(sorted(given, key=lambda item: -len(item[0])))
        self.judge_files = frozenset(os.path.join(trees.build_root, name) for name in JUDGE_FILES)

    def forget(self) -> None:
        """Forget each digest and each fact of a tree.  The next decision then reads each input again."""
        self.digests.forget()
        self.launcher_files.clear()
        self.derivers.clear()

    def deriver(self, root: str) -> guard_inputs.Deriver:
        """Return the reader of the inputs of the tests of the master guard of a tree, one for each tree."""
        if root not in self.derivers:
            self.derivers[root] = guard_inputs.Deriver(Path(root))
        return self.derivers[root]

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
        guard = self.guard_decision(test, index)
        if guard is not None:
            return guard
        program = real(test.command[index]) if os.path.isabs(test.command[index]) else test.command[index]
        if not os.path.isabs(program) or not trees.is_in_build(program):
            return Decision(test, KIND_SCRIPT, REASON_SCRIPT)
        if os.path.exists(program) and not elf_file.is_elf(program):
            return Decision(test, KIND_SCRIPT, REASON_SCRIPT)
        start, launcher = launcher_prefix(test.command)
        always = self.property_reason(test)
        if always:
            return Decision(test, KIND_ALWAYS, always)
        files = sorted({real(item) for item in test.command[start:]
                        if os.path.isabs(item) and trees.is_in_build(real(item)) and not os.path.isdir(item)})
        inputs: set[str] = set(files) | {program}
        texts: tuple[str, ...] = (environment_text(is_launched=False),)
        if launcher:
            launcher_files, texts = self.launcher_inputs(launcher)
            inputs.update(launcher_files)
        directories: set[str] = set()
        lifted = self.source_reason(test, start)
        for item in sorted(inputs):
            if not elf_file.is_elf(item):
                continue
            scan = self.scan(item)
            if scan.always_reason:
                return Decision(test, KIND_ALWAYS, scan.always_reason)
            lifted = lifted or scan.source_reason
            inputs.update(scan.files)
            directories.update(scan.directories)
        static, _ = self.fingerprints(test, inputs, directories, texts)
        is_usable, reads = self.read_set(test, start)
        if not is_usable or (lifted and reads is None):
            return Decision(test, KIND_ALWAYS, lifted if is_usable else REASON_READS, static=static)
        fingerprint, shared = self.fingerprints(test, inputs, directories, texts, reads)
        return Decision(test, KIND_EXECUTABLE, "", fingerprint=fingerprint, shared=shared, static=static, reads=reads)

    def read_set(self, test: ctest_reader.TestEntry, start: int) -> tuple[bool, readset.ReadSet | None]:
        """Return True when a test has no read set or a usable one, and its usable read set or None (THE READ SET).

        `start` is the index of the command after the test launcher.  Complexity: one read of the read set.
        """
        path = readset.read_set_file(self.trees.build_root, test.name)
        found = readset.read(path)
        if found is None:
            return not os.path.exists(path), None
        is_pass = found.status == 0 or not self.is_pass_at_zero(test)
        is_usable = is_pass and found.is_settled and found.command == tuple(test.command[start:])
        return is_usable, found if is_usable else None

    @staticmethod
    def is_pass_at_zero(test: ctest_reader.TestEntry) -> bool:
        """Return True when ctest accepts a run of a test only with the exit status 0 (THE READ SET).

        The property PASS_REGULAR_EXPRESSION, or the value yes of WILL_FAIL, gives the verdict of a test.
        """
        if test.words_of("PASS_REGULAR_EXPRESSION"):
            return False
        return not any(word.upper() in TRUE_WORDS for word in test.words_of("WILL_FAIL"))

    def guard_decision(self, test: ctest_reader.TestEntry, index: int) -> Decision | None:
        """Return the decision of a test of the master guard with inputs, or None for each other test.

        `index` is the index of the program of the command.  THE TESTS OF THE
        MASTER GUARD gives the rules.  Complexity: the cost of
        assay.loop.inputs for the words of the test, and one digest of each
        input that the digest cache does not hold.
        """
        command = test.command
        if not os.path.basename(command[index]).startswith("python"):
            return None
        position = index + 1
        while position < len(command) and command[position].startswith("-"):
            position += 1
        master = os.path.join(self.trees.source_root, "utils", "bin", GUARD_MASTER)
        if position >= len(command) or not os.path.isabs(command[position]) or real(command[position]) != master:
            return None
        start, launcher = launcher_prefix(command)
        assignments = [*test.words_of("ENVIRONMENT"), *env_assignments(command, start, index)]
        variables = {item.split("=", 1)[0] for item in assignments if "=" in item}
        words = command[position + 1:]
        found = self.deriver(self.trees.source_root).test_inputs(words, variables)
        if found is None:
            return None
        always = (found.always or self.property_reason(test, frozenset({guard_inputs.TREE_FIXTURE}))
                  or self.source_reason(test, start, position))
        values = [*command[start:], *(item.split("=", 1)[1] for item in assignments if "=" in item)]
        named = [real(value) for value in values if os.path.isabs(value)]
        if not always and words[:1] == ("selftest",):
            directory = next((path for path in named if self.trees.is_in_build(path) and os.path.isdir(path)), "")
            always = f"the group test reads the build directory {directory}" if directory else ""
        if always:
            return Decision(test, KIND_ALWAYS, always)
        files = set(found.files)
        texts = list(found.texts)
        if launcher:
            launcher_files, launcher_texts = self.launcher_inputs(launcher)
            files.update(launcher_files)
            texts += launcher_texts
        else:
            texts.append(environment_text(is_launched=False))
        files.update(path for path in named if path != master and os.path.isfile(path))
        fingerprint, shared = self.fingerprints(test, files, set(), tuple(texts))
        return Decision(test, KIND_GUARD, "", fingerprint=fingerprint, shared=shared, static=fingerprint)

    def launcher_inputs(self, launcher: str) -> tuple[list[str], tuple[str, ...]]:
        """Return the files and the texts that the test launcher reads.

        The files are the master, the modules of the import closure of
        LAUNCHER_MODULE, the ledger of each rule of the launcher,
        build-kind.txt and build-configuration.txt.  The texts are the rows
        of the rules of the launcher in the registry, and the clean
        environment of the test (environment_text()).  A change of a
        different row then changes no input.  One time for each call of the
        script.
        """
        if launcher not in self.launcher_files:
            root = os.path.dirname(os.path.dirname(os.path.dirname(real(launcher))))
            deriver = self.deriver(root)
            modules = sorted(deriver.closure([LAUNCHER_MODULE]))
            files = [real(launcher), *(os.path.join(root, file) for name in modules
                                       if (file := deriver.module_file(name)) is not None)]
            files += [os.path.join(root, ledger) for ledger in LAUNCHER_LEDGERS]
            files += [os.path.join(self.trees.build_root, name) for name in JUDGE_FILES]
            texts = (*deriver.row_texts(test_launcher.ROWS), environment_text(is_launched=True))
            self.launcher_files[launcher] = (files, texts)
        return self.launcher_files[launcher]

    def property_reason(self, test: ctest_reader.TestEntry, fixtures: frozenset[str] = frozenset()) -> str:
        """Return the property that makes a test always run, or an empty string (THE TESTS THAT ALWAYS RUN).

        A property of a fixture with only the names of `fixtures` gives no reason.
        """
        for name in ALWAYS_RUN_PROPERTIES:
            if name not in test.properties:
                continue
            if name in FIXTURE_PROPERTIES and fixtures and set(test.words_of(name)) <= fixtures:
                continue
            return f"the test has the property {name}"
        return ""

    def source_reason(self, test: ctest_reader.TestEntry, start: int, master: int = -1) -> str:
        """Return the path of the source tree that makes a test always run, or an empty string.

        A usable read set lifts this reason for an executable test (THE TESTS THAT ALWAYS RUN).  `start` is the index
        after the test launcher prefix.  The launcher is a file of the source tree by design, and its files are
        inputs of the fingerprint.  The check of the command then starts after it.  The word at `master` is an input
        too: the master guard of a test of that master.
        """
        for position, item in enumerate(test.command[start:], start):
            found = source_paths_in(item, self.trees) if position != master else []
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

    def fingerprints(self, test: ctest_reader.TestEntry, inputs: set[str], directories: set[str],
                     texts: tuple[str, ...] = (), reads: readset.ReadSet | None = None) -> tuple[str, str]:
        """Return the fingerprint of a test in this build directory, and its key in the shared store.

        The fingerprint is the SHA-256 of the record of the test, of the bytes of each of its inputs and of its texts.
        It also holds the key parts of the read set of the test (THE READ SET).  THE SHARED STORE gives the rules of
        the key.  Complexity: O(n log n) for n input files and key parts, with one digest of each file that the
        digest cache does not hold.
        """
        files = set(inputs)
        for directory in directories:
            files.update(shared_libraries(directory))
        digests = sorted((path, self.digests.digest(path)) for path in files)
        read_parts = None if reads is None else sorted(
            (entry.kind, entry.path, readset.signature(entry.path) if entry.kind == readset.LIST
             else self.digests.digest(entry.path))
            for entry in reads.key_parts()
        )
        local = json.dumps([canonical(test), digests, sorted(directories), sorted(texts), read_parts])
        shared_record = {
            "name": test.name,
            "command": [cost_store.relocated(word, self.marks) for word in test.command],
            "properties": {name: [cost_store.relocated(word, self.marks) for word in words]
                           for name, words in test.properties.items()},
        }
        shared = json.dumps([
            STORE_FORMAT,
            json.dumps(shared_record, sort_keys=True),
            sorted((self.moved(path), digest) for path, digest in digests if path not in self.judge_files),
            sorted(self.moved(directory) for directory in directories),
            sorted(cost_store.relocated(text, self.marks) for text in texts),
            None if read_parts is None else sorted((kind, self.moved(path), value) for kind, path, value in read_parts),
        ])
        return text_digest(local), text_digest(shared)

    def moved(self, path: str) -> str:
        """Return a path with the mark of its root, the build directory or the source root, for the root."""
        for root, mark in self.marks:
            if path == root or path.startswith(root + "/"):
                return mark + path[len(root):]
        return path

    def decide(self, tests: tuple[ctest_reader.TestEntry, ...], run_all: bool) -> list[Decision]:
        """Return the decision for each test (THE RECORD, THE SHARED STORE).

        Complexity: classify() for each test.  For each test of FINGERPRINTED with no record of its own, one open of
        one entry of the store.
        """
        passed, failed = self.state["passed"], self.state["failed"]
        assert isinstance(passed, dict) and isinstance(failed, dict)
        decisions: list[Decision] = []
        for test in tests:
            decision = self.classify(test)
            if decision.kind in FINGERPRINTED:
                recorded = passed.get(test.name)
                if run_all:
                    decision.reason = REASON_ALL
                elif recorded == decision.fingerprint:
                    decision.reason = REASON_SAME
                    decision.is_skipped = True
                elif failed.get(test.name) == decision.fingerprint:
                    decision.reason = REASON_FAILED
                elif self.store.holds_pass(decision.shared):
                    decision.reason = REASON_STORE
                    decision.is_skipped = True
                elif recorded is None:
                    decision.reason = REASON_NO_RECORD
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
    order = {KIND_FIXTURE: 0, KIND_SCRIPT: 1, KIND_ALWAYS: 2, KIND_GUARD: 3, KIND_EXECUTABLE: 4}
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
               "--fixture-exclude-setup", guard_inputs.TREE_FIXTURE, "--output-junit", str(report), f"-j{jobs}",
               *ctest_arguments]
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


def parse_arms(source_root: Path, jobs: int) -> int:
    """Run `loop syntax` on each configured parse build that the change of the tree makes necessary (THE PARSE BUILDS).

    Complexity: one read of the change, and one run of `loop syntax` for each configured parse build of a change of
    an arm.

    Returns:
        0 when each parse build that runs parses each unit, else 1
    """
    from assay.core import configuration as build_config
    from assay.loop import syntax

    try:
        arms = syntax.arm_files(source_root)
    except syntax.ParseError as problem:
        check_report.write_line(f"loop test: {problem}.  The script runs no parse build")
        return 0
    status = 0
    for preset in syntax.PARSE_PRESETS if arms else ():
        try:
            parse_build = syntax.build_directory(source_root, preset)
        except syntax.ParseError as problem:
            check_report.write_line(f"loop test: {problem}")
            continue
        if not build_config.is_parse_build(parse_build):
            check_report.write_line(
                f"loop test: the change holds an arm, and the parse build {parse_build} is not configured.  Configure "
                f"it with `cmake --preset {preset}`"
            )
            continue
        check_report.write_line(f"loop test: the change holds an arm, so the script parses {parse_build}")
        if syntax.run(parse_build, source_root, jobs) != 0:
            status = 1
    return status


def update_state(state: dict[str, object], planner: Planner, decisions: list[Decision],
                 statuses: dict[str, ctest_reader.TestStatus], tests: tuple[ctest_reader.TestEntry, ...]) -> None:
    """Record the result of each run of a test of FINGERPRINTED, in the state and in the shared store.

    A run with the status 0 and with unchanged inputs gives the fingerprint to the state and the entry to the store.
    A failure gives the fingerprint to the failures of the state, and it removes the entry of the store (THE RECORD,
    THE SHARED STORE).
    """
    passed, failed = state["passed"], state["failed"]
    assert isinstance(passed, dict) and isinstance(failed, dict)
    names = {test.name for test in tests}
    for part in (passed, failed):
        for name in [name for name in part if name not in names]:
            del part[name]
    planner.forget()
    for decision in decisions:
        name = decision.test.name
        if decision.kind not in FINGERPRINTED and not decision.static:
            passed.pop(name, None)
            failed.pop(name, None)
            continue
        if decision.is_skipped:
            continue
        after = planner.classify(decision.test)
        # The run can give a new read set.  The script then compares the inputs with no read set.  It compares the
        # read set with the signatures at the end of the run (THE READ SET).
        is_same = (
            after.kind in FINGERPRINTED
            and after.static == decision.static
            and (after.reads is None or after.reads.is_current())
        )
        status = statuses.get(name)
        if status == ctest_reader.TestStatus.RUN and is_same:
            passed[name] = after.fingerprint
            failed.pop(name, None)
            planner.store.put(after.shared, record_digest(planner.trees.build_root, name))
        elif status == ctest_reader.TestStatus.FAIL and decision.kind in FINGERPRINTED:
            passed.pop(name, None)
            failed[name] = decision.fingerprint
            planner.store.remove(decision.shared)
        else:
            passed.pop(name, None)
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
                parse_status = parse_arms(Path(trees.source_root), jobs) if status == 0 else 0
            recorder.finish_build(status or parse_status)
            if status != 0:
                print(f"loop test: the build failed with exit code {status}.  No test ran.")
                save_history(recorder, build_dir)
                return 1
            if parse_status != 0:
                check_report.write_line(
                    "loop test: a unit of a parse build does not parse (the lines above).  No test ran."
                )
                save_history(recorder, build_dir)
                return 1
        elif not should_build:
            print("loop test: --no-build: the script does not make sure that each executable is current.")
        state_path = state_dir / "state.json"
        state, note = load_state(state_path, trees)
        if note:
            print(f"loop test: {note}")
        tests = list_tests(build_dir)
        planner = Planner(trees, state, ResultStore())
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
        planner.store.hold_bound()
        counts = Counter(statuses.get(name, ctest_reader.TestStatus.NOTRUN) for name in selected)
        passed, failed = counts[ctest_reader.TestStatus.RUN], counts[ctest_reader.TestStatus.FAIL]
        skip_code = counts[ctest_reader.TestStatus.SKIPPED]
        not_run = counts[ctest_reader.TestStatus.NOTRUN] + counts[ctest_reader.TestStatus.DISABLED]
        recorder.finish_tests(status, {"selected": len(selected), "passed": passed, "failed": failed,
                                       "skip_code": skip_code, "not_run": not_run, "skipped": len(skipped)})
        print(f"loop test: {len(selected)} tests selected: {passed} passed, {failed} failed, {skip_code} gave their "
              f"skip code, {not_run} did not run.")
        print(f"loop test: {len(skipped)} tests skipped, because {REASON_SKIPPED} "
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
    return elf_file.make_file([elf_file.Planted(".data", allocated, flags=elf_file.SHF_ALLOC),
                               elf_file.Planted(".text", executable,
                                                flags=elf_file.SHF_ALLOC | elf_file.SHF_EXECINSTR),
                               elf_file.Planted(".comment", other)])


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
        # The registry in utils/ is one of the files that the launched test reads.
        self.launcher = plant_launcher(self.source_dir)
        (self.source_dir / "utils" / "ledgers").mkdir()
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
            f'add_test([=[skip_code_test]=] "{sys.executable}" "-S" "-c" "raise SystemExit(77)")',
            "set_tests_properties([=[skip_code_test]=] PROPERTIES SKIP_RETURN_CODE 77)",
            (f'add_test([=[exe_launched]=] "{sys.executable}" "-S" "{self.launcher}" "{LAUNCHER_SUBCOMMAND}" '
             f'"--warnings-dir" "{self.build_dir}/check-warnings" "--build-dir" "{self.build_dir}" '
             f'"{bin_dir}/ok_launched")'),
            (f'add_test([=[script_launched]=] "{sys.executable}" "-S" "{self.launcher}" "{LAUNCHER_SUBCOMMAND}" '
             f'"--build-dir" "{self.build_dir}" "--name" "script_launched" "{shutil.which("true")}" '
             f'"{self.source_dir}/test/check.py")'),
        ]
        (self.build_dir / "CTestTestfile.cmake").write_text("\n".join(lines) + "\n")

    def call(self, *extra: str, path: str | None = None,
             variables: tuple[tuple[str, str], ...] = ()) -> tuple[int, str]:
        """Run the script on the fake build tree without a build, and return its exit code and output."""
        return call_script(self.build_dir, *extra, path=path, variables=variables)

    def skipped(self) -> list[str]:
        """Return the names in skipped.txt."""
        return skipped_tests(self.build_dir)


def plant_launcher(source_dir: Path) -> Path:
    """Write a stand-in for the master loop of a fake tree, and return its path.

    Its subcommand test-step drops its own arguments and runs the test.
    """
    launcher = source_dir / "utils" / "bin" / LOOP_MASTER
    launcher.parent.mkdir(parents=True, exist_ok=True)
    launcher.write_text("import os, sys\narguments = sys.argv[2:]\n"
                        f"while arguments[:1] and arguments[0] in {test_launcher.OPTIONS!r}:\n"
                        "    arguments = arguments[2:]\n"
                        "os.execv(arguments[0], arguments)\n")
    return launcher


def call_script(build_dir: Path, *extra: str, path: str | None = None,
                variables: tuple[tuple[str, str], ...] = ()) -> tuple[int, str]:
    """Run the script on a fake build tree without a build, and return its exit code and output.

    With `path`, the script runs with that PATH.  `variables` adds each pair to its environment.
    """
    overrides = dict(variables) | ({"PATH": path} if path is not None else {})
    environment = proc.environment(overrides) if overrides else None
    result = proc.run(proc.Command([sys.executable, str(LOOP), "test", str(build_dir), "--no-build",
                                    f"-j{proc.DEFAULT_JOBS}", *extra], timeout_s=proc.TOOL_S, env=environment))
    return result.exit, result.out + result.err


def skipped_tests(build_dir: Path) -> list[str]:
    """Return the names in skipped.txt of a fake build tree."""
    return (build_dir / STATE_DIRECTORY / "skipped.txt").read_text().split()


# The rows of the planted rules of the guard tests: three rules with a scope of their own, and a rule of the build.
GUARD_RULES = (
    "rule alpha engine=meta scope=data/alpha/ ledger=none doc=XV | a planted rule | change nothing",
    "rule beta engine=meta scope=data/beta/ ledger=none doc=XV | a planted rule | change nothing",
    "rule gamma engine=meta scope=builddir ledger=none doc=XV | a planted rule of the build | change nothing",
    "rule delta engine=meta scope=data/delta/ ledger=none doc=XV | a planted rule of the history | change nothing",
)
# The file of a build directory that gives a test with that build directory the status 1 from the stand-in master
# guard.  The command names only the directory.  The file then changes no fingerprint.
PLANTED_FAILURE = "planted-failure"
# The files of the fake tree of the guard tests.  The module of the rule alpha imports a shared module.  The module of
# beta lists the files of the tree, and the module of delta reads the history.  The test module of the group imports a
# helper.  A fourth module is in no closure.
GUARD_FILES = {
    ".gitignore": "build/\nbuild-second/\n",
    f"utils/bin/{GUARD_MASTER}": (
        "import os, sys\n"
        "words = sys.argv[1:]\n"
        "build = words[words.index('--build-dir') + 1] if '--build-dir' in words else ''\n"
        f"sys.exit(1 if build and os.path.exists(os.path.join(build, {PLANTED_FAILURE!r})) else 0)\n"
    ),
    "utils/registry.txt": rule_registry.planted(GUARD_RULES),
    "utils/assay/guard/engines/meta/alpha.py": "from assay.fake import shared\n",
    "utils/assay/guard/engines/meta/beta.py": (
        "from assay.core import git\ndef listed(root):\n    return git.tracked_files(root)\n"
    ),
    "utils/assay/guard/engines/meta/gamma.py": "VALUE = 1\n",
    "utils/assay/guard/engines/meta/delta.py": (
        "from assay.core import git\ndef changed(root):\n    return git.path_history(root, [])\n"
    ),
    "utils/assay/fake/shared.py": "VALUE = 1\n",
    "utils/assay/fake/helper.py": "VALUE = 1\n",
    "utils/assay/fake/other.py": "VALUE = 1\n",
    "utils/assay/tests/fakegroup/test_one.py": "from assay.fake import helper\n",
    "utils/plants/alpha/plant-one/input.txt": "one\n",
    "utils/plants/beta/plant-one/input.txt": "one\n",
    "data/alpha/item.txt": "alpha\n",
    "data/beta/item.txt": "beta\n",
    "data/delta/item.txt": "delta\n",
}
# The tests of the fake tree: the words of the master guard after the launcher.
GUARD_TESTS = (
    ("guard_alpha", ("check", "--rule", "alpha", "--build-dir", "{build}")),
    ("guard_alpha_witness", ("witness", "--rule", "alpha")),
    ("guard_beta", ("check", "--rule", "beta", "--build-dir", "{build}")),
    ("guard_gamma", ("check", "--rule", "gamma", "--build-dir", "{build}")),
    ("guard_delta", ("check", "--rule", "delta", "--build-dir", "{build}")),
    ("selftest_fakegroup", ("selftest", "fakegroup")),
)


class GuardFake:
    """A fake git work tree with a stand-in master guard, three planted rules and a test group, and its build tree."""

    def __init__(self, root: Path, ctest: str) -> None:
        """Make the trees in `root`, commit the source tree, and write the tests.  The cache names `ctest`."""
        self.source_dir = root / "src"
        self.ctest = ctest
        self.launcher = plant_launcher(self.source_dir)
        for name, text in GUARD_FILES.items():
            self.write(name, text)
        git.init(self.source_dir)
        git.commit_all(self.source_dir, "a planted tree")
        self.build_dir = self.add_build("build")

    def add_build(self, name: str) -> Path:
        """Make a build tree of the source tree with the tests of GUARD_TESTS, and return it."""
        build_dir = self.source_dir / name
        build_dir.mkdir(parents=True)
        cmake.cache_file(build_dir).write_text("")
        cmake.plant(build_dir, cache={"CMAKE_HOME_DIRECTORY": str(self.source_dir), "CMAKE_CTEST_COMMAND": self.ctest})
        master = self.source_dir / "utils" / "bin" / GUARD_MASTER
        lines = []
        for test_name, words in GUARD_TESTS:
            spelled = " ".join(f'"{word.format(build=build_dir)}"' for word in words)
            lines.append(f'add_test([=[{test_name}]=] "{sys.executable}" "-S" "{self.launcher}" '
                         f'"{LAUNCHER_SUBCOMMAND}" "--build-dir" "{build_dir}" "--name" "{test_name}" '
                         f'"{sys.executable}" "{master}" {spelled})')
        (build_dir / "CTestTestfile.cmake").write_text("\n".join(lines) + "\n")
        return build_dir

    def write(self, name: str, text: str) -> None:
        """Write one file of the source tree."""
        path = self.source_dir / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text)

    def ran(self, *extra: str, build_dir: Path | None = None) -> tuple[set[str], str]:
        """Run the script, and return the names of the guard tests that it did not skip, and its output."""
        chosen = self.build_dir if build_dir is None else build_dir
        _status, output = call_script(chosen, *extra)
        return {name for name, _words in GUARD_TESTS} - set(skipped_tests(chosen)), output


def guard_cases(ctest: str, expect: Callable[[str, bool, str], None]) -> None:
    """Check the tests of the master guard (THE TESTS OF THE MASTER GUARD) on a fake git work tree."""
    with tempfile.TemporaryDirectory(prefix="affected-guard-") as directory:
        fake = GuardFake(Path(directory), ctest)
        ran, output = fake.ran()
        expect("the first call runs each guard test", len(ran) == len(GUARD_TESTS), output)
        ran, output = fake.ran()
        expect("an unchanged tree runs only the check of the rule that reads the build directory",
               ran == {"guard_gamma"} and "always: the rule gamma reads the build directory" in output, output)
        for change, names, label in (
            ("utils/plants/alpha/plant-one/input.txt", {"guard_alpha", "guard_alpha_witness"}, "a plant of alpha"),
            ("utils/assay/fake/shared.py", {"guard_alpha", "guard_alpha_witness"},
             "a module that the module of alpha imports"),
            ("data/beta/item.txt", {"guard_beta"}, "a file of the scope of beta"),
            ("utils/assay/fake/helper.py", {"selftest_fakegroup"}, "a module that the test module of the group imports"),
            ("utils/assay/fake/other.py", set(), "a module of no closure"),
        ):
            fake.write(change, "VALUE = 2\n")
            ran, output = fake.ran()
            expect(f"a change of {label} runs only the tests that read it", ran == names | {"guard_gamma"}, output)
        fake.write("utils/registry.txt", rule_registry.planted(
            [*GUARD_RULES[:1], GUARD_RULES[1].replace("change nothing", "change one thing"), *GUARD_RULES[2:]]))
        ran, output = fake.ran()
        expect("a change of the row of beta runs only the test of beta", ran == {"guard_beta", "guard_gamma"}, output)
        fake.write("notes.txt", "a new file\n")
        ran, output = fake.ran()
        expect("a new file of the tree runs only the tests whose modules list the files",
               ran == {"guard_beta", "guard_gamma"}, output)
        git.commit_all(fake.source_dir, "a second commit")
        ran, output = fake.ran()
        expect("a commit runs only the tests whose modules read the history", ran == {"guard_delta", "guard_gamma"},
               output)
        fake.write("utils/ledgers/alpha.txt", "# a planted ledger\n")
        ran, output = fake.ran()
        expect("a new ledger of alpha runs the tests of alpha and the tests that list the files",
               ran == {"guard_alpha", "guard_alpha_witness", "guard_beta", "guard_gamma"}, output)
        fake.write("utils/ledgers/alpha.txt", "# a planted ledger with a second line\n")
        ran, output = fake.ran()
        expect("a change of the ledger of alpha runs only the tests of alpha",
               ran == {"guard_alpha", "guard_alpha_witness", "guard_gamma"}, output)
        store_cases(fake, expect)


def store_cases(fake: GuardFake, expect: Callable[[str, bool, str], None]) -> None:
    """Check THE SHARED STORE with a second build tree of the fake tree, after a run of each guard test in the first."""
    second = fake.add_build("build-second")
    ran, output = fake.ran(build_dir=second)
    expect("a second build directory with the same inputs runs only the test that reads its build directory",
           ran == {"guard_gamma"} and REASON_STORE in output, output)
    fake.write("utils/plants/alpha/plant-one/input.txt", "three\n")
    (second / PLANTED_FAILURE).write_text("")
    ran, output = fake.ran(build_dir=second)
    expect("a test of alpha fails in the second build directory", "guard_alpha" in ran, output)
    ran, output = fake.ran()
    expect("the first build directory passes the test of alpha with the same inputs", "guard_alpha" in ran, output)
    ran, output = fake.ran(build_dir=second)
    expect("a test that failed in this build directory runs again, also when the shared store holds its pass",
           "guard_alpha" in ran and REASON_FAILED in output, output)
    (second / PLANTED_FAILURE).unlink()
    ran, output = fake.ran(build_dir=second)
    expect("the test passes after the failure goes", "guard_alpha" in ran, output)
    ran, output = fake.ran(build_dir=second)
    expect("the pass in this build directory skips the test again", "guard_alpha" not in ran, output)


def traced_cases(ctest: str, expect: Callable[[str, bool, str], None]) -> None:
    """Check the keys of the read set (THE READ SET) with the test launcher of the tree, on a fake build tree.

    The program of each test is a copy of sh.  exe_traced reads a file by a path that its script makes at run time.
    exe_named names a file of the source tree, and it always runs with no read set.  exe_verdict exits with the status
    1, and its property PASS_REGULAR_EXPRESSION makes ctest accept that status.  The trees are in the work root of
    assay.core.cache.  A path in /tmp is a fact of a read set and not a key part.  On a host that refuses the filter
    of the trace, the launcher writes no read set.  The function then writes a note and checks nothing.
    """
    names = {"exe_traced", "exe_named", "exe_verdict"}
    work = cache.work_root()
    work.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="affected-traced-", dir=work) as directory:
        source_dir = Path(directory) / "src"
        build_dir = source_dir / "build"
        (build_dir / "bin").mkdir(parents=True)
        (build_dir / "data").mkdir()
        (source_dir / "data").mkdir()
        cmake.cache_file(build_dir).write_text("")
        cmake.plant(build_dir, cache={"CMAKE_HOME_DIRECTORY": str(source_dir), "CMAKE_CTEST_COMMAND": ctest})
        (build_dir / cost_meter.KIND_FILE).write_text("x86_64-debug-asan\n")
        target, golden = build_dir / "data" / "target.txt", source_dir / "data" / "golden.csv"
        target.write_text("one\n")
        golden.write_text("1,2\n")
        launched = f'"{sys.executable}" "-S" "{LOOP}" "{LAUNCHER_SUBCOMMAND}" "--build-dir" "{build_dir}"'
        lines = []
        for name, script, extra in (
            ("exe_traced", 'read -r line < "${0%/*}/../data/target.txt"', ""),
            ("exe_named", 'read -r line < "$1"', f' "x" "{golden}"'),
            ("exe_verdict", "echo verdict-marker; exit 1", ""),
        ):
            program = build_dir / "bin" / name
            shutil.copyfile(shutil.which("sh") or "/bin/sh", program)
            program.chmod(0o755)
            lines.append(f'add_test([=[{name}]=] {launched} "{program}" "-c" [=[{script}]=]{extra})')
        lines.append("set_tests_properties([=[exe_verdict]=] PROPERTIES PASS_REGULAR_EXPRESSION [=[verdict-marker]=])")
        (build_dir / "CTestTestfile.cmake").write_text("\n".join(lines) + "\n")
        _status, output = call_script(build_dir)
        if not (build_dir / "test-records" / f"exe_traced{readset.SUFFIX}").is_file():
            check_report.write_line("loop test --self-test: note: the host refused the filter of the trace, so the "
                                    "cases of the read set check nothing")
            return
        _status, output = call_script(build_dir)
        expect("a test with a usable read set is skipped when each key part is the same, also a test that ctest "
               "accepts with a status other than 0", set(skipped_tests(build_dir)) == names, output)
        target.write_text("two\n")
        golden.write_text("1,3\n")
        _status, output = call_script(build_dir)
        skipped = skipped_tests(build_dir)
        expect("a change of a file that a test opens by a path that it makes at run time makes the test run, and so "
               "does a change of a file of the source tree that a test names", skipped == ["exe_verdict"], output)
        _status, output = call_script(build_dir)
        expect("the tests are skipped again after their passes with the new key parts",
               set(skipped_tests(build_dir)) == names, output)


# The presets of the fake tree of the parse cases: one binaryDir for each parse build, and no build is configured.
PARSE_PRESETS_TEXT = (
    '{"version": 6, "configurePresets": ['
    '{"name": "syntax-x86-64-v3", "binaryDir": "${sourceDir}/build-syntax-x86-64-v3"}, '
    '{"name": "syntax-aarch64", "binaryDir": "${sourceDir}/build-syntax-aarch64"}]}\n'
)
PLAIN_UNIT = "int lanes() { return 1; }\n"
ARM_UNIT = "int lanes() {\n#if defined(__AVX2__)\n    return 16;\n#else\n    return 1;\n#endif\n}\n"


def parse_cases(expect: Callable[[str, bool, str], None]) -> None:
    """Check parse_arms (THE PARSE BUILDS) on a fake git work tree with no configured parse build."""
    with tempfile.TemporaryDirectory(prefix="affected-parse-") as directory:
        source_root = Path(directory)
        (source_root / "CMakePresets.json").write_text(PARSE_PRESETS_TEXT)
        unit = source_root / "src" / "lanes.cpp"
        unit.parent.mkdir()
        unit.write_text(PLAIN_UNIT)
        git.init(source_root)
        git.commit_all(source_root, "a planted tree")
        for text, label, should_name in (
            (PLAIN_UNIT, "a tree with no change", False),
            (ARM_UNIT, "a change of an arm", True),
        ):
            unit.write_text(text)
            captured = io.StringIO()
            with contextlib.redirect_stdout(captured):
                status = parse_arms(source_root, 4)
            output = captured.getvalue()
            names = all(f"cmake --preset {preset}" in output for preset in ("syntax-x86-64-v3", "syntax-aarch64"))
            expect(
                f"parse_arms parses nothing for {label}, and names each parse build only for an arm",
                status == 0 and names == should_name,
                output,
            )


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
            data_sections(elf_file.Member(data, "planted"))
        except elf_file.ElfError:
            return True
        return False

    spans = data_sections(elf_file.Member(synthetic_elf(allocated=b"A", other=b"B", executable=b"C"), "planted"))
    expect("data_sections finds one allocated data section",
           spans == [(elf_file.HEADER_SIZE, elf_file.HEADER_SIZE + 1)])
    expect("data_sections refuses a file that is not ELF", is_refused(b"#!/bin/sh\n" + b"\0" * 80))
    damaged = bytearray(synthetic_elf(allocated=b"A"))
    struct.pack_into("<Q", damaged, elf_file.SECTION_TABLE_AT, len(damaged) + 100)
    expect("data_sections refuses a section table outside the file", is_refused(bytes(damaged)))
    no_table = bytearray(synthetic_elf(allocated=b"A"))
    struct.pack_into("<Q", no_table, elf_file.SECTION_TABLE_AT, 0)
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
        tests_line = history[0].get("tests") if history else None
        counts = tests_line if isinstance(tests_line, dict) else {}
        expect("a call appends one line to the loop history, with the tests and with no build after --no-build",
               len(history) == 1 and history[0]["build"] is None and counts["failed"] == 1
               and counts["selected"] == counts["passed"] + 1 + counts["skip_code"] + counts["not_run"]
               and history[0]["jobs"] == proc.DEFAULT_JOBS, output)
        expect("a test that gave its skip code counts apart from the disabled test, which did not run",
               counts.get("skip_code") == 1 and counts.get("not_run") == 1
               and "1 failed, 1 gave their skip code, 1 did not run." in output, output)

        # The first call had a different PATH, which the environment of each executable test holds.
        status, output = fake.call()
        expect("a different PATH makes each executable test run", fake.skipped() == [], output)
        status, output = fake.call()
        expect("an unchanged tree skips exactly the passed executable tests",
               status == 1 and sorted(fake.skipped()) == ["exe_diagnostic", "exe_launched", "exe_named", "exe_ok"],
               output)
        expect("the summary gives the count of skipped tests and the reason",
               f"4 tests skipped, because {REASON_SKIPPED}" in output, output)

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

        dropped = (("PLANTED_HOST_VARIABLE", "1"),)
        status, output = fake.call(variables=dropped)
        expect("a variable that the launcher drops leaves a launched test skipped, and runs a test with no launcher",
               "exe_launched" in fake.skipped() and "exe_named" not in fake.skipped(), output)
        status, output = fake.call(variables=(*dropped, ("CRUCIBLE_PLANTED_VARIABLE", "1")))
        expect("a variable that the launcher keeps makes a launched test run", "exe_launched" not in fake.skipped(),
               output)
        status, output = fake.call()
        status, output = fake.call()
        expect("the environment of the shell again skips each passed test",
               "exe_launched" in fake.skipped() and "exe_named" in fake.skipped(), output)

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

        # The caches are off for this call.  The shared store then skips no test.
        state_path = fake.build_dir / STATE_DIRECTORY / "state.json"
        state_path.write_text("{damaged")
        status, output = fake.call(variables=((cache_rule.ROOT_VARIABLE, cache_rule.OFF),))
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
    guard_cases(ctest, expect)
    parse_cases(expect)
    traced_cases(ctest, expect)

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
    parser.add_argument("-j", "--jobs", type=int, default=proc.DEFAULT_JOBS)
    parser.add_argument("--no-build", action="store_true")
    parser.add_argument("--all", action="store_true")
    parser.add_argument("--plan", action="store_true")
    parser.add_argument("--ctest", help="the ctest of the fake build tree of --self-test; the default is the ctest "
                                        "of PATH, when it gives the pinned version")
    parser.add_argument("--self-test", action="store_true")
    options = parser.parse_args(arguments[:separator])
    if options.self_test:
        try:
            ctest = options.ctest or cmake_pin.pinned_program("ctest")
            # The self-test starts with an empty shared store, and it writes nothing into the store of the user.
            with cache.scratch_root():
                return self_test(ctest)
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
