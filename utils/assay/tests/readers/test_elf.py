"""The test of the reader of ELF files and archives (assay.readers.elf).

`guard selftest tests.readers.test_elf` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The planted cases write files with the writer of the module.  The files hold
sections, symbol tables, relocation tables, version tables and a dynamic
section.  They also hold group sections, a segment of the interpreter, a
segment of program properties and archives.  The compile cases use the
compiler that ASSAY_TEST_CXX names, and the registration of the test in
test/CMakeLists.txt gives the compiler of the build.  They compile small units,
link a shared library, and read the files that the compiler gives.
"""

import os
import struct
import sys
import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.core import proc, testing
from assay.readers import elf

CODE = elf.SHF_ALLOC | elf.SHF_EXECINSTR
# A unit with a throw, a weak undefined reference, a local function, a polymorphic class and a reference to data.
UNIT = """\
extern "C" [[gnu::weak]] void weak_target();
extern int shared_counter;
struct Shape { virtual ~Shape() = default; virtual int sides() const { return 0; } };
[[gnu::noinline]] static int local_twice(int value) { return value * 2; }
int throwing_entry(int value) {
    if (value < 0) { throw value; }
    if (weak_target) { weak_target(); }
    return local_twice(value) + shared_counter;
}
Shape* make_shape() { static Shape shape; return &shape; }
"""
# A unit of a shared library that calls the C library.  The library then gets libc.so.6 and a version of glibc.  Its
# constructor gives an entry of .init_array.
LINKED_UNIT = """\
#include <cstdio>
volatile int linked_state = 0;
[[gnu::constructor]] static void linked_init() { linked_state = 1; }
int report(int value) { return std::printf("%d\\n", value); }
"""


def refuses(action: Callable[[], object]) -> bool:
    """Return True when an action raises ElfError."""
    try:
        action()
    except elf.ElfError:
        return True
    return False


def planted_sections() -> list[elf.Planted]:
    """Return a code section, a data section, a section with no bytes in the file and a section of comments."""
    return [
        elf.Planted(".text", b"\x90\x90", flags=CODE),
        elf.Planted(".data", b"D", flags=elf.SHF_ALLOC),
        elf.Planted(".bss", kind=elf.SHT_NOBITS, flags=elf.SHF_ALLOC, size=4096),
        elf.Planted(".comment", b"C"),
    ]


def section_cases(expect: testing.Expect) -> None:
    """Check the section header table, the bytes of a section and each refused file."""
    plain = elf.make_file(planted_sections())
    member = elf.Member(plain, "plain")
    expect(
        "the parse reads each section with its name",
        [section.name for section in member.sections()] == ["", ".text", ".data", ".bss", ".comment", ".shstrtab"],
    )
    expect(
        "a code section and a data section are told apart",
        [section.name for section in member.sections() if section.is_code] == [".text"]
        and [section.name for section in member.sections() if section.is_data] == [".data"],
    )
    expect("data() gives the bytes of a section", member.data(member.named(".data")[0]) == b"D")
    expect("a section with no bytes in the file has an empty span", member.data(member.named(".bss")[0]) == b"")
    extended = elf.Member(elf.make_file(planted_sections(), extended=True), "x").sections()
    expect(
        "the extended numbering gives the same sections after section 0, which holds the count",
        extended[1:] == member.sections()[1:] and extended[0].size == len(extended),
    )
    expect(
        "a name that a longer name ends with is not that name",
        not elf.Member(elf.make_file([elf.Planted(".rela.text", b"r")]), "x").named(".text"),
    )
    expect("a file that is not ELF is refused", refuses(lambda: elf.Member(b"#!/bin/sh\n" + bytes(80), "script")))
    expect("a 32-bit file is refused", refuses(lambda: elf.Member(elf.MAGIC + b"\x01\x01" + bytes(80), "elf32")))
    expect("a big-endian file is refused", refuses(lambda: elf.Member(elf.MAGIC + b"\x02\x02" + bytes(80), "big")))
    damaged = bytearray(plain)
    struct.pack_into("<Q", damaged, elf.SECTION_TABLE_AT, len(damaged) + 100)
    expect("a section header table outside the file is refused", refuses(lambda: elf.Member(bytes(damaged), "x")))
    expect("a cut table is refused", refuses(lambda: elf.Member(plain[:-10], "cut")))
    outside = elf.Member(elf.make_file([elf.Planted(".data", b"", flags=elf.SHF_ALLOC)]), "outside")
    shifted = elf.Section(9, ".data", elf.SHT_PROGBITS, elf.SHF_ALLOC, len(outside.buffer), 8, 0, 0, 0)
    expect("a section outside the file is refused on read", refuses(lambda: outside.data(shifted)))
    same = elf.Section(9, ".data", elf.SHT_PROGBITS, elf.SHF_ALLOC, len(outside.buffer), 8, 0, 0, 0)
    is_same = shifted == same and hash(shifted) == hash(same)
    expect("two records with the same parts are equal and have one hash", is_same)
    expect("a record with a different part is different", shifted != elf.Section(8, ".data", 1, 2, 0, 8, 0, 0, 0))
    expect(
        "the text of a record names its class and its parts",
        repr(shifted).startswith("Section(address=0, entry_size=0, "),
    )
    expect("a record holds only the fields of its slots", has_no_field(shifted, "other"))


def has_no_field(section: elf.Section, field_name: str) -> bool:
    """Return True when an assignment to a name that is not a slot of a record raises AttributeError."""
    try:
        setattr(section, field_name, 0)
    except AttributeError:
        return True
    return False


def symbol_cases(expect: testing.Expect) -> None:
    """Check the symbols of a planted table: the null entry, each kind, and an index of SHT_SYMTAB_SHNDX."""
    table, strings = elf.symbol_table(
        [
            elf.PlantedSymbol("defined_function", size=12, kind=elf.STT_FUNC, section=1),
            elf.PlantedSymbol("strong_reference"),
            elf.PlantedSymbol("weak_reference", binding=elf.STB_WEAK),
            elf.PlantedSymbol("", kind=elf.STT_SECTION, binding=elf.STB_LOCAL, section=1),
            elf.PlantedSymbol("unit.cpp", kind=elf.STT_FILE, binding=elf.STB_LOCAL, section=0xFFF1),
            elf.PlantedSymbol("far_section", kind=elf.STT_OBJECT, section=elf.SHN_XINDEX),
        ]
    )
    extended = struct.pack("<7I", 0, 0, 0, 0, 0, 0, 70000)
    data = elf.make_file(
        [
            elf.Planted(".text", b"\x90" * 12, flags=CODE),
            elf.Planted(".symtab", table, elf.SHT_SYMTAB, link=3, entry_size=elf.SYMBOL_SIZE),
            elf.Planted(".strtab", strings, elf.SHT_STRTAB),
            elf.Planted(".symtab_shndx", extended, elf.SHT_SYMTAB_SHNDX, link=2, entry_size=4),
        ]
    )
    symbols = elf.Member(data, "symbols").symbols()
    expect(
        "symbols() gives the null entry first and each entry in order",
        [item.index for item in symbols] == list(range(7)),
    )
    expect(
        "an undefined symbol is a named entry with no section: strong and weak",
        [item.name for item in symbols if item.is_undefined] == ["strong_reference", "weak_reference"]
        and symbols[3].is_weak
        and not symbols[2].is_weak,
    )
    expect(
        "a defined symbol is named, has a section, and is not the symbol of a section or a file",
        [item.name for item in symbols if item.is_defined] == ["defined_function", "far_section"],
    )
    expect("a function symbol has its size and its kind", symbols[1].is_function and symbols[1].size == 12)
    expect("the table SHT_SYMTAB_SHNDX gives the section of a symbol with SHN_XINDEX", symbols[6].section == 70000)
    no_table = elf.Member(elf.make_file(planted_sections()), "x")
    expect("a file with no symbol table has no symbol", no_table.symbols() == ())


def tls_cases(expect: testing.Expect) -> None:
    """Check a section of thread-local storage and the symbol of a thread-local variable."""
    table, strings = elf.symbol_table(
        [
            elf.PlantedSymbol("thread_counter", size=8, kind=elf.STT_TLS, section=1),
            elf.PlantedSymbol("plain_counter", size=8, kind=elf.STT_OBJECT, section=2),
        ]
    )
    data = elf.make_file(
        [
            elf.Planted(".tbss", kind=elf.SHT_NOBITS, flags=elf.SHF_ALLOC | elf.SHF_TLS, size=8),
            elf.Planted(".bss", kind=elf.SHT_NOBITS, flags=elf.SHF_ALLOC, size=8),
            elf.Planted(".symtab", table, elf.SHT_SYMTAB, link=4, entry_size=elf.SYMBOL_SIZE),
            elf.Planted(".strtab", strings, elf.SHT_STRTAB),
        ]
    )
    member = elf.Member(data, "tls")
    expect(
        "a section with the flag SHF_TLS is a TLS section, and a plain section is not",
        [section.name for section in member.sections() if section.is_tls] == [".tbss"],
    )
    expect(
        "a symbol of the type STT_TLS is a thread-local variable, and an object symbol is not",
        [symbol.name for symbol in member.symbols() if symbol.is_tls] == ["thread_counter"],
    )


def relocation_cases(expect: testing.Expect) -> None:
    """Check the relocations of planted tables: the sequence of the targets, a section symbol, and an ignored table."""
    table, strings = elf.symbol_table(
        [
            elf.PlantedSymbol("", kind=elf.STT_SECTION, binding=elf.STB_LOCAL, section=1),
            elf.PlantedSymbol("__cxa_throw"),
        ]
    )
    text_entries = elf.relocation_table([(4, 2, 4, -4), (9, 1, 2, 16), (14, 0, 0, 0)])
    debug_entries = elf.relocation_table([(0, 2, 1, 0)])
    data = elf.make_file(
        [
            elf.Planted(".text.entry", b"\x90" * 20, flags=CODE),
            elf.Planted(".debug_info", b"\0" * 8),
            elf.Planted(".symtab", table, elf.SHT_SYMTAB, link=4, entry_size=elf.SYMBOL_SIZE),
            elf.Planted(".strtab", strings, elf.SHT_STRTAB),
            elf.Planted(".rela.debug_info", debug_entries, elf.SHT_RELA, link=3, entry_size=elf.RELA_SIZE, info=2),
            elf.Planted(".rela.text.entry", text_entries, elf.SHT_RELA, link=3, entry_size=elf.RELA_SIZE, info=1),
        ]
    )
    member = elf.Member(data, "relocations")
    found = member.relocations()
    expect(
        "the entries come in the order of the sections that they apply to",
        [item.section for item in found] == [".text.entry"] * 3 + [".debug_info"],
    )
    expect(
        "an entry names its symbol, a section symbol names its section, and no symbol names nothing",
        [(item.offset, item.symbol, item.kind, item.addend) for item in found[:3]]
        == [(4, "__cxa_throw", 4, -4), (9, ".text.entry", 2, 16), (14, "", 0, 0)],
    )
    expect(
        "an excluded prefix leaves out the entries of its sections",
        [item.section for item in member.relocations((".debug",))] == [".text.entry"] * 3,
    )
    rel = struct.pack("<QQ", 3, 2 << 32 | 2)
    rel_data = elf.make_file(
        [
            elf.Planted(".text", b"\x90" * 8, flags=CODE),
            elf.Planted(".symtab", table, elf.SHT_SYMTAB, link=3, entry_size=elf.SYMBOL_SIZE),
            elf.Planted(".strtab", strings, elf.SHT_STRTAB),
            elf.Planted(".rel.text", rel, elf.SHT_REL, link=2, entry_size=elf.REL_SIZE, info=1),
        ]
    )
    expect(
        "an entry of a SHT_REL table has the addend 0",
        [(item.symbol, item.addend) for item in elf.Member(rel_data, "rel").relocations()] == [("__cxa_throw", 0)],
    )


def version_tables() -> tuple[bytes, bytes, bytes, bytes, bytes]:
    """Return a dynamic table, its strings, and the tables SHT_GNU_versym, SHT_GNU_verdef and SHT_GNU_verneed."""
    table, strings = elf.symbol_table(
        [
            elf.PlantedSymbol("defined_v2", kind=elf.STT_FUNC, section=1),
            elf.PlantedSymbol("hidden_v3", kind=elf.STT_FUNC, section=1),
            elf.PlantedSymbol("needed_v4", kind=elf.STT_FUNC),
            elf.PlantedSymbol("plain", kind=elf.STT_FUNC, section=1),
        ]
    )
    names = strings + b"LIB_2\0LIB_3\0NEED_4\0libother.so\0"
    first = len(strings)
    indexes = struct.pack("<5H", 0, 2, 3 | elf.VERSION_HIDDEN, 4, 1)
    definitions = (
        elf.VERDEF_LAYOUT.pack(1, 0, 2, 1, 0, elf.VERDEF_LAYOUT.size, elf.VERDEF_LAYOUT.size + elf.VERDAUX_LAYOUT.size)
        + elf.VERDAUX_LAYOUT.pack(first, 0)
        + elf.VERDEF_LAYOUT.pack(1, 0, 3, 1, 0, elf.VERDEF_LAYOUT.size, 0)
        + elf.VERDAUX_LAYOUT.pack(first + 6, 0)
    )
    needs = elf.VERNEED_LAYOUT.pack(1, 1, first + 19, elf.VERNEED_LAYOUT.size, 0) + elf.VERNAUX_LAYOUT.pack(
        0, 0, 4, first + 12, 0
    )
    return table, names, indexes, definitions, needs


def version_cases(expect: testing.Expect) -> None:
    """Check the versions of planted dynamic symbols: from SHT_GNU_verdef, hidden, from SHT_GNU_verneed, and none."""
    table, names, indexes, definitions, needs = version_tables()
    data = elf.make_file(
        [
            elf.Planted(".text", b"\x90", flags=CODE),
            elf.Planted(".dynsym", table, elf.SHT_DYNSYM, link=3, entry_size=elf.SYMBOL_SIZE),
            elf.Planted(".dynstr", names, elf.SHT_STRTAB),
            elf.Planted(".gnu.version", indexes, elf.SHT_GNU_VERSYM, link=2, entry_size=2),
            elf.Planted(".gnu.version_d", definitions, elf.SHT_GNU_VERDEF, link=3, info=2),
            elf.Planted(".gnu.version_r", needs, elf.SHT_GNU_VERNEED, link=3, info=1),
        ]
    )
    symbols = elf.Member(data, "dynamic").dynamic_symbols()
    expect(
        "each dynamic symbol has the name of its version and its hidden bit",
        [(item.name, item.version, item.is_hidden_version) for item in symbols[1:]]
        == [
            ("defined_v2", "LIB_2", False),
            ("hidden_v3", "LIB_3", True),
            ("needed_v4", "NEED_4", False),
            ("plain", "", False),
        ],
    )
    expect("a symbol of .symtab has no version", elf.Member(data, "dynamic").symbols() == ())
    expect(
        "version_needs gives the file, the index, the weak flag and the name of each needed version",
        elf.Member(data, "dynamic").version_needs() == (elf.NeededVersion("libother.so", 4, False, "NEED_4"),),
    )
    strings = b"\0libc.so.6\0GLIBC_2.2.5\0GLIBC_2.34\0"
    two = elf.VERNEED_LAYOUT.pack(1, 2, 1, elf.VERNEED_LAYOUT.size, 0)
    two += elf.VERNAUX_LAYOUT.pack(0, 0, 2, 11, elf.VERNAUX_LAYOUT.size)
    two += elf.VERNAUX_LAYOUT.pack(0, elf.VER_FLG_WEAK, 3, 23, 0)
    weak = elf.make_file(
        [
            elf.Planted(".dynstr", strings, elf.SHT_STRTAB),
            elf.Planted(".gnu.version_r", two, elf.SHT_GNU_VERNEED, link=1, info=1),
        ]
    )
    expect(
        "an entry with two auxiliary entries gives two needed versions, and the second one is weak",
        elf.Member(weak, "weak").version_needs()
        == (
            elf.NeededVersion("libc.so.6", 2, False, "GLIBC_2.2.5"),
            elf.NeededVersion("libc.so.6", 3, True, "GLIBC_2.34"),
        ),
    )
    cut = elf.make_file(
        [
            elf.Planted(".dynstr", strings, elf.SHT_STRTAB),
            elf.Planted(".gnu.version_r", two[:-4], elf.SHT_GNU_VERNEED, link=1, info=1),
        ]
    )
    expect("a cut auxiliary entry is refused", refuses(lambda: elf.Member(cut, "cut").version_needs()))


def dynamic_cases(expect: testing.Expect) -> None:
    """Check dynamic() on a planted dynamic section: the string tags, a number tag, and the end at DT_NULL."""
    strings = b"\0libx.so.1\0/run/lib\0libself.so\0"
    entries = (
        elf.DYNAMIC_LAYOUT.pack(elf.DT_NEEDED, 1)
        + elf.DYNAMIC_LAYOUT.pack(elf.DT_RUNPATH, 11)
        + elf.DYNAMIC_LAYOUT.pack(elf.DT_SONAME, 20)
        + elf.DYNAMIC_LAYOUT.pack(30, 8)
        + elf.DYNAMIC_LAYOUT.pack(elf.DT_NULL, 0)
        + elf.DYNAMIC_LAYOUT.pack(elf.DT_NEEDED, 1)
    )
    data = elf.make_file(
        [
            elf.Planted(".dynamic", entries, elf.SHT_DYNAMIC, link=2, entry_size=elf.DYNAMIC_LAYOUT.size),
            elf.Planted(".dynstr", strings, elf.SHT_STRTAB),
        ]
    )
    expect(
        "dynamic gives each entry before DT_NULL, with the text of each string tag",
        elf.Member(data, "dynamic").dynamic()
        == (
            elf.DynamicEntry(elf.DT_NEEDED, 1, "libx.so.1"),
            elf.DynamicEntry(elf.DT_RUNPATH, 11, "/run/lib"),
            elf.DynamicEntry(elf.DT_SONAME, 20, "libself.so"),
            elf.DynamicEntry(30, 8, ""),
        ),
    )
    expect(
        "a file with no dynamic section has no entry",
        elf.Member(elf.make_file(planted_sections()), "x").dynamic() == (),
    )
    expect(
        "the directories of a run path are the parts between the colons, with no empty part",
        elf.DynamicEntry(elf.DT_RUNPATH, 0, "/a/lib::$ORIGIN:").directories() == ("/a/lib", "$ORIGIN"),
    )
    expect(
        "the parts of a run path keep each empty part",
        elf.DynamicEntry(elf.DT_RUNPATH, 0, "/a/lib::$ORIGIN:").parts() == ("/a/lib", "", "$ORIGIN", ""),
    )
    with tempfile.TemporaryDirectory(prefix="elf-origin-") as scratch_text:
        scratch = Path(scratch_text).resolve()
        (scratch / "bin").mkdir()
        (scratch / "lib").symlink_to(scratch / "bin")
        program = scratch / "lib" / "tool"
        expanded = {
            part: elf.expanded_origin(part, program)
            for part in ("/a/lib", "$ORIGIN", "${ORIGIN}/../lib", "$ORIGIN/x$ORIGIN", "$ORIGIN.d", "$ORIGIN_x")
        }
        expect(
            "$ORIGIN and ${ORIGIN} give the resolved directory of the object, and a part with no $ stays",
            expanded["/a/lib"] == "/a/lib"
            and expanded["$ORIGIN"] == f"{scratch}/bin"
            and expanded["${ORIGIN}/../lib"] == f"{scratch}/bin/../lib"
            and expanded["$ORIGIN/x$ORIGIN"] == f"{scratch}/bin/x{scratch}/bin"
            and expanded["$ORIGIN.d"] == f"{scratch}/bin.d",
        )
        expect(
            "a name character after $ORIGIN, $LIB, $PLATFORM and a lone $ give None",
            expanded["$ORIGIN_x"] is None
            and all(elf.expanded_origin(part, program) is None for part in ("$LIB", "/a/$PLATFORM/b", "/a$", "${LIB}")),
        )


def group_cases(expect: testing.Expect) -> None:
    """Check groups() on planted group sections, and each refused group."""
    table, strings = elf.symbol_table([elf.PlantedSymbol("_ZTV5Shape", kind=elf.STT_OBJECT, section=1)])

    def file_with(words: tuple[int, ...], info: int = 1) -> bytes:
        """Return a file with a data section, a group of the words, and the symbol table of its signature."""
        return elf.make_file(
            [
                elf.Planted(".data.rel.ro._ZTV5Shape", b"V", flags=elf.SHF_ALLOC),
                elf.Planted(".group", struct.pack(f"<{len(words)}I", *words), elf.SHT_GROUP, link=3, info=info),
                elf.Planted(".symtab", table, elf.SHT_SYMTAB, link=4, entry_size=elf.SYMBOL_SIZE),
                elf.Planted(".strtab", strings, elf.SHT_STRTAB),
            ]
        )

    expect(
        "groups gives the signature, the COMDAT flag and the members of a group",
        elf.Member(file_with((elf.GRP_COMDAT, 1)), "comdat").groups() == (elf.Group(2, "_ZTV5Shape", True, (1,)),),
    )
    expect(
        "a group with no COMDAT flag is not a COMDAT group",
        [item.is_comdat for item in elf.Member(file_with((0, 1)), "plain").groups()] == [False],
    )
    expect("a group with no flag word is refused", refuses(lambda: elf.Member(file_with(()), "empty").groups()))
    expect(
        "a signature outside the symbol table is refused",
        refuses(lambda: elf.Member(file_with((elf.GRP_COMDAT, 1), info=9), "far").groups()),
    )
    expect(
        "a member outside the section header table is refused",
        refuses(lambda: elf.Member(file_with((elf.GRP_COMDAT, 40)), "member").groups()),
    )


def archive_cases(expect: testing.Expect) -> None:
    """Check members() on planted archives: short and long names, the indexes, and each refused archive."""
    first = elf.make_file(planted_sections())
    second = elf.make_file([elf.Planted(".text", b"\x90", flags=CODE)])
    archive = elf.make_archive([("short.o", first), ("a_member_with_a_long_name.cpp.o", second + b"\0")])
    index_member = b"/".ljust(16) + b"0".ljust(32) + b"4".ljust(10) + elf.MEMBER_END + b"\0\0\0\0"
    indexed = archive.replace(elf.ARCHIVE_MAGIC, elf.ARCHIVE_MAGIC + index_member, 1)
    with tempfile.TemporaryDirectory(prefix="elf-test-") as scratch_text:
        scratch = Path(scratch_text)

        def written(name: str, data: bytes) -> Path:
            path = scratch / name
            path.write_bytes(data)
            return path

        members = elf.members(written("lib.a", indexed))
        expect(
            "members() gives each ELF member with its short or long name, and no index",
            [(member.name, member.where) for member in members]
            == [
                ("short.o", f"{scratch}/lib.a(short.o)"),
                ("a_member_with_a_long_name.cpp.o", f"{scratch}/lib.a(a_member_with_a_long_name.cpp.o)"),
            ],
        )
        expect(
            "a member reads its own sections inside the archive",
            [section.name for section in members[0].sections()]
            == [section.name for section in elf.Member(first, "x").sections()]
            and members[1].data(members[1].named(".text")[0]) == b"\x90",
        )
        plain_members = elf.members(written("one.o", first))
        expect("a plain file is its one member", [member.name for member in plain_members] == ["one.o"])
        expect("an archive with no member has no member", elf.members(written("empty.a", elf.ARCHIVE_MAGIC)) == ())
        expect("a thin archive is refused", refuses(lambda: elf.members(written("thin.a", elf.THIN_MAGIC))))
        expect(
            "a member that is not ELF is refused",
            refuses(lambda: elf.members(written("text.a", elf.make_archive([("notes.txt", b"plain text\n" * 8)])))),
        )
        expect("a cut member is refused", refuses(lambda: elf.members(written("cut.a", archive[:-40]))))
        damaged = bytearray(archive)
        damaged[len(elf.ARCHIVE_MAGIC) + 58] = ord("x")
        expect("a damaged member header is refused", refuses(lambda: elf.members(written("bad.a", bytes(damaged)))))
        expect(
            "a long name outside the table of long names is refused",
            refuses(lambda: elf.members(written("far.a", archive.replace(b"/0  ", b"/99 ", 1)))),
        )
        other = written("x", b"data")
        expect("a file that is not ELF and not an archive is refused", refuses(lambda: elf.members(other)))


def file_cases(expect: testing.Expect) -> None:
    """Check mapped(), is_elf() and interpreter() on planted files and on the program of this process."""
    with tempfile.TemporaryDirectory(prefix="elf-test-") as scratch_text:
        scratch = Path(scratch_text)
        plain = scratch / "plain.o"
        plain.write_bytes(elf.make_file(planted_sections()))
        with elf.mapped(plain) as mapped:
            expect("mapped() reads a file", mapped.data(mapped.named(".data")[0]) == b"D" and mapped.name == "plain.o")
        empty = scratch / "empty.o"
        empty.write_bytes(b"")
        expect("mapped() refuses an empty file", refuses(lambda: elf.mapped(empty).__enter__()))
        expect(
            "is_elf reads the magic",
            elf.is_elf(plain) and not elf.is_elf(empty) and not elf.is_elf(scratch / "absent.o"),
        )
        program = scratch / "program"
        program.write_bytes(elf.make_file(planted_sections(), interpreter_path="/planted/ld.so"))
        expect("interpreter() reads the segment PT_INTERP", elf.interpreter(program) == "/planted/ld.so")
        expect("a file with no PT_INTERP has no interpreter", elf.interpreter(plain) is None)
        with elf.mapped(program) as mapped:
            segments = mapped.segments()
        expect(
            "segments gives the one program header of the planted program",
            [(item.index, item.kind, item.file_size, item.memory_size) for item in segments]
            == [(0, elf.PT_INTERP, len(b"/planted/ld.so\0"), len(b"/planted/ld.so\0"))],
        )
        with elf.mapped(plain) as mapped:
            expect("a file with no program header table has no segment", mapped.segments() == ())
        script = scratch / "script"
        script.write_text("#!/bin/sh\n", encoding="utf-8")
        expect("a file that is not ELF has no interpreter", elf.interpreter(script) is None)
    found = elf.interpreter(os.path.realpath(sys.executable))
    expect("the program of this process has an interpreter that exists", found is not None and os.path.isfile(found))


def property_cases(expect: testing.Expect) -> None:
    """Read the program properties of a planted segment PT_GNU_PROPERTY, and refuse a note that is cut (THE PROPERTIES)."""
    note = elf.property_note([(0xC0000002, struct.pack("<I", 3)), (0xC0010001, struct.pack("<I", 9))])
    stack = elf.PlantedSegment(elf.PT_GNU_STACK, 6)
    data = elf.make_file(planted_sections(), segments=[elf.PlantedSegment(elf.PT_GNU_PROPERTY, 4, note), stack])
    member = elf.Member(data, "planted")
    found = member.properties()
    expect(
        "properties gives each property of the note in its sequence, and word gives its first four bytes",
        [(item.kind, item.word) for item in found] == [(0xC0000002, 3), (0xC0010001, 9)],
    )
    expect(
        "segments gives each planted segment with its type and its flags",
        [(item.kind, item.flags) for item in member.segments()] == [(elf.PT_GNU_PROPERTY, 4), (elf.PT_GNU_STACK, 6)],
    )
    # A note of the type 0 with no name and no descriptor: its header and the padding to the next multiple of 8.
    other = elf.make_file(planted_sections(), segments=[elf.PlantedSegment(elf.PT_GNU_PROPERTY, 4, b"\0" * 16)])
    expect(
        "a note of a different type gives no property",
        elf.Member(other, "other").properties() == () and elf.Member(elf.make_file([]), "bare").properties() == (),
    )
    expect("a property with fewer than four bytes has the word 0", elf.Property(1, b"\x01").word == 0)
    cut = elf.make_file(planted_sections(), segments=[elf.PlantedSegment(elf.PT_GNU_PROPERTY, 4, note[:-4])])
    expect("a note that is cut is refused", refuses(elf.Member(cut, "cut").properties))
    head = note[: elf.NOTE_LAYOUT.size + len(elf.GNU_NOTE_NAME)]
    damaged = head[:4] + struct.pack("<I", 16) + head[8:] + struct.pack("<II", 0xC0000002, 64) + bytes(8)
    torn = elf.make_file(planted_sections(), segments=[elf.PlantedSegment(elf.PT_GNU_PROPERTY, 4, damaged)])
    expect("a property that is longer than its note is refused", refuses(elf.Member(torn, "torn").properties))


def demangle_cases(expect: testing.Expect) -> None:
    """Check demangle(): a mangled name, a plain name, a clone, a standard abbreviation and no name."""
    names = ("_ZN3foo3barEi", "plain_name", "_ZN12_GLOBAL__N_14nameEv.cold", "_ZNKSs4sizeEv", ".text.unlikely._Z1fv")
    expect(
        "demangle gives the text of nm --demangle for each name, in order",
        elf.demangle(names)
        == (
            "foo::bar(int)",
            "plain_name",
            "(anonymous namespace)::name() [clone .cold]",
            "std::string::size() const",
            ".text.unlikely._Z1fv",
        ),
    )
    expect("demangle of no name starts no tool", elf.demangle(()) == ())


# Each name, with its first scope and the operator of a global operator new or delete (THE MANGLED SCOPE).
SCOPES = (
    ("_Znwm", "", "nw"),
    ("_ZdlPvm", "", "dl"),
    ("_ZnwmSt11align_val_t", "", "nw"),
    ("_ZdaPv", "", "da"),
    ("__cxa_throw", "", ""),
    ("_ZTVN10__cxxabiv120__si_class_type_infoE", "__cxxabiv1", ""),
    ("_ZSt9terminatev", "std", ""),
    ("_ZNKSt5ctypeIcE13_M_widen_initEv", "std", ""),
    ("_ZTISt9bad_alloc", "std", ""),
    ("_ZTTSt14basic_ifstreamIcSt11char_traitsIcEE", "std", ""),
    ("_ZNSaIcEC1Ev", "std", ""),
    ("_ZNSt3_V215system_categoryEv", "std", ""),
    ("_ZTSSt9bad_alloc", "std", ""),
    ("_ZNVKRSt3fooEv", "std", ""),
    ("_ZGVZN3app4mainEvE4seed", "app", ""),
    ("_ZZN10foundation14ThreadLocalRefImE8storage_EvE7storage", "foundation", ""),
    ("_ZZ4mainE5count", "main", ""),
    ("_ZThn8_N3app5ShapeD1Ev", "", ""),
    ("_ZN9__gnu_cxx27__verbose_terminate_handlerEv", "__gnu_cxx", ""),
    ("__once_proxy", "", ""),
    ("_ZN8crucible6detail5entryEv", "crucible", ""),
    ("_ZN4stdx1fEv", "stdx", ""),
    ("_ZN3std1fEv", "std", ""),
    ("_ZTW8the_seed", "the_seed", ""),
    ("_ZN", "", ""),
)


def scope_cases(expect: testing.Expect) -> None:
    """Check outer_scope() and global_operator() on each name of SCOPES."""
    for symbol, scope, operator in SCOPES:
        expect(f"the first scope of {symbol} is {scope or 'empty'}", elf.outer_scope(symbol) == scope)
        expect(f"the global operator of {symbol} is {operator or 'none'}", elf.global_operator(symbol) == operator)


def compile_cases(expect: testing.Expect) -> None:
    """Compile UNIT, put it in an archive with ar, and read its symbols and relocations."""
    cxx = testing.patched_compiler()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the compiler", bool(cxx) and Path(cxx).is_file())
    if not cxx or not Path(cxx).is_file():
        return
    with tempfile.TemporaryDirectory(prefix="elf-test-") as scratch_text:
        scratch = Path(scratch_text)
        (scratch / "unit.cpp").write_text(UNIT, encoding="utf-8")
        argv = [cxx, "-std=c++26", "-O1", "-ffunction-sections", "-c", "unit.cpp", "-o", "unit.o"]
        proc.require(proc.run(proc.Command(argv, timeout_s=proc.QUICK_S, cwd=scratch)))
        proc.require(proc.run(proc.Command(["ar", "rcs", "libunit.a", "unit.o"], timeout_s=proc.QUICK_S, cwd=scratch)))
        (member,) = elf.members(scratch / "libunit.a")
        undefined = {item.name: item for item in member.symbols() if item.is_undefined}
        expect(
            "the throw and the weak reference are undefined, and only the second one is weak",
            "__cxa_throw" in undefined
            and not undefined["__cxa_throw"].is_weak
            and "weak_target" in undefined
            and undefined["weak_target"].is_weak,
        )
        local = [item.name for item in member.symbols() if item.is_function and item.is_local]
        expect("the static function is a local function symbol", any("local_twice" in name for name in local))
        defined = [item.name for item in member.symbols() if item.is_defined]
        expect(
            "the class gives a defined typeinfo and a defined vtable",
            "typeinfo for Shape" in elf.demangle(defined) and "vtable for Shape" in elf.demangle(defined),
        )
        read = member.relocations((".debug", ".eh_frame"))
        throwing = [item for item in read if item.section.endswith("throwing_entryi")]
        expect(
            "the section of the throwing function holds the typeinfo of int before the call of __cxa_throw",
            [item.symbol for item in throwing if item.symbol in ("_ZTIi", "__cxa_throw")] == ["_ZTIi", "__cxa_throw"],
        )
        vtable = [item for item in member.groups() if item.signature == "_ZTV5Shape"]
        expect(
            "the vtable of the inline class is a COMDAT group with its section",
            len(vtable) == 1
            and vtable[0].is_comdat
            and any(member.sections()[index].name.endswith("_ZTV5Shape") for index in vtable[0].sections),
        )
        link_cases(expect, cxx, scratch)


def link_cases(expect: testing.Expect, cxx: str, scratch: Path) -> None:
    """Link LINKED_UNIT into a shared library with a name, a run path and libc, and read its dynamic parts."""
    (scratch / "linked.cpp").write_text(LINKED_UNIT, encoding="utf-8")
    compile_argv = [cxx, "-std=c++26", "-O1", "-fPIC", "-c", "linked.cpp", "-o", "linked.o"]
    proc.require(proc.run(proc.Command(compile_argv, timeout_s=proc.QUICK_S, cwd=scratch)))
    argv = [
        cxx,
        "-shared",
        "linked.o",
        "-o",
        "libunit.so",
        "-Wl,-soname,libunit.so.1,--enable-new-dtags,-rpath,/planted/run",
    ]
    proc.require(proc.run(proc.Command(argv, timeout_s=proc.QUICK_S, cwd=scratch)))
    with elf.mapped(scratch / "libunit.so") as library:
        entries = library.dynamic()
        kinds = {item.kind for item in library.segments()}
        needs = library.version_needs()
        file_type, machine, init = library.file_type(), library.machine(), library.init_functions()
        text = library.named(".text")
    expect(
        "the shared library has the type ET_DYN, a machine of the tree and a .text at an address",
        file_type == elf.ET_DYN and machine in elf.RELATIVE_TYPES and bool(text) and text[0].address > 0,
    )
    expect(
        "the init array of the shared library names the constructor and frame_dummy",
        "_ZL11linked_initv" in init and "frame_dummy" in init,
    )
    tagged = {(item.tag, item.text) for item in entries}
    expect(
        "the dynamic section names libc.so.6, the soname and the run path",
        {(elf.DT_NEEDED, "libc.so.6"), (elf.DT_SONAME, "libunit.so.1"), (elf.DT_RUNPATH, "/planted/run")} <= tagged,
    )
    expect("the shared library has a segment PT_LOAD and a segment PT_DYNAMIC", {elf.PT_LOAD, elf.PT_DYNAMIC} <= kinds)
    expect(
        "the shared library needs a version of glibc from libc.so.6",
        any(item.file == "libc.so.6" and item.name.startswith("GLIBC_") for item in needs),
    )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    groups = (
        section_cases,
        symbol_cases,
        tls_cases,
        relocation_cases,
        version_cases,
        dynamic_cases,
        group_cases,
        archive_cases,
        file_cases,
        property_cases,
        demangle_cases,
        scope_cases,
        compile_cases,
    )
    return testing.run(__name__.removeprefix("assay."), argv, groups)
