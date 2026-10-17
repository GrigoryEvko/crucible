"""repetition — run each test of a build again in a time budget, and add each pass to the evidence of its key.

THE SELECTION
    run() reads the items of the class stress-passes of one build.  It
    selects the pending items (assay.loop.evidence.items, THE PENDING ITEMS)
    of the nightly target, or with is_release of the release target.

THE ROUNDS
    A round runs each selected test one time with the ctest of the build, as
    `loop test` runs a test (assay.loop.affected, run_tests()).  After the
    round, the runner gives the planner of that module the tests again, as
    `loop test` does after a run (THE READ SET of that module).  A traced run
    can give a test a new read set, and its pass then belongs to the key of
    that read set.  A test with inputs that changed during its run gets no
    pass.
      * A pass adds one sample with the quantity 1 and the wall time of the
        run, from the record of the test (assay.core.record).
      * A failure keeps the failure of its key: the repetition at that key,
        and the command that runs the test again until a failure occurs.
      * A test that ctest did not run gets nothing.
    The runner removes from the selection each test that gets its target or
    gives a failure.

THE BUDGET
    A round starts only when the time that the call used, with the wall time
    of the last round, stays in the budget.  Before the first round, the
    estimate is the largest wall time of one unit in the records of the
    selected items.  An item with no record gives no estimate.  A first round
    can then use more time than the budget, by its own time.

Complexity: one run of ctest for each round, and the planner of assay.loop.affected for each selected test after
each round.

`guard selftest tests.loop.evidence.test_repetition` does a test of the module.
"""

import time
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import record as cost_record
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.loop import affected
from assay.loop.evidence import items as evidence_items
from assay.loop.evidence import store as evidence_store
from assay.loop.evidence import targets
from assay.readers import ctest as ctest_reader

# The directory of the selection file and of the JUnit report of a round, in the build directory.
STATE_DIRECTORY = "evidence"


class Selected:
    """One selected test: its entry, its key, its evidence, its target, its kept failure and the wall time of one pass.

    The runner changes the key, the evidence and the failure after each round (THE ROUNDS).  The class is then a
    state and not a record.
    """

    __slots__ = ("has_failed_now", "has_failure", "key", "target", "test", "unit_wall_s", "value")

    def __init__(
        self,
        test: ctest_reader.TestEntry,
        key: str,
        value: float,
        target: float,
        has_failure: bool = False,
        unit_wall_s: float | None = None,
    ) -> None:
        """Hold the state of one selected test before the first round."""
        self.test = test
        self.key = key
        self.value = value
        self.target = target
        self.has_failure = has_failure
        self.has_failed_now = False
        self.unit_wall_s = unit_wall_s


@dataclass(frozen=True, slots=True)
class Outcome:
    """The result of the rounds: the rounds, the passes and the failures that they added, and the reason of the end.

    `round_s` gives the wall time of each round, in its sequence.
    """

    rounds: int = 0
    passes: int = 0
    failures: tuple[str, ...] = ()
    reason: str = ""
    selected: tuple[str, ...] = ()
    round_s: tuple[float, ...] = ()


def record_wall_s(build_dir: Path, name: str) -> float:
    """Return the wall time of the last run of a test from its record, or 0 when the test has no record."""
    try:
        return cost_record.read_test(Path(cost_record.test_record_file(build_dir, name))).cost.wall_s
    except cost_record.RecordError:
        return 0.0


def selection(
    keys: evidence_items.TestKeys, store: evidence_store.EvidenceStore, target: float, names: Sequence[str]
) -> list[Selected]:
    """Return each test that the rounds run (THE SELECTION).

    Complexity: the planner of assay.loop.affected for each test, and one read of the store for each item with a key.
    """
    by_name = {test.name: test for test in keys.tests}
    found = evidence_items.class_items(keys, targets.STRESS)
    return [
        Selected(
            by_name[candidate.item.name],
            candidate.item.key,
            candidate.value,
            target,
            candidate.has_failure,
            None if candidate.record is None else candidate.record.unit_wall_s,
        )
        for candidate in evidence_items.pending(found, store, target, names)
    ]


def rerun_words(build_dir: Path, name: str, repetitions: int) -> tuple[str, ...]:
    """Return the command that runs a test again until a failure occurs, at most as many times as its repetition."""
    return ("ctest", "--test-dir", str(build_dir), "-R", f"^{name}$", "--repeat", f"until-fail:{repetitions}")


def record_round(
    keys: evidence_items.TestKeys,
    store: evidence_store.EvidenceStore,
    chosen: Sequence[Selected],
    statuses: dict[str, ctest_reader.TestStatus],
    number: int,
) -> tuple[int, list[str]]:
    """Add the pass or keep the failure of each test of one round (THE ROUNDS), and return the passes and the failures.

    Complexity: the planner of assay.loop.affected for each test of the round.
    """
    keys.forget()
    rule = targets.STRESS.rule
    passes, failures = 0, []
    for selected in chosen:
        name = selected.test.name
        status = statuses.get(name)
        if status not in (ctest_reader.TestStatus.RUN, ctest_reader.TestStatus.FAIL):
            continue
        after, is_current = keys.key_after_run(selected.test)
        if status == ctest_reader.TestStatus.RUN and is_current:
            selected.key = after
            sample = evidence_store.Sample(
                evidence_store.now(), 1.0, record_wall_s(keys.build_dir, name), (("round", str(number)),)
            )
            selected.value = store.add(rule, name, selected.key, sample).value
            passes += 1
        elif status == ctest_reader.TestStatus.FAIL:
            key = after or selected.key
            repetition = int(selected.value) + 1
            failure = evidence_store.Failure(
                evidence_store.now(),
                f"the test gave a failure in the repetition {repetition} at its key",
                rerun_words(keys.build_dir, name, repetition),
                (("repetition", str(repetition)), ("round", str(number))),
            )
            store.keep_failure(rule, name, key, failure)
            selected.has_failure = True
            selected.has_failed_now = True
            failures.append(name)
    return passes, failures


def run_rounds(
    keys: evidence_items.TestKeys,
    store: evidence_store.EvidenceStore,
    chosen: list[Selected],
    budget_s: float,
    jobs: int,
) -> Outcome:
    """Run rounds of the selected tests until each one gets its target or a failure, or the budget ends (THE BUDGET).

    Complexity: one run of ctest for each round.
    """
    started = time.monotonic()
    estimate = max((item.unit_wall_s or 0.0 for item in chosen), default=0.0)
    state_dir = keys.build_dir / STATE_DIRECTORY
    state_dir.mkdir(exist_ok=True)
    passes, failures, round_s = 0, [], []
    pending = list(chosen)
    reason = "each selected test has its target or a failure"
    while pending:
        used = time.monotonic() - started
        if used + estimate > budget_s:
            reason = f"the budget of {budget_s:g} s has no room for a round of {estimate:.1f} s after {used:.1f} s"
            break
        round_start = time.monotonic()
        entries = tuple(item.test for item in pending)
        layers = affected.serial_last(entries, affected.test_layers(entries, [entry.name for entry in entries], jobs))
        status, statuses = affected.run_tests(keys.build_dir, layers, state_dir, jobs, [], entries)
        round_s.append(time.monotonic() - round_start)
        if not statuses:
            reason = f"ctest gave no JUnit report (exit code {status})"
            break
        added, failed = record_round(keys, store, pending, statuses, len(round_s))
        passes += added
        failures += failed
        estimate = time.monotonic() - round_start
        pending = [item for item in pending if item.value < item.target and not item.has_failed_now]
    selected = tuple(item.test.name for item in chosen)
    return Outcome(len(round_s), passes, tuple(failures), reason, selected, tuple(round_s))


def run(build_dir: Path, budget_s: float, is_release: bool, names: Sequence[str], jobs: int, target: float) -> ExitCode:
    """Select the tests of a build, run the rounds, and report (THE SELECTION, THE ROUNDS).

    Returns:
        OK when no round found a failure, FINDINGS when one did, MISSING_INPUT when the build cannot be read
    """
    try:
        keys = evidence_items.TestKeys(build_dir)
    except affected.RunnerError as problem:
        check_report.write_line(f"loop evidence run repetition: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    store = evidence_store.EvidenceStore()
    chosen = selection(keys, store, target, names)
    entry = "release" if is_release else "nightly"
    check_report.write_line(
        f"loop evidence run repetition: {len(chosen)} tests of {build_dir} have less evidence than the {entry} target "
        f"{target:g}.  The budget is {budget_s:g} s"
    )
    outcome = run_rounds(keys, store, chosen, budget_s, jobs)
    check_report.write_line(
        f"loop evidence run repetition: {outcome.rounds} rounds added {outcome.passes} passes and "
        f"{len(outcome.failures)} failures.  The rounds stopped because {outcome.reason}"
    )
    for number, wall_s in enumerate(outcome.round_s, start=1):
        check_report.write_line(f"loop evidence run repetition: the round {number} took {wall_s:.1f} s")
    for name in outcome.failures:
        check_report.write_line(f"loop evidence run repetition: the test {name} gave a failure, and its key keeps it")
    return ExitCode.FINDINGS if outcome.failures else ExitCode.OK
