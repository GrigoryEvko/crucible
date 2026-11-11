"""A planted rule module: reads of the git history, through a function of assay.core.git and through git.run()."""

from pathlib import Path

from assay.core import git
from assay.core.git import change_base


def commit(root: Path) -> str:
    """Return the commit of HEAD."""
    return git.head(root)


def base(root: Path) -> str | None:
    """Return the commit that the change starts from."""
    return change_base(root)


def subject(root: Path) -> bytes:
    """Return the log line of the last commit."""
    return git.run(root, "log", "-1", "--format=%s")
