#!/usr/bin/env python3
"""The generated ATen operator table gives the one-argument aten::where no elementwise class.

aten::where(Tensor condition) -> Tensor[] is nonzero: it gives the index
tensors of the true elements, and their length depends on the data.  It is no
elementwise select, so its CKernelId must not be EWISE_WHERE.  The overloads
with three arguments are elementwise selects.

The test reads aten_op_table.h, the table that the recording kernels compile.

Usage:
    python test_op_table_classes.py

Exit 0 when each row has its expected class, 1 otherwise.
"""

import re
import sys
from pathlib import Path

TABLE = Path(__file__).resolve().parent / "aten_op_table.h"

ROW = re.compile(
    r'OpEntry\{"(?P<name>[^"]+)", "(?P<overload>[^"]*)", 0x[0-9a-f]+ULL, (?P<arity>\d+), '
    r'0x[0-9a-f]+, 0x[0-9a-f]+, CKernelId::(?P<kernel>\w+), (?:true|false)\}')

# The expected arity and class of each row that the test reads.
EXPECTED = {
    ("aten::where", ""): (1, "OPAQUE"),
    ("aten::nonzero", ""): (1, "OPAQUE"),
    ("aten::where", "self"): (3, "EWISE_WHERE"),
    ("aten::where", "ScalarOther"): (3, "EWISE_WHERE"),
}


def main() -> int:
    """Read the table and compare each expected row."""
    rows = {}
    for match in ROW.finditer(TABLE.read_text(encoding="utf-8")):
        rows[(match["name"], match["overload"])] = (int(match["arity"]), match["kernel"])
    problems = []
    for key, expected in EXPECTED.items():
        found = rows.get(key)
        if found != expected:
            problems.append(f"{key[0]}.{key[1] or 'default'}: expected {expected}, found {found}")
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print(f"test_op_table_classes: {len(EXPECTED)} rows have their expected class")
    return 0


if __name__ == "__main__":
    sys.exit(main())
