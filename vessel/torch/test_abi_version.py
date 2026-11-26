#!/usr/bin/env python3
"""The loader refuses a vessel library whose layout is not the layout of its build.

Each vessel library carries a layout note with the layout digest of the types
and the C entries that the two libraries share, and the build writes the
layout table beside them (the layout of a library of crucible_native.py).  The
loader reads the table and the note from the file before dlopen, and compares
its ctypes Structures with the table.

The checks run in child processes, so each child loads the dispatch library
at most once:

    built      the note of each library of the build is the digest of the
               table, and each wrapper loads its library
    planted    a copy of the dispatch library with a changed note is refused
               before dlopen: the process maps no byte of it, and the
               dispatcher of PyTorch holds no kernel of the key Crucible.  The
               library of the build then loads in the same process, and its
               kernel is there
    no note    a copy of the vessel library whose note has another type is
               refused, and the message names the library
    mirror     a ctypes Structure with a field of another type is refused, and
               the message names the Structure.  An enum whose values differ
               from the C constants is refused, and the message names the enum
    field type a ctypes Structure with a field of another sign, with the same
               size and offset, is refused, and the message names the Structure
    pair       after the load of the vessel library, a copy of the dispatch
               library of another digest, whose layout table agrees with its
               note, is refused before dlopen: the message names the vessel
               library, the process maps no byte of the copy, and the
               dispatcher of PyTorch holds no kernel of the key Crucible
    undeclared the wrappers give no entry that the layout table does not
               declare: a read of such a name raises AttributeError
    declared   each entry line of the layout table has its declaration in a
               wrapper, with the restype and the argtypes of its type words

Usage:
    python test_abi_version.py

Exit 0 when each check passes, 1 otherwise.
"""

from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path

# A caller starts this file by its path, so the vessel directory is not always
# on the import path.
VESSEL_DIRECTORY = Path(__file__).resolve().parent

PRELUDE = """
import ctypes
import shutil
import sys
import tempfile
from pathlib import Path
sys.path.insert(0, sys.argv[1])
import torch
import crucible_native
from crucible_native import CrucibleAbiMismatchError

def built_path(name):
    return crucible_native.checked_lib_path(name, None, crucible_native.VESSEL_BUILD)

def planted_copy(name, folder, change):
    source = Path(built_path(name))
    copy = Path(folder) / name
    shutil.copyfile(source, copy)
    # The loader reads the layout table and the level probe beside the library.
    for beside in (crucible_native.LAYOUT_TABLE, crucible_native.PROBE_STAMP.library):
        shutil.copyfile(source.parent / beside, Path(folder) / beside)
    image = bytearray(copy.read_bytes())
    change(image, crucible_native.layout_note(str(copy)))
    copy.write_bytes(bytes(image))
    return str(copy)

def has_crucible_kernel():
    return torch._C._dispatch_has_kernel_for_dispatch_key("aten::add.Tensor", "Crucible")
"""

BUILT = PRELUDE + """
import crucible_mode
for name in (crucible_native.VESSEL_LIBRARY, crucible_native.DISPATCH_LIBRARY):
    path = built_path(name)
    table = crucible_native.read_layout_table(Path(path).parent)
    note = crucible_native.layout_note(path)
    print(f"{name} note matches: {note is not None and note.digest == table.digest}", flush=True)
crucible_native._VesselLib()
crucible_mode._VesselLib()
crucible_native._DispatchLib()
print("each wrapper loads", flush=True)
"""

PLANTED = PRELUDE + """
def flip(image, note):
    image[note.offset] ^= 0xFF
with tempfile.TemporaryDirectory() as folder:
    copy = planted_copy(crucible_native.DISPATCH_LIBRARY, folder, flip)
    try:
        crucible_native._DispatchLib(copy)
        print("the planted library loads", flush=True)
    except CrucibleAbiMismatchError as refusal:
        print(f"refused, naming the library: {crucible_native.DISPATCH_LIBRARY in str(refusal)}", flush=True)
    mapped = copy in Path("/proc/self/maps").read_text()
    print(f"mapped after the refusal: {mapped}", flush=True)
    print(f"kernel after the refusal: {has_crucible_kernel()}", flush=True)
crucible_native._DispatchLib()
print(f"kernel after the built load: {has_crucible_kernel()}", flush=True)
"""

NO_NOTE = PRELUDE + """
def retype(image, note):
    # The type is the third word of the header, and the name of three words
    # comes between it and the descriptor.
    image[note.offset - 16] = 9
with tempfile.TemporaryDirectory() as folder:
    copy = planted_copy(crucible_native.VESSEL_LIBRARY, folder, retype)
    print(f"no note read: {crucible_native.layout_note(copy) is None}", flush=True)
    try:
        crucible_native._VesselLib(copy)
        print("the library with no note loads", flush=True)
    except CrucibleAbiMismatchError as refusal:
        print(f"refused, naming the library: {crucible_native.VESSEL_LIBRARY in str(refusal)}", flush=True)
"""

MIRROR = PRELUDE + """
class StaleSlot(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint64 if name == "slot_id" else kind)
                for name, kind in crucible_native.CrucibleSlot._fields_]
built_slot = crucible_native.MIRRORS["CrucibleSlot"]
crucible_native.MIRRORS["CrucibleSlot"] = StaleSlot
try:
    crucible_native._VesselLib()
    print("the stale mirror loads", flush=True)
except CrucibleAbiMismatchError as refusal:
    print(f"refused, naming the Structure: {'StaleSlot' in str(refusal)}", flush=True)
crucible_native.MIRRORS["CrucibleSlot"] = built_slot

import enum
class StaleAction(enum.IntEnum):
    REFUSED = 0
    RECORD = 2
    COMPILED = 1
crucible_native.MIRRORED_CONSTANTS["CRUCIBLE_ACTION_"] = StaleAction
try:
    crucible_native._VesselLib()
    print("the stale enum loads", flush=True)
except CrucibleAbiMismatchError as refusal:
    print(f"refused, naming the enum: {'StaleAction' in str(refusal)}", flush=True)
"""

FIELD_TYPE = PRELUDE + """
class SignlessMeta(ctypes.Structure):
    _fields_ = [(name, ctypes.c_uint8 if name == "device_idx" else kind)
                for name, kind in crucible_native.CrucibleMeta._fields_]
crucible_native.MIRRORS["CrucibleMeta"] = SignlessMeta
try:
    crucible_native._VesselLib()
    print("the mirror of another field type loads", flush=True)
except CrucibleAbiMismatchError as refusal:
    print(f"refused, naming the Structure: {'SignlessMeta' in str(refusal)}", flush=True)
"""

PAIR = PRELUDE + """
crucible_native._VesselLib()
def other_build(image, note):
    image[note.offset] ^= 0xFF
with tempfile.TemporaryDirectory() as folder:
    copy = planted_copy(crucible_native.DISPATCH_LIBRARY, folder, other_build)
    table = Path(folder) / crucible_native.LAYOUT_TABLE
    digest = crucible_native.layout_note(copy).digest
    lines = [line for line in table.read_text().splitlines() if not line.startswith("digest ")]
    table.write_text("\\n".join([f"digest {digest}", *lines]) + "\\n")
    try:
        crucible_native._DispatchLib(copy)
        print("the library of another build loads", flush=True)
    except CrucibleAbiMismatchError as refusal:
        print(f"refused, naming the first library: {crucible_native.VESSEL_LIBRARY in str(refusal)}", flush=True)
    print(f"mapped after the refusal: {copy in Path('/proc/self/maps').read_text()}", flush=True)
    print(f"kernel after the refusal: {has_crucible_kernel()}", flush=True)
"""

UNDECLARED = PRELUDE + """
import crucible_mode
vessel = crucible_mode._VesselLib()
for owner, name in ((vessel, "crucible_output_ptr"), (vessel._lib, "crucible_entry_of_no_table")):
    try:
        getattr(owner, name)
        print(f"{name} is reachable", flush=True)
    except AttributeError:
        print(f"{name} raises AttributeError", flush=True)
"""

DECLARED = PRELUDE + """
import crucible_mode
vessel = crucible_mode._VesselLib()
dispatch = crucible_native._DispatchLib()
table = crucible_native.read_layout_table(Path(built_path(crucible_native.VESSEL_LIBRARY)).parent)
problems = []
for name, line in table.entries.items():
    owner = vessel._lib if line.library == crucible_native.VESSEL_ENTRIES else dispatch._lib
    function = getattr(owner, name, None)
    if function is None:
        problems.append(f"{name} has no declaration")
        continue
    expected = (crucible_native.ctypes_of_word(line.result),
                [crucible_native.ctypes_of_word(word) for word in line.parameters])
    if (function.restype, list(function.argtypes)) != expected:
        problems.append(f"{name} is declared {function.restype} {function.argtypes}, and its line gives {expected}")
print(f"entry lines: {len(table.entries)}", flush=True)
print(f"each entry of the table has its declaration: {not problems} {problems}", flush=True)
"""

CHECKS = (
    ("built", BUILT, ("libcrucible_vessel.so note matches: True", "libcrucible_dispatch.so note matches: True",
                      "each wrapper loads")),
    ("planted", PLANTED, ("refused, naming the library: True", "mapped after the refusal: False",
                          "kernel after the refusal: False", "kernel after the built load: True")),
    ("no note", NO_NOTE, ("no note read: True", "refused, naming the library: True")),
    ("mirror", MIRROR, ("refused, naming the Structure: True", "refused, naming the enum: True")),
    ("field type", FIELD_TYPE, ("refused, naming the Structure: True",)),
    ("pair", PAIR, ("refused, naming the first library: True", "mapped after the refusal: False",
                    "kernel after the refusal: False")),
    ("undeclared", UNDECLARED, ("crucible_output_ptr raises AttributeError",
                                "crucible_entry_of_no_table raises AttributeError")),
    ("declared", DECLARED, ("each entry of the table has its declaration: True",)),
)


def run_child(code: str) -> subprocess.CompletedProcess[str]:
    """Run CODE in a child interpreter with the vessel directory, and give its result."""
    return subprocess.run([sys.executable, "-E", "-B", "-c", code, str(VESSEL_DIRECTORY)], env=dict(os.environ),
                          capture_output=True, text=True, timeout=600, check=False)


def main() -> int:
    """Run each check in its own child and report."""
    problems: list[str] = []
    for name, code, markers in CHECKS:
        child = run_child(code)
        if child.returncode != 0:
            problems.append(f"{name}: the child ends with {child.returncode}: {child.stderr[-2000:]!r}")
        for marker in markers:
            if marker not in child.stdout:
                problems.append(f"{name}: the child does not print {marker!r}: {child.stdout!r}")
    for line in problems:
        print(f"  FAIL: {line}")
    if problems:
        return 1
    print("test_abi_version: each check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
