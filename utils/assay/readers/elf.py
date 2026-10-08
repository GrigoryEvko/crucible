"""elf — the one reader of ELF files and of the ar archives that hold them.

THE READERS
    Each read of an ELF file or of an archive in the package goes through
    this module.  The rule structured-reads refuses each other read: a run of
    nm, readelf or c++filt, and the ELF magic, in a different module.  These
    modules use it:
      * The rules libstdcxx-symbols and no-throw-no-rtti read the symbols and
        the relocations of the members of the static libraries, and the
        dynamic symbols of libstdc++.so.
      * The rules function-size and object-text read the code sections and the
        function symbols of each object.
      * assay.readers.sections reads the section .crucible.quarantine of an
        object.
      * assay.readers.objdump gives the names of a disassembly to demangle().
      * loop test (assay.loop.affected) reads the data sections of a program.
      * loop fixture (assay.loop.fixture.store) reads the interpreter of each
        program of the compiler.

THE FORMAT
    The module reads ELF64 with the low byte first, as each target of the
    tree is (CLAUDE.md section XIV).  A file of a different class or byte
    sequence is an ElfError.  The module reads the extended numbering of a
    file with 65,280 sections or more.  When e_shnum is 0, section 0 holds
    the count.  When e_shstrndx is SHN_XINDEX, section 0 holds the index of
    the section names.  A symbol with the section SHN_XINDEX finds its
    section in the table of the type SHT_SYMTAB_SHNDX.

    The module reads a table on its first use.  A table or a section that is
    not fully in the file is an ElfError.

THE ARCHIVE
    members() reads an archive in the GNU format of ar.  The file starts with
    the magic "!<arch>\\n".  A header of 60 bytes comes before each member,
    at an offset that 2 divides.  The member "/" or "/SYM64/" is the index of
    the symbols, and the member "//" holds the long names.  A short name ends
    with "/".  A long name is "/N", with the offset N of the name in "//".  A
    thin archive ("!<thin>\\n") holds no bytes of its members, and it is an
    ElfError.  A member that is not ELF is an ElfError too, because the
    archives of the tree hold only objects.

THE SYMBOLS
    symbols() gives each entry of .symtab, and dynamic_symbols() gives each
    entry of .dynsym.  Each table is in its sequence, with the null entry at
    the index 0.  A dynamic symbol has its version from the sections of the
    types SHT_GNU_versym, SHT_GNU_verdef and SHT_GNU_verneed.

    `is_undefined` is True for a symbol with a name and the section
    SHN_UNDEF, other than the null entry.  These are the symbols of the
    types U, w and v of nm.  `is_defined` is True for a symbol with a name
    and a different section, other than the symbol of a section or of a
    file.  These are the symbols that `nm --defined-only` gives.

THE RELOCATIONS
    relocations() gives each entry of each section of the type SHT_RELA or
    SHT_REL.  The entries come in the sequence of the sections that they
    apply to, and in the sequence of each table.  The symbol of an entry is
    the name of its symbol.  For the symbol of a section, it is the name of
    that section, as objdump -r prints it.  For no symbol, it is empty.

THE DEMANGLER
    demangle() starts c++filt one time for all its names, with --no-verbose.
    Each name then has the text that nm --demangle and objdump -C give.  No
    other module of the package starts nm, readelf or c++filt, and
    assay.readers.objdump is the one module that starts objdump.

THE RECORDS
    The records are classes with __slots__, as the records of
    assay.readers.compdb are, and they are no dataclasses.  loop fixture
    imports the module on each run, and the import of dataclasses costs
    approximately 6 ms (assay.readers.compdb gives the measure).  Record
    gives the equality, the hash and the text of each record from its parts.
    No function of the package changes a record after its construction.
    assay.readers.objdump uses Record too.

THE WRITER
    make_file() and make_archive() write small files for the tests of the
    readers.

`guard selftest tests.readers.test_elf` does the test of the module.
"""

import contextlib
import mmap
import os
import struct
from collections.abc import Iterable, Iterator, Sequence
from pathlib import Path

from assay.core import proc

MAGIC = b"\x7fELF"
CLASS_64 = 2
DATA_LITTLE = 1
HEADER_SIZE = 64
SECTION_HEADER_SIZE = 64
PROGRAM_HEADER_SIZE = 56
SYMBOL_SIZE = 24
RELA_SIZE = 24
REL_SIZE = 16
SHN_UNDEF = 0
SHN_XINDEX = 0xFFFF
SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_RELA = 4
SHT_NOBITS = 8
SHT_REL = 9
SHT_DYNSYM = 11
SHT_SYMTAB_SHNDX = 18
SHT_GNU_VERDEF = 0x6FFFFFFD
SHT_GNU_VERNEED = 0x6FFFFFFE
SHT_GNU_VERSYM = 0x6FFFFFFF
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
STT_NOTYPE = 0
STT_OBJECT = 1
STT_FUNC = 2
STT_SECTION = 3
STT_FILE = 4
STB_LOCAL = 0
STB_GLOBAL = 1
STB_WEAK = 2
PT_INTERP = 3
# The bit of a version index that hides the version from a link (nm prints "@" and not "@@").
VERSION_HIDDEN = 0x8000
# The version indexes of a symbol that has no version: local, and global.
UNVERSIONED = (0, 1)
ARCHIVE_MAGIC = b"!<arch>\n"
THIN_MAGIC = b"!<thin>\n"
MEMBER_HEADER_SIZE = 60
MEMBER_END = b"`\n"
SYMBOL_INDEXES = (b"/", b"/SYM64/")
LONG_NAMES = b"//"
# The option of c++filt that gives the text of nm --demangle and objdump -C (THE DEMANGLER).
DEMANGLER = ("c++filt", "--no-verbose")
# e_shoff, then e_phoff, e_phentsize, e_phnum, e_shentsize, e_shnum and e_shstrndx.
SECTION_TABLE_AT = 0x28
PROGRAM_TABLE_AT = 0x20
TABLE_SIZES_AT = 0x36
# sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize.
SECTION_LAYOUT = struct.Struct("<IIQQQQIIQQ")
# p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align.
PROGRAM_LAYOUT = struct.Struct("<IIQQQQQQ")
# st_name, st_info, st_other, st_shndx, st_value, st_size.
SYMBOL_LAYOUT = struct.Struct("<IBBHQQ")
# r_offset, r_info, r_addend.  A REL entry has no r_addend.
RELA_LAYOUT = struct.Struct("<QQq")
REL_LAYOUT = struct.Struct("<QQ")
# vd_version, vd_flags, vd_ndx, vd_cnt, vd_hash, vd_aux, vd_next, and vda_name, vda_next.
VERDEF_LAYOUT = struct.Struct("<HHHHIII")
VERDAUX_LAYOUT = struct.Struct("<II")
# vn_version, vn_cnt, vn_file, vn_aux, vn_next, and vna_hash, vna_flags, vna_other, vna_name, vna_next.
VERNEED_LAYOUT = struct.Struct("<HHIII")
VERNAUX_LAYOUT = struct.Struct("<IHHII")


class ElfError(ValueError):
    """A file does not have a format that the module reads (THE FORMAT, THE ARCHIVE), or a part is not in the file."""


class Record:
    """The base of the records of the readers of objects: the equality, the hash and the text (THE RECORDS)."""

    __slots__: tuple[str, ...] = ()

    def parts(self) -> tuple[object, ...]:
        """Return each part of the record, in the sequence of __slots__."""
        return tuple(getattr(self, name) for name in self.__slots__)

    def __eq__(self, other: object) -> bool:
        """Two records are equal when they have the same class and each part is equal."""
        if not isinstance(other, Record) or type(other) is not type(self):
            return NotImplemented
        return self.parts() == other.parts()

    def __hash__(self) -> int:
        """Hash each part."""
        return hash(self.parts())

    def __repr__(self) -> str:
        """Return the record for a message."""
        shown = ", ".join(f"{name}={part!r}" for name, part in zip(self.__slots__, self.parts(), strict=True))
        return f"{type(self).__name__}({shown})"


class Section(Record):
    """One entry of the section header table, with its name."""

    __slots__ = ("entry_size", "flags", "index", "info", "kind", "link", "name", "offset", "size")

    def __init__(
        self,
        index: int,
        name: str,
        kind: int,
        flags: int,
        offset: int,
        size: int,
        link: int,
        info: int,
        entry_size: int,
    ) -> None:
        """Hold the parts of one section header."""
        self.index = index
        self.name = name
        self.kind = kind
        self.flags = flags
        self.offset = offset
        self.size = size
        self.link = link
        self.info = info
        self.entry_size = entry_size

    @property
    def is_code(self) -> bool:
        """True for an allocated section of machine code."""
        return self.flags & (SHF_ALLOC | SHF_EXECINSTR) == SHF_ALLOC | SHF_EXECINSTR

    @property
    def is_data(self) -> bool:
        """True for an allocated section with bytes in the file and no machine code."""
        return bool(self.flags & SHF_ALLOC) and not self.flags & SHF_EXECINSTR and self.kind != SHT_NOBITS


class Symbol(Record):
    """One entry of a symbol table, with its name, its section index and its version (THE SYMBOLS).

    `section` is the index of the section of the symbol, also when the table
    SHT_SYMTAB_SHNDX holds it.  `version` is the name of the version of a
    dynamic symbol, or empty.  `is_hidden_version` is True for a version that
    nm prints after "@" and not after "@@".
    """

    __slots__ = (
        "binding",
        "index",
        "is_hidden_version",
        "kind",
        "name",
        "section",
        "size",
        "value",
        "version",
        "visibility",
    )

    def __init__(
        self,
        index: int,
        name: str,
        value: int,
        size: int,
        kind: int,
        binding: int,
        visibility: int,
        section: int,
        version: str = "",
        is_hidden_version: bool = False,
    ) -> None:
        """Hold the parts of one symbol."""
        self.index = index
        self.name = name
        self.value = value
        self.size = size
        self.kind = kind
        self.binding = binding
        self.visibility = visibility
        self.section = section
        self.version = version
        self.is_hidden_version = is_hidden_version

    @property
    def is_undefined(self) -> bool:
        """True for a symbol with a name and the section SHN_UNDEF, other than the null entry: the U, w or v of nm."""
        return self.section == SHN_UNDEF and self.index != 0 and bool(self.name)

    @property
    def is_defined(self) -> bool:
        """True for a symbol with a name and a section, other than the symbol of a section or a file."""
        return self.section != SHN_UNDEF and bool(self.name) and self.kind not in (STT_SECTION, STT_FILE)

    @property
    def is_weak(self) -> bool:
        """True when `binding` is STB_WEAK."""
        return self.binding == STB_WEAK

    @property
    def is_function(self) -> bool:
        """True for a symbol of the type STT_FUNC."""
        return self.kind == STT_FUNC

    @property
    def is_local(self) -> bool:
        """True when `binding` is STB_LOCAL."""
        return self.binding == STB_LOCAL


class Relocation(Record):
    """One relocation entry: the section that it applies to, its offset, its type, its symbol and its addend.

    `symbol` is the name of the symbol, the name of the section of a section
    symbol, or empty for no symbol (THE RELOCATIONS).  The addend of an entry
    of a SHT_REL table is 0.
    """

    __slots__ = ("addend", "kind", "offset", "section", "section_index", "symbol", "symbol_index")

    def __init__(
        self, section: str, section_index: int, offset: int, kind: int, symbol: str, symbol_index: int, addend: int
    ) -> None:
        """Hold the parts of one relocation entry."""
        self.section = section
        self.section_index = section_index
        self.offset = offset
        self.kind = kind
        self.symbol = symbol
        self.symbol_index = symbol_index
        self.addend = addend


def name_at(names: bytes, offset: int) -> str:
    """Return the name that starts at an offset of a string table, up to its zero byte or the end.

    An offset that is not in the table gives an empty name.  A byte that is
    not UTF-8 stays as a surrogate.  The name then gives its bytes back.
    """
    if offset >= len(names):
        return ""
    stop = names.find(b"\0", offset)
    return names[offset : stop if stop >= 0 else len(names)].decode("utf-8", "surrogateescape")


def is_elf(path: str | os.PathLike[str]) -> bool:
    """Return True when the path is a regular file that starts with the ELF magic."""
    try:
        with open(path, "rb") as handle:
            return handle.read(len(MAGIC)) == MAGIC
    except OSError:
        return False


class Member:
    """One ELF file: a plain file, or one member of an archive.  Its bytes are a span of a buffer of the file.

    `name` is the name of the member in its archive, or the file name of a
    plain file.  `where` names the member in a message: PATH, or
    PATH(MEMBER).  The tables are read on their first use, and the member
    keeps them.
    """

    def __init__(
        self, buffer: bytes | mmap.mmap, where: str, name: str = "", start: int = 0, size: int | None = None
    ) -> None:
        """Read the ELF header, the position of the section header table and the section names.

        Raises:
            ElfError: If the bytes are not an ELF64 file with the low byte first, or a table is not fully in them
        """
        self.buffer = buffer
        self.where = where
        self.name = name or where
        self.start = start
        self.size = len(buffer) - start if size is None else size
        self.table_offset = 0
        self.count = 0
        self.names = b""
        self._sections: tuple[Section, ...] | None = None
        self._symbols: dict[int, tuple[Symbol, ...]] = {}
        if self.size < HEADER_SIZE or buffer[start : start + len(MAGIC)] != MAGIC:
            raise ElfError(f"{where}: the file is not an ELF file")
        if buffer[start + 4] != CLASS_64 or buffer[start + 5] != DATA_LITTLE:
            raise ElfError(f"{where}: the file is not ELF64 with the low byte first")
        (table_offset,) = struct.unpack_from("<Q", buffer, start + SECTION_TABLE_AT)
        entry_size, count, names_index = struct.unpack_from("<HHH", buffer, start + TABLE_SIZES_AT + 4)
        if table_offset == 0:
            return
        if entry_size != SECTION_HEADER_SIZE:
            raise ElfError(f"{where}: a section header has {entry_size} bytes, not {SECTION_HEADER_SIZE}")
        if table_offset + SECTION_HEADER_SIZE > self.size:
            raise ElfError(f"{where}: the section header table is not fully in the file")
        first = SECTION_LAYOUT.unpack_from(buffer, start + table_offset)
        if count == 0:
            count = first[5]
        if names_index == SHN_XINDEX:
            names_index = first[6]
        if table_offset + count * SECTION_HEADER_SIZE > self.size:
            raise ElfError(f"{where}: the section header table is not fully in the file")
        self.table_offset, self.count = table_offset, count
        if names_index:
            if names_index >= count:
                raise ElfError(f"{where}: the index of the section names is not an index of the section header table")
            self.names = self.data(self.raw_entry(names_index, b""))

    def raw_entry(self, index: int, names: bytes) -> Section:
        """Return one entry of the section header table, with its name from the given section names."""
        row = SECTION_LAYOUT.unpack_from(self.buffer, self.start + self.table_offset + index * SECTION_HEADER_SIZE)
        return Section(index, name_at(names, row[0]), row[1], row[2], row[4], row[5], row[6], row[7], row[9])

    def sections(self) -> tuple[Section, ...]:
        """Return each entry of the section header table.  Complexity: linear in the number of sections."""
        if self._sections is None:
            self._sections = tuple(self.raw_entry(index, self.names) for index in range(self.count))
        return self._sections

    def named(self, name: str) -> list[Section]:
        """Return each section with the name, in the sequence of the table.

        A longer name that ends with the name is a different name.
        Complexity: linear in the number of sections.
        """
        return [section for section in self.sections() if section.name == name]

    def span(self, section: Section) -> tuple[int, int]:
        """Return the byte span of a section in the buffer.  A section with no bytes in the file has an empty span.

        Raises:
            ElfError: If the section is not fully in the file
        """
        if section.kind == SHT_NOBITS:
            return self.start + section.offset, self.start + section.offset
        if section.offset + section.size > self.size:
            raise ElfError(f"{self.where}: the section {section.index} is not fully in the file")
        return self.start + section.offset, self.start + section.offset + section.size

    def data(self, section: Section) -> bytes:
        """Return the bytes of a section.

        Raises:
            ElfError: If the section is not fully in the file
        """
        begin, end = self.span(section)
        return bytes(self.buffer[begin:end])

    def linked(self, section: Section) -> Section:
        """Return the section that the field sh_link of a section names.

        Raises:
            ElfError: If the link is not an index of the section header table
        """
        sections = self.sections()
        if section.link >= len(sections):
            raise ElfError(f"{self.where}: the section {section.name} links to a section that the table does not hold")
        return sections[section.link]

    def table_of(self, kind: int) -> Section | None:
        """Return the first section of a type, or None."""
        return next((section for section in self.sections() if section.kind == kind), None)

    def read_symbols(self, kind: int) -> tuple[Symbol, ...]:
        """Return each entry of the first symbol table of a type, with the versions of a dynamic table.

        Complexity: linear in the number of symbols.

        Raises:
            ElfError: If the table, its names, its extended indexes or its versions are not fully in the file
        """
        table = self.table_of(kind)
        if table is None:
            return ()
        data = self.data(table)
        names = self.data(self.linked(table))
        extended = self.extended_indexes(table)
        versions = self.versions(table) if kind == SHT_DYNSYM else {}
        found: list[Symbol] = []
        usable = len(data) - len(data) % SYMBOL_SIZE
        for index, (name, info, other, shndx, value, size) in enumerate(SYMBOL_LAYOUT.iter_unpack(data[:usable])):
            section = extended[index] if shndx == SHN_XINDEX and index < len(extended) else shndx
            version, is_hidden = versions.get(index, ("", False))
            found.append(
                Symbol(
                    index,
                    name_at(names, name),
                    value,
                    size,
                    info & 0xF,
                    info >> 4,
                    other & 0x3,
                    section,
                    version,
                    is_hidden,
                )
            )
        return tuple(found)

    def symbols(self) -> tuple[Symbol, ...]:
        """Return each entry of .symtab, the null entry first (THE SYMBOLS).  Complexity: linear in the symbols."""
        if SHT_SYMTAB not in self._symbols:
            self._symbols[SHT_SYMTAB] = self.read_symbols(SHT_SYMTAB)
        return self._symbols[SHT_SYMTAB]

    def dynamic_symbols(self) -> tuple[Symbol, ...]:
        """Return each entry of .dynsym with its version, the null entry first.  Complexity: linear in the symbols."""
        if SHT_DYNSYM not in self._symbols:
            self._symbols[SHT_DYNSYM] = self.read_symbols(SHT_DYNSYM)
        return self._symbols[SHT_DYNSYM]

    def extended_indexes(self, table: Section) -> tuple[int, ...]:
        """Return the words of the table SHT_SYMTAB_SHNDX that links to a symbol table, or no word."""
        for section in self.sections():
            if section.kind == SHT_SYMTAB_SHNDX and section.link == table.index:
                data = self.data(section)
                return struct.unpack_from(f"<{len(data) // 4}I", data)
        return ()

    def versions(self, table: Section) -> dict[int, tuple[str, bool]]:
        """Return the version name and the hidden bit of each versioned symbol of a dynamic table, by its index.

        Complexity: linear in the symbols and the version entries.

        Raises:
            ElfError: If a version table or a chain of its entries is not fully in the file
        """
        indexes = self.table_of(SHT_GNU_VERSYM)
        if indexes is None:
            return {}
        names = {**self.version_names(SHT_GNU_VERDEF), **self.version_names(SHT_GNU_VERNEED)}
        data = self.data(indexes)
        found: dict[int, tuple[str, bool]] = {}
        for index, (word,) in enumerate(struct.iter_unpack("<H", data[: len(data) - len(data) % 2])):
            number = word & ~VERSION_HIDDEN
            if number not in UNVERSIONED and number in names and index < table.size // SYMBOL_SIZE:
                found[index] = (names[number], bool(word & VERSION_HIDDEN))
        return found

    def version_names(self, kind: int) -> dict[int, str]:
        """Return the name of each version index that a table SHT_GNU_verdef or SHT_GNU_verneed gives.

        An entry of SHT_GNU_verdef names its version with its first auxiliary
        entry.  An entry of SHT_GNU_verneed names each version with an
        auxiliary entry of its own.  Complexity: linear in the entries of the
        table.

        Raises:
            ElfError: If an entry is not fully in the table
        """
        table = self.table_of(kind)
        if table is None:
            return {}
        data = self.data(table)
        names = self.data(self.linked(table))
        found: dict[int, str] = {}
        offset = 0
        for _ in range(table.info):
            if kind == SHT_GNU_VERDEF:
                if offset + VERDEF_LAYOUT.size > len(data):
                    raise ElfError(f"{self.where}: an entry of SHT_GNU_verdef is not fully in its table")
                _, _, number, count, _, aux, following = VERDEF_LAYOUT.unpack_from(data, offset)
                if count and offset + aux + VERDAUX_LAYOUT.size <= len(data):
                    found[number] = name_at(names, VERDAUX_LAYOUT.unpack_from(data, offset + aux)[0])
            else:
                if offset + VERNEED_LAYOUT.size > len(data):
                    raise ElfError(f"{self.where}: an entry of SHT_GNU_verneed is not fully in its table")
                _, count, _, aux, following = VERNEED_LAYOUT.unpack_from(data, offset)
                place = offset + aux
                for _ in range(count):
                    if place + VERNAUX_LAYOUT.size > len(data):
                        raise ElfError(f"{self.where}: an auxiliary entry of SHT_GNU_verneed is not fully in its table")
                    _, _, number, name, step = VERNAUX_LAYOUT.unpack_from(data, place)
                    found[number] = name_at(names, name)
                    place += step
            if not following:
                break
            offset += following
        return found

    def relocations(self, ignored_prefixes: Sequence[str] = ()) -> tuple[Relocation, ...]:
        """Return each relocation entry, with the name of its symbol (THE RELOCATIONS).

        Args:
            ignored_prefixes: The prefixes of section names, such as ".debug".  The function does not read the
                relocation tables of a section with such a name

        Complexity: linear in the entries of the tables that the call reads.

        Raises:
            ElfError: If a table, its symbols or the section that it applies to are not fully in the file
        """
        sections = self.sections()
        tables = [section for section in sections if section.kind in (SHT_RELA, SHT_REL)]
        found: list[Relocation] = []
        for table in sorted(tables, key=lambda each: (each.info, each.index)):
            if table.info >= len(sections):
                raise ElfError(
                    f"{self.where}: the relocation table {table.name} applies to a section that the table does not hold"
                )
            target = sections[table.info]
            if target.name.startswith(tuple(ignored_prefixes)):
                continue
            symbols = self.symbol_table_at(table.link)
            layout = RELA_LAYOUT if table.kind == SHT_RELA else REL_LAYOUT
            data = self.data(table)
            for entry in layout.iter_unpack(data[: len(data) - len(data) % layout.size]):
                offset, info = entry[0], entry[1]
                index = info >> 32
                symbol = symbols[index] if index < len(symbols) else None
                found.append(
                    Relocation(
                        target.name,
                        target.index,
                        offset,
                        info & 0xFFFFFFFF,
                        self.symbol_name(symbol),
                        index,
                        entry[2] if len(entry) == 3 else 0,
                    )
                )
        return tuple(found)

    def symbol_table_at(self, index: int) -> tuple[Symbol, ...]:
        """Return the entries of the symbol table at a section index: .symtab, .dynsym, or none."""
        sections = self.sections()
        if index >= len(sections):
            return ()
        kind = sections[index].kind
        if kind == SHT_SYMTAB:
            return self.symbols()
        if kind == SHT_DYNSYM:
            return self.dynamic_symbols()
        return ()

    def symbol_name(self, symbol: Symbol | None) -> str:
        """Return the name of the symbol of a relocation: the name of its section for a section symbol (THE RELOCATIONS)."""
        if symbol is None or symbol.index == 0:
            return ""
        if symbol.kind == STT_SECTION and not symbol.name:
            sections = self.sections()
            return sections[symbol.section].name if symbol.section < len(sections) else ""
        return symbol.name

    def interpreter(self) -> str | None:
        """Return the program interpreter of the segment PT_INTERP, or None for a file with no such segment.

        Complexity: linear in the number of program headers.

        Raises:
            ElfError: If the program header table or the segment is not fully in the file
        """
        (table_offset,) = struct.unpack_from("<Q", self.buffer, self.start + PROGRAM_TABLE_AT)
        entry_size, count = struct.unpack_from("<HH", self.buffer, self.start + TABLE_SIZES_AT)
        if table_offset == 0 or count == 0:
            return None
        if entry_size != PROGRAM_HEADER_SIZE or table_offset + count * PROGRAM_HEADER_SIZE > self.size:
            raise ElfError(f"{self.where}: the program header table is not fully in the file")
        for index in range(count):
            kind, _, offset, _, _, file_size, _, _ = PROGRAM_LAYOUT.unpack_from(
                self.buffer, self.start + table_offset + index * PROGRAM_HEADER_SIZE
            )
            if kind != PT_INTERP:
                continue
            if offset + file_size > self.size:
                raise ElfError(f"{self.where}: the segment of the interpreter is not fully in the file")
            text = bytes(self.buffer[self.start + offset : self.start + offset + file_size]).rstrip(b"\0")
            return os.fsdecode(text)
        return None


def archive_spans(data: bytes, where: str) -> Iterator[tuple[str, int, int]]:
    """Give the name, the start and the length of each member of a GNU archive (THE ARCHIVE), other than its indexes.

    Complexity: linear in the number of members.

    Raises:
        ElfError: If a header is cut or damaged, the table "//" does not hold a long name, or a name has no "/"
    """
    offset = len(ARCHIVE_MAGIC)
    long_names = b""
    while offset < len(data):
        if offset + MEMBER_HEADER_SIZE > len(data):
            raise ElfError(f"{where}: the header of a member at the offset {offset} is cut")
        header = data[offset : offset + MEMBER_HEADER_SIZE]
        size_text = header[48:58].strip()
        if header[58:60] != MEMBER_END or not size_text.isdigit():
            raise ElfError(f"{where}: the header of a member at the offset {offset} is damaged")
        size = int(size_text)
        start = offset + MEMBER_HEADER_SIZE
        if start + size > len(data):
            raise ElfError(f"{where}: the member at the offset {offset} is cut")
        raw = header[:16].rstrip(b" ")
        offset = start + size + size % 2
        if raw in SYMBOL_INDEXES:
            continue
        if raw == LONG_NAMES:
            long_names = data[start : start + size]
            continue
        if raw.startswith(b"/") and raw[1:].isdigit():
            place = int(raw[1:])
            stop = long_names.find(b"/\n", place)
            if place >= len(long_names) or stop < 0:
                raise ElfError(f"{where}: the table of long names does not hold the long name {raw.decode('ascii')}")
            name = long_names[place:stop]
        elif raw.endswith(b"/"):
            name = raw[:-1]
        else:
            raise ElfError(f"{where}: the member name {raw!r} is not a name of the GNU format of ar")
        yield name.decode("utf-8", "surrogateescape"), start, size


def members(path: str | os.PathLike[str]) -> tuple[Member, ...]:
    """Return each ELF member of an archive, or the one member of a plain ELF file.

    The function reads all of the file into memory.  Use mapped() for a large
    program.  Complexity: linear in the length of the file for an archive.

    Raises:
        ElfError: If the file is not an ELF file or a GNU archive, or a member is not ELF or is damaged
        OSError: If the file cannot be read
    """
    data = Path(path).read_bytes()
    where = os.fspath(path)
    if data.startswith(THIN_MAGIC):
        raise ElfError(f"{where}: the archive is thin, and it holds no bytes of its members")
    if data.startswith(ARCHIVE_MAGIC):
        return tuple(
            Member(data, f"{where}({name})", name, start, size) for name, start, size in archive_spans(data, where)
        )
    return (Member(data, where, Path(path).name),)


@contextlib.contextmanager
def mapped(path: str | os.PathLike[str]) -> Iterator[Member]:
    """Map one ELF file to read it, and give its member.  The map closes at the end of the block.

    Raises:
        ElfError: If the file is empty or is not ELF64 with the low byte first
        OSError: If the file cannot be read
    """
    with open(path, "rb") as handle:
        try:
            buffer = mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ)
        except ValueError:
            raise ElfError(f"{os.fspath(path)}: the file is empty") from None
    with buffer:
        yield Member(buffer, os.fspath(path), Path(path).name)


def interpreter(path: str | os.PathLike[str]) -> str | None:
    """Return the program interpreter of an ELF file, or None for a static program or a file that is not ELF.

    The function maps the file, and it reads only the headers and the segment.

    Raises:
        ElfError: If the file is ELF and is damaged, or is not ELF64 with the low byte first
        OSError: If the file cannot be read
    """
    if not is_elf(path):
        return None
    with mapped(path) as member:
        return member.interpreter()


def demangle(names: Sequence[str]) -> tuple[str, ...]:
    """Return the text of each name, as nm --demangle and objdump -C give it (THE DEMANGLER).

    One run of c++filt reads all the names.  A name that is not mangled
    gives its own text.

    Raises:
        ElfError: If c++filt cannot start, runs longer than its limit, fails, or does not give one line for each name
    """
    if not names:
        return ()
    text = "".join(f"{name}\n" for name in names)
    command = proc.Command(list(DEMANGLER), timeout_s=proc.QUICK_S, stdin=text.encode("utf-8", "surrogateescape"))
    try:
        done = proc.require(proc.run(command))
    except proc.ToolFailed as problem:
        raise ElfError(f"c++filt does not demangle the names: {problem}") from None
    lines = proc.decoded(done.stdout, "surrogateescape").split("\n")
    if lines[-1:] == [""]:
        lines.pop()
    if len(lines) != len(names):
        raise ElfError(f"c++filt gives {len(lines)} lines for {len(names)} names")
    return tuple(lines)


# ── The writer ─────────────────────────────────────────────────────


class Planted(Record):
    """One section of a file that make_file writes.

    A `size` above the length of `data` gives the sh_size of a section that
    holds no bytes in the file, at the offset 0.
    """

    __slots__ = ("data", "entry_size", "flags", "info", "kind", "link", "name", "size")

    def __init__(
        self,
        name: str,
        data: bytes = b"",
        kind: int = SHT_PROGBITS,
        flags: int = 0,
        link: int = 0,
        entry_size: int = 0,
        size: int | None = None,
        info: int = 0,
    ) -> None:
        """Hold the parts of one planted section."""
        self.name = name
        self.data = data
        self.kind = kind
        self.flags = flags
        self.link = link
        self.entry_size = entry_size
        self.size = size
        self.info = info


class PlantedSymbol(Record):
    """One symbol that symbol_table writes: its name, `value`, `size`, `kind`, `binding` and section index."""

    __slots__ = ("binding", "kind", "name", "section", "size", "value")

    def __init__(
        self,
        name: str,
        value: int = 0,
        size: int = 0,
        kind: int = STT_NOTYPE,
        binding: int = STB_GLOBAL,
        section: int = SHN_UNDEF,
    ) -> None:
        """Hold the parts of one planted symbol."""
        self.name = name
        self.value = value
        self.size = size
        self.kind = kind
        self.binding = binding
        self.section = section


def make_file(sections: Iterable[Planted], extended: bool = False, interpreter_path: str = "") -> bytes:
    """Return an ELF64 file with the low byte first and the planted sections, for the tests of the readers.

    Section 0 is the null section.  The planted sections follow in their
    sequence from the index 1, and the section names are the last section.
    With `extended`, the file uses the extended numbering of a large file:
    e_shnum and e_shstrndx are 0 and SHN_XINDEX, and section 0 holds them.
    With an `interpreter_path`, the file has one program header PT_INTERP.
    Its segment holds the path after the sections.
    """
    planted = [Planted(""), *sections, Planted(".shstrtab", kind=SHT_STRTAB)]
    names = b"\0"
    name_offsets = []
    for item in planted:
        name_offsets.append(len(names) if item.name else 0)
        if item.name:
            names += item.name.encode() + b"\0"
    payloads = [item.data for item in planted[:-1]] + [names]
    body = b""
    rows = []
    count = len(planted)
    for index, (item, payload) in enumerate(zip(planted, payloads, strict=True)):
        size = len(payload) if item.size is None or item.size <= len(payload) else item.size
        offset = HEADER_SIZE + len(body) if index and size == len(payload) else 0
        body += payload
        rows.append(
            SECTION_LAYOUT.pack(
                name_offsets[index],
                item.kind,
                item.flags,
                0,
                offset,
                count if index == 0 and extended else size,
                count - 1 if index == 0 and extended else item.link,
                item.info,
                1,
                item.entry_size,
            )
        )
    program = b""
    if interpreter_path:
        text = interpreter_path.encode() + b"\0"
        segment_at = HEADER_SIZE + len(body)
        body += text
        program = PROGRAM_LAYOUT.pack(PT_INTERP, 4, segment_at, 0, 0, len(text), len(text), 1)
    table_at = HEADER_SIZE + len(body)
    header = bytearray(HEADER_SIZE)
    header[:7] = MAGIC + bytes([CLASS_64, DATA_LITTLE, 1])
    struct.pack_into(
        "<HHIQQQIHHHHHH",
        header,
        16,
        1,
        62,
        1,
        0,
        table_at + len(rows) * SECTION_HEADER_SIZE if program else 0,
        table_at,
        0,
        HEADER_SIZE,
        PROGRAM_HEADER_SIZE,
        1 if program else 0,
        SECTION_HEADER_SIZE,
        0 if extended else count,
        SHN_XINDEX if extended else count - 1,
    )
    return bytes(header) + body + b"".join(rows) + program


def symbol_table(symbols: Iterable[PlantedSymbol]) -> tuple[bytes, bytes]:
    """Return the bytes of a symbol table with a null entry first, and of its string table."""
    strings = b"\0"
    table = bytes(SYMBOL_SIZE)
    for symbol in symbols:
        table += SYMBOL_LAYOUT.pack(
            len(strings) if symbol.name else 0,
            symbol.binding << 4 | symbol.kind,
            0,
            symbol.section,
            symbol.value,
            symbol.size,
        )
        if symbol.name:
            strings += symbol.name.encode() + b"\0"
    return table, strings


def relocation_table(entries: Iterable[tuple[int, int, int, int]]) -> bytes:
    """Return the bytes of a SHT_RELA table, from (offset, symbol index, type, addend) for each entry."""
    return b"".join(RELA_LAYOUT.pack(offset, index << 32 | kind, addend) for offset, index, kind, addend in entries)


def make_archive(files: Iterable[tuple[str, bytes]]) -> bytes:
    """Return a GNU archive of the named files, with a table of long names for a name of 16 bytes or more."""
    long_names = b""
    parts: list[tuple[bytes, bytes]] = []
    for file_name, data in files:
        encoded = file_name.encode()
        if len(encoded) >= 16:
            parts.append((f"/{len(long_names)}".encode(), data))
            long_names += encoded + b"/\n"
        else:
            parts.append((encoded + b"/", data))
    if long_names:
        parts.insert(0, (LONG_NAMES, long_names))
    out = ARCHIVE_MAGIC
    for header_name, payload in parts:
        out += header_name.ljust(16) + b"0".ljust(12) + b"0".ljust(6) + b"0".ljust(6) + b"644".ljust(8)
        out += str(len(payload)).encode().ljust(10) + MEMBER_END + payload + (b"\n" if len(payload) % 2 else b"")
    return out
