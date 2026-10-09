"""The test of the rule codegen-shapes (assay.guard.engines.elf.codegen_shapes).

THE CASES
    The cases hold these parts of the rule:
      * The normalization of the registers, of rip and of the symbols
      * The walk of planted disassemblies: the returning paths, the failure
        paths and their kinds, and the loops
      * The verdict of planted pairs: each class of family-only instruction,
        the second tests of the `-fharden-` flags and the locked instructions
      * The count of a probe with no declaration, and the warning of a cost
      * The flags of the probe unit, the `-fharden-` flags, the probe rows of
        a plant and the command of the compile of the verdict
      * The flags and the refused instructions of the constant-time wrappers
      * The reflection census and the census of the grade.
    The compile cases use the compiler that ASSAY_TEST_CXX names.

THE PROBE UNIT OF THE BUILD
    ASSAY_TEST_BUILD_DIR names the build directory, and the registration of
    the test in test/CMakeLists.txt gives it.  The case compiles the probe
    unit with the command of the verdict: the command of the compile database
    with no sanitizer word.  The pairs of PROBES must hold.  Then the case
    copies include/foundation/core/Region.h to a directory that comes first on
    the include path.  It puts a second test of the limits into View::window
    of the copy.  The probes view_window and view_append must then fail, and
    no other probe.  The case applies only on an x86_64 host.
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
# The distance between two planted instructions.
STEP = 4
FATAL = "R_X86_64_GOTPCRELX\tfoundation::core::detail::report_fatal_(char const*)-0x4"
ZERO_COST = shapes.Probe("probe", "c")
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

# A planted line of a part: the instruction, and the relocation after it or None.  A word @N of the instruction is
# the offset of the instruction N of the same part.
Line = tuple[str, str | None]


def cold(index: int) -> str:
    """Return the relocation of a jump into the instruction `index` of the cold part of the planted function."""
    addend = index * STEP - shapes.JUMP_FIELD
    return f"R_X86_64_PC32\t.text.unlikely.probe{'+' if addend >= 0 else '-'}0x{abs(addend):x}"


def part_text(section: str, symbol: str, lines: Sequence[Line]) -> str:
    """Return the objdump text of one planted part, with an instruction at each STEP bytes."""
    rows = [f"\nDisassembly of section {section}:\n\n0000000000000000 <{symbol}>:"]
    for index, (text, relocation) in enumerate(lines):
        words = [
            f"{int(word[1:]) * STEP:x} <x+0x{int(word[1:]) * STEP:x}>" if word[:1] == "@" else word
            for word in text.split(" ")
        ]
        mnemonic, operands = words[0], " ".join(words[1:])
        rows.append(f"  {index * STEP:x}:\t{mnemonic:<6} {operands}".rstrip())
        if relocation is not None:
            rows.append(f"\t\t\t{index * STEP + 2:x}: {relocation}")
    return "\n".join(rows) + "\n"


def planted(name: str, hot: Sequence[Line], cold_lines: Sequence[Line] = ()) -> str:
    """Return the objdump text of one planted function with its hot part and its cold part."""
    text = part_text(f".text.{name}", f"(anonymous namespace)::{name}(int)", hot)
    if cold_lines:
        text += part_text(f".text.unlikely.{name}", f"(anonymous namespace)::{name}(int) [clone .cold]", cold_lines)
    return text


def code_of(name: str, hot: Sequence[Line], cold_lines: Sequence[Line] = ()) -> shapes.Code:
    """Return the code of one planted function."""
    return shapes.codes_of(objdump.parse(planted(name, hot, cold_lines), name))[name]


def plain(*texts: str) -> list[Line]:
    """Return planted lines with no relocation."""
    return [(text, None) for text in texts]


# ── The normalization ──────────────────────────────────────────────


def normalization_cases(expect: testing.Expect) -> None:
    """Hold the class of each register, the removal of the displacement of rip, and the text of a symbol."""
    cases = (
        ("mov rax,QWORD PTR [rdi+0x8]", "mov g64,QWORD PTR [g64+0x8]"),
        ("add r9d,ecx", "add g32,g32"),
        ("movzx eax,dil", "movzx g32,g8"),
        ("vpaddd ymm0,ymm1,ymm15", "vpaddd v256,v256,v256"),
        ("kmovw k1,eax", "kmovw k,g32"),
        ("mov QWORD PTR [rbp-0x8],rsi", "mov QWORD PTR [rbp-0x8],g64"),
        ("lea rax,[rip+0x0]", "lea g64,[rip]"),
        ("lea rax,[rip+0xffffffffffffffaa]", "lea g64,[rip]"),
        ("mov eax,DWORD PTR [rax+rdi*4]", "mov g32,DWORD PTR [g64+g64*4]"),
    )
    for source, wanted in cases:
        text = f"{source.split(' ', 1)[0]} {shapes.normalized_operands(source.split(' ', 1)[1])}"
        expect(f"the operands of '{source}' become '{wanted}'", text == wanted)
    for symbol, wanted in ((".LC12", " -> .LC"), (".rodata.CSWTCH.151", " -> .rodata"), ("memmove", " -> memmove")):
        instruction = shapes.Instruction(0, (), "lea", "rax,[rip+0x0]", (), (), symbol, symbol)
        expect(f"the symbol {symbol} becomes '{wanted}'", shapes.symbol_text(instruction) == wanted)


# ── The walk ───────────────────────────────────────────────────────


def walk_cases(expect: testing.Expect) -> None:
    """Plant functions for each part of THE WALK, and hold the texts and the classes of their steps."""
    fatal_inline = code_of(
        "probe",
        [
            *plain("endbr64", "test rdi,rdi", "je @5", "mov eax,DWORD PTR [rdi]", "ret", "lea rdi,[rip+0x0]"),
            ("call QWORD PTR [rip+0x0]", FATAL),
            *plain("mov eax,0x25", "ret"),
        ],
    )
    texts = [step.text for step in shapes.steps_of(fatal_inline)]
    expect(
        "a call of the fatal exit ends its path, also when code follows it, and the jump into it is a check",
        texts == ["endbr64", "test g64,g64", "je !fatal", "mov g32,DWORD PTR [g64]", "ret"],
    )
    fall_through = code_of(
        "probe",
        [
            *plain("endbr64", "test rdi,rdi", "jne @4"),
            ("call QWORD PTR [rip+0x0]", FATAL),
            *plain("mov eax,0x1", "ret"),
        ],
    )
    steps = shapes.steps_of(fall_through)
    expect(
        "a conditional jump whose failure path is its fall-through is a check of that path",
        [step.text for step in steps] == ["endbr64", "test g64,g64", "jne !fatal", "mov g32,0x1", "ret"]
        and steps[2].kind is shapes.StepClass.CHECK,
    )
    hardened = code_of(
        "probe",
        [*plain("endbr64", "test rdi,rdi"), ("je @0", cold(0)), *plain("test rdi,rdi"), ("je @0", cold(2))]
        + plain("mov eax,DWORD PTR [rdi]", "ret"),
        [("call QWORD PTR [rip+0x0]", FATAL), *plain("ud2", "ud2")],
    )
    details = [step.detail for step in shapes.steps_of(hardened) if step.kind is shapes.StepClass.CHECK]
    expect(
        "a jump into the cold part names the kind of the failure path there: fatal, then trap",
        details == ["fatal", "trap"],
    )
    tail = code_of(
        "probe", [*plain("endbr64", "mov rdx,rsi"), ("jmp QWORD PTR [rip+0x0]", "R_X86_64_GOTPCRELX\tmemset-0x4")]
    )
    steps = shapes.steps_of(tail)
    expect(
        "a jump to a different function is an exit and a call of that function",
        steps[-1].kind is shapes.StepClass.CALL and steps[-1].detail == "memset" and len(steps) == 3,
    )
    loop = code_of(
        "probe",
        plain("endbr64", "xor eax,eax", "add eax,DWORD PTR [rdi]", "add rdi,0x4", "cmp rdi,rsi", "jne @2", "ret"),
    )
    marks = [step.is_in_loop for step in shapes.steps_of(loop)]
    expect("a jump into an earlier instruction marks the loop", marks == [False, False, True, True, True, True, False])
    padded = code_of("probe", plain("endbr64", "mov rax,QWORD PTR [rdi]", "xchg ax,ax", "nop", "ret"))
    expect("a nop that aligns the code is no step", len(shapes.steps_of(padded)) == 3)


# ── The verdict ────────────────────────────────────────────────────


def judged(probe: shapes.Probe, family: shapes.Code, raw: shapes.Code, hardened: bool = False) -> shapes.Verdict:
    """Return the verdict of a planted pair."""
    return shapes.verdict(probe, shapes.steps_of(family), shapes.steps_of(raw), hardened)


def verdict_cases(expect: testing.Expect) -> None:
    """Plant one pair for each part of THE VERDICT, and hold its problems."""
    load = code_of("probe", plain("endbr64", "mov rax,QWORD PTR [rdi]", "ret"))
    load_other = code_of("probe", plain("endbr64", "mov rcx,QWORD PTR [rsi]", "ret"))
    spill = code_of(
        "probe",
        plain("endbr64", "push rbp", "mov rbp,rsp", "mov QWORD PTR [rbp-0x8],rdi", "mov rax,QWORD PTR [rdi]")
        + plain("pop rbp", "ret"),
    )
    longer = code_of("probe", plain("endbr64", "mov rax,QWORD PTR [rdi]", "lea rax,[rax+rax*2+0x1]", "ret"))
    swapped = code_of("probe", plain("endbr64", "lea rax,[rdi+rsi*4]", "mov eax,DWORD PTR [rax]", "ret"))
    swapped_raw = code_of("probe", plain("endbr64", "shl rsi,0x2", "mov eax,DWORD PTR [rdi+rsi*1]", "ret"))
    branch = code_of(
        "probe",
        plain(
            "endbr64", "mov eax,DWORD PTR [rdi]", "cmp eax,0x7", "je @5", "ret", "mov eax,DWORD PTR [rdi+0x4]", "ret"
        ),
    )
    check = code_of(
        "probe",
        [*plain("endbr64", "test rdi,rdi"), ("je @0", cold(0))] + plain("mov rax,QWORD PTR [rdi]", "ret"),
        [("call QWORD PTR [rip+0x0]", FATAL), *plain("ud2")],
    )
    hardened_check = code_of(
        "probe",
        [*plain("endbr64", "test rdi,rdi"), ("je @0", cold(0)), *plain("test rdi,rdi"), ("je @0", cold(2))]
        + plain("mov rax,QWORD PTR [rdi]", "ret"),
        [("call QWORD PTR [rip+0x0]", FATAL), *plain("ud2", "ud2")],
    )
    trap = code_of(
        "probe",
        [*plain("endbr64", "test rdi,rdi"), ("je @0", cold(0))] + plain("mov rax,QWORD PTR [rdi]", "ret"),
        plain("ud2"),
    )
    call = code_of("probe", [*plain("endbr64"), ("jmp QWORD PTR [rip+0x0]", "R_X86_64_GOTPCRELX\tmemmove-0x4")])
    call_raw = code_of("probe", [*plain("endbr64"), ("jmp QWORD PTR [rip+0x0]", "R_X86_64_GOTPCRELX\tmemcpy-0x4")])
    division = code_of("probe", plain("endbr64", "mov eax,edi", "xor edx,edx", "div esi", "ret"))
    division_raw = code_of("probe", plain("endbr64", "mov eax,edi", "xor edx,edx", "shr esi,1", "mov eax,esi", "ret"))
    indexed = code_of("probe", plain("endbr64", "mov eax,DWORD PTR [rdi+rsi*4]", "ret"))
    loop = plain("endbr64", "xor eax,eax", "add eax,DWORD PTR [rdi]", "add rdi,0x4", "cmp rdi,rsi", "jne @2", "ret")
    loop_raw = code_of("probe", loop)
    loop_work = code_of(
        "probe",
        plain("endbr64", "add eax,DWORD PTR [rdi]", "add rdi,0x4", "add rdx,0x1", "cmp rdi,rsi", "jne @1", "ret"),
    )
    locked = code_of("probe", plain("endbr64", "lock add DWORD PTR [rdi],0x1", "ret"))
    unlocked = code_of("probe", plain("endbr64", "add DWORD PTR [rdi],0x1", "ret"))
    fatal = shapes.Probe("probe", "c", checks=("fatal",))
    cases: tuple[tuple[str, shapes.Probe, shapes.Code, shapes.Code, bool, tuple[str, ...]], ...] = (
        ("a pair with the same instructions", ZERO_COST, load, load, False, ()),
        ("a pair that differs only in its registers", ZERO_COST, load_other, load, False, ()),
        ("a raw side with a spill to the stack", ZERO_COST, load, spill, False, ()),
        ("a family side with more plain instructions", ZERO_COST, longer, load, False, ("declares no cost",)),
        ("plain instructions of the same count", ZERO_COST, swapped, swapped_raw, False, ()),
        ("a family-only branch", ZERO_COST, branch, load, False, ("adds a branch",)),
        ("a declared branch", shapes.Probe("probe", "c", branches=True), branch, load, False, ()),
        ("a family-only check", ZERO_COST, check, load, False, ("check into a fatal path",)),
        ("a declared check", fatal, check, load, False, ()),
        ("a declared check and its hardening test", fatal, hardened_check, load, True, ()),
        ("a hardening test with no hardening", fatal, hardened_check, load, False, ("check into a trap path",)),
        ("a trap with no declared check", ZERO_COST, trap, load, True, ("gives at most 0",)),
        ("a family-only call", ZERO_COST, call, call_raw, False, ("call of memmove",)),
        ("a declared call", shapes.Probe("probe", "c", calls=("memmove",)), call, call_raw, False, ()),
        ("a family-only division", ZERO_COST, division, division_raw, False, ("division",)),
        ("a family-only indexed load", ZERO_COST, indexed, load, False, ("index register",)),
        ("a family-only lea with an index", ZERO_COST, swapped, swapped_raw, False, ()),
        ("family-only work in a loop", ZERO_COST, loop_work, loop_raw, False, ("work to a loop",)),
        ("a lost lock", ZERO_COST, unlocked, locked, False, ("locked instructions",)),
    )
    for title, probe, family, raw, hardened, wanted in cases:
        problems = judged(probe, family, raw, hardened).problems
        holds = all(any(part in problem for problem in problems) for part in wanted) if wanted else not problems
        expect(f"the verdict of '{title}' gives {list(wanted) or 'no problem'}", holds)
    functions = {"probe": check, "probe_raw": load}
    found, shown, verdicts = shapes.pair_findings(functions, (fatal,), False)
    expect(
        "a declared cost of a longer family side gives one warning with the counts, and no error",
        [item.level for item in found] == ["warning"]
        and "family side has 5" in found[0].message
        and not shown
        and verdicts["probe"].is_longer,
    )
    found, shown, _ = shapes.pair_findings(functions, (ZERO_COST,), False)
    expect(
        "an undeclared check gives an error and the disassembly of the two functions",
        bool(found) and all(item.level == "error" for item in found) and len(shown) == 1,
    )


# ── The flags ──────────────────────────────────────────────────────


def flag_cases(expect: testing.Expect) -> None:
    """Plant compile commands for each kind of finding of THE FLAGS OF THE PAIRS."""
    base = ["g++", "-DX=1", "-Iinclude", "-std=c++26", "-fno-plt", "-O3", "-fPIC", "-Wall", "-o", "o.o", "-c", "u.cpp"]
    probe_words = ["g++", "-DX=1", "-std=c++26", "-fno-plt", "-O3", "-fPIC", "-fplugin=q.so", "-g", "-c", "p.cpp"]
    cases: tuple[tuple[str, list[str], list[str], int, tuple[str, ...]], ...] = (
        ("the same code words", base, probe_words, 2, ()),
        ("a new flag of the base", [*base, "-fno-jump-tables"], probe_words, 2, ("-fno-jump-tables, and the probe",)),
        ("a flag that the base lost", base, [*probe_words, "-fno-common"], 2, ("no foundation unit does",)),
        ("a different level", base, [w.replace("-O3", "-O1") for w in probe_words], 2, ("-O3, and the probe",)),
        ("a sanitizer of the base only", [*base, "-fsanitize=address"], probe_words, 2, ("-fsanitize=address",)),
        ("no foundation unit", base, probe_words, 0, ("no unit of the target foundation",)),
    )
    for title, base_words, probe_flags, units, wanted in cases:
        found = shapes.flag_findings(
            shapes.code_flags(probe_flags, "p.cpp"), [shapes.code_flags(base_words, "u.cpp")] * units
        )
        messages = [item.message for item in found]
        holds = all(any(part in message for message in messages) for part in wanted) and bool(messages) == bool(wanted)
        expect(f"the flag case '{title}' gives {list(wanted) or 'no finding'}", holds)
    found = shapes.flag_findings(
        shapes.code_flags(probe_words, "p.cpp"),
        [shapes.code_flags(base, "u.cpp"), shapes.code_flags([*base, "-fno-common"], "u.cpp")],
    )
    expect(
        "foundation units with different code flags give a finding",
        any("do not have the same code flags" in item.message for item in found),
    )
    hardening = (
        (["-O3", "-fharden-compares"], True),
        (["-fharden-conditional-branches", "-fno-harden-conditional-branches"], False),
        (["-fharden-compares", "-fno-harden-conditional-branches"], True),
        (["-O1"], False),
    )
    for words, is_on in hardening:
        expect(
            f"the flags {words} turn the second tests {'on' if is_on else 'off'}", shapes.is_hardened(words) == is_on
        )
    entry = compdb.CompileEntry(
        "p.cpp", "/b", "/b/p.o", "g++ -O1 -fsanitize=address -fno-sanitize-recover=all -o /b/p.o -c p.cpp"
    )
    verdict_words = shapes.command_words(entry, Path("/s/x.o"), with_sanitizers=False)
    expect(
        "the compile of the verdict has no sanitizer word and writes the scratch object",
        verdict_words == ["g++", "-O1", "-o", "/s/x.o", "-c", "p.cpp"] and shapes.has_sanitizer(entry),
    )
    expect(
        "the compile with the sanitizer keeps each word",
        shapes.command_words(entry, Path("/s/x.o"), with_sanitizers=True)[2:4]
        == ["-fsanitize=address", "-fno-sanitize-recover=all"],
    )


def probe_row_cases(expect: testing.Expect) -> None:
    """Hold the probe rows of a plant (THE PAIRS)."""
    rows = rows_grammar.parse(
        "probe load check=fatal,trap branch=yes calls=memmove,memset | claim\n"
        "probe plain | a planted claim\n"
        "probe bad branch=no | a planted claim\n"
        "probe bad check=other | a planted claim\n"
        "probe bad relation=same | a planted claim\n"
        "probe bad check=fatal\n",
        "plant.txt",
    )
    expect(
        "a probe row gives each field of its probe",
        shapes.probe_of_row(rows[0]) == shapes.Probe("load", "claim", ("fatal", "trap"), True, ("memmove", "memset")),
    )
    expect("a probe row with no field declares no cost", not shapes.probe_of_row(rows[1]).declares_cost)
    for row in rows[2:]:
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
    """Return the command of the verdict for a row, with the object `obj`, and with the directory `include` first.

    With a directory, the command does not hold the words of the quarantine plugin.
    """
    words = shapes.command_words(entry, obj, with_sanitizers=False)
    kept = [
        word for word in words[1:] if include is None or not (word.startswith(PLUGIN_WORDS) or word == PLUGIN_MACRO)
    ]
    return [words[0], *([] if include is None else [f"-I{include}"]), *kept]


def failed_probes(entry: compdb.CompileEntry, include: Path | None, probes: Sequence[shapes.Probe]) -> set[str]:
    """Compile the probe unit of a row with the command of the verdict, and return the name of each probe with an error.

    Raises:
        proc.ToolFailed: If the compile fails
    """
    with tempfile.TemporaryDirectory(prefix="codegen-shapes-probe-") as scratch:
        obj = Path(scratch) / "core_shapes.o"
        command = proc.Command(compile_words(entry, obj, include), timeout_s=proc.TOOL_S, cwd=Path(entry.directory))
        proc.require(proc.run(command))
        functions = shapes.codes_of(shapes.disassembled(obj))
    hardened = shapes.is_hardened(shapes.code_flags(entry.argv(), entry.file))
    found, _, _ = shapes.pair_findings(functions, probes, hardened)
    return {item.entity for item in found if item.level == "error"}


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
        "each pair of PROBES holds on the probe unit of the build", failed_probes(entry, None, shapes.PROBES) == set()
    )
    with tempfile.TemporaryDirectory(prefix="codegen-shapes-plant-") as scratch:
        include = Path(scratch) / "include"
        planted_header = include / PLANT_HEADER.relative_to("include")
        planted_header.parent.mkdir(parents=True)
        planted_header.write_text(plant_window(REPO_ROOT / PLANT_HEADER), encoding="utf-8")
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
            normalization_cases,
            walk_cases,
            verdict_cases,
            flag_cases,
            probe_row_cases,
            wrapper_flag_cases,
            wrapper_cases,
            census_cases,
            build_probe_cases,
        ),
    )
