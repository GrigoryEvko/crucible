#!/usr/bin/env python3
"""A change of torch.set_flush_denormal during a replay is a divergence, and the next region has a new hash.

The Vigil records each region under the eager word of PyTorch: the precision of
a float32 matmul, the two TF32 bits and the flush of denormal floats.  The
vessel reads the word at the attach and again at each region entry.  A changed
word stops the replay of the region of the old word, counts one divergence, and
the Vigil records the next region under the new word.  The check runs a loop of
aten::add.Tensor under CrucibleNative:

    replay      the loop replays a region of the attach word
    divergence  torch.set_flush_denormal(True) between two iterations gives one
                divergence
    new region  the loop replays again, and the region has a new content hash

Usage:
    python test_eager_numerics.py

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

# The most iterations of the loop before its region must replay.
MAX_ITERATIONS = 2_000


def pump_until_replaying(ctx: CrucibleNative, step, other_than: int | None = None) -> bool:
    """Run step until the Vigil replays a region whose content hash is not other_than, and give True when it does."""
    for _ in range(MAX_ITERATIONS):
        step()
        if ctx.is_compiled() and ctx.active_content_hash() != other_than:
            return True
        ctx.flush()
    return False


def check_flush_change() -> list[str]:
    """Give one line for each problem of a change of the flush of denormal floats during a replay."""
    first = torch.ones(4)
    second = torch.ones(4)

    def step() -> None:
        torch.add(first, second)

    with CrucibleNative() as ctx:
        if not pump_until_replaying(ctx, step):
            return [f"replay: the loop does not replay after {MAX_ITERATIONS} iterations"]
        attach_hash = ctx.active_content_hash()
        diverged = ctx.diverged_count()
        region_ops = ctx.active_num_ops()
        if not torch.set_flush_denormal(True):
            return ["divergence: the host does not flush denormal floats, so the check cannot run"]
        try:
            # The vessel reads the word at the next region entry, which comes
            # at most one iteration of the region later.
            for _ in range(region_ops + 1):
                step()
            problems = []
            if ctx.diverged_count() != diverged + 1:
                problems.append(f"divergence: the change of the flush gives {ctx.diverged_count() - diverged} "
                                f"divergences, not one")
            if not pump_until_replaying(ctx, step, other_than=attach_hash):
                problems.append(f"new region: no region of the new word replays after {MAX_ITERATIONS} iterations")
            return problems
        finally:
            torch.set_flush_denormal(False)


def main() -> int:
    """Run each check and report."""
    torch.set_flush_denormal(False)
    problems = check_flush_change()
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_eager_numerics: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
