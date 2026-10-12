"""The test of the writer of the two header lists of CMake (assay.loop.derive.header_lists).

`guard selftest tests.loop.derive.test_header_lists` runs the test.  Each
case plants its input in a throwaway repository, and writes one line.  Exit 0
when each case holds, else 1.
"""

from assay.core import git, testing
from assay.core.exit import ExitCode
from assay.guard.engines.tsast import header_lists as rule
from assay.loop.derive import header_lists as writer

GATE = (
    b"#pragma once\n#define PLANTED_OPEN_GATE(name) namespace foundation::gates { struct name {}; }\n"
    b"#define PLANTED_OPEN_TWICE(name) PLANTED_OPEN_GATE(name)\n"
)
TREE = {
    "include/foundation/Gate.h": GATE,
    "include/crucible/UsesMacro.h": b"#pragma once\n#include <foundation/Gate.h>\nPLANTED_OPEN_TWICE(planted_gate)\n",
    "include/crucible/Opens.h": b"#pragma once\nnamespace fixy::planted { inline constexpr int value = 0; }\n",
    "include/crucible/Nested.h": b"#pragma once\nnamespace crucible::foundation { inline constexpr int other = 0; }\n",
    "include/crucible/Tag.h": b"#pragma once\nstruct PlantedTag { using parent_type = int; };\n",
}


def write_cases(expect: testing.Expect) -> None:
    """Check --write: the headers of each list, the text of each fragment, a second write and a parse error."""
    with git.scratch_repository(TREE) as root:
        status, _text = testing.captured(lambda: writer.written(root))
        walk, roster = rule.fragments(
            rule.HeaderLists(("crucible/Opens.h", "crucible/UsesMacro.h"), ("crucible/Tag.h",))
        )
        expect(
            "--write gives the walk list each crucible header that opens a layer namespace, also through two macros",
            status == ExitCode.OK and (root / walk.path).read_text(encoding="utf-8") == walk.text,
        )
        expect(
            "--write gives the roster list each header that declares an alias of a permission row",
            (root / roster.path).read_text(encoding="utf-8") == roster.text,
        )
        before = (root / walk.path).stat().st_mtime_ns
        testing.captured(lambda: writer.written(root))
        expect("a second --write keeps a fragment with the same bytes", (root / walk.path).stat().st_mtime_ns == before)
    with git.scratch_repository({**TREE, "include/crucible/Broken.h": b"struct Broken { int x = ; };\n"}) as root:
        status, _text = testing.captured(lambda: writer.written(root))
        expect(
            "--write writes nothing while a header does not parse",
            status == ExitCode.FINDINGS and not (root / rule.WALK_HEADERS).exists(),
        )


def text_cases(expect: testing.Expect) -> None:
    """Check the text of a fragment: the head comment, one header on each line and the end of the command."""
    text = rule.fragment_text("PLANTED", ("a/A.h", "b/B.h"), ("first line", "", "last line"))
    expect(
        "a fragment is the comment, then one set() command with one header on each line",
        text == "# first line\n#\n# last line\nset(PLANTED\n    a/A.h\n    b/B.h\n)\n",
    )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (write_cases, text_cases))
