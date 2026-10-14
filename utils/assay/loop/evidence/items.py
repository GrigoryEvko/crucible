"""items — the items of each class of sampled checks, and the key of each item.

THE ITEMS
    tree_items() gives one item for each member of each class of TARGETS of
    assay.loop.evidence.targets:
      stress-passes  each test of the build of each test preset with the
                     field `repeat`, other than a negative fixture and a
                     disabled test.  A fixture is a compile, and the result
                     store of `loop fixture` keeps its one result.  ctest
                     runs no disabled test
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

THE BUILDS
    With no build directory, the items of a class come from the builds that
    the presets file names (assay.loop.tiers).  A build directory that the
    call names replaces them.  A build that is not configured gives a problem
    and no items.

`guard selftest tests.loop.evidence.test_items` does a test of the module.
"""

from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.loop import affected, tiers
from assay.loop.evidence import targets
from assay.readers import cmake as cmake_reader
from assay.readers import ctest as ctest_reader

# The property of a disabled test.
DISABLED = "DISABLED"


@dataclass(frozen=True, slots=True)
class Item:
    """One member of a class of sampled checks: its rule, its name, the build of its key, and its key or the reason."""

    rule: str
    name: str
    build: str
    key: str = ""
    reason: str = ""


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

    def item(self, rule: str, test: ctest_reader.TestEntry) -> Item:
        """Return the item of a test with its key, or with the reason that it has no key (THE KEYS)."""
        decision = self.decision(test)
        build = str(self.build_dir)
        if decision.kind in affected.FINGERPRINTED and decision.shared:
            return Item(rule, test.name, build, decision.shared)
        return Item(rule, test.name, build, "", f"the test has no key: {decision.kind}: {decision.reason}")


def is_disabled(test: ctest_reader.TestEntry) -> bool:
    """Return True when the property DISABLED of a test has the value yes."""
    return any(word.upper() in affected.TRUE_WORDS for word in test.words_of(DISABLED))


def repeated_builds(presets: tiers.Tiers) -> tuple[tuple[str, Path], ...]:
    """Return the name and the build directory of each test preset with the field `repeat` (THE BUILDS).

    Complexity: linear in the presets.
    """
    builds = {item.name: item.build for item in presets.configure if item.build is not None}
    return tuple(
        (item.name, builds[item.configure]) for item in presets.tests if item.is_repeated and item.configure in builds
    )


def stress_items(keys: TestKeys, rule: str = targets.STRESS.rule) -> list[Item]:
    """Return an item for each test of a build other than a negative fixture and a disabled test (THE ITEMS).

    Complexity: the cost of the planner of assay.loop.affected for each test.
    """
    return [
        keys.item(rule, test) for test in keys.tests if not is_disabled(test) and not affected.is_fixture(test.command)
    ]


def tree_items(root: Path, directories: Sequence[Path] = ()) -> tuple[list[Item], list[str]]:
    """Return the items of each class of TARGETS, and a problem for each build that gives no items (THE BUILDS).

    Complexity: the cost of the planner of assay.loop.affected for each test of each build.
    """
    found: list[Item] = []
    problems: list[str] = []
    if directories:
        stress = tuple((str(directory), directory) for directory in directories)
    else:
        stress = repeated_builds(tiers.read(root))
    for name, build in stress:
        if not cmake_reader.is_configured(build):
            problems.append(f"the build {build} of {name} is not configured.  Its tests have no evidence")
            continue
        try:
            found += stress_items(TestKeys(build))
        except affected.RunnerError as problem:
            problems.append(f"the tests of the build {build} cannot be read: {problem}")
    return found, problems
