"""The test of the reader of DWARF 5 (assay.readers.dwarf).

`guard selftest tests.readers.test_dwarf` runs the test.  Each case writes
one line.  Exit 0 when each case holds, else 1.

The planted cases read small debug sections that the test writes, with each
refused shape.  The compile cases compile a unit with the compiler that
ASSAY_TEST_CXX names, and read its code bodies.  The registration of the
test in test/CMakeLists.txt gives the compiler of the build.
"""

import struct
import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.core import proc, testing
from assay.readers import dwarf, elf

# Two abbreviations: a unit with children and a name, and a function with a name, an address and a length.
ABBREVIATIONS = bytes(
    [1, 0x11, 1, 0x03, 0x08, 0, 0]
    + [2, dwarf.TAG_SUBPROGRAM, 0, 0x03, 0x08, dwarf.AT_LOW_PC, dwarf.FORM_ADDR, dwarf.AT_HIGH_PC, dwarf.FORM_DATA4]
    + [0, 0, 0]
)
# The offset of the address of the function in the planted unit.  The header of 12 bytes and the unit entry come
# first.  Then come the code 2 and the name "f".
ADDRESS_AT = 12 + 1 + 3 + 1 + 2
# A unit with a static function, a template that a COMDAT group holds, and the compare operator of std::string_view.
UNIT = """\
#include <string_view>
template <class T> [[gnu::noinline]] bool planted_compare(T left, T right) { return left == right; }
static int planted_helper(int value) { return value * 3 + 1; }
int planted_entry(std::string_view left, std::string_view right, int value) {
    return planted_compare(left, right) ? planted_helper(value) : value;
}
"""
MAIN = """\
#include <string_view>
int planted_entry(std::string_view left, std::string_view right, int value);
int main(int count, char** words) { return planted_entry(words[0], "x", count); }
"""


def planted_info(version: int = 5, code: int = 2, length: int = 0) -> bytes:
    """Return a planted .debug_info: one unit "cu" with one function "f" of 16 bytes at the address 0.

    `version`, `code` and `length` change the version, the code of the function entry and the unit length.
    """
    entries = b"\x01cu\0" + bytes([code]) + b"f\0" + bytes(8) + struct.pack("<I", 16) + b"\0"
    body = struct.pack("<HBBI", version, 1, 8, 0) + entries
    return struct.pack("<I", length or len(body)) + body


def planted_member(info: bytes, abbreviations: bytes = ABBREVIATIONS, relocation_kind: int = 0) -> elf.Member:
    """Return an object with a planted .debug_info and .debug_abbrev.

    A `relocation_kind` adds a relocation of that kind at the address of the function, with the symbol of the
    section .text.planted (index 1).
    """
    sections = [elf.Planted(".text.planted", bytes(16), flags=elf.SHF_ALLOC | elf.SHF_EXECINSTR)]
    sections += [elf.Planted(dwarf.INFO, info), elf.Planted(dwarf.ABBREV, abbreviations)]
    if relocation_kind:
        table, strings = elf.symbol_table([elf.PlantedSymbol("", kind=elf.STT_SECTION, binding=0, section=1)])
        sections += [
            elf.Planted(".symtab", table, elf.SHT_SYMTAB, link=5, entry_size=elf.SYMBOL_SIZE, info=2),
            elf.Planted(".strtab", strings, elf.SHT_STRTAB),
            elf.Planted(
                ".rela.debug_info",
                elf.relocation_table([(ADDRESS_AT, 1, relocation_kind, 4)]),
                elf.SHT_RELA,
                link=4,
                entry_size=elf.RELA_SIZE,
                info=2,
            ),
        ]
    return elf.Member(elf.make_file(sections), "planted.o")


def refuses(action: Callable[[], object]) -> bool:
    """Return True when an action raises DwarfError."""
    try:
        action()
    except dwarf.DwarfError:
        return True
    return False


def refuses_bodies(member: elf.Member) -> bool:
    """Return True when the read of the code bodies of a member raises DwarfError."""
    return refuses(lambda: dwarf.code_bodies(member))


def number_cases(expect: testing.Expect) -> None:
    """Hold the LEB128 numbers and the strings with a zero byte at their end."""
    expect("an unsigned LEB128 number has seven bits in each byte", dwarf.uleb(b"\xe5\x8e\x26", 0) == (624485, 3))
    expect(
        "a signed LEB128 number takes the sign of its last byte",
        dwarf.sleb(b"\xc0\xbb\x78", 0) == (-123456, 3) and dwarf.sleb(b"\x7f", 0) == (-1, 1),
    )
    expect("a string ends at its zero byte", dwarf.text_at(b"ab\0cd", 0) == ("ab", 3))
    try:
        dwarf.text_at(b"ab", 0)
        has_raised = False
    except IndexError:
        has_raised = True
    expect("a string with no zero byte is cut", has_raised)


def planted_cases(expect: testing.Expect) -> None:
    """Read the planted unit, and refuse each damaged unit."""
    member = planted_member(planted_info())
    reader = dwarf.Dwarf(member)
    units = reader.units()
    dies = units[0].dies() if units else ()
    expect(
        "the planted unit has its entry and its function, with the depth and the parent of each",
        [(die.tag, die.depth, die.parent, die.get(dwarf.AT_NAME)) for die in dies]
        == [(0x11, 0, -1, "cu"), (dwarf.TAG_SUBPROGRAM, 1, 0, "f")],
    )
    expect(
        "an address with no relocation has no section, and a length gives the bytes of the code",
        len(dies) == 2 and units[0].code_range(dies[1]) == (16, dwarf.NO_SECTION),
    )
    expect(
        "a function with a name and no file is a body with its bytes",
        dwarf.code_bodies(member) == (dwarf.Body("f", "", False, False, 16, 16, dwarf.NO_SECTION),),
    )
    relocated = dwarf.Dwarf(planted_member(planted_info(), relocation_kind=1))
    address = relocated.units()[0].dies()[1].get(dwarf.AT_LOW_PC)
    expect(
        "an absolute relocation gives the address S + A and the section of its symbol",
        address == dwarf.Address(4, 1),
    )
    wrong = planted_member(planted_info(), relocation_kind=2)
    expect("a relocation of a different kind at a read offset is refused", refuses_bodies(wrong))
    expect("a unit of DWARF 4 is refused", refuses_bodies(planted_member(planted_info(version=4))))
    expect(
        "an entry with an abbreviation that the table does not hold is refused",
        refuses_bodies(planted_member(planted_info(code=9))),
    )
    unknown = ABBREVIATIONS.replace(bytes([dwarf.AT_HIGH_PC, dwarf.FORM_DATA4]), bytes([dwarf.AT_HIGH_PC, 0x77]))
    expect("an attribute of an unknown form is refused", refuses_bodies(planted_member(planted_info(), unknown)))
    expect("a unit that ends after the section is refused", refuses_bodies(planted_member(planted_info(length=200))))
    twice = elf.Member(
        elf.make_file([elf.Planted(dwarf.INFO, planted_info()), elf.Planted(dwarf.INFO, planted_info())]), "two.o"
    )
    expect("a file with two sections .debug_info is refused", refuses(lambda: dwarf.Dwarf(twice)))
    bare = elf.Member(elf.make_file([elf.Planted(".text", bytes(4))]), "bare.o")
    expect("a file with no .debug_info has no body", dwarf.code_bodies(bare) == ())


def compile_cases(expect: testing.Expect) -> None:
    """Compile UNIT with -g, read its bodies, and link a program to read the bodies of a linked file."""
    cxx = testing.patched_compiler()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the compiler", bool(cxx) and Path(cxx).is_file())
    if not cxx or not Path(cxx).is_file():
        return
    with tempfile.TemporaryDirectory(prefix="dwarf-test-") as scratch_text:
        scratch = Path(scratch_text)
        (scratch / "unit.cpp").write_text(UNIT, encoding="utf-8")
        (scratch / "main.cpp").write_text(MAIN, encoding="utf-8")
        flags = ["-std=c++17", "-O1", "-g", "-ffunction-sections"]
        for name in ("unit", "main"):
            argv = [cxx, *flags, "-c", f"{name}.cpp", "-o", f"{name}.o"]
            proc.require(proc.run(proc.Command(argv, timeout_s=proc.QUICK_S, cwd=scratch)))
        argv = [cxx, "unit.o", "main.o", "-o", "program"]
        proc.require(proc.run(proc.Command(argv, timeout_s=proc.QUICK_S, cwd=scratch)))
        (member,) = elf.members(scratch / "unit.o")
        bodies = dwarf.code_bodies(member)
        files = dwarf.Dwarf(member).units()[0].files()
        comdat = {section for group in member.groups() if group.is_comdat for section in group.sections}
        with elf.mapped(scratch / "program") as program:
            linked = dwarf.code_bodies(program)
    named = {body.name: body for body in bodies if not body.is_inlined}
    entry = next((body for name, body in named.items() if name.startswith("_Z13planted_entry")), None)
    expect(
        "the function planted_entry is a concrete body of the unit, with its file",
        entry is not None and entry.file.endswith("/unit.cpp") and entry.length >= entry.exclusive > 0,
    )
    template = next((body for name, body in named.items() if "planted_compare" in name), None)
    expect(
        "the instance of the template is in a section of a COMDAT group",
        template is not None and template.section in comdat,
    )
    expect(
        "an inlined body of libstdc++ has its header as its file",
        any(body.is_inlined and "/include/c++/" in body.file for body in bodies),
    )
    expect("the file names of the unit hold the unit", any(item.endswith("/unit.cpp") for item in files))
    expect("each body has its exclusive bytes in its bytes", all(0 <= body.exclusive <= body.length for body in bodies))
    expect(
        "a linked file gives its bodies with no section",
        bool(linked) and all(body.section == dwarf.NO_SECTION for body in linked),
    )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (number_cases, planted_cases, compile_cases))
