"""A planted module with a body that a different module copies."""


def totals(rows: list[tuple[str, int]]) -> dict[str, int]:
    """Add the counts of each name."""
    found: dict[str, int] = {}
    for name, count in rows:
        if count > 0:
            found[name] = found.get(name, 0) + count
    return found
