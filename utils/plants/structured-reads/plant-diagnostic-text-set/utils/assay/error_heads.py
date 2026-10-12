"""A planted module that compares a word of the text output of GCC with the heads of an error."""


def is_error_word(word):
    """Return whether a word of a line is the head of an error or of a fatal error."""
    return word in ("error:", "fatal error:")
