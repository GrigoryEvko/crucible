"""A planted module that makes a text from a variable that holds the text of a tsast node, and then splits it."""


def parts_of(node):
    """Return the parts of a qualified name with a scope operator at its end."""
    spelled_name = node.text
    scoped = spelled_name + "::"
    return scoped.split("::")
