"""A planted module with bodies that are not copies."""


def largest(rows: list[tuple[str, int]]) -> str:
    """Return the name with the largest count, or an empty text."""
    best = ""
    top = -1
    for name, count in sorted(rows):
        if count > top:
            best, top = name, count
    return best


def short(value: int) -> int:
    """A small body: fewer than 20 nodes do not count."""
    return value + 1


def outer(rows: list[tuple[str, int]]) -> dict[str, int]:
    """Hold a function with almost the same body: a function and a function that it holds are not a pair."""

    def inner(rows: list[tuple[str, int]]) -> dict[str, int]:
        """Add the counts of each name."""
        found: dict[str, int] = {}
        for name, count in rows:
            if count > 0:
                found[name] = found.get(name, 0) + count
        for name in sorted(found):
            if found[name] > 100:
                found[name] = 100
            elif found[name] < 2:
                found[name] = 2
        return found

    return inner(rows)
