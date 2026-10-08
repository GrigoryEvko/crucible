"""A planted reader: the reader of a format can use a regex and name its file."""

import re

CACHE = "CMakeCache.txt"


def entries(text):
    """Return each entry of a cache."""
    return re.findall(r"^(\w+):\w+=(.*)$", text, re.MULTILINE)
