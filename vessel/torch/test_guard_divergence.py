#!/usr/bin/env python3
"""A replayed op whose structural scalar, tag, stride, element type or storage offset changes is a divergence.

The replay compares two words of each live op with the op of the region: the
schema hash and the guard word.  The guard word folds the layout of each input,
the tag and the role of each scalar, and the value of each structural scalar.
A data scalar is an argument of the type Scalar or float in the schema, such as
an alpha or a step size.  The guard word does not read its value, and the
replay binds the value of the live op (ScalarRole of crucible/Types.h).  Each
check runs under CrucibleNative, and the check wrapped runs under CrucibleMode:

    alpha value  aten::add.Tensor replays with alpha=1, then runs with alpha=2.
                 alpha is a data scalar, so the loop replays with no divergence,
                 and the region holds the new alpha
    alpha tag    aten::add.Tensor replays with the integer alpha 1, then runs
                 with the double alpha 1.5, so the tag of the slot changes
    stride       aten::add.Tensor replays on a matrix, then runs on its transpose,
                 which has the same extents and other strides
    dtype        aten::add.Tensor replays on float32, then runs on float64 of the
                 same extents
    offset       aten::add.Tensor replays on a view at the offset 0 of a buffer,
                 then runs on a view at the offset 4 of the same buffer, with the
                 same extents and strides
    dim          aten::sum.dim_IntList replays with dim 0, then runs with dim 1:
                 an int is a structural scalar
    reduction    aten::mse_loss replays with the reduction mean, then runs with
                 the reduction sum: the reduction is an int of the schema
    exponent     aten::pow.Tensor_Scalar replays with the exponent 2.0, then
                 runs with 3.0: the exponent is a Scalar that picks the arm of
                 the kernel, so the list of the vessel makes it structural
    step size    AdamW with no fused kernel computes its step size in Python at
                 each iteration and gives it to aten::addcdiv_ as a data
                 scalar.  The loop replays with no divergence, and after each
                 iteration the region holds the step size of that iteration
    schedule     SGD under a schedule of the learning rate gives aten::add_ a
                 new alpha at each iteration.  The loop replays with no
                 divergence, and after each iteration the region holds the
                 learning rate of that iteration
    boxed        an operator that the test defines takes the boxed fallback,
                 which reads the role of each slot from the schema: a new
                 float factor replays and the region holds it, and a new int
                 dim diverges
    rows         each argument that the list of structural scalars of the
                 vessel names has the type Scalar or float in the schema of
                 PyTorch, and the list names each of its rows
    fused        AdamW with the fused kernel gives it constant scalars and reads
                 the step from a tensor, so the loop replays
    wrapped      under CrucibleMode, a Python float in a position of the type
                 Tensor, such as the divisor of div.Tensor, is a wrapped
                 number and data: a loop whose divisor changes replays with no
                 divergence.  A changed int index of select still diverges

Each check of a structural change needs a divergence after the change.  The
loops replay with no divergence before the change.  The tests compare no value
of a tensor with eager PyTorch: they check the divergences, the replay of the
region and the values that the region holds.

Usage:
    python test_guard_divergence.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import struct
import sys
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402
import torch.nn.functional as F  # noqa: E402

import crucible_mode  # noqa: E402
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
# The replayed iterations of an optimizer loop whose scalars change.
REPLAYED_UPDATES = 10
# The most iterations of an optimizer loop before its region must replay.  The
# step size of AdamW, lr / (1 - beta1 ** step), stops its change in a double
# after about 350 steps, so a guard that reads its value finds a period there.
# The bound keeps the check before that point.
MAX_UPDATE_ITERATIONS = 100
SEED = 42


def double_bits(value: float) -> int:
    """Return the bits of a double as a slot of a trace holds them: a signed 64-bit integer."""
    return struct.unpack("<q", struct.pack("<d", value))[0]


def pump_until_replaying(ctx: CrucibleNative | crucible_mode.CrucibleMode, step, limit: int = MAX_ITERATIONS) -> bool:
    """Run step until the Vigil replays REPLAYED_BEFORE_CHANGE iterations of a region, and give True when it does.

    The loop runs step at most limit times.
    """
    for _ in range(limit):
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


def structural_steps() -> list[tuple[str, object, object]]:
    """Give the label, the loop before and the loop after of each change that must diverge."""
    first = torch.ones(4, 4)
    second = torch.ones(4, 4)
    # The transpose has the extents of the matrix and the strides (1, 4).
    transposed = torch.ones(4, 4).t()
    first_double = torch.ones(4, 4, dtype=torch.float64)
    second_double = torch.ones(4, 4, dtype=torch.float64)
    # Two views of 16 elements of one buffer: the first at the offset 0, the
    # second at the offset 4.
    buffer = torch.ones(20)
    head = buffer[:16]
    shifted = buffer[4:]
    addend = torch.ones(16)
    return [
        ("alpha tag", lambda: torch.add(first, second, alpha=1), lambda: torch.add(first, second, alpha=1.5)),
        ("stride", lambda: torch.add(first, second), lambda: torch.add(first, transposed)),
        ("dtype", lambda: torch.add(first, second), lambda: torch.add(first_double, second_double)),
        ("offset", lambda: torch.add(head, addend), lambda: torch.add(shifted, addend)),
        ("dim", lambda: torch.sum(first, dim=0), lambda: torch.sum(first, dim=1)),
        ("reduction", lambda: F.mse_loss(first, second, reduction="mean"),
         lambda: F.mse_loss(first, second, reduction="sum")),
        ("exponent", lambda: torch.pow(first, 2.0), lambda: torch.pow(first, 3.0)),
    ]


def check_alpha_value() -> list[str]:
    """Give one line for each problem of a loop whose data scalar alpha changes, which must replay."""
    first = torch.ones(4, 4)
    second = torch.ones(4, 4)
    problems = []
    with CrucibleNative() as ctx:
        if not pump_until_replaying(ctx, lambda: torch.add(first, second, alpha=1)):
            return [f"alpha value: the loop does not replay after {MAX_ITERATIONS} iterations"]
        replayed = ctx.compiled_iterations()
        for _ in range(CHANGED_ITERATIONS):
            torch.add(first, second, alpha=2)
        if ctx.diverged_count() != 0:
            problems.append(f"alpha value: a new alpha diverged {ctx.diverged_count()} times, although alpha is data")
        if ctx.compiled_iterations() <= replayed:
            problems.append("alpha value: the loop with the new alpha replays no iteration")
        held = {bound.bits for bound in ctx.bound_scalars()}
        if held != {2}:
            problems.append(f"alpha value: the region holds the alpha bits {sorted(held)}, not the new alpha 2")
    return problems


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

    return step, optimizer


def check_updates(label: str, step, expected_bits, ctx: CrucibleNative) -> list[str]:
    """Replay REPLAYED_UPDATES iterations of an optimizer loop, and give one line for each problem.

    expected_bits gives the bits of the data scalar that the iteration just
    run gave its update ops.  After each replayed iteration, the region must
    hold those bits and no longer the bits of the iteration before.
    """
    if not pump_until_replaying(ctx, step, MAX_UPDATE_ITERATIONS):
        return [f"{label}: the loop does not replay after {MAX_UPDATE_ITERATIONS} iterations"]
    problems = []
    replayed = ctx.compiled_iterations()
    previous = expected_bits()
    for _ in range(REPLAYED_UPDATES):
        step()
        current = expected_bits()
        held = [bound.bits for bound in ctx.bound_scalars()]
        if current not in held:
            problems.append(f"{label}: the region does not hold the scalar {current:#x} of the last iteration")
        if previous != current and previous in held:
            problems.append(f"{label}: the region still holds the scalar {previous:#x} of the iteration before")
        previous = current
    if ctx.diverged_count() != 0:
        problems.append(f"{label}: the loop diverged {ctx.diverged_count()} times")
    if ctx.compiled_iterations() < replayed + REPLAYED_UPDATES:
        problems.append(f"{label}: the loop replays {ctx.compiled_iterations() - replayed} of {REPLAYED_UPDATES} "
                        f"iterations")
    return problems


def check_changing_step_size() -> list[str]:
    """Give one line for each problem of the AdamW loop with no fused kernel, which must replay."""
    update, optimizer = adamw_step(fused=False)
    group = optimizer.param_groups[0]
    # The count of the updates.  A read of the step tensor of the optimizer
    # would run an op that the region does not hold, so the test counts in
    # Python.
    updates = [0]

    def step() -> None:
        update()
        updates[0] += 1

    def step_size_bits() -> int:
        # _single_tensor_adamw gives addcdiv_ the value -lr / (1 - beta1 ** step).
        return double_bits(-(group["lr"] / (1 - group["betas"][0] ** float(updates[0]))))

    with CrucibleNative() as ctx:
        return check_updates("step size", step, step_size_bits, ctx)


def check_schedule() -> list[str]:
    """Give one line for each problem of an SGD loop under a schedule of the learning rate, which must replay."""
    torch.manual_seed(SEED)
    model = torch.nn.Linear(4, 4)
    optimizer = torch.optim.SGD(model.parameters(), lr=0.1)
    schedule = torch.optim.lr_scheduler.LambdaLR(optimizer, lambda epoch: 0.9 ** epoch)
    batch = torch.ones(2, 4)
    used = [0.0]

    def step() -> None:
        optimizer.zero_grad()
        model(batch).sum().backward()
        used[0] = optimizer.param_groups[0]["lr"]
        optimizer.step()
        schedule.step()

    # _single_tensor_sgd gives add_ the alpha -lr.
    with CrucibleNative() as ctx:
        return check_updates("schedule", step, lambda: double_bits(-used[0]), ctx)


def check_boxed_path() -> list[str]:
    """Give one line for each problem of an op of the boxed fallback with a data scalar and a structural scalar.

    No unboxed kernel takes an operator that the test defines, so the boxed
    fallback records it and reads the role of each slot from the types of the
    schema: the float factor is data, and the int dim is structural.
    """
    library = torch.library.Library("crucible_roles", "DEF")
    library.define("scale(Tensor source, float factor, int dim) -> Tensor")
    library.impl("scale", lambda source, factor, dim: source.mul(factor), "CPU")
    source = torch.ones(4, 4)
    factor = [1.0]

    def step(dim: int) -> None:
        factor[0] += 0.5
        torch.ops.crucible_roles.scale(source, factor[0], dim)

    with CrucibleNative() as ctx:
        problems = check_updates("boxed factor", lambda: step(0), lambda: double_bits(factor[0]), ctx)
        diverged = ctx.diverged_count()
        for _ in range(CHANGED_ITERATIONS):
            step(1)
        if ctx.diverged_count() == diverged:
            problems.append("boxed dim: the changed int replays as a match, so the guard does not read it")
    return problems


# The rows of kStructuralScalars of vessel/torch/entry_slots.h.
STRUCTURAL_ROWS = 16


def check_structural_rows() -> list[str]:
    """Give one line for each row of the list of structural scalars that names no Scalar or float argument.

    The list names an argument by the schema hash of its operator and its
    position.  Each argument of each schema of PyTorch that the list names must
    have the type Scalar or float, and the list must name STRUCTURAL_ROWS
    arguments.  A row with a wrong name or position names no argument, and
    the count falls.  O(arguments of each schema).
    """
    vessel = crucible_mode._VesselLib()
    problems = []
    named = 0
    for schema in torch._C._jit_get_all_schemas():
        name = schema.name + (f".{schema.overload_name}" if schema.overload_name else "")
        schema_hash = crucible_mode._fnv1a(name.encode("ascii"))
        for position, argument in enumerate(schema.arguments):
            if not vessel.crucible_is_structural_scalar(schema_hash, position):
                continue
            named += 1
            if not crucible_mode._is_data_type(argument.type):
                problems.append(f"rows: the list names the argument {argument.name} of {name}, whose type "
                                f"{argument.type} is no Scalar and no float")
    if named != STRUCTURAL_ROWS:
        problems.append(f"rows: the list names {named} arguments of the schemas of PyTorch, not {STRUCTURAL_ROWS}")
    return problems


def check_fused_step() -> list[str]:
    """Give one line for each problem of the AdamW loop with the fused kernel, which must replay."""
    step, _ = adamw_step(fused=True)
    with CrucibleNative() as ctx:
        if not pump_until_replaying(ctx, step):
            return [f"fused: the loop does not replay after {MAX_ITERATIONS} iterations"]
        if ctx.diverged_count() != 0:
            return [f"fused: the loop diverged {ctx.diverged_count()} times"]
    return []


def check_wrapped_number_under_mode() -> list[str]:
    """Give one line for each problem of CrucibleMode with a wrapped number, which must replay, and an int.

    A Python float in a position of the type Tensor is a wrapped number, and
    the mode makes it data, as the native vessel records its tensor as an
    input.  So a loop whose divisor changes at each iteration replays with no
    divergence.  An int of the schema stays structural, so a changed index of
    select diverges.
    """
    problems = []
    source = torch.ones(16)
    divisor = [1.0]
    index = [1]

    def divide() -> None:
        divisor[0] += 0.5
        torch.relu(source / divisor[0])

    def select() -> None:
        torch.relu(source.select(0, index[0]))

    with crucible_mode.CrucibleMode() as mode:
        if not pump_until_replaying(mode, divide, MAX_UPDATE_ITERATIONS):
            return [f"wrapped: the loop of a new divisor at each iteration does not replay under CrucibleMode after "
                    f"{MAX_UPDATE_ITERATIONS} iterations"]
        for _ in range(CHANGED_ITERATIONS):
            divide()
        if mode.diverged_count() != 0:
            problems.append(f"wrapped: the loop diverged {mode.diverged_count()} times")
    with crucible_mode.CrucibleMode() as mode:
        if not pump_until_replaying(mode, select):
            return problems + [f"wrapped: the loop of select does not replay after {MAX_ITERATIONS} iterations"]
        index[0] = 2
        for _ in range(CHANGED_ITERATIONS):
            select()
        if mode.diverged_count() == 0:
            problems.append("wrapped: a changed int index of select replays as a match under CrucibleMode")
    return problems


def main() -> int:
    """Run each check and report."""
    problems = []
    for label, before, after in structural_steps():
        problems += check_change(label, before, after)
    problems += check_alpha_value()
    problems += check_changing_step_size()
    problems += check_schedule()
    problems += check_boxed_path()
    problems += check_structural_rows()
    problems += check_fused_step()
    problems += check_wrapped_number_under_mode()
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_guard_divergence: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
