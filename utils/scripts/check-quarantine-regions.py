#!/usr/bin/env python3
"""check-quarantine-regions — each opt-out region of the quarantine has a row of its ledger, and the ledger only shrinks.

THE REGIONS
    CRUCIBLE_I_KNOW_WHAT_IM_DOING("CLASS: reason") of
    include/foundation/Quarantine.h opens an opt-out region, and the plugin of
    utils/tools/quarantine/ reports each finding inside it as opted_out
    (CLAUDE.md section XXII, R7).  The check reads the preprocessing tokens of
    each tracked C++ file through the parse tree of utils/scripts/tsast.py.  A
    region is the identifier CRUCIBLE_I_KNOW_WHAT_IM_DOING, then `(`, one
    string literal and `)`.  The body of a macro definition is one text token,
    so the definition of the macro is no region.  A region in a comment is no
    region.  The check reads each preprocessor arm, so each preset gives the
    same regions.

THE LEDGER
    utils/scripts/quarantine-region-ledger.txt holds one row for each region:
    "FILE | REASON".  Two regions of one file with one reason are two equal
    rows.  The plugin refuses a reason that does not start with its class in
    each compile.

THE VERDICT
    * A region with no row is an error.  The number of regions can only fall.
    * A row with no region is an error.  The commit that removes a region
      writes the ledger again (--write).
    * Each row gives a warning, so the regions stay in the output of each run.

WRITE THE LEDGER
    --write writes the rows of the tree and keeps the head comment.  It
    refuses a region with no row.  --admit writes such a region too.  Use it
    only for a region of a reason class of CLAUDE.md section XXII, and give the
    reason in the commit.

Usage
    check-quarantine-regions.py [--warnings-dir DIR]
    check-quarantine-regions.py --write [--admit]
    check-quarantine-regions.py --self-test

Exit 0 with no finding or with warnings only, 1 with an error, 2 on a usage error
or a failed self-test, 3 when the kit of the parse tree is missing.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import sys
import tempfile
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
import tsast  # noqa: E402

CHECK = "quarantine-regions"
LEDGER = Path("utils/scripts/quarantine-region-ledger.txt")
MACRO = "CRUCIBLE_I_KNOW_WHAT_IM_DOING"
NOT_APPLICABLE = 3


def string_value(literal: str) -> str | None:
    """Return the text of a plain string literal, or None for any other literal.

    The reason of a region is a plain string literal with no prefix and no
    escape, so the text between its quotes is its value.
    """
    if len(literal) < 2 or not literal.startswith('"') or not literal.endswith('"') or "\\" in literal:
        return None
    return literal[1:-1]


def regions_of(tree: tsast.Tree) -> list[tuple[int, str]]:
    """Return the line and the reason of each region of one file.

    Complexity: linear in the number of tokens of the file.
    """
    tokens = tree.root.lexed()
    found: list[tuple[int, str]] = []
    for index, token in enumerate(tokens[:-3]):
        if token.kind != "identifier" or token.text != MACRO:
            continue
        opening, literal, closing = tokens[index + 1:index + 4]
        if opening.text != "(" or literal.kind != "string" or closing.text != ")":
            continue
        reason = string_value(literal.text)
        if reason is not None:
            found.append((token.row + 1, reason))
    return found


def tree_regions(root: Path) -> tuple[Counter[tuple[str, str]], dict[tuple[str, str], int]]:
    """Return the regions of each tracked C++ file of one tree.

    A file with a parse error still gives its tokens, so its regions count.

    Returns:
        The count of each (file, reason), and the first line of each
    """
    files = [rel for rel in tsast.tracked_files(root) if tsast.is_in_cpp_scope(rel) and (root / rel).is_file()]
    relative_of = {(root / rel).as_posix(): rel for rel in files}
    counts: Counter[tuple[str, str]] = Counter()
    lines: dict[tuple[str, str], int] = {}
    for tree in tsast.parse([root / rel for rel in files], strict=False):
        rel = relative_of.get(Path(tree.path).as_posix(), Path(tree.path).as_posix())
        for line, reason in regions_of(tree):
            counts[(rel, reason)] += 1
            lines.setdefault((rel, reason), line)
    return counts, lines


def read_ledger(root: Path) -> tuple[list[str], Counter[tuple[str, str]], dict[tuple[str, str], int],
                                     list[check_report.Finding]]:
    """Read the ledger of one tree.

    Returns:
        The head comment lines, the count of each (file, reason), the line of
        the first row of each, and an error for each row that does not have the
        format
    """
    path = root / LEDGER
    head: list[str] = []
    rows: Counter[tuple[str, str]] = Counter()
    lines: dict[tuple[str, str], int] = {}
    errors: list[check_report.Finding] = []
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as problem:
        errors.append(check_report.Finding("error", LEDGER.as_posix(), 0, CHECK,
                                           f"the ledger cannot be read ({problem}).  Write it with --write"))
        return head, rows, lines, errors
    for number, line in enumerate(text.splitlines(), start=1):
        if not line.strip() or line.startswith("#"):
            if not rows:
                head.append(line)
            continue
        file, separator, reason = line.partition(" | ")
        if not separator or not file.strip() or not reason.strip():
            errors.append(check_report.Finding("error", LEDGER.as_posix(), number, CHECK,
                                               f"the row {line!r} is not 'FILE | REASON'"))
            continue
        key = (file.strip(), reason.strip())
        rows[key] += 1
        lines.setdefault(key, number)
    return head, rows, lines, errors


def evaluate(root: Path) -> list[check_report.Finding]:
    """Compare the regions of one tree with its ledger, and return each finding."""
    counts, region_lines = tree_regions(root)
    _, rows, row_lines, findings = read_ledger(root)
    for key in sorted(set(counts) | set(rows)):
        file, reason = key
        found, admitted = counts[key], rows[key]
        if found > admitted:
            findings.append(check_report.Finding(
                "error", file, region_lines[key], CHECK,
                f"{found} region(s) with the reason {reason!r}, and the ledger admits {admitted}.  The number of "
                f"regions can only fall: use a type of fixy or foundation, and remove the region"))
        elif found < admitted:
            findings.append(check_report.Finding(
                "error", LEDGER.as_posix(), row_lines[key], CHECK,
                f"{file} has {found} region(s) with the reason {reason!r}, and the ledger holds {admitted}.  Write "
                f"the ledger again in the same commit: python3 utils/scripts/check-quarantine-regions.py --write"))
        for _ in range(min(found, admitted)):
            findings.append(check_report.Finding("warning", file, region_lines[key], CHECK,
                                                 f"an opt-out region stays: {reason}"))
    return findings


def write(root: Path, is_admitted: bool) -> int:
    """Write the regions of one tree into its ledger.

    Returns:
        0 when it wrote the ledger, 1 when it refused a region with no row
    """
    counts, _ = tree_regions(root)
    head, rows, _, _ = read_ledger(root)
    new = sorted(key for key in counts if counts[key] > rows[key])
    if new and not is_admitted:
        for file, reason in new:
            print(f"check-quarantine-regions: {file} has a new region with the reason {reason!r}, and --write keeps "
                  f"the ledger from growing.  Use --admit only for a reason class of CLAUDE.md section XXII",
                  file=sys.stderr)
        return 1
    body = [f"{file} | {reason}" for (file, reason) in sorted(counts.elements())]
    while head and not head[-1].strip():
        head.pop()
    (root / LEDGER).write_text("\n".join([*head, *([""] if head and body else []), *body]) + "\n", encoding="utf-8")
    print(f"check-quarantine-regions: wrote {len(body)} row(s) to {root / LEDGER}", file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant regions and ledgers in a scratch tree, and judge each verdict."""
    failures: list[str] = []

    def expect(name: str, holds: bool, detail: object = "") -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(f"{name}: {detail}")

    tsast.kit_dir()
    with tempfile.TemporaryDirectory(prefix="quarantine-regions-") as scratch:
        root = Path(scratch)
        (root / LEDGER).parent.mkdir(parents=True)
        (root / "include").mkdir()
        (root / "include/Quarantine.h").write_text(
            "#pragma once\n#define CRUCIBLE_I_KNOW_WHAT_IM_DOING(reason) _Pragma(\"crucible\")\n", encoding="utf-8")
        (root / "include/Abi.cpp").write_text(
            "#include \"Quarantine.h\"\n"
            "CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"ABI: the dispatcher takes a raw pointer\")\n"
            "extern \"C\" void entry(void* data);\n"
            "CRUCIBLE_END_I_KNOW_WHAT_IM_DOING\n"
            "// CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: a comment\")\n"
            "const char* text = \"CRUCIBLE_I_KNOW_WHAT_IM_DOING(\\\"PROBE: a string\\\")\";\n"
            "#if 0\n"
            "CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"MEASURE: an arm that no preset compiles\")\n"
            "#endif\n", encoding="utf-8")
        ledger = root / LEDGER
        ledger.write_text("# the head comment\n", encoding="utf-8")

        found = evaluate(root)
        errors = [item for item in found if item.level == "error"]
        expect("a region with no row is an error, and a comment, a text and the definition are no region",
               len(errors) == 2 and all(item.path == "include/Abi.cpp" for item in errors), found)
        expect("--write refuses a new region", write(root, False) == 1 and "Abi.cpp" not in ledger.read_text())
        expect("--admit writes the regions of each preprocessor arm and keeps the head comment",
               write(root, True) == 0 and ledger.read_text().startswith("# the head comment\n")
               and ledger.read_text().count("include/Abi.cpp | ") == 2, ledger.read_text())
        found = evaluate(root)
        expect("the ledger and the tree agree: each row gives a warning and no error",
               [item.level for item in found] == ["warning", "warning"], found)

        ledger.write_text(ledger.read_text() + "include/Abi.cpp | ABI: the dispatcher takes a raw pointer\n",
                          encoding="utf-8")
        found = evaluate(root)
        expect("a row with no region is an error", any(item.level == "error" and item.path == LEDGER.as_posix()
                                                       for item in found), found)
        ledger.write_text(ledger.read_text() + "a row with no separator\n", encoding="utf-8")
        expect("a row that does not have the format is an error",
               any("is not 'FILE | REASON'" in item.message for item in evaluate(root)))

        warnings_dir = root / "warnings"
        with check_report.github_actions(False), contextlib.redirect_stdout(io.StringIO()) as printed:
            status = check_report.emit(evaluate(root), CHECK, warnings_dir)
        expect("an error gives exit status 1 and the line format",
               status == 1 and all(check_report.parse_line(line) is not None
                                   for line in printed.getvalue().splitlines()))
    if failures:
        for failure in failures:
            print(f"check-quarantine-regions --self-test: FAILED, {failure}", file=sys.stderr)
        return 2
    print("check-quarantine-regions --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check, write the ledger, or run the self-test."""
    parser = argparse.ArgumentParser(prog="check-quarantine-regions.py", description=__doc__.split("\n", 1)[0])
    check_report.add_arguments(parser)
    parser.add_argument("--write", action="store_true", help="write the regions of the tree into the ledger")
    parser.add_argument("--admit", action="store_true", help="with --write, write a region with no row too")
    parser.add_argument("--self-test", action="store_true", help="plant each case in a scratch tree")
    arguments = parser.parse_args(argv)
    if arguments.admit and not arguments.write:
        parser.error("--admit needs --write")
    try:
        if arguments.self_test:
            return self_test()
        if arguments.write:
            return write(tsast.REPO_ROOT, arguments.admit)
        findings = evaluate(tsast.REPO_ROOT)
    except tsast.KitMissing as missing:
        print(f"check-quarantine-regions: SKIP, {missing}", file=sys.stderr)
        return NOT_APPLICABLE
    code = check_report.emit(findings, CHECK, arguments.warnings_dir)
    print(f"check-quarantine-regions: {len(findings)} finding(s).", file=sys.stderr)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
