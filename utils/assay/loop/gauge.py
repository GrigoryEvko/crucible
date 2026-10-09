"""gauge — the cost of one edit of a base header: the build after it and the full test run after that.

Run `loop gauge` before each stage gate.  It takes minutes, so no test runs it.
`loop gauge --sample` measures a sample of translation units instead
(utils/assay/loop/gauge_sample.py), and `loop gauge --cold` measures one
clean build and one test run with no compiler cache
(utils/assay/loop/gauge_cold.py).

THE MEASUREMENT
    1. The gauge clones the source tree at its HEAD commit into
       WORK_DIR/tree, with `git clone --shared`.  A change in the work tree of
       the source is not in the copy, and the report gives the count of such
       files.  The gauge writes nothing into the source tree.  With no
       --work-dir, WORK_DIR is a directory of the process in the work root of
       assay.core.cache (THE WORK ROOT of that module).  That root is not in
       a checkout, and the copy adds no second CLAUDE.md in the repository.
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
       tree of utils/assay/readers/tsast.py finds the body (THE EDIT).  Then
       the run builds the tree and runs each test with ctest.
    4. After the runs, it measures a build with nothing to do and a second
       configure (closing_steps()).  Then it runs the rule reconfigure-noop
       with the master guard of the copy.  The rule configures the build
       again, and a compile in one of the two builds after it fails the step.
       A ctest test cannot configure the build that runs it.  The gauge then
       runs this check before each stage gate.
    5. It removes WORK_DIR, unless --keep is given or a step failed.

    --reuse DIR measures in the warm copy that an earlier run with --keep
    left in DIR.  It does no clone, no configure and no warm step, and each
    run starts from the header of the commit of the copy.

    Each step gives its wall time, its CPU time from getrusage, the share of
    the host CPU time that other processes used (utils/assay/loop/history.py,
    THE HOST LOAD), and the user instructions of each process of the step,
    from one counter that each process inherits (utils/assay/core/meter.py,
    THE INSTRUCTION COUNT).  The judged value of each measured rule is the
    median over the runs.

THE EDIT
    The edit writes the shape that the .clang-format of the tree gives.  The
    statement goes on a line of its own, with the indent of the line of the
    definition and one more indent step.  When the body is on one line, each
    statement of the body goes on a line of its own too.  clang-format puts a
    body on the line of its declaration only when the body holds one
    statement.  Before each build, check_edit_format() runs the pinned
    clang-format of the rule clang-format on the edited header.  When the
    formatter changes the header, the gauge stops before the build.  Each run
    of the test guard_clang_format of the copy then gives a failure.  With no
    install of the pin, the gauge prints a note, and that test does not apply.

THE TESTS WITH A FAILURE
    Each test step gives ctest the option --output-junit, with a report in
    the directory of the logs.  When the status of a step is not 0, the gauge
    immediately prints FAILED and the status of the step.  For a test step,
    it also prints the name of each test with the status FAIL in the report.
    Then it prints the name of each test with the status NOTRUN, which ctest
    did not start (assay.readers.ctest, THE STATUS OF A TEST).  A test with
    the status SKIPPED gave its own skip, and it is no failure.  At the end,
    the gauge prints each such step again, and it exits with 1.  A failure in
    the warm test run is a failure of the commit before the edit.

THE MEASURED RULES
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
    Three rules give warnings only: configure-time for the first and the
    second configure, noop-build-time for the build with nothing to do, and
    build-peak-memory for the median of the peak concurrent memory of the
    edit builds.  For each build step, the report also gives the critical
    path (utils/assay/loop/history.py, THE CRITICAL PATH).

THE REPORT
    The report prints each finding in the format of utils/assay/core/report.py,
    at the path of the edited header.  With --warnings-dir, each rule writes
    its warnings to DIR/RULE.txt.  With --record FILE, the gauge appends the
    result as one JSON line to FILE.  The gauge judges that record with
    judged_record().  A record of a different run then gives the same
    findings.  read_records() reads the lines of such a file, and the witness
    of each measured rule of the gauge judges planted records with the two
    functions (assay.guard.engines.buildsys.held).

Usage
    loop gauge [--jobs N] [--runs K] [--preset NAME] [--work-dir DIR | --reuse DIR] [--keep] [--timeout S]
               [--min-free-gb G] [--warnings-dir DIR] [--record FILE]
    loop gauge --sample [ARG]...
    loop gauge --cold [ARG]...
    guard selftest loop.gauge

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
import sys
import tempfile
import time
from collections.abc import Callable, Mapping
from dataclasses import asdict, dataclass
from pathlib import Path

from assay.core import cache, git
from assay.core import meter as cost_meter
from assay.core import proc
from assay.core import report as check_report
from assay.core.paths import REPO_ROOT
from assay.loop import gauge_cold, gauge_sample
from assay.loop import history as loop_history
from assay.readers import cmake as cmake_reader
from assay.readers import ctest as ctest_reader
from assay.readers import ninja as ninja_files
from assay.readers import tsast
from assay.setup import pins as cmake_pin

EDIT_HEADER = Path("include/foundation/Platform.h")
EDIT_NAMESPACE = ("foundation", "detail")
EDIT_FUNCTION = "breakpoint"
# The bits of the random mark of each run, and the mark with the most digits.
MARK_BITS = 62
LONGEST_MARK = (1 << MARK_BITS) - 1
# The IndentWidth of the .clang-format of the tree: the indent step of a statement in its block.
INDENT_WIDTH = 4
QUIET_OTHERS_PCT = 10.0
# The job count of the gate.  The wall rules hold the wall times of a measurement at this job count.
WALL_JOBS = 192
DEFAULT_JOBS = WALL_JOBS
DEFAULT_RUNS = 3
DEFAULT_TIMEOUT_S = 600
DEFAULT_MIN_FREE_GB = 64.0
GIGA = 1e9
# For each judged step: the measured rule of its instructions, of its CPU time and of its wall time.
STEP_ROWS = {"build": ("edit-build-instructions", "edit-build-cpu", "edit-build-wall"),
             "tests": ("edit-test-instructions", "edit-test-cpu", "edit-test-wall")}
NOT_RUNNABLE = 3
# The rule that configures the build again and refuses a compile after it, and the master of the copy that runs it.
RECONFIGURE_RULE = "reconfigure-noop"
GUARD_MASTER = Path("utils/bin/guard")


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


@dataclass(frozen=True, slots=True)
class GaugeRecord:
    """The part of a record of --record that the gauge judges: the job count, each step, and the median of each run."""

    jobs: int
    steps: tuple[StepResult, ...]
    medians: Mapping[str, Median]


# ── The edit ───────────────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class EditSite:
    """The body of the edited function: the offsets of its first and its last character, and the indent of the line of
    its definition."""

    open_brace: int
    close_brace: int
    indent: bytes


def edit_site(header: Path) -> EditSite:
    """Return the body of the edited function (THE EDIT).

    The parse tree finds the function definition with the name EDIT_FUNCTION in the namespace EDIT_NAMESPACE.  The
    indent is the white space at the start of the first line of the definition.

    Args:
        header: The header

    Returns:
        The site of the edit

    Raises:
        GaugeError: If the header has no such function with a body, or the body holds no statement
    """
    tree = next(tsast.parse([header]))
    data = header.read_bytes()
    starts = [0]
    starts.extend(index + 1 for index, byte in enumerate(data) if byte == 0x0A)
    name = "::".join((*EDIT_NAMESPACE, EDIT_FUNCTION))
    for node in tree.find("function_definition"):
        declarator = node.child_by_field("declarator")
        body = node.child_by_field("body")
        if declarator is None or body is None or tsast.leaf_name(declarator) != EDIT_FUNCTION:
            continue
        if tsast.namespace_path(node) != EDIT_NAMESPACE:
            continue
        open_brace = starts[body.start[0]] + body.start[1]
        close_brace = starts[body.end[0]] + body.end[1] - 1
        if data[open_brace:open_brace + 1] != b"{" or data[close_brace:close_brace + 1] != b"}":
            continue
        if not tsast.non_comment_children(body):
            raise GaugeError(f"the body of {name}() in {header} holds no statement.  clang-format puts a body of one "
                             f"statement on the line of its declaration, and the edit writes a body of more lines.  "
                             f"Give the function a statement, or give the gauge a different edit site")
        line_start = starts[node.start[0]]
        indent_end = line_start
        while data[indent_end:indent_end + 1] in (b" ", b"\t"):
            indent_end += 1
        return EditSite(open_brace, close_brace, data[line_start:indent_end])
    raise GaugeError(f"{header} holds no definition of {name}() with a body.  Give EDIT_HEADER, EDIT_NAMESPACE and "
                     f"EDIT_FUNCTION of utils/assay/loop/gauge.py a function of a base header that each translation "
                     f"unit includes")


def edited_text(original: bytes, site: EditSite, mark: int) -> bytes:
    """Return the header with the statement of one run at the start of the body, in the shape of the .clang-format.

    The statement goes on a line of its own.  Its indent is the indent of the line of the definition and one more
    indent step.  When the body is on one line, each statement of the body goes on a line of its own too.  The last
    character of the body then goes on the last line.

    Args:
        original: The bytes of the header before the first run
        site: The site of edit_site()
        mark: The number of the run, which no earlier run used

    Returns:
        The bytes of the edited header
    """
    inner = site.indent + b" " * INDENT_WIDTH
    statement = b"\n" + inner + f"static_cast<void>({mark}LL);".encode()
    after_open = site.open_brace + 1
    body = original[after_open:site.close_brace]
    if b"\n" in body:
        return original[:after_open] + statement + original[after_open:]
    return (original[:after_open] + statement + b"\n" + inner + body.strip() + b"\n" + site.indent
            + original[site.close_brace:])


def check_edit_format(tree: Path) -> str | None:
    """Make sure that the pinned clang-format makes no change to the edited header of a tree (THE EDIT).

    Args:
        tree: The root of the tree, which holds the .clang-format of the tree and the edited header

    Returns:
        None when the formatter makes no change to the header, or a note when the pinned clang-format is not installed

    Raises:
        GaugeError: If the formatter changes the header, cannot format all of it, or gives no verdict
    """
    from assay.guard.engines.meta import clang_format
    from assay.guard.engines.meta.runner import NotApplicable

    try:
        program = clang_format.formatter()
    except NotApplicable as problem:
        return f"the gauge does not check the format of the edit, and the test guard_clang_format does not apply: {problem}"
    verdict = clang_format.single_verdict(tree, program, EDIT_HEADER.as_posix())
    if not verdict.kind:
        return None
    if verdict.kind == "drift":
        what = f"changes {verdict.detail} place(s) of {EDIT_HEADER} after the edit, the first at line {verdict.line}"
    elif verdict.kind == "incomplete":
        what = f"cannot format all of {EDIT_HEADER} after the edit"
    else:
        what = f"gives no verdict on {EDIT_HEADER} after the edit: {verdict.detail}"
    raise GaugeError(f"the pinned clang-format {what}.  Each run of the test guard_clang_format of the copy then gives "
                     f"a failure.  Make edited_text() of utils/assay/loop/gauge.py write the shape of the .clang-format "
                     f"of the tree")


# ── The copy ───────────────────────────────────────────────────────────────


def head_commit(tree: Path) -> str:
    """Return the HEAD commit of a work tree.

    Raises:
        GaugeError: If git cannot read the tree
    """
    try:
        return git.head(tree)
    except git.GitError as problem:
        raise GaugeError(f"git cannot read the HEAD commit of {tree}: {problem}") from None


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
    try:
        # The clone writes each file of the tree, so it gets the limit of one run of a tool.
        git.run(source, "clone", "--quiet", "--shared", str(source.absolute()), str(destination.absolute()),
                timeout_s=proc.TOOL_S)
    except git.GitError as problem:
        raise GaugeError(f"git clone of {source} failed: {problem}") from None
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
        The measures.  A step that runs past the limit BUILD_S of assay.core.proc gets the status of its end on the
        signal of the limit

    Raises:
        proc.CannotStart: If the program of the step cannot start
    """
    before = ninja_files.log(build) if build is not None else {}
    counter = cost_meter.open_instruction_counter()
    step_command = proc.Command(command, timeout_s=proc.BUILD_S, cwd=cwd, env=environment,
                                stdout=logs / f"{name}.log", stderr=proc.Output.MERGE)
    with loop_history.StepTimer() as timer:
        try:
            status = proc.run(step_command).exit
        except proc.TimedOut as problem:
            status = problem.completed.exit if problem.completed is not None else 1
    instructions = cost_meter.read_instruction_counter(counter) if counter is not None else None
    result = timer.result
    step = StepResult(name, status, float(result["wall_s"]), float(result["cpu_s"]), float(result["system_s"]),
                      None if result["others_pct"] is None else float(result["others_pct"]),
                      None if instructions is None else round(instructions / GIGA, 1))
    if build is not None:
        facts = loop_history.summarize_build(build, loop_history.load_graph(build), before, ninja_files.log(build),
                                             timer.started_epoch)
        step.peak_memory_gb = loop_history.number(facts, "peak_memory_gb")
        step.critical_path_s = loop_history.number(facts, "critical_path_s")
        path = facts.get("critical_path")
        step.critical_path = tuple(str(item) for item in path) if isinstance(path, list) else ()
    return step


def junit_report(logs: Path, name: str) -> Path:
    """Return the JUnit report of one test step, in the directory of the logs (THE TESTS WITH A FAILURE)."""
    return logs / f"{name}.junit.xml"


def tests_with(statuses: Mapping[str, ctest_reader.TestStatus], status: ctest_reader.TestStatus) -> list[str]:
    """Return the sorted names of the tests with one status in the statuses of a JUnit report (THE TESTS WITH A FAILURE)."""
    return sorted(name for name, found in statuses.items() if found is status)


def failure_line(result: StepResult, report: Path | None) -> str:
    """Return the line of one step with a status that is not 0.

    For a test step, the line names each test with a failure, and then each test that ctest did not start (THE TESTS
    WITH A FAILURE).

    Args:
        result: The measures of the step
        report: The JUnit report of a test step, or None for a different step
    """
    line = f"{result.name}: status {result.status}"
    if report is None:
        return line
    try:
        statuses = ctest_reader.results(report)
    except ctest_reader.CtestError as problem:
        return f"{line}, and no test name is known: {problem}"
    failed = tests_with(statuses, ctest_reader.TestStatus.FAIL)
    not_run = tests_with(statuses, ctest_reader.TestStatus.NOTRUN)
    if not failed and not not_run:
        return f"{line}, and the JUnit report {report} names no test with a failure"
    parts = [f"{len(failed)} test(s) with a failure: {', '.join(failed)}"] if failed else []
    if not_run:
        parts.append(f"{len(not_run)} test(s) that ctest did not start: {', '.join(not_run)}")
    return f"{line}, {', and '.join(parts)}"


def failure_report(failures: list[str], logs: Path, what: str) -> str:
    """Return the report of the steps with a status that is not 0: each step with its tests, and the logs."""
    lines = [f"edit-loop-gauge: FAILED: {what}, and the gauge exits with 1.  The logs are in {logs}."]
    lines.extend(f"edit-loop-gauge:   {failure}" for failure in failures)
    return "\n".join(lines)


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
    """Compare the medians of the build and the tests with their measured rules.

    Args:
        medians: The median of the build and of the tests
        budgets: The measured rules of the registry
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
            compare(cpu_row, median.cpu_s, "s", f"The rule {instruction_row} holds the error level, because the step "
                                                f"has an exact instruction count")
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
        budgets: The measured rules of the registry
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


# ── The record ─────────────────────────────────────────────────────────────


def read_records(path: Path) -> list[GaugeRecord]:
    """Return each record of a file that --record writes, one JSON object on each line (THE REPORT).

    Complexity: linear in the bytes of the file.

    Raises:
        GaugeError: If the file cannot be read, or a line that is not empty is not a record
    """
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as problem:
        raise GaugeError(f"the records {path} cannot be read: {problem}") from None
    records: list[GaugeRecord] = []
    for number, line in enumerate(text.splitlines(), start=1):
        if not line.strip():
            continue
        try:
            value = json.loads(line)
        except ValueError as problem:
            raise GaugeError(f"{path}:{number}: the line is not JSON: {problem}") from None
        records.append(parsed_record(value, f"{path}:{number}"))
    return records


def record_number(block: Mapping[str, object], key: str, where: str) -> float:
    """Return a number of a part of a record.

    Raises:
        GaugeError: If the part has no number of that key
    """
    value = loop_history.number(block, key)
    if value is None:
        raise GaugeError(f"{where} has no number {key}")
    return value


def record_step(item: object, where: str) -> StepResult:
    """Return the measures of one step of a record, as measure() writes them with asdict().

    Raises:
        GaugeError: If the step does not have that shape
    """
    if not isinstance(item, dict) or not isinstance(item.get("name"), str):
        raise GaugeError(f"{where} is not a step with a name")
    status = item.get("status")
    path = item.get("critical_path", [])
    if not isinstance(status, int) or isinstance(status, bool) or not isinstance(path, (list, tuple)):
        raise GaugeError(f"{where} has no whole status or no list critical_path")
    return StepResult(str(item["name"]), status, record_number(item, "wall_s", where),
                      record_number(item, "cpu_s", where), record_number(item, "system_s", where),
                      loop_history.number(item, "others_pct"), loop_history.number(item, "instructions_g"),
                      loop_history.number(item, "peak_memory_gb"), loop_history.number(item, "critical_path_s"),
                      tuple(str(part) for part in path))


def record_median(item: object, where: str) -> Median:
    """Return the median of one step of a record, as measure() writes it with asdict().

    Raises:
        GaugeError: If the median does not have that shape
    """
    if not isinstance(item, dict):
        raise GaugeError(f"{where} is not a median")
    return Median(record_number(item, "wall_s", where), record_number(item, "cpu_s", where),
                  loop_history.number(item, "instructions_g"), loop_history.number(item, "worst_others_pct"))


def parsed_record(value: object, where: str) -> GaugeRecord:
    """Return the part of a JSON record that the gauge judges, from a record that measure() writes.

    Raises:
        GaugeError: If the value does not have the shape that measure() writes
    """
    if not isinstance(value, dict):
        raise GaugeError(f"{where} is not a JSON object")
    jobs, steps, medians = value.get("jobs"), value.get("steps"), value.get("medians")
    if not isinstance(jobs, int) or isinstance(jobs, bool) or not isinstance(steps, list) \
            or not isinstance(medians, dict):
        raise GaugeError(f"{where} has no whole jobs, no list steps or no object medians")
    missing = sorted(set(STEP_ROWS) - set(medians))
    if missing:
        raise GaugeError(f"{where} has no median of the step {', '.join(missing)}")
    return GaugeRecord(
        jobs,
        tuple(record_step(item, f"the step {index} of {where}") for index, item in enumerate(steps)),
        {name: record_median(medians[name], f"the median {name} of {where}") for name in STEP_ROWS},
    )


def judged_record(record: GaugeRecord, budgets: dict[str, check_report.Budget],
                  position: str) -> tuple[list[check_report.Finding], list[str]]:
    """Judge one record of the gauge: the medians of the edit runs (judge()) and the other steps (judge_others()).

    measure() judges its own record with this function, and the witness of each measured rule of the gauge
    judges a planted record with it (assay.guard.engines.buildsys.held).

    Args:
        record: The record
        budgets: The measured rules of the registry
        position: The path that a finding names

    Returns:
        The findings, and one note for each value that the gauge did not judge
    """
    findings, notes = judge(dict(record.medians), budgets, position, record.jobs)
    return findings + judge_others(list(record.steps), budgets, position), notes


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


def reconfigure_check(tree: Path, build: Path) -> list[str]:
    """Return the command of the rule reconfigure-noop on the build of a copy, with the master guard of the copy."""
    return [sys.executable, str(tree / GUARD_MASTER), "check", "--rule", RECONFIGURE_RULE, "--build-dir", str(build)]


def closing_steps(step: Callable[[str, list[str]], StepResult], build_command: list[str],
                  configure_command: list[str], check_command: list[str]) -> None:
    """Run the steps after the edit runs: a build with nothing to do, a second configure and the rule reconfigure-noop.

    Args:
        step: The runner of one step, which records its result
        build_command: The build of the copy
        configure_command: The configure of the copy
        check_command: The command of the rule reconfigure-noop (reconfigure_check())
    """
    step("noop-build", build_command)
    step("reconfigure", configure_command)
    step(RECONFIGURE_RULE, check_command)


def original_header(tree: Path) -> bytes:
    """Return the edited header as the commit of a copy holds it.

    Raises:
        GaugeError: If git cannot read the header
    """
    try:
        text = git.show(tree, "HEAD", EDIT_HEADER.as_posix())
    except git.GitError as problem:
        raise GaugeError(f"git cannot read {EDIT_HEADER} of the commit of {tree}: {problem}") from None
    if text is None:
        raise GaugeError(f"git cannot read {EDIT_HEADER} of the commit of {tree}: the commit has no such file")
    return text


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
    environment = proc.environment({"CRUCIBLE_CACHE_DIR": str(work / "cache")})
    jobs = str(options.jobs)
    print(f"edit-loop-gauge: host {socket.gethostname()}, commit {commit[:12]}, {state.get('changed_files')} changed "
          f"file(s) in the source work tree that the copy does not hold, preset {options.preset}, {jobs} jobs, "
          f"{options.runs} run(s), work directory {work}")
    build_command: list[str] = []
    test_command: list[str] = []
    results: list[StepResult] = []
    failures: list[str] = []

    def step(name: str, command: list[str], report: Path | None = None) -> StepResult:
        if report is not None:
            report.unlink(missing_ok=True)
        result = run_step(name, command, tree, environment, logs, build if command is build_command else None)
        results.append(result)
        path = ""
        if result.critical_path:
            path = (f", peak {loop_history.cell(result.peak_memory_gb)} GB, critical path {result.critical_path_s:g} s: "
                    f"{' -> '.join(result.critical_path)}")
        failure = ""
        if result.status != 0:
            failures.append(failure_line(result, report))
            failure = f"\nedit-loop-gauge: FAILED {failures[-1]}"
        print(f"edit-loop-gauge: {name}: status {result.status}, {result.wall_s:.1f} s wall, {result.cpu_s:.0f} s CPU, "
              f"{loop_history.cell(result.instructions_g)} G instructions, others "
              f"{loop_history.cell(result.others_pct)} %{path}{failure}", flush=True)
        return result

    def test_step(name: str) -> StepResult:
        report = junit_report(logs, name)
        return step(name, [*test_command, "--output-junit", str(report)], report)

    # The first configure runs the cmake of PATH, and the configure step rejects
    # it when its version is not the pin.  Each later step runs the cmake and
    # the ctest that the build directory names (utils/assay/setup/pins.py).
    failed = False
    if options.reuse is None:
        failed = step("configure", ["cmake", "--preset", options.preset, "-B", str(build)]).status != 0
    try:
        cmake = cmake_pin.configured_program(build, "cmake")
        ctest = cmake_pin.configured_program(build, "ctest")
    except cmake_pin.PinError as error:
        print(f"edit-loop-gauge: {error}  The logs are in {logs}.")
        return 1, True
    build_command = [cmake, "--build", str(build), "-j", jobs]
    test_command = [ctest, "--test-dir", str(build), "-j", jobs, "--timeout", str(options.timeout),
                    "--no-tests=error"]
    configure_command = [cmake, "--preset", options.preset, "-B", str(build)]
    if options.reuse is None and not failed:
        failed = step("warm-build", build_command).status != 0
        if not failed:
            test_step("warm-tests")
    header = tree / EDIT_HEADER
    original = original_header(tree)
    header.write_bytes(original)
    site = edit_site(header)
    runs: dict[str, list[StepResult]] = {"build": [], "tests": []}
    for run in range(1, options.runs + 1):
        if failed:
            break
        header.write_bytes(edited_text(original, site, secrets.randbits(MARK_BITS)))
        note = check_edit_format(tree)
        if note is not None and run == 1:
            check_report.write_line(f"edit-loop-gauge: {note}")
        built = step(f"edit-build-{run}", build_command)
        runs["build"].append(built)
        if built.status != 0:
            failed = True
            break
        runs["tests"].append(test_step(f"edit-tests-{run}"))
    if not failed:
        closing_steps(step, build_command, configure_command, reconfigure_check(tree, build))
    failed = failed or any(result.status != 0 for result in results)
    if not runs["build"] or len(runs["tests"]) != len(runs["build"]):
        what = "a step gave a status that is not 0 before the runs completed"
        check_report.write_line(failure_report(failures, logs, what))
        return 1, True
    medians = {step_name: median_of(step_results) for step_name, step_results in runs.items()}
    print(table(results, medians))
    record = {"time": started, "commit": commit, "changed_files": state.get("changed_files"),
              "host": socket.gethostname(), "jobs": options.jobs, "runs": options.runs, "preset": options.preset,
              "steps": [asdict(result) for result in results],
              "medians": {name: asdict(median) for name, median in medians.items()}}
    budgets = check_report.read_budgets()
    findings, notes = judged_record(GaugeRecord(options.jobs, tuple(results), medians), budgets, str(EDIT_HEADER))
    for note in notes:
        print(f"edit-loop-gauge: {note}")
    status = 0
    for row in sorted({finding.check for finding in findings}):
        status |= check_report.emit([finding for finding in findings if finding.check == row], row,
                                    options.warnings_dir)
    if options.record is not None:
        with options.record.open("a", encoding="utf-8") as handle:
            handle.write(json.dumps(record, separators=(",", ":")) + "\n")
    if failed:
        check_report.write_line(failure_report(failures, logs, "a step gave a status that is not 0"))
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
# Two more edit sites in the namespace foundation::detail.  Each row gives the name of the case, the planted code,
# and the code after the edit with the mark 42.
PLANTED_SITES = (
    ("a body of more lines gets the statement on a line of its own before its first statement",
     "inline void breakpoint() noexcept {\n    __builtin_trap();\n}",
     "inline void breakpoint() noexcept {\n    static_cast<void>(42LL);\n    __builtin_trap();\n}"),
    ("the edit of a member keeps the indent of the line of its definition",
     "struct holder {\n    static void breakpoint() noexcept { __builtin_trap(); }\n};",
     ("struct holder {\n    static void breakpoint() noexcept {\n        static_cast<void>(42LL);\n"
      "        __builtin_trap();\n    }\n};")),
)


def format_case(record_case: Callable[[str, bool], None]) -> None:
    """Make sure that the pinned clang-format makes no change to the edited header of this tree, with the longest mark.

    The case copies the header and the .clang-format of the tree into a scratch tree, and it edits the copy.  With no
    install of the pinned clang-format, the case prints that it does not run.  The test guard_clang_format then does
    not apply too.

    Args:
        record_case: The recorder of one case of the self-test
    """
    from assay.guard.engines.meta import clang_format

    name = f"the pinned clang-format makes no change to {EDIT_HEADER} of the tree after the edit"
    with tempfile.TemporaryDirectory(prefix="edit-loop-gauge-") as work:
        root = Path(work)
        for configuration in clang_format.CONFIGURATION_FILES:
            if (REPO_ROOT / configuration).is_file():
                shutil.copyfile(REPO_ROOT / configuration, root / configuration)
        header = root / EDIT_HEADER
        header.parent.mkdir(parents=True)
        original = (REPO_ROOT / EDIT_HEADER).read_bytes()
        header.write_bytes(original)
        header.write_bytes(edited_text(original, edit_site(header), LONGEST_MARK))
        try:
            note = check_edit_format(root)
        except GaugeError as problem:
            check_report.write_line(f"       {problem}")
            record_case(name, False)
            return
        if note is not None:
            check_report.write_line(f"  skip {name}: {note}")
            return
        record_case(name, True)


PLANTED_JUNIT = """<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="planted" tests="6" failures="2" disabled="1" skipped="2">
  <testcase name="test_passes" classname="test_passes" status="run"/>
  <testcase name="test_times_out" classname="test_times_out" status="fail"><failure message="Timeout"/></testcase>
  <testcase name="test_fails" classname="test_fails" status="fail"><failure message="Failed"/></testcase>
  <testcase name="test_skipped" classname="test_skipped" status="notrun"><skipped message="SKIP_RETURN_CODE=77"/></testcase>
  <testcase name="test_after_setup" classname="test_after_setup" status="notrun"><skipped message="Fixture dependency failed"/></testcase>
  <testcase name="test_disabled" classname="test_disabled" status="disabled"/>
</testsuite>
"""
# The planted report with no failure of a test.  The step gives a status that is not 0, because ctest did not start
# one test.
PLANTED_JUNIT_NOT_RUN = """<?xml version="1.0" encoding="UTF-8"?>
<testsuite name="planted" tests="2" failures="0" skipped="1">
  <testcase name="test_passes" classname="test_passes" status="run"/>
  <testcase name="test_no_program" classname="test_no_program" status="notrun"><skipped message="Unable to find executable"/></testcase>
</testsuite>
"""


def failure_cases(record_case: Callable[[str, bool], None]) -> None:
    """Make sure that the line of a test step names each test with a failure.

    The cases read a planted JUnit report (THE TESTS WITH A FAILURE).

    Args:
        record_case: The recorder of one case of the self-test
    """
    def stopped(name: str) -> StepResult:
        return StepResult(name, 8, 1, 1, 0, None, None)

    with tempfile.TemporaryDirectory(prefix="edit-loop-gauge-") as work:
        logs = Path(work)
        report = junit_report(logs, "edit-tests-1")
        report.write_text(PLANTED_JUNIT, encoding="utf-8")
        line = failure_line(stopped("edit-tests-1"), report)
        record_case("the line of a test step names each test with a failure in sequence, then each test that ctest "
                    "did not start, and no skipped test",
                    line == "edit-tests-1: status 8, 2 test(s) with a failure: test_fails, test_times_out, and 1 "
                            "test(s) that ctest did not start: test_after_setup")
        alone = junit_report(logs, "edit-tests-3")
        alone.write_text(PLANTED_JUNIT_NOT_RUN, encoding="utf-8")
        record_case("the line of a test step with no failure names the test that ctest did not start",
                    failure_line(stopped("edit-tests-3"), alone)
                    == "edit-tests-3: status 8, 1 test(s) that ctest did not start: test_no_program")
        missing = failure_line(stopped("edit-tests-2"), junit_report(logs, "edit-tests-2"))
        record_case("the line of a test step with no JUnit report gives the status and tells why no test name is known",
                    missing.startswith("edit-tests-2: status 8, and no test name is known: "))
        record_case("the line of a step other than a test step gives its status",
                    failure_line(stopped("edit-build-1"), None) == "edit-build-1: status 8")
        text = failure_report([line], logs, "a step gave a status that is not 0")
        record_case("the report of the measurement says FAILED and gives each step with a status that is not 0",
                    text.startswith("edit-loop-gauge: FAILED: a step gave a status that is not 0")
                    and f"edit-loop-gauge:   {line}" in text)


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
            site = edit_site(header)
        except tsast.KitMissing:
            print("edit-loop-gauge --self-test: the parse-tree kit is not installed; run python3 utils/bin/setup install tree-sitter")
            return NOT_RUNNABLE
        edited = edited_text(header.read_bytes(), site, 42).decode()
        expect("the edit goes into the body of foundation::detail::breakpoint, and not into another breakpoint, with "
               "each statement on a line of its own",
               "inline void breakpoint() noexcept {\n    static_cast<void>(42LL);\n    __builtin_trap();\n}\n" in edited
               and "namespace other {\ninline void breakpoint() noexcept {}" in edited)
        for name, planted, expected in PLANTED_SITES:
            header.write_text(f"namespace foundation::detail {{\n{planted}\n}}\n", encoding="utf-8")
            expect(name, expected in edited_text(header.read_bytes(), edit_site(header), 42).decode())
        for name, planted in (("a header with no edit site stops the gauge",
                               PLANTED_HEADER.replace("inline void breakpoint() noexcept { __builtin_trap(); }",
                                                      "inline void breakpoint_moved() noexcept {}")),
                              ("a body with no statement stops the gauge",
                               "namespace foundation::detail {\ninline void breakpoint() noexcept {}\n}\n")):
            header.write_text(planted, encoding="utf-8")
            try:
                edit_site(header)
                expect(name, False)
            except GaugeError:
                expect(name, True)
        format_case(expect)

        source = root / "source"
        source.mkdir()
        git.init(source)
        (source / "tracked.txt").write_text("one\n", encoding="utf-8")
        git.commit_all(source, "one")
        (source / "tracked.txt").write_text("changed in the work tree\n", encoding="utf-8")
        copy = root / "copy"
        commit = clone_tree(source, copy)
        expect("the copy holds the HEAD commit of the source and not a change of its work tree",
               commit == head_commit(source) and (copy / "tracked.txt").read_text(encoding="utf-8") == "one\n")
        (copy / "tracked.txt").write_text("edited in the copy\n", encoding="utf-8")
        expect("an edit of the copy does not change the source",
               (source / "tracked.txt").read_text(encoding="utf-8") == "changed in the work tree\n")
    failure_cases(expect)
    chosen_work = default_work_dir().resolve()
    caches = cache.base_root()
    expect(
        "the default work directory is not in the repository and not in the root of the caches.  The copy then adds "
        "no second CLAUDE.md in the repository",
        not chosen_work.is_relative_to(REPO_ROOT.resolve())
        and chosen_work.parent == cache.work_root().resolve()
        and (caches is None or not chosen_work.is_relative_to(caches.resolve())),
    )

    def result(name: str, wall: float, cpu: float, share: float | None, count: float | None) -> StepResult:
        return StepResult(name, 0, wall, cpu, cpu / 10, share, count)

    called: list[tuple[str, list[str]]] = []

    def recorded(name: str, command: list[str]) -> StepResult:
        called.append((name, command))
        return result(name, 1, 1, None, None)

    copy_tree, copy_build = Path("/work/tree"), Path("/work/tree/build")
    closing_steps(recorded, ["build"], ["configure"], reconfigure_check(copy_tree, copy_build))
    expect("after the runs, the gauge builds with nothing to do, configures again and runs the rule reconfigure-noop "
           "with the master guard of the copy on its build",
           [name for name, _ in called] == ["noop-build", "reconfigure", RECONFIGURE_RULE]
           and called[2][1] == [sys.executable, str(copy_tree / GUARD_MASTER), "check", "--rule", RECONFIGURE_RULE,
                                "--build-dir", str(copy_build)]
           and (REPO_ROOT / GUARD_MASTER).is_file())

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
        record = {"jobs": WALL_JOBS, "steps": [asdict(step) for step in steps],
                  "medians": {name: asdict(median) for name, median in quiet.items()}}
        parsed = parsed_record(json.loads(json.dumps(record)), "the record")
        found, notes = judged_record(parsed, {**rows(10, 100), **other_rows}, "h")
        expect("judged_record judges the medians and the other steps of a record that went through JSON",
               sorted(f.check for f in found) == sorted(["build-peak-memory", "configure-time",
                                                         *(name for triple in STEP_ROWS.values() for name in triple)])
               and not notes)
        direct, _ = judged_record(GaugeRecord(WALL_JOBS, tuple(steps), quiet), {**rows(10, 100), **other_rows}, "h")
        expect("a record that went through JSON gives the findings of the record of the run",
               [item.text() for item in found] == [item.text() for item in direct])
        for name, broken in (("no median of the tests", {**record, "medians": {"build": asdict(quiet["build"])}}),
                             ("a step with no wall time", {**record, "steps": [{"name": "configure", "status": 0}]}),
                             ("a jobs that is no whole number", {**record, "jobs": "many"})):
            try:
                parsed_record(broken, "the record")
                expect(f"parsed_record refuses a record with {name}", False)
            except GaugeError:
                expect(f"parsed_record refuses a record with {name}", True)
    with tempfile.TemporaryDirectory(prefix="edit-loop-gauge-") as work:
        records = Path(work) / "records.jsonl"
        line = json.dumps(record)
        records.write_text(f"{line}\n\n{line}\n", encoding="utf-8")
        expect("read_records gives each record of the file and skips an empty line",
               [item.jobs for item in read_records(records)] == [WALL_JOBS, WALL_JOBS])
        for name, text in (("a line that is not JSON", "{\n"), ("a line that is not an object", "[1]\n")):
            records.write_text(text, encoding="utf-8")
            try:
                read_records(records)
                expect(f"read_records refuses {name}", False)
            except GaugeError:
                expect(f"read_records refuses {name}", True)

    if failures:
        print(f"edit-loop-gauge --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("edit-loop-gauge --self-test: every case holds.")
    return 0


# ── Main ───────────────────────────────────────────────────────────────────


def default_work_dir() -> Path:
    """Return the work directory of a run with no --work-dir: one directory of this process in the work root.

    The work root of assay.core.cache is not in a checkout (THE MEASUREMENT).  The copy of the tree then adds no
    second CLAUDE.md in the repository while the gauge runs.
    """
    return cache.work_root() / f"edit-loop-gauge-{os.getpid()}"


def main(argv: list[str]) -> int:
    """Parse the arguments and run the gauge or the self-test, or give the words after --sample or --cold to its module.

    Args:
        argv: The words after `loop gauge`

    Returns:
        The exit status
    """
    if argv[:1] == ["--sample"]:
        return gauge_sample.main(argv[1:])
    if argv[:1] == ["--cold"]:
        return gauge_cold.main(argv[1:])
    if argv == ["--self-test"]:
        return self_test()
    parser = argparse.ArgumentParser(prog="loop gauge",
                                     description="Measure the build and the tests after one edit of a base header.")
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
        print("loop gauge: --jobs, --runs and --timeout take a positive number.", file=sys.stderr)
        return 2
    free = available_gb()
    if free is not None and free < options.min_free_gb:
        print(f"edit-loop-gauge: the host has {free:.0f} GB of available memory, less than --min-free-gb "
              f"{options.min_free_gb:g}.  Wait until other work ends, or give a smaller --jobs and --min-free-gb.",
              file=sys.stderr)
        return 2
    if options.reuse is not None:
        if options.work_dir is not None:
            print("loop gauge: give --work-dir or --reuse, not the two.", file=sys.stderr)
            return 2
        work = options.reuse.resolve()
        if not cmake_reader.is_configured(work / "tree" / "build") or not (work / "logs").is_dir():
            print(f"loop gauge: {work} is not the work directory of an earlier run with --keep: it has no "
                  f"configured build tree/build or no logs directory.", file=sys.stderr)
            return 2
    else:
        work = (options.work_dir or default_work_dir()).resolve()
        if work.exists():
            print(f"loop gauge: the work directory {work} exists.  Give another --work-dir.", file=sys.stderr)
            return 2
    status, should_keep = 1, True
    try:
        status, should_keep = measure(REPO_ROOT, work, options)
    except (GaugeError, proc.CannotStart) as problem:
        print(f"edit-loop-gauge: {problem}", file=sys.stderr)
    finally:
        if work.exists() and not (options.keep or should_keep):
            shutil.rmtree(work)
        elif work.exists():
            print(f"edit-loop-gauge: the work directory {work} stays.  Remove it when you no longer need its logs.")
    return status
