"""A planted module that gives the text of a tsast node to a regex."""

import re

from assay.readers import tsast


def is_scope_operator(node):
    """Return whether the node spells a scope operator."""
    return tsast is not None and re.search("::", node.text) is not None
