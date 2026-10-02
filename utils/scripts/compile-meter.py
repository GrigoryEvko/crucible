#!/usr/bin/env python3
"""compile-meter — count the user instructions of the compiler run of one ccache call.

utils/scripts/build-launcher.py runs ccache with CCACHE_PREFIX set to the
interpreter of the launcher and this script.  For the compiler run of a miss,
ccache then runs

    python3 compile-meter.py COMPILER ARGS...

and it runs no prefix in front of the preprocessor run.  The script runs the
command, adds the user instructions of the command to the file that the
variable CRUCIBLE_COMPILER_COUNT names, and exits as the command exited.
utils/scripts/cost_meter.py (A COMPILE THROUGH CCACHE) gives the rules.  The
code is in that module, because Python keeps the compiled form of an imported
module and compiles the script that it runs on each start.
"""

import sys

import cost_meter

if __name__ == "__main__":
    cost_meter.meter_compiler(sys.argv[1:])
