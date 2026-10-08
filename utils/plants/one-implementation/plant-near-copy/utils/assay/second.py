"""A planted module that almost copies the body of assay.first.totals: one statement more."""


def totals(rows: list[tuple[str, int]], limit: int) -> dict[str, int]:
    """Add the counts of each name, stop at a limit, and remove the empty names."""
    found: dict[str, int] = {}
    for name, count in rows:
        if count > 0:
            found[name] = found.get(name, 0) + count
        if len(found) >= limit:
            break
    for name in sorted(found):
        if found[name] > limit:
            found[name] = limit
    found.pop("", None)
    return found
