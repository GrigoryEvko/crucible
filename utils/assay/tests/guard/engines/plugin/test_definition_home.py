"""The test of the rule definition-home (assay.guard.engines.plugin.definition_home).

`guard selftest tests.guard.engines.plugin.test_definition_home` runs the
test.  Each case gives the module a planted census in a throwaway tree: the
finding lines of the plugin, the join of the records of the classes and of the
bodies, and the record of each header.  Each check of the join has a red case
and a green case.  Exit 0 when each case holds, else 1.
"""

import tempfile
from pathlib import Path

from assay.core import report as check_report
from assay.core import testing
from assay.guard.engines.plugin import definition_home, runner
from assay.readers import sections

RULE = definition_home.RULE
HEADER = "include/foundation/gates.h"
SOURCE = "src/foundation/gates.cpp"
OTHER_SOURCE = "src/fixy/more.cpp"
OTHER_HEADER = "include/fixy/other.h"


def line(path: str, number: int, kind: str, entity: str) -> sections.Finding:
    """Return one line of the rule in a planted census."""
    return sections.Finding(path, number, 1, RULE, kind, entity)


def judged(root: Path, found: list[sections.Finding], build_dir: Path | None = None) -> list[check_report.Finding]:
    """Return the findings of the module for a planted census of the lines FOUND."""
    return definition_home.findings(runner.PluginContext(root, build_dir, runner.Census(1, frozenset(found))))


def elsewhere(found: list[check_report.Finding]) -> list[check_report.Finding]:
    """Return the findings of the kind elsewhere."""
    return [item for item in found if item.kind == definition_home.ELSEWHERE]


def line_cases(expect: testing.Expect) -> None:
    """Check that a finding line of the plugin is an error at its directory, and that a record is none."""
    with tempfile.TemporaryDirectory(prefix="definition-home-") as work:
        root = Path(work)
        found = judged(
            root,
            [
                line(
                    HEADER,
                    7,
                    definition_home.FRIEND,
                    "foundation::gates::door with no definition in the unit",
                ),
                line(
                    HEADER,
                    9,
                    definition_home.HOMED,
                    "foundation::gates::Owner home owner line 3 include/x.h",
                ),
            ],
        )
        expect(
            "a friend line is an error at the directory of its file, and a record homed gives none",
            [(item.path, item.kind) for item in found] == [("include/foundation/", definition_home.FRIEND)],
        )


def declared_cases(expect: testing.Expect) -> None:
    """Check the join of a declared class: red for a header, for two files and for a template, green else."""
    with tempfile.TemporaryDirectory(prefix="definition-home-") as work:
        root = Path(work)
        declared = line(HEADER, 4, definition_home.DECLARED, "foundation::gates::Opaque")
        expect(
            "a declared class that no file defines is no finding",
            elsewhere(judged(root, [declared])) == [],
        )
        in_source = line(SOURCE, 12, definition_home.DEFINED, "foundation::gates::Opaque")
        expect(
            "a declared class with its one definition in a base source file is no finding",
            elsewhere(judged(root, [declared, in_source])) == [],
        )
        in_header = line(OTHER_HEADER, 20, definition_home.DEFINED, "foundation::gates::Opaque")
        found = elsewhere(judged(root, [declared, in_header]))
        expect(
            "a declared class that a header of a different file defines is the finding elsewhere at the declaration",
            [(item.path, item.line) for item in found] == [("include/foundation/", 4)]
            and OTHER_HEADER in found[0].message,
        )
        in_second = line(OTHER_SOURCE, 30, definition_home.DEFINED, "foundation::gates::Opaque")
        found = elsewhere(judged(root, [declared, in_source, in_second]))
        expect(
            "a declared class that two base source files define is a finding at the declaration and at the second",
            sorted((item.path, item.line) for item in found) == [("include/foundation/", 4), ("src/foundation/", 12)],
        )
        template = line(HEADER, 5, definition_home.DECLARED, "foundation::gates::Shape template")
        template_body = line(SOURCE, 40, definition_home.DEFINED, "foundation::gates::Shape")
        expect(
            "a declared class template with a definition in a base source file is a finding",
            [item.line for item in elsewhere(judged(root, [template, template_body]))] == [5],
        )


def body_cases(expect: testing.Expect) -> None:
    """Check the join of the bodies: red for a symbol that two base source files define, green for one."""
    with tempfile.TemporaryDirectory(prefix="definition-home-") as work:
        root = Path(work)
        first = line(
            SOURCE,
            8,
            definition_home.BODY,
            "_ZN10foundation5gates4pokeEv foundation::gates::poke",
        )
        expect(
            "a symbol that one base source file defines is no finding",
            elsewhere(judged(root, [first])) == [],
        )
        second = line(
            OTHER_SOURCE,
            3,
            definition_home.BODY,
            "_ZN10foundation5gates4pokeEv foundation::gates::poke",
        )
        found = elsewhere(judged(root, [first, second]))
        expect(
            "a symbol that two base source files define is the finding elsewhere at the later definition",
            [(item.path, item.line) for item in found] == [("src/foundation/", 8)]
            and "src/fixy/more.cpp" in found[0].message,
        )


def header_cases(expect: testing.Expect) -> None:
    """Check the record of each header: red for a header with no record in a build, green with each record."""
    with tempfile.TemporaryDirectory(prefix="definition-home-") as work:
        root = Path(work)
        for path in (HEADER, OTHER_HEADER):
            (root / path).parent.mkdir(parents=True, exist_ok=True)
            (root / path).write_text("#pragma once\n", encoding="utf-8")
        build = root / "build"
        one = [line(HEADER, 1, definition_home.HEADER, HEADER)]
        problems = [item for item in judged(root, one, build) if not item.kind]
        expect(
            "a header of the two layers with no record header is a problem of the build",
            [item.path for item in problems] == [OTHER_HEADER],
        )
        each = [*one, line(OTHER_HEADER, 1, definition_home.HEADER, OTHER_HEADER)]
        expect(
            "each header with its record gives no problem",
            [item for item in judged(root, each, build) if not item.kind] == [],
        )
        expect(
            "a planted census with no build directory reads no header",
            [item for item in judged(root, one) if not item.kind] == [],
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (line_cases, declared_cases, body_cases, header_cases),
    )
