"""The test of the rule library-bytes (assay.guard.engines.elf.library_bytes).

`guard selftest tests.guard.engines.elf.test_library_bytes` runs the test.
Each case writes one line.  Exit 0 when each case holds, else 1.

THE CASES
    The cases hold the class of a declaring file, the verdict and the write
    against planted ledgers, and the rows that the rule refuses.  The compile
    case compiles two units with one template, with the compiler that
    ASSAY_TEST_CXX names, and holds the count of a COMDAT group.  The
    registration of the test in test/CMakeLists.txt gives the compiler of the
    build.
"""

import struct
import tempfile
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import ledger as rule_ledger
from assay.core import proc, testing
from assay.core import registry as rule_registry
from assay.guard.engines.elf import library_bytes, runner
from assay.readers import dwarf, elf

RULE = library_bytes.RULE
PLACES = library_bytes.Places(("/tree", "/build"), ("/opt/gcc",))
HEAD = f"# a planted ledger\nformat 1\nrule {RULE}\n"
CONFIGURATIONS = (
    "configuration k kind k\nconfiguration k tier t\nconfiguration other kind other\nconfiguration other tier t\n"
)
CONFIGURATION = build_config.BuildConfiguration("k", (("kind", "k"), ("tier", "t")))
# Two units that instantiate one template, which inlines the compare operator of std::string_view.
UNIT = """\
#include <string_view>
template <class T> [[gnu::noinline]] bool planted_compare(T left, T right) {{ return left == right; }}
int {name}(std::string_view left, std::string_view right) {{ return planted_compare(left, right) ? 1 : 2; }}
"""


def parsed(text: str) -> rule_ledger.Ledger:
    """Read a planted ledger of the rule, with the policy baseline."""
    return rule_ledger.parse(HEAD + text, "planted.txt", RULE, rule_registry.Policy.BASELINE)


def measure(archive: str, library: int, nameless: tuple[tuple[str, int], ...] = ()) -> library_bytes.Measure:
    """Return the measure of an archive with `library` bytes of libstdc++ code and no named body."""
    return library_bytes.Measure(archive, {library_bytes.LIBSTDCXX: library}, {}, nameless)


def kinds_of(measures: list[library_bytes.Measure], text: str, configuration: str = "k") -> list[tuple[str, str]]:
    """Return the level and the kind of each finding of the measures against a planted ledger."""
    ledger = parsed(text)
    built = build_config.BuildConfiguration(configuration, (("kind", configuration), ("tier", "t")))
    judged = build_config.judging(RULE, ledger, built)
    budget = library_bytes.budget_of(rule_ledger.rule_of(RULE))
    found = library_bytes.verdict(measures, ledger, judged, budget, "write")
    return [(item.level, item.kind) for item in found]


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
        library_bytes.Measure(
            "a", {"ours": 5, "libstdc++": 3, "compiler": 2, "system": 1, "generated": 7}, {}, ()
        ).library
        == 6,
    )


def verdict_cases(expect: testing.Expect) -> None:
    """Hold each kind of the verdict against planted rows (THE VERDICT)."""
    rows = f"{CONFIGURATIONS}@build/liba.a | k | 100 | - | a row\n@build/liba.a | other | 500 | - | a row\n"
    expect("a change in the warning threshold gives no finding", kinds_of([measure("liba.a", 101)], rows) == [])
    expect(
        "a growth over the warning threshold is a warning",
        kinds_of([measure("liba.a", 103)], rows) == [("warning", "growth")],
    )
    expect(
        "a growth over the error threshold is an error",
        kinds_of([measure("liba.a", 110)], rows) == [("error", "growth")],
    )
    expect(
        "a fall over the warning threshold is an error", kinds_of([measure("liba.a", 97)], rows) == [("error", "fall")]
    )
    expect(
        "an archive with no row is new, and a row with no archive is an error",
        kinds_of([measure("liba.a", 100), measure("libnew.a", 4)], rows + "@build/libgone.a | k | 3 | - | a row\n")
        == [("error", "new-archive"), ("error", "no-archive")],
    )
    zero = f"{CONFIGURATIONS}@build/liba.a | k | 0 | - | a row\n"
    expect(
        "a row of 0 bytes holds an archive with no library code, and refuses one with library code",
        kinds_of([measure("liba.a", 0)], zero) == []
        and kinds_of([measure("liba.a", 5)], zero) == [("error", "growth")],
    )
    expect(
        "a build of a configuration that the ledger does not hold gets the largest row, and gives no fall",
        kinds_of([measure("liba.a", 400)], rows, "new") == []
        and kinds_of([measure("liba.a", 600)], rows, "new") == [("error", "growth")],
    )
    expect(
        "a body with no file that the compiler did not make is an error of the kind no-file",
        kinds_of([measure("liba.a", 100, (("unit.o: f", 3),))], rows) == [("error", "no-file")],
    )
    problems = library_bytes.row_problems(parsed(f"{CONFIGURATIONS}@build/liba.a | gone | 1 | - | a row\n"))
    expect(
        "a row of a configuration that the ledger does not hold is a ledger-row error",
        [item.kind for item in problems] == ["ledger-row"],
    )
    problems = library_bytes.row_problems(parsed(f"{CONFIGURATIONS}@build/liba.a | k | 1 | entity | a row\n"))
    expect("a row with an entity is a ledger-row error", [item.kind for item in problems] == ["ledger-row"])


def write_cases(expect: testing.Expect) -> None:
    """Hold the write: a growth with no reason, the reasons, the rows of the other configurations (THE WRITE)."""
    rule = rule_ledger.rule_of(RULE)
    ledger = parsed(
        f"{CONFIGURATIONS}@build/liba.a | k | 100 | - | the old reason\n@build/liba.a | other | 7 | - | kept\n"
    )
    judged = build_config.judging(RULE, ledger, CONFIGURATION)
    grown = library_bytes.Reading([measure("liba.a", 110), measure("libb.a", 9)], CONFIGURATION, ledger, judged)
    request = runner.WriteRequest(Path("/tree"), Path("/build"))
    refused = library_bytes.written_ledger(rule, request, grown)
    expect("a growth over the warning threshold with no reason writes nothing", isinstance(refused, str))
    written = library_bytes.written_ledger(
        rule, runner.WriteRequest(Path("/tree"), Path("/build"), reason="why"), grown
    )
    rows = (
        {(key.path, key.kind): (entry.value, entry.reason) for key, entry in written.base.items()}
        if isinstance(written, rule_ledger.Ledger)
        else {}
    )
    expect(
        "a write with a reason writes each archive of the build, and keeps the rows of the other configurations",
        rows
        == {
            ("@build/liba.a", "k"): (110, "why"),
            ("@build/libb.a", "k"): (9, "why"),
            ("@build/liba.a", "other"): (7, "kept"),
        },
    )
    held = library_bytes.Reading([measure("liba.a", 100), measure("libb.a", 9)], CONFIGURATION, ledger, judged)
    rewritten = library_bytes.written_ledger(rule, request, held)
    reasons = (
        {(key.path, key.kind): entry.reason for key, entry in rewritten.base.items()}
        if isinstance(rewritten, rule_ledger.Ledger)
        else {}
    )
    expect(
        "a row keeps its reason, and a new row gets the default reason",
        reasons.get(("@build/liba.a", "k")) == "the old reason"
        and reasons.get(("@build/libb.a", "k")) == library_bytes.DEFAULT_REASON,
    )
    other = build_config.BuildConfiguration("new", (("kind", "new"), ("tier", "t")))
    unheld = library_bytes.Reading([measure("liba.a", 1)], other, ledger, build_config.judging(RULE, ledger, other))
    expect(
        "a build of a configuration that the ledger does not hold writes nothing with no --add-configuration",
        isinstance(library_bytes.written_ledger(rule, request, unheld), str),
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
        places = library_bytes.Places((str(scratch),), (str(runner.toolchain_prefix(cxx)),))
        one = library_bytes.read_measure(scratch / "libone.a", places)
        two = library_bytes.read_measure(scratch / "libtwo.a", places)
        try:
            library_bytes.read_measure(scratch / "libbad.a", places)
            refused = False
        except runner.BuildError:
            refused = True
    expect(
        "the template of a COMDAT group counts one time in two members, so the library bytes stay the same",
        one.library > 0 and two.library == one.library and two.classes["ours"] > one.classes["ours"],
    )
    expect("an archive with a unit of DWARF 4 cannot be read", refused)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (class_cases, verdict_cases, write_cases, compile_cases))
