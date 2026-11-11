"""A planted rule module: reads of the wall clock, of the date and of a time measure."""

import datetime
import time


def stamp() -> float:
    """Return the time of the wall clock."""
    return time.time()


def day() -> str:
    """Return the date of today."""
    return datetime.date.today().isoformat()


def text() -> str:
    """Return the year of the wall clock."""
    return time.strftime("%Y")


def elapsed(start: float) -> float:
    """Return the time since a start on the monotonic clock."""
    return time.monotonic() - start
