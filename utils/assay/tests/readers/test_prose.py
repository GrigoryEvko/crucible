"""The test of the reader of prose (assay.readers.prose).

`guard selftest tests.readers.test_prose` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.  The cases of C++ read the parse
tree of the pinned tree-sitter kit.
"""

import tempfile
from collections.abc import Mapping
from pathlib import Path

from assay.core import testing
from assay.readers import prose

CPP = (
    "#pragma once\n"
    "// a comment on line two\n"
    "struct Point { int x; };\n"
    "inline Point origin{};\n"
    "inline int read = origin.x;\n"
    '#define NOTE "a string of a macro"\n'
    "#error the text of a directive\n"
    'inline constexpr char raw[] = R"(first\n'
    'second)";\n'
)
PYTHON = '"""The docstring."""\n\nVALUE = 1  # a comment\nTEXT = f"left {VALUE} right"\n'


def prose_of(files: Mapping[str, bytes], link: tuple[str, str] | None = None) -> dict[str, prose.FileProse]:
    """Write the files and a symbolic link (NAME, TARGET) to a scratch root, and return the prose of each file."""
    with tempfile.TemporaryDirectory(prefix="test-prose-") as scratch:
        root = Path(scratch)
        for name, data in files.items():
            (root / name).parent.mkdir(parents=True, exist_ok=True)
            (root / name).write_bytes(data)
        names = sorted(files)
        if link is not None:
            (root / link[0]).symlink_to(link[1])
            names.append(link[0])
        return {item.path: item for item in prose.prose_of(root, names)}


def texts(item: prose.FileProse) -> list[tuple[int, str]]:
    """Return the line and the content of each prose line of a file."""
    return [(line.line, line.content) for line in item.lines]


def is_unreadable(item: prose.FileProse, start: str) -> bool:
    """Return True for a file with no lines and a problem that starts with a text."""
    return not item.lines and item.problem.startswith(start)


def cpp_cases(expect: testing.Expect) -> None:
    """Check the prose of a C++ file."""
    found = texts(prose_of({"include/planted.h": CPP.encode()})["include/planted.h"])
    expect(
        "a comment, a raw string of two lines, the string of a macro and the argument of a directive are prose",
        (2, "// a comment on line two") in found
        and (6, '"a string of a macro"') in found
        and (7, "the text of a directive") in found
        and (8, 'R"(first') in found
        and (9, 'second)"') in found,
    )
    expect("code is no prose, so a member access is no line", all("origin" not in text for _, text in found))
    broken = prose_of({"include/broken.h": b"struct Broken { int x = ; \n"})["include/broken.h"]
    expect("a C++ file that does not parse has no lines and a problem", is_unreadable(broken, "the C++ file"))
    unparseable = prose_of({"include/crucible/perf/bpf/vmlinux.h": b"struct planted { int operator; };\n"})
    expect(
        "a file that UNPARSEABLE lists is read line by line",
        texts(unparseable["include/crucible/perf/bpf/vmlinux.h"])[:1] == [(1, "struct planted { int operator; };")],
    )


def python_cases(expect: testing.Expect) -> None:
    """Check the prose of a Python file."""
    found = texts(prose_of({"tool.py": PYTHON.encode()})["tool.py"])
    expect(
        "the docstring, a comment and the text of an f-string are prose, and the code is not",
        (1, '"""The docstring."""') in found
        and (3, "# a comment") in found
        and any(line == 4 and text.startswith("left ") for line, text in found)
        and all("VALUE = 1" not in text for _, text in found),
    )
    bad = prose_of({"bad.py": b'x = """\n'})["bad.py"]
    expect("a Python file that does not tokenize has no lines and a problem", is_unreadable(bad, "the Python file"))


def text_cases(expect: testing.Expect) -> None:
    """Check the prose of a text file, of a binary file and of a symbolic link."""
    found = prose_of(
        {"notes.md": b"first\nsecond\n", "blob.bin": b"\0\1\2", "latin.txt": "caf\xe9".encode("latin-1")},
        ("link.md", "plans/target.md"),
    )
    expect("each line of a text file is prose", texts(found["notes.md"]) == [(1, "first"), (2, "second"), (3, "")])
    expect(
        "a file with a NUL byte and a file that is not UTF-8 have no prose and no problem",
        texts(found["blob.bin"]) == [] and texts(found["latin.txt"]) == [] and not found["latin.txt"].problem,
    )
    expect("a symbolic link gives the path of its target", texts(found["link.md"]) == [(1, "plans/target.md")])


def pattern_cases(expect: testing.Expect) -> None:
    """Check the search of a pattern."""
    pattern = prose.Pattern(r"\bDim [0-9]{1,2}\b", r"\(dim [0-9]{1,2}[,)]")
    expect(
        "first() gives the first match of a branch, and None for a text with no match",
        pattern.first("the axis Dim 22 Staleness") == "Dim 22"
        and pattern.first("Staleness (dim 22, stale)") == "(dim 22,"
        and pattern.first("DimensionAxis::Staleness = 19") is None,
    )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (cpp_cases, python_cases, text_cases, pattern_cases))
