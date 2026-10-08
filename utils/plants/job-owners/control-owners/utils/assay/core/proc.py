"""The planted owner of the process starts."""

import subprocess


def run(words: list[str]) -> int:
    """Start a command."""
    return subprocess.run(words, check=False, timeout=10).returncode
