"""plan — the plan of the tree: each item that an entry runs, with its tier and its entry (`loop plan`).

THE ITEMS
    plan() reads the tree and some builds of it (THE BUILDS), and it gives
    one item of a kind of Kind for each of these:
      preset       a configure preset of the presets file (assay.loop.tiers)
      test-preset  a test preset of the presets file
      test         a test of a build (assay.readers.ctest)
      fixture      a test of a build that runs `loop fixture`
      rule         a rule of utils/registry.txt
      bench        a bench target of the code model of a build (assay.loop.bench)
      harness      a boundary harness of the fuzz campaigns (assay.loop.fuzz)
      derive       a target of `loop derive` (assay.loop.derive)
      step         a step of STEPS
    No file holds the plan.  Each call reads the tree again.

THE COVERS
    A cover of an item is a tier and a text that tells what runs the item:
      * A preset gets the tier of its block.  The tier manual is a cover
        too, because the reason of the block tells what a person runs.  A
        preset with no tier gives a problem and no item
      * A test and a fixture get the tier of the preset of each build that
        registers them with no property DISABLED
      * A test can run a file of a bench target of its build.  Such a test
        also gets the covers of that bench target, also with the property
        DISABLED.  The program of a test behind the test launcher is the
        program after the launcher
      * A rule gets the tier of the build of each test that runs `guard check
        --rule ID`, or `guard check --engine E` for a rule of E with a module
        (assay.guard.engines.meta.test_registration).  A rule with a holder
        gets the cover of the row of its holder in HOLDERS.  A rule that a
        step names gets the tier of the step
      * A bench target and a harness get the tier of each step with `each`
        of their kind
      * A target of `loop derive` gets the covers of each rule with a module
        that imports the module of the target, or that the module of the
        target imports.  That rule compares the bytes that the target writes
        with the tree
      * A step gets its own tier.
    The tier of an item is the first tier of ORDER of its covers, else
    manual.  ENTRIES of assay.loop.tiers gives the command of its entry.  An
    item with no cover runs nowhere.  A row of the ledger of the rule
    plan-coverage then gives the reason.

THE SAMPLED ITEMS
    A sampled item gives only a part of its evidence in one run, and `loop
    evidence` adds the evidence of each run to its key.  Each class of
    TARGETS of assay.loop.evidence.targets gives its items:
      * each test preset with the field `repeat` (Builds.REPEATED)
      * each test with the label of the class (Builds.FUZZ)
      * each bench target (Builds.BENCH)
      * the rule `check` of a class of coverage (Builds.MUTANTS).  The step
        evidence names that rule, and the rule then gets the tier of the
        step.
    UNTARGETED gives the other sampled items: each item of a kind, or one
    item of a kind and a name.  Such an item has no class at this time.
    The target of an item is the rule of its class, which the registry must
    hold with the holder HOLDER of assay.loop.evidence.targets.  Each
    sampled item with no target is in untargeted() of the plan.  A row of
    the ledger of plan-coverage admits such an item with the reason why it
    has no target.

    An item is not sampled when one run gives the full evidence for its
    inputs.  The step gauge judges the medians of the runs of one call, the
    step pgo writes a new profile, and `loop derive header-costs --measure`
    counts instructions.  These items then have no target.

THE BUILDS
    read_builds() reads the build directories that the call names.  With no
    name, it reads the build directory of each configure preset with a tier
    and with no parse option, when CMake configured that directory.  A build
    gives its tests, the bench targets of its code model, and the name of its
    configuration from build-configuration.txt (assay.core.meter).  It also
    gives the files of each bench target.  A build directory of no configure
    preset is an error.

THE STEPS
    STEPS gives each step that an entry runs and that is no preset.  A step
    has a tier, the words of its command and a text.  It can name the
    configure preset of its build and the rules that it runs.  A command that
    ends with --build-dir takes the build directory of that preset.  A step
    with `each` runs each item of that kind.  The command `loop evidence run
    KIND` runs the items of the class of KIND in a time budget, and the
    command `loop fuzz` runs one campaign of each harness
    (assay.loop.entry, THE WORK).

THE COMMANDS
    Each subcommand of utils/bin/loop has a class:
      * a hidden subcommand (a row of SUBCOMMANDS with no help line) is a tool
        of a different subcommand
      * ENTRY_COMMANDS run the items of a tier, and TOOL_COMMANDS read or
        print the facts of a build
      * ITEM_COMMANDS have targets that are items of the plan
      * the subcommand of a step of STEPS.
    A subcommand with no class is a problem.

THE PROBLEMS
    These are the problems of a plan (ProblemKind):
      preset-block   a problem of the presets file or of the block of a preset
      stale-entry    a row of STEPS, HOLDERS or of a class of THE COMMANDS
                     that names no preset, rule, step or subcommand
      no-class       a holder of a rule with no row of HOLDERS, and a
                     subcommand of utils/bin/loop with no class
      configuration  a build with a configuration that is not the
                     configuration of the block of its preset.  The problem
                     applies only when the two names start with the same
                     processor, because a host of a different processor gives
                     a different name

Usage
    loop plan [--build-dir DIR]... [--kind KIND] [--tier TIER]

    The command prints each item with its tier, its kind, its name and the
    text of each cover.  An item with no cover shows the reason of its row of
    the ledger of plan-coverage, or "runs nowhere".  Exit 0 when the plan has
    no problem and each item with no cover has a row, else 1.

`guard selftest tests.loop.test_plan` does a test of the module.
"""

import ast
import enum
from collections.abc import Callable, Iterable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import cli, meter, paths
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.guard.engines.meta import runner as meta_runner
from assay.guard.engines.meta import test_registration
from assay.loop import bench, fuzz, tiers
from assay.loop import teststep as test_launcher
from assay.loop.evidence import targets as evidence_targets
from assay.readers import cmake as cmake_reader
from assay.readers import ctest as ctest_reader
from assay.readers import python as python_reader

# The rule of the plan, the master of the subcommands of THE COMMANDS, and the property of a test that no entry runs.
RULE = "plan-coverage"
LOOP_MASTER = "utils/bin/loop"
DERIVE_MODULE = "utils/assay/loop/derive/__init__.py"
DISABLED = "DISABLED"
# The word of a build directory at the end of the command of a step (THE STEPS), and the path of this module.
BUILD_OPTION = "--build-dir"
MODULE_PATH = Path(__file__).resolve().relative_to(paths.REPO_ROOT).as_posix()


class Kind(enum.StrEnum):
    """The kind of an item of the plan (THE ITEMS)."""

    PRESET = "preset"
    TEST_PRESET = "test-preset"
    TEST = "test"
    FIXTURE = "fixture"
    RULE = "rule"
    BENCH = "bench"
    HARNESS = "harness"
    DERIVE = "derive"
    STEP = "step"


class ProblemKind(enum.StrEnum):
    """The kind of a problem of the plan (THE PROBLEMS)."""

    PRESET_BLOCK = "preset-block"
    STALE_ENTRY = "stale-entry"
    NO_CLASS = "no-class"
    CONFIGURATION = "configuration"


class PlanError(Exception):
    """A build directory of the call is the build of no configure preset, or its tests cannot be read."""


@dataclass(frozen=True, slots=True)
class Cover:
    """One cover of an item: a tier, and the text that tells what runs the item (THE COVERS)."""

    tier: tiers.Tier
    text: str


@dataclass(frozen=True, slots=True)
class Item:
    """One item of the plan: its kind, its name, the file and the line that give it, and its covers."""

    kind: Kind
    name: str
    path: str
    line: int = 0
    covers: tuple[Cover, ...] = ()

    @property
    def tier(self) -> tiers.Tier | None:
        """The first tier of ORDER of the covers, else manual, or None for an item with no cover (THE COVERS)."""
        given = {cover.tier for cover in self.covers}
        return next((tier for tier in (*tiers.ORDER, tiers.Tier.MANUAL) if tier in given), None)

    @property
    def entry(self) -> str:
        """The command of the entry of the tier, or "" for the tier manual and for an item with no cover."""
        return tiers.ENTRIES.get(self.tier, "") if self.tier is not None else ""

    @property
    def entity(self) -> str:
        """The entity of the finding of plan-coverage for the item: its kind and its name."""
        return f"{self.kind} {self.name}"


@dataclass(frozen=True, slots=True)
class Problem:
    """One problem of the plan: its kind, the file and the line that give it, its entity and its message."""

    kind: ProblemKind
    path: str
    line: int
    entity: str
    message: str


@dataclass(frozen=True, slots=True)
class Build:
    """One build of a configure preset: its tests, its bench targets and the name of its configuration (THE BUILDS).

    `bench_programs` holds the resolved path of each file of each bench target, with the name of its target.
    """

    preset: tiers.PresetTier
    directory: Path
    tests: tuple[ctest_reader.TestEntry, ...] = ()
    benches: tuple[str, ...] = ()
    configuration: str | None = None
    bench_programs: tuple[tuple[str, str], ...] = ()

    @property
    def name(self) -> str:
        """The name of the build directory."""
        return self.directory.name


@dataclass(frozen=True, slots=True)
class Step:
    """One step of an entry that is no preset (THE STEPS)."""

    name: str
    tier: tiers.Tier
    command: tuple[str, ...]
    text: str
    preset: str = ""
    rules: tuple[str, ...] = ()
    each: Kind | None = None


@dataclass(frozen=True, slots=True)
class Target:
    """One target of `loop derive`: its name and the dotted name of its module (assay.loop.derive, THE TARGETS)."""

    name: str
    module: str


@dataclass(frozen=True, slots=True)
class Holding:
    """The cover of the rules of one holder: the suite of each build, the covers of a rule, or the tier of a step."""

    is_suite: bool = False
    rule: str = ""
    step: str = ""


@dataclass(frozen=True, slots=True)
class Plan:
    """The plan of a tree: each item, each build that it read, and each problem."""

    items: tuple[Item, ...]
    builds: tuple[Build, ...] = ()
    problems: tuple[Problem, ...] = ()
    untargeted: tuple[Item, ...] = ()

    @property
    def uncovered(self) -> tuple[Item, ...]:
        """Each item with no cover, which runs nowhere."""
        return tuple(item for item in self.items if not item.covers)


STEPS = (
    Step("gauge", tiers.Tier.NIGHTLY, ("loop", "gauge"), "measures the edit loop in a clone of the tree"),
    Step(
        "bench",
        tiers.Tier.NIGHTLY,
        ("loop", "evidence", "run", str(evidence_targets.MEASUREMENT.kind)),
        "runs each bench target of the build of its preset on a reserved core, and adds the valid runs to its evidence",
        preset=bench.DEFAULT_PRESET,
        each=Kind.BENCH,
    ),
    Step(
        "fuzz",
        tiers.Tier.NIGHTLY,
        ("loop", "fuzz"),
        "runs an AFL++ campaign on a boundary harness, in two instrumented builds of its own",
        each=Kind.HARNESS,
    ),
    Step(
        "reconfigure-noop",
        tiers.Tier.NIGHTLY,
        ("guard", "check", "--rule", "reconfigure-noop", BUILD_OPTION),
        "configures the build again, builds it two times and examines the Ninja log",
        preset="default",
        rules=("reconfigure-noop",),
    ),
    Step(
        "pgo",
        tiers.Tier.RELEASE,
        ("loop", "pgo"),
        "collects the profile of profile-guided optimization for the tier of the host",
        preset="pgo-generate",
    ),
    Step(
        "evidence",
        tiers.Tier.NIGHTLY,
        ("loop", "evidence", "run"),
        "adds evidence to the sampled checks in a time budget, and judges it against the targets of the registry",
        rules=tuple(target.check for target in evidence_targets.TARGETS if target.check),
    ),
)
SUITE = Holding(is_suite=True)
HOLDERS = {
    "cmake": SUITE,
    "guard-warnings": SUITE,
    "header-alone": Holding(rule="header-alone"),
    "loop-evidence": Holding(step="evidence"),
    "loop-fixture": SUITE,
    "loop-gauge": Holding(step="gauge"),
    "loop-history": SUITE,
    "loop-test-step": SUITE,
}
# The sampled items with no class of assay.loop.evidence.targets: a kind, and a name or "" for each item of the kind
# (THE SAMPLED ITEMS).
UNTARGETED = ((Kind.HARNESS, ""),)
ENTRY_COMMANDS = ("land", "nightly", "release", "syntax", "test")
TOOL_COMMANDS = ("audit", "heavy", "history", "plan")
ITEM_COMMANDS = ("bench", "derive")


# ── The builds ─────────────────────────────────────────────────────────────


def read_build(preset: tiers.PresetTier, directory: Path) -> Build:
    """Return one build of a configure preset: its tests, its bench targets and its configuration (THE BUILDS).

    A build with no code model of CMake has no bench target.  Complexity: one run of ctest, and linear in the targets.

    Raises:
        PlanError: If ctest cannot list the tests of the build
    """
    try:
        tests = ctest_reader.tests(directory)
    except ctest_reader.CtestError as problem:
        raise PlanError(f"the tests of the build {directory} of the preset {preset.name}: {problem}") from None
    configuration = meter.read_configuration(str(directory))
    try:
        model = cmake_reader.codemodel(directory)
    except cmake_reader.CmakeError:
        return Build(preset, directory, tests, (), configuration)
    benches = bench.bench_targets(model)
    return Build(preset, directory, tests, benches, configuration, bench_program_paths(model, benches))


def bench_program_paths(model: cmake_reader.CodeModel, benches: Sequence[str]) -> tuple[tuple[str, str], ...]:
    """Return the resolved path of each file of each bench target of a code model, with the name of its target.

    Complexity: linear in the targets and in their files.
    """
    names = set(benches)
    return tuple(
        (str(Path(path).resolve()), target.name)
        for target in model.targets
        if target.name in names
        for path in target.artifacts
    )


def read_builds(presets: tiers.Tiers, directories: Sequence[Path] = ()) -> tuple[Build, ...]:
    """Return the builds of some build directories, or of each configured build of a preset (THE BUILDS).

    Complexity: one run of ctest for each build, and linear in the presets.

    Raises:
        PlanError: If a directory is the build of no configure preset, or the tests of a build do not read
    """
    by_directory = {item.build.resolve(): item for item in presets.configure if item.build is not None}
    if not directories:
        return tuple(
            read_build(item, item.build)
            for item in presets.configure
            if item.tier is not None
            and not item.is_parse
            and item.build is not None
            and cmake_reader.is_configured(item.build)
        )
    builds: list[Build] = []
    for directory in directories:
        preset = by_directory.get(directory.resolve())
        if preset is None:
            raise PlanError(f"the build directory {directory} is the build of no configure preset of the presets file")
        builds.append(read_build(preset, directory))
    return tuple(builds)


# ── The items ──────────────────────────────────────────────────────────────


def preset_items(presets: tiers.Tiers) -> list[Item]:
    """Return an item for each configure preset and each test preset with a tier, with the cover of its tier.

    A preset with no tier gives no item, because it gives a problem (THE PROBLEMS).  A plain test preset of a configure
    preset with no tier gives no item too, because its configure preset gives the problem (assay.loop.tiers).
    """
    found: list[Item] = []
    for item in presets.configure:
        if item.tier is tiers.Tier.MANUAL:
            found.append(Item(Kind.PRESET, item.name, tiers.PRESETS_FILE, 0, (Cover(item.tier, item.reason),)))
        elif item.tier is not None:
            work = "parses each unit with the plugin" if item.is_parse else "builds and tests it"
            cover = Cover(item.tier, f"{tiers.ENTRIES[item.tier]} configures it and {work} in {item.build}")
            found.append(Item(Kind.PRESET, item.name, tiers.PRESETS_FILE, 0, (cover,)))
    for item in presets.tests:
        if item.tier is not None:
            text = f"the tier of its configure preset {item.configure}" if item.is_inherited else "its block"
            cover = Cover(item.tier, text if item.tier is not tiers.Tier.MANUAL else item.reason)
            found.append(Item(Kind.TEST_PRESET, item.name, tiers.PRESETS_FILE, 0, (cover,)))
    return found


def is_fixture(test: ctest_reader.TestEntry, root: Path) -> bool:
    """Tell if a test runs the driver of a negative fixture, `loop fixture`."""
    run = test_registration.master_of(test, root)
    return run is not None and (run[0], run[1][:1]) == (test_registration.FIXTURE[0], [test_registration.FIXTURE[1]])


def bench_target_of(test: ctest_reader.TestEntry, programs: Mapping[str, str]) -> str | None:
    """Return the bench target that has the program of a test as a file, or None (THE COVERS).

    The program comes after the launcher and after the emulator of a cross build (assay.loop.teststep, THE EMULATOR).
    """
    program = test_launcher.launched_program(list(test.command))
    if program is None or not programs:
        return None
    return programs.get(str(Path(program).resolve()))


def test_items(builds: Sequence[Build], root: Path) -> list[Item]:
    """Return an item for each test of the builds, with the tier of each build that runs it (THE COVERS).

    Complexity: linear in the tests of the builds.
    """
    bench_steps = [step for step in STEPS if step.each is Kind.BENCH]
    covers: dict[str, list[Cover]] = {}
    first: dict[str, tuple[Kind, str, int]] = {}
    for build in builds:
        programs = dict(build.bench_programs)
        for test in build.tests:
            path, line = test_registration.place_of(test, root)
            first.setdefault(test.name, (Kind.FIXTURE if is_fixture(test, root) else Kind.TEST, path, line))
            listed = covers.setdefault(test.name, [])
            if build.preset.tier is not None and DISABLED not in test.properties:
                entry = tiers.ENTRIES.get(build.preset.tier, "a person")
                listed.append(Cover(build.preset.tier, f"{entry} runs it in {build.name}"))
            target = bench_target_of(test, programs)
            if target is None:
                continue
            for step in bench_steps:
                cover = Cover(step.tier, f"the step {step.name} runs its program, the bench target {target}")
                if cover not in listed:
                    listed.append(cover)
    return [Item(kind, name, path, line, tuple(covers[name])) for name, (kind, path, line) in first.items()]


def test_covers(builds: Sequence[Build], registry: rule_registry.Registry, root: Path) -> dict[str, list[Cover]]:
    """Return the covers of each rule from the tests of the builds that run `guard check` of the rule (THE COVERS).

    Complexity: O(t * a) for t tests of a words.
    """
    covers: dict[str, list[Cover]] = {}
    for build in builds:
        if build.preset.tier is None:
            continue
        for test in build.tests:
            run = test_registration.master_of(test, root)
            if run is None or DISABLED in test.properties:
                continue
            master, words = run
            if master != "guard" or words[:1] != ["check"] or "--self-test" in words:
                continue
            rule_ids = [*test_registration.rule_words(words)]
            rule_ids += [rule.id for rule in test_registration.engine_rules(words, registry)]
            for rule_id in rule_ids:
                text = f"the test {test.name} of {build.name}"
                covers.setdefault(rule_id, []).append(Cover(build.preset.tier, text))
    return covers


def rule_items(builds: Sequence[Build], registry: rule_registry.Registry, root: Path) -> list[Item]:
    """Return an item for each rule of the registry, with the covers of its tests, its holder and its steps.

    Complexity: O(t * a) for t tests of a words, and linear in the rules.
    """
    covers = test_covers(builds, registry, root)
    for step in STEPS:
        for rule_id in step.rules:
            covers.setdefault(rule_id, []).append(Cover(step.tier, f"the step {step.name}"))
    steps = {step.name: step for step in STEPS}
    held: dict[str, list[Cover]] = {}
    for rule in registry.rules.values():
        holding = HOLDERS.get(rule.held_by or "")
        if holding is None:
            continue
        listed = held.setdefault(rule.id, [])
        if holding.is_suite:
            listed += [
                Cover(build.preset.tier, f"{rule.held_by} judges it in the suite of {build.name}")
                for build in builds
                if build.preset.tier is not None
            ]
        elif holding.rule:
            through = covers.get(holding.rule, [])
            listed += [Cover(cover.tier, f"the rule {holding.rule}: {cover.text}") for cover in through]
        elif holding.step in steps:
            listed.append(Cover(steps[holding.step].tier, f"{rule.held_by} judges it in the step {holding.step}"))
    path = rule_registry.REGISTRY_PATH
    return [
        Item(Kind.RULE, rule.id, path, rule.line, (*covers.get(rule.id, []), *held.get(rule.id, [])))
        for rule in sorted(registry.rules.values(), key=lambda rule: rule.line)
    ]


def module_imports(root: Path, module: str) -> tuple[str, ...]:
    """Return the dotted name of each module that a module of the package in a tree imports, or none when it is missing.

    Raises:
        python_reader.PythonError: If the module does not parse
    """
    path = meta_runner.module_file(root, module)
    if path is None:
        return ()
    return python_reader.imports(python_reader.read(path, path.relative_to(root).as_posix()))


def derive_items(
    root: Path, registry: rule_registry.Registry, rules: Sequence[Item], targets: Sequence[Target]
) -> list[Item]:
    """Return an item for each target of `loop derive`, with the covers of each rule that compares it (THE COVERS).

    Complexity: one parse of the module of each rule and of each target.

    Raises:
        python_reader.PythonError: If the module of a rule does not parse
    """
    by_id = {item.name: item for item in rules}
    modules: dict[str, tuple[str, frozenset[str]]] = {}
    for rule in sorted(registry.rules.values(), key=lambda rule: rule.id):
        module = test_registration.implementing_module(rule, root)
        if module is not None:
            modules[rule.id] = (module, frozenset(module_imports(root, module)))
    found: list[Item] = []
    for target in targets:
        imported = frozenset(module_imports(root, target.module))
        comparing = [rule_id for rule_id, (own, names) in modules.items() if target.module in names or own in imported]
        covers = tuple(
            Cover(cover.tier, f"the rule {rule_id} compares it: {cover.text}")
            for rule_id in comparing
            for cover in by_id[rule_id].covers
        )
        path = meta_runner.module_file(root, target.module)
        shown = path.relative_to(root).as_posix() if path else target.module
        found.append(Item(Kind.DERIVE, target.name, shown, 0, covers))
    return found


def each_items(kind: Kind, names: Iterable[str], path_of: Callable[[str], str]) -> list[Item]:
    """Return an item of a kind for each name, with the tier of each step that takes an item of that kind.

    `path_of` gives the file of the item of a name.
    """
    covers = tuple(Cover(step.tier, f"the step {step.name}") for step in STEPS if step.each is kind)
    return [Item(kind, name, path_of(name), 0, covers) for name in sorted(set(names))]


def harness_names(root: Path) -> tuple[str, ...]:
    """Return the name of each boundary harness of a tree: each header of fuzz.HARNESSES."""
    return tuple(path.stem for path in paths.listing(root / fuzz.HARNESSES, "*.h"))


def harness_file(name: str) -> str:
    """Return the header of the boundary harness of a name, relative to the root of the tree."""
    return (fuzz.HARNESSES / f"{name}.h").as_posix()


def step_items() -> list[Item]:
    """Return an item for each step of STEPS, with its own tier."""
    return [Item(Kind.STEP, step.name, MODULE_PATH, 0, (Cover(step.tier, step.text),)) for step in STEPS]


def sampled_rule(item: Item, repeat_presets: frozenset[str], labels: Mapping[str, frozenset[str]]) -> str | None:
    """Return the rule of the class of a sampled item, "" for a sampled item with no class, or None (THE SAMPLED ITEMS).

    Args:
        item: An item of the plan
        repeat_presets: The names of the test presets with the field `repeat`
        labels: The labels of each test of the builds of the plan
    """
    for target in evidence_targets.TARGETS:
        if target.builds is evidence_targets.Builds.REPEATED:
            is_member = item.kind is Kind.TEST_PRESET and item.name in repeat_presets
        elif target.builds is evidence_targets.Builds.FUZZ:
            is_member = item.kind is Kind.TEST and target.label in labels.get(item.name, frozenset())
        elif target.builds is evidence_targets.Builds.MUTANTS:
            is_member = item.kind is Kind.RULE and item.name == target.check
        else:
            is_member = item.kind is Kind.BENCH
        if is_member:
            return target.rule
    return "" if (item.kind, item.name) in UNTARGETED or (item.kind, "") in UNTARGETED else None


def untargeted_items(
    items: Sequence[Item], presets: tiers.Tiers, builds: Sequence[Build], registry: rule_registry.Registry
) -> tuple[Item, ...]:
    """Return each sampled item with no target: no class, or a class rule with no row of HOLDER (THE SAMPLED ITEMS).

    Complexity: linear in the items, the tests of the builds and the rules.
    """
    repeat_presets = frozenset(item.name for item in presets.tests if item.is_repeated)
    labels: dict[str, frozenset[str]] = {}
    for build in builds:
        for test in build.tests:
            labels[test.name] = labels.get(test.name, frozenset()) | frozenset(test.words_of(ctest_reader.LABELS))
    held = {rule.id for rule in registry.rules.values() if rule.held_by == evidence_targets.HOLDER}
    found: list[Item] = []
    for item in items:
        rule = sampled_rule(item, repeat_presets, labels)
        if rule is not None and rule not in held:
            found.append(item)
    return tuple(found)


# ── The problems ───────────────────────────────────────────────────────────


def preset_problems(presets: tiers.Tiers) -> list[Problem]:
    """Return a problem for each problem of the presets file and of the block of a preset (THE PROBLEMS)."""
    found = [Problem(ProblemKind.PRESET_BLOCK, tiers.PRESETS_FILE, 0, "", text) for text in presets.problems]
    found += [
        Problem(ProblemKind.PRESET_BLOCK, tiers.PRESETS_FILE, 0, item.name, item.problem)
        for item in (*presets.configure, *presets.tests)
        if item.problem
    ]
    return found


def subcommands(root: Path) -> dict[str, bool]:
    """Return each subcommand of the master loop of a tree, and True for a subcommand with a help line.

    Complexity: linear in the nodes of the syntax tree of the master.

    Raises:
        OSError: If the master cannot be read
        python_reader.PythonError: If the master does not parse
    """
    found: dict[str, bool] = {}
    for key, value in table_rows(python_reader.read(root / LOOP_MASTER, LOOP_MASTER), "SUBCOMMANDS"):
        if isinstance(value, ast.Tuple):
            line = value.elts[1] if len(value.elts) > 1 else None
            found[key] = not (isinstance(line, ast.Constant) and line.value is None)
    return found


def table_rows(source: python_reader.Source, name: str) -> list[tuple[str, ast.expr]]:
    """Return the text key and the value of each row of a dictionary that a source gives a name at module level.

    Complexity: linear in the statements of the module.
    """
    found: list[tuple[str, ast.expr]] = []
    for node in source.tree.body:
        if not isinstance(node, ast.Assign) or not isinstance(node.value, ast.Dict):
            continue
        if not any(isinstance(target, ast.Name) and target.id == name for target in node.targets):
            continue
        for key, value in zip(node.value.keys, node.value.values, strict=True):
            if isinstance(key, ast.Constant) and isinstance(key.value, str):
                found.append((key.value, value))
    return found


def step_problems(
    registry: rule_registry.Registry, presets: tiers.Tiers, commands: Mapping[str, bool]
) -> list[Problem]:
    """Return a problem for each row of STEPS and HOLDERS that names nothing, and each holder with no row."""
    configure = {item.name for item in presets.configure}
    found: list[Problem] = []
    for step in STEPS:
        texts = [f"the configure preset {step.preset}"] if step.preset and step.preset not in configure else []
        texts += [f"the rule {rule_id}" for rule_id in step.rules if rule_id not in registry.rules]
        if step.command[0] == "loop" and step.command[1] not in commands:
            texts.append(f"the subcommand loop {step.command[1]}")
        entity = f"step {step.name}"
        message = f"the step {step.name} names {{}}, and the tree does not give it"
        found += [Problem(ProblemKind.STALE_ENTRY, MODULE_PATH, 0, entity, message.format(text)) for text in texts]
    words = {rule.held_by for rule in registry.rules.values() if rule.held_by is not None}
    steps = {step.name for step in STEPS}
    for word, holding in sorted(HOLDERS.items()):
        is_named = (holding.rule and holding.rule not in registry.rules) or (holding.step and holding.step not in steps)
        if word not in words or is_named:
            message = f"the row {word} of HOLDERS names no holder of a rule, or a rule or a step that is not there"
            found.append(Problem(ProblemKind.STALE_ENTRY, MODULE_PATH, 0, f"holder {word}", message))
    for rule in sorted(registry.rules.values(), key=lambda rule: rule.line):
        if rule.held_by is not None and rule.held_by not in HOLDERS:
            message = f"the rule {rule.id} has the holder {rule.held_by}, and HOLDERS of {MODULE_PATH} gives it no row"
            entity = f"holder {rule.held_by}"
            found.append(Problem(ProblemKind.NO_CLASS, rule_registry.REGISTRY_PATH, rule.line, entity, message))
    return found


def command_problems(commands: Mapping[str, bool]) -> list[Problem]:
    """Return a problem for each subcommand of utils/bin/loop with no class, and each class name with no subcommand."""
    path = MODULE_PATH
    stepped = {step.command[1] for step in STEPS if step.command[0] == "loop"}
    classed = {*ENTRY_COMMANDS, *TOOL_COMMANDS, *ITEM_COMMANDS}
    found = [
        Problem(
            ProblemKind.NO_CLASS,
            LOOP_MASTER,
            0,
            f"loop {name}",
            f"the subcommand loop {name} has no class.  Give it a step of STEPS, or put it in ENTRY_COMMANDS, "
            f"TOOL_COMMANDS or ITEM_COMMANDS of {path}",
        )
        for name, is_visible in sorted(commands.items())
        if is_visible and name not in classed | stepped
    ]
    for name in sorted(classed - set(commands)):
        message = f"{path} gives a class to loop {name}, which {LOOP_MASTER} does not have"
        found.append(Problem(ProblemKind.STALE_ENTRY, path, 0, f"loop {name}", message))
    return found


def processor_of(configuration: str) -> str:
    """Return the processor of the name of a configuration: the word before its first hyphen."""
    return configuration.partition("-")[0]


def configuration_problems(builds: Sequence[Build]) -> list[Problem]:
    """Return a problem for each build with a configuration that is not the configuration of the block of its preset."""
    found: list[Problem] = []
    for build in builds:
        declared, given = build.preset.configuration, build.configuration
        if not declared or not given or declared == given or processor_of(declared) != processor_of(given):
            continue
        message = (
            f"the block of the preset {build.preset.name} gives the configuration {declared}, and its build {build.name} "
            f"has the configuration {given}.  Give the block the configuration of the build"
        )
        found.append(Problem(ProblemKind.CONFIGURATION, tiers.PRESETS_FILE, 0, build.preset.name, message))
    return found


# ── The plan ───────────────────────────────────────────────────────────────


def plan(
    root: Path,
    registry: rule_registry.Registry,
    presets: tiers.Tiers,
    builds: Sequence[Build],
    targets: Sequence[Target],
) -> Plan:
    """Return the plan of a tree with some builds and the targets of `loop derive` (THE ITEMS, THE PROBLEMS).

    Complexity: linear in the presets, the tests of the builds and the rules.  One parse of the module of each rule and
    of each target.

    Raises:
        OSError: If the master loop cannot be read
        python_reader.PythonError: If the master loop or the module of a rule does not parse
    """
    rules = rule_items(builds, registry, root)
    items = [
        *preset_items(presets),
        *test_items(builds, root),
        *rules,
        *each_items(Kind.BENCH, (name for build in builds for name in build.benches), lambda _: "bench/CMakeLists.txt"),
        *each_items(Kind.HARNESS, harness_names(root), harness_file),
        *derive_items(root, registry, rules, targets),
        *step_items(),
    ]
    commands = subcommands(root)
    problems = [
        *preset_problems(presets),
        *step_problems(registry, presets, commands),
        *command_problems(commands),
        *configuration_problems(builds),
    ]
    return Plan(tuple(items), tuple(builds), tuple(problems), untargeted_items(items, presets, builds, registry))


def derive_targets(root: Path) -> tuple[Target, ...]:
    """Return each target of `loop derive` of a tree, with its module, from the table TARGETS of DERIVE_MODULE.

    A row of the table is a name and a call with the module as its first argument (assay.loop.derive, THE TARGETS).
    A tree with no such module has no target.  Complexity: linear in the nodes of the syntax tree of the module.

    Raises:
        python_reader.PythonError: If the module does not parse
    """
    path = root / DERIVE_MODULE
    if not path.is_file():
        return ()
    found: list[Target] = []
    for key, value in table_rows(python_reader.read(path, DERIVE_MODULE), "TARGETS"):
        first = value.args[0] if isinstance(value, ast.Call) and value.args else None
        if isinstance(first, ast.Constant) and isinstance(first.value, str):
            found.append(Target(key, first.value))
    return tuple(found)


def tree_plan(root: Path, registry: rule_registry.Registry, directories: Sequence[Path] = ()) -> Plan:
    """Return the plan of a tree with its builds of THE BUILDS.

    Raises:
        PlanError: If a build directory is the build of no configure preset, or its tests do not read
        OSError: If the master loop cannot be read
        python_reader.PythonError: If the master loop or the module of a rule does not parse
    """
    presets = tiers.read(root)
    return plan(root, registry, presets, read_builds(presets, directories), derive_targets(root))


# ── The command ────────────────────────────────────────────────────────────


def reasons(root: Path, registry: rule_registry.Registry) -> dict[tuple[str, str], str]:
    """Return the reason of each row of the ledger of plan-coverage, keyed by its path and its entity."""
    rule = registry.rules.get(RULE)
    if rule is None:
        return {}
    ledger = rule_ledger.read(rule, root)
    return {(key.path, key.entity): entry.reason for key, entry in ledger.base.items()}


def show(found: Plan, rows: Mapping[tuple[str, str], str], kind: str | None, tier: str | None) -> int:
    """Print the items and the problems of a plan, and return the exit status of `loop plan`.

    Complexity: linear in the items and the problems.
    """
    is_clean = not found.problems
    for item in found.items:
        shown_tier = str(item.tier) if item.tier is not None else "none"
        if (kind and item.kind != kind) or (tier and shown_tier != tier):
            continue
        reason = rows.get((item.path, item.entity))
        if not item.covers and reason is None:
            is_clean = False
        texts = "; ".join(cover.text for cover in item.covers) or (f"a row: {reason}" if reason else "runs nowhere")
        check_report.write_line(f"{shown_tier:8} {item.kind:11} {item.name}  ({texts})")
    for problem in found.problems:
        check_report.write_line(f"{problem.path}:{problem.line}: {problem.kind}: {problem.message}")
    return ExitCode.OK if is_clean else ExitCode.FINDINGS


def main(argv: list[str]) -> int:
    """Print the plan of the tree (Usage)."""
    parser = cli.parser("loop plan", "Print each item of the plan with its tier.")
    parser.add_argument("--build-dir", type=Path, action="append", default=[], dest="build_dirs")
    parser.add_argument("--kind", choices=[str(item) for item in Kind])
    parser.add_argument("--tier", choices=[*(str(item) for item in tiers.Tier), "none"])
    parser.add_argument("--root", type=Path, default=paths.REPO_ROOT)
    options = parser.parse_args(argv)
    root = options.root.resolve()
    registry = rule_registry.load(root / rule_registry.REGISTRY_PATH)
    try:
        found = tree_plan(root, registry, options.build_dirs)
    except (PlanError, OSError, python_reader.PythonError) as problem:
        check_report.write_line(f"loop plan: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    return show(found, reasons(root, registry), options.kind, options.tier)
