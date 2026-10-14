"""A planted module that reads the parts of an operand from its record, and gives its spelling only to a message."""

from assay.readers import objdump


def message(instruction: objdump.Instruction) -> str:
    """Return a message with the mnemonic and the spelling of the operands of an instruction."""
    return f"{instruction.mnemonic} {instruction.operand_spelling()}"


def is_accumulator(operand: objdump.Operand) -> bool:
    """Tell whether an operand is the register rax, with a compare of its whole spelling."""
    return operand.spelling() == "rax"


def bases(instruction: objdump.Instruction) -> tuple[str, ...]:
    """Return the base register of each memory operand of an instruction."""
    return tuple(memory.base for memory in instruction.memories if memory.base is not None)
