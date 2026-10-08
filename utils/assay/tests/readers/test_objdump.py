"""The test of the reader of a disassembly (assay.readers.objdump).

`guard selftest tests.readers.test_objdump` runs the test.  Each case writes
one line.  Exit 0 when each case holds, else 1.

The planted case gives the parse each shape of a line of the grammar.  The
compile case uses the compiler that ASSAY_TEST_CXX names, and the
registration of the test in test/CMakeLists.txt gives the compiler of the
build.  It disassembles a small object with objdump.
"""

import tempfile
from pathlib import Path

from assay.core import proc, testing
from assay.readers import objdump

# The text of objdump -d -r for two functions, with each shape of THE LINES of the module.
PLANTED = """\

unit.o:     file format elf64-x86-64


Disassembly of section .text._Z5entryi:

0000000000000000 <_Z5entryi>:
   0:\tendbr64
   4:\tlock xadd DWORD PTR [rdi+rsi*4+0x10],eax
   9:\tmov    eax,DWORD PTR [rip+0x0]        # f <_Z5entryi+0xf>
\t\t\tb: R_X86_64_PC32\tshared_counter-0x4
   f:\tjne    4 <_Z5entryi+0x4>
  11:\tcall   16 <_Z5entryi+0x16>
\t\t\t12: R_X86_64_PLT32\t_ZN3foo3barEv-0x4
  16:\tcs nop WORD PTR [rax+rax*1+0x0]
  1f:\tdata16
  20:\t
\t...
  2a:\tret

Disassembly of section .text.unlikely._Z5entryi:

0000000000000000 <_Z5entryi.cold>:
   0:\tmov    rsi,QWORD PTR [rip+0x0]
\t\t\t3: R_X86_64_REX_GOTPCRELX\t_ZTIi+0x10
   7:\tjmp    0 <_Z5entryi.cold>
\t\t\t8: R_X86_64_PLT32\t.text._Z5entryi
"""


def parse_cases(expect: testing.Expect) -> None:
    """Check each shape of the grammar on the planted text."""
    hot, cold = objdump.parse(PLANTED, "planted")
    expect(
        "a function has its section, its symbol and the name that c++filt gives",
        (hot.section, hot.symbol, hot.name) == (".text._Z5entryi", "_Z5entryi", "entry(int)")
        and (cold.section, cold.name) == (".text.unlikely._Z5entryi", "entry(int) [clone .cold]"),
    )
    expect(
        "each instruction line gives one instruction, and an empty one or the line of zero bytes gives none",
        [item.offset for item in hot.instructions] == [0x0, 0x4, 0x9, 0xF, 0x11, 0x16, 0x1F, 0x2A],
    )
    locked = hot.instructions[1]
    expect(
        "a prefix comes before the mnemonic, and the operands have one space between two words",
        (locked.prefixes, locked.mnemonic, locked.operands) == (("lock",), "xadd", "DWORD PTR [rdi+rsi*4+0x10],eax"),
    )
    load = hot.instructions[2]
    expect(
        "a comment leaves the operands, and a relocation line goes to the instruction before it",
        load.operands == "eax,DWORD PTR [rip+0x0]"
        and load.comment == "f <_Z5entryi+0xf>"
        and load.relocation is not None
        and (load.relocation.offset, load.relocation.kind, load.relocation.symbol, load.relocation.addend)
        == (0xB, "R_X86_64_PC32", "shared_counter", -4),
    )
    expect("an annotation leaves the operands of a jump", hot.instructions[3].operands == "4")
    call = hot.instructions[4]
    expect(
        "the relocation of a call names its symbol and the text that c++filt gives",
        call.relocation is not None
        and (call.relocation.symbol, call.relocation.name) == ("_ZN3foo3barEv", "foo::bar()"),
    )
    nop = hot.instructions[5]
    expect("a segment prefix is a prefix", (nop.prefixes, nop.mnemonic) == (("cs",), "nop"))
    only_prefixes = hot.instructions[6]
    expect("an instruction of prefixes only has the last prefix as its mnemonic", only_prefixes.mnemonic == "data16")
    expect("an instruction with no relocation line has none", hot.instructions[0].relocation is None)
    first, jump = cold.instructions
    expect(
        "a positive addend and a section symbol are read",
        first.relocation is not None
        and (first.relocation.symbol, first.relocation.addend) == ("_ZTIi", 16)
        and jump.relocation is not None
        and (jump.relocation.symbol, jump.relocation.name, jump.relocation.addend)
        == (".text._Z5entryi", ".text._Z5entryi", 0),
    )
    expect(
        "memory_operands gives the text of each memory operand",
        locked.memory_operands == ("rdi+rsi*4+0x10",) and hot.instructions[0].memory_operands == (),
    )
    orphan = objdump.parse("Disassembly of section .text:\n\n0000000000000000 <f>:\n\t\t\t0: R_X86_64_NONE\tx\n", "x")
    expect("a relocation line with no instruction before it is no part of a function", orphan[0].instructions == ())


def register_cases(expect: testing.Expect) -> None:
    """Check register_words() on each shape of a register name and on words that are not registers."""
    cases = (
        ("rdi+rsi*4+0x10", ("rdi", "rsi")),
        ("rip+0x0", ("rip",)),
        ("r8d+r15*1", ("r8d", "r15")),
        ("eax+eiz*1", ("eax", "eiz")),
        ("sil", ("sil",)),
        ("bx+si", ("bx", "si")),
        ("0x10", ()),
        ("rax+ymm1*4", ("rax",)),
    )
    for text, expected in cases:
        expect(f"register_words({text!r}) gives {expected}", objdump.register_words(text) == expected)


def compile_cases(expect: testing.Expect) -> None:
    """Disassemble an object of the compiler, and refuse a file that objdump cannot read."""
    cxx = testing.patched_compiler()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the compiler", bool(cxx) and Path(cxx).is_file())
    if not cxx or not Path(cxx).is_file():
        return
    unit = 'extern "C" int external_sink(int);\nextern "C" int probe_call(int value) { return external_sink(value) + 1; }\n'
    with tempfile.TemporaryDirectory(prefix="objdump-test-") as scratch_text:
        scratch = Path(scratch_text)
        (scratch / "unit.cpp").write_text(unit, encoding="utf-8")
        argv = [cxx, "-std=c++26", "-O2", "-ffunction-sections", "-c", "unit.cpp", "-o", "unit.o"]
        proc.require(proc.run(proc.Command(argv, timeout_s=proc.QUICK_S, cwd=scratch)))
        found = {function.symbol: function for function in objdump.functions(scratch / "unit.o")}
        probe = found.get("probe_call")
        expect(
            "the object gives the function, its call of the external function and its return",
            probe is not None
            and any(
                item.mnemonic == "call" and item.relocation is not None and item.relocation.symbol == "external_sink"
                for item in probe.instructions
            )
            and probe.instructions[-1].mnemonic == "ret",
        )
        (scratch / "text.o").write_text("not an object\n", encoding="utf-8")
        try:
            objdump.functions(scratch / "text.o")
            expect("a file that objdump cannot read is refused", False)
        except objdump.ObjdumpError:
            expect("a file that objdump cannot read is refused", True)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (parse_cases, register_cases, compile_cases))
