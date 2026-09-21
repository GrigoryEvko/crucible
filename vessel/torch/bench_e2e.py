#!/usr/bin/env python3
"""End-to-end cost of the PyTorch vessel on a real training loop.

WHAT THIS MEASURES, AND WHY IT IS NOT THE SAME AS THE C++ BENCHES

`bench/baselines/record_leaf.json` times `Vigil::dispatch_op` with the ring and
the metadata already in hand.  That is the last step of the per-op path.  The
steps ahead of it -- the Python binding, the ATen dispatcher, the unboxed
recording kernel and the metadata bridge -- are the rest of it, and no figure in
the tree covered them.  This file covers the whole path, from the step a user
writes to the values coming back.

THE WORKLOADS ARE MODELS, NOT ONE OPERATOR

A loop over one tiny operator measures the harness more than the vessel, and it
puts the runtime in a state no user reaches: the Vigil sees a five-op signature,
replays it after a couple of hundred ops, and the recording window is too short
to measure. A model gives the runtime the shape it was built for.

    mlp_train   A four-layer MLP, batch 32, forward, loss, backward, SGD step.
                73 ATen operations per iteration
    mlp_infer   The same model, forward only, under no_grad.
                11 ATen operations per iteration
    add1        One-element add. A microbenchmark, kept because it isolates the
                per-op cost that the models bury under real arithmetic

RECORDING IS A STARTUP COST, REPLAY IS THE STEADY STATE

A real loop holds its shapes constant, so the Vigil records the first few
iterations, builds a region, and replays it for the rest of the run. That is why
the two attached arms are not measured the same way:

    INACTIVE   The dispatch key is off. PyTorch alone. This is the control
    RECORD     The first iterations after a fresh attach, while the Vigil still
               records. A startup cost, measured over many attaches because one
               attach only offers a few iterations of it
    COMPILED   The steady state: the Vigil replays the region. One attach offers
               as many iterations as asked, so this arm carries the tail

A fourth arm belongs to a later layer of the runtime. Measured from the source:
`call_recording` in record_kernel.h calls `Op::redispatch` on every path and
then discards the result of `dispatch_op_pure`, so a replayed operation still
executes eagerly and the replay only replaces the ring append. This tier
therefore cannot beat PyTorch, and a shadow tensor type is what would let a
replayed op return a handle without reaching the kernel. The vessel has no such
type. Nothing here approximates the arm that needs one.

THREE ARMS, SEPARATE PROCESSES

The dispatch library's schema table is a process-global static, so an arm that
ran after another in one process would read a table the earlier arm filled.
Every (workload, arm) pair runs in a process of its own.

USAGE

    taskset -c 88 .venv-torch/bin/python vessel/torch/bench_e2e.py
    taskset -c 88 .venv-torch/bin/python vessel/torch/bench_e2e.py \
        --workload mlp_train --arm record

The pin matters. Refer to the Code Guide, "Measurement discipline". Set
CRUCIBLE_BUILD_DIR to the directory holding the libraries under test.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import subprocess
import sys
import time
from pathlib import Path

# bench_e2e.py sits beside crucible_native.py and is run by path, so the
# vessel directory is not already on the import path in every invocation.
sys.path.insert(0, str(Path(__file__).resolve().parent))

RUNS = 10
SEED = 42

# Iterations per run, per workload. Chosen so a run is about a fifth of a
# second: long enough for a stable per-run p50, short enough that ten runs of
# nine (workload, arm) pairs finish in about a minute.
ITERATIONS = {"mlp_train": 1_000, "mlp_infer": 4_000, "add1": 100_000}

# The model. Small enough that one core runs an iteration in a few hundred
# microseconds, large enough that each operation does real arithmetic: at batch
# 32 and width 128 an addmm takes about 2 us, so the vessel's per-op cost is a
# few percent of it rather than a rounding error on it.
WIDTH = 128
BATCH = 32
DEPTH = 4

# An attach builds a ring and a MetaLog of about 208 MB together and touches
# them for the first time, so the RECORD arm holds its attach count down. It
# takes every iteration a window offers, which is what makes a small count
# enough.
RECORD_ATTACH_BUDGET = {"mlp_train": 40, "mlp_infer": 40, "add1": 80}
RECORD_SAMPLES_PER_RUN = {"mlp_train": 120, "mlp_infer": 200, "add1": 2_000}

# The add1 microbenchmark reaches a replayed region after a couple of hundred
# ops, so it is timed in chunks and only chunks the mode brackets are kept.
ADD1_CHUNK = 10
ADD1_KEEP_CHUNKS = 6

# The run-to-run spread of p50 above which the host is judged to be contended
# and the numbers are discarded rather than reported with a caveat.
VARIANCE_GATE = 0.05


# =====================================================================
# Statistics
# =====================================================================

def percentile(sorted_samples: list[int], q: float) -> int:
    """Nearest-rank percentile of an already-sorted sample list.

    Args:
        sorted_samples: Samples in ascending order, at least one
        q: The percentile to take, between 0.0 and 1.0

    Returns:
        The sample at the nearest rank
    """
    if not sorted_samples:
        raise ValueError("percentile of an empty sample list")
    rank = int(q * (len(sorted_samples) - 1) + 0.5)
    return sorted_samples[rank]


def median(values: list[float]) -> float:
    """The middle value, or the mean of the two middle values."""
    ordered = sorted(values)
    mid = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[mid]
    return (ordered[mid - 1] + ordered[mid]) / 2.0


def across_runs(per_run: list[list[int]]) -> dict:
    """Reduce a set of runs to one reported row.

    The percentiles come from every sample of every run pooled together, because
    a percentile wants the whole population: the RECORD arm's runs are short,
    and a p99.9 taken inside one of them would be its second-worst sample rather
    than an estimate of anything.

    The variance gate reads the per-run p50 instead. That statistic is stable
    within a short run and independent between runs, which is what a throttling
    check needs.

    Args:
        per_run: The per-iteration samples of each run, at least one run, each
            holding at least one sample

    Returns:
        The reported row, with the p50 spread that the variance gate reads
    """
    pooled = sorted(sample for run in per_run for sample in run)
    p50s = [median(run) for run in per_run]
    p50_median = median(p50s)
    spread = 0.0 if p50_median == 0 else (max(p50s) - min(p50s)) / p50_median

    # A single run has an unmeasured spread, not a zero one, so it cannot
    # satisfy the gate however steady it looks.
    passes = len(per_run) >= 2 and spread <= VARIANCE_GATE

    return {"p50_ns": percentile(pooled, 0.50),
            "p99_ns": percentile(pooled, 0.99),
            "p999_ns": percentile(pooled, 0.999),
            "max_ns": pooled[-1],
            "runs": len(per_run),
            "samples_total": len(pooled),
            "samples_per_run_min": min(len(run) for run in per_run),
            "p50_spread": round(spread, 4) if len(per_run) >= 2 else None,
            "gate": 1 if passes else 0}


# =====================================================================
# The timed loop
# =====================================================================
#
# One clock read per iteration, diffed afterwards. On a model that is a
# rounding error: the read costs about 50 ns against an iteration of 40 to 170
# microseconds. On the add1 microbenchmark it is not, which is why that
# workload also reports the floor.

def time_iterations(count: int, step, stamps: list[int], clock) -> None:
    """Fill stamps[0..count] with a reading taken after each step."""
    stamps[0] = clock()
    for i in range(count):
        step()
        stamps[i + 1] = clock()


def time_floor(count: int, stamps: list[int], clock) -> None:
    """Fill stamps[0..count] with the same loop, the step taken out."""
    stamps[0] = clock()
    for i in range(count):
        stamps[i + 1] = clock()


def diffs(stamps: list[int], count: int) -> list[int]:
    """Per-iteration durations from count+1 adjacent timestamps."""
    return [stamps[i + 1] - stamps[i] for i in range(count)]


# =====================================================================
# Workloads
# =====================================================================

def build_workload(kind: str):
    """Build one workload and return what every arm needs to run it.

    Args:
        kind: One of the keys of ITERATIONS

    Returns:
        The model to attach, the optimizer or None, and the callable that
        performs one iteration

    Raises:
        ValueError: If the workload is not one this file defines
    """
    import torch
    import torch.nn as nn

    torch.manual_seed(SEED)

    if kind == "add1":
        # No model of its own, but attach wants one. Identity adds no operation
        # to the iteration, because nothing calls it.
        left, right = torch.ones(1), torch.ones(1)

        def add_step() -> None:
            """One elementwise add."""
            left + right

        return nn.Identity(), None, add_step

    layers: list = []
    for index in range(DEPTH - 1):
        layers += [nn.Linear(WIDTH, WIDTH), nn.ReLU()]
        del index
    layers += [nn.Linear(WIDTH, 10)]
    model = nn.Sequential(*layers)
    inputs = torch.randn(BATCH, WIDTH)

    if kind == "mlp_infer":
        model.eval()

        def infer_step() -> None:
            """One forward pass, no autograd."""
            with torch.no_grad():
                model(inputs)

        return model, None, infer_step

    if kind == "mlp_train":
        target = torch.randn(BATCH, 10)
        optimizer = torch.optim.SGD(model.parameters(), lr=0.01)
        loss_fn = nn.MSELoss()

        def train_step() -> None:
            """One forward pass, backward pass and optimizer step."""
            optimizer.zero_grad(set_to_none=True)
            loss_fn(model(inputs), target).backward()
            optimizer.step()

        return model, optimizer, train_step

    raise ValueError(f"unknown workload: {kind}")


def count_ops(step) -> int:
    """Count the ATen operations one iteration dispatches.

    Args:
        step: The callable that performs one iteration

    Returns:
        The number of operations, which turns a per-iteration figure into a
        per-op one
    """
    from torch.utils._python_dispatch import TorchDispatchMode

    class Counter(TorchDispatchMode):
        """Counts every operation the dispatcher routes through it."""

        def __init__(self) -> None:
            self.seen = 0

        def __torch_dispatch__(self, func, types, args=(), kwargs=None):
            """Count one operation and let it run."""
            self.seen += 1
            return func(*args, **(kwargs or {}))

    with Counter() as counter:
        step()
    return counter.seen


# =====================================================================
# Arms
# =====================================================================

def arm_inactive(kind: str, runs: int, count: int) -> dict:
    """PyTorch alone, with the dispatch key off.  The control."""
    _model, _optimizer, step = build_workload(kind)
    clock = time.perf_counter_ns
    stamps = [0] * (count + 1)

    for _ in range(min(count, 200)):
        step()                                    # warm the caches

    per_run = []
    for _ in range(runs):
        time_iterations(count, step, stamps, clock)
        per_run.append(diffs(stamps, count))

    out = {"arm": "INACTIVE", "workload": kind, "attached": False,
           "measured": across_runs(per_run),
           "ops_per_iteration": count_ops(step)}

    if kind == "add1":
        floor_runs = []
        for _ in range(runs):
            time_floor(count, stamps, clock)
            floor_runs.append(diffs(stamps, count))
        out["floor"] = across_runs(floor_runs)
    return out


def arm_record(kind: str, runs: int, count: int) -> dict:
    """The first iterations after a fresh attach, while the Vigil records.

    A run takes a fresh Vigil as many times as it needs, because one attach only
    offers the handful of iterations that pass before the background thread
    publishes a region. That is the honest shape of the cost: a real loop pays
    it once at startup, not once per iteration.
    """
    from crucible_native import attach

    model, optimizer, step = build_workload(kind)
    clock = time.perf_counter_ns
    target = RECORD_SAMPLES_PER_RUN[kind]
    budget = RECORD_ATTACH_BUDGET[kind]

    per_run = []
    attaches = 0
    windows: list[int] = []
    last_state: dict = {}

    for _ in range(runs):
        samples: list[int] = []
        spent = 0
        while len(samples) < target and spent < budget:
            with attach(model, optimizer) as ctx:
                spent += 1
                attaches += 1
                if ctx.is_compiled():
                    raise RuntimeError("the Vigil was already replaying at attach")

                window = record_window(kind, ctx, step, clock, target - len(samples))
                samples += window
                windows.append(len(window))

                last_state = vigil_state(ctx)

        if samples:
            per_run.append(samples)

    if not per_run:
        raise RuntimeError("no iteration completed while the Vigil was recording")

    ordered = sorted(windows)
    return {"arm": "RECORD", "workload": kind, "attached": True,
            "measured": across_runs(per_run),
            "attaches": attaches,
            "samples_per_attach": {"min": ordered[0],
                                   "median": ordered[len(ordered) // 2],
                                   "max": ordered[-1]},
            "vigil": last_state}


def record_window(kind: str, ctx, step, clock, want: int) -> list[int]:
    """Time iterations of one attach for as long as the Vigil still records.

    On a model an iteration is many operations, so the mode is checked between
    iterations and a single iteration is the unit that can be filed under one
    mode. On add1 an iteration is one operation and the check would cost more
    than the operation, so that workload is timed in chunks.

    Args:
        kind: The workload being run
        ctx: The live controller
        step: The callable that performs one iteration
        clock: The monotonic nanosecond clock to read
        want: Stop once this many samples are in hand

    Returns:
        The samples taken while the Vigil recorded
    """
    if kind == "add1":
        return add1_window(ctx, step, clock, want)

    samples: list[int] = []
    previous = clock()
    while len(samples) < want and not ctx.is_compiled():
        step()
        now = clock()
        # The mode is read after the iteration ran. A True here means the
        # iteration straddled the transition, so it belongs to neither mode.
        if not ctx.is_compiled():
            samples.append(now - previous)
        previous = now
    return samples


def add1_window(ctx, step, clock, want: int) -> list[int]:
    """Time chunks of the add1 loop that the recording mode brackets.

    The per-op cost drifts across a window as the caches warm, and windows
    differ in length, so every window contributes the same chunks from the same
    position. A window with too few chunks is skipped rather than part-counted:
    measured, keeping every middle chunk let the window-length mix of a run move
    its p50 to a 6.4% spread and voided the arm.
    """
    stamps = [0] * (ADD1_CHUNK + 1)
    collected: list[list[int]] = []
    while len(collected) < ADD1_KEEP_CHUNKS + 2 and not ctx.is_compiled():
        time_iterations(ADD1_CHUNK, step, stamps, clock)
        if ctx.is_compiled():
            break                                 # the chunk straddles; drop it
        collected.append(diffs(stamps, ADD1_CHUNK))

    if len(collected) < ADD1_KEEP_CHUNKS + 2:
        return []
    kept = [s for c in collected[1:1 + ADD1_KEEP_CHUNKS] for s in c]
    return kept[:want]


def arm_compiled(kind: str, runs: int, count: int) -> dict:
    """The steady state: the Vigil replays the region.

    A real loop spends all but its first few iterations here, so this is the arm
    whose tail matters. One attach per run offers as many iterations as asked.
    """
    from crucible_native import attach

    model, optimizer, step = build_workload(kind)
    clock = time.perf_counter_ns
    stamps = [0] * (count + 1)

    per_run = []
    attaches = 0
    diverged_total = 0
    interrupted = 0
    last_state: dict = {}

    for _ in range(runs):
        with attach(model, optimizer) as ctx:
            attaches += 1
            if not pump_until_replaying(ctx, step, 30.0):
                last_state = vigil_state(ctx)
                continue

            time_iterations(min(count, 100), step, stamps, clock)   # settle

            time_iterations(count, step, stamps, clock)
            samples = diffs(stamps, count)

            # A divergence inside the window means some of these iterations
            # recorded rather than replayed. The run is reported as interrupted
            # rather than silently mixed into the steady-state row.
            if ctx.is_compiled():
                per_run.append(samples)
            else:
                interrupted += 1

            diverged_total += ctx.diverged_count()
            last_state = vigil_state(ctx)

    if not per_run:
        return {"arm": "COMPILED", "workload": kind, "attached": True,
                "unavailable": "no run completed entirely while the Vigil replayed",
                "attaches": attaches, "runs_interrupted": interrupted,
                "vigil": last_state}

    return {"arm": "COMPILED", "workload": kind, "attached": True,
            "measured": across_runs(per_run),
            "attaches": attaches,
            "runs_interrupted_by_a_divergence": interrupted,
            "divergences_over_all_runs": diverged_total,
            "vigil": last_state}


def pump_until_replaying(ctx, step, seconds: float) -> bool:
    """Run the loop until the Vigil replays a region, or the time runs out.

    Args:
        ctx: The live controller
        step: The callable that performs one iteration
        seconds: How long to keep trying

    Returns:
        True when the Vigil replays
    """
    deadline = time.monotonic() + seconds
    while not ctx.is_compiled() and time.monotonic() < deadline:
        for _ in range(20):
            step()
        ctx.flush()
        time.sleep(0.002)            # let the background thread build the region
    return ctx.is_compiled()


def vigil_state(ctx) -> dict:
    """What the Vigil reports about itself."""
    return {"compiled": ctx.is_compiled(),
            "compiled_iterations": ctx.compiled_iterations(),
            "diverged": ctx.diverged_count(),
            "active_num_ops": ctx.active_num_ops(),
            "bg_iterations": ctx.bg_iterations()}


ARMS = {"inactive": arm_inactive, "record": arm_record, "compiled": arm_compiled}


# =====================================================================
# Host fingerprint
# =====================================================================

def _read(path: str) -> str:
    """The contents of a file, stripped, or the empty string when unreadable."""
    try:
        with open(path) as handle:
            return handle.read().strip()
    except OSError:
        return ""


def benchtune_state() -> str:
    """Whether the bench-host tuning unit holds the isolated core block.

    An inactive unit means the sibling thread of the bench core is schedulable
    and every number taken on it is void.
    """
    try:
        done = subprocess.run(["systemctl", "is-active", "crucible-benchtune"],
                              capture_output=True, text=True, timeout=10)
        return done.stdout.strip() or "unknown"
    except (OSError, subprocess.SubprocessError):
        return "unknown"


def host_fingerprint() -> dict:
    """Everything about the host that a later comparison has to match."""
    import torch

    model = ""
    for line in _read("/proc/cpuinfo").splitlines():
        if line.startswith("model name"):
            model = line.split(":", 1)[1].strip()
            break

    affinity = sorted(os.sched_getaffinity(0))
    cpu = affinity[0] if len(affinity) == 1 else None

    return {
        "cpu_model": model,
        "cpu_count": os.cpu_count(),
        "isolated_cpus": _read("/sys/devices/system/cpu/isolated"),
        "affinity": affinity if len(affinity) <= 8 else f"{len(affinity)} cpus",
        "pinned_cpu": cpu,
        "governor": _read(f"/sys/devices/system/cpu/cpu{cpu}/cpufreq/scaling_governor")
                    if cpu is not None else "",
        # The sibling list of the pinned core is the fact that decides whether
        # the numbers mean anything. A second entry here is a thread sharing the
        # core's execution units, and every sample is then void.
        "pinned_thread_siblings":
            _read(f"/sys/devices/system/cpu/cpu{cpu}/topology/thread_siblings_list")
            if cpu is not None else "",
        "offline_cpus": _read("/sys/devices/system/cpu/offline"),
        "benchtune": benchtune_state(),
        "kernel": platform.release(),
        "torch": torch.__version__,
        "python": platform.python_version(),
        "torch_num_threads": torch.get_num_threads(),
        "build_dir": os.environ.get("CRUCIBLE_BUILD_DIR", "(default search)"),
    }


# =====================================================================
# Arm process
# =====================================================================

def run_one_arm(kind: str, name: str, runs: int, count: int) -> dict:
    """Run one (workload, arm) pair in this process and return its report."""
    import torch

    if not time.get_clock_info("perf_counter").monotonic:
        raise RuntimeError("perf_counter is not monotonic on this host")

    # One thread, so no intra-op pool competes for the pinned core.
    torch.set_num_threads(1)

    report = ARMS[name](kind, runs, count)
    report["host"] = host_fingerprint()
    return report


# =====================================================================
# Hardware counters
# =====================================================================

PERF_EVENTS = ("cycles", "instructions", "L1-dcache-load-misses")


def perf_stat(kind: str, name: str, runs: int, count: int) -> dict:
    """Count cycles, instructions and L1d misses over one arm's process.

    The counters cover the whole process, interpreter startup included, so this
    is a profile of the arm and not a decomposition of one iteration. Read the
    instructions-per-cycle and the miss rate.

    A host with perf_event_paranoid at 2 counts user mode only, which the
    returned names carry as a `:u` suffix.

    Args:
        kind: The workload to run under perf
        name: The arm to run under perf
        runs: Runs to ask that arm for
        count: Iterations per run to ask that arm for

    Returns:
        A map of the counters, or one naming why they are absent
    """
    command = ["perf", "stat", "-x,", "-e", ",".join(PERF_EVENTS),
               sys.executable, str(Path(__file__).resolve()),
               "--workload", kind, "--arm", name, "--runs", str(runs),
               "--iterations", str(count), "--emit-json"]
    try:
        done = subprocess.run(command, capture_output=True, text=True)
    except OSError as err:
        return {"unavailable": f"perf could not be run: {err}"}

    counters: dict[str, float] = {}
    for line in done.stderr.splitlines():
        parts = line.split(",")
        if len(parts) < 3 or not parts[0].strip():
            continue
        try:
            value = float(parts[0])
        except ValueError:
            continue
        counters[parts[2].strip()] = value

    if not counters:
        return {"unavailable": "perf printed no counters"}

    out: dict = {"workload": kind, "arm": name, "events": counters,
                 "paranoid": _read("/proc/sys/kernel/perf_event_paranoid")}
    cycles = next((v for k, v in counters.items() if k.startswith("cycles")), 0.0)
    instructions = next((v for k, v in counters.items()
                         if k.startswith("instructions")), 0.0)
    if cycles:
        out["insn_per_cycle"] = round(instructions / cycles, 3)
    return out


# =====================================================================
# Orchestrator
# =====================================================================

def spawn(kind: str, name: str, runs: int, count: int) -> dict:
    """Run one (workload, arm) pair in a process of its own.

    The schema table of the dispatch library is a process-global static, so two
    arms in one process would not be independent.
    """
    done = subprocess.run(
        [sys.executable, str(Path(__file__).resolve()),
         "--workload", kind, "--arm", name, "--runs", str(runs),
         "--iterations", str(count), "--emit-json"],
        capture_output=True, text=True)
    if done.returncode != 0:
        raise RuntimeError(f"{kind}/{name} failed:\n{done.stdout}\n{done.stderr}")

    # The report is the last line of stdout. Walking back to the last line that
    # parses skips anything a library wrote first, where hunting for a brace
    # would trip over the report's own nested braces.
    for line in reversed(done.stdout.splitlines()):
        line = line.strip()
        if not line:
            continue
        try:
            return json.loads(line)
        except json.JSONDecodeError:
            continue
    raise RuntimeError(f"{kind}/{name} printed no JSON report:\n{done.stdout}")


def row(name: str, summary: dict, ops: int, note: str = "") -> dict:
    """One reported measurement, in the shape record_leaf.json uses.

    Args:
        name: What was measured
        summary: The reduced statistics of the runs
        ops: ATen operations per iteration, which turns the per-iteration
            figures into per-op ones
        note: Anything the reader needs in order not to misread the row

    Returns:
        The row
    """
    out = {"name": name,
           "p50_ns": summary["p50_ns"],
           "p99_ns": summary["p99_ns"],
           "p999_ns": summary["p999_ns"],
           "max_ns": summary["max_ns"],
           "p50_ns_per_op": round(summary["p50_ns"] / ops, 1) if ops else None,
           "ops_per_iteration": ops,
           "runs": summary["runs"],
           "samples_total": summary["samples_total"],
           "p50_spread": summary["p50_spread"],
           "gate": summary["gate"]}
    if note:
        out["_note"] = note
    return out


def delta_row(name: str, arm: dict, control: dict, ops: int) -> dict:
    """What one arm adds over the control, per iteration and per op."""
    a, c = arm["measured"], control["measured"]
    per_iteration = a["p50_ns"] - c["p50_ns"]
    return {"name": name,
            "p50_ns": per_iteration,
            "p99_ns": a["p99_ns"] - c["p99_ns"],
            "p999_ns": a["p999_ns"] - c["p999_ns"],
            "max_ns": None,
            "p50_ns_per_op": round(per_iteration / ops, 1) if ops else None,
            "p50_overhead_pct": round(100.0 * per_iteration / c["p50_ns"], 2)
                                if c["p50_ns"] else None,
            "ops_per_iteration": ops,
            "gate": 1 if (a["gate"] and c["gate"]) else 0,
            "_note": "max is absent: the worst iteration of two arms is not a "
                     "difference"}


def explain_variance(block: list, control: dict) -> None:
    """Say, for each void row, whether the host or the measurement varied.

    The gate cannot tell the two apart on its own. The control can: it runs on
    the same host in the same session, so a control that held its spread is
    evidence that the host was steady and the variance belongs to what the row
    measures. A control that also failed is evidence of the host.

    Args:
        block: The rows of one workload, edited in place
        control: The INACTIVE arm's report for that workload
    """
    steady = control["measured"]["gate"] == 1
    held = control["measured"]["p50_spread"]
    for entry in block:
        if entry.get("name") and entry.get("gate") == 0 \
                and entry.get("p50_spread") is not None:
            if steady:
                entry["_variance"] = (
                    "The spread is in what this row measures, not in the host: "
                    f"the control held {held:.2%} on the same host in the same "
                    "session. Recording races the background thread building "
                    "the region, so its cost varies by nature.")
            else:
                entry["_variance"] = (
                    "The control was void too, so the host was contended and "
                    "this row is void with it. Re-run on a quiet host.")


def workload_block(kind: str, reports: dict[str, dict]) -> list:
    """The rows for one workload: the three arms and the two deltas."""
    control = reports["inactive"]
    ops = control.get("ops_per_iteration", 0)
    block: list = [{"_workload": kind, "ops_per_iteration": ops}]

    block.append(row(f"{kind} [INACTIVE, PyTorch alone]", control["measured"], ops))
    if "floor" in control:
        block.append(row(f"{kind} [harness floor, clock read only]",
                         control["floor"], ops,
                         "the part of every sample that is not the workload"))

    rec = reports["record"]
    block.append(row(f"{kind} [RECORD, first iterations after attach]",
                     rec["measured"], ops,
                     "a startup cost: a real loop pays it once, over "
                     f"{rec.get('attaches')} attaches here because one attach "
                     "offers only the iterations before the region is built"))

    comp = reports["compiled"]
    if "measured" in comp:
        block.append(row(f"{kind} [COMPILED, steady state, region replays]",
                         comp["measured"], ops,
                         "dispatch still reaches the recording kernel and the "
                         "operation still executes eagerly; the replay only "
                         "replaces the ring append"))
    else:
        block.append({"name": f"{kind} [COMPILED, steady state, region replays]",
                      "p50_ns": None, "p99_ns": None, "p999_ns": None,
                      "max_ns": None, "gate": 0,
                      "_unavailable": comp.get("unavailable", "not reached"),
                      "vigil": comp.get("vigil", {})})

    block.append(delta_row(f"{kind}: RECORD - INACTIVE [startup cost per iteration]",
                           rec, control, ops))
    if "measured" in comp:
        block.append(delta_row(
            f"{kind}: COMPILED - INACTIVE [steady-state cost per iteration]",
            comp, control, ops))

    block.append({"_vigil_state": {"record": rec.get("vigil", {}),
                                   "compiled": comp.get("vigil", {})},
                  "_record_samples_per_attach": rec.get("samples_per_attach"),
                  "_compiled_runs_interrupted":
                      comp.get("runs_interrupted_by_a_divergence"),
                  "_compiled_divergences": comp.get("divergences_over_all_runs")})
    explain_variance(block, control)
    return block


def build_document(by_workload: dict[str, dict], runs: int,
                   perf: list[dict] | None) -> list:
    """Assemble the baseline document.

    The shape follows bench/baselines/record_leaf.json: a list whose first
    element carries the commentary and a zeroed template of the fields, and
    whose later elements are the measurements.
    """
    first = next(iter(by_workload.values()))
    host = first["inactive"]["host"]

    header = {
        "_comment": "End-to-end cost of the PyTorch vessel on a real training loop: "
                    "the step a user writes, through the binding, the dispatcher, "
                    "the recording kernel and the Vigil. Every (workload, arm) pair "
                    "runs in its own process, because the dispatch library's schema "
                    "table is a process-global static.",
        "_comment2": f"{runs} runs per pair, one clock read per iteration. The "
                     "percentiles are pooled over every sample of every run; "
                     "p50_spread is the run-to-run spread of the per-run p50, which "
                     "is what the gate reads. max is the worst iteration observed. "
                     "The mean is absent by rule.",
        "_comment3": "A real loop holds its shapes constant, so the Vigil records the "
                     "first iterations and replays the rest. RECORD is therefore a "
                     "startup cost and COMPILED is the steady state, and only the "
                     "latter's tail describes a running job.",
        "_comment4": "gate=1 means the run-to-run spread of p50 stayed at or below "
                     "5%. gate=0 means the host was contended and the row is void.",
        "_comment5": "No fast-path row. Measured from the source: call_recording in "
                     "record_kernel.h calls Op::redispatch on every path and discards "
                     "the result of dispatch_op_pure, so a replayed operation still "
                     "executes eagerly. This tier adds cost rather than removing it. "
                     "A shadow tensor type would let a replayed op return a handle "
                     "without reaching the kernel; the vessel has none, and nothing "
                     "here approximates it.",
        "_comment6": "add1 is a microbenchmark, not a workload. It is kept because it "
                     "isolates the per-op cost that the models bury under real "
                     "arithmetic. Read the models for what the vessel costs a loop.",
        "host": host,
        "name": "", "p50_ns": 0, "p99_ns": 0, "p999_ns": 0, "max_ns": 0, "gate": 0,
    }

    document: list = [header]
    for kind in by_workload:
        document += workload_block(kind, by_workload[kind])

    if perf:
        document.append({
            "_perf_stat": perf,
            "_perf_note": "Counters over the whole arm process, interpreter startup "
                          "included, so this is a profile of the arm and not a "
                          "per-iteration decomposition. A `:u` suffix means the host "
                          "counts user mode only.",
        })
    return document


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workload", choices=sorted(ITERATIONS),
                        help="run one workload in this process")
    parser.add_argument("--arm", choices=sorted(ARMS),
                        help="run one arm in this process")
    parser.add_argument("--runs", type=int, default=RUNS)
    parser.add_argument("--iterations", type=int, default=0,
                        help="override the per-workload iteration count")
    parser.add_argument("--emit-json", action="store_true",
                        help="print the report as JSON, for the orchestrator")
    parser.add_argument("--out", default="bench/baselines/vessel_e2e.json")
    parser.add_argument("--no-perf", action="store_true",
                        help="skip the hardware-counter table")
    parser.add_argument("--perf-runs", type=int, default=2,
                        help="runs to give each arm under perf")
    args = parser.parse_args()

    if args.arm:
        kind = args.workload or "mlp_train"
        count = args.iterations or ITERATIONS[kind]
        report = run_one_arm(kind, args.arm, args.runs, count)
        print(json.dumps(report) if args.emit_json
              else json.dumps(report, indent=2))
        return 0

    kinds = [args.workload] if args.workload else list(ITERATIONS)
    by_workload: dict[str, dict] = {}
    for kind in kinds:
        count = args.iterations or ITERATIONS[kind]
        by_workload[kind] = {}
        for name in ("inactive", "record", "compiled"):
            print(f"[bench_e2e] {kind} / {name} ...", file=sys.stderr, flush=True)
            by_workload[kind][name] = spawn(kind, name, args.runs, count)

    perf = []
    if not args.no_perf:
        # The training loop is the workload the table is for. Its control comes
        # with it, because a counter total means little without one.
        for name in ("record", "compiled", "inactive"):
            print(f"[bench_e2e] perf stat over mlp_train / {name} ...",
                  file=sys.stderr, flush=True)
            perf.append(perf_stat("mlp_train", name, args.perf_runs,
                                  ITERATIONS["mlp_train"]))

    document = build_document(by_workload, args.runs, perf)
    text = json.dumps(document, indent=2) + "\n"

    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(text)
    print(text)
    print(f"[bench_e2e] wrote {out}", file=sys.stderr)

    voided = [d["name"] for d in document
              if d.get("name") and d.get("gate") == 0]
    if voided:
        print(f"[bench_e2e] VOID (spread > {VARIANCE_GATE:.0%}): {voided}",
              file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
