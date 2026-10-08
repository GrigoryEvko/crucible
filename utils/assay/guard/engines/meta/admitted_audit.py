"""admitted-audit — each audited name has one row of the audit table and one section of the flag matrix.

The quarantine plan (misc/01_10_2026_quarantine.md, section 8.2) audits each
entry of the admitted list and each candidate of its appendix A.  The audit
table gives one row for each name.  The flag matrix gives a static_assert for
each property of each name.  The rule keeps the three sources in step.

THE INPUTS
    utils/rule-table.txt                 the rule table.  Its admit rows are
                                         the admitted list, and
                                         assay.core.ruletable reads them.
    utils/admitted-audit.txt             the audit table.  This module is its
                                         schema module (THE AUDIT TABLE).
    test/layer/admitted_flag_matrix.cpp  the flag matrix.  The rule reads its
                                         comments from the parse tree.

THE AUDIT TABLE
    The table obeys the rows grammar of assay.core.rows.  Each row has the
    name as its one word and eight cells:
        NAME | ORIGIN | FLAGS | PREDICTABLE | SAFE | USES | COST | VERDICT | REASON
    No cell is empty, and no name has two rows.  ORIGIN is admitted,
    candidate or dropped.  FLAGS is invariant, or it starts with "varies:".
    PREDICTABLE and SAFE start with the word yes or the word no.  VERDICT is
    KEEP, DROP, "KEEP-RESTRICTED: RESTRICTION" or "REPLACE: FAMILY".  FAMILY
    is one of the eight families of FAMILIES.

THE FLAG MATRIX
    Each row has one section in the matrix.  A run of comments "// name: NAME"
    on lines one after the other starts one section for each name.  A comment
    "// varies: MACRO" belongs to the section of the last run.  The row of
    such a section has FLAGS that start with "varies:" and name the macro.

THE KINDS
    table      the audit table or the rule table does not obey its schema.
               Examples are a row with no nine fields, an empty cell, a second
               row of one name and a cell with an incorrect shape
    list       the admitted list does not apply the audit.  A row is admitted
               when its name is an entry of the list, and each entry has a
               row.  A dropped row has the verdict DROP and no entry.  A KEEP
               and a KEEP-RESTRICTED verdict have an entry.  A DROP verdict
               has none.  "REPLACE: FAMILY" has the entry "admit NAME until
               FAMILY", and each entry with until has the verdict REPLACE of
               that family.
    candidate  a candidate of CANDIDATES with no row, or a row with the ORIGIN
               candidate that names no candidate
    matrix     a row with no section, a section with no row, a second section
               of one name, a comment "// varies:" before the first section,
               and a macro of a section that FLAGS does not name
    The entity of a finding is the name of the row or of the section, or "-".

THE PLANTS
    The control control-clean holds a full tree: the two admitted entries,
    each candidate and a section of each.  The copy of each other plant
    starts with that tree.  A file of the plant then replaces its file in
    the tree.  A file NAME.append adds its lines at the end of NAME,
    and a file NAME.prepend adds them at the start (runner.copy_overlay()).

Complexity: linear in the rows of the two tables and in the comments of the matrix.
"""

from dataclasses import dataclass, replace
from pathlib import Path

from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import ruletable
from assay.guard.engines.meta.runner import MetaContext, copy_overlay
from assay.readers import tsast

RULE = "admitted-audit"
# The control that gives the first tree of each plant (THE PLANTS).
BASE = "control-clean"
ADMITTED = "utils/rule-table.txt"
TABLE = "utils/admitted-audit.txt"
MATRIX = "test/layer/admitted_flag_matrix.cpp"
FIELDS = ("NAME", "ORIGIN", "FLAGS", "PREDICTABLE", "SAFE", "USES", "COST", "VERDICT", "REASON")
ORIGINS = frozenset({"admitted", "candidate", "dropped"})
FAMILIES = frozenset({"Choice", "Record", "Region", "Ref", "Atomic", "Scalar", "Report", "Os"})
# The candidates of appendix A of the plan.  The appendix names the integer spellings and the is_eq family as
# groups, and this set gives each name of each group.
CANDIDATES = frozenset(
    {
        "std::initializer_list",
        "std::strong_ordering",
        "std::weak_ordering",
        "std::partial_ordering",
        "std::is_eq",
        "std::is_neq",
        "std::is_lt",
        "std::is_lteq",
        "std::is_gt",
        "std::is_gteq",
        "std::byte",
        "std::size_t",
        "std::ptrdiff_t",
        "std::int8_t",
        "std::int16_t",
        "std::int32_t",
        "std::int64_t",
        "std::uint8_t",
        "std::uint16_t",
        "std::uint32_t",
        "std::uint64_t",
        "std::intptr_t",
        "std::uintptr_t",
        "std::intmax_t",
        "std::uintmax_t",
        "std::int_least8_t",
        "std::int_least16_t",
        "std::int_least32_t",
        "std::int_least64_t",
        "std::uint_least8_t",
        "std::uint_least16_t",
        "std::uint_least32_t",
        "std::uint_least64_t",
        "std::int_fast8_t",
        "std::int_fast16_t",
        "std::int_fast32_t",
        "std::int_fast64_t",
        "std::uint_fast8_t",
        "std::uint_fast16_t",
        "std::uint_fast32_t",
        "std::uint_fast64_t",
        "std::nullptr_t",
        "std::meta::info",
        "std::source_location",
        "std::contracts::contract_violation",
        "std::align_val_t",
        "std::nothrow_t",
    }
)
INVARIANT = "invariant"
VARIES = "varies:"
KEEP = "KEEP"
DROP = "DROP"
RESTRICTED = "KEEP-RESTRICTED:"
REPLACE = "REPLACE:"
COMMENT_START = "//"
NAME_MARKER = "name:"
VARIES_MARKER = "varies:"
NO_ENTITY = "-"


@dataclass(frozen=True, slots=True)
class AuditRow:
    """One row of the audit table (THE AUDIT TABLE): its line, its name and its eight cells after the name."""

    line: int
    name: str
    origin: str
    flags: str
    predictable: str
    safe: str
    uses: str
    cost: str
    verdict: str
    reason: str


@dataclass(frozen=True, slots=True)
class Section:
    """One section of the flag matrix: the line of its marker and the macros of its comments "// varies:"."""

    line: int
    varies: frozenset[str] = frozenset()


def error(path: str, line: int, kind: str, entity: str, message: str) -> check_report.Finding:
    """Return one error of the rule."""
    return check_report.Finding("error", path, line, RULE, message, kind, entity)


def starts_with_word(text: str, word: str) -> bool:
    """Return True when a text starts with a word that no letter, digit or underscore continues."""
    rest = text[len(word) :]
    return text.startswith(word) and not (rest[:1].isalnum() or rest[:1] == "_")


def family_of(verdict: str) -> str | None:
    """Return the family of a verdict "REPLACE: FAMILY ...", or None for each other verdict.

    The family is the run of letters after the colon, and the next character is not a letter, a digit or an
    underscore.
    """
    if not verdict.startswith(REPLACE):
        return None
    rest = verdict[len(REPLACE) :].lstrip()
    end = 0
    while end < len(rest) and rest[end].isascii() and rest[end].isalpha():
        end += 1
    if end == 0 or rest[end : end + 1].isalnum() or rest[end : end + 1] == "_":
        return None
    return rest[:end]


def is_verdict(verdict: str) -> bool:
    """Return True for a verdict of one of the four shapes of THE AUDIT TABLE."""
    if verdict in (KEEP, DROP):
        return True
    if verdict.startswith(RESTRICTED):
        return bool(verdict[len(RESTRICTED) :].strip())
    return family_of(verdict) is not None


def is_kept(verdict: str) -> bool:
    """Return True for the verdict KEEP or KEEP-RESTRICTED."""
    return verdict == KEEP or verdict.startswith(RESTRICTED)


def read_table(root: Path, table: str) -> tuple[dict[str, AuditRow] | None, list[check_report.Finding]]:
    """Read the audit table, and give each row by its name with the findings of the shape of the rows.

    The rows are None when the table does not obey the rows grammar.  Then the one finding of the table is the
    only finding of the rule that reads a row.

    Complexity: linear in the rows of the table.
    """
    try:
        rows = rows_grammar.read(root / table, table)
    except (rows_grammar.RowsError, OSError) as problem:
        line = problem.line if isinstance(problem, rows_grammar.RowsError) else 0
        return None, [error(table, line, "table", NO_ENTITY, f"the audit table cannot be read: {problem}.  Correct it")]
    found: list[check_report.Finding] = []
    audited: dict[str, AuditRow] = {}
    for row in rows:
        if len(row.words) != 1 or len(row.cells) != len(FIELDS) - 1:
            message = (
                f"a row has {len(FIELDS)} fields ({' | '.join(FIELDS)}), and this row has "
                f"{len(row.words) + len(row.cells)}.  Correct the row"
            )
            found.append(error(table, row.line, "table", row.words[0], message))
            continue
        name = row.words[0]
        empty = [label for label, cell in zip(FIELDS[1:], row.cells, strict=True) if not cell]
        if empty:
            message = f"the row gives no {', '.join(empty)}.  Give each field"
            found.append(error(table, row.line, "table", name, message))
            continue
        if name in audited:
            message = f"{name} has a second row.  The first is at line {audited[name].line}.  Keep one row"
            found.append(error(table, row.line, "table", name, message))
            continue
        audited[name] = AuditRow(row.line, name, *row.cells)
        found += shape_findings(table, audited[name])
    return audited, found


def shape_findings(table: str, row: AuditRow) -> list[check_report.Finding]:
    """Return the findings of the cells of one row that do not have their shape (THE AUDIT TABLE)."""
    found: list[check_report.Finding] = []
    if row.origin not in ORIGINS:
        message = f"the ORIGIN of {row.name} is `{row.origin}`.  It is `admitted`, `candidate` or `dropped`"
        found.append(error(table, row.line, "table", row.name, message))
    elif row.origin == "dropped" and row.verdict != DROP:
        message = (
            f"the ORIGIN of {row.name} is `dropped`, and its verdict is not DROP.  Correct the ORIGIN or the verdict"
        )
        found.append(error(table, row.line, "table", row.name, message))
    if row.flags != INVARIANT and not row.flags.startswith(VARIES):
        message = f"the FLAGS of {row.name} is `invariant` or starts with `varies:`.  Correct the FLAGS"
        found.append(error(table, row.line, "table", row.name, message))
    for label, cell in (("PREDICTABLE", row.predictable), ("SAFE", row.safe)):
        if not (starts_with_word(cell, "yes") or starts_with_word(cell, "no")):
            message = f"the {label} of {row.name} starts with `yes` or `no`.  Correct the cell"
            found.append(error(table, row.line, "table", row.name, message))
    family = family_of(row.verdict)
    if not is_verdict(row.verdict):
        message = (
            f"the VERDICT of {row.name} is KEEP, DROP, `KEEP-RESTRICTED: RESTRICTION` or `REPLACE: FAMILY`.  Correct "
            f"the verdict"
        )
        found.append(error(table, row.line, "table", row.name, message))
    elif family is not None and family not in FAMILIES:
        message = (
            f"the VERDICT of {row.name} names the family {family}.  The families are {', '.join(sorted(FAMILIES))}"
        )
        found.append(error(table, row.line, "table", row.name, message))
    return found


def read_admitted(root: Path, admitted: str) -> tuple[dict[str, ruletable.Admit], list[check_report.Finding]]:
    """Read the admit rows of the rule table by their entries, or give one finding when the table cannot be read."""
    try:
        table = ruletable.load(root / admitted)
    except (ruletable.TableError, OSError) as problem:
        message = f"the rule table does not obey its schema: {problem}.  Correct the row"
        return {}, [error(admitted, 0, "table", NO_ENTITY, message)]
    return {admit.entry: admit for admit in table.admits}, []


def read_matrix(root: Path, matrix: str) -> tuple[dict[str, Section], list[check_report.Finding]]:
    """Read the sections of the flag matrix from the comments of its parse tree (THE FLAG MATRIX).

    Complexity: linear in the comments of the matrix.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    sections: dict[str, Section] = {}
    found: list[check_report.Finding] = []
    current: list[str] = []
    last_name_line = -2
    for tree in tsast.parse_files(root, [matrix]):
        for comment in tree.find("comment"):
            words = tsast.prose_text(comment)
            line = comment.line
            body = words[len(COMMENT_START) :].strip() if words.startswith(COMMENT_START) else ""
            if body.startswith(NAME_MARKER) and body[len(NAME_MARKER) :].strip():
                if line != last_name_line + 1:
                    current = []
                last_name_line = line
                name = body[len(NAME_MARKER) :].strip()
                if name in sections:
                    message = f"{name} has a second section.  The first is at line {sections[name].line}.  Keep one"
                    found.append(error(matrix, line, "matrix", name, message))
                    continue
                sections[name] = Section(line)
                current.append(name)
                continue
            macro = body[len(VARIES_MARKER) :].strip() if body.startswith(VARIES_MARKER) else ""
            if macro.isascii() and macro.isidentifier():
                if not current:
                    message = (
                        "a `// varies:` comment comes before the first section.  Put it in the section of its name"
                    )
                    found.append(error(matrix, line, "matrix", NO_ENTITY, message))
                for name in current:
                    sections[name] = replace(sections[name], varies=sections[name].varies | {macro})
    return sections, found


def list_findings(
    admitted: str, table: str, rows: dict[str, AuditRow], entries: dict[str, ruletable.Admit]
) -> list[check_report.Finding]:
    """Return the findings of the kind list: the admitted list does not apply the audit (THE KINDS).

    Complexity: linear in the rows and the entries.
    """
    found: list[check_report.Finding] = []
    for listed, admit in entries.items():
        if listed not in rows:
            message = f"the admitted entry {listed} has no row in {table}.  Audit the entry and add its row"
            found.append(error(admitted, admit.line, "list", listed, message))
    for name, row in rows.items():
        entry = entries.get(name)
        if row.origin == "admitted" and entry is None:
            message = f"the row of {name} says `admitted`, and {admitted} has no such entry.  Correct the ORIGIN"
            found.append(error(table, row.line, "list", name, message))
        if row.origin == "dropped" and entry is not None:
            message = f"the row of {name} says `dropped`, and {admitted} holds the name.  Remove the admit row"
            found.append(error(table, row.line, "list", name, message))
        if row.origin == "candidate" and entry is not None:
            message = f"the row of {name} says `candidate`, and {admitted} holds the name.  Write `admitted`"
            found.append(error(table, row.line, "list", name, message))
        if not is_verdict(row.verdict):
            continue
        family = family_of(row.verdict)
        if is_kept(row.verdict) and entry is None:
            message = (
                f"the verdict of {name} is {row.verdict.split(':')[0]}, and {admitted} has no admit row for it.  Add "
                f"`admit {name} | REASON`, with the words of the restriction"
            )
            found.append(error(table, row.line, "list", name, message))
        elif row.verdict == DROP and entry is not None:
            message = f"the verdict of {name} is DROP, and the row admits it.  Remove the row"
            found.append(error(admitted, entry.line, "list", name, message))
        elif family is not None and (entry is None or entry.until != family):
            message = (
                f"the verdict of {name} is REPLACE: {family}, and {admitted} has no row `admit {name} until "
                f"{family}`.  Write that row"
            )
            found.append(error(table, row.line, "list", name, message))
    for listed, admit in entries.items():
        row_of_entry = rows.get(listed)
        family = family_of(row_of_entry.verdict) if row_of_entry is not None else None
        if admit.until and family != admit.until:
            message = (
                f"the row admits {listed} until {admit.until}, and the audit verdict of {listed} is not REPLACE: "
                f"{admit.until}.  Correct the row or the verdict"
            )
            found.append(error(admitted, admit.line, "list", listed, message))
    return found


def matrix_findings(
    table: str, matrix: str, rows: dict[str, AuditRow], sections: dict[str, Section]
) -> list[check_report.Finding]:
    """Return the findings of the kind matrix that compare the rows and the sections (THE FLAG MATRIX)."""
    found: list[check_report.Finding] = []
    for name, row in rows.items():
        section = sections.get(name)
        if section is None:
            message = (
                f"{name} has no section in {matrix}.  Add a section with the comment `// name: {name}` and a "
                f"static_assert for each property"
            )
            found.append(error(table, row.line, "matrix", name, message))
            continue
        for macro in sorted(section.varies):
            if not row.flags.startswith(VARIES) or macro not in row.flags:
                message = (
                    f"the section of {name} in {matrix} has the comment `// varies: {macro}`, and the FLAGS of the row "
                    f"does not record it.  Write `varies:` and name {macro}"
                )
                found.append(error(table, row.line, "matrix", name, message))
    for name, section in sections.items():
        if name not in rows:
            message = f"the section of {name} has no row in {table}.  Add the row, or remove the section"
            found.append(error(matrix, section.line, "matrix", name, message))
    return found


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each row, entry and section that the three sources do not keep in step (THE KINDS).

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    root = context.root
    entries, found = read_admitted(root, ADMITTED)
    rows, table_findings = read_table(root, TABLE)
    sections, section_findings = read_matrix(root, MATRIX)
    found += table_findings + section_findings
    if rows is None:
        return found
    found += list_findings(ADMITTED, TABLE, rows, entries)
    for candidate in sorted(CANDIDATES - rows.keys()):
        message = f"the candidate {candidate} of appendix A has no row.  Audit the candidate and add its row"
        found.append(error(TABLE, 0, "candidate", candidate, message))
    for name, row in rows.items():
        if row.origin == "candidate" and name not in entries and name not in CANDIDATES:
            message = (
                f"{name} is no admitted entry and no candidate of appendix A.  The audit examines those names only.  "
                f"Remove the row"
            )
            found.append(error(TABLE, row.line, "candidate", name, message))
    return found + matrix_findings(TABLE, MATRIX, rows, sections)


def plant_context(plant: Path, scratch: Path) -> MetaContext:
    """Copy the tree of the base control and then the files of a plant to a throwaway repository (THE PLANTS).

    Complexity: linear in the bytes of the files of the base and of the plant.
    """
    return copy_overlay(plant, scratch, BASE)
