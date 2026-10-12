"""A planted module that runs a git command for its effect, and uses no value of the run."""

from pathlib import Path

from assay.core import git


def add_each_file(root: Path) -> None:
    """Put each file of a tree in its index."""
    git.run(root, "add", "-A", "--", ".")
