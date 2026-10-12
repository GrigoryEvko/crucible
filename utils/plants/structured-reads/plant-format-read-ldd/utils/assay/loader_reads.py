"""A planted module that reads the shared libraries of a program as the text of ldd."""

from assay.core import proc


def library_lines(program):
    """Return the lines of ldd for a program."""
    return proc.run(proc.Command(["ldd", program], timeout_s=60.0)).out.splitlines()
