"""A planted module: dictionaries of strings in a public signature and in a field."""

from dataclasses import dataclass
from typing import Any


def fields(row: dict[str, str]) -> None:
    """Take a dictionary of strings."""


class Holder:
    """A class with a public method that returns a dictionary of strings in a text annotation."""

    def values(self) -> "dict[str, Any]":
        """Return an empty dictionary."""
        return {}


@dataclass(frozen=True, slots=True)
class Row:
    """A record with a field that is a dictionary of strings to objects."""

    extra: dict[str, object]
