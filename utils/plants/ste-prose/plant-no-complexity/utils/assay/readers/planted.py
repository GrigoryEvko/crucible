"""A planted reader with a function that lists a directory and gives no cost."""

from pathlib import Path


def names(directory: Path) -> list[str]:
    """Return the name of each entry of a directory, sorted."""
    return sorted(path.name for path in directory.iterdir())
