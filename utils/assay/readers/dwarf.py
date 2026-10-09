"""dwarf — the one reader of DWARF 5 in the package.

THE SECTIONS
    The module reads the debug sections of one ELF file of assay.readers.elf:
    .debug_info, .debug_abbrev, .debug_str, .debug_line_str, .debug_line and
    .debug_rnglists.  A file has one section of each name at the maximum.  An
    object of the type ET_REL holds a relocation for each offset and each
    address in these sections (THE RELOCATIONS).  A linked file holds each
    value with no relocation.

THE UNITS
    units() gives each unit of .debug_info.  The reader reads DWARF 5 only,
    and a unit of a different version is a DwarfError.  The unit header gives
    the 32-bit or the 64-bit format, the unit type, the bytes of an address
    and the offset of the abbreviations.  dies() of a unit gives each entry of
    the unit in its sequence, with its depth and the index of its parent.

THE ENCODINGS
    The reader decodes each encoding DW_FORM of DWARF 5 and four GNU
    encodings (FORM_NAMES).  A reference becomes an offset in .debug_info.  A
    string of the encoding string, strp or line_strp becomes its text.  An
    address becomes an Address: its value, and the section of the symbol of
    its relocation.  An index encoding (strx, addrx, loclistx and rnglistx)
    keeps its index, and a string of such an encoding is an IndexedString.  A
    range list of such an encoding is a DwarfError, because the reader does
    not read .debug_addr or the bases of a unit.

THE FILES
    files() of a unit reads the header of the line program that the
    attribute DW_AT_stmt_list of its first entry names.  It gives the path of
    each file name entry, in the sequence of the header.  DW_AT_decl_file
    names an entry by that index.  A relative name gets its directory, and a
    relative directory gets the directory 0, the directory of the compile.

THE RANGES
    code_range() gives the bytes of the code of an entry and the section of
    the code.  DW_AT_high_pc is a length, or an address after DW_AT_low_pc.
    A range list of .debug_rnglists gives the sum of its ranges.  Its section
    is the section of its first address with a relocation, else the section
    of the base address of the unit.  A linked file has no relocation, and
    the section of each code range is NO_SECTION.

THE RELOCATIONS
    The value at an offset with a relocation is S + A: the value of the
    symbol and the addend.  An entry of a table SHT_REL has no addend, and it
    adds the stored value.  The reader applies only an absolute relocation
    of the width of the value (ABSOLUTE_KINDS), and only at an offset that it
    reads.  A relocation of a different kind at such an offset is a
    DwarfError.  A relocation in a location expression, such as the offset
    of a thread-local variable, stays unread.

THE CODE BODIES
    code_bodies() gives a Body for each concrete function and each inlined
    body with code.  A concrete function is an entry DW_TAG_subprogram with a
    range.  An inlined body is an entry DW_TAG_inlined_subroutine with a
    range.  The exclusive bytes of a body are its bytes less the bytes of
    each inlined body that is nearest in it.  The sum of the exclusive bytes
    then counts each byte of code one time.  A concrete function can be an
    entry in a different function.  Its code is not in the range of that
    function, and it does not decrease the exclusive bytes of that function.

    The source entries of a body give its name and its declaring file.  The
    reader follows DW_AT_abstract_origin and DW_AT_specification for
    ORIGIN_STEPS steps at the maximum.  The name is the first
    DW_AT_linkage_name, else the first DW_AT_name.  The file is the first
    DW_AT_decl_file, else the DW_AT_decl_file of the nearest parent of the
    last source entry, such as the class of a lambda.  An inlined body with no
    section of its own gets the section of the body that holds it.

`guard selftest tests.readers.test_dwarf` does the test of the module.
"""

import posixpath
import struct
from collections.abc import Callable

from assay.readers import elf

INFO = ".debug_info"
ABBREV = ".debug_abbrev"
STRINGS = ".debug_str"
LINE_STRINGS = ".debug_line_str"
LINE = ".debug_line"
RANGE_LISTS = ".debug_rnglists"
SECTIONS = (INFO, ABBREV, STRINGS, LINE_STRINGS, LINE, RANGE_LISTS)
VERSION = 5
# The unit length that starts the 64-bit format.
FORMAT_64 = 0xFFFFFFFF
# The unit types of DWARF 5.  A unit of the first set has an identifier of 8 bytes after the offset of its
# abbreviations.  A type unit has a signature of 8 bytes and the offset of its type there.
SIGNATURE_UNITS = frozenset({4, 5})
TYPE_UNITS = frozenset({2, 6})
TAG_INLINED = 0x1D
TAG_SUBPROGRAM = 0x2E
CODE_TAGS = frozenset({TAG_INLINED, TAG_SUBPROGRAM})
AT_NAME = 0x03
AT_STMT_LIST = 0x10
AT_LOW_PC = 0x11
AT_HIGH_PC = 0x12
AT_ABSTRACT_ORIGIN = 0x31
AT_ARTIFICIAL = 0x34
AT_DECL_FILE = 0x3A
AT_SPECIFICATION = 0x47
AT_RANGES = 0x55
AT_LINKAGE_NAME = 0x6E
AT_MIPS_LINKAGE_NAME = 0x2007
ORIGIN_ATTRIBUTES = (AT_ABSTRACT_ORIGIN, AT_SPECIFICATION)
NAME_ATTRIBUTES = (AT_LINKAGE_NAME, AT_MIPS_LINKAGE_NAME, AT_NAME)
# The steps of a chain of source entries that THE CODE BODIES follows.
ORIGIN_STEPS = 8
FORM_ADDR = 0x01
FORM_BLOCK2 = 0x03
FORM_BLOCK4 = 0x04
FORM_DATA2 = 0x05
FORM_DATA4 = 0x06
FORM_DATA8 = 0x07
FORM_STRING = 0x08
FORM_BLOCK = 0x09
FORM_BLOCK1 = 0x0A
FORM_DATA1 = 0x0B
FORM_FLAG = 0x0C
FORM_SDATA = 0x0D
FORM_STRP = 0x0E
FORM_UDATA = 0x0F
FORM_REF_ADDR = 0x10
FORM_REF1 = 0x11
FORM_REF2 = 0x12
FORM_REF4 = 0x13
FORM_REF8 = 0x14
FORM_REF_UDATA = 0x15
FORM_INDIRECT = 0x16
FORM_SEC_OFFSET = 0x17
FORM_EXPRLOC = 0x18
FORM_FLAG_PRESENT = 0x19
FORM_STRX = 0x1A
FORM_ADDRX = 0x1B
FORM_REF_SUP4 = 0x1C
FORM_STRP_SUP = 0x1D
FORM_DATA16 = 0x1E
FORM_LINE_STRP = 0x1F
FORM_REF_SIG8 = 0x20
FORM_IMPLICIT_CONST = 0x21
FORM_LOCLISTX = 0x22
FORM_RNGLISTX = 0x23
FORM_REF_SUP8 = 0x24
FORM_STRX1 = 0x25
FORM_STRX2 = 0x26
FORM_STRX3 = 0x27
FORM_STRX4 = 0x28
FORM_ADDRX1 = 0x29
FORM_ADDRX2 = 0x2A
FORM_ADDRX3 = 0x2B
FORM_ADDRX4 = 0x2C
FORM_GNU_ADDR_INDEX = 0x1F01
FORM_GNU_STR_INDEX = 0x1F02
FORM_GNU_REF_ALT = 0x1F20
FORM_GNU_STRP_ALT = 0x1F21
# The name of each encoding that the reader decodes (THE ENCODINGS).
FORM_NAMES = {
    FORM_ADDR: "addr",
    FORM_BLOCK2: "block2",
    FORM_BLOCK4: "block4",
    FORM_DATA2: "data2",
    FORM_DATA4: "data4",
    FORM_DATA8: "data8",
    FORM_STRING: "string",
    FORM_BLOCK: "block",
    FORM_BLOCK1: "block1",
    FORM_DATA1: "data1",
    FORM_FLAG: "flag",
    FORM_SDATA: "sdata",
    FORM_STRP: "strp",
    FORM_UDATA: "udata",
    FORM_REF_ADDR: "ref_addr",
    FORM_REF1: "ref1",
    FORM_REF2: "ref2",
    FORM_REF4: "ref4",
    FORM_REF8: "ref8",
    FORM_REF_UDATA: "ref_udata",
    FORM_INDIRECT: "indirect",
    FORM_SEC_OFFSET: "sec_offset",
    FORM_EXPRLOC: "exprloc",
    FORM_FLAG_PRESENT: "flag_present",
    FORM_STRX: "strx",
    FORM_ADDRX: "addrx",
    FORM_REF_SUP4: "ref_sup4",
    FORM_STRP_SUP: "strp_sup",
    FORM_DATA16: "data16",
    FORM_LINE_STRP: "line_strp",
    FORM_REF_SIG8: "ref_sig8",
    FORM_IMPLICIT_CONST: "implicit_const",
    FORM_LOCLISTX: "loclistx",
    FORM_RNGLISTX: "rnglistx",
    FORM_REF_SUP8: "ref_sup8",
    FORM_STRX1: "strx1",
    FORM_STRX2: "strx2",
    FORM_STRX3: "strx3",
    FORM_STRX4: "strx4",
    FORM_ADDRX1: "addrx1",
    FORM_ADDRX2: "addrx2",
    FORM_ADDRX3: "addrx3",
    FORM_ADDRX4: "addrx4",
    FORM_GNU_ADDR_INDEX: "GNU_addr_index",
    FORM_GNU_STR_INDEX: "GNU_str_index",
    FORM_GNU_REF_ALT: "GNU_ref_alt",
    FORM_GNU_STRP_ALT: "GNU_strp_alt",
}
# The bytes of each encoding with a constant width that holds a number, a flag or an index.
FIXED_WIDTHS = {
    FORM_DATA1: 1,
    FORM_DATA2: 2,
    FORM_DATA4: 4,
    FORM_DATA8: 8,
    FORM_DATA16: 16,
    FORM_FLAG: 1,
    FORM_REF_SUP4: 4,
    FORM_REF_SIG8: 8,
    FORM_REF_SUP8: 8,
    FORM_STRX1: 1,
    FORM_STRX2: 2,
    FORM_STRX3: 3,
    FORM_STRX4: 4,
    FORM_ADDRX1: 1,
    FORM_ADDRX2: 2,
    FORM_ADDRX3: 3,
    FORM_ADDRX4: 4,
}
# The bytes of each encoding of a reference in its unit.
UNIT_REFERENCES = {FORM_REF1: 1, FORM_REF2: 2, FORM_REF4: 4, FORM_REF8: 8}
# The encodings that hold an offset of the width of the format: 4 bytes, or 8 bytes in the 64-bit format.
OFFSET_FORMS = frozenset({FORM_SEC_OFFSET, FORM_REF_ADDR, FORM_STRP_SUP, FORM_GNU_REF_ALT, FORM_GNU_STRP_ALT})
# The encodings with a ULEB128 number: an index, or a plain number.
ULEB_FORMS = frozenset({FORM_UDATA, FORM_LOCLISTX, FORM_RNGLISTX, FORM_ADDRX, FORM_GNU_ADDR_INDEX})
STRING_INDEXES = frozenset({FORM_STRX, FORM_STRX1, FORM_STRX2, FORM_STRX3, FORM_STRX4, FORM_GNU_STR_INDEX})
# The bytes of the length of each block encoding, with 0 for a ULEB128 length.
BLOCK_LENGTHS = {FORM_BLOCK1: 1, FORM_BLOCK2: 2, FORM_BLOCK4: 4, FORM_BLOCK: 0, FORM_EXPRLOC: 0}
# The kinds of an absolute relocation of each machine, with the bytes of its value (THE RELOCATIONS).
ABSOLUTE_KINDS = {(elf.EM_X86_64, 1): 8, (elf.EM_X86_64, 10): 4, (elf.EM_AARCH64, 257): 8, (elf.EM_AARCH64, 258): 4}
# The content types of an entry of the header of a line program.
LNCT_PATH = 1
LNCT_DIRECTORY_INDEX = 2
# The kinds of an entry of a range list in .debug_rnglists.
RLE_END_OF_LIST = 0
RLE_INDEX_KINDS = frozenset({1, 2, 3})
RLE_OFFSET_PAIR = 4
RLE_BASE_ADDRESS = 5
RLE_START_END = 6
RLE_START_LENGTH = 7
# The section of a code range with no relocation (THE RANGES).
NO_SECTION = -1


class DwarfError(ValueError):
    """The debug sections do not have a shape that the module reads, with the file and the offset of the problem."""


class Address(elf.Record):
    """A value of the encoding addr: the address, and the section of the symbol of its relocation, or NO_SECTION."""

    __slots__ = ("section", "value")

    def __init__(self, value: int, section: int) -> None:
        """Hold the parts of one address."""
        self.value = value
        self.section = section


class IndexedString(elf.Record):
    """A string of an index encoding, with its index (THE ENCODINGS)."""

    __slots__ = ("index",)

    def __init__(self, index: int) -> None:
        """Hold the index of the string."""
        self.index = index


type Value = int | str | bytes | bool | Address | IndexedString


class Die(elf.Record):
    """One entry of a unit: its offset in .debug_info, its tag, its depth, its parent and its attributes.

    `parent` is the index of the parent in the entries of the unit, or -1 for the first entry.  `attributes` holds
    each attribute, its encoding and its value, in the sequence of the abbreviation.
    """

    __slots__ = ("attributes", "depth", "offset", "parent", "tag")

    def __init__(
        self, offset: int, tag: int, depth: int, parent: int, attributes: tuple[tuple[int, int, Value], ...]
    ) -> None:
        """Hold the parts of one entry."""
        self.offset = offset
        self.tag = tag
        self.depth = depth
        self.parent = parent
        self.attributes = attributes

    def get(self, attribute: int) -> Value | None:
        """Return the value of an attribute, or None.  Complexity: linear in the attributes of the entry."""
        for name, _, value in self.attributes:
            if name == attribute:
                return value
        return None

    def encoding(self, attribute: int) -> int:
        """Return the encoding of an attribute, or 0 when the entry has no such attribute."""
        for name, encoding, _ in self.attributes:
            if name == attribute:
                return encoding
        return 0

    def number(self, attribute: int) -> int | None:
        """Return the value of an attribute when it is a number, else None."""
        value = self.get(attribute)
        return value if isinstance(value, int) and not isinstance(value, bool) else None


class Body(elf.Record):
    """One concrete function or inlined body with code (THE CODE BODIES).

    `name` and `file` come from the source entries, with an empty text when they have none.  `section` is the index of
    the section of the code in the ELF file, or NO_SECTION.
    """

    __slots__ = ("exclusive", "file", "is_artificial", "is_inlined", "length", "name", "section")

    def __init__(
        self, name: str, file: str, is_inlined: bool, is_artificial: bool, length: int, exclusive: int, section: int
    ) -> None:
        """Hold the parts of one body."""
        self.name = name
        self.file = file
        self.is_inlined = is_inlined
        self.is_artificial = is_artificial
        self.length = length
        self.exclusive = exclusive
        self.section = section


class Abbreviation:
    """One abbreviation: its tag, its flag of children, and the attribute, the encoding and the constant of each spec."""

    __slots__ = ("has_children", "specs", "tag")

    def __init__(self, tag: int, has_children: bool, specs: tuple[tuple[int, int, int], ...]) -> None:
        """Hold the parts of one abbreviation."""
        self.tag = tag
        self.has_children = has_children
        self.specs = specs


def uleb(data: bytes, position: int) -> tuple[int, int]:
    """Return an unsigned LEB128 number at a position, and the position after it.

    Raises:
        IndexError: If the number does not end in the data
    """
    result = shift = 0
    while True:
        byte = data[position]
        position += 1
        result |= (byte & 0x7F) << shift
        if byte < 0x80:
            return result, position
        shift += 7


def sleb(data: bytes, position: int) -> tuple[int, int]:
    """Return a signed LEB128 number at a position, and the position after it.

    Raises:
        IndexError: If the number does not end in the data
    """
    result = shift = 0
    while True:
        byte = data[position]
        position += 1
        result |= (byte & 0x7F) << shift
        shift += 7
        if byte < 0x80:
            return (result - (1 << shift) if byte & 0x40 else result), position


def text_at(data: bytes, position: int) -> tuple[str, int]:
    """Return the string at a position up to its zero byte, and the position after the zero byte.

    Raises:
        IndexError: If no zero byte comes before the end of the data
    """
    stop = data.find(b"\0", position)
    if stop < 0:
        raise IndexError(position)
    return data[position:stop].decode("utf-8", "surrogateescape"), stop + 1


def fixed(data: bytes, position: int, size: int) -> int:
    """Return the number of `size` bytes at a position, with the low byte first.

    Raises:
        IndexError: If the number is not fully in the data
    """
    if position + size > len(data):
        raise IndexError(position)
    return int.from_bytes(data[position : position + size], "little")


class Relocated:
    """The bytes of one debug section, with the relocation at each offset that has one (THE RELOCATIONS)."""

    __slots__ = ("data", "entries", "is_rel", "machine", "name", "symbols", "where")

    def __init__(
        self,
        member: elf.Member,
        name: str,
        data: bytes,
        entries: dict[int, elf.Relocation],
        is_rel: bool,
    ) -> None:
        """Hold the bytes, the relocation of each offset and the symbols of the file."""
        self.where = member.where
        self.machine = member.machine()
        self.symbols = member.symbols() if entries else ()
        self.name = name
        self.data = data
        self.entries = entries
        self.is_rel = is_rel

    def word(self, position: int, size: int) -> int:
        """Return the number of `size` bytes at a position, with the relocation at that position.

        Raises:
            DwarfError: If the relocation at the position is not an absolute relocation of `size` bytes
            IndexError: If the number is not fully in the section
        """
        stored = fixed(self.data, position, size)
        entry = self.entries.get(position)
        if entry is None:
            return stored
        if ABSOLUTE_KINDS.get((self.machine, entry.kind)) != size:
            raise DwarfError(
                f"{self.where}: the relocation at {self.name}+{position} has the kind {entry.kind}, and a value of "
                f"{size} bytes takes an absolute relocation of {size} bytes"
            )
        base = self.symbols[entry.symbol_index].value if entry.symbol_index < len(self.symbols) else 0
        return (base + entry.addend + (stored if self.is_rel else 0)) & ((1 << (8 * size)) - 1)

    def section_at(self, position: int) -> int:
        """Return the section of the symbol of the relocation at a position, or NO_SECTION."""
        entry = self.entries.get(position)
        if entry is None or entry.symbol_index >= len(self.symbols):
            return NO_SECTION
        return self.symbols[entry.symbol_index].section


class Unit:
    """One unit of .debug_info: its header, its entries and its file names (THE UNITS)."""

    __slots__ = (
        "_dies",
        "_files",
        "abbrev_offset",
        "address_size",
        "dwarf",
        "end",
        "first",
        "info",
        "offset",
        "offset_size",
        "unit_type",
    )

    def __init__(self, dwarf: Dwarf, offset: int) -> None:
        """Read the header of the unit at an offset of .debug_info.

        Raises:
            DwarfError: If the header is cut, or the unit is not DWARF 5
        """
        self.dwarf = dwarf
        self.info = dwarf.section(INFO)
        self.offset = offset
        data = self.info.data
        try:
            length = fixed(data, offset, 4)
            position = offset + 4
            self.offset_size = 4
            if length == FORMAT_64:
                length = fixed(data, position, 8)
                position += 8
                self.offset_size = 8
            self.end = position + length
            version, self.unit_type, self.address_size = struct.unpack_from("<HBB", data, position)
            position += 4
            self.abbrev_offset = self.info.word(position, self.offset_size)
        except IndexError, struct.error:
            raise DwarfError(f"{dwarf.where}: the header of the unit at {INFO}+{offset} is cut") from None
        if version != VERSION:
            raise DwarfError(
                f"{dwarf.where}: the unit at {INFO}+{offset} has the DWARF version {version}, and the reader reads "
                f"only version {VERSION}"
            )
        if self.end > len(data):
            raise DwarfError(f"{dwarf.where}: the unit at {INFO}+{offset} ends after the section")
        position += self.offset_size
        if self.unit_type in SIGNATURE_UNITS:
            position += 8
        elif self.unit_type in TYPE_UNITS:
            position += 8 + self.offset_size
        self.first = position
        self._dies: tuple[Die, ...] | None = None
        self._files: tuple[str, ...] | None = None

    def dies(self) -> tuple[Die, ...]:
        """Return each entry of the unit, in its sequence (THE UNITS).  Complexity: linear in the bytes of the unit.

        Raises:
            DwarfError: If an entry is cut, names an abbreviation that the table does not hold, or has an unknown
                encoding
        """
        if self._dies is None:
            try:
                self._dies = self.read_dies()
            except IndexError:
                raise DwarfError(f"{self.dwarf.where}: an entry of the unit at {INFO}+{self.offset} is cut") from None
        return self._dies

    def read_dies(self) -> tuple[Die, ...]:
        """Read each entry of the unit.

        Raises:
            DwarfError: If an entry names an abbreviation that the table does not hold, or has an unknown encoding
            IndexError: If an entry is cut
        """
        abbreviations = self.dwarf.abbreviations(self.abbrev_offset)
        data = self.info.data
        found: list[Die] = []
        parents: list[int] = []
        position = self.first
        while position < self.end:
            offset = position
            code, position = uleb(data, position)
            if code == 0:
                if parents:
                    parents.pop()
                continue
            abbreviation = abbreviations.get(code)
            if abbreviation is None:
                raise DwarfError(
                    f"{self.dwarf.where}: the entry at {INFO}+{offset} names the abbreviation {code}, which the table "
                    f"at {ABBREV}+{self.abbrev_offset} does not hold"
                )
            attributes: list[tuple[int, int, Value]] = []
            for attribute, form, constant in abbreviation.specs:
                value, position, actual = self.value(form, constant, position)
                attributes.append((attribute, actual, value))
            found.append(Die(offset, abbreviation.tag, len(parents), parents[-1] if parents else -1, tuple(attributes)))
            if abbreviation.has_children:
                parents.append(len(found) - 1)
        if position != self.end:
            raise DwarfError(f"{self.dwarf.where}: the last entry of the unit at {INFO}+{self.offset} is cut")
        return tuple(found)

    def value(self, form: int, constant: int, position: int) -> tuple[Value, int, int]:
        """Return the value of an encoding at a position, the position after it, and the encoding after DW_FORM_indirect.

        Raises:
            DwarfError: If the encoding is unknown, or a relocation of the value is not absolute of its width
            IndexError: If the value is cut
        """
        info, data = self.info, self.info.data
        if form in UNIT_REFERENCES:
            size = UNIT_REFERENCES[form]
            return self.offset + info.word(position, size), position + size, form
        if form in FIXED_WIDTHS:
            size = FIXED_WIDTHS[form]
            if form == FORM_DATA16:
                fixed(data, position, size)
                return bytes(data[position : position + size]), position + size, form
            number = fixed(data, position, size)
            if form in STRING_INDEXES:
                return IndexedString(number), position + size, form
            return (bool(number) if form == FORM_FLAG else number), position + size, form
        if form == FORM_FLAG_PRESENT:
            return True, position, form
        if form == FORM_IMPLICIT_CONST:
            return constant, position, form
        if form in (FORM_STRP, FORM_LINE_STRP):
            name = LINE_STRINGS if form == FORM_LINE_STRP else STRINGS
            text = self.dwarf.string(name, info.word(position, self.offset_size))
            return text, position + self.offset_size, form
        if form == FORM_STRING:
            text, after = text_at(data, position)
            return text, after, form
        if form == FORM_ADDR:
            address = Address(info.word(position, self.address_size), info.section_at(position))
            return address, position + self.address_size, form
        if form in OFFSET_FORMS:
            return info.word(position, self.offset_size), position + self.offset_size, form
        return self.variable_value(form, constant, position)

    def variable_value(self, form: int, constant: int, position: int) -> tuple[Value, int, int]:
        """Return the value of an encoding of a variable width: a block, a LEB128 number or DW_FORM_indirect.

        Raises:
            DwarfError: If the encoding is unknown
            IndexError: If the value is cut
        """
        data = self.info.data
        if form in BLOCK_LENGTHS:
            size = BLOCK_LENGTHS[form]
            if size:
                length = fixed(data, position, size)
                position += size
            else:
                length, position = uleb(data, position)
            if position + length > len(data):
                raise IndexError(position)
            return bytes(data[position : position + length]), position + length, form
        if form in ULEB_FORMS or form in STRING_INDEXES:
            number, after = uleb(data, position)
            return (IndexedString(number) if form in STRING_INDEXES else number), after, form
        if form == FORM_SDATA:
            number, after = sleb(data, position)
            return number, after, form
        if form == FORM_REF_UDATA:
            number, after = uleb(data, position)
            return self.offset + number, after, form
        if form == FORM_INDIRECT:
            actual, after = uleb(data, position)
            return self.value(actual, constant, after)
        raise DwarfError(
            f"{self.dwarf.where}: an entry of the unit at {INFO}+{self.offset} has the unknown encoding {form}"
        )

    def files(self) -> tuple[str, ...]:
        """Return the path of each file name entry of the line program of the unit (THE FILES).

        A unit with no DW_AT_stmt_list has no file.  Complexity: linear in the bytes of the header.

        Raises:
            DwarfError: If the header is cut, is not DWARF 5, or has an encoding that a header cannot have
        """
        if self._files is None:
            dies = self.dies()
            offset = dies[0].number(AT_STMT_LIST) if dies else None
            self._files = () if offset is None else self.dwarf.line_files(offset)
        return self._files

    def file_of(self, die: Die) -> str:
        """Return the declaring file of an entry, or an empty text (THE FILES).

        Raises:
            DwarfError: If the header of the line program cannot be read
        """
        index = die.number(AT_DECL_FILE)
        if index is None:
            return ""
        files = self.files()
        return files[index] if 0 <= index < len(files) else ""

    def code_range(self, die: Die) -> tuple[int, int]:
        """Return the bytes of the code of an entry and the section of the code (THE RANGES).

        An entry with no range gives 0 bytes.

        Raises:
            DwarfError: If a range list is cut, has an index encoding, or has an unknown kind of entry
        """
        low, high = die.get(AT_LOW_PC), die.get(AT_HIGH_PC)
        if isinstance(low, Address) and high is not None:
            if isinstance(high, Address):
                return max(0, high.value - low.value), low.section
            return (high, low.section) if isinstance(high, int) else (0, NO_SECTION)
        if die.get(AT_RANGES) is None:
            return 0, NO_SECTION
        offset = die.number(AT_RANGES)
        if die.encoding(AT_RANGES) == FORM_RNGLISTX or offset is None:
            raise DwarfError(
                f"{self.dwarf.where}: the entry at {INFO}+{die.offset} has a range list of an index encoding, and the "
                f"reader does not read the bases of a unit"
            )
        dies = self.dies()
        base = dies[0].get(AT_LOW_PC) if dies else None
        return self.dwarf.range_list(offset, self.address_size, base if isinstance(base, Address) else None)


class LineHeader:
    """The read of the header of one line program, up to its file name entries (THE FILES)."""

    __slots__ = ("dwarf", "line", "offset_size", "position")

    def __init__(self, dwarf: Dwarf, offset: int) -> None:
        """Read the fields of the header before the directory entries.

        Raises:
            DwarfError: If the header is not DWARF 5
            IndexError: If the header is cut
        """
        self.dwarf = dwarf
        self.line = dwarf.section(LINE)
        data = self.line.data
        position = offset + 4
        self.offset_size = 4
        if fixed(data, offset, 4) == FORMAT_64:
            position += 8
            self.offset_size = 8
        version = fixed(data, position, 2)
        if version != VERSION:
            raise DwarfError(
                f"{dwarf.where}: the line program at {LINE}+{offset} has the version {version}, and the reader reads "
                f"only version {VERSION}"
            )
        # The version, the bytes of an address, the bytes of a segment selector and the length of the header come
        # first.  Then come five fields of one byte, and opcode_base.  The lengths of the standard opcodes follow it.
        position += 4 + self.offset_size + 5
        opcode_base = data[position]
        self.position = position + 1 + max(0, opcode_base - 1)

    def entries(self) -> list[dict[int, Value]]:
        """Read one table of entries: the format, the count and the entries, each by its content type.

        Raises:
            DwarfError: If an entry has an encoding that a header cannot have
            IndexError: If the table is cut
        """
        data = self.line.data
        count = data[self.position]
        self.position += 1
        formats: list[tuple[int, int]] = []
        for _ in range(count):
            content, self.position = uleb(data, self.position)
            form, self.position = uleb(data, self.position)
            formats.append((content, form))
        number, self.position = uleb(data, self.position)
        return [{content: self.value(form) for content, form in formats} for _ in range(number)]

    def value(self, form: int) -> Value:
        """Read one value of a directory entry or a file name entry.

        Raises:
            DwarfError: If the encoding is not an encoding that a header can have
            IndexError: If the value is cut
        """
        data = self.line.data
        if form in (FORM_LINE_STRP, FORM_STRP):
            offset = self.line.word(self.position, self.offset_size)
            self.position += self.offset_size
            return self.dwarf.string(LINE_STRINGS if form == FORM_LINE_STRP else STRINGS, offset)
        if form == FORM_STRING:
            text, self.position = text_at(data, self.position)
            return text
        if form == FORM_UDATA:
            number, self.position = uleb(data, self.position)
            return number
        if form in FIXED_WIDTHS:
            size = FIXED_WIDTHS[form]
            number = fixed(data, self.position, size)
            self.position += size
            return number
        if form == FORM_BLOCK:
            length, self.position = uleb(data, self.position)
            if self.position + length > len(data):
                raise IndexError(self.position)
            block = bytes(data[self.position : self.position + length])
            self.position += length
            return block
        raise DwarfError(f"{self.dwarf.where}: an entry of the header of a line program has the encoding {form}")

    def files(self) -> tuple[str, ...]:
        """Return the path of each file name entry, with its directory (THE FILES).

        Raises:
            DwarfError: If an entry has an encoding that a header cannot have
            IndexError: If the header is cut
        """
        directories = [str(entry.get(LNCT_PATH, "")) for entry in self.entries()]
        compile_directory = directories[0] if directories else ""
        found: list[str] = []
        for entry in self.entries():
            name = str(entry.get(LNCT_PATH, ""))
            index = entry.get(LNCT_DIRECTORY_INDEX, 0)
            directory = directories[index] if isinstance(index, int) and 0 <= index < len(directories) else ""
            if directory and not posixpath.isabs(directory) and compile_directory:
                directory = posixpath.join(compile_directory, directory)
            found.append(name if posixpath.isabs(name) or not directory else posixpath.join(directory, name))
        return tuple(found)


class Dwarf:
    """The debug sections of one ELF file, read on their first use (THE SECTIONS)."""

    __slots__ = ("_abbreviations", "_relocated", "_strings", "_units", "member", "where")

    def __init__(self, member: elf.Member) -> None:
        """Hold the file.

        Raises:
            DwarfError: If the file has more than one section of a name of SECTIONS
        """
        self.member = member
        self.where = member.where
        for name in SECTIONS:
            if len(member.named(name)) > 1:
                raise DwarfError(f"{self.where}: the file has more than one section {name}")
        self._relocated: dict[str, Relocated] | None = None
        self._abbreviations: dict[int, dict[int, Abbreviation]] = {}
        self._strings: dict[tuple[str, int], str] = {}
        self._units: tuple[Unit, ...] | None = None

    def relocated(self) -> dict[str, Relocated]:
        """Return each debug section of SECTIONS that the file has, with its relocations (THE RELOCATIONS).

        Raises:
            DwarfError: If a section or a relocation table is not fully in the file
        """
        if self._relocated is None:
            try:
                self._relocated = self.read_relocated()
            except elf.ElfError as problem:
                raise DwarfError(str(problem)) from None
        return self._relocated

    def read_relocated(self) -> dict[str, Relocated]:
        """Read each debug section of SECTIONS that the file has, and the relocation of each of its offsets.

        Raises:
            elf.ElfError: If a section or a relocation table is not fully in the file
        """
        present = {name: found[0] for name in SECTIONS if (found := self.member.named(name))}
        names = {section.index: name for name, section in present.items()}
        rel_targets = {section.info for section in self.member.sections() if section.kind == elf.SHT_REL}
        entries: dict[str, dict[int, elf.Relocation]] = {name: {} for name in present}
        if names:
            for item in self.member.relocations():
                if item.section_index in names:
                    entries[names[item.section_index]][item.offset] = item
        return {
            name: Relocated(self.member, name, self.member.data(section), entries[name], section.index in rel_targets)
            for name, section in present.items()
        }

    def section(self, name: str) -> Relocated:
        """Return a debug section of the file.

        Raises:
            DwarfError: If the file has no section of the name
        """
        found = self.relocated().get(name)
        if found is None:
            raise DwarfError(f"{self.where}: the file has no section {name}")
        return found

    def units(self) -> tuple[Unit, ...]:
        """Return each unit of .debug_info, or no unit for a file with no such section (THE UNITS).

        Raises:
            DwarfError: If a unit header is cut, or a unit is not DWARF 5
        """
        if self._units is None:
            found: list[Unit] = []
            if INFO in self.relocated():
                size = len(self.section(INFO).data)
                offset = 0
                while offset < size:
                    unit = Unit(self, offset)
                    found.append(unit)
                    offset = unit.end
            self._units = tuple(found)
        return self._units

    def abbreviations(self, offset: int) -> dict[int, Abbreviation]:
        """Return the abbreviation table at an offset of .debug_abbrev, by the code of each abbreviation.

        Raises:
            DwarfError: If the table is cut
        """
        if offset not in self._abbreviations:
            try:
                self._abbreviations[offset] = self.read_abbreviations(offset)
            except IndexError:
                raise DwarfError(f"{self.where}: the abbreviation table at {ABBREV}+{offset} is cut") from None
        return self._abbreviations[offset]

    def read_abbreviations(self, offset: int) -> dict[int, Abbreviation]:
        """Read the abbreviation table at an offset of .debug_abbrev.

        Raises:
            DwarfError: If the file has no section .debug_abbrev
            IndexError: If the table is cut
        """
        data = self.section(ABBREV).data
        table: dict[int, Abbreviation] = {}
        position = offset
        while True:
            code, position = uleb(data, position)
            if code == 0:
                return table
            tag, position = uleb(data, position)
            has_children = data[position] != 0
            position += 1
            specs: list[tuple[int, int, int]] = []
            while True:
                attribute, position = uleb(data, position)
                form, position = uleb(data, position)
                if attribute == 0 and form == 0:
                    break
                constant = 0
                if form == FORM_IMPLICIT_CONST:
                    constant, position = sleb(data, position)
                specs.append((attribute, form, constant))
            table[code] = Abbreviation(tag, has_children, tuple(specs))

    def string(self, name: str, offset: int) -> str:
        """Return the string at an offset of .debug_str or .debug_line_str.

        Raises:
            DwarfError: If the file has no such section, or the offset is not in it
        """
        key = (name, offset)
        if key not in self._strings:
            data = self.section(name).data
            if offset >= len(data):
                raise DwarfError(f"{self.where}: the string offset {offset} is not in {name}")
            self._strings[key] = elf.name_at(data, offset)
        return self._strings[key]

    def line_files(self, offset: int) -> tuple[str, ...]:
        """Return the path of each file name entry of the header of the line program at an offset (THE FILES).

        Raises:
            DwarfError: If the header is cut, is not DWARF 5, or has an encoding that a header cannot have
        """
        try:
            return LineHeader(self, offset).files()
        except IndexError:
            raise DwarfError(f"{self.where}: the header of the line program at {LINE}+{offset} is cut") from None

    def range_list(self, offset: int, address_size: int, base: Address | None) -> tuple[int, int]:
        """Return the bytes of the ranges of the range list at an offset of .debug_rnglists, and their section.

        Raises:
            DwarfError: If the list is cut, has an index encoding, or has an unknown kind of entry
        """
        try:
            total, section = self.read_range_list(offset, address_size)
        except IndexError:
            raise DwarfError(f"{self.where}: the range list at {RANGE_LISTS}+{offset} is cut") from None
        if section == NO_SECTION and base is not None:
            section = base.section
        return total, section

    def read_range_list(self, offset: int, address_size: int) -> tuple[int, int]:
        """Read the range list at an offset of .debug_rnglists (THE RANGES).

        Raises:
            DwarfError: If the list has an index encoding or an unknown kind of entry
            IndexError: If the list is cut
        """
        ranges = self.section(RANGE_LISTS)
        data = ranges.data
        total, section, position = 0, NO_SECTION, offset
        while True:
            kind = data[position]
            position += 1
            if kind == RLE_END_OF_LIST:
                return total, section
            if kind in RLE_INDEX_KINDS:
                raise DwarfError(
                    f"{self.where}: the range list at {RANGE_LISTS}+{offset} has an entry of an index encoding, and the "
                    f"reader does not read .debug_addr"
                )
            if kind == RLE_OFFSET_PAIR:
                start, position = uleb(data, position)
                end, position = uleb(data, position)
                total += max(0, end - start)
                continue
            if kind not in (RLE_BASE_ADDRESS, RLE_START_END, RLE_START_LENGTH):
                raise DwarfError(f"{self.where}: the range list at {RANGE_LISTS}+{offset} has the kind {kind}")
            start = ranges.word(position, address_size)
            section = ranges.section_at(position) if section == NO_SECTION else section
            position += address_size
            if kind == RLE_START_END:
                total += max(0, ranges.word(position, address_size) - start)
                position += address_size
            elif kind == RLE_START_LENGTH:
                length, position = uleb(data, position)
                total += length


def source_of(unit: Unit, die: Die, lookup: Callable[[int], tuple[Unit, Die] | None]) -> tuple[str, str, bool]:
    """Return the name, the declaring file and the artificial flag of the source entries of an entry (THE CODE BODIES).

    Raises:
        DwarfError: If the header of a line program cannot be read
    """
    name, file, is_artificial = "", "", False
    current_unit, current = unit, die
    for _ in range(ORIGIN_STEPS):
        if not name:
            name = next((value for value in map(current.get, NAME_ATTRIBUTES) if isinstance(value, str)), "")
        file = file or current_unit.file_of(current)
        is_artificial = is_artificial or current.get(AT_ARTIFICIAL) is True
        following = next((offset for offset in map(current.number, ORIGIN_ATTRIBUTES) if offset is not None), None)
        found = lookup(following) if following is not None else None
        if found is None:
            break
        current_unit, current = found
    dies = current_unit.dies()
    parent = current.parent
    while not file and parent > 0:
        file = current_unit.file_of(dies[parent])
        parent = dies[parent].parent
    return name, file, is_artificial


def unit_bodies(unit: Unit, lookup: Callable[[int], tuple[Unit, Die] | None]) -> list[Body]:
    """Return the bodies of one unit, in the sequence of its entries (THE CODE BODIES).

    Raises:
        DwarfError: If an entry, a range list or the header of a line program cannot be read
    """
    found: list[tuple[str, str, bool, bool, int, int]] = []
    inner: list[int] = []
    # The depth, the index in `found` and the section of each code entry that holds the current entry.
    holders: list[tuple[int, int, int]] = []
    for die in unit.dies():
        while holders and holders[-1][0] >= die.depth:
            holders.pop()
        if die.tag not in CODE_TAGS:
            continue
        length, section = unit.code_range(die)
        if length <= 0:
            continue
        is_inlined = die.tag == TAG_INLINED
        if section == NO_SECTION and holders:
            section = holders[-1][2]
        if is_inlined and holders:
            inner[holders[-1][1]] += length
        name, file, is_artificial = source_of(unit, die, lookup)
        found.append((name, file, is_inlined, is_artificial, length, section))
        inner.append(0)
        holders.append((die.depth, len(found) - 1, section))
    return [
        Body(name, file, is_inlined, is_artificial, length, max(0, length - inside), section)
        for (name, file, is_inlined, is_artificial, length, section), inside in zip(found, inner, strict=True)
    ]


def code_bodies(member: elf.Member) -> tuple[Body, ...]:
    """Return each concrete function and each inlined body with code of an ELF file (THE CODE BODIES).

    A file with no .debug_info gives no body.  Complexity: linear in the entries of the units, times ORIGIN_STEPS.

    Raises:
        DwarfError: If the debug sections do not have a shape that the module reads
    """
    units = Dwarf(member).units()
    index: dict[int, tuple[Unit, int]] = {}
    for unit in units:
        for position, die in enumerate(unit.dies()):
            index[die.offset] = (unit, position)

    def lookup(offset: int) -> tuple[Unit, Die] | None:
        """Return the unit and the entry at an offset of .debug_info, or None."""
        found = index.get(offset)
        return (found[0], found[0].dies()[found[1]]) if found is not None else None

    return tuple(body for unit in units for body in unit_bodies(unit, lookup))
