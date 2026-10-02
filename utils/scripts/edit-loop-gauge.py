#!/usr/bin/env python3
"""edit-loop-gauge — the cost of one edit of a base header: the build after it and the full test run after that.

Run this command before each stage gate.  It takes minutes, so no test runs it.

THE MEASUREMENT
    1. The gauge clones the source tree at its HEAD commit into
       WORK_DIR/tree, with `git clone --shared`.  A change in the work tree of
       the source is not in the copy, and the report gives the count of such
       files.  The gauge writes nothing into the source tree.
    2. It configures WORK_DIR/tree/build with the preset, and it builds the
       tree and runs each test one time, to make the build directory and the
       caches warm.  The steps after the clone get CRUCIBLE_CACHE_DIR in
       WORK_DIR/cache, so the stores of the fixtures and of the guards start
       empty and the stores of the user do not change.  ccache stays the
       ccache of the user.
    3. Each run makes a code change to a base header: it puts the statement
       `static_cast<void>(N);` with a new random N at the start of the body of
       foundation::detail::breakpoint() in include/foundation/Platform.h.  A
       touch or a comment does not change the preprocessed text, so ccache and
       the store of the fixtures would give each result again.  The parse
       tree of utils/scripts/tsast.py finds the body.  Then the run builds
       the tree and runs each test with ctest.
    4. After the runs, it measures a build with nothing to do and a second
       configure.
    5. It removes WORK_DIR, unless --keep is given or a step failed.

    --reuse DIR measures in the warm copy that an earlier run with --keep
    left in DIR.  It does no clone, no configure and no warm step, and each
    run starts from the header of the commit of the copy.

    Each step gives its wall time, its CPU time from getrusage, the share of
    the host CPU time that other processes used (utils/scripts/loop_history.py,
    THE HOST LOAD), and the user instructions of each process of the step,
    from one counter that each process inherits (utils/scripts/cost_meter.py,
    THE INSTRUCTION COUNT).  The judged value of each row is the median over
    the runs.

THE BUDGET ROWS
    edit-build-instructions and edit-test-instructions hold the error level.
    The instruction count of the build does not change with the load of the
    host.  A test that spins runs more instructions on a busy host, so the
    count of the tests changes by a few percent.
    edit-build-cpu and edit-test-cpu give a warning only when the step has an
    exact instruction count, and an error otherwise.  edit-build-wall and
    edit-test-wall hold the gate at WALL_JOBS jobs, so they are judged only
    in a measurement at that job count.  They are judged only when the host
    was quiet, too: other processes used at most QUIET_OTHERS_PCT percent of
    the host CPU time in the step, in each run.  Else the report gives the
    wall time and the reason, and no finding.
    Three rows give warnings only: configure-time for the first and the
    second configure, noop-build-time for the build with nothing to do, and
    build-peak-memory for the median of the peak concurrent memory of the
    edit builds.  For each build step, the report also gives the critical
    path (utils/scripts/loop_history.py, THE CRITICAL PATH).

THE REPORT
    The report prints each finding in the format of utils/scripts/check_report.py,
    at the path of the edited header.  With --warnings-dir, each row writes
    its warnings to DIR/ROW.txt.  With --record FILE, the gauge appends the
    result as one JSON line to FILE.

Usage
    edit-loop-gauge.py [--jobs N] [--runs K] [--preset NAME] [--work-dir DIR | --reuse DIR] [--keep] [--timeout S]
                       [--min-free-gb G] [--warnings-dir DIR] [--record FILE]
    edit-loop-gauge.py --self-test

Exit 0 with no error, 1 with an error finding or a step that failed, 2 on a usage error, too little free memory or a
failed self-test, 3 when the self-test cannot run because the parse-tree kit is not installed.
"""

from __future__ import annotations

import argparse
import json
import os
import secrets
import shutil
import socket
import statistics
import subprocess
import sys
import tempfile
import time
from dataclasses import asdict, dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
import cost_meter  # noqa: E402
import loop_history  # noqa: E402
import ninja_files  # noqa: E402
import throwaway_repo  # noqa: E402
import tsast  # noqa: E402
from repo_root import REPO_ROOT  # noqa: E402

EDIT_HEADER = Path("include/foundation/Platform.h")
EDIT_NAMESPACE = ("foundation", "detail")
EDIT_FUNCTION = "breakpoint"
QUIET_OTHERS_PCT = 10.0
# The job count of the gate.  The wall rows hold the wall times of a measurement at this job count.
WALL_JOBS = 192
DEFAULT_JOBS = WALL_JOBS
DEFAULT_RUNS = 3
DEFAULT_TIMEOUT_S = 600
DEFAULT_MIN_FREE_GB = 64.0
GIGA = 1e9
# For each judged step: the row of its instructions, of its CPU time and of its wall time.
STEP_ROWS = {"build": ("edit-build-instructions", "edit-build-cpu", "edit-build-wall"),
             "tests": ("edit-test-instructions", "edit-test-cpu", "edit-test-wall")}
NOT_RUNNABLE = 3


class GaugeError(Exception):
    """A condition that stops the gauge: a missing edit site, a clone of another commit, or too little memory."""


@dataclass(slots=True)
class StepResult:
    """The measures of one step, and for a build step its peak concurrent memory and its critical path."""

    name: str
    status: int
    wall_s: float
    cpu_s: float
    system_s: float
    others_pct: float | None
    instructions_g: float | None
    peak_memory_gb: float | None = None
    critical_path_s: float | None = None
    critical_path: tuple[str, ...] = ()


@dataclass(slots=True)
class Median:
    """The median of each measure of one step over the runs, and the largest other share of the runs."""

    wall_s: float
    cpu_s: float
    instructions_g: float | None
    worst_others_pct: float | None


# ── The edit ───────────────────────────────────────────────────────────────


def edit_offset(header: Path) -> int:
    """Return the byte offset just after the opening brace of the body of the edited function.

    The parse tree finds the function definition with the name EDIT_FUNCTION in the namespace EDIT_NAMESPACE.

    Args:
        header: The header

    Returns:
        The offset

    Raises:
        GaugeError: If the header has no such function with a body
    """
    tree = next(tsast.parse([header]))
    data = header.read_bytes()
    starts = [0]
    starts.extend(index + 1 for index, byte in enumerate(data) if byte == 0x0A)
    for node in tree.find("function_definition"):
        declarator = node.child_by_field("declarator")
        body = node.child_by_field("body")
        if declarator is None or body is None or tsast.leaf_name(declarator) != EDIT_FUNCTION:
            continue
        if tsast.namespace_path(node) != EDIT_NAMESPACE:
            continue
        row, column = body.start
        offset = starts[row] + column
        if data[offset:offset + 1] == b"{":
            return offset + 1
    raise GaugeError(f"{header} holds no definition of {'::'.join((*EDIT_NAMESPACE, EDIT_FUNCTION))}() with a body.  "
                     f"Give EDIT_HEADER, EDIT_NAMESPACE and EDIT_FUNCTION of utils/scripts/edit-loop-gauge.py a "
                     f"function of a base header that each translation unit includes")


def edited_text(original: bytes, offset: int, mark: int) -> bytes:
    """Return the header with the statement of one run after the opening brace of the body.

    Args:
        original: The bytes of the header before the first run
        offset: The offset of edit_offset()
        mark: The number of the run, which no earlier run used

    Returns:
        The bytes of the edited header
    """
    return original[:offset] + f" static_cast<void>({mark}LL);".encode() + original[offset:]


# ── The copy ───────────────────────────────────────────────────────────────


def git_environment() -> dict[str, str]:
    """Return the environment of this process with no repository variable, so git finds the repository of its path."""
    return {name: value for name, value in os.environ.items() if name not in throwaway_repo.REPOSITORY_VARIABLES}


def head_commit(tree: Path) -> str:
    """Return the HEAD commit of a work tree.

    Raises:
        GaugeError: If git cannot read the tree
    """
    result = subprocess.run(["git", "-C", str(tree), "rev-parse", "HEAD"], capture_output=True, text=True,
                            check=False, env=git_environment())
    if result.returncode != 0:
        raise GaugeError(f"git cannot read the HEAD commit of {tree}: {result.stderr.strip()}")
    return result.stdout.strip()


def clone_tree(source: Path, destination: Path) -> str:
    """Clone a source tree at its HEAD commit, with the objects of the source, and return the commit.

    Args:
        source: The source tree
        destination: The directory of the copy, which must not exist

    Returns:
        The commit of the copy

    Raises:
        GaugeError: If the clone fails or has another commit than the source
    """
    commit = head_commit(source)
    result = subprocess.run(["git", "clone", "--quiet", "--shared", str(source), str(destination)],
                            capture_output=True, text=True, check=False, env=git_environment())
    if result.returncode != 0:
        raise GaugeError(f"git clone of {source} failed: {result.stderr.strip()}")
    copied = head_commit(destination)
    if copied != commit:
        raise GaugeError(f"the clone {destination} has the commit {copied}, and the source has {commit}")
    return commit


# ── The steps ──────────────────────────────────────────────────────────────


def run_step(name: str, command: list[str], cwd: Path, environment: dict[str, str], logs: Path,
             build: Path | None = None) -> StepResult:
    """Run one step with its output in a log, and measure it.

    Args:
        name: The name of the step, also the name of its log
        command: The command
        cwd: The working directory
        environment: The environment of the command
        logs: The directory of the logs
        build: For a build step, the build directory, whose ninja log gives the edges that ran

    Returns:
        The measures
    """
    before = ninja_files.read_log(build) if build is not None else {}
    counter = cost_meter.open_instruction_counter()
    with (logs / f"{name}.log").open("w", encoding="utf-8") as log, loop_history.StepTimer() as timer:
        status = subprocess.run(command, cwd=cwd, env=environment, stdout=log, stderr=subprocess.STDOUT,
                                check=False).returncode
    instructions = cost_meter.read_instruction_counter(counter) if counter is not None else None
    result = timer.result
    step = StepResult(name, status, float(result["wall_s"]), float(result["cpu_s"]), float(result["system_s"]),
                      None if result["others_pct"] is None else float(result["others_pct"]),
                      None if instructions is None else round(instructions / GIGA, 1))
    if build is not None:
        facts = loop_history.summarize_build(build, loop_history.load_graph(build), before, ninja_files.read_log(build),
                                             timer.started_epoch)
        step.peak_memory_gb = loop_history.number(facts, "peak_memory_gb")
        step.critical_path_s = loop_history.number(facts, "critical_path_s")
        path = facts.get("critical_path")
        step.critical_path = tuple(str(item) for item in path) if isinstance(path, list) else ()
    return step


def median_of(results: list[StepResult]) -> Median:
    """Return the median of each measure of one step over the runs.

    Args:
        results: The measures of the step in each run

    Returns:
        The medians.  The instructions are None when one run has no exact count
    """
    counts = [result.instructions_g for result in results]
    shares = [result.others_pct for result in results]
    return Median(statistics.median(result.wall_s for result in results),
                  statistics.median(result.cpu_s for result in results),
                  None if None in counts else statistics.median(count for count in counts if count is not None),
                  None if None in shares else max(share for share in shares if share is not None))


def judge(medians: dict[str, Median], budgets: dict[str, check_report.Budget], place: str, jobs: int) -> tuple[
        list[check_report.Finding], list[str]]:
    """Compare the medians of the build and the tests with their rows.

    Args:
        medians: The median of the build and of the tests
        budgets: The budget table
        place: The path that a finding names
        jobs: The job count of the measurement

    Returns:
        The findings, and one note for each value that the gauge did not judge
    """
    findings: list[check_report.Finding] = []
    notes: list[str] = []

    def compare(row: str, value: float, unit: str, cap_to_warning: str = "") -> None:
        budget = budgets[row]
        level = check_report.classify(value, budget)
        if level is None:
            return
        threshold = budget.error if level == "error" else budget.warn
        message = f"the median of the runs is {value:g} {unit}, over the {level} threshold of {threshold:g} {unit}"
        if level == "error" and cap_to_warning:
            level, message = "warning", f"{message}.  {cap_to_warning}"
        findings.append(check_report.judged(level, place, 0, row, message))

    for step, (instruction_row, cpu_row, wall_row) in STEP_ROWS.items():
        median = medians[step]
        if median.instructions_g is not None:
            compare(instruction_row, median.instructions_g, "G instructions")
            compare(cpu_row, median.cpu_s, "s", f"The row {instruction_row} holds the error level, because the step has "
                                                f"an exact instruction count")
        else:
            notes.append(f"the {step} step has no exact instruction count, so {cpu_row} holds the error level")
            compare(cpu_row, median.cpu_s, "s")
        share = median.worst_others_pct
        if jobs != WALL_JOBS:
            notes.append(f"the {step} wall time {median.wall_s:g} s is not judged: the measurement ran {jobs} jobs, and "
                         f"{wall_row} holds the gate at {WALL_JOBS} jobs")
        elif share is not None and share <= QUIET_OTHERS_PCT:
            compare(wall_row, median.wall_s, "s")
        else:
            shown = "an unknown share" if share is None else f"{share:g} %"
            notes.append(f"the {step} wall time {median.wall_s:g} s is not judged: other processes used {shown} of the "
                         f"host CPU time in one run, more than {QUIET_OTHERS_PCT:g} %")
    return findings, notes


def judge_others(results: list[StepResult], budgets: dict[str, check_report.Budget],
                 place: str) -> list[check_report.Finding]:
    """Return the warnings of the configure steps, of the build with nothing to do, and of the peak memory.

    Args:
        results: The measures of each step
        budgets: The budget table
        place: The path that a finding names

    Returns:
        The warnings
    """
    findings: list[check_report.Finding] = []
    for result in results:
        if result.name in ("configure", "reconfigure"):
            findings += loop_history.warning_over(budgets, "configure-time", result.wall_s, "s",
                                                  f"the step {result.name}", place)
        elif result.name == "noop-build":
            findings += loop_history.warning_over(budgets, "noop-build-time", result.wall_s, "s",
                                                  "the build with no edge to run", place)
    peaks = [result.peak_memory_gb for result in results
             if result.name.startswith("edit-build-") and result.peak_memory_gb is not None]
    if peaks:
        findings += loop_history.warning_over(budgets, "build-peak-memory", statistics.median(peaks), "GB",
                                              "the median of the edit builds", place)
    return findings


def available_gb() -> float | None:
    """Return the available memory of the host in GB, from /proc/meminfo, or None when it cannot be read."""
    try:
        with open("/proc/meminfo", encoding="ascii") as handle:
            for line in handle:
                if line.startswith("MemAvailable:"):
                    return int(line.split()[1]) / (1024 * 1024)
    except (OSError, ValueError, IndexError):
        return None
    return None


def table(results: list[StepResult], medians: dict[str, Median]) -> str:
    """Return the measures of each step and the medians as a table."""
    rows = [("step", "status", "wall s", "CPU s", "system s", "others %", "G instr")]
    for result in results:
        rows.append((result.name, str(result.status), f"{result.wall_s:.1f}", f"{result.cpu_s:.0f}",
                     f"{result.system_s:.0f}", loop_history.cell(result.others_pct), loop_history.cell(result.instructions_g)))
    for step, median in medians.items():
        rows.append((f"median edit {step}", "", f"{median.wall_s:.1f}", f"{median.cpu_s:.0f}", "",
                     loop_history.cell(median.worst_others_pct), loop_history.cell(median.instructions_g)))
    widths = [max(len(row[index]) for row in rows) for index in range(len(rows[0]))]
    return "\n".join("  ".join(text.ljust(width) if index == 0 else text.rjust(width)
                               for index, (text, width) in enumerate(zip(row, widths, strict=True))) for row in rows)


def original_header(tree: Path) -> bytes:
    """Return the edited header as the commit of a copy holds it.

    Raises:
        GaugeError: If git cannot read the header
    """
    result = subprocess.run(["git", "-C", str(tree), "show", f"HEAD:{EDIT_HEADER.as_posix()}"], capture_output=True,
                            check=False, env=git_environment())
    if result.returncode != 0:
        raise GaugeError(f"git cannot read {EDIT_HEADER} of the commit of {tree}: {result.stderr.decode().strip()}")
    return result.stdout


def measure(source: Path, work: Path, options: argparse.Namespace) -> tuple[int, bool]:
    """Clone, warm, edit, build and test, and report.

    Args:
        source: The source tree
        work: The work directory: a new one, or with --reuse the work directory of an earlier run with --keep
        options: The options of the command line

    Returns:
        The exit status, and whether a step failed
    """
    started = loop_history.iso_time(time.time())
    state = loop_history.git_state(source)
    logs = work / "logs"
    tree = work / "tree"
    if options.reuse is None:
        work.mkdir(parents=True)
        logs.mkdir()
        commit = clone_tree(source, tree)
    else:
        commit = head_commit(tree)
    build = tree / "build"
    environment = dict(git_environment(), CRUCIBLE_CACHE_DIR=str(work / "cache"))
    jobs = str(options.jobs)
    print(f"edit-loop-gauge: host {socket.gethostname()}, commit {commit[:12]}, {state.get('changed_files')} changed "
          f"file(s) in the source work tree that the copy does not hold, preset {options.preset}, {jobs} jobs, "
          f"{options.runs} run(s), work directory {work}")
    build_command = ["cmake", "--build", str(build), "-j", jobs]
    test_command = ["ctest", "--test-dir", str(build), "-j", jobs, "--timeout", str(options.timeout),
                    "--no-tests=error"]
    configure_command = ["cmake", "--preset", options.preset, "-B", str(build)]
    results: list[StepResult] = []

    def step(name: str, command: list[str]) -> StepResult:
        result = run_step(name, command, tree, environment, logs, build if command is build_command else None)
        results.append(result)
        path = ""
        if result.critical_path:
            path = (f", peak {loop_history.cell(result.peak_memory_gb)} GB, critical path {result.critical_path_s:g} s: "
                    f"{' -> '.join(result.critical_path)}")
        print(f"edit-loop-gauge: {name}: status {result.status}, {result.wall_s:.1f} s wall, {result.cpu_s:.0f} s CPU, "
              f"{loop_history.cell(result.instructions_g)} G instructions, others "
              f"{loop_history.cell(result.others_pct)} %{path}", flush=True)
        return result

    failed = False
    if options.reuse is None:
        failed = step("configure", configure_command).status != 0
        failed = failed or step("warm-build", build_command).status != 0
        if not failed:
            step("warm-tests", test_command)
    header = tree / EDIT_HEADER
    original = original_header(tree)
    header.write_bytes(original)
    offset = edit_offset(header)
    runs: dict[str, list[StepResult]] = {"build": [], "tests": []}
    for run in range(1, options.runs + 1):
        if failed:
            break
        header.write_bytes(edited_text(original, offset, secrets.randbits(62)))
        built = step(f"edit-build-{run}", build_command)
        runs["build"].append(built)
        if built.status != 0:
            failed = True
            break
        runs["tests"].append(step(f"edit-tests-{run}", test_command))
    if not failed:
        step("noop-build", build_command)
        step("reconfigure", configure_command)
    failed = failed or any(result.status != 0 for result in results)
    if not runs["build"] or len(runs["tests"]) != len(runs["build"]):
        print(f"edit-loop-gauge: a step failed before the runs completed.  The logs are in {logs}.")
        return 1, True
    medians = {step_name: median_of(step_results) for step_name, step_results in runs.items()}
    print(table(results, medians))
    budgets = check_report.read_budgets()
    findings, notes = judge(medians, budgets, str(EDIT_HEADER), options.jobs)
    findings += judge_others(results, budgets, str(EDIT_HEADER))
    for note in notes:
        print(f"edit-loop-gauge: {note}")
    status = 0
    for row in sorted({finding.check for finding in findings}):
        status |= check_report.emit([finding for finding in findings if finding.check == row], row,
                                    options.warnings_dir)
    if options.record is not None:
        record = {"time": started, "commit": commit, "changed_files": state.get("changed_files"),
                  "host": socket.gethostname(), "jobs": options.jobs, "runs": options.runs, "preset": options.preset,
                  "steps": [asdict(result) for result in results],
                  "medians": {name: asdict(median) for name, median in medians.items()}}
        with options.record.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(record, separators=(",", ":")) + "\n")
    if failed:
        print(f"edit-loop-gauge: a step failed.  The logs are in {logs}.")
        return 1, True
    return status, False


# ── The self-test ──────────────────────────────────────────────────────────

PLANTED_HEADER = """#pragma once
namespace other {
inline void breakpoint() noexcept {}
}
namespace foundation::detail {
inline void breakpoint_if_debugging() noexcept {}
[[gnu::always_inline]] inline void breakpoint() noexcept { __builtin_trap(); }
}
"""


def self_test() -> int:
    """Check the edit, the median, the judgment and the copy on planted input.

    Returns:
        0 when every case holds, 2 otherwise, 3 when the parse-tree kit is not installed
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="edit-loop-gauge-") as work:
        root = Path(work)
        header = root / "Platform.h"
        header.write_bytes(PLANTED_HEADER.encode())
        try:
            offset = edit_offset(header)
        except tsast.KitMissing:
            print("edit-loop-gauge --self-test: the parse-tree kit is not installed; run utils/scripts/install-tree-sitter.sh")
            return NOT_RUNNABLE
        edited = edited_text(header.read_bytes(), offset, 42).decode()
        expect("the edit goes into the body of foundation::detail::breakpoint, and not into another breakpoint",
               "inline void breakpoint() noexcept { static_cast<void>(42LL); __builtin_trap(); }" in edited
               and "namespace other {\ninline void breakpoint() noexcept {}" in edited)
        header.write_text(PLANTED_HEADER.replace("inline void breakpoint() noexcept { __builtin_trap(); }",
                                                 "inline void breakpoint_moved() noexcept {}"), encoding="utf-8")
        try:
            edit_offset(header)
            expect("a header with no edit site stops the gauge", False)
        except GaugeError:
            expect("a header with no edit site stops the gauge", True)

        source = root / "source"
        source.mkdir()
        throwaway_repo.init(source)
        (source / "tracked.txt").write_text("one\n", encoding="utf-8")
        identity = dict(git_environment(), GIT_AUTHOR_NAME="gauge", GIT_AUTHOR_EMAIL="gauge@invalid",
                        GIT_COMMITTER_NAME="gauge", GIT_COMMITTER_EMAIL="gauge@invalid")
        subprocess.run(["git", "-C", str(source), "add", "tracked.txt"], check=True, env=identity)
        subprocess.run(["git", "-C", str(source), "commit", "-q", "-m", "one"], check=True, env=identity)
        (source / "tracked.txt").write_text("changed in the work tree\n", encoding="utf-8")
        copy = root / "copy"
        commit = clone_tree(source, copy)
        expect("the copy holds the HEAD commit of the source and not a change of its work tree",
               commit == head_commit(source) and (copy / "tracked.txt").read_text(encoding="utf-8") == "one\n")
        (copy / "tracked.txt").write_text("edited in the copy\n", encoding="utf-8")
        expect("an edit of the copy does not change the source",
               (source / "tracked.txt").read_text(encoding="utf-8") == "changed in the work tree\n")

    def result(name: str, wall: float, cpu: float, share: float | None, count: float | None) -> StepResult:
        return StepResult(name, 0, wall, cpu, cpu / 10, share, count)

    median = median_of([result("b", 10, 100, 2, 50), result("b", 30, 300, 4, 70), result("b", 20, 200, 12, 60)])
    expect("median_of takes the median of each measure and the largest other share",
           median == Median(20, 200, 60, 12))
    expect("median_of gives no instruction count when one run has none",
           median_of([result("b", 1, 1, 1, 5), result("b", 1, 1, 1, None)]).instructions_g is None)

    def rows(warn: float, error: float) -> dict[str, check_report.Budget]:
        names = [name for triple in STEP_ROWS.values() for name in triple]
        return {name: check_report.Budget(name, warn, error, "u", "m") for name in names}

    with check_report.github_actions(False):
        quiet = {"build": Median(20, 200, 60, 1), "tests": Median(20, 200, 60, 1)}
        found, notes = judge(quiet, rows(10, 100), "h", WALL_JOBS)
        expect("on a quiet host, each value over the warning threshold gives a warning, also the wall time",
               sorted((f.check, f.level) for f in found) == sorted((name, "warning") for triple in STEP_ROWS.values()
                                                                   for name in triple) and not notes)
        found, _ = judge(quiet, rows(10, 50), "h", WALL_JOBS)
        levels = {f.check: f.level for f in found}
        expect("an instruction count over the error threshold is an error, and the CPU time is a warning that says why",
               levels["edit-build-instructions"] == "error" and levels["edit-build-cpu"] == "warning"
               and any("holds the error level" in f.message for f in found if f.check == "edit-build-cpu"))
        no_count = {"build": Median(20, 200, None, 1), "tests": Median(20, 200, 60, 1)}
        found, notes = judge(no_count, rows(10, 50), "h", WALL_JOBS)
        expect("with no instruction count, the CPU time holds the error level",
               {f.check: f.level for f in found}["edit-build-cpu"] == "error"
               and any("no exact instruction count" in note for note in notes))
        busy = {"build": Median(20, 1, 1, 25), "tests": Median(20, 1, 1, None)}
        found, notes = judge(busy, rows(10, 15), "h", WALL_JOBS)
        expect("on a busy host, the wall time gives no finding and a note",
               not found and len([note for note in notes if "is not judged" in note]) == 2)
        found, notes = judge(quiet, rows(10, 15), "h", WALL_JOBS // 8)
        expect("at another job count, the wall time gives no finding and a note that names the job count",
               not any(f.check.endswith("-wall") for f in found)
               and len([note for note in notes if f"{WALL_JOBS // 8} jobs" in note]) == 2)
        other_rows = {row: check_report.Budget(row, limit, limit, "u", "m")
                      for row, limit in (("configure-time", 10.0), ("noop-build-time", 0.5),
                                         ("build-peak-memory", 100.0))}
        steps = [result("configure", 12.0, 1, 1, None), result("noop-build", 0.3, 0, 1, None),
                 result("reconfigure", 6.0, 1, 1, None)]
        for name, peak in (("edit-build-1", 90.0), ("edit-build-2", 120.0), ("edit-build-3", 130.0)):
            steps.append(result(name, 20, 200, 1, 60))
            steps[-1].peak_memory_gb = peak
        found = judge_others(steps, other_rows, "h")
        expect("judge_others warns on a slow configure and on the median peak memory of the edit builds, and not on a "
               "fast build with nothing to do",
               sorted(f.check for f in found) == ["build-peak-memory", "configure-time"]
               and all(f.level == "warning" for f in found) and any("120 GB" in f.message for f in found))

    if failures:
        print(f"edit-loop-gauge --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("edit-loop-gauge --self-test: every case holds.")
    return 0


# ── Main ───────────────────────────────────────────────────────────────────


def main(argv: list[str]) -> int:
    """Parse the arguments and run the gauge or the self-test.

    Args:
        argv: The arguments

    Returns:
        The exit status
    """
    if argv == ["--self-test"]:
        return self_test()
    parser = argparse.ArgumentParser(description="Measure the build and the tests after one edit of a base header.")
    parser.add_argument("--jobs", type=int, default=DEFAULT_JOBS)
    parser.add_argument("--runs", type=int, default=DEFAULT_RUNS)
    parser.add_argument("--preset", default="default")
    parser.add_argument("--work-dir", type=Path, default=None)
    parser.add_argument("--reuse", type=Path, default=None,
                        help="measure in the work directory of an earlier run with --keep, with no warm step")
    parser.add_argument("--keep", action="store_true", help="keep the work directory after the measurement")
    parser.add_argument("--timeout", type=int, default=DEFAULT_TIMEOUT_S, help="the ctest limit of one test, in s")
    parser.add_argument("--min-free-gb", type=float, default=DEFAULT_MIN_FREE_GB)
    parser.add_argument("--record", type=Path, default=None, help="append the result as one JSON line to this file")
    check_report.add_arguments(parser)
    options = parser.parse_args(argv)
    if options.jobs < 1 or options.runs < 1 or options.timeout < 1:
        print("edit-loop-gauge: --jobs, --runs and --timeout take a positive number.", file=sys.stderr)
        return 2
    free = available_gb()
    if free is not None and free < options.min_free_gb:
        print(f"edit-loop-gauge: the host has {free:.0f} GB of available memory, less than --min-free-gb "
              f"{options.min_free_gb:g}.  Wait until other work ends, or give a smaller --jobs and --min-free-gb.",
              file=sys.stderr)
        return 2
    if options.reuse is not None:
        if options.work_dir is not None:
            print("edit-loop-gauge: give --work-dir or --reuse, not the two.", file=sys.stderr)
            return 2
        work = options.reuse.resolve()
        if not (work / "tree" / "build" / "CMakeCache.txt").is_file() or not (work / "logs").is_dir():
            print(f"edit-loop-gauge: {work} is not the work directory of an earlier run with --keep: it has no "
                  f"tree/build/CMakeCache.txt or no logs directory.", file=sys.stderr)
            return 2
    else:
        work = (options.work_dir or REPO_ROOT / ".worktrees" / f"edit-loop-gauge-{os.getpid()}").resolve()
        if work.exists():
            print(f"edit-loop-gauge: the work directory {work} exists.  Give another --work-dir.", file=sys.stderr)
            return 2
    status, should_keep = 1, True
    try:
        status, should_keep = measure(REPO_ROOT, work, options)
    except GaugeError as problem:
        print(f"edit-loop-gauge: {problem}", file=sys.stderr)
    finally:
        if work.exists() and not (options.keep or should_keep):
            shutil.rmtree(work)
        elif work.exists():
            print(f"edit-loop-gauge: the work directory {work} stays.  Remove it when you no longer need its logs.")
    return status


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
