#!/usr/bin/env python3
"""Each wrapper of a vessel library refuses a library of a different ABI version.

libcrucible_vessel.so reports CRUCIBLE_VESSEL_ABI_VERSION through
crucible_abi_version(), and crucible_native.EXPECTED_ABI_VERSION is the version
that the Python side reads.  A library of a different version has a different
layout of CrucibleMeta or CrucibleDispatchResult, and a read of it gives wrong
results.  libcrucible_dispatch.so reports kDispatchAbiVersion through
crucible_dispatch_abi_version(), and crucible_native.EXPECTED_DISPATCH_ABI_VERSION
is its version.  Each wrapper loads its library through a door of
crucible_native, which checks the stamp first:

    crucible_native._VesselLib    the wrapper of the native controller
    crucible_mode._VesselLib      the wrapper of the dispatch mode
    crucible_native._DispatchLib  the wrapper of the dispatch library

The checks, through each wrapper:

    built       the library of the build has the expected stamp, and it loads
    planted     a library with a different stamp is refused, and the message
                names the two versions
    no stamp    a library with no stamp symbol is refused

A planted library takes the place of ctypes.CDLL for one load.  It holds a
function for each symbol of the C API, and its stamp gives the planted value.

Usage:
    python test_abi_version.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import ctypes
import sys
from collections.abc import Callable
from pathlib import Path
from typing import Any, NamedTuple
from unittest import mock

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import crucible_mode  # noqa: E402
import crucible_native  # noqa: E402


class PlantedFunction:
    """A symbol of a planted library: it holds a restype and argtypes, and a call gives result."""

    def __init__(self, result: int) -> None:
        """Hold the value that each call gives."""
        self.restype: Any = None
        self.argtypes: list[Any] = []
        self._result = result

    def __call__(self, *args: Any) -> int:
        """Give the planted value."""
        return self._result


class PlantedLibrary:
    """A loaded library with a function for each symbol of a C API of the vessel.

    STAMP_SYMBOL is the name of the stamp function of the library.  STAMP is
    its value, or None for a library with no such symbol.  Each other symbol
    of the C API gives zero.
    """

    def __init__(self, stamp_symbol: str, stamp: int | None) -> None:
        """Hold the name and the value of the planted stamp."""
        self._stamp_symbol = stamp_symbol
        self._stamp = stamp

    def __getattr__(self, name: str) -> PlantedFunction:
        """Give the function of a symbol, as ctypes.CDLL does, or raise AttributeError for no symbol."""
        if not name.startswith("crucible_"):
            raise AttributeError(name)
        if name == self._stamp_symbol:
            if self._stamp is None:
                raise AttributeError(name)
            return PlantedFunction(self._stamp)
        return PlantedFunction(0)


class Wrapper(NamedTuple):
    """One wrapper of a vessel library, and the stamp of its library."""

    name: str
    load: Callable[[str], Any]
    stamp: crucible_native.AbiStamp


WRAPPERS: tuple[Wrapper, ...] = (
    Wrapper("crucible_native._VesselLib", crucible_native._VesselLib, crucible_native.VESSEL_STAMP),
    Wrapper("crucible_mode._VesselLib", crucible_mode._VesselLib, crucible_native.VESSEL_STAMP),
    Wrapper("crucible_native._DispatchLib", crucible_native._DispatchLib, crucible_native.DISPATCH_STAMP),
)


def refusal_of(wrapper: Wrapper, path: str, stamp: int | None) -> str | None:
    """Load the planted library of STAMP through WRAPPER, and return the message of the refusal, or None."""
    planted = PlantedLibrary(wrapper.stamp.symbol, stamp)
    with mock.patch.object(crucible_native.ctypes, "CDLL", lambda *args, **kwargs: planted):
        try:
            wrapper.load(path)
        except crucible_native.CrucibleAbiMismatchError as refusal:
            return str(refusal)
    return None


def built_stamp_of(path: str, stamp: crucible_native.AbiStamp) -> int:
    """Return the stamp of the library of the build at PATH, read with no wrapper."""
    built = ctypes.CDLL(path)
    reader = getattr(built, stamp.symbol)
    reader.restype = ctypes.c_uint64
    reader.argtypes = []
    return int(reader())


def check_wrapper(wrapper: Wrapper) -> list[str]:
    """Run the three checks through WRAPPER, and return the text of each failure."""
    problems: list[str] = []
    stamp = wrapper.stamp
    path = crucible_native.checked_lib_path(stamp.library, None, crucible_native.VESSEL_BUILD)
    observed = built_stamp_of(path, stamp)
    if observed != stamp.expected:
        problems.append(f"{stamp.library} of the build reports {observed}, not {stamp.expected}")
    try:
        wrapper.load(path)
    except crucible_native.CrucibleAbiMismatchError as refusal:
        problems.append(f"{wrapper.name} refuses the library of the build: {refusal}")
    # A version that no library of this tree reports.
    planted_version = stamp.expected + 1
    planted = refusal_of(wrapper, path, planted_version)
    if planted is None:
        problems.append(f"{wrapper.name} loads a library with the stamp {planted_version}")
    elif str(planted_version) not in planted or str(stamp.expected) not in planted:
        problems.append(f"{wrapper.name} refuses the stamp {planted_version} with a message that does not "
                        f"name the two versions: {planted}")
    if refusal_of(wrapper, path, None) is None:
        problems.append(f"{wrapper.name} loads a library with no {stamp.symbol} symbol")
    return problems


def main() -> int:
    """Run each check through each wrapper and report."""
    problems = [problem for wrapper in WRAPPERS for problem in check_wrapper(wrapper)]
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_abi_version: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
