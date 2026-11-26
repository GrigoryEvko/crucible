#!/usr/bin/env python3
"""The unboxed kernel of an operator with no exact bound of its records records into a frame of the record stack.

An operator with a tensor-list argument has no bound of its records at compile
time.  Its unboxed kernel takes MAX_INLINE_METAS slots of the record stack of
the Vigil, and it holds them while the operator runs.  A kernel with an exact
bound puts its records on its own stack.  The checks:

    frame held      a TorchDispatchMode runs below the Crucible key.  Inside
                    aten::cat, crucible_dispatch_op with the most records of
                    one operation gives the action REFUSED, because the frame
                    of aten::cat holds 32 of the 128 slots of the stack
    no frame        inside aten::mm, which has an exact bound, the same call
                    gives the action RECORD
    frame records   a loop runs aten::cat at the top, so its kernel records
                    through a frame.  Before it, a C caller writes records of
                    rank 4 into the same slots.  The region holds that
                    aten::cat with the records of its own tensors, and each
                    lane of a size or a stride past the rank is zero
    cold path       the loop also runs crucible_frame::level_0, a chain of four
                    boxed operations whose frames fill the stack.  The last
                    one runs aten::cat.  Just before it, crucible_dispatch_op
                    with one record gives the action REFUSED, so the stack has
                    no free slot, and the kernel can record only through the
                    list of its cold path.  The region holds that aten::cat
                    with the records of its own tensors

Usage:
    python test_unboxed_record_frame.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import ctypes
import struct
import sys
import tempfile
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402
from crucible_native import CrucibleMeta, CrucibleNative, DispatchAction  # noqa: E402
from torch.utils._python_dispatch import TorchDispatchMode  # noqa: E402

# The import sets one intra-op thread and one inter-op thread, so the
# instruction count of the test is the same in each run.  PyTorch refuses a
# second set of the inter-op threads, so this file does not set them again.
from test_record_coverage import MAX_ITERATIONS, TraceOp, read_trace, schema_hash  # noqa: E402

Meta = CrucibleMeta

# The most inputs and the most outputs of one operation (kMaxOpInputs and
# kMaxOpOutputs of crucible/ValidatedOp.h).  An operation with each of them
# needs each slot of the record stack.
MAX_OP_INPUTS = 64
MAX_OP_OUTPUTS = 64

META_SIZES_OFFSET = Meta.sizes.offset
META_STRIDES_OFFSET = Meta.strides.offset
META_NDIM_OFFSET = Meta.ndim.offset

# The lanes of the sizes and of the strides of one record.
LANES = 8

# The records of rank 4 that a C caller writes before the aten::cat of the
# loop, into the slots that the frame of aten::cat takes next.
WIDE_RECORDS = 4
WIDE_SHAPE = (7, 7, 7, 7)

# The crucible ordinal of float32.
FLOAT = 6

CAT = "aten::cat"

# The tensors of the aten::cat of the loop, and of the aten::cat at the end of
# the chain.  Each shape names its operation.
TOP_PARTS = [(2, 3), (4, 3)]
TOP_RESULT = (6, 3)
DEEP_PARTS = [(5, 7), (1, 7)]
DEEP_RESULT = (6, 7)

# The boxed operations of the chain.  Each frame of the boxed fallback holds
# 32 of the 128 slots of the stack, so four frames fill it.
DEPTH = 4
LEVEL_INPUTS = [(level + 2, 2) for level in range(DEPTH)]
LEVEL_OUTPUTS = [(level + 30,) for level in range(DEPTH)]


class DeepProbe:
    """The probe that the last level of the chain runs just before its aten::cat, while a recording binds it."""

    def __init__(self) -> None:
        """Start with no probe and no action."""
        self.probe = None
        self.actions: list[int] = []

    def run(self) -> None:
        """Run the probe, when a recording binds one, and keep the action that it gives."""
        if self.probe is not None:
            self.actions.append(self.probe())


DEEP_PROBE = DeepProbe()


def define_level(level: int) -> None:
    """Define crucible_frame::level_N, which runs level_N+1, or aten::cat at the end of the chain."""

    def run_level(source: torch.Tensor) -> torch.Tensor:
        if level + 1 < DEPTH:
            getattr(torch.ops.crucible_frame, f"level_{level + 1}")(torch.ones(LEVEL_INPUTS[level + 1]))
        else:
            parts = [torch.ones(part) for part in DEEP_PARTS]
            DEEP_PROBE.run()
            torch.cat(parts)
        return torch.zeros(LEVEL_OUTPUTS[level]) + source.sum()

    run_level.__name__ = f"level_{level}"
    torch.library.custom_op(f"crucible_frame::level_{level}", mutates_args=())(run_level)


for frame_level in range(DEPTH):
    define_level(frame_level)


def full_record_metas() -> ctypes.Array:
    """Return the C records of an operation with MAX_OP_INPUTS inputs and MAX_OP_OUTPUTS outputs."""
    metas = (Meta * (MAX_OP_INPUTS + MAX_OP_OUTPUTS))()
    for meta in metas:
        meta.ndim = 1
        meta.sizes[0] = 4
        meta.strides[0] = 1
        meta.dtype = FLOAT
        meta.device_idx = -1
    return metas


def wide_metas() -> ctypes.Array:
    """Return WIDE_RECORDS C records of a contiguous float32 tensor of WIDE_SHAPE."""
    metas = (Meta * WIDE_RECORDS)()
    for meta in metas:
        meta.ndim = len(WIDE_SHAPE)
        stride = 1
        for lane in reversed(range(len(WIDE_SHAPE))):
            meta.sizes[lane] = WIDE_SHAPE[lane]
            meta.strides[lane] = stride
            stride *= WIDE_SHAPE[lane]
        meta.dtype = FLOAT
        meta.device_idx = -1
    return metas


def bind_dispatch_op(ctx: CrucibleNative):
    """Return crucible_dispatch_op of the vessel library of ctx, which the layout table declares."""
    return ctx._vessel._lib.crucible_dispatch_op


class ProbeMode(TorchDispatchMode):
    """Call probe once inside each operation of PROBED, and keep the action that it gives."""

    PROBED = (torch.ops.aten.cat.default, torch.ops.aten.mm.default)

    def __init__(self, probe) -> None:
        """Keep the probe, a function with no argument that gives an action."""
        super().__init__()
        self.probe = probe
        self.actions: dict = {}

    def __torch_dispatch__(self, func, types, args=(), kwargs=None):
        """Run the probe in the first call of each probed operation, then run the operation."""
        if func in self.PROBED and func not in self.actions:
            self.actions[func] = self.probe()
        return func(*args, **(kwargs or {}))


def probe_actions() -> dict:
    """Return the action of crucible_dispatch_op with the most records, inside aten::cat and inside aten::mm."""
    metas = full_record_metas()
    full_hash = schema_hash("crucible_frame::full_records")
    with CrucibleNative() as ctx:
        dispatch_op = bind_dispatch_op(ctx)

        def probe() -> int:
            result = dispatch_op(ctx._handle, full_hash, MAX_OP_INPUTS, MAX_OP_OUTPUTS, metas, len(metas))
            return result.action

        with ProbeMode(probe) as mode:
            torch.cat([torch.ones(part) for part in TOP_PARTS])
            torch.mm(torch.ones(2, 3), torch.ones(3, 4))
    return mode.actions


def record(directory: Path) -> list[TraceOp]:
    """Record the loop under CrucibleNative until a region is active, and return its operations."""
    path = directory / "frame.crtrace"
    parts = [torch.ones(part) for part in TOP_PARTS]
    level_source = torch.ones(LEVEL_INPUTS[0])
    wide = wide_metas()
    wide_hash = schema_hash("crucible_frame::wide_records")
    single = full_record_metas()
    single_hash = schema_hash("crucible_frame::single_record")
    with CrucibleNative() as ctx:
        dispatch_op = bind_dispatch_op(ctx)
        DEEP_PROBE.probe = lambda: dispatch_op(ctx._handle, single_hash, 1, 0, single, 1).action
        try:
            for _ in range(MAX_ITERATIONS):
                dispatch_op(ctx._handle, wide_hash, WIDE_RECORDS // 2, WIDE_RECORDS // 2, wide, len(wide))
                torch.cat(parts)
                torch.ops.crucible_frame.level_0(level_source)
                if ctx.active_num_ops() > 0:
                    break
                ctx.flush()
        finally:
            DEEP_PROBE.probe = None
        if not ctx.export_trace(str(path)):
            raise RuntimeError(f"no region after {MAX_ITERATIONS} iterations")
    return read_trace(path)


def shape_of(meta: bytes) -> tuple[int, ...]:
    """Return the sizes of a meta, up to its rank."""
    ndim = meta[META_NDIM_OFFSET]
    return struct.unpack_from(f"<{ndim}q", meta, META_SIZES_OFFSET)


def tail_lanes(meta: bytes) -> tuple[int, ...]:
    """Return the lanes of the sizes and of the strides of a meta past its rank."""
    ndim = meta[META_NDIM_OFFSET]
    sizes = struct.unpack_from(f"<{LANES}q", meta, META_SIZES_OFFSET)
    strides = struct.unpack_from(f"<{LANES}q", meta, META_STRIDES_OFFSET)
    return sizes[ndim:] + strides[ndim:]


def check_cat(ops: list[TraceOp], parts: list[tuple[int, ...]], result: tuple[int, ...], label: str) -> list[str]:
    """Return lines when no aten::cat of the region holds the records of parts and result, or a tail is not zero."""
    wanted = schema_hash(CAT)
    expected = parts + [result]
    for op in ops:
        if op.schema_hash != wanted or op.num_inputs != len(parts) or op.num_outputs != 1:
            continue
        if [shape_of(meta) for meta in op.metas] == expected:
            tails = [tail_lanes(meta) for meta in op.metas]
            if any(any(tail) for tail in tails):
                return [f"a record of the {label} has a lane past its rank that is not zero: {tails}"]
            return []
    shapes = [[shape_of(meta) for meta in op.metas] for op in ops if op.schema_hash == wanted]
    return [f"no {CAT} of the region holds the records {expected} of the {label}, only {shapes}"]


def main() -> int:
    """Run each check and report."""
    problems = []
    actions = probe_actions()
    expected_actions = {
        torch.ops.aten.cat.default: (DispatchAction.REFUSED, "aten::cat holds no frame of the record stack"),
        torch.ops.aten.mm.default: (DispatchAction.RECORD, "aten::mm holds slots of the record stack"),
    }
    for func, (expected, failure) in expected_actions.items():
        action = actions.get(func)
        if action != expected:
            name = DispatchAction(action).name if action in iter(DispatchAction) else action
            problems.append(f"the probe inside {func} gives the action {name}, not {expected.name}: {failure}")
    with tempfile.TemporaryDirectory(prefix="crucible-unboxed-record-frame-") as scratch:
        ops = record(Path(scratch))
    free = [action for action in DEEP_PROBE.actions if action != DispatchAction.REFUSED]
    if not DEEP_PROBE.actions or free:
        problems.append(f"the stack has a free slot just before the aten::cat at the end of the chain in "
                        f"{len(free)} of {len(DEEP_PROBE.actions)} iterations, so the check does not reach the cold "
                        f"path")
    problems += check_cat(ops, TOP_PARTS, TOP_RESULT, "aten::cat of the loop, which records through a frame")
    problems += check_cat(ops, DEEP_PARTS, DEEP_RESULT, "aten::cat at the end of the chain, which takes the cold path")
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_unboxed_record_frame: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
