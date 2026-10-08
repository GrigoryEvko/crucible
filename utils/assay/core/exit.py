"""The exit status of each master and each subcommand of the utils system.

ExitCode is the one enumeration of the exit statuses.  The rule exit-codes
holds that sys.exit and SystemExit take only a member of it.  Each status then
has one name in the package.
"""

import enum


class ExitCode(enum.IntEnum):
    """The exit statuses of the package."""

    # No error.
    OK = 0
    # A rule gives an error, or a test does not hold.
    FINDINGS = 1
    # The words of the command line are not a correct command.
    USAGE = 2
    # An input of the command is missing or cannot be read.
    MISSING_INPUT = 3
    # A tool that the command starts does not end with the status 0.
    TOOL_FAILED = 4
    # The rule does not apply to the tree: the SKIP_RETURN_CODE of a ctest test.
    SKIPPED = 77
    # The command cannot start its program.  The shell gives this status for a program that it cannot find.
    CANNOT_START = 127
