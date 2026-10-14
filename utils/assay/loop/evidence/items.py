"""items — the items of each class of sampled checks, and the key of each item.

THE ITEMS
    tree_items() gives one item for each member of each class of TARGETS of
    assay.loop.evidence.targets.  A member is a test with the label of the
    class in a build of the class (THE ITEMS OF A CLASS of that module):
      stress-passes    each test of the build of each test preset with the
                       field `repeat`
      property-search  each test with the label fuzz_property in each build
                       that builds the fuzzers
      bench-runs       each bench target of the code model of each build
                       that builds the benches for a measurement
                       (assay.loop.bench, bench_targets())
    A negative fixture and a disabled test are no items.  A fixture is a
    compile, and the result store of `loop fixture` keeps its one result.
    ctest runs no disabled test.
    An item gives its rule, its name, the build directory that gives its key,
    and its key.  An item with no key gives the reason, and no evidence
    applies to it: no runner can add evidence that a subsequent run can find.

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
import platform
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import target as build_target
from assay.loop import affected, bench, tiers
from assay.loop.evidence import store as evidence_store
from assay.loop.evidence import targets
from assay.readers import cmake as cmake_reader
from assay.readers import ctest as ctest_reader

# The property of a disabled test.
DISABLED = "DISABLED"
# The format of the key of a bench, the entry of the compiler in the CMake cache, and the native flag of a processor
# (THE KEYS).
BENCH_KEY_FORMAT = "crucible-bench-key 1"
COMPILER_ENTRY = "CMAKE_CXX_COMPILER"
NATIVE_FLAGS = {"x86_64": "-march=native", "aarch64": "-mcpu=native"}


@dataclass(frozen=True, slots=True)
class Item:
    """One member of a class of sampled checks: its rule, its name, the build of its key, and its key or the reason."""

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
    return [keys.item(sampled.rule, test) for test in keys.tests if is_member(test, sampled)]


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
            keys = builds.of(name, build)
            if keys is not None:
                found += class_items(keys, sampled)
    return found, builds.problems
