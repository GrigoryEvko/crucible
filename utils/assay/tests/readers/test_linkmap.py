"""The test of the reader of the map of a link (assay.readers.linkmap).

`guard selftest tests.readers.test_linkmap` runs the test.  Each case writes
one line.  Exit 0 when each case holds, else 1.

The planted cases read lines of the maps that mold 3.0.0 wrote of links of
the tree, with each kind of line of THE MAP.  They also read lines with
each shape error.  The link case links a small program with the compiler that
ASSAY_TEST_CXX names and the pinned mold (assay.setup.install), with
-Wl,-Map, and reads the map.  The registration of the test in
test/CMakeLists.txt gives the compiler of the build.
"""

import tempfile
from collections.abc import Callable
from functools import partial
from pathlib import Path

from assay.core import proc, testing
from assay.readers import linkmap

ARCHIVE = "/toolchain/lib/gcc/x86_64-pc-linux-gnu/16.2.1/libgcc.a"
UNIT = "CMakeFiles/crucible_hwprobe.dir/utils/tools/crucible_hwprobe.cpp.o"
# Lines of mold maps of the tree (THE MAP).  The output chunks have the address 0 or an address.  The input sections
# name plain files and members of archives, and the symbols hold spaces and parentheses.  One output section has an
# `Align` of 2 MiB, which follows its `Size` with no space (THE COLUMNS).
PLANTED = (
    linkmap.HEADING,
    "                 0         64     8 EHDR",
    "             0x318         28     1 .interp",
    "             0x388         32     4 .note.ABI-tag",
    "             0x388         32     4         /lib64/Scrt1.o:(.note.ABI-tag)",
    "             0x388          0     0                 __abi_tag",
    "            0x7a8c        741     4 .gcc_except_table",
    f"            0x7a8c          4     1         {UNIT}:(.gcc_except_table._ZN4fixy10concurrent8TopologyD2Ev)",
    "            0x7c63          4     1         libcrucible_perf.a(SenseHub.cpp.o):(.gcc_except_table.main)",
    "            0x7c67          4     1         libcrucible_perf.a(SenseHub.cpp.o):(.gcc_except_table._ZN8SenseHub)",
    "            0xcd80          0     0                 std::basic_string_view<char> f<crucible::ledger::LedgerError>()",
    "            0xcdc0          0     0                 (anonymous namespace)::kWantedVerdicts",
    "           0x13e00       1024    64 .text",
    f"           0x13e00         64    16         {ARCHIVE}(cpuinfo.o):(.text.startup)",
    f"           0x13e40         32    16         {UNIT}:(.text.main)",
    "          0x400000    84076162097152 .bss",
    "          0x400000          1     1         libfixy.a(Signal.cpp.o):(.bss)",
    "          0x400000          0     0                 completed.0",
    "                 0       4096     1 .debug_info",
    "                 0       2048     1         /usr/lib64/libc_nonshared.a(atexit.oS):(.debug_info)",
)
# A sample program for the link case: a member of libgcc.a and a symbol of libstdc++.
SOURCE = """\
#include <cstdio>
#include <string>
int main(int argc, char** argv) {
    std::string word(argv[0]);
    std::printf("%zu\\n", word.size() + static_cast<std::size_t>(argc));
    return __builtin_cpu_supports("avx2") ? 0 : 1;
}
"""


def refused(action: Callable[[], object]) -> str:
    """Return the text of the LinkMapError of an action, or an empty text when the action raises none."""
    try:
        action()
    except linkmap.LinkMapError as problem:
        return str(problem) or "refused"
    return ""


def with_line(text: str) -> tuple[str, ...]:
    """Return PLANTED with one more line after its first output chunk."""
    return (*PLANTED[:2], text, *PLANTED[2:])


def planted_cases(expect: testing.Expect) -> None:
    """Read PLANTED, and hold each kind of line and each shape error (THE MAP, THE INPUTS)."""
    inputs = linkmap.inputs_of(PLANTED, "planted.map")
    expect(
        "the inputs are the files of the input sections, each one time, in the sequence of their first section",
        inputs
        == (
            "/lib64/Scrt1.o",
            UNIT,
            "libcrucible_perf.a(SenseHub.cpp.o)",
            f"{ARCHIVE}(cpuinfo.o)",
            "libfixy.a(Signal.cpp.o)",
            "/usr/lib64/libc_nonshared.a(atexit.oS)",
        ),
    )
    expect(
        "the members are the inputs of the shape ARCHIVE(MEMBER), with a relative or an absolute archive",
        linkmap.members_of(inputs)
        == (
            linkmap.ArchiveMember("libcrucible_perf.a", "SenseHub.cpp.o"),
            linkmap.ArchiveMember(ARCHIVE, "cpuinfo.o"),
            linkmap.ArchiveMember("libfixy.a", "Signal.cpp.o"),
            linkmap.ArchiveMember("/usr/lib64/libc_nonshared.a", "atexit.oS"),
        ),
    )
    with tempfile.TemporaryDirectory(prefix="linkmap-planted-") as scratch_text:
        scratch = Path(scratch_text)
        (scratch / "planted.map").write_text("\n".join(PLANTED) + "\n", encoding="utf-8")
        read = linkmap.read_inputs(scratch / "planted.map")
        members = linkmap.read_members(scratch / "planted.map")
        (scratch / "empty.map").write_text("", encoding="utf-8")
        empty = refused(lambda: linkmap.read_inputs(scratch / "empty.map"))
    expect(
        "read_inputs and read_members read a file as inputs_of reads its lines",
        (read, members) == (inputs, linkmap.members_of(inputs)),
    )
    gnu = refused(lambda: linkmap.inputs_of(("Archive member included to satisfy reference by file (symbol)",), "g"))
    expect(
        "a map of GNU ld and an empty file are refused, and the error tells that the tree links with mold",
        "links with mold" in gnu and "is not the heading" in empty,
    )
    expect(
        "a map with only its heading is refused",
        "no output chunk" in refused(lambda: linkmap.inputs_of((linkmap.HEADING,), "h")),
    )
    shapes = {
        "an empty line": "",
        "a line of five spaces after the alignment": "             0x318         28     1     .text",
        "an address with no prefix 0x": "               318         28     1 .text",
        "an address that is not at the right end of its column": "            0x318         28     1 .text",
        "an address with no digit": "                0x         28     1 .text",
        "a size that is not decimal": "             0x318       0x1c     1 .text",
        "an alignment that is not decimal": "             0x318         28     a .text",
        "an input section with no ':('": "             0x318         28     1         unit.o(.text)",
        "an input section with no section": "             0x318         28     1         unit.o:()",
        "an input section with no file": "             0x318         28     1         :(.text)",
        "a line with two columns": "             0x318         28",
    }
    for name, text in shapes.items():
        expect(f"{name} is refused, with its line", ":3:" in refused(partial(linkmap.inputs_of, with_line(text), "s")))
    early = (linkmap.HEADING, "             0x388         32     4         /lib64/Scrt1.o:(.note.ABI-tag)")
    expect(
        "an input section before an output chunk is refused",
        "before an output chunk" in refused(lambda: linkmap.inputs_of(early, "early")),
    )
    expect(
        "split_member gives the archive and the member, and an empty archive for a plain path",
        linkmap.split_member("/lib/libx.a(y.o)") == ("/lib/libx.a", "y.o")
        and linkmap.split_member("z.o") == ("", "z.o"),
    )
    expect(
        "an address is 0 or a number in hexadecimal with the prefix 0x",
        linkmap.is_address("0")
        and linkmap.is_address("0x1F")
        and not any(linkmap.is_address(text) for text in ("0x", "10", "0xg", "00")),
    )


def link_cases(expect: testing.Expect) -> None:
    """Link SOURCE with the pinned mold, -Wl,-Map and --gc-sections, and read the map."""
    from assay.setup import install

    cxx = testing.held_compiler(expect)
    if cxx is None:
        return
    try:
        mold_directory = install.program("mold")
    except install.InstallError as problem:
        expect(f"the pinned mold is installed: {problem}", False)
        return
    with tempfile.TemporaryDirectory(prefix="linkmap-test-") as scratch_text:
        scratch = Path(scratch_text)
        (scratch / "unit.cpp").write_text(SOURCE, encoding="utf-8")
        compile_argv = [cxx, "-std=c++26", "-O1", "-ffunction-sections", "-c", "unit.cpp", "-o", "unit.o"]
        proc.require(proc.run(proc.Command(compile_argv, timeout_s=proc.QUICK_S, cwd=scratch)))
        link_argv = [
            cxx,
            "unit.o",
            "-o",
            "program",
            "-Wl,-Map=program.map,--gc-sections",
            "-fuse-ld=mold",
            f"-B{mold_directory}/",
            "-Wl,--threads=4",
            "-Wl,--no-fork",
        ]
        proc.require(proc.run(proc.Command(link_argv, timeout_s=proc.QUICK_S, cwd=scratch)))
        inputs = linkmap.read_inputs(scratch / "program.map")
        members = linkmap.read_members(scratch / "program.map")
    expect(
        "the inputs hold the start file first and the object of the unit, as the link names them",
        bool(inputs) and Path(inputs[0]).name in ("crt1.o", "Scrt1.o") and "unit.o" in inputs,
    )
    expect(
        "the map names no shared library, because a shared library has no input section",
        not any(".so" in Path(item).name for item in inputs),
    )
    expect(
        "the map names cpuinfo.o of libgcc.a for __builtin_cpu_supports",
        any(Path(item.archive).name == "libgcc.a" and item.member == "cpuinfo.o" for item in members),
    )
    expect("read_members gives the members of the inputs", members == linkmap.members_of(inputs))


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (planted_cases, link_cases))
