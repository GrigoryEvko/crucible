"""A planted module with small bodies that agree with the bodies of assay.first, and with a different body."""


def short(value: int) -> int:
    """A small body: fewer than 20 nodes do not count."""
    return value + 1


def location(path: str, line: int, kind: str) -> str:
    """Return a text of a path, a line and a kind."""
    return f"{path}:{line} ({kind})"


def place(name: str, number: int, word: str) -> str:
    """Return a different text: the same node types with different names do not agree."""
    return f"{name}:{number} ({word})"
