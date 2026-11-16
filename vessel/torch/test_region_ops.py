#!/usr/bin/env python3
"""The region of one small training loop holds the operations of region_ops_mlp.txt, in their order.

The script records one loop under attach() until the Vigil activates a region.
The loop trains nn.Sequential(Linear(16, 24), ReLU(), Linear(24, 4)) on a
batch of 8 float32 rows on the host, with the MSE loss and plain SGD.  Each
iteration is one forward pass, one backward pass and one optimizer step.  The
script exports the region as a .crtrace file and reads the file back.  It
writes one line for each operation, in the order of the file.  That order is
the order in which the C++ recorder gives each operation to the Vigil.

A line holds these fields:

    index     the position of the operation in the region
    phase     F for the forward pass, B for the backward pass, O for the
              optimizer, X for each other phase
    depth     the nesting depth: 0 for an operation that no recorded
              operation runs, and d + 1 for an operation that an operation
              of depth d runs
    name      the qualified name of the operator, from the name table, after
              two spaces for each level of the depth
    inputs    the shape and the element type of each input tensor
    outputs   the shape and the element type of each output tensor

A recording kernel gives its operation to the Vigil after the operation runs.
So an operation that another operation runs comes before the operation that
runs it.  The parent of an operation of depth d is the first operation after
it with the depth d - 1.

The test also checks the depths that the plan of the first compute path reads:
aten::linear and aten::mse_loss of the forward pass are at depth 0, and the
operations that they run are below them.

Usage:
    python test_region_ops.py            compare the list with the golden file
    python test_region_ops.py --print    print the list
    python test_region_ops.py --write    write the list into the golden file

Exit 0 when the list is the list of the golden file, 1 otherwise.
"""

from __future__ import annotations

import difflib
import struct
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402
import torch.nn as nn  # noqa: E402

import crucible_mode  # noqa: E402
from crucible_native import attach  # noqa: E402

# The import sets one intra-op thread and one inter-op thread, so that each run
# takes the same kernels of PyTorch and gives the same operations.  PyTorch
# refuses a second set of the inter-op threads, so this file does not set them
# again.
from test_record_coverage import TraceOp, read_schema_names, read_trace  # noqa: E402

GOLDEN = Path(__file__).resolve().parent / "region_ops_mlp.txt"

# The iteration limit of one recording.  The Vigil activates a region after
# two periods of the loop.
MAX_ITERATIONS = 60

BATCH = 8
IN_FEATURES = 16
HIDDEN = 24
OUT_FEATURES = 4
SEED = 0
LEARNING_RATE = 0.01

META_SIZES_OFFSET = crucible_mode._VesselLib.Meta.sizes.offset
META_NDIM_OFFSET = crucible_mode._VesselLib.Meta.ndim.offset
META_DTYPE_OFFSET = crucible_mode._VesselLib.Meta.dtype.offset

PHASE_SHIFT = 2
PHASE_LETTERS = "FBOX"

# The short names of the crucible element types that the loop uses.
DTYPE_NAMES = {6: "f32", 4: "i64", 11: "b8", 7: "f64"}


@dataclass(frozen=True)
class RegionOp:
    """One operation of an exported region."""

    name: str
    phase: str
    depth: int
    inputs: tuple[str, ...]
    outputs: tuple[str, ...]

    def line(self, index: int) -> str:
        """Return the line of the operation at index, with the name indented by its depth."""
        inputs = " ".join(self.inputs) or "-"
        outputs = " ".join(self.outputs) or "-"
        indent = "  " * self.depth
        return f"{index:3d} {self.phase} {self.depth} {indent}{self.name} : {inputs} -> {outputs}"


def describe_meta(meta: bytes) -> str:
    """Return the shape and the element type of one metadata record, such as f32[8,24]."""
    ndim = meta[META_NDIM_OFFSET]
    sizes = struct.unpack_from(f"<{ndim}q", meta, META_SIZES_OFFSET)
    dtype = struct.unpack_from("<b", meta, META_DTYPE_OFFSET)[0]
    return f"{DTYPE_NAMES.get(dtype, f'dtype{dtype}')}[{','.join(str(size) for size in sizes)}]"


def region_op(op: TraceOp, names: dict[int, str]) -> RegionOp:
    """Return the line fields of one operation of a trace, with its name from the name table."""
    metas = [describe_meta(meta) for meta in op.metas]
    return RegionOp(
        name=names.get(op.schema_hash, f"schema{op.schema_hash:016x}"),
        phase=PHASE_LETTERS[(op.flags >> PHASE_SHIFT) & 0x3],
        depth=op.depth,
        inputs=tuple(metas[:op.num_inputs]),
        outputs=tuple(metas[op.num_inputs:]),
    )


def read_region(path: Path) -> list[RegionOp]:
    """Return the operations of a .crtrace file, each with its name.  O(n) in the size of the file."""
    names = read_schema_names(path)
    return [region_op(op, names) for op in read_trace(path)]


def record_region(directory: Path) -> list[RegionOp]:
    """Record the training loop under attach() until a region is active, and return its operations."""
    torch.manual_seed(SEED)
    model = nn.Sequential(nn.Linear(IN_FEATURES, HIDDEN), nn.ReLU(), nn.Linear(HIDDEN, OUT_FEATURES))
    optimizer = torch.optim.SGD(model.parameters(), lr=LEARNING_RATE)
    criterion = nn.MSELoss()
    inputs = torch.randn(BATCH, IN_FEATURES)
    targets = torch.randn(BATCH, OUT_FEATURES)
    path = directory / "mlp.crtrace"
    with attach(model, optimizer, device="cpu") as ctx:
        for _ in range(MAX_ITERATIONS):
            optimizer.zero_grad()
            loss = criterion(model(inputs), targets)
            loss.backward()
            optimizer.step()
            if ctx.active_num_ops() > 0:
                break
            ctx.flush()
        if not ctx.export_trace(str(path)):
            raise RuntimeError(f"no region after {MAX_ITERATIONS} iterations")
    return read_region(path)


def check_nests(ops: list[RegionOp]) -> list[str]:
    """Return one line for each operation whose nest does not close on a parent one level up.

    An operation of depth d > 0 runs inside its parent, which comes later with
    the depth d - 1.  Each operation between the two runs inside the parent
    too, so its depth is at least d.  An operation of the end of the region
    has no parent in the region, and it holds no line.  O(n^2) in the
    operations, for a region of 94.
    """
    problems = []
    for index, op in enumerate(ops):
        if op.depth == 0:
            continue
        for later in ops[index + 1:]:
            if later.depth < op.depth:
                if later.depth != op.depth - 1:
                    problems.append(f"op {index} {op.name} of depth {op.depth} closes on {later.name} of depth "
                                    f"{later.depth}")
                break
    return problems


# The depths that the plan of the first compute path reads, as (name, the
# shape of the first input, depth).  A lowering takes only the operations of
# depth 0.
EXPECTED_DEPTHS = (
    ("aten::linear", "f32[8,16]", 0),
    ("aten::addmm", "f32[24]", 1),
    ("aten::copy_", "f32[8,24]", 2),
    ("aten::relu", "f32[8,24]", 0),
    ("aten::mse_loss", "f32[8,4]", 0),
    ("aten::mean.out", "f32[8,4]", 1),
    ("aten::sum.IntList_out", "f32[8,4]", 2),
    ("aten::div_.Scalar", "f32[]", 2),
)


def check_expected_depths(ops: list[RegionOp]) -> list[str]:
    """Return one line for each row of EXPECTED_DEPTHS that no operation of the forward pass matches."""
    problems = []
    for name, first_input, depth in EXPECTED_DEPTHS:
        found = [op for op in ops if op.phase == "F" and op.name == name and op.inputs[:1] == (first_input,)]
        if not found:
            problems.append(f"the forward pass holds no {name} with the first input {first_input}")
        elif any(op.depth != depth for op in found):
            problems.append(f"{name} with the first input {first_input} has the depths "
                            f"{sorted({op.depth for op in found})}, not {depth}")
    return problems


def main() -> int:
    """Record the region, then print it, write it, or compare it with the golden file."""
    with tempfile.TemporaryDirectory(prefix="crucible-region-ops-") as scratch:
        ops = record_region(Path(scratch))
    lines = [op.line(index) for index, op in enumerate(ops)]
    if "--print" in sys.argv:
        print("\n".join(lines))
        return 0
    problems = check_nests(ops) + check_expected_depths(ops)
    for line in problems:
        print(f"  FAIL: {line}")
    if "--write" in sys.argv:
        GOLDEN.write_text("\n".join(lines) + "\n")
        print(f"test_region_ops: wrote {len(lines)} operations into {GOLDEN.name}")
        return 1 if problems else 0
    golden = GOLDEN.read_text().splitlines()
    if lines != golden:
        print(f"  FAIL: the region of {len(lines)} operations is not the list of {len(golden)} operations of "
              f"{GOLDEN.name}.  If the change of the list is correct, run this file with --write")
        print("\n".join(difflib.unified_diff(golden, lines, GOLDEN.name, "the region", lineterm="")))
        return 1
    if problems:
        return 1
    print(f"test_region_ops: the region holds the {len(lines)} operations of {GOLDEN.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
