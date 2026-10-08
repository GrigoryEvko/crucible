"""A planted module that writes to the standard streams, changes the module path and finds the root itself."""

import os
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OLD_ROOT = os.path.dirname(os.path.dirname(__file__))


def report(text: str) -> None:
    """Write a line to the standard output and to the standard error."""
    print(text)
    sys.stderr.write(text)


def extend(directory: str) -> None:
    """Add a directory to the module path."""
    sys.path.insert(0, directory)
