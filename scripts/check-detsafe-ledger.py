#!/usr/bin/env python3
"""check-detsafe-ledger — no header on the hashing path reaches the hardware ledger, read from the parse tree.

A measured verdict may change how fast Crucible runs.  It may never change
what Crucible computes (axiom 8, DetSafe).  The ledger caches answers such
as "is AVX-512 faster than AVX2 on this part", and each answer is a property
of one machine.  So the ledger may steer a scheduler, size a tile or pick a
thread count, but it must not reach content_hash, merkle_hash, the memory
plan, the choice of a BITEXACT recipe, or the per-Cog identity that keys the
kernel cache.  CogMimic draws the same line for itself: calibrated
throughput is not folded into the binary-compatibility class.

WHAT IS CHECKED
    For each header of the hashing path (ROOTS below), the guard walks the
    transitive include closure on the parse tree of the pinned tree-sitter
    kit, and it fails when the closure holds a file under a directory named
    `ledger` in include/.
      * An include in every preprocessor branch is followed, a dead branch
        too, so the closure can only be larger than the one the compiler
        sees.
      * An angle include resolves against include/.  A quoted include
        resolves first against the directory of the file, as the compiler
        does.  An include that resolves to neither is a system or a
        third-party header, and nothing there can reach the ledger.
      * A computed include must resolve through an object-like macro of the
        same file, or it is a violation, because the guard cannot see where
        it goes.
    It also fails when a file of the closure names a ledger namespace, a
    namespace `ledger` directly under a project root, without the include:
      * a qualified name, spelled from `::` or relative to an enclosing
        namespace, so `ledger::Verdict` inside namespace crucible counts;
      * a namespace definition, which is how a forward declaration dodges
        the include;
      * a using-directive or a using-declaration, qualified or not, and a
        namespace alias;
      * the words `root::ledger` or `ledger::` in a macro body.
    Every name comes from the nodes of the parse tree, so a comment inside a
    qualified name does not hide it.  A relative name can reach the ledger in
    three ways, and the guard asks each one, which can only find more:
      * from an enclosing namespace;
      * through a namespace alias, `namespace cr = crucible;` then
        `cr::ledger`;
      * through a using-directive, `using namespace crucible;` then
        `ledger::Verdict`.
    The aliases and the directives come from every file of every closure, and
    the guard ignores their scope.  That also can only find more.
    A comment and a string literal name nothing, so a hashing-path header
    may say in prose why it stays clear of the ledger.

NO ALLOWLIST
    An exception here is a DetSafe violation with paperwork.  If the guard
    fires, move the consumer off the hashing path.

ROOTS
    Each root is checked for existence.  A root that is gone is exit 2,
    because a guard whose root list has rotted passes for the wrong reason.

Exit 0 clean, 1 on a violation or a parse failure, 2 on a missing root, a
usage error or a failed self-test, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import sys
import tempfile
from collections import deque
from collections.abc import Iterator
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402
from cxx_lex import blank, line_of, splice  # noqa: E402

# The hashing path.
ROOTS: tuple[str, ...] = (
    # content_hash and merkle_hash themselves
    "include/crucible/Types.h",
    "include/crucible/MerkleDag.h",
    "include/crucible/DimHash.h",
    "include/crucible/TraceGraph.h",
    # the IR whose shape the hashes read
    "include/crucible/Graph.h",
    "include/crucible/Expr.h",
    "include/crucible/ExprPool.h",
    "include/crucible/CKernel.h",
    "include/crucible/StorageNbytes.h",
    # the memory plan
    "include/crucible/PoolAllocator.h",
    # the deterministic RNG: the same (counter, key) gives the same bits
    "include/crucible/Philox.h",
    # the choice of a BITEXACT recipe
    "include/crucible/NumericalRecipe.h",
    "include/crucible/RecipeRegistry.h",
    # the on-disk form of all of the above
    "include/crucible/Serialize.h",
    # the per-Cog identity and the kernel-cache key that comes from it
    "include/crucible/cog/CogIdentity.h",
    "include/crucible/mimic/CogMimic.h",
)
FORBIDDEN_NAMESPACE = "ledger"


def project_roots(root: Path) -> frozenset[str]:
    """Return the name of each directory directly under include/."""
    base = root / "include"
    return frozenset(path.name for path in base.iterdir() if path.is_dir()) if base.is_dir() else frozenset()


def is_ledger_file(rel: str) -> bool:
    """Return True for a file under a directory named ledger in include/."""
    parts = rel.split("/")
    return parts[0] == "include" and FORBIDDEN_NAMESPACE in parts[1:-1]


def is_ledger_namespace(path: tuple[str, ...], roots: frozenset[str]) -> bool:
    """Return True when a namespace path starts with a project root and then ledger."""
    return len(path) >= 2 and path[0] in roots and path[1] == FORBIDDEN_NAMESPACE


def resolve_include(root: Path, rel: str, spelled: str) -> str | None:
    """Return the repo-relative file that one include names, or None for a system header.

    Args:
        root: The scan root
        rel: The including file, relative to the scan root
        spelled: The include path with its delimiters

    Returns:
        The included file, relative to the scan root, or None
    """
    body = spelled[1:-1].strip()
    candidates = [(root / rel).parent / body] if spelled.startswith('"') else []
    candidates.append(root / "include" / body)
    for candidate in candidates:
        if candidate.is_file():
            return os.path.relpath(candidate.resolve(), root.resolve())
    return None


# A namespace path, outermost name first.
NsPath = tuple[str, ...]
# A name as written: whether it starts at `::`, and its parts, outermost first.
Written = tuple[bool, NsPath]

# Leaves whose own text is one name.  Reading the text of a leaf that the
# parser has already classified reads a token, not structure.
NAME_LEAVES = frozenset({"identifier", "namespace_identifier", "type_identifier", "field_identifier"})


def named_children(node: tsast.Node) -> list[tsast.Node]:
    """Return the children of a node that are not comments."""
    return [child for child in node.children if child.type != "comment"]


def written_name(node: tsast.Node) -> Written:
    """Return the full name that one name node spells, comments skipped.

    Handles a leaf name, a nested_namespace_specifier (the target of an alias
    and the name of a namespace definition), a qualified_identifier, and a
    template_type or template_function through its name field.  A part the
    guard cannot name comes back as "?", which matches no namespace.

    A nested_namespace_specifier starts at `::` when its node starts before
    its first child, because the only token that can stand there is `::`.
    """
    if node.type in NAME_LEAVES:
        return False, (node.text,)
    if node.type == "nested_namespace_specifier":
        children = named_children(node)
        is_global = bool(children) and children[0].start != node.start
        parts: list[str] = []
        for child in children:
            parts.extend(written_name(child)[1])
        return is_global, tuple(parts)
    if node.type == "qualified_identifier":
        scope, name = node.child_by_field("scope"), node.child_by_field("name")
        scope_parts = written_name(scope)[1] if scope is not None else ()
        name_parts = written_name(name)[1] if name is not None else ("?",)
        return scope is None, scope_parts + name_parts
    if node.type in ("template_type", "template_function"):
        name = node.child_by_field("name")
        return (False, written_name(name)[1]) if name is not None else (False, ("?",))
    return False, ("?",)


def namespace_path(node: tsast.Node) -> NsPath:
    """Return the names of the namespaces that enclose a node, outermost first."""
    parts: list[str] = []
    owner = node.parent
    while owner is not None:
        if owner.type == "namespace_definition":
            named = owner.child_by_field("name")
            parts[:0] = written_name(named)[1] if named is not None else ("(anonymous)",)
        owner = owner.parent
    return tuple(parts)


def outermost_qualified(tree: tsast.Tree) -> Iterator[tsast.Node]:
    """Yield each qualified_identifier that no other qualified_identifier holds as its name."""
    for node in tree.find("qualified_identifier"):
        parent = node.parent
        if not (parent is not None and parent.type == "qualified_identifier" and node.field == "name"):
            yield node


def alias_target(node: tsast.Node) -> tsast.Node | None:
    """Return the node that a namespace_alias_definition names as its target."""
    return next((child for child in named_children(node) if child.field != "name"), None)


def using_target(node: tsast.Node) -> tsast.Node | None:
    """Return the name that a using-directive or a using-declaration nominates."""
    return next(iter(named_children(node)), None)


class NameTables:
    """The namespace aliases and the using-directive targets of the files that a scan reads.

    Each alias maps to every absolute path that it can stand for, and each
    directive target is every absolute path that it can nominate.  The tables
    ignore scope, so a lookup can only find more paths than the compiler does.
    """

    def __init__(self) -> None:
        self.written_aliases: dict[str, list[tuple[Written, NsPath]]] = {}
        self.written_directives: list[tuple[Written, NsPath]] = []
        self.aliases: dict[str, frozenset[NsPath]] = {}
        self.directives: frozenset[NsPath] = frozenset()

    def add(self, tree: tsast.Tree) -> None:
        """Record the aliases and the using-directive targets of one parsed file."""
        for node in tree.find("namespace_alias_definition"):
            name, target = node.child_by_field("name"), alias_target(node)
            if name is not None and target is not None:
                self.written_aliases.setdefault(name.text, []).append((written_name(target), namespace_path(node)))
        for node in tree.find("using_declaration"):
            target = using_target(node)
            if target is not None:
                self.written_directives.append((written_name(target), namespace_path(node)))

    def seal(self) -> None:
        """Resolve every alias to a fixpoint, then every directive target."""
        for alias in self.written_aliases:
            self.aliases[alias] = self.alias_paths(alias, frozenset())
        self.directives = frozenset(path for written, enclosing in self.written_directives
                                    for path in self.expand(written, enclosing, use_directives=False))

    def alias_paths(self, alias: str, visiting: frozenset[str]) -> frozenset[NsPath]:
        """Return every absolute path that one alias can stand for; a cycle stops the walk."""
        if alias in visiting:
            return frozenset()
        paths: set[NsPath] = set()
        for written, enclosing in self.written_aliases[alias]:
            paths.update(self.expand(written, enclosing, use_directives=False, visiting=visiting | {alias}))
        return frozenset(paths)

    def expand(self, written: Written, enclosing: NsPath, *, use_directives: bool = True,
               visiting: frozenset[str] = frozenset()) -> set[NsPath]:
        """Return every absolute path that a written name can mean at a point with this enclosing path.

        Complexity: linear in the enclosing depth plus the number of alias and directive paths.
        """
        is_global, parts = written
        paths: set[NsPath] = {parts} if is_global else {enclosing[:depth] + parts
                                                        for depth in range(len(enclosing) + 1)}
        if parts and parts[0] in self.written_aliases:
            known = self.aliases.get(parts[0])
            for base in known if known is not None else self.alias_paths(parts[0], visiting):
                paths.add(base + parts[1:])
        if use_directives and not is_global:
            paths.update(target + parts for target in self.directives)
        return paths


def reaches_ledger(written: Written, enclosing: NsPath, tables: NameTables, roots: frozenset[str]) -> bool:
    """Decide whether a written namespace path can name a ledger namespace."""
    return any(is_ledger_namespace(path, roots) for path in tables.expand(written, enclosing))


def ledger_names(tree: tsast.Tree, tables: NameTables, roots: frozenset[str]) -> Iterator[tuple[int, str]]:
    """Yield each name of a ledger namespace in a parsed file, as (row, form).

    A qualified name counts by its scopes, because `crucible::ledger::f`
    names something inside the ledger.  A using-directive, a
    using-declaration and an alias count by their whole target, because
    `using namespace crucible::ledger;` nominates the ledger itself.

    Complexity: linear in the number of nodes times the namespace depth.
    """
    for node in outermost_qualified(tree):
        is_global, parts = written_name(node)
        if len(parts) > 1 and reaches_ledger((is_global, parts[:-1]), namespace_path(node), tables, roots):
            yield node.start[0], "a qualified name"
    for node in tree.find("namespace_definition"):
        named = node.child_by_field("name")
        if named is not None and is_ledger_namespace(namespace_path(node) + written_name(named)[1], roots):
            yield node.start[0], "a namespace definition"
    for node in tree.find("using_declaration"):
        target = using_target(node)
        if target is not None and reaches_ledger(written_name(target), namespace_path(node), tables, roots):
            yield node.start[0], "a using-directive or a using-declaration"
    for node in tree.find("namespace_alias_definition"):
        target = alias_target(node)
        if target is not None and reaches_ledger(written_name(target), namespace_path(node), tables, roots):
            yield node.start[0], "a namespace alias"


def lexical_ledger(text: str, roots: frozenset[str]) -> Iterator[int]:
    """Yield the zero-based row of each `root::ledger` or `ledger::` in raw text, after the lexer blanks it."""
    joined, joins = splice(text)
    code, _ = blank(joined, blank_literals=True)
    pattern = re.compile(r"\b(?:(?:" + "|".join(sorted(roots)) + r")\s*::\s*ledger\b|ledger\s*::)")
    for match in pattern.finditer(code):
        yield line_of(joined, joins, match.start()) - 1


def includes_of(root: Path, rel: str, tree: tsast.Tree) -> Iterator[tuple[str | None, int, str]]:
    """Yield each include of a parsed file as (resolved file or None, row, spelling)."""
    macros = {}
    for node in tree.find("preproc_def"):
        named, value = node.child_by_field("name"), node.child_by_field("value")
        if named is not None and value is not None:
            macros[named.text] = value.text.strip()
    for node in tree.find("preproc_include"):
        path = node.child_by_field("path")
        spelled = path.text.strip() if path is not None else ""
        if path is not None and path.type == "identifier":
            spelled = macros.get(path.text, "")
            if not spelled.startswith(("<", '"')):
                yield None, node.start[0], f"#include {path.text}"
                continue
        yield resolve_include(root, rel, spelled), node.start[0], spelled


def file_names(tree: tsast.Tree, tables: NameTables, roots: frozenset[str]) -> list[tuple[int, str]]:
    """Return each ledger name of one file as (row, form), sorted and without duplicates."""
    if tree.diagnostic is not None:
        source = tree.source.decode("utf-8", "replace")
        return [(row, "a word in a file the parser cannot read") for row in lexical_ledger(source, roots)]
    rows = list(ledger_names(tree, tables, roots))
    for body in tree.find("preproc_arg"):
        rows.extend((body.start[0] + row, "a word in a macro body") for row in lexical_ledger(body.text, roots))
    return sorted(set(rows))


class Closure:
    """The include closure of one hashing-path root: each file in walk order and the edge that reached it."""

    def __init__(self, origin: str) -> None:
        self.origin = origin
        self.parent: dict[str, str | None] = {origin: None}
        self.order: list[str] = []

    def chain(self, node: str) -> str:
        """Return the include chain from the root to a file."""
        hops = [node]
        while self.parent.get(hops[0]) is not None:
            hops.insert(0, self.parent[hops[0]])
        return " -> ".join(hops)


def walk(root: Path, origin: str, trees: dict[str, tsast.Tree], violations: list[str]) -> Closure:
    """Walk the include closure of one root and record each way the walk itself reaches the ledger.

    A ledger file, a file the parser cannot read and a computed include that
    the file cannot resolve are violations of the walk.  The names come later,
    once every closure is known.

    Complexity: linear in the total size of the files in the closure.
    """
    closure = Closure(origin)
    queue = deque([origin])
    while queue:
        current = queue.popleft()
        if is_ledger_file(current):
            violations.append(f"{origin} reaches the hardware ledger. Include chain: {closure.chain(current)}")
            continue
        if current not in trees:
            # One parser run reads the whole frontier, not one file.
            pending = [current] + [path for path in queue if path not in trees and not is_ledger_file(path)]
            for parsed in tsast.parse([root / path for path in pending], strict=False):
                trees[Path(parsed.path).resolve().relative_to(root.resolve()).as_posix()] = parsed
        tree = trees[current]
        if tree.diagnostic is not None and current not in tsast.UNPARSEABLE:
            violations.append(f"{current}: the parser cannot read this file, so the guard cannot follow it. "
                              f"Include chain: {closure.chain(current)}")
            continue
        closure.order.append(current)
        if tree.diagnostic is not None:
            continue
        for child, row, spelled in includes_of(root, current, tree):
            if child is None and spelled.startswith("#include"):
                violations.append(f"{current}:{row + 1} has a computed include that no object-like macro of the "
                                  f"file resolves, so the guard cannot follow it. Include chain: "
                                  f"{closure.chain(current)}")
            elif child is not None and child not in closure.parent:
                closure.parent[child] = current
                queue.append(child)
    return closure


def scan(root: Path, roots_list: tuple[str, ...] = ROOTS) -> tuple[list[str], list[str]]:
    """Walk the include closure of each root and report each way it reaches the ledger.

    Complexity: linear in the total size of the files in the closures.

    Args:
        root: The scan root
        roots_list: The hashing-path roots, relative to the scan root

    Returns:
        Each violation, and each root that does not exist
    """
    missing = [entry for entry in roots_list if not (root / entry).is_file()]
    if missing:
        return [], missing
    roots = project_roots(root)
    trees: dict[str, tsast.Tree] = {}
    violations: list[str] = []
    closures = [walk(root, origin, trees, violations) for origin in roots_list]
    tables = NameTables()
    for rel in dict.fromkeys(rel for closure in closures for rel in closure.order):
        if trees[rel].diagnostic is None:
            tables.add(trees[rel])
    tables.seal()
    names: dict[str, list[tuple[int, str]]] = {}
    for closure in closures:
        for rel in closure.order:
            if rel not in names:
                names[rel] = file_names(trees[rel], tables, roots)
            violations.extend(f"{rel}:{row + 1} names a ledger namespace through {form}. "
                              f"Include chain: {closure.chain(rel)}" for row, form in names[rel])
    return violations, []


def check(root: Path, roots_list: tuple[str, ...] = ROOTS) -> int:
    """Run the scan and report.

    Returns:
        0 clean, 1 on a violation, 2 on a missing root
    """
    violations, missing = scan(root, roots_list)
    for entry in missing:
        print(f"DETSAFE missing root: {entry} does not exist. Update ROOTS in check-detsafe-ledger.py in the "
              f"commit that moves the file.", file=sys.stderr)
    if missing:
        return 2
    for violation in violations:
        print(f"DETSAFE violation: {violation}", file=sys.stderr)
    if violations:
        print("check-detsafe-ledger: a ledger verdict is a property of one machine, so it must not reach the "
              "hashing path. Move the consumer off the hashing path, or pass the verdict in above it. There "
              "is no allowlist.", file=sys.stderr)
        return 1
    print(f"check-detsafe-ledger: clean — no ledger header or name is reachable from the hashing path "
          f"({len(roots_list)} roots walked).", file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each way to reach the ledger and each shape that does not, then check the verdicts.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def expect(name: str, ok: bool, negative: bool = False) -> None:
        """Record one case."""
        nonlocal negatives
        negatives += negative
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    planted = {
        "include/crucible/ledger/Verdict.h": "#pragma once\nnamespace crucible::ledger { struct Verdict {}; }\n",
        "include/crucible/planted/Clean.h": (
            "#pragma once\n"
            "// Does not consult crucible::ledger, and #include <crucible/ledger/Verdict.h> stays a comment.\n"
            "#include <vector>\n"
            "#include \"Sibling.h\"\n"
            "inline const char* text = \"crucible::ledger::Verdict\";\n"
            "namespace crucible::planted { struct ledger_like {}; inline int clean() { return 0; } }\n"
            "namespace fixy { namespace detail { struct ledger {}; } }\n"),
        "include/crucible/planted/Sibling.h": "#pragma once\n",
        "include/crucible/planted/Unrelated.h": "#pragma once\nnamespace other { int h(ledger::Thing*); }\n"
                                                "namespace other::ledger { struct Thing {}; }\n",
        "include/crucible/planted/Missing.h": '#pragma once\n#include "config.h"\n',
        "include/crucible/planted/Middle.h": "#pragma once\n#include <crucible/ledger/Verdict.h>\n",
        "include/crucible/planted/Transitive.h": "#pragma once\n#include <crucible/planted/Middle.h>\n",
        "include/crucible/planted/Quoted.h": '#pragma once\n#include "../ledger/Verdict.h"\n',
        "include/crucible/planted/Computed.h": "#pragma once\n#define LEDGER_HEADER <crucible/ledger/Verdict.h>\n"
                                               "#include LEDGER_HEADER\n",
        "include/crucible/planted/Unresolved.h": "#pragma once\n#include OTHER_HEADER\n",
        "include/crucible/planted/Dead.h": "#pragma once\n#if 0\n#include <crucible/ledger/Verdict.h>\n#endif\n",
        "include/crucible/planted/Forward.h": "#pragma once\nnamespace crucible::ledger { struct Verdict; }\n",
        "include/crucible/planted/Nested.h": "#pragma once\nnamespace crucible { namespace ledger { struct V; } }\n",
        "include/crucible/planted/Relative.h": "#pragma once\nnamespace crucible { int f(ledger::Verdict*); }\n",
        "include/crucible/planted/Global.h": "#pragma once\nint g(::crucible::ledger::Verdict*);\n",
        "include/crucible/planted/Directive.h": "#pragma once\nnamespace crucible { using namespace ledger; }\n",
        "include/crucible/planted/Alias.h": "#pragma once\nnamespace l = ::crucible::ledger;\n",
        "include/crucible/planted/DirectiveQualified.h": "#pragma once\nusing namespace crucible::ledger;\n",
        "include/crucible/planted/DirectiveGlobal.h": "#pragma once\nusing namespace ::crucible::ledger;\n",
        "include/crucible/planted/DirectiveCommented.h": "#pragma once\nusing namespace crucible :: /* c */ ledger;\n",
        "include/crucible/planted/Declaration.h": "#pragma once\nusing ::crucible::ledger::Verdict;\n",
        "include/crucible/planted/AliasComment.h": "#pragma once\nnamespace l = crucible:: /*x*/ ledger;\n",
        "include/crucible/planted/DefinitionComment.h": "#pragma once\nnamespace crucible:: /*c*/ ledger { struct V; }\n",
        "include/crucible/planted/AliasUse.h": "#pragma once\nnamespace cr = crucible;\nint f(cr::ledger::Verdict*);\n",
        "include/crucible/planted/AliasDirective.h": "#pragma once\nnamespace cr = ::crucible;\n"
                                                     "using namespace cr::ledger;\n",
        "include/crucible/planted/AliasChain.h": "#pragma once\nnamespace cr = crucible;\nnamespace cl = cr::ledger;\n",
        "include/crucible/planted/ImportedRoot.h": "#pragma once\nusing namespace crucible;\nint f(ledger::Verdict*);\n",
        "include/crucible/planted/AliasHeader.h": "#pragma once\nnamespace cr = crucible;\n",
        "include/crucible/planted/AliasCrossUse.h": "#pragma once\n#include <crucible/planted/AliasHeader.h>\n"
                                                    "int f(cr::ledger::Verdict*);\n",
        "include/crucible/planted/OtherAlias.h": "#pragma once\nnamespace ol = other::ledger;\n",
        "include/crucible/planted/OtherImport.h": "#pragma once\nnamespace other { namespace ledger { struct T {}; } }\n"
                                                  "using namespace other;\nint f(ledger::T*);\n",
        "include/crucible/planted/RootImport.h": "#pragma once\nusing namespace crucible;\nint f(planted::T*);\n",
        "include/crucible/planted/Macro.h": "#pragma once\n#define VERDICT crucible :: ledger :: Verdict\n",
        "include/fixy/ledger/New.h": "#pragma once\n",
        "include/crucible/planted/NewTree.h": "#pragma once\n#include <fixy/ledger/New.h>\n",
        "include/crucible/planted/Broken.h": "#pragma once\nvoid f() { g(1) { } }\n",
        "include/crucible/planted/Reaches.h": "#pragma once\n#include <crucible/planted/Broken.h>\n",
    }
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in planted.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")

        def verdict(name: str) -> list[str]:
            """Scan one planted root alone."""
            return scan(root, (f"include/crucible/planted/{name}.h",))[0]

        expect("a clean root, with the ledger only in a comment, a string and a longer name, passes",
               not verdict("Clean"), True)
        expect("a namespace ledger outside a project root is not the ledger", not verdict("Unrelated"), True)
        expect("a missing quoted include is not followed and not reported", not verdict("Missing"), True)
        expect("an alias to a ledger outside a project root is not the ledger", not verdict("OtherAlias"), True)
        expect("a directive to a namespace outside a project root does not reach the ledger",
               not verdict("OtherImport"), True)
        expect("a directive to a root reaches only the names written after it", not verdict("RootImport"), True)
        transitive = verdict("Transitive")
        expect("caught: a transitive include, with its chain",
               bool(transitive) and "Middle.h -> include/crucible/ledger/Verdict.h" in transitive[0])
        for name, label in (("Quoted", "a relative quoted include"), ("Computed", "a computed include"),
                            ("Unresolved", "a computed include that the file cannot resolve"),
                            ("Dead", "an include in a dead branch, because the closure over-approximates"),
                            ("Forward", "a forward declaration in the ledger namespace"),
                            ("Nested", "a nested ledger namespace definition"),
                            ("Relative", "a relative qualified name inside namespace crucible"),
                            ("Global", "a qualified name from ::"), ("Directive", "a using-directive"),
                            ("DirectiveQualified", "a qualified using-directive"),
                            ("DirectiveGlobal", "a using-directive from ::"),
                            ("DirectiveCommented", "a using-directive with a comment inside its name"),
                            ("Declaration", "a using-declaration"),
                            ("AliasComment", "an alias with a comment inside its target"),
                            ("DefinitionComment", "a namespace definition with a comment inside its name"),
                            ("AliasUse", "a qualified name through an alias of a root"),
                            ("AliasDirective", "a using-directive through an alias of a root"),
                            ("AliasChain", "an alias of an alias"),
                            ("ImportedRoot", "a relative name after a using-directive to a root"),
                            ("AliasCrossUse", "an alias that another file of the closure defines"),
                            ("Alias", "a namespace alias"), ("Macro", "a macro body"),
                            ("NewTree", "a ledger directory of another project root"),
                            ("Reaches", "a closure file that the parser cannot read")):
            expect(f"caught: {label}", bool(verdict(name)))

        def captured(cwd: Path, roots_list: tuple[str, ...]) -> tuple[int, str]:
            """Run the check from one working directory and keep its report."""
            previous = Path.cwd()
            buffer = io.StringIO()
            os.chdir(cwd)
            try:
                with contextlib.redirect_stderr(buffer):
                    code = check(root, roots_list)
            finally:
                os.chdir(previous)
            return code, buffer.getvalue()

        dirty = ("include/crucible/planted/Transitive.h",)
        expect("the report from / equals the report from the scan root",
               captured(Path("/"), dirty) == captured(root, dirty) and captured(root, dirty)[0] == 1)
        expect("a missing root exits 2", captured(root, ("include/crucible/planted/Gone.h",))[0] == 2)
        expect("a clean root exits 0", captured(root, ("include/crucible/planted/Clean.h",))[0] == 0, True)
    if failures:
        print(f"check-detsafe-ledger --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-detsafe-ledger --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    if argv not in ([], ["--self-test"]):
        print("usage: check-detsafe-ledger.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-detsafe-ledger: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
