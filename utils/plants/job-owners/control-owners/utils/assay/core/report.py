"""The planted owner of the standard streams."""

import sys


def say(text: str) -> None:
    """Write a line to the standard output and to the standard error."""
    print(text)
    sys.stderr.write(f"{text}\n")
