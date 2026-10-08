"""A planted record that a caller can change after its construction."""

from dataclasses import dataclass


@dataclass(slots=True)
class Record:
    """A record with slots and no frozen=True."""

    name: str
