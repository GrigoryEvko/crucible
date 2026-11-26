#!/usr/bin/env python3
"""Raise an error from the flush of crucible_native when the background thread stopped.

crucible_flush writes 1 when the background thread processed each recorded
op, and 0 when the thread stopped first.  The flush of the vessel wrapper, the
flush of CrucibleNative and export_trace read that result, and each one raises
the error of crucible_mode on 0.  No C entry stops the background thread, so
the test gives the wrapper a stand-in library whose crucible_flush writes the
result of the case and gives the handle status LIVE.  The stand-in loads no
library.

Usage:
    python test_native_flush.py

Exit 0 when each flush of a stopped background raises and each flush of a
running background does not, 1 otherwise.
"""

import sys
from collections.abc import Callable
from typing import Any, cast

import torch

from crucible_native import CrucibleNative, DeclaredEntries, HandleStatus, _VesselLib

# One intra-op thread and one inter-op thread, as in each vessel test.
torch.set_num_threads(1)
torch.set_num_interop_threads(1)

# The handle of the cases.  The stand-in reads no Vigil behind it.
HANDLE = 1

# The error text of crucible_mode, which crucible_native raises too.
STOPPED_TEXT = "the background thread of the Vigil stopped before the flush completed"


class FlushLibrary:
    """A stand-in for libcrucible_vessel.so whose crucible_flush writes a fixed result."""

    def __init__(self, result: int) -> None:
        """Write RESULT at each call of crucible_flush: 1 for a complete flush, 0 for a stopped background."""
        self.result = result
        self.calls = 0

    def crucible_flush(self, handle: int, out: Any) -> int:
        """Count the call, write the result of the case through OUT, and give the handle status LIVE."""
        self.calls += 1
        out._obj.value = self.result
        return int(HandleStatus.LIVE)


class StandInVessel(_VesselLib):
    """The vessel wrapper over a stand-in library, with no load."""

    def __init__(self, library: FlushLibrary) -> None:
        """Take LIBRARY in place of the loaded library."""
        self._lib = cast(DeclaredEntries, library)


def native_over(library: FlushLibrary) -> CrucibleNative:
    """Return a CrucibleNative whose Vigil is the handle of the cases over LIBRARY."""
    native = CrucibleNative()
    native._vessel = StandInVessel(library)
    native._handle = HANDLE
    return native


def raises_stop(name: str, call: Callable[[], object]) -> bool:
    """Return True when CALL raises the error of a stopped background, and print the result of the case NAME."""
    try:
        call()
    except RuntimeError as error:
        is_stop = str(error) == STOPPED_TEXT
        print(f"test_native_flush: {name} raises: {error}")
        return is_stop
    print(f"test_native_flush: {name} raises no error")
    return False


def main() -> int:
    """Flush a stopped and a running background through each of the three calls."""
    stopped = FlushLibrary(0)
    cases: list[tuple[str, Callable[[], object]]] = [
        ("the flush of the vessel wrapper", lambda: StandInVessel(stopped).flush(HANDLE)),
        ("CrucibleNative.flush", lambda: native_over(stopped).flush()),
        ("CrucibleNative.export_trace", lambda: native_over(stopped).export_trace("unused.crtrace")),
    ]
    results = [raises_stop(name, call) for name, call in cases]
    is_correct = all(results)
    if stopped.calls != 3:
        print(f"test_native_flush: the stopped library saw {stopped.calls} flushes, and the cases give 3")
        is_correct = False

    running = FlushLibrary(1)
    StandInVessel(running).flush(HANDLE)
    native_over(running).flush()
    if running.calls != 2:
        print(f"test_native_flush: the running library saw {running.calls} flushes, and the cases give 2")
        is_correct = False
    print(f"test_native_flush: {'PASS' if is_correct else 'FAIL'}")
    return 0 if is_correct else 1


if __name__ == "__main__":
    sys.exit(main())
