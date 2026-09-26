#!/usr/bin/env python3
"""check-fixy-discipline — code under a fixy-only directory spells the substrate through the fixy umbrella.

The directories in scripts/fixy-only-paths.txt are under fixy discipline.  Code
under them names a substrate type through the fixy umbrella: `fixy::fn<...>`
for the aggregator, `fixy::wrap::Refined<...>` and its peers for the Tier-1
wrappers.  A raw `crucible::safety` spelling reaches past the umbrella, so it
skips the engagement gate, leaks the positional substrate signature, and
breaks the federation cache key.

THE RULE
    A file under a fixy-only directory does not name, by any route:
      * the aggregator `crucible::safety::fn::Fn`
      * a Tier-1 wrapper `crucible::safety::X` for X in Refined, Tagged,
        Linear, Monotonic, Stale, Secret, Permission, Affine
    A route is a qualified name from `::`, a relative name that an enclosing
    namespace or a using-directive in force makes reach the substrate, a
    namespace alias (and an alias of an alias), or a using-declaration that
    brings the name in.  A name counts with or without template arguments.
    A using-directive that nominates `crucible::safety` or
    `crucible::safety::fn` is itself a reach past the umbrella, because every
    unqualified name after it can resolve there.

SUPPRESSION
    A comment that holds `FIXY-DISCIPLINE-OK: <reason>` inside the statement
    of the site, or trailing on its last line, exempts the site.  A marker in a
    comment above the statement exempts nothing.  scripts/fixy-discipline-allowlist.txt
    lists grandfathered files, one path per line.  A listed file that no longer
    names the substrate is STALE, and the guard fails until the entry goes.

WHAT READS THE CODE
    The parse tree of the pinned tree-sitter kit (scripts/tsast.py).  A
    comment and a string literal hold no name node, so a mention in prose
    counts for nothing.

Usage
    check-fixy-discipline.py              scan the fixy-only directories
    check-fixy-discipline.py --list       print the fixy-only directories
    check-fixy-discipline.py --self-test  plant each case and examine each verdict

Exit 0 clean, 1 on a violation or a file that does not parse, 2 on a stale
allowlist entry, a missing path list, or a failed self-test, 3 when the kit is
not installed.
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

PATHS_FILE = "scripts/fixy-only-paths.txt"
ALLOWLIST = "scripts/fixy-discipline-allowlist.txt"
MARKER = "FIXY-DISCIPLINE-OK"
SAFETY = ("crucible", "safety")
AGGREGATOR = SAFETY + ("fn", "Fn")
PRIMITIVES = ("Refined", "Tagged", "Linear", "Monotonic", "Stale", "Secret", "Permission", "Affine")
BANNED = frozenset({AGGREGATOR} | {SAFETY + (name,) for name in PRIMITIVES})
NOMINATED = frozenset({SAFETY, SAFETY + ("fn",)})
# The outermost name nodes: a name inside one of these is part of it.
NAME_NODES = ("qualified_identifier", "template_type", "template_function", "type_identifier")


@dataclass(frozen=True)
class Site:
    """One reach past the umbrella."""

    path: str
    line: int
    spelling: str
    route: str


def fixy_only_paths(root: Path) -> list[str] | None:
    """Return the fixy-only directories, or None when the list is missing.

    Args:
        root: The repository root

    Returns:
        The directories in file order
    """
    listing = root / PATHS_FILE
    if not listing.is_file():
        return None
    paths = [line.split("#", 1)[0].strip() for line in listing.read_text().splitlines()]
    return [path for path in paths if path]


def banned_spelling(parts: tuple[str, ...]) -> str | None:
    """Return the banned spelling that a resolved name denotes, or None.

    Args:
        parts: The name from the root, without template arguments

    Returns:
        The spelling for the report, for example `safety::Refined<`
    """
    if parts in BANNED:
        return "::".join(parts[1:]) + "<"
    return None


def visible_at(using: tsast.UsingDecl, at: tsast.Node) -> bool:
    """Report whether a using declaration is in force at a node.

    It is in force when its block or namespace body holds the node and it
    ends before the node starts.

    Args:
        using: A using declaration or directive of the same file
        at: The node where a name is used

    Returns:
        True when the declaration applies to the name
    """
    scope = using.scope
    return (scope.start <= at.start and at.end <= scope.end) and using.node.end <= at.start


def denotations(parts: tuple[str, ...], is_global: bool, at: tsast.Node,
                aliases: list[tsast.NamespaceAlias],
                directives: list[tsast.UsingDecl]) -> list[tuple[tuple[str, ...], str]]:
    """Return each name from the root that a spelling can denote at a node, with its route.

    The rule asks whether a spelling can reach the substrate, so a relative
    name gets one candidate for each enclosing namespace and one for each
    using-directive in force.  A leading namespace alias resolves first.

    Complexity: O(depth + directives) for each spelling.

    Args:
        parts: The parts of the spelling, without template arguments
        is_global: True when the spelling starts at `::`
        at: The node where the spelling is used
        aliases: The namespace aliases of the file
        directives: The using-directives of the file

    Returns:
        Each candidate and the route that reaches it, innermost first
    """
    if is_global:
        return [(parts, "")]
    resolved = tsast.resolve_namespace(parts, at, aliases)
    route = " (reached through a namespace alias)" if resolved != parts else ""
    enclosing = tsast.namespace_path(at, skip_inline=True)
    found = [(enclosing[:depth] + resolved, route) for depth in range(len(enclosing), -1, -1)]
    for directive in directives:
        if visible_at(directive, at):
            target = tsast.resolve_namespace(directive.target, directive.node, aliases,
                                             is_global=directive.is_global)
            found.append((target + resolved, " (reached through a using-directive)"))
    return found


def reach(parts: tuple[str, ...], is_global: bool, at: tsast.Node, aliases: list[tsast.NamespaceAlias],
          directives: list[tsast.UsingDecl], wanted: frozenset) -> tuple[tuple[str, ...], str] | None:
    """Return the first candidate of a spelling that lies in a wanted set, with its route.

    Args:
        parts: The parts of the spelling
        is_global: True when the spelling starts at `::`
        at: The node where the spelling is used
        aliases: The namespace aliases of the file
        directives: The using-directives of the file
        wanted: The full names to look for

    Returns:
        The candidate and its route, or None when no candidate is wanted
    """
    for candidate, route in denotations(parts, is_global, at, aliases, directives):
        if candidate in wanted:
            return candidate, route
    return None


def sites_in(tree: tsast.Tree, rel: str) -> list[Site]:
    """Return every reach past the umbrella in one parsed file, after the markers apply.

    Complexity: linear in the node count of the file, times the candidates of each name.

    Args:
        tree: The parsed file
        rel: Its path relative to the repository root

    Returns:
        The sites that no marker exempts
    """
    aliases = tsast.namespace_aliases(tree)
    usings = tsast.using_names(tree)
    directives = [using for using in usings if using.is_directive]
    declarations = [using for using in usings if not using.is_directive and using.target]
    found: list[tuple[Site, tsast.Node]] = []
    for using in usings:
        others = [directive for directive in directives if directive.node.index != using.node.index]
        wanted = NOMINATED if using.is_directive else BANNED
        hit = reach(using.target, using.is_global, using.node, aliases, others, wanted)
        if hit is None:
            continue
        if using.is_directive:
            found.append((Site(rel, using.node.line, "::".join(hit[0][1:]) + "::",
                               " (a using-directive nominates the substrate)"), using.node))
        else:
            found.append((Site(rel, using.node.line, banned_spelling(hit[0]),
                               " (a using-declaration brings it in)"), using.node))
    for node in tree.find(*NAME_NODES):
        if node.parent is not None and node.parent.type in NAME_NODES:
            continue
        if node.ancestor_of_type("using_declaration", "namespace_alias_definition") is not None:
            continue
        spelled = tsast.qualified_parts(node)
        if spelled is None or not spelled[1]:
            continue
        hit = reach(spelled[1], spelled[0], node, aliases, directives, BANNED)
        if hit is None and len(spelled[1]) == 1:
            for using in declarations:
                if using.target[-1] == spelled[1][0] and visible_at(using, node):
                    through = reach(using.target, using.is_global, using.node, aliases, directives, BANNED)
                    if through is not None:
                        hit = (through[0], " (reached through a using-declaration)")
                        break
        if hit is not None:
            found.append((Site(rel, node.line, banned_spelling(hit[0]), hit[1]), node))
    return [site for site, node in found if not tsast.has_marker(tsast.enclosing_statement(node), MARKER)]


def check(root: Path) -> int:
    """Scan every fixy-only directory and report.

    Args:
        root: The repository root

    Returns:
        0 clean, 1 on a violation or a parse failure, 2 on a stale entry or a missing path list
    """
    paths = fixy_only_paths(root)
    if paths is None:
        print(f"check-fixy-discipline: {PATHS_FILE} is missing, so no directory is under fixy discipline.",
              file=sys.stderr)
        return 2
    files: list[Path] = []
    for directory in paths:
        base = root / directory
        if base.is_dir():
            files += sorted(p for p in base.rglob("*") if tsast.is_in_cpp_scope(p.relative_to(root)) and p.is_file())
    allowlist = root / ALLOWLIST
    entries: list[str] = []
    if allowlist.is_file():
        entries = [line.split("#", 1)[0].strip() for line in allowlist.read_text().splitlines()]
        entries = [entry for entry in entries if entry]
    allowed = set(entries)
    live: set[str] = set()
    violations = 0
    unreadable = 0
    for tree in tsast.parse(files, strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            print(f"FIXY-DISCIPLINE parse failure: {rel} — the parser cannot read it, so its spellings are "
                  f"unknown.\n  {tree.diagnostic}", file=sys.stderr)
            unreadable += 1
            continue
        for site in sites_in(tree, rel):
            live.add(rel)
            if rel in allowed:
                continue
            wrap = site.spelling.removesuffix("<").removesuffix("::").split("::")[-1]
            print(f"FIXY-DISCIPLINE violation: {site.path}:{site.line} — raw {site.spelling} spelling{site.route}.  "
                  f"Use fixy::wrap::{wrap} or fixy::fn<...> instead.", file=sys.stderr)
            violations += 1
    if violations or unreadable:
        print(f"\ncheck-fixy-discipline detected {violations} reach-past-the-umbrella site(s) and {unreadable} "
              f"unreadable file(s) under the directories of {PATHS_FILE}.\n"
              f"  (1) Spell the type through the umbrella: fixy::fn<...> or fixy::wrap::<Name>.\n"
              f"  (2) A deliberate round-trip fixture takes `// {MARKER}: <reason>` in its statement.\n"
              f"  (3) A grandfathered file awaiting its migration goes on {ALLOWLIST}.", file=sys.stderr)
        return 1
    stale = [entry for entry in entries if entry not in live]
    for entry in stale:
        print(f"FIXY-DISCIPLINE stale: {entry} — STALE allowlist entry (no raw safety:: substrate spelling remains in "
              f"this file; it migrated to fixy::* — remove it from the allowlist).", file=sys.stderr)
    if stale:
        return 2
    print(f"check-fixy-discipline: clean — {len(files)} file(s) under {len(paths)} fixy-only directories, no "
          f"reach past the umbrella, no stale allowlist entry.", file=sys.stderr)
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

    def captured(root: Path) -> tuple[int, str]:
        """Run the check on a planted root and return its code and its stderr."""
        buffer = io.StringIO()
        with contextlib.redirect_stderr(buffer):
            code = check(root)
        return code, buffer.getvalue()

    planted = "examples/fn/planted.cpp"
    cases: list[tuple[str, str, list[int]]] = [
        ("the aggregator from :: is caught", "using D = ::crucible::safety::fn::Fn<int>;\n", [1]),
        ("each Tier-1 wrapper is caught",
         "".join(f"using P{i} = ::crucible::safety::{name}<int>;\n" for i, name in enumerate(PRIMITIVES)),
         list(range(1, len(PRIMITIVES) + 1))),
        ("a relative spelling inside namespace crucible is caught",
         "namespace crucible { using D = safety::Refined<int, int>; }\n", [1]),
        ("a spelling split after :: is caught", "using D = ::crucible::safety::\n    Refined<int, int>;\n", [1]),
        ("an alias, an alias of an alias and an alias to fn are caught",
         "namespace sf = crucible::safety;\nnamespace ch = sf;\nnamespace fnn = ::crucible::safety::fn;\n"
         "using A = sf::Refined<int, int>;\nusing B = ch::Linear<int>;\nusing C = fnn::Fn<int>;\n", [4, 5, 6]),
        ("an alias with its target on the next line is caught",
         "namespace sf =\n    crucible::safety;\nusing D = sf::Refined<int, int>;\n", [3]),
        ("a using-declaration and the bare name after it are caught",
         "using ::crucible::safety::Refined;\nusing D = Refined<int, int>;\n", [1, 2]),
        ("a relative using-declaration inside namespace crucible and the bare name are caught",
         "namespace crucible {\nusing safety::Refined;\nusing D = Refined<int, int>;\n}\n", [2, 3]),
        ("a using-directive for safety is caught", "using namespace ::crucible::safety;\n", [1]),
        ("a spelling that a using-directive for crucible makes reach the substrate is caught",
         "using namespace crucible;\nusing D = safety::Refined<int, int>;\n", [2]),
        ("a using-directive after the spelling does not apply to it",
         "using D = safety::Refined<int, int>;\nusing namespace crucible;\n", []),
        ("a wrapper name in another namespace is clean",
         "namespace other { template <class> struct Refined {}; }\nusing D = other::Refined<int>;\n", []),
        ("a non-substrate member of the substrate namespace is clean",
         "namespace sf = crucible::safety;\nnamespace fnn = crucible::safety::fn;\nusing T = fnn::pred::True;\n"
         "using R = sf::Row<int>;\n", []),
        ("an alias to another namespace is clean", "namespace ef = crucible::effects;\nusing R = ef::Refined<int>;\n",
         []),
        ("the spelling in a string literal or a trailing comment is clean",
         'inline const char* text = "safety::Refined<int, int>";\nint value = 0;  // safety::Refined<int, int>\n', []),
        ("a marker on the continuation line exempts the statement",
         "using D = ::crucible::safety::Tagged<\n    int, int>;  // FIXY-DISCIPLINE-OK: planted\n", []),
        ("a marker on the next statement does not leak backwards",
         "using W = ::crucible::safety::Linear<\n    int>;\n"
         "using N = ::crucible::safety::Secret<int>;  // FIXY-DISCIPLINE-OK: planted\n", [1]),
        ("a marker in a comment above the statement exempts nothing",
         "// FIXY-DISCIPLINE-OK: planted above\nusing D = ::crucible::safety::Tagged<int, int>;\n", [2]),
    ]
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        (root / "scripts").mkdir()
        (root / "examples/fn").mkdir(parents=True)
        (root / PATHS_FILE).write_text("# planted\nexamples/fn\n")
        (root / ALLOWLIST).write_text("")
        for name, text, lines in cases:
            (root / planted).write_text(text)
            tree = next(tsast.parse([root / planted], strict=False))
            got = sorted({site.line for site in sites_in(tree, planted)})
            expect(name, tree.diagnostic is None and got == lines)
            if got != lines:
                print(f"       expected lines {lines}, got {got}")

        (root / planted).write_text("using D = ::crucible::safety::Refined<int, int>;\n")
        code, report = captured(root)
        expect("a violation exits 1 and names the spelling", code == 1 and "raw safety::Refined< spelling" in report)
        (root / ALLOWLIST).write_text(f"{planted}\nexamples/fn/migrated_away.cpp\n")
        code, report = captured(root)
        expect("a stale entry exits 2, and the live entry is not stale",
               code == 2 and "stale: examples/fn/migrated_away.cpp" in report and f"stale: {planted}" not in report)
        (root / planted).write_text("namespace sf = crucible::safety;\nusing D = sf::Refined<int, int>;\n")
        (root / ALLOWLIST).write_text(f"{planted}\n")
        expect("a file whose only site is reached through an alias counts as live", captured(root)[0] == 0)
        previous = Path.cwd()
        os.chdir("/")
        try:
            from_slash = captured(root)
        finally:
            os.chdir(previous)
        expect("the report from / equals the report from the repository", from_slash == captured(root))
        (root / planted).write_text("namespace broken { void f() { g(1) { } } }\n")
        code, report = captured(root)
        expect("a file the parser cannot read exits 1", code == 1 and "parse failure" in report)
        (root / PATHS_FILE).unlink()
        expect("a missing path list exits 2", captured(root)[0] == 2)

    if failures:
        print(f"check-fixy-discipline --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print("check-fixy-discipline --self-test: every case passes.")
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
            return check(tsast.REPO_ROOT)
        if argv == ["--self-test"]:
            return self_test()
        if argv == ["--list"]:
            print("check-fixy-discipline: fixy-only directories:")
            for path in fixy_only_paths(tsast.REPO_ROOT) or []:
                print(f"  {path}")
            return 0
    except tsast.KitMissing as exc:
        print(f"check-fixy-discipline: {exc}", file=sys.stderr)
        return 3
    print("usage: check-fixy-discipline.py [--list | --self-test]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
