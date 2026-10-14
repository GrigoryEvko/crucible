"""A planted module that reads no record of the quarantine plugin, and splits an entity that a check of its own writes."""

from assay.core import report as check_report


def first_word(finding: check_report.Finding) -> str:
    """Return the first word of the entity of a finding of a check."""
    return finding.entity.split(" ")[0]
