"""A planted module: a loop over the entries of a directory in the sequence of the file system."""

from pathlib import Path


def names(directory: Path) -> list[str]:
    """Return the name of each entry of a directory."""
    return [path.name for path in directory.iterdir()]
