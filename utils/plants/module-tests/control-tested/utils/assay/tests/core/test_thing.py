"""The planted test module of assay.core.thing."""

from assay.core import thing


def main(argv: list[str]) -> int:
    """Return 0 when the case holds."""
    return 0 if thing.double(2) == 4 else 1
