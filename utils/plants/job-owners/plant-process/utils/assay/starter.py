"""A planted module that starts processes, and is not the owner of the process starts."""

import os
import subprocess as sp


def run(words: list[str]) -> int:
    """Start a command through an alias of subprocess."""
    return sp.run(words, check=False).returncode


def replace(words: list[str]) -> None:
    """Replace this process with a command."""
    os.execvp(words[0], words)


def spawn(words: list[str]) -> int:
    """Start a command with posix_spawnp."""
    return os.posix_spawnp(words[0], words, dict(os.environ))
