"""A planted module: each listing is sorted, and no read of the wall clock."""

import glob
import os
import time
from pathlib import Path

from assay.core import paths


def names(directory: Path) -> list[str]:
    """Return the name of each entry of a directory, sorted."""
    return [path.name for path in sorted(directory.iterdir())]


def directories(directory: Path) -> list[Path]:
    """Return each subdirectory, sorted, through a comprehension in the argument of sorted()."""
    return sorted(path for path in directory.glob("*") if path.is_dir())


def modules(directory: Path) -> tuple[Path, ...]:
    """Return the Python files of a directory through the sorted listing of the package."""
    return paths.listing(directory, "*.py")


def files(directory: str) -> list[str]:
    """Return the entries of os.listdir and of glob.glob, sorted."""
    return sorted(os.listdir(directory)) + sorted(glob.glob(f"{directory}/*"))


def elapsed(start: float) -> float:
    """Return the time since a start on the monotonic clock, which is not the wall clock."""
    return time.monotonic() - start


def year_of(seconds: float) -> str:
    """Return the year of a time that the caller gives."""
    return time.strftime("%Y", time.gmtime(seconds))
