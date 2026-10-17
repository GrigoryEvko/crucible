"""linkmap — the reader of the map of a link that mold writes with -Map.

THE MAP
    mold writes the map of a link when the link gives -Map=FILE
    (cmake/LinkMap.cmake).  The function print_map of src/mapfile.cc of
    mold 3.0.0 gives the format.  The first line is the heading HEADING.
    Each other line starts with the three columns `VMA`, `Size` and `Align`
    of the heading.  The spaces after `Align` give the kind of the line
    (GAPS):
      * One space: an output chunk, such as EHDR, .text or .debug_info.  Its
        name follows the space.
      * Nine spaces: an input section of the last output chunk, with the
        shape FILE:(SECTION) (THE FILE OF AN INPUT SECTION).
      * Seventeen spaces: a symbol of the last input section.  The text of
        the symbol can hold spaces and parentheses, and the reader keeps
        none of it.
    `VMA` is the address, a number in hexadecimal with the prefix 0x, or the
    text 0.  `Size` and `Align` are decimal numbers.  A map has no empty
    line, and each other line is an error.  A line of a large program can
    hold thousands of characters.  For that reason, the reader reads one
    line at a time.

THE COLUMNS
    mold writes `VMA` in a column of 18 characters, `Size` in a column of 11
    characters and `Align` in a column of 6 characters.  Each value is at
    the right end of its column.  A value that is longer than its column
    makes the line longer, with no space before the next column.  `Align`
    of the section .bss after a page-aligned .data can be 2 MiB, a number of
    7 digits.  `Align` then follows `Size` with no space, as in the line
    "    84076162097152 .bss" for 8407616 and 2097152.  For that reason, the
    reader reads `VMA` and `Size` at their positions in the line, and
    `Align` as the digits after the column of `Size`.  An address of 64 bits
    fills its column.  A `Size` of 11 digits, 10 GB or more, fills its
    column, and no link of the tree gives a longer one.

THE FILE OF AN INPUT SECTION
    FILE is a path, relative to the working directory of the link when it
    is not absolute.  It is ARCHIVE(MEMBER) for a member of an archive, and
    split_member() gives the archive and the member.  A path of a link of
    the tree holds no ":(".  FILE is then the text before the first ":(".

THE INPUTS
    The inputs of a map are the files of its input sections, each one time,
    in the sequence of their first input section.  The map names only a
    file with a section in the output.  A shared library has no input
    section, and the map does not name it.  mold keeps each section of a
    file of the link that is not allocated, such as .debug_info and
    .gnu.build.attributes.  A file with such a section stays in the map
    after --gc-sections removes each allocated section of it.  A file with
    only allocated sections has no line when --gc-sections removes each of
    them.

WHAT THE MAP DOES NOT HOLD
    mold writes no cause of an inclusion, no merge of the program
    properties, no discarded section and no library of `--as-needed`.  The
    rule artifact-hardening then reads the program properties of each input
    file (assay.guard.engines.elf.artifact_hardening, THE CONTROL FLOW).

THE MEMBERS AND GNU LD
    On 2026-10-17, mold 3.0.0 and GNU ld 2.46.1 each linked
    crucible-hwprobe, test_session_handle and libcrucible_vessel.so.  The
    maps of the two linkers named the same archive members: 31, 17 and 38.
    A planted link showed one difference.  GNU ld names
    each member that the link takes, also when --gc-sections removes each
    section of it.  mold names such a member only when it holds a section
    that is not allocated (THE INPUTS).

THE USERS
    The rule artifact-links reads the archive members of the map of each
    link with read_members().  The rule artifact-hardening reads the inputs
    of the map of a link with read_inputs().

`guard selftest tests.readers.test_linkmap` does the test of the module.
"""

from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path

HEADING = "               VMA       Size Align Out     In      Symbol"
# The spaces after the third column of each kind of line (THE MAP).
OUTPUT_GAP = 1
INPUT_GAP = 9
SYMBOL_GAP = 17
GAPS = frozenset({OUTPUT_GAP, INPUT_GAP, SYMBOL_GAP})
# The end of the column `VMA` and of the column `Size` (THE COLUMNS).
ADDRESS_END = 18
SIZE_END = 29
HEX_PREFIX = "0x"
HEX_DIGITS = frozenset("0123456789abcdefABCDEF")
SECTION_MARK = ":("
SECTION_END = ")"


class LinkMapError(ValueError):
    """The map does not have the shape that the module reads (THE MAP), with the line of the problem."""


@dataclass(frozen=True, slots=True)
class ArchiveMember:
    """One member of an archive that a link takes: the path of the archive and the name of the member."""

    archive: str
    member: str


def split_member(text: str) -> tuple[str, str]:
    """Return the archive and the member of ARCHIVE(MEMBER), or an empty archive and the text for a plain path."""
    if text.endswith(")") and "(" in text:
        archive, _, member = text[:-1].rpartition("(")
        return archive, member
    return "", text


def is_address(token: str) -> bool:
    """Return True for an address of the map: 0, or a number in hexadecimal with the prefix 0x (THE MAP)."""
    if token == "0":
        return True
    digits = token.removeprefix(HEX_PREFIX)
    return token.startswith(HEX_PREFIX) and bool(digits) and set(digits) <= HEX_DIGITS


def is_decimal(token: str) -> bool:
    """Return True for a decimal number of ASCII digits."""
    return token.isascii() and token.isdigit()


def right_value(column: str) -> str:
    """Return the value at the right end of a column, or an empty text when the column holds a space after its value."""
    value = column.lstrip(" ")
    return "" if " " in value else value


def line_of(text: str, where: str, line: int) -> tuple[int, str]:
    """Return the spaces after the column `Align` of one line, and the text after them (THE COLUMNS).

    Raises:
        LinkMapError: If the line does not have the three columns, a kind of GAPS and a text
    """
    address = right_value(text[:ADDRESS_END])
    size = right_value(text[ADDRESS_END:SIZE_END])
    after = text[SIZE_END:].lstrip(" ")
    rest = after.lstrip("0123456789")
    alignment = after[: len(after) - len(rest)]
    if not is_address(address) or not is_decimal(size) or not is_decimal(alignment):
        raise LinkMapError(f"{where}:{line}: the line {text[:120]!r} has no VMA, Size and Align in their columns")
    gap = len(rest) - len(rest.lstrip(" "))
    if gap not in GAPS:
        raise LinkMapError(
            f"{where}:{line}: the line {text[:120]!r} has {gap} spaces after its Align, and a mold map has "
            f"{OUTPUT_GAP}, {INPUT_GAP} or {SYMBOL_GAP}"
        )
    if not rest[gap:]:
        raise LinkMapError(f"{where}:{line}: the line {text[:120]!r} has no text after its Align")
    return gap, rest[gap:]


def file_of(rest: str, where: str, line: int) -> str:
    """Return FILE of the text FILE:(SECTION) of an input section (THE FILE OF AN INPUT SECTION).

    Raises:
        LinkMapError: If the text does not have that shape
    """
    file_text, mark, section = rest.partition(SECTION_MARK)
    if not mark or not file_text or len(section) <= len(SECTION_END) or not section.endswith(SECTION_END):
        raise LinkMapError(f"{where}:{line}: the input section {rest[:120]!r} does not have the shape 'FILE:(SECTION)'")
    return file_text


def inputs_of(lines: Iterable[str], where: str) -> tuple[str, ...]:
    """Return the inputs of the lines of a map (THE INPUTS).  Complexity: linear in the length of the lines.

    Raises:
        LinkMapError: If the first line is not HEADING, a line does not have its shape, or an input section comes
            before the first output chunk
    """
    rows = iter(lines)
    first = next(rows, "").removesuffix("\n")
    if first != HEADING:
        raise LinkMapError(
            f"{where}: the first line {first[:80]!r} is not the heading of a map that mold writes.  The tree links "
            f"with mold, and a map of GNU ld has a different format: configure the build again, and link again"
        )
    found: dict[str, None] = {}
    has_output = False
    for line, row in enumerate(rows, start=2):
        gap, rest = line_of(row.removesuffix("\n"), where, line)
        if gap == OUTPUT_GAP:
            has_output = True
        elif gap == INPUT_GAP:
            if not has_output:
                raise LinkMapError(f"{where}:{line}: the input section {rest[:120]!r} comes before an output chunk")
            found.setdefault(file_of(rest, where, line))
    if not has_output:
        raise LinkMapError(f"{where}: the map has no output chunk after its heading")
    return tuple(found)


def read_inputs(path: Path) -> tuple[str, ...]:
    """Read the inputs of the map file of a link (THE INPUTS).

    Complexity: linear in the length of the file, because an input section of a file can come in each output chunk.

    Raises:
        LinkMapError: If the map does not have the shape that the module reads
        OSError: If the file cannot be read
    """
    with path.open(encoding="utf-8", errors="surrogateescape") as handle:
        return inputs_of(handle, str(path))


def members_of(inputs: Iterable[str]) -> tuple[ArchiveMember, ...]:
    """Return the archive member of each input that is ARCHIVE(MEMBER), in the sequence of the inputs."""
    pairs = (split_member(item) for item in inputs)
    return tuple(ArchiveMember(archive, member) for archive, member in pairs if archive)


def read_members(path: Path) -> tuple[ArchiveMember, ...]:
    """Read the archive members that the map file of a link names (THE INPUTS).  Complexity: as read_inputs().

    Raises:
        LinkMapError: If the map does not have the shape that the module reads
        OSError: If the file cannot be read
    """
    return members_of(read_inputs(path))
