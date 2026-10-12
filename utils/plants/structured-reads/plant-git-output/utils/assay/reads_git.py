"""A planted module that splits the output of a git run, and reads the exit of a second run through an alias."""

from pathlib import Path

from assay.core import git
from assay.core import git as tree_git


def entry_mode(root: Path, path: str) -> str:
    """Return the mode of a path in the tree of HEAD."""
    return git.run(root, "ls-tree", "HEAD", "--", path).decode().split()[0]


def is_branch(root: Path) -> bool:
    """Return whether HEAD is on a branch."""
    return tree_git.run_any_exit(root, "symbolic-ref", "-q", "HEAD").exit == 0
