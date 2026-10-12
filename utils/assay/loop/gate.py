"""gate — build and test the commit of a landing in a warm tree, in each configuration that the change selects.

THE CONFIGURATIONS
    CONFIGURATIONS gives each configuration of the gate.  A configuration has
    a name, a configure preset and a build directory in the warm tree.  It
    also has the cache variables that its configure adds, and its name in a
    ledger (assay.core.configuration).  A parse configuration is a parse
    build of PARSE_PRESETS of assay.loop.syntax.  The ledger names are the
    names of an x86_64 host, the build host of the tree.

THE SELECTION
    select() gives the configurations of a change, in the sequence of
    CONFIGURATIONS:
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
         assay.guard.engines.buildsys.parse_builds.
      3. A configuration of objects: `loop test BUILD --all`, which builds
         the target all and runs each test (assay.loop.affected).  A retry
         after a move of HEAD runs `loop test BUILD`, which runs only the
         tests with an input that changed.  Each test that is red runs one
         more time, alone.  A test that is red alone too rejects the commit.
         An exit other than 0 with no red test rejects the commit too.  Then
         `guard warnings BUILD`.  The suite holds the ledger checks of the build
         kind, such as the tests guard_plugin (with the rule
         quarantine-lines), guard_artifact_symbols and guard_library_bytes.
         A second run of these checks gives no new verdict.  A test that gives
         a skip does not reject the commit, and the gate writes its name.
         A red test that runs alone writes its output to its log.
    Each step runs the masters of the warm tree.  Then the tests of the
    commit judge the commit.  The step writes its two outputs to a log in
    the directory LOGS.  The first configuration with a problem stops the
    gate.

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
    its heavy step after the waits, in the sequence of `configurations`.
    """

    configurations: tuple[str, ...]
    problems: tuple[str, ...]
    seconds: float
    step_seconds: tuple[float, ...] = ()

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
    """Return the build directory of a configuration in the warm tree."""
    return tree / configuration.build


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
        command = ["cmake", "--preset", configuration.preset, "-B", str(build), *configuration.cache]
        if self._run_logged(command, log, environment, CONFIGURE_S) != 0:
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

    def _run_parse(self, configuration: Configuration, environment: Mapping[str, str]) -> list[str]:
        """Do steps 1 and 2 of THE RUN OF A CONFIGURATION for a parse configuration, and give each problem."""
        problem = self._configure(configuration, environment)
        if problem is not None:
            return [problem]
        build = build_directory(self.tree, configuration)
        log = f"{configuration.name}.syntax.log"
        command = [*self._master("loop"), "syntax", str(build), "-j", str(self.jobs)]
        if self._run_logged(command, log, environment, TEST_RUN_S) != 0:
            return [f"{configuration.name}: a unit does not parse, see {self.logs / log}"]
        return self._checked(configuration, COUNTED_RULES, environment)

    def _run_objects(self, configuration: Configuration, environment: Mapping[str, str], is_retry: bool) -> list[str]:
        """Do steps 1 and 3 of THE RUN OF A CONFIGURATION for a configuration of objects, and give each problem."""
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
        for name in names:
            if name not in BY_LEDGER_NAME:
                check_report.write_line(f"loop land: the host does not build the configuration {name} of a ledger")
        selected = select(candidate.changed, presets, names, self.is_every)
        shown = " ".join(item.name for item in selected)
        check_report.write_line(f"loop land: the commit {candidate.commit[:9]} runs in {shown}, logs in {self.logs}")
        ran: list[str] = []
        spent: list[float] = []
        for configuration in selected:
            check_report.write_line(f"loop land: {configuration.name}: build and test")
            with heavy.heavy_step(heavy.FULL_LEVEL_GB, self.root, read=self.read, pause=self.pause):
                began = time.monotonic()
                if configuration.is_parse:
                    problems = self._run_parse(configuration, environment)
                else:
                    problems = self._run_objects(configuration, environment, is_retry)
                    if is_stacked and not problems:
                        problems = self._checked(configuration, (STACKED_RULE,), based)
                spent.append(round(time.monotonic() - began, 1))
            check_report.write_line(f"loop land: {configuration.name}: {spent[-1]:.0f} s in the heavy step")
            ran.append(configuration.name)
            if problems:
                return Verdict(tuple(ran), tuple(problems), time.monotonic() - started, tuple(spent))
        return Verdict(tuple(ran), (), time.monotonic() - started, tuple(spent))


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
