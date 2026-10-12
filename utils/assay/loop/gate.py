"""gate — build and test the commit of a landing in a warm tree, in each configuration that the change selects.

THE CONFIGURATIONS
    CONFIGURATIONS gives each configuration of the gate.  A configuration has
    a name, a configure preset and a build directory in the warm tree.  It
    also has the cache variables that its configure adds, and its name in a
    ledger (assay.core.configuration).  A parse configuration is a parse
    build of PARSE_PRESETS of assay.loop.syntax.  The ledger names are the
    names of an x86_64 host, the build host of the tree.

THE COUNTED RULES
    A counted rule is a rule of the engine plugin or elf with the policy
    count and a ledger of configurations (counted_ledgers()).  Each one reads
    the objects of the target all of a build, or the reports of a parse
    build.  A write in the build of a configuration changes only the counts
    of that configuration (THE WRITE of assay.core.ledger).  The writes of the
    configurations then give one ledger in each sequence.  `guard write
    --all-configurations` writes each configuration of a counted ledger in
    its build directory (assay.guard.write_configurations), and the gate
    writes the falls of each configuration that it builds (THE FALLS).

THE SELECTION
    select() gives the configurations of a change, in the sequence of
    CONFIGURATIONS.  Each such configuration runs the suite:
      * debug for each change
      * release and tsan for a change of a C++ file or of a build file.
        ThreadSanitizer gives incorrect errors of -Wmaybe-uninitialized that
        the other builds do not give, also for a change of a test
      * tsan and ubsan-strict for a change of include/, src/ or cmake/, or of
        a build file at the top of the tree
      * bench-fuzz for a change of bench/ or test/fuzz/
      * each parse configuration that `loop syntax --changed` of the warm
        tree names, for a change of an arm
      * each configuration that the ledger of a changed rule names.  A rule
        changes with its ledger, its plants, its row of the registry or its
        module (changed_rules())
      * each configuration for a change of the quarantine plugin or of the
        rule table, and for --all-configurations.
    A build file is a file CMakeLists.txt, a file *.cmake or
    CMakePresets.json.  A ledger can name a configuration that the host
    cannot build, such as an aarch64 configuration of objects.  The gate
    then writes a note and does not build it.

    ledger_checks() gives each other configuration of a counted ledger, for
    a change of a C++ file or of a build file.  Such a configuration runs the
    ledger check, and no suite (THE RUN OF A CONFIGURATION, step 4).  The
    rows of each configuration then hold its counts at each commit of main.
    Each configuration compiles the files of include/ and src/.  The bench
    targets also compile the arms of CRUCIBLE_BENCH in them.  An arm in
    include/crucible/BackgroundThread.h is an example.  An arm that only one
    configuration compiles then gets its verdict in the landing of the
    change.  This applies to a count above its row and to a count below it.  A change
    that no unit of a configuration reads makes its build do nothing.  A
    parse build then keeps the report of each unit (THE KEPT REPORTS of
    assay.loop.syntax).

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
    names no commit of them.  A landing with a row of the kind `grow` has
    two commits (assay.loop.land).  The rule quarantine-lines of the suite
    then judges only the second commit.  For such a landing, the gate runs
    `guard check --rule quarantine-lines --build-dir BUILD` again, with the
    variable set to the commit that the composition started from.  `loop
    syntax --changed` gets the variable too.  Each configuration runs as
    one heavy step (assay.loop.heavy), with the lock of the repository of
    the landing.  The parse configurations run first.  Then the tests
    guard_parse_* of the debug build read current reports.
      1. A build directory with no graph of Ninja gets its configure:
         `cmake --preset PRESET -B BUILD` with its cache variables.
      2. A parse configuration: `loop syntax BUILD`, then `guard check
         --build-dir BUILD` of each rule of COUNTED_RULES of
         assay.guard.engines.buildsys.parse_builds.  A check that fails
         gets THE FALLS, and then each check runs again.
      3. A configuration of objects: `loop test BUILD --all`, which builds
         the target all and runs each test (assay.loop.affected).  A retry
         after a move of HEAD runs `loop test BUILD`, which runs only the
         tests with an input that changed.  A red test gets THE FALLS first.
         Then each test that is red runs one more time, alone.  A test that
         is red alone too rejects the commit.
         An exit other than 0 with no red test rejects the commit too.  Then
         `guard warnings BUILD`.  The suite holds the ledger checks of the build
         kind, such as the tests guard_plugin (with the rule
         quarantine-lines), guard_artifact_symbols and guard_library_bytes.
         A second run of these checks gives no new verdict.  A test that gives
         a skip does not reject the commit, and the gate writes its name.
         A red test that runs alone writes its output to its log.
      4. A configuration of the ledger check: `cmake --build BUILD`, then
         `guard check --build-dir BUILD` of each counted rule with a ledger
         that holds the configuration.  A check that fails gets THE FALLS,
         and then each check runs again.
    Each step runs the masters of the warm tree.  Then the tests of the
    commit judge the commit.  The step writes its two outputs to a log in
    the directory LOGS.  The first configuration with a problem stops the
    gate.

THE FALLS
    A count that falls makes the check of its counted rule fail with a count
    below its row.  The gate then runs `guard write --rule RULE --build-dir
    BUILD` of each counted rule with a ledger that holds the configuration,
    in the mode lower.  That write refuses each count above its row and each
    new row.  The check then keeps such a count as its error.  After the
    last configuration, the gate folds each ledger that a write changed into
    a new commit (replaced_commit() of assay.core.git).  The new commit has
    the parents and the message of the commit, and its tree holds these
    ledgers.  A landing of two commits gets the ledgers in its second commit.  The row
    of THE GROWTH of assay.loop.land then names the first commit as before.
    The warm tree moves to the new commit.  Each configuration of the suite
    then runs `loop test BUILD`, which runs each test with an input that
    changed, as the run after a move of HEAD does.  The verdict gives the new
    commit and the ledgers, and the landing moves the branch to that commit.
    A change that lowers counts then lands with no manual write of a ledger.

THE VERDICT
    The gate has no base side: it does not compare the commit with HEAD.  A
    test that is red alone on HEAD then rejects each landing, until a commit
    corrects it.  Main must stay green.

THE LOCK OF THE TREE
    One landing at a time uses the warm tree.  session() holds an exclusive
    lock of flock(2) on the file LOCK of the work directory, and it waits
    for the lock.

`guard selftest tests.loop.test_gate` does a test of the module on a planted repository.
"""

import contextlib
import fcntl
import sys
import time
from collections.abc import Iterator, Mapping, Sequence
from dataclasses import dataclass, replace
from pathlib import Path, PurePosixPath

from assay.core import git, proc
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.guard.engines.buildsys.parse_builds import COUNTED_RULES
from assay.guard.engines.meta import runner as meta_runner
from assay.loop import affected, heavy
from assay.readers import ctest as ctest_reader
from assay.readers import ninja as ninja_reader
from assay.setup import pins as cmake_pin

# The directory of the warm tree, the lock of the tree and the directory of the logs, in the work directory.
TREE = "tree"
LOCK = "tree.lock"
LOGS = "logs"
# The C++ suffixes of a change that selects the release build and ThreadSanitizer (THE SELECTION).
CPP_SUFFIXES = (".h", ".hh", ".hpp", ".hxx", ".c", ".cc", ".cpp", ".cxx", ".inc", ".ipp", ".tpp")
BUILD_FILE_NAMES = ("CMakeLists.txt", "CMakePresets.json")
BUILD_FILE_SUFFIX = ".cmake"
SANITIZED_PREFIXES = ("include/", "src/", "cmake/")
BENCH_PREFIXES = ("bench/", "test/fuzz/")
EVERY_PREFIXES = ("utils/tools/quarantine/", "utils/rule-table.txt")
LEDGER_PREFIX = "utils/ledgers/"
PLANT_PREFIX = "utils/plants/"
ENGINE_PREFIX = "utils/assay/guard/engines/"
# The rule that judges the full change of a landing with two commits again (THE RUN OF A CONFIGURATION).
STACKED_RULE = "quarantine-lines"
# The engines of a counted rule (THE COUNTED RULES).
COUNTED_ENGINES = frozenset({rule_registry.Engine.PLUGIN, rule_registry.Engine.ELF})
# The statuses of a test that count as red, and the status of a skip.
RED = frozenset({ctest_reader.TestStatus.FAIL, ctest_reader.TestStatus.NOTRUN})
SKIPPED = ctest_reader.TestStatus.SKIPPED
# The limit of one call of `loop test`, of one configure, and of the run of one test alone, in seconds.
TEST_RUN_S = 2 * proc.BUILD_S
CONFIGURE_S = proc.TOOL_S
ALONE_S = 1500.0


@dataclass(frozen=True, slots=True)
class Configuration:
    """One configuration of the gate (THE CONFIGURATIONS)."""

    name: str
    preset: str
    build: str
    cache: tuple[str, ...]
    ledger_name: str
    is_parse: bool


CONFIGURATIONS = (
    Configuration(
        "syntax-x86-64-v3", "syntax-x86-64-v3", "build-syntax-x86-64-v3", (), "x86_64-debug-asan-syntax", True
    ),
    Configuration("syntax-aarch64", "syntax-aarch64", "build-syntax-aarch64", (), "aarch64-debug-asan-syntax", True),
    Configuration("debug", "default", "build", (), "x86_64-debug-asan", False),
    Configuration("release", "release", "build-release", (), "x86_64-release", False),
    Configuration("tsan", "tsan", "build-tsan", (), "x86_64-debug-tsan", False),
    Configuration("ubsan-strict", "ubsan-strict", "build-ubsan-strict", (), "x86_64-debug-ubsan-strict", False),
    Configuration(
        "bench-fuzz",
        "default",
        "build-bench-fuzz",
        ("-DCRUCIBLE_BENCH=ON", "-DCRUCIBLE_FUZZ=ON"),
        "x86_64-debug-asan-bench-fuzz",
        False,
    ),
)
BY_NAME = {configuration.name: configuration for configuration in CONFIGURATIONS}
BY_LEDGER_NAME = {configuration.ledger_name: configuration for configuration in CONFIGURATIONS}


class GateError(Exception):
    """The warm tree cannot hold the commit, or a step of the gate cannot start."""


@dataclass(frozen=True, slots=True)
class Candidate:
    """The commit of a landing: the commit that the composition started from, the commit, and the changed paths."""

    head: str
    commit: str
    changed: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class Verdict:
    """The result of the gate: the configurations that ran, each problem, and the wall times in seconds.

    `seconds` holds the waits for the lock and for the memory.  `step_seconds` gives the time of each configuration in
    its heavy step after the waits, in the sequence of `configurations`.  `final` is the commit that the gate judged
    last, and `ledgers` holds each ledger that THE FALLS folded into it.  `final` is "" for a verdict of no commit.
    """

    configurations: tuple[str, ...]
    problems: tuple[str, ...]
    seconds: float
    step_seconds: tuple[float, ...] = ()
    final: str = ""
    ledgers: tuple[str, ...] = ()

    @property
    def is_accepted(self) -> bool:
        """Tell if the gate accepts the commit: no configuration has a problem."""
        return not self.problems


# ── The selection ──────────────────────────────────────────────────────────


def is_build_file(path: str) -> bool:
    """Tell if a path is a build file (THE SELECTION)."""
    return PurePosixPath(path).name in BUILD_FILE_NAMES or path.endswith(BUILD_FILE_SUFFIX)


def select(
    changed: Sequence[str], parse_presets: Sequence[str], ledger_names: Sequence[str], is_every: bool
) -> tuple[Configuration, ...]:
    """Return the configurations of a change, in the sequence of CONFIGURATIONS (THE SELECTION).

    Args:
        changed: The paths that the change touches
        parse_presets: The parse presets that `loop syntax --changed` names
        ledger_names: The configuration names of the ledgers of the changed rules
        is_every: Select each configuration

    Complexity: linear in the paths and the names.
    """
    names = {"debug"}
    for path in changed:
        if path.endswith(CPP_SUFFIXES) or is_build_file(path):
            names.update(("release", "tsan"))
        is_top_build_file = is_build_file(path) and "/" not in path
        if path.startswith(SANITIZED_PREFIXES) or is_top_build_file:
            names.update(("tsan", "ubsan-strict"))
        if path.startswith(BENCH_PREFIXES):
            names.add("bench-fuzz")
        if path.startswith(EVERY_PREFIXES):
            is_every = True
    names.update(preset for preset in parse_presets if preset in BY_NAME)
    names.update(BY_LEDGER_NAME[name].name for name in ledger_names if name in BY_LEDGER_NAME)
    return tuple(item for item in CONFIGURATIONS if is_every or item.name in names)


def is_compiled_change(path: str) -> bool:
    """Tell if a path of a change is a C++ file or a build file, which a build of a configuration can read."""
    return path.endswith(CPP_SUFFIXES) or is_build_file(path)


def ledger_checks(
    changed: Sequence[str], counted: Mapping[str, Sequence[str]], selected: Sequence[Configuration]
) -> tuple[Configuration, ...]:
    """Return each configuration of a counted ledger that runs the ledger check, in the sequence of CONFIGURATIONS.

    `counted` gives the configuration names of each counted ledger (counted_ledgers()), and `selected` holds the
    configurations of the suite (THE SELECTION).  Complexity: linear in the paths and the names.
    """
    if not any(is_compiled_change(path) for path in changed):
        return ()
    names = {name for listed in counted.values() for name in listed}
    picked = {item.name for item in selected}
    return tuple(item for item in CONFIGURATIONS if item.ledger_name in names and item.name not in picked)


def counted_ledgers(tree: Path) -> dict[str, tuple[str, ...]]:
    """Return the configuration names of the ledger of each counted rule of a tree (THE COUNTED RULES).

    Raises:
        GateError: If the registry or the ledger of a counted rule does not read

    Complexity: one read of the registry, and one read of the ledger of each rule of the engine plugin or elf with
    the policy count.
    """
    try:
        registry = rule_registry.load(tree / rule_registry.REGISTRY_PATH)
    except (OSError, ValueError) as problem:
        raise GateError(f"the registry of {tree} does not read: {problem}") from None
    counted: dict[str, tuple[str, ...]] = {}
    for rule_id, rule in sorted(registry.rules.items()):
        if rule.engine not in COUNTED_ENGINES or rule.policy != rule_registry.Policy.COUNT:
            continue
        try:
            ledger = rule_ledger.read(rule, tree)
        except (OSError, ValueError) as problem:
            raise GateError(f"the ledger of {rule_id} in {tree} does not read: {problem}") from None
        if ledger.configurations:
            counted[rule_id] = tuple(item.name for item in ledger.configurations)
    return counted


def holding(counted: Mapping[str, Sequence[str]], configuration: Configuration) -> list[str]:
    """Return each counted rule with a ledger that holds a configuration, sorted."""
    return sorted(rule_id for rule_id, names in counted.items() if configuration.ledger_name in names)


def rule_module_path(rule: rule_registry.Rule, tree: Path) -> str | None:
    """Return the path of the module of a rule in a tree, or None when the rule has no module of its own."""
    if not rule.has_module:
        return None
    return "utils/" + meta_runner.module_name_of(rule, tree).replace(".", "/") + ".py"


def changed_rules(tree: Path, head: str, changed: Sequence[str]) -> list[rule_registry.Rule]:
    """Return each rule that a change touches: its ledger, its plants, its row of the registry or its module.

    The tree holds the commit of the change, and head is the commit that the change starts from.

    Raises:
        GateError: If the registry of the tree does not read

    Complexity: one read of the registry at each commit.  A change of a module of an engine adds one search of the
    module of each rule.
    """
    try:
        current = rule_registry.load(tree / rule_registry.REGISTRY_PATH)
    except (OSError, ValueError) as problem:
        raise GateError(f"the registry of the warm tree does not read: {problem}") from None
    earlier_text = git.show(tree, head, rule_registry.REGISTRY_PATH)
    earlier = rule_registry.examine(earlier_text.decode("utf-8"))[0].rules if earlier_text is not None else {}
    paths = set(changed)
    has_engine_change = any(path.startswith(ENGINE_PREFIX) for path in paths)
    found: list[rule_registry.Rule] = []
    for rule in current.rules.values():
        plants = f"{PLANT_PREFIX}{rule.id}/"
        old = earlier.get(rule.id)
        is_changed = (
            f"{LEDGER_PREFIX}{rule.id}.txt" in paths
            or any(path.startswith(plants) for path in paths)
            or old is None
            or replace(old, line=0) != replace(rule, line=0)
            or (has_engine_change and rule_module_path(rule, tree) in paths)
        )
        if is_changed:
            found.append(rule)
    return found


def ledger_names(tree: Path, rules: Sequence[rule_registry.Rule]) -> list[str]:
    """Return the configuration names of the ledgers of some rules in a tree, one time each, sorted.

    Raises:
        GateError: If a ledger does not read
    """
    names: set[str] = set()
    for rule in rules:
        if rule.policy == rule_registry.Policy.NONE:
            continue
        try:
            ledger = rule_ledger.read(rule, tree)
        except (OSError, ValueError) as problem:
            raise GateError(f"the ledger of {rule.id} in the warm tree does not read: {problem}") from None
        names.update(configuration.name for configuration in ledger.configurations)
    return sorted(names)


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
    """Return the words of the configure of the build directory of a configuration in a tree, with its cache variables."""
    build = build_directory(tree, configuration)
    return ["cmake", "--preset", configuration.preset, "-B", str(build), *configuration.cache]


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
    """

    root: Path
    work: Path
    jobs: int
    is_every: bool
    read: heavy.Reader = heavy.available_gb
    pause: heavy.Pause = time.sleep

    @property
    def tree(self) -> Path:
        """The warm tree."""
        return self.work / TREE

    @property
    def logs(self) -> Path:
        """The directory of the logs."""
        return self.work / LOGS

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
        self, configuration: Configuration, counted: Mapping[str, Sequence[str]], environment: Mapping[str, str]
    ) -> bool:
        """Run `guard write` of each counted rule with a ledger that holds a configuration (THE FALLS).

        A write that refuses a count above its row exits 1, and the check of the rule keeps that count as its error.
        Return True when a rule holds the configuration.
        """
        build = build_directory(self.tree, configuration)
        rules = holding(counted, configuration)
        for rule in rules:
            log = f"{configuration.name}.write-{rule}.log"
            command = [*self._master("guard"), "write", "--rule", rule, "--build-dir", str(build)]
            status = self._run_logged(command, log, environment, proc.TOOL_S)
            check_report.write_line(f"loop land: {configuration.name}: the write of {rule} exits {status}")
        return bool(rules)

    def _checked_falls(
        self,
        configuration: Configuration,
        rules: Sequence[str],
        counted: Mapping[str, Sequence[str]],
        environment: Mapping[str, str],
    ) -> list[str]:
        """Run the checks of some rules on a build, and after a failure THE FALLS and the checks again."""
        problems = self._checked(configuration, rules, environment)
        if problems and self._write_falls(configuration, counted, environment):
            problems = self._checked(configuration, rules, environment)
        return problems

    def _run_ledgers(
        self, configuration: Configuration, counted: Mapping[str, Sequence[str]], environment: Mapping[str, str]
    ) -> list[str]:
        """Do steps 1 and 4 of THE RUN OF A CONFIGURATION for a configuration of the ledger check, and give each problem.

        Raises:
            GateError: If the CMake cache of the build names no cmake
        """
        problem = self._configure(configuration, environment)
        if problem is not None:
            return [problem]
        log = f"{configuration.name}.build.log"
        if self._run_logged(build_words(self.tree, configuration, self.jobs), log, environment, TEST_RUN_S) != 0:
            return [f"{configuration.name}: the build gives an error, see {self.logs / log}"]
        return self._checked_falls(configuration, holding(counted, configuration), counted, environment)

    def _fold(self, candidate: Candidate) -> tuple[str, tuple[str, ...]]:
        """Fold each ledger that THE FALLS changed in the warm tree into the commit, and give the commit and the ledgers.

        The warm tree moves to the new commit.  With no changed ledger, the result is the commit and no ledger.

        Raises:
            git.GitError: If a git command fails
        """
        ledgers = tuple(sorted(git.work_changes(self.tree, "HEAD", [LEDGER_PREFIX])))
        if not ledgers:
            return candidate.commit, ()
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
        self, configuration: Configuration, counted: Mapping[str, Sequence[str]], environment: Mapping[str, str]
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
        return self._checked_falls(configuration, COUNTED_RULES, counted, environment)

    def _run_objects(
        self,
        configuration: Configuration,
        counted: Mapping[str, Sequence[str]],
        environment: Mapping[str, str],
        is_retry: bool,
    ) -> list[str]:
        """Do steps 1 and 3 of THE RUN OF A CONFIGURATION for a configuration of objects, and give each problem.

        With no counted rule in `counted`, a red test gets no write (THE FALLS).
        """
        problem = self._configure(configuration, environment)
        if problem is not None:
            return [problem]
        build = build_directory(self.tree, configuration)
        report = build / affected.STATE_DIRECTORY / "junit.xml"
        report.unlink(missing_ok=True)
        log = f"{configuration.name}.test.log"
        command = [*self._master("loop"), "test", str(build), "-j", str(self.jobs), *(() if is_retry else ("--all",))]
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
            self._write_falls(configuration, counted, environment)
            check_report.write_line(f"loop land: {configuration.name}: {len(red)} red, each runs again alone")
        still = self._red_alone(configuration, red, environment)
        if still:
            return [f"{configuration.name}: red alone too: {' '.join(still)}, see {self.logs / log}"]
        warnings_log = f"{configuration.name}.warnings.log"
        if self._run_logged([*self._master("guard"), "warnings", str(build)], warnings_log, environment, proc.TOOL_S):
            return [f"{configuration.name}: `guard warnings` gives an error, see {self.logs / warnings_log}"]
        return []

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
        rules = changed_rules(self.tree, candidate.head, candidate.changed)
        presets = self._parse_presets(based)
        names = ledger_names(self.tree, rules)
        counted = counted_ledgers(self.tree)
        for name in sorted({*names, *(item for listed in counted.values() for item in listed)}):
            if name not in BY_LEDGER_NAME:
                check_report.write_line(f"loop land: the host does not build the configuration {name} of a ledger")
        selected = select(candidate.changed, presets, names, self.is_every)
        checks = ledger_checks(candidate.changed, counted, selected)
        plan = tuple(item for item in CONFIGURATIONS if item in selected or item in checks)
        shown = " ".join(item.name for item in selected)
        checked = f", and the ledger check in {' '.join(item.name for item in checks)}" if checks else ""
        check_report.write_line(
            f"loop land: the commit {candidate.commit[:9]} runs in {shown}{checked}, logs in {self.logs}"
        )
        ran: list[str] = []
        spent: list[float] = []

        def run_of(configuration: Configuration, is_again: bool) -> list[str]:
            """Run one configuration of the plan, or its suite again after THE FALLS, and give each problem."""
            if configuration.is_parse:
                return self._run_parse(configuration, counted, environment)
            if configuration not in selected:
                return self._run_ledgers(configuration, counted, environment)
            problems = self._run_objects(configuration, {} if is_again else counted, environment, is_retry or is_again)
            if is_stacked and not problems:
                problems = self._checked(configuration, (STACKED_RULE,), based)
            return problems

        def step(configuration: Configuration, is_again: bool) -> list[str]:
            """Run one configuration in a heavy step, and keep its name and its time."""
            words = "build and test" if configuration in selected else "build and check the counted ledgers"
            if configuration.is_parse:
                words = "parse and check"
            check_report.write_line(f"loop land: {configuration.name}: {'test again' if is_again else words}")
            with heavy.heavy_step(heavy.FULL_LEVEL_GB, self.root, read=self.read, pause=self.pause):
                began = time.monotonic()
                problems = run_of(configuration, is_again)
                spent.append(round(time.monotonic() - began, 1))
            check_report.write_line(f"loop land: {configuration.name}: {spent[-1]:.0f} s in the heavy step")
            ran.append(configuration.name)
            return problems

        problems: list[str] = []
        for configuration in plan:
            problems = step(configuration, False)
            if problems:
                return Verdict(tuple(ran), tuple(problems), time.monotonic() - started, tuple(spent), candidate.commit)
        final, ledgers = self._fold(candidate)
        if ledgers:
            check_report.write_line(
                f"loop land: the counted rules wrote {' '.join(ledgers)}, folded into the commit {final[:9]}, and "
                f"each suite runs again"
            )
            for configuration in plan:
                if configuration.is_parse or configuration not in selected:
                    continue
                problems = step(configuration, True)
                if problems:
                    break
        return Verdict(tuple(ran), tuple(problems), time.monotonic() - started, tuple(spent), final, ledgers)


@contextlib.contextmanager
def session(root: Path, work: Path, jobs: int, is_every: bool) -> Iterator[Gate]:
    """Hold the lock of the warm tree for the block, and give the gate (THE LOCK OF THE TREE).

    The block waits while a different landing holds the lock.
    """
    work.mkdir(parents=True, exist_ok=True)
    with open(work / LOCK, "a", encoding="ascii") as handle:
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            check_report.write_line(f"loop land: a different landing holds {work / LOCK}, and this one waits")
            fcntl.flock(handle, fcntl.LOCK_EX)
        yield Gate(root, work, jobs, is_every)
