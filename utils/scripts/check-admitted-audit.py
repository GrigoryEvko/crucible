#!/usr/bin/env python3
"""check-admitted-audit — each audited standard library name has one row and one section of the flag matrix.

The quarantine plan (misc/01_10_2026_quarantine.md) audits each entry of the
admitted list and each candidate of its appendix A.  The audit table gives one
row for each name, and the flag matrix gives a static_assert for each property
of each name.  This check keeps the three sources in step.

THE INPUTS
    utils/scripts/layer-rules.txt                the rule table.  Its admit
                                                 rows are the admitted list,
                                                 and utils/scripts/layer_rules.py
                                                 reads them
    utils/scripts/quarantine-admitted-audit.txt  the audit table, one row of
                                                 nine fields each
    test/layer/admitted_flag_matrix.cpp          the flag matrix.  The check
                                                 reads its comments from the
                                                 parse tree of utils/scripts/tsast.py

THE RULES
    1. A row has nine fields: NAME | ORIGIN | FLAGS | PREDICTABLE | SAFE |
       USES | COST | VERDICT | REASON.  No field is empty, and no name has two
       rows.
    2. ORIGIN is `admitted`, `candidate` or `dropped`.  A row is `admitted`
       when, and only when, its name is an entry of the admitted list.  Each
       entry of the list has a row.  A `dropped` row names an entry that the
       audit removed from the list, and its verdict is DROP.
    3. The name of a `candidate` row is in CANDIDATES, the candidates of
       appendix A, and each candidate has a row.
    4. FLAGS is `invariant` or starts with `varies:`.  PREDICTABLE and SAFE
       start with `yes` or `no`.
    5. VERDICT is KEEP, DROP, `KEEP-RESTRICTED: RESTRICTION` or
       `REPLACE: FAMILY ...`.  FAMILY is one of the eight families of the plan.
    6. Each row has one section in the matrix, which starts with the comment
       `// name: NAME`, and each section names a row.
    7. A section with the comment `// varies: MACRO` has a row whose FLAGS
       starts with `varies:` and names MACRO.
    8. The admitted list applies each verdict.  A KEEP verdict and a
       KEEP-RESTRICTED verdict have an entry, and a DROP verdict has none.  The
       plugin enforces each restriction through the restriction words of the
       entry, and check_plugin.py holds a plant for each one.  A verdict
       `REPLACE: FAMILY` has the entry
       `admit NAME until FAMILY`, and each entry with `until` has the verdict
       REPLACE of that family.

Each finding is an error.  The tree holds no violation, so the check has no
ledger.

Usage
    check-admitted-audit.py [--warnings-dir DIR]
    check-admitted-audit.py --self-test

Exit 0 with no finding, 1 on a finding, 2 on a usage error or a failed
self-test, 3 when the pinned tree-sitter kit is not installed.
"""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
import layer_rules  # noqa: E402
import tsast  # noqa: E402

CHECK = "admitted-audit"
ADMITTED = Path("utils/scripts/layer-rules.txt")
TABLE = Path("utils/scripts/quarantine-admitted-audit.txt")
MATRIX = Path("test/layer/admitted_flag_matrix.cpp")
FIELDS = ("NAME", "ORIGIN", "FLAGS", "PREDICTABLE", "SAFE", "USES", "COST", "VERDICT", "REASON")
FAMILIES = frozenset({"Choice", "Record", "Region", "Ref", "Atomic", "Scalar", "Report", "Os"})
# The candidates of appendix A of misc/01_10_2026_quarantine.md.  The appendix
# names the integer spellings and the is_eq family as groups, and this set
# gives each name of each group.
CANDIDATES = frozenset({
    "std::initializer_list",
    "std::strong_ordering", "std::weak_ordering", "std::partial_ordering",
    "std::is_eq", "std::is_neq", "std::is_lt", "std::is_lteq", "std::is_gt", "std::is_gteq",
    "std::byte",
    "std::size_t", "std::ptrdiff_t",
    "std::int8_t", "std::int16_t", "std::int32_t", "std::int64_t",
    "std::uint8_t", "std::uint16_t", "std::uint32_t", "std::uint64_t",
    "std::intptr_t", "std::uintptr_t", "std::intmax_t", "std::uintmax_t",
    "std::int_least8_t", "std::int_least16_t", "std::int_least32_t", "std::int_least64_t",
    "std::uint_least8_t", "std::uint_least16_t", "std::uint_least32_t", "std::uint_least64_t",
    "std::int_fast8_t", "std::int_fast16_t", "std::int_fast32_t", "std::int_fast64_t",
    "std::uint_fast8_t", "std::uint_fast16_t", "std::uint_fast32_t", "std::uint_fast64_t",
    "std::nullptr_t",
    "std::meta::info",
    "std::source_location",
    "std::contracts::contract_violation",
    "std::align_val_t", "std::nothrow_t",
})
NAME_MARKER = re.compile(r"//\s*name:\s*(\S.*?)\s*")
VARIES_MARKER = re.compile(r"//\s*varies:\s*([A-Za-z_][A-Za-z0-9_]*)\s*")
VERDICT = re.compile(r"KEEP|DROP|KEEP-RESTRICTED:\s*\S.*|REPLACE:\s*(?P<family>[A-Za-z]+)\b.*")


@dataclass
class Row:
    """One row of the audit table."""

    line: int
    cells: dict[str, str]


@dataclass
class Section:
    """One section of the flag matrix: its first line and the macros of its `varies` comments."""

    line: int
    varies: set[str] = field(default_factory=set)


def error(path: Path, line: int, message: str) -> check_report.Finding:
    """Return one error of this check.

    Args:
        path: The file of the finding, relative to the root
        line: The line of the finding, or 0
        message: The message

    Returns:
        The finding
    """
    return check_report.Finding("error", str(path), line, CHECK, message)


def read_admitted(root: Path, admitted: Path) -> tuple[dict[str, layer_rules.Admit], list[check_report.Finding]]:
    """Read the admit rows of the rule table, which are the entries of the admitted list.

    Args:
        root: The root of the tree
        admitted: The rule table, relative to the root

    Returns:
        Each admit row by its entry, and one finding when the table does not obey its format
    """
    try:
        table = layer_rules.parse((root / admitted).read_text(encoding="utf-8"), admitted.name)
    except layer_rules.TableError as failure:
        return {}, [error(admitted, 0, f"the rule table does not obey its format: {failure}.  Correct the row.")]
    return {admit.entry: admit for admit in table.admits}, []


def verdict_findings(admitted: Path, table: Path, rows: dict[str, Row],
                     entries: dict[str, layer_rules.Admit]) -> list[check_report.Finding]:
    """Return the findings of rule 8: the admitted list applies each verdict.

    Args:
        admitted: The rule table, relative to the root
        table: The audit table, relative to the root
        rows: Each row of the audit table by its name
        entries: Each admit row of the rule table by its entry

    Returns:
        One finding for each verdict that the admitted list does not apply
    """
    findings: list[check_report.Finding] = []
    for name, row in rows.items():
        verdict = VERDICT.fullmatch(row.cells["VERDICT"])
        if verdict is None:
            continue
        entry = entries.get(name)
        family = verdict["family"]
        is_kept = row.cells["VERDICT"] == "KEEP" or row.cells["VERDICT"].startswith("KEEP-RESTRICTED")
        if is_kept and entry is None:
            findings.append(error(table, row.line, f"the verdict of {name} is {row.cells['VERDICT'].split(':')[0]}, "
                                                   f"and {admitted} has no admit row for it.  Add `admit {name} | "
                                                   "REASON`, with the words of the restriction."))
        elif row.cells["VERDICT"] == "DROP" and entry is not None:
            findings.append(error(admitted, entry.line, f"the verdict of {name} is DROP, and the row admits it.  "
                                                        "Remove the row."))
        elif family is not None and (entry is None or entry.until != family):
            findings.append(error(table, row.line, f"the verdict of {name} is REPLACE: {family}, and {admitted} has "
                                                   f"no row `admit {name} until {family}`.  Write that row."))
    for name, entry in entries.items():
        row = rows.get(name)
        verdict = VERDICT.fullmatch(row.cells["VERDICT"]) if row is not None else None
        if entry.until and (verdict is None or verdict["family"] != entry.until):
            findings.append(error(admitted, entry.line, f"the row admits {name} until {entry.until}, and the audit "
                                                        f"verdict of {name} is not REPLACE: {entry.until}.  Correct "
                                                        "the row or the verdict."))
    return findings


def read_table(root: Path, table: Path) -> tuple[dict[str, Row], list[check_report.Finding]]:
    """Read the rows of the audit table and examine the form of each row.

    Complexity: linear in the number of rows.

    Args:
        root: The root of the tree
        table: The audit table, relative to the root

    Returns:
        Each row by its name, and the findings of rules 1, 4 and 5
    """
    rows: dict[str, Row] = {}
    findings: list[check_report.Finding] = []
    for number, raw in enumerate((root / table).read_text(encoding="utf-8").splitlines(), start=1):
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        values = [value.strip() for value in stripped.split("|")]
        if len(values) != len(FIELDS):
            findings.append(error(table, number, f"a row has {len(FIELDS)} fields ({' | '.join(FIELDS)}), and this "
                                                 f"row has {len(values)}.  Correct the row."))
            continue
        cells = dict(zip(FIELDS, values))
        empty = [name for name, value in cells.items() if not value]
        if empty:
            findings.append(error(table, number, f"the row gives no {', '.join(empty)}.  Give each field."))
            continue
        name = cells["NAME"]
        if name in rows:
            findings.append(error(table, number, f"{name} has a second row.  The first is at line "
                                                 f"{rows[name].line}.  Keep one row."))
            continue
        rows[name] = Row(number, cells)
        findings.extend(form_findings(table, number, cells))
    return rows, findings


def form_findings(table: Path, number: int, cells: dict[str, str]) -> list[check_report.Finding]:
    """Return the findings of rules 2 to 5 that one row has alone.

    Args:
        table: The audit table, relative to the root
        number: The line of the row
        cells: The fields of the row

    Returns:
        One finding for each field that does not have its form
    """
    findings: list[check_report.Finding] = []
    name = cells["NAME"]
    if cells["ORIGIN"] not in ("admitted", "candidate", "dropped"):
        findings.append(error(table, number, f"the ORIGIN of {name} is `{cells['ORIGIN']}`.  It is `admitted`, "
                                             "`candidate` or `dropped`."))
    elif cells["ORIGIN"] == "dropped" and cells["VERDICT"] != "DROP":
        findings.append(error(table, number, f"the ORIGIN of {name} is `dropped`, and its verdict is not DROP.  "
                                             "Correct the ORIGIN or the verdict."))
    if cells["FLAGS"] != "invariant" and not cells["FLAGS"].startswith("varies:"):
        findings.append(error(table, number, f"the FLAGS of {name} is `invariant` or starts with `varies:`."))
    for column in ("PREDICTABLE", "SAFE"):
        if not re.match(r"(yes|no)\b", cells[column]):
            findings.append(error(table, number, f"the {column} of {name} starts with `yes` or `no`."))
    verdict = VERDICT.fullmatch(cells["VERDICT"])
    if verdict is None:
        findings.append(error(table, number, f"the VERDICT of {name} is KEEP, DROP, `KEEP-RESTRICTED: RESTRICTION` "
                                             "or `REPLACE: FAMILY`.  Correct the verdict."))
    elif verdict["family"] is not None and verdict["family"] not in FAMILIES:
        findings.append(error(table, number, f"the VERDICT of {name} names the family {verdict['family']}.  The "
                                             f"families are {', '.join(sorted(FAMILIES))}."))
    return findings


def read_matrix(root: Path, matrix: Path) -> tuple[dict[str, Section], list[check_report.Finding]]:
    """Read the sections of the flag matrix from the comments of its parse tree.

    A run of `// name:` comments on lines one after the other starts one
    section.  A `// varies:` comment belongs to the section of the last run.

    Args:
        root: The root of the tree
        matrix: The flag matrix, relative to the root

    Returns:
        Each section by the name that it examines, and the findings of the comments that break the form

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    sections: dict[str, Section] = {}
    findings: list[check_report.Finding] = []
    current: list[str] = []
    last_name_row = -2
    for tree in tsast.parse([root / matrix]):
        for comment in tree.find("comment"):
            prose = tsast.prose_text(comment)
            row = comment.line
            named = NAME_MARKER.fullmatch(prose)
            if named is not None:
                if row != last_name_row + 1:
                    current = []
                last_name_row = row
                name = named.group(1)
                if name in sections:
                    findings.append(error(matrix, row, f"{name} has a second section.  The first is at line "
                                                       f"{sections[name].line}.  Keep one section."))
                    continue
                sections[name] = Section(row)
                current.append(name)
                continue
            varies = VARIES_MARKER.fullmatch(prose)
            if varies is not None:
                if not current:
                    findings.append(error(matrix, row, "a `// varies:` comment comes before the first section.  "
                                                       "Put it in the section of its name."))
                for name in current:
                    sections[name].varies.add(varies.group(1))
    return sections, findings


def evaluate(root: Path, admitted: Path = ADMITTED, table: Path = TABLE,
             matrix: Path = MATRIX) -> list[check_report.Finding]:
    """Return each finding of the rules.

    Complexity: linear in the rows, the entries and the comments.

    Args:
        root: The root of the tree
        admitted: The rule table, whose admit rows are the admitted list, relative to the root
        table: The audit table, relative to the root
        matrix: The flag matrix, relative to the root

    Returns:
        The findings, each an error

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    entries, findings = read_admitted(root, admitted)
    rows, table_findings = read_table(root, table)
    sections, matrix_findings = read_matrix(root, matrix)
    findings.extend(table_findings)
    findings.extend(matrix_findings)
    findings.extend(verdict_findings(admitted, table, rows, entries))
    for entry, admit in entries.items():
        if entry not in rows:
            findings.append(error(admitted, admit.line, f"the admitted entry {entry} has no row in {table}.  Audit "
                                                        "the entry and add its row."))
    for candidate in sorted(CANDIDATES - rows.keys()):
        findings.append(error(table, 0, f"the candidate {candidate} of appendix A has no row.  Audit the candidate "
                                        "and add its row."))
    for name, row in rows.items():
        origin = row.cells["ORIGIN"]
        if origin == "admitted" and name not in entries:
            findings.append(error(table, row.line, f"the row of {name} says `admitted`, and {admitted} has no such "
                                                   "entry.  Correct the ORIGIN, or remove the row."))
        if origin == "dropped" and name in entries:
            findings.append(error(table, row.line, f"the row of {name} says `dropped`, and {admitted} holds the "
                                                   "name.  Remove the admit row, or write `admitted`."))
        if origin == "candidate":
            if name in entries:
                findings.append(error(table, row.line, f"the row of {name} says `candidate`, and {admitted} holds "
                                                       "the name.  Write `admitted`."))
            elif name not in CANDIDATES:
                findings.append(error(table, row.line, f"{name} is no admitted entry and no candidate of appendix "
                                                       "A.  The audit examines those names only.  Remove the row."))
        section = sections.get(name)
        if section is None:
            findings.append(error(table, row.line, f"{name} has no section in {matrix}.  Add a section with the "
                                                   f"comment `// name: {name}` and a static_assert for each property."))
            continue
        for macro in sorted(section.varies):
            flags = row.cells["FLAGS"]
            if not flags.startswith("varies:") or macro not in flags:
                findings.append(error(table, row.line, f"the section of {name} in {matrix} has the comment "
                                                       f"`// varies: {macro}`, and the FLAGS of the row does not "
                                                       f"record it.  Write `varies:` and name {macro}."))
    for name, section in sections.items():
        if name not in rows:
            findings.append(error(matrix, section.line, f"the section of {name} has no row in {table}.  Add the row, "
                                                        "or remove the section."))
    return findings


def self_test() -> int:
    """Plant each defect in a scratch tree and examine each verdict.

    Returns:
        0 when each case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        """Record one case."""
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    def row(name: str, origin: str, flags: str = "invariant", verdict: str = "DROP") -> str:
        """Return one row with plain fields."""
        return f"{name} | {origin} | {flags} | yes | yes | 1 total | 0 | {verdict} | a reason\n"

    candidate_rows = "".join(row(name, "candidate") for name in sorted(CANDIDATES))
    candidate_sections = "".join(f"// name: {name}\nstatic_assert(true);\n" for name in sorted(CANDIDATES))
    with tempfile.TemporaryDirectory(prefix="admitted-audit-") as work:
        root = Path(work)
        (root / "list.txt").write_text("# a comment\nadmit std::move | a cast\nadmit <meta> | reflection\n"
                                       "quarantine test/\n", encoding="utf-8")

        def run(table: str, matrix: str) -> list[str]:
            """Evaluate one table and one matrix, and return the messages."""
            (root / "table.txt").write_text(table, encoding="utf-8")
            (root / "matrix.cpp").write_text(matrix, encoding="utf-8")
            found = evaluate(root, Path("list.txt"), Path("table.txt"), Path("matrix.cpp"))
            return [finding.text() for finding in found]

        clean_table = (row("std::move", "admitted", verdict="KEEP-RESTRICTED: one argument")
                       + row("<meta>", "admitted", flags="varies: -D_GLIBCXX_DEBUG breaks it", verdict="KEEP")
                       + candidate_rows)
        clean_matrix = ("// name: std::move\nstatic_assert(true);\n"
                        "// name: <meta>\n// a note\n// varies: _GLIBCXX_DEBUG\nstatic_assert(true);\n"
                        + candidate_sections)
        clean = run(clean_table, clean_matrix)
        expect("a clean tree gives no finding", clean == [])
        expect("an admitted entry with no row is an error",
               any("the admitted entry std::move has no row" in text
                   for text in run(clean_table.replace(row("std::move", "admitted",
                                                           verdict="KEEP-RESTRICTED: one argument"), ""),
                                   clean_matrix)))
        expect("a row of a name that is no entry and no candidate is an error",
               any("std::swap is no admitted entry and no candidate" in text
                   for text in run(clean_table + row("std::swap", "candidate"),
                                   clean_matrix + "// name: std::swap\nstatic_assert(true);\n")))
        expect("an `admitted` row of a name that the list does not hold is an error",
               any("says `admitted`" in text
                   for text in run(clean_table + row("std::forward", "admitted"),
                                   clean_matrix + "// name: std::forward\nstatic_assert(true);\n")))
        expect("a candidate with no row is an error",
               any("the candidate std::byte of appendix A has no row" in text
                   for text in run(clean_table.replace(row("std::byte", "candidate"), ""), clean_matrix)))
        expect("a row with no section is an error",
               any("std::move has no section" in text
                   for text in run(clean_table, clean_matrix.replace("// name: std::move\n", ""))))
        expect("a section with no row is an error",
               any("the section of std::declval has no row" in text
                   for text in run(clean_table, clean_matrix + "// name: std::declval\nstatic_assert(true);\n")))
        expect("a `varies` section whose row says invariant is an error",
               any("does not record it" in text
                   for text in run(clean_table.replace("varies: -D_GLIBCXX_DEBUG breaks it", "invariant"),
                                   clean_matrix)))
        expect("a row with eight fields is an error",
               any("this row has 8" in text
                   for text in run(clean_table + "std::byte | candidate | invariant | yes | yes | 1 | 0 | KEEP\n",
                                   clean_matrix)))
        expect("a REPLACE that names no family of the plan is an error",
               any("names the family Vector" in text
                   for text in run(clean_table.replace(row("std::size_t", "candidate"),
                                                       row("std::size_t", "candidate", verdict="REPLACE: Vector")),
                                   clean_matrix)))
        expect("a verdict outside the four forms is an error",
               any("the VERDICT of std::nullptr_t" in text
                   for text in run(clean_table.replace(row("std::nullptr_t", "candidate"),
                                                       row("std::nullptr_t", "candidate", verdict="MAYBE")),
                                   clean_matrix)))
        expect("a KEEP-RESTRICTED with no restriction is an error",
               any("the VERDICT of std::byte" in text
                   for text in run(clean_table.replace(row("std::byte", "candidate"),
                                                       row("std::byte", "candidate", verdict="KEEP-RESTRICTED:")),
                                   clean_matrix)))
        expect("a second row of one name is an error",
               any("std::byte has a second row" in text
                   for text in run(clean_table + row("std::byte", "candidate"), clean_matrix)))
        expect("a second section of one name is an error",
               any("std::byte has a second section" in text
                   for text in run(clean_table, clean_matrix + "// name: std::byte\nstatic_assert(true);\n")))
        clean_list = (root / "list.txt").read_text(encoding="utf-8")

        def run_list(listed: str, table: str, matrix: str = clean_matrix) -> list[str]:
            """Evaluate one rule table with one audit table, and give the clean rule table back."""
            (root / "list.txt").write_text(listed, encoding="utf-8")
            try:
                return run(table, matrix)
            finally:
                (root / "list.txt").write_text(clean_list, encoding="utf-8")

        expect("a rule table that does not obey its format is an error",
               any("does not obey its format" in text for text in run_list("admit std::move\n", clean_table)))
        expect("a KEEP verdict with no admit row is an error",
               any("verdict of <meta> is KEEP" in text
                   for text in run_list(clean_list.replace("admit <meta> | reflection\n", ""), clean_table)))
        expect("a KEEP-RESTRICTED verdict with no admit row is an error",
               any("verdict of std::move is KEEP-RESTRICTED" in text
                   for text in run_list(clean_list.replace("admit std::move | a cast\n", ""), clean_table)))
        expect("a DROP verdict with an admit row is an error",
               any("verdict of std::byte is DROP" in text
                   for text in run_list(clean_list + "admit std::byte | a type\n", clean_table)))
        expect("a REPLACE verdict with no row of its family is an error",
               any("has no row `admit std::move until Scalar`" in text
                   for text in run(clean_table.replace("KEEP-RESTRICTED: one argument", "REPLACE: Scalar"),
                                   clean_matrix)))
        expect("a REPLACE verdict with the row of its family gives no finding",
               run_list(clean_list.replace("admit std::move |", "admit std::move until Scalar |"),
                        clean_table.replace("KEEP-RESTRICTED: one argument", "REPLACE: Scalar")) == [])
        expect("a row with `until` and a verdict that is not REPLACE is an error",
               any("until Report, and the audit verdict" in text
                   for text in run_list(clean_list.replace("admit <meta> |", "admit <meta> until Report |"),
                                        clean_table)))
        dropped_table = clean_table.replace(row("std::move", "admitted", verdict="KEEP-RESTRICTED: one argument"),
                                            row("std::move", "dropped"))
        expect("a dropped entry with no admit row gives no finding",
               run_list(clean_list.replace("admit std::move | a cast\n", ""), dropped_table) == [])
        expect("a dropped entry that the rule table still admits is an error",
               any("says `dropped`" in text for text in run(dropped_table, clean_matrix)))
        expect("a dropped row whose verdict is not DROP is an error",
               any("is `dropped`, and its verdict is not DROP" in text
                   for text in run_list(clean_list.replace("admit std::move | a cast\n", ""),
                                        dropped_table.replace(row("std::move", "dropped"),
                                                              row("std::move", "dropped", verdict="KEEP")))))
    if failures:
        print(f"check-admitted-audit --self-test: FAILED, {len(failures)} case(s)", file=sys.stderr)
        return 2
    print("check-admitted-audit --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or its self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    parser = argparse.ArgumentParser(prog="check-admitted-audit.py", add_help=True)
    parser.add_argument("--self-test", action="store_true", help="plant each defect and examine each verdict")
    check_report.add_arguments(parser)
    try:
        options = parser.parse_args(argv)
    except SystemExit as stop:
        return 0 if stop.code == 0 else 2
    try:
        if options.self_test:
            return self_test()
        return check_report.emit(evaluate(tsast.REPO_ROOT), CHECK, options.warnings_dir)
    except tsast.KitMissing as exc:
        print(f"check-admitted-audit: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
