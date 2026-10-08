"""A planted module that tells an ELF file and an archive apart by their magic, outside the ELF reader."""


def is_elf(path):
    """Return True when a file starts with the ELF magic."""
    with open(path, "rb") as handle:
        return handle.read(4) == b"\x7fELF"


def is_archive(path):
    """Return True when a file starts with the magic of an ar archive."""
    with open(path, "rb") as handle:
        return handle.read(8) == b"!<arch>\n"
