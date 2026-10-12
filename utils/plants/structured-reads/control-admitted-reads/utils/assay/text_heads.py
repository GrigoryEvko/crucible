"""A planted module with texts that start with the head of a diagnostic, and a function compile that re does not give."""


def compile(rows):
    """Return the rows of a planted report in one text."""
    return "".join(rows)


def counted(report, rows):
    """Count the lines of a report that the module writes, which start with a head and hold more text after it."""
    kinds = {"error:": 0, "warning:": 0}
    joined = compile(rows)
    return report.count("error: [test-time] the test took") + len(kinds) + len(joined) + ("--target" in rows)
