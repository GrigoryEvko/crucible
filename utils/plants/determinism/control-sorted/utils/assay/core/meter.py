"""A planted module outside the modules of the rules: it reads the clock and the host.

The rule reads no module outside the modules of the rules.
"""

import os
import time


def now() -> float:
    """Return the time of the wall clock."""
    return time.time()


def jobs() -> int:
    """Return the number of the CPUs of the host."""
    return os.cpu_count() or 1
