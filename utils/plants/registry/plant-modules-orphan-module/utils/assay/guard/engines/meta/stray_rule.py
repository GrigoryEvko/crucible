"""A planted rule module with no rule."""


def findings(context):
    """Give no finding."""
    return [] if context is not None else []
