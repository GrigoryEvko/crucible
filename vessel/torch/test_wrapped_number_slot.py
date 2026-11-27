#!/usr/bin/env python3
"""A wrapped number is a data scalar with no slot of the memory plan, under CrucibleNative and under CrucibleMode.

A Python number in a position of the type Tensor, such as the divisor of
div.Tensor, is a wrapped number: PyTorch wraps it into a CPU tensor of rank
zero.  It is a value and no memory (ScalarRole of crucible/Types.h), so the
two sessions record it the same way.  Its input has rank zero and no data
address, so the memory plan gives it no slot, and its value is a data slot of
the op, which the replay binds at each iteration.  The checks:

    div        aten::div.Tensor(x, divisor) with a float divisor that changes at
               each iteration
    mul        aten::mul.Tensor(x, factor) with an int factor that changes at
               each iteration

For each of the two and each session, the loop records until it replays, and:
  - the plan has no slot of the bytes of the wrapped number, and its one
    external slot is the slot of x
  - the input of the number in the record of the top-level op has rank zero
    and its element type
  - the top-level op has a data slot with the tag of the number, and the
    region binds the value of the last iteration
  - the loop never diverges
  - the two sessions give the same external slots, the same meta of the
    number and the same tags of the scalar slots of the op

    complex    aten::mul.Tensor(x, factor) of a complex x with a complex factor,
               then abs.  No tag of a slot holds a complex number, so each
               session refuses the mul, and the region holds the abs and no
               mul at depth 0
    overflow   aten::_fused_adam_ has more than five scalars, so its entry
               overflows, and four stay inline.  A changed lr (an inline data slot)
               replays with no divergence.  A changed eps (the fifth scalar, in
               the digest) diverges, because the entry has no slot that the
               replay can bind for it

Usage:
    python test_wrapped_number_slot.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import gc
import struct
import sys
import tempfile
from collections.abc import Callable
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402

import crucible_mode  # noqa: E402
from crucible_native import BoundScalar, CrucibleNative, RegionSlot  # noqa: E402

# The import sets one intra-op thread and one inter-op thread, so that each run
# takes the same kernels of PyTorch and gives the same operations.
from test_record_coverage import (  # noqa: E402
    META_NDIM_OFFSET,
    OP_RECORD,
    TraceOp,
    read_schema_names,
    read_trace,
)

# The most iterations of a loop before its region replays.
MAX_ITERATIONS = 64
# The iterations of a loop after its first replay.
REPLAY_ITERATIONS = 6
# The extent of x: 6 floats of 4 bytes.
EXTENT = 6
TENSOR_BYTES = EXTENT * 4
# The tags of the scalar slots (ScalarType2 of crucible/TraceRing.h).
TAG_INT, TAG_FLOAT = 0, 1
# The crucible element types of a wrapped double and a wrapped long.
DTYPE_DOUBLE, DTYPE_LONG = 7, 4
# The bytes of the storage of a wrapped double or a wrapped long.
WRAPPED_BYTES = 8
# The field of the data slots in an op record (OP_RECORD of test_record_coverage).
DATA_SLOTS_FIELD = 16

# One iteration of a loop, by its index.
Step = Callable[[int], None]


class Session:
    """One session of a loop, CrucibleNative or CrucibleMode, with the queries that the checks read."""

    def __init__(self, kind: str) -> None:
        """Make the session of the kind native or mode."""
        self.native = CrucibleNative() if kind == "native" else None
        self.mode = crucible_mode.CrucibleMode() if kind == "mode" else None

    def __enter__(self) -> "Session":
        """Enter the session."""
        if self.native is not None:
            self.native.__enter__()
        else:
            self.mode.__enter__()
        return self

    def __exit__(self, *exc: object) -> None:
        """Leave the session, and end the Vigil of the mode."""
        if self.native is not None:
            self.native.__exit__(*exc)
            return
        self.mode.__exit__(*exc)
        self.mode = None
        gc.collect()

    def flush(self) -> None:
        """Wait until the background thread processed each recorded op."""
        (self.native or self.mode).flush()

    def is_compiled(self) -> bool:
        """Give whether the Vigil replays a region."""
        return (self.native or self.mode).is_compiled()

    def diverged_count(self) -> int:
        """Give the divergences of the Vigil."""
        return (self.native or self.mode).diverged_count()

    def slots(self) -> list[RegionSlot]:
        """Give the slots of the memory plan of the active region."""
        if self.native is not None:
            return self.native.active_region_slots()
        return self.mode._vessel.active_region_slots(self.mode._handle)

    def bound_scalars(self) -> list[BoundScalar]:
        """Give the data slots of the region of the last activation."""
        if self.native is not None:
            return self.native.bound_scalars()
        return self.mode._vessel.bound_scalars(self.mode._handle)

    def export(self, path: Path) -> bool:
        """Write the active region to a .crtrace file."""
        if self.native is not None:
            return self.native.export_trace(str(path))
        return self.mode.export_trace(path)


def replay(session: Session, step: Step) -> int | None:
    """Run the loop until it replays, then REPLAY_ITERATIONS more, and give the last iteration, or None for no replay."""
    for iteration in range(MAX_ITERATIONS):
        step(iteration)
        session.flush()
        if session.is_compiled():
            last = iteration + REPLAY_ITERATIONS
            for later in range(iteration + 1, last + 1):
                step(later)
            session.flush()
            return last if session.is_compiled() else None
    return None


def top_level_op(path: Path, name: str) -> TraceOp | None:
    """Give the first op of the name at depth 0 of the region, or None."""
    names = read_schema_names(path)
    for op in read_trace(path):
        if op.depth == 0 and names.get(op.schema_hash) == name:
            return op
    return None


def number_meta_of(op: TraceOp) -> tuple[int, int]:
    """Give the rank and the element type of the second input of an op.

    The export writes no data address, so the plan shows the address: a slot
    of the number is the slot of its address.
    """
    return op.metas[1][META_NDIM_OFFSET], op.dtypes()[1]


def check_number(label: str, op_name: str, make_step: Callable[[], Step], bits_of: Callable[[int], int], tag: int,
                 dtype: int, directory: Path) -> list[str]:
    """Check one loop of an op with a wrapped number under each session, and compare the two sessions."""
    problems: list[str] = []
    seen: dict[str, tuple] = {}
    for kind in ("native", "mode"):
        where = f"{label} under {kind}"
        path = directory / f"{label}-{kind}.crtrace"
        with Session(kind) as session:
            last = replay(session, make_step())
            if last is None:
                problems.append(f"{where}: the loop does not replay after {MAX_ITERATIONS} iterations")
                continue
            if session.diverged_count() != 0:
                problems.append(f"{where}: the loop diverged {session.diverged_count()} times")
            slots = session.slots()
            bound = {scalar.bits for scalar in session.bound_scalars()}
            if not session.export(path):
                problems.append(f"{where}: no export of the region")
                continue
        op = top_level_op(path, op_name)
        if op is None:
            problems.append(f"{where}: the region holds no {op_name} at depth 0")
            continue
        number_meta = number_meta_of(op)
        if number_meta != (0, dtype):
            problems.append(f"{where}: the input of the number has the rank and element type {number_meta}, not "
                            f"(0, {dtype})")
        wrapped = [slot for slot in slots if slot.nbytes == WRAPPED_BYTES]
        if wrapped:
            problems.append(f"{where}: the plan has {len(wrapped)} slots of {WRAPPED_BYTES} bytes, the bytes of the "
                            f"wrapped number")
        external = sorted(slot.nbytes for slot in slots if slot.is_external)
        if external != [TENSOR_BYTES]:
            problems.append(f"{where}: the external slots have the bytes {external}, not the slot of x alone")
        data_slots = OP_RECORD.unpack(op.record)[DATA_SLOTS_FIELD]
        data_tags = [op.scalar_tag(slot) for slot in range(len(op.scalars)) if (data_slots >> slot) & 1]
        if tag not in data_tags:
            problems.append(f"{where}: the op has data slots of the tags {data_tags}, and none of the tag {tag}")
        if bits_of(last) not in bound:
            problems.append(f"{where}: the region does not bind the value of the last iteration")
        tags = [op.scalar_tag(slot) for slot in range(len(op.scalars))]
        seen[kind] = (external, op.metas[1], tags)
    if len(seen) == 2 and seen["native"] != seen["mode"]:
        problems.append(f"{label}: the sessions differ in the external slots, the meta of the number or the tags "
                        f"of the op: native {seen['native'][0]} {seen['native'][2]}, mode {seen['mode'][0]} "
                        f"{seen['mode'][2]}")
    return problems


def make_div_step() -> Step:
    """Give one iteration of x / divisor, with a new float divisor at each iteration."""
    source = torch.arange(float(EXTENT))

    def step(iteration: int) -> None:
        torch.div(source, 2.0 + iteration)

    return step


def make_mul_step() -> Step:
    """Give one iteration of x * factor, with a new int factor at each iteration."""
    source = torch.arange(float(EXTENT))

    def step(iteration: int) -> None:
        torch.mul(source, 3 + iteration)

    return step


def check_complex(directory: Path) -> list[str]:
    """Check that each session refuses the op of a complex wrapped number.

    The source is complex double, the element type of the wrapped number, so
    the mul runs no nested op of a conversion, and the abs after it records.
    """
    problems: list[str] = []
    source = torch.arange(float(EXTENT), dtype=torch.float64) * (1 + 1j)

    def step(iteration: int) -> None:
        torch.abs(torch.mul(source, 1 + 2j))

    for kind in ("native", "mode"):
        path = directory / f"complex-{kind}.crtrace"
        with Session(kind) as session:
            if replay(session, step) is None:
                problems.append(f"complex under {kind}: the loop does not replay after {MAX_ITERATIONS} iterations")
                continue
            if not session.export(path):
                problems.append(f"complex under {kind}: no export of the region")
                continue
        if top_level_op(path, "aten::mul.Tensor") is not None:
            problems.append(f"complex under {kind}: the region holds the mul of the complex number")
    return problems


def make_adam(changed: str, changes_from: list[int]) -> Step:
    """Give one step of _fused_adam_ over one parameter, with lr or eps new at each iteration after changes_from[0].

    The caller sets changes_from[0] when the loop replays, so the loop keeps
    its tensors and changes only the value.
    """
    params = [torch.ones(8)]
    grads = [torch.full((8,), 0.1)]
    exp_avgs = [torch.zeros(8)]
    exp_avg_sqs = [torch.zeros(8)]
    steps = [torch.tensor(1.0)]

    def step(iteration: int) -> None:
        scale = 1 + max(0, iteration - changes_from[0])
        lr = 0.001 * scale if changed == "lr" else 0.001
        eps = 1e-8 * scale if changed == "eps" else 1e-8
        torch._fused_adam_(params, grads, exp_avgs, exp_avg_sqs, [], steps, lr=lr, beta1=0.9, beta2=0.999,
                           weight_decay=0.0, eps=eps, amsgrad=False, maximize=False)

    return step


def check_overflow() -> list[str]:
    """Check the limit of the overflow digest: a changed inline data slot replays, a changed digest value diverges."""
    problems: list[str] = []
    for kind in ("native", "mode"):
        for changed, should_diverge in (("lr", False), ("eps", True)):
            where = f"overflow under {kind} with a new {changed}"
            with Session(kind) as session:
                # The value stays until the loop replays, then changes at each
                # iteration.
                changes_from = [MAX_ITERATIONS + REPLAY_ITERATIONS]
                step = make_adam(changed, changes_from)
                start = 0
                for start in range(MAX_ITERATIONS):
                    step(start)
                    session.flush()
                    if session.is_compiled():
                        break
                if not session.is_compiled():
                    problems.append(f"{where}: the loop of a constant value does not replay")
                    continue
                before = session.diverged_count()
                changes_from[0] = start
                for later in range(start + 1, start + 1 + REPLAY_ITERATIONS):
                    step(later)
                session.flush()
                if (session.diverged_count() != before) != should_diverge:
                    problems.append(f"{where}: {session.diverged_count() - before} divergences after the change, "
                                    f"and the rule gives {'some' if should_diverge else 'none'}")
    return problems


def main() -> int:
    """Run each check and report."""
    problems: list[str] = []
    with tempfile.TemporaryDirectory() as name:
        directory = Path(name)
        problems += check_number("div", "aten::div.Tensor", make_div_step,
                                 lambda iteration: struct.unpack("<q", struct.pack("<d", 2.0 + iteration))[0],
                                 TAG_FLOAT, DTYPE_DOUBLE, directory)
        problems += check_number("mul", "aten::mul.Tensor", make_mul_step, lambda iteration: 3 + iteration, TAG_INT,
                                 DTYPE_LONG, directory)
        problems += check_complex(directory)
    problems += check_overflow()
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_wrapped_number_slot: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
