"""The planted owner of the bars of a row."""


def cells(line: str) -> list[str]:
    """Divide a line at its bars."""
    return line.split(" | ")
