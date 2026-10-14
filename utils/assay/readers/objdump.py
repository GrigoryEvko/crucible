"""objdump — the one reader of a disassembly: the line grammar of objdump -d -r and the operands of each instruction.

THE COMMAND
    functions() starts objdump one time for an object:

        objdump -d -r --no-show-raw-insn -M intel OBJECT

    Then one run of c++filt (assay.readers.elf.demangle) gives the text of
    each name of a function and of a relocation symbol.  A disassembly has no
    structured shape.  This module holds its grammar and its test.  No other
    module of the package starts objdump, and the rule structured-reads
    refuses a run of objdump in a different module.

THE LINES
    The module reads these lines of the text, and it ignores each other line.
    The head of the file, an empty line and the line "..." of zero bytes that
    objdump does not print are such lines:

        Disassembly of section SECTION:           the section of the functions after it
        ADDRESS <SYMBOL>:                          the head of a function
        OFFSET:<tab>PREFIX... MNEMONIC OPERANDS   one instruction of the function
        <tabs>OFFSET: TYPE<tab>SYMBOL[+-0xADDEND] one relocation of the instruction before it

    OFFSET and ADDRESS are hexadecimal numbers.  An instruction line can end
    with a comment after "#", and an operand can end with an annotation
    " <SYMBOL+0xN>".  An instruction line with no words after the offset is
    no instruction.

THE INSTRUCTION
    The prefixes of an instruction are the words before its mnemonic: each
    word of PREFIXES, each REX prefix ("rex" or "rex." and its bits) and each
    pseudo-prefix such as `{vex}`.  When each word is a prefix, the last
    prefix is the mnemonic.  The operands are the words after the mnemonic,
    with no comment and no annotation.  The relocation of an instruction is
    the last relocation line after it.

THE OPERANDS
    objdump divides two operands with a comma and no space.  parse_operands()
    gives each operand one record:
      `Register`   a register of NAMES, with the marks `{kN}` and `{z}` of an
                   AVX-512 operation
      `Memory`     the width word and PTR or BCST, a segment such as "fs:",
                   and the address in brackets: a base, an index with its
                   scale and a signed displacement.  Each part can be
                   missing.  A segment with a number and no brackets, such as
                   fs:0x28, is a memory operand too.  A memory operand can
                   have the marks `{kN}` and `{1toN}`
      `Immediate`  a number 0xN, or a decimal number.  objdump prints the
                   count 1 of `shl` and `sar` in decimal digits
      `Target`     the hexadecimal address with no 0x of a jump, a call, a
                   loop or `xbegin`, after the annotation goes
      `Opaque`     each other text.  The parse reads each text, and a new
                   spelling of objdump then gives no error here.  A caller
                   that must know each operand refuses an `Opaque` record.
    spelling() of each record gives the text that objdump prints.  The parse
    then keeps each part of the line.  A caller gives spelling() a function
    for the names of the registers, such as the class of a register.

THE NAMES
    The symbol of a function or of a relocation is the text of objdump, and
    its name is the text that c++filt gives for it.  A symbol of a section,
    such as .text.unlikely.NAME, and a name that is not mangled give their own
    text.  The names are then the names that objdump -C prints.

`guard selftest tests.readers.test_objdump` does the test of the module.
"""

import os
from collections.abc import Callable

from assay.core import proc
from assay.readers import elf

# The command of THE COMMAND, before the object.
COMMAND = ("objdump", "-d", "-r", "--no-show-raw-insn", "-M", "intel")
SECTION_HEAD = "Disassembly of section "
SEGMENTS = frozenset({"cs", "ds", "es", "fs", "gs", "ss"})
# The words that objdump prints before a mnemonic: the prefixes lock, rep and its variants, the branch and segment
# prefixes, data16 and addr32.
PREFIXES = frozenset({"lock", "rep", "repz", "repe", "repnz", "repne", "notrack", "bnd", "data16", "addr32"}) | SEGMENTS
# A REX prefix: "rex" alone, or "rex." and the letters of its bits (THE INSTRUCTION).
REX = "rex"
REX_BITS = frozenset("WRXB")
RELOCATION_MARK = "R_"
COMMENT_MARK = "#"
ANNOTATION_MARK = " <"
OPERAND_SEPARATOR = ","
HEX_DIGITS = frozenset("0123456789abcdef")
DECIMAL_DIGITS = frozenset("0123456789")
HEX_MARK = "0x"
# The mnemonics with a number operand that is the address of the target, with no 0x (THE OPERANDS).
TARGET_MNEMONICS = frozenset({"call", "jmp", "loop", "loope", "loopne", "loopz", "loopnz", "xbegin"})
TARGET_MNEMONIC_START = "j"
# The width words of a memory operand, and the two words after a width word.
SIZES = frozenset({"BYTE", "WORD", "DWORD", "FWORD", "QWORD", "TBYTE", "OWORD", "XMMWORD", "YMMWORD", "ZMMWORD"})
SIZE_MARKS = frozenset({"PTR", "BCST"})
SEGMENT_MARK = ":"
ZEROING = "z"
MASK_START = "k"
BROADCAST_MARK = "1to"
SCALE_MARK = "*"
SCALES = frozenset({1, 2, 4, 8})


def register_names() -> frozenset[str]:
    """Return each register name that objdump prints for x86-64 (THE OPERANDS).

    The names are the general registers of each width, also r16 to r31 of APX, rip and the index pseudo-registers.
    They are also the segment registers and the registers of the vector unit, AVX-512, MMX, x87, MPX and AMX.
    """
    names = {"rip", "eip", "ip", "riz", "eiz", "st", *SEGMENTS}
    for letter in "abcd":
        names |= {f"r{letter}x", f"e{letter}x", f"{letter}x", f"{letter}l", f"{letter}h"}
    for base in ("si", "di", "sp", "bp"):
        names |= {f"r{base}", f"e{base}", base, f"{base}l"}
    for number in range(8, 32):
        names |= {f"r{number}", f"r{number}d", f"r{number}w", f"r{number}b"}
    for number in range(32):
        names |= {f"xmm{number}", f"ymm{number}", f"zmm{number}"}
    for number in range(8):
        names |= {f"k{number}", f"mm{number}", f"st({number})", f"tmm{number}"}
    names |= {f"bnd{number}" for number in range(4)}
    return frozenset(names)


NAMES = register_names()


class ObjdumpError(ValueError):
    """objdump or c++filt does not give the disassembly of an object."""


def same_name(name: str) -> str:
    """Return a register name as it is: the default function of spelling()."""
    return name


def hex_text(value: int) -> str:
    """Return a number as objdump prints it, 0x and its lowercase hexadecimal digits."""
    return f"{HEX_MARK}{value:x}"


def hex_number(text: str) -> int | None:
    """Return the value of a hexadecimal number of lowercase digits with no prefix, or None."""
    return int(text, 16) if text and set(text) <= HEX_DIGITS else None


def marked_hex(text: str) -> int | None:
    """Return the value of a number 0xN of lowercase digits, or None."""
    return hex_number(text[len(HEX_MARK) :]) if text.startswith(HEX_MARK) else None


class Register(elf.Record):
    """One register operand: its name, the `{kN}` register of an AVX-512 operation, and its zeroing mark."""

    __slots__ = ("mask", "name", "zeroing")

    def __init__(self, name: str, mask: str | None = None, zeroing: bool = False) -> None:
        """Hold the parts of one register operand."""
        self.name = name
        self.mask = mask
        self.zeroing = zeroing

    def spelling(self, name_of: Callable[[str], str] = same_name) -> str:
        """Return the operand as objdump prints it, with each register name through `name_of`."""
        mask = "" if self.mask is None else f"{{{name_of(self.mask)}}}"
        return f"{name_of(self.name)}{mask}{'{z}' if self.zeroing else ''}"


class Memory(elf.Record):
    """One memory operand: its width, its segment, its address and its AVX-512 marks (THE OPERANDS).

    `size` is a width word of SIZES, and `size_mark` is PTR or BCST.  `scale` is 0 when the address has no index.
    `displacement` is the signed number that objdump prints, or None when it prints none.  `is_bracketed` is False
    for a segment and a number with no brackets, such as fs:0x28.  `mask` and `broadcast` hold the marks `{kN}` and
    `{1toN}` after the operand.
    """

    __slots__ = (
        "base",
        "broadcast",
        "displacement",
        "index",
        "is_bracketed",
        "mask",
        "scale",
        "segment",
        "size",
        "size_mark",
    )

    def __init__(
        self,
        base: str | None = None,
        index: str | None = None,
        scale: int = 0,
        displacement: int | None = None,
        size: str | None = None,
        size_mark: str = "PTR",
        segment: str | None = None,
        is_bracketed: bool = True,
        mask: str | None = None,
        broadcast: str | None = None,
    ) -> None:
        """Hold the parts of one memory operand."""
        self.base = base
        self.index = index
        self.scale = scale
        self.displacement = displacement
        self.size = size
        self.size_mark = size_mark
        self.segment = segment
        self.is_bracketed = is_bracketed
        self.mask = mask
        self.broadcast = broadcast

    def registers(self) -> tuple[str, ...]:
        """Return the base and the index of the address that the operand has, in that sequence."""
        return tuple(name for name in (self.base, self.index) if name is not None)

    def spelling(self, name_of: Callable[[str], str] = same_name, keeps_displacement: bool = True) -> str:
        """Return the operand as objdump prints it, with each register name through `name_of`.

        With `keeps_displacement` False, the text has no displacement.
        """
        size = "" if self.size is None else f"{self.size} {self.size_mark} "
        segment = "" if self.segment is None else f"{name_of(self.segment)}{SEGMENT_MARK}"
        displacement = self.displacement if keeps_displacement else None
        if not self.is_bracketed:
            address = "" if displacement is None else hex_text(displacement)
        else:
            parts = [] if self.base is None else [name_of(self.base)]
            if self.index is not None:
                parts.append(f"{name_of(self.index)}{SCALE_MARK}{self.scale}")
            text = "+".join(parts)
            if displacement is not None:
                sign = "-" if displacement < 0 else ("+" if text else "")
                text += f"{sign}{hex_text(abs(displacement))}"
            address = f"[{text}]"
        mask = "" if self.mask is None else f"{{{name_of(self.mask)}}}"
        broadcast = "" if self.broadcast is None else f"{{{self.broadcast}}}"
        return f"{size}{segment}{address}{mask}{broadcast}"


class Immediate(elf.Record):
    """One number operand: its value, and True when objdump prints it in decimal digits (THE OPERANDS)."""

    __slots__ = ("is_decimal", "value")

    def __init__(self, value: int, is_decimal: bool = False) -> None:
        """Hold the parts of one number operand."""
        self.value = value
        self.is_decimal = is_decimal

    def spelling(self, name_of: Callable[[str], str] = same_name) -> str:
        """Return the operand as objdump prints it.  A number has no register name, and `name_of` has no effect."""
        return str(self.value) if self.is_decimal else hex_text(self.value)


class Target(elf.Record):
    """The address of the target of a jump, a call, a loop or xbegin, in its section (THE OPERANDS)."""

    __slots__ = ("address",)

    def __init__(self, address: int) -> None:
        """Hold the address of the target."""
        self.address = address

    def spelling(self, name_of: Callable[[str], str] = same_name) -> str:
        """Return the address as objdump prints it, with no 0x.  An address has no register name."""
        return f"{self.address:x}"


class Opaque(elf.Record):
    """An operand that no other record of THE OPERANDS reads: its text."""

    __slots__ = ("text",)

    def __init__(self, text: str) -> None:
        """Hold the text of the operand."""
        self.text = text

    def spelling(self, name_of: Callable[[str], str] = same_name) -> str:
        """Return the text of the operand.  The text has no register name that the parse knows."""
        return self.text


Operand = Register | Memory | Immediate | Target | Opaque


def split_marks(text: str) -> tuple[str, tuple[str, ...]] | None:
    """Return the text of an operand with no mark such as `{k1}` at its end, and each such mark, or None.

    The result is None when a mark has no "{" before its "}".  Complexity: linear in the length of the text.
    """
    marks: list[str] = []
    core = text
    while core.endswith("}"):
        start = core.rfind("{")
        if start < 0:
            return None
        marks.insert(0, core[start + 1 : -1])
        core = core[:start]
    return core, tuple(marks)


def mask_and_zeroing(marks: tuple[str, ...]) -> tuple[str | None, bool] | None:
    """Return the `{kN}` register and the zeroing mark of the marks of a register, or None for a different mark."""
    mask: str | None = None
    zeroing = False
    for mark in marks:
        if mark == ZEROING and not zeroing:
            zeroing = True
        elif mark in NAMES and mark.startswith(MASK_START) and mask is None:
            mask = mark
        else:
            return None
    return mask, zeroing


def address_of(text: str) -> tuple[str | None, str | None, int, int | None] | None:
    """Return the base, the index, the scale and the displacement of the address in brackets, or None.

    An address is [BASE][+INDEX*SCALE][+-0xN], with at least one part, as objdump prints it.  Complexity: linear in
    the length of the text.
    """
    terms: list[tuple[str, str]] = []
    sign, start = "+", 0
    for place, character in enumerate(text):
        if character in "+-" and place > 0:
            terms.append((sign, text[start:place]))
            sign, start = character, place + 1
    terms.append((sign, text[start:]))
    base: str | None = None
    index: str | None = None
    scale = 0
    displacement: int | None = None
    for number, (term_sign, term) in enumerate(terms):
        value = marked_hex(term)
        if value is not None and number == len(terms) - 1 and displacement is None:
            displacement = -value if term_sign == "-" else value
        elif term_sign == "-":
            return None
        elif SCALE_MARK in term and index is None:
            name, _, factor = term.partition(SCALE_MARK)
            if name not in NAMES or not factor.isdigit() or int(factor) not in SCALES:
                return None
            index, scale = name, int(factor)
        elif number == 0 and term in NAMES:
            base = term
        else:
            return None
    if base is None and index is None and displacement is None:
        return None
    return base, index, scale, displacement


def memory_of(text: str, marks: tuple[str, ...]) -> Memory | None:
    """Return the memory operand of a text with no marks, or None when the text is no memory operand."""
    size: str | None = None
    size_mark = "PTR"
    rest = text
    words = text.split(" ", 2)
    if len(words) == 3 and words[0] in SIZES and words[1] in SIZE_MARKS:
        size, size_mark, rest = words
    segment: str | None = None
    head, separator, tail = rest.partition(SEGMENT_MARK)
    if separator and head in SEGMENTS:
        segment, rest = head, tail
    mask: str | None = None
    broadcast: str | None = None
    for mark in marks:
        if mark in NAMES and mark.startswith(MASK_START) and mask is None:
            mask = mark
        elif mark.startswith(BROADCAST_MARK) and mark[len(BROADCAST_MARK) :].isdigit() and broadcast is None:
            broadcast = mark
        else:
            return None
    if rest.startswith("[") and rest.endswith("]"):
        address = address_of(rest[1:-1])
        if address is None:
            return None
        base, index, scale, displacement = address
        return Memory(base, index, scale, displacement, size, size_mark, segment, True, mask, broadcast)
    value = marked_hex(rest)
    if segment is not None and value is not None:
        return Memory(None, None, 0, value, size, size_mark, segment, False, mask, broadcast)
    return None


def is_target_mnemonic(mnemonic: str) -> bool:
    """Tell if the number operand of a mnemonic is the address of a target (TARGET_MNEMONICS)."""
    return mnemonic in TARGET_MNEMONICS or mnemonic.startswith(TARGET_MNEMONIC_START)


def operand_of(text: str, mnemonic: str) -> Operand:
    """Return the record of one operand of an instruction with the mnemonic (THE OPERANDS)."""
    split = split_marks(text)
    if split is None:
        return Opaque(text)
    core, marks = split
    if core in NAMES:
        register_marks = mask_and_zeroing(marks)
        return Opaque(text) if register_marks is None else Register(core, *register_marks)
    memory = memory_of(core, marks)
    if memory is not None:
        return memory
    if marks:
        return Opaque(text)
    value = marked_hex(core)
    if value is not None:
        return Immediate(value)
    if is_target_mnemonic(mnemonic):
        address = hex_number(core)
        return Opaque(text) if address is None else Target(address)
    if core and set(core) <= DECIMAL_DIGITS:
        return Immediate(int(core), is_decimal=True)
    return Opaque(text)


def parse_operands(text: str, mnemonic: str) -> tuple[Operand, ...]:
    """Return the record of each operand of the operand text of an instruction (THE OPERANDS).

    The text has no comment and no annotation, and one space divides two words.  Complexity: linear in the length of
    the text.
    """
    return tuple(operand_of(item, mnemonic) for item in text.split(OPERAND_SEPARATOR)) if text else ()


def spelling_of(operands: tuple[Operand, ...], name_of: Callable[[str], str] = same_name) -> str:
    """Return the operand text of an instruction as objdump prints it, with each register name through `name_of`."""
    return OPERAND_SEPARATOR.join(operand.spelling(name_of) for operand in operands)


class Relocation(elf.Record):
    """One relocation line: its offset, its type, its symbol as objdump prints it, its name and its addend."""

    __slots__ = ("addend", "kind", "name", "offset", "symbol")

    def __init__(self, offset: int, kind: str, symbol: str, name: str, addend: int) -> None:
        """Hold the parts of one relocation line."""
        self.offset = offset
        self.kind = kind
        self.symbol = symbol
        self.name = name
        self.addend = addend


class Instruction(elf.Record):
    """One instruction: its offset, its prefixes, its mnemonic, its operands, its comment and its relocations.

    `operands` holds the record of each operand (THE OPERANDS).  `lines` holds the instruction line and each
    relocation line after it, as objdump prints them.
    """

    __slots__ = ("comment", "lines", "mnemonic", "offset", "operands", "prefixes", "relocations")

    def __init__(
        self,
        offset: int,
        prefixes: tuple[str, ...],
        mnemonic: str,
        operands: tuple[Operand, ...],
        comment: str,
        relocations: tuple[Relocation, ...],
        lines: tuple[str, ...],
    ) -> None:
        """Hold the parts of one instruction."""
        self.offset = offset
        self.prefixes = prefixes
        self.mnemonic = mnemonic
        self.operands = operands
        self.comment = comment
        self.relocations = relocations
        self.lines = lines

    @property
    def relocation(self) -> Relocation | None:
        """The last relocation of the instruction, or None (THE INSTRUCTION)."""
        return self.relocations[-1] if self.relocations else None

    @property
    def memories(self) -> tuple[Memory, ...]:
        """The memory operands of the instruction, in sequence."""
        return tuple(operand for operand in self.operands if isinstance(operand, Memory))

    def operand_spelling(self, name_of: Callable[[str], str] = same_name) -> str:
        """Return the operand text as objdump prints it, with each register name through `name_of`."""
        return spelling_of(self.operands, name_of)


class Function(elf.Record):
    """One function of a disassembly: its section, its symbol, its name and its instructions (THE NAMES)."""

    __slots__ = ("instructions", "name", "section", "symbol")

    def __init__(self, section: str, symbol: str, name: str, instructions: tuple[Instruction, ...]) -> None:
        """Hold the parts of one function."""
        self.section = section
        self.symbol = symbol
        self.name = name
        self.instructions = instructions


def is_prefix(word: str) -> bool:
    """Tell if a word before a mnemonic is a prefix (THE INSTRUCTION)."""
    if word in PREFIXES:
        return True
    if word == REX or (word.startswith(f"{REX}.") and set(word[len(REX) + 1 :]) <= REX_BITS):
        return True
    return word.startswith("{") and word.endswith("}") and len(word) > 2


def split_value(value: str) -> tuple[str, int]:
    """Return the symbol and the addend of the value of a relocation line: SYMBOL, SYMBOL+0xN or SYMBOL-0xN."""
    for mark, sign in (("+0x", 1), ("-0x", -1)):
        place = value.rfind(mark)
        if place > 0:
            number = hex_number(value[place + len(mark) :])
            if number is not None:
                return value[:place], sign * number
    return value, 0


class Builder:
    """The state of one parse: the functions that it read, the open function and the open instruction."""

    def __init__(self) -> None:
        """Start with no function and no section."""
        self.functions: list[tuple[str, str, list[Instruction]]] = []
        self.section = ""
        self.instructions: list[Instruction] | None = None

    def add_relocation(self, relocation: Relocation, line: str) -> None:
        """Give a relocation line to the instruction before it.  A relocation before each instruction has no owner."""
        if not self.instructions:
            return
        last = self.instructions[-1]
        self.instructions[-1] = Instruction(
            last.offset,
            last.prefixes,
            last.mnemonic,
            last.operands,
            last.comment,
            (*last.relocations, relocation),
            (*last.lines, line),
        )

    def read_line(self, line: str) -> None:
        """Read one line of the text (THE LINES)."""
        if line.startswith(SECTION_HEAD) and line.endswith(":"):
            self.section = line[len(SECTION_HEAD) : -1]
            self.instructions = None
            return
        head, separator, symbol = line.partition(" <")
        if separator and symbol.endswith(">:") and hex_number(head) is not None:
            self.instructions = []
            self.functions.append((self.section, symbol[:-2], self.instructions))
            return
        if self.instructions is None:
            return
        offset_text, separator, rest = line.strip().partition(":")
        offset = hex_number(offset_text)
        if not separator or offset is None:
            return
        words = rest.split(None, 1)
        if words and words[0].startswith(RELOCATION_MARK) and len(words) == 2:
            symbol_text, addend = split_value(words[1].strip())
            self.add_relocation(Relocation(offset, words[0], symbol_text, symbol_text, addend), line)
            return
        text, _, comment = rest.partition(COMMENT_MARK)
        tokens = text.split()
        if not tokens:
            return
        prefixes: list[str] = []
        while tokens and is_prefix(tokens[0]):
            prefixes.append(tokens.pop(0))
        if not tokens:
            tokens = [prefixes.pop()]
        operands = " ".join(tokens[1:]).split(ANNOTATION_MARK, 1)[0]
        self.instructions.append(
            Instruction(
                offset, tuple(prefixes), tokens[0], parse_operands(operands, tokens[0]), comment.strip(), (), (line,)
            )
        )


def parse(text: str, where: str) -> tuple[Function, ...]:
    """Return each function of the text of objdump -d -r, with the names that c++filt gives (THE NAMES).

    Complexity: linear in the lines of the text, and one run of c++filt.

    Raises:
        ObjdumpError: If c++filt does not give the names
    """
    builder = Builder()
    for line in text.splitlines():
        builder.read_line(line)
    symbols = sorted(
        {symbol for _, symbol, _ in builder.functions}
        | {
            relocation.symbol
            for _, _, instructions in builder.functions
            for instruction in instructions
            for relocation in instruction.relocations
        }
    )
    try:
        names = dict(zip(symbols, elf.demangle(symbols), strict=True))
    except elf.ElfError as problem:
        raise ObjdumpError(f"{where}: {problem}") from None

    def named(instruction: Instruction) -> Instruction:
        """Return the instruction with the name that c++filt gives for the symbol of each relocation."""
        if not instruction.relocations:
            return instruction
        relocations = tuple(
            Relocation(item.offset, item.kind, item.symbol, names[item.symbol], item.addend)
            for item in instruction.relocations
        )
        return Instruction(
            instruction.offset,
            instruction.prefixes,
            instruction.mnemonic,
            instruction.operands,
            instruction.comment,
            relocations,
            instruction.lines,
        )

    return tuple(
        Function(section, symbol, names[symbol], tuple(named(instruction) for instruction in instructions))
        for section, symbol, instructions in builder.functions
    )


def functions(path: str | os.PathLike[str]) -> tuple[Function, ...]:
    """Disassemble one object (THE COMMAND), and return each function of it.

    Raises:
        ObjdumpError: If objdump cannot start, runs longer than its limit or fails, or c++filt does not give the names
    """
    command = proc.Command([*COMMAND, os.fspath(path)], timeout_s=proc.TOOL_S)
    try:
        done = proc.require(proc.run(command))
    except proc.ToolFailed as problem:
        raise ObjdumpError(f"objdump does not disassemble {os.fspath(path)}: {problem}") from None
    return parse(done.out, os.fspath(path))
