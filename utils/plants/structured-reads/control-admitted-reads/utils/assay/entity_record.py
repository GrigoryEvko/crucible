"""A planted module that reads the parts of an entity from its record, and gives the entity only to a key or a compare."""

from assay.readers import sections


def header_of(finding: sections.Finding) -> str:
    """Return the header of a finding of the kind layer-header, from the record of its entity."""
    entity = sections.quarantine_entity(finding.kind, finding.entity)
    return entity.header if isinstance(entity, sections.LayerHeader) else entity.text


def is_vector(finding: sections.Finding) -> bool:
    """Tell whether a finding names std::vector, with a compare of its whole entity."""
    return finding.entity == "std::vector"


def key_of(finding: sections.Finding) -> tuple[str, str, str]:
    """Return the file, the kind and the entity of a finding, as a key of a count."""
    return (finding.file, finding.kind, finding.entity)
