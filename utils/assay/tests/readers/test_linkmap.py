"""The test of the reader of the map of a link (assay.readers.linkmap).

`guard selftest tests.readers.test_linkmap` runs the test.  Each case writes
one line.  Exit 0 when each case holds, else 1.

The planted cases read texts with each shape of THE LINES OF AN ITEM and of
THE MERGES.  The link case links a small program with the compiler that
ASSAY_TEST_CXX names, with -Wl,-Map, and reads the map that GNU ld writes.
The registration of the test in test/CMakeLists.txt gives the compiler of the
build.
"""

import tempfile
from collections.abc import Callable
from functools import partial
from pathlib import Path

from assay.core import proc, testing
from assay.readers import linkmap

ARCHIVE = "/toolchain/lib/libgcc.a"
LONG_MEMBER = "/a/very/long/path/of/the/toolchain/lib/libstdc++exp.a(shared_ptr_control.o)"
# A map with each part that the reader reads, and one part that it ignores.
PLANTED = f"""\
Archive member included to satisfy reference by file (symbol)

{ARCHIVE}(cpuinfo.o)          unit.o (__cpu_model)
{LONG_MEMBER}
                              unit.o (_ZNSt3__16shared_count7releaseEv)

As-needed library included to satisfy reference by file (symbol)

libbpf.so.1                   perf.o (bpf_object__open)

Merging program properties

Removed property 0xc0000002 to merge crt1.o (0x3) and unit.o (not found)
Removed property 0xc0000002 to merge crt1.o (not found) and {ARCHIVE}(cpuinfo.o) (0x3)
Updated property 0xc0010001 (0x9) to merge crt1.o (0x1) and unit.o (0x9)
Removed property 0xc0008002 to merge crt1.o and unit.o

Discarded input sections

 .data          0x0000000000000000        0x4 /lib64/crt1.o
 .text._Z12unused_entryv
                0x0000000000000000       0x1a unit.o

Memory Configuration

Name             Origin             Length             Attributes
*default*        0x0000000000000000 0xffffffffffffffff

Linker script and memory map

LOAD /lib64/crt1.o
LOAD unit.o
START GROUP
LOAD /lib64/libc.so.6
END GROUP
                [!provide]                        PROVIDE (__executable_start = SEGMENT_START ("text-segment", 0x400000))

.text           0x0000000000401000      0x200
 *(.text.unlikely .text.*_unlikely .text.unlikely.*)
 .text.startup  0x0000000000401000      0x154 {ARCHIVE}(cpuinfo.o)
                0x0000000000401000                __cpu_indicator_init
 *fill*         0x0000000000401154        0xc
 .text._ZNSt3__16shared_count7releaseEv
                0x0000000000401160       0x20 {LONG_MEMBER}
                0x0000000000401160                _ZNSt3__16shared_count7releaseEv

.gnu.version_r
                0x0000000000402000       0x30
 .gnu.version_r
                0x0000000000402000       0x30 /lib64/crt1.o
OUTPUT(program elf64-x86-64)

Cross Reference Table

Symbol                                            File
main                                              unit.o
"""
# A sample program for the link case: a member of libgcc.a, a symbol of libstdc++ and a function that --gc-sections
# removes.
UNIT = """\
#include <cstdio>
#include <string>
[[gnu::noinline]] int unused_entry() { return 3; }
int main(int argc, char** argv) {
    std::string word(argv[0]);
    std::printf("%zu\\n", word.size() + static_cast<std::size_t>(argc));
    return __builtin_cpu_supports("avx2") ? 0 : 1;
}
"""


def refuses(action: Callable[[], object]) -> bool:
    """Return True when an action raises LinkMapError."""
    try:
        action()
    except linkmap.LinkMapError:
        return True
    return False


def planted_cases(expect: testing.Expect) -> None:
    """Read PLANTED, and check each part and each line shape."""
    found = linkmap.parse(PLANTED, "planted.map")
    expect(
        "each archive member has its archive, its member, its file and its symbol, on one line or two lines",
        found.members
        == (
            linkmap.Inclusion(ARCHIVE, "cpuinfo.o", "unit.o", "__cpu_model"),
            linkmap.Inclusion(
                LONG_MEMBER.partition("(")[0], "shared_ptr_control.o", "unit.o", "_ZNSt3__16shared_count7releaseEv"
            ),
        ),
    )
    expect(
        "a library of --as-needed has an empty archive and its path",
        found.libraries == (linkmap.Inclusion("", "libbpf.so.1", "perf.o", "bpf_object__open"),),
    )
    expect(
        "the discarded sections have no output section, on one line or two lines",
        found.discarded
        == (
            linkmap.InputSection("", ".data", 0, 4, "/lib64/crt1.o"),
            linkmap.InputSection("", ".text._Z12unused_entryv", 0, 0x1A, "unit.o"),
        ),
    )
    expect(
        "the loads come in the sequence of the link, and the group marks are no load",
        found.loads == ("/lib64/crt1.o", "unit.o", "/lib64/libc.so.6"),
    )
    expect(
        "each input section has its output section, and no fill, rule, symbol or assignment is a section",
        found.sections
        == (
            linkmap.InputSection(".text", ".text.startup", 0x401000, 0x154, f"{ARCHIVE}(cpuinfo.o)"),
            linkmap.InputSection(".text", ".text._ZNSt3__16shared_count7releaseEv", 0x401160, 0x20, LONG_MEMBER),
            linkmap.InputSection(".gnu.version_r", ".gnu.version_r", 0x402000, 0x30, "/lib64/crt1.o"),
        ),
    )
    expect(
        "each merge of a program property has its action, its type, the merged value of an update, and the two files "
        "with their values, or None for (not found) and for no value",
        found.merges
        == (
            linkmap.PropertyMerge("Removed", 0xC0000002, None, "crt1.o", 3, "unit.o", None),
            linkmap.PropertyMerge("Removed", 0xC0000002, None, "crt1.o", None, f"{ARCHIVE}(cpuinfo.o)", 3),
            linkmap.PropertyMerge("Updated", 0xC0010001, 9, "crt1.o", 1, "unit.o", 9),
            linkmap.PropertyMerge("Removed", 0xC0008002, None, "crt1.o", None, "unit.o", None),
        ),
    )
    with tempfile.TemporaryDirectory(prefix="linkmap-merges-") as scratch_text:
        planted_map = Path(scratch_text) / "planted.map"
        planted_map.write_text(PLANTED, encoding="utf-8")
        merges = linkmap.read_merges(planted_map)
        no_part = Path(scratch_text) / "no-part.map"
        no_part.write_text(PLANTED.replace(linkmap.PROPERTIES, "Merging object attributes"), encoding="utf-8")
        no_merges = linkmap.read_merges(no_part)
        (Path(scratch_text) / "empty.map").write_text("no heading\n", encoding="utf-8")
        refused_empty = refuses(lambda: linkmap.read_merges(Path(scratch_text) / "empty.map"))
    expect("read_merges gives the merges of parse", merges == found.merges)
    expect("read_merges gives no merge for a map with no part of the merges", no_merges == ())
    expect("read_merges refuses a file with no heading of a map", refused_empty)
    for wrong in (
        "Removed thing 0xc0000002 to merge a.o and b.o",
        "Removed property 0xc0000002 to merge a.o (0x3) or b.o",
        "Removed property 0xc0000002 to merge a.o (0x3) and b.o (0x3) more",
        "Removed property 0xc0000002 to merge",
        "Moved property 0xc0000002 to merge a.o and b.o",
        "a note of the linker",
    ):
        text = PLANTED.replace("Updated property 0xc0010001 (0x9) to merge crt1.o (0x1) and unit.o (0x9)", wrong)
        expect(f"the line {wrong!r} of the merges is refused", refuses(partial(linkmap.parse, text, "merge")))
    expect(
        "split_member gives the archive and the member, and an empty archive for a plain path",
        linkmap.split_member("/lib/libx.a(y.o)") == ("/lib/libx.a", "y.o")
        and linkmap.split_member("z.o") == ("", "z.o"),
    )
    expect("a hexadecimal number has the prefix 0x and a digit", linkmap.is_hex("0x1F") and not linkmap.is_hex("0x"))
    expect("a text with no script heading is refused", refuses(lambda: linkmap.parse("LOAD x.o\n", "plain")))
    cut = PLANTED.replace("unit.o (__cpu_model)", "unit.o __cpu_model")
    expect("a reference with no symbol in parentheses is refused", refuses(lambda: linkmap.parse(cut, "cut")))
    lone = f"{linkmap.INCLUDED}\n\n                              unit.o (x)\n\n{linkmap.SCRIPT}\n"
    expect("a reference line with no item before it is refused", refuses(lambda: linkmap.parse(lone, "lone")))
    damaged = PLANTED.replace("0x154", "size")
    expect("an input section with no size is refused", refuses(lambda: linkmap.parse(damaged, "damaged")))
    open_end = f"{linkmap.SCRIPT}\n\n.text\n .text.a_long_input_section_name\n"
    expect("an input section with no next line at the end is refused", refuses(lambda: linkmap.parse(open_end, "end")))


def link_cases(expect: testing.Expect) -> None:
    """Link UNIT with -Wl,-Map and --gc-sections, and read the map that GNU ld writes."""
    cxx = testing.patched_compiler()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the compiler", bool(cxx) and Path(cxx).is_file())
    if not cxx or not Path(cxx).is_file():
        return
    with tempfile.TemporaryDirectory(prefix="linkmap-test-") as scratch_text:
        scratch = Path(scratch_text)
        (scratch / "unit.cpp").write_text(UNIT, encoding="utf-8")
        argv = [
            cxx,
            "-std=c++26",
            "-O1",
            "-ffunction-sections",
            "unit.cpp",
            "-o",
            "program",
            "-Wl,-Map=program.map,--gc-sections,--no-demangle",
        ]
        proc.require(proc.run(proc.Command(argv, timeout_s=proc.QUICK_S, cwd=scratch)))
        found = linkmap.read(scratch / "program.map")
        head = linkmap.read_members(scratch / "program.map")
        merges = linkmap.read_merges(scratch / "program.map")
        (scratch / "empty.map").write_text("no heading\n", encoding="utf-8")
        refused_empty = refuses(lambda: linkmap.read_members(scratch / "empty.map"))
    expect("read_members gives the archive members of read, and stops after them", head == found.members)
    expect("read_members refuses a file with no heading of a map", refused_empty)
    expect(
        "the unit has no control-flow flag, so the link removes the feature property of the processor, and "
        "read_merges gives the merges of read",
        merges == found.merges
        and any(item.action == "Removed" and item.kind in (0xC0000002, 0xC0000000) for item in merges),
    )
    members = {(Path(item.archive).name, item.member, item.symbol) for item in found.members}
    expect("the map names cpuinfo.o of libgcc.a for __cpu_model", ("libgcc.a", "cpuinfo.o", "__cpu_model") in members)
    loaded = [Path(item).name for item in found.loads]
    expect(
        "the loads hold the start file, the libraries and the end file in the sequence of the link",
        bool(loaded)
        and loaded[0] == "crt1.o"
        and "libstdc++.so" in loaded
        and "libc.so.6" in loaded
        and loaded[-1] == "crtn.o",
    )
    expect(
        "--gc-sections discards the unused function, with its mangled name",
        any(item.name == ".text._Z12unused_entryv" for item in found.discarded),
    )
    expect(
        "the code of cpuinfo.o is an input section of .text with its size",
        any(
            item.output == ".text" and item.file.endswith("(cpuinfo.o)") and item.length > 0 for item in found.sections
        ),
    )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (planted_cases, link_cases))
