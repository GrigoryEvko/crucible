"""A planted module that reads C++ files as text, and searches the text for a namespace and an include."""

from pathlib import Path

EDIT_HEADER = Path("include/foundation/Platform.h")


def opens_namespace(root: Path) -> bool:
    """Return whether the header opens a namespace, from its bytes."""
    data = (root / EDIT_HEADER).read_bytes()
    return b"namespace foundation" in data


def includes(root: Path) -> list[str]:
    """Return each include line of each source file of a directory."""
    found: list[str] = []
    for path in sorted(root.glob("src/*.cpp")):
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        found += [line for line in text.splitlines() if line.startswith("#include")]
    return found
