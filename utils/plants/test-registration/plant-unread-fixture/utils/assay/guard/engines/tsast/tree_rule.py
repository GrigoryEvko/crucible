"""A planted rule module that reads the parse trees."""

from assay.readers import tsast


def findings(context):
    """Give no finding."""
    return [] if context is not None and tsast is not None else []
