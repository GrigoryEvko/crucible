"""The test of the pin table (assay.core.pins): the pin rows, the platform of a host, and the pin of Python.

`guard selftest tests.core.test_pins` runs the test.  Each case prints one
line.  Exit 0 when each case holds, else 1.
"""

import ast
import sys
from collections.abc import Callable

from assay.core import pins as pin_table
from assay.core import rows as rows_grammar
from assay.core.paths import GUARD, LOOP, SETUP

DIGEST = "0" * 64
COMMIT = "1" * 40
ARCHIVE = f"pin tool via=archive version=1.0 platform=linux-x86_64 source=https://example.invalid/t.zip sha256={DIGEST} | a tool"
MASTERS = (GUARD, LOOP, SETUP)


def planted(*rows: str) -> str:
    """Return the text of a pin table with a head comment, the format row and the rows."""
    return "# a planted pin table\nformat 1\n" + "".join(f"{row}\n" for row in rows)


def problem_of(*rows: str) -> str:
    """Return the text of the problem of a planted table, or "" when it reads."""
    try:
        pin_table.parse(planted(*rows), "planted.txt")
    except rows_grammar.RowsError as problem:
        return str(problem)
    return ""


def row_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the fields of each via, the key of a row and the format row."""
    pins = pin_table.parse(planted(ARCHIVE, f"pin repo via=git version=2 source=https://example.invalid/r.git "
                                            f"commit={COMMIT} | a repository", "pin language via=host version=3.14 | a host"),
                           "planted.txt")
    expect("a good table reads, with the fields of each row",
           [pin.name for pin in pins] == ["tool", "repo", "language"] and pins[0].digest == DIGEST
           and pins[0].platform == "linux-x86_64" and pins[1].commit == COMMIT and pins[1].via == pin_table.Via.GIT
           and pins[2].via == pin_table.Via.HOST and pins[2].source == "" and pins[0].text == "a tool"
           and pins[0].line == 3)
    refused = {
        "an unknown via": ARCHIVE.replace("via=archive", "via=ftp"),
        "an unknown field": ARCHIVE.replace("version=1.0", "version=1.0 colour=red"),
        "no version": ARCHIVE.replace(" version=1.0", ""),
        "no source": ARCHIVE.replace(" source=https://example.invalid/t.zip", ""),
        "a short digest": ARCHIVE.replace(DIGEST, "0" * 63),
        "a digest with an uppercase digit": ARCHIVE.replace(DIGEST, "A" * 64),
        "an archive with a commit": ARCHIVE.replace(f"sha256={DIGEST}", f"commit={COMMIT}"),
        "a git row with a digest": f"pin repo via=git version=2 source=https://example.invalid/r sha256={DIGEST} | r",
        "a git row with a short commit": f"pin repo via=git version=2 source=https://example.invalid/r commit=12 | r",
        "a host row with a source": "pin language via=host version=3.14 source=https://example.invalid | a host",
        "a platform with an unknown machine": ARCHIVE.replace("linux-x86_64", "linux-riscv64"),
        "a platform with no machine": ARCHIVE.replace("linux-x86_64", "linux"),
        "no cell": ARCHIVE.split(" | ")[0],
        "a field in place of the name": ARCHIVE.replace("pin tool ", "pin via=git "),
    }
    for name, row in refused.items():
        expect(f"a pin row with {name} is refused", bool(problem_of(row)))
    expect("a second row with one name and one platform is refused, with the line of the first",
           "line 3" in problem_of(ARCHIVE, ARCHIVE.replace("a tool", "the same tool")))
    expect("two rows with one name and two platforms read",
           not problem_of(ARCHIVE, ARCHIVE.replace("linux-x86_64", "linux-aarch64")))
    expect("a table with no format row is refused", bool(problem_of_text("pin language via=host version=3 | x\n")))
    expect("a row of another kind is refused", bool(problem_of("tool language via=host version=3 | x")))


def problem_of_text(text: str) -> str:
    """Return the text of the problem of a table text, or "" when it reads."""
    try:
        pin_table.parse(text, "planted.txt")
    except rows_grammar.RowsError as problem:
        return str(problem)
    return ""


def platform_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the platform of a host and the choice of a row for a platform."""
    expect("the platform of a host has the words of the table",
           pin_table.host_platform("Linux", "x86_64") == "linux-x86_64"
           and pin_table.host_platform("Darwin", "arm64") == "darwin-aarch64"
           and pin_table.host_platform("Linux", "aarch64") == "linux-aarch64")
    expect("a machine that the table does not name stays as the kernel gives it",
           pin_table.host_platform("Linux", "riscv64") == "linux-riscv64")
    pins = pin_table.parse(planted(ARCHIVE, ARCHIVE.replace("linux-x86_64", "linux-aarch64").replace("a tool", "arm"),
                                   ARCHIVE.replace(" platform=linux-x86_64", "").replace("a tool", "general")),
                           "planted.txt")
    expect("a platform gets its own row", (pin_table.find(pins, "tool", "linux-aarch64") or pins[0]).text == "arm")
    expect("a platform with no row of its own gets the row with no platform",
           (pin_table.find(pins, "tool", "darwin-aarch64") or pins[0]).text == "general")
    expect("an unknown name gives None", pin_table.find(pins, "other", "linux-x86_64") is None)


def tree_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the pin table of the tree, and that each master refuses the Python below the pin of Python."""
    pins = pin_table.load()
    expect("the pin table of the tree reads, and each tool of an install has a row for linux-x86_64",
           all(pin_table.find(pins, name, "linux-x86_64") is not None
               for name in ("tree-sitter-kit", "ast-grep", "cmake", "gcc", "python")))
    python = pin_table.find(pins, "python", "")
    wanted = tuple(int(part) for part in python.version.split(".")) if python is not None else ()
    for master in MASTERS:
        found: list[tuple[int, ...]] = []
        for node in ast.walk(ast.parse(master.read_text(encoding="utf-8"), str(master))):
            if (isinstance(node, ast.Compare) and isinstance(node.left, ast.Attribute)
                    and node.left.attr == "version_info" and isinstance(node.comparators[0], ast.Tuple)):
                found.append(tuple(element.value for element in node.comparators[0].elts
                                   if isinstance(element, ast.Constant)))
        expect(f"the master {master.name} refuses a Python older than the pin python {python.version if python else '?'}",
               found == [wanted])


def main(argv: list[str]) -> int:
    """Run each case, and print one line for each.

    Returns:
        0 when each case holds, 1 when a case fails, 2 for a usage error
    """
    if argv:
        print("usage: guard selftest tests.core.test_pins", file=sys.stderr)
        return 2
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    row_cases(expect)
    platform_cases(expect)
    tree_cases(expect)
    if failures:
        print(f"tests.core.test_pins: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("tests.core.test_pins: every case holds.")
    return 0
