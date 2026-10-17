"""evidence — the evidence of the sampled checks of the tree: its status, and the runners that add to it (`loop evidence`).

THE EVIDENCE
    One run of a test with a result that does not change is full evidence
    for its key, and `loop test` keeps that one result (assay.loop.affected).
    A sampled check gives only a part of its evidence in one run.  The store
    of assay.loop.evidence.store adds the evidence of each run to the
    evidence of the previous runs.  It keeps the evidence by the key of the
    inputs that the check read (assay.loop.evidence.items, THE KEYS).  Each
    class of sampled checks has a target in a measured rule of
    utils/registry.txt (assay.loop.evidence.targets).  A failure is evidence
    too, and it stays until its key changes.

THE SUBCOMMANDS
    status  Give the evidence of each item against its targets, and the
            remaining work (assay.loop.evidence.status).  With --json, print
            the rows as one JSON object, which an entry reads to schedule the
            remaining work in a time budget.  Without it, print a table and
            the findings of THE JUDGMENT of that module.
    run     Add evidence of one kind in a time budget.  The kind repetition
            runs each test of a build again (assay.loop.evidence.repetition).
            The kind search runs each property test of a build with new
            seeds (assay.loop.evidence.search).  The kind measurement runs
            each bench of a build, one at a time
            (assay.loop.evidence.measurement).  The kind coverage runs the
            rule gate-mutants on each header of include/
            (assay.loop.evidence.coverage).  The builds of the kind share
            the budget, in their sequence.  After the runs, the store goes
            back to its row of LIMITS of assay.core.cache.

Usage
    loop evidence status [--build-dir DIR]... [--json] [--release]
    loop evidence run KIND --budget-s SECONDS [--build-dir DIR]... [--item NAME]... [--release] [-j N]

    KIND         repetition, search, measurement or coverage

    --build-dir  A build directory that replaces the builds of the presets
                 file (assay.loop.evidence.items, THE BUILDS)
    --release    Use the release target, and not the nightly target
    --item       Run only the named items

Exit 0 when no item has a kept failure and no run found a failure.  Exit 1 when an item has a kept failure or a run
found one.  Exit 2 on a usage error, and 3 when an input is missing.
"""

import time
from pathlib import Path

from assay.core import cli, paths
from assay.core import report as check_report
from assay.core.exit import ExitCode

PROGRAM = "loop evidence"
# The kinds that `loop evidence run` runs.
RUNNERS = ("repetition", "search", "measurement", "coverage")


def status_main(directories: list[Path], as_json: bool, is_release: bool) -> int:
    """Give the status of the evidence of the tree (THE SUBCOMMANDS, status)."""
    from assay.loop.evidence import items as evidence_items
    from assay.loop.evidence import status as evidence_status
    from assay.loop.evidence import store as evidence_store
    from assay.loop.evidence import targets

    try:
        budgets = check_report.read_budgets()
    except (OSError, ValueError) as problem:
        check_report.write_line(f"{PROGRAM}: the registry cannot be read: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    found, problems = evidence_items.tree_items(paths.REPO_ROOT, directories)
    rows = evidence_status.rows_of(found, evidence_store.EvidenceStore(), budgets)
    if as_json:
        check_report.write_line(evidence_status.status_json(rows, problems))
        return ExitCode.OK
    check_report.write_line(evidence_status.table(rows))
    for text in problems:
        check_report.write_line(f"{PROGRAM}: {text}")
    status = ExitCode.OK
    findings = evidence_status.judged(rows, budgets, paths.LOOP.relative_to(paths.REPO_ROOT).as_posix(), is_release)
    for target in targets.TARGETS:
        chosen = [finding for finding in findings if finding.check == target.rule]
        if check_report.emit(chosen, target.rule, None):
            status = ExitCode.FINDINGS
    return status


def run_main(kind: str, directories: list[Path], budget_s: float, is_release: bool, names: list[str], jobs: int) -> int:
    """Add evidence of one kind in a time budget (THE SUBCOMMANDS, run)."""
    from assay.core import cache
    from assay.loop import tiers
    from assay.loop.evidence import coverage, measurement, repetition, search, targets
    from assay.loop.evidence import items as evidence_items
    from assay.loop.evidence import store as evidence_store

    sampled, run_build = {
        "repetition": (targets.STRESS, repetition.run),
        "search": (targets.PROPERTY, search.run),
        "measurement": (targets.MEASUREMENT, measurement.run),
        "coverage": (targets.COVERAGE, coverage.run),
    }[kind]
    try:
        budget = check_report.read_budgets().get(sampled.rule)
    except (OSError, ValueError) as problem:
        check_report.write_line(f"{PROGRAM}: the registry cannot be read: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    if budget is None:
        check_report.write_line(f"{PROGRAM}: the registry has no measured rule {sampled.rule}", is_error=True)
        return ExitCode.MISSING_INPUT
    builds = directories or [build for _, build in evidence_items.class_builds(sampled, tiers.read(paths.REPO_ROOT))]
    target = budget.error if is_release else budget.warn
    status = ExitCode.OK
    started = time.monotonic()
    for build in builds:
        remaining_s = budget_s - (time.monotonic() - started)
        if remaining_s <= 0:
            check_report.write_line(f"{PROGRAM}: the budget has no time for the build {build}")
            break
        status = max(status, run_build(build, remaining_s, is_release, names, jobs, target))
    store = evidence_store.EvidenceStore()
    if store.directory is not None:
        cache.hold_bound(store.directory, cache.LIMITS[evidence_store.STORE_NAME])
    return status


def main(argv: list[str]) -> int:
    """Parse the arguments and run one subcommand (Usage)."""
    parser = cli.parser(PROGRAM, "Give the evidence of the sampled checks, or add to it in a time budget.")
    commands = parser.add_subparsers(dest="command", required=True)
    status = commands.add_parser("status")
    status.add_argument("--build-dir", type=Path, action="append", default=[], dest="build_dirs")
    status.add_argument("--json", action="store_true")
    status.add_argument("--release", action="store_true")
    runner = commands.add_parser("run")
    runner.add_argument("kind", choices=RUNNERS)
    runner.add_argument("--budget-s", type=float, required=True)
    runner.add_argument("--build-dir", type=Path, action="append", default=[], dest="build_dirs")
    runner.add_argument("--item", action="append", default=[], dest="items")
    runner.add_argument("--release", action="store_true")
    runner.add_argument("-j", "--jobs", type=int, default=0)
    options = parser.parse_args(argv)
    directories = [directory.resolve() for directory in options.build_dirs]
    if options.command == "status":
        return status_main(directories, options.json, options.release)
    from assay.core import proc

    if options.budget_s <= 0 or options.jobs < 0:
        check_report.write_line(f"{PROGRAM}: give --budget-s a positive number, and -j a positive count", is_error=True)
        return ExitCode.USAGE
    return run_main(
        options.kind, directories, options.budget_s, options.release, options.items, options.jobs or proc.DEFAULT_JOBS
    )
