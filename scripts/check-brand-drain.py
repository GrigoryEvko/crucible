#!/usr/bin/env python3
"""check-brand-drain — count each spelling of a branded type that names no brand, and let no count increase.

Region identity in this tree is a brand: a trailing template parameter on
Permission, SharedPermission, SharedPermissionGuard, SharedPermissionPool,
OwnedRegion, ScopedView, Borrowed, BorrowedRef and ReadView.  A spelling
that names no brand, such as `Permission<Tag>`, is the erased identity
DefaultBrand.  Each token of that identity is interchangeable with each
other token of its tag.  foundation/Brand.h gives the rules of a brand.

THE RULE
    A site is a spelling of one of the nine templates whose argument list
    has the old arity, or names DefaultBrand.  scripts/brand-drain.txt holds
    one count per file.  A file with more sites than its entry fails: write
    the brand or a generic parameter.  A file with fewer sites than its
    entry fails as stale: run --refresh in the same commit.  An empty ledger
    makes every site a hard error, and that is the end state.

WHAT READS THE SITES
    The guard reads the parse tree of the pinned tree-sitter kit
    (scripts/tsast.py).  It reads each template_type and template_function
    node that names one of the nine templates, and the named arguments of
    its list.  A comment, a string literal and a raw string hold no node.
    A list on several lines is one node.  A macro body is one preproc_arg of
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
LEDGER = "scripts/brand-drain.txt"
SUFFIXES = (".h", ".cpp")
# The nine templates, and the arity a spelling has when it names no brand.
OLD_ARITY = {
    "Permission": 1,
    "SharedPermission": 1,
    "SharedPermissionGuard": 1,
    "SharedPermissionPool": 1,
    "OwnedRegion": 2,
    "ScopedView": 2,
    "Borrowed": 2,
    "BorrowedRef": 1,
    "ReadView": 1,
}
NAME = re.compile(r"\b(" + "|".join(OLD_ARITY) + r")\s*<")
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


def lexical_sites(text: str, path: str, first_row: int) -> tuple[list[Site], list[Site]]:
    """Return the sites and the unreadable lists of raw text, read with the lexer."""
    blanked, _ = cxx_lex.blank(text, blank_literals=True)
    sites, unreadable = [], []
    for match in NAME.finditer(blanked):
        name = match.group(1)
        row = first_row + blanked.count("\n", 0, match.start())
        count, end = argument_count(blanked, match.end() - 1)
        if count is None:
            unreadable.append(Site(path, row, name))
        elif count == OLD_ARITY[name] or ERASED.search(blanked[match.end():end]):
            sites.append(Site(path, row, name))
    return sites, unreadable


def tree_sites(tree: tsast.Tree, path: str) -> list[Site]:
    """Return the sites that the parse tree of one file holds."""
    sites = []
    for node in tree.find("template_type", "template_function"):
        name = node.child_by_field("name")
        arguments = node.child_by_field("arguments")
        if name is None or arguments is None or name.text not in OLD_ARITY:
            continue
        named = [child for child in arguments.children if child.type != "comment"]
        if len(named) == OLD_ARITY[name.text] or any(ERASED.search(child.text) for child in named):
            sites.append(Site(path, node.start[0] + 1, name.text))
    return sites


def scan(root: Path, roots: tuple[str, ...] = ROOTS) -> tuple[list[Site], list[Site], list[str]]:
    """Return every site, every unreadable list, and every parse failure under the roots.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    files = sorted(path for top in roots if (root / top).is_dir()
                   for path in (root / top).rglob("*") if path.is_file() and path.suffix in SUFFIXES)
    sites: list[Site] = []
    unreadable: list[Site] = []
    failures: list[str] = []
    for tree in tsast.parse(files, strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        source = tree.source.decode("utf-8", "replace")
        if tree.diagnostic is not None:
            if rel not in tsast.UNPARSEABLE:
                failures.append(f"{rel}: the parser cannot read this file.  Its sites are unknown")
                continue
            found, lost = lexical_sites(source, rel, 1)
            sites += found
            unreadable += lost
            continue
        sites += tree_sites(tree, rel)
        for body in tree.find("preproc_arg"):
            found, lost = lexical_sites(body.text, rel, body.start[0] + 1)
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
    )
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        (root / "include/fixy").mkdir(parents=True)
        (root / "include/fixy/Planted.h").write_text(planted, encoding="utf-8")
        (root / "include/crucible").mkdir(parents=True)
        (root / "include/crucible/Outside.h").write_text("Permission<Tag> outside;\n", encoding="utf-8")
        ledger = root / LEDGER
        ledger.parent.mkdir(parents=True)

        sites, unreadable, parse_failures = scan(root)
        rows = sorted(site.row for site in sites)
        expect("ten sites in the planted header", len(sites) == 10 and not unreadable and not parse_failures)
        for row, label in ((3, "a bare spelling"), (4, "a qualified spelling"), (5, "DefaultBrand named"),
                           (11, "an erased region"), (14, "a list over two lines"),
                           (16, "a spelling inside a template argument"), (17, "an erased spelling in an expression"),
                           (19, "an erased spelling in a macro body"), (21, "a pool member"),
                           (23, "a brand that only a comment names")):
            expect(f"counted: {label}", row in rows)
        for row, label in ((6, "a branded spelling"), (8, "a line comment"), (9, "a block comment"),
                           (10, "a string literal"), (12, "a branded list over two lines"),
                           (18, "a generic brand parameter"), (20, "a branded macro body"),
                           (22, "DefaultBrand only in a comment of a branded list")):
            expect(f"not counted: {label}", row not in rows, True)
        expect("a file outside the roots is not read", all(site.path == "include/fixy/Planted.h" for site in sites),
               True)

        def captured(action) -> tuple[int, str]:
            buffer = io.StringIO()
            with contextlib.redirect_stderr(buffer):
                code = action()
            return code, buffer.getvalue()

        ledger.write_text("include/fixy/Planted.h\t10\n", encoding="utf-8")
        expect("a ledger that matches passes", captured(lambda: check(root, ledger))[0] == 0)
        ledger.write_text("include/fixy/Planted.h\t9\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("one more site than the ledger permits fails", code == 1 and "NEW SITE  include/fixy/Planted.h: 10"
               in report, True)
        ledger.write_text("include/fixy/Planted.h\t12\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a ledger above the tree is stale", code == 2 and "STALE" in report, True)
        ledger.write_text("", encoding="utf-8")
        expect("an empty ledger refuses every site", captured(lambda: check(root, ledger))[0] == 1, True)
        ledger.unlink()
        expect("a missing ledger refuses every site", captured(lambda: check(root, ledger))[0] == 1, True)
        ledger.write_text("include/fixy/Planted.h nine\n", encoding="utf-8")
        code, report = captured(lambda: check(root, ledger))
        expect("a malformed row fails", code != 0 and "MALFORMED" in report, True)
        ledger.write_text("include/fixy/Planted.h\t10\ninclude/fixy/Planted.h\t40\n", encoding="utf-8")
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
    except tsast.KitMissing as exc:
        print(f"check-brand-drain: {exc}", file=sys.stderr)
        return 3
    print("usage: check-brand-drain.py [--list | --refresh | --self-test]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
