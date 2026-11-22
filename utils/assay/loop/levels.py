"""levels — build the vessel pair of each x86-64 level, install one level root, and run the vessel tests at each level.

Levels
    The engine ships one pair of vessel libraries for each x86-64 level, v1 to
    v4, in one level root (the level root of vessel/torch/CMakeLists.txt).  The
    loader of the vessel picks the highest level of the host from the root
    (the level of a library of vessel/torch/crucible_native.py).
    LEVEL_PRESETS names the configure preset of each level, release-v1 to
    release-v4.  The command reads FLAGS_VARIABLE and LEVEL_VARIABLE of each
    one from the presets file, so the flags of a level have one home, and the
    command makes no preset of its own.

Steps
    For each level of LEVEL_PRESETS, in their sequence, the command does these
    steps:
      1. It configures the build directory BUILD_PREFIX-vN of the tree from the
         preset VESSEL of assay.loop.gate, with the flags and the level of the
         level preset:
             cmake --preset vessel -B build-vessel-vN -DCMAKE_CXX_FLAGS=FLAGS -DCRUCIBLE_X86_LEVEL=vN
         The configure runs on each call, so a change of a preset reaches the
         build.
      2. It builds VESSEL_TARGETS of assay.loop.gate in that build.
      3. It installs the component vessel of the build into the level root
         ROOT_DIRECTORY of the tree.  The pair goes into
         lib/crucible/x86-64-vN, and the level probe into lib/crucible.  The
         call removes the root first, so the root holds only the libraries of
         this call.
    Then it reads the level of the host through the level probe of the root,
    with no request (the level of a host of assay.core.x86level).  So
    REQUEST_VARIABLE in the environment of the call changes no step.  For each
    level at or below the level of the host:
      4. It runs the vessel tests of the build of the level, `ctest --test-dir
         BUILD -L ^vessel$ --no-tests=error`, with ROOT_VARIABLE (the
         directory lib/crucible of the root) and with REQUEST_VARIABLE of
         assay.core.x86level set to the level.  The loader then takes the pair
         of the level from the root, and the test test_isa_level holds that
         the loaded pair is in the directory of that level.  A run in which no
         test has the label fails.
    A level above the host gets a line and no test run.  A step that fails
    stops the steps of its level, and the command goes on with the next level.
    When each level fails, the root has no probe, and the command reads no
    level of the host.
    The log of each step is ROOT_DIRECTORY/LOGS/vN-STEP.log.

Jobs
    A lease of the slot pool in a process that holds the lock of the heavy
    steps, or in a child of it, waits for that lock for ever (the lock of
    assay.loop.heavy).  So the command takes a lease only in a call by hand.
    share_of() reads the environment of the call, and Share gives the three
    ways:
      LEASE      a call by hand.  Each build and each test run goes behind
                 `loop heavy -j N`, which holds a lease of the pool with the
                 cap N of -j.  The build gets no -j, so it takes the
                 jobserver of the lease
      JOBSERVER  MAKEFLAGS names the FIFO jobserver of the caller, such as
                 `loop heavy -j N -- loop levels`.  The build gets no -j, and
                 ninja obeys the jobserver only with no -j.  The test run
                 keeps -j N, and ctest obeys the jobserver also with -j (the
                 jobserver of assay.loop.affected)
      STEP       a step of an entry, which holds the exclusive lock with
                 STEP_VARIABLE of assay.loop.heavy and gives no jobserver
                 (the work of assay.loop.entry).  Each build and each test run
                 gets -j with the job count that the memory gives the jobs of
                 the vessel (jobs_for_memory() of assay.loop.heavy, with the
                 memory of one job of the vessel of assay.loop.gate), with the
                 job count of the host as the maximum.
    No command of a call gets BUILD_PARALLEL_VARIABLE of assay.loop.heavy,
    because `cmake --build` gives ninja -j from it.

Usage
    loop levels [-j N]

Exit 0 when each step of each level passes, 1 when a step fails, 2 on a usage error, 3 when the presets do not read or
the host level cannot be read.  `guard selftest tests.loop.test_levels` does a test of the module.
"""

import enum
import os
import shutil
import sys
from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import cli, jobserver, paths, proc, x86level
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.loop import gate, heavy
from assay.readers import cmake as cmake_reader

PROGRAM = "loop levels"
# The configure preset of each level, in the sequence of the levels (the levels).
LEVEL_PRESETS = ("release-v1", "release-v2", "release-v3", "release-v4")
# The cache variables of a level preset that the build of its vessel pair takes (the steps, step 1).
FLAGS_VARIABLE = "CMAKE_CXX_FLAGS"
LEVEL_VARIABLE = "CRUCIBLE_X86_LEVEL"
# The start of the name of the build directory of a level, and the directory of the level root, in the tree.
BUILD_PREFIX = "build-vessel"
ROOT_DIRECTORY = "build-vessel-levels"
# The directory of the root that the loader reads, below the install prefix (the level root of
# vessel/torch/CMakeLists.txt), and the directory of the logs of a call.
ROOT_LIBRARIES = Path("lib") / "crucible"
LOGS = "logs"
# Mirrors LEVEL_ROOT_VARIABLE of vessel/torch/crucible_native.py.
ROOT_VARIABLE = "CRUCIBLE_LEVEL_ROOT"
# The component of the install of the vessel pair and of the probe (vessel/torch/CMakeLists.txt).
COMPONENT = "vessel"
# The steps of a level, in their sequence, for the names of the logs (the steps).
CONFIGURE, BUILD, INSTALL, TEST = "configure", "build", "install", "test"

# A runner of one command: its words, its environment and the path of its log.  It returns the exit of the command.
type Execute = Callable[[Sequence[str], Mapping[str, str], Path], int]


class LevelsError(Exception):
    """The presets of the tree give no level preset or no vessel preset that the command can use."""


class Share(enum.Enum):
    """The way that the builds and the test runs of a call get their jobs (the jobs)."""

    LEASE = "lease"
    JOBSERVER = "jobserver"
    STEP = "step"


class State(enum.StrEnum):
    """The state of a level at the end of a call."""

    PASSED = "passed"
    FAILED = "failed"
    ABOVE_HOST = "not run"


@dataclass(frozen=True, slots=True)
class Level:
    """One level of the levels: its word, the flags of its preset and the build directory of its vessel pair."""

    word: str
    flags: str
    build: Path

    @property
    def number(self) -> int:
        """The level as a number from 1 to 4."""
        return x86level.LEVEL_WORDS[self.word]


@dataclass(frozen=True, slots=True)
class Outcome:
    """The end of one level: its state, and the step that failed or an empty text."""

    level: Level
    state: State
    step: str = ""


def read_levels(root: Path) -> list[Level]:
    """Return each level of LEVEL_PRESETS from the presets file of a tree (the levels).

    Raises:
        LevelsError: If a level preset is missing, has no flags, or names a level that is not its own
    """
    configure = [preset for item in cmake_reader.presets(root) for preset in item.configure]
    names = {preset.name for preset in configure}
    if gate.VESSEL not in names:
        raise LevelsError(f"the presets of {root} give no preset {gate.VESSEL}, and each level builds from it")
    levels: list[Level] = []
    for name in LEVEL_PRESETS:
        try:
            preset = cmake_reader.effective(configure, name)
        except cmake_reader.CmakeError as problem:
            raise LevelsError(f"the level preset {name} cannot be read: {problem}") from None
        flags = preset.cache_variables.get(FLAGS_VARIABLE) or ""
        word = preset.cache_variables.get(LEVEL_VARIABLE) or ""
        if word not in x86level.LEVEL_WORDS or not flags:
            raise LevelsError(
                f"the level preset {name} gives {LEVEL_VARIABLE}={word!r} and {FLAGS_VARIABLE}={flags!r}, and a level "
                f"preset gives a level of {', '.join(x86level.LEVEL_WORDS)} and its flags"
            )
        levels.append(Level(word, flags, root / f"{BUILD_PREFIX}-{word}"))
    return levels


def share_of(environment: Mapping[str, str]) -> Share:
    """Return the share of a call from its environment (the jobs).

    A FIFO jobserver in MAKEFLAGS decides first, because `loop heavy` gives its command MAKEFLAGS and STEP_VARIABLE.
    """
    if jobserver.client_path(environment) is not None:
        return Share.JOBSERVER
    if environment.get(heavy.STEP_VARIABLE):
        return Share.STEP
    return Share.LEASE


def heavy_words(root: Path, jobs: int, share: Share, words: Sequence[str]) -> list[str]:
    """Return the words of a heavy command: the command behind `loop heavy -j N` in a call by hand, else the command.

    The jobs section gives the rule.
    """
    if share is not Share.LEASE:
        return list(words)
    return [sys.executable, str(root / "utils" / "bin" / "loop"), "heavy", "-j", str(jobs), "--", *words]


def level_commands(level: Level, root: Path, jobs: int, share: Share) -> list[tuple[str, list[str]]]:
    """Return the steps 1 to 3 of one level: the name of each step and its words (the steps).

    A build in a step gets -j with the job count of the step.  Each other build gets no -j, so it takes the jobserver
    of the lease of `loop heavy` or of the caller (the jobs).
    """
    build_jobs = ["-j", str(jobs)] if share is Share.STEP else []
    configure = [
        "cmake",
        "--preset",
        gate.VESSEL,
        "-B",
        str(level.build),
        f"-D{FLAGS_VARIABLE}={level.flags}",
        f"-D{LEVEL_VARIABLE}={level.word}",
    ]
    build = ["cmake", "--build", str(level.build), "--target", *gate.VESSEL_TARGETS, *build_jobs]
    install = ["cmake", "--install", str(level.build), "--component", COMPONENT, "--prefix", str(root / ROOT_DIRECTORY)]
    return [
        (CONFIGURE, configure),
        (BUILD, heavy_words(root, jobs, share, build)),
        (INSTALL, install),
    ]


def test_command(level: Level, root: Path, jobs: int, share: Share) -> tuple[list[str], dict[str, str]]:
    """Return the words and the variables of the run of the vessel tests of one level (the steps, step 4)."""
    words = ["ctest", "--test-dir", str(level.build), "-L", f"^{gate.VESSEL_LABEL}$", "--no-tests=error"]
    words += ["--output-on-failure", "-j", str(jobs)]
    variables = {
        ROOT_VARIABLE: str(root / ROOT_DIRECTORY / ROOT_LIBRARIES),
        x86level.REQUEST_VARIABLE: level.word,
    }
    return heavy_words(root, jobs, share, words), variables


def run_command(words: Sequence[str], variables: Mapping[str, str], log: Path) -> int:
    """Run one command in the tree with its two outputs in a log, and return its exit (the Execute of a call).

    The command gets no BUILD_PARALLEL_VARIABLE (the jobs).  A command past BUILD_S of assay.core.proc gives the exit
    TOOL_FAILED, as a program that cannot start does, and the log gives the reason.
    """
    environment = proc.environment(dict(variables))
    environment.pop(heavy.BUILD_PARALLEL_VARIABLE, None)
    command = proc.Command(
        list(words),
        timeout_s=proc.BUILD_S,
        cwd=paths.REPO_ROOT,
        env=environment,
        stdout=log,
        stderr=proc.Output.MERGE,
    )
    try:
        return heavy.exit_of(proc.run(command))
    except proc.ToolFailed as problem:
        log.write_text(f"{problem}\n", encoding="utf-8")
        return int(ExitCode.TOOL_FAILED)


def run_levels(
    root: Path,
    levels: Sequence[Level],
    jobs: int,
    share: Share,
    execute: Execute = run_command,
    host_of: Callable[[Path], int] | None = None,
) -> list[Outcome]:
    """Build, install and test each level, and return the outcome of each one (the steps).

    Args:
        root: The source tree
        levels: The levels of read_levels()
        jobs: The job count of each build and each test run (the jobs)
        share: The way that the builds and the test runs get their jobs (the jobs)
        execute: The runner of one command
        host_of: The reader of the level of the host from the probe of the root, or None for the level probe

    Raises:
        x86level.LevelError: If the probe of the root cannot be read
    """
    prefix = root / ROOT_DIRECTORY
    shutil.rmtree(prefix, ignore_errors=True)
    logs = prefix / LOGS
    logs.mkdir(parents=True)
    failed: dict[str, str] = {}
    for level in levels:
        for step, words in level_commands(level, root, jobs, share):
            log = logs / f"{level.word}-{step}.log"
            check_report.write_line(f"{PROGRAM}: {level.word}: {step}, log {log}")
            if execute(words, {}, log) != 0:
                failed[level.word] = step
                check_report.write_line(f"{PROGRAM}: {level.word}: the step {step} fails, see {log}", is_error=True)
                break
    outcomes = [Outcome(level, State.FAILED, failed[level.word]) for level in levels if level.word in failed]
    if len(outcomes) == len(levels):
        return outcomes
    probe = prefix / ROOT_LIBRARIES / x86level.PROBE_LIBRARY
    host = host_of(probe) if host_of is not None else x86level.probe_level(probe, "")
    outcomes = []
    for level in levels:
        if level.word in failed:
            outcomes.append(Outcome(level, State.FAILED, failed[level.word]))
            continue
        if level.number > host:
            check_report.write_line(f"{PROGRAM}: {level.word} is above the level v{host} of the host, so no test runs")
            outcomes.append(Outcome(level, State.ABOVE_HOST))
            continue
        words, variables = test_command(level, root, jobs, share)
        log = logs / f"{level.word}-{TEST}.log"
        check_report.write_line(f"{PROGRAM}: {level.word}: the vessel tests from the root, log {log}")
        if execute(words, variables, log) != 0:
            check_report.write_line(f"{PROGRAM}: {level.word}: the vessel tests fail, see {log}", is_error=True)
            outcomes.append(Outcome(level, State.FAILED, TEST))
            continue
        outcomes.append(Outcome(level, State.PASSED))
    return outcomes


def main(argv: list[str]) -> int:
    """Build, install and test each level of the tree (the steps), and print the outcome of each level."""
    parser = cli.parser(PROGRAM, "Build the vessel pair of each x86-64 level, install one level root, and test it.")
    parser.add_argument("-j", "--jobs", type=int, default=heavy.SLICE)
    options = parser.parse_args(argv)
    if options.jobs < 1:
        check_report.write_line(f"{PROGRAM}: give -j a count above 0, not {options.jobs}", is_error=True)
        return ExitCode.USAGE
    if not x86level.is_x86_64():
        check_report.write_line(f"{PROGRAM}: the host is not x86_64, and only an x86_64 build has a level")
        return ExitCode.OK
    root = paths.REPO_ROOT
    try:
        levels = read_levels(root)
    except LevelsError as problem:
        check_report.write_line(f"{PROGRAM}: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    torch_problem = gate.link_torch(root, root)
    if torch_problem is not None:
        check_report.write_line(f"{PROGRAM}: {torch_problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    share = share_of(os.environ)
    jobs = options.jobs
    if share is Share.STEP:
        jobs = heavy.jobs_for_memory(proc.DEFAULT_JOBS, gb_per_job=gate.gb_per_job_of(gate.VESSEL))
    try:
        outcomes = run_levels(root, levels, jobs, share)
    except x86level.LevelError as problem:
        check_report.write_line(f"{PROGRAM}: the level of the host cannot be read: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    for outcome in outcomes:
        failed = f" at the step {outcome.step}" if outcome.step else ""
        check_report.write_line(f"{PROGRAM}: {outcome.level.word}: {outcome.state}{failed}")
    return ExitCode.FINDINGS if any(outcome.state is State.FAILED for outcome in outcomes) else ExitCode.OK
