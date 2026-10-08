"""A planted module: each heavy import is in a function or in the block of TYPE_CHECKING."""

from typing import TYPE_CHECKING

if TYPE_CHECKING:
    import argparse


def parser() -> "argparse.ArgumentParser":
    """Return a parser.  The import is in the function that uses it."""
    from assay.core import cli

    return cli.parser()


def run() -> int:
    """Start a pool.  The import is in the function that uses it."""
    from concurrent.futures import ThreadPoolExecutor

    with ThreadPoolExecutor(1) as pool:
        return pool.submit(int).result()
