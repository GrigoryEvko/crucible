#!/usr/bin/env python3
"""The enumeration maps of crucible_mode.py give no crucible enumerator for a value they do not name.

The C ABI takes a crucible::TensorMeta, so each element type, device and
layout of a tensor becomes the byte of a crucible enumerator.  The native
vessel refuses an operation with a value that crucible does not name.  The
maps of crucible_mode.py must give such a value a byte that names no
enumerator, so that the trust ladder of the C ABI refuses the operation too.
A map that falls back to CPU, to the strided layout or to the undefined
element type records the tensor as something it is not.

Usage:
    python test_enumeration_maps.py

Exit 0 when each map agrees with the crucible enumerations, 1 otherwise.
"""

import sys
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402

import crucible_mode  # noqa: E402

# The largest ordinal of crucible::ScalarType, crucible::DeviceType and
# crucible::Layout in include/crucible/Types.h.  The unnamed byte must be
# larger than each.
LARGEST_NAMED_ORDINAL = 45


def check_unnamed_values() -> list[str]:
    """Return one line for each value that does not get the unnamed byte."""
    problems: list[str] = []
    unnamed = crucible_mode._UNNAMED_ORDINAL
    if unnamed <= LARGEST_NAMED_ORDINAL:
        problems.append(f"the unnamed byte {unnamed} names a crucible enumerator")
    if crucible_mode._device_ordinal("xpu") != unnamed:
        problems.append("the device xpu, which crucible does not name, gets a crucible device")
    if crucible_mode._layout_ordinal(torch.jagged) != unnamed:
        problems.append("the jagged layout, which crucible does not name, gets a crucible layout")
    if crucible_mode._layout_ordinal(torch._mkldnn) != unnamed:
        problems.append("the mkldnn layout, which crucible does not name, gets a crucible layout")
    if crucible_mode._dtype_ordinal(torch.qint8) != unnamed:
        problems.append("the element type qint8, which crucible does not name, gets a crucible element type")
    return problems


def check_named_values() -> list[str]:
    """Return one line for each named value that does not get its crucible ordinal."""
    expected_dtypes = {
        torch.float32: 6,
        torch.bfloat16: 15,
        torch.float8_e8m0fnu: 44,
        torch.float4_e2m1fn_x2: 45,
    }
    problems: list[str] = []
    for dtype, ordinal in expected_dtypes.items():
        if crucible_mode._dtype_ordinal(dtype) != ordinal:
            problems.append(f"the element type {dtype} does not get the crucible ordinal {ordinal}")
    if crucible_mode._device_ordinal("cpu") != 0 or crucible_mode._device_ordinal("cuda") != 1:
        problems.append("the devices cpu and cuda do not get their crucible ordinals")
    if crucible_mode._layout_ordinal(torch.sparse_csc) != 3:
        problems.append("the layout sparse_csc does not get the crucible ordinal 3")
    return problems


def check_fill_meta() -> list[str]:
    """Return one line when _fill_meta records a quantized tensor with a crucible element type."""
    source = torch.arange(1, 9, dtype=torch.float32)
    quantized = torch.quantize_per_tensor(source, 0.1, 0, torch.qint8)
    meta = crucible_mode._VesselLib.Meta()
    crucible_mode.CrucibleMode._fill_meta(meta, quantized)
    if meta.dtype != crucible_mode._UNNAMED_ORDINAL:
        return [f"_fill_meta records a qint8 tensor with the element type byte {meta.dtype}"]
    return []


def main() -> int:
    """Check each map and report."""
    problems = check_unnamed_values() + check_named_values() + check_fill_meta()
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_enumeration_maps: each map agrees with the crucible enumerations")
    return 0


if __name__ == "__main__":
    sys.exit(main())
