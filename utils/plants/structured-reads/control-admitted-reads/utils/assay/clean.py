"""A planted module with each read of a tsast node that the rule admits."""

import re

from assay.readers import tsast


def marked(node, comment, tree):
    """Return a leaf compare, a lexeme, an excerpt, a row of a report and a regex over prose."""
    is_leaf = node.text == "x" and tsast.lexeme(node).endswith("_t")
    shown = tsast.excerpt(node).strip() + tree.line(3).strip()
    return re.search("MARK", tsast.prose_text(comment)) and is_leaf and shown
