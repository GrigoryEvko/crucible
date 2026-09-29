#!/usr/bin/env python3
"""check-brand-drain — count each spelling of a branded type that names no brand, and let no count increase.

Region identity in this tree is a brand: a template parameter of a region,
view, borrow or permission template.  A spelling that names no brand, such
as `Permission<Tag>`, is the erased identity DefaultBrand.  Each token of
that identity is interchangeable with each other token of its tag.
foundation/Brand.h gives the rules of a brand.

WHICH TEMPLATES ARE BRANDED
    The guard derives the set from the parse tree of include/, and the
    script names no template.  A class template or an alias template is
    branded when one of its template parameters is named Brand, or has a
    default that names DefaultBrand.  The position of that parameter is the
    count of arguments that a spelling with no brand holds.  A template is
    known by its qualified name, so fixy::Borrowed is branded and
    fixy::session::Borrowed is a template of its own.  Two primary
    declarations of one qualified name with the brand at two positions fail,
    because a spelling of that name then has no one reading.  A list that did
    not derive its templates missed OwnedMmap and NumaPlacement when each
    gained a brand, so a spelling of either on the erased identity was never
    counted.

WHICH TEMPLATE A SPELLING NAMES
    Name lookup of utils/scripts/tsast.py (NameIndex) decides: the innermost
    enclosing scope that declares the name, with the using-declarations,
    using-directives and namespace aliases in force at the spelling.  The
    index holds the declarations of include/ and of the file itself.  A
    qualifier whose head no scope knows can name a branded template that
    the index does not see, so the guard then counts the spelling for each
    branded template of that last name.  A macro body has no enclosing
    scope until it expands, so a spelling in a body counts the same way.

THE RULE
    A site is a spelling of a branded template whose argument list stops at
    or before the brand, or names DefaultBrand.  utils/scripts/brand-drain.txt
    holds one count per file.  A file with more sites than its entry fails:
    write the brand or a generic parameter.  A file with fewer sites than
    its entry fails as stale: run --refresh in the same commit.  An empty
    ledger makes every site a hard error, and that is the end state.

WHAT READS THE SITES
    The guard reads the parse tree of the pinned tree-sitter kit
    (utils/scripts/tsast.py).  It reads each template_type and template_function
    node that names a branded template, and the named arguments of its
    list.  An argument names the erased brand when its own final name is
    DefaultBrand, so Wrap<DefaultBrand> in the brand position is a brand of
    its own.  A comment, a string literal and a raw string hold no node.  A
    list on several lines is one node.  The body of each #define is parsed
    as a fragment (tsast.macro_bodies), and its sites are read from that
    tree.  A body that does not parse and spells a branded name followed by
    `<` is unreadable, and it fails.  An alias counts one time, at the
    alias, and not at each use.

Usage
    check-brand-drain.py              compare the tree with the ledger
    check-brand-drain.py --list       print every site
    check-brand-drain.py --refresh    write the ledger again from the tree
    check-brand-drain.py --self-test  put sites in a temporary tree and examine each verdict

Exit 0 clean, 1 on a new site, an unreadable list or a parse failure, 2 on
a stale entry, a usage error or a failed self-test, 3 when the kit is not
installed.
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

import tsast  # noqa: E402

ROOTS = ("include/foundation", "include/fixy", "test/foundation", "test/fixy")
TEMPLATE_ROOT = "include"
LEDGER = "utils/scripts/brand-drain.txt"
SUFFIXES = (".h", ".cpp")
BRAND_PARAMETER = "Brand"
ERASED_BRAND = "DefaultBrand"
LEDGER_HEADER = (
    "# utils/scripts/brand-drain.txt — the sites on the erased brand, one count per\n"
    "# file.  utils/scripts/check-brand-drain.py reads this ledger.  A count can only\n"
    "# decrease.  Where a new spelling is necessary, write the brand.  Run\n"
    "# --refresh in the commit that removes a site.\n"
    "#\n"
    "# This ledger holds the counts of 2026-09-24, when the guard moved onto\n"
    "# the parse tree and into CI.  These counts replace the smaller counts of\n"
    "# the last ledger.  That is permitted only because the erased brand cannot\n"
    "# open a door that trusts a brand to skip a check.  mint_shared_read\n"
    "# refuses the erased brand, and it asks the guard for its pool at run\n"
    "# time.  OwnedRegion::recombine examines the address of each shard.  A\n"
    "# site here is drain work, not a hole.\n"
    "#\n"
    "# The total rose from 484 to 574 on the same day, when the guard began\n"
    "# to derive the branded templates from the parse tree.  The list of nine\n"
    "# names that it replaced did not see OwnedMmap, NumaPlacement or the\n"
    "# other templates that carry a brand.\n"
)


@dataclass(frozen=True)
class Site:
    """One spelling on the erased identity."""

    path: str
    row: int
    name: str


def names_erased_brand(node: tsast.Node) -> bool:
    """Return True when a template argument or a parameter default is the erased brand itself.

    The final name of the node decides, so `::foundation::brand::DefaultBrand`
    counts, and `Wrap<DefaultBrand>` is a brand of its own.  A type_descriptor
    is read through its `type` field.
    """
    target = node.child_by_field("type") if node.type == "type_descriptor" else node
    if target is None:
        return False
    parts = tsast.qualified_parts(target)
    return parts is not None and parts[1][-1:] == (ERASED_BRAND,)


def parameter_name(node: tsast.Node) -> str | None:
    """Return the name of one template parameter, or None for an unnamed one."""
    for field in ("name", "declarator"):
        named = node.child_by_field(field)
        if named is not None:
            return named.text
    for child in node.children:
        if child.type in ("type_identifier", "identifier"):
            return child.text
    return None


def defaults_to_erased_brand(node: tsast.Node) -> bool:
    """Return True when the default of one template parameter is the erased brand."""
    default = node.child_by_field("default_type")
    return default is not None and names_erased_brand(default)


def declared_template(declaration: tsast.Node) -> str | None:
    """Return the name of the primary class template or alias template that a template declaration declares.

    A partial specialization names a template_type, a friend declaration and a
    function declare no class, and each returns None.
    """
    for child in declaration.children:
        if child.type in ("class_specifier", "struct_specifier", "union_specifier", "alias_declaration"):
            named = child.child_by_field("name")
            return named.text if named is not None and named.type == "type_identifier" else None
    return None


QualifiedName = tuple[str, ...]


@dataclass(frozen=True)
class Branded:
    """The branded templates of include/, and the index that name lookup reads.

    Attributes:
        position: The position of the brand for each qualified name
        by_last_name: The qualified names of the branded templates for each last name
        index: The declarations of include/
    """

    position: dict[QualifiedName, int]
    by_last_name: dict[str, tuple[QualifiedName, ...]]
    index: tsast.NameIndex


def branded_templates(root: Path) -> tuple[Branded, list[str]]:
    """Derive each branded template under include/ and the position of its brand.

    Complexity: linear in the size of the headers under include/.

    Returns:
        The branded templates, and one line for each file the guard cannot
        read, or each qualified name it reads with two positions

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    base = root / TEMPLATE_ROOT
    files = sorted(path for path in base.rglob("*") if path.is_file() and path.suffix in SUFFIXES
                   and tsast.is_in_cpp_scope(path.relative_to(root))) if base.is_dir() else []
    positions: dict[QualifiedName, set[int]] = {}
    failures: list[str] = []
    index = tsast.NameIndex()
    for tree in tsast.parse(files, strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            failures.append(f"{rel}: the parser cannot read this file.  Its branded templates are unknown")
            continue
        index.add(tree, share_aliases=True)
        for declaration in tree.find("template_declaration"):
            name = declared_template(declaration)
            parameters = declaration.child_by_field("parameters")
            if name is None or parameters is None:
                continue
            qualified = tsast.scope_levels(declaration)[0] + (name,)
            listed = [child for child in parameters.children if not tsast.is_comment(child)]
            for position, parameter in enumerate(listed):
                if parameter_name(parameter) == BRAND_PARAMETER or defaults_to_erased_brand(parameter):
                    positions.setdefault(qualified, set()).add(position)
                    break
    position_of: dict[QualifiedName, int] = {}
    for qualified in sorted(positions):
        if len(positions[qualified]) != 1:
            failures.append(f"{'::'.join(qualified)}: two primary declarations of this template carry the brand at "
                            f"positions {sorted(positions[qualified])}, so a spelling of it has no one reading.  "
                            "Make them agree")
            continue
        position_of[qualified] = next(iter(positions[qualified]))
    by_last_name: dict[str, list[QualifiedName]] = {}
    for qualified in position_of:
        by_last_name.setdefault(qualified[-1], []).append(qualified)
    return Branded(position_of, {name: tuple(sorted(found)) for name, found in by_last_name.items()}, index), failures


def referents(node: tsast.Node, branded: Branded, lookup: tuple[list, list, frozenset] | None) -> tuple[QualifiedName, ...]:
    """Return the branded templates that one template-id can name.

    A spelling that lookup cannot resolve counts for each branded template of
    its last name, so an unknown case is counted.  A spelling that resolves
    to a template with no brand is no site.

    Args:
        node: A template_type or template_function node
        branded: The branded templates
        lookup: (the namespace aliases, the usings, the local names) of the
            tree, or None for a macro body, whose scope is unknown

    Returns:
        The qualified names of the branded templates it names
    """
    name = node.child_by_field("name")
    candidates = branded.by_last_name.get(name.text, ()) if name is not None else ()
    path = tsast.qualified_path(node)
    if not candidates or lookup is None or path is None:
        return candidates
    aliases, usings, local = lookup
    site = tsast.lookup_site_of_parts(node, path[0], path[1], aliases, usings)
    found, _known = branded.index.resolve(site, local)
    if not found:
        return candidates
    return tuple(qualified for qualified in found if qualified in branded.position)


def tree_site_nodes(tree: tsast.Tree, branded: Branded, *, is_macro_body: bool = False) -> list[tuple[tsast.Node, str]]:
    """Return each spelling of a branded template in one tree that stops at or before the brand or names DefaultBrand.

    Args:
        tree: A parsed file, or the tree of a macro body
        branded: The branded templates
        is_macro_body: True for a macro body, whose spellings count for each
            branded template of their last name
    """
    lookup = None
    if not is_macro_body:
        local = tsast.NameIndex()
        local.add(tree)
        lookup = (tsast.namespace_aliases(tree), tsast.using_names(tree), frozenset(local.names))
    found = []
    for node in tree.find("template_type", "template_function"):
        name = node.child_by_field("name")
        arguments = node.child_by_field("arguments")
        if name is None or arguments is None or name.text not in branded.by_last_name:
            continue
        named = [child for child in arguments.children if not tsast.is_comment(child)]
        erased = any(names_erased_brand(child) for child in named)
        if any(len(named) <= branded.position[qualified] or erased
               for qualified in referents(node, branded, lookup)):
            found.append((node, name.text))
    return found


def unreadable_body(body: tsast.MacroBody, branded: Branded) -> str | None:
    """Return a branded name that a macro body spells before `<` without a template node in its parse, or None.

    A body is a fragment, so `Permission<x` can parse as a comparison.  When
    the preprocessing tokens hold more `Name <` pairs of a branded name than
    the parse holds template nodes of that name, the parse did not read a
    list the tokens show, and the guard fails closed rather than guess its
    arguments.  A body that does not parse at all has no template node, so
    every such pair in it counts.
    """
    tokens = tsast.pp_tokens(body.text, body.first_row)
    spelled: dict[str, int] = {}
    for token, following in zip(tokens, tokens[1:]):
        if token.kind == "identifier" and token.text in branded.by_last_name and following.text == "<":
            spelled[token.text] = spelled.get(token.text, 0) + 1
    read: dict[str, int] = {}
    if body.is_parsed:
        for node in body.root.descendants("template_type", "template_function"):
            name = node.child_by_field("name")
            if name is not None and name.text in branded.by_last_name:
                read[name.text] = read.get(name.text, 0) + 1
    for name in sorted(spelled):
        if spelled[name] > read.get(name, 0):
            return name
    return None


def scan(root: Path, roots: tuple[str, ...] = ROOTS) -> tuple[list[Site], list[Site], list[str]]:
    """Return every site, every unreadable list, and every parse failure under the roots.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    branded, failures = branded_templates(root)
    files = sorted(path for top in roots if (root / top).is_dir()
                   for path in (root / top).rglob("*") if path.is_file() and path.suffix in SUFFIXES)
    sites: list[Site] = []
    unreadable: list[Site] = []
    parsed: list[tsast.Tree] = []
    for tree in tsast.parse(files, strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            failures.append(f"{rel}: the parser cannot read this file.  Its sites are unknown")
            continue
        parsed.append(tree)
        sites += [Site(rel, node.start[0] + 1, name) for node, name in tree_site_nodes(tree, branded)]
    for body in tsast.macro_bodies(parsed):
        rel = Path(body.define.tree.path).relative_to(root).as_posix()
        name = unreadable_body(body, branded)
        if name is not None:
            unreadable.append(Site(rel, body.define.start[0] + 1, name))
        elif body.is_parsed:
            sites += [Site(rel, body.origin(node)[0] + 1, name)
                      for node, name in tree_site_nodes(body.tree, branded, is_macro_body=True)]
    return sites, unreadable, failures


def per_file(sites: list[Site]) -> dict[str, int]:
    """Count the sites of each file."""
    counts: dict[str, int] = {}
    for site in sites:
        counts[site.path] = counts.get(site.path, 0) + 1
    return counts


def read_ledger(path: Path) -> tuple[dict[str, int], list[str]]:
    """Return the count of each file the ledger lists, and one line for each malformed row."""
    allowed: dict[str, int] = {}
    rot = []
    if not path.is_file():
        return allowed, rot
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        shown, _, count = entry.rpartition("\t")
        if not shown or not count.isdigit():
            rot.append(f"MALFORMED line {number}: {entry} — a row is `path<TAB>count`")
            continue
        if shown in allowed:
            rot.append(f"DUPLICATE line {number}: {shown} has a row before this one")
            continue
        allowed[shown] = int(count)
    return allowed, rot


def check(root: Path, ledger: Path, roots: tuple[str, ...] = ROOTS) -> int:
    """Compare the tree with the ledger, and print the report."""
    sites, unreadable, failures = scan(root, roots)
    counts = per_file(sites)
    allowed, rot = read_ledger(ledger)
    over = [(path, counts.get(path, 0), allowed.get(path, 0)) for path in sorted(set(counts) | set(allowed))
            if counts.get(path, 0) > allowed.get(path, 0)]
    stale = [(path, counts.get(path, 0), allowed[path]) for path in sorted(allowed)
             if counts.get(path, 0) < allowed[path]]
    for path, have, limit in over:
        print(f"NEW SITE  {path}: {have} site(s) on the erased identity, the ledger permits {limit}.  "
              "Write the brand.", file=sys.stderr)
    for path, have, limit in stale:
        print(f"STALE     {path}: {have} site(s), the ledger still says {limit}.  Run --refresh in this commit.",
              file=sys.stderr)
    for site in unreadable:
        print(f"UNREADABLE {site.path}:{site.row}  {site.name}<...> does not close", file=sys.stderr)
    for line in failures + rot:
        print(line, file=sys.stderr)
    print(f"check-brand-drain: {len(sites)} site(s) in {len(counts)} file(s).  The ledger permits "
          f"{sum(allowed.values())} in {len(allowed)} file(s).  {len(over)} over, {len(stale)} stale, "
          f"{len(unreadable)} unreadable.", file=sys.stderr)
    if over or unreadable or failures:
        return 1
    return 2 if stale or rot else 0


def refresh(root: Path, ledger: Path, roots: tuple[str, ...] = ROOTS) -> int:
    """Write the ledger again from the tree.  An unreadable list or a parse failure stops the write."""
    sites, unreadable, failures = scan(root, roots)
    if unreadable or failures:
        print("check-brand-drain: --refresh does not write the ledger while a list is unreadable or a file "
              "does not parse.", file=sys.stderr)
        return 1
    counts = per_file(sites)
    ledger.write_text(LEDGER_HEADER + "".join(f"{path}\t{counts[path]}\n" for path in sorted(counts)),
                      encoding="utf-8")
    print(f"check-brand-drain: ledger written with {len(counts)} file(s), {len(sites)} site(s).", file=sys.stderr)
    return 0


def self_test() -> int:
    """Put sites in a temporary tree, some to count and some not to count, and make sure that each verdict is correct."""
    failures: list[str] = []
    negatives = 0

    def expect(name: str, ok: bool, negative: bool = False) -> None:
        nonlocal negatives
        negatives += negative
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    planted = (
        "#pragma once\n"
        "struct Tag {}; struct Fresh {};\n"
        "Permission<Tag> bare;\n"                                   # site
        "fp::ReadView<Tag> qualified;\n"                            # site
        "Permission<Tag, DefaultBrand> named_erased;\n"             # site
        "Permission<Tag, Fresh> branded;\n"
        "ReadView<Tag, Fresh> branded_view;\n"
        "// A comment spelling Permission<Tag> is not a site.\n"
        "/* nor in a block comment: Permission<Tag> */\n"
        'const char* text = "nor Permission<Tag> in a string";\n'
        "OwnedRegion<int, Tag> region;\n"                           # site
        "using Long = Permission<Tag,\n"
        "                        Fresh>;\n"
        "using LongErased = Borrowed<int,\n"                        # site
        "                            Tag>;\n"
        "static_assert(std::is_same_v<Permission<Tag>, int>);\n"   # site
        "auto call = fp::Permission<Tag>{mint()};\n"                # site
        "template <class B> void generic(Permission<Tag, B>&&);\n"
        "#define ERASED_IN_MACRO(x) Permission<x> m\n"              # site
        "#define BRANDED_IN_MACRO(x) Permission<x, Fresh> m\n"
        "struct Wide { SharedPermissionPool<Tag> pool; };\n"        # site
        "Permission<Tag, /* DefaultBrand */ Fresh> named_in_a_comment;\n"
        "Permission<Tag /* , Fresh */> brand_in_a_comment;\n"       # site
        "Novel<Tag> novel;\n"                                       # site
        "Renamed<Tag> renamed;\n"                                   # site
        "Handle<Tag> handle;\n"                                     # site
        "Plain<Tag> plain;\n"
        "Late<Tag> late;\n"                                         # site
        "Late<Tag, int> late_but_erased;\n"                         # site
        "Late<Tag, int, Fresh> late_branded;\n"
        "Permission<Tag, Wrap<DefaultBrand>> wrapped_brand;\n"
        "Permission<Tag, Wrap</* DefaultBrand */ int>> brand_comment_inside;\n"
        "#define SPLIT_ERASED(x) Permission<x, /* a comment splits the body */ \\\n"  # site
        "    DefaultBrand> split_erased\n"
    )
    # The branded templates, declared outside the roots of the sites.  Novel,
    # Renamed, Handle and Late are named nowhere else in this script.
    definitions = (
        "#pragma once\n"
        "namespace foundation::brand { struct DefaultBrand {}; }\n"
        "template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand> class Permission;\n"
        "template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand> class ReadView;\n"
        "template <typename T, typename Tag, typename Brand = ::foundation::brand::DefaultBrand> class OwnedRegion;\n"
        "template <class T, class Source, class Brand = ::foundation::brand::DefaultBrand> class Borrowed;\n"
        "template <typename Tag, typename Brand = ::foundation::brand::DefaultBrand> class SharedPermissionPool;\n"
        "template <class Tag, class Brand = ::foundation::brand::DefaultBrand> struct Novel final {};\n"
        "template <class Tag, class B = ::foundation::brand::DefaultBrand> class Renamed;\n"
        "template <class Tag, class Brand = ::foundation::brand::DefaultBrand> using Handle = Permission<Tag, Brand>;\n"
        "template <class Tag> class Plain;\n"
        "template <class Tag, class Extra = int, class Brand = ::foundation::brand::DefaultBrand> class Late;\n"
        "template <class Tag, class Brand> class Permission<Tag*, Brand> {};\n"
        "struct Holder { template <class U, class Brand> friend class Plain; };\n"
        "namespace fixy {\n"
        "template <class T, class S, class Brand = ::foundation::brand::DefaultBrand> class Carried;\n"
        "namespace session { template <class T, class S> class Carried; }\n"
        "}\n"
    )
    # Two templates share the last name Carried.  Only fixy::Carried has a
    # brand, so a spelling that names fixy::session::Carried is no site.
    qualified = (
        "namespace fixy::session { Carried<int, int> inner_unbranded; }\n"
        "namespace fixy { Carried<int, int> outer_erased; }\n"                         # site
        "namespace fixy::session { fixy::Carried<int, int> qualified_outer; }\n"       # site
        "namespace fixy::session { ::fixy::session::Carried<int, int> qualified_inner; }\n"
        "namespace other { using namespace fixy::session; Carried<int, int> via_directive; }\n"
        "namespace other { using fixy::Carried; Carried<int, int> via_declaration; }\n"  # site
        "mystery::Carried<int, int> unknown_head;\n"                                    # site
        "auto scoped = fixy::Carried<int, int>::make();\n"                             # site
        "namespace elsewhere { Carried<int, int> unresolved; }\n"                      # site
    )
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        (root / "include/fixy").mkdir(parents=True)
        (root / "include/fixy/Planted.h").write_text(planted, encoding="utf-8")
        (root / "include/crucible").mkdir(parents=True)
        (root / "include/crucible/Outside.h").write_text("Permission<Tag> outside;\n", encoding="utf-8")
        (root / "include/crucible/Definitions.h").write_text(definitions, encoding="utf-8")
        ledger = root / LEDGER
        ledger.parent.mkdir(parents=True)

        branded, derive_failures = branded_templates(root)
        expect("the branded templates are derived from the parse tree, each by its qualified name",
               branded.position == {("Permission",): 1, ("ReadView",): 1, ("OwnedRegion",): 2, ("Borrowed",): 2,
                                    ("SharedPermissionPool",): 1, ("Novel",): 1, ("Renamed",): 1, ("Handle",): 1,
                                    ("Late",): 2, ("fixy", "Carried"): 2}
               and not derive_failures)
        expect("a template with no brand is not branded", ("Plain",) not in branded.position, True)
        expect("a template of the same last name in another namespace is not branded",
               ("fixy", "session", "Carried") not in branded.position, True)
        sites, unreadable, parse_failures = scan(root)
        rows = sorted(site.row for site in sites)
        expect("sixteen sites in the planted header", len(sites) == 16 and not unreadable and not parse_failures)
        for row, label in ((3, "a bare spelling"), (4, "a qualified spelling"), (5, "DefaultBrand named"),
                           (11, "an erased region"), (14, "a list over two lines"),
                           (16, "a spelling inside a template argument"), (17, "an erased spelling in an expression"),
                           (19, "an erased spelling in a macro body"), (21, "a pool member"),
                           (23, "a brand that only a comment names"),
                           (24, "a new branded template that no list names"),
                           (25, "a template whose defaulted brand has another name"),
                           (26, "an alias template with a brand"),
                           (28, "a spelling that stops two places before the brand"),
                           (29, "a spelling that stops one place before the brand"),
                           (33, "an erased brand in a macro body that a block comment splits over two lines")):
            expect(f"counted: {label}", row in rows)
        for row, label in ((6, "a branded spelling"), (8, "a line comment"), (9, "a block comment"),
                           (10, "a string literal"), (12, "a branded list over two lines"),
                           (18, "a generic brand parameter"), (20, "a branded macro body"),
                           (22, "DefaultBrand only in a comment of a branded list"),
                           (27, "a template with no brand"), (30, "a spelling that names the brand after a default"),
                           (31, "DefaultBrand as the argument of another template in the brand position"),
                           (32, "DefaultBrand only in a comment inside a nested argument")):
            expect(f"not counted: {label}", row not in rows, True)
        expect("a file outside the roots is not read", all(site.path == "include/fixy/Planted.h" for site in sites),
               True)

        (root / "include/fixy/Qualified.h").write_text(qualified, encoding="utf-8")
        qualified_rows = sorted(site.row for site in scan(root)[0] if site.path == "include/fixy/Qualified.h")
        expect("counted: the branded template that the enclosing namespace declares", 2 in qualified_rows)
        expect("counted: a qualified spelling of the branded template", 3 in qualified_rows)
        expect("counted: a using-declaration of the branded template", 6 in qualified_rows)
        expect("counted: a qualifier that no scope knows, which fails closed", 7 in qualified_rows)
        expect("counted: a template-id that qualifies a member name", 8 in qualified_rows)
        expect("counted: a name that lookup cannot resolve, which fails closed", 9 in qualified_rows)
        expect("not counted: a template of the same last name that an inner namespace declares",
               1 not in qualified_rows, True)
        expect("not counted: a qualified spelling of the template with no brand", 4 not in qualified_rows, True)
        expect("not counted: a using-directive of the namespace of the template with no brand",
               5 not in qualified_rows, True)
        expect("six sites in the qualified header", len(qualified_rows) == 6)
        (root / "include/fixy/Qualified.h").unlink()

        def captured(action) -> tuple[int, str]:
            buffer = io.StringIO()
            with contextlib.redirect_stderr(buffer):
                code = action()
            return code, buffer.getvalue()

        ledger.write_text("include/fixy/Planted.h\t16\n", encoding="utf-8")
        expect("a ledger that matches passes", captured(lambda: check(root, ledger))[0] == 0)
        ledger.write_text("include/fixy/Planted.h\t15\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("one more site than the ledger permits fails", code == 1 and "NEW SITE  include/fixy/Planted.h: 16"
               in report, True)
        clash = root / "include/crucible/Clash.h"
        clash.write_text("template <class A, class B, class Brand = ::foundation::brand::DefaultBrand> class Novel;\n",
                         encoding="utf-8")
        ledger.write_text("include/fixy/Planted.h\t16\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("two templates of one name with the brand at two positions fail",
               code == 1 and "Novel: two primary declarations" in report, True)
        clash.unlink()
        ledger.write_text("include/fixy/Planted.h\t18\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a ledger above the tree is stale", code == 2 and "STALE" in report, True)
        ledger.write_text("", encoding="utf-8")
        expect("an empty ledger refuses every site", captured(lambda: check(root, ledger))[0] == 1, True)
        ledger.unlink()
        expect("a missing ledger refuses every site", captured(lambda: check(root, ledger))[0] == 1, True)
        ledger.write_text("include/fixy/Planted.h nine\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a malformed row fails", code != 0 and "MALFORMED" in report, True)
        ledger.write_text("include/fixy/Planted.h\t16\ninclude/fixy/Planted.h\t40\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a second row for one file fails", code != 0 and "DUPLICATE line 2" in report, True)
        expect("--refresh writes a ledger that passes",
               captured(lambda: refresh(root, ledger))[0] == 0 and captured(lambda: check(root, ledger))[0] == 0)
        expect("the refreshed ledger carries the header", ledger.read_text().startswith(LEDGER_HEADER))

        (root / "include/fixy/Open.h").write_text("#define OPEN(x) Permission<x\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a list in a macro body that does not close is unreadable and fails",
               code == 1 and "UNREADABLE include/fixy/Open.h:1" in report, True)
        expect("--refresh refuses while a list is unreadable", captured(lambda: refresh(root, ledger))[0] == 1, True)
        (root / "include/fixy/Open.h").unlink()

        (root / "include/fixy/Broken.h").write_text("void f() { g(1) { } }\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a file the parser cannot read fails", code == 1 and "include/fixy/Broken.h" in report, True)
        (root / "include/fixy/Broken.h").unlink()

        previous = Path.cwd()
        os.chdir("/")
        try:
            from_slash = captured(lambda: check(root, ledger))
        finally:
            os.chdir(previous)
        expect("the report from / equals the report from the repository", from_slash == captured(
            lambda: check(root, ledger)))
    if failures:
        print(f"check-brand-drain --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-brand-drain --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode."""
    root = tsast.REPO_ROOT
    ledger = root / LEDGER
    try:
        if argv == []:
            return check(root, ledger)
        if argv == ["--self-test"]:
            return self_test()
        if argv == ["--refresh"]:
            return refresh(root, ledger)
        if argv == ["--list"]:
            sites, unreadable, failures = scan(root)
            for site in sites:
                print(f"ERASED     {site.path}:{site.row}  {site.name}")
            for site in unreadable:
                print(f"UNREADABLE {site.path}:{site.row}  {site.name}")
            for line in failures:
                print(line)
            return 0
        if argv == ["--templates"]:
            branded, failures = branded_templates(root)
            for name, position in branded.items():
                print(f"BRANDED    {name}  brand at position {position}")
            for line in failures:
                print(line)
            return 1 if failures else 0
    except tsast.KitMissing as exc:
        print(f"check-brand-drain: {exc}", file=sys.stderr)
        return 3
    print("usage: check-brand-drain.py [--list | --templates | --refresh | --self-test]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
