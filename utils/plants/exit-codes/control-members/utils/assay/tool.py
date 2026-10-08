"""A planted module: each exit takes a member of ExitCode."""

import sys

from assay.core import exit as exits
from assay.core.exit import ExitCode


def stop() -> None:
    """Exit with a member by its name."""
    sys.exit(ExitCode.USAGE)


def stop_value(status: int) -> None:
    """Exit with a member from its value."""
    raise SystemExit(ExitCode(status))


def stop_module() -> None:
    """Exit with a member through the module."""
    sys.exit(exits.ExitCode.OK)


class Door:
    """A planted class with a method that has the name exit."""

    def exit(self, code: int) -> int:
        """Return the code."""
        return code


def leave(door: Door) -> int:
    """Call the method exit of an object: the rule reads no such call."""
    return door.exit(5)
