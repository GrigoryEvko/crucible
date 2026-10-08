"""A planted module that names the section of the quarantine plugin outside its reader."""

SECTION = ".crucible.quarantine"


def section_of(names):
    """Return the index of the section of the plugin in a list of section names, or -1."""
    return names.index(SECTION) if SECTION in names else -1
