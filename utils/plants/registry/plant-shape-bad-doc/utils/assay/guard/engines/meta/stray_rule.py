"""A planted rule module."""


def findings(context):
    """Give no finding."""
    return [] if context is not None else []
