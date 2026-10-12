"""A planted module that reads a C++ file through the parse tree, and compares a whole file to an expected text."""

from pathlib import Path

from assay.readers import tsast

EDIT_HEADER = Path("include/foundation/Platform.h")


def namespaces(root: Path) -> list[str]:
    """Return the name of each namespace that the header defines, from the parse tree."""
    tree = next(tsast.parse([root / EDIT_HEADER]))
    return [tsast.lexeme(name) for node in tree.find("namespace_definition")
            if (name := node.child_by_field("name")) is not None]


def is_written(root: Path, expected: str) -> bool:
    """Return whether a generated header holds the expected text."""
    return (root / "build" / "walk_headers.h").read_text(encoding="utf-8") == expected
