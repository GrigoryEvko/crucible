"""The test of assay.oracle.probe: the show values and the field `rejection` that a probe reads from its SARIF log.

Each case compiles a planted unit with no include.  The test then takes less than one second.

`guard selftest tests.oracle.test_probe` runs the test alone, and `guard selftest oracle` runs it in its group.
"""

import tempfile
from pathlib import Path

from assay.core import testing
from assay.oracle import probe

# Two show values.  GCC spells `unsigned long` as `long unsigned int`, and canonical() removes the white space.
SHOWS = probe.show("a", "int") + probe.show("b", "unsigned long")
SHOWN = {"a": "int", "b": "longunsignedint"}


def probe_cases(expect: testing.Expect) -> None:
    """Measure planted units: show values, a bracketed tag, an error with its option, a plain error and a nested tag."""
    compiler = testing.held_compiler(expect)
    if compiler is None:
        return
    with tempfile.TemporaryDirectory(prefix="probe-test-") as scratch:
        empty = Path(scratch)

        def measured(source: str) -> probe.Measured:
            """Return the measure of one planted unit, with an empty directory for the heads and the includes."""
            return probe.run_probe(compiler, empty, source, empty)

        expect(
            "each show value gives its spelling with no white space, and a show value gives no `rejection`",
            measured(SHOWS) == probe.Measured(SHOWN, None),
        )
        expect(
            "a failed static assertion gives the bracketed tag of its message",
            measured('static_assert(false, "[PlantedTag] a planted failure");\n' + SHOWS)
            == probe.Measured(SHOWN, "PlantedTag"),
        )
        expect(
            "an error with an option gives its message and its option in brackets, as the text output does",
            measured('int planted_value = "text";\n' + SHOWS)
            == probe.Measured(SHOWN, "invalid conversion from 'const char*' to 'int' [-fpermissive]"),
        )
        expect(
            "an error with no tag and no option gives its message",
            measured("undeclared_planted_name planted_object;\n" + SHOWS)
            == probe.Measured(SHOWN, "'undeclared_planted_name' does not name a type"),
        )
        expect(
            "a failed static assertion in an instantiation gives the tag of its message",
            measured(
                'template <class T> struct planted_box { static_assert(sizeof(T) == 0, "[DeepTag] deep"); };\n'
                "planted_box<int> planted_instance;\n"
            )
            == probe.Measured({}, "DeepTag"),
        )


def main(argv: list[str]) -> int:
    """Run the cases."""
    return testing.run(__name__.removeprefix("assay."), argv, (probe_cases,))
