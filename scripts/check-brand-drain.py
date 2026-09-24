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
    count of arguments that a spelling with no brand holds.  A superseded
    `_Name.h` header is out of scope.  Two primary templates of one name
    with the brand at two positions fail, because a spelling of that name
    then has no one reading.  A list that did not derive its templates
    missed OwnedMmap and NumaPlacement when each gained a brand, so a
    spelling of either on the erased identity was never counted.

THE RULE
    A site is a spelling of a branded template whose argument list stops at
    or before the brand, or names DefaultBrand.  scripts/brand-drain.txt
    holds one count per file.  A file with more sites than its entry fails:
    write the brand or a generic parameter.  A file with fewer sites than
    its entry fails as stale: run --refresh in the same commit.  An empty
    ledger makes every site a hard error, and that is the end state.

WHAT READS THE SITES
    The guard reads the parse tree of the pinned tree-sitter kit
    (scripts/tsast.py).  It reads each template_type and template_function
    node that names a branded template, and the named arguments of its
    list.  A comment, a string literal and a raw string hold no node.  A
    list on several lines is one node.  A macro body is one preproc_arg of
    raw text, and a file in tsast.UNPARSEABLE has no tree.  The guard reads
    those with the lexer of scripts/cxx_lex.py, and it counts the arguments
    of each list itself.  The guard cannot read a list there that does not
    close, and that list fails.  An alias counts one time, at the alias, and
    not at each use.

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
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cxx_lex  # noqa: E402
import tsast  # noqa: E402

ROOTS = ("include/foundation", "include/fixy", "test/foundation", "test/fixy")
TEMPLATE_ROOT = "include"
LEDGER = "scripts/brand-drain.txt"
SUFFIXES = (".h", ".cpp")
BRAND_PARAMETER = "Brand"
SUPERSEDED_PREFIX = "_"
ERASED = re.compile(r"\bDefaultBrand\b")
LEDGER_HEADER = (
    "# scripts/brand-drain.txt — the sites on the erased brand, one count per\n"
    "# file.  scripts/check-brand-drain.py reads this ledger.  A count can only\n"
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


def argument_count(text: str, start: int) -> tuple[int | None, int]:
    """Count the top-level arguments of the list that opens at text[start] == '<'.

    `->` does not close the list, and a bracketed argument keeps its commas.
    The count is incorrect for a comparison inside the list.  The parse tree
    prevents that error, so this count reads only raw text.

    Complexity: linear in the length of the list.

    Returns:
        (count, index of the closing '>'), or (None, start) when the list does not close
    """
    depth, count, index = 0, 1, start
    while index < len(text):
        char = text[index]
        if char == "<":
            depth += 1
        elif char == ">" and not (index > 0 and text[index - 1] == "-"):
            depth -= 1
            if depth == 0:
                return count, index
        elif char == "," and depth == 1:
            count += 1
        elif char in "([{":
            close = {"(": ")", "[": "]", "{": "}"}[char]
            inner = 1
            index += 1
            while index < len(text) and inner:
                inner += (text[index] == char) - (text[index] == close)
                index += 1
            continue
        index += 1
    return None, start


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


def parameter_default(node: tsast.Node) -> str:
    """Return the default argument text of one template parameter, or an empty string."""
    for field in ("default_type", "default_value"):
        value = node.child_by_field(field)
        if value is not None:
            return value.text
    return ""


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


def branded_templates(root: Path) -> tuple[dict[str, int], list[str]]:
    """Derive each branded template under include/ and the position of its brand.

    Complexity: linear in the size of the files that hold the bytes `Brand`.

    Returns:
        The position of the brand for each template name, and one line for
        each name the guard cannot read, or reads with two positions

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    base = root / TEMPLATE_ROOT
    files = sorted(path for path in base.rglob("*") if path.is_file() and path.suffix in SUFFIXES
                   and not path.name.startswith(SUPERSEDED_PREFIX) and b"Brand" in path.read_bytes()) \
        if base.is_dir() else []
    positions: dict[str, set[int]] = {}
    failures: list[str] = []
    for tree in tsast.parse(files, strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            if rel not in tsast.UNPARSEABLE:
                failures.append(f"{rel}: the parser cannot read this file.  Its branded templates are unknown")
            continue
        for declaration in tree.find("template_declaration"):
            name = declared_template(declaration)
            parameters = declaration.child_by_field("parameters")
            if name is None or parameters is None:
                continue
            listed = [child for child in parameters.children if child.type != "comment"]
            for index, parameter in enumerate(listed):
                if parameter_name(parameter) == BRAND_PARAMETER or ERASED.search(parameter_default(parameter)):
                    positions.setdefault(name, set()).add(index)
                    break
    branded: dict[str, int] = {}
    for name in sorted(positions):
        if len(positions[name]) != 1:
            failures.append(f"{name}: two primary templates of this name carry the brand at positions "
                            f"{sorted(positions[name])}, so a spelling of the name has no one reading.  Rename one")
            continue
        branded[name] = next(iter(positions[name]))
    return branded, failures


def lexical_sites(text: str, path: str, first_row: int, branded: dict[str, int]) -> tuple[list[Site], list[Site]]:
    """Return the sites and the unreadable lists of raw text, read with the lexer."""
    if not branded:
        return [], []
    blanked, _ = cxx_lex.blank(text, blank_literals=True)
    pattern = re.compile(r"\b(" + "|".join(map(re.escape, sorted(branded))) + r")\s*<")
    sites, unreadable = [], []
    for match in pattern.finditer(blanked):
        name = match.group(1)
        row = first_row + blanked.count("\n", 0, match.start())
        count, end = argument_count(blanked, match.end() - 1)
        if count is None:
            unreadable.append(Site(path, row, name))
        elif count <= branded[name] or ERASED.search(blanked[match.end():end]):
            sites.append(Site(path, row, name))
    return sites, unreadable


def tree_sites(tree: tsast.Tree, path: str, branded: dict[str, int]) -> list[Site]:
    """Return the sites that the parse tree of one file holds."""
    sites = []
    for node in tree.find("template_type", "template_function"):
        name = node.child_by_field("name")
        arguments = node.child_by_field("arguments")
        if name is None or arguments is None or name.text not in branded:
            continue
        named = [child for child in arguments.children if child.type != "comment"]
        if len(named) <= branded[name.text] or any(ERASED.search(child.text) for child in named):
            sites.append(Site(path, node.start[0] + 1, name.text))
    return sites


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
    for tree in tsast.parse(files, strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        source = tree.source.decode("utf-8", "replace")
        if tree.diagnostic is not None:
            if rel not in tsast.UNPARSEABLE:
                failures.append(f"{rel}: the parser cannot read this file.  Its sites are unknown")
                continue
            found, lost = lexical_sites(source, rel, 1, branded)
            sites += found
            unreadable += lost
            continue
        sites += tree_sites(tree, rel, branded)
        for body in tree.find("preproc_arg"):
            found, lost = lexical_sites(body.text, rel, body.start[0] + 1, branded)
            sites += found
            unreadable += lost
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
    )
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        (root / "include/fixy").mkdir(parents=True)
        (root / "include/fixy/Planted.h").write_text(planted, encoding="utf-8")
        (root / "include/crucible").mkdir(parents=True)
        (root / "include/crucible/Outside.h").write_text("Permission<Tag> outside;\n", encoding="utf-8")
        (root / "include/crucible/Definitions.h").write_text(definitions, encoding="utf-8")
        (root / "include/crucible/_Superseded.h").write_text(
            "template <class A, class B, class Brand = DefaultBrand> class Novel;\n", encoding="utf-8")
        ledger = root / LEDGER
        ledger.parent.mkdir(parents=True)

        branded, derive_failures = branded_templates(root)
        expect("the branded templates are derived from the parse tree",
               branded == {"Permission": 1, "ReadView": 1, "OwnedRegion": 2, "Borrowed": 2,
                           "SharedPermissionPool": 1, "Novel": 1, "Renamed": 1, "Handle": 1, "Late": 2}
               and not derive_failures)
        expect("a template with no brand is not branded", "Plain" not in branded, True)
        sites, unreadable, parse_failures = scan(root)
        rows = sorted(site.row for site in sites)
        expect("fifteen sites in the planted header", len(sites) == 15 and not unreadable and not parse_failures)
        for row, label in ((3, "a bare spelling"), (4, "a qualified spelling"), (5, "DefaultBrand named"),
                           (11, "an erased region"), (14, "a list over two lines"),
                           (16, "a spelling inside a template argument"), (17, "an erased spelling in an expression"),
                           (19, "an erased spelling in a macro body"), (21, "a pool member"),
                           (23, "a brand that only a comment names"),
                           (24, "a new branded template that no list names"),
                           (25, "a template whose defaulted brand has another name"),
                           (26, "an alias template with a brand"),
                           (28, "a spelling that stops two places before the brand"),
                           (29, "a spelling that stops one place before the brand")):
            expect(f"counted: {label}", row in rows)
        for row, label in ((6, "a branded spelling"), (8, "a line comment"), (9, "a block comment"),
                           (10, "a string literal"), (12, "a branded list over two lines"),
                           (18, "a generic brand parameter"), (20, "a branded macro body"),
                           (22, "DefaultBrand only in a comment of a branded list"),
                           (27, "a template with no brand"), (30, "a spelling that names the brand after a default")):
            expect(f"not counted: {label}", row not in rows, True)
        expect("a file outside the roots is not read", all(site.path == "include/fixy/Planted.h" for site in sites),
               True)

        def captured(action) -> tuple[int, str]:
            buffer = io.StringIO()
            with contextlib.redirect_stderr(buffer):
                code = action()
            return code, buffer.getvalue()

        ledger.write_text("include/fixy/Planted.h\t15\n", encoding="utf-8")
        expect("a ledger that matches passes", captured(lambda: check(root, ledger))[0] == 0)
        ledger.write_text("include/fixy/Planted.h\t14\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("one more site than the ledger permits fails", code == 1 and "NEW SITE  include/fixy/Planted.h: 15"
               in report, True)
        clash = root / "include/crucible/Clash.h"
        clash.write_text("template <class A, class B, class Brand = ::foundation::brand::DefaultBrand> class Novel;\n",
                         encoding="utf-8")
        ledger.write_text("include/fixy/Planted.h\t15\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("two templates of one name with the brand at two positions fail",
               code == 1 and "Novel: two primary templates" in report, True)
        clash.unlink()
        ledger.write_text("include/fixy/Planted.h\t17\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a ledger above the tree is stale", code == 2 and "STALE" in report, True)
        ledger.write_text("", encoding="utf-8")
        expect("an empty ledger refuses every site", captured(lambda: check(root, ledger))[0] == 1, True)
        ledger.unlink()
        expect("a missing ledger refuses every site", captured(lambda: check(root, ledger))[0] == 1, True)
        ledger.write_text("include/fixy/Planted.h nine\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a malformed row fails", code != 0 and "MALFORMED" in report, True)
        ledger.write_text("include/fixy/Planted.h\t15\ninclude/fixy/Planted.h\t40\n", encoding="utf-8")
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
