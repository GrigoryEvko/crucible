"""A planted module that mypy in strict mode accepts."""

from dataclasses import dataclass


@dataclass(frozen=True, slots=True)
class Row:
    """A planted record."""

    name: str
    count: int


def double(value: int) -> int:
    """Return two times the value."""
    return 2 * value


def counts(rows: list[Row]) -> dict[str, int]:
    """Return the count of each name, two times."""
    return {row.name: double(row.count) for row in rows}
