#!/usr/bin/env python3
"""The dispatch library gives the modes of c10::CrucibleMode, and it refuses a mode value with no name.

The checks:

    stamp           crucible_dispatch_mode_entry gives the names and the values
                    of TlsMode, in the order of the declarations, and nothing
                    past the last enumerator
    other stamp     check_tls_modes refuses a stamp with another value, with
                    another name and with one more enumerator
    refused value   crucible_dispatch_set_tls_mode gives 0 for each value from
                    the last enumerator plus one to 255, and the mode of the
                    thread stays.  _DispatchLib.set_mode raises ValueError for
                    such a value
    named value     crucible_dispatch_set_tls_mode gives 1 for each enumerator,
                    and the mode of the thread is then that enumerator

Usage:
    python test_tls_mode.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import ctypes
import sys
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import torch  # noqa: E402, F401

from crucible_native import (  # noqa: E402
    CrucibleAbiMismatchError,
    DeclaredEntries,
    TlsMode,
    _DispatchLib,
    check_tls_modes,
    read_tls_modes,
)

# The values of the uint8_t base of c10::CrucibleMode.
VALUE_COUNT = 256


def check_stamp(library: DeclaredEntries) -> list[str]:
    """Give one line for each problem of the stamp of the modes of the library."""
    problems: list[str] = []
    observed = read_tls_modes(library)
    expected = {member.name: int(member.value) for member in TlsMode}
    if observed != expected:
        problems.append(f"the library gives the modes {observed}, and TlsMode holds {expected}")
    if list(observed) != [member.name for member in TlsMode]:
        problems.append(f"the library gives the modes in the order {list(observed)}, not in the order of TlsMode")
    value = ctypes.c_uint8(0)
    name = ctypes.c_char_p(None)
    if library.crucible_dispatch_mode_entry(ctypes.c_uint32(len(TlsMode)), ctypes.byref(value),
                                            ctypes.byref(name)) != 0:
        problems.append("crucible_dispatch_mode_entry gives an enumerator past the last mode of TlsMode")
    return problems


def check_other_stamps() -> list[str]:
    """Give one line for each stamp that check_tls_modes does not refuse."""
    expected = {member.name: int(member.value) for member in TlsMode}
    other_value = dict(expected, RECORD=7)
    other_name = {("REPLAY" if mode == "COMPILED" else mode): value for mode, value in expected.items()}
    one_more = dict(expected, PAUSED=len(expected))
    problems: list[str] = []
    for case, stamp in (("another value", other_value), ("another name", other_name), ("one more mode", one_more)):
        try:
            check_tls_modes(stamp, "test_tls_mode")
        except CrucibleAbiMismatchError:
            continue
        problems.append(f"check_tls_modes takes a stamp with {case}: {stamp}")
    try:
        check_tls_modes(expected, "test_tls_mode")
    except CrucibleAbiMismatchError as error:
        problems.append(f"check_tls_modes refuses the stamp of TlsMode: {error}")
    return problems


def check_values(dispatch: _DispatchLib) -> list[str]:
    """Give one line for each value that the set entry treats wrongly."""
    problems: list[str] = []
    library = dispatch._lib
    dispatch.set_mode(TlsMode.RECORD)
    for raw in range(len(TlsMode), VALUE_COUNT):
        if library.crucible_dispatch_set_tls_mode(ctypes.c_uint8(raw)) != 0:
            problems.append(f"crucible_dispatch_set_tls_mode takes the value {raw}, which names no mode")
        if library.crucible_dispatch_get_tls_mode() != int(TlsMode.RECORD):
            problems.append(f"the value {raw} changes the mode of the thread to "
                            f"{library.crucible_dispatch_get_tls_mode()}")
            dispatch.set_mode(TlsMode.RECORD)
    try:
        dispatch.set_mode(len(TlsMode))
        problems.append(f"_DispatchLib.set_mode takes the value {len(TlsMode)}, which names no mode")
    except ValueError:
        pass
    for member in TlsMode:
        if library.crucible_dispatch_set_tls_mode(ctypes.c_uint8(int(member))) != 1:
            problems.append(f"crucible_dispatch_set_tls_mode refuses the mode {member.name}")
        if dispatch.get_mode() is not member:
            problems.append(f"the mode {member.name} reads back as {dispatch.get_mode()}")
    dispatch.set_mode(TlsMode.INACTIVE)
    return problems


def main() -> int:
    """Run each check and report."""
    dispatch = _DispatchLib()
    problems = check_stamp(dispatch._lib)
    problems += check_other_stamps()
    problems += check_values(dispatch)
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_tls_mode: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
