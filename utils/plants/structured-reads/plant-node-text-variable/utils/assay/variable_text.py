"""A planted module that keeps the text of a tsast node in a variable, and then splits the variable."""

from assay.readers import tsast


def parts_of(node):
    """Return the parts of a qualified name."""
    spelled_name = node.text
    return tsast is not None and spelled_name.split("::")
