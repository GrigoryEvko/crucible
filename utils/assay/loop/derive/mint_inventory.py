"""Generate misc/mint-inventory.md, the auditor surface of every §XXI mint.

The inventory reads the one mint model in `utils/assay/loop/derive/mint_model.py`,
which the §XXI rule mint-pattern (`guard check --rule mint-pattern`) also reads.
The two therefore agree about which sites are mints.  The model reads the parse tree, so a comment, a
string, a friend declaration, a deleted overload or a call site is never a row.

SCOPE.  All of `include/`.  The scope is derived, not listed: a directory added
later is audited the moment it declares a mint.

ONE ROW FOR EACH FUNCTION.  Overloads are separate rows.  A forward declaration
and its definition are one row, keyed at the definition.

HS14 COUNTS BY LAYER.  Two layers can declare mints of one name, so a fixture of
one layer says nothing about a gate of the other.  A row counts only the
fixtures of its own layer: `test/fixy/**/neg` and `test/foundation/**/neg` for a
mint of `include/fixy/` or `include/foundation/`, every other `test/**/*_neg`
for a mint of `include/crucible/`.

MODES
    The command is `loop derive mint-inventory MODE`.  `guard selftest
    loop.derive.mint_inventory` gives the mode --self-test.

    --write        regenerate misc/mint-inventory.md
    --stdout       print the inventory (read-only)
    --check        exit 1 when the committed copy differs from a fresh render
    --check-floor  exit 1 when a row is under the HS14 floor and no row of
                   utils/ledgers/mint-floor.txt names it, exit 2 on a stale
                   ledger row.  `guard check --rule mint-floor` runs this mode
    --self-test    plant a tree of fixtures and check every rule, positive and
                   negative

THE FLOOR LEDGER
    utils/ledgers/mint-floor.txt (assay.core.ledger) holds one row
    `PATH | - | admit | MINT | REASON` for each mint under the floor, where
    MINT is `Class::mint_name` for a member mint.  The key is the path and
    the name, with no line, so an edit above a mint does not move it.

EXIT CODES
    0  success
    1  drift, or a live HS14 floor violation
    2  a stale floor ledger row, a malformed ledger, a self-test failure, or a
       usage error
    3  the pinned tree-sitter kit is not installed
"""

from __future__ import annotations

import difflib
import sys
import tempfile
from collections.abc import Collection
from dataclasses import dataclass, replace
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import rows as rows_grammar
from assay.loop.derive import mint_model as mintmodel
from assay.readers import tsast

INVENTORY = tsast.REPO_ROOT / "misc" / "mint-inventory.md"
FLOOR_RULE = "mint-floor"

# The HS14 floor (CLAUDE.md §XVIII): two negative-compile fixtures for each mint.
HS14_FLOOR = 2

# Written in a cell whose axis does not apply to the row, as opposed to `-`,
# which is a shortfall.
NOT_APPLICABLE = "·"


@dataclass(frozen=True, slots=True)
class Row:
    """One mint, with the facts the inventory renders for it.

    Attributes:
        mint: The mint model entry
        path: The site path, relative to the scanned root
        group: The section the row belongs to
        family: `crucible` or `substrate`, the layer the site belongs to
        hs14: The number of fixtures of the row's layer that name the mint
    """

    mint: mintmodel.Mint
    path: str
    group: str
    family: str
    hs14: int
    overloaded: bool = False

    @property
    def name(self) -> str:
        """Return `Class::name` for a member mint, or the bare name."""
        return self.mint.qualified

    @property
    def label(self) -> str:
        """Return the name, with the parameter types when the header overloads it."""
        return f"{self.name}{self.mint.signature}" if self.overloaded else self.name

    @property
    def floor_key(self) -> str:
        """Return the floor key: the name and the path, no line, as `name|path`."""
        return f"{self.name}|{self.path}"


def relative(path: str, root: Path) -> str:
    """Return a site path relative to the scanned root.

    Args:
        path: The path the model recorded, relative or absolute
        root: The scanned root

    Returns:
        The path relative to the root, with forward slashes
    """
    candidate = Path(path)
    if candidate.is_absolute():
        candidate = candidate.relative_to(root)
    return candidate.as_posix()


def group_of(path: str) -> str:
    """Return the section a header path belongs to.

    A section is a layer root plus its first directory: `include/crucible/cntp/`,
    `include/fixy/session/`, `include/foundation/algebra/`.  A header directly
    under a layer root belongs to the root itself.

    Args:
        path: A path relative to the scanned root

    Returns:
        The section key, ending in a slash
    """
    parts = Path(path).parts
    depth = 3 if len(parts) > 3 else 2
    return "/".join(parts[:depth]) + "/"


def build_rows(root: Path, surface: list[Path], fixtures: dict[str, list[Path]]) -> list[Row]:
    """Collect every mint and the facts each row renders.

    Args:
        root: The scanned root
        surface: The headers of the mint surface
        fixtures: `crucible` and `substrate` to the fixture files of that layer

    Returns:
        One row for each mint, sorted by section, name, path and line
    """
    mints = mintmodel.collect(surface)
    names = {family: mintmodel.fixture_names(paths) for family, paths in fixtures.items()}
    aliases = mintmodel.alias_heads(surface)
    family_of = {id(mint): "substrate" if mintmodel.is_substrate_path(relative(mint.path, root)) else "crucible"
                 for mint in mints}
    carriers: dict[tuple[str, str], set[str | None]] = {}
    for mint in mints:
        carriers.setdefault((family_of[id(mint)], mint.name), set()).add(mint.owner)
    rows: list[Row] = []
    for mint in mints:
        path = relative(mint.path, root)
        family = family_of[id(mint)]
        rows.append(Row(
            mint=mint,
            path=path,
            group=group_of(path),
            family=family,
            hs14=mintmodel.fixture_count(mint, len(carriers[(family, mint.name)]), names.get(family, []), aliases),
        ))
    per_site: dict[tuple[str, str], int] = {}
    for row in rows:
        per_site[(row.path, row.name)] = per_site.get((row.path, row.name), 0) + 1
    rows = [replace(r, overloaded=per_site[(r.path, r.name)] > 1) for r in rows]
    return sorted(rows, key=lambda r: (r.group, r.name, r.path, r.mint.signature))


def cells(row: Row) -> list[str]:
    """Return the table cells of one row, in column order.

    Args:
        row: The row to render

    Returns:
        The cells: name, site, nd, cx, ne, rq, cb, fit, HS14
    """
    mint = row.mint
    if mint.constexpr:
        cx = "Y"
    elif not mintmodel.constexpr_applies(mint):
        cx = NOT_APPLICABLE
    elif mint.carve_out_cx:
        cx = "- (alloc)"
    else:
        cx = "-"
    if not mintmodel.requires_applies(mint):
        rq = NOT_APPLICABLE
    elif mintmodel.has_fit_constraint(mint):
        rq = "Y"
    elif mint.carve_out_rq:
        rq = "- (pre)"
    else:
        rq = "-"
    fit = ("Y" if mint.ctx_gated else "-") if mintmodel.ctxfit_applies(mint) else NOT_APPLICABLE
    hs14 = f"HS14: {row.hs14}" + (" ⚠" if row.hs14 < HS14_FLOOR else "")
    return [
        f"`{row.label}`",
        f"`{row.path}`",
        "Y" if mint.nodiscard else "-",
        cx,
        "Y" if mint.noexcept_ else "-",
        rq,
        mint.shape,
        fit,
        hs14,
    ]


HEADER = f"""\
# Mint inventory — auditor snapshot

Generated by `loop derive mint-inventory` from the AST mint model in
`utils/assay/loop/derive/mint_model.py`, which the §XXI rule mint-pattern
(`guard check --rule mint-pattern`) also reads.  Regenerate it in the same
commit that adds, removes, moves or renames a `mint_*` factory:

```bash
python3 utils/bin/loop derive mint-inventory --write         # regenerate this file
python3 utils/bin/loop derive mint-inventory --stdout        # print it (read-only)
python3 utils/bin/loop derive mint-inventory --check         # exit 1 on drift
python3 utils/bin/loop derive mint-inventory --check-floor   # exit 1 on a row under the HS14 floor
```

`--check-floor`, which `guard check --rule mint-floor` runs, reads
`utils/ledgers/mint-floor.txt`.  It lists the mints that are under the floor
today.  A mint added without fixtures fails the gate.  A listed mint that
reaches the floor leaves a stale row, which fails the gate at exit 2, so the
file only drains.

The scope is all of `include/`.  One row is one function: overloads are
separate rows, and a forward declaration folds into its definition.  A section
is a layer root and its first directory.

The inventory holds no line numbers.  An edit above a mint moves its line, and
a line key would make every such edit drift this file.  A row names its header,
and a header that overloads a mint names each overload by its parameter types.

| Column | Meaning |
|---|---|
| `mint` | The factory, with its class for a member, and its parameter types when its header overloads it. |
| `site` | The header of the canonical site: the definition when one is in scope. |
| `nd` | `[[nodiscard]]` on the declaration or the definition. |
| `cx` | `constexpr` or `consteval`.  `- (alloc)` is the documented carve-out for a mint that allocates or calls the kernel (marker `// §XXI carve-out: cx=alloc` above the signature).  `{NOT_APPLICABLE}` marks a borrow projection, which returns a view over its own object and can never be constant-evaluated. |
| `ne` | `noexcept`. |
| `rq` | A type-level constraint: a `requires` clause or a concept on a template parameter.  `- (pre)` is the documented carve-out for a value-dependent gate written as a `pre(...)` clause (marker `// §XXI carve-out: rq=pre`).  `{NOT_APPLICABLE}` marks a mint that is not a template, which cannot carry a constraint. |
| `cb` | The authorization shape: `ctx` (the first parameter is `Ctx const&`), `token` (authority from the arguments), or `member` (a non-static method, whose authority is its object).  A static member takes its shape from its parameters. |
| `fit` | For a `ctx` row: a constraint names the context, so the mint refuses a context that does not fit.  `-` means the mint accepts every context. |
| `HS14` | The number of negative-compile fixtures of the row's own layer that name the mint.  `⚠` marks a count under the floor of {HS14_FLOOR}. |

`-` in a flag column is a shortfall.  `{NOT_APPLICABLE}` means the axis does not
apply to the row.
"""


def render(rows: list[Row]) -> str:
    """Render the inventory as markdown.

    The output is a pure function of the rows: it holds no time stamp, so two
    renders of one tree are byte-identical and `--check` compares whole files.

    Args:
        rows: The rows, sorted by section

    Returns:
        The document text
    """
    out = [HEADER]
    group = None
    for row in rows:
        if row.group != group:
            group = row.group
            out.append(f"\n## {group}\n\n")
            out.append("| mint | site | nd | cx | ne | rq | cb | fit | HS14 |\n")
            out.append("|---|---|---|---|---|---|---|---|---|\n")
        out.append("| " + " | ".join(cells(row)) + " |\n")

    def count(family: str, **match: str) -> int:
        """Return the number of rows of one layer that match the given fields."""
        return sum(
            1 for r in rows
            if r.family == family and all(getattr(r.mint, k) == v for k, v in match.items())
        )

    out.append("\n## Summary\n\n")
    out.append("| layer | mints | ctx | token | member | ctx with no fit | under the HS14 floor |\n")
    out.append("|---|---|---|---|---|---|---|\n")
    for family, label in (("crucible", "crucible (`include/crucible/`)"),
                          ("substrate", "substrate (`include/foundation/`, `include/fixy/`)")):
        mine = [r for r in rows if r.family == family]
        unfit = sum(1 for r in mine if mintmodel.ctxfit_applies(r.mint) and not r.mint.ctx_gated)
        under = sum(1 for r in mine if r.hs14 < HS14_FLOOR)
        out.append(
            f"| {label} | {len(mine)} | {count(family, shape='ctx')} | "
            f"{count(family, shape='token')} | {count(family, shape='member')} | "
            f"{unfit} | {under} |\n"
        )
    return "".join(out)


def read_floor_ledger(file: Path | None = None) -> dict[str, str]:
    """Return the key of each row of the floor ledger, `name|path`, with the start of the row as the file holds it.

    Args:
        file: A planted ledger file, or None for utils/ledgers/mint-floor.txt

    Returns:
        The start of each row, with no reason, by its key

    Raises:
        ValueError: If the ledger does not obey the rows grammar, or a row has a kind, no path or no mint
    """
    shown = rule_ledger.shown_path(FLOOR_RULE) if file is None else str(file)
    try:
        ledger = rule_ledger.read(rule_ledger.rule_of(FLOOR_RULE), file=file)
    except rows_grammar.RowsError as exc:
        raise ValueError(f"malformed floor ledger: {exc}") from None
    keys: dict[str, str] = {}
    for key, entry in ledger.base.items():
        if key.kind or not key.path or not key.entity:
            raise ValueError(f"malformed floor ledger row {shown}:{entry.line} (want `PATH | - | admit | MINT | "
                             f"REASON`)")
        keys[f"{key.entity}|{key.path}"] = rows_grammar.render([key.path], ["", rule_ledger.ADMIT_VALUE, key.entity])
    return keys


def floor_verdict(rows: list[Row], allowed: Collection[str]) -> tuple[list[Row], list[str]]:
    """Split the rows under the floor into live violations and find stale ledger rows.

    Args:
        rows: Every row
        allowed: The keys of the floor ledger

    Returns:
        The live violations, and the ledger keys that no violating row uses
    """
    under = [r for r in rows if r.hs14 < HS14_FLOOR]
    live = [r for r in under if r.floor_key not in allowed]
    seen = {r.floor_key for r in under}
    return live, sorted(set(allowed) - seen)


def real_rows() -> list[Row]:
    """Build the rows of the repository itself.

    Returns:
        Every row of the tree
    """
    return build_rows(tsast.REPO_ROOT, mintmodel.surface_files(), mintmodel.fixture_files())


def check_floor(rows: list[Row], file: Path | None = None) -> int:
    """Apply the HS14 floor gate and print its findings.

    Args:
        rows: Every row
        file: A planted floor ledger file, or None for utils/ledgers/mint-floor.txt

    Returns:
        0 when clean, 1 on a live violation, 2 on a stale ledger row or a malformed ledger
    """
    shown = rule_ledger.shown_path(FLOOR_RULE) if file is None else str(file)
    try:
        allowed = read_floor_ledger(file)
    except ValueError as exc:
        print(f"gen-mint-inventory: {exc}", file=sys.stderr)
        return 2
    live, stale = floor_verdict(rows, allowed)
    if live:
        print(f"gen-mint-inventory: HS14 FLOOR VIOLATION — these mints have fewer than "
              f"{HS14_FLOOR} negative-compile fixtures of their own layer:", file=sys.stderr)
        for row in live:
            print(f"  {row.floor_key}:{row.mint.line}  HS14: {row.hs14}", file=sys.stderr)
        print("Write fixtures that name the mint in a fixture directory of its layer.", file=sys.stderr)
        return 1
    if stale:
        print(f"gen-mint-inventory: STALE floor ledger rows — these mints meet the "
              f"floor now, or moved, or were renamed.  Delete them from {shown}:", file=sys.stderr)
        for key in stale:
            print(f"  {allowed[key]}", file=sys.stderr)
        return 2
    print(f"gen-mint-inventory: every mint meets the HS14 floor of {HS14_FLOOR}, "
          f"or {Path(shown).name} lists it ({len(allowed)} entries).", file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant a tree, generate its inventory, and check every rule.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    counts = {"cases": 0, "negatives": 0}

    def check(name: str, ok: bool, negative: bool = False) -> None:
        """Record one case result and print it.

        Args:
            name: What the case asserts
            ok: Whether it held
            negative: Whether the case plants something the generator must refuse
        """
        counts["cases"] += 1
        counts["negatives"] += negative
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    print("gen-mint-inventory --self-test")
    planted = {
        "include/crucible/sample/Mint.h": """
namespace crucible::sample {
struct Thing {};
struct Ctx {};
[[nodiscard]] constexpr Thing mint_planted_token(Thing) noexcept { return {}; }
[[nodiscard]] constexpr Thing mint_planted_token(int) noexcept = delete("gone");
struct Holder {
    [[nodiscard]] Thing mint_planted_member() const noexcept { return {}; }
    friend constexpr Thing mint_planted_token(Thing) noexcept;
};
struct Keeper {
    [[nodiscard]] Thing mint_planted_member() const noexcept { return {}; }
};
using Kept = Keeper;
}  // namespace crucible::sample
""",
        "include/foundation/algebra/Lattice.h": """
namespace foundation::algebra {
template <typename> concept IsExecCtx = true;
template <typename> concept Fits = true;
struct Thing {};
struct Lattice {
    template <IsExecCtx C>
        requires Fits<C>
    [[nodiscard]] static constexpr Thing mint_from_image(C const&, int) noexcept { return {}; }
};
template <IsExecCtx C>
    requires Fits<C>
[[nodiscard]] constexpr Thing mint_planted_source(C const& ctx) noexcept;
template <IsExecCtx C>
    requires Fits<C>
constexpr Thing mint_planted_source(C const&) noexcept { return {}; }
}
""",
        "test/sample_neg/uses_token.cpp": "void f() { mint_planted_token(0); }\n",
        "test/sample_neg/names_it_in_a_comment.cpp": "// mint_planted_token is refused here\nvoid g();\n",
        "test/fixy/neg/uses_image_a.cpp": "void f() { mint_from_image(0, 0); }\n",
        "test/fixy/neg/uses_image_b.cpp": "void f() { mint_from_image(1, 1); }\n",
        "test/fixy/neg/substrate_names_the_crucible_token.cpp": "void f() { mint_planted_token(0); }\n",
        # Two classes carry the member name mint_planted_member.  A fixture
        # counts for the class it names, and one that names neither class
        # counts for neither.
        "test/sample_neg/holder_first.cpp": "void f(Holder const& h) { (void)h.mint_planted_member(); }\n",
        "test/sample_neg/holder_second.cpp": "void f() { sample::Holder h; (void)h.mint_planted_member(); }\n",
        "test/sample_neg/keeper.cpp": "void f(Keeper const& k) { (void)k.mint_planted_member(); }\n",
        "test/sample_neg/keeper_alias.cpp": "void f(Kept const& k) { (void)k.mint_planted_member(); }\n",
        "test/sample_neg/no_class.cpp": "void f(auto const& any) { (void)any.mint_planted_member(); }\n",
    }
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in planted.items():
            target = root / rel
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_text(text, encoding="utf-8")
        surface = sorted((root / "include").rglob("*.h"))
        rows = build_rows(root, surface, mintmodel.fixture_files(root))
        by_name = {r.name: r for r in rows}

        check(
            "finds exactly the five live mints",
            sorted(by_name) == [
                "Holder::mint_planted_member", "Keeper::mint_planted_member", "Lattice::mint_from_image",
                "mint_planted_source", "mint_planted_token",
            ],
        )
        holder, keeper = by_name.get("Holder::mint_planted_member"), by_name.get("Keeper::mint_planted_member")
        check(
            "two classes that carry one member name each count the fixtures that name the class or its alias",
            holder is not None and keeper is not None and holder.hs14 == 2 and keeper.hs14 == 2,
            True,
        )
        token = by_name.get("mint_planted_token")
        check(
            "the deleted overload and the friend add no row",
            len([r for r in rows if r.mint.name == "mint_planted_token"]) == 1,
            True,
        )
        check(
            "HS14 counts one fixture: a comment and a fixture of the other layer do not count",
            token is not None and token.family == "crucible" and token.hs14 == 1,
            True,
        )
        member = by_name.get("Holder::mint_planted_member")
        check(
            "a non-static member has the member shape",
            member is not None and member.mint.shape == "member",
        )
        image = by_name.get("Lattice::mint_from_image")
        check(
            "a static member of the substrate layer is a ctx row in its section, with two fixtures",
            image is not None and image.mint.shape == "ctx" and image.family == "substrate"
            and image.group == "include/foundation/algebra/" and image.hs14 == 2,
        )
        source = by_name.get("mint_planted_source")
        check(
            "a declaration folds into its definition, and its nodiscard counts",
            source is not None and source.mint.defines and source.mint.nodiscard
            and len([r for r in rows if r.mint.name == "mint_planted_source"]) == 1,
        )

        text = render(rows)
        check("the render has no time stamp", "Snapshot generated" not in text)
        check("two renders are byte-identical", render(rows) == text)
        check(
            "a row under the floor carries the warning mark",
            "| `mint_planted_token` | `include/crucible/sample/Mint.h` |" in text and "HS14: 1 ⚠" in text,
        )
        # Negative control: a line number would make every edit above a mint drift.
        check("the render holds no line number", "Mint.h:" not in text and "Lattice.h:" not in text, True)

        # The floor gate, three arms.
        allow = root / "allow.txt"

        def plant(sites: list[tuple[str, str]]) -> None:
            """Write the planted floor ledger: one row (path, mint) for each site, each with a planted reason."""
            keys = [rule_ledger.Key(path, "", name) for path, name in sites]
            base = {key: rule_ledger.Entry(key, rule_ledger.ADMIT_VALUE, "planted") for key in keys}
            rule_ledger.store(rule_ledger.Ledger(FLOOR_RULE, rule_ledger.rule_of(FLOOR_RULE).policy, "# planted\n",
                                                 base=base), file=allow)

        plant([])
        check("an unlisted row under the floor is a live violation", check_floor(rows, allow) == 1, True)
        listed = [(r.path, r.name) for r in rows if r.hs14 < HS14_FLOOR]
        plant(listed)
        check("listing every row under the floor passes", check_floor(rows, allow) == 0)
        plant([*listed, ("include/crucible/sample/Gone.h", "mint_gone")])
        check("a row that no mint uses is stale", check_floor(rows, allow) == 2, True)
        allow.write_text(f"format 1\nrule {FLOOR_RULE}\nmint_no_bar\n", encoding="utf-8")
        check("a malformed ledger fails", check_floor(rows, allow) == 2, True)
        allow.write_text(f"format 1\nrule {FLOOR_RULE}\ninclude/crucible/sample/Mint.h | fixture | admit | "
                         f"mint_planted_token | planted\n", encoding="utf-8")
        check("a row with a kind fails", check_floor(rows, allow) == 2, True)

    if failures:
        print(f"gen-mint-inventory --self-test: FAILED — {len(failures)} case(s)")
        return 2
    print(f"gen-mint-inventory --self-test: {counts['cases']} cases pass, "
          f"{counts['negatives']} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode.

    Args:
        argv: The words after `loop derive mint-inventory`: one mode

    Returns:
        The exit code
    """
    if len(argv) != 1 or argv[0] not in ("--write", "--stdout", "--check", "--check-floor", "--self-test"):
        print("usage: loop derive mint-inventory --write | --stdout | --check | --check-floor | --self-test\n"
              "Writing is opt-in: --stdout prints the inventory without touching the tracked file.",
              file=sys.stderr)
        return 2
    mode = argv[0]
    try:
        if mode == "--self-test":
            return self_test()
        rows = real_rows()
    except tsast.KitMissing as exc:
        print(f"gen-mint-inventory: {exc}", file=sys.stderr)
        return 3
    if mode == "--check-floor":
        return check_floor(rows)
    text = render(rows)
    if mode == "--stdout":
        sys.stdout.write(text)
        return 0
    if mode == "--write":
        INVENTORY.write_text(text, encoding="utf-8")
        print(f"gen-mint-inventory: wrote {INVENTORY.relative_to(tsast.REPO_ROOT)} ({len(rows)} mints).",
              file=sys.stderr)
        return 0
    committed = INVENTORY.read_text(encoding="utf-8") if INVENTORY.is_file() else ""
    if committed == text:
        print("gen-mint-inventory: misc/mint-inventory.md is up to date.", file=sys.stderr)
        return 0
    print("gen-mint-inventory: DRIFT — misc/mint-inventory.md is out of date. "
          "Run: python3 utils/bin/loop derive mint-inventory --write", file=sys.stderr)
    diff = difflib.unified_diff(committed.splitlines(), text.splitlines(),
                                "committed", "generated", lineterm="", n=1)
    for index, line in enumerate(diff):
        if index >= 60:
            print("  ...", file=sys.stderr)
            break
        print(line, file=sys.stderr)
    return 1
