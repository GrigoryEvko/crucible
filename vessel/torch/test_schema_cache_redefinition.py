#!/usr/bin/env python3
"""A new schema at the address of an old one gets its own facts from the schema cache of the boxed fallback.

The boxed fallback keeps the schema hash, the mutability and the foreach bit
of each operator in a process-wide cache.  The key is the address of the name
of the operator in its entry of the dispatcher.  The entry can outlive a
schema: an implementation keeps the entry when the last definition goes, and a
later definition gives the same entry a different schema.  A cache that keeps
the facts of the old schema then gives the new schema the wrong mutability.

No test can make the dispatcher put a new entry at the address of a freed
entry.  So the test keeps one entry alive with an implementation of the key
Meta, and the address of the name stays the same by construction:

    1. crucible_redefine::step(Tensor source) -> Tensor, which is not mutable
    2. one call under CrucibleNative puts its facts in the cache
    3. the definition goes, and the Meta implementation keeps the entry
    4. crucible_redefine::step(Tensor(a!) source) -> Tensor(a!), which writes
       its argument
    5. a loop of the new schema records until the Vigil activates a region

Each operation of the region with the schema hash of crucible_redefine::step
must carry the flag IS_MUTABLE of the new schema.

Usage:
    python test_schema_cache_redefinition.py

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
from crucible_native import CrucibleNative  # noqa: E402

# The import sets one intra-op thread and one inter-op thread, so the
# instruction count of the test is the same in each run.  PyTorch refuses a
# second set of the inter-op threads, so this file does not set them again.
from test_record_coverage import MAX_ITERATIONS, read_trace, schema_hash  # noqa: E402

NAMESPACE = "crucible_redefine"
OPERATOR = f"{NAMESPACE}::step"

# The bit of the mutability of the op flags (bit 1 of OpFlags of
# crucible/TraceRing.h), which the trace export writes in the last byte of
# each operation record.
IS_MUTABLE = 1 << 1


def copy_source(source: torch.Tensor) -> torch.Tensor:
    """Return a copy of the source, for the schema that writes nothing."""
    return source.clone()


def add_one_in_place(source: torch.Tensor) -> torch.Tensor:
    """Add one to the source and return it, for the schema that writes its argument."""
    return source.add_(1)


def empty_like_source(source: torch.Tensor) -> torch.Tensor:
    """Return a tensor of the shape of the source, for the Meta key."""
    return torch.empty_like(source)


def operation_flags(path: Path) -> list[tuple[int, int]]:
    """Return the schema hash and the op flags of each operation of a .crtrace file."""
    return [(op.schema_hash, op.flags) for op in read_trace(path)]


def record(directory: Path) -> list[tuple[int, int]]:
    """Run the five steps of the docstring, and return the schema hash and the op flags of each region operation."""
    path = directory / "redefinition.crtrace"
    keeper = torch.library.Library(NAMESPACE, "FRAGMENT")
    first = torch.library.Library(NAMESPACE, "FRAGMENT")
    first.define("step(Tensor source) -> Tensor")
    first.impl("step", copy_source, "CPU")
    keeper.impl("step", empty_like_source, "Meta")
    buffer = torch.zeros(6)
    with CrucibleNative() as ctx:
        torch.ops.crucible_redefine.step(buffer)
        first._destroy()
        second = torch.library.Library(NAMESPACE, "FRAGMENT")
        second.define("step(Tensor(a!) source) -> Tensor(a!)")
        second.impl("step", add_one_in_place, "CPU")
        for _ in range(MAX_ITERATIONS):
            torch.ops.crucible_redefine.step(buffer)
            scaled = buffer * 2
            del scaled
            if ctx.active_num_ops() > 0:
                break
            ctx.flush()
        if not ctx.export_trace(str(path)):
            raise RuntimeError(f"no region after {MAX_ITERATIONS} iterations")
    second._destroy()
    keeper._destroy()
    return operation_flags(path)


def main() -> int:
    """Run the check and report."""
    with tempfile.TemporaryDirectory(prefix="crucible-schema-cache-redefinition-") as scratch:
        operations = record(Path(scratch))
    wanted = schema_hash(OPERATOR)
    steps = [flags for hash_value, flags in operations if hash_value == wanted]
    problems = []
    if not steps:
        problems.append(f"the region holds no {OPERATOR}")
    stale = sum(1 for flags in steps if not flags & IS_MUTABLE)
    if stale:
        problems.append(f"{stale} of {len(steps)} operations of {OPERATOR} have no flag IS_MUTABLE, so the cache "
                        f"gave them the facts of the old schema")
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print(f"test_schema_cache_redefinition: {len(steps)} operation(s) of {OPERATOR}, each with the flag IS_MUTABLE")
    return 0


if __name__ == "__main__":
    sys.exit(main())
