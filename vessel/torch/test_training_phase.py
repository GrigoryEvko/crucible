#!/usr/bin/env python3
"""Each op of a backward pass records the phase BACKWARD, on the native paths and under CrucibleMode.

The checks record one loop of one layer under a session until a region is
active, export the region, and read the phase of each op:

    forward   (inputs @ weight).relu().sum(), each op and each nested op FORWARD
    seed      the ones_like of the gradient that the caller makes before the
              engine starts, FORWARD, because it is no node of the engine
    backward  each op that the autograd engine runs, and each op nested in
              one, BACKWARD
    step      torch._foreach_sub_ of the weight under no_grad, whose op is
              OPTIMIZER

The loop takes its gradient in two ways: loss.backward(), which runs the
wrapper of Tensor.backward of CrucibleNative, and torch.autograd.grad, which
runs no wrapper.  The phase comes from the graph task of the engine on the
thread of the op, so the two ways give one phase to each op.  The sessions
are CrucibleNative and CrucibleMode, and the two give the same phases for each
part of the loop.

Usage:
    python test_training_phase.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import gc
import sys
import tempfile
from collections.abc import Callable
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402

import crucible_mode  # noqa: E402
from crucible_native import CrucibleNative  # noqa: E402

# The import sets one intra-op thread and one inter-op thread, so that each run
# takes the same kernels of PyTorch and gives the same operations.
from test_region_ops import RegionOp, read_region  # noqa: E402

# The most iterations of a loop before its region is active.
MAX_ITERATIONS = 64
SEED = 0
LEARNING_RATE = 0.01

# The depth-0 op that ends each part of the iteration.
FORWARD_END = "aten::sum"
SEED_END = "aten::ones_like"
STEP_OP = "aten::_foreach_sub_.List"

# The phase of each op of each part.  The step holds its op and the ops nested
# in it, and only its op is a fused multi-tensor op.
EXPECTED = {"forward": {"F"}, "seed": {"F"}, "backward": {"B"}}
EXPECTED_STEP = "O"

# The phases of a part of the iteration, as a set.
PartPhases = dict[str, set[str]]


def make_step(gradient: str) -> Callable[[], None]:
    """Give one iteration of the loop, with the gradient of loss.backward() or of torch.autograd.grad."""
    torch.manual_seed(SEED)
    weight = torch.randn(8, 4, requires_grad=True)
    inputs = torch.randn(8, 8)

    def step() -> None:
        loss = (inputs @ weight).relu().sum()
        if gradient == "grad":
            (grad,) = torch.autograd.grad(loss, [weight])
        else:
            weight.grad = None
            loss.backward()
            grad = weight.grad
        with torch.no_grad():
            torch._foreach_sub_([weight], [grad], alpha=LEARNING_RATE)

    return step


def record_native(step: Callable[[], None], path: Path) -> bool:
    """Record the loop under CrucibleNative until a region is active, and export it."""
    with CrucibleNative() as ctx:
        for _ in range(MAX_ITERATIONS):
            step()
            ctx.flush()
            if ctx.active_num_ops() > 0:
                break
        return ctx.export_trace(str(path))


def record_mode(step: Callable[[], None], path: Path) -> bool:
    """Record the loop under CrucibleMode until a region is active, and export it."""
    mode = crucible_mode.CrucibleMode()
    with mode:
        for _ in range(MAX_ITERATIONS):
            step()
            mode.flush()
            if mode.active_num_ops() > 0:
                break
        exported = mode.export_trace(path)
    del mode
    gc.collect()
    return exported


def nests_of(ops: list[RegionOp]) -> list[list[RegionOp]]:
    """Group the ops into nests: each depth-0 op with the ops nested in it, which come before it.

    The ops after the last depth-0 op open a nest that the start of the
    region closes, so they join the first nest.  Complexity: O(ops).
    """
    nests: list[list[RegionOp]] = []
    current: list[RegionOp] = []
    for op in ops:
        current.append(op)
        if op.depth == 0:
            nests.append(current)
            current = []
    if current and nests:
        nests[0] = current + nests[0]
    return nests


def parts_of(ops: list[RegionOp]) -> dict[str, list[RegionOp]] | None:
    """Split the ops of one iteration into its parts, from the nest after the step, or give None for no step.

    The region is a rotation of the iteration.  A part ends with the nest of
    its last depth-0 op.  Complexity: O(ops).
    """
    nests = nests_of(ops)
    ends = [index for index, nest in enumerate(nests) if nest[-1].name == STEP_OP]
    if len(ends) != 1:
        return None
    start = ends[0] + 1
    iteration = nests[start:] + nests[:start]
    parts: dict[str, list[RegionOp]] = {"forward": [], "seed": [], "backward": [], "step": iteration[-1]}
    order = iter((("forward", FORWARD_END), ("seed", SEED_END)))
    part, end = next(order)
    for nest in iteration[:-1]:
        parts[part].extend(nest)
        if part != "backward" and nest[-1].name == end:
            part, end = next(order, ("backward", ""))
    return parts


def check_region(label: str, ops: list[RegionOp]) -> tuple[list[str], PartPhases]:
    """Check the phase of each op of each part, and give the phases of the depth-0 ops of each part."""
    parts = parts_of(ops)
    if parts is None:
        return [f"{label}: the region holds no single depth-0 {STEP_OP}"], {}
    problems = []
    for part, expected in EXPECTED.items():
        if not parts[part]:
            problems.append(f"{label}: the part {part} holds no op")
        for op in parts[part]:
            if op.phase not in expected:
                problems.append(f"{label}: {part} op {op.name} at depth {op.depth} has the phase {op.phase}, "
                                f"not {'/'.join(sorted(expected))}")
    step_ops = [op for op in parts["step"] if op.depth == 0]
    if [op.phase for op in step_ops] != [EXPECTED_STEP]:
        problems.append(f"{label}: the step op has the phases {[op.phase for op in step_ops]}, not "
                        f"[{EXPECTED_STEP}]")
    phases = {part: {op.phase for op in part_ops if op.depth == 0} for part, part_ops in parts.items()}
    return problems, phases


def main() -> int:
    """Record the loop with each gradient under each session, and report."""
    problems: list[str] = []
    with tempfile.TemporaryDirectory() as directory:
        for gradient in ("backward", "grad"):
            phases_of_session: dict[str, PartPhases] = {}
            for session, record in (("native", record_native), ("mode", record_mode)):
                label = f"{session} with {gradient}"
                path = Path(directory) / f"{session}-{gradient}.crtrace"
                if not record(make_step(gradient), path):
                    problems.append(f"{label}: no region after {MAX_ITERATIONS} iterations")
                    continue
                found, phases = check_region(label, read_region(path))
                problems.extend(found)
                phases_of_session[session] = phases
            if len(phases_of_session) == 2 and phases_of_session["native"] != phases_of_session["mode"]:
                problems.append(f"{gradient}: the phases of the parts differ between the sessions: native "
                                f"{phases_of_session['native']}, mode {phases_of_session['mode']}")
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_training_phase: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
