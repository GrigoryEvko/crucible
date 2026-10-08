"""A planted module with a body that a different module almost copies."""


def totals(rows: list[tuple[str, int]], limit: int) -> dict[str, int]:
    """Add the counts of each name, and stop at a limit."""
    found: dict[str, int] = {}
    for name, count in rows:
        if count > 0:
            found[name] = found.get(name, 0) + count
        if len(found) >= limit:
            break
    for name in sorted(found):
        if found[name] > limit:
            found[name] = limit
    return found
