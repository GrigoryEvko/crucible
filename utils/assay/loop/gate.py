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
    modes (Mode):
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
        changed input, each script test with no usable read set, each
        fixture, and each test that reads the build.
      * The touched tests, `loop test BUILD --only FILE`, in each other
        configuration of objects, for each other change.  A touched test
        reads the build, and the change touches one of its inputs.
        build_tests() of assay.loop.affected gives the tests that read the
        build from the build of default.  The run of default first builds
        the target build.ninja of its build, which configures the build
        again with the change (THE THREADS).  Inputs.is_touched() of
        assay.loop.inputs judges each one (THE TOUCH).  A test that reads no
        build gives the same result in each configuration, and default runs
        it.  When the change touches no such test, these configurations do
        not run.
    A build file is a file CMakeLists.txt, a file *.cmake or
    CMakePresets.json.

    A test of the master guard that reads no build gives the same result in
    each configuration (THE TESTS THAT READ THE BUILD of assay.loop.affected).
    default runs the full suite for each change.  The gate then gives each
    other configuration of the first two modes the option --build-only of
    `loop test`, which skips such a test.  The run of the base can have no
    default, and its configurations do not get the option.

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

    The configurations of the last two modes run at the same time as the
    configurations of the full suite (THE THREADS).  The model holds no
    inputs of a script test that reads a build, such as the parts of the
    test of the fixture store.  Such a test runs only in the first two
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
    names no commit of them.  A batch of more than one request has more than
    one commit (assay.loop.land).  The rule quarantine-lines of the suite
    then judges only the last commit.  For such a landing, the gate runs `guard check
    --rule quarantine-lines --build-dir BUILD` again, with the variable set
    to the commit that the composition started from.  `loop
    syntax --changed` gets the variable too.  Each check of the gate runs in
    one heavy step (assay.loop.heavy), with the lock of the repository of
    the landing.  The step reads the memory one time, and the memory gives
    the jobs of the step (THE JOBSERVER).  The landing holds the session of
    the gate (THE PRIORITY OF THE GATE of assay.loop.heavy).  The step of
    the gate then does not wait for it.  In the heavy step, each
    configuration runs in a thread of its own (THE THREADS):
      1. A build directory with no graph of Ninja gets its configure:
         `cmake --preset PRESET -B BUILD`.
      2. A parse configuration: `loop syntax BUILD`, then `guard check
         --build-dir BUILD` of each rule of COUNTED_RULES of
         assay.guard.engines.buildsys.parse_builds, at the same time.  A
         check that fails gets THE FALLS, and then each check runs again.
      3. A configuration of objects: `loop test BUILD` with the words of its
         mode, which builds the target all and runs the tests of the mode
         (assay.loop.affected, THE SELECTION).  A retry after a move of HEAD
         and each subsequent run of a batch run `loop test BUILD` in a
         configuration of the full suite.  That command runs only the tests
         with an input that changed, and each test of a rule that reads a
         build.  The file of --only is in the directory of the logs.  A red
         test gets THE FALLS first.
         Then each test that is red runs one more time, alone, one test at
         a time.  A test that is red alone too rejects the commit.
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
    configuration with a problem stops the others (THE STOP).

THE JOBSERVER
    The heavy step gives the jobs of the gate, or a smaller count when the
    available memory is small (THE MEMORY of assay.loop.heavy).  It makes
    one jobserver of these jobs (assay.core.jobserver).  Each command of the
    step gets the variable
    MAKEFLAGS of that jobserver.  The gate holds one slot of the jobserver
    for each command that it runs: its implicit slot, or one token.  The
    command then runs its first job on that slot.  ninja, ctest and `loop
    syntax` take one token for each other job.  `loop test` gives its build
    no -j with the jobserver, because ninja obeys the jobserver only with no
    -j (assay.loop.affected).  ctest and the pool of `loop syntax` get -j
    with the jobs of the step too.  The jobs of all the configurations
    together are then at most the jobs of the step, and the memory stays
    that of one build.
    Each check makes a new jobserver.  A token that a stopped command did
    not give back then does not make the next check smaller.

THE THREADS
    Each configuration of the plan runs in a thread of its own, at the same
    time as the others.  The other configurations of objects (the last two
    modes of THE SELECTION) run in threads of their own too.  They start
    when their plan is known.  A change that can change each build gives
    their plan immediately.  Else, their plan reads the touched tests of the
    build of ALWAYS.  The run of ALWAYS then first builds the target
    build.ninja of its build, which configures the build again with the
    change.  Then the other configurations start.  The checks of a parse
    configuration run in threads of their own too.  The writes of THE FALLS
    of a configuration also run in threads of their own.

    The verdict gives the configurations, their times and their problems in
    the sequence of the plan and then of the other configurations.  It does
    not give them in the sequence of their ends.

THE STOP
    In the run of a commit, the first configuration with a problem stops
    the others.  A run of the base has no such stop.  Each command
    of the step looks at the stop event each STOP_POLL_S seconds.  At the
    stop, the process group of the command gets the stop of
    Running.stop() of assay.core.proc.  Its configuration then ends with no
    problem of its own.  A command that did not start does not start.  The
    verdict gives each problem of each configuration that ended, and it
    does not give a configuration that the stop ended.  An error of a step
    of the gate stops the others too, and the check raises it after each
    thread ended.  A thread that the error stopped does not hide the error.
    A heavy step that the stop ended with no problem of a configuration is
    an error of the gate (require_problem()).  The landing then refuses the
    commit with that error, and it does not accept it.

    Each command of the gate has the stop word interrupt of
    assay.core.proc.  Its group gets SIGINT, and SIGKILL after STOP_GRACE_S
    seconds.  A master of the package then stops in its own way.  Its wait
    sends SIGINT to the group of each child (AN INTERRUPT of
    assay.core.proc), and ninja and ctest stop their jobs.  SIGTERM ends a
    master with no such step.  Its children run in process groups of their
    own, and they then go on in the warm tree.

THE LOCKS OF THE LEDGERS
    Two configurations that run at the same time can write one ledger.  An
    example is the ledger of the rule quarantine, which holds rows of each
    configuration.  A write reads the ledger, changes the counts of its
    configuration and writes the ledger again.  When two such writes occur
    at one time, the ledger can hold only one of them.  The heavy step then
    holds one lock for each rule.  Each write of a rule, in THE FALLS and in
    THE KEPT ROWS, holds the lock of its rule.  A write takes the lock
    before its slot of the jobserver.  No thread then holds a slot while it
    waits for a lock.  store() of
    assay.core.ledger writes a new file and renames it over the ledger.  A
    test that reads a ledger during a write then reads all of the earlier
    file or all of the new file.  A write of the base count keeps the counts
    of each other configuration (THE WRITE of assay.core.ledger).  The
    sequence of the writes of two configurations then does not change the
    ledger.

THE FALLS
    A count that falls makes the check of its counted rule fail with a count
    below its row.  A row of the policy admit that admits no finding makes
    the check fail too.  The gate then runs `guard write --rule RULE
    --build-dir BUILD` of each counted rule with a ledger that holds the
    configuration, in the mode lower.  These writes run at the same time,
    each with the lock of its rule (THE LOCKS OF THE LEDGERS).  A write
    refuses each count above its row and each new row, other than an
    increase that a move pays (THE MOVE of assay.core.ledger).  The write
    gets the variable CRUCIBLE_CHANGE_BASE with the commit that the
    composition started from.  Its moves are then those of the full change,
    also after an earlier write changed a ledger of the warm tree.  The
    check then keeps a refused count as its error.  After each thread of the
    heavy step ended, the gate folds each ledger that a write changed into a
    new commit (replaced_commit() of assay.core.git).  The new commit has
    the parents and the message of the commit, and its tree holds these
    ledgers.  A landing of more than one commit gets the ledgers in its last
    commit.

    The warm tree moves to the new commit.  Each configuration of the run
    then runs again, at the same time, in the same heavy step.  A parse
    configuration runs `guard check --build-dir BUILD` of each rule of
    COUNTED_RULES again, on the reports that it keeps, with no write.  A
    ledger that the gate folded can make such a check red, and the gate then
    rejects the commit.  A configuration of the full suite runs `loop test
    BUILD`, which runs each test with an input that changed, as the run
    after a move of HEAD does.  The other configurations get the mode of THE
    SELECTION again, with the ledgers in the change.

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
    --build-dir BUILD` there, with the lock of the rule.  It runs the write
    after THE FALLS and before the run of the test alone.  The write gives
    each row of the configuration the measured value of its item.  It gives
    no row to an item over the error threshold with no row, and it refuses
    such an item.  That error then stays, and the test stays red.  Each row
    with a value that rose gets REASON as its reason.  The ledger goes into
    the commit of THE FALLS, and the field kept of the verdict gives each
    raised row.  The write adds no row to the ledger.

THE RUN OF THE BASE
    A candidate with a focus is the commit of a run of the base (THE RUN OF
    THE BASE of assay.loop.land).  The focus gives each configuration with
    a problem in the run of the batch, and the tests that were red alone
    too in it (Focus).  The gate runs only that focus, each configuration
    in a thread of its own (focused_runs()).  It runs the red tests with
    `loop test BUILD --only FILE`, a parse configuration in its mode, and
    each other configuration in the mode of the changed inputs.  The run of
    the base writes no fall and folds no ledger.  Its problems then have the
    words of the problems of the batch.  A problem of a run of the base does
    not stop the other configurations of the focus (THE STOP).  The run of
    the base gives the result of each of them, because the landing compares
    each problem of the two runs.

THE VERDICT
    The gate has no base side: it does not compare the commit with HEAD.  A
    test that is red alone on HEAD then rejects each landing, until a commit
    corrects it.  Main must stay green.  The verdict gives the focus of each
    configuration with a problem, for THE RUN OF THE BASE.

THE LOGS
    Each step writes its log into the directory of the logs of its gate.
    That is the directory LOGS of the work directory, or a directory of the
    run in LOGS.  next_run() makes a new directory of a run, one above the
    highest number of a run.  It removes each directory of a run, other than
    the last RUNS_KEPT.  The problems of a refused landing then name logs
    that the next run does not change.  The name of a log holds its
    configuration and its step.  Two threads then do not write one log.

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
import functools
import shutil
import sys
import threading
import time
from collections.abc import Callable, Iterator, Mapping, Sequence
from dataclasses import dataclass, replace
from pathlib import Path, PurePosixPath

from assay.core import git, jobserver, paths, proc
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.guard.engines.buildsys.parse_builds import COUNTED_RULES
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
# The interval of the look of a command at the stop event, in seconds (THE STOP).  The other configurations wait for
# the build of ALWAYS with the same interval (THE THREADS).  The wait between SIGINT and SIGKILL to the group of a
# command at a stop or at its limit, in seconds.
STOP_POLL_S = 0.2
STOP_GRACE_S = 30.0


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


class Stopped(Exception):
    """A step of a run ends, because a different run of the heavy step gave a problem (THE STOP)."""


class Mode(enum.Enum):
    """How one configuration runs (THE SELECTION).  The value is the words that the gate writes before its step."""

    PARSE = "parse and check"
    FULL = "build and test"
    CHANGED = "build and test the changed inputs"
    TOUCHED = "build and run the touched tests"


@dataclass(frozen=True, slots=True)
class Run:
    """One configuration of a run of the gate: its mode, the names of the tests of the mode TOUCHED, and --build-only.

    With `is_build_only`, the run skips each test of the master guard that reads no build, and default runs it (THE
    SELECTION).
    """

    configuration: Configuration
    mode: Mode
    names: tuple[str, ...] = ()
    is_build_only: bool = False


@dataclass(frozen=True, slots=True)
class Keep:
    """One row of `--keep` of a landing: a measured rule of the policy max, and the reason of its rows (THE KEPT ROWS)."""

    rule: str
    reason: str


@dataclass(frozen=True, slots=True)
class Focus:
    """One configuration of a run of the base, and the tests that were red alone too in it (THE RUN OF THE BASE).

    With no names, the problem of the configuration was not a red test, and the run of the base runs the configuration.
    """

    configuration: str
    names: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class Candidate:
    """The commit of a landing: the commit that the composition started from, the commit, the paths and the kept rules.

    `keeps` holds the rows of `--keep` of the requests of the commit (THE KEPT ROWS).  `focus` is empty for the commit
    of a batch, and it gives the configurations of a run of the base (THE RUN OF THE BASE).
    """

    head: str
    commit: str
    changed: tuple[str, ...]
    keeps: tuple[Keep, ...] = ()
    focus: tuple[Focus, ...] = ()


@dataclass(frozen=True, slots=True)
class Verdict:
    """The result of the gate: the configurations that ran, each problem, and the wall times in seconds.

    `seconds` holds the wait for the lock.  `step_seconds` gives the time of each configuration in its heavy step after
    the wait, in the sequence of `configurations`.  `final` is the commit that the gate judged
    last, and `ledgers` holds each ledger that THE FALLS folded into it.  `final` is "" for a verdict of no commit.
    `logs` is the directory of the logs of the run (THE LOGS).  `kept` gives each row that a rule of `--keep` raised
    (THE KEPT ROWS).  `focus` gives each configuration with a problem (THE VERDICT).
    """

    configurations: tuple[str, ...]
    problems: tuple[str, ...]
    seconds: float
    step_seconds: tuple[float, ...] = ()
    final: str = ""
    ledgers: tuple[str, ...] = ()
    logs: str = ""
    kept: tuple[str, ...] = ()
    focus: tuple[Focus, ...] = ()

    @property
    def is_accepted(self) -> bool:
        """Tell if the gate accepts the commit: no configuration has a problem."""
        return not self.problems


@dataclass(frozen=True, slots=True)
class Outcome:
    """The end of the run of one configuration in a heavy step (THE THREADS, THE STOP).

    `red` holds the tests that were red alone too.  `is_stopped` tells that the stop ended the run, which then has no
    problem of its own.
    """

    name: str
    problems: tuple[str, ...]
    red: tuple[str, ...]
    seconds: float
    is_stopped: bool


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
        return tuple(Run(item, Mode.CHANGED, is_build_only=True) for item in others)
    return tuple(Run(item, Mode.TOUCHED, tuple(names)) for item in others) if names else ()


def focused_runs(focus: Sequence[Focus]) -> tuple[Run, ...]:
    """Return the runs of a run of the base, one for each configuration of its focus (THE RUN OF THE BASE).

    A parse configuration runs in its mode.  A configuration with red tests runs them in the mode TOUCHED, and each
    other configuration runs in the mode CHANGED.

    Raises:
        GateError: If the focus names a configuration that the gate does not have
    """
    runs: list[Run] = []
    for item in focus:
        configuration = BY_NAME.get(item.configuration)
        if configuration is None:
            raise GateError(
                f"the run of the base names the configuration {item.configuration}, which the gate does not have"
            )
        if configuration.is_parse:
            runs.append(Run(configuration, Mode.PARSE))
        elif item.names:
            runs.append(Run(configuration, Mode.TOUCHED, item.names))
        else:
            runs.append(Run(configuration, Mode.CHANGED))
    return tuple(runs)


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


# ── The gate ───────────────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Gate:
    """The gate of one session: the repository, the work directory, the jobs, --all-configurations, and the memory.

    `jobs` is the most jobs of a heavy step.  `read` is the reader of the available memory, which gives the jobs of the
    heavy step (THE MEMORY of assay.loop.heavy).  `run` names the directory of the logs of one run in LOGS, or it is ""
    for LOGS itself (THE LOGS).
    """

    root: Path
    work: Path
    jobs: int
    is_every: bool
    read: heavy.Reader = heavy.available_gb
    run: str = ""

    @property
    def tree(self) -> Path:
        """The warm tree."""
        return self.work / TREE

    @property
    def logs(self) -> Path:
        """The directory of the logs of the run."""
        return self.work / LOGS / self.run if self.run else self.work / LOGS

    def master(self, name: str) -> list[str]:
        """Return the words that run a master of the warm tree."""
        return [sys.executable, str(self.tree / "utils" / "bin" / name)]

    def _parse_presets(self, environment: Mapping[str, str]) -> list[str]:
        """Return each parse preset that `loop syntax --changed` of the warm tree names.

        Raises:
            GateError: If the command cannot run or gives an error
        """
        command = [*self.master("loop"), "syntax", "--changed", "--root", str(self.tree)]
        try:
            completed = proc.run(proc.Command(command, timeout_s=proc.TOOL_S, cwd=self.tree, env=dict(environment)))
        except proc.ToolFailed as problem:
            raise GateError(f"`loop syntax --changed` cannot run: {problem}") from None
        if completed.exit != ExitCode.OK:
            raise GateError(f"`loop syntax --changed` gives an error: {completed.err.strip()}")
        return completed.out.split()

    def loop_words(self, run: Run, is_plain: bool) -> list[str]:
        """Return the words of the mode of a run after `loop test BUILD` (THE SELECTION).

        A run of the full suite with `is_plain` runs the tests of the changed inputs.  The run of the touched tests
        writes the file of --only into the directory of the logs.  A run with `is_build_only` gets --build-only.
        """
        if run.mode is Mode.TOUCHED:
            self.logs.mkdir(parents=True, exist_ok=True)
            only = self.logs / f"{run.configuration.name}.only.txt"
            only.write_text("".join(f"{name}\n" for name in run.names), encoding="utf-8")
            return ["--only", str(only)]
        words = ["--all"] if run.mode is Mode.FULL and not is_plain else []
        return [*words, "--build-only"] if run.is_build_only else words

    def touched_names(self, change: guard_inputs.Change) -> tuple[str, ...]:
        """Return each touched test of the build of ALWAYS, sorted: it reads the build, and the change touches its inputs.

        The run of ALWAYS configures its build with the change first (THE THREADS).

        Raises:
            GateError: If the build of ALWAYS or its tests cannot be read
        """
        build = build_directory(self.tree, BY_NAME[ALWAYS])
        try:
            tests = affected.build_tests(build)
        except affected.RunnerError as problem:
            raise GateError(f"the tests of {build} that read the build do not read: {problem}") from None
        return tuple(sorted(name for name, found in tests.items() if found.is_touched(change)))

    def changes_each(self, candidate: Candidate, ledgers: Sequence[str] = ()) -> bool:
        """Tell if the change of a candidate and `ledgers` can change each build (changes_each_build()).

        Raises:
            git.GitError: If git cannot read the registry at the commit that the change starts from
        """
        changed = (*candidate.changed, *ledgers)
        return changes_each_build(self.tree, guard_inputs.read_change(self.tree, candidate.head, changed), changed)

    def other_plan(
        self, candidate: Candidate, others: Sequence[Configuration], ledgers: Sequence[str] = (), is_each: bool = False
    ) -> tuple[Run, ...]:
        """Return the runs of the configurations of objects with no full suite, for the change and `ledgers`.

        `is_each` tells that the change can change each build (changes_each()).  Else, the plan reads the touched tests
        of the build of ALWAYS.

        Raises:
            GateError: If the build of ALWAYS or its tests cannot be read
            git.GitError: If git cannot read the registry at the commit that the change starts from
        """
        if not others:
            return ()
        if is_each:
            return other_runs(others, True, ())
        change = guard_inputs.read_change(self.tree, candidate.head, (*candidate.changed, *ledgers))
        names = self.touched_names(change)
        shown = " ".join(names) if names else "none"
        check_report.write_line(f"loop land: the touched tests of the other configurations of objects: {shown}")
        return other_runs(others, False, names)

    def check(self, candidate: Candidate, is_retry: bool) -> Verdict:
        """Give the warm tree the commit, select the configurations, and run them in one heavy step (THE THREADS).

        A candidate with a focus runs only its focus (THE RUN OF THE BASE).

        Raises:
            GateError: If the warm tree cannot hold the commit, or a step cannot start
            git.GitError: If a git command fails
        """
        started = time.monotonic()
        move_tree(self.root, self.tree, candidate.commit)
        based = proc.environment({git.CHANGE_BASE_VARIABLE: candidate.head})
        environment = {name: value for name, value in based.items() if name != git.CHANGE_BASE_VARIABLE}
        is_stacked = candidate.commit != candidate.head and git.parent(self.tree, candidate.commit) != candidate.head
        others: tuple[Configuration, ...] = ()
        counted: dict[str, tuple[str, ...]] = {}
        is_each = True
        if candidate.focus:
            plan = focused_runs(candidate.focus)
            shown = " ".join(run.configuration.name + "".join(f" {name}" for name in run.names) for run in plan)
            check_report.write_line(
                f"loop land: the run of the base at {candidate.commit[:9]} runs {shown}, logs in {self.logs}"
            )
        else:
            presets = self._parse_presets(based)
            uncompiled = [path for path in candidate.changed if not is_compiled_change(path)]
            if touches_parse_rules(self.tree, guard_inputs.read_change(self.tree, candidate.head, uncompiled)):
                presets += [item.name for item in CONFIGURATIONS if item.is_parse]
            counted = counted_ledgers(self.tree)
            for name in sorted({item for listed in counted.values() for item in listed}):
                if name not in BY_LEDGER_NAME:
                    check_report.write_line(f"loop land: the host does not build the configuration {name} of a ledger")
            selected = select(candidate.changed, presets, self.is_every)
            plan = tuple(
                Run(item, Mode.PARSE) if item.is_parse else Run(item, Mode.FULL, is_build_only=item.name != ALWAYS)
                for item in selected
            )
            others = tuple(item for item in CONFIGURATIONS if not item.is_parse and item not in selected)
            is_each = self.changes_each(candidate) if others else True
            shown = " ".join(item.name for item in selected)
            rest = f", and the tests of the change in {' '.join(item.name for item in others)}" if others else ""
            check_report.write_line(
                f"loop land: the commit {candidate.commit[:9]} runs in {shown}{rest} at the same time, logs in "
                f"{self.logs}"
            )
        try:
            with (
                heavy.heavy_step(self.root, self.jobs, read=self.read, is_gate_step=True) as jobs,
                jobserver.serving(jobs) as server,
                jobserver.connected(server.path) as client,
            ):
                flags = {jobserver.VARIABLE: server.makeflags}
                step = Step(
                    self, candidate, {**environment, **flags}, {**based, **flags}, client, jobs, counted, is_stacked
                )
                if is_each:
                    step.always_ready.set()

                def first_plan() -> tuple[Run, ...]:
                    """Return the runs of the other configurations of the first round."""
                    return self.other_plan(candidate, others, (), is_each)

                outcomes = step.run_all(plan, first_plan if others else None, False, is_retry)
                step.require_problem(outcomes)
                final: str = candidate.commit
                ledgers: tuple[str, ...] = ()
                if not candidate.focus and not any(item.problems for item in outcomes):
                    final, ledgers = step.fold()
                if ledgers:
                    check_report.write_line(
                        f"loop land: the writes of the gate changed {' '.join(ledgers)}, folded into the commit "
                        f"{final[:9]}, and each configuration of the run runs again"
                    )
                    is_each_again = self.changes_each(candidate, ledgers) if others else True

                    def again_plan() -> tuple[Run, ...]:
                        """Return the runs of the other configurations of the round after the fold."""
                        return self.other_plan(candidate, others, ledgers, is_each_again)

                    outcomes += step.run_all(plan, again_plan if others else None, True, is_retry)
                    step.require_problem(outcomes)
                kept = tuple(step.kept)
        except jobserver.JobserverError as problem:
            raise GateError(str(problem)) from None
        ended = [item for item in outcomes if not item.is_stopped]
        return Verdict(
            tuple(item.name for item in ended),
            tuple(problem for item in outcomes for problem in item.problems),
            time.monotonic() - started,
            tuple(item.seconds for item in ended),
            final,
            ledgers,
            str(self.logs),
            kept,
            tuple(Focus(item.name, item.red) for item in outcomes if item.problems),
        )


# ── The heavy step ─────────────────────────────────────────────────────────


class Step:
    """The work of one heavy step of a check: its commands, its threads, its stop event and the locks of its rules.

    `environment` is the environment of each command, with MAKEFLAGS of the jobserver (THE JOBSERVER), and `based` adds
    the base of the change (THE RUN OF A CONFIGURATION).  `jobs` is the job count of the heavy step and of its
    jobserver.  `always_ready` tells that the build of ALWAYS holds the configure of the change (THE THREADS).  `kept`
    receives each row that a rule of `--keep` raised (THE KEPT ROWS).
    """

    __slots__ = (
        "_locks",
        "_locks_guard",
        "_said",
        "always_ready",
        "based",
        "candidate",
        "client",
        "counted",
        "environment",
        "gate",
        "is_stacked",
        "jobs",
        "kept",
        "stop",
    )

    def __init__(
        self,
        gate: Gate,
        candidate: Candidate,
        environment: Mapping[str, str],
        based: Mapping[str, str],
        client: jobserver.Client,
        jobs: int,
        counted: Mapping[str, Sequence[str]],
        is_stacked: bool,
    ) -> None:
        """Keep the gate, the candidate, the environments, the jobserver client, the jobs and the counted rules."""
        self.gate = gate
        self.candidate = candidate
        self.environment = dict(environment)
        self.based = dict(based)
        self.client = client
        self.jobs = jobs
        self.counted = counted
        self.is_stacked = is_stacked
        self.stop = threading.Event()
        self.always_ready = threading.Event()
        self.kept: list[str] = []
        self._locks: dict[str, threading.Lock] = {}
        self._locks_guard = threading.Lock()
        self._said = threading.Lock()

    def say(self, text: str) -> None:
        """Write one line of the gate, with no line of a different thread in it."""
        with self._said:
            check_report.write_line(text)

    def rule_lock(self, rule: str) -> threading.Lock:
        """Return the lock of the writes of a rule in the heavy step (THE LOCKS OF THE LEDGERS)."""
        with self._locks_guard:
            return self._locks.setdefault(rule, threading.Lock())

    def concurrently[Item, Result](self, items: Sequence[Item], work: Callable[[Item], Result]) -> list[Result]:
        """Run the work of each item in a thread of its own, and return the results in the sequence of the items.

        An exception of a thread sets the stop event immediately.  After each thread ended, the first exception that is
        not Stopped goes to the caller, and Stopped goes only when each exception is Stopped.  A Stopped of a thread that
        a different error stopped then does not hide that error (THE STOP).  The pool has at most proc.DEFAULT_JOBS
        threads.  Each caller gives fewer items.  Then each item gets a thread of its own, and no item waits for a
        different item.  Complexity: one thread for each item.
        """
        from concurrent.futures import ThreadPoolExecutor, wait

        def guarded(item: Item) -> Result:
            """Do the work of one item, and set the stop event on an exception."""
            try:
                return work(item)
            except BaseException:
                self.stop.set()
                raise

        if not items:
            return []
        with ThreadPoolExecutor(max_workers=min(proc.DEFAULT_JOBS, len(items))) as pool:
            futures = [pool.submit(guarded, item) for item in items]
            try:
                wait(futures)
            except BaseException:
                self.stop.set()
                raise
        errors = [error for future in futures if (error := future.exception()) is not None]
        for error in errors:
            if not isinstance(error, Stopped):
                raise error
        if errors:
            raise errors[0]
        return [future.result() for future in futures]

    def run_logged(self, command: list[str], log: str, environment: Mapping[str, str], timeout_s: float) -> int:
        """Run one command in the warm tree with its two outputs in a log, in one slot of the jobserver, and give its exit.

        A command that runs longer than its limit gives the exit of its end.  The command looks at the stop event each
        STOP_POLL_S seconds (THE STOP).

        Raises:
            Stopped: If the stop event is set before the command starts, or while it runs
            GateError: If the program cannot start
        """
        logs = self.gate.logs
        logs.mkdir(parents=True, exist_ok=True)
        command_line = proc.Command(
            command,
            timeout_s=timeout_s,
            cwd=self.gate.tree,
            env=dict(environment),
            stdout=logs / log,
            stderr=proc.Output.MERGE,
            grace_s=STOP_GRACE_S,
            stop=proc.Stop.INTERRUPT,
        )
        with self.client.held(self.stop) as is_held:
            if not is_held:
                raise Stopped
            try:
                running = proc.start(command_line)
            except proc.ToolFailed as problem:
                raise GateError(f"{command[0]} cannot run: {problem}") from None
            try:
                completed = running.poll(STOP_POLL_S)
                while completed is None:
                    if self.stop.is_set():
                        running.stop()
                        raise Stopped
                    completed = running.poll(STOP_POLL_S)
            except proc.TimedOut as problem:
                return problem.completed.exit if problem.completed is not None else ExitCode.TOOL_FAILED
            return completed.exit

    def configure(self, configuration: Configuration) -> str | None:
        """Configure the build directory of a configuration when it has no graph of Ninja, and give a problem."""
        build = build_directory(self.gate.tree, configuration)
        if ninja_reader.graph_file(build).is_file():
            return None
        log = f"{configuration.name}.configure.log"
        if self.run_logged(configure_words(self.gate.tree, configuration), log, self.environment, CONFIGURE_S) != 0:
            return f"{configuration.name}: the configure gives an error, see {self.gate.logs / log}"
        return None

    def regenerate(self, configuration: Configuration) -> str | None:
        """Build the target build.ninja of the build of a configuration, and give a problem (THE THREADS).

        The build configures the build directory again when the change touches its configure.  It gets no -j, because
        ninja obeys the jobserver only with no -j (THE JOBSERVER).

        Raises:
            GateError: If the CMake cache of the build names no cmake
        """
        build = build_directory(self.gate.tree, configuration)
        try:
            cmake = cmake_pin.configured_program(build, "cmake")
        except cmake_pin.PinError as problem:
            raise GateError(str(problem)) from None
        log = f"{configuration.name}.regenerate.log"
        command = [cmake, "--build", str(build), "--target", ninja_reader.GRAPH_NAME]
        if self.run_logged(command, log, self.environment, CONFIGURE_S) != 0:
            return f"{configuration.name}: the configure gives an error, see {self.gate.logs / log}"
        return None

    def checked(self, configuration: Configuration, rules: Sequence[str], environment: Mapping[str, str]) -> list[str]:
        """Run `guard check --build-dir` of each rule on a build at the same time, and give a problem for each exit other
        than 0 or 77."""
        build = build_directory(self.gate.tree, configuration)

        def check_one(rule: str) -> str | None:
            """Run the check of one rule, and give its problem or None."""
            log = f"{configuration.name}.check-{rule}.log"
            command = [*self.gate.master("guard"), "check", "--rule", rule, "--build-dir", str(build)]
            status = self.run_logged(command, log, environment, proc.TOOL_S)
            if status in (ExitCode.OK, ExitCode.SKIPPED):
                return None
            return f"{configuration.name}: the rule {rule} gives findings, see {self.gate.logs / log}"

        return [problem for problem in self.concurrently(list(rules), check_one) if problem is not None]

    def write_falls(self, configuration: Configuration, counted: Mapping[str, Sequence[str]]) -> bool:
        """Run `guard write` of each counted rule with a ledger that holds a configuration, at the same time (THE FALLS).

        Each write gets the environment with the base of the change, and it holds the lock of its rule (THE LOCKS OF THE
        LEDGERS).  A write that refuses a count above its row exits 1, and the check of the rule keeps that count as
        its error.  Return True when a rule holds the configuration.
        """
        build = build_directory(self.gate.tree, configuration)
        rules = holding(counted, configuration)

        def write_one(rule: str) -> None:
            """Write the ledger of one rule with the lock of the rule."""
            log = f"{configuration.name}.write-{rule}.log"
            command = [*self.gate.master("guard"), "write", "--rule", rule, "--build-dir", str(build)]
            with self.rule_lock(rule):
                status = self.run_logged(command, log, self.based, proc.TOOL_S)
            self.say(f"loop land: {configuration.name}: the write of {rule} exits {status}")

        self.concurrently(rules, write_one)
        return bool(rules)

    def checked_falls(
        self, configuration: Configuration, rules: Sequence[str], counted: Mapping[str, Sequence[str]]
    ) -> list[str]:
        """Run the checks of some rules on a build, and after a failure THE FALLS and the checks again."""
        problems = self.checked(configuration, rules, self.environment)
        if problems and self.write_falls(configuration, counted):
            problems = self.checked(configuration, rules, self.environment)
        return problems

    def fold(self) -> tuple[str, tuple[str, ...]]:
        """Fold each ledger that THE FALLS changed in the warm tree into the commit, and give the commit and the ledgers.

        The warm tree moves to the new commit.  With no changed ledger, the result is the commit and no ledger.

        Raises:
            GateError: If the warm tree is in the work tree of the repository
            git.GitError: If a git command fails
        """
        tree = self.gate.tree
        ledgers = tuple(sorted(git.work_changes(tree, "HEAD", [LEDGER_PREFIX])))
        if not ledgers:
            return self.candidate.commit, ()
        files = {name: (tree / name).read_bytes() if (tree / name).is_file() else None for name in ledgers}
        final = git.replaced_commit(self.gate.root, self.candidate.commit, files)
        move_tree(self.gate.root, tree, final)
        return final, ledgers

    def red_alone(self, configuration: Configuration, names: Sequence[str]) -> list[str]:
        """Run each test alone, one at a time, and return the names of the tests that are red alone too.

        Raises:
            GateError: If the CMake cache of the build names no ctest
        """
        if not names:
            return []
        build = build_directory(self.gate.tree, configuration)
        logs = self.gate.logs
        selection = logs / f"{configuration.name}.alone.txt"
        report = logs / f"{configuration.name}.alone.junit.xml"
        try:
            ctest = cmake_pin.configured_program(build, "ctest")
        except cmake_pin.PinError as problem:
            raise GateError(str(problem)) from None
        logs.mkdir(parents=True, exist_ok=True)
        red: list[str] = []
        for name in names:
            selection.write_text(f"{name}\n", encoding="utf-8")
            report.unlink(missing_ok=True)
            command = [ctest, "--test-dir", str(build), "--tests-from-file", str(selection)]
            command += ["--timeout", str(int(ALONE_S)), "--output-on-failure", "--output-junit", str(report)]
            self.run_logged(command, f"{configuration.name}.alone.{name}.log", self.environment, ALONE_S)
            try:
                status = ctest_reader.results(report).get(name, ctest_reader.TestStatus.NOTRUN)
            except ctest_reader.CtestError:
                status = ctest_reader.TestStatus.NOTRUN
            if status in RED:
                red.append(name)
        return red

    def run_parse(self, configuration: Configuration, counted: Mapping[str, Sequence[str]]) -> list[str]:
        """Do steps 1 and 2 of THE RUN OF A CONFIGURATION for a parse configuration, and give each problem."""
        problem = self.configure(configuration)
        if problem is not None:
            return [problem]
        build = build_directory(self.gate.tree, configuration)
        log = f"{configuration.name}.syntax.log"
        command = [*self.gate.master("loop"), "syntax", str(build), "-j", str(self.jobs)]
        if self.run_logged(command, log, self.environment, TEST_RUN_S) != 0:
            return [f"{configuration.name}: a unit does not parse, see {self.gate.logs / log}"]
        return self.checked_falls(configuration, COUNTED_RULES, counted)

    def write_keeps(self, configuration: Configuration, keeps: Sequence[Keep], red: Sequence[str]) -> list[str]:
        """Write the measured value of each row of each rule of `--keep` with a red test, and return the raised rows.

        A raised row gets the reason of its `--keep` (THE KEPT ROWS).  The write holds the lock of its rule (THE LOCKS OF
        THE LEDGERS).  A write that refuses an item exits 1, and the test of the rule keeps that item as its error.

        Raises:
            GateError: If the registry or a ledger of the warm tree does not read
        """
        tree = self.gate.tree
        build = build_directory(tree, configuration)
        raised: list[str] = []
        for keep in keeps:
            if guard_test_name(keep.rule) not in red:
                continue
            with self.rule_lock(keep.rule):
                raised += self.write_keep(configuration, build, keep)
        return raised

    def write_keep(self, configuration: Configuration, build: Path, keep: Keep) -> list[str]:
        """Write the measured value of each row of one rule of `--keep`, and return its raised rows (THE KEPT ROWS).

        The caller holds the lock of the rule.

        Raises:
            GateError: If the registry or the ledger of the rule does not read
        """
        tree = self.gate.tree
        try:
            rule = rule_registry.load(tree / rule_registry.REGISTRY_PATH).rules[keep.rule]
            before = rule_ledger.read(rule, tree)
        except (OSError, ValueError, KeyError) as problem:
            raise GateError(f"the ledger of the rule {keep.rule} of --keep does not read: {problem}") from None
        log = f"{configuration.name}.keep-{keep.rule}.log"
        command = [*self.gate.master("guard"), "write", "--rule", keep.rule, "--build-dir", str(build)]
        status = self.run_logged(command, log, self.environment, proc.TOOL_S)
        self.say(f"loop land: {configuration.name}: the write of --keep {keep.rule} exits {status}")
        if status != ExitCode.OK:
            return []
        try:
            after = rule_ledger.read(rule, tree)
        except (OSError, ValueError) as problem:
            raise GateError(f"the ledger of the rule {keep.rule} of --keep does not read: {problem}") from None
        raised: list[str] = []
        base = dict(after.base)
        for key, entry in after.base.items():
            old = before.base.get(key)
            if old is None or not isinstance(entry.value, int | float) or not isinstance(old.value, int | float):
                continue
            if entry.value > old.value:
                base[key] = replace(entry, reason=keep.reason)
                values = f"from {rule_ledger.value_text(old.value)} to {rule_ledger.value_text(entry.value)}"
                raised.append(f"{keep.rule}: {key.path} in {key.kind} {values}")
        rule_ledger.store(replace(after, base=base), tree)
        return raised

    def run_objects(
        self, configuration: Configuration, words: Sequence[str], is_again: bool
    ) -> tuple[list[str], tuple[str, ...]]:
        """Do steps 1 and 3 of THE RUN OF A CONFIGURATION for a configuration of objects, and give each problem.

        `words` are the words of the mode after `loop test BUILD` (Gate.loop_words()).  A run again after the fold, and
        a run with no counted rule, give a red test no write (THE FALLS).  A red test of a rule of `--keep` gets the
        write of THE KEPT ROWS.  The run of ALWAYS first builds the target build.ninja when the other configurations
        wait for it (THE THREADS).  The second result holds the tests that are red alone too.
        """
        counted: Mapping[str, Sequence[str]] = {} if is_again else self.counted
        keeps = () if is_again else self.candidate.keeps
        problem = self.configure(configuration)
        if problem is not None:
            return [problem], ()
        if configuration.name == ALWAYS and not self.always_ready.is_set():
            problem = self.regenerate(configuration)
            self.always_ready.set()
            if problem is not None:
                return [problem], ()
        build = build_directory(self.gate.tree, configuration)
        report = build / affected.STATE_DIRECTORY / "junit.xml"
        report.unlink(missing_ok=True)
        logs = self.gate.logs
        log = f"{configuration.name}.test.log"
        command = [*self.gate.master("loop"), "test", str(build), "-j", str(self.jobs), *words]
        status = self.run_logged(command, log, self.environment, TEST_RUN_S)
        statuses: dict[str, ctest_reader.TestStatus] = {}
        if report.is_file():
            try:
                statuses = ctest_reader.results(report)
            except ctest_reader.CtestError as problem_text:
                return [f"{configuration.name}: the test report does not read: {problem_text}"], ()
        elif status != 0:
            return [f"{configuration.name}: the build or the test run gives an error, see {logs / log}"], ()
        red = sorted(name for name, test_status in statuses.items() if test_status in RED)
        if status != 0 and not red:
            return [f"{configuration.name}: `loop test` exits {status} with no red test, see {logs / log}"], ()
        skipped = sorted(name for name, test_status in statuses.items() if test_status is SKIPPED)
        if skipped:
            self.say(f"loop land: {configuration.name}: {len(skipped)} skipped: {' '.join(skipped)}")
        if red:
            self.write_falls(configuration, counted)
            raised = self.write_keeps(configuration, keeps, red)
            with self._locks_guard:
                self.kept += raised
            self.say(f"loop land: {configuration.name}: {len(red)} red, each runs again alone")
        still = self.red_alone(configuration, red)
        if still:
            return [f"{configuration.name}: red alone too: {' '.join(still)}, see {logs / log}"], tuple(still)
        warnings_log = f"{configuration.name}.warnings.log"
        warnings = [*self.gate.master("guard"), "warnings", str(build)]
        if self.run_logged(warnings, warnings_log, self.environment, proc.TOOL_S):
            return [f"{configuration.name}: `guard warnings` gives an error, see {logs / warnings_log}"], ()
        return [], ()

    def run_of(self, run: Run, is_again: bool, is_retry: bool) -> tuple[list[str], tuple[str, ...]]:
        """Run one configuration, or its tests again after THE FALLS, and give each problem and the tests red alone too."""
        configuration = run.configuration
        if configuration.is_parse and is_again:
            return self.checked(configuration, COUNTED_RULES, self.environment), ()
        if configuration.is_parse:
            return self.run_parse(configuration, self.counted), ()
        words = self.gate.loop_words(run, is_retry or is_again)
        problems, red = self.run_objects(configuration, words, is_again)
        if self.is_stacked and not problems:
            problems = self.checked(configuration, (STACKED_RULE,), self.based)
        return problems, red

    def outcome_of(self, run: Run, is_again: bool, is_retry: bool) -> Outcome:
        """Run one configuration in the thread of the caller, and give its outcome (THE THREADS, THE STOP).

        A problem sets the stop event, other than in a run of the base (THE RUN OF THE BASE).  An error of the run sets
        it too, and goes to the caller.
        """
        name = run.configuration.name
        self.say(f"loop land: {name}: {'test again' if is_again else run.mode.value}")
        began = time.monotonic()
        try:
            problems, red = self.run_of(run, is_again, is_retry)
        except Stopped:
            seconds = round(time.monotonic() - began, 1)
            self.say(f"loop land: {name}: the stop ends the run after {seconds:.0f} s")
            return Outcome(name, (), (), seconds, True)
        except BaseException:
            self.stop.set()
            raise
        finally:
            if name == ALWAYS:
                self.always_ready.set()
        seconds = round(time.monotonic() - began, 1)
        self.say(f"loop land: {name}: {seconds:.0f} s in the heavy step")
        if problems and not self.candidate.focus:
            self.stop.set()
        return Outcome(name, tuple(problems), red, seconds, False)

    def require_problem(self, outcomes: Sequence[Outcome]) -> None:
        """Refuse a heavy step that the stop ended with no problem of a configuration (THE STOP).

        Only a problem or an error sets the stop event, and an error goes to the caller.  A set event with no problem
        then tells of an error that did not get to the caller.  The landing then refuses the commit, and it does not
        accept it.

        Raises:
            GateError: If the stop event is set and no outcome has a problem
        """
        if self.stop.is_set() and not any(item.problems for item in outcomes):
            raise GateError("the heavy step of the gate stopped with no problem of a configuration")

    def run_all(
        self, runs: Sequence[Run], others: Callable[[], Sequence[Run]] | None, is_again: bool, is_retry: bool
    ) -> list[Outcome]:
        """Run each configuration at the same time, and the runs that `others` gives, and give each outcome (THE THREADS).

        `others` gives the runs of the other configurations of objects.  The function calls it when the build of ALWAYS
        holds the configure of the change.  The outcomes are in the sequence of `runs`, and then of the runs of
        `others`.
        """
        later: list[Outcome] = []

        def run_others() -> None:
            """Wait for the build of ALWAYS, then run the other configurations at the same time."""
            while not self.always_ready.wait(STOP_POLL_S):
                if self.stop.is_set():
                    return
            if self.stop.is_set() or others is None:
                return
            later.extend(self.concurrently(others(), lambda run: self.outcome_of(run, is_again, is_retry)))

        tasks: list[Callable[[], Outcome | None]] = [
            functools.partial(self.outcome_of, run, is_again, is_retry) for run in runs
        ]
        if others is not None:
            tasks.append(run_others)
        results = self.concurrently(tasks, lambda task: task())
        return [*(item for item in results if item is not None), *later]


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
