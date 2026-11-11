"""entry — the nightly entry and the release entry: run the work of the plan of a tier in a time budget.

THE SCOPE
    `loop nightly` and `loop release` (assay.loop.nightly, assay.loop.release)
    run the entry of their tier.  The scope of an entry is each tier of ORDER
    of assay.loop.tiers up to its own tier.  The nightly entry then runs the
    items of the gate and of the nightly tier.  The release entry also runs
    the items of the release tier.  The entry reads the plan of the tree
    (assay.loop.plan) and the presets file on each run.  No list of this
    module names a preset, a test, a bench or a rule.

THE WORK
    The entry makes one job of each of these, in this sequence:
      1. Each parse preset in the scope.  The job configures its build when
         the build has no graph of Ninja, and runs `loop syntax BUILD`.  Then
         it runs `guard check --rule R --build-dir BUILD` of each rule R of
         COUNTED_RULES of assay.guard.engines.buildsys.parse_builds
      2. Each configure preset of objects in the scope that uses no profile,
         the tier gate first and then in the sequence of the presets file.
         The job configures its build when the build has no graph, and runs
         `loop test BUILD`.  That command runs only the tests with no current
         pass in its state or in the shared store (assay.loop.affected).  A
         configure preset with no plain test preset and with a filtered test
         preset in the scope builds the target all.  Then it runs `ctest
         --preset Q` of each such test preset Q
      3. Each step of STEPS of assay.loop.plan in the scope, in its sequence.
         A command that ends with --build-dir gets the build directory of the
         preset of the step.  The step of `loop fuzz` runs one campaign of
         each harness (THE CAMPAIGN).  A step of `loop evidence run` is part
         of job 5
      4. Each configure preset of objects in the scope that uses the profile
         of `loop pgo` (assay.loop.tiers, PresetTier).  It comes after the
         step that collects the profile
      5. The runner of each class of TARGETS of assay.loop.evidence.targets
         that a step in the scope names.  The step `loop evidence run` names
         each class, and the step `loop evidence run KIND` names the class of
         KIND.  The runner runs only when an item of the class has less
         evidence than the target of the tier (THE BUDGET).
    Each job runs as one heavy step of assay.loop.heavy.  A session of the
    gate then goes before it, and the other heavy steps of the host wait for
    it.  A job of a preset and a runner of evidence get the jobs that the
    memory gives the step (THE MEMORY of assay.loop.heavy).  A campaign gets
    one CPU for each job of its step (THE CAMPAIGN).  A different step of
    STEPS holds only the lock, because its command gives its own job count.
    A job that fails stops no other job.

THE BUDGET
    --budget-s gives the wall time of the run, or DEFAULT_BUDGET_S of the
    tier.  A job gets the remaining time of the budget, and it stops at the
    end of the budget.  A job that cannot start in the budget does not run.

    The runners of job 5 divide the remaining time.  A runner gets the time
    of the remaining units of its class with NEED_MARGIN, from the wall time
    of one unit of each item.  When an item has no such wall time, the runner
    gets an equal part of the remaining time.  A runner does not get more
    than that part.

THE CAMPAIGN
    A campaign uses afl-fuzz in the directory of the AFL++ tools of
    assay.loop.fuzz.  With no afl-fuzz, the job of each harness is blocked,
    and the report gives the reason.  Else the job runs `loop fuzz BUILD OUT
    HARNESS CPUS SECONDS`.  BUILD is CAMPAIGN_BUILD in the tree, and OUT is a
    directory of the harness in it.

    CPUS are at most CAMPAIGN_CORES CPUs of this process that are not
    reserved CPUs of the host (reserved_cpus() of assay.loop.fuzz, which
    reads them as `loop bench` does).  The campaign gives its heavy step one
    job for each of these CPUs.  CPUS then keeps one CPU for each job that
    the memory gives the step (THE MEMORY of assay.loop.heavy).  SECONDS is
    an equal part of the remaining time for each remaining campaign.

THE REPORT
    Each run writes the directory TIER-TIME in REPORTS of the build directory
    of the preset REPORT_PRESET.  The directory holds the log of each command
    in LOGS, and the text report REPORT_NAME:
      * the tier, the commit, the state of the tree, the host, the times and
        the budget
      * each job with its state, its wall time, the tests that ran and the
        tests with a current pass in the store.  The state is a word of
        State.  Each failure has the command that runs it again
      * the evidence of each class against the target of the tier, and each
        open item.  An open item has less evidence than its target, or a kept
        failure with its command, or no key
      * the totals.
    The command prints the report, with at most SHOWN_OPEN open items of each
    class.  The run keeps the reports of the last KEPT_RUNS runs of its tier,
    and it removes the older ones.

Usage
    loop nightly [--budget-s SECONDS] [-j N] [--plan]
    loop release [--budget-s SECONDS] [-j N] [--plan]

    --plan  Print the jobs of a run, and run nothing

Exit 1 when a job fails or an item has a kept failure, else 0.  Exit 2 on a usage error or in a heavy step.  Exit 3
when the plan or the registry does not read, or when a preset has a problem.
`guard selftest tests.loop.test_entry` does a test of the module.
"""

import enum
import os
import shutil
import socket
import sys
import time
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import cli, git, paths, proc
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.guard.engines.buildsys.parse_builds import COUNTED_RULES
from assay.loop import affected, bench, fuzz, heavy, plan, tiers
from assay.loop import history as loop_history
from assay.loop.evidence import items as evidence_items
from assay.loop.evidence import status as evidence_status
from assay.loop.evidence import store as evidence_store
from assay.loop.evidence import targets as evidence_targets
from assay.readers import ctest as ctest_reader
from assay.readers import ninja as ninja_reader
from assay.readers import python as python_reader
from assay.setup import pins as cmake_pin

# The wall time of a run of each tier when the call gives none, in seconds (THE BUDGET).
DEFAULT_BUDGET_S = {tiers.Tier.NIGHTLY: 8 * 3600.0, tiers.Tier.RELEASE: 24 * 3600.0}
# The minimum time of a runner, in seconds (THE BUDGET).
MINIMUM_RUNNER_S = 60.0
# The multiplier of the estimate of a runner (THE BUDGET).
NEED_MARGIN = 1.25
# The time after its budget that a runner gets to complete its last unit, in seconds.
RUNNER_GRACE_S = 900.0
# The preset of the build directory of the reports, and the directory of the reports in it (THE REPORT).
REPORT_PRESET = "default"
REPORTS = "loop-entries"
# The name of a report, the directory of the logs, and the runs that each tier keeps (THE REPORT).
REPORT_NAME = "report.txt"
LOGS = "logs"
KEPT_RUNS = 20
# The open items of each class that the printed report gives (THE REPORT).
SHOWN_OPEN = 20
# The build directory of a campaign in the tree, and the most CPUs of a campaign (THE CAMPAIGN).
CAMPAIGN_BUILD = "build-afl"
CAMPAIGN_CORES = 16
# The first words of a step that adds evidence, and of a step of campaigns (THE WORK, 3 and 5).
EVIDENCE_WORDS = ("loop", "evidence", "run")
FUZZ_WORDS = ("loop", "fuzz")


class State(enum.StrEnum):
    """The state of a job at the end of a run (THE REPORT)."""

    PASSED = "passed"
    FAILED = "failed"
    BLOCKED = "blocked"
    STOPPED = "stopped"
    NOT_RUN = "not run"


class JobKind(enum.StrEnum):
    """The kind of a job (THE WORK)."""

    PARSE = "parse"
    TESTS = "tests"
    PRESET_TESTS = "preset-tests"
    STEP = "step"
    EVIDENCE = "evidence"


@dataclass(frozen=True, slots=True)
class Job:
    """One job of a run (THE WORK).

    `name` is the name of a configure preset, of a step or of a kind of evidence.  `build` is the build directory of
    a preset.  `test_presets` are the filtered test presets of a job of the kind PRESET_TESTS, and `item` is the item
    of a step with `each`.
    """

    kind: JobKind
    name: str
    build: Path | None = None
    test_presets: tuple[str, ...] = ()
    step: plan.Step | None = None
    item: str = ""

    @property
    def title(self) -> str:
        """The kind and the name of the job, and the item of a step, for the report."""
        return f"{self.kind} {self.name}" + (f" {self.item}" if self.item else "")


@dataclass(frozen=True, slots=True)
class Outcome:
    """The end of one job: its state, a summary, its wall time, its tests and the commands that run its failures again.

    `ran` counts the tests that ran, and `stored` counts the tests with a current pass in the store.
    """

    job: Job
    state: State
    summary: str
    wall_s: float = 0.0
    ran: int = 0
    stored: int = 0
    reruns: tuple[str, ...] = ()


@dataclass(frozen=True, slots=True)
class RunFacts:
    """The facts of the head of a report: the tier, the commit, the changed files, the host, the times and the budget."""

    tier: tiers.Tier
    commit: str
    changed: int
    host: str
    started: str
    ended: str
    wall_s: float
    budget_s: float
    directory: Path


# ── The jobs ───────────────────────────────────────────────────────────────


def scope_of(tier: tiers.Tier) -> tuple[tiers.Tier, ...]:
    """Return the tiers of the scope of the entry of a tier (THE SCOPE)."""
    return tiers.ORDER[: tiers.ORDER.index(tier) + 1]


def preset_jobs(
    presets: tiers.Tiers, chosen: frozenset[tuple[plan.Kind, str]], scope: Sequence[tiers.Tier]
) -> tuple[list[Job], list[Job], list[Job]]:
    """Return the jobs of the parse presets, of the presets with no profile and of the presets with a profile.

    Complexity: linear in the presets.
    """
    plain = {item.configure for item in presets.tests if item.is_inherited}
    filtered: dict[str, list[str]] = {}
    for item in presets.tests:
        if not item.is_inherited and not item.is_repeated and (plan.Kind.TEST_PRESET, item.name) in chosen:
            filtered.setdefault(item.configure, []).append(item.name)
    parse: list[Job] = []
    objects: list[tuple[int, Job, bool]] = []
    for item in presets.configure:
        is_chosen = (plan.Kind.PRESET, item.name) in chosen
        if item.build is None or not (is_chosen or item.name in filtered):
            continue
        if item.is_parse:
            parse.append(Job(JobKind.PARSE, item.name, item.build))
            continue
        if item.name in filtered and (item.name not in plain or not is_chosen):
            job = Job(JobKind.PRESET_TESTS, item.name, item.build, tuple(filtered[item.name]))
        else:
            job = Job(JobKind.TESTS, item.name, item.build)
        rank = scope.index(item.tier) if item.tier in scope else len(scope)
        objects.append((rank, job, item.uses_profile))
    ordered = sorted(objects, key=lambda entry: entry[0])
    return (
        parse,
        [job for _, job, uses_profile in ordered if not uses_profile],
        [job for _, job, uses_profile in ordered if uses_profile],
    )


def jobs_of(found: plan.Plan, presets: tiers.Tiers, tier: tiers.Tier) -> list[Job]:
    """Return the jobs of a run of the entry of a tier, in their sequence (THE WORK).

    Complexity: linear in the items of the plan, the presets and the steps.
    """
    scope = scope_of(tier)
    chosen = frozenset((item.kind, item.name) for item in found.items if item.tier in scope)
    parse, objects, profiled = preset_jobs(presets, chosen, scope)
    steps: list[Job] = []
    kinds: set[str] = set()
    for step in plan.STEPS:
        if (plan.Kind.STEP, step.name) not in chosen:
            continue
        if step.command[: len(EVIDENCE_WORDS)] == EVIDENCE_WORDS:
            named = step.command[len(EVIDENCE_WORDS) :]
            kinds |= {str(target.kind) for target in evidence_targets.TARGETS if not named or target.kind in named}
            continue
        if step.each is None:
            steps.append(Job(JobKind.STEP, step.name, step=step))
            continue
        names = sorted(item.name for item in found.items if item.kind is step.each)
        steps += [Job(JobKind.STEP, step.name, step=step, item=name) for name in names]
    evidence = [
        Job(JobKind.EVIDENCE, str(target.kind)) for target in evidence_targets.TARGETS if str(target.kind) in kinds
    ]
    return [*parse, *objects, *steps, *profiled, *evidence]


# ── The budget ─────────────────────────────────────────────────────────────


def is_open(row: evidence_status.Row, is_release: bool) -> bool:
    """Tell if a runner can add evidence to an item: it has a key, no kept failure, and less than its target."""
    return bool(row.key) and row.failure is None and evidence_status.remaining(row, is_release) > 0


def evidence_budget(
    rows: Sequence[evidence_status.Row], is_release: bool, remaining_s: float, classes_left: int
) -> float:
    """Return the time of the runner of one class, from the open items of its rows (THE BUDGET).

    Complexity: linear in the rows.
    """
    share = remaining_s / max(1, classes_left)
    chosen = [row for row in rows if is_open(row, is_release)]
    if any(row.unit_wall_s is None for row in chosen):
        return share
    need = sum(evidence_status.remaining(row, is_release) * float(row.unit_wall_s or 0.0) for row in chosen)
    return min(share, max(MINIMUM_RUNNER_S, need * NEED_MARGIN))


def run_jobs(
    jobs: Sequence[Job],
    run_one: Callable[[int, Job, float], Outcome],
    deadline: float,
    clock: Callable[[], float] = time.monotonic,
) -> list[Outcome]:
    """Run each job in its sequence until the deadline, and return the outcome of each one (THE BUDGET).

    Args:
        jobs: The jobs of the run
        run_one: The function that runs one job: its index, the job and the deadline
        deadline: The end of the budget on the clock
        clock: The clock of the deadline
    """
    found: list[Outcome] = []
    for index, job in enumerate(jobs):
        if deadline - clock() <= 0:
            found.append(Outcome(job, State.NOT_RUN, "the budget ended before the job"))
            continue
        check_report.write_line(f"entry: start {job.title}")
        outcome = run_one(index, job, deadline)
        check_report.write_line(f"entry: {outcome.state} {job.title} in {outcome.wall_s:.0f} s: {outcome.summary}")
        found.append(outcome)
    return found


# ── The commands of the jobs ───────────────────────────────────────────────


def failed_names(report: Path) -> list[str]:
    """Return each test with the status fail in a JUnit report of ctest, or none when the report does not read."""
    try:
        results = ctest_reader.results(report)
    except ctest_reader.CtestError:
        return []
    return sorted(name for name, status in results.items() if status is ctest_reader.TestStatus.FAIL)


def campaign_cpus(available: Sequence[int], reserved: Sequence[int]) -> tuple[int, ...]:
    """Return at most CAMPAIGN_CORES CPUs of a campaign: the available CPUs that are not reserved (THE CAMPAIGN)."""
    excluded = set(reserved)
    return tuple(cpu for cpu in sorted(available) if cpu not in excluded)[:CAMPAIGN_CORES]


class Runner:
    """The commands of the jobs of one run in a tree (THE WORK)."""

    def __init__(self, root: Path, tier: tiers.Tier, jobs: int, logs: Path, presets: tiers.Tiers) -> None:
        """Keep the tree, the tier, the job count, the directory of the logs and the presets of the run."""
        self.root = root
        self.tier = tier
        self.jobs = jobs
        self.logs = logs
        self.builds = {item.name: item.build for item in presets.configure if item.build is not None}
        self.rows: list[evidence_status.Row] | None = None
        self.evidence_left = 0
        self.campaigns_left = 0

    def master(self, name: str) -> list[str]:
        """Return the words that run a master of the tree."""
        return [sys.executable, str(self.root / "utils" / "bin" / name)]

    def logged(self, words: Sequence[str], log: Path, deadline: float, limit_s: float | None = None) -> int | None:
        """Run one command in the tree with its outputs in a log, and give its exit, or None when the budget ends.

        A program that cannot start gives the exit TOOL_FAILED, and the log gives the reason.
        """
        left = deadline - time.monotonic() if limit_s is None else limit_s
        if left <= 0:
            return None
        command = proc.Command(
            list(words),
            timeout_s=left,
            cwd=self.root,
            env=proc.environment({heavy.STEP_VARIABLE: heavy.STEP_VALUE}),
            stdout=log,
            stderr=proc.Output.MERGE,
            stop=proc.Stop.INTERRUPT,
        )
        try:
            return heavy.exit_of(proc.run(command))
        except proc.TimedOut:
            return None
        except proc.ToolFailed as problem:
            log.write_text(f"{problem}\n", encoding="utf-8")
            return int(ExitCode.TOOL_FAILED)

    def run(self, index: int, job: Job, deadline: float) -> Outcome:
        """Run one job as one heavy step, and return its outcome with its wall time (THE WORK)."""
        started = time.monotonic()
        prefix = self.logs / f"{index:02d}-{job.kind}-{job.name}{'-' + job.item if job.item else ''}"
        if job.kind is JobKind.EVIDENCE:
            outcome = self.evidence(job, prefix, deadline)
        elif job.kind is JobKind.STEP and job.step is not None and job.step.command[:2] == FUZZ_WORDS and job.item:
            outcome = self.campaign(job, prefix, deadline)
        elif job.kind is JobKind.STEP:
            with heavy.heavy_lock(self.root):
                outcome = self.step(job, prefix, deadline)
        else:
            with heavy.heavy_step(self.root, self.jobs) as step_jobs:
                outcome = self.preset_job(job, prefix, deadline, step_jobs)
        return Outcome(
            outcome.job,
            outcome.state,
            outcome.summary,
            time.monotonic() - started,
            outcome.ran,
            outcome.stored,
            outcome.reruns,
        )

    def preset_job(self, job: Job, prefix: Path, deadline: float, jobs: int) -> Outcome:
        """Run a job of a preset in the heavy step of the caller, with the job count of that step."""
        build = job.build or self.root
        if not ninja_reader.graph_file(build).is_file():
            words = ["cmake", "--preset", job.name]
            status = self.logged(words, Path(f"{prefix}.configure.log"), deadline)
            if status != 0:
                return self.ended(job, status, words, Path(f"{prefix}.configure.log"))
        if job.kind is JobKind.PARSE:
            return self.parse(job, build, prefix, deadline, jobs)
        if job.kind is JobKind.TESTS:
            return self.tests(job, build, prefix, deadline, jobs)
        return self.preset_tests(job, build, prefix, deadline, jobs)

    def ended(self, job: Job, status: int | None, words: Sequence[str], log: Path) -> Outcome:
        """Return the outcome of a job when a command does not give the exit 0: stopped, or a failure with its command."""
        if status is None:
            return Outcome(job, State.STOPPED, f"the budget ended during `{words[0]}`, see {log}")
        return Outcome(job, State.FAILED, f"a command exits {status}, see {log}", reruns=(" ".join(words),))

    def parse(self, job: Job, build: Path, prefix: Path, deadline: float, jobs: int) -> Outcome:
        """Parse each unit of a parse build, and check the counted rules on its reports (THE WORK, 1)."""
        commands = [[*self.master("loop"), "syntax", str(build), "-j", str(jobs)]]
        commands += [
            [*self.master("guard"), "check", "--rule", rule, "--build-dir", str(build)] for rule in COUNTED_RULES
        ]
        for number, words in enumerate(commands):
            log = Path(f"{prefix}.{number}.log")
            status = self.logged(words, log, deadline)
            if status != 0:
                return self.ended(job, status, words, log)
        return Outcome(job, State.PASSED, f"each unit parses, and the rules {', '.join(COUNTED_RULES)} hold")

    def tests(self, job: Job, build: Path, prefix: Path, deadline: float, jobs: int) -> Outcome:
        """Build a build and run each test with no current pass through `loop test` (THE WORK, 2)."""
        before = len(loop_history.read_lines(build))
        words = [*self.master("loop"), "test", str(build), "-j", str(jobs)]
        log = Path(f"{prefix}.log")
        status = self.logged(words, log, deadline)
        lines = loop_history.read_lines(build)
        block = lines[-1].get("tests") if len(lines) > before else None
        ran, stored, failed = (int(loop_history.number(block, name) or 0) for name in ("selected", "skipped", "failed"))
        if status == 0:
            text = f"{ran} tests ran, {stored} with a current pass in the store"
            return Outcome(job, State.PASSED, text, 0, ran, stored)
        if status is None or failed == 0:
            outcome = self.ended(job, status, words, log)
            return Outcome(job, outcome.state, outcome.summary, 0, ran, stored, outcome.reruns)
        ctest = cmake_pin.configured_program(build, "ctest")
        names = failed_names(build / affected.STATE_DIRECTORY / "junit.xml")
        reruns = tuple(f"{ctest} --test-dir {build} -R '^{name}$' --output-on-failure" for name in names)
        return Outcome(job, State.FAILED, f"{failed} tests failed, see {log}", 0, ran, stored, reruns)

    def preset_tests(self, job: Job, build: Path, prefix: Path, deadline: float, jobs: int) -> Outcome:
        """Build a build and run each filtered test preset of it with ctest (THE WORK, 2)."""
        cmake = cmake_pin.configured_program(build, "cmake")
        ctest = cmake_pin.configured_program(build, "ctest")
        words = [cmake, "--build", str(build), "-j", str(jobs)]
        log = Path(f"{prefix}.build.log")
        status = self.logged(words, log, deadline)
        if status != 0:
            return self.ended(job, status, words, log)
        ran = 0
        reruns: list[str] = []
        for name in job.test_presets:
            report = Path(f"{prefix}.{name}.junit.xml")
            words = [ctest, "--preset", name, "--output-junit", str(report)]
            log = Path(f"{prefix}.{name}.log")
            status = self.logged(words, log, deadline)
            if status is None:
                return Outcome(job, State.STOPPED, f"the budget ended during the test preset {name}", 0, ran)
            try:
                results = ctest_reader.results(report)
            except ctest_reader.CtestError:
                results = {}
            ran += len(results)
            names = sorted(test for test, result in results.items() if result is ctest_reader.TestStatus.FAIL)
            reruns += [f"{ctest} --preset {name} -R '^{test}$' --output-on-failure" for test in names]
            if status != 0 and not names:
                reruns.append(" ".join(words))
        if reruns:
            return Outcome(job, State.FAILED, f"a test preset fails, see {prefix}.*.log", 0, ran, 0, tuple(reruns))
        return Outcome(job, State.PASSED, f"{ran} tests ran in {', '.join(job.test_presets)}", 0, ran)

    def step(self, job: Job, prefix: Path, deadline: float) -> Outcome:
        """Run one step of STEPS (THE WORK, 3)."""
        step = job.step
        if step is None or step.each is not None:
            return Outcome(job, State.BLOCKED, "the entry gives no arguments to an item of this step")
        words = self.master(step.command[0]) + list(step.command[1:]) if step.command[0] in ("loop", "guard") else []
        if not words:
            return Outcome(job, State.BLOCKED, f"the entry does not run the program {step.command[0]}")
        if words[-1] == plan.BUILD_OPTION:
            build = self.builds.get(step.preset)
            if build is None or not ninja_reader.graph_file(build).is_file():
                return Outcome(job, State.BLOCKED, f"the build of the preset {step.preset!r} is not configured")
            words.append(str(build))
        log = Path(f"{prefix}.log")
        status = self.logged(words, log, deadline)
        if status != 0:
            return self.ended(job, status, words, log)
        return Outcome(job, State.PASSED, step.text)

    def campaign(self, job: Job, prefix: Path, deadline: float) -> Outcome:
        """Run one campaign of AFL++ on a harness, or give the reason why it cannot run (THE CAMPAIGN)."""
        self.campaigns_left = max(1, self.campaigns_left)
        tool = fuzz.afl_directory() / "afl-fuzz"
        if not tool.is_file():
            self.campaigns_left -= 1
            return Outcome(job, State.BLOCKED, f"the host has no {tool}.  Install AFL++, or set AFL_BIN")
        try:
            reserved = fuzz.reserved_cpus()
        except bench.BenchError as problem:
            self.campaigns_left -= 1
            return Outcome(job, State.BLOCKED, f"the reserved CPUs do not read: {problem}")
        cpus = campaign_cpus(tuple(os.sched_getaffinity(0)), reserved)
        seconds = int((deadline - time.monotonic()) / self.campaigns_left) - fuzz.STOP_GRACE_S - fuzz.INSTANCE_MARGIN_S
        self.campaigns_left -= 1
        if not cpus or seconds < MINIMUM_RUNNER_S:
            return Outcome(job, State.NOT_RUN, "no free CPU, or no time for a campaign")
        build = self.root / CAMPAIGN_BUILD
        log = Path(f"{prefix}.log")
        with heavy.heavy_step(self.root, len(cpus)) as step_jobs:
            used = cpus[:step_jobs]
            words = [*self.master("loop"), "fuzz", str(build), str(build / "campaigns" / job.item), job.item]
            words += [",".join(str(cpu) for cpu in used), str(seconds)]
            status = self.logged(words, log, deadline)
        if status != 0:
            return self.ended(job, status, words, log)
        return Outcome(job, State.PASSED, f"a campaign of {seconds} s on {len(used)} CPUs")

    def evidence_rows(self) -> list[evidence_status.Row]:
        """Return the rows of the evidence of the tree, and read them one time for the runners of a run."""
        if self.rows is None:
            self.rows = read_rows(self.root)[0]
        return self.rows

    def evidence(self, job: Job, prefix: Path, deadline: float) -> Outcome:
        """Run the runner of one class of evidence when an item of the class is open (THE WORK, 5)."""
        is_release = self.tier is tiers.Tier.RELEASE
        target = next(item for item in evidence_targets.TARGETS if str(item.kind) == job.name)
        rows = [row for row in self.evidence_rows() if row.rule == target.rule]
        classes_left = max(1, self.evidence_left)
        self.evidence_left = max(0, self.evidence_left - 1)
        if not any(is_open(row, is_release) for row in rows):
            text = f"no item of {target.rule} needs more evidence for the {self.tier} target"
            return Outcome(job, State.PASSED, text)
        budget_s = evidence_budget(rows, is_release, deadline - time.monotonic(), classes_left)
        log = Path(f"{prefix}.log")
        with heavy.heavy_step(self.root, self.jobs) as step_jobs:
            words = [*self.master("loop"), "evidence", "run", job.name, "--budget-s", f"{budget_s:.0f}"]
            words += ["-j", str(step_jobs), *(["--release"] if is_release else [])]
            status = self.logged(words, log, deadline, budget_s + RUNNER_GRACE_S)
        if status == 0:
            return Outcome(job, State.PASSED, f"the runner added evidence in a budget of {budget_s:.0f} s")
        if status is None:
            return Outcome(job, State.STOPPED, f"the runner ran past its budget of {budget_s:.0f} s, see {log}")
        return Outcome(job, State.FAILED, f"the runner exits {status}, see {log}", reruns=(" ".join(words),))


# ── The report ─────────────────────────────────────────────────────────────


def read_rows(root: Path) -> tuple[list[evidence_status.Row], list[str]]:
    """Return the rows of the evidence of a tree and the problems of its builds (assay.loop.evidence.status)."""
    try:
        budgets = check_report.read_budgets()
    except (OSError, ValueError) as problem:
        return [], [f"the registry cannot be read: {problem}"]
    found, problems = evidence_items.tree_items(root, [])
    return evidence_status.rows_of(found, evidence_store.EvidenceStore(), budgets), list(problems)


def open_lines(rows: Sequence[evidence_status.Row], is_release: bool, shown: int | None) -> list[str]:
    """Return a line for each open item of the rows, at most `shown` of each class (THE REPORT)."""
    lines: list[str] = []
    for target in evidence_targets.TARGETS:
        chosen: list[str] = []
        for row in rows:
            if row.rule != target.rule:
                continue
            if row.failure is not None:
                chosen.append(f"  failure  {row.rule} {row.item}: {row.failure.summary}")
                chosen.append(f"           again: {' '.join(row.failure.rerun)}")
            elif not row.key:
                chosen.append(f"  no key   {row.rule} {row.item}: {row.reason}")
            elif evidence_status.remaining(row, is_release) > 0:
                target_value = row.release if is_release else row.nightly
                chosen.append(f"  short    {row.rule} {row.item}: {row.value:g} of {target_value:g} {row.unit}")
        if shown is not None and len(chosen) > shown:
            hidden = len(chosen) - shown
            chosen = [*chosen[:shown], f"  ... {hidden} more lines of {target.rule} in the report file"]
        lines += chosen
    return lines


def open_count(rows: Sequence[evidence_status.Row], is_release: bool) -> int:
    """Return the open items of the rows: a kept failure, no key, or less evidence than the target (THE REPORT)."""
    return sum(row.failure is not None or not row.key or evidence_status.remaining(row, is_release) > 0 for row in rows)


def class_lines(rows: Sequence[evidence_status.Row], is_release: bool) -> list[str]:
    """Return a line for each class: its items, the items at the target, short, with a failure and with no key."""
    lines: list[str] = []
    for target in evidence_targets.TARGETS:
        chosen = [row for row in rows if row.rule == target.rule]
        failures = sum(row.failure is not None for row in chosen)
        keyless = sum(not row.key and row.failure is None for row in chosen)
        short = sum(is_open(row, is_release) for row in chosen)
        done = len(chosen) - failures - keyless - short
        lines.append(
            f"  {target.rule:16} {len(chosen)} items: {done} at the target, {short} short, {failures} with a failure, "
            f"{keyless} with no key"
        )
    return lines


def report_text(
    facts: RunFacts,
    outcomes: Sequence[Outcome],
    rows: Sequence[evidence_status.Row],
    problems: Sequence[str],
    shown: int | None = None,
) -> str:
    """Return the text of the report of one run (THE REPORT).

    Complexity: linear in the outcomes and the rows.
    """
    is_release = facts.tier is tiers.Tier.RELEASE
    tree = "the tree is clean" if facts.changed == 0 else f"the tree has {facts.changed} changed files"
    lines = [
        f"{tiers.ENTRIES[facts.tier]}: one run of the {facts.tier} entry",
        f"  commit   {facts.commit}, {tree}",
        f"  host     {facts.host}",
        f"  started  {facts.started}, ended {facts.ended}",
        f"  wall     {facts.wall_s:.0f} s of a budget of {facts.budget_s:.0f} s",
        f"  report   {facts.directory / REPORT_NAME}",
        "",
        "The jobs",
    ]
    for outcome in outcomes:
        lines.append(f"  {outcome.state:8} {outcome.job.title:40} {outcome.wall_s:7.0f} s  {outcome.summary}")
        lines += [f"           again: {words}" for words in outcome.reruns]
    lines += ["", f"The evidence against the {facts.tier} target", *class_lines(rows, is_release)]
    lines += [f"  problem: {text}" for text in problems]
    lines += ["", "The open items", *(open_lines(rows, is_release, shown) or ["  none"])]
    counts = {state: sum(outcome.state is state for outcome in outcomes) for state in State}
    states = ", ".join(f"{counts[state]} {state}" for state in State)
    failures = sum(row.failure is not None for row in rows)
    ran, stored = sum(outcome.ran for outcome in outcomes), sum(outcome.stored for outcome in outcomes)
    lines += [
        "",
        "Totals",
        f"  {len(outcomes)} jobs: {states}",
        f"  {ran} tests ran, {stored} with a current pass in the store",
        f"  {open_count(rows, is_release)} open items, {failures} with a kept failure",
    ]
    return "\n".join(lines) + "\n"


def report_directory(root: Path, presets: tiers.Tiers, tier: tiers.Tier, stamp: str) -> Path:
    """Return the directory of the report of one run (THE REPORT)."""
    builds = [item.build for item in presets.configure if item.name == REPORT_PRESET and item.build is not None]
    return (builds[0] if builds else root / "build") / REPORTS / f"{tier}-{stamp}"


def remove_old(directory: Path, tier: tiers.Tier) -> None:
    """Remove the reports of a tier other than the last KEPT_RUNS (THE REPORT)."""
    runs = sorted(path for path in directory.glob(f"{tier}-*") if path.is_dir())
    for path in runs[: max(0, len(runs) - KEPT_RUNS)]:
        shutil.rmtree(path, ignore_errors=True)


# ── The command ────────────────────────────────────────────────────────────


def read_plan(root: Path) -> tuple[plan.Plan, tiers.Tiers]:
    """Return the plan of a tree with no build, and its presets.

    Raises:
        OSError: If the registry or the master loop cannot be read
        ValueError: If the registry does not read
        python_reader.PythonError: If a module of the plan does not parse
    """
    registry = rule_registry.load(root / rule_registry.REGISTRY_PATH)
    presets = tiers.read(root)
    return plan.plan(root, registry, presets, (), ()), presets


def main(tier: tiers.Tier, argv: list[str]) -> int:
    """Run the entry of a tier with the words after its command (Usage)."""
    parser = cli.parser(tiers.ENTRIES[tier], f"Run the work of the plan of the tier {tier} in a time budget.")
    parser.add_argument("--budget-s", type=float, default=DEFAULT_BUDGET_S[tier])
    parser.add_argument("-j", "--jobs", type=int, default=proc.DEFAULT_JOBS)
    parser.add_argument("--plan", action="store_true", dest="is_plan_only")
    options = parser.parse_args(argv)
    program = tiers.ENTRIES[tier]
    if options.budget_s <= 0 or options.jobs < 1:
        check_report.write_line(f"{program}: give --budget-s a positive number, and -j a positive count", is_error=True)
        return ExitCode.USAGE
    if os.environ.get(heavy.STEP_VARIABLE):
        check_report.write_line(
            f"{program}: the entry takes the lock of the heavy steps for each job.  Do not run it in a heavy step",
            is_error=True,
        )
        return ExitCode.USAGE
    root = paths.REPO_ROOT
    try:
        found, presets = read_plan(root)
    except (OSError, ValueError, plan.PlanError, python_reader.PythonError) as problem:
        check_report.write_line(f"{program}: the plan does not read: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    blocking = [item for item in found.problems if item.kind is plan.ProblemKind.PRESET_BLOCK]
    for item in blocking:
        check_report.write_line(f"{program}: {item.path}: {item.message}", is_error=True)
    if blocking:
        return ExitCode.MISSING_INPUT
    jobs = jobs_of(found, presets, tier)
    if options.is_plan_only:
        for job in jobs:
            check_report.write_line(job.title)
        return ExitCode.OK
    started = loop_history.iso_now()
    begin = time.monotonic()
    deadline = begin + options.budget_s
    directory = report_directory(root, presets, tier, started.replace("-", "").replace(":", ""))
    (directory / LOGS).mkdir(parents=True, exist_ok=True)
    runner = Runner(root, tier, options.jobs, directory / LOGS, presets)
    runner.evidence_left = sum(job.kind is JobKind.EVIDENCE for job in jobs)
    runner.campaigns_left = sum(job.kind is JobKind.STEP and bool(job.item) for job in jobs)
    outcomes = run_jobs(jobs, runner.run, deadline)
    rows, problems = read_rows(root)
    try:
        state = git.status(root)
        commit, changed = state.commit or "unknown", state.changed
    except git.GitError:
        commit, changed = "unknown", 0
    facts = RunFacts(
        tier,
        commit,
        changed,
        socket.gethostname(),
        started,
        loop_history.iso_now(),
        time.monotonic() - begin,
        options.budget_s,
        directory,
    )
    (directory / REPORT_NAME).write_text(report_text(facts, outcomes, rows, problems), encoding="utf-8")
    remove_old(directory.parent, tier)
    check_report.write_line(report_text(facts, outcomes, rows, problems, SHOWN_OPEN))
    is_red = any(outcome.state is State.FAILED for outcome in outcomes) or any(row.failure for row in rows)
    return ExitCode.FINDINGS if is_red else ExitCode.OK
