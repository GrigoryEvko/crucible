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
  2b:\tdata16 rex.W call QWORD PTR [rip+0x0]        # 33 <_Z5entryi+0x33>
\t\t\t2f: R_X86_64_GOTPCRELX\t__tls_get_addr-0x4

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
        [item.offset for item in hot.instructions] == [0x0, 0x4, 0x9, 0xF, 0x11, 0x16, 0x1F, 0x2A, 0x2B],
    )
    locked = hot.instructions[1]
    expect(
        "a prefix comes before the mnemonic, and each operand is a record",
        (locked.prefixes, locked.mnemonic) == (("lock",), "xadd")
        and locked.operands == (objdump.Memory("rdi", "rsi", 4, 0x10, "DWORD"), objdump.Register("eax"))
        and locked.operand_spelling() == "DWORD PTR [rdi+rsi*4+0x10],eax",
    )
    load = hot.instructions[2]
    expect(
        "a comment leaves the operands, and a relocation line goes to the instruction before it",
        load.operands == (objdump.Register("eax"), objdump.Memory("rip", displacement=0, size="DWORD"))
        and load.comment == "f <_Z5entryi+0xf>"
        and load.relocation is not None
        and (load.relocation.offset, load.relocation.kind, load.relocation.symbol, load.relocation.addend)
        == (0xB, "R_X86_64_PC32", "shared_counter", -4),
    )
    expect(
        "an annotation leaves the operands of a jump, and its address is a target",
        hot.instructions[3].operands == (objdump.Target(0x4),),
    )
    tls_call = hot.instructions[8]
    expect(
        "each REX prefix is a prefix, so the mnemonic of the call of a TLS access is call",
        (tls_call.prefixes, tls_call.mnemonic) == (("data16", "rex.W"), "call")
        and tls_call.operands == (objdump.Memory("rip", displacement=0, size="QWORD"),),
    )
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
        "memories gives each memory operand",
        locked.memories == (objdump.Memory("rdi", "rsi", 4, 0x10, "DWORD"),) and hot.instructions[0].memories == (),
    )
    orphan = objdump.parse("Disassembly of section .text:\n\n0000000000000000 <f>:\n\t\t\t0: R_X86_64_NONE\tx\n", "x")
    expect("a relocation line with no instruction before it is no part of a function", orphan[0].instructions == ())


# Each shape of an operand, with the mnemonic of its instruction and the record that the parse gives.
OPERANDS: tuple[tuple[str, str, objdump.Operand], ...] = (
    ("mov", "r8d", objdump.Register("r8d")),
    ("vmovdqu8", "zmm0{k1}{z}", objdump.Register("zmm0", "k1", zeroing=True)),
    ("fxch", "st(1)", objdump.Register("st(1)")),
    ("mov", "QWORD PTR [rbp-0x8]", objdump.Memory("rbp", displacement=-8, size="QWORD")),
    ("lea", "[r15*4+0x0]", objdump.Memory(index="r15", scale=4, displacement=0)),
    ("lea", "[rax+rax*1]", objdump.Memory("rax", "rax", 1)),
    ("nop", "WORD PTR cs:[rax+rax*1+0x0]", objdump.Memory("rax", "rax", 1, 0, "WORD", segment="cs")),
    ("mov", "QWORD PTR fs:0x28", objdump.Memory(displacement=0x28, size="QWORD", segment="fs", is_bracketed=False)),
    ("cmp", "DWORD PTR fs:[rax]", objdump.Memory("rax", size="DWORD", segment="fs")),
    ("lea", "[rip+0xffffffffffffffaa]", objdump.Memory("rip", displacement=0xFFFFFFFFFFFFFFAA)),
    ("lea", "[rax+riz*1+0x0]", objdump.Memory("rax", "riz", 1, 0)),
    ("vmovdqu8", "ZMMWORD PTR [rdi]{k1}", objdump.Memory("rdi", size="ZMMWORD", mask="k1")),
    ("vpaddd", "DWORD BCST [rax]", objdump.Memory("rax", size="DWORD", size_mark="BCST")),
    ("vpaddd", "DWORD PTR [rax]{1to16}", objdump.Memory("rax", size="DWORD", broadcast="1to16")),
    ("cmp", "0xffffffff", objdump.Immediate(0xFFFFFFFF)),
    ("shl", "1", objdump.Immediate(1, is_decimal=True)),
    ("jmp", "ff", objdump.Target(0xFF)),
    ("call", "0", objdump.Target(0)),
    ("vaddps", "{rn-sae}", objdump.Opaque("{rn-sae}")),
    ("mov", "ff", objdump.Opaque("ff")),
    ("lea", "[rax-rbx*2]", objdump.Opaque("[rax-rbx*2]")),
    ("lea", "[rax+rbx*3]", objdump.Opaque("[rax+rbx*3]")),
    ("mov", "QWORD PTR [rax]{z}", objdump.Opaque("QWORD PTR [rax]{z}")),
)


def operand_cases(expect: testing.Expect) -> None:
    """Hold the record of each shape of an operand, and the spelling that gives the text of objdump again."""
    for mnemonic, text, wanted in OPERANDS:
        found = objdump.parse_operands(text, mnemonic)
        expect(f"{mnemonic} {text} gives {wanted!r}", found == (wanted,))
        expect(f"the spelling of {text} is the text of objdump", objdump.spelling_of(found) == text)
    two = objdump.parse_operands("rax,QWORD PTR [rdi+0x8]", "mov")
    expect(
        "a comma divides two operands, and spelling() gives each register name through the function of the caller",
        two == (objdump.Register("rax"), objdump.Memory("rdi", displacement=8, size="QWORD"))
        and objdump.spelling_of(two, str.upper) == "RAX,QWORD PTR [RDI+0x8]",
    )
    expect("no operand text gives no operand", objdump.parse_operands("", "ret") == ())
    rip = objdump.Memory("rip", displacement=0, size="QWORD")
    expect(
        "a memory operand with no displacement keeps each other part",
        rip.spelling(keeps_displacement=False) == "QWORD PTR [rip]" and rip.registers() == ("rip",),
    )


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
    return testing.run(__name__.removeprefix("assay."), argv, (parse_cases, operand_cases, compile_cases))
