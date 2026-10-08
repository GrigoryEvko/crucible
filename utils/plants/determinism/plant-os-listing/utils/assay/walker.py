"""A planted module: listings of the modules os and glob that no sort holds."""

import glob as patterns
import os


def first(directory: str) -> str:
    """Return the first entry that os.listdir gives."""
    return os.listdir(directory)[0]


def count(directory: str) -> int:
    """Return the number of the Python files of a directory."""
    return len(patterns.glob(f"{directory}/*.py"))


def walked(directory: str) -> list[str]:
    """Return the directories of a walk."""
    return [root for root, _, _ in os.walk(directory)]
