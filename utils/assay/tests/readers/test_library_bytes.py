"""The test of the reader of the library bytes (assay.readers.library_bytes).

`guard selftest tests.readers.test_library_bytes` runs the test.
Each case writes one line.  Exit 0 when each case holds, else 1.

THE CASES
    The class cases hold the class of each kind of declaring file.  The
    compile case compiles two units with one template, with the compiler that
    ASSAY_TEST_CXX names, and holds the count of a COMDAT group.  The
    registration of the test in test/CMakeLists.txt gives the compiler of the
    build.
"""

import struct
import tempfile
from pathlib import Path

from assay.core import proc, testing
from assay.guard.engines.elf import runner
from assay.readers import dwarf, elf, library_bytes

PLACES = library_bytes.Places(("/tree", "/build"), ("/opt/gcc",))
# Two units that instantiate one template, which inlines the compare operator of std::string_view.
UNIT = """\
#include <string_view>
template <class T> [[gnu::noinline]] bool planted_compare(T left, T right) {{ return left == right; }}
int {name}(std::string_view left, std::string_view right) {{ return planted_compare(left, right) ? 1 : 2; }}
"""


def class_cases(expect: testing.Expect) -> None:
    """Hold the class of each kind of declaring file (THE MEASURE)."""
    files = [
        ("", True),
        ("relative.h", False),
        ("/tree/include/x.h", False),
        ("/build/generated.h", False),
        ("/opt/gcc/usr/include/c++/16/bits/char_traits.h", False),
        ("/opt/gcc/usr/lib/gcc/x86_64/16/../../../../include/c++/16/string_view", False),
        ("/opt/gcc/usr/lib/gcc/x86_64/16/include/stddef.h", False),
        ("/usr/include/string.h", False),
        ("/treeother/x.h", False),
    ]
    expect(
        "the class of a file follows THE MEASURE, with the prefix of a directory and the normal form of a path",
        [library_bytes.class_of(file, artificial, PLACES) for file, artificial in files]
        == ["generated", "ours", "ours", "ours", "libstdc++", "libstdc++", "compiler", "system", "system"],
    )
    expect(
        "a body with no file that the compiler did not make has no class",
        library_bytes.class_of("", False, PLACES) is None,
    )
    expect(
        "the library bytes add the classes libstdc++, compiler and system",
        library_bytes.Measure("a", {"ours": 5, "libstdc++": 3, "compiler": 2, "system": 1, "generated": 7}, {}).library
        == 6,
    )


def planted_object(version: int) -> bytes:
    """Return an object with a planted .debug_info of one empty unit of a DWARF version."""
    body = struct.pack("<HBBI", version, 1, 8, 0) + b"\0"
    info = struct.pack("<I", len(body)) + body
    return elf.make_file([elf.Planted(dwarf.INFO, info), elf.Planted(dwarf.ABBREV, b"\0")])


def compile_cases(expect: testing.Expect) -> None:
    """Hold the count of a COMDAT group in two members, and an archive with DWARF that the reader refuses."""
    cxx = testing.patched_compiler()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the compiler", bool(cxx) and Path(cxx).is_file())
    if not cxx or not Path(cxx).is_file():
        return
    with tempfile.TemporaryDirectory(prefix="library-bytes-test-") as scratch_text:
        scratch = Path(scratch_text)
        objects: list[tuple[str, bytes]] = []
        for name in ("first", "second"):
            (scratch / f"{name}.cpp").write_text(UNIT.format(name=f"planted_{name}"), encoding="utf-8")
            argv = [cxx, "-std=c++17", "-O1", "-g", "-ffunction-sections", "-c", f"{name}.cpp", "-o", f"{name}.o"]
            proc.require(proc.run(proc.Command(argv, timeout_s=proc.QUICK_S, cwd=scratch)))
            objects.append((f"{name}.o", (scratch / f"{name}.o").read_bytes()))
        (scratch / "libone.a").write_bytes(elf.make_archive(objects[:1]))
        (scratch / "libtwo.a").write_bytes(elf.make_archive(objects))
        (scratch / "libbad.a").write_bytes(elf.make_archive([("bad.o", planted_object(4))]))
        places = library_bytes.places_of([scratch], runner.toolchain_prefix(cxx))
        one = library_bytes.read_measure(scratch / "libone.a", places)
        two = library_bytes.read_measure(scratch / "libtwo.a", places)
        try:
            library_bytes.read_measure(scratch / "libbad.a", places)
            refused = False
        except library_bytes.MeasureError:
            refused = True
    expect(
        "the template of a COMDAT group counts one time in two members, so the library bytes stay the same",
        one.library > 0 and two.library == one.library and two.classes["ours"] > one.classes["ours"],
    )
    expect("an archive with a unit of DWARF 4 cannot be read", refused)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (class_cases, compile_cases))
