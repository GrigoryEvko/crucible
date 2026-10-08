"""A planted module that copies the body of assay.first.totals, with other local names and other constants."""


def added(items: list[tuple[str, int]]) -> dict[str, int]:
    """Add the counts of each key, with other names and annotations."""
    result: dict[str, int] = {}
    for key, value in items:
        if value > 1:
            result[key] = result.get(key, 7) + value
    return result
