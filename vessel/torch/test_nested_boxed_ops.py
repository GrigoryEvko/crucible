#!/usr/bin/env python3
"""Each boxed operation keeps its own schema hash and the records of its own tensors.

The boxed fallback records each operation that has no unboxed kernel.  It
writes the records of an operation into a frame of the record stack of the
Vigil: the inputs before the operation runs, and the outputs after it.  An
operation can dispatch other operations while it runs, and the Crucible key
fires again for each of them on the same thread.  The check records a loop
under CrucibleNative until the Vigil activates a region, exports the region
as a .crtrace file, and reads the file back:

    schema hash     crucible_nest::scale and crucible_nest::outer run one
                    after the other from the loop, at one stack depth.  Each
                    operation of the region has the schema hash of its own
                    name
    nested records  crucible_nest::outer runs crucible_nest::inner.  Each
                    operation of the region holds the records of its own
                    tensors
    unwind          crucible_nest::fail raises in its kernel.  The unwind
                    closes the frame of the fallback.  Then a C caller can
                    still decode the most records of one operation into the
                    stack, and the region holds that operation

Usage:
    python test_nested_boxed_ops.py

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

import crucible_mode  # noqa: E402
import torch  # noqa: E402
from crucible_native import CrucibleNative  # noqa: E402

# The import sets one intra-op thread and one inter-op thread, so the
# instruction count of the test is the same in each run.  PyTorch refuses a
# second set of the inter-op threads, so this file does not set them again.
from test_record_coverage import TraceOp, read_trace, schema_hash  # noqa: E402

# The iteration limit of one recording.  The Vigil activates a region after
# two periods of the loop.
MAX_ITERATIONS = 60

# The calls of crucible_nest::fail in each iteration.  The fallback holds 32
# slots of the stack of 128 slots for each operation, so a frame that an
# unwind does not close fills the stack after four calls.
FAILURES = 8

# The most inputs and the most outputs of one operation (kMaxOpInputs and
# kMaxOpOutputs of crucible/ValidatedOp.h).  An operation with each of them
# needs each slot of the record stack.
MAX_OP_INPUTS = 64
MAX_OP_OUTPUTS = 64

META_SIZES_OFFSET = crucible_mode._VesselLib.Meta.sizes.offset
META_NDIM_OFFSET = crucible_mode._VesselLib.Meta.ndim.offset

# The crucible ordinal of float32.
FLOAT = 6

SCALE_SOURCE = (5,)
OUTER_FIRST = (3, 5)
OUTER_SECOND = (7,)
OUTER_RESULT = (11,)
INNER_SOURCE = (2, 2, 2)
FULL_RECORD = (4,)


@torch.library.custom_op("crucible_nest::inner", mutates_args=())
def inner(source: torch.Tensor) -> torch.Tensor:
    """Return a copy of the source."""
    return source.clone()


@torch.library.custom_op("crucible_nest::outer", mutates_args=())
def outer(first: torch.Tensor, second: torch.Tensor) -> torch.Tensor:
    """Run crucible_nest::inner on a tensor of its own, and return a tensor of OUTER_RESULT."""
    nested = torch.ops.crucible_nest.inner(torch.ones(INNER_SOURCE))
    return torch.zeros(OUTER_RESULT) + nested.sum() + first.sum() + second.sum()


@torch.library.custom_op("crucible_nest::scale", mutates_args=())
def scale(source: torch.Tensor) -> torch.Tensor:
    """Return two times the source."""
    return source * 2


@torch.library.custom_op("crucible_nest::fail", mutates_args=())
def fail(source: torch.Tensor) -> torch.Tensor:
    """Raise on each call."""
    raise RuntimeError("crucible_nest::fail raises on each call")


def full_record_metas() -> ctypes.Array:
    """Return the C records of an operation with MAX_OP_INPUTS inputs and MAX_OP_OUTPUTS outputs."""
    metas = (crucible_mode._VesselLib.Meta * (MAX_OP_INPUTS + MAX_OP_OUTPUTS))()
    for meta in metas:
        meta.ndim = 1
        meta.sizes[0] = FULL_RECORD[0]
        meta.strides[0] = 1
        meta.dtype = FLOAT
        meta.device_idx = -1
    return metas


def bind_dispatch_op(ctx: CrucibleNative):
    """Return crucible_dispatch_op of the vessel library of ctx, with its C signature."""
    function = ctx._vessel._lib.crucible_dispatch_op
    function.restype = crucible_mode._VesselLib.DispatchResult
    function.argtypes = [
        ctypes.c_void_p,
        ctypes.c_uint64,
        ctypes.c_uint64,
        ctypes.c_uint16,
        ctypes.c_uint16,
        ctypes.POINTER(crucible_mode._VesselLib.Meta),
        ctypes.c_uint32,
    ]
    return function


def record(directory: Path) -> list[TraceOp]:
    """Record the loop under CrucibleNative until a region is active, and return its operations."""
    path = directory / "nested.crtrace"
    first = torch.ones(OUTER_FIRST)
    second = torch.ones(OUTER_SECOND)
    source = torch.ones(SCALE_SOURCE)
    metas = full_record_metas()
    full_hash = schema_hash("crucible_nest::full_records")
    with CrucibleNative() as ctx:
        dispatch_op = bind_dispatch_op(ctx)
        for _ in range(MAX_ITERATIONS):
            for _ in range(FAILURES):
                try:
                    torch.ops.crucible_nest.fail(source)
                except RuntimeError:
                    pass
            dispatch_op(ctx._handle, full_hash, full_hash, MAX_OP_INPUTS, MAX_OP_OUTPUTS, metas, len(metas))
            torch.ops.crucible_nest.scale(source)
            torch.ops.crucible_nest.outer(first, second)
            if ctx.active_num_ops() > 0:
                break
            ctx.flush()
        if not ctx.export_trace(str(path)):
            raise RuntimeError(f"no region after {MAX_ITERATIONS} iterations")
    return read_trace(path)


def shape_of(meta: bytes) -> tuple[int, ...]:
    """Return the sizes of a meta, up to its rank."""
    ndim = meta[META_NDIM_OFFSET]
    return struct.unpack_from(f"<{ndim}q", meta, META_SIZES_OFFSET)


def ops_named(ops: list[TraceOp], name: str) -> list[TraceOp]:
    """Return each operation of the region with the schema hash of name."""
    wanted = schema_hash(name)
    return [op for op in ops if op.schema_hash == wanted]


def check_records(ops: list[TraceOp], name: str, inputs: list[tuple[int, ...]],
                  outputs: list[tuple[int, ...]]) -> list[str]:
    """Return one line for each operation of name that does not hold the shapes of its own tensors."""
    named = ops_named(ops, name)
    if not named:
        return [f"the region holds no {name}"]
    expected = inputs + outputs
    problems = []
    for op in named:
        shapes = [shape_of(meta) for meta in op.metas]
        if op.num_inputs != len(inputs) or op.num_outputs != len(outputs) or shapes != expected:
            counts = f"{op.num_inputs} inputs, {op.num_outputs} outputs"
            problems.append(f"an op with the hash of {name} has {counts} and the shapes {shapes}, not {expected}")
    return problems


def main() -> int:
    """Run each check and report."""
    with tempfile.TemporaryDirectory(prefix="crucible-nested-boxed-ops-") as scratch:
        ops = record(Path(scratch))
    problems = check_records(ops, "crucible_nest::scale", [SCALE_SOURCE], [SCALE_SOURCE])
    problems += check_records(ops, "crucible_nest::outer", [OUTER_FIRST, OUTER_SECOND], [OUTER_RESULT])
    problems += check_records(ops, "crucible_nest::inner", [INNER_SOURCE], [INNER_SOURCE])
    problems += check_records(ops, "crucible_nest::full_records", [FULL_RECORD] * MAX_OP_INPUTS,
                              [FULL_RECORD] * MAX_OP_OUTPUTS)
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_nested_boxed_ops: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
