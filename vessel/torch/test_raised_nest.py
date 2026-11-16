#!/usr/bin/env python3
"""A loop whose operation raises after an operation inside it reached the Vigil stays eager.

The Vigil takes the operations in post-order: each operation comes after the
operations that it runs.  aten::linear with a weight of the wrong shape runs
aten::t, and then the aten::addmm that it runs raises.  So aten::t reaches the
Vigil, and aten::linear does not.  Without a mark, a reader of the stream
gives aten::t to the next operation of depth 0, and a region of the loop holds
a computation that did not finish.  The nest of aten::linear gives a closing
entry in its place, and the build refuses each region that holds it.  The
check records three loops under CrucibleNative:

    raised nest     aten::linear raises in each iteration.  No region builds
    finished nest   the same loop with a weight of the right shape.  A region
                    builds
    raised leaf     crucible_raise::fail raises in its kernel and runs no
                    operation, so nothing of it reaches the Vigil.  A region
                    builds

Usage:
    python test_raised_nest.py

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

# One intra-op thread and one inter-op thread, so the instruction count of the
# test is the same in each run.
torch.set_num_threads(1)
torch.set_num_interop_threads(1)

# The iteration limit of one loop.  The Vigil activates a region after two
# periods of a loop.
MAX_ITERATIONS = 40

LINEAR_INPUT = (2, 3)
# A weight that aten::linear can multiply, and one that it cannot.
GOOD_WEIGHT = (4, 3)
BAD_WEIGHT = (4, 5)
SCALE_SOURCE = (5,)


@torch.library.custom_op("crucible_raise::fail", mutates_args=())
def fail(source: torch.Tensor) -> torch.Tensor:
    """Raise on each call, before any operation runs."""
    raise RuntimeError("crucible_raise::fail raises on each call")


def call_and_ignore_raise(call: Callable[[], object]) -> None:
    """Run call, and ignore the RuntimeError that it raises."""
    try:
        call()
    except RuntimeError:
        pass


def region_ops(body: Callable[[], None]) -> int:
    """Run body in a loop under CrucibleNative, and return the op count of the region that it gets, or 0."""
    with CrucibleNative() as ctx:
        for _ in range(MAX_ITERATIONS):
            body()
            if ctx.active_num_ops() > 0:
                return ctx.active_num_ops()
            # The pipeline processes each recorded iteration before the next
            # one starts, so the result does not depend on its speed.
            ctx.flush()
    return 0


def main() -> int:
    """Record the three loops, and report."""
    linear_input = torch.ones(LINEAR_INPUT)
    good_weight = torch.ones(GOOD_WEIGHT)
    bad_weight = torch.ones(BAD_WEIGHT)
    source = torch.ones(SCALE_SOURCE)

    def raised_nest() -> None:
        call_and_ignore_raise(lambda: torch.nn.functional.linear(linear_input, bad_weight))
        source.mul(2)

    def finished_nest() -> None:
        torch.nn.functional.linear(linear_input, good_weight)
        source.mul(2)

    def raised_leaf() -> None:
        call_and_ignore_raise(lambda: torch.ops.crucible_raise.fail(source))
        source.mul(2)

    problems = []
    raised = region_ops(raised_nest)
    if raised != 0:
        problems.append(f"the loop of a raised aten::linear has a region of {raised} ops")
    finished = region_ops(finished_nest)
    if finished == 0:
        problems.append(f"the loop of a finished aten::linear has no region after {MAX_ITERATIONS} iterations")
    leaf = region_ops(raised_leaf)
    if leaf == 0:
        problems.append(f"the loop of a raised leaf op has no region after {MAX_ITERATIONS} iterations")
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print(f"test_raised_nest: no region of the raised nest, regions of {finished} and {leaf} ops of the controls")
    return 0


if __name__ == "__main__":
    sys.exit(main())
