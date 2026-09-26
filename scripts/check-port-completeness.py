#!/usr/bin/env python3
"""Every symbol a superseded header declares has a home in the new tree, or a written reason for none.

Three ports that were recorded as complete were incomplete, and each was found
by downstream work or by a one-off sweep, never by review.  This guard is the
permanent form of that sweep.

THE INPUT SET
    The headers the project claims to have ported: every `_Foo.h` under
    include/crucible/, because the leading underscore is the superseded marking.
    Each public symbol such a header declares must exist in the new tree
    (include/foundation/, include/fixy/), or be listed in scripts/port-drops.txt
    with one sentence that says why it was not carried.  Anything else is a
    missing port, and the guard names the header and the symbol.

HOW THE OLD SURFACE IS ENUMERATED (three methods, unioned)
    1. Reflection.  A sentinel translation unit includes every superseded
       header and walks std::meta::members_of over the root namespace, into the
       public sub-namespaces.  It gives the name set with the namespace of each
       name.  members_of does not report a name that a using-declaration brings
       in, and std::meta::source_location_of reports the first declaration in
       translation order, which can sit in an unported header.  So reflection
       gives the names, and the parse tree of each superseded header decides
       which headers declare each name at public namespace scope.  A name that
       reflection first saw in an unported file and the tree attributes to a
       superseded one is printed as a NOTE.
    2. The preprocessor.  `-dD -E` over the same headers prints every #define
       with line markers, so each macro is attributed to its file exactly, and a
       #define in a false #if arm is never counted.
    3. The parse tree gives each using-declaration at public namespace scope, the
       one form of public surface neither compiler pass reports.

    A namespace is private when one of its names is detail or self_test, ends in
    _self_test, _smoke, _test or _layout, or is anonymous.  The reflection walk
    and the tree walk use the same definition, generated from one list here.
    tsast.is_private_namespace is not used, because it also makes a test
    namespace private.  effects::testing holds the test contexts, and the port
    must carry it.

HOW PRESENCE IN THE NEW TREE IS DECIDED
    (a) The qualified index.  A second sentinel walks the new root namespaces,
        private ones included, and prints each namespace-scope member with its
        namespace.  The port moved namespaces on purpose (crucible::safety::Linear
        is fixy::Linear), so the correspondence between an old namespace and the
        new ones is measured: a new namespace that holds at least the
        corroboration floor (two) of the old namespace's symbols received it, and
        one of those must declare the symbol.
    (b) The declared-name index.  Every name the new tree declares, from its
        parse trees: namespace-scope and class-scope declarations of every kind,
        enumerators and macro definitions.  A name the new tree only uses (a call,
        a parameter, a local) is not a declaration, so it does not count.  This
        index decides a macro (a macro has no namespace), a symbol of an old
        namespace below the corroboration floor, and a name the tree attributed
        to a header without a namespace.

    Neither index says anything about a signature or a semantic.  Member
    functions and nested members are out of scope, and a header without the
    underscore is not measured at all.

THE SECOND HALF OF A TRAIT'S IDENTITY: SPECIALISATION FOLDS
    A trait can keep its name through the port and lose every specialisation,
    and then answer the same thing for every argument (that is how the Qtt
    discipline tables made linearity a tautology).  So the guard counts
    specialisations on both sides, from the parse trees: a class, struct or union
    whose name is a template-id, qualified or not, and a variable template whose
    declarator is a template-id.  A specialisation that a macro body spells is
    counted once for each body, from its preprocessing tokens, and never for each
    expansion.  A name must carry a sentence in scripts/port-folds.txt when the
    old tree specialises it, the new tree declares a primary for it, and the new
    tree specialises it zero times.  A count is the trigger and the sentence is
    the evidence, because P2996 has no query for the specialisations of a
    template.

scripts/port-drops.txt
    One row per `<old header>:<symbol>  [→ <carrier>]  — <one sentence>.`, keyed
    by the (header, symbol) pair and checked in both directions: STALE when the
    header no longer declares the symbol, OBSOLETE when the new tree now has it.
    A row whose header only forward-declares the symbol must name a carrier, a
    header under a new root, or `none`, and a `none` row must be on the pinned
    list below.  scripts/port-folds.txt has the same two-way check.

EXIT STATUS
    0  every symbol is present or dropped, and every fold is written
    1  at least one missing port or unjustified fold (takes precedence over 2)
    2  a stale, obsolete or malformed row, a bad carrier or pin, a sentinel that
       does not compile, a header that does not parse, or a bad invocation
    3  the pinned tree-sitter kit is not installed (ctest reports a skip)

ENVIRONMENT
    CXX / CRUCIBLE_CXX            the compiler for the sentinels (a GCC 16)
    PORT_GUARD_OLD_ROOT           include root of the old tree (include)
    PORT_GUARD_OLD_SUBDIR         old subtree under that root (crucible)
    PORT_GUARD_NEW_ROOTS          new roots (include/foundation include/fixy)
    PORT_GUARD_NS                 root namespace of the old tree (crucible)
    PORT_GUARD_NEW_NS             root namespaces of the new tree (foundation fixy)
    PORT_GUARD_NS_CORROBORATION   the corroboration floor (2)
    PORT_GUARD_DROPS              drops file (scripts/port-drops.txt)
    PORT_GUARD_FOLDS              folds file (scripts/port-folds.txt)
    PORT_GUARD_UNCARRIED          `header:symbol` pins replacing the list below
"""

from __future__ import annotations

import os
import shutil
import subprocess
import sys
import tempfile
from collections import defaultdict
from collections.abc import Iterable, Iterator
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import tsast  # noqa: E402

REPO = tsast.REPO_ROOT

# The drop rows admitted to write `→ none`.  Each one is a type the port has not
# reached.  The set is pinned by name, never by a count: a new uncarried row is
# refused as UNPINNED and a pin whose row is gone is refused as STALE, so one
# name swapped for another reports both halves.
UNCARRIED_PINNED = (
    "crucible/effects/_Capabilities.h:HwProbeEntry",
    "crucible/safety/diag/_RowHashFold.h:Consistency",
    "crucible/safety/diag/_RowHashFold.h:JoinPolicy",
    "crucible/safety/diag/_RowHashFold.h:MemOrder",
    "crucible/safety/diag/_RowHashFold.h:MemOrderTag",
    "crucible/safety/diag/_RowHashFold.h:TimeOrdered",
)

# The private namespace convention.  The reflection sentinel is generated from
# these two tuples, so the tree walk and the reflection walk cannot disagree.
PRIVATE_NAMES = ("detail", "self_test")
PRIVATE_SUFFIXES = ("_self_test", "_smoke", "_test", "_layout")

# Flags the frozen tree compiles under.  No -Werror: the warnings of the old tree
# are not this guard's business.
SENTINEL_FLAGS = ("-std=c++26", "-freflection", "-fcontracts", "-w",
                  "-DCRUCIBLE_FIXY_STRICT=1", "-DCRUCIBLE_FP_STRICT_FLOOR=1")

CLASS_KINDS = ("class_specifier", "struct_specifier", "union_specifier")
TYPE_KINDS = CLASS_KINDS + ("enum_specifier",)
# The parents under which a class specifier is a declaration of its own and not
# an elaborated type inside another declaration (`void f(struct X*)`).
STANDALONE_PARENTS = ("translation_unit", "declaration_list", "field_declaration_list",
                      "template_declaration", "preproc_if", "preproc_ifdef", "preproc_else",
                      "preproc_elif", "linkage_specification")


class GuardError(Exception):
    """A condition that stops the scan with exit status 2 and a message."""


@dataclass(frozen=True)
class Config:
    """Where the old and new trees live and what the guard compares."""

    old_root: Path
    old_subdir: str
    new_roots: tuple[Path, ...]
    walk_ns: str
    new_ns: tuple[str, ...]
    corroboration: int
    drops: Path
    folds: Path
    uncarried: tuple[str, ...]
    cxx: str


@dataclass
class Measurement:
    """What one scan measured over the two trees, before any row file is read."""

    headers: list[str]
    surface: dict[tuple[str, str], dict[str, set[str]]]
    verdicts: dict[tuple[str, str], tuple[str, str]]
    notes: list[str]
    old_trees: dict[str, tsast.Tree]
    new_headers: list[str]
    fold_reqs: dict[tuple[str, str], int]
    new_specialised: set[str]
    pair_count: int
    namespace_count: int
    declared_count: int


@dataclass
class Judgement:
    """The verdict of the row files over one measurement."""

    misses: list[tuple[str, str]] = field(default_factory=list)
    fold_misses: list[tuple[str, str, int]] = field(default_factory=list)
    problems: list[str] = field(default_factory=list)
    drops: set[tuple[str, str]] = field(default_factory=set)
    folds: set[tuple[str, str]] = field(default_factory=set)


def is_private_segment(name: str) -> bool:
    """Say whether one namespace name marks a private namespace.

    Args:
        name: One namespace name, "" for an anonymous namespace

    Returns:
        True for detail, self_test, a private suffix, or an anonymous namespace
    """
    return name == "" or name in PRIVATE_NAMES or any(
        len(name) > len(suffix) and name.endswith(suffix) for suffix in PRIVATE_SUFFIXES)


def is_public_path(path: tuple[str, ...]) -> bool:
    """Say whether a namespace path lies wholly outside private namespaces.

    Args:
        path: Enclosing namespaces, outermost first

    Returns:
        True when no segment is private
    """
    return not any(is_private_segment(segment) for segment in path)


def config_from_env() -> Config:
    """Build the configuration from the environment, with the tree defaults.

    Returns:
        The configuration of this run

    Raises:
        GuardError: If no compiler can be found
    """
    env = os.environ
    cxx = env.get("CXX") or (env.get("CRUCIBLE_CXX") if env.get("CRUCIBLE_CXX") and os.access(env["CRUCIBLE_CXX"], os.X_OK) else None) or shutil.which("g++")
    if not cxx:
        raise GuardError("check-port-completeness: no compiler.  Set CXX or CRUCIBLE_CXX to a GCC 16.")
    pins = env.get("PORT_GUARD_UNCARRIED")
    return Config(
        old_root=REPO / env.get("PORT_GUARD_OLD_ROOT", "include"),
        old_subdir=env.get("PORT_GUARD_OLD_SUBDIR", "crucible"),
        new_roots=tuple(REPO / r for r in env.get("PORT_GUARD_NEW_ROOTS", "include/foundation include/fixy").split()),
        walk_ns=env.get("PORT_GUARD_NS", "crucible"),
        new_ns=tuple(env.get("PORT_GUARD_NEW_NS", "foundation fixy").split()),
        corroboration=int(env.get("PORT_GUARD_NS_CORROBORATION", "2")),
        drops=REPO / env.get("PORT_GUARD_DROPS", "scripts/port-drops.txt"),
        folds=REPO / env.get("PORT_GUARD_FOLDS", "scripts/port-folds.txt"),
        uncarried=UNCARRIED_PINNED if pins is None else tuple(pins.split()),
        cxx=cxx,
    )


def display(path: Path) -> str:
    """Return a path relative to the repository when it lies inside it.

    Args:
        path: An absolute path

    Returns:
        The repo-relative spelling, or the absolute one outside the repository
    """
    try:
        return path.relative_to(REPO).as_posix()
    except ValueError:
        return path.as_posix()


# ── The compiler passes ──────────────────────────────────────────────────────


def sentinel_prelude() -> str:
    """Return the shared head of both reflection sentinels.

    Returns:
        C++ source with the includes and the private-namespace predicate
    """
    names = ", ".join(f'"{n}"' for n in PRIVATE_NAMES)
    suffixes = ", ".join(f'"{s}"' for s in PRIVATE_SUFFIXES)
    return (
        "#include <meta>\n#include <cstdio>\n#include <string>\n#include <string_view>\n"
        "namespace port_guard {\n"
        "consteval bool is_private_ns(std::string_view id) noexcept {\n"
        f"    for (std::string_view name : {{{names}}}) if (id == name) return true;\n"
        f"    for (std::string_view suffix : {{{suffixes}}}) {{\n"
        "        if (id.size() > suffix.size() && id.substr(id.size() - suffix.size()) == suffix) return true;\n"
        "    }\n"
        "    return false;\n"
        "}\n"
        "consteval bool is_synthesised(std::string_view id) noexcept {\n"
        "    return id.size() >= 2 && id[0] == '_' && id[1] == '_';\n"
        "}\n"
        "}  // namespace port_guard\n"
    )


# A namespace alias is not recursed into.  An alias whose target is an ancestor
# makes walk<A> call walk<B> and walk<B> call walk<A>: it compiles, and it
# recurses for ever at run time.  The entity is reached through its own name.
OLD_WALK = """
namespace port_guard {
template <std::meta::info NS>
void walk(std::string const& path) {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(NS, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_namespace(member) && !std::meta::is_namespace_alias(member)) {
            if constexpr (std::meta::has_identifier(member)) {
                constexpr std::string_view ns = std::meta::identifier_of(member);
                if constexpr (!is_private_ns(ns)) walk<member>(path + "::" + std::string(ns));
            }
        } else if constexpr (!std::meta::is_namespace(member) && std::meta::has_identifier(member)) {
            constexpr std::string_view id = std::meta::identifier_of(member);
            if constexpr (!is_synthesised(id)) {
                constexpr auto where = std::meta::source_location_of(member);
                const char* kind = std::meta::is_template(member)   ? "template"
                                   : std::meta::is_type_alias(member) ? "alias"
                                   : std::meta::is_type(member)       ? "type"
                                   : std::meta::is_function(member)   ? "function"
                                   : std::meta::is_variable(member)   ? "variable"
                                   : std::meta::is_concept(member)    ? "concept"
                                                                      : "other";
                std::printf("%s\\t%.*s\\t%s\\t%s\\n", where.file_name(), static_cast<int>(id.size()), id.data(), kind,
                            path.c_str());
            }
        }
    }
}
}  // namespace port_guard
"""

NEW_WALK = """
namespace port_guard {
template <std::meta::info NS>
void walk_new(std::string const& path) {
    static constexpr auto members =
        std::define_static_array(std::meta::members_of(NS, std::meta::access_context::unchecked()));
    template for (constexpr auto member : members) {
        if constexpr (std::meta::is_namespace(member) && !std::meta::is_namespace_alias(member)) {
            if constexpr (std::meta::has_identifier(member)) {
                constexpr std::string_view ns = std::meta::identifier_of(member);
                walk_new<member>(path + "::" + std::string(ns));
            }
        } else if constexpr (!std::meta::is_namespace(member) && std::meta::has_identifier(member)) {
            constexpr std::string_view id = std::meta::identifier_of(member);
            if constexpr (!is_synthesised(id)) {
                std::printf("%s\\t%.*s\\n", path.c_str(), static_cast<int>(id.size()), id.data());
            }
        }
    }
}
}  // namespace port_guard
"""


def run_sentinel(cfg: Config, work: Path, name: str, source: str, include_dirs: Iterable[Path], what: str) -> list[str]:
    """Compile one reflection sentinel, run it, and return its output lines.

    Args:
        cfg: The configuration, for the compiler
        work: A scratch directory
        name: The base name of the sentinel files
        source: The C++ source of the sentinel
        include_dirs: The include directories it needs
        what: What a failure means, for the message

    Returns:
        The lines the sentinel printed

    Raises:
        GuardError: If the sentinel does not compile, since a guard that cannot
            measure must not report green
    """
    tu = work / f"{name}.cpp"
    binary = work / name
    tu.write_text(source, encoding="utf-8")
    command = [cfg.cxx, *SENTINEL_FLAGS, *(f"-I{d}" for d in include_dirs), "-o", str(binary), str(tu)]
    built = subprocess.run(command, capture_output=True, text=True, check=False)
    if built.returncode != 0:
        errors = [line for line in built.stderr.splitlines() if "error" in line][:20]
        raise GuardError(
            f"check-port-completeness: the {what} sentinel did not compile, so the guard cannot measure.\n"
            f"  A guard that cannot measure does not report green.  compiler: {cfg.cxx}\n  " + "\n  ".join(errors))
    ran = subprocess.run([str(binary)], capture_output=True, text=True, check=False)
    if ran.returncode != 0:
        raise GuardError(f"check-port-completeness: the {what} sentinel exited {ran.returncode}.")
    return ran.stdout.splitlines()


def enumerate_reflection(cfg: Config, work: Path, headers: list[str]) -> list[tuple[str, str, str, str]]:
    """Return (file, name, kind, namespace) for each public namespace-scope member of the old tree.

    Args:
        cfg: The configuration
        work: A scratch directory
        headers: The superseded headers, relative to the old include root

    Returns:
        One row for each member, the file relative to the old root when inside it
    """
    source = (sentinel_prelude() + "".join(f"#include <{h}>\n" for h in headers) + OLD_WALK
              + f'int main() {{ port_guard::walk<^^{cfg.walk_ns}>("{cfg.walk_ns}"); }}\n')
    rows = set()
    prefix = str(cfg.old_root) + "/"
    for line in run_sentinel(cfg, work, "old_sentinel", source, [cfg.old_root], "reflection"):
        parts = line.split("\t")
        if len(parts) != 4:
            continue
        path = parts[0][len(prefix):] if parts[0].startswith(prefix) else parts[0]
        rows.add((path, parts[1], parts[2], parts[3]))
    return sorted(rows)


def enumerate_macros(cfg: Config, work: Path, headers: list[str]) -> list[tuple[str, str]]:
    """Return (header, macro) for each #define the preprocessor attributes to a superseded header.

    The input is the compiler's -dD output, whose line markers name the file of
    each definition, so a #define in a false #if arm never appears.

    Args:
        cfg: The configuration
        work: A scratch directory
        headers: The superseded headers, relative to the old include root

    Returns:
        One row for each macro, sorted
    """
    tu = work / "macros.cpp"
    tu.write_text("".join(f"#include <{h}>\n" for h in headers), encoding="utf-8")
    done = subprocess.run([cfg.cxx, *SENTINEL_FLAGS, f"-I{cfg.old_root}", "-dD", "-E", str(tu)],
                          capture_output=True, text=True, check=False)
    prefix = str(cfg.old_root) + "/"
    current = ""
    rows = set()
    for line in done.stdout.splitlines():
        if line.startswith("# ") and '"' in line:
            marker = line.split('"')
            if len(marker) >= 2 and marker[0].split()[1:2] and marker[0].split()[1].isdigit():
                current = marker[1][len(prefix):] if marker[1].startswith(prefix) else marker[1]
            continue
        if not line.startswith("#define "):
            continue
        if not current.startswith(cfg.old_subdir + "/"):
            continue
        base = current.rsplit("/", 1)[-1]
        if not (base.startswith("_") and base.endswith(".h")):
            continue
        name = line.split()[1].split("(", 1)[0]
        if not name.startswith("__"):
            rows.add((current, name))
    return sorted(rows)


# ── The parse-tree passes ────────────────────────────────────────────────────


def parse_map(paths: list[Path], keys: list[str]) -> dict[str, tsast.Tree]:
    """Parse files and key each tree by the spelling the caller uses for it.

    Args:
        paths: Files to parse
        keys: The key for each file, in the same order

    Returns:
        The trees, keyed

    Raises:
        GuardError: If a file does not parse clean
    """
    try:
        trees = list(tsast.parse(paths))
    except tsast.ParseError as exc:
        raise GuardError(f"check-port-completeness: {exc}") from exc
    return dict(zip(keys, trees))


def is_namespace_scope(node: tsast.Node) -> bool:
    """Say whether a declaration sits at namespace scope, outside every class and function body.

    Args:
        node: A declaration node

    Returns:
        True when no class body, function body, lambda or parameter list encloses it
    """
    return node.ancestor_of_type(
        "field_declaration_list", "compound_statement", "parameter_list", "lambda_expression",
        "template_parameter_list", "requires_expression", "enumerator_list") is None


def declared_name(node: tsast.Node) -> str | None:
    """Return the name a declaration node introduces, or None when it introduces none.

    Args:
        node: A class, enum, alias, typedef, concept, variable or function declaration

    Returns:
        The declared identifier
    """
    if node.type in TYPE_KINDS or node.type in ("alias_declaration", "concept_definition"):
        name = node.child_by_field("name")
        return tsast.leaf_name(name) if name is not None else None
    if node.type in ("declaration", "field_declaration", "type_definition", "function_definition"):
        declarator = node.child_by_field("declarator")
        return tsast.leaf_name(declarator) if declarator is not None else None
    return None


DECLARATION_KINDS = TYPE_KINDS + ("alias_declaration", "concept_definition", "declaration",
                                  "type_definition", "function_definition")


def public_declarations(tree: tsast.Tree) -> set[str]:
    """Return the names a header declares at public namespace scope.

    Args:
        tree: One parsed superseded header

    Returns:
        The declared names outside class bodies, function bodies and private namespaces
    """
    names = set()
    for node in tree.find(*DECLARATION_KINDS):
        if node.type in TYPE_KINDS and node.parent is not None and node.parent.type not in STANDALONE_PARENTS \
                and node.child_by_field("body") is None:
            continue
        if not is_namespace_scope(node) or not is_public_path(tsast.namespace_path(node)):
            continue
        name = declared_name(node)
        if name:
            names.add(name)
    return names


def public_reexports(tree: tsast.Tree) -> set[str]:
    """Return the names a header re-exports with a using-declaration at public namespace scope.

    Args:
        tree: One parsed superseded header

    Returns:
        The last name of each re-exported entity
    """
    names = set()
    for using in tsast.using_names(tree):
        if using.is_directive or not using.target:
            continue
        if not is_namespace_scope(using.node) or not is_public_path(tsast.namespace_path(using.node)):
            continue
        last = using.target[-1]
        if last and not last.startswith("__"):
            names.add(last)
    return names


def declared_names_anywhere(tree: tsast.Tree) -> set[str]:
    """Return every name a new-tree header declares, at namespace or class scope, and every macro it defines.

    A call, a parameter, a local variable and a template parameter declare
    nothing here, so a name the header only uses does not count.

    Args:
        tree: One parsed new-tree header

    Returns:
        The declared names
    """
    names = set()
    local_scopes = ("compound_statement", "parameter_list", "lambda_expression",
                    "template_parameter_list", "requires_expression")
    for node in tree.find(*DECLARATION_KINDS):
        if node.ancestor_of_type(*local_scopes) is not None:
            continue
        if node.type in TYPE_KINDS and node.parent is not None and node.parent.type not in STANDALONE_PARENTS \
                and node.child_by_field("body") is None:
            continue
        name = declared_name(node)
        if name:
            names.add(name)
    for node in tree.find("field_declaration", "enumerator", "preproc_def", "preproc_function_def"):
        if node.type in ("preproc_def", "preproc_function_def", "enumerator"):
            name = node.child_by_field("name")
            if name is not None:
                names.add(name.text)
        elif node.ancestor_of_type(*local_scopes) is None:
            for declarator in node.children:
                if declarator.field == "declarator":
                    leaf = tsast.leaf_name(declarator)
                    if leaf:
                        names.add(leaf)
    for using in tsast.using_names(tree):
        if not using.is_directive and using.target:
            names.add(using.target[-1])
    return names


def class_template_entries(tree: tsast.Tree) -> Iterator[tuple[str, str]]:
    """Yield ("S" or "P", name) for each class-template declaration and variable template of a file.

    S is a specialisation, explicit or partial: a class, struct or union named by a
    template-id, qualified or not, or a variable template whose declarator is a
    template-id.  P is a primary: a class, struct or union named by a plain
    identifier, or a variable template with a plain declarator.  A specialisation
    that a macro body spells counts once for the body.

    Args:
        tree: One parsed header

    Yields:
        One entry for each declaration
    """
    for node in tree.find(*CLASS_KINDS):
        if node.parent is None or node.parent.type not in STANDALONE_PARENTS:
            continue
        name = node.child_by_field("name")
        if name is None:
            continue
        tail = name
        while tail.type == "qualified_identifier" and tail.child_by_field("name") is not None:
            tail = tail.child_by_field("name")
        if tail.type == "template_type":
            base = tail.child_by_field("name")
            if base is not None:
                yield "S", base.text
        elif tail.type == "type_identifier" and name.type == "type_identifier":
            yield "P", tail.text
    for template in tree.find("template_declaration"):
        for declaration in template.children_of_type("declaration"):
            for init in declaration.children_of_type("init_declarator"):
                declarator = init.child_by_field("declarator")
                if declarator is None:
                    continue
                if declarator.type == "template_function":
                    base = declarator.child_by_field("name")
                    if base is not None:
                        yield "S", base.text
                elif declarator.type == "identifier":
                    yield "P", declarator.text
    for body in tree.find("preproc_arg"):
        if body.parent is None or body.parent.type not in ("preproc_def", "preproc_function_def"):
            continue
        tokens = [t.text for t in tsast.pp_tokens(body.text)]
        for i in range(len(tokens) - 2):
            if tokens[i] in ("struct", "class", "union") and tokens[i + 2] == "<" \
                    and (tokens[i + 1][:1].isalpha() or tokens[i + 1][:1] == "_"):
                yield "S", tokens[i + 1]


def is_forward_declared_only(tree: tsast.Tree, symbol: str) -> bool:
    """Say whether a header declares a symbol only as an opaque forward declaration.

    A forward declaration is a class, struct, union or enum declaration of the
    name with no body.  A definition, an alias, a concept, or a specialisation of
    the name with a body means the header owns it.

    Args:
        tree: One parsed superseded header
        symbol: The symbol of the drop row

    Returns:
        True when every declaration of the name in the header is opaque
    """
    forward = False
    for node in tree.find(*TYPE_KINDS):
        name = node.child_by_field("name")
        if name is None:
            continue
        has_body = node.child_by_field("body") is not None
        if name.type == "type_identifier" and name.text == symbol:
            if has_body:
                return False
            if node.parent is not None and node.parent.type in STANDALONE_PARENTS:
                forward = True
        elif name.type == "template_type" and tsast.leaf_name(name) == symbol and has_body:
            return False
    for node in tree.find("alias_declaration", "concept_definition"):
        name = node.child_by_field("name")
        if name is not None and name.text == symbol:
            return False
    return forward


# ── Measurement ──────────────────────────────────────────────────────────────


def list_superseded(cfg: Config) -> list[str]:
    """Return every superseded header, relative to the old include root, sorted.

    Args:
        cfg: The configuration

    Returns:
        The `_Foo.h` headers under the old subtree
    """
    base = cfg.old_root / cfg.old_subdir
    return sorted(p.relative_to(cfg.old_root).as_posix() for p in base.rglob("_*.h") if p.is_file())


def attribute(reflection: list[tuple[str, str, str, str]], headers: list[str],
              declared: dict[str, set[str]]) -> tuple[list[tuple[str, str, str]], list[str]]:
    """Attribute each reflected name to the superseded headers that declare it.

    Args:
        reflection: (first file, name, kind, namespace) rows from the sentinel
        headers: The superseded headers
        declared: The public declarations of each superseded header

    Returns:
        (header, name, namespace) rows, and the NOTE lines for rescued names
    """
    superseded = set(headers)
    by_name: dict[str, list[tuple[str, str]]] = defaultdict(list)
    for path, name, _kind, namespace in reflection:
        by_name[name].append((path, namespace))
    rows = []
    notes = []
    for name in sorted(by_name):
        entries = by_name[name]
        first = entries[0][0]
        claims = sorted(h for h in headers if name in declared[h])
        first_is_superseded = first in superseded
        if not claims and not first_is_superseded:
            continue
        if claims and not first_is_superseded:
            notes.append(f"NOTE  {name} is first declared in {first} (not superseded); attributed by its parse tree to: "
                         + ",".join(claims))
        owners = sorted(set(claims) | ({first} if first_is_superseded else set()))
        everywhere = {namespace for _path, namespace in entries}
        for owner in owners:
            spaces = {namespace for path, namespace in entries if path == owner} or everywhere
            for namespace in sorted(spaces):
                rows.append((owner, name, namespace))
    return rows, notes


def decide_presence(surface: dict[tuple[str, str], dict[str, set[str]]], pairs: set[tuple[str, str]],
                    declared: set[str], floor: int) -> dict[tuple[str, str], tuple[str, str]]:
    """Decide present or absent for each (header, symbol) of the surface.

    Complexity: O(surface rows x namespaces a name is held in).

    Args:
        surface: (header, symbol) -> method -> namespaces ("-" when none)
        pairs: The (namespace, symbol) pairs the new tree declares
        declared: The names the new tree declares anywhere
        floor: The corroboration floor

    Returns:
        (header, symbol) -> (verdict, rule)
    """
    holders: dict[str, set[str]] = defaultdict(set)
    for namespace, symbol in pairs:
        holders[symbol].add(namespace)
    old_members = {(namespace, symbol) for (_h, symbol), methods in surface.items()
                   for spaces in methods.values() for namespace in spaces if namespace != "-"}
    hits: dict[tuple[str, str], int] = defaultdict(int)
    for old, symbol in old_members:
        for new in holders[symbol]:
            hits[(old, new)] += 1
    target: dict[str, set[str]] = defaultdict(set)
    for (old, new), count in hits.items():
        if count >= floor:
            target[old].add(new)
    verdicts = {}
    for key, methods in surface.items():
        symbol = key[1]
        spaces = sorted({namespace for s in methods.values() for namespace in s})
        verdict, rule = "present", "qualified"
        for namespace in spaces:
            if namespace == "-" or not target.get(namespace):
                rule = "bare"
                here = symbol in declared
            else:
                here = any((new, symbol) in pairs for new in target[namespace])
            if not here:
                verdict = "absent"
        if not spaces:
            rule = "bare"
            verdict = "present" if symbol in declared else "absent"
        verdicts[key] = (verdict, rule)
    return verdicts


def measure(cfg: Config) -> Measurement:
    """Enumerate the old surface, index the new tree and tally the templates on both sides.

    Args:
        cfg: The configuration

    Returns:
        The measurement

    Raises:
        GuardError: If a sentinel does not compile, a header does not parse, or a tree is empty
    """
    headers = list_superseded(cfg)
    if not headers:
        raise GuardError(f"check-port-completeness: no superseded headers under {display(cfg.old_root / cfg.old_subdir)}.")
    new_headers = sorted(display(p) for root in cfg.new_roots if root.is_dir() for p in root.rglob("*.h") if p.is_file())
    if not new_headers:
        raise GuardError("check-port-completeness: no headers under the new roots. Nothing to compare against.")
    old_trees = parse_map([cfg.old_root / h for h in headers], headers)
    new_trees = parse_map([REPO / h for h in new_headers], new_headers)

    with tempfile.TemporaryDirectory() as scratch:
        work = Path(scratch)
        reflection = enumerate_reflection(cfg, work, headers)
        macros = enumerate_macros(cfg, work, headers)
        includes = sorted({root.parent for root in cfg.new_roots if root.is_dir()})

        def spelling(header: str) -> str:
            """Return the include spelling of a new-tree header, relative to its include directory."""
            path = REPO / header
            return path.relative_to(next(d for d in includes if path.is_relative_to(d))).as_posix()

        source = (sentinel_prelude() + "".join(f"#include <{spelling(h)}>\n" for h in new_headers)
                  + NEW_WALK + "int main() {" + "".join(f' port_guard::walk_new<^^{ns}>("{ns}");' for ns in cfg.new_ns) + " }\n")
        pair_lines = run_sentinel(cfg, work, "new_sentinel", source, includes, "new-tree")
    pairs = {tuple(line.split("\t", 1)) for line in pair_lines if "\t" in line}

    declared = {h: public_declarations(old_trees[h]) for h in headers}
    rows, notes = attribute(reflection, headers, declared)
    surface: dict[tuple[str, str], dict[str, set[str]]] = defaultdict(lambda: defaultdict(set))
    for header, name, namespace in rows:
        surface[(header, name)]["reflection"].add(namespace)
    for header, name in macros:
        surface[(header, name)]["macro"].add("-")
    for header in headers:
        for name in public_reexports(old_trees[header]):
            surface[(header, name)]["reexport"].add("-")

    new_declared: set[str] = set()
    for tree in new_trees.values():
        new_declared |= declared_names_anywhere(tree)
    verdicts = decide_presence(surface, pairs, new_declared, cfg.corroboration)

    old_spec: dict[str, int] = defaultdict(int)
    for tree in old_trees.values():
        for kind, name in class_template_entries(tree):
            if kind == "S":
                old_spec[name] += 1
    new_spec: set[str] = set()
    homes: set[tuple[str, str]] = set()
    for header, tree in new_trees.items():
        for kind, name in class_template_entries(tree):
            if kind == "S":
                new_spec.add(name)
            else:
                homes.add((header, name))
    fold_reqs = {(header, name): old_spec[name] for header, name in homes
                 if name in old_spec and name not in new_spec}

    return Measurement(
        headers=headers, surface=surface, verdicts=verdicts, notes=notes, old_trees=old_trees,
        new_headers=new_headers, fold_reqs=fold_reqs, new_specialised=new_spec,
        pair_count=len(pairs), namespace_count=len({ns for ns, _s in pairs}), declared_count=len(new_declared),
    )


# ── The row files ────────────────────────────────────────────────────────────


def split_row(line: str, kind: str) -> tuple[str, str | None, str] | str:
    """Split one row into its key, its optional carrier and its sentence.

    Args:
        line: One non-comment row
        kind: DROP or FOLD, for the message

    Returns:
        (key, carrier or None, sentence), or a MALFORMED message
    """
    if " — " not in line:
        return f"MALFORMED {kind}  {line}\n  (expected `<header>:<symbol>  [→ <carrier>]  — <one sentence>.`)"
    key, sentence = line.split(" — ", 1)
    key, sentence = key.strip(), sentence.strip()
    carrier = None
    if " → " in key:
        key, carrier = (part.strip() for part in key.split(" → ", 1))
        if not carrier or any(c.isspace() for c in carrier):
            return (f"MALFORMED {kind}  {line}\n  (the carrier after the arrow must be one header path under a new "
                    "root, or the word none)")
    if ":" not in key or not key.split(":", 1)[0] or not key.rsplit(":", 1)[1]:
        return f"MALFORMED {kind}  {line}\n  (no `<header>:<symbol>` key)"
    if not sentence.endswith("."):
        return f"MALFORMED {kind}  {key}\n  (the sentence after the em dash must be non-empty and end with a period)"
    return key, carrier, sentence


def row_lines(path: Path) -> Iterator[str]:
    """Yield the non-empty, non-comment rows of a row file; nothing when the file is absent.

    Args:
        path: The row file

    Yields:
        Each row
    """
    if not path.is_file():
        return
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.strip() and not line.startswith("#"):
            yield line


def is_new_tree_header(cfg: Config, carrier: str) -> bool:
    """Say whether a carrier names an existing header under one of the new roots.

    Args:
        cfg: The configuration
        carrier: The carrier path of a drop row

    Returns:
        True for an existing .h file under a new root
    """
    path = Path(carrier) if Path(carrier).is_absolute() else REPO / carrier
    return carrier.endswith(".h") and path.is_file() and any(path.is_relative_to(root) for root in cfg.new_roots)


def judge(cfg: Config, m: Measurement) -> Judgement:
    """Read the drops and folds files against a measurement.

    Args:
        cfg: The configuration
        m: The measurement

    Returns:
        The misses, the fold misses and every row problem
    """
    j = Judgement()
    seen_none: set[str] = set()
    for line in row_lines(cfg.drops):
        parsed = split_row(line, "DROP")
        if isinstance(parsed, str):
            j.problems.append(parsed)
            continue
        key, carrier, _sentence = parsed
        header, symbol = key.split(":", 1)
        if (header, symbol) not in m.surface:
            j.problems.append(f"STALE DROP      {header}  {symbol}\n  (that header no longer declares this symbol — remove the entry)")
            continue
        if m.verdicts[(header, symbol)][0] == "present":
            j.problems.append(f"OBSOLETE DROP   {header}  {symbol}\n  (the symbol now exists in the new tree — it is no "
                              "longer a drop; remove the entry)")
            continue
        j.drops.add((header, symbol))
        if carrier is None:
            if is_forward_declared_only(m.old_trees[header], symbol):
                j.problems.append(
                    f"UNCARRIED DEFERRAL  {header}  {symbol}\n  (that header only forward-declares the symbol, so it "
                    "never owned the type; name the new-tree\n   header that accounts for it with `→ <header>`, or "
                    "write `→ none` and pin the row in the guard)")
            continue
        if carrier == "none":
            seen_none.add(key)
            if key not in cfg.uncarried:
                j.problems.append(
                    f"UNPINNED UNCARRIED  {header}  {symbol}\n  (`→ none` is admitted only for the rows the guard pins "
                    "by name in UNCARRIED_PINNED; a new\n   uncarried type is a new gap, so admitting it is an edit to "
                    "the guard, not to this file)")
            continue
        if not is_new_tree_header(cfg, carrier):
            j.problems.append(f"MISSING CARRIER  {header}  {symbol}  → {carrier}\n  (the carrier must be an existing "
                              "header under one of the new roots)")
    for pin in cfg.uncarried:
        if pin not in seen_none:
            j.problems.append(f"STALE PIN       {pin}\n  (pinned as uncarried, but no loaded drop row writes `→ none` "
                              "for it — remove it from UNCARRIED_PINNED)")
    for line in row_lines(cfg.folds):
        parsed = split_row(line, "FOLD")
        if isinstance(parsed, str):
            j.problems.append(parsed)
            continue
        key, _carrier, _sentence = parsed
        header, name = key.rsplit(":", 1)
        if (header, name) not in m.fold_reqs:
            if name in m.new_specialised:
                j.problems.append(f"OBSOLETE FOLD   {header}  {name}\n  (the new tree specializes this trait again — "
                                  "there is no fold left to justify; remove the entry)")
            else:
                j.problems.append(f"STALE FOLD      {header}  {name}\n  (that header declares no primary of this name "
                                  "the old tree specialized — remove the entry)")
            continue
        j.folds.add((header, name))
    j.misses = sorted(k for k, (verdict, _rule) in m.verdicts.items() if verdict == "absent" and k not in j.drops)
    j.fold_misses = sorted((h, n, c) for (h, n), c in m.fold_reqs.items() if (h, n) not in j.folds)
    return j


# ── Reporting ────────────────────────────────────────────────────────────────


def report(cfg: Config, m: Measurement, j: Judgement, quiet: bool) -> int:
    """Print the surface, the misses and the row problems, and return the exit status.

    Args:
        cfg: The configuration
        m: The measurement
        j: The judgement
        quiet: When true, do not print the enumerated surface

    Returns:
        0, 1 or 2 as the module docstring states
    """
    for note in m.notes:
        print(note, file=sys.stderr)
    if not quiet:
        rows = sorted((h, s, method, ns) for (h, s), methods in m.surface.items()
                      for method, spaces in methods.items() for ns in spaces)
        print(f"== enumerated surface: {len(m.headers)} superseded headers, {len(rows)} (header, symbol, method) rows ==")
        for row in rows:
            print("\t".join(row))
        print("\n== method per header ==")
        for header in m.headers:
            counts = {method: sum(1 for (h, _s), methods in m.surface.items() if h == header and method in methods)
                      for method in ("reflection", "macro", "reexport")}
            print(f"{header}  reflection={counts['reflection']} macros={counts['macro']} reexports={counts['reexport']}")
        print(f"\n== new-tree declared-name index: {m.declared_count} names ==")
        print(f"== new-tree qualified index: {m.pair_count} (namespace, symbol) pairs in {m.namespace_count} namespaces ==\n")
    for problem in j.problems:
        print(problem, file=sys.stderr)
    for header, symbol in j.misses:
        methods = "+".join(sorted(m.surface[(header, symbol)]))
        rule = m.verdicts[(header, symbol)][1]
        spaces = ",".join(sorted(ns for s in m.surface[(header, symbol)].values() for ns in s if ns != "-"))
        if rule == "qualified" and spaces:
            print(f"MISSING PORT    {header}  {symbol}  ({methods}; {spaces} declares it and the new tree does not)")
        else:
            print(f"MISSING PORT    {header}  {symbol}  ({methods}; matched against the declared names)")
    if j.misses:
        print(f"\ncheck-port-completeness: {len(j.misses)} symbol(s) from superseded headers have no home in the new tree\n"
              f"  and no entry in {display(cfg.drops)}.  Port each one, or write its sentence there.\n"
              "  `--emit-drops` prints a template line per miss.", file=sys.stderr)
    for header, name, count in j.fold_misses:
        print(f"UNJUSTIFIED FOLD  {header}  {name}  (the old tree specializes it {count} time(s); the new tree, never)")
    if j.fold_misses:
        print(f"\ncheck-port-completeness: {len(j.fold_misses)} trait(s) kept their name through the port and lost every\n"
              f"  specialization, with no entry in {display(cfg.folds)}.  Restore the discrimination, or write the\n"
              "  sentence that names what now computes the answer.  `--emit-folds` prints a template line.",
              file=sys.stderr)
    if j.misses or j.fold_misses:
        return 1
    if j.problems:
        print(f"check-port-completeness: no missing ports and no unjustified folds, but {display(cfg.drops)} or "
              f"{display(cfg.folds)}\n  has entries that no longer hold.", file=sys.stderr)
        return 2
    print(f"check-port-completeness: clean — every symbol of {len(m.headers)} superseded headers is present or has a "
          f"written drop ({len(j.drops)} drops),\n  and every trait that lost its specializations has a written fold "
          f"({len(j.folds)} folds).")
    return 0


def emit_drops(m: Measurement, j: Judgement) -> None:
    """Print a template drop row for each miss.

    Args:
        m: The measurement
        j: The judgement
    """
    for header, symbol in j.misses:
        if is_forward_declared_only(m.old_trees[header], symbol):
            print(f"{header}:{symbol}  → <new-tree header that accounts for the type, or none>  — <why this symbol has no home in the new tree>.")
        else:
            print(f"{header}:{symbol}  — <why this symbol has no home in the new tree>.")


def emit_folds(j: Judgement) -> None:
    """Print a template fold row for each unjustified fold.

    Args:
        j: The judgement
    """
    for header, name, count in j.fold_misses:
        print(f"{header}:{name}  — <what now computes the answer the {count} specializations spelled out>.")


# ── Self-test ────────────────────────────────────────────────────────────────

OLD_PLANTED = {
    # One ported symbol and one unported.
    "_PlantedMinimal.h": "#pragma once\nnamespace crucible {\nstruct planted_ported {};\nstruct planted_unported {};\n}\n",
    # A template, an alias reported under its own name, a re-export from a
    # private namespace, a macro, and a private member that is never reported.
    "_PlantedBlindSpots.h": (
        "#pragma once\n#define PLANTED_MACRO_UNPORTED 1\nnamespace crucible {\nstruct planted_ported_base {};\n"
        "template <class T> struct planted_template_unported {};\nusing planted_alias_unported = planted_ported_base;\n"
        "namespace detail {\nstruct planted_reexport_unported {};\nstruct planted_private {};\n}\n"
        "using detail::planted_reexport_unported;\n}\n"),
    # Two siblings port into one new namespace, which teaches the guard where the
    # namespace went; the third does not, and its bare name lives elsewhere.
    "_PlantedQualified.h": (
        "#pragma once\nnamespace crucible::planted_family {\nstruct planted_sibling_one {};\n"
        "struct planted_sibling_two {};\nstruct planted_collided_name {};\n}\n"),
    # One name declared by two headers in two namespaces, neither ported.
    "_PlantedHomeA.h": "#pragma once\n#define PLANTED_MACRO_KEYED 1\nnamespace crucible::planted_home_a {\nstruct planted_two_homes {};\n}\n",
    "_PlantedHomeB.h": "#pragma once\nnamespace crucible::planted_home_b {\nstruct planted_two_homes {};\n}\n",
    # Fold controls: planted_folded loses its specialisation, planted_kept keeps
    # one, planted_var_folded is a variable template that loses its own, and
    # planted_qualified_kept keeps one that the new tree spells qualified.
    "_PlantedFold.h": (
        "#pragma once\nnamespace crucible {\ntemplate <class T>\nstruct planted_folded { static constexpr bool value = false; };\n"
        "template <>\nstruct planted_folded<int> { static constexpr bool value = true; };\n"
        "template <class T>\nstruct planted_kept { static constexpr bool value = false; };\n"
        "template <>\nstruct planted_kept<int> { static constexpr bool value = true; };\n"
        "template <class T> inline constexpr bool planted_var_folded = false;\n"
        "template <> inline constexpr bool planted_var_folded<int> = true;\n"
        "template <class T> struct planted_qualified_kept { static constexpr bool value = false; };\n"
        "template <> struct planted_qualified_kept<int> { static constexpr bool value = true; };\n}\n"),
    # Carrier controls: two opaque declarations, one with alignas, and a
    # body-less primary the header specialises, which is its own trait.
    "_PlantedFwd.h": (
        "#pragma once\nnamespace crucible {\nstruct planted_fwd_carried;\nenum class planted_fwd_uncarried : unsigned char;\n"
        "struct alignas(64) planted_fwd_aligned;\n"
        "template <class T>\nstruct planted_trait;\ntemplate <>\nstruct planted_trait<int> { static constexpr bool value = true; };\n}\n"),
    # Positive controls for the text scan: a namespace whose brace sits on the
    # next line, and a re-export split over two lines.
    "_PlantedLayout.h": (
        "#pragma once\nnamespace crucible::detail {\nstruct planted_split_reexport {};\nstruct planted_brace_reexport {};\n}\n"
        "namespace crucible\n{\nusing ::crucible::detail::\n    planted_split_reexport;\n}\n"
        "namespace crucible::planted_braced\n{\nusing ::crucible::detail::planted_brace_reexport;\n}\n"),
}

NEW_PLANTED = (
    "#pragma once\nnamespace planted_new {\nstruct planted_ported {};\nstruct planted_ported_base {};\n"
    "namespace planted_family {\nstruct planted_sibling_one {};\nstruct planted_sibling_two {};\n}\n"
    "namespace planted_elsewhere {\nstruct planted_collided_name {};\n}\n"
    "template <class T>\nstruct planted_folded { static constexpr bool value = false; };\n"
    "template <class T>\nstruct planted_kept { static constexpr bool value = false; };\n"
    "template <>\nstruct planted_kept<int> { static constexpr bool value = true; };\n"
    "template <class T> inline constexpr bool planted_var_folded = false;\n"
    "template <class T> struct planted_qualified_kept { static constexpr bool value = false; };\n"
    "inline int planted_unported_parameter(int planted_unported) { return planted_unported; }\n}\n"
    "template <> struct planted_new::planted_qualified_kept<int> { static constexpr bool value = true; };\n"
)


def self_test() -> int:
    """Plant the controls in a scratch tree, measure once, and judge each arm.

    Returns:
        0 when every arm holds, 2 otherwise
    """
    failures: list[str] = []

    def check(name: str, held: bool, detail: str = "") -> None:
        """Record one arm.

        Args:
            name: What the arm proves
            held: Whether it held
            detail: Output to print on a failure
        """
        print(f"  {'ok  ' if held else 'FAIL'} {name}")
        if not held:
            failures.append(name)
            if detail:
                print("       " + detail.replace("\n", "\n       "))

    base = config_from_env()
    with tempfile.TemporaryDirectory() as scratch:
        work = Path(scratch)
        (work / "old" / "crucible").mkdir(parents=True)
        (work / "new" / "foundation").mkdir(parents=True)
        for name, text in OLD_PLANTED.items():
            (work / "old" / "crucible" / name).write_text(text, encoding="utf-8")
        planted_new = work / "new" / "foundation" / "Planted.h"
        planted_new.write_text(NEW_PLANTED, encoding="utf-8")
        folds_ok = work / "folds-ok.txt"
        folds_ok.write_text(f"{planted_new}:planted_folded  — planted as a legitimate fold.\n"
                            f"{planted_new}:planted_var_folded  — planted as a legitimate variable-template fold.\n",
                            encoding="utf-8")
        drops_ok = work / "drops-ok.txt"
        carried_row = "crucible/_PlantedFwd.h:planted_fwd_carried"
        drops_ok.write_text(
            "# planted\n"
            f"{carried_row}  → {planted_new}  — planted with a carrier that exists.\n"
            "crucible/_PlantedFwd.h:planted_fwd_uncarried  → none  — planted as a pinned uncarried type.\n"
            f"crucible/_PlantedFwd.h:planted_fwd_aligned  → {planted_new}  — planted opaque with alignas.\n"
            "crucible/_PlantedFwd.h:planted_trait  — planted; the header owns this trait, so no carrier is owed.\n"
            "crucible/_PlantedMinimal.h:planted_unported  — planted by the self-test as a legitimate drop.\n"
            "crucible/_PlantedBlindSpots.h:planted_template_unported  — planted.\n"
            "crucible/_PlantedBlindSpots.h:planted_alias_unported  — planted.\n"
            "crucible/_PlantedBlindSpots.h:planted_reexport_unported  — planted.\n"
            "crucible/_PlantedBlindSpots.h:PLANTED_MACRO_UNPORTED  — planted.\n"
            "crucible/_PlantedQualified.h:planted_collided_name  — planted.\n"
            "crucible/_PlantedHomeA.h:planted_two_homes  — planted.\n"
            "crucible/_PlantedHomeB.h:planted_two_homes  — planted.\n"
            "crucible/_PlantedHomeA.h:PLANTED_MACRO_KEYED  — planted.\n"
            "crucible/_PlantedLayout.h:planted_split_reexport  — planted.\n"
            "crucible/_PlantedLayout.h:planted_brace_reexport  — planted.\n",
            encoding="utf-8")
        pin_ok = ("crucible/_PlantedFwd.h:planted_fwd_uncarried",)

        def cfg_for(drops: Path, folds: Path = folds_ok, pins: tuple[str, ...] = pin_ok) -> Config:
            """Return the scratch configuration with the given row files.

            Args:
                drops: The drops file
                folds: The folds file
                pins: The uncarried pins

            Returns:
                A configuration over the scratch tree
            """
            return Config(old_root=work / "old", old_subdir="crucible", new_roots=(work / "new" / "foundation",),
                          walk_ns="crucible", new_ns=("planted_new",), corroboration=2, drops=drops, folds=folds,
                          uncarried=pins, cxx=base.cxx)

        def variant(name: str, lines: list[str], drop_prefix: str | None = None) -> Path:
            """Write drops-ok.txt with one row removed and extra rows appended.

            Args:
                name: The file name
                lines: Rows to append
                drop_prefix: Remove each row that starts with this text

            Returns:
                The written path
            """
            kept = [line for line in drops_ok.read_text(encoding="utf-8").splitlines()
                    if drop_prefix is None or not line.startswith(drop_prefix)]
            path = work / name
            path.write_text("\n".join(kept + lines) + "\n", encoding="utf-8")
            return path

        try:
            m = measure(cfg_for(drops_ok))
        except GuardError as exc:
            print(f"check-port-completeness --self-test: FAILED, the planted tree could not be measured:\n{exc}")
            return 2

        def run(drops: Path, folds: Path = folds_ok, pins: tuple[str, ...] = pin_ok) -> tuple[int, Judgement, str]:
            """Judge the measurement with the given row files.

            Args:
                drops: The drops file
                folds: The folds file
                pins: The uncarried pins

            Returns:
                (exit status, judgement, all problem text)
            """
            cfg = cfg_for(drops, folds, pins)
            j = judge(cfg, m)
            status = 1 if (j.misses or j.fold_misses) else 2 if j.problems else 0
            return status, j, "\n".join(j.problems)

        missing = work / "no-such-drops.txt"
        status, j, _ = run(missing)
        reported = sorted(s for _h, s in j.misses)
        want = sorted(["PLANTED_MACRO_KEYED", "PLANTED_MACRO_UNPORTED", "planted_alias_unported",
                       "planted_collided_name", "planted_fwd_aligned", "planted_fwd_carried", "planted_fwd_uncarried",
                       "planted_reexport_unported", "planted_template_unported", "planted_trait", "planted_two_homes",
                       "planted_two_homes", "planted_unported", "planted_split_reexport", "planted_brace_reexport"])
        check("the misses are exactly the planted unported set: a template, an alias, a re-export, a macro, "
              "a qualified collision, two homes; the private member is not one", status == 1 and reported == want,
              f"status {status}, reported {reported}\nwant {want}")
        check("a name the new tree only uses as a parameter is not a declaration", "planted_unported" in reported)
        check("a re-export under a brace on the next line, and one split over two lines, are both enumerated",
              "planted_split_reexport" in reported and "planted_brace_reexport" in reported)

        status, j, text = run(drops_ok)
        check("every miss with a written drop is clean", status == 0, f"status {status}\n{text}")

        status, j, text = run(variant("drops-stale.txt", ["crucible/_PlantedMinimal.h:planted_never_existed  — stale on purpose."]))
        check("a stale drop is exit 2", status == 2 and "STALE DROP" in text and "planted_never_existed" in text, text)

        status, j, text = run(variant("drops-obsolete.txt", ["crucible/_PlantedMinimal.h:planted_ported  — obsolete on purpose."]))
        check("an obsolete drop is exit 2", status == 2 and "OBSOLETE DROP" in text and "planted_ported" in text, text)

        status, j, text = run(variant("drops-prose.txt", ["crucible/_PlantedBlindSpots.h:PLANTED_MACRO_UNPORTED  — "],
                                      "crucible/_PlantedBlindSpots.h:PLANTED_MACRO_UNPORTED"))
        check("a sentence-less drop is rejected and its symbol is then missing", status != 0 and "MALFORMED DROP" in text, text)

        status, j, text = run(variant("drops-one-home.txt", [], "crucible/_PlantedHomeB.h:planted_two_homes"))
        check("a row for one header does not silence the same name in another",
              status == 1 and j.misses == [("crucible/_PlantedHomeB.h", "planted_two_homes")], str(j.misses))

        moved = variant("drops-macro-moved.txt", ["crucible/_PlantedHomeB.h:PLANTED_MACRO_KEYED  — planted."],
                        "crucible/_PlantedHomeA.h:PLANTED_MACRO_KEYED")
        status, j, text = run(moved)
        check("a macro row is keyed by header and name",
              status == 1 and "STALE DROP" in text and ("crucible/_PlantedHomeA.h", "PLANTED_MACRO_KEYED") in j.misses, text)

        empty = work / "folds-empty.txt"
        empty.write_text("", encoding="utf-8")
        status, j, text = run(drops_ok, empty)
        folded = sorted(n for _h, n, _c in j.fold_misses)
        check("a lost specialisation is a fold, a kept or qualified one is not, a variable template counts",
              status == 1 and folded == ["planted_folded", "planted_var_folded"], str(folded))

        obsolete = work / "folds-obsolete.txt"
        obsolete.write_text(folds_ok.read_text(encoding="utf-8") + f"{planted_new}:planted_kept  — obsolete on purpose.\n",
                            encoding="utf-8")
        status, j, text = run(drops_ok, obsolete)
        check("a fold row for a trait the new tree still specializes is obsolete", status == 2 and "OBSOLETE FOLD" in text, text)

        stale = work / "folds-stale.txt"
        stale.write_text(folds_ok.read_text(encoding="utf-8") + f"{planted_new}:planted_never_folded  — stale on purpose.\n",
                         encoding="utf-8")
        status, j, text = run(drops_ok, stale)
        check("a fold row naming no fold is stale", status == 2 and "STALE FOLD" in text, text)

        prose = work / "folds-prose.txt"
        prose.write_text(f"{planted_new}:planted_folded  — \n{planted_new}:planted_var_folded  — planted.\n", encoding="utf-8")
        status, j, text = run(drops_ok, prose)
        check("a sentence-less fold is rejected", status != 0 and "MALFORMED FOLD" in text, text)

        uncarried = variant("drops-uncarried.txt", [f"{carried_row}  — the type's own port is not this header's to carry."],
                            carried_row + " ")
        status, j, text = run(uncarried)
        check("a forward-declaration row with no carrier is refused and the header's own trait is not",
              status == 2 and "UNCARRIED DEFERRAL  crucible/_PlantedFwd.h  planted_fwd_carried" in text
              and "planted_trait" not in text, text)

        aligned = variant("drops-aligned.txt", ["crucible/_PlantedFwd.h:planted_fwd_aligned  — no carrier on purpose."],
                          "crucible/_PlantedFwd.h:planted_fwd_aligned")
        status, j, text = run(aligned)
        check("an alignas forward declaration also needs a carrier", status == 2 and "planted_fwd_aligned" in text, text)

        status, j, text = run(variant("drops-nocarrier.txt", [f"{carried_row}  → {work}/new/foundation/NoSuchCarrier.h  — missing on purpose."],
                                      carried_row + " "))
        check("a carrier naming no existing header is refused", status == 2 and "MISSING CARRIER" in text, text)

        status, j, text = run(variant("drops-oldcarrier.txt", [f"{carried_row}  → {work}/old/crucible/_PlantedFwd.h  — old tree on purpose."],
                                      carried_row + " "))
        check("a carrier outside the new roots is refused", status == 2 and "MISSING CARRIER" in text, text)

        status, j, text = run(drops_ok, folds_ok, (carried_row,))
        check("one pinned name swapped for another reports both halves",
              status == 2 and "UNPINNED UNCARRIED" in text and "STALE PIN" in text, text)

        status, j, text = run(drops_ok, folds_ok, ())
        check("with nothing pinned, a `→ none` row is refused", status == 2 and "UNPINNED UNCARRIED" in text, text)

    if failures:
        print(f"check-port-completeness --self-test: FAILED, {len(failures)} arm(s) did not hold")
        return 2
    print("check-port-completeness --self-test: every arm holds")
    return 0


def main(argv: list[str]) -> int:
    """Run the scan, an emit mode or the self-test, as the arguments ask.

    Args:
        argv: The command-line arguments after the program name

    Returns:
        The process exit status
    """
    modes = {"": "scan", "--quiet": "scan", "--emit-drops": "emit", "--emit-folds": "emit_folds", "--self-test": "self"}
    if len(argv) > 1 or (argv and argv[0] not in modes):
        print("usage: check-port-completeness.py [--quiet | --emit-drops | --emit-folds | --self-test]", file=sys.stderr)
        return 2
    mode = modes[argv[0] if argv else ""]
    try:
        if mode == "self":
            return self_test()
        cfg = config_from_env()
        m = measure(cfg)
        j = judge(cfg, m)
    except tsast.KitMissing as exc:
        print(f"check-port-completeness: {exc}", file=sys.stderr)
        return 3
    except GuardError as exc:
        print(str(exc), file=sys.stderr)
        return 2
    if mode == "emit":
        emit_drops(m, j)
        return 0
    if mode == "emit_folds":
        emit_folds(j)
        return 0
    return report(cfg, m, j, quiet=bool(argv))


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
