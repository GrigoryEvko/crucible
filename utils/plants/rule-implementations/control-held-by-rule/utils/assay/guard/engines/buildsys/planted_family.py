"""A planted family module that holds the rule alpha-rule, and the thresholds of the rule alpha-size."""


def alpha_values(context):
    """Give the planted values."""
    return [] if context is not None else []


def no_findings(context):
    """Give no finding."""
    return alpha_values(context)


RULES = {"alpha-rule": no_findings}
