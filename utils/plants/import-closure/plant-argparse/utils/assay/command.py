"""A planted module: an import of argparse at module level in a module that is not its owner."""

import argparse


def parser() -> argparse.ArgumentParser:
    """Return a parser."""
    return argparse.ArgumentParser()
