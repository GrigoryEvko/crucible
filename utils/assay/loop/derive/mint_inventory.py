"""loop derive mint-inventory — write misc/mint-inventory.md, the list of each section XXI mint, or compare it.

usage: loop derive mint-inventory --write [--build-dir DIR]
       loop derive mint-inventory --stdout [--build-dir DIR]
       loop derive mint-inventory --check [--build-dir DIR]

THE MINTS
    The quarantine plugin writes a record of each mint into the objects of a
    build (utils/tools/quarantine/declaration.cpp, THE RULE mint-pattern).
    assay.guard.engines.plugin.declarations reads the records in the scope of
    the rule mint-pattern.  The rule mint-pattern, the rule mint-floor and the
    inventory read the same records.  DIR is the build directory, with
    REPO_ROOT/build as the default.

THE ROWS
    One row is one function: an overload is a row of its own, and a
    declaration and its definition are one row at the definition.  A section
    is a layer root and its first directory.  The inventory holds no line
    numbers, because an edit above a mint moves its line.

THE CARVE-OUTS
    A row of utils/ledgers/mint-pattern.txt with the kind mint-not-constexpr
    admits a mint that allocates or reads runtime state, and the inventory
    shows "- (alloc)".  A row with the kind mint-unconstrained admits a mint
    with a gate on values that no concept can see, and the inventory shows
    "- (pre)".

THE FIXTURES
    The HS14 count of a row is the number of negative-compile fixtures of its
    own layer that name the mint.  Two layers can declare mints of one name,
    and a fixture of one layer says nothing about a gate of the other.  A
    fixture directory is a directory in test/ with the name neg or a name
    that ends with _neg.  The fixtures of test/fixy/ and test/foundation/ are
    the fixtures of the substrate layer, include/foundation/ and
    include/fixy/.  Each other fixture is a fixture of the crucible layer.  A
    fixture does not compile, and the parse trees of assay.readers.tsast give
    its names.  More than one class of a layer can have a member mint of one
    name.  Then a fixture counts for the mint of one class only when the
    fixture also names that class or an alias of the class
    (declarations.aliases()).

THE CHECK
    --check compares misc/mint-inventory.md with the rows of the build.  The
    rule derived-files (assay.guard.engines.meta.derived_files) reads the
    same finding (drift()), and the rule mint-floor reads the same rows.

Exit 0 when the mode did its work, 1 on drift and 2 on a usage error.  Exit 3
when the build or the pinned tree-sitter kit is not available.
"""

from __future__ import annotations

from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import ruletable
from assay.core.exit import ExitCode
from assay.core.paths import REPO_ROOT
from assay.guard.engines.plugin import declarations, runner
from assay.readers import tsast

PROGRAM = "loop derive mint-inventory"
# The rule of the registry that reads the findings of drift().
CHECK = "derived-files"
INVENTORY = "misc/mint-inventory.md"
DEFAULT_BUILD = "build"
# The HS14 floor (CLAUDE.md section XVIII): two negative-compile fixtures for each mint.
HS14_FLOOR = 2
# The cell of an axis that does not apply to the row.  "-" is a shortfall.
NOT_APPLICABLE = "·"
SUBSTRATE_ROOTS = ("include/foundation/", "include/fixy/")
SUBSTRATE_TESTS = ("fixy", "foundation")
FIXTURE_SUFFIXES = (".h", ".hpp", ".cpp", ".cc")
CRUCIBLE = "crucible"
SUBSTRATE = "substrate"
CARVE_CX = "mint-not-constexpr"
CARVE_RQ = "mint-unconstrained"
WRITER = "python3 utils/bin/loop derive mint-inventory --write"


class InventoryError(Exception):
    """The inventory cannot be made: the build, the records or the ledger of mint-pattern cannot be read."""


@dataclass(frozen=True, slots=True)
class Row:
    """One mint, with the facts that the inventory shows for it (THE ROWS).

    `family` is crucible or substrate, the layer of the site.  `hs14` is the number of fixtures of its layer that
    name the mint (THE FIXTURES).  `is_overloaded` is True when its header has a second mint of the same name.
    """

    mint: declarations.Mint
    group: str
    family: str
    hs14: int
    is_overloaded: bool
    is_cx_carved: bool
    is_rq_carved: bool

    @property
    def name(self) -> str:
        """Return `Class::name` for a member mint, or the bare name."""
        return (
            f"{self.mint.class_name}::{self.mint.name}" if self.mint.owner != declarations.NO_OWNER else self.mint.name
        )

    @property
    def label(self) -> str:
        """Return the name, with the parameter types when the header overloads the mint."""
        if not self.is_overloaded:
            return self.name
        return f"{self.name}({self.mint.entity.rpartition(f'{self.mint.name}(')[2]}"


def family_of(path: str) -> str:
    """Return the layer of a header path: substrate in include/foundation/ and include/fixy/, else crucible."""
    return SUBSTRATE if path.startswith(SUBSTRATE_ROOTS) else CRUCIBLE


def group_of(path: str) -> str:
    """Return the section of a header path: a layer root and its first directory, or the root itself."""
    parts = Path(path).parts
    depth = 3 if len(parts) > 3 else 2
    return "/".join(parts[:depth]) + "/"


def fixture_files(root: Path) -> dict[str, list[Path]]:
    """Return the fixture files of each layer, relative to the root and sorted (THE FIXTURES).

    Complexity: linear in the directories in test/.
    """
    found: dict[str, list[Path]] = {CRUCIBLE: [], SUBSTRATE: []}
    test_dir = root / "test"
    if not test_dir.is_dir():
        return found
    for directory in sorted(path for path in test_dir.rglob("*") if path.is_dir()):
        if directory.name != "neg" and not directory.name.endswith("_neg"):
            continue
        relative = directory.relative_to(root)
        family = SUBSTRATE if relative.parts[1] in SUBSTRATE_TESTS else CRUCIBLE
        found[family] += sorted(
            path.relative_to(root) for path in directory.iterdir() if path.suffix in FIXTURE_SUFFIXES
        )
    return {family: sorted(set(paths)) for family, paths in found.items()}


def fixture_names(root: Path, paths: Sequence[Path]) -> list[frozenset[str]]:
    """Return the names that the code of each fixture spells: identifiers, fields, types and namespaces.

    A comment that names a mint is no use of it, and the parse tree holds no identifier in a comment.

    Complexity: one kit run parses each fixture.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    files = [root / path for path in paths]
    kinds = ("identifier", "field_identifier", "type_identifier", "namespace_identifier")
    return [frozenset(tsast.spelled(node) for node in tree.find(*kinds)) for tree in tsast.parse(files, strict=False)]


def fixture_count(
    mint: declarations.Mint, carriers: int, names: Sequence[frozenset[str]], aliases: dict[str, frozenset[str]]
) -> int:
    """Return the number of fixtures that name one mint (THE FIXTURES).

    Complexity: linear in the fixtures.
    """
    if mint.owner != declarations.NO_OWNER and carriers > 1:
        spellings = {mint.class_name} | aliases.get(mint.owner, frozenset())
        return sum(1 for spelled in names if mint.name in spelled and spellings & spelled)
    return sum(1 for spelled in names if mint.name in spelled)


def carved(root: Path, rule: rule_registry.Rule) -> dict[str, frozenset[str]]:
    """Return the entities that each kind of THE CARVE-OUTS admits in the ledger of mint-pattern.

    Raises:
        InventoryError: If the ledger does not obey its grammar
    """
    try:
        ledger = rule_ledger.read(rule, root)
    except (rows_grammar.RowsError, OSError) as problem:
        raise InventoryError(f"the ledger of {rule.id} cannot be read: {problem}") from None
    return {kind: frozenset(key.entity for key in ledger.base if key.kind == kind) for kind in (CARVE_CX, CARVE_RQ)}


def rows_of(context: runner.PluginContext) -> list[Row]:
    """Return one row for each mint of the build of the context, sorted by section, name, path and entity.

    Complexity: linear in the records and the fixtures, and one kit run for the fixtures.

    Raises:
        NotApplicable: If the build does not apply, or the call gives no build directory
        InventoryError: If the build, a record, the rule table or the ledger of mint-pattern cannot be read
        tsast.KitMissing: If the pinned kit is not installed
    """
    try:
        rule = declarations.rule_of(context, declarations.MINT_RULE)
        mints = declarations.mints(context, rule)
        aliases = declarations.aliases(context, rule)
    except (runner.BuildError, declarations.RecordError, ruletable.TableError) as problem:
        raise InventoryError(str(problem)) from None
    carve_outs = carved(context.root, rule)
    files = fixture_files(context.root)
    names = {family: fixture_names(context.root, paths) for family, paths in files.items()}
    carriers: dict[tuple[str, str], set[str]] = {}
    # The mints of one name in one class, or with no class, of one header: the overloads of the name.
    sites: dict[tuple[str, str, str], int] = {}
    for mint in mints:
        carriers.setdefault((family_of(mint.path), mint.name), set()).add(mint.owner)
        site = (mint.path, mint.owner, mint.name)
        sites[site] = sites.get(site, 0) + 1
    rows = [
        Row(
            mint,
            group_of(mint.path),
            family_of(mint.path),
            fixture_count(mint, len(carriers[(family_of(mint.path), mint.name)]), names[family_of(mint.path)], aliases),
            sites[(mint.path, mint.owner, mint.name)] > 1,
            mint.entity in carve_outs[CARVE_CX],
            mint.entity in carve_outs[CARVE_RQ],
        )
        for mint in mints
    ]
    return sorted(rows, key=lambda row: (row.group, row.name, row.mint.path, row.mint.entity))


def cells(row: Row) -> list[str]:
    """Return the cells of one row: mint, site, nd, cx, ne, rq, cb, fit and HS14."""
    mint = row.mint
    if mint.is_constexpr:
        cx = "Y"
    elif mint.is_projection:
        cx = NOT_APPLICABLE
    else:
        cx = "- (alloc)" if row.is_cx_carved else "-"
    if not mint.is_template:
        rq = NOT_APPLICABLE
    elif mint.is_constrained:
        rq = "Y"
    else:
        rq = "- (pre)" if row.is_rq_carved else "-"
    fit = ("Y" if mint.is_gated else "-") if mint.shape == "ctx" else NOT_APPLICABLE
    hs14 = f"HS14: {row.hs14}" + (" ⚠" if row.hs14 < HS14_FLOOR else "")
    return [
        f"`{row.label}`",
        f"`{mint.path}`",
        "Y" if mint.is_nodiscard else "-",
        cx,
        "Y" if mint.is_noexcept else "-",
        rq,
        mint.shape,
        fit,
        hs14,
    ]


HEADER = f"""\
# Mint inventory — auditor snapshot

Generated by `loop derive mint-inventory` from the mint records that the
quarantine plugin writes into the objects of a build
(`utils/tools/quarantine/declaration.cpp`).  The rule mint-pattern and the rule
mint-floor read the same records.  Write it again in the same commit that adds,
removes, moves or renames a `mint_*` factory:

```bash
python3 utils/bin/loop derive mint-inventory --write --build-dir build   # write this file
python3 utils/bin/loop derive mint-inventory --stdout --build-dir build  # print it (read-only)
python3 utils/bin/guard check --rule derived-files --build-dir build     # exit 1 on drift
python3 utils/bin/guard check --rule mint-floor --build-dir build        # a row under the HS14 floor
```

The rule mint-floor reads `utils/ledgers/mint-floor.txt`, which admits the
mints under the floor today.  A new mint with too few fixtures fails the rule,
and a row of the ledger that admits nothing fails it too, so the file only
drains.

The scope is all of `include/`.  One row is one function: an overload is a row
of its own, and a declaration folds into its definition.  A section is a layer
root and its first directory.  The inventory holds no line numbers, because an
edit above a mint moves its line.  A header that overloads a mint names each
overload by its parameter types.

| Column | Meaning |
|---|---|
| `mint` | The factory, with its class for a member, and its parameter types when its header overloads it. |
| `site` | The header of the record with the body of the mint, or else of its first declaration. |
| `nd` | `[[nodiscard]]` on a declaration. |
| `cx` | `constexpr` or `consteval`.  `- (alloc)` is a mint that allocates or reads runtime state, which a row of the kind mint-not-constexpr of `utils/ledgers/mint-pattern.txt` admits.  `{NOT_APPLICABLE}` marks a borrow projection, which returns a view of its own object and is never a constant expression. |
| `ne` | `noexcept`. |
| `rq` | A type-level constraint: a `requires` clause or a concept on a template parameter.  `- (pre)` is a gate on values that a row of the kind mint-unconstrained admits.  `{NOT_APPLICABLE}` marks a mint that is not a template, which cannot have a constraint. |
| `cb` | The authorization shape: `ctx` (the first parameter is `Ctx const&`), `token` (authority from the arguments), or `member` (a non-static method, whose authority is its object).  A static member takes its shape from its parameters. |
| `fit` | For a `ctx` row: a constraint names the context, so the mint refuses a context that does not fit.  `-` means that the mint accepts each context. |
| `HS14` | The number of negative-compile fixtures of the layer of the row that name the mint.  `⚠` marks a count under the floor of {HS14_FLOOR}. |

`-` in a flag column is a shortfall.  `{NOT_APPLICABLE}` means that the axis
does not apply to the row.
"""


def render(rows: Sequence[Row]) -> str:
    """Return the inventory as markdown.  It holds no time stamp, so two renders of one build are equal."""
    out = [HEADER]
    group = None
    for row in rows:
        if row.group != group:
            group = row.group
            out += [f"\n## {group}\n\n", "| mint | site | nd | cx | ne | rq | cb | fit | HS14 |\n"]
            out.append("|---|---|---|---|---|---|---|---|---|\n")
        out.append("| " + " | ".join(cells(row)) + " |\n")
    out.append("\n## Summary\n\n")
    out.append("| layer | mints | ctx | token | member | ctx with no fit | under the HS14 floor |\n")
    out.append("|---|---|---|---|---|---|---|\n")
    for family, label in (
        (CRUCIBLE, "crucible (`include/crucible/`)"),
        (SUBSTRATE, "substrate (`include/foundation/`, `include/fixy/`)"),
    ):
        mine = [row for row in rows if row.family == family]
        shapes = {shape: sum(row.mint.shape == shape for row in mine) for shape in ("ctx", "token", "member")}
        unfit = sum(1 for row in mine if row.mint.shape == "ctx" and not row.mint.is_gated)
        under = sum(1 for row in mine if row.hs14 < HS14_FLOOR)
        out.append(
            f"| {label} | {len(mine)} | {shapes['ctx']} | {shapes['token']} | {shapes['member']} | {unfit} | {under} |\n"
        )
    return "".join(out)


def drift(context: runner.PluginContext) -> list[check_report.Finding]:
    """Return an error when the inventory of the tree is not the inventory of the mint records of the build.

    Complexity: linear in the records and the fixtures (rows_of()).

    Raises:
        NotApplicable: If the build does not apply, or the call gives no build directory
        InventoryError: If the build, a record, the rule table or the ledger of mint-pattern cannot be read
        tsast.KitMissing: If the pinned kit is not installed
    """
    path = context.root / INVENTORY
    if path.is_file() and path.read_text(encoding="utf-8") == render(rows_of(context)):
        return []
    message = f"the inventory differs from the mint records of the build.  Run: {WRITER} --build-dir BUILD_DIR"
    return [check_report.Finding("error", INVENTORY, 0, CHECK, message)]


def main(argv: list[str]) -> int:
    """Write, print or compare the inventory.

    Args:
        argv: The words after `loop derive mint-inventory`

    Returns:
        The exit code
    """
    from assay.core import cli

    parser = cli.parser(PROGRAM, "write misc/mint-inventory.md from the mint records of a build, or compare it")
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--write", action="store_true", help="write the inventory")
    modes.add_argument("--stdout", action="store_true", help="print the inventory")
    modes.add_argument("--check", action="store_true", help="compare the inventory with the build")
    parser.add_argument("--build-dir", type=Path, default=REPO_ROOT / DEFAULT_BUILD)
    try:
        options = parser.parse_args(argv)
    except SystemExit as stop:
        return ExitCode.OK if stop.code == 0 else ExitCode.USAGE
    context = runner.PluginContext(REPO_ROOT, options.build_dir.resolve())
    try:
        if options.check:
            differences = drift(context)
            for finding in differences:
                check_report.write_line(finding.text(), is_error=True)
            return ExitCode.FINDINGS if differences else ExitCode.OK
        rows = rows_of(context)
    except (runner.NotApplicable, InventoryError, tsast.KitMissing) as problem:
        check_report.write_line(f"{PROGRAM}: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    text = render(rows)
    if options.stdout:
        check_report.write_line(text.removesuffix("\n"))
        return ExitCode.OK
    (REPO_ROOT / INVENTORY).write_text(text, encoding="utf-8")
    check_report.write_line(f"{PROGRAM}: wrote {INVENTORY} ({len(rows)} mints).", is_error=True)
    return ExitCode.OK
