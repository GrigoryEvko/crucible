"""A planted owner of the job argparse: its import at module level is permitted."""

import argparse


def parser() -> argparse.ArgumentParser:
    """Return a parser."""
    return argparse.ArgumentParser()
