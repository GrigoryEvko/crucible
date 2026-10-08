"""A planted module that reads an input with a regex outside a reader."""

import re


def version_of(text):
    """Return the version that a line of a log gives."""
    return re.search(r"version (\d+)", text)
