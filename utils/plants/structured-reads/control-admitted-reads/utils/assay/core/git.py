"""A planted git module: the one module that can hold the words of a git command."""

from pathlib import Path

from assay.core import proc


def head(root: Path) -> str:
    """Return the HEAD commit of a tree."""
    return proc.run(proc.Command(["git", "-C", str(root), "rev-parse", "HEAD"], timeout_s=60.0)).out.strip()
