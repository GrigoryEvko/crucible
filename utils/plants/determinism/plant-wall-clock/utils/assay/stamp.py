"""A planted module: reads of the wall clock in a module that is not an owner of the clock."""

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
