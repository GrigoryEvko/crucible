#!/usr/bin/env python3
"""Record a loop with a uint16 tensor through the C ABI, until the Vigil replays it.

The C ABI gives torch.uint16 the c10 ordinal 27, and no crucible element type
has that ordinal.  The trust ladder refuses each operation with such a tensor,
so the operation runs eagerly and stays out of the ring.  Without the refusal,
the background pipeline gives the ordinal to element_size, which ends the
process.

Usage:
    python test_unmapped_dtype.py

Exit 0 when the loop completes and the Vigil replays it, 1 when the Vigil does
not replay it.  The process ends with an abort when the ladder admits the
ordinal.
"""

import sys

import torch

from crucible_mode import CrucibleMode

ITERATIONS = 40


def main() -> int:
    narrow = torch.arange(8, dtype=torch.int32).to(torch.uint16)
    with CrucibleMode() as mode:
        for _ in range(ITERATIONS):
            widened = narrow.to(torch.int64)
            doubled = widened * 2
            del doubled
        is_compiled = mode.is_compiled()
    print(f"test_unmapped_dtype: {ITERATIONS} iterations, compiled={is_compiled}")
    return 0 if is_compiled else 1


if __name__ == "__main__":
    sys.exit(main())
