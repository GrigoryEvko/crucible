#!/usr/bin/env python3
"""loop_history — the record of each edit-build-test loop of utils/scripts/run-affected-tests.py.

THE HISTORY
    After each call that builds or runs tests, run-affected-tests.py appends
    one JSON object on one line to BUILD_DIR/loop-history/history.jsonl.  A
    line holds:
      time, commit, tree, changed_files
                  the start of the call (ISO 8601, UTC), the commit of the
                  source tree, "clean" or "dirty", and the number of tracked
                  files that differ from the commit
      kind, jobs, cpus, load
                  the build kind (build-kind.txt), the job count, the CPUs
                  that the process can use, and the one-minute load average
                  at the start
      build       null when the call did not build.  Else the status, the
                  wall time, the CPU time and the system time of the build
                  step, the share of the host CPU time that other processes
                  used during the step, the edges that ran by kind, the
                  compiles and the links with their CPU time from the
                  records of the build launcher, the critical path, the
                  longest edge, and the peak concurrent memory
      tests       null when the call ran no test.  Else the status, the wall
                  time, the CPU time and the system time of the ctest step,
                  the other share, and the counts of the tests
    A failure of the history never changes the result of run-affected-tests.py.
    When the file grows past TRIM_BYTES, the next append keeps the newer half
    of its lines.

THE EDGES OF ONE BUILD
    Ninja can write its log again at the start of a build, with one record
    for each output in no order (utils/scripts/ninja_files.py), so an offset
    into the log does not find the records of one build.  The recorder reads
    the last record of each output before the build and after it.  An output
    whose record is new or changed ran in the build.  The glob check of CMake
    runs at each start of Ninja and the CMake run is not a compile, so a
    build whose edges are only of the kind "cmake" did no work.  The time of
    a CMake run inside the build is the field reconfigure_s.

THE CRITICAL PATH
    The critical path is the longest chain of edges that ran, where each edge
    of the chain is an input or an order-only input of the next one, through
    phony edges.  Its length is the sum of the durations of its edges, the
    wall time of the build on a host with no limit of jobs.  An edge that did
    not run adds nothing.  The CPU time of the build divided by the jobs is
    the other lower bound.  A wall time far above the two bounds tells that
    the order of the jobs, and not the work, sets the wall time.
    The graph of build.ninja costs approximately 0.5 s to read, so the
    recorder keeps a compact graph in BUILD_DIR/loop-history/graph.json, and
    it reads build.ninja again only when the inode, the size or the time of
    build.ninja changes, after a configure.

THE COST RECORDS
    For each compile and each link that ran, the recorder reads the record
    OUTPUT.cost of utils/scripts/build-launcher.py, when the build wrote it.
    The CPU time and the instructions of a compile come from its cost block.
    A ccache hit holds no cost.  The instructions of the compiles are a sum
    only when each compile that did not hit has an exact count.  The peak
    concurrent memory is the largest sum of the peak memory of the steps
    whose wall times overlap, from the end and the wall time of each cost
    block.  Each step reaches its peak at a different time, so the sum is an
    upper bound.

THE HOST LOAD
    The CPU time of a step comes from getrusage of the children of this
    process, so it holds the step and each process that it waited for.
    /proc/stat gives the busy time of all CPUs of the host.  The busy time
    less the CPU time of the step is the time of the other processes, and
    others_pct gives it as a share of the CPU time of the host during the
    step.  A wall time is comparable with another one only when others_pct
    is small for the two.

THE WARNINGS OF A LINE
    judge_line() compares one line with three rows of utils/scripts/budgets.txt,
    and each finding is a warning: noop-build-time for the wall time of a
    build that ran no edge but the glob check, build-peak-memory for the peak
    concurrent memory of the build, and configure-time for a CMake run inside
    the build.  run-affected-tests.py prints the warnings of its line, and
    the table prints the warnings of each line that it shows.

THE CONFIGURE TIME
    The root CMakeLists.txt writes the start and the end of each configure to
    BUILD_DIR/configure-time.txt, and CMake then writes build.ninja.  When a
    query of the CMake file API exists, CMake writes the reply after
    build.ninja, and the index file of the reply last.  configure_time()
    gives the configure, the generate step, the reply and their sum.  The
    start is the first line of the root CMakeLists.txt, approximately 0.5 s
    after the start of the cmake process.

THE TABLE
    loop_history.py BUILD_DIR [-n N] prints the last N lines (20 by default)
    as a table.

    loop_history.py --self-test

Exit 0 on success, 2 on a usage error, a history that cannot be read, or a
failed self-test.
"""

from __future__ import annotations

import argparse
import json
import os
import resource
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
import cost_meter  # noqa: E402
import ninja_files  # noqa: E402
import throwaway_repo  # noqa: E402

CONFIGURE_TIME_FILE = "configure-time.txt"
# The end of the configure can be a few milliseconds after the time that the file system gives build.ninja.
CONFIGURE_SLACK_S = 0.05
# The directory of the reply of the CMake file API, and the pattern of its index file.
FILE_API_REPLY = Path(".cmake") / "api" / "v1" / "reply"
REPLY_INDEX = "index-*.json"
HISTORY_DIR = "loop-history"
HISTORY_NAME = "history.jsonl"
GRAPH_NAME = "graph.json"
LINE_FORMAT = 1
GRAPH_FORMAT = 1
TRIM_BYTES = 4 * 1024 * 1024
DEFAULT_LINES = 20
# A record of the build launcher counts when it is not older than the start of
# the build less this slack, which covers the time granularity of a file system.
RECORD_SLACK_S = 1.0
# The edge kinds whose time the critical path counts.  The glob check and the
# CMake run are of the kind "cmake", and a phony edge does no work.
WORK_KINDS = frozenset({"compile", "link", "archive", "custom", "other"})
# The word that read_graph() leaves of the variable ${cmake_ninja_workdir}, which
# CMake puts before the absolute name of an implicit output.
WORKDIR_PREFIX = "{cmake_ninja_workdir}"
# The fields of the line "cpu" of /proc/stat that count busy time: user, nice,
# system, irq, softirq and steal.  The time of a guest is part of user.
BUSY_FIELDS = (1, 2, 3, 6, 7, 8)
KB_PER_GB = 1024 * 1024


# ── The host load ──────────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class HostSample:
    """The busy CPU time of the host, the online CPUs, and the CPU time of the waited children of this process."""

    busy_s: float | None
    online: int
    children_user_s: float
    children_system_s: float


def sample_host() -> HostSample:
    """Read the busy time of the host from /proc/stat, and the CPU time of the children of this process.

    Returns:
        The sample.  busy_s is None when /proc/stat cannot be read
    """
    usage = resource.getrusage(resource.RUSAGE_CHILDREN)
    busy: float | None = None
    online = 0
    try:
        with open("/proc/stat", encoding="ascii") as handle:
            for line in handle:
                if line.startswith("cpu "):
                    cells = line.split()
                    busy = sum(int(cells[index]) for index in BUSY_FIELDS if index < len(cells))
                elif line.startswith("cpu"):
                    online += 1
                else:
                    break
        if busy is not None:
            busy /= os.sysconf("SC_CLK_TCK")
    except (OSError, ValueError):
        busy = None
    return HostSample(busy, online, usage.ru_utime, usage.ru_stime)


def others_share(before: HostSample, after: HostSample, wall_s: float) -> float | None:
    """Return the share of the CPU time of the host that other processes used between two samples, in percent.

    Args:
        before: The sample at the start of the step
        after: The sample at the end of the step
        wall_s: The wall time of the step

    Returns:
        The share, or None when /proc/stat gave no busy time or the step took no time
    """
    if before.busy_s is None or after.busy_s is None or wall_s <= 0 or after.online <= 0:
        return None
    own = (after.children_user_s - before.children_user_s) + (after.children_system_s - before.children_system_s)
    others = max(0.0, (after.busy_s - before.busy_s) - own)
    return 100.0 * others / (wall_s * after.online)


class StepTimer:
    """The wall time, the CPU time, the system time and the other share of one step, as a context manager."""

    def __init__(self) -> None:
        """Start with no measure."""
        self.started_epoch = 0.0
        self.result: dict[str, object] = {}
        self._started = 0.0
        self._sample: HostSample | None = None

    def __enter__(self) -> StepTimer:
        """Take the samples of the start of the step."""
        self.started_epoch = time.time()
        self._sample = sample_host()
        self._started = time.monotonic()
        return self

    def __exit__(self, *_exc: object) -> None:
        """Take the samples of the end of the step, and keep the measures in result."""
        wall_s = time.monotonic() - self._started
        after = sample_host()
        before = self._sample
        assert before is not None
        user = after.children_user_s - before.children_user_s
        system = after.children_system_s - before.children_system_s
        share = others_share(before, after, wall_s)
        self.result = {"wall_s": round(wall_s, 3), "cpu_s": round(user + system, 2), "system_s": round(system, 2),
                       "others_pct": None if share is None else round(share, 1)}


# ── The compact graph ──────────────────────────────────────────────────────


@dataclass(slots=True)
class CompactGraph:
    """The edges of build.ninja with the kind, the outputs and the producing edges of the inputs of each edge.

    `outputs` holds the outputs of each edge, relative to the build directory when inside it.  `deps` holds, for each
    edge, the edges that write its inputs and its order-only inputs.  `edge_of` maps the absolute normalized path of
    each output to its edge.
    """

    kinds: list[str]
    outputs: list[list[str]]
    deps: list[list[int]]
    edge_of: dict[str, int] = field(default_factory=dict)


def absolute_output(build: Path, name: str) -> str:
    """Return the absolute normalized path of one path of build.ninja.

    Args:
        build: The build directory
        name: The path as read_graph() gives it

    Returns:
        The absolute path
    """
    if name.startswith(WORKDIR_PREFIX):
        name = name[len(WORKDIR_PREFIX):]
    return os.path.normpath(os.path.join(build, name))


def compact(graph: ninja_files.Graph, build: Path) -> CompactGraph:
    """Make the compact graph of one read of build.ninja.

    Complexity: linear in the edges and their inputs.

    Args:
        graph: The graph of read_graph()
        build: The build directory

    Returns:
        The compact graph
    """
    edge_of: dict[str, int] = {}
    outputs: list[list[str]] = []
    for index, edge in enumerate(graph.edges):
        names = []
        for output in edge.outputs:
            path = absolute_output(build, output)
            edge_of[path] = index
            names.append(os.path.relpath(path, build) if path.startswith(f"{build}{os.sep}") else path)
        outputs.append(names)
    deps = []
    for index, edge in enumerate(graph.edges):
        producers = {edge_of.get(absolute_output(build, name)) for name in edge.inputs + edge.order_only}
        deps.append(sorted(producer for producer in producers if producer is not None and producer != index))
    return CompactGraph([ninja_files.edge_kind(edge.rule) for edge in graph.edges], outputs, deps, edge_of)


def manifest_key(build: Path) -> list[int] | None:
    """Return the inode, the size and the time of build.ninja, or None when it does not exist."""
    try:
        status = (build / "build.ninja").stat()
    except OSError:
        return None
    return [status.st_ino, status.st_size, status.st_mtime_ns]


def load_graph(build: Path) -> CompactGraph | None:
    """Return the compact graph of build.ninja, from the cache when the cache holds the graph of the same file.

    A cache that cannot be read counts as missing.  The function writes a new cache after it reads build.ninja.

    Args:
        build: The build directory

    Returns:
        The graph, or None when the build directory has no build.ninja
    """
    key = manifest_key(build)
    if key is None:
        return None
    cache = build / HISTORY_DIR / GRAPH_NAME
    try:
        stored = json.loads(cache.read_text(encoding="utf-8"))
        if stored.get("format") == GRAPH_FORMAT and stored.get("key") == key and stored.get("build") == str(build):
            graph = CompactGraph(stored["kinds"], stored["outputs"], stored["deps"])
            graph.edge_of = {absolute_output(build, name): index for index, names in enumerate(graph.outputs)
                             for name in names}
            return graph
    except (OSError, ValueError, KeyError, TypeError, AttributeError):
        pass
    graph = compact(ninja_files.read_graph(build / "build.ninja"), build)
    cache.parent.mkdir(parents=True, exist_ok=True)
    staging = cache.with_name(f".{cache.name}.{os.getpid()}")
    staging.write_text(json.dumps({"format": GRAPH_FORMAT, "key": key, "build": str(build), "kinds": graph.kinds,
                                   "outputs": graph.outputs, "deps": graph.deps}), encoding="utf-8")
    os.replace(staging, cache)
    return graph


# ── The analysis of one build ──────────────────────────────────────────────


def edge_spans(graph: CompactGraph, records: dict[str, ninja_files.LogRecord],
               outputs: list[str]) -> tuple[dict[int, tuple[int, int]], int]:
    """Return the start and the end of each edge that ran, from the records of its outputs.

    Args:
        graph: The compact graph
        records: The ninja log records after the build
        outputs: The outputs that ran

    Returns:
        The span in milliseconds of each edge, and the number of outputs that the graph does not hold
    """
    spans: dict[int, tuple[int, int]] = {}
    unknown = 0
    for output in outputs:
        edge = graph.edge_of.get(output)
        if edge is None:
            unknown += 1
            continue
        record = records[output]
        start, end = spans.get(edge, (record.start_ms, record.end_ms))
        spans[edge] = (min(start, record.start_ms), max(end, record.end_ms))
    return spans, unknown


def critical_path(graph: CompactGraph, spans: dict[int, tuple[int, int]]) -> tuple[int, list[int]]:
    """Return the length of the critical path of the edges that ran, and its edges in order.

    Complexity: linear in the edges and the dependencies that the walk reaches from the edges that ran.

    Args:
        graph: The compact graph
        spans: The span in milliseconds of each edge that ran

    Returns:
        The length in milliseconds, and the edges of the path from the first to the last
    """
    counted = {edge for edge in spans if graph.kinds[edge] in WORK_KINDS}
    # best[node]: the length of the longest chain that ends at the node or below
    # it, and the counted edge at the end of that chain (-1 for none).
    best: dict[int, tuple[int, int]] = {}
    before: dict[int, int] = {}
    for root in counted:
        stack = [(root, False)]
        while stack:
            node, expanded = stack.pop()
            if node in best:
                continue
            passes = node in counted or graph.kinds[node] == "phony"
            if not passes:
                best[node] = (0, -1)
                continue
            if not expanded:
                stack.append((node, True))
                stack.extend((dep, False) for dep in graph.deps[node] if dep not in best)
                continue
            longest = (0, -1)
            for dep in graph.deps[node]:
                candidate = best.get(dep, (0, -1))
                if candidate[0] > longest[0]:
                    longest = candidate
            if node in counted:
                start, end = spans[node]
                best[node] = (longest[0] + max(0, end - start), node)
                before[node] = longest[1]
            else:
                best[node] = longest
    if not counted:
        return 0, []
    last = max(counted, key=lambda edge: (best[edge][0], -edge))
    chain = []
    node = last
    while node >= 0:
        chain.append(node)
        node = before.get(node, -1)
    return best[last][0], chain[::-1]


def read_cost_record(path: Path, since_epoch: float) -> dict[str, object] | None:
    """Return the record of the build launcher at one path when the build of since_epoch wrote it.

    Args:
        path: The record OUTPUT.cost
        since_epoch: The start of the build, in seconds since the epoch

    Returns:
        The record, or None when it is missing, older than the build or not a record
    """
    try:
        if path.stat().st_mtime < since_epoch - RECORD_SLACK_S:
            return None
        record = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    return record if isinstance(record, dict) else None


def number(block: object, key: str) -> float | None:
    """Return one number of a cost block, or None when the block has no such number."""
    if not isinstance(block, dict):
        return None
    value = block.get(key)
    return float(value) if isinstance(value, (int, float)) and not isinstance(value, bool) else None


def peak_concurrent_gb(blocks: list[dict[str, object]]) -> float | None:
    """Return the largest sum of the peak memory of the steps whose wall times overlap.

    Each block gives the interval [end - wall_s, end].  Complexity: O(n log n) for n blocks, because of the sort.

    Args:
        blocks: The cost blocks of the steps

    Returns:
        The sum in GB of 2^30 bytes, or None when no block has the three numbers
    """
    events: list[tuple[float, int, float]] = []
    for block in blocks:
        end, wall, peak = number(block, "end"), number(block, "wall_s"), number(block, "peak_rss_kb")
        if end is None or wall is None or peak is None:
            continue
        # At one time, an end comes before a start, so two steps that touch do not overlap.
        events.append((end - wall, 1, peak))
        events.append((end, 0, -peak))
    if not events:
        return None
    current = largest = 0.0
    for _moment, _order, delta in sorted(events):
        current += delta
        largest = max(largest, current)
    return round(largest / KB_PER_GB, 2)


def summarize_build(build: Path, graph: CompactGraph | None, before: dict[str, ninja_files.LogRecord],
                    after: dict[str, ninja_files.LogRecord], since_epoch: float) -> dict[str, object]:
    """Return the facts of one build: its edges, its compiles and links, its critical path and its peak memory.

    Args:
        build: The build directory
        graph: The compact graph, or None when the build has no build.ninja
        before: The ninja log records before the build
        after: The ninja log records after the build
        since_epoch: The start of the build, in seconds since the epoch

    Returns:
        The facts as the build part of a history line
    """
    outputs = ninja_files.ran_outputs(before, after)
    if graph is None:
        return {"edges": len(outputs), "graph": "the build directory has no build.ninja"}
    spans, unknown = edge_spans(graph, after, outputs)
    kinds: dict[str, int] = {}
    for edge in spans:
        kinds[graph.kinds[edge]] = kinds.get(graph.kinds[edge], 0) + 1
    facts: dict[str, object] = {"edges": sum(count for kind, count in kinds.items() if kind != "cmake"),
                                "kinds": dict(sorted(kinds.items()))}
    if unknown:
        facts["unknown_outputs"] = unknown
    manifest = graph.edge_of.get(os.path.normpath(build / "build.ninja"))
    if manifest is not None and manifest in spans:
        start, end = spans[manifest]
        facts["reconfigure_s"] = round((end - start) / 1000, 2)
    blocks: list[dict[str, object]] = []
    compiles = {"records": 0, "hits": 0, "cpu_s": 0.0, "instructions": 0, "exact": True}
    links = {"records": 0, "cpu_s": 0.0}
    for edge in spans:
        kind = graph.kinds[edge]
        if kind not in ("compile", "link") or not graph.outputs[edge]:
            continue
        record = read_cost_record(Path(absolute_output(build, graph.outputs[edge][0]) + cost_meter.RECORD_SUFFIX),
                                  since_epoch)
        if record is None:
            continue
        block = record.get("cost")
        target = compiles if kind == "compile" else links
        target["records"] += 1
        if record.get("result") == "hit":
            compiles["hits"] += 1
            continue
        if not isinstance(block, dict):
            continue
        blocks.append(block)
        target["cpu_s"] += number(block, "cpu_s") or 0.0
        if kind == "compile":
            count = number(block, "instructions")
            if count is None:
                compiles["exact"] = False
            else:
                compiles["instructions"] += int(count)
    facts["compile_records"] = compiles["records"]
    facts["ccache_hits"] = compiles["hits"]
    facts["compile_cpu_s"] = round(compiles["cpu_s"], 1)
    facts["compile_instructions_g"] = round(compiles["instructions"] / 1e9, 1) if compiles["exact"] else None
    facts["link_records"] = links["records"]
    facts["link_cpu_s"] = round(links["cpu_s"], 1)
    facts["peak_memory_gb"] = peak_concurrent_gb(blocks)
    length, chain = critical_path(graph, spans)
    facts["critical_path_s"] = round(length / 1000, 2)
    facts["critical_path"] = [graph.outputs[edge][0] for edge in chain if graph.outputs[edge]]
    work = [edge for edge in spans if graph.kinds[edge] in WORK_KINDS]
    if work:
        longest = max(work, key=lambda edge: (spans[edge][1] - spans[edge][0], -edge))
        facts["longest_edge_s"] = round((spans[longest][1] - spans[longest][0]) / 1000, 2)
        facts["longest_edge"] = graph.outputs[longest][0] if graph.outputs[longest] else ""
    return facts


def configure_time(build: Path) -> dict[str, object] | None:
    """Return the times of the last configure of a build directory.

    Args:
        build: The build directory

    Returns:
        The start (ISO 8601, UTC), and the configure, the generate step, the reply of the file API and their sum in
        seconds.  None when the build directory has no configure-time.txt, or when the last configure wrote no
        build.ninja after its end
    """
    try:
        start_text, end_text = (build / CONFIGURE_TIME_FILE).read_text(encoding="ascii").split()
        start, end = float(start_text), float(end_text)
        written = (build / "build.ninja").stat().st_mtime
    except (OSError, ValueError):
        return None
    if not start <= end <= written + CONFIGURE_SLACK_S:
        return None
    replied = written
    for index in (build / FILE_API_REPLY).glob(REPLY_INDEX):
        try:
            replied = max(replied, index.stat().st_mtime)
        except OSError:
            continue
    return {"start": iso_time(start), "configure_s": round(end - start, 2), "generate_s": round(max(0.0, written - end), 2),
            "file_api_s": round(replied - written, 2), "total_s": round(replied - start, 2)}


def warning_over(budgets: dict[str, check_report.Budget], row: str, value: float | None, unit: str, what: str,
                 place: str) -> list[check_report.Finding]:
    """Return a warning when a value is over the warning threshold of its row, else no finding.

    Args:
        budgets: The budget table
        row: The row
        value: The value, or None for no value
        unit: The unit of the value
        what: The words that name the value
        place: The path that the finding names

    Returns:
        The warning, or an empty list
    """
    budget = budgets.get(row)
    if budget is None or value is None or value <= budget.warn:
        return []
    return [check_report.Finding("warning", place, 0, row, f"{what} took {value:g} {unit}, over the warning threshold "
                                                           f"of {budget.warn:g} {unit} of the row {row}")]


def judge_line(line: dict[str, object], budgets: dict[str, check_report.Budget],
               place: str) -> list[check_report.Finding]:
    """Return the warnings of one history line (THE WARNINGS OF A LINE).

    Args:
        line: The line
        budgets: The budget table
        place: The path that a finding names

    Returns:
        The warnings
    """
    build = line.get("build")
    if not isinstance(build, dict):
        return []
    findings: list[check_report.Finding] = []
    did_nothing = build.get("status") == 0 and build.get("edges") == 0 and "reconfigure_s" not in build
    if did_nothing:
        findings += warning_over(budgets, "noop-build-time", number(build, "wall_s"), "s",
                                 "the build with no edge to run", place)
    findings += warning_over(budgets, "build-peak-memory", number(build, "peak_memory_gb"), "GB",
                             "the compiles and links of the build together", place)
    findings += warning_over(budgets, "configure-time", number(build, "reconfigure_s"), "s",
                             "the CMake run inside the build", place)
    return findings


# ── The source tree ────────────────────────────────────────────────────────


def git_state(source: Path) -> dict[str, object]:
    """Return the commit of a source tree, its state and the number of tracked files that differ from the commit.

    The call reads the tree with `git status` and writes nothing.  A repository variable of the environment is not
    given to git, so git reads the repository of the source tree itself.

    Args:
        source: The source tree

    Returns:
        The commit, "clean" or "dirty", and the count; each is None when git cannot read the tree
    """
    environment = {name: value for name, value in os.environ.items()
                   if name not in throwaway_repo.REPOSITORY_VARIABLES}
    try:
        result = subprocess.run(["git", "-C", str(source), "status", "--porcelain=v2", "--branch",
                                 "--untracked-files=no"], capture_output=True, text=True, check=False, env=environment)
    except OSError:
        return {"commit": None, "tree": None, "changed_files": None}
    if result.returncode != 0:
        return {"commit": None, "tree": None, "changed_files": None}
    commit = None
    changed = 0
    for line in result.stdout.splitlines():
        if line.startswith("# branch.oid "):
            commit = line.split()[2]
        elif not line.startswith("#"):
            changed += 1
    if commit == "(initial)":
        commit = None
    return {"commit": commit, "tree": "clean" if changed == 0 else "dirty", "changed_files": changed}


# ── The recorder ───────────────────────────────────────────────────────────


class Recorder:
    """Collects the history line of one call of run-affected-tests.py.

    Each method catches each exception and keeps it as a note, so that a failure of the history never changes the
    result of the caller.
    """

    def __init__(self, build: Path, source: Path, jobs: int) -> None:
        """Start the line of one call.

        Args:
            build: The build directory
            source: The source tree
            jobs: The job count of the build and of the test run
        """
        self.build = build
        self.problems: list[str] = []
        self.line: dict[str, object] = {"format": LINE_FORMAT, "time": iso_time(time.time())}
        self._log_before: dict[str, ninja_files.LogRecord] = {}
        self._build_timer = StepTimer()
        self.test_timer = StepTimer()
        try:
            self.line.update(git_state(source))
            self.line["kind"] = cost_meter.read_kind(str(build))
            self.line["jobs"] = jobs
            self.line["cpus"] = len(os.sched_getaffinity(0))
            self.line["load"] = round(os.getloadavg()[0], 1)
        except Exception as problem:  # noqa: BLE001
            self.problems.append(f"the state of the call: {problem}")
        self.line["build"] = None
        self.line["tests"] = None

    def start_build(self) -> StepTimer:
        """Read the ninja log before the build, and return the timer of the build step."""
        try:
            self._log_before = ninja_files.read_log(self.build)
        except Exception as problem:  # noqa: BLE001
            self.problems.append(f"the ninja log before the build: {problem}")
        return self._build_timer

    def finish_build(self, status: int) -> None:
        """Keep the facts of the build after the build step.

        Args:
            status: The exit status of the build
        """
        facts: dict[str, object] = {"status": status, **self._build_timer.result}
        try:
            after = ninja_files.read_log(self.build)
            facts.update(summarize_build(self.build, load_graph(self.build), self._log_before, after,
                                         self._build_timer.started_epoch))
        except Exception as problem:  # noqa: BLE001
            self.problems.append(f"the edges of the build: {problem}")
        self.line["build"] = facts

    def finish_tests(self, status: int, counts: dict[str, int]) -> None:
        """Keep the facts of the test run after the ctest step.

        Args:
            status: The exit status of ctest, or 0 when no test ran
            counts: The selected, passed, failed, not run and skipped tests
        """
        self.line["tests"] = {"status": status, **self.test_timer.result, **counts}

    def save(self) -> str | None:
        """Append the line to the history.

        Returns:
            None on success, else the text of each problem of the history
        """
        try:
            if self.problems:
                self.line["problems"] = self.problems
            append_line(self.build, self.line)
        except Exception as problem:  # noqa: BLE001
            self.problems.append(f"the append to the history: {problem}")
            return "; ".join(self.problems)
        return "; ".join(self.problems) if self.problems else None


def iso_time(epoch: float) -> str:
    """Return a time as ISO 8601 text in UTC, with seconds."""
    return time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime(epoch))


def history_path(build: Path) -> Path:
    """Return the history file of a build directory."""
    return build / HISTORY_DIR / HISTORY_NAME


def append_line(build: Path, line: dict[str, object]) -> None:
    """Append one line to the history, and keep the newer half of the lines when the file is larger than TRIM_BYTES.

    Args:
        build: The build directory
        line: The line
    """
    path = history_path(build)
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(line, separators=(",", ":")) + "\n")
    if path.stat().st_size > TRIM_BYTES:
        lines = path.read_text(encoding="utf-8").splitlines(keepends=True)
        staging = path.with_name(f".{path.name}.{os.getpid()}")
        staging.write_text("".join(lines[len(lines) // 2:]), encoding="utf-8")
        os.replace(staging, path)


def read_lines(build: Path) -> list[dict[str, object]]:
    """Return each line of the history of a build directory, oldest first.

    A line that is not a JSON object is skipped.

    Args:
        build: The build directory

    Returns:
        The lines
    """
    lines = []
    try:
        text = history_path(build).read_text(encoding="utf-8")
    except FileNotFoundError:
        return lines
    for raw in text.splitlines():
        try:
            value = json.loads(raw)
        except ValueError:
            continue
        if isinstance(value, dict):
            lines.append(value)
    return lines


# ── The table ──────────────────────────────────────────────────────────────


def cell(value: object, digits: int = 1) -> str:
    """Return the text of one value of the table: a dash for no value, and a number with the given digits."""
    if value is None:
        return "-"
    if isinstance(value, bool):
        return "yes" if value else "no"
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def row_cells(line: dict[str, object]) -> list[str]:
    """Return the cells of one history line, in the order of TABLE_HEADER."""
    build = line.get("build") if isinstance(line.get("build"), dict) else {}
    tests = line.get("tests") if isinstance(line.get("tests"), dict) else {}
    assert isinstance(build, dict) and isinstance(tests, dict)
    commit = line.get("commit")
    jobs = line.get("jobs")
    cpu = number(build, "cpu_s")
    cpu_bound = round(cpu / jobs, 1) if cpu is not None and isinstance(jobs, int) and jobs > 0 else None
    kinds = build.get("kinds") if isinstance(build.get("kinds"), dict) else {}
    assert isinstance(kinds, dict)
    others = f"{cell(number(build, 'others_pct'), 0)}/{cell(number(tests, 'others_pct'), 0)}"
    when = str(line.get("time", "-")).replace("T", " ").rstrip("Z")[:16]
    return [when, (str(commit)[:9] if commit else "-"), cell(line.get("tree")), cell(jobs), cell(line.get("load")),
            others, cell(number(build, "wall_s")), cell(number(build, "reconfigure_s")),
            cell(number(build, "critical_path_s")), cell(cpu_bound),
            cell(cpu, 0), cell(kinds.get("compile")), cell(build.get("ccache_hits")), cell(kinds.get("link")),
            cell(number(build, "peak_memory_gb")), cell(number(tests, "wall_s")), cell(number(tests, "cpu_s"), 0),
            cell(tests.get("selected")), cell(tests.get("failed"))]


TABLE_HEADER = ("when (UTC)", "commit", "tree", "jobs", "load", "others %", "build s", "cmake s", "path s", "CPU/jobs s",
                "build CPU s", "compiles", "hits", "links", "peak GB", "tests s", "test CPU s", "tests", "failed")


def table(lines: list[dict[str, object]]) -> str:
    """Return the history lines as a table with a header and aligned columns."""
    rows = [list(TABLE_HEADER), *(row_cells(line) for line in lines)]
    widths = [max(len(row[index]) for row in rows) for index in range(len(TABLE_HEADER))]
    return "\n".join("  ".join(text.rjust(width) if index > 2 else text.ljust(width)
                               for index, (text, width) in enumerate(zip(row, widths, strict=True))).rstrip()
                     for row in rows)


def show(build: Path, count: int) -> int:
    """Print the last lines of the history of a build directory as a table.

    Args:
        build: The build directory
        count: The number of lines

    Returns:
        0 when the history exists, 2 otherwise
    """
    lines = read_lines(build)
    if not lines:
        print(f"loop_history: {history_path(build)} holds no line.  python3 utils/scripts/run-affected-tests.py "
              f"{build} writes one line for each call.")
        return 2
    shown = lines[-count:]
    print(table(shown))
    budgets = check_report.read_budgets()
    for line in shown:
        build_part = line.get("build")
        if isinstance(build_part, dict) and build_part.get("critical_path"):
            path = build_part["critical_path"]
            assert isinstance(path, list)
            print(f"{line.get('time')}: critical path {' -> '.join(str(item) for item in path)}")
        for finding in judge_line(line, budgets, str(history_path(build))):
            print(f"{line.get('time')}: {finding.text()}")
        for problem in line.get("problems") or []:
            print(f"{line.get('time')}: problem of the history: {problem}")
    return 0


# ── The self-test ──────────────────────────────────────────────────────────

PLANTED_NINJA = (
    "build CMakeFiles/cmake.verify_globs: VERIFY_GLOBS | CMakeFiles/force\n"
    "build build.ninja: RERUN_CMAKE CMakeFiles/cmake.verify_globs\n"
    "build a.o: CXX_COMPILER__a_unscanned_Debug a.cpp || order_a\n"
    "build order_a: phony || gen.h\n"
    "build gen.h | ${cmake_ninja_workdir}gen.h: CUSTOM_COMMAND gen.in\n"
    "build b.o: CXX_COMPILER__b_unscanned_Debug b.cpp\n"
    "build liba.a: CXX_STATIC_LIBRARY_LINKER__a_Debug a.o\n"
    "build libs: phony liba.a\n"
    "build tool: CXX_EXECUTABLE_LINKER__tool_Debug c.o libs\n"
    "build c.o: CXX_COMPILER__c_unscanned_Debug c.cpp\n"
    "build other: CXX_EXECUTABLE_LINKER__other_Debug b.o\n"
    "build all: phony tool other\n")


def self_test() -> int:
    """Check the parts of the history on planted build directories.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    before = HostSample(100.0, 4, 1.0, 0.5)
    after = HostSample(110.0, 4, 3.0, 1.0)
    expect("others_share gives the busy time that the children did not use, as a share of the host",
           others_share(before, after, 2.0) == 100.0 * 7.5 / 8.0)
    expect("others_share gives no share without /proc/stat", others_share(HostSample(None, 4, 0, 0), after, 1) is None)
    expect("others_share does not go below zero",
           others_share(HostSample(100.0, 4, 0, 0), HostSample(100.5, 4, 2.0, 0), 1.0) == 0.0)
    expect("peak_concurrent_gb adds the steps that overlap, and two steps that touch do not overlap",
           peak_concurrent_gb([{"end": 10.0, "wall_s": 10.0, "peak_rss_kb": KB_PER_GB},
                               {"end": 6.0, "wall_s": 2.0, "peak_rss_kb": 2 * KB_PER_GB},
                               {"end": 14.0, "wall_s": 4.0, "peak_rss_kb": 4 * KB_PER_GB},
                               {"end": 1.0, "wall_s": 1.0}]) == 4.0)
    expect("peak_concurrent_gb gives no value for no block", peak_concurrent_gb([]) is None)

    with tempfile.TemporaryDirectory(prefix="loop-history-") as work:
        build = Path(work)
        (build / "build.ninja").write_text(PLANTED_NINJA, encoding="utf-8")
        graph = load_graph(build)
        assert graph is not None
        expect("load_graph writes the cache", (build / HISTORY_DIR / GRAPH_NAME).is_file())
        cached = load_graph(build)
        expect("load_graph reads the same graph from the cache",
               cached is not None and cached.deps == graph.deps and cached.edge_of == graph.edge_of)
        expect("compact names the kinds and maps an implicit output with the work directory to its edge",
               graph.kinds[graph.edge_of[str(build / "liba.a")]] == "archive"
               and graph.edge_of[str(build / "gen.h")] == 4)
        edge = graph.edge_of

        def span(name: str, start: int, end: int, mtime: int = 1) -> tuple[str, ninja_files.LogRecord]:
            return str(build / name), ninja_files.LogRecord(start, end, mtime, "h")

        log_before = dict([span("CMakeFiles/cmake.verify_globs", 10, 20), span("a.o", 0, 1), span("b.o", 0, 1)])
        log_after = dict([span("CMakeFiles/cmake.verify_globs", 11, 22), span("gen.h", 100, 600, 2),
                          span("a.o", 600, 5600, 2), span("liba.a", 5600, 6600, 2), span("c.o", 100, 4100, 2),
                          span("tool", 6600, 9600, 2), span("b.o", 0, 1), span("other", 100, 400, 2)])
        spans, unknown = edge_spans(graph, log_after, ninja_files.ran_outputs(log_before, log_after))
        length, chain = critical_path(graph, spans)
        expect("critical_path follows an order-only input and a phony input to the longest chain",
               length == 9500 and chain == [edge[str(build / name)] for name in ("gen.h", "a.o", "liba.a", "tool")]
               and unknown == 0)
        expect("critical_path counts no edge that did not run, and no glob check",
               critical_path(graph, {edge[str(build / "CMakeFiles/cmake.verify_globs")]: (0, 50000)}) == (0, []))

        for name, record in (("a.o", {"step": "compile", "result": "built",
                                      "cost": {"cpu_s": 4.0, "wall_s": 5.0, "end": time.time(),
                                               "peak_rss_kb": KB_PER_GB, "instructions": 2_000_000_000}}),
                             ("c.o", {"step": "compile", "result": "hit"}),
                             ("tool", {"step": "link", "result": "built",
                                       "cost": {"cpu_s": 1.5, "wall_s": 3.0, "end": time.time() - 100,
                                                "peak_rss_kb": KB_PER_GB // 2}})):
            (build / f"{name}{cost_meter.RECORD_SUFFIX}").write_text(json.dumps(record), encoding="utf-8")
        old_record = build / f"other{cost_meter.RECORD_SUFFIX}"
        old_record.write_text(json.dumps({"step": "link", "result": "built", "cost": {"cpu_s": 9.0}}), encoding="utf-8")
        os.utime(old_record, (time.time() - 3600, time.time() - 3600))
        facts = summarize_build(build, graph, log_before, log_after, time.time() - 10)
        expect("summarize_build counts the edges by kind, without the glob check",
               facts["edges"] == 6 and facts["kinds"] == {"archive": 1, "cmake": 1, "compile": 2, "custom": 1,
                                                          "link": 2})
        expect("summarize_build adds the CPU time and the instructions of the compiles and counts a hit",
               facts["compile_records"] == 2 and facts["ccache_hits"] == 1 and facts["compile_cpu_s"] == 4.0
               and facts["compile_instructions_g"] == 2.0)
        expect("summarize_build reads no record that is older than the build",
               facts["link_records"] == 1 and facts["link_cpu_s"] == 1.5)
        expect("summarize_build gives the critical path, the longest edge and the peak memory",
               facts["critical_path_s"] == 9.5 and facts["critical_path"] == ["gen.h", "a.o", "liba.a", "tool"]
               and facts["longest_edge"] == "a.o" and facts["longest_edge_s"] == 5.0 and facts["peak_memory_gb"] == 1.0)
        log_after[str(build / "build.ninja")] = ninja_files.LogRecord(0, 6300, 3, "h")
        expect("summarize_build gives the time of a CMake run inside the build",
               summarize_build(build, graph, log_before, log_after, time.time() - 10)["reconfigure_s"] == 6.3)
        (build / "build.ninja").write_text(PLANTED_NINJA + "build extra: phony all\n", encoding="utf-8")
        changed = load_graph(build)
        expect("load_graph reads build.ninja again after it changes",
               changed is not None and str(build / "extra") in changed.edge_of)

        expect("configure_time gives nothing for a build directory with no configure-time.txt",
               configure_time(build) is None)
        written = (build / "build.ninja").stat().st_mtime
        (build / CONFIGURE_TIME_FILE).write_text(f"{written - 6.5} {written - 2.0}\n", encoding="ascii")
        times = configure_time(build)
        expect("configure_time gives the configure, the generate step and their sum",
               times is not None and times["configure_s"] == 4.5 and times["generate_s"] == 2.0
               and times["file_api_s"] == 0.0 and times["total_s"] == 6.5)
        reply = build / FILE_API_REPLY
        reply.mkdir(parents=True)
        for name, offset in (("index-old.json", -100.0), ("index-new.json", 1.5)):
            (reply / name).write_text("{}", encoding="ascii")
            os.utime(reply / name, (written + offset, written + offset))
        times = configure_time(build)
        expect("configure_time adds the reply of the file API that CMake writes after build.ninja",
               times is not None and times["file_api_s"] == 1.5 and times["total_s"] == 8.0)
        (build / CONFIGURE_TIME_FILE).write_text(f"{written + 1.0} {written + 3.0}\n", encoding="ascii")
        expect("configure_time gives nothing when the last configure wrote no build.ninja after its end",
               configure_time(build) is None)
        budgets = {row: check_report.Budget(row, limit, limit, "u", "m")
                   for row, limit in (("noop-build-time", 0.5), ("build-peak-memory", 100.0), ("configure-time", 10.0))}
        idle = {"build": {"status": 0, "wall_s": 0.9, "edges": 0, "peak_memory_gb": None}}
        expect("judge_line warns on a slow build that ran no edge",
               [(found.check, found.level) for found in judge_line(idle, budgets, "h")]
               == [("noop-build-time", "warning")])
        busy = {"build": {"status": 0, "wall_s": 90.0, "edges": 900, "peak_memory_gb": 150.0, "reconfigure_s": 12.0}}
        expect("judge_line warns on the peak memory and on a slow CMake run, and not on the time of a build with work",
               sorted(found.check for found in judge_line(busy, budgets, "h")) == ["build-peak-memory",
                                                                                    "configure-time"])
        expect("judge_line gives no warning for a line with no build or with values under the thresholds",
               judge_line({"build": None}, budgets, "h") == []
               and judge_line({"build": {"status": 0, "wall_s": 0.2, "edges": 0}}, budgets, "h") == [])

        expect("read_lines gives no line for a build with no history", read_lines(build) == [])
        recorder = Recorder(build, build, 4)
        with recorder.start_build():
            subprocess.run([sys.executable, "-c", "sum(range(20_000_000))"], check=True)
        recorder.finish_build(0)
        with recorder.test_timer:
            pass
        recorder.finish_tests(0, {"selected": 3, "passed": 3, "failed": 0, "not_run": 0, "skipped": 1})
        problem = recorder.save()
        lines = read_lines(build)
        expect("a recorder appends one line with the build and the tests, and with no commit outside git",
               problem is None and len(lines) == 1 and lines[0]["commit"] is None and lines[0]["jobs"] == 4
               and lines[0]["tests"]["selected"] == 3 and lines[0]["build"]["status"] == 0)
        expect("a recorder measures the CPU time of the build step", lines[0]["build"]["cpu_s"] > 0)
        with history_path(build).open("a", encoding="utf-8") as handle:
            handle.write("not json\n")
        expect("read_lines skips a line that is not JSON", len(read_lines(build)) == 1)
        printed = table(read_lines(build))
        expect("table gives a header and one row for each line", len(printed.splitlines()) == 2
               and printed.splitlines()[0].startswith("when (UTC)"))
        filler = json.dumps({"format": LINE_FORMAT, "time": "x", "pad": "y" * 1000}) + "\n"
        with history_path(build).open("a", encoding="utf-8") as handle:
            handle.write(filler * (TRIM_BYTES // len(filler) + 10))
        append_line(build, {"format": LINE_FORMAT, "time": "last"})
        kept = read_lines(build)
        expect("append_line keeps the newer half of the lines when the file grows too large",
               history_path(build).stat().st_size <= TRIM_BYTES and kept[-1]["time"] == "last")

    if failures:
        print(f"loop_history --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("loop_history --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Print the history of a build directory as a table, or run the self-test.

    Args:
        argv: The arguments

    Returns:
        The exit status
    """
    if argv == ["--self-test"]:
        return self_test()
    parser = argparse.ArgumentParser(description="Print the last lines of the loop history of a build directory.")
    parser.add_argument("build_dir", type=Path)
    parser.add_argument("-n", "--lines", type=int, default=DEFAULT_LINES)
    options = parser.parse_args(argv)
    if options.lines < 1:
        print("loop_history: give -n a positive number.", file=sys.stderr)
        return 2
    return show(options.build_dir.resolve(), options.lines)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
