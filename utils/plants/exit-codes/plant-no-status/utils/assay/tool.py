"""A planted module: an exit with no status, and an exit through an alias of the module os."""

import os as system
import sys


def stop() -> None:
    """Exit with no status."""
    sys.exit()


def stop_now() -> None:
    """Exit at once with the status 3."""
    system._exit(3)
