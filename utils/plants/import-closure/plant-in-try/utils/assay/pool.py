"""A planted module: imports of heavy modules in a try and in an if at module level."""

import sys

try:
    import logging
except ImportError:
    logging = None

if sys.platform == "linux":
    from concurrent.futures import ThreadPoolExecutor
else:
    import multiprocessing.pool


def workers() -> int:
    """Return a number of workers."""
    return 4
