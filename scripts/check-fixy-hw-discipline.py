#!/usr/bin/env python3
"""check-fixy-hw-discipline — each hardware-axis site keeps the atoms that pin its hardware claim.

A hardware-axis site is a header whose behaviour depends on a hardware
construct that the preprocessor selects: a SIMD ISA, a cache instruction or a
memory fence.  The site restates that construct as fixy atoms, in a namespace
named `<site>_hw`, and pins each atom with a static_assert.  The build proves
each pin correct.  This guard proves that the pins are present, so a refactor
cannot delete the block or strip its asserts and leave the claim unstated.

THE RULE
    MANIFEST below names each site: its file, the qualified name of its
    block, and the axes the block pins.  For each row:
      * the file exists and the parser reads it
      * a namespace with that qualified name exists, and no preprocessor
        conditional encloses it, so it is not a dead arm
      * for each axis A of the row, a static_assert sits directly in the
        block, outside every preprocessor conditional, and its condition is
        a conjunction that holds `IsAtom<X>` and `X::axis == E`.  IsAtom
        denotes ::fixy::atom::IsAtom, E denotes ::fixy::Axis::A, and X is
        one identifier that the block declares as an alias
    In the other direction, every `*_hw` namespace under include/ and src/
    is a row, and every axis a block pins is on its row.  A superseded
    `_Name.h` header is out of scope, because it is frozen and leaves with
    the old tree.

WHAT READS THE BLOCK
    The parse tree of the pinned tree-sitter kit (scripts/tsast.py).  A
    comment and a string hold no node, so a block name or an assert in one
    counts for nothing.  A disjunction, a negation or an equality inside a
    parenthesized disjunction is no pin, because only the top-level
    conjuncts count.  A pin names an alias of the block, so a pin of an
    unrelated atom states nothing about the site.

HOW A NAME RESOLVES
    A name counts when it denotes the target and nothing else:
      * a name written from `::` denotes itself
      * a namespace alias, a type alias and a using-declaration in an
        enclosing scope of the file map their name to their target
      * a using-directive and a `using enum` make the name a candidate in
        each nominated scope, and more than one candidate refuses the name
      * an object-like macro of the file that spells a qualified name
        replaces the first part of the name
      * a function-like macro that the block invokes is expanded, and its
        expansion is parsed and read like the block itself.  A body that
        uses `#`, `##` or variadic arguments is not expanded.
    A relative name that nothing in the file resolves counts for nothing,
    because a namespace of the same name in an enclosing scope, declared in
    another header, would win the lookup.  Write such a name from `::`.
    A using-directive has the same residue: a declaration of the same name
    in an enclosing namespace of another header would hide the nominated
    one.  The build then still proves each pin that the guard reads.

WHAT THE KIT MISREADS
    `(IsAtom<X>) and (...)` parses as a cast of a call to `and`, so that pin
    counts for nothing and the guard reports the axis as lost.  The error
    lies on the safe side.  Write the IsAtom term without parentheses.  The
    self-test holds this case, so a kit that reads it differently fails it.

Usage
    check-fixy-hw-discipline.py              scan the tree
    check-fixy-hw-discipline.py --self-test  plant each evasion and each spelling

Exit 0 clean, 1 when a site lost its file, its block or a pin, or a file does
not parse, 2 when the tree holds a block or a pin that the manifest does not
record, or the self-test fails, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import sys
import tempfile
from collections.abc import Iterator
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cxx_lex  # noqa: E402  (the path insert above has to come first)
import tsast  # noqa: E402

ROOTS = ("include", "src")
SUFFIXES = (".h", ".hpp", ".cpp", ".cc")
SUPERSEDED_PREFIX = "_"
IS_ATOM = ("fixy", "atom", "IsAtom")
AXIS_ENUM = ("fixy", "Axis")
PREPROC_ARMS = frozenset({"preproc_if", "preproc_ifdef", "preproc_else", "preproc_elif", "preproc_elifdef"})
QUALIFIED = re.compile(r"(::)?([A-Za-z_]\w*(?:::[A-Za-z_]\w*)*)")
HW_NAMESPACE = re.compile(r"\bnamespace\s+[A-Za-z_]\w*_hw\b")
MAX_DEPTH = 8


@dataclass(frozen=True)
class Site:
    """One manifest row: a hardware-axis site and the axes its block pins."""

    path: str
    namespace: str
    axes: tuple[str, ...]
    reason: str


MANIFEST = (
    Site("include/crucible/SwissTable.h", "crucible::detail::swiss_hw", ("SimdIsa", "HwInstruction"),
         "the control-byte probe is emitted for the ISA that the preprocessor selects"),
    Site("include/crucible/TraceRing.h", "crucible::tracering_hw", ("HwInstruction",),
         "the append issues a prefetch, and the hot path bounds its instruction class"),
    Site("include/fixy/concurrent/ChaseLevDeque.h", "fixy::concurrent::chaselev_hw", ("BarrierStrength",),
         "the two seq_cst fences of the deque are the strength that its correctness needs"),
)

# The kinds that exit 1: the tree lost a claim, or the guard cannot read it.
LOST = frozenset({"missing-file", "parse", "missing-block", "dead-block", "gutted"})


@dataclass(frozen=True)
class Finding:
    """One result of the guard."""

    kind: str
    path: str
    text: str


@dataclass(frozen=True)
class Spelled:
    """A name as the source writes it: from `::` or relative, and its parts."""

    glob: bool
    parts: tuple[str, ...]


@dataclass
class Scope:
    """The declarations of one namespace body that can rename a name.

    Attributes:
        names: A namespace alias, a type alias or a using-declaration, by the
            name it introduces.  None marks a target the guard cannot read
        directives: The namespaces that a using-directive nominates
        enums: The enumerations that a `using enum` opens
    """

    names: dict[str, Spelled | None] = field(default_factory=dict)
    directives: list[Spelled] = field(default_factory=list)
    enums: list[Spelled] = field(default_factory=list)


@dataclass
class Macros:
    """The macros of one file, each name with every body it is given."""

    objects: dict[str, list[str]] = field(default_factory=dict)
    functions: dict[str, list[tuple[tuple[str, ...], str]]] = field(default_factory=dict)


def spelled_of(text: str) -> Spelled | None:
    """Read a qualified name from its text, or return None for any other shape.

    Args:
        text: The source text of a name, for example `::fixy::atom`

    Returns:
        The name, or None when the text is not one qualified name
    """
    compact = re.sub(r"\s+", "", text)
    match = QUALIFIED.fullmatch(compact)
    if match is None:
        return None
    return Spelled(match.group(1) is not None, tuple(match.group(2).split("::")))


def members(body: tsast.Node) -> Iterator[tsast.Node]:
    """Yield each declaration of a body, and each one inside a preprocessor arm of it.

    Args:
        body: A declaration list or a translation unit

    Yields:
        The member nodes, in source order
    """
    stack = list(reversed(body.children))
    while stack:
        node = stack.pop()
        if node.type in PREPROC_ARMS:
            stack.extend(reversed(node.children))
            continue
        yield node


def under_preprocessor(node: tsast.Node, stop: tsast.Node | None = None) -> bool:
    """Report whether a preprocessor conditional encloses a node, below an optional stop node.

    Args:
        node: The node in question
        stop: An ancestor where the walk ends, or None for the root

    Returns:
        True when a conditional arm lies between the node and the stop
    """
    owner = node.parent
    while owner is not None and (stop is None or owner.index != stop.index):
        if owner.type in PREPROC_ARMS:
            return True
        owner = owner.parent
    return False


def namespace_parts(node: tsast.Node) -> list[str]:
    """Return the name parts that one namespace definition adds.

    Args:
        node: A namespace_definition

    Returns:
        The parts, or `(anonymous)` for an unnamed namespace
    """
    name = node.child_by_field("name")
    if name is None:
        return ["(anonymous)"]
    if name.type == "nested_namespace_specifier":
        return [part.text for part in name.descendants("namespace_identifier")]
    return [name.text]


def qualified_name(node: tsast.Node) -> str:
    """Return the qualified name of a namespace definition, from the root.

    Args:
        node: A namespace_definition

    Returns:
        The name, for example `crucible::detail::swiss_hw`
    """
    parts = namespace_parts(node)
    owner = node.parent
    while owner is not None:
        if owner.type == "namespace_definition":
            parts = namespace_parts(owner) + parts
        owner = owner.parent
    return "::".join(parts)


def scope_of(body: tsast.Node) -> Scope:
    """Collect the declarations of one body that rename a name.

    Complexity: linear in the member count of the body.

    Args:
        body: A declaration list or a translation unit

    Returns:
        The scope of that body
    """
    scope = Scope()
    for node in members(body):
        if node.type == "namespace_alias_definition":
            name = node.child_by_field("name")
            target = [child for child in node.children if child.field is None]
            if name is not None:
                scope.names[name.text] = spelled_of(target[0].text) if len(target) == 1 else None
        elif node.type == "using_declaration":
            text = node.text.strip().removesuffix(";").strip()
            if text.startswith("using namespace"):
                target = spelled_of(text.removeprefix("using namespace"))
                if target is not None:
                    scope.directives.append(target)
            elif text.startswith("using enum"):
                target = spelled_of(text.removeprefix("using enum"))
                if target is not None:
                    scope.enums.append(target)
            else:
                target = spelled_of(text.removeprefix("using").removeprefix(" typename"))
                if target is not None:
                    scope.names[target.parts[-1]] = target
        elif node.type == "alias_declaration":
            name = node.child_by_field("name")
            kind = node.child_by_field("type")
            if name is not None:
                scope.names[name.text] = spelled_of(kind.text) if kind is not None else None
        elif node.type == "type_definition":
            name = node.child_by_field("declarator")
            kind = node.child_by_field("type")
            if name is not None and name.type == "type_identifier":
                scope.names[name.text] = spelled_of(kind.text) if kind is not None else None
    return scope


def macros_of(tree: tsast.Tree) -> Macros:
    """Collect every macro definition of a file.

    Args:
        tree: The parsed file

    Returns:
        The object-like and the function-like macros, each with every body
    """
    found = Macros()
    for node in tree.find("preproc_def"):
        name = node.child_by_field("name")
        value = node.child_by_field("value")
        if name is not None:
            found.objects.setdefault(name.text, []).append(value.text.strip() if value is not None else "")
    for node in tree.find("preproc_function_def"):
        name = node.child_by_field("name")
        params = node.child_by_field("parameters")
        value = node.child_by_field("value")
        if name is None or params is None:
            continue
        names = tuple(child.text for child in params.children if child.type == "identifier")
        if "..." in params.text:
            names += ("...",)
        found.functions.setdefault(name.text, []).append((names, value.text if value is not None else ""))
    return found


class Resolver:
    """Resolve names at one position of one file."""

    def __init__(self, chain: list[Scope], macros: Macros) -> None:
        """Bind the scope chain, innermost first, and the macros of the file.

        Args:
            chain: The scopes that enclose the position, innermost first
            macros: The macros of the file
        """
        self.chain = chain
        self.macros = macros

    def candidates(self, name: Spelled, start: int = 0, depth: int = 0) -> set[tuple[str, ...]]:
        """Return every name, written from `::`, that a spelled name can denote.

        Args:
            name: The spelled name
            start: The index of the innermost scope that the lookup sees
            depth: The count of renames so far, which bounds a cycle

        Returns:
            The candidates.  An empty set means that the name does not resolve
        """
        if depth > MAX_DEPTH or not name.parts:
            return set()
        first, rest = name.parts[0], name.parts[1:]
        bodies = self.macros.objects.get(first)
        if bodies is not None:
            if len(set(bodies)) != 1:
                return set()
            body = spelled_of(bodies[0])
            if body is None:
                return set()
            return self.candidates(Spelled(body.glob, body.parts + rest), start, depth + 1)
        if name.glob:
            root = self.chain[-1]
            if first in root.names:
                target = root.names[first]
                if target is None:
                    return set()
                return {found + rest for found in self.candidates(target, len(self.chain) - 1, depth + 1)}
            return {name.parts}
        for index in range(start, len(self.chain)):
            scope = self.chain[index]
            if first in scope.names:
                target = scope.names[first]
                if target is None:
                    return set()
                return {found + rest for found in self.candidates(target, index, depth + 1)}
        found: set[tuple[str, ...]] = set()
        for index in range(start, len(self.chain)):
            scope = self.chain[index]
            for nominated in scope.directives:
                found |= {base + name.parts for base in self.candidates(nominated, index, depth + 1)}
            if not rest:
                for enum in scope.enums:
                    found |= {base + name.parts for base in self.candidates(enum, index, depth + 1)}
        return found

    def denotes(self, name: Spelled | None, target: tuple[str, ...]) -> bool:
        """Report whether a spelled name denotes the target and nothing else.

        Args:
            name: The spelled name, or None
            target: The name written from `::`, without the leading `::`

        Returns:
            True when the only candidate is the target
        """
        return name is not None and self.candidates(name) == {target}


def flatten(node: tsast.Node) -> tuple[Spelled, tsast.Node | None] | None:
    """Read a name node as a spelled name and its template arguments.

    Args:
        node: An identifier, a template_function or a qualified_identifier

    Returns:
        The name and the argument list, or None for any other shape
    """
    if node.type == "qualified_identifier":
        scope = node.child_by_field("scope")
        inner = node.child_by_field("name")
        if inner is None:
            return None
        below = flatten(inner)
        if below is None or below[0].glob:
            return None
        if scope is None:
            return Spelled(True, below[0].parts), below[1]
        if scope.type != "namespace_identifier":
            return None
        return Spelled(False, (scope.text,) + below[0].parts), below[1]
    if node.type == "template_function":
        name = node.child_by_field("name")
        return (Spelled(False, (name.text,)), node.child_by_field("arguments")) if name is not None else None
    if node.type in ("identifier", "type_identifier", "namespace_identifier", "field_identifier"):
        return Spelled(False, (node.text,)), None
    return None


def operator_of(node: tsast.Node) -> str:
    """Return the operator of a binary expression, from the text between its operands.

    Args:
        node: A binary_expression

    Returns:
        The operator text, or an empty string when an operand is missing
    """
    left = node.child_by_field("left")
    right = node.child_by_field("right")
    if left is None or right is None:
        return ""
    return node.tree.slice(left.end, right.start).strip()


def conjuncts(node: tsast.Node) -> list[tsast.Node]:
    """Return the top-level conjuncts of a condition.

    A parenthesized conjunction opens.  A disjunction, a negation and any other
    shape is one conjunct, so an equality inside it is not a top-level term.

    Args:
        node: The condition expression

    Returns:
        The conjuncts, in source order
    """
    while node.type == "parenthesized_expression":
        inner = [child for child in node.children if child.type != "comment"]
        if len(inner) != 1:
            return [node]
        node = inner[0]
    if node.type == "binary_expression" and operator_of(node) in ("&&", "and"):
        left = node.child_by_field("left")
        right = node.child_by_field("right")
        assert left is not None and right is not None
        return conjuncts(left) + conjuncts(right)
    return [node]


def atom_argument(term: tsast.Node, resolver: Resolver) -> str | None:
    """Return X when a conjunct is `IsAtom<X>` with IsAtom denoting ::fixy::atom::IsAtom.

    Args:
        term: One conjunct
        resolver: The resolver at the position of the assert

    Returns:
        The identifier X, or None
    """
    read = flatten(term)
    if read is None or read[1] is None or not resolver.denotes(read[0], IS_ATOM):
        return None
    arguments = [child for child in read[1].children if child.type != "comment"]
    if len(arguments) != 1:
        return None
    argument = arguments[0]
    if argument.type == "type_descriptor":
        kind = argument.child_by_field("type")
        if kind is None or len([c for c in argument.children if c.type != "comment"]) != 1:
            return None
        argument = kind
    return argument.text if argument.type in ("type_identifier", "identifier") else None


def axis_equality(term: tsast.Node, resolver: Resolver) -> tuple[str, str] | None:
    """Return (X, A) when a conjunct is `X::axis == E` with E denoting ::fixy::Axis::A.

    Args:
        term: One conjunct
        resolver: The resolver at the position of the assert

    Returns:
        The alias X and the axis A, or None
    """
    if term.type != "binary_expression" or operator_of(term) != "==":
        return None
    sides = [term.child_by_field("left"), term.child_by_field("right")]
    if sides[0] is None or sides[1] is None:
        return None
    for mine, other in ((sides[0], sides[1]), (sides[1], sides[0])):
        read = flatten(mine)
        if read is None or read[1] is not None or read[0].glob or len(read[0].parts) != 2 \
                or read[0].parts[1] != "axis":
            continue
        value = flatten(other)
        if value is None or value[1] is not None:
            continue
        found = resolver.candidates(value[0])
        if len(found) == 1:
            only = next(iter(found))
            if len(only) == len(AXIS_ENUM) + 1 and only[:-1] == AXIS_ENUM:
                return read[0].parts[0], only[-1]
    return None


def pinned_axes(assertion: tsast.Node, resolver: Resolver, aliases: set[str]) -> set[str]:
    """Return the axes that one static_assert pins.

    Args:
        assertion: A static_assert_declaration
        resolver: The resolver at the position of the assert
        aliases: The alias names that the block declares

    Returns:
        Each axis A that the assert pins for an alias of the block
    """
    condition = assertion.child_by_field("condition")
    if condition is None:
        return set()
    terms = conjuncts(condition)
    atoms = {name for term in terms if (name := atom_argument(term, resolver)) is not None}
    axes: set[str] = set()
    for term in terms:
        pair = axis_equality(term, resolver)
        if pair is not None and pair[0] in atoms and pair[0] in aliases:
            axes.add(pair[1])
    return axes


def split_arguments(text: str) -> list[str]:
    """Split the argument text of a macro invocation at its top-level commas.

    Args:
        text: The text between the outer parentheses

    Returns:
        The arguments, each stripped
    """
    arguments, depth, start = [], 0, 0
    for index, char in enumerate(text):
        if char in "([{":
            depth += 1
        elif char in ")]}":
            depth -= 1
        elif char == "," and depth == 0:
            arguments.append(text[start:index].strip())
            start = index + 1
    arguments.append(text[start:].strip())
    return arguments if arguments != [""] else []


def expand(invocation: tsast.Node, macros: Macros) -> str | None:
    """Expand one function-like macro invocation, or return None when the guard cannot.

    Args:
        invocation: A macro_invocation node
        macros: The macros of the file

    Returns:
        The expansion text, or None for an unknown, redefined, variadic or
        token-pasting macro, or a wrong argument count
    """
    name = invocation.child_by_field("name")
    arguments = invocation.child_by_field("arguments")
    if name is None or arguments is None:
        return None
    bodies = macros.functions.get(name.text, [])
    if len(set(bodies)) != 1:
        return None
    params, body = bodies[0]
    if "..." in params or "#" in body:
        return None
    text = arguments.text.strip()
    if not (text.startswith("(") and text.endswith(")")):
        return None
    values = split_arguments(text[1:-1])
    if len(values) != len(params):
        return None
    mapping = dict(zip(params, values))
    out, last = [], 0
    for match in cxx_lex.LEXER.finditer(body):
        out.append(body[last:match.start()])
        token = match.group(0)
        out.append(mapping.get(token, token) if match.group("ident") is not None else token)
        last = match.end()
    out.append(body[last:])
    return "".join(out)


class Reader:
    """Read the pins of blocks, parsing macro expansions in a scratch directory."""

    def __init__(self, scratch: Path) -> None:
        """Bind the scratch directory that holds each expansion file.

        Args:
            scratch: A directory that lives until the reader is done
        """
        self.scratch = scratch
        self.count = 0

    def expansion_pins(self, invocation: tsast.Node, resolver: Resolver, aliases: set[str],
                       macros: Macros, depth: int = 0) -> set[str]:
        """Return the axes that the expansion of one invocation pins.

        Args:
            invocation: A macro_invocation directly in the block
            resolver: The resolver at the invocation
            aliases: The alias names that the block declares
            macros: The macros of the file
            depth: The nesting of expansions so far

        Returns:
            The pinned axes, empty when the guard cannot expand the macro
        """
        text = expand(invocation, macros)
        if text is None or depth > MAX_DEPTH:
            return set()
        self.count += 1
        path = self.scratch / f"expansion_{self.count}.cpp"
        path.write_text(text + ";\n", encoding="utf-8")
        tree = next(tsast.parse([path], strict=False))
        if tree.diagnostic is not None:
            return set()
        axes: set[str] = set()
        for node in tree.root.children:
            if node.type == "static_assert_declaration":
                axes |= pinned_axes(node, resolver, aliases)
            elif node.type == "macro_invocation":
                axes |= self.expansion_pins(node, resolver, aliases, macros, depth + 1)
        return axes

    def block_pins(self, blocks: list[tsast.Node], macros: Macros, scopes: dict[int, Scope]) -> set[str]:
        """Return the axes that the live definitions of one namespace pin.

        Args:
            blocks: The live namespace_definition nodes of the site
            macros: The macros of the file
            scopes: The scope of each body, by node index, filled on demand

        Returns:
            Each pinned axis
        """
        aliases: set[str] = set()
        for block in blocks:
            body = block.child_by_field("body")
            if body is None:
                continue
            for node in members(body):
                if node.type == "alias_declaration" and (name := node.child_by_field("name")) is not None:
                    aliases.add(name.text)
                elif node.type == "type_definition" and (name := node.child_by_field("declarator")) is not None:
                    aliases.add(name.text)
        axes: set[str] = set()
        for block in blocks:
            body = block.child_by_field("body")
            if body is None:
                continue
            resolver = Resolver(chain_of(body, scopes), macros)
            for node in body.children:
                if node.type == "static_assert_declaration":
                    axes |= pinned_axes(node, resolver, aliases)
                elif node.type == "macro_invocation":
                    axes |= self.expansion_pins(node, resolver, aliases, macros)
        return axes


def chain_of(body: tsast.Node, scopes: dict[int, Scope]) -> list[Scope]:
    """Return the scopes that enclose a body, innermost first, ending at the file.

    Args:
        body: The declaration list of a namespace
        scopes: The scope of each body, by node index, filled on demand

    Returns:
        The scope chain
    """
    chain: list[Scope] = []
    node: tsast.Node | None = body
    while node is not None:
        if node.type in ("declaration_list", "translation_unit") and (
                node.type == "translation_unit" or (node.parent is not None
                                                    and node.parent.type == "namespace_definition")):
            if node.index not in scopes:
                scopes[node.index] = scope_of(node)
            chain.append(scopes[node.index])
        node = node.parent
    return chain


def scanned_files(root: Path) -> list[Path]:
    """Return the files under the roots that can hold a `*_hw` namespace.

    A file whose bytes do not hold `_hw` cannot name such a namespace, so the
    guard does not parse it.  A superseded `_Name.h` header is out of scope.

    Args:
        root: The repository root

    Returns:
        Paths relative to the root, sorted
    """
    found: list[Path] = []
    for top in ROOTS:
        base = root / top
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            if path.suffix in SUFFIXES and path.is_file() and not path.name.startswith(SUPERSEDED_PREFIX) \
                    and b"_hw" in path.read_bytes():
                found.append(path.relative_to(root))
    return sorted(found)


def check(root: Path, manifest: tuple[Site, ...] = MANIFEST) -> list[Finding]:
    """Hold the tree to the manifest, in both directions.

    Complexity: linear in the size of the files that hold `_hw`.

    Args:
        root: The repository root
        manifest: The rows to hold the tree to

    Returns:
        Every finding, in a stable order

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    findings: list[Finding] = []
    listed = {site.namespace: site for site in manifest}
    files = sorted(set(scanned_files(root)) | {Path(site.path) for site in manifest if (root / site.path).is_file()})
    for site in manifest:
        if not (root / site.path).is_file():
            findings.append(Finding("missing-file", site.path,
                                    f"FIXY-HW-DISCIPLINE missing file: {site.path} — the site of {site.namespace} is "
                                    f"gone.  Restore it, or move its row in scripts/check-fixy-hw-discipline.py."))
    blocks: dict[str, list[tuple[Path, tsast.Node, bool]]] = {}
    trees: dict[str, tuple[tsast.Tree, Macros]] = {}
    for tree in tsast.parse([root / rel for rel in files], strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            if rel in tsast.UNPARSEABLE:
                blanked, _ = cxx_lex.blank(tree.source.decode("utf-8", "replace"), blank_literals=True)
                if HW_NAMESPACE.search(blanked) is None:
                    continue
            findings.append(Finding("parse", rel, f"FIXY-HW-DISCIPLINE parse failure: {rel} — the parser cannot "
                                                  f"read this file, so its hardware-axis blocks are unknown.\n"
                                                  f"  {tree.diagnostic}"))
            continue
        trees[rel] = (tree, macros_of(tree))
        for node in tree.find("namespace_definition"):
            if not namespace_parts(node)[-1].endswith("_hw"):
                continue
            blocks.setdefault(qualified_name(node), []).append((Path(rel), node, under_preprocessor(node)))

    with tempfile.TemporaryDirectory() as scratch:
        reader = Reader(Path(scratch))
        for site in manifest:
            if site.path not in trees:
                continue
            tree, macros = trees[site.path]
            found = [(node, dead) for rel, node, dead in blocks.get(site.namespace, []) if rel.as_posix() == site.path]
            live = [node for node, dead in found if not dead]
            if not found:
                findings.append(Finding("missing-block", site.path,
                                        f"FIXY-HW-DISCIPLINE missing block: {site.path} — no namespace "
                                        f"{site.namespace} is defined.  The site states {', '.join(site.axes)} "
                                        f"because {site.reason}.  Restore the block, or move the row."))
                continue
            if not live:
                findings.append(Finding("dead-block", site.path,
                                        f"FIXY-HW-DISCIPLINE dead block: {site.path} — each definition of "
                                        f"{site.namespace} sits in a preprocessor conditional, so no build can be "
                                        f"shown to read it.  Move the block out of the conditional."))
                continue
            pins = reader.block_pins(live, macros, {})
            for axis in site.axes:
                if axis not in pins:
                    findings.append(Finding(
                        "gutted", site.path,
                        f"FIXY-HW-DISCIPLINE gutted: {site.path} — the block {site.namespace} pins no atom of the "
                        f"{axis} axis.  A pin is a static_assert directly in the block, outside every #if: "
                        f"`::fixy::atom::IsAtom<X> && X::axis == ::fixy::Axis::{axis}`, where X is an alias "
                        f"that the block declares."))
            for axis in sorted(pins - set(site.axes)):
                findings.append(Finding(
                    "unlisted-axis", site.path,
                    f"FIXY-HW-DISCIPLINE unlisted axis: {site.path} — the block {site.namespace} pins the {axis} "
                    f"axis, and its row does not list it.  Add {axis} to the row in the same commit."))

    for name in sorted(blocks):
        if name in listed:
            continue
        for rel, node, _dead in blocks[name]:
            findings.append(Finding(
                "unlisted-site", rel.as_posix(),
                f"FIXY-HW-DISCIPLINE unlisted site: {rel.as_posix()}:{node.line} — the namespace {name} is a "
                f"hardware-axis block with no row, so no gate keeps it.  Add a row to MANIFEST in "
                f"scripts/check-fixy-hw-discipline.py in the same commit."))
    return findings


def run(root: Path, manifest: tuple[Site, ...] = MANIFEST) -> int:
    """Check the tree, print each finding, and return the exit code.

    Args:
        root: The repository root
        manifest: The rows to hold the tree to

    Returns:
        0 when clean, 1 when a claim is lost, 2 when the manifest is behind the tree
    """
    findings = check(root, manifest)
    for finding in findings:
        print(finding.text, file=sys.stderr)
    lost = sum(finding.kind in LOST for finding in findings)
    if findings:
        print(f"check-fixy-hw-discipline: {len(findings)} finding(s): {lost} lost claim(s), "
              f"{len(findings) - lost} unlisted block(s) or axis(es).", file=sys.stderr)
        return 1 if lost else 2
    print(f"check-fixy-hw-discipline: clean — {len(manifest)} hardware-axis sites keep each pin, and every "
          f"`*_hw` block has its row.", file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each evasion and each spelling, and examine each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def expect(name: str, ok: bool, negative: bool = False) -> None:
        """Record one case result and print it."""
        nonlocal negatives
        negatives += negative
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    site = Site("include/hw/Site.h", "demo::site_hw", ("SimdIsa", "HwInstruction"), "the planted probe")
    manifest = (site,)
    isa_pin = "static_assert(::fixy::atom::IsAtom<ActiveSimdIsa> && ActiveSimdIsa::axis == ::fixy::Axis::SimdIsa);\n"
    tier_pin = ("static_assert(::fixy::atom::IsAtom<InstructionTier> "
                "&& InstructionTier::axis == ::fixy::Axis::HwInstruction);\n")
    aliases = ("#if defined(__AVX2__)\nusing ActiveSimdIsa = ::fixy::atom::simd::avx2;\n#else\n"
               "using ActiveSimdIsa = ::fixy::atom::simd::scalar;\n#endif\n"
               "using InstructionTier = ::fixy::atom::hw::scalar;\n")

    def block(pins: str = isa_pin + tier_pin, prelude: str = "", head: str = "", tail: str = "",
              outer: str = "demo", inner: str = "site_hw") -> str:
        """Return a planted site: a file prelude, an outer namespace, the block and its pins."""
        return (f"#pragma once\n{prelude}namespace {outer} {{\n{head}namespace {inner} {{\n{aliases}{pins}"
                f"}}  // namespace {inner}\n{tail}}}  // namespace {outer}\n")

    cases: list[tuple[str, str, set[tuple[str, str]], bool]] = [
        ("a complete block passes", block(), set(), False),
        ("a gutted block is flagged for each axis", block(pins=""),
         {("gutted", "SimdIsa"), ("gutted", "HwInstruction")}, True),
        ("an old grant block is flagged",
         block(pins="static_assert(::crucible::fixy::grant::IsGrantTag<ActiveSimdIsa>, \"x\");\n"
                    "static_assert(::crucible::fixy::grant::which_dim_v<InstructionTier> == 0, \"x\");\n"),
         {("gutted", "SimdIsa"), ("gutted", "HwInstruction")}, True),
        ("a block name in a comment is no block",
         "#pragma once\n// namespace site_hw {\nnamespace demo {\n" + aliases + isa_pin + tier_pin + "}\n",
         {("missing-block", "")}, True),
        ("a block name in a string is no block",
         "#pragma once\nnamespace demo {\ninline const char* text = \"namespace site_hw {\";\n}\n",
         {("missing-block", "")}, True),
        ("pins outside the block do not count", block(pins="", tail=isa_pin + tier_pin),
         {("gutted", "SimdIsa"), ("gutted", "HwInstruction")}, True),
        ("pins in a comment do not count", block(pins="// " + isa_pin + "/* " + tier_pin + " */\n"),
         {("gutted", "SimdIsa"), ("gutted", "HwInstruction")}, True),
        ("a pin under #if 0 does not count", block(pins="#if 0\n" + isa_pin + "#endif\n" + tier_pin),
         {("gutted", "SimdIsa")}, True),
        ("a block under #if 0 is dead", "#pragma once\n#if 0\n" + block().removeprefix("#pragma once\n") + "#endif\n",
         {("dead-block", "")}, True),
        ("a disjunction is no pin", block(pins=isa_pin.replace("&&", "||") + tier_pin), {("gutted", "SimdIsa")}, True),
        ("an equality inside a disjunction is no pin",
         block(pins="static_assert(::fixy::atom::IsAtom<ActiveSimdIsa> && "
                    "(ActiveSimdIsa::axis == ::fixy::Axis::SimdIsa || true));\n" + tier_pin),
         {("gutted", "SimdIsa")}, True),
        ("a negated pin is no pin", block(pins="static_assert(!(" + isa_pin[14:-3] + "));\n" + tier_pin),
         {("gutted", "SimdIsa")}, True),
        ("two different aliases in one assert are no pin",
         block(pins="static_assert(::fixy::atom::IsAtom<InstructionTier> && "
                    "ActiveSimdIsa::axis == ::fixy::Axis::SimdIsa);\n" + tier_pin),
         {("gutted", "SimdIsa")}, True),
        ("an atom the block does not declare is no pin",
         block(pins="static_assert(::fixy::atom::IsAtom<Elsewhere> && Elsewhere::axis == ::fixy::Axis::SimdIsa);\n"
                    + tier_pin, head="using Elsewhere = ::fixy::atom::simd::avx2;\n"),
         {("gutted", "SimdIsa")}, True),
        ("a pin of one axis does not stand for another", block(pins=tier_pin), {("gutted", "SimdIsa")}, True),
        ("a block in another namespace is missing, and unlisted", block(outer="other"),
         {("missing-block", ""), ("unlisted-site", "")}, True),
        ("a relative IsAtom that nothing resolves is no pin",
         block(pins=isa_pin.replace("::fixy::atom::IsAtom", "fixy::atom::IsAtom") + tier_pin),
         {("gutted", "SimdIsa")}, True),
        ("an alias to another namespace is no pin",
         block(pins="namespace fa = ::other::atom;\n" + isa_pin.replace("::fixy::atom::IsAtom", "fa::IsAtom")
                    + tier_pin), {("gutted", "SimdIsa")}, True),
        ("two using-directives make IsAtom ambiguous",
         block(pins="using namespace ::fixy::atom;\nusing namespace ::other;\n"
                    + isa_pin.replace("::fixy::atom::IsAtom", "IsAtom") + tier_pin), {("gutted", "SimdIsa")}, True),
        ("a macro with token pasting is not expanded",
         block(prelude="#define PIN(A, X) static_assert(::fixy::atom::IsAtom<A> && A::axis == ::fixy::Axis::X##Isa)\n",
               pins="PIN(ActiveSimdIsa, Simd);\n" + tier_pin), {("gutted", "SimdIsa")}, True),
        ("a macro body that no line invokes is no pin",
         block(prelude="#define PIN " + isa_pin[:-2] + "\n", pins=tier_pin), {("gutted", "SimdIsa")}, True),
        ("a pin of an axis the row does not list is reported",
         block(pins=isa_pin + tier_pin + "using BarrierTier = ::fixy::atom::barrier::seq_cst;\n"
                    "static_assert(::fixy::atom::IsAtom<BarrierTier> && "
                    "BarrierTier::axis == ::fixy::Axis::BarrierStrength);\n"),
         {("unlisted-axis", "BarrierStrength")}, True),
        ("a namespace alias resolves",
         block(pins="namespace fa = ::fixy::atom;\n" + isa_pin.replace("::fixy::atom::IsAtom", "fa::IsAtom")
                    + tier_pin), set(), False),
        ("an alias of an alias resolves",
         block(prelude="namespace fx = ::fixy;\n",
               pins="namespace fa = fx::atom;\n" + isa_pin.replace("::fixy::atom::IsAtom", "fa::IsAtom") + tier_pin),
         set(), False),
        ("a using-declaration resolves",
         block(pins="using ::fixy::atom::IsAtom;\n" + isa_pin.replace("::fixy::atom::IsAtom", "IsAtom") + tier_pin),
         set(), False),
        ("one using-directive resolves",
         block(pins="using namespace ::fixy::atom;\n" + isa_pin.replace("::fixy::atom::IsAtom", "IsAtom") + tier_pin),
         set(), False),
        ("a using enum resolves the axis",
         block(pins="using enum ::fixy::Axis;\n" + isa_pin.replace("::fixy::Axis::SimdIsa", "SimdIsa") + tier_pin),
         set(), False),
        ("a type alias of the axis enum resolves",
         block(pins="using Ax = ::fixy::Axis;\n" + isa_pin.replace("::fixy::Axis::", "Ax::") + tier_pin),
         set(), False),
        ("a swapped equality in parentheses, joined by `and`, reads",
         block(pins="static_assert(::fixy::atom::IsAtom<ActiveSimdIsa> and "
                    "(::fixy::Axis::SimdIsa == ActiveSimdIsa::axis), \"isa\");\n" + tier_pin), set(), False),
        ("limit: a parenthesized IsAtom before `and` parses as a cast, and counts for nothing",
         block(pins="static_assert((::fixy::atom::IsAtom<ActiveSimdIsa>) and "
                    "(::fixy::Axis::SimdIsa == ActiveSimdIsa::axis), \"isa\");\n" + tier_pin),
         {("gutted", "SimdIsa")}, True),
        ("an object-like macro prefix resolves",
         block(prelude="#define FIXY_ATOM ::fixy::atom\n",
               pins=isa_pin.replace("::fixy::atom::IsAtom", "FIXY_ATOM::IsAtom") + tier_pin), set(), False),
        ("a function-like macro that the block invokes is read",
         block(prelude="#define PIN(A, X) static_assert(::fixy::atom::IsAtom<A> && A::axis == ::fixy::Axis::X)\n",
               pins="PIN(ActiveSimdIsa, SimdIsa);\n" + tier_pin), set(), False),
        ("a typedef alias counts",
         block(pins=tier_pin + "typedef ::fixy::atom::simd::avx2 Isa;\n" + isa_pin.replace("ActiveSimdIsa", "Isa")),
         set(), False),
    ]

    def captured(action) -> tuple[int, str]:
        """Run an action and return its code and its stderr."""
        buffer = io.StringIO()
        with contextlib.redirect_stderr(buffer):
            code = action()
        return code, buffer.getvalue()

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        (root / "include/hw").mkdir(parents=True)
        target = root / site.path
        for name, text, expected, negative in cases:
            target.write_text(text, encoding="utf-8")
            found = check(root, manifest)
            got = set()
            for finding in found:
                axis = next((a for a in ("SimdIsa", "HwInstruction", "BarrierStrength")
                             if finding.kind in ("gutted", "unlisted-axis") and f" {a} axis" in finding.text), "")
                got.add((finding.kind, axis))
            expect(name, got == expected, negative)
            if got != expected:
                print(f"       expected {sorted(expected)}, got {sorted(got)}")

        target.write_text(block(), encoding="utf-8")
        expect("a complete tree exits 0", captured(lambda: run(root, manifest))[0] == 0)
        previous = Path.cwd()
        os.chdir("/")
        try:
            from_slash = captured(lambda: run(root, manifest))
        finally:
            os.chdir(previous)
        expect("the report from / equals the report from the repository",
               from_slash == captured(lambda: run(root, manifest)))
        target.write_text(block(pins=""), encoding="utf-8")
        expect("a gutted block exits 1", captured(lambda: run(root, manifest))[0] == 1, True)
        target.write_text(block(), encoding="utf-8")

        other = root / "src/Other.cpp"
        other.parent.mkdir(parents=True)
        other.write_text("namespace crucible::other_hw {\n}\n", encoding="utf-8")
        code, report = captured(lambda: run(root, manifest))
        expect("an unlisted block under src/ exits 2", code == 2 and "src/Other.cpp:1" in report, True)
        other.unlink()
        (root / "include/hw/_Old.h").write_text("namespace old_hw {\n}\n", encoding="utf-8")
        expect("a superseded header is out of scope", captured(lambda: run(root, manifest))[0] == 0)

        broken = root / "include/hw/Broken.h"
        broken.write_text("namespace broken_hw { void f() { g(1) { } } }\n", encoding="utf-8")
        code, report = captured(lambda: run(root, manifest))
        expect("a file the parser cannot read exits 1", code == 1 and "parse failure: include/hw/Broken.h" in report,
               True)
        broken.unlink()

        target.unlink()
        code, report = captured(lambda: run(root, manifest))
        expect("a missing file exits 1", code == 1 and "missing file: include/hw/Site.h" in report, True)

    if failures:
        print(f"check-fixy-hw-discipline --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-fixy-hw-discipline --self-test: every case passes, {negatives} of them negative controls.")
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
            return run(tsast.REPO_ROOT)
        if argv == ["--self-test"]:
            return self_test()
    except tsast.KitMissing as exc:
        print(f"check-fixy-hw-discipline: {exc}", file=sys.stderr)
        return 3
    print("usage: check-fixy-hw-discipline.py [--self-test]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
