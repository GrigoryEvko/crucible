"""A planted rule module that reads no tree."""


def findings(context):
    """Give no finding."""
    return [] if context is not None else []
