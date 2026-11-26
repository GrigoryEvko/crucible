#!/usr/bin/env python3
"""The entries of the dispatch library that read a PyTorch object by address read it only under the GIL.

crucible_dispatch_storage_generation reads the c10::StorageImpl at the address
that Python gives.  The entry reads the object only while the calling thread
holds the GIL (gil_held.h), and the wrapper binds it through a PyDLL object,
which keeps the GIL for the call (GIL_ENTRIES of crucible_native.py).  The
checks:

    resolves    a read through the wrapper of a live storage gives a word that
                is not 0.  The library declares PyGILState_Check as a weak
                symbol, and a symbol that does not resolve makes each read give
                0.  So this check fails if the symbol does not resolve in the
                process of the test
    no GIL      a call of the same entry through the CDLL object, which
                releases the GIL, gives 0 and reads nothing
    table       load_gil_entries refuses a name of GIL_ENTRIES that the
                library does not export
    race        one thread reads the generation of a tensor while a second
                thread gives the tensor a new storage in a loop.  The GIL
                keeps the second thread out of each read, so each read gives
                the word of a live storage

Usage:
    python test_gil_entries.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import ctypes
import sys
import threading
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402

import crucible_native  # noqa: E402
from crucible_native import CrucibleAbiMismatchError, _DispatchLib, is_gil_enabled, load_gil_entries  # noqa: E402

torch.set_num_threads(1)
torch.set_num_interop_threads(1)

# The reads of the race, and the elements of each new storage of the second
# thread.
RACE_READS = 20_000
RACE_ELEMENTS = 256
# A name that no library exports.
MISSING_ENTRY = "crucible_dispatch_entry_that_no_library_exports"


def read_without_gil(lib: _DispatchLib, tensor: torch.Tensor) -> int:
    """Call the generation entry through the CDLL object of the wrapper, which releases the GIL."""
    entry = lib._lib.crucible_dispatch_storage_generation
    entry.restype = ctypes.c_uint32
    entry.argtypes = [ctypes.c_void_p]
    return entry(ctypes.c_void_p(tensor.untyped_storage()._cdata))


def check_reads(lib: _DispatchLib) -> list[str]:
    """Run the checks resolves and no GIL on one live storage."""
    problems = []
    tensor = torch.empty(1000)
    word = lib.storage_generation(tensor)
    if word == 0:
        problems.append("a read with the GIL gave 0: PyGILState_Check does not resolve in the library, or the "
                        "wrapper does not keep the GIL for the call")
    released = read_without_gil(lib, tensor)
    if released != 0:
        problems.append(f"a read with no GIL gave the word {released:#x}, not 0")
    return problems


def check_table(lib: _DispatchLib) -> list[str]:
    """Run the check table: a name of GIL_ENTRIES that the library does not export."""
    kept = crucible_native.GIL_ENTRIES
    crucible_native.GIL_ENTRIES = (*kept, MISSING_ENTRY)
    try:
        load_gil_entries(lib._lib)
    except CrucibleAbiMismatchError as error:
        if MISSING_ENTRY not in str(error):
            return [f"the refusal of a missing entry does not name it: {error}"]
        return []
    finally:
        crucible_native.GIL_ENTRIES = kept
    return ["load_gil_entries accepted a name that the library does not export"]


def check_race(lib: _DispatchLib) -> list[str]:
    """Run the check race: reads of the generation against a loop that gives the tensor new storages."""
    tensor = torch.empty(RACE_ELEMENTS)
    is_stopped = threading.Event()

    def give_new_storages() -> None:
        """Give the tensor a new storage until the reads stop."""
        while not is_stopped.is_set():
            tensor.set_(torch.empty(RACE_ELEMENTS).untyped_storage())

    writer = threading.Thread(target=give_new_storages)
    writer.start()
    zero_reads = 0
    try:
        for _ in range(RACE_READS):
            if lib.storage_generation(tensor) == 0:
                zero_reads += 1
    finally:
        is_stopped.set()
        writer.join()
    if zero_reads:
        return [f"{zero_reads} of {RACE_READS} reads during the race gave 0"]
    return []


def main() -> int:
    """Run each check and report."""
    if not is_gil_enabled():
        print("test_gil_entries: the interpreter runs with no GIL, so the dispatch library reads no object")
        return 0
    lib = _DispatchLib()
    problems = check_reads(lib) + check_table(lib) + check_race(lib)
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_gil_entries: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
