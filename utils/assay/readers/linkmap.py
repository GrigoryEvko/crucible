"""linkmap — the reader of the map of a link that GNU ld writes with -Map.

THE MAP
    GNU ld writes the map of a link when the link gives -Map=FILE.  The map
    has parts, and a heading of HEADINGS starts each part.  The reader reads
    four parts, and it ignores each other part:
      * The archive members ("Archive member included to satisfy reference
        by file (symbol)").  Each item names a member of an archive, and the
        file and the symbol that made the link take the member.
      * The shared libraries of `--as-needed` ("As-needed library included to
        satisfy reference by file (symbol)").  Each item names a library, and
        the file and the symbol that made the link keep the library.
      * The discarded sections ("Discarded input sections").  Each item names
        an input section that the link removed: a section that --gc-sections
        removed, or the second copy of a COMDAT group.
      * The script and the memory map ("Linker script and memory map").  A
        line "LOAD FILE" names each input file in the sequence of the link.
        A line at the column 0 names an output section.  A line that starts
        with one space names an input section of that output section, with
        its address, its length in bytes and its file.
    A map of a link with --cref ends with "Cross Reference Table", and the
    reader reads nothing there.

THE LINES OF AN ITEM
    An item of the archive members or of the shared libraries starts at the
    column 0 with the member or the library.  When that name has 30
    characters or more, the file and the symbol are on the next line, after
    white space.  Else they are on the same line, after white space.  The
    file and the symbol have the shape "FILE (SYMBOL)".

    An input section has the shape " NAME ADDRESS LENGTH FILE".  When its
    name has 16 characters or more, the address, the length and the file are
    on the next line, after white space.  A line " *fill*" gives the fill
    bytes between two sections, and a line " *(PATTERN)" gives a rule of the
    script.  A line with only white space before its address gives a symbol
    or an assignment.  The reader keeps none of these lines.

THE FILE OF AN ITEM
    A file is a path, or ARCHIVE(MEMBER) for a member of an archive.
    split_member() gives the archive and the member, and it gives an empty
    archive for a plain path.  A path of a link of the tree has no space, and
    the reader reads the first word of a line as one path.  The file of an
    input section that the linker makes is a text such as "linker stubs",
    and the reader keeps the full text.

THE NAMES
    ld writes each name of a symbol after its demangler, unless the link
    gives --no-demangle.  The reader keeps each name as the map gives it.
    The address and the length are numbers in hexadecimal with the prefix 0x.

`guard selftest tests.readers.test_linkmap` does the test of the module.
"""

from dataclasses import dataclass
from pathlib import Path

INCLUDED = "Archive member included to satisfy reference by file (symbol)"
AS_NEEDED = "As-needed library included to satisfy reference by file (symbol)"
DISCARDED = "Discarded input sections"
SCRIPT = "Linker script and memory map"
CROSS_REFERENCES = "Cross Reference Table"
# Each heading that ld writes at the column 0 (THE MAP).  The reader reads the parts of the first four headings.
HEADINGS = frozenset(
    {
        INCLUDED,
        AS_NEEDED,
        DISCARDED,
        SCRIPT,
        CROSS_REFERENCES,
        "Allocating common symbols",
        "Memory Configuration",
        "Merging program properties",
        "Merging object attributes",
        "Linker stubs",
    }
)
LOAD = "LOAD"
# The script lines at the column 0 that name no output section.
SCRIPT_MARKS = frozenset({"START GROUP", "END GROUP"})
OUTPUT_MARK = "OUTPUT("
HEX_PREFIX = "0x"
HEX_DIGITS = frozenset("0123456789abcdefABCDEF")
PATTERN_MARK = "*"
REFERENCE_MARK = " ("


class LinkMapError(ValueError):
    """The map does not have the shape that the module reads (THE LINES OF AN ITEM), with the line of the problem."""


@dataclass(frozen=True, slots=True)
class Inclusion:
    """One item of the archive members or of the shared libraries (THE MAP).

    `archive` and `member` come from split_member(): a shared library has the path of the library in `member` and
    an empty `archive`.  A reference of the file `referrer` to `symbol` made the link take the item.
    """

    archive: str
    member: str
    referrer: str
    symbol: str


@dataclass(frozen=True, slots=True)
class InputSection:
    """One input section: its output section, its name, its address, its length and its file (THE FILE OF AN ITEM).

    `length` counts the bytes of the section.  A discarded section has an empty `output`.
    """

    output: str
    name: str
    address: int
    length: int
    file: str


@dataclass(frozen=True, slots=True)
class LinkMap:
    """The parts of one map that the module reads (THE MAP), each in the sequence of the map."""

    members: tuple[Inclusion, ...]
    libraries: tuple[Inclusion, ...]
    discarded: tuple[InputSection, ...]
    loads: tuple[str, ...]
    sections: tuple[InputSection, ...]


def split_member(text: str) -> tuple[str, str]:
    """Return the archive and the member of ARCHIVE(MEMBER), or an empty archive and the text for a plain path."""
    if text.endswith(")") and "(" in text:
        archive, _, member = text[:-1].rpartition("(")
        return archive, member
    return "", text


def is_hex(token: str) -> bool:
    """Return True for a number in hexadecimal with the prefix 0x (THE NAMES)."""
    return token.startswith(HEX_PREFIX) and len(token) > len(HEX_PREFIX) and set(token[len(HEX_PREFIX) :]) <= HEX_DIGITS


def reference_of(text: str, where: str, line: int) -> tuple[str, str]:
    """Return the file and the symbol of the text "FILE (SYMBOL)".

    Raises:
        LinkMapError: If the text does not have that shape
    """
    referrer, mark, rest = text.partition(REFERENCE_MARK)
    if not mark or not referrer or not rest.endswith(")"):
        raise LinkMapError(f"{where}:{line}: the reference {text!r} does not have the shape 'FILE (SYMBOL)'")
    return referrer, rest[:-1]


def section_of(output: str, name: str, words: list[str], where: str, line: int) -> InputSection:
    """Return the input section of a name and the words "ADDRESS LENGTH FILE" after it.

    Raises:
        LinkMapError: If the words do not start with two hexadecimal numbers and a file
    """
    if len(words) < 3 or not is_hex(words[0]) or not is_hex(words[1]):
        raise LinkMapError(f"{where}:{line}: the input section {name} has no 'ADDRESS LENGTH FILE' after it")
    return InputSection(output, name, int(words[0], 16), int(words[1], 16), " ".join(words[2:]))


class Reader:
    """The state of one read: the part, the item that waits for its next line, and the found items.

    `waiting` holds the name of an archive member, of a library or of an input section when its next line holds its
    address and its file (THE LINES OF AN ITEM).
    """

    __slots__ = ("discarded", "libraries", "loads", "members", "output", "part", "sections", "waiting", "where")

    def __init__(self, where: str) -> None:
        """Start a read with no part."""
        self.where = where
        self.part = ""
        self.output = ""
        self.waiting = ""
        self.members: list[Inclusion] = []
        self.libraries: list[Inclusion] = []
        self.discarded: list[InputSection] = []
        self.loads: list[str] = []
        self.sections: list[InputSection] = []

    def read_inclusion(self, text: str, line: int) -> None:
        """Read one line of the archive members or of the shared libraries.

        Raises:
            LinkMapError: If a reference line has no item before it, or a reference does not have its shape
        """
        if not text.strip():
            return
        if text[0].isspace():
            if not self.waiting:
                raise LinkMapError(f"{self.where}:{line}: the reference {text.strip()!r} follows no item")
            item, rest = self.waiting, text.strip()
        else:
            item, _, rest = text.partition(" ")
            rest = rest.strip()
            if not rest:
                self.waiting = item
                return
        self.waiting = ""
        referrer, symbol = reference_of(rest, self.where, line)
        archive, member = split_member(item)
        target = self.members if self.part == INCLUDED else self.libraries
        target.append(Inclusion(archive, member, referrer, symbol))

    def read_section(self, text: str, line: int, output: str) -> InputSection | None:
        """Read one line that can name an input section, and give the section when the line completes one.

        Raises:
            LinkMapError: If a line completes a section and does not have the shape of THE LINES OF AN ITEM
        """
        words = text.split()
        if not words:
            return None
        if text.startswith("  "):
            name, self.waiting = self.waiting, ""
            return section_of(output, name, words, self.where, line) if name else None
        self.waiting = ""
        if words[0].startswith(PATTERN_MARK):
            return None
        if len(words) == 1:
            self.waiting = words[0]
            return None
        return section_of(output, words[0], words[1:], self.where, line)

    def read_script(self, text: str, line: int) -> None:
        """Read one line of the script and the memory map.

        Raises:
            LinkMapError: If an input section does not have its shape
        """
        if text and not text[0].isspace():
            self.waiting = ""
            words = text.split()
            if words[0] == LOAD and len(words) > 1:
                self.loads.append(text.partition(" ")[2].strip())
            elif text.strip() not in SCRIPT_MARKS and not text.startswith(OUTPUT_MARK):
                self.output = words[0]
            return
        section = self.read_section(text, line, self.output)
        if section is not None:
            self.sections.append(section)

    def read_line(self, text: str, line: int) -> bool:
        """Read one line of the map, and return False at the table of the references, where the read stops.

        Raises:
            LinkMapError: If the line does not have the shape of its part
        """
        heading = text.rstrip()
        if heading in HEADINGS:
            self.part, self.waiting, self.output = heading, "", ""
            return heading != CROSS_REFERENCES
        if self.part in (INCLUDED, AS_NEEDED):
            self.read_inclusion(text, line)
        elif self.part == DISCARDED:
            section = self.read_section(text, line, "")
            if section is not None:
                self.discarded.append(section)
        elif self.part == SCRIPT:
            self.read_script(text, line)
        return True

    def result(self) -> LinkMap:
        """Return the found items.

        Raises:
            LinkMapError: If an item waits for a next line at the end of the map
        """
        if self.waiting:
            raise LinkMapError(f"{self.where}: the item {self.waiting} at the end of the map has no next line")
        return LinkMap(
            tuple(self.members), tuple(self.libraries), tuple(self.discarded), tuple(self.loads), tuple(self.sections)
        )


def parse(text: str, where: str) -> LinkMap:
    """Read the text of a map (THE MAP).  Complexity: linear in the length of the text.

    Raises:
        LinkMapError: If a line does not have the shape of its part, or the map has no heading of the script
    """
    reader = Reader(where)
    has_script = False
    for line, row in enumerate(text.splitlines(), start=1):
        has_script = has_script or row.rstrip() == SCRIPT
        if not reader.read_line(row, line):
            break
    if not has_script:
        raise LinkMapError(f"{where}: the map has no heading {SCRIPT!r}, so it is not a map that GNU ld writes")
    return reader.result()


def read(path: Path) -> LinkMap:
    """Read the map file of a link.

    Raises:
        LinkMapError: If the map does not have the shape that the module reads
        OSError: If the file cannot be read
    """
    return parse(path.read_text(encoding="utf-8", errors="surrogateescape"), str(path))
