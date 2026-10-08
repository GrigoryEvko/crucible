"""The parser of the arguments of a subcommand that a person starts.

The import of argparse costs 6.3 ms, so this module imports it only in the
first call of parser().  A step meter that CMake or ctest starts reads its
arguments without argparse.
"""

TYPE_CHECKING = False
if TYPE_CHECKING:
    import argparse


def parser(prog: str, summary: str) -> argparse.ArgumentParser:
    """Return a parser of the arguments of one subcommand.

    Args:
        prog: The command as a person types it, such as "guard check"
        summary: One line that tells what the subcommand does
    """
    import argparse

    return argparse.ArgumentParser(prog=prog, description=summary)
