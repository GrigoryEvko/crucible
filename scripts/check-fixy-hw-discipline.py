#!/usr/bin/env python3
"""check-fixy-hw-discipline — every hardware-axis block has a row, and every row has a live block.

A hardware-axis block is a namespace named `<site>_hw` that restates a
hardware construct as fixy atoms: a SIMD ISA, a cache instruction or a memory
fence that the preprocessor selects.  Each atom is a type alias in the block.

THE TWO HALVES OF THE GATE
    The compiler half is test/test_hardware_axis_pins.cpp.  It holds one row
    for each block, `static_assert(hw_axis_pins::pinned<^^block, Axis...>);`,
    and test/fixy/hw_axis_pins.h walks the block by reflection when the row is
    evaluated.  The row fails to compile when an axis of the row has no atom
    alias in the block, when an atom alias engages an axis that the row does
    not list, or when the block does not exist in the build.

    This script is the other half.  It reads the parse tree of the pinned
    tree-sitter kit (scripts/tsast.py) and holds the tree to the rows:
      * every `*_hw` namespace under include/ and src/ has a row
      * every row names a block that a file under include/ or src/ defines
      * at least one definition of each block sits outside every preprocessor
        conditional, because the compiler sees only the arm that one build
        selects, and a block in a conditional is dead in the other builds
      * every row names its block from `::`, because this script compares the
        name as written, and the compiler would resolve a relative name
    A file on the tsast UNPARSEABLE roster is not C++ and is out of scope.

WHAT READS THE TREE
    Only parse nodes.  A comment and a string hold no node, so a block name or
    a row in one counts for nothing.

Usage
    check-fixy-hw-discipline.py              scan the tree
    check-fixy-hw-discipline.py --self-test  plant each case and examine each verdict

Exit 0 clean, 1 when a row lost its block, a block is dead, the rows file is
gone or a file does not parse, 2 when the tree holds a block that has no row
or a row that is not written from `::`, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import os
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402  (the path insert above has to come first)

ROOTS = ("include", "src")
ROWS_FILE = "test/test_hardware_axis_pins.cpp"
ROW_TEMPLATE = ("hw_axis_pins", "pinned")
PREPROC_ARMS = ("preproc_if", "preproc_ifdef", "preproc_else", "preproc_elif", "preproc_elifdef")
OPERAND_NAMES = ("qualified_identifier", "namespace_identifier", "type_identifier", "identifier")

# The kinds that exit 1: the tree lost a claim, or the guard cannot read it.
LOST = frozenset({"missing-rows", "parse", "missing-block", "dead-block"})


@dataclass(frozen=True)
class Finding:
    """One result of the guard."""

    kind: str
    path: str
    text: str


@dataclass(frozen=True)
class Row:
    """One row of the rows file: the block it names and where it names it."""

    block: str
    line: int
    from_root: bool


def defined_name(node: tsast.Node) -> tuple[str, ...]:
    """Return the full name of the namespace that a definition opens, from the root.

    Args:
        node: A namespace_definition

    Returns:
        The name parts, for example ("crucible", "detail", "swiss_hw").  An
        anonymous namespace is "".
    """
    name = node.child_by_field("name")
    parts = tsast.qualified_parts(name) if name is not None else None
    return tsast.namespace_path(node) + (parts[1] if parts is not None else ("",))


def row_of(assertion: tsast.Node) -> Row | None:
    """Read one static_assert as a row, or return None when it is not a row.

    A row is `static_assert(hw_axis_pins::pinned<^^block, Axis...>);`.  The
    block is the reflected name, the first template argument.

    Args:
        assertion: A static_assert_declaration node

    Returns:
        The row, or None for any other assertion
    """
    name = assertion.child_by_field("condition")
    if name is None or name.type != "qualified_identifier":
        return None
    template = name.child_by_field("name")
    scope = name.child_by_field("scope")
    if template is None or template.type != "template_function" or scope is None:
        return None
    template_name = template.child_by_field("name")
    if template_name is None or (scope.text, template_name.text) != ROW_TEMPLATE:
        return None
    arguments = template.child_by_field("arguments")
    reflected = next(iter(arguments.children_of_type("reflect_expression")), None) if arguments else None
    # descendants() walks in source order, so the first name is the outermost one.
    operand = next(reflected.descendants(*OPERAND_NAMES), None) if reflected is not None else None
    parts = tsast.qualified_parts(operand) if operand is not None else None
    if parts is None:
        return None
    return Row("::".join(parts[1]), assertion.line, parts[0])


def read_rows(root: Path) -> tuple[list[Row], list[Finding]]:
    """Read the rows from the parse tree of the rows file.

    Args:
        root: The repository root

    Returns:
        The rows, and a finding when the file is gone, unreadable or empty
    """
    path = root / ROWS_FILE
    if not path.is_file():
        return [], [Finding("missing-rows", ROWS_FILE, f"FIXY-HW-DISCIPLINE missing rows: {ROWS_FILE} is gone, so no "
                                                       f"hardware-axis block is held to its axes.  Restore it.")]
    tree = next(tsast.parse([path], strict=False))
    if tree.diagnostic is not None:
        return [], [Finding("parse", ROWS_FILE, f"FIXY-HW-DISCIPLINE parse failure: {ROWS_FILE} — the parser cannot "
                                                f"read the rows.\n  {tree.diagnostic}")]
    rows = [row for node in tree.find("static_assert_declaration") if (row := row_of(node)) is not None]
    if not rows:
        return [], [Finding("missing-rows", ROWS_FILE, f"FIXY-HW-DISCIPLINE missing rows: {ROWS_FILE} holds no "
                                                       f"hw_axis_pins::pinned row.")]
    return rows, []


def scanned_files(root: Path) -> list[Path]:
    """Return the C++ files under the roots that are in scope.

    Args:
        root: The repository root

    Returns:
        Paths relative to the root, sorted
    """
    found: list[Path] = []
    for top in ROOTS:
        base = root / top
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            rel = path.relative_to(root)
            if tsast.is_in_cpp_scope(rel) and path.is_file():
                found.append(rel)
    return sorted(found)


def check(root: Path) -> list[Finding]:
    """Hold the tree to the rows, in both directions.

    Complexity: one parse of each file under the roots.

    Args:
        root: The repository root

    Returns:
        Every finding, in a stable order

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    rows, findings = read_rows(root)
    blocks: dict[str, list[tuple[str, int, bool]]] = {}
    for tree in tsast.parse([root / rel for rel in scanned_files(root)], strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            findings.append(Finding("parse", rel, f"FIXY-HW-DISCIPLINE parse failure: {rel} — the parser cannot read "
                                                  f"this file, so its hardware-axis blocks are unknown.\n"
                                                  f"  {tree.diagnostic}"))
            continue
        for node in tree.find("namespace_definition"):
            name = defined_name(node)
            if name[-1].endswith("_hw"):
                dead = node.ancestor_of_type(*PREPROC_ARMS) is not None
                blocks.setdefault("::".join(name), []).append((rel, node.line, dead))

    listed: set[str] = set()
    for row in rows:
        listed.add(row.block)
        where = f"{ROWS_FILE}:{row.line}"
        if not row.from_root:
            findings.append(Finding("relative-row", ROWS_FILE,
                                    f"FIXY-HW-DISCIPLINE relative row: {where} — the row names {row.block} without a "
                                    f"leading `::`.  Write the row from `::`, so this script and the compiler read "
                                    f"the same name."))
        found = blocks.get(row.block, [])
        if not found:
            findings.append(Finding("missing-block", ROWS_FILE,
                                    f"FIXY-HW-DISCIPLINE missing block: {where} — no file under include/ or src/ "
                                    f"defines namespace {row.block}.  Restore the block, or remove its row."))
        elif all(dead for _rel, _line, dead in found):
            places = ", ".join(f"{rel}:{line}" for rel, line, _dead in found)
            findings.append(Finding("dead-block", found[0][0],
                                    f"FIXY-HW-DISCIPLINE dead block: {places} — each definition of {row.block} sits "
                                    f"in a preprocessor conditional, so a build that takes the other arm has no "
                                    f"block.  Move the block out of the conditional."))
    for name in sorted(blocks):
        if name in listed:
            continue
        for rel, line, _dead in blocks[name]:
            findings.append(Finding("unlisted-site", rel,
                                    f"FIXY-HW-DISCIPLINE unlisted site: {rel}:{line} — the namespace {name} is a "
                                    f"hardware-axis block with no row, so no gate holds it to its axes.  Add a row to "
                                    f"{ROWS_FILE} in the same commit."))
    return findings


def run(root: Path) -> int:
    """Check the tree, print each finding, and return the exit code.

    Args:
        root: The repository root

    Returns:
        0 when clean, 1 when a claim is lost, 2 when a block has no row or a row is relative
    """
    findings = check(root)
    for finding in findings:
        print(finding.text, file=sys.stderr)
    lost = sum(finding.kind in LOST for finding in findings)
    if findings:
        print(f"check-fixy-hw-discipline: {len(findings)} finding(s): {lost} lost claim(s), "
              f"{len(findings) - lost} unlisted block(s) or relative row(s).", file=sys.stderr)
        return 1 if lost else 2
    print("check-fixy-hw-discipline: clean — every hardware-axis block has a row, and every row has a live block.",
          file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each case and examine each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, ok: bool) -> None:
        """Record one case result and print it."""
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    row = ("static_assert(hw_axis_pins::pinned<^^::demo::site_hw, ::fixy::Axis::SimdIsa,\n"
           "                                   ::fixy::Axis::HwInstruction>);\n")
    block = "namespace demo {\nnamespace site_hw {\nusing Tier = ::fixy::atom::hw::scalar;\n}\n}\n"

    cases: list[tuple[str, str, str, set[str]]] = [
        ("a row and its block pass", row, block, set()),
        ("a row without a block is a lost claim", row, "namespace demo {}\n", {"missing-block"}),
        ("a block name in a comment is no block", row, "// namespace site_hw {\nnamespace demo {}\n",
         {"missing-block"}),
        ("a block name in a string is no block", row,
         "namespace demo { inline const char* text = \"namespace site_hw {\"; }\n", {"missing-block"}),
        ("a block under #if 0 is dead", row, "#if 0\n" + block + "#endif\n", {"dead-block"}),
        ("a block under #ifdef is dead", row, "#ifdef __AVX2__\n" + block + "#endif\n", {"dead-block"}),
        ("a live definition keeps a block live", row, block + "#if 0\nnamespace demo::site_hw {}\n#endif\n", set()),
        ("a block in another namespace is missing, and unlisted", row,
         "namespace other {\nnamespace site_hw {}\n}\n", {"missing-block", "unlisted-site"}),
        ("a row in a comment is no row, so its block is unlisted",
         "// " + row.replace("\n", "\n// ") + "\n" + row.replace("site_hw", "other_hw"),
         block + "namespace demo::other_hw {}\n", {"unlisted-site"}),
        ("a relative row is refused", row.replace("^^::demo", "^^demo"), block, {"relative-row"}),
        ("a nested namespace specifier names the block", row, "namespace demo::site_hw {}\n", set()),
    ]

    def plant(root: Path, rows_text: str, site_text: str) -> None:
        """Write the rows file and the site header."""
        (root / ROWS_FILE).write_text(f'#include "fixy/hw_axis_pins.h"\n{rows_text}int main() {{ return 0; }}\n',
                                      encoding="utf-8")
        (root / "include/hw/Site.h").write_text(f"#pragma once\n{site_text}", encoding="utf-8")

    def captured(action) -> tuple[int, str]:
        """Run an action and return its code and its stderr."""
        buffer = io.StringIO()
        with contextlib.redirect_stderr(buffer):
            code = action()
        return code, buffer.getvalue()

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        (root / "include/hw").mkdir(parents=True)
        (root / "test").mkdir()
        for name, rows_text, site_text, expected in cases:
            plant(root, rows_text, site_text)
            got = {finding.kind for finding in check(root)}
            expect(name, got == expected)
            if got != expected:
                print(f"       expected {sorted(expected)}, got {sorted(got)}")

        plant(root, row, block)
        expect("a complete tree exits 0", captured(lambda: run(root))[0] == 0)
        previous = Path.cwd()
        os.chdir("/")
        try:
            from_slash = captured(lambda: run(root))
        finally:
            os.chdir(previous)
        expect("the report from / equals the report from the repository", from_slash == captured(lambda: run(root)))

        other = root / "src/Other.cpp"
        other.parent.mkdir(parents=True)
        other.write_text("namespace crucible::other_hw {\n}\n", encoding="utf-8")
        code, report = captured(lambda: run(root))
        expect("an unlisted block under src/ exits 2", code == 2 and "src/Other.cpp:1" in report)
        other.unlink()

        broken = root / "include/hw/Broken.h"
        broken.write_text("namespace broken_hw { void f() { g(1) { } } }\n", encoding="utf-8")
        code, report = captured(lambda: run(root))
        expect("a file the parser cannot read exits 1", code == 1 and "parse failure: include/hw/Broken.h" in report)
        broken.unlink()

        plant(root, "int unrelated;\n", block)
        code, report = captured(lambda: run(root))
        expect("a rows file with no row exits 1", code == 1 and "holds no hw_axis_pins::pinned row" in report)
        (root / ROWS_FILE).unlink()
        code, report = captured(lambda: run(root))
        expect("a missing rows file exits 1", code == 1 and f"missing rows: {ROWS_FILE}" in report)

    if failures:
        print(f"check-fixy-hw-discipline --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print("check-fixy-hw-discipline --self-test: every case passes.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode.

    Args:
        argv: The arguments after the script name

    Returns:
        The exit code
    """
    try:
        if argv == []:
            return run(tsast.REPO_ROOT)
        if argv == ["--self-test"]:
            return self_test()
    except tsast.KitMissing as exc:
        print(f"check-fixy-hw-discipline: {exc}", file=sys.stderr)
        return 3
    print("usage: check-fixy-hw-discipline.py [--self-test]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
