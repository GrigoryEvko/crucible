"""A planted reader with a function that lists a directory and gives its cost."""

from pathlib import Path


def names(directory: Path) -> list[str]:
    """Return the name of each entry of a directory, sorted.

    Complexity: O(n log n) for n entries.
    """
    return sorted(path.name for path in directory.iterdir())
