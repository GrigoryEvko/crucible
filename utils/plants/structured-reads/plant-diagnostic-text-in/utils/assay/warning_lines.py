"""A planted module that counts the warnings of a compile in the text output of GCC."""


def warning_count(output):
    """Return the number of lines that hold the head of a warning."""
    return sum(1 for line in output.splitlines() if "warning:" in line)
