"""A planted module that splits the text of a tsast node, and tests a word in it."""

from assay.readers import tsast


def parts_of(node):
    """Return the parts of a qualified name, and whether it names decltype."""
    return tsast is not None and node.text.split("::"), "decltype" in node.text
