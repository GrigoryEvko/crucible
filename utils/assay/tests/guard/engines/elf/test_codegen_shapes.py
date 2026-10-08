"""The test of the rule codegen-shapes (assay.guard.engines.elf.codegen_shapes).

THE CASES
    The cases hold these parts of the rule:
      * Each kind of finding of THE SHAPE on planted disassemblies
      * The flags of the probes and of the wrappers, and the probe rows of a
        plant
      * The refused instructions of planted constant-time functions
      * The reflection census and the census of the grade.
    The compile cases use the compiler that ASSAY_TEST_CXX names.

THE PROBE UNIT OF THE BUILD
    ASSAY_TEST_BUILD_DIR names the build directory, and the registration of
    the test in test/CMakeLists.txt gives it.  The case compiles the probe
    unit with the command of the compile database of that build.  The probes
    of PROBES must hold.  Then the case copies include/foundation/core/Region.h
    to a directory that comes first on the include path.  It puts a second
    test of the limits into View::window of the copy.  The probes view_window and
    view_append must then fail, and no other probe.  The case applies only on
    an x86_64 host.
"""

import os
import platform
import tempfile
from collections.abc import Sequence
from pathlib import Path

from assay.core import git as throwaway_repo
from assay.core import proc, testing
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT
from assay.guard.engines.elf import codegen_shapes as shapes
from assay.readers import compdb, objdump, tsast

BUILD_VARIABLE = "ASSAY_TEST_BUILD_DIR"
PLANT_HEADER = Path("include/foundation/core/Region.h")
# The words of a compile command that load the quarantine plugin.  A compile with a copied header does not use them.
PLUGIN_WORDS = ("-fplugin=", "-fplugin-arg-")
PLUGIN_MACRO = "-DCRUCIBLE_QUARANTINE_ACTIVE"
SYNTHETIC_HEAD = "\nDisassembly of section .text.{name}:\n\n0000000000000000 <(anonymous namespace)::{name}(int)>:\n"
SYNTHETIC_COLD = (
    "\nDisassembly of section .text.unlikely.{name}:\n\n"
    "0000000000000000 <(anonymous namespace)::{name}(int) [clone .cold]>:\n"
)
FATAL_CALL = (
    "   0:\tcall   QWORD PTR [rip+0x0]        # 6 <x>\n"
    "\t\t\t2: R_X86_64_GOTPCRELX\tfoundation::core::detail::report_fatal_(char const*, char const*, "
    "unsigned int, char const*)-0x4\n"
)
LOAD = "   0:\tendbr64\n   4:\tmov    rax,QWORD PTR [rdi]\n   7:\tret\n"
EXCHANGE = "   0:\tendbr64\n   4:\tmov    rax,rsi\n   7:\txchg   QWORD PTR [rdi],rax\n   a:\tret\n"
CHECKED = (
    "   0:\tendbr64\n   4:\ttest   rdi,rdi\n   7:\tje     d <x+0xd>\n"
    "\t\t\t9: R_X86_64_PC32\t.text.unlikely.probe-0x4\n   d:\tmov    eax,DWORD PTR [rdi]\n   f:\tret\n"
)
LOOP = (
    "   0:\tendbr64\n   4:\tvpaddd ymm0,ymm0,YMMWORD PTR [rax]\n   8:\tadd    rax,0x20\n"
    "   c:\tcmp    rax,rdx\n   f:\tjne    4 <x+0x4>\n  11:\tret\n"
)
OTHER_LOOP = (
    "   0:\tendbr64\n   4:\tvpaddd ymm0,ymm0,YMMWORD PTR [rax]\n   8:\tadd    rax,0x20\n"
    "   c:\tlea    rcx,[rax+0x1]\n   e:\tcmp    rax,rdx\n  11:\tjne    4 <x+0x4>\n  13:\tret\n"
)
COPY = "   0:\tendbr64\n   4:\tjmp    QWORD PTR [rip+0x0]        # a <x>\n\t\t\t6: R_X86_64_GOTPCRELX\tmemmove-0x4\n"
RAW_COPY = "   0:\tendbr64\n   4:\tjmp    QWORD PTR [rip+0x0]        # a <x>\n\t\t\t6: R_X86_64_GOTPCRELX\tmemcpy-0x4\n"
PLAIN = "   0:\tendbr64\n   4:\tmov    eax,DWORD PTR [rdi]\n   6:\tret\n"
PADDED = "   0:\tendbr64\n   4:\tmov    rax,QWORD PTR [rdi]\n   7:\txchg   ax,ax\n   9:\tret\n"
HELPER_CALL = "   7:\tcall   QWORD PTR [rip+0x0]\n\t\t\t9: R_X86_64_GOTPCRELX\thelper-0x4\n   d:\tret"
FATAL_IN_HOT = (
    "   7:\tcall   QWORD PTR [rip+0x0]\n\t\t\t9: R_X86_64_GOTPCRELX\t"
    "foundation::core::detail::report_fatal_(char const*)-0x4\n   d:\tret"
)
# The planted constant-time functions: one refused instruction of each kind, a compare and a clean function.  The
# variants with the -fharden- flags give the compare a branch.
PLANTED_CT = r"""
#include <cstdint>
extern "C" std::uint32_t external_sink(std::uint32_t) noexcept;
extern "C" std::uint32_t ct_plant_branch(std::uint32_t b, std::uint32_t x) noexcept {
    if (b & 1u) __builtin_trap();
    return x;
}
extern "C" std::uint8_t ct_plant_index(std::uint32_t x) noexcept {
    static const std::uint8_t table[256] = {1, 2, 3, 4, 5};
    return table[x & 255u];
}
extern "C" std::uint32_t ct_plant_call(std::uint32_t x) noexcept { return external_sink(x) + 1u; }
extern "C" std::uint32_t ct_plant_divide(std::uint32_t x, std::uint32_t y) noexcept { return x / (y | 1u); }
extern "C" bool ct_plant_hardened(std::uint8_t x) noexcept { return x == 0u; }
extern "C" std::uint32_t ct_plant_clean(std::uint32_t x, std::uint32_t y) noexcept { return (x * 5u + y) ^ 0xFFu; }
"""
# The variants with flags that put a branch to a trap after the compare of ct_plant_hardened.
HARDENED_VARIANTS = frozenset({"release", "verify"})
COMMON = ["g++", "-std=c++26", "-fPIC", "-fno-plt", "-Wall", "-Iinclude", "-fplugin=q.so", "-o", "u.o", "-c", "u.cpp"]
RELEASE = [
    *COMMON,
    "-O1",
    "-march=native",
    "-DNDEBUG",
    "-O3",
    "-DNDEBUG",
    "-fharden-compares",
    "-fharden-conditional-branches",
    "-fcontract-evaluation-semantic=observe",
]


def synthetic(name: str, body: str, cold: str | None = None) -> str:
    """Return the objdump text of one synthetic function: its hot part and its cold part."""
    text = SYNTHETIC_HEAD.format(name=name) + body
    if cold is not None:
        text += SYNTHETIC_COLD.format(name=name) + cold
    return text


def probe(relation: shapes.Relation, bound: int, extra: int = 0, **flags: object) -> shapes.Probe:
    """Return a planted probe with the name probe, a relation, a bound, the number of more instructions and flags."""
    calls = flags.get("calls", ())
    raw_calls = flags.get("raw_calls", ())
    return shapes.Probe(
        "probe",
        relation,
        bound,
        "c",
        extra,
        tuple(calls) if isinstance(calls, tuple) else (),
        tuple(raw_calls) if isinstance(raw_calls, tuple) else (),
        flags.get("cold") is True,
        flags.get("locked") is True,
    )


# ── The shape ──────────────────────────────────────────────────────


def shape_cases(expect: testing.Expect) -> None:
    """Plant one disassembly for each kind of finding of THE SHAPE, and hold each verdict."""
    cases: tuple[tuple[str, shapes.Probe, str, str, str | None, tuple[str, ...]], ...] = (
        ("a clean pair", probe("same", 3), LOAD, LOAD, None, ()),
        ("a nop that aligns the code", probe("same", 3), PADDED, LOAD, None, ()),
        (
            "an extra instruction",
            probe("same", 3),
            LOAD.replace("   7:\tret", "   7:\tmov    rcx,rax\n   a:\tret"),
            LOAD,
            None,
            ("over the bound", "not the instruction sequence"),
        ),
        (
            "a call where none belongs",
            probe("cost", 4, 1),
            LOAD.replace("   7:\tret", HELPER_CALL),
            LOAD,
            None,
            ("calls ['helper']",),
        ),
        (
            "a missing memmove",
            probe("same", 3, calls=("memmove",), raw_calls=("memmove",)),
            LOAD,
            LOAD,
            None,
            ("expectation is ['memmove']",),
        ),
        (
            "a memmove and a memcpy",
            probe("cost", 2, calls=("memmove",), raw_calls=("memcpy",)),
            COPY,
            RAW_COPY,
            None,
            (),
        ),
        ("a missing lock", probe("same", 3, locked=True), LOAD, LOAD, None, ("holds no locked instruction",)),
        ("a lock where none belongs", probe("same", 4), EXCHANGE, EXCHANGE, None, ("holds a locked instruction",)),
        (
            "a cold call in the hot part",
            probe("cost", 4, 1, cold=True),
            LOAD.replace("   7:\tret", FATAL_IN_HOT),
            LOAD,
            None,
            ("has no cold part", "calls ['foundation::core::detail::report_fatal_']"),
        ),
        (
            "a cold part with no jump into it",
            probe("cost", 3, cold=True),
            LOAD,
            LOAD,
            FATAL_CALL,
            ("no jump into its cold part",),
        ),
        (
            "a cold part with no fatal exit",
            probe("cost", 5, 2, cold=True),
            CHECKED,
            PLAIN,
            "   0:\tud2\n",
            ("does not call the fatal exit",),
        ),
        ("a checked access", probe("cost", 5, 2, cold=True), CHECKED, PLAIN, FATAL_CALL, ()),
        ("a cold part where none belongs", probe("cost", 5, 2), CHECKED, PLAIN, FATAL_CALL, ("has a cold part",)),
        (
            "a family part over the raw part",
            probe("cost", 9, 1, cold=True),
            CHECKED,
            PLAIN,
            FATAL_CALL,
            ("permits 1 more",),
        ),
        ("the same loop", probe("loop", 6), LOOP, LOOP, None, ()),
        ("a different loop", probe("loop", 7, 1), OTHER_LOOP, LOOP, None, ("inner loops",)),
    )
    for title, planted, family_body, raw_body, cold, expected in cases:
        text = synthetic("probe", family_body, cold) + synthetic("probe_raw", raw_body)
        found, shown = shapes.shape_findings(shapes.codes_of(objdump.parse(text, title)), (planted,))
        messages = [item.message for item in found]
        holds = all(any(part in message for message in messages) for part in expected) if expected else not messages
        expect(f"the shape case '{title}' gives {list(expected) or 'no finding'}", holds and bool(shown) == bool(found))


def flag_cases(expect: testing.Expect) -> None:
    """Plant compile commands for each kind of finding of THE FLAGS OF THE PROBES."""
    base = ["g++", "-DX=1", "-Iinclude", "-std=c++26", "-fno-plt", "-Wall", "-o", "out.o", "-c", "unit.cpp"]
    probe_words = ["g++", "-DX=1", "-std=c++26", "-fno-plt", *shapes.FIXED_FLAGS, "-fplugin=q.so", "-c", "probe.cpp"]
    cases: tuple[tuple[str, list[str], list[str], int, tuple[str, ...]], ...] = (
        ("the same flags", base, probe_words, 2, ()),
        (
            "flags of the base that change with the preset",
            [*base, "-O1", "-g", "-fsanitize=address", "-fharden-compares", "-D_GLIBCXX_ASSERTIONS=1", "-fPIC"],
            probe_words,
            2,
            (),
        ),
        (
            "a new flag of the base",
            [*base, "-fno-jump-tables"],
            probe_words,
            2,
            ("compiles with -fno-jump-tables, and the probe",),
        ),
        ("a flag that the base lost", base, [*probe_words, "-fno-common"], 2, ("no foundation unit does",)),
        ("a new flag of each preset", [*base, "-fno-tree-vectorize"], probe_words, 2, ("-fno-tree-vectorize",)),
        ("a missing fixed flag", base, [word for word in probe_words if word != "-DNDEBUG"], 2, ("PER_PRESET",)),
        (
            "a different target",
            base,
            [word.replace("x86-64-v3", "native") for word in probe_words],
            2,
            ("expectations of PROBES",),
        ),
        ("no foundation unit", base, probe_words, 0, ("no unit of the target foundation",)),
    )
    for title, base_words, probe_flags, units, expected in cases:
        found = shapes.flag_findings(
            shapes.code_flags(probe_flags, "probe.cpp"), [shapes.code_flags(base_words, "unit.cpp")] * units
        )
        messages = [item.message for item in found]
        holds = all(any(part in message for message in messages) for part in expected) and bool(messages) == bool(
            expected
        )
        expect(f"the flag case '{title}' gives {list(expected) or 'no finding'}", holds)
    found = shapes.flag_findings(
        shapes.code_flags(probe_words, "probe.cpp"),
        [shapes.code_flags(base, "unit.cpp"), shapes.code_flags([*base, "-fno-common"], "unit.cpp")],
    )
    expect(
        "foundation units with different code flags give a finding",
        any("do not have the same code flags" in item.message for item in found),
    )


def probe_row_cases(expect: testing.Expect) -> None:
    """Hold the probe rows of a plant (THE PROBES)."""
    rows = rows_grammar.parse(
        "probe load relation=cost bound=4 extra=2 calls=memmove,memset raw-calls=memcpy cold=yes locked=yes | claim\n"
        "probe bad relation=other bound=1 | a planted claim\n"
        "probe bad relation=same | a planted claim\n"
        "probe bad relation=same bound=1 cold=no | a planted claim\n"
        "probe bad relation=same bound=1\n",
        "plant.txt",
    )
    expect(
        "a probe row gives each field of its probe",
        shapes.probe_of_row(rows[0])
        == shapes.Probe("load", "cost", 4, "claim", 2, ("memmove", "memset"), ("memcpy",), True, True),
    )
    for row in rows[1:]:
        try:
            shapes.probe_of_row(row)
            is_refused = False
        except rows_grammar.RowsError:
            is_refused = True
        expect(f"the probe row at line {row.line} is refused", is_refused)


# ── The wrappers ───────────────────────────────────────────────────


def wrapper_flag_cases(expect: testing.Expect) -> None:
    """Plant the flags of foundation units for each kind of problem of THE FLAGS OF THE WRAPPERS."""
    debug = [*COMMON, "-O1", "-D_GLIBCXX_ASSERTIONS=1", "-D_GLIBCXX_SANITIZE_VECTOR=1", "-fsanitize=address"]
    verify = [
        *COMMON,
        "-O3",
        "-march=native",
        "-fharden-compares",
        "-fharden-conditional-branches",
        "-fcontract-evaluation-semantic=enforce",
    ]
    profiled = [*RELEASE, "-fprofile-use=p", "-DCRUCIBLE_PGO=1"]
    variants = shapes.variants_of(shapes.split_flags(shapes.code_flags(RELEASE, "u.cpp"))[1])
    cases: tuple[tuple[str, list[list[str]], int], ...] = (
        ("a Debug build with a sanitizer", [debug, debug], 0),
        ("a Release build", [RELEASE, RELEASE], 0),
        ("a verify build", [verify], 0),
        ("a Release build with a profile", [profiled], 0),
        ("a Release build with a new hardening flag", [[*RELEASE, "-fharden-control-flow-redundancy"]], 1),
        ("a Release build with no hardening", [[word for word in RELEASE if not word.startswith("-fharden-")]], 1),
        ("two units with different base flags", [RELEASE, [*RELEASE, "-fno-common"]], 1),
    )
    for title, commands, expected in cases:
        splits = [shapes.split_flags(shapes.code_flags(words, "u.cpp")) for words in commands]
        problems = shapes.flag_problems([base for base, _ in splits], [item for _, item in splits], variants)
        expect(f"the wrapper flag case '{title}' gives {expected} problems", len(problems) == expected)
    base, _ = shapes.split_flags(shapes.code_flags(RELEASE, "u.cpp"))
    expect("the flags that each build takes are -std=c++26 -fPIC -fno-plt", base == ("-std=c++26", "-fPIC", "-fno-plt"))
    expect(
        "the variants are debug, release and verify, and verify has enforce and no NDEBUG",
        [variant.name for variant in variants] == ["debug", "release", "verify"]
        and shapes.ENFORCE in variants[2].flags
        and shapes.NDEBUG not in variants[2].flags,
    )
    _, names = shapes.wrapper_source(shapes.CTCRYPTO_CASES)
    expect(
        "the wrapper unit holds one wrapper for each primitive and width, and one for each static length",
        len(names) == len(shapes.WIDTHS) * 5 + len(shapes.STATIC_EQ_LENGTHS),
    )


def planted_plan(compiler: str) -> shapes.Plan:
    """Return the plan of the planted wrappers: the base flags of RELEASE, and the variants of its release words."""
    base, release = shapes.split_flags(shapes.code_flags(RELEASE, "u.cpp"))
    return shapes.Plan(compiler, base, shapes.variants_of(release))


def wrapper_cases(expect: testing.Expect) -> None:
    """Compile the planted constant-time functions for each target, and hold the refused instructions of each."""
    compiler = testing.patched_compiler()
    expect(
        "ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the patched compiler",
        bool(compiler) and Path(compiler).is_file(),
    )
    if not compiler or not Path(compiler).is_file() or platform.machine() != shapes.EXPECTED_MACHINE:
        return
    plan = planted_plan(compiler)
    flagged = {
        "ct_plant_branch": ("conditional branch",),
        "ct_plant_index": ("calculated address", "indexed load"),
        "ct_plant_call": ("call",),
        "ct_plant_divide": ("operand-dependent latency",),
    }
    names = {name: False for name in flagged} | {"ct_plant_hardened": False, "ct_plant_clean": False}
    with tempfile.TemporaryDirectory(prefix="codegen-shapes-test-") as scratch:
        source = Path(scratch) / "planted.cpp"
        source.write_text(PLANTED_CT, encoding="utf-8")
        problems = shapes.wrapper_results(plan, source, names, REPO_ROOT / "include")
    targets = [target for target, _ in plan.targets()]
    expect("the plan compiles each level of each variant for each target", len(targets) == 12)
    for name, kinds in flagged.items():
        expect(
            f"{name} is refused for one of {list(kinds)} at each target",
            all(
                any(item.startswith(f"{target} {name}: ") and any(kind in item for kind in kinds) for item in problems)
                for target in targets
            ),
        )
    hardened = [
        target for target in targets if any(item.startswith(f"{target} ct_plant_hardened: ") for item in problems)
    ]
    expect(
        "the compare of ct_plant_hardened has a branch only with the flags of a variant with the -fharden- flags",
        hardened == [target for target in targets if target.split()[0] in HARDENED_VARIANTS],
    )
    expect("the clean function gives no problem", not any(" ct_plant_clean: " in item for item in problems))
    for fake, body, must_fail in (
        ("fake_ct_unwrapped", "inline unsigned leak(unsigned x) { return x; }", True),
        ("fake_ct_covered", "inline unsigned select(unsigned x) { return x; }", False),
    ):
        unit = (
            f"{shapes.census_prelude()}\nnamespace {fake} {{ {body} }}\n"
            f'static_assert(ct_disassembly::covers_every_primitive(^^{fake}), "census fired");\n'
        )
        with tempfile.TemporaryDirectory(prefix="codegen-shapes-census-") as scratch:
            source = Path(scratch) / "census.cpp"
            source.write_text(unit, encoding="utf-8")
            command = [compiler, "-std=c++26", "-freflection", "-fsyntax-only", str(source)]
            result = proc.run(proc.Command(command, timeout_s=proc.TOOL_S))
        fired = result.exit != 0 and "census fired" in result.err
        expect(f"the reflection census {'stops' if must_fail else 'passes'} the namespace {fake}", fired == must_fail)


# ── The census of the grade ────────────────────────────────────────


def census_cases(expect: testing.Expect) -> None:
    """Plant statements of the grade in a scratch tree, and hold the count of the findings of the census."""
    statement = "using Seal = ::fixy::fn<int, ::fixy::atom::with<>, ::fixy::atom::constant_time>;\n"
    role = "using Mac = ::fixy::role::CtCrypto<int>;\n"
    comment = "// role::CtCrypto and atom::constant_time, named in a comment only\n"
    block_comment = "/* role::CtCrypto */ int plain = 0;\n"
    literal = 'inline constexpr char spelled[] = "role::CtCrypto and atom::constant_time";\n'
    split = "using Seal = ::fixy::fn<int, ::fixy::atom::\n    constant_time>;\n"
    aliased = "namespace grade = ::fixy::atom;\nusing Seal = ::fixy::fn<int, grade::constant_time>;\n"
    wrapper = ("ct_case_seal", False, 'extern "C" int ct_case_seal(int x) noexcept { return x; }')
    trials: tuple[tuple[str, dict[str, str], dict[str, tuple[tuple[str, bool, str], ...]], int], ...] = (
        ("a statement of the atom with no case", {"include/a.h": statement}, {}, 1),
        ("a statement of the role with no case", {"src/b.cpp": role}, {}, 1),
        ("a comment only", {"include/c.h": comment}, {}, 0),
        ("a block comment only", {"include/c.h": block_comment}, {}, 0),
        ("a string literal only", {"include/c.h": literal}, {}, 0),
        ("a statement of the atom on two lines", {"include/a.h": split}, {}, 1),
        ("a statement of the atom through a namespace alias", {"include/a.h": aliased}, {}, 1),
        ("a statement with a case", {"include/a.h": statement}, {"include/a.h": (wrapper,)}, 0),
        ("a case entry with no wrapper", {"include/a.h": statement}, {"include/a.h": ()}, 1),
        ("a case entry of a file with no statement", {"include/c.h": comment}, {"include/c.h": (wrapper,)}, 1),
        ("a statement in a definition file", {"include/fixy/Role.h": role}, {}, 0),
    )
    for title, files, cases, expected in trials:
        with tempfile.TemporaryDirectory(prefix="codegen-shapes-grade-") as scratch:
            root = Path(scratch)
            for rel, text in files.items():
                (root / rel).parent.mkdir(parents=True, exist_ok=True)
                (root / rel).write_text(text, encoding="utf-8")
            throwaway_repo.init(root)
            throwaway_repo.run(root, "add", "-A", "--", ".")
            found = shapes.grade_findings(root, cases)
        expect(f"the census of the grade over {title} gives {expected} findings", len(found) == expected)


# ── The probe unit of the build ────────────────────────────────────


def plant_window(header: Path) -> str:
    """Return the text of a header with a second test of the limits at the start of View::window.

    The window of a dynamic extent gets the test: the member function window
    of the class View with two parameters.  The parse tree of the header
    gives its body and the names of its parameters.

    Raises:
        ValueError: If the header does not hold one such function and no other
    """
    sites: list[tuple[tuple[int, int], list[str]]] = []
    for tree in tsast.parse([header], strict=False):
        for function in tree.find("function_definition"):
            body = function.child_by_field("body")
            names = [name for name, _ in tsast.parameters(function)]
            if body is None or len(names) != 2 or tsast.enclosing_function(body) != ("View", "window"):
                continue
            sites.append((body.start, names))
    if len(sites) != 1:
        raise ValueError(f"{header} holds {len(sites)} definitions of View::window with two parameters, not one")
    (row, column), (offset_name, count_name) = sites[0]
    data = header.read_bytes()
    starts = [0, *(index + 1 for index, byte in enumerate(data) if byte == 0x0A)]
    position = starts[row] + column + 1
    plant = (
        f"\n        if ({offset_name} + {count_name} > size()) [[unlikely]] {{\n"
        f'            fatal("the shape test planted this second test of the limits");\n        }}\n'
    )
    return (data[:position] + plant.encode() + data[position:]).decode()


def compile_words(entry: compdb.CompileEntry, obj: Path, include: Path | None) -> list[str]:
    """Return the compile command of a row with the object `obj`, and with the directory `include` first.

    With a directory, the command does not hold the words of the quarantine plugin.
    """
    words = [entry.argv()[0], *([] if include is None else [f"-I{include}"])]
    arguments = list(entry.argv()[1:])
    index = 0
    while index < len(arguments):
        word = arguments[index]
        index += 1
        if word == "-o":
            words += ["-o", str(obj)]
            index += 1
        elif include is None or not (word.startswith(PLUGIN_WORDS) or word == PLUGIN_MACRO):
            words.append(word)
    return words


def failed_probes(entry: compdb.CompileEntry, include: Path | None, probes: Sequence[shapes.Probe]) -> set[str]:
    """Compile the probe unit of a row, and return the name of each probe with a finding.

    Raises:
        proc.ToolFailed: If the compile fails
    """
    with tempfile.TemporaryDirectory(prefix="codegen-shapes-probe-") as scratch:
        obj = Path(scratch) / "core_shapes.o"
        command = proc.Command(compile_words(entry, obj, include), timeout_s=proc.TOOL_S, cwd=Path(entry.directory))
        proc.require(proc.run(command))
        functions = shapes.codes_of(shapes.disassembled(obj))
    found, _ = shapes.shape_findings(functions, probes)
    return {item.entity for item in found}


def build_probe_cases(expect: testing.Expect) -> None:
    """Compile the probe unit of the build with and without a second test of the limits.

    THE PROBE UNIT OF THE BUILD tells the case.
    """
    if platform.machine() != shapes.EXPECTED_MACHINE:
        return
    build = os.environ.get(BUILD_VARIABLE, "")
    expect(f"{BUILD_VARIABLE} names a build directory with a compile database", bool(build) and Path(build).is_dir())
    if not build or not Path(build).is_dir():
        return
    entry = next((item for item in compdb.entries(Path(build)) if item.file.endswith(shapes.PROBE_SOURCE)), None)
    expect("the compile database of the build compiles the probe unit", entry is not None)
    if entry is None:
        return
    expect(
        "each probe of PROBES holds on the probe unit of the build", failed_probes(entry, None, shapes.PROBES) == set()
    )
    with tempfile.TemporaryDirectory(prefix="codegen-shapes-plant-") as scratch:
        include = Path(scratch) / "include"
        planted = include / PLANT_HEADER.relative_to("include")
        planted.parent.mkdir(parents=True)
        planted.write_text(plant_window(REPO_ROOT / PLANT_HEADER), encoding="utf-8")
        failed = failed_probes(entry, include, shapes.PROBES)
    expect(
        "a second test of the limits in View::window fails the probes view_window and view_append, and no other",
        failed == {"view_window", "view_append"},
    )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (
            shape_cases,
            flag_cases,
            probe_row_cases,
            wrapper_flag_cases,
            wrapper_cases,
            census_cases,
            build_probe_cases,
        ),
    )
