"""cmake — the CMake cache of a configured build directory.

THE INPUT
    CMake writes CMakeCache.txt into each build directory that it configures.
    A directory with that file is a build directory that CMake configured.

WHAT THE READER GIVES
    cache_file() gives the path of the cache of a build directory, and
    is_configured() tells whether the directory holds a cache.  A module that
    asks whether a directory is a build directory calls is_configured(), and
    it does not spell the name of the file.

Complexity: one stat for each call.
"""

from pathlib import Path

CACHE_NAME = "CMakeCache.txt"


def cache_file(build_dir: Path) -> Path:
    """Return the path of the CMake cache of a build directory.

    Args:
        build_dir: The build directory

    Returns:
        The path of its cache, which can be absent
    """
    return build_dir / CACHE_NAME


def is_configured(build_dir: Path) -> bool:
    """Whether a directory holds the cache that CMake writes when it configures a build directory.

    Args:
        build_dir: The directory

    Returns:
        True when the directory holds a CMake cache
    """
    return cache_file(build_dir).is_file()
