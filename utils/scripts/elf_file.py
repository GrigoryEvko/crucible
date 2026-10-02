"""elf_file — read the section table, the sections and the function symbols of an ELF64 file.

THE READERS
    This module is the one reader of ELF headers in utils/scripts:
    * run-affected-tests.py reads the data sections of a test program
    * check-compile-cost.py reads the code sections and the largest function
      of an object
    * quarantine_sections.py reads the section .crucible.quarantine of an
      object.

THE FORMAT
    The module reads ELF64 in little-endian order, as each target of the tree
    is (CLAUDE.md section XIV).  A file of another class or byte order is an
    ElfError.  It follows the extended numbering of a file with 65,280
    sections or more: when e_shnum is 0, section 0 holds the count, and when
    e_shstrndx is SHN_XINDEX, section 0 holds the index of the section names.
    The parse reads the ELF header, the section header table and the section
    names, and no other part of the file.  The data of a section is read only
    on request, and a section outside the file is then an ElfError.

THE WRITER
    make_file() writes a small file with named sections for the self-tests of
    the readers.

Run this file with --self-test to do a test of the module.
"""

from __future__ import annotations

import contextlib
import mmap
import struct
import sys
import tempfile
from array import array
from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from pathlib import Path

MAGIC = b"\x7fELF"
CLASS_64 = 2
DATA_LITTLE = 1
HEADER_SIZE = 64
SECTION_HEADER_SIZE = 64
SYMBOL_SIZE = 24
SHN_XINDEX = 0xFFFF
SHT_PROGBITS = 1
SHT_SYMTAB = 2
SHT_STRTAB = 3
SHT_NOBITS = 8
SHF_ALLOC = 0x2
SHF_EXECINSTR = 0x4
STT_FUNC = 2
# sh_name, sh_type, sh_flags, sh_addr, sh_offset, sh_size, sh_link, sh_info, sh_addralign, sh_entsize.
SECTION_LAYOUT = struct.Struct("<IIQQQQIIQQ")
# st_name, st_info, st_other, st_shndx, st_value, st_size.
SYMBOL_LAYOUT = struct.Struct("<IBBHQQ")


class ElfError(ValueError):
    """A file is not an ELF64 little-endian file, or a table or a section is outside the file."""


@dataclass(frozen=True, slots=True)
class Section:
    """One entry of the section header table."""

    index: int
    name: bytes
    kind: int
    flags: int
    offset: int
    size: int
    link: int

    @property
    def is_code(self) -> bool:
        """True for an allocated section of machine code."""
        return self.flags & (SHF_ALLOC | SHF_EXECINSTR) == SHF_ALLOC | SHF_EXECINSTR

    @property
    def is_data(self) -> bool:
        """True for an allocated section with bytes in the file and no machine code."""
        return bool(self.flags & SHF_ALLOC) and not self.flags & SHF_EXECINSTR and self.kind != SHT_NOBITS


def name_at(names: bytes, offset: int) -> bytes:
    """Return the name that starts at an offset of the section names, up to its zero byte or the end."""
    if offset >= len(names):
        return b""
    stop = names.find(b"\0", offset)
    return names[offset:stop if stop >= 0 else len(names)]


def is_elf(path: str | Path) -> bool:
    """Return True when the path is a regular file that starts with the ELF magic."""
    try:
        with open(path, "rb") as handle:
            return handle.read(4) == MAGIC
    except OSError:
        return False


class ElfFile:
    """The section table of one ELF64 little-endian file, over its bytes or a map of the file."""

    def __init__(self, buffer: bytes | mmap.mmap, where: str) -> None:
        """Read the ELF header, the place of the section header table and the section names.

        The entries of the table are read on the first use of `sections`, or
        only the entries with one name by `named`.

        Raises:
            ElfError: If the buffer is not an ELF64 little-endian file, or its tables are outside the buffer
        """
        self.buffer = buffer
        self.where = where
        self.table_offset = 0
        self.count = 0
        self.names = b""
        self._sections: tuple[Section, ...] | None = None
        if len(buffer) < HEADER_SIZE or buffer[:4] != MAGIC:
            raise ElfError(f"{where}: the file is not an ELF file")
        if buffer[4] != CLASS_64 or buffer[5] != DATA_LITTLE:
            raise ElfError(f"{where}: the file is not ELF64 little-endian")
        (table_offset,) = struct.unpack_from("<Q", buffer, 0x28)
        entry_size, count, names_index = struct.unpack_from("<HHH", buffer, 0x3A)
        if table_offset == 0:
            return
        if entry_size != SECTION_HEADER_SIZE:
            raise ElfError(f"{where}: a section header has {entry_size} bytes, not {SECTION_HEADER_SIZE}")
        if table_offset + SECTION_HEADER_SIZE > len(buffer):
            raise ElfError(f"{where}: the section header table is outside the file")
        first = SECTION_LAYOUT.unpack_from(buffer, table_offset)
        if count == 0:
            count = first[5]
        if names_index == SHN_XINDEX:
            names_index = first[6]
        if table_offset + count * SECTION_HEADER_SIZE > len(buffer):
            raise ElfError(f"{where}: the section header table is outside the file")
        self.table_offset, self.count = table_offset, count
        if names_index:
            if names_index >= count:
                raise ElfError(f"{where}: the index of the section names is outside the section header table")
            names = self.entry(names_index)
            if names.offset + names.size > len(buffer):
                raise ElfError(f"{where}: the section names are outside the file")
            self.names = bytes(buffer[names.offset:names.offset + names.size])

    def entry(self, index: int) -> Section:
        """Return one entry of the section header table, with its name."""
        row = SECTION_LAYOUT.unpack_from(self.buffer, self.table_offset + index * SECTION_HEADER_SIZE)
        return Section(index, name_at(self.names, row[0]), row[1], row[2], row[4], row[5], row[6])

    @property
    def sections(self) -> tuple[Section, ...]:
        """Each entry of the section header table.  Complexity: linear in the number of sections."""
        if self._sections is None:
            self._sections = tuple(self.entry(index) for index in range(self.count))
        return self._sections

    @classmethod
    @contextlib.contextmanager
    def mapped(cls, path: str | Path) -> Iterator[ElfFile]:
        """Map one file for reading, and give its section table.

        Raises:
            ElfError: If the file is empty or is not an ELF64 little-endian file
            OSError: If the file cannot be read
        """
        with open(path, "rb") as handle:
            try:
                buffer = mmap.mmap(handle.fileno(), 0, access=mmap.ACCESS_READ)
            except ValueError:
                raise ElfError(f"{path}: the file is empty") from None
        with buffer:
            yield cls(buffer, str(path))

    def span(self, section: Section) -> tuple[int, int]:
        """Return the byte span of a section in the file.

        Raises:
            ElfError: If the section is outside the file
        """
        if section.kind == SHT_NOBITS:
            return section.offset, section.offset
        if section.offset + section.size > len(self.buffer):
            raise ElfError(f"{self.where}: the section {section.index} is outside the file")
        return section.offset, section.offset + section.size

    def data(self, section: Section) -> bytes:
        """Return the bytes of a section.

        Raises:
            ElfError: If the section is outside the file
        """
        start, end = self.span(section)
        return bytes(self.buffer[start:end])

    def named(self, name: bytes) -> list[Section]:
        """Return each section with the name, in the order of the table.

        The search finds each place of the name in the section names, and
        then reads only the entries whose sh_name is one of those places.  A
        linker can give a name the tail of a longer name, so each place counts.
        Complexity: linear in the size of the names and the number of sections.
        """
        wanted = name + b"\0"
        places: set[int] = set()
        start = self.names.find(wanted)
        while start >= 0:
            places.add(start)
            start = self.names.find(wanted, start + 1)
        if not places:
            return []
        words = array("I")
        words.frombytes(self.buffer[self.table_offset:self.table_offset + self.count * SECTION_HEADER_SIZE])
        if sys.byteorder != "little":
            words.byteswap()
        # sh_name is the first word of each entry.
        return [self.entry(index) for index, word in enumerate(words[::SECTION_HEADER_SIZE // 4]) if word in places]

    def largest_function(self) -> tuple[int, str]:
        """Return the size and the name of the largest function symbol, or (0, "") when no symbol table has one.

        Complexity: linear in the number of symbols.

        Raises:
            ElfError: If the symbol table or its strings are outside the file
        """
        largest, name_offset, strings = 0, 0, None
        for table in (section for section in self.sections if section.kind == SHT_SYMTAB):
            if table.link >= len(self.sections):
                raise ElfError(f"{self.where}: the symbol table links to a section outside the table")
            start, end = self.span(table)
            for name, info, _other, _section, _value, size in SYMBOL_LAYOUT.iter_unpack(
                    self.buffer[start:end - (end - start) % SYMBOL_SIZE]):
                if info & 0xF == STT_FUNC and size > largest:
                    largest, name_offset, strings = size, name, self.sections[table.link]
        if strings is None:
            return 0, ""
        start, end = self.span(strings)
        stop = self.buffer.find(b"\0", start + name_offset, end)
        return largest, bytes(self.buffer[start + name_offset:stop if stop >= 0 else end]).decode("utf-8", "replace")


# ── The writer ─────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Planted:
    """One section of a file that make_file writes.

    A `size` above the length of `data` gives the size of a section that
    holds no bytes in the file, at offset 0.
    """

    name: bytes
    data: bytes = b""
    kind: int = SHT_PROGBITS
    flags: int = 0
    link: int = 0
    entry_size: int = 0
    size: int | None = None


def make_file(sections: Iterable[Planted], extended: bool = False) -> bytes:
    """Return an ELF64 little-endian file with the planted sections, for the self-tests of the readers.

    Section 0 is the null section, the planted sections follow in their
    order from index 1, and the section names are the last section.  With
    EXTENDED, the file uses the extended numbering of a large file: e_shnum
    and e_shstrndx are 0 and SHN_XINDEX, and section 0 holds them.
    """
    planted = [Planted(b""), *sections, Planted(b".shstrtab", kind=SHT_STRTAB)]
    names = b"\0"
    name_offsets = []
    for item in planted:
        name_offsets.append(len(names) if item.name else 0)
        if item.name:
            names += item.name + b"\0"
    payloads = [item.data for item in planted[:-1]] + [names]
    body = b""
    rows = []
    count = len(planted)
    for index, (item, payload) in enumerate(zip(planted, payloads, strict=True)):
        size = len(payload) if item.size is None or item.size <= len(payload) else item.size
        offset = HEADER_SIZE + len(body) if index and size == len(payload) else 0
        body += payload
        rows.append(SECTION_LAYOUT.pack(name_offsets[index], item.kind, item.flags, 0, offset,
                                        count if index == 0 and extended else size,
                                        count - 1 if index == 0 and extended else item.link, 0, 1,
                                        item.entry_size))
    header = bytearray(HEADER_SIZE)
    header[:7] = MAGIC + bytes([CLASS_64, DATA_LITTLE, 1])
    struct.pack_into("<HHIQQQIHHHHHH", header, 16, 1, 62, 1, 0, 0, HEADER_SIZE + len(body), 0, HEADER_SIZE, 0, 0,
                     SECTION_HEADER_SIZE, 0 if extended else count, SHN_XINDEX if extended else count - 1)
    return bytes(header) + body + b"".join(rows)


def function_symbols(functions: Iterable[tuple[str, int]]) -> tuple[bytes, bytes]:
    """Return the bytes of a symbol table and of its strings, with one function symbol of each name and size."""
    strings = b"\0"
    symbols = bytes(SYMBOL_SIZE)
    for name, size in functions:
        symbols += SYMBOL_LAYOUT.pack(len(strings), 0x10 | STT_FUNC, 0, 1, 0, size)
        strings += name.encode() + b"\0"
    return symbols, strings


# ── The self-test ──────────────────────────────────────────────────


def self_test() -> int:
    """Do a test of the reader on planted files.

    Returns:
        0 when each case holds, else 1
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    def refuses(name: str, action: object) -> None:
        try:
            action()  # type: ignore[operator]
        except ElfError:
            expect(name, True)
            return
        expect(name, False)

    planted = [Planted(b".text", b"\x90\x90", flags=SHF_ALLOC | SHF_EXECINSTR),
               Planted(b".data", b"D", flags=SHF_ALLOC),
               Planted(b".bss", kind=SHT_NOBITS, flags=SHF_ALLOC, size=4096), Planted(b".comment", b"C")]
    plain = make_file(planted)
    elf = ElfFile(plain, "plain")
    expect("the parse reads each section with its name",
           [section.name for section in elf.sections] == [b"", b".text", b".data", b".bss", b".comment", b".shstrtab"])
    expect("a code section and a data section are told apart",
           [section.name for section in elf.sections if section.is_code] == [b".text"]
           and [section.name for section in elf.sections if section.is_data] == [b".data"])
    expect("data() gives the bytes of a section", elf.data(elf.named(b".data")[0]) == b"D")
    expect("a section with no bytes in the file has an empty span",
           elf.data(elf.named(b".bss")[0]) == b"")
    expect("the extended numbering gives the same sections",
           [section.name for section in ElfFile(make_file(planted, extended=True), "x").sections]
           == [section.name for section in elf.sections])
    expect("a name that a longer name ends with is not that name",
           not ElfFile(make_file([Planted(b".rela.text", b"r")]), "x").named(b".text"))
    symbols, strings = function_symbols([("small", 10), ("large", 300), ("medium", 50)])
    with_symbols = make_file([Planted(b".symtab", symbols, SHT_SYMTAB, link=2, entry_size=SYMBOL_SIZE),
                              Planted(b".strtab", strings, SHT_STRTAB),
                              Planted(b".text", flags=SHF_ALLOC | SHF_EXECINSTR, size=360)])
    reader = ElfFile(with_symbols, "symbols")
    expect("largest_function reads the largest function symbol", reader.largest_function() == (300, "large"))
    expect("a declared code size needs no bytes in the file",
           sum(section.size for section in reader.sections if section.is_code) == 360)
    expect("a file with no symbol table has no function", elf.largest_function() == (0, ""))
    refuses("a file that is not ELF is refused", lambda: ElfFile(b"#!/bin/sh\n" + bytes(80), "script"))
    refuses("a 32-bit file is refused", lambda: ElfFile(MAGIC + b"\x01\x01" + bytes(80), "elf32"))
    refuses("a big-endian file is refused", lambda: ElfFile(MAGIC + b"\x02\x02" + bytes(80), "big"))
    damaged = bytearray(plain)
    struct.pack_into("<Q", damaged, 0x28, len(damaged) + 100)
    refuses("a section header table outside the file is refused", lambda: ElfFile(bytes(damaged), "damaged"))
    refuses("a cut table is refused", lambda: ElfFile(plain[:-10], "cut"))
    outside = ElfFile(make_file([Planted(b".data", b"", flags=SHF_ALLOC)]), "outside")
    shifted = Section(9, b".data", SHT_PROGBITS, SHF_ALLOC, len(outside.buffer), 8, 0)
    refuses("a section outside the file is refused on read", lambda: outside.data(shifted))
    with tempfile.TemporaryDirectory(prefix="elf-file-") as scratch_text:
        scratch = Path(scratch_text)
        (scratch / "plain.o").write_bytes(plain)
        with ElfFile.mapped(scratch / "plain.o") as mapped:
            expect("mapped() reads a file", mapped.data(mapped.named(b".data")[0]) == b"D")
        (scratch / "empty.o").write_bytes(b"")
        refuses("an empty file is refused", lambda: ElfFile.mapped(scratch / "empty.o").__enter__())
        expect("is_elf reads the magic", is_elf(scratch / "plain.o") and not is_elf(scratch / "empty.o")
               and not is_elf(scratch / "absent.o"))
    if failures:
        print(f"elf_file --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("elf_file --self-test: every case holds.")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    print("usage: elf_file.py --self-test", file=sys.stderr)
    sys.exit(2)
