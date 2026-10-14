"""search — run each property test of a build with new seeds in a time budget, and add its CPU seconds to its evidence.

THE SELECTION
    run() reads the items of the class property-search of one build.  It
    selects the pending items (assay.loop.evidence.items, THE PENDING ITEMS)
    of the nightly target, or with is_release of the release target.

THE SLICES
    A slice is one run of the program of a property test, with a seed and a
    count of iterations.  It runs with no test launcher (assay.loop.teststep,
    launched()).  The program gets the clean environment of a test and the
    variables of the property ENVIRONMENT of the test.  It also gets the core
    limit of the launcher.  The seed comes from the key, the evidence of the
    key and the number of the slice.  Two slices of one key then search
    different inputs.  The count of iterations fills SLICE_S CPU seconds at
    the rate of the kept samples.  With no rate, it is the count of the
    command of the test.
      * A slice with the status 0 adds one sample: its CPU seconds, its wall
        time, its seed and its count of iterations.
      * A slice with a different status keeps the failure of its key.  A
        slice that runs for more time than its limit keeps it too.  The
        failure holds the seed, the count, the status, the last line of the
        error output and the command of the slice.  The property runner gives
        the same inputs for the same seed and count
        (test/fuzz/property/property_runner.h).
    After each slice, the planner of assay.loop.affected gives the key of the
    test again.  A slice of a test with inputs that changed during the slice
    gets no evidence.

THE BUDGET
    At most JOBS slices run at one time, one for each item.  A slice starts
    only when the time that the call used, with SLICE_S, stays in the budget.
    The next slice goes to the pending item with the minimum evidence, until
    each item gets its target or a failure.

Complexity: one run of the program of a test for each slice, and the planner of assay.loop.affected after each slice.

`guard selftest tests.loop.evidence.test_search` does a test of the module.
"""

import hashlib
import time
from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import proc
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.loop import affected
from assay.loop import teststep as test_launcher
from assay.loop.evidence import items as evidence_items
from assay.loop.evidence import store as evidence_store
from assay.loop.evidence import targets
from assay.readers import ctest as ctest_reader

# The CPU seconds of one slice (THE SLICES), and the time limit of a slice as a multiple of it, with a floor.
SLICE_S = 30.0
LIMIT_FACTOR = 10.0
LIMIT_FLOOR_S = 300.0
# The options of the property runner, and the count of iterations of a command that gives none.
SEED_OPTION = "--seed="
ITERATIONS_OPTION = "--iters="
DEFAULT_ITERATIONS = 1000
LARGEST_ITERATIONS = 1 << 40
# The property of the environment of a test, and the length of the line of the error output that a failure keeps.
ENVIRONMENT = "ENVIRONMENT"
ERROR_LINE_LIMIT = 300


class Searched:
    """One selected property test: its entry, its key, its evidence, its target, its rate and its slices.

    The runner changes the key, the evidence, the rate and the failure after each slice (THE SLICES).  The class is
    then a state and not a record.
    """

    __slots__ = ("has_failed_now", "is_running", "key", "number", "rate", "target", "test", "value")

    def __init__(
        self, test: ctest_reader.TestEntry, key: str, value: float, target: float, rate: float | None = None
    ) -> None:
        """Hold the state of one selected test before the first slice."""
        self.test = test
        self.key = key
        self.value = value
        self.target = target
        self.rate = rate
        self.number = 0
        self.has_failed_now = False
        self.is_running = False

    @property
    def is_pending(self) -> bool:
        """True when the test has less evidence than its target, and no slice of this call found a failure."""
        return self.value < self.target and not self.has_failed_now


@dataclass(frozen=True, slots=True)
class Slice:
    """One run of a property test: its seed, its count of iterations and its command."""

    seed: int
    iterations: int
    words: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class Outcome:
    """The result of the slices: the slices, the CPU seconds and the failures that they added, and the reason of the end."""

    slices: int = 0
    cpu_s: float = 0.0
    failures: tuple[str, ...] = ()
    reason: str = ""
    selected: tuple[str, ...] = ()


def program_words(test: ctest_reader.TestEntry) -> tuple[str, ...]:
    """Return the program of a test and its arguments, with no test launcher (THE SLICES)."""
    return tuple(test_launcher.launched(list(test.command)))


def command_iterations(words: Sequence[str]) -> int:
    """Return the count of iterations of the command of a test, or DEFAULT_ITERATIONS when it gives none."""
    for word in words[1:]:
        text = word.removeprefix(ITERATIONS_OPTION)
        if text != word and text.isdigit() and int(text) > 0:
            return int(text)
    return DEFAULT_ITERATIONS


def rate_of(record: evidence_store.Record | None) -> float | None:
    """Return the iterations for each CPU second in the kept samples of a record, or None when no sample gives them."""
    if record is None:
        return None
    iterations = cpu_s = 0.0
    for sample in record.samples:
        counted = dict(sample.facts).get("iterations", "")
        if counted.isdigit() and sample.quantity > 0:
            iterations += int(counted)
            cpu_s += sample.quantity
    return iterations / cpu_s if cpu_s > 0 and iterations > 0 else None


def seed_for(key: str, value: float, number: int) -> int:
    """Return the seed of a slice: 64 bits of the SHA-256 of the key, the evidence and the number of the slice."""
    return int.from_bytes(hashlib.sha256(f"{key}\n{value!r}\n{number}".encode()).digest()[:8], "big")


def slice_of(searched: Searched, slice_s: float = SLICE_S) -> Slice:
    """Return the next slice of a selected test, and count it (THE SLICES)."""
    words = program_words(searched.test)
    iterations = command_iterations(words)
    if searched.rate is not None:
        iterations = max(1, min(LARGEST_ITERATIONS, int(searched.rate * slice_s)))
    seed = seed_for(searched.key, searched.value, searched.number)
    searched.number += 1
    kept = [word for word in words[1:] if not word.startswith((SEED_OPTION, ITERATIONS_OPTION))]
    return Slice(seed, iterations, (words[0], *kept, f"{SEED_OPTION}{seed:#x}", f"{ITERATIONS_OPTION}{iterations}"))


def run_slice(command: Slice, test: ctest_reader.TestEntry, cwd: str, slice_s: float) -> proc.Completed | None:
    """Run one slice of a test, and return its result, or None when it ran for more time than its limit (THE SLICES).

    The slice gets the clean environment of a test and the property ENVIRONMENT of the test.
    """
    environment = dict(test_launcher.canonical_environment(proc.environment()))
    for word in test.words_of(ENVIRONMENT):
        name, equals, value = word.partition("=")
        if equals and name:
            environment[name] = value
    limit_s = max(LIMIT_FLOOR_S, slice_s * LIMIT_FACTOR)
    try:
        return proc.run(
            proc.Command(
                command.words,
                timeout_s=limit_s,
                cwd=cwd,
                env=environment,
                stdout=proc.Output.DISCARD,
                stderr=proc.Output.CAPTURE,
            )
        )
    except proc.TimedOut:
        return None


def last_error_line(completed: proc.Completed | None) -> str:
    """Return the first ERROR_LINE_LIMIT characters of the last line of the error output that is not empty, or ""."""
    if completed is None:
        return ""
    lines = [line.strip() for line in completed.err.splitlines() if line.strip()]
    return lines[-1][:ERROR_LINE_LIMIT] if lines else ""


def record_slice(
    keys: evidence_items.TestKeys,
    store: evidence_store.EvidenceStore,
    searched: Searched,
    command: Slice,
    completed: proc.Completed | None,
) -> bool:
    """Add the CPU seconds or keep the failure of one slice (THE SLICES), and return True for a failure."""
    keys.forget()
    rule, name = targets.PROPERTY.rule, searched.test.name
    after, is_current = keys.key_after_run(searched.test)
    facts = (("seed", f"{command.seed:#x}"), ("iterations", str(command.iterations)))
    if completed is not None and completed.exit == 0:
        if is_current:
            searched.key = after
            sample = evidence_store.Sample(evidence_store.now(), completed.cpu_s, completed.wall_s, facts)
            record = store.add(rule, name, after, sample)
            searched.value, searched.rate = record.value, rate_of(record) or searched.rate
        return False
    status = "at its time limit" if completed is None else f"with the status {completed.exit}"
    failure = evidence_store.Failure(
        evidence_store.now(),
        f"the property test stopped {status} for the seed {command.seed:#x}",
        command.words,
        (
            *facts,
            ("status", "limit" if completed is None else str(completed.exit)),
            ("error", last_error_line(completed)),
        ),
    )
    store.keep_failure(rule, name, after or searched.key, failure)
    searched.has_failed_now = True
    return True


def selection(
    keys: evidence_items.TestKeys, store: evidence_store.EvidenceStore, target: float, names: Sequence[str]
) -> list[Searched]:
    """Return each property test that the slices run (THE SELECTION).

    Complexity: the planner of assay.loop.affected for each member, and one read of the store for each item with a key.
    """
    by_name = {test.name: test for test in keys.tests}
    found = evidence_items.class_items(keys, targets.PROPERTY)
    return [
        Searched(by_name[candidate.item.name], candidate.item.key, candidate.value, target, rate_of(candidate.record))
        for candidate in evidence_items.pending(found, store, target, names)
    ]


def next_searched(chosen: Sequence[Searched]) -> Searched | None:
    """Return the pending test with the minimum evidence and with no slice that runs, or None (THE BUDGET)."""
    waiting = [searched for searched in chosen if searched.is_pending and not searched.is_running]
    return min(waiting, key=lambda searched: (searched.value, searched.test.name), default=None)


def run_slices(
    keys: evidence_items.TestKeys,
    store: evidence_store.EvidenceStore,
    chosen: list[Searched],
    budget_s: float,
    jobs: int,
    slice_s: float = SLICE_S,
) -> Outcome:
    """Run slices of the selected tests until each one gets its target or a failure, or the budget ends (THE BUDGET).

    Complexity: one run of a program for each slice.
    """
    from concurrent import futures

    started = time.monotonic()
    slices, cpu_s, failures = 0, 0.0, []
    reason = "each selected test has its target or a failure"
    cwd = str(keys.build_dir)
    with futures.ThreadPoolExecutor(max_workers=max(1, jobs)) as pool:
        running: dict[futures.Future[proc.Completed | None], tuple[Searched, Slice]] = {}
        while True:
            has_room = time.monotonic() - started + slice_s <= budget_s
            searched = next_searched(chosen) if has_room else None
            while searched is not None and len(running) < jobs:
                command = slice_of(searched, slice_s)
                searched.is_running = True
                running[pool.submit(run_slice, command, searched.test, cwd, slice_s)] = (searched, command)
                searched = next_searched(chosen)
            if not running:
                if not has_room and any(item.is_pending for item in chosen):
                    reason = f"the budget of {budget_s:g} s has no room for a slice of {slice_s:g} s"
                break
            done, _ = futures.wait(running, return_when=futures.FIRST_COMPLETED)
            for future in done:
                finished, command = running.pop(future)
                finished.is_running = False
                completed = future.result()
                slices += 1
                cpu_s += 0.0 if completed is None else completed.cpu_s
                if record_slice(keys, store, finished, command, completed):
                    failures.append(finished.test.name)
    return Outcome(slices, cpu_s, tuple(failures), reason, tuple(searched.test.name for searched in chosen))


def run(build_dir: Path, budget_s: float, is_release: bool, names: Sequence[str], jobs: int, target: float) -> ExitCode:
    """Select the property tests of a build, run the slices, and report (THE SELECTION, THE SLICES).

    Returns:
        OK when no slice found a failure, FINDINGS when one did, MISSING_INPUT when the build cannot be read
    """
    try:
        keys = evidence_items.TestKeys(build_dir)
    except affected.RunnerError as problem:
        check_report.write_line(f"loop evidence run search: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    test_launcher.stop_core_dumps()
    store = evidence_store.EvidenceStore()
    chosen = selection(keys, store, target, names)
    entry = "release" if is_release else "nightly"
    check_report.write_line(
        f"loop evidence run search: {len(chosen)} property tests of {build_dir} have less evidence than the {entry} "
        f"target {target:g} s.  The budget is {budget_s:g} s"
    )
    outcome = run_slices(keys, store, chosen, budget_s, jobs)
    check_report.write_line(
        f"loop evidence run search: {outcome.slices} slices added {outcome.cpu_s:.1f} CPU s and "
        f"{len(outcome.failures)} failures.  The slices stopped because {outcome.reason}"
    )
    for name in outcome.failures:
        check_report.write_line(f"loop evidence run search: the test {name} gave a failure, and its key keeps it")
    return ExitCode.FINDINGS if outcome.failures else ExitCode.OK
