"""A planted module with a long sentence in its docstring.

The function reads the value and compares it with zero, and then it returns
True when the value is more than zero and False when the value is zero or
less than zero.
"""


def check(value: int) -> bool:
    """Return True when the value is more than zero."""
    return value > 0
