"""objdump — the one reader of a disassembly: the line grammar of objdump -d -r.

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
    The prefixes of an instruction are the words of PREFIXES before its
    mnemonic.  When each word is a prefix, the last prefix is the mnemonic.
    The operands are the words after the mnemonic, with one space between two
    words, and with no comment and no annotation.  A memory operand is the
    text of the operands in "[" and "]", and its registers are its words with
    the shape of a register name of x86-64 (register_words).  The relocation
    of an instruction is the last relocation line after it.

THE NAMES
    The symbol of a function or of a relocation is the text of objdump, and
    its name is the text that c++filt gives for it.  A symbol of a section,
    such as .text.unlikely.NAME, and a name that is not mangled give their own
    text.  The names are then the names that objdump -C prints.

`guard selftest tests.readers.test_objdump` does the test of the module.
"""

import os

from assay.core import proc
from assay.readers import elf

# The command of THE COMMAND, before the object.
COMMAND = ("objdump", "-d", "-r", "--no-show-raw-insn", "-M", "intel")
SECTION_HEAD = "Disassembly of section "
# The words that objdump prints before a mnemonic: the prefixes lock, rep and its variants, the branch and segment
# prefixes, and data16.
PREFIXES = frozenset(
    {"lock", "rep", "repz", "repe", "repnz", "repne", "notrack", "bnd", "data16", "cs", "ds", "es", "ss", "fs", "gs"}
)
RELOCATION_MARK = "R_"
COMMENT_MARK = "#"
ANNOTATION_MARK = " <"
HEX_DIGITS = frozenset("0123456789abcdef")
LOWER = frozenset("abcdefghijklmnopqrstuvwxyz")
LOWER_AND_DIGITS = LOWER | frozenset("0123456789")


class ObjdumpError(ValueError):
    """objdump or c++filt does not give the disassembly of an object, or a line does not obey the grammar."""


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

    `lines` holds the instruction line and each relocation line after it, as
    objdump prints them.
    """

    __slots__ = ("comment", "lines", "mnemonic", "offset", "operands", "prefixes", "relocations")

    def __init__(
        self,
        offset: int,
        prefixes: tuple[str, ...],
        mnemonic: str,
        operands: str,
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
    def memory_operands(self) -> tuple[str, ...]:
        """The text in "[" and "]" of each memory operand, in sequence.  Complexity: linear in the operands."""
        found: list[str] = []
        start = self.operands.find("[")
        while start >= 0:
            stop = self.operands.find("]", start + 1)
            if stop < 0:
                break
            found.append(self.operands[start + 1 : stop])
            start = self.operands.find("[", stop + 1)
        return tuple(found)


class Function(elf.Record):
    """One function of a disassembly: its section, its symbol, its name and its instructions (THE NAMES)."""

    __slots__ = ("instructions", "name", "section", "symbol")

    def __init__(self, section: str, symbol: str, name: str, instructions: tuple[Instruction, ...]) -> None:
        """Hold the parts of one function."""
        self.section = section
        self.symbol = symbol
        self.name = name
        self.instructions = instructions


def register_words(address: str) -> tuple[str, ...]:
    """Return each word of a memory operand with the shape of a register name of x86-64, in sequence.

    A word is a run of letters, digits and "_".  The shapes are: "r" and
    lowercase letters or digits, "e" and two lowercase letters, two lowercase
    letters and "l", and two lowercase letters.  Complexity: linear in the text.
    """
    words: list[str] = []
    current: list[str] = []
    for character in [*address, " "]:
        if character.isalnum() or character == "_":
            current.append(character)
            continue
        word = "".join(current)
        current = []
        is_long = len(word) >= 2 and word[0] == "r" and set(word[1:]) <= LOWER_AND_DIGITS
        is_short = len(word) in (2, 3) and set(word) <= LOWER and (len(word) == 2 or word[0] == "e" or word[2] == "l")
        if is_long or is_short:
            words.append(word)
    return tuple(words)


def hex_number(text: str) -> int | None:
    """Return the value of a hexadecimal number of lowercase digits with no prefix, or None."""
    return int(text, 16) if text and set(text) <= HEX_DIGITS else None


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
        while tokens and tokens[0] in PREFIXES:
            prefixes.append(tokens.pop(0))
        if not tokens:
            tokens = [prefixes.pop()]
        operands = " ".join(tokens[1:]).split(ANNOTATION_MARK, 1)[0]
        self.instructions.append(
            Instruction(offset, tuple(prefixes), tokens[0], operands, comment.strip(), (), (line,))
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
