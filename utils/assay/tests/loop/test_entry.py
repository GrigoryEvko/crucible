"""The test of the nightly entry and of the release entry (assay.loop.entry).

`guard selftest tests.loop.test_entry` runs the test.  Each case writes one line.  Exit 0 when each case holds, else
1.

The cases hold the scope of each tier, and the jobs of a planted plan in their sequence.  They hold the budget of a
runner of evidence, the stop of a run at its deadline, and a step that cannot run.  They also hold the text of the
report and the usage of the command.  A run removes each report other than the last ones of its tier.
"""

import os
import tempfile
from pathlib import Path

from assay.core import testing
from assay.core.exit import ExitCode
from assay.loop import entry, heavy, plan, tiers
from assay.loop.evidence import status as evidence_status
from assay.loop.evidence import store as evidence_store
from assay.loop.evidence import targets as evidence_targets

GATE = tiers.Tier.GATE
NIGHTLY = tiers.Tier.NIGHTLY
RELEASE = tiers.Tier.RELEASE
CONFIGURE = tiers.PresetKind.CONFIGURE
TEST = tiers.PresetKind.TEST


def planted_presets() -> tiers.Tiers:
    """Return planted presets: a parse preset, presets of each tier, a filtered test preset and a preset with a profile."""
    configure = (
        tiers.PresetTier(CONFIGURE, "base", GATE, configuration="x86_64-debug-asan", build=Path("build")),
        tiers.PresetTier(CONFIGURE, "parse", GATE, configuration="x", build=Path("build-parse"), is_parse=True),
        tiers.PresetTier(CONFIGURE, "profiled", RELEASE, build=Path("build-profiled"), uses_profile=True),
        tiers.PresetTier(CONFIGURE, "night", NIGHTLY, build=Path("build-night")),
        tiers.PresetTier(CONFIGURE, "narrow", NIGHTLY, build=Path("build-narrow")),
        tiers.PresetTier(CONFIGURE, "hand", tiers.Tier.MANUAL, "a person runs it", build=Path("build-hand")),
    )
    tests = (
        tiers.PresetTier(TEST, "base", GATE, configure="base", is_inherited=True),
        tiers.PresetTier(TEST, "again", NIGHTLY, configure="base", is_repeated=True),
        tiers.PresetTier(TEST, "narrow-some", NIGHTLY, configure="narrow"),
        tiers.PresetTier(TEST, "profiled", RELEASE, configure="profiled", is_inherited=True),
    )
    return tiers.Tiers(configure, tests, ())


def planted_plan(presets: tiers.Tiers) -> plan.Plan:
    """Return a planted plan of the presets, the steps of STEPS and two harnesses."""
    harnesses = plan.each_items(plan.Kind.HARNESS, ("trace", "region"), plan.harness_file)
    return plan.Plan((*plan.preset_items(presets), *plan.step_items(), *harnesses))


def scope_cases(expect: testing.Expect) -> None:
    """Hold the scope of each entry: each tier of ORDER up to its own tier (THE SCOPE)."""
    expect(
        "the nightly entry runs the items of the gate and of the nightly tier",
        entry.scope_of(NIGHTLY) == (GATE, NIGHTLY),
    )
    expect("the release entry runs the items of each tier of ORDER", entry.scope_of(RELEASE) == tiers.ORDER)


def jobs_cases(expect: testing.Expect) -> None:
    """Hold the jobs of a planted plan in their sequence (THE WORK)."""
    presets = planted_presets()
    nightly = entry.jobs_of(planted_plan(presets), presets, NIGHTLY)
    release = entry.jobs_of(planted_plan(presets), presets, RELEASE)
    titles = [job.title for job in nightly]
    expect(
        f"the parse preset comes first, then the gate preset, then the nightly presets: {titles[:4]}",
        titles[:4] == ["parse parse", "tests base", "tests night", "preset-tests narrow"],
    )
    narrow = next(job for job in nightly if job.name == "narrow")
    expect("a preset with only a filtered test preset runs that test preset", narrow.test_presets == ("narrow-some",))
    expect(
        "a manual preset and a repeated test preset give no job",
        not any(job.name in ("hand", "again") for job in nightly),
    )
    expect("a release preset gives no job of the nightly entry", "tests profiled" not in titles)
    evidence = [job.name for job in nightly if job.kind is entry.JobKind.EVIDENCE]
    expect(
        f"the step evidence gives a runner of each class, in the sequence of TARGETS: {evidence}",
        evidence == [str(target.kind) for target in evidence_targets.TARGETS]
        and titles[-len(evidence) :] == [f"evidence {name}" for name in evidence],
    )
    expect(
        "a step of `loop evidence run` gives no step job",
        not any(job.step is not None and job.step.command[:3] == entry.EVIDENCE_WORDS for job in nightly),
    )
    campaigns = [job.item for job in nightly if job.step is not None and job.step.command[:2] == entry.FUZZ_WORDS]
    expect(f"a step with `each` gives one job of each item: {campaigns}", campaigns == ["region", "trace"])
    privileged = [job.name for job in nightly if job.step is not None and job.step.command[0] == entry.CTEST_WORD]
    expect(f"the nightly entry gives a job of the step bpf of ctest: {privileged}", privileged == ["bpf"])
    release_titles = [job.title for job in release]
    profiled = release_titles.index("tests profiled")
    steps = [index for index, job in enumerate(release) if job.kind is entry.JobKind.STEP]
    first_runner = next(index for index, job in enumerate(release) if job.kind is entry.JobKind.EVIDENCE)
    expect(
        "the release entry runs the preset with a profile after each step and before the runners",
        all(index < profiled for index in steps) and profiled < first_runner,
    )
    expect(
        "the release entry also runs the steps of the release tier",
        any(job.step is not None and job.step.tier is RELEASE for job in release),
    )


def row(item: str, value: float, unit_wall_s: float | None, key: str = "key") -> evidence_status.Row:
    """Return a planted row of `stress-passes` with a value, the wall time of one unit and a key."""
    return evidence_status.Row(
        evidence_targets.STRESS.rule, "repetition", "passes", item, "build", key, "", value, 10.0, 50.0, unit_wall_s
    )


def budget_cases(expect: testing.Expect) -> None:
    """Hold the time of a runner of evidence (THE BUDGET)."""
    known = [row("one", 4.0, 2.0), row("two", 10.0, 2.0)]
    expect(
        "a runner gets the time of the remaining units with the margin, when it is less than its part",
        entry.evidence_budget(known, False, 3600.0, 2) == max(entry.MINIMUM_RUNNER_S, 6 * 2.0 * entry.NEED_MARGIN),
    )
    expect(
        "the release target gives more remaining units",
        entry.evidence_budget(known, True, 36000.0, 1) == (46 + 40) * 2.0 * entry.NEED_MARGIN,
    )
    expect(
        "a runner never gets more than an equal part of the time that remains",
        entry.evidence_budget(known, True, 100.0, 4) == 25.0,
    )
    unknown = [row("one", 0.0, None)]
    expect("an item with no wall time gives the equal part", entry.evidence_budget(unknown, False, 900.0, 3) == 300.0)
    failed = evidence_status.Row(
        evidence_targets.STRESS.rule,
        "repetition",
        "passes",
        "bad",
        "build",
        "key",
        "",
        0.0,
        10.0,
        50.0,
        None,
        evidence_store.Failure("2026-10-17T00:00:00Z", "a race", ("ctest",), ()),
    )
    expect(
        "an item with a kept failure or no key is not open",
        not entry.is_open(failed, False) and not entry.is_open(row("bare", 0.0, None, key=""), False),
    )


def run_cases(expect: testing.Expect) -> None:
    """Hold the stop of a run at its deadline, and a step that cannot run (THE BUDGET, THE WORK)."""
    now = [0.0]

    def planted_run(_index: int, job: entry.Job, _deadline: float) -> entry.Outcome:
        now[0] += 60.0
        return entry.Outcome(job, entry.State.PASSED, "planted", 60.0)

    jobs = [entry.Job(entry.JobKind.STEP, name) for name in ("one", "two", "three")]
    found = entry.run_jobs(jobs, planted_run, 100.0, lambda: now[0])
    expect(
        "a job that cannot start before the deadline does not run",
        [outcome.state for outcome in found] == [entry.State.PASSED, entry.State.PASSED, entry.State.NOT_RUN],
    )
    with tempfile.TemporaryDirectory(prefix="entry-") as directory:
        runner = entry.Runner(Path(directory), NIGHTLY, 1, Path(directory), tiers.Tiers((), (), ()))
        step = plan.Step("probe", NIGHTLY, ("guard", "check", "--rule", "x", plan.BUILD_OPTION), "a probe", "absent")
        blocked = runner.step(entry.Job(entry.JobKind.STEP, "probe", step=step), Path(directory) / "probe", 1e18)
        expect("a step whose build is not configured is blocked", blocked.state is entry.State.BLOCKED)
        saved = os.environ.get("AFL_BIN")
        os.environ["AFL_BIN"] = directory
        try:
            fuzz_step = next(item for item in plan.STEPS if item.command[:2] == entry.FUZZ_WORDS)
            job = entry.Job(entry.JobKind.STEP, fuzz_step.name, step=fuzz_step, item="region")
            campaign = runner.campaign(job, Path(directory) / "fuzz", 1e18)
        finally:
            if saved is None:
                os.environ.pop("AFL_BIN", None)
            else:
                os.environ["AFL_BIN"] = saved
        expect(
            f"a campaign with no afl-fuzz is blocked with the reason: {campaign.summary}",
            campaign.state is entry.State.BLOCKED and campaign.summary.startswith(f"the host has no {directory}"),
        )
        bpf_step = next(item for item in plan.STEPS if item.command[0] == entry.CTEST_WORD)
        runner.launcher = Path(directory) / "crucible-caps"
        unready = runner.privileged(entry.Job(entry.JobKind.STEP, bpf_step.name, step=bpf_step), Path(directory), 1e18)
        expect(
            f"a step of ctest with no launcher is blocked with one line of the reason and the fix: {unready.summary}",
            unready.state is entry.State.BLOCKED
            and unready.summary.startswith(f"the launcher {runner.launcher} does not exist.  ")
            and "\n" not in unready.summary,
        )
        report = Path(directory) / "junit.xml"
        report.write_text(
            '<testsuite><testcase name="good"/><testcase name="bad"><failure/></testcase></testsuite>', encoding="utf-8"
        )
        expect("the failed tests of a JUnit report are the failures", entry.failed_names(report) == ["bad"])
        expect("a report that does not read gives no failure", entry.failed_names(Path(directory) / "none") == [])
    expect(
        "a campaign takes the available CPUs that are not reserved, at most CAMPAIGN_CORES",
        entry.campaign_cpus(range(40), (0, 1, 2)) == tuple(range(3, 3 + entry.CAMPAIGN_CORES)),
    )


def report_cases(expect: testing.Expect) -> None:
    """Hold the text of the report of a run, and the removal of each report other than the last ones (THE REPORT)."""
    failing = evidence_status.Row(
        evidence_targets.MEASUREMENT.rule,
        "measurement",
        "runs",
        "bench_noisy",
        "build-bench",
        "key",
        "",
        0.0,
        3.0,
        10.0,
        None,
        evidence_store.Failure("2026-10-17T00:00:00Z", "a report over the limit", ("loop", "bench"), ()),
    )
    rows = [row(f"test_{index}", 1.0, 1.0) for index in range(3)] + [row("keyless", 0.0, None, key=""), failing]
    job = entry.Job(entry.JobKind.TESTS, "base", Path("build"))
    outcomes = [
        entry.Outcome(job, entry.State.FAILED, "1 tests failed", 12.0, 10, 90, ("ctest -R '^bad$'",)),
        entry.Outcome(entry.Job(entry.JobKind.STEP, "gauge"), entry.State.NOT_RUN, "the budget ended before the job"),
    ]
    facts = entry.RunFacts(NIGHTLY, "abc", 0, "host", "t0", "t1", 30.0, 60.0, Path("/r"))
    text = entry.report_text(facts, outcomes, rows, ("the build build-x gives no items",))
    expect("the report gives each job with its state", "failed   tests base" in text and "not run  step gauge" in text)
    expect("the report gives the command of a failure", "again: ctest -R '^bad$'" in text)
    expect(
        "the report gives each open item: short, a kept failure with its command, and no key",
        "short    stress-passes test_0: 1 of 10 passes" in text
        and "failure  bench-runs bench_noisy" in text
        and "again: loop bench" in text
        and "no key   stress-passes keyless" in text,
    )
    expect(
        "the totals give the jobs, the tests and the open items",
        "2 jobs: 0 passed, 1 failed" in text
        and "10 tests ran, 90 with a current pass" in text
        and "5 open items" in text,
    )
    expect("the report gives the problems of the builds", "problem: the build build-x gives no items" in text)
    shown = entry.report_text(facts, outcomes, rows, (), 1)
    expect("the printed report gives at most the shown lines of a class", "more lines of stress-passes" in shown)
    with tempfile.TemporaryDirectory(prefix="entry-reports-") as directory:
        base = Path(directory)
        for index in range(entry.KEPT_RUNS + 2):
            (base / f"nightly-2026101{index:02d}").mkdir()
        (base / "release-20261001").mkdir()
        entry.remove_old(base, NIGHTLY)
        kept = sorted(path.name for path in base.iterdir())
    expect(
        "a run keeps the last reports of its tier and no report of a different tier is removed",
        len(kept) == entry.KEPT_RUNS + 1 and "release-20261001" in kept and "nightly-202610100" not in kept,
    )


def usage_cases(expect: testing.Expect) -> None:
    """Hold the usage errors of the command, and --plan on this tree (Usage)."""
    expect("a budget of 0 is a usage error", entry.main(NIGHTLY, ["--budget-s", "0"]) == ExitCode.USAGE)
    saved = os.environ.get(heavy.STEP_VARIABLE)
    os.environ[heavy.STEP_VARIABLE] = heavy.STEP_VALUE
    try:
        refused = entry.main(NIGHTLY, ["--plan"])
        os.environ.pop(heavy.STEP_VARIABLE)
        planned = entry.main(RELEASE, ["--plan"])
    finally:
        if saved is None:
            os.environ.pop(heavy.STEP_VARIABLE, None)
        else:
            os.environ[heavy.STEP_VARIABLE] = saved
    expect("the entry refuses to run in a heavy step", refused == ExitCode.USAGE)
    expect("--plan prints the jobs of this tree and runs nothing", planned == ExitCode.OK)


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (scope_cases, jobs_cases, budget_cases, run_cases, report_cases, usage_cases),
    )
