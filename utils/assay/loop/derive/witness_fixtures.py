"""loop derive witness-fixtures — write the negative fixture of each witness row of the rule table, or compare them.

usage: loop derive witness-fixtures --write
       loop derive witness-fixtures --check

THE FIXTURES
    Each witness row of utils/rule-table.txt (assay.core.ruletable) names a
    class of the base that attests to a fact that it cannot see.  The row also
    gives the header of the class, the refusal of the compiler and the
    expression of a forger.  For each row, this module writes the fixture
    test/fixy/neg/neg_witness_SLUG_direct_construction.cpp.  The fixture
    tries the expression, and the compiler must refuse it.  The slug is the
    full name of the class with no root namespace fixy or foundation.  It is
    in lowercase, with one underscore for each run of other characters.

    The module also writes the manifest test/fixy/neg/witness-fixtures.json.
    The manifest holds one record for each fixture, with the name of its test
    and its two diagnostics.  It also holds one record for each row, with the
    type and the header.  test/fixy/CMakeLists.txt reads the manifest with
    string(JSON).  It registers one negative compile test for each fixture
    record, and it writes witness_roster.h from the records of the rows.

THE DIAGNOSTICS
    A fixture spells its construction with curly brackets, `T{args}`.  GCC
    writes the constructor that it refuses with parentheses,
    `T(args) [with ...]`.  The pattern `\\bT\\(` then matches a text of the
    compiler, and not the text of the fixture.  It is the second diagnostic
    of each fixture.  The first diagnostic is the text of the refusal of the
    row.

THE MODES
    --write   write each fixture and the manifest, and remove each fixture of
              no row.  A file changes only when its bytes change.
    --check   compare each fixture and the manifest with the rows, and write
              nothing.

    Each mode refuses a row with a header that is not a file of include/, and
    two rows with one slug.  --check also refuses a fixture that is different
    or missing, a fixture of no row and a manifest that is different.

Exit 0 when the mode did its work, and 1 when a check finds an error.  Exit 2
on a usage error, and 3 when the rule table does not obey its format.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from assay.core import report as check_report
from assay.core import ruletable
from assay.core.exit import ExitCode
from assay.core.paths import REPO_ROOT, listing

PROGRAM = "loop derive witness-fixtures"
# The rule table, the directory of the fixtures and the include directory of a tree, relative to its root.
TABLE = ruletable.TABLE.relative_to(REPO_ROOT)
FIXTURE_DIR = Path("test") / "fixy" / "neg"
INCLUDE = Path("include")
FIXTURE_PREFIX = "neg_witness_"
FIXTURE_SUFFIX = "_direct_construction.cpp"
MANIFEST_NAME = "witness-fixtures.json"
WRITER = "loop derive witness-fixtures --write"
# The text of each refusal of a witness row, as GCC writes it.
REFUSAL_TEXTS = {
    "private": "is private within this context",
    "deleted": "use of deleted function",
    "no-match": "no matching function for call",
}
# The root namespaces that a slug drops.
ROOT_NAMESPACES = ("fixy::", "foundation::")
# The brand that an expression can name.  A branded type with no brand is the erased identity, which the rule
# erased-brand counts.  An expression that names one brand at two positions names this brand.  The brand is empty,
# and foundation::brand::IsBrand admits it.  Only a fixture with an expression that names it holds the declaration.
BRAND_DECLARATION = """
// The brand that the expression names.  It is empty, which is all that
// foundation::brand::IsBrand asks of a brand.
struct brand {};
"""


@dataclass(frozen=True, slots=True)
class Fixture:
    """One fixture: its file name and its text."""

    name: str
    text: str


@dataclass(frozen=True, slots=True)
class Derivation:
    """The files that the rows of a table give, and the errors of the rows."""

    fixtures: tuple[Fixture, ...]
    manifest: str
    errors: tuple[str, ...]


def key_of(name: str) -> str:
    """Return the full name of a class with no root namespace fixy or foundation."""
    for root in ROOT_NAMESPACES:
        if name.startswith(root):
            return name.removeprefix(root)
    return name


def slug_of(name: str) -> str:
    """Return the slug of a class name (THE FIXTURES).  Complexity: linear in the length of NAME."""
    parts: list[str] = []
    is_run = False
    for character in key_of(name).lower():
        if character.isascii() and character.isalnum():
            parts.append(character)
            is_run = False
        elif not is_run:
            parts.append("_")
            is_run = True
    return "".join(parts).strip("_")


def fixture_name(witness: ruletable.Witness) -> str:
    """Return the file name of the fixture of a witness row."""
    return f"{FIXTURE_PREFIX}{slug_of(witness.name)}{FIXTURE_SUFFIX}"


def base_name(name: str) -> str:
    """Return the last name of the full name of a class, which the compiler writes before `(` when it refuses."""
    return name.rsplit("::", 1)[-1]


def render(witness: ruletable.Witness) -> str:
    """Return the text of the fixture of a witness row."""
    brand = BRAND_DECLARATION if "forge::brand" in witness.expression else ""
    return f"""// Written by `{WRITER}` from the witness row of
// {witness.type_} in utils/rule-table.txt.  Edit the row, not this file.
// clang-format off: the writer owns this layout, and --check compares it byte for byte.
//
// {witness.type_} attests to a fact that it cannot see.  The expression below is
// the raw data that a forger gives its constructor, and the fixture keeps the door
// shut: the compiler must refuse the construction with "{REFUSAL_TEXTS[witness.refusal]}".
//
// The second diagnostic is the constructor as the compiler writes it, with
// parentheses.  The source spells it with braces, so the text of the fixture
// cannot match it.

#include {witness.header}

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace forge {{
// Stand-ins for the values that a forger must give.  Neither returns: the
// fixture compiles, and it never runs.
template <class T> [[gnu::noinline]] T& lvalue() noexcept {{ std::abort(); }}
template <class T> [[gnu::noinline]] T&& rvalue() noexcept {{ std::abort(); }}
{brand}}}  // namespace forge

int main() {{
    [[maybe_unused]] auto forged = {witness.expression};
    return 0;
}}
"""


def manifest_text(witnesses: tuple[ruletable.Witness, ...]) -> str:
    """Return the manifest of the rows (THE FIXTURES): the fixture records sorted by name, and then each row."""
    fixtures = sorted(
        (
            {
                "name": fixture_name(witness).removesuffix(".cpp"),
                "reason": REFUSAL_TEXTS[witness.refusal],
                "constructor": f"\\b{base_name(witness.name)}\\(",
            }
            for witness in witnesses
        ),
        key=lambda record: record["name"],
    )
    roster = [{"type": witness.type_, "header": witness.header[1:-1]} for witness in witnesses]
    return json.dumps({"writer": WRITER, "fixtures": fixtures, "roster": roster}, indent=2) + "\n"


def derive(root: Path) -> Derivation:
    """Read the witness rows of the table of a tree, and give their files and their errors.

    Complexity: linear in the number of rows.

    Raises:
        ruletable.TableError: When the table does not obey its format
        OSError: When the table cannot be read
    """
    witnesses = ruletable.load(root / TABLE).witnesses
    errors: list[str] = []
    fixtures: list[Fixture] = []
    lines: dict[str, int] = {}  # the name of each fixture, and the line of its row
    for witness in witnesses:
        where = f"{TABLE}:{witness.line}"
        if not (root / INCLUDE / witness.header[1:-1]).is_file():
            errors.append(f"{where}: the header {witness.header} of {witness.name} is not a file of include/")
        name = fixture_name(witness)
        if name in lines:
            errors.append(
                f"{where}: the class {witness.name} gives the fixture {name}, and the row on line {lines[name]} "
                f"gives it too.  Give one of the two classes a different name"
            )
        lines[name] = witness.line
        fixtures.append(Fixture(name, render(witness)))
    return Derivation(tuple(fixtures), manifest_text(witnesses), tuple(errors))


def store(path: Path, text: str) -> bool:
    """Write TEXT to PATH only when the bytes of PATH are different.  Return True when the function writes the file."""
    if path.is_file() and path.read_text(encoding="utf-8") == text:
        return False
    path.write_text(text, encoding="utf-8")
    return True


def report_errors(errors: tuple[str, ...] | list[str]) -> None:
    """Write each error and the count line."""
    for error in errors:
        check_report.write_line(error, is_error=True)
    check_report.write_line(f"{PROGRAM}: {len(errors)} error(s).", is_error=True)


def written(root: Path) -> ExitCode:
    """Write each fixture and the manifest, and remove each fixture of no row (THE MODES)."""
    derivation = derive(root)
    if derivation.errors:
        report_errors(derivation.errors)
        return ExitCode.FINDINGS
    directory = root / FIXTURE_DIR
    directory.mkdir(parents=True, exist_ok=True)
    changed = sum(store(directory / fixture.name, fixture.text) for fixture in derivation.fixtures)
    names = {fixture.name for fixture in derivation.fixtures}
    for stale in listing(directory, f"{FIXTURE_PREFIX}*{FIXTURE_SUFFIX}"):
        if stale.name not in names:
            stale.unlink()
            changed += 1
    changed += store(directory / MANIFEST_NAME, derivation.manifest)
    check_report.write_line(
        f"{PROGRAM}: {len(derivation.fixtures)} fixture(s) and {MANIFEST_NAME} in {FIXTURE_DIR}, {changed} file(s) "
        f"changed.",
        is_error=True,
    )
    return ExitCode.OK


def checked(root: Path) -> ExitCode:
    """Compare each fixture and the manifest with the rows, and write nothing (THE MODES)."""
    derivation = derive(root)
    errors = list(derivation.errors)
    directory = root / FIXTURE_DIR
    repair = f"Run `python3 utils/bin/{WRITER}`."
    for fixture in derivation.fixtures:
        path = directory / fixture.name
        if not path.is_file():
            errors.append(f"{FIXTURE_DIR / fixture.name}: the fixture of a witness row is missing.  {repair}")
        elif path.read_text(encoding="utf-8") != fixture.text:
            errors.append(f"{FIXTURE_DIR / fixture.name}: the fixture is different from the text of its row.  {repair}")
    names = {fixture.name for fixture in derivation.fixtures}
    for stray in listing(directory, f"{FIXTURE_PREFIX}*{FIXTURE_SUFFIX}"):
        if stray.name not in names:
            errors.append(f"{FIXTURE_DIR / stray.name}: the fixture has no witness row.  {repair}")
    manifest = directory / MANIFEST_NAME
    if not manifest.is_file() or manifest.read_text(encoding="utf-8") != derivation.manifest:
        errors.append(
            f"{FIXTURE_DIR / MANIFEST_NAME}: the manifest is missing or different from the rows, and CMake then "
            f"registers a different set of fixtures.  {repair}"
        )
    if errors:
        report_errors(errors)
        return ExitCode.FINDINGS
    check_report.write_line(f"{PROGRAM}: {len(derivation.fixtures)} fixture(s) agree with their rows.", is_error=True)
    return ExitCode.OK


def main(argv: list[str]) -> int:
    """Run one mode (THE MODES).

    Args:
        argv: The words after `loop derive witness-fixtures`

    Returns:
        The exit code
    """
    from assay.core import cli

    parser = cli.parser(PROGRAM, "write the negative fixture of each witness row of the rule table, or compare them")
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--write", action="store_true", help="write each fixture and the manifest")
    modes.add_argument("--check", action="store_true", help="compare each fixture and the manifest with the rows")
    try:
        options = parser.parse_args(argv)
    except SystemExit as stop:
        return ExitCode.OK if stop.code == 0 else ExitCode.USAGE
    try:
        return written(REPO_ROOT) if options.write else checked(REPO_ROOT)
    except (ruletable.TableError, OSError) as problem:
        check_report.write_line(f"{PROGRAM}: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
