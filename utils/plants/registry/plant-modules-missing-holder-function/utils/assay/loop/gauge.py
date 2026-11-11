"""A planted module of the holder.  It has a different function, and a method with the name of the holder."""


def other_record(record):
    """Give the record."""
    return record


class Gauge:
    """A class whose method has the name of the holder.  A method is not a function at the top level."""

    def judged_record(self, record):
        """Give the record."""
        return record
