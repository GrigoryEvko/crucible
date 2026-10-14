"""The test of the runner of the kind measurement (assay.loop.evidence.measurement).

`guard selftest tests.loop.evidence.test_measurement` runs the test.  Each case writes one line.  Exit 0 when each case
holds, else 1.

The cases hold the judgment of a run: a run that counts, each reason of a run that does not count, and each failure.
They also run three planted benches through the runner, with a planted load of the host.  One gets its target, one
keeps its failure, and one has no room in the budget.  The planted benches are shell scripts.  The case then uses a
shell and taskset from PATH.
"""

import dataclasses
import json
import os
import shutil
import tempfile
from pathlib import Path

from assay.core import cache, proc, testing
from assay.loop.evidence import measurement, targets
from assay.loop.evidence import store as evidence_store
from assay.readers import bench as bench_reader

RULE = targets.MEASUREMENT.rule
CORE_FACTS = (("core", "0"), ("isolated", "no"))
REPORT = {"name": "try_push", "n": 100, "p50": 7.0, "p99": 9.0, "max": 20.0, "cv": 0.01, "wall_ns": 1e6, "drift_pct": 0}


def completed(status: int, out: str = "") -> proc.Completed:
    """Return a planted result of a command with an exit status and a standard output."""
    command = proc.Command(["planted"], timeout_s=proc.QUICK_S)
    return proc.Completed(command, status << 8, out.encode(), b"planted error\n", 1.0, 1.0, 0.0, 0)


def tail(cv: float) -> str:
    """Return the output of a planted bench with one report of a coefficient of variation."""
    return f"try_push: p50=7 ns\n{bench_reader.MARKER}\n{json.dumps([dict(REPORT, cv=cv)])}\n"


def judged_cases(expect: testing.Expect) -> None:
    """Hold the judgment of one run (THE RUN)."""

    def judge(result: proc.Completed | None, share: float | None) -> measurement.Judged:
        return measurement.judged(measurement.Ran(("planted",), result, 1.0, share), CORE_FACTS)

    counted = judge(completed(0, tail(0.01)), 2.0)
    facts = dict(counted.facts)
    expect(
        "a quiet run with a small variation counts, with its variation, its share and its core",
        counted.quantity == 1.0
        and not counted.failure
        and facts.get("cv") == "0.0100"
        and facts.get("others_pct") == "2.0"
        and facts.get("core") == "0",
    )
    for name, result, share, word in (
        ("a busy host", completed(0, tail(0.01)), 50.0, "other processes"),
        ("a host with no busy time", completed(0, tail(0.01)), None, "no busy time"),
        ("a large variation", completed(0, tail(0.2)), 2.0, "coefficient of variation"),
    ):
        verdict = judge(result, share)
        expect(
            f"a run on {name} does not count, and gives the reason",
            verdict.quantity == 0.0 and not verdict.failure and word in dict(verdict.facts).get("skip", ""),
        )
    plain = judge(completed(0, "plain text\n"), 2.0)
    expect(
        "a quiet run of a bench with no JSON tail counts",
        plain.quantity == 1.0 and dict(plain.facts).get("reports") == "0",
    )
    failures: tuple[tuple[str, proc.Completed | None, str], ...] = (
        ("a run at its time limit", None, "limit"),
        ("a run with the status 3", completed(3, tail(0.01)), "status 3"),
        ("a run with a JSON tail that does not read", completed(0, f"{bench_reader.MARKER}\n[{{\n"), "does not read"),
    )
    for name, failed, word in failures:
        verdict = judge(failed, 2.0)
        expect(f"{name} is a failure", verdict.quantity == 0.0 and word in verdict.failure)


def selection_cases(expect: testing.Expect) -> None:
    """Hold the selection of the next bench: the minimum runs of the call, then the minimum evidence (THE BUDGET)."""
    first = measurement.Measured("bench_a", Path("a"), "key-a", 0.0, 3.0, 1.0)
    second = measurement.Measured("bench_b", Path("b"), "key-b", 2.0, 3.0, 1.0)
    first.runs_now = 4
    expect(
        "a bench with less runs in the call goes first, also with more evidence",
        measurement.next_measured([first, second], 60.0) is second,
    )
    second.runs_now = 4
    expect(
        "of two benches with the same runs in the call, the bench with less evidence goes first",
        measurement.next_measured([first, second], 60.0) is first,
    )
    expect(
        "no bench goes when no run fits in the remaining time", measurement.next_measured([first, second], 0.5) is None
    )


def planted_bench(directory: Path, name: str, script: str, shell: str) -> Path:
    """Write a planted bench: an executable shell script, and return its path."""
    path = directory / name
    path.write_text(f"#!{shell}\n{script}\n", encoding="utf-8")
    path.chmod(0o755)
    return path


def run_cases(expect: testing.Expect) -> None:
    """Run three planted benches through the runner with a quiet planted host (THE SELECTION, THE BUDGET)."""
    shell = shutil.which("sh")
    has_tools = shell is not None and shutil.which("taskset") is not None
    expect("PATH gives a shell and taskset for the planted benches", has_tools)
    if shell is None or not has_tools:
        return
    core = str(min(os.sched_getaffinity(0)))
    with tempfile.TemporaryDirectory(prefix="evidence-measurement-") as directory, cache.scratch_root():
        root = Path(directory)
        store = evidence_store.EvidenceStore(root / "store")
        output = tail(0.01).replace("\n", "\\n")
        quiet = planted_bench(root, "bench_quiet", f"printf '{output}'", shell)
        failing = planted_bench(root, "bench_fail", "exit 3", shell)

        def quiet_run(path: Path, chosen_core: str, cwd: Path, limit_s: float) -> measurement.Ran:
            return dataclasses.replace(measurement.run_once(path, chosen_core, cwd, limit_s), others_pct=1.0)

        chosen = [
            measurement.Measured("bench_quiet", quiet, "key-quiet", 0.0, 2.0, 0.1),
            measurement.Measured("bench_fail", failing, "key-fail", 0.0, 2.0, 0.1),
            measurement.Measured("bench_long", quiet, "key-long", 0.0, 2.0, 1000.0),
        ]
        outcome = measurement.run_benches(store, chosen, 60.0, core, CORE_FACTS, root, quiet_run)
        passed = store.read(RULE, "key-quiet")
        failed = store.read(RULE, "key-fail")
        expect(
            f"a quiet bench gets its target in runs that count: {outcome}",
            passed is not None and passed.value == 2.0 and len(passed.samples) == 2 and outcome.counted == 2,
        )
        facts = {} if failed is None or failed.failure is None else dict(failed.failure.facts)
        expect(
            "a failing bench keeps its failure with its status and the command that runs it again",
            failed is not None
            and failed.failure is not None
            and facts.get("status") == "3"
            and failed.failure.rerun == ("taskset", "-c", core, str(failing))
            and outcome.failures == ("bench_fail",),
        )
        expect(
            "a bench whose run does not fit in the budget does not run, and the outcome gives the reason",
            store.read(RULE, "key-long") is None and "no room" in outcome.reason,
        )
        record = evidence_store.Record(RULE, "bench_quiet", "key", 1.0, passed.samples if passed is not None else ())
        expect(
            "the time of one run is the mean wall time of the samples, or the first estimate with no sample",
            measurement.run_time(None) == measurement.FIRST_RUN_S and 0.0 < measurement.run_time(record) < 60.0,
        )


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(__name__.removeprefix("assay."), argv, (judged_cases, selection_cases, run_cases))
