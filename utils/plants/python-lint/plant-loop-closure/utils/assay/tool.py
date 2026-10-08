"""A planted module with a closure that reads the variable of a loop."""

from collections.abc import Callable


def adders() -> list[Callable[[int], int]]:
    """Return one adder for each number, which all add the last number."""
    found = []
    for number in range(3):
        found.append(lambda value: value + number)
    return found
