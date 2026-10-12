"""A planted module that calls a function of the module re through a name that `from re import` makes."""

from re import compile as compile_pattern

VERSION = compile_pattern(r"version (\d+)")


def version_of(text):
    """Return the version that a line of a log gives."""
    return VERSION.search(text)
