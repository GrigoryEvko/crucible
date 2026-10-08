"""A planted module that names the cache of a build directory outside its reader."""

from pathlib import Path


def cache_of(build_dir):
    """Return the path of the cache of a build directory."""
    return Path(build_dir) / "CMakeCache.txt"
