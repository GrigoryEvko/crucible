"""A planted module that reads the parts of an entity of the quarantine plugin from its text, and not from its record."""

from assay.readers import sections


def header_of(finding: sections.Finding) -> str:
    """Return the header of an include finding, from the first word of its entity."""
    return finding.entity.split(" ")[0]


def names_std(finding: sections.Finding) -> bool:
    """Tell whether a finding names an entity of the scope std, from a test over the text of its entity."""
    return "std::" in finding.entity
