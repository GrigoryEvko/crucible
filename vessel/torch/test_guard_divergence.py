#!/usr/bin/env python3
"""A replayed op whose alpha, stride or element type changes is a divergence, and a changing step size gives no region.

The replay compares two words of each live op with the op of the region: the
schema hash and the guard word.  The guard word folds the layout of each input
and each scalar, so an op that computes a different result cannot replay as a
match.  Each check runs under CrucibleNative:

    alpha       aten::add.Tensor replays with alpha=1, then runs with alpha=2
    stride      aten::add.Tensor replays on a matrix, then runs on its transpose,
                which has the same extents and other strides
    dtype       aten::add.Tensor replays on float32, then runs on float64 of the
                same extents
    step size   AdamW with no fused kernel computes its step size in Python at
                each iteration and gives it to aten::div and aten::addcdiv_ as
                a scalar.  The scalar changes at each iteration, so the loop
                has no period: the Vigil records, builds no region and counts
                no divergence
    fused       AdamW with the fused kernel gives it constant scalars and reads
                the step from a tensor, so the loop replays

Each check of a change needs a divergence after the change.  The loops replay
with no divergence before the change.

Usage:
    python test_guard_divergence.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import sys
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402
from crucible_native import CrucibleNative  # noqa: E402

# One intra-op thread and one inter-op thread.  The instruction count of the
# test is then the same in each run, and the budgets of the test launcher hold.
torch.set_num_threads(1)
torch.set_num_interop_threads(1)

# The most iterations of a loop before its region must replay.
MAX_ITERATIONS = 2_000
# The replayed iterations of a region before the change.
REPLAYED_BEFORE_CHANGE = 3
# The iterations of the loop after the change.
CHANGED_ITERATIONS = 20
# The iterations of an AdamW loop.
ADAMW_ITERATIONS = 60
SEED = 42


def pump_until_replaying(ctx: CrucibleNative, step) -> bool:
    """Run step until the Vigil replays REPLAYED_BEFORE_CHANGE iterations of a region, and give True when it does."""
    for _ in range(MAX_ITERATIONS):
        step()
        if ctx.is_compiled() and ctx.compiled_iterations() >= REPLAYED_BEFORE_CHANGE:
            return True
        ctx.flush()
    return False


def check_change(label: str, before, after) -> list[str]:
    """Replay the loop of before, run the loop of after, and give one line for each problem."""
    problems = []
    with CrucibleNative() as ctx:
        if not pump_until_replaying(ctx, before):
            return [f"{label}: the loop does not replay after {MAX_ITERATIONS} iterations"]
        diverged = ctx.diverged_count()
        if diverged != 0:
            problems.append(f"{label}: the loop diverged {diverged} times before the change")
        for _ in range(CHANGED_ITERATIONS):
            after()
        if ctx.diverged_count() == diverged:
            problems.append(f"{label}: the changed op replays as a match, so the guard does not read the change")
    return problems


def add_steps() -> list[tuple[str, object, object]]:
    """Give the label, the loop before and the loop after of each change of aten::add.Tensor."""
    first = torch.ones(4, 4)
    second = torch.ones(4, 4)
    # The transpose has the extents of the matrix and the strides (1, 4).
    transposed = torch.ones(4, 4).t()
    first_double = torch.ones(4, 4, dtype=torch.float64)
    second_double = torch.ones(4, 4, dtype=torch.float64)
    return [
        ("alpha", lambda: torch.add(first, second, alpha=1), lambda: torch.add(first, second, alpha=2)),
        ("stride", lambda: torch.add(first, second), lambda: torch.add(first, transposed)),
        ("dtype", lambda: torch.add(first, second), lambda: torch.add(first_double, second_double)),
    ]


def adamw_step(fused: bool):
    """Give one training iteration of a linear layer under AdamW with or without the fused kernel."""
    torch.manual_seed(SEED)
    model = torch.nn.Linear(4, 4)
    optimizer = torch.optim.AdamW(model.parameters(), lr=1e-3, fused=fused)
    batch = torch.ones(2, 4)

    def step() -> None:
        optimizer.zero_grad()
        model(batch).sum().backward()
        optimizer.step()

    return step


def check_changing_step_size() -> list[str]:
    """Give one line for each problem of the AdamW loop with no fused kernel, which must record and build nothing."""
    step = adamw_step(fused=False)
    problems = []
    with CrucibleNative() as ctx:
        for _ in range(ADAMW_ITERATIONS):
            step()
            ctx.flush()
        if ctx.is_compiled() or ctx.compiled_iterations() != 0:
            problems.append(f"step size: the loop replays {ctx.compiled_iterations()} iterations, although its step "
                            f"size changes at each iteration")
        if ctx.active_num_ops() != 0:
            problems.append(f"step size: the Vigil built a region of {ctx.active_num_ops()} ops")
        if ctx.diverged_count() != 0:
            problems.append(f"step size: the loop diverged {ctx.diverged_count()} times")
    return problems


def check_fused_step() -> list[str]:
    """Give one line for each problem of the AdamW loop with the fused kernel, which must replay."""
    step = adamw_step(fused=True)
    with CrucibleNative() as ctx:
        if not pump_until_replaying(ctx, step):
            return [f"fused: the loop does not replay after {MAX_ITERATIONS} iterations"]
        if ctx.diverged_count() != 0:
            return [f"fused: the loop diverged {ctx.diverged_count()} times"]
    return []


def main() -> int:
    """Run each check and report."""
    problems = []
    for label, before, after in add_steps():
        problems += check_change(label, before, after)
    problems += check_changing_step_size()
    problems += check_fused_step()
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_guard_divergence: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
