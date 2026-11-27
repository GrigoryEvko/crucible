#!/usr/bin/env python3
"""A loop whose iteration holds a run of equal groups of ops replays its whole iteration.

The checks record each loop under CrucibleNative until the region replays:

    foreach equal   aten::_foreach_add over 12 tensors of one extent, then a
                    relu of one output.  The CPU path runs one group of six
                    ops for each tensor, so the iteration holds a run of 12
                    equal groups.  The region holds the 75 ops of the iteration
    adam            aten::_fused_adam_ over 10 parameters.  The kernel reads
                    each step tensor of rank zero with item(), so the iteration
                    holds a run of 10 equal pairs.  The region holds the 21 ops
                    of the iteration
    foreach short   the loop over 4 tensors, whose run holds no square of two
                    groups.  The region holds 27 ops
    foreach mixed   the loop over 12 tensors of 12 extents, whose groups
                    differ.  The region holds 75 ops

A region of a part of the run diverges at the end of the run.  The detector
refutes that period at its divergence.  It accepts the next multiple of the
group as one fallback, and no longer multiple, so each loop diverges at most
two times.  Then the region holds for four more iterations.

Usage:
    python test_equal_groups_replay.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import sys
from collections.abc import Callable
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402

from crucible_native import CrucibleNative  # noqa: E402

# One intra-op thread and one inter-op thread, so that each run takes the same
# kernels of PyTorch and gives the same ops.
torch.set_num_threads(1)
torch.set_num_interop_threads(1)

# The most iterations of a loop before its region must replay.
MAX_ITERATIONS = 64
# The iterations after the first replay in which the region must hold.
HOLD_ITERATIONS = 4
# The most divergences of a loop: the regions of one group and of its
# fallback of two groups.
MAX_DIVERGENCES = 2


def foreach_step(extents: list[int]) -> Callable[[], None]:
    """Give one iteration of _foreach_add over one tensor of each extent."""
    sources = [torch.full((extent,), float(index)) for index, extent in enumerate(extents)]

    def step() -> None:
        results = torch._foreach_add(sources, 1.0)
        torch.relu(results[1])

    return step


def adam_step(params: int) -> Callable[[], None]:
    """Give one iteration of _fused_adam_ over the parameters, each of extent 8."""
    weights = [torch.ones(8) for _ in range(params)]
    grads = [torch.full((8,), 0.1) for _ in range(params)]
    exp_avgs = [torch.zeros(8) for _ in range(params)]
    exp_avg_sqs = [torch.zeros(8) for _ in range(params)]
    state_steps = [torch.tensor(1.0) for _ in range(params)]

    def step() -> None:
        torch._fused_adam_(weights, grads, exp_avgs, exp_avg_sqs, [], state_steps, lr=0.001, beta1=0.9,
                           beta2=0.999, weight_decay=0.0, eps=1e-8, amsgrad=False, maximize=False)

    return step


def check_loop(name: str, step: Callable[[], None], expected_ops: int) -> list[str]:
    """Record the loop until it replays, and give a line for each check that fails."""
    with CrucibleNative() as ctx:
        for _ in range(MAX_ITERATIONS):
            step()
            ctx.flush()
            if ctx.is_compiled():
                break
        if not ctx.is_compiled():
            return [f"{name}: no replay after {MAX_ITERATIONS} iterations, active region of "
                    f"{ctx.active_num_ops()} ops, {ctx.diverged_count()} divergences"]
        problems = []
        if ctx.active_num_ops() != expected_ops:
            problems.append(f"{name}: the region holds {ctx.active_num_ops()} ops, not the {expected_ops} ops of "
                            f"the iteration")
        for _ in range(HOLD_ITERATIONS):
            step()
        ctx.flush()
        if not ctx.is_compiled():
            problems.append(f"{name}: the region did not hold for {HOLD_ITERATIONS} more iterations")
        if ctx.diverged_count() > MAX_DIVERGENCES:
            problems.append(f"{name}: {ctx.diverged_count()} divergences, more than {MAX_DIVERGENCES}")
        return problems


def main() -> int:
    """Run each check and report."""
    problems = (check_loop("foreach equal", foreach_step([16] * 12), 75)
                + check_loop("adam", adam_step(10), 21)
                + check_loop("foreach short", foreach_step([16] * 4), 27)
                + check_loop("foreach mixed", foreach_step([16 + index for index in range(12)]), 75))
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_equal_groups_replay: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
