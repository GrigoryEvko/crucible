"""A planted family module that holds the rule alpha-rule."""


def no_findings(context):
    """Give no finding."""
    return [] if context is not None else []


RULES = {"alpha-rule": no_findings}
