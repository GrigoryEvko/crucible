"""measurement — run each bench of a build in a time budget, and add each run that counts to its evidence.

THE SELECTION
    run() reads the items of the class `bench-runs` of one build: each bench
    target of its code model (assay.loop.evidence.items, THE KEYS).  The key
    of a bench holds its program and the host.  run() selects the pending
    items of the nightly target, or with is_release of the release target
    (THE PENDING ITEMS of that module).  A bench whose x86-64 level is above
    the level of the host does not run, and a line names the two levels
    (THE CHECK of assay.core.x86level).

THE RUN
    A run is one run of the program of a bench on the cores that claimed() of
    assay.loop.bench holds for that run, one core for each thread that the
    bench states (THE CORES OF A RUN of that module).  The placement is
    taskset on cores of the reserved CPUs of the host that no other bench
    holds.  When the host reserves its CPUs with a partition, the run also
    starts in the slice of the partition.  The program gets the clean
    environment of a test (assay.loop.teststep).  It also gets the variable
    JSON_VARIABLE of assay.readers.bench, and it then prints its reports as a
    JSON tail.  The runner reads the load of the host and the steal time of
    each core before and after the run (assay.loop.history, others_share(),
    and THE STEAL TIME of assay.loop.bench).  A run with steal time adds its
    lines to the record of that section.  Only one run occurs at a time,
    because two runs at the same time change the times of the two.
      * A run counts when its status is 0, the host was quiet, the cores lost
        no time to steal, no report has a pause over the bound of the
        harness (THE PAUSE CHECK of bench/bench_harness.h), and the
        coefficient of variation of each report is at most MAX_CV.  The host is quiet at QUIET_OTHERS_PCT of
        assay.loop.history.  The run adds one sample of the quantity 1.  A
        bench that does not use the reports of the harness prints no JSON
        tail.  Its run counts on a quiet host.
      * A run that does not count adds one sample of the quantity 0, with
        the reason in the fact `skip`.  Its wall time stays in the record.
        An entry then knows the time of a run of the bench on this host.
      * A failure keeps the failure of the key.  A failure is a run with a
        different status, a run over its time limit, or a run with a tail
        that does not read.  The failure holds the command, the status and
        the last line of the error output.
    Each sample holds the largest variation of the reports, the share of the
    other processes and the cores.  It also tells how the host reserves the
    cores, and the slice of its partition.

THE BUDGET
    A run starts only when the time that the call used, with the time of one
    run of the bench, stays in the budget.  The time of one run is the mean
    wall time of the samples of the record, or FIRST_RUN_S for a bench with
    no sample.  The next run goes to the pending bench with the minimum runs
    in this call.  Only a bench with a run that fits in the budget can go.
    Of two such benches, the bench with less evidence goes first.  Each pending
    bench runs one time before a bench runs a second time.  A bench with
    runs that do not count then uses only its part of the budget.  The runs
    stop when each bench has its target or a failure.  The time limit of a run is LIMIT_FACTOR times the
    time of one run, with the minimum LIMIT_FLOOR_S.

Complexity: one run of the program of a bench for each run, and one read of each program for its key.

`guard selftest tests.loop.evidence.test_measurement` does a test of the module.
"""

import contextlib
import time
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import proc, x86level
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.loop import bench
from assay.loop import history as loop_history
from assay.loop import teststep as test_launcher
from assay.loop.evidence import items as evidence_items
from assay.loop.evidence import search, targets
from assay.loop.evidence import store as evidence_store
from assay.readers import bench as bench_reader
from assay.readers import cmake as cmake_reader

# The largest coefficient of variation of a report of a run that counts (THE RUN).
MAX_CV = 0.05
# The time of one run of a bench with no sample.  The time limit of a run is a multiple of the time of one run, with a
# floor (THE BUDGET).
FIRST_RUN_S = 60.0
LIMIT_FACTOR = 10.0
LIMIT_FLOOR_S = 600.0
PROGRAM = "loop evidence run measurement"


class Measured:
    """One selected bench: its name, its program, its key, its evidence, its target, the time of one run and its cores.

    The runner changes the evidence and the time of one run after each run (THE RUN).  The class is then a state and
    not a record.
    """

    __slots__ = ("cores", "has_failed_now", "key", "name", "path", "run_s", "runs_now", "target", "value")

    def __init__(
        self,
        name: str,
        path: Path,
        key: str,
        value: float,
        target: float,
        run_s: float,
        cores: bench.CoreCount = bench.ONE_CORE,
    ) -> None:
        """Hold the state of one selected bench before its first run."""
        self.name = name
        self.path = path
        self.key = key
        self.value = value
        self.target = target
        self.run_s = run_s
        self.cores = cores
        self.runs_now = 0
        self.has_failed_now = False

    @property
    def is_pending(self) -> bool:
        """True when the bench has less evidence than its target, and no run of this call found a failure."""
        return self.value < self.target and not self.has_failed_now


@dataclass(frozen=True, slots=True)
class Ran:
    """One run of a bench: its command, its result or None at its time limit, its wall time, the share of others.

    steal_ms is the steal time of the cores during the run, or None when the run has no core or the statistics give
    none.
    """

    words: tuple[str, ...]
    completed: proc.Completed | None
    wall_s: float
    others_pct: float | None
    steal_ms: float | None = None


@dataclass(frozen=True, slots=True)
class Judged:
    """The judgment of one run: its quantity, its facts, and the summary of its failure or ""."""

    quantity: float
    facts: tuple[tuple[str, str], ...]
    failure: str = ""


@dataclass(frozen=True, slots=True)
class Outcome:
    """The result of the runs: the runs, the runs that counted, the failures and the reason of the end."""

    runs: int = 0
    counted: int = 0
    failures: tuple[str, ...] = ()
    reason: str = ""


def run_time(record: evidence_store.Record | None) -> float:
    """Return the mean wall time of the samples of a record, or FIRST_RUN_S when it has no sample (THE BUDGET)."""
    if record is None or not record.samples:
        return FIRST_RUN_S
    return sum(sample.wall_s for sample in record.samples) / len(record.samples)


def run_once(path: Path, where: bench.Placement, cwd: Path, limit_s: float) -> Ran:
    """Run the program of a bench one time on its placement, and read the load of the host around it (THE RUN)."""
    environment = dict(test_launcher.canonical_environment(proc.environment()))
    environment[bench_reader.JSON_VARIABLE] = "1"
    words = where.command((str(path),))
    steal_before = bench.read_steals(where.cores)
    before = loop_history.sample_host()
    started = time.monotonic()
    try:
        completed: proc.Completed | None = proc.run(
            proc.Command(
                list(words),
                timeout_s=limit_s,
                cwd=str(cwd),
                env=environment,
                stdout=proc.Output.CAPTURE,
                stderr=proc.Output.CAPTURE,
            )
        )
    except proc.TimedOut:
        completed = None
    wall_s = time.monotonic() - started
    steal_after = bench.read_steals(where.cores)
    stolen = bench.stolen_ms_of(steal_before, steal_after)
    steal_ms = sum(stolen.values()) if stolen else None
    bench.note_steal(where, steal_before, steal_after)
    return Ran(
        words, completed, wall_s, loop_history.others_share(before, loop_history.sample_host(), wall_s), steal_ms
    )


def judged(ran: Ran, core_facts: Sequence[tuple[str, str]]) -> Judged:
    """Return the judgment of one run: a run that counts, a run that does not count, or a failure (THE RUN)."""
    share = "" if ran.others_pct is None else f"{ran.others_pct:.1f}"
    stolen = "" if ran.steal_ms is None else f"{ran.steal_ms:.0f}"
    facts: tuple[tuple[str, str], ...] = (*core_facts, ("others_pct", share), ("steal_ms", stolen))
    if ran.completed is None:
        return Judged(0.0, facts, "the bench ran for more time than its limit")
    if ran.completed.exit != 0:
        return Judged(0.0, facts, f"the bench stopped with the status {ran.completed.exit}")
    try:
        found = bench_reader.reports(ran.completed.out)
    except bench_reader.BenchError as problem:
        return Judged(0.0, facts, str(problem))
    largest = max((report.cv for report in found or ()), default=0.0)
    paused = [report for report in found or () if report.is_paused]
    facts = (*facts, ("cv", f"{largest:.4f}"), ("reports", str(len(found or ()))))
    quiet_pct = loop_history.QUIET_OTHERS_PCT
    if ran.others_pct is None:
        reason = "the host gave no busy time"
    elif ran.others_pct > quiet_pct:
        reason = f"other processes used {ran.others_pct:.1f} % of the host, more than {quiet_pct:g} %"
    elif ran.steal_ms is not None and ran.steal_ms > 0:
        reason = f"the core lost {ran.steal_ms:.0f} ms to steal: the hypervisor ran other work on its host core"
    elif paused:
        reason = (
            f"the report {paused[0].name} lost {paused[0].pause_ns / 1e6:.2f} ms to a pause, more than the bound of "
            "the pause check of the harness"
        )
    elif largest > MAX_CV:
        reason = f"the largest coefficient of variation is {largest:.4f}, more than {MAX_CV:g}"
    else:
        return Judged(1.0, facts)
    return Judged(0.0, (*facts, ("skip", reason)))


def record_run(store: evidence_store.EvidenceStore, measured: Measured, ran: Ran, verdict: Judged) -> bool:
    """Add the sample or keep the failure of one run (THE RUN), and return True for a failure."""
    rule = targets.MEASUREMENT.rule
    if verdict.failure:
        status = "limit" if ran.completed is None else str(ran.completed.exit)
        failure = evidence_store.Failure(
            evidence_store.now(),
            verdict.failure,
            ran.words,
            (*verdict.facts, ("status", status), ("error", search.last_error_line(ran.completed))),
        )
        store.keep_failure(rule, measured.name, measured.key, failure)
        measured.has_failed_now = True
        return True
    sample = evidence_store.Sample(evidence_store.now(), verdict.quantity, ran.wall_s, verdict.facts)
    record = store.add(rule, measured.name, measured.key, sample)
    measured.value, measured.run_s = record.value, run_time(record)
    return False


def selection(
    programs: Sequence[evidence_items.Program], store: evidence_store.EvidenceStore, target: float, names: Sequence[str]
) -> list[Measured]:
    """Return each bench that the runs run (THE SELECTION).

    Complexity: one read of the store for each bench with a key.
    """
    found = {program.item.name: program for program in programs}
    chosen = evidence_items.pending([program.item for program in programs], store, target, names)
    return [
        Measured(
            item.item.name,
            found[item.item.name].path,
            item.item.key,
            item.value,
            target,
            run_time(item.record),
            found[item.item.name].cores,
        )
        for item in chosen
    ]


def level_selection(chosen: list[Measured], build_dir: Path) -> list[Measured]:
    """Return the selected benches that the host can run, and write a line for each bench above the host.

    A bench whose x86-64 level is above the level of the host does not start, because a check in the program can run
    after code of its level (THE CHECK of assay.core.x86level).  Complexity: one read of the needs of the build.

    Raises:
        x86level.LevelError: If the needs of the build or the level of the host cannot be read
    """
    kept: list[Measured] = []
    for measured in chosen:
        text = x86level.bench_refusal(build_dir, measured.name)
        if text is None:
            kept.append(measured)
        else:
            check_report.write_line(f"{PROGRAM}: {text}")
    return kept


def next_measured(chosen: Sequence[Measured], remaining_s: float) -> Measured | None:
    """Return the pending bench with the minimum runs, then the minimum evidence, of the benches with a run that fits.

    Returns None when no pending bench fits (THE BUDGET).
    """
    fitting = [measured for measured in chosen if measured.is_pending and measured.run_s <= remaining_s]
    return min(fitting, key=lambda measured: (measured.runs_now, measured.value, measured.name), default=None)


def run_benches(
    store: evidence_store.EvidenceStore,
    chosen: list[Measured],
    budget_s: float,
    place: Callable[[bench.CoreCount], contextlib.AbstractContextManager[bench.Placement]],
    cwd: Path,
    run_one: Callable[[Path, bench.Placement, Path, float], Ran] = run_once,
) -> Outcome:
    """Run the selected benches one at a time until each one gets its target or a failure, or the budget ends.

    `place` holds the cores of one run for its block: claimed() of assay.loop.bench, or the planted placement of a
    test.  `run_one` runs one bench: run_once(), or the planted run of a test.  Complexity: one run of a program for
    each run (THE BUDGET).

    Raises:
        bench.BenchError: If `place` refuses the claim of a run
    """
    started = time.monotonic()
    runs = counted = 0
    failures: list[str] = []
    reason = "each selected bench has its target or a failure"
    while True:
        remaining_s = budget_s - (time.monotonic() - started)
        measured = next_measured(chosen, remaining_s)
        if measured is None:
            if any(item.is_pending for item in chosen):
                reason = f"the budget of {budget_s:g} s has no room for one more run"
            break
        with place(measured.cores) as where:
            ran = run_one(measured.path, where, cwd, max(LIMIT_FLOOR_S, LIMIT_FACTOR * measured.run_s))
        verdict = judged(ran, where.facts())
        measured.runs_now += 1
        runs += 1
        counted += int(verdict.quantity > 0)
        skip = dict(verdict.facts).get("skip", "")
        if record_run(store, measured, ran, verdict):
            failures.append(measured.name)
        check_report.write_line(
            f"{PROGRAM}: {measured.name} ran for {ran.wall_s:.1f} s"
            + (f", and the run does not count: {skip}" if skip else "")
        )
    return Outcome(runs, counted, tuple(failures), reason)


def run(build_dir: Path, budget_s: float, is_release: bool, names: Sequence[str], jobs: int, target: float) -> ExitCode:
    """Select the benches of a build, run them in the budget, and report (THE SELECTION, THE RUN).

    `jobs` has no effect: only one run occurs at a time (THE RUN).

    Returns:
        OK when no run found a failure, FINDINGS when one did, MISSING_INPUT when the build cannot be read
    """
    del jobs
    try:
        programs = evidence_items.bench_programs(targets.MEASUREMENT.rule, build_dir)
    except (cmake_reader.CmakeError, bench.BenchError) as problem:
        check_report.write_line(f"{PROGRAM}: the benches of {build_dir} cannot be read: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    store = evidence_store.EvidenceStore()
    try:
        chosen = level_selection(selection(programs, store, target, names), build_dir)
    except x86level.LevelError as problem:
        check_report.write_line(f"{PROGRAM}: the x86-64 level of the host cannot be read: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    settings = bench.Settings.from_environment()
    entry = "release" if is_release else "nightly"
    check_report.write_line(
        f"{PROGRAM}: {len(chosen)} benches of {build_dir} have less evidence than the {entry} target {target:g} "
        f"runs.  The budget is {budget_s:g} s, and each run takes a claim of the cores of its bench"
    )
    try:
        outcome = run_benches(store, chosen, budget_s, lambda count: bench.claimed(settings, count), build_dir)
    except bench.BenchError as problem:
        check_report.write_line(f"{PROGRAM}: the reserved CPUs of the host give no claim: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    check_report.write_line(
        f"{PROGRAM}: {outcome.runs} runs, {outcome.counted} of them count, and {len(outcome.failures)} failures.  The "
        f"runs stopped because {outcome.reason}"
    )
    for name in outcome.failures:
        check_report.write_line(f"{PROGRAM}: the bench {name} gave a failure, and its key keeps it")
    return ExitCode.FINDINGS if outcome.failures else ExitCode.OK
