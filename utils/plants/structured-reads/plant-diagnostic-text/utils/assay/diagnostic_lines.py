"""A planted module that divides the lines of the text output of GCC at the head of an error."""


def first_message(output):
    """Return the message of the first line that holds the head of an error."""
    for line in output.splitlines():
        head, separator, message = line.partition("error: ")
        if separator:
            return message
    return ""
