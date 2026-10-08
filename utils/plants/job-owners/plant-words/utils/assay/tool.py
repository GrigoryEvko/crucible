"""A planted module that names the words of the jobs of other owners."""

import argparse

STATUS = ("git", "status")
CACHE = "XDG_CACHE_HOME"
RECORD = "build/obj.o.cost"
LOG = "LastTestsFailed.log"


def cells(line: str) -> list[str]:
    """Divide a row at its bars."""
    return line.split(" | ")


def parser() -> argparse.ArgumentParser:
    """Return a parser."""
    return argparse.ArgumentParser()
