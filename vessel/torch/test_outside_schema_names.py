#!/usr/bin/env python3
"""A schema name from outside the process gets a refusal at its door, and the process lives.

A library or a Python module can define an operator with a name of any length
at run time, so the name of an operator of PyTorch is outside data.  The boxed
fallback gives the full name "namespace::name.overload" to the door of an
outside name one time.  A refused name makes the operator run eagerly, and the
fallback records no operation of it.  The C entry crucible_register_schema_name
is the other door of an outside name, and its caller gives the hash.  The
checks:

    long name       an operator whose full name has 5000 characters is in the
                    region, and the name table of the trace holds its name
    at the limit    the same for a full name of 65535 characters
                    (SCHEMA_NAME_MAX of crucible/SchemaTable.h, the most that
                    the 16-bit length of a name of a trace holds)
    past the limit  an operator whose full name has 65536 characters gives its
                    value two times, and the trace holds no operation and no
                    name of it
    long overload   the same for a name of 65536 characters whose overload
                    holds most of the characters
    reserved hash   the C entry gives the collision code for a different name
                    with the hash of aten::mm, an operator of the generated
                    table.  Then the unboxed kernel of aten::mm registers its
                    own name, the region holds its operations, and the name
                    table of the trace holds its name
    collision       the C entry registers a different name with the hash of
                    the full name of a fourth operator.  The fallback then
                    finds that name on the hash of the operator, and the
                    operator gives its value two times, and the trace holds
                    no operation and no name of it

The refused operators run before the loop of the region.  The operation
inside a refused operator gives the closing entry of an unrecorded nest
(Vigil::give_unrecorded_nest), and the build refuses each region that holds
one, so a loop with a refused operator gets no region.

Usage:
    python test_outside_schema_names.py

Exit 0 when each check passes, 1 otherwise.  The process ends with an abort
when a door ends the process on a long name or on a collision, or when the
unboxed kernel of aten::mm finds a different name on its hash.
"""

from __future__ import annotations

import ctypes
import sys
import tempfile
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402
from crucible_native import CrucibleNative, SchemaNameResult  # noqa: E402

# The import sets one intra-op thread and one inter-op thread, so the
# instruction count of the test is the same in each run.  PyTorch refuses a
# second set of the inter-op threads, so this file does not set them again.
from test_record_coverage import MAX_ITERATIONS, read_schema_names, read_trace, schema_hash  # noqa: E402

NAMESPACE = "crucible_outside_name"
PREFIX = f"{NAMESPACE}::"
# The most characters of a schema name (SCHEMA_NAME_MAX of crucible/SchemaTable.h).
SCHEMA_NAME_MAX = 65535

# The names of the five operators.  The full name of an operator with no
# overload is the namespace, two colons and the name.
LONG_NAME = "e" * (5000 - len(PREFIX))
AT_LIMIT = "a" * (SCHEMA_NAME_MAX - len(PREFIX))
PAST_LIMIT = "b" * (SCHEMA_NAME_MAX + 1 - len(PREFIX))
OVERLOADED = "c"
LONG_OVERLOAD = "o" * (SCHEMA_NAME_MAX + 1 - len(PREFIX) - len(OVERLOADED) - len("."))
COLLIDED = "d"

FULL_LONG_NAME = f"{PREFIX}{LONG_NAME}"
FULL_AT_LIMIT = f"{PREFIX}{AT_LIMIT}"
FULL_PAST_LIMIT = f"{PREFIX}{PAST_LIMIT}"
FULL_LONG_OVERLOAD = f"{PREFIX}{OVERLOADED}.{LONG_OVERLOAD}"
FULL_COLLIDED = f"{PREFIX}{COLLIDED}"

# An operator of the generated table (vessel/torch/aten_op_table.h) with an
# unboxed kernel, and a different name for its hash.
RESERVED_OPERATOR = "aten::mm"
OTHER_NAME = f"{NAMESPACE}::mm"
# A different name for the hash of the full name of the fourth operator.
OTHER_COLLIDED_NAME = f"{NAMESPACE}::not_d"

# The names that the C entry registers before the operators run.  They are
# more than the 512 places of the global table, so the name of each operator
# is a later name of the late table.
FILLER_NAMES = [f"{NAMESPACE}::filler_{index}" for index in range(600)]


def add_one(source: torch.Tensor) -> torch.Tensor:
    """Return the source plus one, the value of each of the five operators."""
    return source + 1


def define_operators() -> torch.library.Library:
    """Define the five operators with a CPU implementation, and return their library."""
    library = torch.library.Library(NAMESPACE, "FRAGMENT")
    for name in (LONG_NAME, AT_LIMIT, PAST_LIMIT, f"{OVERLOADED}.{LONG_OVERLOAD}", COLLIDED):
        library.define(f"{name}(Tensor source) -> Tensor")
        library.impl(name, add_one, "CPU")
    return library


def register_name(ctx: CrucibleNative, hash_of: str, name: str) -> int:
    """Give the C entry the hash of the name HASH_OF with the name NAME, and return its code."""
    entry = ctx._vessel._lib.crucible_register_schema_name
    entry.restype = ctypes.c_int
    entry.argtypes = [ctypes.c_void_p, ctypes.c_uint64, ctypes.c_char_p]
    return entry(ctx._handle, schema_hash(hash_of), name.encode("ascii"))


def record(directory: Path) -> tuple[Path, list[str]]:
    """Run the refused operators, then a loop until the Vigil activates a region, and export the region.

    Returns the path of the trace and the problems of the code of the C entry and of the values of the operators.
    """
    path = directory / "outside_schema_names.crtrace"
    namespace = getattr(torch.ops, NAMESPACE)
    long_name = getattr(namespace, LONG_NAME).default
    at_limit = getattr(namespace, AT_LIMIT).default
    refused = (("past the limit", getattr(namespace, PAST_LIMIT).default),
               ("long overload", getattr(getattr(namespace, OVERLOADED), LONG_OVERLOAD)),
               ("collision", getattr(namespace, COLLIDED).default))
    source = torch.arange(6, dtype=torch.float32)
    wanted = source + 1
    left = torch.arange(6, dtype=torch.float32).reshape(2, 3)
    right = torch.ones(3, 2)
    product = torch.full((2, 2), 3.0) + torch.tensor([[0.0], [9.0]])
    problems = []
    with CrucibleNative() as ctx:
        code = register_name(ctx, RESERVED_OPERATOR, OTHER_NAME)
        if code != SchemaNameResult.COLLISION:
            problems.append(f"the C entry gives the code {code} for a different name with the hash of "
                            f"{RESERVED_OPERATOR}, not the collision code {int(SchemaNameResult.COLLISION)}")
        code = register_name(ctx, FULL_COLLIDED, OTHER_COLLIDED_NAME)
        if code != SchemaNameResult.REGISTERED:
            problems.append(f"the C entry gives the code {code} for a new hash, not the code "
                            f"{int(SchemaNameResult.REGISTERED)}")
        for filler in FILLER_NAMES:
            code = register_name(ctx, filler, filler)
            if code != SchemaNameResult.REGISTERED:
                problems.append(f"the C entry gives the code {code} for the name {filler}, not the code "
                                f"{int(SchemaNameResult.REGISTERED)}")
                break
        # The second call of each refused operator finds the refusal in the
        # schema cache of the fallback.
        for _ in range(2):
            for label, operator in refused:
                value = operator(source)
                if not torch.equal(value, wanted):
                    problems.append(f"the operator {label} gives {value.tolist()}, not {wanted.tolist()}")
        for _ in range(MAX_ITERATIONS):
            for label, operator in (("long name", long_name), ("at the limit", at_limit)):
                value = operator(source)
                if not torch.equal(value, wanted):
                    problems.append(f"the operator {label} gives {value.tolist()}, not {wanted.tolist()}")
            value = torch.mm(left, right)
            if not torch.equal(value, product):
                problems.append(f"{RESERVED_OPERATOR} gives {value.tolist()}, not {product.tolist()}")
            if ctx.active_num_ops() > 0:
                break
            ctx.flush()
        if not ctx.export_trace(str(path)):
            raise RuntimeError(f"no region after {MAX_ITERATIONS} iterations")
    return path, problems


def main() -> int:
    """Run the checks and report."""
    library = define_operators()
    with tempfile.TemporaryDirectory(prefix="crucible-outside-schema-names-") as scratch:
        path, problems = record(Path(scratch))
        hashes = [op.schema_hash for op in read_trace(path)]
        names = read_schema_names(path)
    library._destroy()
    for label, name in (("long name", FULL_LONG_NAME), ("at the limit", FULL_AT_LIMIT),
                        ("reserved", RESERVED_OPERATOR)):
        wanted = schema_hash(name)
        if wanted not in hashes:
            problems.append(f"the region holds no operation of the name {label} ({len(name)} characters)")
        if names.get(wanted) != name:
            problems.append(f"the name table holds no name {label} ({len(name)} characters) for its hash")
    if names.get(schema_hash(FILLER_NAMES[-1])) != FILLER_NAMES[-1]:
        problems.append(f"the name table holds no name {FILLER_NAMES[-1]} for its hash")
    for label, name in (("past the limit", FULL_PAST_LIMIT), ("long overload", FULL_LONG_OVERLOAD),
                        ("collision", FULL_COLLIDED)):
        refused = schema_hash(name)
        count = hashes.count(refused)
        if count:
            problems.append(f"the region holds {count} operation(s) of the name {label} ({len(name)} characters)")
        if names.get(refused) == name:
            problems.append(f"the name table holds the name {label} ({len(name)} characters)")
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print(f"test_outside_schema_names: {len(hashes)} operation(s) in the region, the names of {SCHEMA_NAME_MAX + 1} "
          f"characters, the reserved hash and the collision refused")
    return 0


if __name__ == "__main__":
    sys.exit(main())
