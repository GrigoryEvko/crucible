"""A planted module: frozen records with slots, and dictionaries of strings only where the rule permits them."""

import enum
from dataclasses import dataclass


class Colour(enum.Enum):
    """A closed set of words."""

    RED = "red"
    BLUE = "blue"


@dataclass(frozen=True, slots=True)
class Row:
    """A frozen record with slots and typed fields."""

    name: str
    counts: dict[str, int]
    colour: Colour


def _private(row: dict[str, str]) -> None:
    """A private function can take a dictionary of strings."""


def public(names: list[str]) -> int:
    """A public function with a nested function that takes a dictionary of strings."""

    def inner(row: dict[str, str]) -> int:
        """Count the keys."""
        return len(row)

    return inner(dict.fromkeys(names, ""))


class _Hidden:
    """A private class: its methods are not public."""

    def method(self, row: dict[str, str]) -> None:
        """Take a dictionary of strings."""


class Plain:
    """A class that is not a dataclass."""

    def __init__(self) -> None:
        """Make the object."""
        self.cache: dict[str, str] = {}
