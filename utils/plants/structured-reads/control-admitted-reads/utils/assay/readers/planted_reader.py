"""A planted reader: the reader of a format can use a regex, name its file, run its tool and test its magic."""

import re

CACHE = "CMakeCache.txt"
SECTION = ".crucible.quarantine"
DEMANGLER = ("c++filt", "--no-verbose")
DISASSEMBLER = ("objdump", "-d", "-r")
ELF_MAGIC = b"\x7fELF"
ARCHIVE_MAGIC = b"!<arch>\n"


def entries(text):
    """Return each entry of a cache."""
    return re.findall(r"^(\w+):\w+=(.*)$", text, re.MULTILINE)
