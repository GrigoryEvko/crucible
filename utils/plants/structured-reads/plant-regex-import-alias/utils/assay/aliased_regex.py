"""A planted module that calls a function of the module re through an alias of the module."""

import re as pattern_module


def words_of(text):
    """Return the words of a line of a log."""
    return pattern_module.findall(r"\w+", text)
