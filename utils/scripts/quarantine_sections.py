"""quarantine_sections — read the findings of the quarantine plugin from the section of an object.

THE SECTION
    The quarantine plugin of utils/tools/quarantine/ writes the findings of a
    unit into the section .crucible.quarantine of its object (the head comment
    of quarantine.cpp, THE SECTION).  The section is text in UTF-8.  Its first
    line is "# crucible-quarantine 1 stamp=TEXT", and each other line is one
    finding:

        quarantine: KIND FILE:LINE:COLUMN ENTITY

    FILE is relative to the source root.  A report file of the plugin argument
    out= holds the same finding lines.

THE OBJECT
    read_section() reads an ELF64 object of a little-endian target, as each
    target of the tree is (CLAUDE.md section XIV).  It reads the section
    header table and the section names, and it follows the extended numbering
    of an object with 65,280 sections or more.  It reads no other part of the
    object.

Run this file with the paths of objects to print their finding lines, or with
--self-test to do a test of the module.
"""

from __future__ import annotations

import contextlib
import dataclasses
import io
import struct
import sys
import tempfile
from array import array
from pathlib import Path

SECTION_NAME = b".crucible.quarantine"
HEAD_PREFIX = "# crucible-quarantine "
FORMAT = 1
LINE_PREFIX = "quarantine: "
OPTED_OUT = "opted_out"
# A line of this kind is an opt-out region with its reason, and no finding.
REGION = "region"
NOT_FINDINGS = frozenset((OPTED_OUT, REGION))

ELF_MAGIC = b"\x7fELF"
ELF_CLASS_64 = 2
ELF_LITTLE_ENDIAN = 1
ELF_HEADER_SIZE = 64
SECTION_HEADER_SIZE = 64
SHN_XINDEX = 0xFFFF


class SectionError(ValueError):
    """An object or a section does not have the format that this module reads."""


@dataclasses.dataclass(frozen=True, slots=True, order=True)
class Finding:
    """One finding of the plugin: its kind, its place and its entity."""

    file: str
    line: int
    column: int
    kind: str
    entity: str

    def text(self) -> str:
        """Return the finding as one line of a section, with no line break."""
        return f"{LINE_PREFIX}{self.kind} {self.file}:{self.line}:{self.column} {self.entity}"


@dataclasses.dataclass(frozen=True, slots=True)
class Section:
    """The findings of one object, and the stamp of the compile that wrote them."""

    stamp: str
    findings: tuple[Finding, ...]


def parse_line(text: str, where: str) -> Finding:
    """Read one finding line of a section or of a report file.

    Raises:
        SectionError: If the line does not have the format
    """
    if not text.startswith(LINE_PREFIX):
        raise SectionError(f"{where}: the line {text[:120]!r} is not a finding of the quarantine plugin")
    words = text[len(LINE_PREFIX):].split(" ", 2)
    if len(words) < 2:
        raise SectionError(f"{where}: the line {text[:120]!r} has no place")
    place = words[1].rsplit(":", 2)
    if len(place) != 3 or not place[1].isdigit() or not place[2].isdigit() or not place[0]:
        raise SectionError(f"{where}: the place {words[1]!r} is not FILE:LINE:COLUMN")
    return Finding(place[0], int(place[1]), int(place[2]), words[0], words[2] if len(words) == 3 else "")


def parse_section(data: bytes, where: str) -> Section:
    """Read the text of one section.

    Complexity: linear in the size of the section.

    Raises:
        SectionError: If the text is not UTF-8, or its head line or a finding line does not have the format
    """
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError as problem:
        raise SectionError(f"{where}: the section is not UTF-8 ({problem})") from None
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines.pop()
    if not lines or not lines[0].startswith(HEAD_PREFIX):
        raise SectionError(f"{where}: the section has no head line {HEAD_PREFIX!r}")
    head = lines[0][len(HEAD_PREFIX):].split(" ", 1)
    if head[0] != str(FORMAT) or len(head) != 2 or not head[1].startswith("stamp="):
        raise SectionError(f"{where}: the head line {lines[0]!r} is not format {FORMAT} with a stamp")
    findings = tuple(parse_line(line, f"{where}: line {number}") for number, line in enumerate(lines[1:], start=2))
    return Section(head[1][len("stamp="):], findings)


def read_section(path: Path) -> bytes | None:
    """Return the bytes of the section .crucible.quarantine of one object, or None when it has none.

    Two sections with the name, as a relocatable link can make, give their
    bytes one after the other.  Complexity: linear in the number of sections.

    Raises:
        SectionError: If the file is not an ELF64 little-endian object, or its tables are cut short
        OSError: If the file cannot be read
    """
    with open(path, "rb") as stream:
        header = stream.read(ELF_HEADER_SIZE)
        if len(header) < ELF_HEADER_SIZE or header[:4] != ELF_MAGIC:
            raise SectionError(f"{path}: the file is not an ELF object")
        if header[4] != ELF_CLASS_64 or header[5] != ELF_LITTLE_ENDIAN:
            raise SectionError(f"{path}: the object is not ELF64 little-endian")
        (table_offset,) = struct.unpack_from("<Q", header, 0x28)
        entry_size, count, names_index = struct.unpack_from("<HHH", header, 0x3A)
        if table_offset == 0:
            return None
        if entry_size != SECTION_HEADER_SIZE:
            raise SectionError(f"{path}: a section header has {entry_size} bytes, not {SECTION_HEADER_SIZE}")
        stream.seek(table_offset)
        first = stream.read(SECTION_HEADER_SIZE)
        if len(first) < SECTION_HEADER_SIZE:
            raise SectionError(f"{path}: the section header table is cut short")
        if count == 0:
            (count,) = struct.unpack_from("<Q", first, 0x20)
        if names_index == SHN_XINDEX:
            (names_index,) = struct.unpack_from("<I", first, 0x28)
        stream.seek(table_offset)
        table = stream.read(count * SECTION_HEADER_SIZE)
        if len(table) < count * SECTION_HEADER_SIZE or names_index >= count:
            raise SectionError(f"{path}: the section header table is cut short")
        names_offset, names_size = struct.unpack_from("<QQ", table, names_index * SECTION_HEADER_SIZE + 0x18)
        stream.seek(names_offset)
        names = stream.read(names_size)
        wanted = SECTION_NAME + b"\0"
        offsets: set[int] = set()
        start = names.find(wanted)
        while start >= 0:
            offsets.add(start)
            start = names.find(wanted, start + 1)
        if not offsets:
            return None
        # sh_name is the first word of each 64-byte header.
        name_words = array("I")
        name_words.frombytes(table)
        if sys.byteorder != "little":
            name_words.byteswap()
        parts: list[bytes] = []
        for index, name in enumerate(name_words[::SECTION_HEADER_SIZE // 4]):
            if name in offsets:
                offset, size = struct.unpack_from("<QQ", table, index * SECTION_HEADER_SIZE + 0x18)
                stream.seek(offset)
                data = stream.read(size)
                if len(data) < size:
                    raise SectionError(f"{path}: the section {SECTION_NAME.decode()} is cut short")
                parts.append(data)
        return b"".join(parts) if parts else None


# ── The self-test ──────────────────────────────────────────────────


def make_object(sections: list[tuple[bytes, bytes]], extended: bool = False) -> bytes:
    """Return an ELF64 little-endian object with the named sections, for the self-test.

    With EXTENDED, the object uses the extended numbering of a large object:
    e_shnum and e_shstrndx are 0 and SHN_XINDEX, and section 0 holds them.
    """
    named = [(b"", b""), *sections, (b".shstrtab", b"")]
    names = b"\0"
    name_offsets = []
    for name, _ in named:
        if name:
            name_offsets.append(len(names))
            names += name + b"\0"
        else:
            name_offsets.append(0)
    payloads = [data for _, data in named[:-1]] + [names]
    body = b""
    data_offsets = []
    for data in payloads:
        data_offsets.append(ELF_HEADER_SIZE + len(body))
        body += data
    table_offset = ELF_HEADER_SIZE + len(body)
    count = len(named)
    names_index = count - 1
    header = bytearray(ELF_HEADER_SIZE)
    header[:6] = ELF_MAGIC + bytes([ELF_CLASS_64, ELF_LITTLE_ENDIAN])
    struct.pack_into("<Q", header, 0x28, table_offset)
    struct.pack_into("<HHH", header, 0x3A, SECTION_HEADER_SIZE, 0 if extended else count,
                     SHN_XINDEX if extended else names_index)
    table = b""
    for index in range(count):
        entry = bytearray(SECTION_HEADER_SIZE)
        struct.pack_into("<I", entry, 0, name_offsets[index])
        struct.pack_into("<QQ", entry, 0x18, data_offsets[index] if index else 0, len(payloads[index]))
        if index == 0 and extended:
            struct.pack_into("<Q", entry, 0x20, count)
            struct.pack_into("<I", entry, 0x28, names_index)
        table += entry
    return bytes(header) + body + table


def self_test() -> int:
    """Do a test of each function with planted objects and sections.

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
        except SectionError:
            expect(name, True)
            return
        expect(name, False)

    text = (b"# crucible-quarantine 1 stamp=00ab\n"
            b"quarantine: std_object test/a.cpp:3:7 std::vector (std::vector<int>)\n"
            b"quarantine: c_library_call include/crucible/B.h:9:1 memcpy\n")
    section = parse_section(text, "a.o")
    expect("parse_section reads the stamp", section.stamp == "00ab")
    expect("parse_section reads each finding",
           section.findings == (Finding("test/a.cpp", 3, 7, "std_object", "std::vector (std::vector<int>)"),
                                Finding("include/crucible/B.h", 9, 1, "c_library_call", "memcpy")))
    expect("a finding gives its line back", section.findings[0].text()
           == "quarantine: std_object test/a.cpp:3:7 std::vector (std::vector<int>)")
    expect("a section with the head line only has no finding",
           parse_section(b"# crucible-quarantine 1 stamp=\n", "a.o") == Section("", ()))
    refuses("parse_section refuses a section with no head line", lambda: parse_section(text.split(b"\n", 1)[1], "a.o"))
    refuses("parse_section refuses another format", lambda: parse_section(b"# crucible-quarantine 2 stamp=x\n", "a.o"))
    refuses("parse_section refuses a line that is not a finding",
            lambda: parse_section(b"# crucible-quarantine 1 stamp=x\nnoise\n", "a.o"))
    refuses("parse_section refuses a place with no column",
            lambda: parse_section(b"# crucible-quarantine 1 stamp=x\nquarantine: std_object a.cpp:3 x\n", "a.o"))
    refuses("parse_section refuses bytes that are not UTF-8",
            lambda: parse_section(b"# crucible-quarantine 1 stamp=x\n\xff\n", "a.o"))

    with tempfile.TemporaryDirectory(prefix="quarantine-sections-") as scratch_text:
        scratch = Path(scratch_text)
        plain = scratch / "plain.o"
        plain.write_bytes(make_object([(b".text", b"\x90"), (SECTION_NAME, text)]))
        expect("read_section finds the section", read_section(plain) == text)
        extended = scratch / "extended.o"
        extended.write_bytes(make_object([(b".text", b"\x90"), (SECTION_NAME, text)], extended=True))
        expect("read_section follows the extended numbering", read_section(extended) == text)
        absent = scratch / "absent.o"
        absent.write_bytes(make_object([(b".text", b"\x90"), (b".data", b"x")]))
        expect("read_section gives None for an object with no section", read_section(absent) is None)
        suffix = scratch / "suffix.o"
        suffix.write_bytes(make_object([(b".text", b"\x90"), (b".rela.crucible.quarantine", b"r")]))
        expect("read_section does not take a longer name that ends with the name", read_section(suffix) is None)
        joined = scratch / "joined.o"
        joined.write_bytes(make_object([(SECTION_NAME, b"one\n"), (SECTION_NAME, b"two\n")]))
        expect("read_section joins two sections with the name", read_section(joined) == b"one\ntwo\n")
        foreign = scratch / "foreign.o"
        foreign.write_bytes(b"!<arch>\n" + b"\0" * 64)
        refuses("read_section refuses a file that is not ELF", lambda: read_section(foreign))
        short = scratch / "short.o"
        short.write_bytes(make_object([(SECTION_NAME, text)])[:-10])
        refuses("read_section refuses a cut table", lambda: read_section(short))
        with contextlib.redirect_stdout(io.StringIO()) as printed, contextlib.redirect_stderr(io.StringIO()):
            status = print_objects([str(plain)])
            absent_status = print_objects([str(absent)])
        expect("the command prints the finding lines of an object",
               status == 0 and printed.getvalue() == "".join(f"{item.text()}\n" for item in section.findings))
        expect("the command fails for an object with no section", absent_status == 1)

    if failures:
        print(f"quarantine_sections --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("quarantine_sections --self-test: every case holds.")
    return 0


def print_objects(paths: list[str]) -> int:
    """Print the finding lines of the section of each object, in the order of the section.

    Returns:
        0 when each object holds a section, 1 when an object holds none or cannot be read
    """
    status = 0
    for name in paths:
        try:
            data = read_section(Path(name))
            if data is None:
                print(f"quarantine_sections: {name} holds no section {SECTION_NAME.decode()}", file=sys.stderr)
                status = 1
                continue
            for finding in parse_section(data, name).findings:
                print(finding.text())
        except (OSError, SectionError) as problem:
            print(f"quarantine_sections: {problem}", file=sys.stderr)
            status = 1
    return status


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    if sys.argv[1:] and not sys.argv[1].startswith("-"):
        sys.exit(print_objects(sys.argv[1:]))
    print("usage: quarantine_sections.py OBJECT... | --self-test", file=sys.stderr)
    sys.exit(2)
