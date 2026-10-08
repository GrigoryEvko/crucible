"""A planted module with a call that gives a text for a number."""


def double(value: int) -> int:
    """Return two times the value."""
    return 2 * value


def text_twice() -> int:
    """Give a text where the function takes a number."""
    return double("two")
