"""library_bytes — the bytes of each class of code in a static library, from DWARF.

THE MEASURE
    read_measure() reads the code bodies of each object of an archive
    (assay.readers.dwarf, THE CODE BODIES).  The declaring file of a body
    gives its class (CLASSES):
      `ours`        a relative path, or a file in a directory of the tree,
                    such as the source root or the build directory
      `libstdc++`   a file in an include/c++ directory of the toolchain
      `compiler`    a different file of the toolchain, such as a header of GCC
      `system`      a different absolute path, such as a header of the C
                    library
      `generated`   no file, for a body that the compiler makes, such as the
                    handler of a contract violation (DW_AT_artificial)
    The toolchain is the prefix of the compiler of the build (places_of).  A
    body with no file that the compiler does not make has no class, and the
    measure does not count it.  The library bytes of an archive are the
    exclusive bytes of its bodies of the classes libstdc++, compiler and
    system (LIBRARY_CLASSES).  `guard bom` prints the measure of an artifact
    (assay.guard.bom, THE LAYERS).

THE COMDAT GROUPS
    A body in a section of a COMDAT group counts only in the first member of
    the archive with a group of that signature.  A link keeps that copy.  A
    body of each other section counts in each member.

`guard selftest tests.readers.test_library_bytes` does the test of the module.
"""

import posixpath
from collections import Counter
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.readers import dwarf, elf

OURS = "ours"
LIBSTDCXX = "libstdc++"
COMPILER = "compiler"
SYSTEM = "system"
GENERATED = "generated"
CLASSES = (OURS, LIBSTDCXX, COMPILER, SYSTEM, GENERATED)
LIBRARY_CLASSES = (LIBSTDCXX, COMPILER, SYSTEM)
# The directory of the headers of libstdc++ in the toolchain.
LIBSTDCXX_PART = "/include/c++/"


class MeasureError(ValueError):
    """The archive, an object of it or its debug sections cannot be read."""


@dataclass(frozen=True, slots=True)
class Places:
    """The directories that give the class of a declaring file (THE MEASURE): ours, and the toolchain."""

    ours: tuple[str, ...]
    toolchain: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class Measure:
    """The bytes of one archive by the class of each body, and the library bytes of each library body by its name."""

    archive: str
    classes: Mapping[str, int]
    bodies: Mapping[str, int]

    @property
    def library(self) -> int:
        """The library bytes of the archive (THE MEASURE)."""
        return sum(self.classes.get(name, 0) for name in LIBRARY_CLASSES)


def places_of(ours: Sequence[Path], prefix: Path) -> Places:
    """Return the directories of the tree and the toolchain prefix, each also with its links resolved (THE MEASURE)."""
    mine = sorted({text for item in ours for text in (str(item), str(item.resolve()))}, key=len, reverse=True)
    toolchain = sorted({str(prefix), str(prefix.resolve())}, key=len, reverse=True)
    return Places(tuple(mine), tuple(toolchain))


def under(path: str, directories: Sequence[str]) -> str:
    """Return the first directory that holds a path, or an empty text."""
    return next((item for item in directories if path == item or path.startswith(f"{item}/")), "")


def class_of(file: str, is_artificial: bool, places: Places) -> str | None:
    """Return the class of the declaring file of a body, or None for a body with no file that the compiler does not make.

    THE MEASURE gives the classes.
    """
    if not file:
        return GENERATED if is_artificial else None
    if not posixpath.isabs(file):
        return OURS
    path = posixpath.normpath(file)
    if under(path, places.ours):
        return OURS
    prefix = under(path, places.toolchain)
    if prefix:
        return LIBSTDCXX if LIBSTDCXX_PART in path[len(prefix) :] else COMPILER
    return SYSTEM


def comdat_signatures(member: elf.Member) -> dict[int, str]:
    """Return the signature of the COMDAT group of each section of a member that has one.

    Raises:
        elf.ElfError: If a group section is damaged
    """
    return {section: group.signature for group in member.groups() if group.is_comdat for section in group.sections}


def read_measure(path: Path, places: Places) -> Measure:
    """Read the code bodies of each member of an archive, and give its measure (THE MEASURE, THE COMDAT GROUPS).

    Complexity: linear in the debug sections of the members.

    Raises:
        MeasureError: If the archive, a member or its debug sections cannot be read
    """
    classes: Counter[str] = Counter()
    bodies: Counter[str] = Counter()
    seen: set[str] = set()
    try:
        for member in elf.members(path):
            signatures = comdat_signatures(member)
            fresh: set[str] = set()
            for body in dwarf.code_bodies(member):
                signature = signatures.get(body.section, "")
                if signature in seen:
                    continue
                if signature:
                    fresh.add(signature)
                kind = class_of(body.file, body.is_artificial, places)
                if kind is None:
                    continue
                classes[kind] += body.exclusive
                if kind in LIBRARY_CLASSES and body.exclusive > 0:
                    bodies[body.name] += body.exclusive
            seen |= fresh
    except (OSError, elf.ElfError, dwarf.DwarfError) as problem:
        raise MeasureError(f"the DWARF of {path} cannot be read: {problem}") from None
    return Measure(path.name, dict(classes), dict(bodies))
