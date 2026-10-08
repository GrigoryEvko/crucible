"""A planted module that names the compile database, the Ninja files and the test list outside their readers."""

from pathlib import Path


def database_of(build_dir):
    """Return the path of the compile database of a build directory."""
    return Path(build_dir) / "compile_commands.json"


def ninja_files_of(build_dir):
    """Return the paths of the build graph and the build log of a build directory."""
    return Path(build_dir) / "build.ninja", Path(build_dir) / ".ninja_log"


def listing_command(ctest, build_dir):
    """Return the command that prints the test list of a build directory."""
    return [ctest, "--show-only=json-v1", "--test-dir", str(build_dir)]
