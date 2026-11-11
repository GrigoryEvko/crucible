"""coverage — run the gate mutants of each header of include/ in a time budget, and keep each run at its key.

THE SELECTION
    run() reads the items of the class mutant-runs.  An item is a header of
    include/ of the source tree of a build.  Its key is the key of THE KEYS
    OF A HEADER of assay.loop.evidence.items.  run() selects the pending items
    of the nightly target, or with is_release of the release target (THE
    PENDING ITEMS of that module).

THE TREE OF THE RUN
    The run of the gate mutants writes each mutant into the source tree of
    its build, and it puts the header back after each mutant
    (assay.mutation.run).  Thus the runner does not run the mutants in the
    tree of this package.  For a build of that tree, the runner uses the
    warm export EXPORT of the work root
    (assay.core.cache).  The export is one repository that holds the commit
    of HEAD of that tree, as the warm tree of the gate does (move_tree() of
    assay.loop.gate).  The runner configures the
    build of the preset MUTANT_PRESET in the export, and it runs `loop test`
    of the export in that build.  The build then holds each object of the
    target all and the record of each fixture, which the keys read.  A lock
    on the file LOCK of the export keeps two runs apart.  A build of a
    different tree, such as the warm tree of an entry, is the build of the run
    as it is.  The caller then owns that tree, and its build must hold the
    record of each fixture.

THE RUN OF A HEADER
    run_header() of assay.mutation.run runs each mutant of one header with the
    caps of THE JOBS of that module.  The candidates of the mutants come from
    the units of the build, one time for each call.
      * A run with no survivor and no invalid mutant that the admitted gates
        do not admit adds one sample of the quantity 1 (THE ADMITTED GATES of
        assay.loop.evidence.items).  Its facts are the count of the gates and
        of the mutants that a test killed.
      * Each other run keeps the failure of the key.  The failure gives the
        count of the survivors and of the invalid mutants, the first of each,
        and the command `loop evidence run coverage --build-dir BUILD --item
        HEADER --budget-s SECONDS`, which runs the mutants of the header
        again.  The budget is twice the time of the run, and at least twice
        FIRST_HEADER_S, so that the header can start.
    A SIGTERM stops the run after the header gets its bytes back.

THE BUDGET
    A header starts only when the time that the call used, with the time of
    one header, stays in the budget.  The time of one header is the wall time
    of one unit of the record of its key.  A key with no sample has the time
    FIRST_HEADER_S.  The headers run in the sequence of their names.

Complexity: one census of the build, one read of the candidates, and the run of the mutants of each selected header.

`guard selftest tests.loop.evidence.test_coverage` does a test of the module.
"""

import contextlib
import fcntl
import signal
import sys
import time
from collections.abc import Callable, Iterator, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import cache, git, paths, proc
from assay.core import ledger as rule_ledger
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.loop import gate, tiers
from assay.loop.evidence import items as evidence_items
from assay.loop.evidence import store as evidence_store
from assay.loop.evidence import targets
from assay.mutation import run as mutation_run
from assay.readers import cmake as cmake_reader

PROGRAM = "loop evidence run coverage"
# The directory of the warm export in the work root, its tree and its lock (THE TREE OF THE RUN).
EXPORT = "evidence-export"
TREE = "tree"
LOCK = "tree.lock"
# The time of one header with no sample (THE BUDGET), and the entry of the builder in the CMake cache.
FIRST_HEADER_S = 180.0
MAKE_ENTRY = "CMAKE_MAKE_PROGRAM"
# The statuses of a run of assay.mutation.run that keep a failure (THE RUN OF A HEADER).
INVALID = "invalid"
SURVIVED = "survived"
KILLED = "killed"

type HeaderRun = Callable[[str], list[mutation_run.Outcome]]


class CoverageError(Exception):
    """The tree of the run cannot be made, or its build gives no keys."""


@dataclass(frozen=True, slots=True)
class Verdict:
    """The judgment of the run of one header: its quantity, its facts, and the summary of its failure or ""."""

    quantity: float
    facts: tuple[tuple[str, str], ...]
    failure: str = ""


@dataclass(frozen=True, slots=True)
class Outcome:
    """The result of the runs: the headers that ran, the headers with no failure, the failures and the reason of the end."""

    headers: int = 0
    covered: int = 0
    failures: tuple[str, ...] = ()
    reason: str = ""


def admitted(ledger: rule_ledger.Ledger | None, outcome: mutation_run.Outcome) -> bool:
    """Return True when a row of the admitted gates admits the invalid mutant of an outcome (THE RUN OF A HEADER)."""
    key = mutation_run.exemption_row(outcome.header, outcome.kind, outcome.entity, outcome.original)
    return ledger is not None and key in ledger.base


def judged(outcomes: Sequence[mutation_run.Outcome], ledger: rule_ledger.Ledger | None) -> Verdict:
    """Return the judgment of the run of one header (THE RUN OF A HEADER)."""
    survivors = [item for item in outcomes if item.status.startswith(SURVIVED)]
    invalid = [item for item in outcomes if item.status == INVALID and not admitted(ledger, item)]
    killed = sum(1 for item in outcomes if item.status == KILLED)
    facts: tuple[tuple[str, str], ...] = (("gates", str(len(outcomes))), ("killed", str(killed)))
    if not survivors and not invalid:
        return Verdict(1.0, facts)
    parts = [f"{len(survivors)} mutants that no test kills"] if survivors else []
    parts += [f"{len(invalid)} invalid mutants that no row admits"] if invalid else []
    shown = [*survivors[:1], *invalid[:1]]
    firsts = tuple(("first", f"line {item.line} {item.kind} {item.entity} ({item.status})") for item in shown)
    facts = (*facts, ("survivors", str(len(survivors))), ("invalid", str(len(invalid))), *firsts)
    return Verdict(0.0, facts, f"the header has {' and '.join(parts)}")


def rerun_words(build_dir: Path, header: str, wall_s: float) -> tuple[str, ...]:
    """Return the command that runs the mutants of a header of the build again, after a run of wall_s seconds.

    The budget lets the header start (THE RUN OF A HEADER).
    """
    loop = paths.LOOP.relative_to(paths.REPO_ROOT).as_posix()
    budget = f"{2 * max(FIRST_HEADER_S, wall_s):.0f}"
    kind = str(targets.COVERAGE.kind)
    words = ("--build-dir", str(build_dir), "--item", header, "--budget-s", budget)
    return ("python3", loop, "evidence", "run", kind, *words)


def header_time(record: evidence_store.Record | None) -> float:
    """Return the wall time of one unit of a record, or FIRST_HEADER_S for a record with no sample (THE BUDGET)."""
    unit = None if record is None else record.unit_wall_s
    return FIRST_HEADER_S if unit is None else unit


def run_headers(
    store: evidence_store.EvidenceStore,
    chosen: Sequence[evidence_items.Pending],
    keyed: evidence_items.HeaderKeys,
    build_dir: Path,
    budget_s: float,
    run_one: HeaderRun,
) -> Outcome:
    """Run the selected headers in the budget, and add the sample or keep the failure of each one (THE BUDGET).

    `run_one` runs the mutants of one header: the run of assay.mutation.run, or the planted run of a test.
    Complexity: one call of `run_one` for each header that starts.
    """
    started = time.monotonic()
    rule = targets.COVERAGE.rule
    headers = covered = 0
    failures: list[str] = []
    reason = "each selected header has its target or a failure"
    for candidate in sorted(chosen, key=lambda item: item.item.name):
        estimate = header_time(candidate.record)
        used = time.monotonic() - started
        if used + estimate > budget_s:
            reason = f"the budget of {budget_s:g} s has no room for a header of {estimate:.0f} s after {used:.0f} s"
            break
        name = candidate.item.name
        header_start = time.monotonic()
        outcomes = run_one(name)
        wall_s = time.monotonic() - header_start
        verdict = judged(outcomes, keyed.ledger)
        headers += 1
        if verdict.failure:
            failure = evidence_store.Failure(
                evidence_store.now(), verdict.failure, rerun_words(build_dir, name, wall_s), verdict.facts
            )
            store.keep_failure(rule, name, candidate.item.key, failure)
            failures.append(name)
        else:
            sample = evidence_store.Sample(evidence_store.now(), 1.0, wall_s, verdict.facts)
            store.add(rule, name, candidate.item.key, sample)
            covered += 1
        shown = f": {verdict.failure}" if verdict.failure else ""
        check_report.write_line(f"{PROGRAM}: {name} took {wall_s:.0f} s{shown}")
    return Outcome(headers, covered, tuple(failures), reason)


def run_build(build_dir: Path, jobs: int) -> None:
    """Configure the build of the preset MUTANT_PRESET in its tree when it is not configured, and run `loop test`.

    The run of `loop test` builds the target all, and it runs each fixture (THE TREE OF THE RUN).

    Raises:
        CoverageError: If the configure cannot start or fails, or `loop test` cannot start
    """
    tree = evidence_items.source_tree(build_dir) if cmake_reader.is_configured(build_dir) else build_dir.parent
    try:
        if not cmake_reader.is_configured(build_dir):
            words = ["cmake", "--preset", targets.MUTANT_PRESET, "-B", str(build_dir)]
            proc.require(proc.run(proc.Command(words, timeout_s=proc.TOOL_S, cwd=tree, stdout=proc.Output.INHERIT)))
        loop = tree / paths.LOOP.relative_to(paths.REPO_ROOT)
        words = [sys.executable, "-S", str(loop), "test", str(build_dir), "-j", str(jobs)]
        shown = proc.Output.INHERIT
        completed = proc.run(proc.Command(words, timeout_s=proc.BUILD_S, cwd=tree, stdout=shown, stderr=shown))
    except proc.ToolFailed as problem:
        raise CoverageError(f"the build {build_dir} of the export cannot be made: {problem}") from None
    if completed.exit != 0:
        check_report.write_line(f"{PROGRAM}: `loop test` of the export exited with {completed.exit}")


@contextlib.contextmanager
def export_build(root: Path, jobs: int) -> Iterator[Path]:
    """Hold the lock of the warm export, give it the commit of HEAD of a tree, and give its build (THE TREE OF THE RUN).

    Raises:
        CoverageError: If the export cannot get the commit, or its build cannot be made
    """
    work = cache.work_root() / EXPORT
    work.mkdir(parents=True, exist_ok=True)
    with open(work / LOCK, "a", encoding="ascii") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX)
        tree = work / TREE
        try:
            gate.move_tree(root, tree, git.head(root))
        except (gate.GateError, git.GitError) as problem:
            raise CoverageError(f"the export {tree} cannot get the commit of {root}: {problem}") from None
        preset = next((item for item in tiers.read(tree).configure if item.name == targets.MUTANT_PRESET), None)
        if preset is None or preset.build is None:
            raise CoverageError(f"the presets of the export {tree} give no build of the preset {targets.MUTANT_PRESET}")
        run_build(preset.build, jobs)
        yield preset.build


def header_runner(keyed: evidence_items.HeaderKeys, build_dir: Path) -> HeaderRun:
    """Return the function that runs the mutants of one header of the build, with the candidates of its units.

    Raises:
        mutation_run.RunError: If the tests, the compile database or the targets of the build cannot be read
    """
    candidates = mutation_run.load_candidates(build_dir, {source: list(files) for source, files in keyed.deps.items()})
    entry = cmake_reader.cache(build_dir).get(MAKE_ENTRY)
    ninja = entry.value if entry is not None and entry.value else mutation_run.NINJA

    def run_one(header: str) -> list[mutation_run.Outcome]:
        """Run each mutant of one header, and put the header back on a SIGTERM too."""
        previous = signal.signal(signal.SIGTERM, mutation_run.stop_on_term)
        try:
            return mutation_run.run_header(
                header,
                keyed.source,
                candidates,
                mutation_run.JOBS,
                mutation_run.NEGATIVE_CAP,
                mutation_run.SYNTAX_CAP,
                run_cap=mutation_run.RUN_CAP,
                ninja_program=ninja,
            )
        except mutation_run.RunError as problem:
            check_report.write_line(f"{PROGRAM}: the run of {header} stopped: {problem}", is_error=True)
            return [mutation_run.Outcome("", header, "header", 0, header, "", INVALID)]
        finally:
            signal.signal(signal.SIGTERM, previous)

    return run_one


def run_keyed(build_dir: Path, budget_s: float, is_release: bool, names: Sequence[str], target: float) -> ExitCode:
    """Read the keys of the headers of a build, select them, and run them in the budget (THE SELECTION)."""
    try:
        keyed = evidence_items.mutant_keys(build_dir)
    except evidence_items.MUTANT_ERRORS as problem:
        message = f"{PROGRAM}: the headers of the build {build_dir} have no key: {problem}"
        check_report.write_line(message, is_error=True)
        return ExitCode.MISSING_INPUT
    store = evidence_store.EvidenceStore()
    found = evidence_items.mutant_items(targets.COVERAGE.rule, build_dir, keyed)
    chosen = evidence_items.pending(found, store, target, names)
    entry = "release" if is_release else "nightly"
    check_report.write_line(
        f"{PROGRAM}: {len(chosen)} headers of {build_dir} have less evidence than the {entry} target {target:g}.  The "
        f"budget is {budget_s:g} s"
    )
    if not chosen:
        return ExitCode.OK
    try:
        run_one = header_runner(keyed, build_dir)
    except mutation_run.RunError as problem:
        check_report.write_line(f"{PROGRAM}: the candidates of the build cannot be read: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    outcome = run_headers(store, chosen, keyed, build_dir, budget_s, run_one)
    check_report.write_line(
        f"{PROGRAM}: {outcome.headers} headers ran, {outcome.covered} of them with no failure, and "
        f"{len(outcome.failures)} kept a failure.  The runs stopped because {outcome.reason}"
    )
    return ExitCode.FINDINGS if outcome.failures else ExitCode.OK


def run(build_dir: Path, budget_s: float, is_release: bool, names: Sequence[str], jobs: int, target: float) -> ExitCode:
    """Run the headers of a build in the budget, in the tree of THE TREE OF THE RUN, and report.

    Returns:
        OK when no header kept a failure, FINDINGS when one did, MISSING_INPUT when the build gives no keys
    """
    started = time.monotonic()
    try:
        source = evidence_items.source_tree(build_dir) if cmake_reader.is_configured(build_dir) else paths.REPO_ROOT
        if source != paths.REPO_ROOT.resolve():
            return run_keyed(build_dir, budget_s, is_release, names, target)
        with export_build(paths.REPO_ROOT, jobs) as exported:
            remaining_s = budget_s - (time.monotonic() - started)
            return run_keyed(exported, remaining_s, is_release, names, target)
    except (CoverageError, cmake_reader.CmakeError) as problem:
        check_report.write_line(f"{PROGRAM}: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
