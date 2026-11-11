"""A planted family module."""


def no_findings(context):
    """Give no finding."""
    return [] if context is not None else []


RULES = {"alpha-rule": no_findings, "gamma-rule": no_findings}
