"""loop meter-compiler — count the user instructions of the compiler run of one ccache call.

`loop step` runs ccache with CCACHE_PREFIX set to the interpreter of the
launcher, the master loop and this subcommand.  For the compiler run of a
miss, ccache then runs

    python3 utils/bin/loop meter-compiler COMPILER ARGS...

and it runs no prefix in front of the preprocessor run.  The subcommand runs
the command, adds the user instructions of the command to the file that the
variable CRUCIBLE_COMPILER_COUNT names, and exits as the command exited.
utils/assay/core/meter.py (A COMPILE THROUGH CCACHE) gives the rules and
holds the code.
"""

from assay.core import meter as cost_meter


def main(argv: list[str]) -> int:
    """Run the compiler command of the command line, and end as it ended.

    Args:
        argv: The compiler command that ccache gives the prefix

    Returns:
        Nothing: meter_compiler() ends the process
    """
    cost_meter.meter_compiler(argv)
    return 2
