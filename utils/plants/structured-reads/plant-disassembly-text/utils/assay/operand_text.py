"""A planted module that reads the parts of an operand of objdump from its spelling, and not from its record."""

from assay.readers import objdump


def first_operand(instruction: objdump.Instruction) -> str:
    """Return the text of the first operand of an instruction."""
    return instruction.operand_spelling().split(",")[0]


def is_memory(operand: objdump.Operand) -> bool:
    """Tell whether an operand reads memory, from the brackets of its spelling."""
    return "[" in operand.spelling()


def size_of(operands: tuple[objdump.Operand, ...]) -> str:
    """Return the size word of the first memory operand of the operands."""
    return objdump.spelling_of(operands).strip().partition(" PTR")[0]
