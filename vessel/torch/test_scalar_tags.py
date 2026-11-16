#!/usr/bin/env python3
"""Two calls that differ only in the type of a scalar, or in which optional is absent, give two op records.

Each scalar of an op goes into an int64 slot of its record.  The double
4.9e-324 has the bits of the integer 1, and aten::clamp with only min=1.0 has
the one scalar of aten::clamp with only max=1.0.  The record holds the tag of
each slot, which is the tag of its boxed value: INT for an integer or an
enumerator, FLOAT for a double, BOOL for a bool, and NONE for an argument that
boxes into None, such as an absent optional.  The checks record a loop of one
call under CrucibleNative until a region is active, export it, and compare the
op records of two loops:

    type of alpha   aten::add.Tensor with alpha=1 and with alpha=4.9e-324
    absent optional aten::clamp with min=1.0 and with max=1.0

Usage:
    python test_scalar_tags.py

Exit 0 when each pair gives two different records, 1 otherwise.
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402

# The import sets one intra-op thread and one inter-op thread.  PyTorch refuses
# a second set of the inter-op threads, so this file does not set them again.
from test_record_coverage import TraceOp, record, schema_hash  # noqa: E402

# The tags of a slot, as ScalarType2 of crucible/TraceRing.h numbers them.
INT, FLOAT, BOOL, NONE = 0, 1, 2, 3

# The smallest positive double, whose bits are the integer 1.
SMALLEST_DOUBLE = 4.9e-324


def add_step(alpha: float | int):
    """Return one iteration of aten::add.Tensor with the alpha."""
    first = torch.ones(4)
    second = torch.ones(4)

    def step() -> None:
        torch.add(first, second, alpha=alpha)

    return step


def clamp_step(**bounds: float):
    """Return one iteration of aten::clamp with the bounds."""
    source = torch.ones(4)

    def step() -> None:
        torch.clamp(source, **bounds)

    return step


def first_op(ops: list[TraceOp], name: str) -> TraceOp:
    """Return the first op of name in a region."""
    wanted = schema_hash(name)
    for op in ops:
        if op.schema_hash == wanted:
            return op
    raise RuntimeError(f"the region holds no {name}")


def check_pair(directory: Path, label: str, name: str, first, second,
               tags: tuple[tuple[int, ...], tuple[int, ...]]) -> list[str]:
    """Return one line for each problem of a pair: one record for the two loops, or a slot of a wrong tag.

    tags holds the expected tag of each scalar slot of the op of each loop.
    """
    ops = (first_op(record(first, directory, f"{label}-first"), name),
           first_op(record(second, directory, f"{label}-second"), name))
    problems = []
    if ops[0].record == ops[1].record:
        problems.append(f"{label}: the two calls of {name} give one op record, so the region cannot tell them apart")
    for op, expected in zip(ops, tags):
        found = tuple(op.scalar_tag(slot) for slot in range(len(op.scalars)))
        if found != expected:
            problems.append(f"{label}: {name} has the scalars {op.scalars} with the tags {found}, not {expected}")
    return problems


def main() -> int:
    """Run each check and report."""
    assert int.from_bytes(torch.tensor(SMALLEST_DOUBLE, dtype=torch.float64).numpy().tobytes(), "little") == 1
    with tempfile.TemporaryDirectory(prefix="crucible-scalar-tags-") as scratch:
        directory = Path(scratch)
        problems = check_pair(directory, "type of alpha", "aten::add.Tensor", add_step(1), add_step(SMALLEST_DOUBLE),
                              ((INT,), (FLOAT,)))
        problems += check_pair(directory, "absent optional", "aten::clamp", clamp_step(min=1.0),
                               clamp_step(max=1.0), ((FLOAT, NONE), (NONE, FLOAT)))
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_scalar_tags: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
