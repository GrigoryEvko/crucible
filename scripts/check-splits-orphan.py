#!/usr/bin/env python3
"""check-splits-orphan — a split manifest is specialized only where its tags are authored.

The split traits gate mint_permission_split, mint_permission_combine and
mint_permission_fork.  A translation unit that specializes one of them for
tags it does not own forges cross-region authority (CLAUDE.md §IX).  C++ has
no orphan rule, so this guard gives one: a specialization of a split trait
is legal only in an authoring location.

WHAT COUNTS AS A SPECIALIZATION
    The guard reads the parse tree of the pinned tree-sitter kit.
      * A class or struct whose name is a template-id of a split trait, in
        a full or a partial specialization, with or without a body, and
        with or without a namespace qualifier.
      * A variable declaration whose declarator is a template-id of one of
        the trait variables, such as can_split_into_v<P, L, R>.  Only an
        explicit or partial specialization can declare that name, and the
        mints read those variables, so this is the same forgery.
    The traits are the four of the new tree (can_split_into,
    can_split_into_pack, has_split_authoring_witness,
    has_split_pack_authoring_witness), the four of the old tree
    (splits_into, splits_into_pack, splits_into_authoring_witness,
    splits_into_pack_authoring_witness), the _v variable of each, and
    well_authored_split_v and well_authored_split_pack_v.

MACRO BODIES
    A macro body is parsed on its own (tsast.macro_bodies), with every
    fragment joined, so a block comment inside it does not split a head.  A
    specialization in a macro body is refused wherever the macro stands,
    because the tags that it names are its arguments.  A use of a trait
    variable, such as a static_assert, is not a specialization.  A body that
    the parser cannot read, such as one that pastes tokens with ##, is read
    from its preprocessing tokens: a `struct` or `class` head of a trait with
    `<` after it, and a trait variable with `<` after it.  The files of
    tsast.UNPARSEABLE are not C++ and are out of scope.  A parse error in any
    other file is a guard failure.

WHERE A SPECIALIZATION IS ADMITTED
    Beside its tags: in the file that defines every tag it names.  A tag is
    a top-level template argument of the specialization, and it counts as
    defined here when this file holds the class definition, with a body,
    that the spelling resolves to.  A class is defined once in a program,
    so the file that defines it owns it, and a forward declaration owns
    nothing.  The spelling resolves by its qualified name: a name written
    from `::` must equal the qualified name of the definition, and a
    relative name must name a class of the namespace that holds the
    specialization, because name lookup there tries that namespace first
    and the guard cannot see the other headers.  These shapes are no tag,
    so they refuse the admission:
      * A template parameter or a pack at the top level, so a
        specialization for every parent is never beside its tags.
      * A relative name when the trait name is qualified, because the
        lookup can then reach the namespace of the qualifier.
      * A qualifier with template arguments, such as Outer<int>::Tag,
        because an explicit specialization of Outer in another file can
        make Tag an alias of a foreign tag.
      * A class in a preprocessor conditional, a function body or an
        unnamed class, and a class that an alias or a typedef names.

    In an authoring location of the AUTHORING table, each with its reason.
    A generator over every parent still needs it.  A location that no
    specialization needs is stale, and the check fails until it is removed.

WHAT THE GUARD CANNOT SEE
    A trait name that a macro builds with `##` is not in the text, so
    neither the parser nor the lexer finds it.

Exit 0 clean, 1 on an orphan specialization or a parse failure, 2 on a stale
authoring location, a usage error or a failed self-test, 3 when the kit is
not installed.
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

import tsast  # noqa: E402

TRAITS = frozenset({
    "can_split_into", "can_split_into_pack", "has_split_authoring_witness", "has_split_pack_authoring_witness",
    "splits_into", "splits_into_pack", "splits_into_authoring_witness", "splits_into_pack_authoring_witness",
})
VARIABLES = frozenset({name + "_v" for name in TRAITS} | {"well_authored_split_v", "well_authored_split_pack_v"})
NEEDLES = re.compile(b"|".join(re.escape(name.encode()) for name in sorted(TRAITS | {"well_authored_split"})))
LINE_SPLICE = re.compile(rb"\\\r?\n")
# Each authoring location, with the reason that it still needs the exemption.
# A key that ends in "/" admits every file under it, and any other key admits
# one file.  A location that no specialization needs is stale, and the check
# fails until the key is removed, so the table can only shrink.
AUTHORING: dict[str, str] = {
    "include/crucible/safety/_PermissionTreeGenerator.h": "the generator splits every parent into its slices",
    "include/crucible/safety/_PermissionGridGenerator.h": "the generator splits every parent into its grid cells",
    "include/fixy/OwnedRegion.h": "the region splits every parent into its slices",
    "test/": "the tests specialize for local tags on purpose, and a negative fixture forges a split to prove "
             "that the mint refuses it",
}
ROOTS = ("include", "src", "vessel", "bench", "tools", "fuzz", "examples", "test")


def authored_at(rel: str) -> str | None:
    """Return the authoring location that holds a repo-relative path, or None.

    Args:
        rel: The path relative to the scan root

    Returns:
        The key of AUTHORING that admits the file, or None
    """
    for entry in AUTHORING:
        if rel == entry or (entry.endswith("/") and rel.startswith(entry)):
            return entry
    return None


def final_name(node: tsast.Node | None) -> tuple[str, bool] | None:
    """Return the last name of a declared name and whether it carries template arguments.

    Args:
        node: A name node: an identifier, a template-id or a qualified name

    Returns:
        The name, as the lexer spells it after the line splices of phase 2,
        and True for a template-id, or None for another shape
    """
    while node is not None:
        if node.type in ("type_identifier", "identifier", "field_identifier"):
            return tsast.spelled(node), False
        if node.type in ("template_type", "template_function"):
            name = node.child_by_field("name")
            return (tsast.spelled(name), True) if name is not None else None
        if node.type == "qualified_identifier":
            node = node.child_by_field("name")
        else:
            return None
    return None


def declared_name(declarator: tsast.Node | None) -> tsast.Node | None:
    """Return the name node inside a declarator, through init, pointer and reference declarators."""
    while declarator is not None and declarator.type in ("init_declarator", "pointer_declarator",
                                                         "reference_declarator", "attributed_declarator"):
        declarator = declarator.child_by_field("declarator") or (declarator.children[-1] if declarator.children
                                                                  else None)
    return declarator


CLASS_NODES = ("class_specifier", "struct_specifier", "union_specifier")
CONDITIONALS = ("preproc_if", "preproc_ifdef", "preproc_else", "preproc_elif", "preproc_elifdef")


def template_id(node: tsast.Node | None) -> tsast.Node | None:
    """Return the template-id node of a declared name, through its qualifiers, or None."""
    while node is not None:
        if node.type in ("template_type", "template_function"):
            return node
        node = node.child_by_field("name") if node.type == "qualified_identifier" else None
    return None


@dataclass(frozen=True)
class Site:
    """One split-trait specialization that the parser reads.

    Attributes:
        row: The zero-based row where the declaration starts
        node: The class or variable declaration
        arguments: The template argument list of the trait, or None
        qualified: Whether the trait name carries a namespace qualifier
    """

    row: int
    node: tsast.Node
    arguments: tsast.Node | None
    qualified: bool


def site_of(row: int, node: tsast.Node, name: tsast.Node | None) -> Site:
    """Build a Site from the declared name of a specialization."""
    named = template_id(name)
    return Site(row, node, named.child_by_field("arguments") if named is not None else None,
                name is not None and name.type == "qualified_identifier")


def shapes(root: tsast.Node, declarations: tuple[str, ...]) -> Iterator[tuple[tsast.Node, tsast.Node, tsast.Node | None]]:
    """Yield each split-trait specialization under a root node.

    Complexity: linear in the number of nodes under the root.

    Args:
        root: The root of a file, or of a macro body
        declarations: The declaration node types whose declarator can name a trait variable

    Yields:
        (the node where the head starts, the class or variable declaration, its declared name)
    """
    for node in root.descendants(*CLASS_NODES):
        found = final_name(node.child_by_field("name"))
        if found is not None and found[1] and found[0] in TRAITS:
            yield node, node, node.child_by_field("name")
    for node in root.descendants(*declarations):
        for declarator in node.children:
            if declarator.field != "declarator":
                continue
            found = final_name(declared_name(declarator))
            if found is not None and found[1] and found[0] in VARIABLES:
                yield declarator, node, declared_name(declarator)


def ast_sites(tree: tsast.Tree) -> Iterator[Site]:
    """Yield each split-trait specialization of a parsed file, a class head or a trait variable."""
    for start, node, name in shapes(tree.root, ("declaration",)):
        yield site_of(start.start[0], node, name)


def namespace_path(node: tsast.Node) -> list[str]:
    """Return the names of the namespaces that enclose a node, outermost first."""
    return list(tsast.namespace_path(node))


def class_path(node: tsast.Node) -> tuple[str, ...] | None:
    """Return the qualified name of a class definition, or None when it does not count.

    The name is the enclosing namespaces, the enclosing classes and the
    class name.  A class in a function body or in an unnamed class has no
    name that a specialization at namespace scope can spell.  A class in a
    preprocessor conditional does not count, because the branch can be
    dead while the class it seems to define comes from another header.

    Args:
        node: A class, struct or union definition

    Returns:
        The parts of the qualified name, or None
    """
    parts: list[str] = []
    if node.ancestor_of_type(*CONDITIONALS) is not None:
        return None
    owner: tsast.Node | None = node
    while owner is not None and owner.type != "namespace_definition":
        if owner.type in ("compound_statement", "function_definition", "lambda_expression"):
            return None
        if owner.type in CLASS_NODES and owner.child_by_field("body") is not None:
            named = owner.child_by_field("name")
            if named is None or named.type != "type_identifier":
                return None
            parts.insert(0, named.text)
        owner = owner.parent
    return tuple(namespace_path(node) + parts)


def defined_classes(tree: tsast.Tree) -> set[tuple[str, ...]]:
    """Return the qualified name of each primary class that a file defines with a body.

    Complexity: linear in the number of class nodes times the nesting depth.
    """
    found: set[tuple[str, ...]] = set()
    for node in tree.find(*CLASS_NODES):
        path = class_path(node) if node.child_by_field("body") is not None else None
        if path is not None:
            found.add(path)
    return found


def spelled(node: tsast.Node | None) -> tuple[bool, list[str]] | None:
    """Read a type name as (written from `::`, its parts), each part without template arguments.

    Args:
        node: The type of a template argument

    Returns:
        Whether the spelling starts at `::`, and its parts, or None for a
        shape that is not a plain class name, such as decltype, a
        dependent `typename`, or a qualifier with template arguments, whose
        members an explicit specialization in another file can replace
    """
    if node is None:
        return None
    if node.type == "qualified_identifier":
        scope = node.child_by_field("scope")
        below = spelled(node.child_by_field("name"))
        if below is None or below[0]:
            return None
        if scope is None:
            return True, below[1]
        if scope.type not in ("namespace_identifier", "type_identifier"):
            return None
        return False, [scope.text] + below[1]
    if node.type == "template_type":
        named = node.child_by_field("name")
        return (False, [named.text]) if named is not None and named.type == "type_identifier" else None
    if node.type == "type_identifier":
        return False, [node.text]
    return None


def parameter_names(parameter: tsast.Node) -> Iterator[str]:
    """Yield the names that one template parameter binds.

    Args:
        parameter: One child of a template parameter list

    Yields:
        The parameter name, if it has one
    """
    if parameter.type in ("type_parameter_declaration", "variadic_type_parameter_declaration"):
        yield from (child.text for child in parameter.children_of_type("type_identifier"))
    elif parameter.type == "optional_type_parameter_declaration":
        named = parameter.child_by_field("name")
        if named is not None:
            yield named.text
    elif parameter.type == "template_template_parameter_declaration":
        for inner in parameter.children:
            if inner.field != "parameters":
                yield from parameter_names(inner)
    else:
        declarator = parameter.child_by_field("declarator")
        if declarator is not None:
            yield from (child.text for child in declarator.descendants("identifier"))
            if declarator.type == "identifier":
                yield declarator.text


def bound_names(node: tsast.Node) -> set[str]:
    """Return the names that the template parameter lists around a declaration bind."""
    names: set[str] = set()
    owner = node.ancestor_of_type("template_declaration")
    while owner is not None:
        listed = owner.child_by_field("parameters")
        if listed is not None:
            for parameter in listed.children:
                names.update(parameter_names(parameter))
        owner = owner.ancestor_of_type("template_declaration")
    return names


def beside_tags(site: Site, defined: set[tuple[str, ...]]) -> bool:
    """Report whether a specialization sits in the file that defines every tag it names.

    A name written from `::` must equal the qualified name of a definition of
    this file.  A relative name must name a class of the namespace that
    holds the specialization.  A specialization whose trait name is itself
    qualified admits no relative name, because the lookup of its arguments
    can then reach the namespace of the qualifier, which this file does not
    show.  A template parameter, a pack, a pointer, a reference or a
    non-type argument at the top level is no tag, so it refuses the
    admission.

    Args:
        site: One specialization
        defined: The classes that this file defines

    Returns:
        Whether every top-level argument names a class of this file
    """
    if site.arguments is None:
        return False
    tags = [child for child in site.arguments.children if child.type != "comment"]
    if not tags:
        return False
    bound = bound_names(site.node)
    scope = namespace_path(site.node)
    for tag in tags:
        if tag.type != "type_descriptor" or tag.child_by_field("declarator") is not None:
            return False
        read = spelled(tag.child_by_field("type"))
        if read is None:
            return False
        is_global, parts = read
        if not is_global and (site.qualified or parts[0] in bound):
            return False
        if (tuple(parts) if is_global else tuple(scope + parts)) not in defined:
            return False
    return True


def body_rows(body: tsast.MacroBody) -> Iterator[int]:
    """Yield the file row of each specialization shape in one macro body.

    A parsed body is read from its tree, where a class member list holds a
    variable as a field declaration.  A body that did not parse is read from
    its tokens: `struct` or `class`, an optional qualifier, a trait name and
    `<`, or a trait variable name and `<`.
    """
    if body.is_parsed:
        for start, _, _ in shapes(body.root, ("declaration", "field_declaration")):
            yield body.origin(start)[0]
        return
    tokens = tsast.pp_tokens(body.text, body.first_row)
    for index, token in enumerate(tokens):
        after = tokens[index + 1].text if index + 1 < len(tokens) else ""
        if token.text in VARIABLES and after == "<":
            yield token.row
        elif token.text in ("struct", "class"):
            cursor = index + 1
            while cursor + 1 < len(tokens) and (tokens[cursor].text == "::" or tokens[cursor + 1].text == "::"):
                cursor += 1
            if cursor + 1 < len(tokens) and tokens[cursor].text in TRAITS and tokens[cursor + 1].text == "<":
                yield token.row


def scope_files(root: Path) -> list[Path]:
    """Return every C++ file under the scan roots that spells a split trait name, sorted.

    The test reads the bytes with each line splice removed, so a name that
    a splice cuts in two still counts.  A file that spells no trait name
    cannot specialize one: a name that a macro builds with ## is not in the
    text for the parser either.  Complexity: linear in the total size of the
    files under the scan roots.
    """
    found: list[Path] = []
    for top in ROOTS:
        base = root / top
        if base.is_dir():
            for path in base.rglob("*"):
                rel = path.relative_to(root)
                if path.is_file() and tsast.is_in_cpp_scope(rel) \
                        and not any(part.startswith("build") for part in rel.parts) \
                        and NEEDLES.search(LINE_SPLICE.sub(b"", path.read_bytes())):
                    found.append(path)
    return sorted(found)


@dataclass
class Scan:
    """The result of one scan.

    Attributes:
        orphans: Each refused specialization as (path, line, line text)
        failures: Each file the parser cannot read
        needed: For each authoring location, the sites that only it admits
    """

    orphans: list[tuple[str, int, str]] = field(default_factory=list)
    failures: list[str] = field(default_factory=list)
    needed: dict[str, list[str]] = field(default_factory=dict)


def scan(root: Path) -> Scan:
    """Find every split-trait specialization that is neither beside its tags nor in an authoring location.

    Complexity: linear in the total size of the files in scope.

    Args:
        root: The scan root

    Returns:
        The orphans, the parse failures and the use of each authoring location
    """
    result = Scan(needed={entry: [] for entry in AUTHORING})
    trees: list[tsast.Tree] = []
    refused: dict[str, set[int]] = {}
    for tree in tsast.parse(scope_files(root), strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            result.failures.append(f"{rel}: the parser cannot read this file. {tree.diagnostic.strip()}")
            continue
        trees.append(tree)
        defined = defined_classes(tree)
        refused[rel] = {site.row for site in ast_sites(tree) if not beside_tags(site, defined)}
    for body in tsast.macro_bodies(trees):
        refused[Path(body.define.tree.path).relative_to(root).as_posix()].update(body_rows(body))
    for tree in trees:
        rel = Path(tree.path).relative_to(root).as_posix()
        entry = authored_at(rel)
        for row in sorted(refused[rel]):
            if entry is None:
                result.orphans.append((rel, row + 1, tree.line(row).strip()))
            else:
                result.needed[entry].append(f"{rel}:{row + 1}")
    return result


def check(root: Path) -> int:
    """Run the scan and report.

    Args:
        root: The scan root

    Returns:
        0 clean, 1 on an orphan or a parse failure, 2 on an authoring
        location that no site needs
    """
    result = scan(root)
    for rel, line, text in result.orphans:
        print(f"splits_into_orphan: forbidden specialization at {rel}:{line}\n  {text}", file=sys.stderr)
    for failure in result.failures:
        print(f"splits_into_orphan: parse failure: {failure}", file=sys.stderr)
    if result.orphans or result.failures:
        print("splits_into_orphan: a split trait is specialized only in the file that defines every tag it "
              "names, or in an authoring location of AUTHORING in this script (CLAUDE.md §IX).", file=sys.stderr)
        return 1
    stale = sorted(entry for entry, sites in result.needed.items() if not sites)
    for entry in stale:
        print(f"splits_into_orphan: stale authoring location {entry}: every specialization there sits beside "
              "its tags, so remove it from AUTHORING.", file=sys.stderr)
    if stale:
        return 2
    print("check-splits-orphan: clean — every split trait is specialized beside its tags or in an authoring "
          "location that still needs its exemption.", file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each specialization shape and each exemption, then check the verdicts.

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

    planted = (
        "namespace crucible { struct P; struct L; struct R; }\n"                    # 1
        "namespace crucible {\n"                                                    # 2
        "template <>\n"                                                             # 3
        "struct splits_into<P, L, R> : std::true_type {};\n"                        # 4
        "}\n"                                                                       # 5
        "template <>\n"                                                             # 6
        "struct\n"                                                                  # 7
        "    foundation::permissions::can_split_into_pack\n"                        # 8
        "    <crucible::P, crucible::L> : std::true_type {};\n"                     # 9
        "template <> struct ::foundation::permissions::has_split_authoring_witness<crucible::P, "
        "crucible::L, crucible::R> : std::true_type {};\n"                          # 10
        "template <class T> struct foundation::permissions::can_split_into<T, crucible::L, "
        "crucible::R> : std::true_type {};\n"                                       # 11
        "template <> struct foundation::permissions::has_split_pack_authoring_witness<crucible::P>;\n"  # 12
        "template <> inline constexpr bool foundation::permissions::can_split_into_v<crucible::P, "
        "crucible::L, crucible::R> = true;\n"                                       # 13
        "#define FORGE(P, L, R) template <> struct ::foundation::permissions::can_split_into<P, L, R> "
        ": std::true_type {};\n"                                                    # 14
        "// template <> struct splits_into<P, L, R> {};\n"                          # 15
        "/* template <> struct splits_into<P, L, R> {}; */\n"                       # 16
        'inline const char* text = "template <> struct splits_into<P, L, R> {};";\n'  # 17
        "template <class P, class L, class R> struct splits_into_like {};\n"        # 18
        "struct can_split_into_record { int can_split_into = 0; };\n"              # 19
        "#define FORGE2(P, L, R) template <> struct /* x */ \\\n"                  # 20
        "    can_split_into<P, L, R> {};\n"                                         # 21
        "#define READS(P, L, R) static_assert(can_split_into_v<P, L, R>);\n"       # 22
        "#define PASTE(T) template <> struct can_split_into<T##_w, T##_l, T##_r> {};\n"  # 23
        "template <> struct can_split_\\\n"                                         # 24
        "into<crucible::P, crucible::L, crucible::R> {};\n"                         # 25
    )
    expected = {4, 7, 10, 11, 12, 13, 14, 20, 23, 24}
    trait = "template <> struct foundation::permissions::can_split_into"
    metalog = ("<::crucible::metalog_tag::Whole<U>, ::crucible::metalog_tag::Producer<U>, "
               "::crucible::metalog_tag::Consumer<U>> : std::true_type {};")
    # Each line of the file beside its tags, and whether the guard admits it.
    beside: list[tuple[str, bool | None, str]] = [
        ("namespace crucible::metalog_tag { template <class U> struct Whole {}; template <class U> struct "
         "Producer {}; template <class U> struct Consumer {}; }", None, ""),
        ("struct G {}; struct GL {}; struct GR {};", None, ""),
        ("namespace n { struct Outer2 { struct Tag {}; }; }", None, ""),
        ("namespace q { template <class T> struct Outer { struct Tag {}; }; }", None, ""),
        ("namespace al { using A = ::G; }", None, ""),
        ("#if 0", None, ""),
        ("namespace victim { struct V {}; }", None, ""),
        ("#endif", None, ""),
        ("void local() { struct Loc {}; }", None, ""),
        ("namespace foundation::permissions {", None, ""),
        ("struct Mine {}; struct MineL {}; struct MineR {};", None, ""),
        ("template <class U> struct can_split_into" + metalog, True,
         "a family specialization whose tags this file defines, spelled from ::"),
        ("template <> struct can_split_into<Mine, MineL, MineR> : std::true_type {};", True,
         "a relative name of a class that this file defines in the namespace of the specialization"),
        ("template <class U, class X> struct can_split_into<::crucible::metalog_tag::Whole<U>, X, "
         "::crucible::metalog_tag::Consumer<U>> : std::true_type {};", False,
         "a template parameter at the top level"),
        ("template <> struct can_split_into<crucible::metalog_tag::Whole<int>, crucible::metalog_tag::"
         "Producer<int>, crucible::metalog_tag::Consumer<int>> : std::true_type {};", False,
         "a relative name that the namespace of the specialization does not define"),
        ("template <class... Cs> struct can_split_into_pack<::G, Cs...> : std::true_type {};", False,
         "a pack at the top level"),
        ("}", None, ""),
        (trait + "<::G, ::GL, ::GR> : std::true_type {};", True,
         "a qualified trait name whose tags are spelled from ::"),
        (trait + "<G, GL, GR> : std::true_type {};", False,
         "a qualified trait name with relative tags"),
        (trait + "<::n::Outer2::Tag, ::G, ::GL> : std::true_type {};", True,
         "a class nested in a class that this file defines"),
        (trait + "<::q::Outer<int>::Tag, ::G, ::GL> : std::true_type {};", False,
         "a qualifier with template arguments"),
        (trait + "<::al::A, ::G, ::GL> : std::true_type {};", False, "an alias of a class of this file"),
        (trait + "<::victim::V, ::G, ::GL> : std::true_type {};", False,
         "a class defined only in a preprocessor conditional"),
        (trait + "<::Loc, ::G, ::GL> : std::true_type {};", False, "a class of a function body"),
        (trait + "<::G*, ::GL, ::GR> : std::true_type {};", False, "a pointer at the top level"),
        (trait + "<::G, ::GL, 3> : std::true_type {};", False, "a non-type argument"),
        ("template <> inline constexpr bool foundation::permissions::can_split_into_v<::G, ::GR, ::GL> = true;",
         True, "a trait variable whose tags this file defines"),
    ]
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in (
            ("src/planted/Forge.cpp", planted),
            ("src/planted/Beside.h", "\n".join(line for line, _, _ in beside) + "\n"),
            ("src/planted/Foreign.h",
             "namespace crucible::metalog_tag { template <class U> struct Whole; template <class U> struct "
             "Producer; template <class U> struct Consumer; }\n"
             "namespace foundation::permissions {\n"
             "template <class U> struct can_split_into" + metalog + "\n"
             "}\n"),
            ("include/fixy/OwnedRegion.h", "template <class P, class... S> struct can_split_into_pack<P, S...> {};\n"),
            ("include/fixy/Other.h", "template <class P, class... S> struct can_split_into_pack<P, S...> {};\n"),
            ("test/fixy/Local.cpp", "template <> struct splits_into<P, L, R> {};\n"),
        ):
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        result = scan(root)
        orphans, broken = result.orphans, result.failures
        lines = {line for rel, line, _ in orphans if rel == "src/planted/Forge.cpp"}
        for line, label in ((4, "a full specialization whose tags this file only declares"),
                            (7, "a specialization whose head spans three lines"),
                            (10, "a specialization qualified from the global namespace"),
                            (11, "a partial specialization"),
                            (12, "a specialization declared with no body"),
                            (13, "a specialization of a trait variable"),
                            (14, "a specialization inside a macro body"),
                            (20, "a specialization in a macro body that a block comment splits"),
                            (23, "a specialization in a macro body that pastes tokens, read from its tokens"),
                            (24, "a trait name that a line splice cuts in two")):
            expect(f"caught: {label}", line in lines)
        expect("nothing else in the planted file is reported", lines <= expected, True)
        for line, label in ((15, "a line comment"), (16, "a block comment"), (17, "a string literal"),
                            (18, "a different template name"), (19, "a member with a trait's name"),
                            (21, "the second line of a split macro head"),
                            (22, "a macro that only reads a trait variable")):
            expect(f"not caught: {label}", line not in lines, True)
        refused = {line for rel, line, _ in orphans if rel == "src/planted/Beside.h"}
        for line, (_, admitted, label) in enumerate(beside, start=1):
            if admitted is True:
                expect(f"admitted beside its tags: {label}", line not in refused, True)
            elif admitted is False:
                expect(f"refused beside other classes: {label}", line in refused)
        expect("nothing else in the file beside its tags is reported",
               refused <= {line for line, (_, admitted, _) in enumerate(beside, start=1) if admitted is False}, True)
        expect("refused: a family specialization whose tags this file only declares",
               ("src/planted/Foreign.h", 3) in {(rel, line) for rel, line, _ in orphans})
        exempt = {rel for rel, _, _ in orphans}
        expect("a listed file is an authoring location", "include/fixy/OwnedRegion.h" not in exempt, True)
        expect("a file beside a listed file is not", "include/fixy/Other.h" in exempt)
        expect("test/ is an authoring location", "test/fixy/Local.cpp" not in exempt, True)
        expect("a location that admits a site is not stale", bool(result.needed["include/fixy/OwnedRegion.h"]), True)
        expect("the planted tree parses", not broken)

        def captured(cwd: Path) -> tuple[int, str]:
            """Run the check from one working directory and keep its report."""
            previous = Path.cwd()
            buffer = io.StringIO()
            os.chdir(cwd)
            try:
                with contextlib.redirect_stderr(buffer):
                    code = check(root)
            finally:
                os.chdir(previous)
            return code, buffer.getvalue()

        expect("the report from / equals the report from the scan root", captured(Path("/")) == captured(root))
        (root / "src/planted/Silent.cpp").write_text("void f() { g(1) { } }\n", encoding="utf-8")
        expect("a file that spells no split trait name is not parsed", not scan(root).failures, True)
        rostered = root / next(iter(tsast.UNPARSEABLE))
        rostered.parent.mkdir(parents=True, exist_ok=True)
        rostered.write_text("void f() { g(1) { } }  // can_split_into\n", encoding="utf-8")
        expect("a file of the UNPARSEABLE roster is out of scope", not scan(root).failures, True)
        (root / "src/planted/Broken.cpp").write_text("void f() { g(1) { } }  // can_split_\\\ninto\n",
                                                     encoding="utf-8")
        with contextlib.redirect_stderr(io.StringIO()):
            code = check(root)
        expect("a file the parser cannot read fails the check, and a splice does not hide its trait name",
               code == 1 and bool(scan(root).failures))
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        listed = root / "include/fixy/OwnedRegion.h"
        listed.parent.mkdir(parents=True)
        listed.write_text("namespace foundation::permissions {\nstruct W {}; struct A {}; struct B {};\n"
                          "template <> struct can_split_into<W, A, B> {};\n}\n", encoding="utf-8")
        expect("a location that admits no site is stale", not scan(root).needed["include/fixy/OwnedRegion.h"])
        buffer = io.StringIO()
        with contextlib.redirect_stderr(buffer):
            code = check(root)
        expect("a listed file whose splits all sit beside their tags is a stale location",
               code == 2 and "stale authoring location include/fixy/OwnedRegion.h" in buffer.getvalue())
    if failures:
        print(f"check-splits-orphan --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-splits-orphan --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    if argv not in ([], ["--self-test"]):
        print("usage: check-splits-orphan.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-splits-orphan: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
