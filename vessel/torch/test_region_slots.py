#!/usr/bin/env python3
"""The memory plan of the active region, read through crucible_active_region_slots.

The checks:

    mlp             the region of the training loop of test_region_ops.py
                    plans the result of the first addmm of the forward pass as
                    a slot of the region, with bytes of the pool
    reused address  a loop frees a storage that a recorded op made, makes a new
                    storage at its address with no record, and reads the new
                    storage in a recorded op.  The new storage has another
                    generation, so the plan gives it an external slot of its
                    own, and the read does not join the slot of the dead
                    storage

Usage:
    python test_region_slots.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import sys
import tempfile
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402
import torch.nn as nn  # noqa: E402

from crucible_native import CrucibleNative, RegionSlot, _DispatchLib, attach  # noqa: E402
from test_region_ops import (  # noqa: E402
    BATCH,
    HIDDEN,
    IN_FEATURES,
    LEARNING_RATE,
    MAX_ITERATIONS,
    OUT_FEATURES,
    SEED,
    RegionOp,
    read_region,
)

# The bytes of one float32 element.
FLOAT_BYTES = 4
# The depth of the addmm of a linear layer in the region of the training loop.
ADDMM_DEPTH = 1
# The float elements of the storages of the loop of a reused address: 36 MiB,
# above 32 MiB, the largest mmap threshold of glibc.  So malloc maps each
# storage on its own, and the kernel gives the range of a freed storage to the
# next map of the same size.  A chunk of the heap does not serve here, because
# the aligned allocation of PyTorch asks for more bytes than a freed chunk of
# the same storage holds.
REUSED_ELEMENTS = 9 << 20
# The most iterations of the loop of a reused address before its region is
# active.
MAX_REUSE_ITERATIONS = 200


def record_mlp(directory: Path) -> tuple[list[RegionOp], list[RegionSlot], int]:
    """Record the training loop of test_region_ops.py, and return its operations, its slots and its pool bytes."""
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
        slots = ctx.active_region_slots()
        pool_bytes = ctx.active_pool_bytes()
    return read_region(path), slots, pool_bytes


def nest_start(ops: list[RegionOp], index: int) -> int:
    """Return the index of the first op of the nest of the op at index: the ops of a larger depth just before it."""
    start = index
    while start > 0 and ops[start - 1].depth > ops[index].depth:
        start -= 1
    return start


def check_mlp() -> list[str]:
    """Check that the result of the first addmm of the forward pass is a slot of the region with pool bytes."""
    with tempfile.TemporaryDirectory(prefix="crucible-region-slots-") as scratch:
        ops, slots, pool_bytes = record_mlp(Path(scratch))
    addmm = next((index for index, op in enumerate(ops)
                  if op.phase == "F" and op.name == "aten::addmm" and op.depth == ADDMM_DEPTH
                  and op.inputs[:1] == (f"f32[{HIDDEN}]",)), None)
    if addmm is None:
        return ["the forward pass holds no aten::addmm of the first layer"]
    if not slots:
        return ["the active region of the training loop has no slot"]
    # The nest of addmm writes its result first: the copy_ of the bias into
    # the new result.  So the slot of the result is born in the nest.  The
    # build gives an external slot the birth op zero, so an external result
    # has no slot that is born in the nest.
    first = nest_start(ops, addmm)
    result_bytes = BATCH * HIDDEN * FLOAT_BYTES
    made = [slot for slot in slots if first <= slot.birth_op <= addmm and slot.nbytes == result_bytes]
    if len(made) != 1:
        return [f"the nest of addmm at ops {first} to {addmm} makes {len(made)} slots of {result_bytes} bytes, not "
                f"one, so the result of addmm is not one slot of the region"]
    result = made[0]
    problems = []
    if result.is_external:
        problems.append(f"the result of addmm is the external slot {result.slot_id}, not a slot of the region")
    if result.offset_bytes + result.nbytes > pool_bytes:
        problems.append(f"the result of addmm takes the bytes {result.offset_bytes} to "
                        f"{result.offset_bytes + result.nbytes}, past the pool of {pool_bytes} bytes")
    if result.death_op <= addmm:
        problems.append(f"the result of addmm dies at op {result.death_op}, before the op after addmm reads it")
    return problems


def check_reused_address() -> list[str]:
    """Check that a storage at the address of a dead storage, with no record of its allocation, is a new input."""
    lib = _DispatchLib()
    problems = []
    is_each_address_reused = True
    with CrucibleNative() as ctx:
        for _ in range(MAX_REUSE_ITERATIONS):
            dead = torch.full((REUSED_ELEMENTS,), 1.0)
            # The sum is one element, so no storage of the size of the dead
            # one is freed between the free and the new storage.
            kept = torch.sum(dead)
            address = dead.data_ptr()
            del dead
            # The TLS mode INACTIVE keeps the op out of the record, so the
            # recorder sees no allocation at the address.
            lib.set_mode(CrucibleNative.INACTIVE)
            fresh = torch.full((REUSED_ELEMENTS,), 3.0)
            lib.set_mode(CrucibleNative.RECORD)
            is_each_address_reused = is_each_address_reused and fresh.data_ptr() == address
            torch.add(fresh, kept)
            if ctx.active_num_ops() > 0:
                break
            ctx.flush()
        if ctx.active_num_ops() == 0:
            return [f"the loop of a reused address has no region after {MAX_REUSE_ITERATIONS} iterations"]
        slots = ctx.active_region_slots()
    if not is_each_address_reused:
        problems.append("malloc gave the new storage another address in some iteration, so the plan proves nothing")
    reused_bytes = REUSED_ELEMENTS * FLOAT_BYTES
    external = [slot for slot in slots if slot.is_external and slot.nbytes == reused_bytes]
    if len(external) != 1:
        problems.append(f"the region has {len(external)} external slots of {reused_bytes} bytes, not one: the read "
                        f"of the new storage at the address of the dead storage reads the slot of the dead storage")
    return problems


def main() -> int:
    """Run each check and report."""
    problems = check_mlp() + check_reused_address()
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_region_slots: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
