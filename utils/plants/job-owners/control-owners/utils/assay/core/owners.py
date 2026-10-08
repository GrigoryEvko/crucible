"""A planted module that does no job of an owner."""

from pathlib import Path


def rows(path: Path) -> list[str]:
    """Write a text to a file, and return the words of a line."""
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("one two\n")
    return "one two".split(" ")


def parent(path: Path) -> Path:
    """Return the directory of a path that is not __file__."""
    return path.parent.parent
