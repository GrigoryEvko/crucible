#!/usr/bin/env python3
"""Each wrapper of the vessel library refuses a library of a different ABI version.

libcrucible_vessel.so reports CRUCIBLE_VESSEL_ABI_VERSION through
crucible_abi_version(), and crucible_native.EXPECTED_ABI_VERSION is the version
that the Python side reads.  A library of a different version has a different
layout of CrucibleMeta or CrucibleDispatchResult, and a read of it gives wrong
results.  The two wrappers load the library through
crucible_native.load_vessel_library, which checks the stamp first:

    crucible_native._VesselLib    the wrapper of the native controller
    crucible_mode._VesselLib      the wrapper of the dispatch mode

The checks, through each wrapper:

    built       the library of the build has the expected stamp, and it loads
    planted     a library with a different stamp is refused, and the message
                names the two versions
    no stamp    a library with no crucible_abi_version symbol is refused

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
from typing import Any
from unittest import mock

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import crucible_mode  # noqa: E402
import crucible_native  # noqa: E402

# A version that no library of this tree reports.
PLANTED_VERSION = crucible_native.EXPECTED_ABI_VERSION - 1


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
    """A loaded library with a function for each symbol of the C API of the vessel.

    STAMP is the value of crucible_abi_version, or None for a library with no
    such symbol.
    """

    def __init__(self, stamp: int | None) -> None:
        """Hold the planted stamp."""
        self._stamp = stamp

    def __getattr__(self, name: str) -> PlantedFunction:
        """Give the function of a symbol, as ctypes.CDLL does, or raise AttributeError for no symbol."""
        if not name.startswith("crucible_"):
            raise AttributeError(name)
        if name == "crucible_abi_version":
            if self._stamp is None:
                raise AttributeError(name)
            return PlantedFunction(self._stamp)
        return PlantedFunction(0)


WRAPPERS: tuple[tuple[str, Callable[[str], Any]], ...] = (
    ("crucible_native._VesselLib", crucible_native._VesselLib),
    ("crucible_mode._VesselLib", crucible_mode._VesselLib),
)


def refusal_of(wrapper: Callable[[str], Any], path: str, stamp: int | None) -> str | None:
    """Load the planted library of STAMP through WRAPPER, and return the message of the refusal, or None."""
    with mock.patch.object(crucible_native.ctypes, "CDLL", lambda *args, **kwargs: PlantedLibrary(stamp)):
        try:
            wrapper(path)
        except crucible_native.CrucibleAbiMismatchError as refusal:
            return str(refusal)
    return None


def main() -> int:
    """Run each check through each wrapper and report."""
    problems: list[str] = []
    path = crucible_native.checked_lib_path("libcrucible_vessel.so", None, crucible_native.RELEASE_BUILD)
    built = ctypes.CDLL(path)
    built.crucible_abi_version.restype = ctypes.c_uint64
    built.crucible_abi_version.argtypes = []
    if built.crucible_abi_version() != crucible_native.EXPECTED_ABI_VERSION:
        problems.append(f"the library of the build reports {built.crucible_abi_version()}, "
                        f"not {crucible_native.EXPECTED_ABI_VERSION}")
    for name, wrapper in WRAPPERS:
        try:
            wrapper(path)
        except crucible_native.CrucibleAbiMismatchError as refusal:
            problems.append(f"{name} refuses the library of the build: {refusal}")
        planted = refusal_of(wrapper, path, PLANTED_VERSION)
        if planted is None:
            problems.append(f"{name} loads a library with the stamp {PLANTED_VERSION}")
        elif str(PLANTED_VERSION) not in planted or str(crucible_native.EXPECTED_ABI_VERSION) not in planted:
            problems.append(f"{name} refuses the stamp {PLANTED_VERSION} with a message that does not name "
                            f"the two versions: {planted}")
        if refusal_of(wrapper, path, None) is None:
            problems.append(f"{name} loads a library with no crucible_abi_version symbol")
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_abi_version: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
