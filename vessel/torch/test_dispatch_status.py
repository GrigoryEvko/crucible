#!/usr/bin/env python3
"""The C entries give a refused op the action REFUSED, and a recorded op the action RECORD.

crucible_dispatch_op and crucible_dispatch_op_ex check the records of the
caller before the Vigil sees the op.  The Vigil holds no record of an op that a
check refuses, and the result of the entry must tell the caller that.  Each
check runs through the two entries:

    recorded        an op with one input and one output gives RECORD
    no array        a count of records with no array gives REFUSED
    count           a count of records that is not the inputs plus the outputs
                    gives REFUSED
    broken record   a record with the gradient byte 2 gives REFUSED
    too many        more records than the record stack of the Vigil holds gives
                    REFUSED
    again           an op with one input and one output after the refused ops
                    gives RECORD

Usage:
    python test_dispatch_status.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import ctypes
import sys
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import crucible_mode  # noqa: E402
import torch  # noqa: E402
from crucible_native import DispatchAction  # noqa: E402

# One intra-op thread and one inter-op thread.  The instruction count of the
# test is then the same in each run, and the budgets of the test launcher hold.
torch.set_num_threads(1)
torch.set_num_interop_threads(1)

Meta = crucible_mode._VesselLib.Meta
DispatchResult = crucible_mode._VesselLib.DispatchResult

# The slots of the record stack of a Vigil (kMaxOpRecords of
# crucible/ValidatedOp.h).
MAX_OP_RECORDS = 128

# The crucible ordinal of float32.
FLOAT = 6

# The schema hash of each op of the test.  The Vigil reads it as an opaque key.
# The thunk computes the guard word from the records.
SCHEMA_HASH = 0x5EED_0001


def records(count: int) -> ctypes.Array:
    """Return count C records of a float32 tensor of four elements on the host."""
    metas = (Meta * count)()
    for meta in metas:
        meta.ndim = 1
        meta.sizes[0] = 4
        meta.strides[0] = 1
        meta.dtype = FLOAT
        meta.device_idx = -1
    return metas


def bind(lib: ctypes.CDLL) -> tuple:
    """Return crucible_dispatch_op and crucible_dispatch_op_ex of lib, with their C signatures."""
    dispatch_op = lib.crucible_dispatch_op
    dispatch_op.restype = DispatchResult
    dispatch_op.argtypes = [
        ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint16, ctypes.c_uint16, ctypes.POINTER(Meta), ctypes.c_uint32,
    ]
    dispatch_op_ex = lib.crucible_dispatch_op_ex
    dispatch_op_ex.restype = DispatchResult
    dispatch_op_ex.argtypes = [
        ctypes.c_void_p, ctypes.c_uint64, ctypes.c_uint16, ctypes.c_uint16, ctypes.POINTER(Meta), ctypes.c_uint32,
        ctypes.POINTER(ctypes.c_int64), ctypes.c_uint16, ctypes.c_uint16, ctypes.c_uint8, ctypes.c_uint8,
    ]
    return dispatch_op, dispatch_op_ex


def action_name(action: int) -> str:
    """Return the name of an action byte, or the byte itself when no action has it."""
    try:
        return DispatchAction(action).name
    except ValueError:
        return str(action)


def check(problems: list[str], entry: str, case: str, result: DispatchResult, expected: DispatchAction) -> None:
    """Add one line to problems when result does not have the action expected and zero payload."""
    if result.action != expected:
        problems.append(f"{entry}: {case} gives the action {action_name(result.action)}, not {expected.name}")
    if expected != DispatchAction.COMPILED and (result.status != 0 or result.op_index != 0):
        problems.append(f"{entry}: {case} gives the status {result.status} and the op index {result.op_index}, "
                        f"not zero")


def run_entry(entry: str, call, handle: int) -> list[str]:
    """Run each case through call, which takes (handle, inputs, outputs, metas, count), and return the problems."""
    problems: list[str] = []
    pair = records(2)
    broken = records(2)
    broken[1].requires_grad = 2
    three = records(3)
    too_many = records(MAX_OP_RECORDS + 1)

    recorded = call(handle, 1, 1, pair, 2)
    check(problems, entry, "an op with one input and one output", recorded, DispatchAction.RECORD)
    refused = call(handle, 1, 1, None, 2)
    check(problems, entry, "a count of two records with no array", refused, DispatchAction.REFUSED)
    if refused.action == recorded.action:
        problems.append(f"{entry}: a refused op and a recorded op give the same action "
                        f"{action_name(recorded.action)}")
    check(problems, entry, "three records for one input and one output", call(handle, 1, 1, three, 3),
          DispatchAction.REFUSED)
    check(problems, entry, "a record with the gradient byte 2", call(handle, 1, 1, broken, 2), DispatchAction.REFUSED)
    check(problems, entry, f"{MAX_OP_RECORDS + 1} records", call(handle, 64, 65, too_many, MAX_OP_RECORDS + 1),
          DispatchAction.REFUSED)
    check(problems, entry, "an op with one input and one output after the refused ops", call(handle, 1, 1, pair, 2),
          DispatchAction.RECORD)
    return problems


def main() -> int:
    """Run each check through the two entries and report."""
    vessel = crucible_mode._VesselLib()
    dispatch_op, dispatch_op_ex = bind(vessel._lib)
    handle = vessel.crucible_create()
    try:
        problems = run_entry(
            "crucible_dispatch_op",
            lambda h, inputs, outputs, metas, count: dispatch_op(h, SCHEMA_HASH, inputs, outputs, metas, count),
            handle)
        problems += run_entry(
            "crucible_dispatch_op_ex",
            lambda h, inputs, outputs, metas, count: dispatch_op_ex(h, SCHEMA_HASH, inputs, outputs, metas, count,
                                                                    None, 0, 0, 0, 0),
            handle)
    finally:
        vessel.crucible_destroy(handle)
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_dispatch_status: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
