"""A planted module that gives the text of a tsast node to calls whose results are no text, and tests the results."""


def is_new_user(node, users, cache, candidate):
    """Return whether a name has no user yet, and whether the source of a candidate spells a word."""
    named = users.setdefault(node.text, [])
    spelled = cache.get(candidate.source, frozenset())
    return "x" not in named and "y" in spelled
