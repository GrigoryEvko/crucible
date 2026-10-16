"""gate — build and test the commit of a landing in a warm tree, in each configuration that the change selects.

THE CONFIGURATIONS
    CONFIGURATIONS gives each configuration of the gate: each configure
    preset with the tier gate (assay.loop.tiers), the parse presets first.  A
    configuration has the name of its preset and its build directory in the
    warm tree.  The block of the preset gives its name in a ledger
    (assay.core.configuration).  A parse configuration is a preset that sets
    CRUCIBLE_SYNTAX_ONLY.  The ledger names are the names of an x86_64 host,
    the build host of the tree.  The gate reads the presets of the tree of
    the package, and it refuses presets with a problem.

THE COUNTED RULES
    A counted rule is a rule of the engine plugin or elf with the policy
    count or admit and a ledger with a row (counted_ledgers()).  Each one
    reads the objects of the target all of a build, or the reports of a
    parse build.  A ledger holds each configuration that its configuration
    rows name.  A ledger with no configuration row has one set of rows, and
    these rows judge each build of the rule.  Such a ledger holds each
    configuration that checks its rule in its run.  The run of a
    configuration of objects checks each rule.  The run of a parse
    configuration checks only the rules of COUNTED_RULES of
    assay.guard.engines.buildsys.parse_builds.  A parse configuration holds
    no ledger of a different rule (holding()).

    A write in the build of a configuration changes only the counts of that
    configuration (THE WRITE of assay.core.ledger).  The writes of the
    configurations then give one ledger in each sequence.  A write of a
    ledger with no configuration row measures each of its rows, or it keeps
    a row that the build does not judge.  A second write in a different
    build then finds no fall to move, and a move applies one time.  `guard
    write --all-configurations` writes each configuration of a counted
    ledger in its build directory (assay.guard.write_configurations), and
    the gate writes the falls of each configuration that it builds (THE
    FALLS).

THE SELECTION
    The gate holds one invariant: a landing cannot make a test red in a
    configuration of the gate when the test can see the change.  Each
    configuration of objects then runs for each change, in one of three
    modes, in the sequence of CONFIGURATIONS (Mode):
      * The full suite, `loop test BUILD --all`.  select() gives these
        configurations:
          - default for each change
          - release and tsan for a change of a C++ file or of a build file.
            ThreadSanitizer gives incorrect errors of -Wmaybe-uninitialized
            that the other builds do not give, also for a change of a test
          - tsan and ubsan-strict for a change of include/, src/ or cmake/,
            or of a build file at the top of the tree
          - bench-fuzz for a change of bench/ or test/fuzz/
          - each configuration for a change of the quarantine plugin or of
            the rule table, and for --all-configurations.
      * The tests of the changed inputs, `loop test BUILD`, in each other
        configuration of objects, when the change can change each build.
        Such a change holds a C++ file or a build file.  It can also change
        an input of the test launcher or of the fixture driver
        (meter_inputs() of assay.loop.affected).  The build then compiles
        again what the change touches.  `loop test` runs each test with a
        changed input, each script test, each fixture, and each test that
        reads the build.
      * The touched tests, `loop test BUILD --only FILE`, in each other
        configuration of objects, for each other change.  A touched test
        reads the build, and the change touches one of its inputs.
        build_tests() of assay.loop.affected gives the tests that read the
        build from the build of default, after the run of default.  That run
        configures the build with the change.  Inputs.is_touched() of
        assay.loop.inputs judges each one (THE TOUCH).  A test that reads no
        build gives the same result in each configuration, and default runs
        it.  When the change touches no such test, these configurations do
        not run.
    A build file is a file CMakeLists.txt, a file *.cmake or
    CMakePresets.json.

    A parse configuration runs when `loop syntax --changed` of the warm tree
    names it for a change of an arm.  It also runs when a path of the change
    touches the inputs of a rule of COUNTED_RULES of
    assay.guard.engines.buildsys.parse_builds.  That path is no C or C++ file
    and no build file.  A change of such a file changes the result of a parse
    build only through an arm.  Each parse configuration also runs for a
    change of the plugin or of the rule table, and for --all-configurations.

    A ledger can name a configuration that the host cannot build, such as an
    aarch64 configuration of objects.  The gate then writes a note and does
    not build it.

    The configurations of the last two modes run after the last
    configuration of the full suite (THE RUN OF A CONFIGURATION).  The model
    holds no inputs of a script test that reads a build, such as the parts of
    the test of the fixture store.  Such a test runs only in the first two
    modes.

    An executable test reads the build too.  Its inputs in the source tree
    are each file that its binary names and each key part of its read set
    (assay.loop.readset).  build_tests() does not give such a test.  Such a
    test then also runs only in the first two modes.  On 2026-10-14, one executable
    test of the warm tree had such an input: bench/baselines/record_leaf.json
    in the bench and fuzz build.  A change of bench/ selects the full suite
    of that build.

    The rows of each counted ledger hold the counts of each configuration at
    each commit of main.  Each configuration compiles the files of include/
    and src/.  The bench targets also compile the arms of CRUCIBLE_BENCH in
    them.  An arm in include/crucible/BackgroundThread.h is an example.  An
    arm that only one configuration compiles then gets its verdict in the
    landing of the change.  This applies to a count above its row and to a
    count below it.

    A change that no unit of a configuration reads makes its build do
    nothing.  A parse build then keeps the report of each unit (THE KEPT
    REPORTS of assay.loop.syntax).

THE WARM TREE
    One repository in the directory TREE of the work directory holds the
    commit of each landing.  It borrows the objects of the repository of the
    landing through the file objects/info/alternates.  move_tree() gives it
    the commit with `git read-tree --reset -u` and `git update-ref
    --no-deref HEAD`.  Git writes only the files that the commit changes.
    Each other file keeps its time of change, and Ninja builds again only
    what the commit changes.  The build directory of each configuration is
    in the warm tree, at the binaryDir of its preset.  The build directories
    stay from one landing to the next.  A guard that reads the git history
    reads the history of the landing.  The gate refuses a warm tree in the
    work tree of the repository.

THE RUN OF A CONFIGURATION
    The HEAD of the warm tree is the commit, with no branch.  The change of the
    tree is then the commit on its first parent (assay.core.git, THE
    CHANGE).  The suite does not get the variable CRUCIBLE_CHANGE_BASE,
    because a self-test plants repositories of its own, and the variable
    names no commit of them.  A batch of more than one request, or a request
    with a row of the kind `grow`, has more than one commit
    (assay.loop.land).  The rule quarantine-lines of the suite then judges
    only the last commit.  For such a landing, the gate runs `guard check
    --rule quarantine-lines --build-dir BUILD` again, with the variable set
    to the commit that the composition started from.  `loop
    syntax --changed` gets the variable too.  Each configuration of the
    full suite runs as one heavy step (assay.loop.heavy), with the lock of
    the repository of the landing.  The configurations of the two other
    modes run after the last configuration of the full suite, in its heavy
    step.  Each heavy step waits for the memory, and this sequence saves one
    wait for each landing.  The landing holds the
    session of the gate (THE PRIORITY OF THE GATE of assay.loop.heavy).  The
    step of the gate then does not wait for it.  The parse configurations
    run first.  Then the tests guard_parse_* of the debug build read current
    reports.
      1. A build directory with no graph of Ninja gets its configure:
         `cmake --preset PRESET -B BUILD`.
      2. A parse configuration: `loop syntax BUILD`, then `guard check
         --build-dir BUILD` of each rule of COUNTED_RULES of
         assay.guard.engines.buildsys.parse_builds.  A check that fails
         gets THE FALLS, and then each check runs again.
      3. A configuration of objects: `loop test BUILD` with the words of its
         mode, which builds the target all and runs the tests of the mode
         (assay.loop.affected, THE SELECTION).  A retry after a move of HEAD
         and each subsequent run of a batch run `loop test BUILD` in a
         configuration of the full suite.  That command runs only the tests
         with an input that changed, and each test of a rule that reads a
         build.  The file of --only is in the directory of the logs.  A red
         test gets THE FALLS first.
         Then each test that is red runs one more time, alone.  A test that
         is red alone too rejects the commit.
         An exit other than 0 with no red test rejects the commit too.  Then
         `guard warnings BUILD`.  The suite holds the ledger checks of the build
         kind, such as the tests guard_plugin (with the rule
         quarantine-lines), guard_artifact_symbols and guard_library_bytes.
         A second run of these checks gives no new verdict.  A test that gives
         a skip does not reject the commit, and the gate writes its name.
         A red test that runs alone writes its output to its log.
    Each step runs the masters of the warm tree.  Then the tests of the
    commit judge the commit.  The step writes its two outputs to a log in
    the directory of the logs of the gate (THE LOGS).  The first
    configuration with a problem stops the gate.

THE FALLS
    A count that falls makes the check of its counted rule fail with a count
    below its row.  A row of the policy admit that admits no finding makes
    the check fail too.  The gate then runs `guard write --rule RULE
    --build-dir BUILD` of each counted rule with a ledger that holds the
    configuration, in the mode lower.  That write refuses each count above
    its row and each new row, other than an increase that a move pays (THE
    MOVE of assay.core.ledger).  The write gets the variable
    CRUCIBLE_CHANGE_BASE with the commit that the composition started from.
    Its moves are then those of the full change, also after an earlier write
    changed a ledger of the warm tree.  The check then keeps a refused count
    as its error.  After the
    last configuration, the gate folds each ledger that a write changed into
    a new commit (replaced_commit() of assay.core.git).  The new commit has
    the parents and the message of the commit, and its tree holds these
    ledgers.  A landing of more than one commit gets the ledgers in its last
    commit.  A row of THE GROWTH of assay.loop.land then names its first
    commit as before.

    Before the fold, when a write changed a ledger, the gate runs `guard
    write --rule history --root TREE` in the mode lower.  That write removes
    each row of the ledger of the rule history that admits nothing
    (assay.guard.engines.meta.history).  A fall can make a row of the kind
    `grow` or `rebase` admit nothing.  The fall removes the rows of the
    growth that such a row admits.  The ledger of the rule history then goes
    into the new commit with the other ledgers.  A tree with no rule history
    in its registry gets no such write.

    The warm tree moves to the new commit.  Each configuration of the run
    then runs again.  A parse configuration runs `guard check --build-dir
    BUILD` of each rule of COUNTED_RULES again, on the reports that it
    keeps, with no write.  A ledger that the gate folded can make such a
    check red, and the gate then rejects the commit.  A configuration of the
    full suite runs `loop test BUILD`, which runs each test with an input
    that changed, as the run after a move of HEAD does.  The other
    configurations get the mode of THE SELECTION again, with the ledgers in
    the change.

    The verdict gives the new commit and the ledgers, and the landing moves
    the branch to that commit.  A change that lowers counts then lands with
    no manual write of a ledger.

THE KEPT ROWS
    A measured rule of the policy max keeps an item over its warning
    threshold with a row of its ledger.  The row keeps the item up to the
    value of the row, with the noise of its unit (assay.core.budget, THE
    NOISE).  The dimension of an object has no noise.  An object that
    becomes one byte larger than its row then fails the test of the rule.
    That also occurs in a configuration that the author of the change does
    not build.  The candidate can hold rows of `--keep` RULE REASON of
    assay.loop.land, each for such a rule.  When the test of such a rule is
    red in a configuration, the gate runs `guard write --rule RULE
    --build-dir BUILD` there.  It runs the write after THE FALLS and before
    the run of the test alone.  The write gives each row of the
    configuration the measured value of its item.  It gives no row to an
    item over the error threshold with no row, and it refuses such an item.
    That error then stays, and the test stays red.  Each row with a value
    that rose gets REASON as its reason.  The ledger goes into the commit of
    THE FALLS, and the field kept of the verdict gives each raised row.  The
    write adds no row to the ledger, and the rule history then sees no
    growth.

THE VERDICT
    The gate has no base side: it does not compare the commit with HEAD.  A
    test that is red alone on HEAD then rejects each landing, until a commit
    corrects it.  Main must stay green.

THE LOGS
    Each step writes its log into the directory of the logs of its gate.
    That is the directory LOGS of the work directory, or a directory of the
    run in LOGS.  next_run() makes a new directory of a run, one above the
    highest number of a run.  It removes each directory of a run, other than
    the last RUNS_KEPT.  The problems of a refused landing then name logs
    that the next run does not change.

THE LOCK OF THE TREE
    One landing at a time uses the warm tree.  tree_lock() holds an
    exclusive lock of flock(2) on the file LOCK of the work directory.  It
    waits for the lock, or with no wait it tells that a different landing
    holds it.  session() holds the lock and gives the gate.

`guard selftest tests.loop.test_gate` does a test of the module on a planted repository.
"""

import contextlib
import enum
import fcntl
import shutil
import sys
import time
from collections.abc import Callable, Iterator, Mapping, Sequence
from dataclasses import dataclass, replace
from pathlib import Path, PurePosixPath

from assay.core import git, paths, proc
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.guard.engines.buildsys.parse_builds import COUNTED_RULES
from assay.guard.engines.meta import runner as meta_runner
from assay.loop import affected, heavy, tiers
from assay.loop import inputs as guard_inputs
from assay.readers import ctest as ctest_reader
from assay.readers import ninja as ninja_reader
from assay.setup import pins as cmake_pin

# The directory of the warm tree, the lock of the tree and the directory of the logs, in the work directory.
TREE = "tree"
LOCK = "tree.lock"
LOGS = "logs"
# The name of the directory of the logs of one run, before its number, and the directories of the runs that stay (THE
# LOGS).
RUN_MARK = "run-"
RUN_WIDTH = 6
RUNS_KEPT = 24
# The build files of a change that selects the release build and ThreadSanitizer, as a C or C++ file does (THE
# SELECTION, COMPILED_SUFFIXES of assay.loop.inputs).
BUILD_FILE_NAMES = ("CMakeLists.txt", "CMakePresets.json")
BUILD_FILE_SUFFIX = ".cmake"
SANITIZED_PREFIXES = ("include/", "src/", "cmake/")
BENCH_PREFIXES = ("bench/", "test/fuzz/")
EVERY_PREFIXES = ("utils/tools/quarantine/", "utils/rule-table.txt")
LEDGER_PREFIX = "utils/ledgers/"
# The configuration that runs the full suite for each change.  Its build gives the tests that read a build (THE
# SELECTION).
ALWAYS = "default"
# The rule that judges the full change of a landing with two commits again (THE RUN OF A CONFIGURATION).
STACKED_RULE = "quarantine-lines"
# The engines and the policies of a counted rule (THE COUNTED RULES).
COUNTED_ENGINES = frozenset({rule_registry.Engine.PLUGIN, rule_registry.Engine.ELF})
COUNTED_POLICIES = frozenset({rule_registry.Policy.COUNT, rule_registry.Policy.ADMIT})
# The statuses of a test that count as red, and the status of a skip.
RED = frozenset({ctest_reader.TestStatus.FAIL, ctest_reader.TestStatus.NOTRUN})
SKIPPED = ctest_reader.TestStatus.SKIPPED
# The limit of one call of `loop test`, of one configure, and of the run of one test alone, in seconds.
TEST_RUN_S = 2 * proc.BUILD_S
CONFIGURE_S = proc.TOOL_S
ALONE_S = 1500.0


@dataclass(frozen=True, slots=True)
class Configuration:
    """One configuration of the gate: a configure preset with the tier gate (THE CONFIGURATIONS).

    `build` is the build directory of the preset, relative to the tree.
    """

    name: str
    build: str
    ledger_name: str
    is_parse: bool


def configurations(root: Path) -> tuple[Configuration, ...]:
    """Return each configure preset of a tree with the tier gate, the parse presets first (THE CONFIGURATIONS).

    Complexity: one read of the presets file, and linear in the presets.

    Raises:
        GateError: If a preset has a problem, a build directory is not in the tree, or no preset has the tier gate
    """
    read = tiers.read(root)
    if read.each_problem:
        raise GateError(f"the gate refuses the presets of {root}: {'; '.join(read.each_problem)}")
    found: list[Configuration] = []
    for preset in read.configure:
        if preset.tier is not tiers.Tier.GATE or preset.build is None:
            continue
        if not preset.build.is_relative_to(root):
            raise GateError(f"the build directory {preset.build} of the preset {preset.name} is not in the tree {root}")
        build = preset.build.relative_to(root).as_posix()
        found.append(Configuration(preset.name, build, preset.configuration, preset.is_parse))
    if not found:
        raise GateError(f"no configure preset of {root / tiers.PRESETS_FILE} has the tier gate")
    return tuple(sorted(found, key=lambda item: not item.is_parse))


class GateError(Exception):
    """The warm tree cannot hold the commit, or a step of the gate cannot start."""


class Mode(enum.Enum):
    """How one configuration runs (THE SELECTION).  The value is the words that the gate writes before its step."""

    PARSE = "parse and check"
    FULL = "build and test"
    CHANGED = "build and test the changed inputs"
    TOUCHED = "build and run the touched tests"


@dataclass(frozen=True, slots=True)
class Run:
    """One configuration of a run of the gate, its mode, and the names of the tests of the mode TOUCHED."""

    configuration: Configuration
    mode: Mode
    names: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class Keep:
    """One row of `--keep` of a landing: a measured rule of the policy max, and the reason of its rows (THE KEPT ROWS)."""

    rule: str
    reason: str


@dataclass(frozen=True, slots=True)
class Candidate:
    """The commit of a landing: the commit that the composition started from, the commit, the paths and the kept rules.

    `keeps` holds the rows of `--keep` of the requests of the commit (THE KEPT ROWS).
    """

    head: str
    commit: str
    changed: tuple[str, ...]
    keeps: tuple[Keep, ...] = ()


@dataclass(frozen=True, slots=True)
class Verdict:
    """The result of the gate: the configurations that ran, each problem, and the wall times in seconds.

    `seconds` holds the waits for the lock and for the memory.  `step_seconds` gives the time of each configuration in
    its heavy step after the waits, in the sequence of `configurations`.  `final` is the commit that the gate judged
    last, and `ledgers` holds each ledger that THE FALLS folded into it.  `final` is "" for a verdict of no commit.
    `logs` is the directory of the logs of the run (THE LOGS).  `kept` gives each row that a rule of `--keep` raised
    (THE KEPT ROWS).
    """

    configurations: tuple[str, ...]
    problems: tuple[str, ...]
    seconds: float
    step_seconds: tuple[float, ...] = ()
    final: str = ""
    ledgers: tuple[str, ...] = ()
    logs: str = ""
    kept: tuple[str, ...] = ()

    @property
    def is_accepted(self) -> bool:
        """Tell if the gate accepts the commit: no configuration has a problem."""
        return not self.problems


# ── The selection ──────────────────────────────────────────────────────────

CONFIGURATIONS = configurations(paths.REPO_ROOT)
BY_NAME = {configuration.name: configuration for configuration in CONFIGURATIONS}
BY_LEDGER_NAME = {configuration.ledger_name: configuration for configuration in CONFIGURATIONS}


def is_build_file(path: str) -> bool:
    """Tell if a path is a build file (THE SELECTION)."""
    return PurePosixPath(path).name in BUILD_FILE_NAMES or path.endswith(BUILD_FILE_SUFFIX)


def is_compiled_change(path: str) -> bool:
    """Tell if a path of a change is a C or C++ file or a build file, which a build of a configuration can read."""
    return guard_inputs.is_compiled_file(path) or is_build_file(path)


def select(changed: Sequence[str], parse_presets: Sequence[str], is_every: bool) -> tuple[Configuration, ...]:
    """Return the configurations of the full suite and the parse configurations of a change (THE SELECTION).

    The configurations are in the sequence of CONFIGURATIONS.

    Args:
        changed: The paths that the change touches
        parse_presets: The parse presets that run: those that `loop syntax --changed` names, and each parse preset
            when the change touches the inputs of a rule of COUNTED_RULES
        is_every: Select each configuration

    Complexity: linear in the paths and the names.
    """
    names = {ALWAYS}
    for path in changed:
        if is_compiled_change(path):
            names.update(("release", "tsan"))
        is_top_build_file = is_build_file(path) and "/" not in path
        if path.startswith(SANITIZED_PREFIXES) or is_top_build_file:
            names.update(("tsan", "ubsan-strict"))
        if path.startswith(BENCH_PREFIXES):
            names.add("bench-fuzz")
        if path.startswith(EVERY_PREFIXES):
            is_every = True
    names.update(preset for preset in parse_presets if preset in BY_NAME)
    return tuple(item for item in CONFIGURATIONS if is_every or item.name in names)


def changes_each_build(tree: Path, change: guard_inputs.Change, changed: Sequence[str]) -> bool:
    """Tell if a change can change each build: a C or C++ file, a build file, or an input of a meter of a test.

    The meters are the test launcher and the fixture driver (meter_inputs() of assay.loop.affected, THE SELECTION).
    Complexity: linear in the paths, and one parse of each module of the closures of the meters.
    """
    if any(is_compiled_change(path) for path in changed):
        return True
    return any(meter.is_touched(change) for meter in affected.meter_inputs(tree))


def touches_parse_rules(tree: Path, change: guard_inputs.Change) -> bool:
    """Tell if a change touches the inputs of a rule that a parse configuration checks (COUNTED_RULES, THE SELECTION).

    The caller gives the change with no C or C++ file and no build file.  A change of such a file changes the result of
    a parse build only through an arm, and `loop syntax --changed` names the parse configurations of an arm.

    Raises:
        git.GitError: If the tree is not a git work tree

    Complexity: the cost of assay.loop.inputs for each rule of COUNTED_RULES.
    """
    deriver = guard_inputs.Deriver(tree)
    return any(deriver.rule_inputs(rule_id, "check").is_touched(change) for rule_id in COUNTED_RULES)


def other_runs(others: Sequence[Configuration], is_each: bool, names: Sequence[str]) -> tuple[Run, ...]:
    """Return the runs of the configurations of objects that do not run the full suite (THE SELECTION).

    Args:
        others: The configurations of objects that select() does not give
        is_each: The change can change each build (changes_each_build())
        names: The touched tests: each test that reads the build and that has an input that the change touches
    """
    if is_each:
        return tuple(Run(item, Mode.CHANGED) for item in others)
    return tuple(Run(item, Mode.TOUCHED, tuple(names)) for item in others) if names else ()


def guard_test_name(rule_id: str) -> str:
    """Return the name of the test guard_ID of a rule, as cmake/Guards.cmake gives it: each - of the ID becomes _."""
    return "guard_" + rule_id.replace("-", "_")


def counted_ledgers(tree: Path) -> dict[str, tuple[str, ...]]:
    """Return the configuration names of the ledger of each counted rule of a tree (THE COUNTED RULES).

    A ledger with no configuration row gives no name.  A ledger with no row gives a write no fall and no move, and its
    rule is not counted.

    Raises:
        GateError: If the registry or the ledger of a counted rule does not read

    Complexity: one read of the registry, and one read of the ledger of each rule of COUNTED_ENGINES and
    COUNTED_POLICIES.
    """
    try:
        registry = rule_registry.load(tree / rule_registry.REGISTRY_PATH)
    except (OSError, ValueError) as problem:
        raise GateError(f"the registry of {tree} does not read: {problem}") from None
    counted: dict[str, tuple[str, ...]] = {}
    for rule_id, rule in sorted(registry.rules.items()):
        if rule.engine not in COUNTED_ENGINES or rule.policy not in COUNTED_POLICIES:
            continue
        try:
            ledger = rule_ledger.read(rule, tree)
        except (OSError, ValueError) as problem:
            raise GateError(f"the ledger of {rule_id} in {tree} does not read: {problem}") from None
        if ledger.configurations or ledger.base:
            counted[rule_id] = tuple(item.name for item in ledger.configurations)
    return counted


def is_checked(configuration: Configuration, rule_id: str) -> bool:
    """Tell if the run of a configuration checks a rule: each run of objects, and a parse run for COUNTED_RULES."""
    return not configuration.is_parse or rule_id in COUNTED_RULES


def holding(counted: Mapping[str, Sequence[str]], configuration: Configuration) -> list[str]:
    """Return each counted rule with a ledger that holds a configuration, sorted (THE COUNTED RULES).

    A ledger holds each configuration that it names.  A ledger that names no configuration holds each configuration
    that checks its rule (is_checked()).  Complexity: linear in the rules and their names.
    """
    return sorted(
        rule_id
        for rule_id, names in counted.items()
        if is_checked(configuration, rule_id) and (configuration.ledger_name in names or not names)
    )


# ── The warm tree ──────────────────────────────────────────────────────────


def move_tree(root: Path, tree: Path, commit: str) -> None:
    """Give the warm tree the commit, and make the tree first when it is not there (THE WARM TREE).

    Raises:
        GateError: If the tree is in the work tree of the repository
        git.GitError: If a git command fails
    """
    resolved_root, resolved_tree = root.resolve(), tree.resolve()
    if resolved_tree == resolved_root or resolved_root in resolved_tree.parents:
        raise GateError(f"the warm tree {tree} is in the work tree {root} of the repository")
    if not (tree / ".git").is_dir():
        tree.mkdir(parents=True, exist_ok=True)
        git.init(tree)
        git.run(tree, "config", "gc.auto", "0")
    info = tree / ".git" / "objects" / "info"
    info.mkdir(parents=True, exist_ok=True)
    (info / "alternates").write_text(f"{git.common_dir(root) / 'objects'}\n", encoding="utf-8")
    # A file with a new time of change and the same bytes gets a new entry of the index, and read-tree then keeps it.
    git.run_any_exit(tree, "update-index", "-q", "--refresh", timeout_s=proc.TOOL_S)
    git.run(tree, "read-tree", "--reset", "-u", commit, timeout_s=proc.TOOL_S)
    git.run(tree, "update-ref", "--no-deref", "HEAD", commit)


def build_directory(tree: Path, configuration: Configuration) -> Path:
    """Return the build directory of a configuration in a tree, such as the warm tree."""
    return tree / configuration.build


def configure_words(tree: Path, configuration: Configuration) -> list[str]:
    """Return the words of the configure of the build directory of a configuration in a tree."""
    return ["cmake", "--preset", configuration.name, "-B", str(build_directory(tree, configuration))]


def build_words(tree: Path, configuration: Configuration, jobs: int) -> list[str]:
    """Return the words of the build of the target all of a configuration of objects in a tree.

    Raises:
        GateError: If the CMake cache of the build names no cmake
    """
    build = build_directory(tree, configuration)
    try:
        cmake = cmake_pin.configured_program(build, "cmake")
    except cmake_pin.PinError as problem:
        raise GateError(str(problem)) from None
    return [cmake, "--build", str(build), "-j", str(jobs)]


# ── The steps ──────────────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Gate:
    """The gate of one session: the repository, the work directory, the jobs, --all-configurations, and the memory wait.

    `read` and `pause` are the reader of the available memory and the pause of the memory wait (assay.loop.heavy).
    `run` names the directory of the logs of one run in LOGS, or it is "" for LOGS itself (THE LOGS).
    """

    root: Path
    work: Path
    jobs: int
    is_every: bool
    read: heavy.Reader = heavy.available_gb
    pause: heavy.Pause = time.sleep
    run: str = ""

    @property
    def tree(self) -> Path:
        """The warm tree."""
        return self.work / TREE

    @property
    def logs(self) -> Path:
        """The directory of the logs of the run."""
        return self.work / LOGS / self.run if self.run else self.work / LOGS

    def _run_logged(self, command: list[str], log: str, environment: Mapping[str, str], timeout_s: float) -> int:
        """Run one command in the warm tree with its two outputs in a log, and return its exit.

        A command that runs longer than its limit gives the exit of its end.

        Raises:
            GateError: If the program cannot start
        """
        self.logs.mkdir(parents=True, exist_ok=True)
        command_line = proc.Command(
            command,
            timeout_s=timeout_s,
            cwd=self.tree,
            env=dict(environment),
            stdout=self.logs / log,
            stderr=proc.Output.MERGE,
        )
        try:
            return proc.run(command_line).exit
        except proc.TimedOut as problem:
            return problem.completed.exit if problem.completed is not None else ExitCode.TOOL_FAILED
        except proc.ToolFailed as problem:
            raise GateError(f"{command[0]} cannot run: {problem}") from None

    def _master(self, name: str) -> list[str]:
        """Return the words that run a master of the warm tree."""
        return [sys.executable, str(self.tree / "utils" / "bin" / name)]

    def _parse_presets(self, environment: Mapping[str, str]) -> list[str]:
        """Return each parse preset that `loop syntax --changed` of the warm tree names.

        Raises:
            GateError: If the command cannot run or gives an error
        """
        command = [*self._master("loop"), "syntax", "--changed", "--root", str(self.tree)]
        try:
            completed = proc.run(proc.Command(command, timeout_s=proc.TOOL_S, cwd=self.tree, env=dict(environment)))
        except proc.ToolFailed as problem:
            raise GateError(f"`loop syntax --changed` cannot run: {problem}") from None
        if completed.exit != ExitCode.OK:
            raise GateError(f"`loop syntax --changed` gives an error: {completed.err.strip()}")
        return completed.out.split()

    def _configure(self, configuration: Configuration, environment: Mapping[str, str]) -> str | None:
        """Configure the build directory of a configuration when it has no graph of Ninja, and give a problem."""
        build = build_directory(self.tree, configuration)
        if ninja_reader.graph_file(build).is_file():
            return None
        log = f"{configuration.name}.configure.log"
        if self._run_logged(configure_words(self.tree, configuration), log, environment, CONFIGURE_S) != 0:
            return f"{configuration.name}: the configure gives an error, see {self.logs / log}"
        return None

    def _checked(self, configuration: Configuration, rules: Sequence[str], environment: Mapping[str, str]) -> list[str]:
        """Run `guard check --build-dir` of each rule on a build, and give a problem for each exit other than 0 or 77."""
        build = build_directory(self.tree, configuration)
        problems: list[str] = []
        for rule in rules:
            log = f"{configuration.name}.check-{rule}.log"
            command = [*self._master("guard"), "check", "--rule", rule, "--build-dir", str(build)]
            status = self._run_logged(command, log, environment, proc.TOOL_S)
            if status not in (ExitCode.OK, ExitCode.SKIPPED):
                problems.append(f"{configuration.name}: the rule {rule} gives findings, see {self.logs / log}")
        return problems

    def _write_falls(
        self, configuration: Configuration, counted: Mapping[str, Sequence[str]], based: Mapping[str, str]
    ) -> bool:
        """Run `guard write` of each counted rule with a ledger that holds a configuration (THE FALLS).

        `based` is the environment with the base of the change.  A write that refuses a count above its row exits 1,
        and the check of the rule keeps that count as its error.  Return True when a rule holds the configuration.
        """
        build = build_directory(self.tree, configuration)
        rules = holding(counted, configuration)
        for rule in rules:
            log = f"{configuration.name}.write-{rule}.log"
            command = [*self._master("guard"), "write", "--rule", rule, "--build-dir", str(build)]
            status = self._run_logged(command, log, based, proc.TOOL_S)
            check_report.write_line(f"loop land: {configuration.name}: the write of {rule} exits {status}")
        return bool(rules)

    def _checked_falls(
        self,
        configuration: Configuration,
        rules: Sequence[str],
        counted: Mapping[str, Sequence[str]],
        environment: Mapping[str, str],
        based: Mapping[str, str],
    ) -> list[str]:
        """Run the checks of some rules on a build, and after a failure THE FALLS and the checks again."""
        problems = self._checked(configuration, rules, environment)
        if problems and self._write_falls(configuration, counted, based):
            problems = self._checked(configuration, rules, environment)
        return problems

    def _write_history(self, environment: Mapping[str, str]) -> None:
        """Write the ledger of the rule history of the warm tree in the mode lower (THE FALLS).

        The write removes each row that admits nothing.  A write that refuses a finding exits 1 and writes nothing, and
        the test of the rule then keeps that finding as its error.  A tree with no rule history in its registry gets no
        write.

        Raises:
            GateError: If the registry of the warm tree does not read
        """
        from assay.guard.engines.meta import history

        try:
            registry = rule_registry.load(self.tree / rule_registry.REGISTRY_PATH)
        except (OSError, ValueError) as problem:
            raise GateError(f"the registry of {self.tree} does not read: {problem}") from None
        if history.RULE not in registry.rules:
            return
        command = [*self._master("guard"), "write", "--rule", history.RULE, "--root", str(self.tree)]
        status = self._run_logged(command, f"write-{history.RULE}.log", environment, proc.TOOL_S)
        check_report.write_line(f"loop land: the write of {history.RULE} exits {status}")

    def _fold(self, candidate: Candidate, environment: Mapping[str, str]) -> tuple[str, tuple[str, ...]]:
        """Fold each ledger that THE FALLS changed in the warm tree into the commit, and give the commit and the ledgers.

        A change of a ledger first gets the write of the ledger of the rule history (_write_history()).  The warm tree
        moves to the new commit.  With no changed ledger, the result is the commit and no ledger.

        Raises:
            GateError: If the registry of the warm tree does not read
            git.GitError: If a git command fails
        """
        if not git.work_changes(self.tree, "HEAD", [LEDGER_PREFIX]):
            return candidate.commit, ()
        self._write_history(environment)
        ledgers = tuple(sorted(git.work_changes(self.tree, "HEAD", [LEDGER_PREFIX])))
        files = {name: (self.tree / name).read_bytes() if (self.tree / name).is_file() else None for name in ledgers}
        final = git.replaced_commit(self.root, candidate.commit, files)
        move_tree(self.root, self.tree, final)
        return final, ledgers

    def _red_alone(
        self, configuration: Configuration, names: Sequence[str], environment: Mapping[str, str]
    ) -> list[str]:
        """Run each test alone, one at a time, and return the names of the tests that are red alone too.

        Raises:
            GateError: If the CMake cache of the build names no ctest
        """
        if not names:
            return []
        build = build_directory(self.tree, configuration)
        selection = self.logs / f"{configuration.name}.alone.txt"
        report = self.logs / f"{configuration.name}.alone.junit.xml"
        try:
            ctest = cmake_pin.configured_program(build, "ctest")
        except cmake_pin.PinError as problem:
            raise GateError(str(problem)) from None
        self.logs.mkdir(parents=True, exist_ok=True)
        red: list[str] = []
        for name in names:
            selection.write_text(f"{name}\n", encoding="utf-8")
            report.unlink(missing_ok=True)
            command = [ctest, "--test-dir", str(build), "--tests-from-file", str(selection)]
            command += ["--timeout", str(int(ALONE_S)), "--output-on-failure", "--output-junit", str(report)]
            self._run_logged(command, f"{configuration.name}.alone.{name}.log", environment, ALONE_S)
            try:
                status = ctest_reader.results(report).get(name, ctest_reader.TestStatus.NOTRUN)
            except ctest_reader.CtestError:
                status = ctest_reader.TestStatus.NOTRUN
            if status in RED:
                red.append(name)
        return red

    def _run_parse(
        self,
        configuration: Configuration,
        counted: Mapping[str, Sequence[str]],
        environment: Mapping[str, str],
        based: Mapping[str, str],
    ) -> list[str]:
        """Do steps 1 and 2 of THE RUN OF A CONFIGURATION for a parse configuration, and give each problem."""
        problem = self._configure(configuration, environment)
        if problem is not None:
            return [problem]
        build = build_directory(self.tree, configuration)
        log = f"{configuration.name}.syntax.log"
        command = [*self._master("loop"), "syntax", str(build), "-j", str(self.jobs)]
        if self._run_logged(command, log, environment, TEST_RUN_S) != 0:
            return [f"{configuration.name}: a unit does not parse, see {self.logs / log}"]
        return self._checked_falls(configuration, COUNTED_RULES, counted, environment, based)

    def _write_keeps(
        self,
        configuration: Configuration,
        keeps: Sequence[Keep],
        red: Sequence[str],
        environment: Mapping[str, str],
    ) -> list[str]:
        """Write the measured value of each row of each rule of `--keep` with a red test, and return the raised rows.

        A raised row gets the reason of its `--keep` (THE KEPT ROWS).  A write that refuses an item exits 1, and the
        test of the rule keeps that item as its error.

        Raises:
            GateError: If the registry or a ledger of the warm tree does not read
        """
        build = build_directory(self.tree, configuration)
        raised: list[str] = []
        for keep in keeps:
            if guard_test_name(keep.rule) not in red:
                continue
            try:
                rule = rule_registry.load(self.tree / rule_registry.REGISTRY_PATH).rules[keep.rule]
                before = rule_ledger.read(rule, self.tree)
            except (OSError, ValueError, KeyError) as problem:
                raise GateError(f"the ledger of the rule {keep.rule} of --keep does not read: {problem}") from None
            log = f"{configuration.name}.keep-{keep.rule}.log"
            command = [*self._master("guard"), "write", "--rule", keep.rule, "--build-dir", str(build)]
            status = self._run_logged(command, log, environment, proc.TOOL_S)
            check_report.write_line(f"loop land: {configuration.name}: the write of --keep {keep.rule} exits {status}")
            if status != ExitCode.OK:
                continue
            try:
                after = rule_ledger.read(rule, self.tree)
            except (OSError, ValueError) as problem:
                raise GateError(f"the ledger of the rule {keep.rule} of --keep does not read: {problem}") from None
            base = dict(after.base)
            for key, entry in after.base.items():
                old = before.base.get(key)
                if old is None or not isinstance(entry.value, int | float) or not isinstance(old.value, int | float):
                    continue
                if entry.value > old.value:
                    base[key] = replace(entry, reason=keep.reason)
                    values = f"from {rule_ledger.value_text(old.value)} to {rule_ledger.value_text(entry.value)}"
                    raised.append(f"{keep.rule}: {key.path} in {key.kind} {values}")
            rule_ledger.store(replace(after, base=base), self.tree)
        return raised

    def _run_objects(
        self,
        configuration: Configuration,
        counted: Mapping[str, Sequence[str]],
        environment: Mapping[str, str],
        based: Mapping[str, str],
        words: Sequence[str],
        keeps: Sequence[Keep] = (),
        kept: list[str] | None = None,
    ) -> list[str]:
        """Do steps 1 and 3 of THE RUN OF A CONFIGURATION for a configuration of objects, and give each problem.

        `words` are the words of the mode after `loop test BUILD` (loop_words()).  With no counted rule in `counted`, a
        red test gets no write (THE FALLS).  A red test of a rule of `keeps` gets the write of THE KEPT ROWS, and `kept`
        receives each raised row.
        """
        problem = self._configure(configuration, environment)
        if problem is not None:
            return [problem]
        build = build_directory(self.tree, configuration)
        report = build / affected.STATE_DIRECTORY / "junit.xml"
        report.unlink(missing_ok=True)
        log = f"{configuration.name}.test.log"
        command = [*self._master("loop"), "test", str(build), "-j", str(self.jobs), *words]
        status = self._run_logged(command, log, environment, TEST_RUN_S)
        statuses: dict[str, ctest_reader.TestStatus] = {}
        if report.is_file():
            try:
                statuses = ctest_reader.results(report)
            except ctest_reader.CtestError as problem_text:
                return [f"{configuration.name}: the test report does not read: {problem_text}"]
        elif status != 0:
            return [f"{configuration.name}: the build or the test run gives an error, see {self.logs / log}"]
        red = sorted(name for name, test_status in statuses.items() if test_status in RED)
        if status != 0 and not red:
            return [f"{configuration.name}: `loop test` exits {status} with no red test, see {self.logs / log}"]
        skipped = sorted(name for name, test_status in statuses.items() if test_status is SKIPPED)
        if skipped:
            check_report.write_line(f"loop land: {configuration.name}: {len(skipped)} skipped: {' '.join(skipped)}")
        if red:
            self._write_falls(configuration, counted, based)
            raised = self._write_keeps(configuration, keeps, red, environment)
            if kept is not None:
                kept += raised
            check_report.write_line(f"loop land: {configuration.name}: {len(red)} red, each runs again alone")
        still = self._red_alone(configuration, red, environment)
        if still:
            return [f"{configuration.name}: red alone too: {' '.join(still)}, see {self.logs / log}"]
        warnings_log = f"{configuration.name}.warnings.log"
        if self._run_logged([*self._master("guard"), "warnings", str(build)], warnings_log, environment, proc.TOOL_S):
            return [f"{configuration.name}: `guard warnings` gives an error, see {self.logs / warnings_log}"]
        return []

    def history_problems(self, commit: str, first: str, grown: Sequence[str]) -> tuple[str, ...]:
        """Give the warm tree a commit, and return each error of the rule history there, with no build.

        The function runs findings() of the rule, judged against the ledger of the rule, as `guard check --rule
        history` does.  For each rule of `grown`, growth_kinds() of the rule at the commit `first` must hold a grow
        row (assay.loop.land, THE CHECKS BEFORE THE BUILDS).  A tree with no rule history, and a rule that does not
        apply to the tree, give no error.  Complexity: one read of the history of the warm tree for the rule, and
        one more read for each rule of `grown`.

        Raises:
            GateError: If the warm tree is in the work tree of the repository
            git.GitError: If a git command fails
        """
        from assay.guard.engines.meta import history

        move_tree(self.root, self.tree, commit)
        context = meta_runner.MetaContext(self.tree)
        rule = context.registry.rules.get(history.RULE)
        if rule is None:
            return ()
        problems: list[str] = []
        try:
            for rule_id in grown:
                kinds = history.growth_kinds(self.tree, context.registry, rule_id, first)
                if history.GROW_KIND in kinds:
                    continue
                problems.append(
                    f"--grow {rule_id}: no grow row admits a growth of {rule_ledger.shown_path(rule_id)} at the commit "
                    f"{first[: history.SHORT_HASH]}, and the rows of that growth are of the kinds "
                    f"{' and '.join(kinds) or 'none'}.  Land the change with no --grow {rule_id}, and then admit its "
                    f"rows with `guard write --rule history --growth {rule_id} COMMIT --reason TEXT`"
                )
            found = meta_runner.judged(rule, self.tree, history.findings(context))
        except meta_runner.NotApplicable as reason:
            check_report.write_line(f"loop land: the rule history does not apply before the builds: {reason}")
            return tuple(problems)
        return (*problems, *(item.text() for item in found if item.level == "error"))

    def loop_words(self, run: Run, is_plain: bool) -> list[str]:
        """Return the words of the mode of a run after `loop test BUILD` (THE SELECTION).

        A run of the full suite with `is_plain` runs the tests of the changed inputs.  The run of the touched tests
        writes the file of --only into the directory of the logs.
        """
        if run.mode is Mode.TOUCHED:
            self.logs.mkdir(parents=True, exist_ok=True)
            only = self.logs / f"{run.configuration.name}.only.txt"
            only.write_text("".join(f"{name}\n" for name in run.names), encoding="utf-8")
            return ["--only", str(only)]
        return ["--all"] if run.mode is Mode.FULL and not is_plain else []

    def touched_names(self, change: guard_inputs.Change) -> tuple[str, ...]:
        """Return each touched test of the build of ALWAYS, sorted: it reads the build, and the change touches its inputs.

        The run of ALWAYS configures its build with the change first (THE SELECTION).

        Raises:
            GateError: If the build of ALWAYS or its tests cannot be read
        """
        build = build_directory(self.tree, BY_NAME[ALWAYS])
        try:
            tests = affected.build_tests(build)
        except affected.RunnerError as problem:
            raise GateError(f"the tests of {build} that read the build do not read: {problem}") from None
        return tuple(sorted(name for name, found in tests.items() if found.is_touched(change)))

    def other_plan(
        self, candidate: Candidate, others: Sequence[Configuration], ledgers: Sequence[str] = ()
    ) -> tuple[Run, ...]:
        """Return the runs of the configurations of objects with no full suite, for the change and `ledgers`.

        Raises:
            GateError: If the build of ALWAYS or its tests cannot be read
            git.GitError: If git cannot read the registry at the commit that the change starts from
        """
        if not others:
            return ()
        changed = (*candidate.changed, *ledgers)
        change = guard_inputs.read_change(self.tree, candidate.head, changed)
        if changes_each_build(self.tree, change, changed):
            return other_runs(others, True, ())
        names = self.touched_names(change)
        shown = " ".join(names) if names else "none"
        check_report.write_line(f"loop land: the touched tests of the other configurations of objects: {shown}")
        return other_runs(others, False, names)

    def check(self, candidate: Candidate, is_retry: bool) -> Verdict:
        """Give the warm tree the commit, select the configurations, and run each of them (THE RUN OF A CONFIGURATION).

        Raises:
            GateError: If the warm tree cannot hold the commit, or a step cannot start
            git.GitError: If a git command fails
        """
        started = time.monotonic()
        move_tree(self.root, self.tree, candidate.commit)
        based = proc.environment({git.CHANGE_BASE_VARIABLE: candidate.head})
        environment = {name: value for name, value in based.items() if name != git.CHANGE_BASE_VARIABLE}
        is_stacked = candidate.commit != candidate.head and git.parent(self.tree, candidate.commit) != candidate.head
        presets = self._parse_presets(based)
        uncompiled = [path for path in candidate.changed if not is_compiled_change(path)]
        if touches_parse_rules(self.tree, guard_inputs.read_change(self.tree, candidate.head, uncompiled)):
            presets += [item.name for item in CONFIGURATIONS if item.is_parse]
        counted = counted_ledgers(self.tree)
        for name in sorted({item for listed in counted.values() for item in listed}):
            if name not in BY_LEDGER_NAME:
                check_report.write_line(f"loop land: the host does not build the configuration {name} of a ledger")
        selected = select(candidate.changed, presets, self.is_every)
        plan = tuple(Run(item, Mode.PARSE if item.is_parse else Mode.FULL) for item in selected)
        others = tuple(item for item in CONFIGURATIONS if not item.is_parse and item not in selected)
        shown = " ".join(item.name for item in selected)
        rest = f", and then the tests of the change in {' '.join(item.name for item in others)}" if others else ""
        check_report.write_line(
            f"loop land: the commit {candidate.commit[:9]} runs in {shown}{rest}, logs in {self.logs}"
        )
        ran: list[str] = []
        spent: list[float] = []
        kept: list[str] = []

        def run_of(run: Run, is_again: bool) -> list[str]:
            """Run one configuration of the plan, or its tests again after THE FALLS, and give each problem."""
            configuration = run.configuration
            if configuration.is_parse and is_again:
                return self._checked(configuration, COUNTED_RULES, environment)
            if configuration.is_parse:
                return self._run_parse(configuration, counted, environment, based)
            problems = self._run_objects(
                configuration,
                {} if is_again else counted,
                environment,
                based,
                self.loop_words(run, is_retry or is_again),
                () if is_again else candidate.keeps,
                kept,
            )
            if is_stacked and not problems:
                problems = self._checked(configuration, (STACKED_RULE,), based)
            return problems

        def run_each(runs: Sequence[Run], is_again: bool) -> list[str]:
            """Run some configurations in the heavy step of the caller, keep the name and the time of each, and give
            each problem."""
            for run in runs:
                name = run.configuration.name
                check_report.write_line(f"loop land: {name}: {'test again' if is_again else run.mode.value}")
                began = time.monotonic()
                problems = run_of(run, is_again)
                spent.append(round(time.monotonic() - began, 1))
                check_report.write_line(f"loop land: {name}: {spent[-1]:.0f} s in the heavy step")
                ran.append(name)
                if problems:
                    return problems
            return []

        def step(runs: Sequence[Run], is_again: bool, tail: Callable[[], Sequence[Run]] | None = None) -> list[str]:
            """Run some configurations in one heavy step, then the runs that `tail` gives, and give each problem.

            The step calls `tail` after the configurations, because the plan of the other configurations reads the
            build of ALWAYS after its run (THE SELECTION).
            """
            with heavy.heavy_step(heavy.FULL_LEVEL_GB, self.root, read=self.read, pause=self.pause, is_gate_step=True):
                problems = run_each(runs, is_again)
                return problems if problems or tail is None else run_each(tail(), is_again)

        def stopped(problems: Sequence[str]) -> Verdict:
            """Return the verdict of a run that a configuration with a problem stopped."""
            elapsed = time.monotonic() - started
            return Verdict(
                tuple(ran),
                tuple(problems),
                elapsed,
                tuple(spent),
                candidate.commit,
                logs=str(self.logs),
                kept=tuple(kept),
            )

        *first, last = plan
        for run in first:
            problems = step((run,), False)
            if problems:
                return stopped(problems)
        problems = step((last,), False, lambda: self.other_plan(candidate, others))
        if problems:
            return stopped(problems)
        final, ledgers = self._fold(candidate, environment)
        if ledgers:
            check_report.write_line(
                f"loop land: the writes of the gate changed {' '.join(ledgers)}, folded into the commit {final[:9]}, "
                f"and each configuration of the run runs again"
            )
            # The checks of the parse configurations read kept reports, and they run in the first heavy step.
            parses = tuple(run for run in plan if run.configuration.is_parse)
            steps: list[tuple[Run, ...]] = [(run,) for run in plan if not run.configuration.is_parse]
            steps[0] = (*parses, *steps[0])
            for index, runs in enumerate(steps):
                is_last = index == len(steps) - 1
                problems = step(runs, True, (lambda: self.other_plan(candidate, others, ledgers)) if is_last else None)
                if problems:
                    break
        elapsed = time.monotonic() - started
        return Verdict(tuple(ran), tuple(problems), elapsed, tuple(spent), final, ledgers, str(self.logs), tuple(kept))


def next_run(work: Path) -> str:
    """Make the directory of the logs of a new run, remove the expired ones, and return its name (THE LOGS).

    Complexity: linear in the directories of the runs.
    """
    logs = work / LOGS
    logs.mkdir(parents=True, exist_ok=True)
    runs = [path for path in paths.listing(logs, f"{RUN_MARK}*") if path.name.removeprefix(RUN_MARK).isdigit()]
    number = max((int(path.name.removeprefix(RUN_MARK)) for path in runs), default=0) + 1
    while True:
        name = f"{RUN_MARK}{number:0{RUN_WIDTH}d}"
        try:
            (logs / name).mkdir()
            break
        except FileExistsError:
            number += 1
    for old in runs[: max(0, len(runs) + 1 - RUNS_KEPT)]:
        shutil.rmtree(old, ignore_errors=True)
    return name


@contextlib.contextmanager
def tree_lock(work: Path, wait: bool = True) -> Iterator[bool]:
    """Hold the lock of the warm tree for the block, and give True (THE LOCK OF THE TREE).

    With a wait, the block waits while a different landing holds the lock.  With no wait, the block gets False
    immediately when a different landing holds it, and holds nothing.
    """
    work.mkdir(parents=True, exist_ok=True)
    with open(work / LOCK, "a", encoding="ascii") as handle:
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            is_held = True
        except BlockingIOError:
            is_held = False
        if not is_held and wait:
            check_report.write_line(f"loop land: a different landing holds {work / LOCK}, and this one waits")
            fcntl.flock(handle, fcntl.LOCK_EX)
            is_held = True
        yield is_held


@contextlib.contextmanager
def session(root: Path, work: Path, jobs: int, is_every: bool) -> Iterator[Gate]:
    """Hold the lock of the warm tree for the block, and give the gate (THE LOCK OF THE TREE).

    The block waits while a different landing holds the lock.
    """
    with tree_lock(work):
        yield Gate(root, work, jobs, is_every)
