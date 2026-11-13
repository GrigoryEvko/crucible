#!/usr/bin/env python3
"""The native vessel records the operations and the values that it used to leave out.

Each check records a loop under CrucibleNative until the Vigil activates a
region, exports the region as a .crtrace file, and reads the file back:

    narrow dtypes   an operation on a float8_e8m0fnu tensor and an operation on
                    a float4_e2m1fn_x2 tensor are in the region
    scalar result   aten::_local_scalar_dense, the operation of .item(), has an
                    output meta of rank zero, because its value drives the
                    control flow of the caller
    overflow        a fused update with 40 tensors and 40 step sizes gives an
                    entry whose scalar slots differ when only the last step
                    size differs, or when only the shape of the last tensor
                    differs.  The content hash of a region folds these slots

Usage:
    python test_record_coverage.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import ctypes
import struct
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402

import crucible_mode  # noqa: E402
from crucible_native import CrucibleNative  # noqa: E402

# One intra-op thread and one inter-op thread.  The instruction count of the
# test is then the same in each run, and the budgets of the test launcher hold.
torch.set_num_threads(1)
torch.set_num_interop_threads(1)

# The iteration limit of one recording.  The Vigil activates a region after
# two periods of the loop.
MAX_ITERATIONS = 60

# The tensors and the step sizes of the fused update: more than the 32 metas
# and the 5 scalars that one entry holds inline.
UPDATE_WIDTH = 40

HEADER = struct.Struct("<4sIII")
OP_RECORD = struct.Struct("<QQQQ5qHHHBB")
META_SIZE = ctypes.sizeof(crucible_mode._VesselLib.Meta)
META_NDIM_OFFSET = crucible_mode._VesselLib.Meta.ndim.offset
META_DTYPE_OFFSET = crucible_mode._VesselLib.Meta.dtype.offset

# The crucible ordinals of the element types that the checks read.
DOUBLE = 7
FLOAT8_E8M0FNU = 44
FLOAT4_E2M1FN_X2 = 45


@dataclass(frozen=True)
class TraceOp:
    """One operation of an exported trace, with the metas of its tensors."""

    schema_hash: int
    scalars: tuple[int, ...]
    num_inputs: int
    num_outputs: int
    metas: tuple[bytes, ...]

    def dtypes(self) -> list[int]:
        """Return the element type byte of each meta."""
        return [struct.unpack_from("<b", meta, META_DTYPE_OFFSET)[0] for meta in self.metas]


def read_trace(path: Path) -> list[TraceOp]:
    """Return the operations of a .crtrace file.  O(n) in the size of the file."""
    data = path.read_bytes()
    magic, _version, num_ops, _num_metas = HEADER.unpack_from(data, 0)
    if magic != b"CRTR":
        raise ValueError(f"{path} is no .crtrace file")
    records = []
    offset = HEADER.size
    for _ in range(num_ops):
        records.append(OP_RECORD.unpack_from(data, offset))
        offset += OP_RECORD.size
    ops = []
    for record in records:
        schema_hash, _shape, _scope, _callsite, *values = record[:9]
        num_inputs, num_outputs, num_scalars = record[9:12]
        metas = []
        for _ in range(num_inputs + num_outputs):
            metas.append(data[offset:offset + META_SIZE])
            offset += META_SIZE
        ops.append(TraceOp(schema_hash, tuple(values[:num_scalars]), num_inputs, num_outputs, tuple(metas)))
    return ops


def schema_hash(name: str) -> int:
    """Return the schema hash of an operator name such as aten::add.Tensor."""
    return crucible_mode._fnv1a(name.encode("ascii"))


def record(step, directory: Path, label: str) -> list[TraceOp]:
    """Record step() under CrucibleNative until a region is active, and return its operations."""
    path = directory / f"{label}.crtrace"
    with CrucibleNative() as ctx:
        for _ in range(MAX_ITERATIONS):
            step()
            if ctx.active_num_ops() > 0:
                break
            ctx.flush()
        if not ctx.export_trace(str(path)):
            raise RuntimeError(f"{label}: no region after {MAX_ITERATIONS} iterations")
    return read_trace(path)


def narrow_step() -> None:
    """One iteration over the two narrow element types and one .item()."""
    source = torch.arange(1, 9, dtype=torch.float32)
    scale = source.to(torch.float8_e8m0fnu)
    widened = scale.to(torch.float32)
    packed = torch.zeros(4, dtype=torch.uint8).view(torch.float4_e2m1fn_x2)
    restored = packed.view(torch.uint8)
    total = (widened * 2).sum().item()
    del restored, total


def update_step(last_step_size: float, last_extent: int):
    """Return one iteration of a fused update with UPDATE_WIDTH tensors and step sizes.

    The CPU kernel of the fused update calls add_ once for each tensor, and the
    vessel records each call.  The detector keys an operation by its schema and
    its shape, so tensor i has the extent i + 1.  Then no run of those calls
    repeats, and the only period is the full step, which holds the fused update.
    last_extent must be more than UPDATE_WIDTH - 1.
    """
    tensors = [torch.zeros(extent) for extent in range(1, UPDATE_WIDTH)] + [torch.zeros(last_extent)]
    step_sizes = [0.5] * (UPDATE_WIDTH - 1) + [last_step_size]

    def step() -> None:
        torch._foreach_add_(tensors, step_sizes)

    return step


def check_narrow_dtypes(ops: list[TraceOp]) -> list[str]:
    """Return one line for each narrow element type that no recorded operation has."""
    seen = {dtype for op in ops for dtype in op.dtypes()}
    problems = []
    for name, ordinal in (("float8_e8m0fnu", FLOAT8_E8M0FNU), ("float4_e2m1fn_x2", FLOAT4_E2M1FN_X2)):
        if ordinal not in seen:
            problems.append(f"no recorded operation has a {name} tensor")
    return problems


def check_scalar_result(ops: list[TraceOp]) -> list[str]:
    """Return one line when the value of .item() has no output meta of rank zero."""
    wanted = schema_hash("aten::_local_scalar_dense")
    recorded = [op for op in ops if op.schema_hash == wanted]
    if not recorded:
        return ["the region holds no aten::_local_scalar_dense"]
    op = recorded[0]
    if op.num_outputs != 1:
        return [f"aten::_local_scalar_dense has {op.num_outputs} output metas, not 1"]
    output = op.metas[op.num_inputs]
    ndim = output[META_NDIM_OFFSET]
    dtype = struct.unpack_from("<b", output, META_DTYPE_OFFSET)[0]
    if ndim != 0 or dtype != DOUBLE:
        return [f"the output meta of aten::_local_scalar_dense has rank {ndim} and element type {dtype}"]
    return []


def update_slots(ops: list[TraceOp]) -> tuple[int, ...]:
    """Return the scalar slots of the fused update of a region."""
    wanted = schema_hash("aten::_foreach_add_.ScalarList")
    for op in ops:
        if op.schema_hash == wanted:
            return op.scalars
    raise RuntimeError("the region holds no aten::_foreach_add_.ScalarList")


def check_overflow(directory: Path) -> list[str]:
    """Return one line for each pair of updates that the scalar slots do not tell apart."""
    base = update_slots(record(update_step(0.25, 64), directory, "base"))
    again = update_slots(record(update_step(0.25, 64), directory, "again"))
    step_size = update_slots(record(update_step(0.75, 64), directory, "step_size"))
    extent = update_slots(record(update_step(0.25, 65), directory, "extent"))
    problems = []
    if base != again:
        problems.append("one update recorded two times gives two different scalar slots")
    if base == step_size:
        problems.append("two updates that differ only in the last step size give the same scalar slots")
    if base == extent:
        problems.append("two updates that differ only in the shape of the last tensor give the same scalar slots")
    return problems


def main() -> int:
    """Run each check and report."""
    with tempfile.TemporaryDirectory(prefix="crucible-record-coverage-") as scratch:
        directory = Path(scratch)
        narrow_ops = record(narrow_step, directory, "narrow")
        problems = check_narrow_dtypes(narrow_ops) + check_scalar_result(narrow_ops) + check_overflow(directory)
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_record_coverage: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
