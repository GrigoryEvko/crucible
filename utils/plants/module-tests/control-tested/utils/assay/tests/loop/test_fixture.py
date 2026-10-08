"""The planted test module of assay.loop.fixture."""

from assay.loop import fixture


def main(argv: list[str]) -> int:
    """Return 0 when the case holds."""
    return fixture.run()
