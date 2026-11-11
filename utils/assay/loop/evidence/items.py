"""items — the items of each class of sampled checks, and the key of each item.

THE ITEMS
    tree_items() gives one item for each member of each class of TARGETS of
    assay.loop.evidence.targets.  A member is a test with the label of the
    class in a build of the class (THE ITEMS OF A CLASS of that module):
      stress-passes    each test of the build of each test preset with the
                       field `repeat`, when the program of the test is an ELF
                       file of the build tree (build_program() of
                       assay.loop.affected)
      property-search  each test with the label fuzz_property in each build
                       that builds the fuzzers
      bench-runs       each bench target of the code model of each build
                       that builds the benches for a measurement
                       (assay.loop.bench, bench_targets())
      mutant-runs      each header of include/ that the source tree of the
                       build of the preset MUTANT_PRESET holds
    A negative fixture and a disabled test are no items.  A fixture is a
    compile, and the result store of `loop fixture` keeps its one result.
    ctest runs no disabled test.  A repetition looks for a race in a program
    of the tree.  A test of the master guard, a self-test of a module of the
    package and a CMake script run no such program.  Their results come from
    their inputs only, and such a test is no item of stress-passes.
    An item gives the name of its class, its name, the build directory that
    gives its key, and its key.  An item with no key gives the reason, and no
    evidence applies to it: no runner can add evidence that a subsequent run
    can find.

THE KEYS
    The key of a test is its key in the shared store of `loop test`: the
    shared fingerprint of the planner of assay.loop.affected (THE SHARED
    STORE of that module).  It holds the record of the test, the bytes of
    each input and the clean environment.  It also holds the key parts of
    the read set of the test.  It holds no path of the source root or of a
    build directory.  Each work tree of the host then gives one test the same
    key.  The planner cannot key a script test or a test that always runs
    (THE TESTS THAT ALWAYS RUN of that module).  Such a test has no key.

    The key of a bench is the SHA-256 of BENCH_KEY_FORMAT, the tier of the
    host and the SHA-256 of the bytes of its program.  The tier of the host
    is the native tier that the compiler of the build gives on this host
    (assay.core.target, native_tier()).  A measurement on a different
    processor, or of a different program, then has a different key.  A
    bench with no program has no key.  When the compiler of a build gives no
    native tier, no bench of the build has a key.

THE KEYS OF A HEADER
    The key of a header of include/ holds each input of a run of the gate
    mutants of the header (assay.mutation.run).  These are the inputs:
      * the units of the build that read the header
      * the code of the mutants
      * the rows of ADMITTED_GATES for the header (THE ADMITTED GATES).
    A unit is an object of the target all or a negative fixture
    (assay.readers.census).
      * The digest of a unit holds its command, the resolved path of its
        compiler, and the bytes of each file that its compile read.
      * relocated() of assay.core.cost_store replaces the source root and the
        build directory with marks in each word of the command.  A file of
        the build directory gets the same marks in its bytes.  The build can
        write these paths into a file that it makes.
      * A file has the name that file_key() of assay.readers.census gives.
        That name holds no path of the source root or of the build
        directory.
    The key of a header is the SHA-256 of these parts:
      * MUTANT_KEY_FORMAT and the header
      * the digest of each file of MUTATION_FILES in the tree of this package
      * the rows of the ledger for the header
      * the sorted digests of the units that read the header.
    A change of a test or of a fixture that reads the header then changes
    the key.  These are the limits:
      * A test executable also links objects that do not read the header.  A
        change of only such an object does not change the key.
      * The census uses the record of each fixture of the build.  A build
        with no such record gives no key to a header, with the reason.
      * A header that no unit of the build reads has no key.
      * A header of a host row or of a kernel row of the rule table is no
        item.  A different compiler compiles it, and the build has no unit of
        it (assay.core.ruletable, EXEMPT_KINDS).

THE ADMITTED GATES
    The file ADMITTED_GATES of the source tree of the build admits the
    invalid mutant of some gates, in the format of a ledger of the policy
    admit (assay.core.ledger).  A row is HEADER | GATE KIND | admit | NAME:
    TEXT | REASON.  NAME is the gated entity, and TEXT is the gate text with
    its whitespace squeezed (exemption_row() of assay.mutation.run).  REASON
    tells why no mutant of the gate can parse, or which other test holds the
    gate.  A person writes each row.  A source tree with no such file admits
    no gate.

THE BUILDS
    With no build directory, the items of a class come from the builds that
    the presets file names (assay.loop.tiers).  A build directory that the
    call names replaces the builds of each class.  A build that is not
    configured gives one problem and no items.

THE PENDING ITEMS
    pending() gives each item that a runner works on: an item with a key and
    with less evidence than the target (assay.loop.evidence.store).  It does
    not give an item with a kept failure, because a failure stays until its
    key changes (THE FAILURE of that module).  With names, it gives only the
    named items, also an item with a kept failure.  The new evidence of such
    an item adds to its record, and its failure stays.

`guard selftest tests.loop.evidence.test_items` does a test of the module.
"""

import hashlib
import os
import platform
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import cost_store, git, paths, ruletable
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import rows as rows_grammar
from assay.core import target as build_target
from assay.loop import affected, bench, tiers
from assay.loop.evidence import store as evidence_store
from assay.loop.evidence import targets
from assay.readers import census as build_census
from assay.readers import cmake as cmake_reader
from assay.readers import compdb
from assay.readers import ctest as ctest_reader

# The property of a disabled test.
DISABLED = "DISABLED"
# The format of the key of a bench, the entry of the compiler in the CMake cache, and the native flag of a processor
# (THE KEYS).
BENCH_KEY_FORMAT = "crucible-bench-key 1"
COMPILER_ENTRY = "CMAKE_CXX_COMPILER"
NATIVE_FLAGS = {"x86_64": "-march=native", "aarch64": "-mcpu=native"}
# The format of the key of a header and the files of the code of the mutants (THE KEYS OF A HEADER).  Then the directory
# and the suffix of a header.  Then the entry of the source tree in the CMake cache, and the digest of a file with no
# read.
MUTANT_KEY_FORMAT = "crucible-mutant-key 1"
MUTATION_FILES = ("utils/assay/mutation/gates.py", "utils/assay/mutation/run.py")
HEADER_DIRECTORY = "include/"
HEADER_SUFFIX = ".h"
SOURCE_ENTRY = "CMAKE_HOME_DIRECTORY"
UNREAD = "unread"
# The file of the admitted gates in a source tree (THE ADMITTED GATES).
ADMITTED_GATES = "utils/assay/loop/evidence/admitted-gates.txt"


@dataclass(frozen=True, slots=True)
class Item:
    """One member of a class of sampled checks: the name of its class, its name, the build of its key, and its key."""

    rule: str
    name: str
    build: str
    key: str = ""
    reason: str = ""


@dataclass(frozen=True, slots=True)
class Pending:
    """One item that a runner works on, with the record of its key or None (THE PENDING ITEMS)."""

    item: Item
    record: evidence_store.Record | None = None

    @property
    def value(self) -> float:
        """The evidence of the key of the item."""
        return 0.0 if self.record is None else self.record.value

    @property
    def has_failure(self) -> bool:
        """True when the key of the item keeps a failure."""
        return self.record is not None and self.record.failure is not None


class TestKeys:
    """The keys of the tests of one build directory, from the planner of assay.loop.affected (THE KEYS)."""

    def __init__(self, build_dir: Path) -> None:
        """Read the trees and the tests of a configured build directory.

        Raises:
            affected.RunnerError: If the build directory is not configured, or its tests cannot be read
        """
        trees = affected.read_trees(build_dir)
        self.build_dir = build_dir
        self.planner = affected.Planner(trees, affected.empty_state(trees), affected.ResultStore())
        self.tests = affected.list_tests(build_dir)

    def decision(self, test: ctest_reader.TestEntry) -> affected.Decision:
        """Return the decision of the planner for a test: its kind, its shared key and its read set."""
        return self.planner.classify(test)

    def runs_build_program(self, test: ctest_reader.TestEntry) -> bool:
        """Return True when the program of a test is an ELF file of the build tree (THE ITEMS)."""
        return self.planner.build_program(test) is not None

    def forget(self) -> None:
        """Forget each digest.  After a run of the tests, the next key then reads each input again."""
        self.planner.forget()

    def key_after_run(self, test: ctest_reader.TestEntry) -> tuple[str, bool]:
        """Return the key of a test after a run, and True when the read set of the run is current (THE KEYS).

        A traced run can give a test a new read set, and the key then changes.  A test with no key gives "".  Call
        forget() after the run first.  The key then reads each input again.
        """
        after = self.decision(test)
        if after.kind not in affected.FINGERPRINTED or not after.shared:
            return "", False
        return after.shared, after.reads is None or after.reads.is_current()

    def item(self, rule: str, test: ctest_reader.TestEntry) -> Item:
        """Return the item of a test with its key, or with the reason that it has no key (THE KEYS)."""
        decision = self.decision(test)
        build = str(self.build_dir)
        if decision.kind in affected.FINGERPRINTED and decision.shared:
            return Item(rule, test.name, build, decision.shared)
        return Item(rule, test.name, build, "", f"the test has no key: {decision.kind}: {decision.reason}")


@dataclass(frozen=True, slots=True)
class Program:
    """One bench target of a build: its item, and the path of its program."""

    item: Item
    path: Path


def host_tier(build_dir: Path) -> str:
    """Return the native tier that the compiler of a build gives on this host, or "" when it gives none (THE KEYS).

    Raises:
        cmake_reader.CmakeError: If the CMake cache of the build cannot be read
    """
    entry = cmake_reader.cache(build_dir).get(COMPILER_ENTRY)
    flag = NATIVE_FLAGS.get(platform.machine(), "")
    return build_target.native_tier(entry.value, flag) if entry is not None and flag else ""


def bench_key(tier: str, program: Path) -> str:
    """Return the key of a bench: the SHA-256 of BENCH_KEY_FORMAT, the tier and the digest of the program (THE KEYS).

    Complexity: linear in the bytes of the program.

    Raises:
        OSError: If the program cannot be read
    """
    with open(program, "rb") as handle:
        digest = hashlib.file_digest(handle, "sha256").hexdigest()
    return hashlib.sha256(f"{BENCH_KEY_FORMAT}\n{tier}\n{digest}".encode()).hexdigest()


def bench_programs(rule: str, build_dir: Path) -> list[Program]:
    """Return the item and the program of each bench target of a build (THE ITEMS, THE KEYS).

    Complexity: linear in the targets of the code model, and in the bytes of each program.

    Raises:
        cmake_reader.CmakeError: If the code model or the CMake cache of the build cannot be read
    """
    model = cmake_reader.codemodel(build_dir)
    tier = host_tier(build_dir)
    names = set(bench.bench_targets(model))
    build = str(build_dir)
    found: dict[str, Program] = {}
    for target in model.targets:
        if target.name not in names or target.name in found:
            continue
        path = Path(target.artifacts[0]) if target.artifacts else build_dir / "bench" / target.name
        if not tier:
            reason = f"the compiler of the build gives no native tier on the processor {platform.machine()}"
            found[target.name] = Program(Item(rule, target.name, build, "", reason), path)
            continue
        try:
            found[target.name] = Program(Item(rule, target.name, build, bench_key(tier, path)), path)
        except OSError:
            reason = f"the bench has no program {path}.  Build the target {target.name}"
            found[target.name] = Program(Item(rule, target.name, build, "", reason), path)
    return sorted(found.values(), key=lambda program: program.item.name)


@dataclass(frozen=True, slots=True)
class HeaderKeys:
    """The source tree of a build, each header of include/ in it, and the key of each header that a unit reads.

    `deps` gives the files of include/ that each unit reads, by the resolved path of its source, as the candidates of
    assay.mutation.run take them.  `ledger` holds the rows of ADMITTED_GATES of the source tree, or None.
    """

    source: Path
    headers: tuple[str, ...]
    keys: Mapping[str, str]
    deps: Mapping[str, tuple[str, ...]]
    ledger: rule_ledger.Ledger | None = None


def source_tree(build_dir: Path) -> Path:
    """Return the source tree of a configured build: the directory of SOURCE_ENTRY of its CMake cache.

    Raises:
        cmake_reader.CmakeError: If the CMake cache cannot be read, or it names no directory
    """
    entry = cmake_reader.cache(build_dir).get(SOURCE_ENTRY)
    if entry is None or not entry.value or not Path(entry.value).is_dir():
        raise cmake_reader.CmakeError(f"the CMake cache of {build_dir} names no source tree in {SOURCE_ENTRY}")
    return Path(entry.value).resolve()


class Digests:
    """The SHA-256 of the bytes of each file that a call reads, each file read one time."""

    __slots__ = ("found",)

    def __init__(self) -> None:
        """Start with no file."""
        self.found: dict[str, str] = {}

    def of(self, path: str) -> str:
        """Return the SHA-256 of the bytes of a file, or UNREAD when the file cannot be read.

        Complexity: linear in the bytes of the file at the first call for the file, then one lookup.
        """
        known = self.found.get(path)
        if known is None:
            try:
                with open(path, "rb") as handle:
                    known = hashlib.file_digest(handle, "sha256").hexdigest()
            except OSError:
                known = UNREAD
            self.found[path] = known
        return known


def unit_commands(build_dir: Path) -> dict[tuple[str, str], tuple[str, ...]]:
    """Return the command of each unit of a build: an object from the compile database, a fixture from its test.

    The key of a unit is its kind and its item, as a unit of assay.readers.census gives them.  Complexity: linear in
    the entries of the compile database and in the tests.

    Raises:
        compdb.CompdbError: If the compile database cannot be read
        ctest_reader.CtestError: If ctest cannot list the tests
    """
    found: dict[tuple[str, str], tuple[str, ...]] = {}
    for entry in compdb.entries(build_dir):
        if entry.output is not None:
            found[("object", build_census.shown(entry.output, build_dir))] = entry.argv()
    for test in ctest_reader.tests(build_dir):
        if build_census.fixture_words(list(test.command)) is not None:
            found[("fixture", test.name)] = tuple(test.command)
    return found


def gate_ledger(source: Path) -> rule_ledger.Ledger | None:
    """Return the rows of ADMITTED_GATES of a source tree, or None when the tree has no such file (THE ADMITTED GATES).

    Raises:
        rows_grammar.RowsError: If the file does not obey the format of a ledger of the policy admit
        OSError: If the file cannot be read
    """
    path = source / ADMITTED_GATES
    if not path.is_file():
        return None
    text = path.read_text(encoding="utf-8")
    return rule_ledger.parse(text, ADMITTED_GATES, targets.COVERAGE.rule, rule_registry.Policy.ADMIT)


def gate_rows(ledger: rule_ledger.Ledger | None) -> dict[str, list[str]]:
    """Return the text of each row of ADMITTED_GATES, by its header (THE ADMITTED GATES)."""
    found: dict[str, list[str]] = {}
    for key, entry in sorted(() if ledger is None else ledger.base.items()):
        found.setdefault(key.path, []).append(f"{key.kind}\t{key.entity}\t{entry.value}")
    return found


def built_digest(path: str, build: str, source: str) -> str:
    """Return the SHA-256 of the bytes of a file of a build, with each path of the build and the source tree as a mark.

    A file that a build makes can hold the path of its build or of its source tree.  Its digest holds no such path, and
    two trees with the same files give it the same digest.  Complexity: linear in the bytes of the file.
    """
    try:
        with open(path, "rb") as handle:
            data = handle.read()
    except OSError:
        return UNREAD
    for root, mark in relocation_roots(build, source):
        data = data.replace(root.encode("utf-8", "surrogateescape"), mark.encode())
    return hashlib.sha256(data).hexdigest()


def relocation_roots(build: str, source: str) -> list[tuple[str, str]]:
    """Return the build directory and the source tree with their marks, the longest path first (relocated())."""
    return sorted(((build, cost_store.BUILD_MARK), (source, cost_store.ROOT_MARK)), key=lambda item: -len(item[0]))


def mutant_keys(build_dir: Path, package_root: Path = paths.REPO_ROOT) -> HeaderKeys:
    """Return the key of each header of include/ that a unit of a build reads (THE KEYS OF A HEADER).

    Complexity: linear in the files of the units of the build, with one read of each file.

    Raises:
        cmake_reader.CmakeError: If the CMake cache of the build names no source tree
        build_census.NotApplicable: If the build has no census, such as a build with no fixture record
        build_census.CensusError: If the census of the build cannot be read
        compdb.CompdbError: If the compile database cannot be read
        ctest_reader.CtestError: If the tests of the build cannot be read
        rows_grammar.RowsError: If the file of the admitted gates of the source tree does not obey its schema
        OSError: If the file of the admitted gates of the source tree cannot be read
        git.GitError: If git cannot list the headers of the source tree
    """
    source = source_tree(build_dir)
    build = build_dir.resolve()
    found = build_census.read_census(build_dir, source)
    commands = unit_commands(build_dir)
    roots = relocation_roots(str(build), str(source))
    digests = Digests()
    tool = "\0".join(digests.of(str(package_root / name)) for name in MUTATION_FILES)
    ledger = gate_ledger(source)
    rows = gate_rows(ledger)
    built: dict[str, str] = {}
    readers: dict[str, set[str]] = {}
    deps: dict[str, tuple[str, ...]] = {}
    for unit in found.units:
        command = commands.get((unit.kind, unit.item), ())
        compiler = os.path.realpath(command[0]) if command else ""
        named = sorted((build_census.file_key(real, source, build), real) for real in unit.files)
        for name, real in named:
            if name.startswith(build_census.BUILD_KEY) and real not in built:
                built[real] = built_digest(real, str(build), str(source))
        words = (cost_store.relocated(word, roots) for word in command)
        files = (f"{name}\t{built.get(real) or digests.of(real)}" for name, real in named)
        digest = hashlib.sha256("\0".join((unit.kind, *words, compiler, *files)).encode("utf-8", "surrogateescape"))
        headers = tuple(name for name, _ in named if name.startswith(HEADER_DIRECTORY))
        deps[os.path.realpath(os.path.join(source, unit.source))] = headers
        for name in headers:
            if name.endswith(HEADER_SUFFIX):
                readers.setdefault(name, set()).add(digest.hexdigest())
    keys: dict[str, str] = {}
    for header, units in readers.items():
        material = "\0".join((MUTANT_KEY_FORMAT, header, tool, *rows.get(header, ()), *sorted(units)))
        keys[header] = hashlib.sha256(material.encode("utf-8", "surrogateescape")).hexdigest()
    table_path = source / ruletable.TABLE.relative_to(paths.REPO_ROOT)
    table = ruletable.load(table_path) if table_path.is_file() else None
    listed = {
        name
        for name in git.tracked_files(source, [HEADER_DIRECTORY], with_new=True)
        if name.endswith(HEADER_SUFFIX) and (table is None or table.class_of(name)[0] not in ruletable.EXEMPT_KINDS)
    }
    return HeaderKeys(source, tuple(sorted(listed | set(keys))), keys, deps, ledger)


def mutant_items(rule: str, build_dir: Path, keyed: HeaderKeys) -> list[Item]:
    """Return an item for each header of a source tree, with its key or the reason that it has none."""
    build = str(build_dir)
    reason = "no unit of the build reads the header"
    return [
        Item(rule, header, build, keyed.keys[header]) if header in keyed.keys else Item(rule, header, build, "", reason)
        for header in keyed.headers
    ]


# The errors of a build that gives no key to its headers (THE KEYS OF A HEADER).
MUTANT_ERRORS = (
    cmake_reader.CmakeError,
    build_census.NotApplicable,
    build_census.CensusError,
    compdb.CompdbError,
    ctest_reader.CtestError,
    rows_grammar.RowsError,
    OSError,
    git.GitError,
)


def is_disabled(test: ctest_reader.TestEntry) -> bool:
    """Return True when the property DISABLED of a test has the value yes."""
    return any(word.upper() in affected.TRUE_WORDS for word in test.words_of(DISABLED))


def is_member(test: ctest_reader.TestEntry, sampled: targets.Target) -> bool:
    """Return True when a test is a member of a class: it has the label of the class, and it is a test (THE ITEMS)."""
    has_label = not sampled.label or sampled.label in test.words_of(ctest_reader.LABELS)
    return has_label and not is_disabled(test) and not affected.is_fixture(test.command)


def repeated_builds(presets: tiers.Tiers) -> tuple[tuple[str, Path], ...]:
    """Return the name and the build directory of each test preset with the field `repeat` (THE BUILDS).

    Complexity: linear in the presets.
    """
    builds = {item.name: item.build for item in presets.configure if item.build is not None}
    return tuple(
        (item.name, builds[item.configure]) for item in presets.tests if item.is_repeated and item.configure in builds
    )


def class_builds(sampled: targets.Target, presets: tiers.Tiers) -> tuple[tuple[str, Path], ...]:
    """Return the name of the preset and the build directory of each build of a class (THE ITEMS OF A CLASS).

    Complexity: linear in the presets.
    """
    if sampled.builds is targets.Builds.REPEATED:
        return repeated_builds(presets)
    if sampled.builds is targets.Builds.MUTANTS:
        return tuple(
            (item.name, item.build)
            for item in presets.configure
            if item.name == targets.MUTANT_PRESET and item.build is not None
        )
    is_bench = sampled.builds is targets.Builds.BENCH
    return tuple(
        (item.name, item.build)
        for item in presets.configure
        if (item.is_bench if is_bench else item.is_fuzz) and not item.is_parse and item.build is not None
    )


def class_items(keys: TestKeys, sampled: targets.Target) -> list[Item]:
    """Return an item for each member of a class in one build (THE ITEMS).

    Complexity: the cost of the planner of assay.loop.affected for each member.
    """
    is_repeated = sampled.builds is targets.Builds.REPEATED
    return [
        keys.item(sampled.rule, test)
        for test in keys.tests
        if is_member(test, sampled) and (not is_repeated or keys.runs_build_program(test))
    ]


class BuildKeys:
    """The keys of each build that a call reads, each one read one time, and the problem of each build with no items."""

    __slots__ = ("keys", "missing", "problems")

    def __init__(self) -> None:
        """Start with no build."""
        self.keys: dict[Path, TestKeys | None] = {}
        self.missing: set[Path] = set()
        self.problems: list[str] = []

    def is_configured(self, name: str, build: Path) -> bool:
        """Return True when a build is configured, else give one problem for the build, one time."""
        if cmake_reader.is_configured(build):
            return True
        if build not in self.missing:
            self.missing.add(build)
            self.problems.append(f"the build {build} of {name} is not configured.  Its items have no evidence")
        return False

    def of(self, name: str, build: Path) -> TestKeys | None:
        """Return the keys of a build, or None with one problem when the build is not configured or does not read."""
        if build not in self.keys:
            self.keys[build] = None
            if self.is_configured(name, build):
                try:
                    self.keys[build] = TestKeys(build)
                except affected.RunnerError as problem:
                    self.problems.append(f"the tests of the build {build} cannot be read: {problem}")
        return self.keys[build]

    def benches(self, name: str, build: Path, rule: str) -> list[Item]:
        """Return the item of each bench target of a build, or no items with one problem (THE ITEMS)."""
        if not self.is_configured(name, build):
            return []
        try:
            return [program.item for program in bench_programs(rule, build)]
        except cmake_reader.CmakeError as problem:
            self.problems.append(f"the benches of the build {build} cannot be read: {problem}")
            return []

    def headers(self, name: str, build: Path, rule: str) -> list[Item]:
        """Return the item of each header of the source tree of a build, or no items with one problem (THE ITEMS)."""
        if not self.is_configured(name, build):
            return []
        try:
            return mutant_items(rule, build, mutant_keys(build))
        except MUTANT_ERRORS as problem:
            self.problems.append(f"the headers of the build {build} have no key: {problem}")
            return []


def pending(
    found: Sequence[Item], store: evidence_store.EvidenceStore, goal: float, names: Sequence[str]
) -> list[Pending]:
    """Return each item of a list with less evidence than a target, that a runner works on (THE PENDING ITEMS).

    Complexity: one read of the store for each item with a key.
    """
    wanted = set(names)
    chosen: list[Pending] = []
    for item in found:
        if not item.key or (wanted and item.name not in wanted):
            continue
        candidate = Pending(item, store.read(item.rule, item.key))
        if candidate.value >= goal or (candidate.has_failure and item.name not in wanted):
            continue
        chosen.append(candidate)
    return chosen


def tree_items(
    root: Path, directories: Sequence[Path] = (), chosen: Sequence[targets.Target] = targets.TARGETS
) -> tuple[list[Item], list[str]]:
    """Return the items of each chosen class, and a problem for each build that gives no items (THE BUILDS).

    Complexity: the cost of the planner of assay.loop.affected for each member of each build.
    """
    presets = None if directories else tiers.read(root)
    builds = BuildKeys()
    found: list[Item] = []
    for sampled in chosen:
        named = (
            tuple((str(directory), directory) for directory in directories)
            if presets is None
            else class_builds(sampled, presets)
        )
        for name, build in named:
            if sampled.builds is targets.Builds.BENCH:
                found += builds.benches(name, build, sampled.rule)
                continue
            if sampled.builds is targets.Builds.MUTANTS:
                found += builds.headers(name, build, sampled.rule)
                continue
            keys = builds.of(name, build)
            if keys is not None:
                found += class_items(keys, sampled)
    return found, builds.problems
