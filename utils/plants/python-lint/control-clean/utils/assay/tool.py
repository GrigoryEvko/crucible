"""A planted module that the pinned ruff accepts, in the format of ruff format."""

from collections.abc import Callable


def double(value: int) -> int:
    """Return two times the value."""
    return 2 * value


def adders() -> list[Callable[[int], int]]:
    """Return one adder for each number, with the number as the default of a parameter."""
    return [lambda value, number=number: value + number for number in range(3)]
