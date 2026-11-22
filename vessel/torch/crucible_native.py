"""Crucible native vessel controller — C++ dispatch via DispatchKey::Crucible.

Activates the C++ boxed fallback (crucible_fallback.cpp) so that EVERY ATen
op — forward, backward, optimizer — is intercepted at ~100ns/op and fed to
the Vigil runtime (TraceRing -> BackgroundThread -> MerkleDag).

Requires:
  - PyTorch fork with DispatchKey::Crucible + CrucibleState TLS
    (built from patches/pytorch-crucible-integration.patch)
  - libcrucible_vessel.so  (C API to Vigil lifecycle)
  - libcrucible_dispatch.so (C++ fallback + TLS accessors)
  - libcrucible_level.so   (on x86_64: the level probe, which picks the
    x86-64 level of the two libraries; the level of a library below)

Usage:
    from crucible_native import attach

    model = torchvision.models.resnet18()
    x = torch.randn(4, 3, 224, 224)

    with attach(model, optimizer, device="cuda:0") as ctx:
        for i in range(3):  # 1 warmup + 2 for iteration detection
            optimizer.zero_grad()
            out = model(x)
            loss = criterion(out, labels)
            loss.backward()
            optimizer.step()
        ctx.export_trace("resnet18.crtrace")

attach() takes over what a caller used to do by hand: it enables the dispatch
key, installs the module scope hooks for the recording phase and takes them off
again at activation, keeps torch.compile off the attached model, and wraps
Tensor.backward so that the runtime can see where a backward pass starts and
ends.  The phase of an operation is derived from that, not declared:

  BACKWARD   inside the extent of the Tensor.backward wrapper
  OPTIMIZER  a fused `_foreach_` kernel with gradients off
  FORWARD    anything else

The derivation lives in vessel/torch/record_kernel.h, which is the one copy
both recording paths read.
"""

import contextlib
import ctypes
import enum
import logging
import os
import platform
from collections.abc import Callable, Iterable, Iterator
from pathlib import Path
from typing import TYPE_CHECKING, Any, NamedTuple

import torch

if TYPE_CHECKING:
    import torch.nn as nn
    import torch.optim as optim


log = logging.getLogger(__name__)


# =====================================================================
# FNV-1a 64-bit: the scope hash of a module path
#
# The fold is the FNV-1a of fnv1a_step in vessel/torch/entry_slots.h.  The
# dispatch library takes the scope hash from the CrucibleState of the thread
# as it is, and no C++ code folds a module path again.
# =====================================================================

_FNV_OFFSET = 0xCBF29CE484222325
_FNV_PRIME  = 0x100000001B3
_MASK64     = (1 << 64) - 1


class SchemaNameResult(enum.IntEnum):
    """The result of crucible_register_schema_name (CRUCIBLE_SCHEMA_NAME_* of vessel_api.h)."""

    # The table holds the name for the hash.
    REGISTERED = 0
    # The call gave no name to register.
    REFUSED = 1
    # The table holds a different name for the hash, and it keeps that name.
    COLLISION = 2
    # The table holds the most names that it can hold, and the hash is not one of them.
    FULL = 3


# The top bit of the word of a storage generation (crucible::StorageGeneration
# in include/crucible/Types.h).  A word with the bit holds the period of the
# allocator for a storage that the generation allocator did not make.  Zero is
# no generation, and each other word is a known generation.
STORAGE_GENERATION_UNKNOWN_BIT = 1 << 31


class AbiStamp(NamedTuple):
    """The ABI stamp of the level probe, and the version that this module reads.

    The two vessel libraries carry a layout note in place of a stamp (the
    layout of a library).  The probe keeps its stamp, because the test
    launcher of the gate reads it too (utils/assay/core/x86level.py).
    """

    library: str  # the file name of the library, for the message
    symbol: str  # the C function that gives the stamp
    expected: int
    rebuild: str  # the build command that the message names


class CrucibleSlot(ctypes.Structure):
    """One slot of the memory plan of the active region, in the layout of CrucibleSlot of vessel_api.h."""

    _fields_ = [
        ("offset_bytes", ctypes.c_uint64),
        ("nbytes", ctypes.c_uint64),
        ("birth_op", ctypes.c_uint32),
        ("death_op", ctypes.c_uint32),
        ("slot_id", ctypes.c_uint32),
        ("dtype", ctypes.c_int8),
        ("device_type", ctypes.c_int8),
        ("device_idx", ctypes.c_int8),
        ("layout", ctypes.c_int8),
        ("is_external", ctypes.c_uint8),
        ("pad0", ctypes.c_uint8),
        ("pad1", ctypes.c_uint16),
        ("pad2", ctypes.c_uint32),
    ]


class RegionSlot(NamedTuple):
    """One slot of the memory plan of the active region (crucible::TensorSlot).

    An op index is the position of the op in the region, in the order of
    export_trace.  An external slot keeps the storage that it had before the
    region, so it takes no bytes of the pool.
    """

    slot_id: int
    offset_bytes: int
    nbytes: int
    birth_op: int
    death_op: int
    dtype: int
    is_external: bool


class CrucibleBoundScalar(ctypes.Structure):
    """One data slot of the replayed region, in the layout of CrucibleBoundScalar of vessel_api.h."""

    _fields_ = [
        ("op_index", ctypes.c_uint32),
        ("slot", ctypes.c_uint32),
        ("value", ctypes.c_int64),
    ]


class BoundScalar(NamedTuple):
    """One data slot of the region of the last activation or switch (crucible::Vigil::BoundScalar).

    A data slot holds a scalar of the type Scalar, float or double, and the
    replay binds the value of the live op at each iteration.  An op index is
    the position of the op in the region, in the order of export_trace.  The
    value is the bits of the scalar, as a slot of a trace holds them.
    """

    op_index: int
    slot: int
    bits: int


class CrucibleMeta(ctypes.Structure):
    """The record of one tensor of an op, in the layout of CrucibleMeta of vessel_api.h."""

    _fields_ = [
        ("sizes", ctypes.c_int64 * 8),
        ("strides", ctypes.c_int64 * 8),
        ("data_ptr", ctypes.c_void_p),
        ("ndim", ctypes.c_uint8),
        ("dtype", ctypes.c_int8),
        ("device_type", ctypes.c_int8),
        ("device_idx", ctypes.c_int8),
        ("layout", ctypes.c_int8),
        ("requires_grad", ctypes.c_uint8),
        ("flags", ctypes.c_uint8),
        ("output_nr", ctypes.c_uint8),
        ("storage_offset", ctypes.c_int64),
        ("version", ctypes.c_uint32),
        ("pad0", ctypes.c_uint32),
        ("storage_nbytes", ctypes.c_uint64),
        ("grad_fn_hash", ctypes.c_uint64),
    ]


class CrucibleDispatchResult(ctypes.Structure):
    """The answer of a dispatch, in the layout of CrucibleDispatchResult of vessel_api.h."""

    _fields_ = [
        ("action", ctypes.c_uint8),
        ("status", ctypes.c_uint8),
        ("enters_region", ctypes.c_uint8),
        ("pad", ctypes.c_uint8),
        ("op_index", ctypes.c_uint32),
    ]


# The library names of the two vessel libraries, and their build commands.
VESSEL_LIBRARY = "libcrucible_vessel.so"
DISPATCH_LIBRARY = "libcrucible_dispatch.so"


class DispatchAction(enum.IntEnum):
    """The action of a CrucibleDispatchResult (CRUCIBLE_ACTION_* of vessel_api.h)."""

    # The C entry refused the op, and the Vigil holds no record of it.
    REFUSED = 0
    # The Vigil recorded the op.
    RECORD = 1
    # The Vigil replays a region and checked the guard of the op.
    COMPILED = 2


class DispatchStatus(enum.IntEnum):
    """The status of a CrucibleDispatchResult with the action COMPILED (CRUCIBLE_STATUS_*)."""

    MATCH = 0
    DIVERGED = 1
    COMPLETE = 2


class TlsMode(enum.IntEnum):
    """The mode of the TLS state of a thread, the enumerators of c10::CrucibleMode of the PyTorch fork patch.

    The loader of the dispatch library compares these names and values with
    the enumerators that the library reads from the patched enum
    (check_tls_modes), so a change of the enum in the patch stops the load.
    """

    # The fallback runs each op with no record.
    INACTIVE = 0
    # The ops run eagerly, and the Vigil records them.
    RECORD = 1
    # The Vigil replays a region and checks the guard of each op.
    COMPILED = 2
    # A guard failed, and the Vigil goes back to RECORD.
    DIVERGED = 3


class CrucibleAbiMismatchError(RuntimeError):
    """Raised before the load of a Crucible library whose layout is not the layout of the build.

    The layout note of a vessel library differs from the layout table of its
    build, or the library has no note, or a ctypes Structure of this package
    differs from the table (the layout of a library).  The level probe has an
    ABI stamp, and a probe whose stamp is not AbiStamp.expected raises it
    too.  The message names the library and the build command.
    """


def _fnv1a(data: bytes) -> int:
    """FNV-1a 64-bit hash, the fold of fnv1a_step in vessel/torch/entry_slots.h."""
    h = _FNV_OFFSET
    for b in data:
        h = ((h ^ b) * _FNV_PRIME) & _MASK64
    return h


# =====================================================================
# Library locator
# =====================================================================

# Python loads the libraries of the release preset.  The sanitizer presets
# put ASan or ThreadSanitizer on every target, and a library built under one
# of them loads only when its runtime is already in the process: preloaded,
# and so first in the load order.
RELEASE_BUILD = "  cmake --preset release && cmake --build --preset release"
# Only a build with the patched PyTorch has the dispatch library.
VESSEL_BUILD = ("  cmake --preset vessel && cmake --build --preset vessel\n"
                "and set CRUCIBLE_BUILD_DIR=build-vessel")

class SanitizerRuntime(NamedTuple):
    """The runtime that an instrumented library needs in the process."""

    start: str  # the runtime-start symbol that the library imports
    sanitizer: str
    library: str  # the runtime library to preload
    option: str  # the runtime option that a run inside Python needs


# LeakSanitizer reports the objects that CPython and PyTorch keep until exit,
# and then ends the process with status 1, so an AddressSanitizer run turns
# leak detection off.  PyTorch carries no ThreadSanitizer instrumentation, and
# the runtime asks to ignore such modules.
_SANITIZER_RUNTIMES = (
    SanitizerRuntime("__asan_init", "AddressSanitizer", "libasan.so", "ASAN_OPTIONS=detect_leaks=0"),
    SanitizerRuntime("__tsan_init", "ThreadSanitizer", "libtsan.so", "TSAN_OPTIONS=ignore_noninstrumented_modules=1"),
)
_SANITIZER_START = {runtime.start.encode(): runtime for runtime in _SANITIZER_RUNTIMES}

_SHT_DYNSYM = 11
_ELF64_SECTION_HEADER = 64
_ELF64_SYMBOL = 24


def _find_lib(name: str) -> str | None:
    """Return the path of one Crucible library in the release build, or None.

    CRUCIBLE_BUILD_DIR names one directory to search before build-release.
    It exists because several agents share one worktree and each builds
    into its own directory. The value is a path, absolute or relative to
    the repository root.
    """
    base = Path(__file__).resolve().parent.parent.parent
    searched = []
    override = os.environ.get("CRUCIBLE_BUILD_DIR")
    if override:
        searched.append(Path(override) if Path(override).is_absolute() else base / override)
    searched.append(base / "build-release")
    for d in searched:
        p = d / "lib" / name
        if p.exists():
            return str(p)
    return None


def sanitizer_runtime(path: str) -> SanitizerRuntime | None:
    """Name the sanitizer runtime that the ELF library at path imports.

    Returns the runtime for a library built under ASan or ThreadSanitizer,
    and None for a library that needs neither. The read
    walks the dynamic symbol table once, O(number of dynamic symbols).
    A file that is not a 64-bit little-endian ELF object raises, because
    the check cannot be made on it.
    """
    image = Path(path).read_bytes()
    if image[:4] != b"\x7fELF" or image[4] != 2 or image[5] != 1:
        raise RuntimeError(f"{path} is not a 64-bit little-endian ELF library")
    table = int.from_bytes(image[0x28:0x30], "little")
    count = int.from_bytes(image[0x3C:0x3E], "little")

    def section(index: int) -> tuple[int, int, int, int]:
        """Return (type, offset, size, link) of one section header."""
        at = table + index * _ELF64_SECTION_HEADER
        return (int.from_bytes(image[at + 4:at + 8], "little"),
                int.from_bytes(image[at + 24:at + 32], "little"),
                int.from_bytes(image[at + 32:at + 40], "little"),
                int.from_bytes(image[at + 40:at + 44], "little"))

    for index in range(count):
        kind, offset, size, link = section(index)
        if kind != _SHT_DYNSYM:
            continue
        _, names, _, _ = section(link)
        for at in range(offset, offset + size, _ELF64_SYMBOL):
            start = names + int.from_bytes(image[at:at + 4], "little")
            symbol = image[start:image.index(b"\0", start)]
            if symbol in _SANITIZER_START:
                return _SANITIZER_START[symbol]
    return None


def checked_lib_path(name: str, lib_path: str | None, missing: str) -> str:
    """Find one Crucible library and return its path once it is safe to load.

    missing is the build command to name when no library is found. A
    library built under ASan or ThreadSanitizer loads only when its runtime
    is already in the process, which a preload does.  Without the runtime,
    dlopen of such a library ends the process, so the library is refused
    before it is loaded.

    On x86_64 the level of the library must suit the host (the level of a
    library).  With no lib_path and with CRUCIBLE_LEVEL_ROOT, the library
    comes from the directory of the picked level of that root.  A library of
    lib_path gets the level probe of named_probe().

    Raises:
        RuntimeError: If no library is found, or if it needs a sanitizer
            runtime that the process does not hold
        CrucibleLevelError: If the level of the library does not suit the
            host or the picked level (check_level)
    """
    root = os.environ.get(LEVEL_ROOT_VARIABLE, "") if lib_path is None else ""
    source = LevelSource.ROOT if root and _is_x86_64() else LevelSource.BUILD
    if source is LevelSource.ROOT:
        path: str | None = level_root_path(name, Path(root))
    else:
        path = lib_path or _find_lib(name)
    if path is None:
        raise RuntimeError(f"Cannot find {name}. Build it with:\n{missing}")
    _refuse_missing_runtime(path)
    if _is_x86_64():
        probe = Path(root) / PROBE_STAMP.library if source is LevelSource.ROOT else named_probe(Path(path))
        check_level(path, probe, source)
    return path


def _refuse_missing_runtime(path: str) -> None:
    """Refuse the library at path when it needs a sanitizer runtime that the process does not hold."""
    runtime = sanitizer_runtime(path)
    if runtime is not None and not _is_runtime_loaded(runtime):
        raise RuntimeError(
            f"{path} is built with {runtime.sanitizer}, and the "
            f"{runtime.sanitizer} runtime is not in this process.\n"
            f"Build the libraries that Python loads with the release preset:\n"
            f"{RELEASE_BUILD}\n"
            f"or preload the runtime of the same compiler:\n"
            f"  {runtime.option} LD_PRELOAD=$(g++-16p -print-file-name={runtime.library}) python ...")


# =====================================================================
# The level of a library
# =====================================================================
#
# The level of a library.  The engine builds one time for each x86-64 level,
# v1 to v4 (the x86 levels of include/foundation/os/X86Level.h).  Each
# vessel library carries an ELF note with the level of its flags (the note of
# vessel/torch/level_note.h).  The loader reads the note from the file before
# dlopen, because the global constructors of a library of a level above the
# host can fault before any check after the load.  The level probe
# libcrucible_level.so compiles at x86-64 (v1), so it runs on each host, and
# it gives the level of the host.
#
# A level root (CRUCIBLE_LEVEL_ROOT) holds the probe and one directory of the
# two vessel libraries for each level, x86-64-v1 to x86-64-v4 (the level root
# of vessel/torch/CMakeLists.txt).  The loader picks the highest level of the
# host, and takes only exact libraries of that level.  CRUCIBLE_ISA_LEVEL (v1
# to v4) asks for a level at or below the level of the host, so a test runs
# each level on one host.  The probe ends the process for a level above the
# host and for a text that is no level, and its message names the missing
# feature or the text.
#
# With no level root, the loader takes the libraries of one build directory
# (_find_lib), with the probe of that directory.  A library that a caller
# names gets the probe beside it, or the probe of the root for a library of a
# level directory of a root (named_probe).  The level of each such library
# must be at or below the picked level, so a build for -march=native loads on
# its own host.  The two vessel libraries of one directory must have the same
# note, because they share live objects and inline code.

LEVEL_ROOT_VARIABLE = "CRUCIBLE_LEVEL_ROOT"
LEVEL_REQUEST_VARIABLE = "CRUCIBLE_ISA_LEVEL"

# Mirrors kLevelProbeAbiVersion of vessel/torch/level_probe.cpp.
EXPECTED_LEVEL_ABI_VERSION = 1
PROBE_STAMP = AbiStamp("libcrucible_level.so", "crucible_level_abi_version", EXPECTED_LEVEL_ABI_VERSION,
                       RELEASE_BUILD)

_NOTE_OWNER = b"Crucible\0"
_NOTE_TYPE_LEVEL = 1
_NOTE_DESCRIPTOR_SIZE = 4
_SHT_NOTE = 7
_ELF64_HEADER = 64
_LEVELS = (1, 2, 3, 4)
_LEVEL_DIRECTORIES = frozenset(f"x86-64-v{level}" for level in _LEVELS)
_PAIR = ("libcrucible_vessel.so", "libcrucible_dispatch.so")


class LevelKind(enum.IntEnum):
    """The kind of the level of a library (the note of vessel/torch/level_note.h)."""

    # The flags give each feature of the level and no feature above it.
    EXACT = 1
    # The flags can give features above the level, as -march=native does.
    HOST = 2


_KINDS = tuple(item.value for item in LevelKind)


class LevelSource(enum.Enum):
    """The place that a library comes from, which sets the rule for its level."""

    # A directory of a level root: an exact library of the picked level.
    ROOT = "root"
    # A build directory or a given path: a library at or below the picked level.
    BUILD = "build"


class LevelNote(NamedTuple):
    """The level note of one library, and the offset of its descriptor in the file."""

    level: int  # 1 to 4
    kind: LevelKind
    offset: int


class CrucibleLevelError(RuntimeError):
    """Raised before the load of a Crucible library whose level does not suit the host or the picked level.

    The message names the library, its level and kind, and the picked level.
    """


def _is_x86_64() -> bool:
    """Say whether this host is x86_64, the one architecture whose libraries carry a level note."""
    return platform.machine() in ("x86_64", "AMD64")


class CrucibleNote(NamedTuple):
    """One note of the owner Crucible in an ELF library: its type, its descriptor, and the offset of the descriptor."""

    note_type: int
    descriptor: bytes
    offset: int


def crucible_notes(path: str) -> list[CrucibleNote]:
    """Read each note of the owner Crucible of the ELF library at path, in the order of the file.

    The read takes the ELF header, the section headers and each note
    section, and no other byte of the file.  Complexity: O(sections + bytes
    of the note sections).

    Raises:
        RuntimeError: If the file is not a 64-bit little-endian ELF object,
            or if a note runs past the end of its section
    """
    found: list[CrucibleNote] = []
    with open(path, "rb") as image:
        header = image.read(_ELF64_HEADER)
        if header[:4] != b"\x7fELF" or header[4] != 2 or header[5] != 1:
            raise RuntimeError(f"{path} is not a 64-bit little-endian ELF library")
        table = int.from_bytes(header[0x28:0x30], "little")
        entry_size = int.from_bytes(header[0x3A:0x3C], "little")
        count = int.from_bytes(header[0x3C:0x3E], "little")
        image.seek(table)
        sections = image.read(entry_size * count)
        for index in range(count):
            at = index * entry_size
            if int.from_bytes(sections[at + 4:at + 8], "little") != _SHT_NOTE:
                continue
            offset = int.from_bytes(sections[at + 24:at + 32], "little")
            size = int.from_bytes(sections[at + 32:at + 40], "little")
            alignment = max(4, int.from_bytes(sections[at + 48:at + 56], "little"))
            image.seek(offset)
            found.extend(_notes_of_section(image.read(size), offset, alignment, path))
    return found


def _notes_of_section(notes: bytes, base: int, alignment: int, path: str) -> list[CrucibleNote]:
    """Give the notes of the owner Crucible among the notes of one section.

    base is the offset of the section in the file, and alignment is the
    alignment of its notes, 4 or 8.

    Raises:
        RuntimeError: If a note runs past the end of the section
    """
    def aligned(offset: int) -> int:
        """Round the offset in the section up to the alignment of the notes.

        The header of a note has 12 bytes, so a note of the alignment 8 has
        padding after the name also when the name has 4 bytes.
        """
        return (offset + alignment - 1) // alignment * alignment

    found: list[CrucibleNote] = []
    at = 0
    while at + 12 <= len(notes):
        name_size = int.from_bytes(notes[at:at + 4], "little")
        descriptor_size = int.from_bytes(notes[at + 4:at + 8], "little")
        note_type = int.from_bytes(notes[at + 8:at + 12], "little")
        descriptor_at = aligned(at + 12 + name_size)
        end = aligned(descriptor_at + descriptor_size)
        if end > len(notes):
            raise RuntimeError(f"{path} holds a note that runs past the end of its section")
        if notes[at + 12:at + 12 + name_size] == _NOTE_OWNER:
            found.append(CrucibleNote(note_type, notes[descriptor_at:descriptor_at + descriptor_size],
                                      base + descriptor_at))
        at = end
    return found


def level_note(path: str) -> LevelNote | None:
    """Read the level note of the ELF library at path, or give None for a library with no note.

    Raises:
        RuntimeError: If the file is not a 64-bit little-endian ELF object,
            or if it holds a malformed note, or a level note with a size, a
            level or a kind that the note does not give
    """
    for note in crucible_notes(path):
        if note.note_type != _NOTE_TYPE_LEVEL:
            continue
        level, kind = (note.descriptor[0], note.descriptor[1]) if len(note.descriptor) >= 2 else (0, 0)
        if len(note.descriptor) != _NOTE_DESCRIPTOR_SIZE or level not in _LEVELS or kind not in _KINDS:
            raise RuntimeError(f"{path} holds a level note with the size {len(note.descriptor)}, the level {level} "
                               f"and the kind {kind}, and THE NOTE of vessel/torch/level_note.h gives none of them")
        return LevelNote(level, LevelKind(kind), note.offset)
    return None


# The picked level of each probe and request of the process.  The host and
# the request do not change while the process runs.
_PICKS: dict[tuple[str, bytes], int] = {}


def picked_level(probe: Path) -> int:
    """Give the level that the probe at probe picks: the level of the host, or the level of CRUCIBLE_ISA_LEVEL.

    One load and one call for each probe and request of the process.  The
    probe ends the process when CRUCIBLE_ISA_LEVEL names a level above the
    host or is no level.

    Raises:
        CrucibleLevelError: If no probe is at probe, or if its note does not
            say exact v1
        RuntimeError: If the probe needs a sanitizer runtime that the process
            does not hold
        CrucibleAbiMismatchError: If the ABI stamp of the probe is not
            EXPECTED_LEVEL_ABI_VERSION
    """
    request = os.environ.get(LEVEL_REQUEST_VARIABLE, "").encode()
    key = (str(probe), request)
    if key in _PICKS:
        return _PICKS[key]
    if not probe.exists():
        raise CrucibleLevelError(f"{probe} does not exist, and the loader needs the level probe to pick the level of "
                                 f"the host.  Build it with:\n{RELEASE_BUILD}")
    note = level_note(str(probe))
    if note is None or note.level != 1 or note.kind is not LevelKind.EXACT:
        raise CrucibleLevelError(f"{probe} is no level probe: its note is {note}, and a probe is exact v1")
    _refuse_missing_runtime(str(probe))
    library = ctypes.CDLL(str(probe))
    check_abi_stamp(library, str(probe), PROBE_STAMP)
    pick = library.crucible_level_pick
    pick.restype = ctypes.c_uint32
    pick.argtypes = [ctypes.c_char_p, ctypes.c_uint64]
    level = int(pick(request, len(request)))
    _PICKS[key] = level
    return level


def level_root_path(name: str, root: Path) -> str:
    """Give the path of the library name in the directory of the picked level of the level root root.

    Raises:
        CrucibleLevelError: If root holds no probe, or no library name at the
            picked level
    """
    level = picked_level(root / PROBE_STAMP.library)
    path = root / f"x86-64-v{level}" / name
    if not path.exists():
        raise CrucibleLevelError(f"the level root {root} has no {name} at the picked level v{level}: {path} does not "
                                 f"exist.  Install a build of the level into the root:\n"
                                 f"  cmake --install BUILD --component vessel --prefix PREFIX")
    return str(path)


def named_probe(path: Path) -> Path:
    """Give the level probe of the library at path that a caller names.

    The probe of a build directory is beside its libraries.  A level
    directory x86-64-vN of a level root holds no probe, and the probe of the
    root is in its parent (the level root of vessel/torch/CMakeLists.txt).
    So a path that came from a root, such as a path of checked_lib_path()
    under CRUCIBLE_LEVEL_ROOT, loads again.  Only a level directory sends
    the read to its parent, and the rule of the level stays the rule of a
    build: the library is at or below the picked level.
    """
    beside = path.parent / PROBE_STAMP.library
    if beside.exists() or path.parent.name not in _LEVEL_DIRECTORIES:
        return beside
    return path.parent.parent / PROBE_STAMP.library


def check_level(path: str, probe: Path, source: LevelSource) -> None:
    """Refuse the library at path before its load when its level note does not suit the picked level.

    probe is the level probe that picks the level, and source gives the rule:
    a library of a level root is exact and of the picked level, and each
    other library is at or below the picked level.  The two vessel libraries
    of one directory must hold the same note.

    Raises:
        CrucibleLevelError: If the library has no note, or a note that breaks
            the rule of source, or a note that differs from the note of the
            other vessel library of its directory
    """
    note = level_note(path)
    if note is None:
        raise CrucibleLevelError(f"{path} has no level note (THE NOTE of vessel/torch/level_note.h).  Rebuild it:\n"
                                 f"{RELEASE_BUILD}")
    picked = picked_level(probe)
    where = f"{path} has the level v{note.level} of the kind {note.kind.name.lower()}"
    if source is LevelSource.ROOT and (note.kind is not LevelKind.EXACT or note.level != picked):
        raise CrucibleLevelError(f"{where}, and a level root takes only an exact library of the picked level "
                                 f"v{picked}")
    if source is LevelSource.BUILD and note.level > picked:
        raise CrucibleLevelError(f"{where}, and the picked level is v{picked}: the host or {LEVEL_REQUEST_VARIABLE} "
                                 f"gives no level above v{picked}.  Load a library of a level root with "
                                 f"{LEVEL_ROOT_VARIABLE}")
    folder = Path(path).parent
    for other in _PAIR:
        sibling = folder / other
        if other == Path(path).name or not sibling.exists():
            continue
        sibling_note = level_note(str(sibling))
        if sibling_note is None or (sibling_note.level, sibling_note.kind) != (note.level, note.kind):
            raise CrucibleLevelError(f"{where}, and {sibling} beside it has the note {sibling_note}.  The two vessel "
                                     f"libraries of one directory share live objects, so they must have one level")


def _is_runtime_loaded(runtime: SanitizerRuntime) -> bool:
    """Say whether a sanitizer runtime is already in the process.

    A preloaded runtime is in the global scope, so its start symbol resolves
    from the handle of the process itself.
    """
    return hasattr(ctypes.CDLL(None), runtime.start)


def check_abi_stamp(library: Any, path: str, stamp: AbiStamp) -> None:
    """Refuse a loaded Crucible library whose ABI stamp is not stamp.expected.

    library is the loaded library, and path is the file that it came from,
    for the message.  A library from before the stamp has no stamp.symbol,
    and the check refuses it too.  The check runs before a caller declares
    any other symbol, so a stale library gives this message and not the
    error of a missing symbol.

    Raises:
        CrucibleAbiMismatchError: If the stamp is missing or names a
            different version.  The message names the two versions.
    """
    try:
        reader = getattr(library, stamp.symbol)
    except AttributeError:
        raise CrucibleAbiMismatchError(
            f"{stamp.library} has no ABI stamp:\n"
            f"  loaded from   : {path}\n"
            f"  observed ABI  : none (no {stamp.symbol} symbol)\n"
            f"  expected ABI  : {stamp.expected}\n"
            f"Rebuild Crucible after a change of the C ABI of the library:\n"
            f"{stamp.rebuild}") from None
    reader.restype = ctypes.c_uint64
    reader.argtypes = []
    observed = int(reader())
    if observed != stamp.expected:
        raise CrucibleAbiMismatchError(
            f"{stamp.library} ABI mismatch:\n"
            f"  loaded from   : {path}\n"
            f"  observed ABI  : {observed}\n"
            f"  expected ABI  : {stamp.expected}\n"
            f"Rebuild Crucible after a change of the C ABI of the library:\n"
            f"{stamp.rebuild}")


# =====================================================================
# The layout of a library
# =====================================================================
#
# The layout of a library.  The two vessel libraries compile the header-only
# Vigil, and each one reads values that the other one made.  Each vessel
# library carries an ELF note with the layout digest of the types and the C
# entries that the two share (the note of vessel/torch/layout_note.h).  The
# build writes the layout table beside the libraries (the table of
# vessel/torch/layout_table.cpp): the digest of the build, and the size and
# the member offsets of each struct of the C ABI that a ctypes Structure of
# this package mirrors (MIRRORS).
#
# The loader reads the table and the note from the file before dlopen.  So a
# refused library runs no global constructor and registers no kernel, and the
# process can load a rebuilt library after the refusal.  It refuses a library
# with no note, or with a note that differs from the table of its directory,
# and a mirror whose size or field offsets differ from the table.  After the
# load, the library gives its digest through its C entry, and the loader
# compares it with the table again, as the second guard.

LAYOUT_TABLE = "crucible_layout_digest.txt"
_NOTE_TYPE_LAYOUT = 2
_LAYOUT_DESCRIPTOR_SIZE = 8

# The ctypes Structures of this package, by the name of the C struct that
# each one mirrors.
MIRRORS: dict[str, type[ctypes.Structure]] = {
    "CrucibleMeta": CrucibleMeta,
    "CrucibleDispatchResult": CrucibleDispatchResult,
    "CrucibleSlot": CrucibleSlot,
    "CrucibleBoundScalar": CrucibleBoundScalar,
}

# The enums of this package, by the prefix of the C constants of vessel_api.h
# that each one mirrors: the member REFUSED of DispatchAction mirrors
# CRUCIBLE_ACTION_REFUSED.
MIRRORED_CONSTANTS: dict[str, type[enum.IntEnum]] = {
    "CRUCIBLE_ACTION_": DispatchAction,
    "CRUCIBLE_STATUS_": DispatchStatus,
    "CRUCIBLE_SCHEMA_NAME_": SchemaNameResult,
}


class LayoutTable(NamedTuple):
    """The layout table of a build: the digest, the layout of each mirrored struct, and the C constants."""

    digest: int
    sizes: dict[str, int]
    offsets: dict[str, dict[str, int]]
    constants: dict[str, int]


class LayoutNote(NamedTuple):
    """The layout note of one library, and the offset of its descriptor in the file."""

    digest: int
    offset: int


def read_layout_table(folder: Path) -> LayoutTable:
    """Read the layout table of the build of the libraries in folder.

    Raises:
        CrucibleAbiMismatchError: If folder holds no table, or a line of the
            table is no line of the table
    """
    path = folder / LAYOUT_TABLE
    if not path.exists():
        raise CrucibleAbiMismatchError(f"{folder} holds no layout table {LAYOUT_TABLE}, and the loader compares the "
                                       f"layout note of each vessel library with it.  Build the libraries with:\n"
                                       f"{VESSEL_BUILD}")
    digest: int | None = None
    sizes: dict[str, int] = {}
    offsets: dict[str, dict[str, int]] = {}
    constants: dict[str, int] = {}
    for line in path.read_text(encoding="ascii").splitlines():
        words = line.split()
        if len(words) == 2 and words[0] == "digest":
            digest = int(words[1])
        elif len(words) == 3 and words[0] == "struct":
            sizes[words[1]] = int(words[2])
            offsets.setdefault(words[1], {})
        elif len(words) == 4 and words[0] == "field":
            offsets.setdefault(words[1], {})[words[2]] = int(words[3])
        elif len(words) == 3 and words[0] == "constant":
            constants[words[1]] = int(words[2])
        else:
            raise CrucibleAbiMismatchError(f"{path} holds the line {line!r}, which is no line of THE TABLE of "
                                           f"vessel/torch/layout_table.cpp")
    if digest is None:
        raise CrucibleAbiMismatchError(f"{path} holds no digest line")
    return LayoutTable(digest, sizes, offsets, constants)


def layout_note(path: str) -> LayoutNote | None:
    """Read the layout note of the ELF library at PATH, or give None for a library with no note.

    Raises:
        RuntimeError: If the file is not a 64-bit little-endian ELF object,
            or if it holds a malformed note, or a layout note of a size other
            than 8 bytes
    """
    for note in crucible_notes(path):
        if note.note_type != _NOTE_TYPE_LAYOUT:
            continue
        if len(note.descriptor) != _LAYOUT_DESCRIPTOR_SIZE:
            raise RuntimeError(f"{path} holds a layout note of {len(note.descriptor)} bytes, and THE NOTE of "
                               f"vessel/torch/layout_note.h gives 8")
        return LayoutNote(int.from_bytes(note.descriptor, "little"), note.offset)
    return None


def check_layout(path: str, rebuild: str) -> LayoutTable:
    """Refuse the library at path before its load when its layout is not the layout of its build.

    The table comes from the directory of the library.  rebuild is the build
    command that the message names.  Gives the table, for the second guard
    after the load.

    Raises:
        CrucibleAbiMismatchError: If the directory holds no table, if the
            library has no layout note or a note that differs from the
            table, or if a mirror differs from the table (check_mirrors)
    """
    table = read_layout_table(Path(path).parent)
    note = layout_note(path)
    if note is None or note.digest != table.digest:
        observed = "none" if note is None else str(note.digest)
        raise CrucibleAbiMismatchError(
            f"{Path(path).name} has a layout that is not the layout of its build:\n"
            f"  library       : {path}\n"
            f"  layout note   : {observed}\n"
            f"  layout table  : {table.digest} ({Path(path).parent / LAYOUT_TABLE})\n"
            f"The two vessel libraries share the layout of the Vigil and of the C ABI.  Rebuild the libraries:\n"
            f"{rebuild}")
    check_mirrors(table)
    return table


def check_mirrors(table: LayoutTable) -> None:
    """Refuse each ctypes Structure of MIRRORS whose size or field offsets differ from the layout table.

    Complexity: O(fields of the mirrors).

    Raises:
        CrucibleAbiMismatchError: If the table names no such struct, or if
            the size, the names or the offsets of the fields differ
    """
    for name, mirror in MIRRORS.items():
        if name not in table.sizes:
            raise CrucibleAbiMismatchError(f"the layout table names no struct {name}, which {mirror.__name__} of "
                                           f"crucible_native.py mirrors.  Rebuild the libraries:\n{VESSEL_BUILD}")
        expected = table.offsets[name]
        observed = {field: getattr(mirror, field).offset for field, *_ in mirror._fields_}
        if ctypes.sizeof(mirror) != table.sizes[name] or observed != expected:
            raise CrucibleAbiMismatchError(
                f"{mirror.__name__} of crucible_native.py does not mirror {name} of vessel_api.h:\n"
                f"  ctypes size and offsets : {ctypes.sizeof(mirror)} {observed}\n"
                f"  C size and offsets      : {table.sizes[name]} {expected}\n"
                f"Make the fields of the Structure the fields of the struct, in their order and types.")
    for prefix, mirror_enum in MIRRORED_CONSTANTS.items():
        expected_values = {name[len(prefix):]: value for name, value in table.constants.items()
                           if name.startswith(prefix)}
        observed_values = {member.name: int(member.value) for member in mirror_enum}
        if observed_values != expected_values:
            raise CrucibleAbiMismatchError(
                f"{mirror_enum.__name__} of crucible_native.py does not mirror the constants {prefix}* of "
                f"vessel_api.h:\n"
                f"  Python values : {observed_values}\n"
                f"  C values      : {expected_values}\n"
                f"Give the enum the names and the values of the C constants.")


def check_loaded_layout(library: Any, symbol: str, path: str, table: LayoutTable) -> None:
    """Refuse a loaded library whose layout entry gives a digest other than the digest of the table.

    The second guard of the layout of a library, after the check of the note.

    Raises:
        CrucibleAbiMismatchError: If the library has no SYMBOL, or it gives a
            different digest
    """
    try:
        reader = getattr(library, symbol)
    except AttributeError:
        raise CrucibleAbiMismatchError(f"{path} has no {symbol}, which gives the layout digest of the library.  "
                                       f"Rebuild the libraries:\n{VESSEL_BUILD}") from None
    reader.restype = ctypes.c_uint64
    reader.argtypes = []
    observed = int(reader())
    if observed != table.digest:
        raise CrucibleAbiMismatchError(f"{path} gives the layout digest {observed} after the load, and its layout "
                                       f"table holds {table.digest}.  Rebuild the libraries:\n{VESSEL_BUILD}")


def load_vessel_library(lib_path: str | None = None) -> ctypes.CDLL:
    """Load libcrucible_vessel.so, and return it once its layout is the layout of its build.

    lib_path names the library, or None to search the build directories
    (_find_lib).  Each wrapper of the vessel library loads it through this
    function, so no wrapper reads a library of a different layout.

    Raises:
        RuntimeError: If no library is found, or if it needs a sanitizer
            runtime that the process does not hold (checked_lib_path).
        CrucibleAbiMismatchError: If the layout of the library or of a
            mirror is not the layout of the build (check_layout)
    """
    path = checked_lib_path(VESSEL_LIBRARY, lib_path, RELEASE_BUILD)
    table = check_layout(path, RELEASE_BUILD)
    library = ctypes.CDLL(path)
    check_loaded_layout(library, "crucible_layout_digest", path, table)
    return library


# The most enumerators that read_tls_modes reads: one for each value of the
# uint8_t base of c10::CrucibleMode.
_MAX_TLS_MODES = 256


def read_tls_modes(library: Any) -> dict[str, int]:
    """Read the name and the value of each enumerator of c10::CrucibleMode from the loaded dispatch library.

    The library reads them from the patched enum by reflection, and gives
    them through crucible_dispatch_mode_entry.  Complexity: one call for each
    enumerator.

    Raises:
        CrucibleAbiMismatchError: If the library has no crucible_dispatch_mode_entry
    """
    try:
        entry = library.crucible_dispatch_mode_entry
    except AttributeError:
        raise CrucibleAbiMismatchError("the dispatch library has no crucible_dispatch_mode_entry, which gives the "
                                       f"modes of c10::CrucibleMode.  Rebuild the libraries:\n{VESSEL_BUILD}") from None
    entry.restype = ctypes.c_uint32
    entry.argtypes = [ctypes.c_uint32, ctypes.POINTER(ctypes.c_uint8), ctypes.POINTER(ctypes.c_char_p)]
    modes: dict[str, int] = {}
    for index in range(_MAX_TLS_MODES):
        value = ctypes.c_uint8(0)
        name = ctypes.c_char_p(None)
        if entry(ctypes.c_uint32(index), ctypes.byref(value), ctypes.byref(name)) != 1:
            break
        if name.value is not None:
            modes[name.value.decode("ascii")] = int(value.value)
    return modes


def check_tls_modes(observed: dict[str, int], path: str) -> None:
    """Refuse a dispatch library whose enumerators of c10::CrucibleMode differ from TlsMode.

    Raises:
        CrucibleAbiMismatchError: If a name or a value differs
    """
    expected = {member.name: int(member.value) for member in TlsMode}
    if observed != expected:
        raise CrucibleAbiMismatchError(
            f"TlsMode of crucible_native.py does not mirror c10::CrucibleMode of the dispatch library {path}:\n"
            f"  Python values : {expected}\n"
            f"  C values      : {observed}\n"
            f"Give TlsMode the names and the values of c10::CrucibleMode of "
            f"patches/pytorch-crucible-integration.patch.")


def load_dispatch_library(lib_path: str | None = None) -> ctypes.CDLL:
    """Load libcrucible_dispatch.so, and return it once its layout is the layout of its build.

    lib_path names the library, or None to search the build directories
    (_find_lib).  The wrapper of the dispatch library loads it through this
    function.  The load runs the global constructors of the library, and
    they register its kernels and its fallback for DispatchKey::Crucible.
    The check of the layout reads the file before the load, so a refused
    library registers no kernel, and the process can load a rebuilt library.
    After the load, the modes of the library must be the modes of TlsMode
    (check_tls_modes).  A refused library gets no mode from this package, so
    its fallback records nothing.

    Raises:
        RuntimeError: If no library is found, or if it needs a sanitizer
            runtime that the process does not hold (checked_lib_path).
        CrucibleAbiMismatchError: If the layout of the library or of a
            mirror is not the layout of the build (check_layout), or if
            its modes differ from TlsMode (check_tls_modes)
    """
    path = checked_lib_path(DISPATCH_LIBRARY, lib_path, VESSEL_BUILD)
    table = check_layout(path, VESSEL_BUILD)
    library = ctypes.CDLL(path)
    check_loaded_layout(library, "crucible_dispatch_layout_digest", path, table)
    check_tls_modes(read_tls_modes(library), path)
    return library


# =====================================================================
# Vessel C API wrapper (Vigil lifecycle + trace export)
# =====================================================================

class _VesselLib:
    """Ctypes wrapper for libcrucible_vessel.so — Vigil lifecycle + queries."""

    def __init__(self, lib_path: str | None = None) -> None:
        """Load the vessel library once its layout is the layout of its build (load_vessel_library)."""
        self._lib = load_vessel_library(lib_path)
        self._setup()

    def _setup(self) -> None:
        """Declare the restype and argtypes of every symbol this class calls."""
        L = self._lib

        # Lifecycle.  crucible_create takes the eager word of the attach.
        L.crucible_create.restype = ctypes.c_void_p
        L.crucible_create.argtypes = [ctypes.c_uint32]
        L.crucible_destroy.restype = None
        L.crucible_destroy.argtypes = [ctypes.c_void_p]
        L.crucible_flush.restype = ctypes.c_int
        L.crucible_flush.argtypes = [ctypes.c_void_p]

        # Queries
        L.crucible_is_compiled.restype = ctypes.c_int
        L.crucible_is_compiled.argtypes = [ctypes.c_void_p]
        L.crucible_compiled_iterations.restype = ctypes.c_uint64
        L.crucible_compiled_iterations.argtypes = [ctypes.c_void_p]
        L.crucible_diverged_count.restype = ctypes.c_uint64
        L.crucible_diverged_count.argtypes = [ctypes.c_void_p]
        L.crucible_bg_iterations.restype = ctypes.c_uint32
        L.crucible_bg_iterations.argtypes = [ctypes.c_void_p]
        L.crucible_ring_size.restype = ctypes.c_uint32
        L.crucible_ring_size.argtypes = [ctypes.c_void_p]
        L.crucible_metalog_size.restype = ctypes.c_uint32
        L.crucible_metalog_size.argtypes = [ctypes.c_void_p]

        # Trace export
        L.crucible_export_crtrace.restype = ctypes.c_int
        L.crucible_export_crtrace.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        L.crucible_active_num_ops.restype = ctypes.c_uint32
        L.crucible_active_num_ops.argtypes = [ctypes.c_void_p]
        L.crucible_active_content_hash.restype = ctypes.c_uint64
        L.crucible_active_content_hash.argtypes = [ctypes.c_void_p]
        L.crucible_active_region_slots.restype = ctypes.c_uint32
        L.crucible_active_region_slots.argtypes = [ctypes.c_void_p, ctypes.POINTER(CrucibleSlot), ctypes.c_uint32]
        L.crucible_active_pool_bytes.restype = ctypes.c_uint64
        L.crucible_active_pool_bytes.argtypes = [ctypes.c_void_p]
        L.crucible_bound_scalars.restype = ctypes.c_uint32
        L.crucible_bound_scalars.argtypes = [ctypes.c_void_p, ctypes.POINTER(CrucibleBoundScalar), ctypes.c_uint32]

        # The eager word of a region entry
        L.crucible_observe_eager_numerics.restype = ctypes.c_int
        L.crucible_observe_eager_numerics.argtypes = [ctypes.c_void_p, ctypes.c_uint32]

    def create(self, eager_numerics: int) -> int:
        """Create a Vigil under the eager word of the attach, and return its opaque handle.

        Args:
            eager_numerics: The eager word, as crucible_dispatch_eager_numerics gives it

        Returns:
            The handle of the Vigil

        Raises:
            ValueError: If the library refuses the word, which names a bit that EagerNumerics does not name
        """
        handle = self._lib.crucible_create(eager_numerics)
        if not handle:
            raise ValueError(f"crucible_create refuses the eager word {eager_numerics:#x}: it names a bit that "
                             f"EagerNumerics of crucible/NumericalRecipe.h does not name")
        return handle

    def observe_eager_numerics(self, h: int, eager_numerics: int) -> bool:
        """Give the Vigil the eager word of a region entry, and give False when the library refuses the word."""
        return bool(self._lib.crucible_observe_eager_numerics(h, eager_numerics))

    def active_content_hash(self, h: int) -> int:
        """The content hash of the active region, or 0 when the Vigil has no region."""
        return self._lib.crucible_active_content_hash(h)

    def destroy(self, h: int) -> None:
        """Destroy the Vigil behind one handle."""
        self._lib.crucible_destroy(h)

    def flush(self, h: int) -> None:
        """Wait until the background thread of the Vigil processed each recorded op.

        crucible_flush gives 1 when the work is complete, and 0 when the
        background thread stopped first.  The ops after the stop stay
        unprocessed, so a stop is an error, as in crucible_mode.

        Raises:
            RuntimeError: If the background thread stopped before the flush completed
        """
        if self._lib.crucible_flush(h) == 0:
            raise RuntimeError("the background thread of the Vigil stopped before the flush completed")

    def is_compiled(self, h: int) -> bool:
        """True once the Vigil replays a region instead of recording one."""
        return bool(self._lib.crucible_is_compiled(h))

    def compiled_iterations(self, h: int) -> int:
        """The number of iterations the Vigil has replayed."""
        return self._lib.crucible_compiled_iterations(h)

    def diverged_count(self, h: int) -> int:
        """The number of times a replayed operation failed its guard."""
        return self._lib.crucible_diverged_count(h)

    def bg_iterations(self, h: int) -> int:
        """The number of iteration boundaries the background thread found."""
        return self._lib.crucible_bg_iterations(h)

    def ring_size(self, h: int) -> int:
        """The number of entries waiting in the TraceRing."""
        return self._lib.crucible_ring_size(h)

    def metalog_size(self, h: int) -> int:
        """The number of entries waiting in the MetaLog."""
        return self._lib.crucible_metalog_size(h)

    def export_crtrace(self, h: int, path: str) -> bool:
        """Write the active region to one .crtrace file."""
        return bool(self._lib.crucible_export_crtrace(h, path.encode()))

    def active_num_ops(self, h: int) -> int:
        """The number of operations in the active region."""
        return self._lib.crucible_active_num_ops(h)

    def active_region_slots(self, h: int) -> list[RegionSlot]:
        """The slots of the memory plan of the active region, in the order of the plan.  O(n) in the slots.

        Raises:
            RuntimeError: If the count of the slots changes between the read of the count and the read of the slots
        """
        count = self._lib.crucible_active_region_slots(h, None, 0)
        slots = (CrucibleSlot * count)()
        written = self._lib.crucible_active_region_slots(h, slots, count)
        if written != count:
            raise RuntimeError(f"the plan of the active region went from {count} to {written} slots between two reads.  "
                               f"Read the slots on the thread of the session, between two ops")
        return [RegionSlot(slot.slot_id, slot.offset_bytes, slot.nbytes, slot.birth_op, slot.death_op, slot.dtype,
                           bool(slot.is_external)) for slot in slots]

    def active_pool_bytes(self, h: int) -> int:
        """The bytes of the pool of the plan of the active region, or 0 when the Vigil has no region or no plan."""
        return self._lib.crucible_active_pool_bytes(h)

    def bound_scalars(self, h: int) -> list[BoundScalar]:
        """The data slots of the region of the last activation or switch, in the order of the ops.  O(n) in the slots.

        Raises:
            RuntimeError: If the count of the slots changes between the read of the count and the read of the slots
        """
        count = self._lib.crucible_bound_scalars(h, None, 0)
        bound = (CrucibleBoundScalar * count)()
        written = self._lib.crucible_bound_scalars(h, bound, count)
        if written != count:
            raise RuntimeError(f"the region went from {count} to {written} data slots between two reads.  "
                               f"Read the slots on the thread of the session, between two ops")
        return [BoundScalar(item.op_index, item.slot, item.value) for item in bound]


# =====================================================================
# Dispatch library wrapper (TLS accessors from crucible_fallback.cpp)
# =====================================================================

class _DispatchLib:
    """Ctypes wrapper for libcrucible_dispatch.so — TLS state + scope."""

    def __init__(self, lib_path: str | None = None) -> None:
        """Load the dispatch library once its layout is the layout of its build (load_dispatch_library)."""
        # Ensure torch._C is loaded first (provides libc10 symbols).
        import torch._C  # noqa: F401
        # The load registers the C++ fallback (load_dispatch_library).
        self._lib = load_dispatch_library(lib_path)
        self._setup()

    def _setup(self) -> None:
        """Declare the restype and argtypes of every symbol this class calls."""
        L = self._lib

        # TLS mode/context
        L.crucible_dispatch_set_tls_mode.restype = ctypes.c_uint32
        L.crucible_dispatch_set_tls_mode.argtypes = [ctypes.c_uint8]
        L.crucible_dispatch_get_tls_mode.restype = ctypes.c_uint8
        L.crucible_dispatch_get_tls_mode.argtypes = []
        L.crucible_dispatch_set_tls_context.restype = None
        L.crucible_dispatch_set_tls_context.argtypes = [ctypes.c_void_p]
        L.crucible_dispatch_get_tls_context.restype = ctypes.c_void_p
        L.crucible_dispatch_get_tls_context.argtypes = []

        # TLS scope hash
        L.crucible_dispatch_set_tls_scope.restype = None
        L.crucible_dispatch_set_tls_scope.argtypes = [ctypes.c_uint64]
        L.crucible_dispatch_get_tls_scope.restype = ctypes.c_uint64
        L.crucible_dispatch_get_tls_scope.argtypes = []

        # The backward window, from which the C++ side derives the BACKWARD
        # phase into op_flags bits 2-3.
        L.crucible_dispatch_backward_enter.restype = None
        L.crucible_dispatch_backward_enter.argtypes = []
        L.crucible_dispatch_backward_exit.restype = None
        L.crucible_dispatch_backward_exit.argtypes = []
        L.crucible_dispatch_backward_depth.restype = ctypes.c_uint32
        L.crucible_dispatch_backward_depth.argtypes = []

        # Schema table accessors, which list the recorded operator names
        L.crucible_dispatch_schema_count.restype = ctypes.c_uint32
        L.crucible_dispatch_schema_count.argtypes = []
        L.crucible_dispatch_schema_entry.restype = ctypes.c_int
        L.crucible_dispatch_schema_entry.argtypes = [
            ctypes.c_uint32,
            ctypes.POINTER(ctypes.c_uint64),
            ctypes.POINTER(ctypes.c_char_p),
        ]

        # The eager word of this thread: the float32 matmul precision, the two
        # TF32 bits and the flush of denormal floats.
        L.crucible_dispatch_eager_numerics.restype = ctypes.c_uint32
        L.crucible_dispatch_eager_numerics.argtypes = []

        # The generation allocator (storage_generation.h): the attach, the
        # detach, and the reads of the chain and of a storage.
        L.crucible_dispatch_attach_allocator.restype = ctypes.c_uint32
        L.crucible_dispatch_attach_allocator.argtypes = []
        L.crucible_dispatch_detach_allocator.restype = None
        L.crucible_dispatch_detach_allocator.argtypes = []
        for name in ("crucible_dispatch_cpu_allocator", "crucible_dispatch_generation_allocator",
                     "crucible_dispatch_inner_allocator"):
            getattr(L, name).restype = ctypes.c_uint64
            getattr(L, name).argtypes = []
        L.crucible_dispatch_storage_generation.restype = ctypes.c_uint32
        L.crucible_dispatch_storage_generation.argtypes = [ctypes.c_void_p]

    def eager_numerics(self) -> int:
        """The eager word of this thread, which the Vigil folds into the hash of each region."""
        return self._lib.crucible_dispatch_eager_numerics()

    def attach_allocator(self) -> bool:
        """Count one more session of the generation allocator, which then follows the mode of each Vigil.

        The first session sets the generation allocator as the CPU allocator of
        PyTorch.  At the entry and at the exit of each op, the CPU allocator
        follows the mode of its Vigil: the generation allocator for a record,
        the prior allocator for a replay.  True when the CPU allocator follows
        the Vigils after the call.
        """
        return self._lib.crucible_dispatch_attach_allocator() == 1

    def detach_allocator(self) -> None:
        """End one session of the generation allocator.  The last one puts the prior allocator back."""
        self._lib.crucible_dispatch_detach_allocator()

    def cpu_allocator(self) -> int:
        """The address of the CPU allocator of PyTorch at this time."""
        return self._lib.crucible_dispatch_cpu_allocator()

    def generation_allocator(self) -> int:
        """The address of the generation allocator."""
        return self._lib.crucible_dispatch_generation_allocator()

    def inner_allocator(self) -> int:
        """The address of the allocator that the generation allocator allocates through, or 0."""
        return self._lib.crucible_dispatch_inner_allocator()

    def storage_generation(self, tensor: Any) -> int:
        """The word of the generation of the storage of tensor that fill_meta gives it at this time.

        A known generation for a storage of the generation allocator, and else
        STORAGE_GENERATION_UNKNOWN_BIT with the period of the allocator.
        """
        return self._lib.crucible_dispatch_storage_generation(ctypes.c_void_p(tensor.untyped_storage()._cdata))

    def backward_enter(self) -> None:
        """Open a backward window on this thread."""
        self._lib.crucible_dispatch_backward_enter()

    def backward_exit(self) -> None:
        """Close the innermost backward window on this thread."""
        self._lib.crucible_dispatch_backward_exit()

    def backward_depth(self) -> int:
        """How many backward windows are open on this thread."""
        return self._lib.crucible_dispatch_backward_depth()

    def set_mode(self, mode: TlsMode) -> None:
        """Set the CrucibleState TLS mode for this thread.

        Raises:
            ValueError: If the library refuses the value, because it names no
                enumerator of c10::CrucibleMode.  The mode of the thread stays
        """
        if self._lib.crucible_dispatch_set_tls_mode(ctypes.c_uint8(int(mode))) != 1:
            raise ValueError(f"the dispatch library refuses the TLS mode {int(mode)}, which names no enumerator of "
                             f"c10::CrucibleMode.  Give a member of TlsMode")

    def set_context(self, ctx: int) -> None:
        """Set the CrucibleState TLS Vigil handle for this thread."""
        self._lib.crucible_dispatch_set_tls_context(ctypes.c_void_p(ctx))

    def set_scope(self, scope_hash: int) -> None:
        """Set the scope hash the next recorded operations carry."""
        self._lib.crucible_dispatch_set_tls_scope(ctypes.c_uint64(scope_hash))

    def get_mode(self) -> TlsMode:
        """The CrucibleState TLS mode of this thread."""
        return TlsMode(self._lib.crucible_dispatch_get_tls_mode())

    def get_scope(self) -> int:
        """The scope hash of this thread."""
        return self._lib.crucible_dispatch_get_tls_scope()

    def schema_count(self) -> int:
        """The number of entries in the two schema tables of the process.

        A hash registered before the seal and again after it counts two times.
        """
        return self._lib.crucible_dispatch_schema_count()

    def schema_entries(self) -> list[tuple[int, str]]:
        """Return all (schema_hash, name) pairs from the two schema tables of the process."""
        n = self.schema_count()
        result = []
        for i in range(n):
            h = ctypes.c_uint64(0)
            name = ctypes.c_char_p(None)
            ok = self._lib.crucible_dispatch_schema_entry(
                ctypes.c_uint32(i),
                ctypes.byref(h),
                ctypes.byref(name),
            )
            if ok and name.value is not None:
                result.append((h.value, name.value.decode("utf-8")))
        return result


# =====================================================================
# DispatchKey::Crucible accessor
# =====================================================================

def _get_crucible_dispatch_key() -> Any:
    """Get DispatchKey::Crucible from the PyTorch fork, or None.

    The fork exposes the key on the Python enum, which is the first branch.
    The string lookup behind it serves a fork that carries the key in C++
    without the enum entry.
    """
    import torch._C as _C
    try:
        return _C.DispatchKey.Crucible
    except AttributeError:
        pass
    # String-based lookup (key exists in C++ but Python enum not updated)
    for fn_name in ("_parse_dispatch_key", "_dispatch_key_parse"):
        fn = getattr(_C, fn_name, None)
        if fn is not None:
            try:
                key = fn("Crucible")
                if key is not None:
                    return key
            except Exception:
                pass
    return None


# =====================================================================
# CrucibleNative — the vessel controller
# =====================================================================

class CrucibleNative:
    """Context manager for native C++ Crucible dispatch.

    attach() is the way in.  Constructing this class directly gives the same
    controller without a model bound to it, which is what a session that only
    wants the dispatch key does.

    On activation:
      1. Load libcrucible_dispatch.so (registers TORCH_LIBRARY_IMPL fallback)
      2. Create Vigil via vessel C API
      3. Set CrucibleState TLS: mode=RECORD, context=vigil*
      4. Enable DispatchKey::Crucible in this thread's TLS

    While active:
      - Every ATen op goes through the recording kernels or crucibleFallback()
      - Scope hooks carry the module hierarchy, until the Vigil activates
      - Vigil records to TraceRing, bg thread builds DAG
      - The Tensor.backward wrapper marks the backward window, from which the
        C++ side derives the phase, and holds the autograd engine on this
        thread so that the whole window reaches the ring

    On deactivation:
      5. Disable DispatchKey::Crucible
      6. Clear TLS state
      7. Give the autograd engine its worker threads back
      8. Restore Tensor.backward and the attached model
      9. Remove scope hooks
      10. Detach the generation allocator, then destroy Vigil

    The TLS mode is a two-state gate for the C++ side: INACTIVE or not.  RECORD
    is what activation sets and the Vigil owns the move from recording a region
    to replaying one, which is_compiled() reports.

    The sessions of a process run on one thread.  A session writes structures
    of the process with no lock: the state of the generation allocator, the CPU
    allocator of PyTorch and the wrap of Tensor.backward.  The create of the
    Vigil claims the thread of the session, and each live Vigil of the process
    has one producer thread.  So a session on a second thread, while a session
    of another thread lives, ends the process at the create of its Vigil.
    """

    def __init__(self, *, vessel_lib_path: str | None = None,
                 dispatch_lib_path: str | None = None,
                 verbose: bool = False) -> None:
        """Prepare a controller.  Nothing is loaded until it is activated."""
        self._vessel_lib_path = vessel_lib_path
        self._dispatch_lib_path = dispatch_lib_path
        self._verbose = verbose

        self._vessel: _VesselLib | None = None
        self._dispatch: _DispatchLib | None = None
        self._handle: int = 0
        self._dispatch_key: Any = None
        self._active: bool = False

        # Scope tracking state
        self._hook_handles: list[Any] = []
        self._scope_names: dict[int, str] = {}  # hash -> module path

        # What attach() bound, and what has to be given back on the way out.
        # The scope model is held apart from the bound one: the hooks follow
        # whichever hierarchy track_modules was given, and only the bound
        # model's forward carries the compile mark to take off again.
        self._model: "nn.Module | None" = None
        self._scope_model: "nn.Module | None" = None
        self._optimizer: "optim.Optimizer | None" = None
        self._loader: Iterable[Any] | None = None
        self._device: torch.device | None = None
        self._model_forward_disabled: bool = False
        self._tensor_backward: Callable[..., Any] | None = None

        # The live autograd-serialisation guard, or None when the engine
        # keeps its worker threads.  One slot, so arming twice is a no-op
        # and no path restores the flag twice.
        self._mt_guard: Any = None

        # How many backward windows ran with the engine on this thread.  The
        # count is the evidence that the guard armed for each one, which no
        # later query can recover: by the time a loop asks, the guard of every
        # window it ran is already released.
        self._serialised_windows: int = 0

        # True while this session counts in the sessions of the generation
        # allocator of the dispatch library.
        self._allocator_attached: bool = False

    def __enter__(self) -> "CrucibleNative":
        """Activate the controller, or return it unchanged if already active.

        attach() hands back an active controller, so `with attach(model) as ctx`
        reaches an already-active one here.  Returning it rather than loading a
        second Vigil is what makes the two spellings one thing.
        """
        if self._active:
            return self

        self._dispatch_key = _get_crucible_dispatch_key()
        if self._dispatch_key is None:
            raise RuntimeError(
                "PyTorch fork with DispatchKey::Crucible required.\n"
                "Build from ~/Downloads/pytorch with the crucible patch applied.")

        # Load the dispatch lib (C++ fallback + TLS accessors) first, because
        # it reads the eager word that the Vigil records its regions under.
        # The load is the step that fails in practice: the build makes the
        # library only when it was given a PyTorch tree, so a session against
        # a partial build stops here, before any Vigil exists.
        self._dispatch = _DispatchLib(self._dispatch_lib_path)

        # Load vessel lib (Vigil lifecycle)
        self._vessel = _VesselLib(self._vessel_lib_path)
        self._handle = self._vessel.create(self._dispatch.eager_numerics())

        # From here the Vigil exists and owns a background thread, so every
        # later step that can fail has to give it back.
        try:
            # The generation allocator gives each CPU storage that the Vigil
            # records a generation, so the build tells a new storage from a
            # dead one at the same address.  A replay runs on the prior
            # allocator.  The flag says that this session counts in it.
            self._allocator_attached = self._dispatch.attach_allocator()
            if not self._allocator_attached:
                raise RuntimeError("the generation allocator did not attach as the CPU allocator of PyTorch")

            # Set CrucibleState TLS: mode=RECORD, context=vigil handle
            self._dispatch.set_mode(TlsMode.RECORD)
            self._dispatch.set_context(self._handle)

            # Enable DispatchKey::Crucible in this thread
            import torch._C as _C
            _C._dispatch_tls_set_dispatch_key_included(
                self._dispatch_key, True)

            self._wrap_tensor_backward()
        except BaseException:
            self._teardown()
            raise

        self._active = True

        if self._verbose:
            log.info("[crucible] native dispatch active, vigil=%#x", self._handle)

        return self

    def __exit__(self, *exc: Any) -> None:
        """Deactivate the controller and give the runtime back as it was."""
        self.detach()

    def detach(self) -> None:
        """Take the recorder off the runtime.  Safe to call more than once."""
        if not self._active:
            return
        self._active = False
        self._teardown()

    def _teardown(self) -> None:
        """Undo each step of the activation, in the reverse order.

        A process that attached and detached is left as it was found.  Every
        step is written to be safe on a half-built controller, because the
        activation calls this when one of its own steps fails.
        """
        # Disable dispatch key (stops fallback from firing)
        if self._dispatch_key is not None:
            try:
                import torch._C as _C
                _C._dispatch_tls_set_dispatch_key_included(
                    self._dispatch_key, False)
            except Exception:
                pass

        # Clear TLS state
        if self._dispatch is not None:
            self._dispatch.set_mode(TlsMode.INACTIVE)
            self._dispatch.set_context(0)
            self._dispatch.set_scope(0)

        # Restore the engine before anything else can observe it.  A
        # session that left the guard armed would change the backward
        # behavior of code that is no longer being recorded.
        self._disarm_backward_serialisation()

        # Give back Tensor.backward and the model's own forward.  Both are
        # process-wide or caller-visible, so leaving either in place would
        # change code this session no longer records.
        self._restore_tensor_backward()
        self._restore_model_forward()

        # Remove scope hooks
        self._remove_hooks()

        # The last session puts the prior CPU allocator back.  A storage that
        # the session made keeps its generation and frees through the
        # generation allocator after this.  The detach comes before the end of
        # the Vigil, so it runs while this thread holds the claim of the Vigil,
        # and a session of another thread cannot start between the two.
        if self._dispatch is not None and self._allocator_attached:
            self._dispatch.detach_allocator()
            self._allocator_attached = False

        # Destroy Vigil
        if self._vessel and self._handle:
            self._vessel.destroy(self._handle)
            self._handle = 0

        if self._verbose:
            log.info("[crucible] native dispatch deactivated")

    # ── What attach() binds ──────────────────────────────────────────

    def bind(self, model: "nn.Module",
             optimizer: "optim.Optimizer | None" = None,
             loader: Iterable[Any] | None = None,
             device: "torch.device | str | None" = None) -> None:
        """Bind one training loop's parts to this controller.

        The model is moved to the device when one is named, kept out of the
        reach of torch.compile, and tracked for module scope.  The optimizer
        and the loader are held for reference and for the check below.

        Call this on an active controller.  attach() does it for the caller.
        """
        if not self._active:
            raise RuntimeError("bind() requires an active CrucibleNative")

        self._model = model
        self._optimizer = optimizer
        self._loader = loader

        if device is not None:
            self._device = torch.device(device)
            model.to(self._device)

        self._disable_model_compile(model)
        self.track_modules(model)
        self._warn_if_optimizer_phase_is_invisible(optimizer)

    def _disable_model_compile(self, model: "nn.Module") -> None:
        """Keep torch.compile off the attached model.

        Crucible is the compiler for this model: it records the operations the
        model dispatches and replays them from a region of its own.  A dynamo
        graph would capture those operations before the dispatcher sees them,
        so the recorder would be handed a compiled artifact instead of the
        model, and the region would describe the artifact.

        The mark goes on the model's own forward rather than on a returned
        wrapper, so it holds for the caller's object.  torch.compile() applied
        to this model later then finds a forward it must not trace.
        """
        model.forward = torch.compiler.disable(model.forward)
        self._model_forward_disabled = True

    def _restore_model_forward(self) -> None:
        """Take the compile mark off the attached model's forward."""
        if not self._model_forward_disabled:
            return
        self._model_forward_disabled = False
        model = self._model
        if model is not None:
            # The mark is an instance attribute shadowing the bound method, so
            # deleting it uncovers the class's own forward.
            model.__dict__.pop("forward", None)

    def _warn_if_optimizer_phase_is_invisible(
            self, optimizer: "optim.Optimizer | None") -> None:
        """Say so when this optimizer asked for single-tensor kernels.

        The OPTIMIZER phase is derived from a fused `_foreach_` kernel with
        gradients off, so a step built on single-tensor kernels carries the
        FORWARD phase bits instead.  The step is still recorded and replayed
        correctly; only the label is lost.

        What this check reads is the one case that is visible from here: a group
        that asked for foreach=False outright.  A group that left the choice to
        the runtime is not covered, because the runtime makes it per device
        inside the step and a CPU step commonly resolves to single-tensor
        kernels.  So silence here is not a promise that the step will carry the
        OPTIMIZER label.
        """
        if optimizer is None:
            return
        groups = optimizer.param_groups
        if groups and all(group.get("foreach") is False for group in groups):
            log.warning(
                "[crucible] %s asked for foreach=False, so its step emits no "
                "_foreach_ kernel and its operations carry the FORWARD phase",
                type(optimizer).__name__)

    # ── The backward window ──────────────────────────────────────────
    #
    # The phase of an operation is derived in C++ (record_kernel.h), and the
    # one fact the derivation cannot read off the operation is whether a
    # backward pass is running.  This wrapper is where that fact comes from:
    # the window is the dynamic extent of Tensor.backward, bracketed by a
    # counter in the dispatch library.
    #
    # The extent is a better signal than the thread the operation arrived on,
    # and it is the serialisation below that makes it so.  Thread identity used
    # to be the only option, because a backward pass over accelerator tensors
    # ran on a device worker thread and a flag on the calling thread would have
    # missed every operation of it.  With the engine held on the calling
    # thread, the window and the operations it produces are on one thread, so
    # the flag sees all of them.  The counter stays thread-local so that a
    # caller who reaches the engine another way is merely short of a phase bit
    # instead of labelling another thread's forward pass backward.
    #
    # Wrapping the class rather than the tensor is what makes this work for a
    # caller who writes loss.backward() and nothing else.  torch.autograd.grad
    # and a direct torch.autograd.backward call are not covered; a loop that
    # uses either wraps it in recording_backward() to get the serialisation,
    # and its backward operations carry the FORWARD phase bits.

    def _wrap_tensor_backward(self) -> None:
        """Wrap Tensor.backward for the lifetime of this controller."""
        if self._tensor_backward is not None:
            return
        original = torch.Tensor.backward
        self._tensor_backward = original
        controller = self
        # Bound once, as the scope hooks bind it: the wrapper is installed
        # after the library is loaded and removed before the controller lets go
        # of it, so the handle cannot go away underneath a call.
        dispatch = self._dispatch

        def backward(tensor: torch.Tensor, *args: Any, **kwargs: Any) -> Any:
            """Run one backward pass inside a marked, serialised window."""
            with controller.recording_backward():
                dispatch.backward_enter()
                try:
                    return original(tensor, *args, **kwargs)
                finally:
                    dispatch.backward_exit()
                    controller.sync_scope_hooks()

        torch.Tensor.backward = backward  # type: ignore[method-assign]

    def _restore_tensor_backward(self) -> None:
        """Put the runtime's own Tensor.backward back."""
        original, self._tensor_backward = self._tensor_backward, None
        if original is not None:
            torch.Tensor.backward = original  # type: ignore[method-assign]

    def backward_depth(self) -> int:
        """How many backward windows are open on this thread."""
        return self._dispatch.backward_depth() if self._dispatch else 0

    @property
    def serialised_backward_windows(self) -> int:
        """How many backward windows ran with the engine on this thread.

        A loop reads this to check that every window it ran was serialised.
        The guard of a finished window is already released, so the count is the
        only record that it was ever armed.
        """
        return self._serialised_windows

    # ── Module scope tracking ────────────────────────────────────────

    def sync_scope_hooks(self) -> None:
        """Match the scope hooks to the mode the Vigil is in.

        The hooks exist to put a module path on each recorded operation.  A
        replayed operation records nothing and carries no scope, so from
        activation onward each hook is a Python call per module per iteration
        that reaches a value nobody reads.  Removing them also gives
        nn.Module._call_impl its no-hooks fast path back.

        The reverse direction matters as much.  A divergence puts the Vigil
        back to recording, and a session that only ever took hooks off would
        record every later region without a module path on any operation.  So
        this reads the mode and installs or removes to match it.

        The scope hash is not part of the region content hash, so neither
        direction can turn into a divergence of its own.

        The backward wrapper calls this once per iteration.  A loop with no
        backward pass calls it itself.
        """
        if self._scope_model is None:
            return
        compiled = self.is_compiled()
        if compiled and self._hook_handles:
            self._remove_hooks()
            if self._verbose:
                log.info("[crucible] region activated, scope hooks removed")
        elif not compiled and not self._hook_handles:
            self.track_modules(self._scope_model)

    def track_modules(self, model: "nn.Module", backward_hooks: bool = False) -> None:
        """Install scope hooks on all modules for hierarchy tracking.

        Each hook sets the CrucibleState TLS scope_hash to the FNV-1a hash
        of the module's path (e.g. "layer1.0.conv1").  The C++ fallback
        reads this per-op and records it in the TraceRing.

        Forward hooks: always installed. Fire when model(x) enters each module.
        Backward hooks: opt-in (backward_hooks=True). Fire during loss.backward().
            WARNING: backward hooks conflict with PyTorch's inplace ops (relu_).
            When disabled, backward ops inherit the last forward module's scope.
        """
        if not self._dispatch:
            raise RuntimeError("track_modules() requires active CrucibleNative context")

        self._remove_hooks()  # clean any previous hooks
        self._scope_model = model

        for name, module in model.named_modules():
            if not name:
                name = "<root>"
            scope_hash = _fnv1a(name.encode("utf-8"))
            self._scope_names[scope_hash] = name

            # Forward pre-hook: sets scope for ops during forward pass
            handle = module.register_forward_pre_hook(
                self._make_forward_hook(scope_hash))
            self._hook_handles.append(handle)

            # Backward pre-hook: optional (conflicts with inplace ops)
            if backward_hooks:
                handle = module.register_full_backward_pre_hook(
                    self._make_backward_hook(scope_hash))
                self._hook_handles.append(handle)

        if self._verbose:
            n_modules = len(self._scope_names)
            kind = "fwd+bwd" if backward_hooks else "fwd-only"
            log.info("[crucible] tracking %d modules (%d hooks, %s)",
                     n_modules, len(self._hook_handles), kind)

    def _make_forward_hook(self, scope_hash: int) -> Callable[..., None]:
        """Build the forward pre-hook that names one module's scope."""
        dispatch = self._dispatch

        def hook(module: "nn.Module", input: Any) -> None:
            """Set the scope hash the next recorded operations carry."""
            dispatch.set_scope(scope_hash)
        return hook

    def _make_backward_hook(self, scope_hash: int) -> Callable[..., None]:
        """Build the backward pre-hook that names one module's scope."""
        dispatch = self._dispatch

        def hook(module: "nn.Module", grad_output: Any) -> None:
            """Set the scope hash the next recorded operations carry."""
            dispatch.set_scope(scope_hash)
        return hook

    def _remove_hooks(self) -> None:
        """Remove every scope hook this controller installed."""
        for h in self._hook_handles:
            h.remove()
        self._hook_handles.clear()

    # ── Trace export ─────────────────────────────────────────────────

    def export_trace(self, path: str) -> bool:
        """Export the active compiled region as .crtrace binary.

        Flushes the ring buffer first, waits for the bg thread to process
        all pending entries, then serializes the RegionNode to disk.

        Requires at least 2 complete iterations for the IterationDetector
        to have built a region.  Returns True on success.

        Raises:
            RuntimeError: If the context is not active, or if the background
                thread stopped before the flush completed
        """
        if not self._vessel or not self._handle:
            raise RuntimeError("export_trace() requires active CrucibleNative context")

        self._vessel.flush(self._handle)

        num_ops = self._vessel.active_num_ops(self._handle)
        if num_ops == 0:
            if self._verbose:
                bg_iters = self._vessel.bg_iterations(self._handle)
                log.info("[crucible] no active region (bg_iterations=%d)", bg_iters)
                log.info("[crucible] need 2+ complete iterations for detection")
            return False

        # The schema tables are one pair for the process, so the names that
        # the dispatch library registered while it recorded are already in
        # the tables that the export reads.
        ok = self._vessel.export_crtrace(self._handle, path)
        if self._verbose:
            if ok:
                log.info("[crucible] exported %d ops to %s", num_ops, path)
            else:
                log.error("[crucible] export failed: %s", path)
        return ok

    # ── Queries ──────────────────────────────────────────────────────

    def flush(self) -> None:
        """Wait until the background thread processed each recorded op.

        Raises:
            RuntimeError: If the background thread stopped before the flush completed
        """
        if self._vessel and self._handle:
            self._vessel.flush(self._handle)

    def is_compiled(self) -> bool:
        """True once the Vigil replays a region instead of recording one."""
        return bool(self._vessel and self._vessel.is_compiled(self._handle))

    def compiled_iterations(self) -> int:
        """The number of iterations the Vigil has replayed."""
        return self._vessel.compiled_iterations(self._handle) if self._vessel else 0

    def diverged_count(self) -> int:
        """The number of times a replayed operation failed its guard."""
        return self._vessel.diverged_count(self._handle) if self._vessel else 0

    def bg_iterations(self) -> int:
        """The number of iteration boundaries the background thread found."""
        return self._vessel.bg_iterations(self._handle) if self._vessel else 0

    def ring_size(self) -> int:
        """The number of entries waiting in the TraceRing."""
        return self._vessel.ring_size(self._handle) if self._vessel else 0

    def metalog_size(self) -> int:
        """The number of entries waiting in the MetaLog."""
        return self._vessel.metalog_size(self._handle) if self._vessel else 0

    def active_num_ops(self) -> int:
        """The number of operations in the active region."""
        return self._vessel.active_num_ops(self._handle) if self._vessel else 0

    def active_content_hash(self) -> int:
        """The content hash of the active region, or 0 when the Vigil has no region."""
        return self._vessel.active_content_hash(self._handle) if self._vessel else 0

    def active_region_slots(self) -> list[RegionSlot]:
        """The slots of the memory plan of the active region, or no slot when the Vigil has no region."""
        return self._vessel.active_region_slots(self._handle) if self._vessel else []

    def active_pool_bytes(self) -> int:
        """The bytes of the pool of the plan of the active region, or 0 when the Vigil has no region."""
        return self._vessel.active_pool_bytes(self._handle) if self._vessel else 0

    def bound_scalars(self) -> list[BoundScalar]:
        """The data slots of the replayed region with the values that the replay bound last, or no slot."""
        return self._vessel.bound_scalars(self._handle) if self._vessel else []

    # ── Autograd engine serialisation ────────────────────────────────
    #
    # The recorded op order is the trace's identity.  It fixes the region
    # content hash, the memory plan and the replay order.  The ring is
    # single-producer, so one thread alone can contribute to that order.
    #
    # By default this runtime runs a backward pass on several threads.
    # Engine::execute_with_graph_task drives the graph task's CPU ready
    # queue on the calling thread, and one worker thread serves each
    # accelerator.  A graph with a host branch and a device branch runs
    # its backward pass on two threads at the same time.  A graph across
    # two accelerators runs on two worker threads, with the calling thread
    # parked.  Every thread but the first is turned away at the ring, so
    # the trace of an accelerator model was short by its whole backward
    # window.
    #
    # Engine::ready_queue routes every node to the CPU ready queue when
    # multithreading is off, and the calling thread drives that queue.
    # One thread then produces the whole backward pass, the producer gate
    # admits it, and the window reaches the trace.
    #
    # Serialising removes a source of variation rather than adding one.
    # Measured on three accelerators, 80 runs for each mode, over a graph
    # with several paths into one leaf: the default engine gave two
    # distinct gradients, 78 runs against 2, and the serialised engine
    # gave one.  Accumulation order decides that gradient, and the order
    # is what the worker threads leave undefined.
    #
    # The guard covers every backward window the Vigil sees, recording and
    # compiled alike.  A compiled dispatch records nothing, but it advances
    # the replay cursor one operation at a time, and the region it walks is
    # a recording of the serialised stream.  A backward window that keeps
    # its worker threads reaches the producer gate on those threads and is
    # turned away, so the cursor stands still across the whole window and
    # the first operation after it fails its guard.  Measured on cuda:0
    # with the guard lifted under replay: the cursor reached index 286 of
    # an 801-operation region, where the region holds aten::zero_ from the
    # engine-driven window and the arriving operation was
    # aten::_foreach_mul_.Scalar from the optimizer.
    #
    # Replaying a stream needs the same single producer that recorded it.
    # That is the rule the Tensor.backward wrapper encodes: it arms the guard
    # across every window, whichever mode the Vigil is in.

    def _arm_backward_serialisation(self) -> None:
        """Make the next backward pass run on this thread alone."""
        if self._mt_guard is not None:
            return
        guard = torch.autograd.set_multithreading_enabled(False)
        guard.__enter__()
        self._mt_guard = guard
        self._serialised_windows += 1

    def _disarm_backward_serialisation(self) -> None:
        """Give the backward pass its worker threads back."""
        guard, self._mt_guard = self._mt_guard, None
        if guard is not None:
            guard.__exit__(None, None, None)

    @property
    def backward_serialised(self) -> bool:
        """True while the guard holds the engine on the calling thread."""
        return self._mt_guard is not None

    @contextlib.contextmanager
    def recording_backward(self) -> Iterator["CrucibleNative"]:
        """Serialise the autograd engine across one backward pass.

        The Tensor.backward wrapper uses this, so a loop that writes
        loss.backward() gets it without asking.  A loop that reaches the engine
        another way asks for it:

            with ctx.recording_backward():
                torch.autograd.backward(loss)

        The body runs on one thread whether the Vigil records the window or
        replays it.  Refer to the note above.
        """
        # A guard already armed by an enclosing call stays with that caller,
        # so the restore below belongs to whoever armed it.
        armed_here = not self.backward_serialised
        if armed_here:
            self._arm_backward_serialisation()
        try:
            yield self
        finally:
            if armed_here:
                self._disarm_backward_serialisation()

    @property
    def scope_names(self) -> dict[int, str]:
        """Map of scope_hash -> module path for all tracked modules."""
        return self._scope_names


# =====================================================================
# attach — the way in
# =====================================================================

def attach(model: "nn.Module",
           optimizer: "optim.Optimizer | None" = None,
           loader: Iterable[Any] | None = None,
           device: "torch.device | str | None" = None,
           *,
           verbose: bool = False,
           vessel_lib_path: str | None = None,
           dispatch_lib_path: str | None = None) -> CrucibleNative:
    """Put the recorder on one training loop and return the live controller.

    The controller comes back active, so both spellings work:

        ctx = attach(model, optimizer, device="cuda:0")
        ...
        ctx.detach()

        with attach(model, optimizer, device="cuda:0") as ctx:
            ...

    Args:
        model: The model to record.  attach moves it to the device when one is
            named, keeps torch.compile off it, and tracks its module hierarchy
        optimizer: The optimizer of the loop, held for reference and checked
            against the phase derivation.  None when the loop has none
        loader: The data source of the loop, held for reference.  Nothing reads
            it yet: absorbing the input pipeline is a later layer of the runtime
        device: Where the model runs.  None leaves the model where it is, which
            is what a loop that places its own parameters wants
        verbose: Log the lifecycle of the recorder
        vessel_lib_path: An explicit path to libcrucible_vessel.so
        dispatch_lib_path: An explicit path to libcrucible_dispatch.so

    Returns:
        The active controller

    Raises:
        RuntimeError: If the PyTorch build carries no DispatchKey::Crucible
    """
    controller = CrucibleNative(vessel_lib_path=vessel_lib_path,
                                dispatch_lib_path=dispatch_lib_path,
                                verbose=verbose)
    controller.__enter__()
    try:
        controller.bind(model, optimizer, loader, device)
    except BaseException:
        controller.detach()
        raise
    return controller
