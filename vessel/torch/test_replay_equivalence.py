#!/usr/bin/env python3
"""Replay through the native vessel gives the bytes that PyTorch alone gives.

attach() puts DispatchKey::Crucible on a loop.  The Vigil records the first
iterations, builds a region, and then replays it.  A replayed operation still
reaches the kernel, so each iteration must give the same bytes as PyTorch with
the key off, from the same seed.  When a replayed operation returns without
the kernel, this test shows whether the result stays the same.

The workloads are the workloads of bench_e2e.py, so this test and the bench
measure one set of loops.  Each workload has three arms in one process:

    attached   iterations under attach(), until MIN_REPLAYED iterations replay
               or MAX_ITERATIONS iterations are done
    eager      the same number of iterations with the key off
    control    the eager arm a second time.  If the two eager arms disagree,
               the eager path is not deterministic, and no comparison is valid

The mode of the Vigil before and after an iteration puts the iteration in one
class.  A replayed iteration starts and ends in replay, with no divergence in
it.  A recorded iteration starts and ends outside replay.  All other
iterations are mixed.  A workload with fewer than MIN_REPLAYED replayed
iterations fails, because a comparison without replay says nothing about
replay.

The test compares the bytes of the result of each iteration, and the bytes of
each parameter and gradient at the end.

Usage:
    CRUCIBLE_BUILD_DIR=build-release python test_replay_equivalence.py

Exit 0 when each workload agrees, 1 on a difference or on a workload that did
not replay enough.
"""

from __future__ import annotations

import sys
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402

import bench_e2e  # noqa: E402
from crucible_native import attach  # noqa: E402

# The replayed iterations each workload must show.  Twenty is more than one
# region of each workload, so the comparison crosses a region boundary.
MIN_REPLAYED = 20

# The iteration limit of the attached arm.  Measured on the fork: the add
# workloads of bench_e2e.py are the slowest to replay.  They record 19
# iterations and replay from the 21st, when the arm flushes the ring between
# two recorded iterations.
MAX_ITERATIONS = 400


def as_bytes(tensor: torch.Tensor) -> torch.Tensor:
    """Return the bytes of a tensor as a flat uint8 tensor.

    A zero-dimensional tensor becomes one element before the view, because
    view(torch.uint8) needs a last dimension.  O(n) in the number of bytes.
    """
    return tensor.detach().reshape(-1).contiguous().view(torch.uint8)


def same_bytes(left: torch.Tensor, right: torch.Tensor) -> bool:
    """Return True when two tensors have one dtype, one shape and the same bytes."""
    return (left.dtype == right.dtype and left.shape == right.shape
            and torch.equal(as_bytes(left), as_bytes(right)))


def final_state(model: torch.nn.Module) -> list[torch.Tensor]:
    """Return a copy of each parameter and each gradient of a model.

    Call this with the key off.  With the key on, the copies are operations of
    the loop and the Vigil sees them.
    """
    state: list[torch.Tensor] = []
    for parameter in model.parameters():
        state.append(parameter.detach().clone())
        if parameter.grad is not None:
            state.append(parameter.grad.detach().clone())
    return state


def attached_arm(kind: str) -> tuple[list[torch.Tensor], list[torch.Tensor], dict[str, int]]:
    """Operate one workload under attach() and classify each iteration.

    The arm flushes the ring after each iteration that ends outside replay, so
    the background thread builds the region before the next iteration.  A
    flush does not change the operations of the loop.

    Returns:
        The result of each iteration, the final parameters and gradients, and
        the count of each class of iteration with the divergence count
    """
    model, optimizer, step = bench_e2e.build_workload(kind)
    results: list[torch.Tensor] = []
    census = {"replayed": 0, "recorded": 0, "mixed": 0, "divergences": 0}
    with attach(model, optimizer) as ctx:
        while census["replayed"] < MIN_REPLAYED and len(results) < MAX_ITERATIONS:
            started_in_replay = ctx.is_compiled()
            divergences_before = ctx.diverged_count()
            results.append(step())
            ended_in_replay = ctx.is_compiled()
            if started_in_replay and ended_in_replay and ctx.diverged_count() == divergences_before:
                census["replayed"] += 1
            elif not started_in_replay and not ended_in_replay:
                census["recorded"] += 1
            else:
                census["mixed"] += 1
            if not ended_in_replay:
                ctx.flush()
        census["divergences"] = ctx.diverged_count()
    return results, final_state(model), census


def eager_arm(kind: str, count: int) -> tuple[list[torch.Tensor], list[torch.Tensor]]:
    """Operate one workload for `count` iterations with the key off."""
    model, _optimizer, step = bench_e2e.build_workload(kind)
    results = [step() for _ in range(count)]
    return results, final_state(model)


def first_difference(left: list[torch.Tensor], right: list[torch.Tensor]) -> int | None:
    """Return the index of the first pair that differs, or None when all agree."""
    if len(left) != len(right):
        return min(len(left), len(right))
    for index, (one, other) in enumerate(zip(left, right)):
        if not same_bytes(one, other):
            return index
    return None


def compare_workload(kind: str) -> list[str]:
    """Compare the attached arm of one workload with its eager arm.

    Returns:
        One line for each problem.  An empty list means the workload agrees
    """
    attached, attached_state, census = attached_arm(kind)
    count = len(attached)
    eager, eager_state = eager_arm(kind, count)
    control, control_state = eager_arm(kind, count)

    print(f"  {kind:10s} iterations={count} replayed={census['replayed']} "
          f"recorded={census['recorded']} mixed={census['mixed']} "
          f"divergences={census['divergences']}")

    problems: list[str] = []
    if first_difference(eager, control) is not None or first_difference(eager_state, control_state) is not None:
        problems.append(f"{kind}: two eager arms from one seed disagree, so the eager path is not "
                        f"deterministic and the comparison with replay is not valid")
        return problems
    if census["replayed"] < MIN_REPLAYED:
        problems.append(f"{kind}: {census['replayed']} replayed iteration(s) in {count}, less than the "
                        f"minimum of {MIN_REPLAYED}.  A comparison without replay says nothing about replay")
    iteration = first_difference(attached, eager)
    if iteration is not None:
        problems.append(f"{kind}: the result of iteration {iteration} under attach() differs from "
                        f"PyTorch alone")
    parameter = first_difference(attached_state, eager_state)
    if parameter is not None:
        problems.append(f"{kind}: final parameter or gradient {parameter} under attach() differs "
                        f"from PyTorch alone")
    return problems


def main() -> int:
    """Compare every workload of bench_e2e.py and report."""
    # One intra-op thread, as each arm of bench_e2e.py sets, so that no
    # thread count changes the order of a reduction between two arms.
    torch.set_num_threads(1)

    print("=" * 60)
    print("Crucible Vessel -- replay against PyTorch alone, bit for bit")
    print("=" * 60)
    print(f"PyTorch: {torch.__version__}")

    problems: list[str] = []
    for kind in bench_e2e.ITERATIONS:
        problems += compare_workload(kind)

    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print(f"  PASS: {len(bench_e2e.ITERATIONS)} workload(s) give the bytes of PyTorch alone "
          f"under replay")
    return 0


if __name__ == "__main__":
    sys.exit(main())
