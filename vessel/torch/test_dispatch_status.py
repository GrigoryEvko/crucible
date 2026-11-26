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
                    REFUSED from crucible_dispatch_op.  crucible_dispatch_op_ex
                    keeps the first 32 records, as the native paths do, so it
                    gives RECORD
    again           an op with one input and one output after the refused ops
                    gives RECORD

crucible_dispatch_op_ex also checks the claim of the scalars: a count past
CRUCIBLE_MAX_FFI_SCALARS, a null array with a count, a tag of 4 and a role of
2 give REFUSED.  A count of 257 scalars with each array of that length, and
data slots in the first five, give RECORD.  A bit of the tag of slot 4 in the
op flags gives REFUSED.

The eager word of crucible_create and crucible_observe_eager_numerics has a
layout (EagerNumerics of crucible/NumericalRecipe.h).  A word with a bit that
no field names, or with the precision 3, gives the status REFUSED and no Vigil.
Each result of a live handle gives the handle status LIVE.

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
import crucible_native  # noqa: E402
from crucible_native import DispatchAction, HandleStatus  # noqa: E402

# One intra-op thread and one inter-op thread.  The instruction count of the
# test is then the same in each run, and the budgets of the test launcher hold.
torch.set_num_threads(1)
torch.set_num_interop_threads(1)

Meta = crucible_native.CrucibleMeta
DispatchResult = crucible_native.CrucibleDispatchResult

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
    if result.handle_status != HandleStatus.LIVE:
        problems.append(f"{entry}: {case} gives the handle status {result.handle_status} for a live handle")
    if expected != DispatchAction.COMPILED and (result.status != 0 or result.op_index != 0):
        problems.append(f"{entry}: {case} gives the status {result.status} and the op index {result.op_index}, "
                        f"not zero")


def run_entry(entry: str, call, handle: int, too_many_action: DispatchAction) -> list[str]:
    """Run each case through call, which takes (handle, inputs, outputs, metas, count), and return the problems.

    too_many_action is the action of the entry for more records than the
    record stack holds.
    """
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
          too_many_action)
    check(problems, entry, "an op with one input and one output after the refused ops", call(handle, 1, 1, pair, 2),
          DispatchAction.RECORD)
    return problems


def check_scalar_claims(dispatch_op_ex, handle: int) -> list[str]:
    """Return one line for each claim of the scalars of crucible_dispatch_op_ex that the entry judges wrongly.

    An entry holds five scalars inline, and the scalars past them fold into
    the overflow digest.  So a count of 257 scalars records, and it does not
    wrap to a count of one.  The count, the tags and the roles come from the
    caller, so a count past the bound, a null array, and a tag or a role that
    names no value give REFUSED.
    """
    problems: list[str] = []
    pair = records(2)
    limit = crucible_native.DispatchLimit.FFI_SCALARS
    many = 257
    values = (ctypes.c_int64 * many)()
    tags = (ctypes.c_uint8 * many)()
    roles = (ctypes.c_uint8 * many)()
    unnamed_tag = (ctypes.c_uint8 * many)(*([0] * (many - 1) + [4]))
    unnamed_role = (ctypes.c_uint8 * many)(*([0] * (many - 1) + [2]))
    data_roles = (ctypes.c_uint8 * many)(*([1] * 5 + [0] * (many - 5)))
    entry = "crucible_dispatch_op_ex"
    # The arrays hold 257 elements, and the entry refuses a count past the
    # bound before it reads an element.
    cases = (
        ("a count past the bound", values, tags, roles, limit + 1, DispatchAction.REFUSED),
        ("a null value array with 5 scalars", None, tags, roles, 5, DispatchAction.REFUSED),
        ("a null tag array with 5 scalars", values, None, roles, 5, DispatchAction.REFUSED),
        ("a null role array with 5 scalars", values, tags, None, 5, DispatchAction.REFUSED),
        ("a tag of 4 in the last of 257 scalars", values, unnamed_tag, roles, many, DispatchAction.REFUSED),
        ("a role of 2 in the last of 257 scalars", values, tags, unnamed_role, many, DispatchAction.REFUSED),
        ("257 scalars", values, tags, roles, many, DispatchAction.RECORD),
        ("257 scalars with five data slots", values, tags, data_roles, many, DispatchAction.RECORD),
    )
    for case, values_at, tags_at, roles_at, count, expected in cases:
        result = dispatch_op_ex(handle, SCHEMA_HASH, 1, 1, pair, 2, values_at, tags_at, roles_at, count, 0)
        check(problems, entry, case, result, expected)
    # Bits 6 and 7 of the op flags hold the tag of slot 4, which the entry
    # writes from the scalars.
    check(problems, entry, "the bit of the tag of slot 4 in the op flags",
          dispatch_op_ex(handle, SCHEMA_HASH, 1, 1, pair, 2, None, None, None, 0, 1 << 6), DispatchAction.REFUSED)
    return problems


# Eager words with a bit that no field of EagerNumerics names, or with the
# precision 3, which PyTorch does not name.
UNNAMED_EAGER_WORDS = (1 << 5, 1 << 31, 0x3)


def check_eager_words(vessel, handle: int) -> list[str]:
    """Return one line for each unnamed eager word that crucible_create or crucible_observe_eager_numerics takes."""
    problems = []
    for word in UNNAMED_EAGER_WORDS:
        try:
            refused = vessel.create(word)
        except ValueError:
            pass
        else:
            vessel.destroy(refused)
            problems.append(f"crucible_create takes the unnamed eager word {word:#x}")
        if vessel.observe_eager_numerics(handle, word):
            problems.append(f"crucible_observe_eager_numerics takes the unnamed eager word {word:#x}")
    if not vessel.observe_eager_numerics(handle, 0):
        problems.append("crucible_observe_eager_numerics refuses the eager word 0")
    return problems


def main() -> int:
    """Run each check through the two entries and report."""
    vessel = crucible_mode._VesselLib()
    # The two entries, as the layout table declares them.
    dispatch_op = vessel._lib.crucible_dispatch_op
    dispatch_op_ex = vessel._lib.crucible_dispatch_op_ex
    # The word 0: the highest precision, no TF32 and no flush of denormal floats.
    handle = vessel.create(0)
    try:
        problems = run_entry(
            "crucible_dispatch_op",
            lambda h, inputs, outputs, metas, count: dispatch_op(h, SCHEMA_HASH, inputs, outputs, metas, count),
            handle, DispatchAction.REFUSED)
        problems += run_entry(
            "crucible_dispatch_op_ex",
            lambda h, inputs, outputs, metas, count: dispatch_op_ex(h, SCHEMA_HASH, inputs, outputs, metas, count,
                                                                    None, None, None, 0, 0),
            handle, DispatchAction.RECORD)
        problems += check_scalar_claims(dispatch_op_ex, handle)
        problems += check_eager_words(vessel, handle)
    finally:
        vessel.destroy(handle)
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_dispatch_status: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
