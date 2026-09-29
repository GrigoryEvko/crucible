#!/usr/bin/env python3
"""check-trait-injection — a substrate trait or a fail-closed relation is specialized only where it is declared.

C++ has no orphan rule.  Any translation unit may specialize any template for
any type, including types it does not own.  Every fail-closed relation in the
tree is only as strong as the discipline that keeps its specializations beside
the declarations they name, and that discipline needs a scanner.

FAMILY A: SUBSTRATE TRAITS
    is_graded_specialization, value_type_decoupled, graded_modality and
    is_numerical_tier_impl assert an algebraic property of a Graded wrapper.
    A specialization from a foreign file forges a property that the wrapper
    never proved.

FAMILY B: FAIL-CLOSED RELATIONS
    Each relation defaults to false, and every legal edge is one explicit
    specialization.  Forging an edge is forging authority:

      retag_policy<From, To>        a provenance or trust transition.  A
                                    forged edge launders an untrusted value
                                    into a Sanitized or Verified tag.
      machine_transition<From, To>  a state-machine edge, also reached
                                    through CRUCIBLE_ALLOW_MACHINE_TRANSITION.

    can_split_into and can_split_into_pack have their own guard,
    utils/scripts/check-splits-orphan.py.

FAMILY C: FAIL-CLOSED NAMESPACES
    The two layers, include/foundation and include/fixy, state a relation as a
    namespace of foundation::fail_closed::edge<From, To> variables
    (foundation/diag/FailClosed.h).
    A namespace is open by definition, so the guard refuses each definition
    of a namespace with one of these names outside its authoring set:
    admitted_retags (fixy/Tagged.h), admitted_policies (fixy/Secret.h),
    admitted_transitions (fixy/Machine.h), admitted_implications
    (fixy/Refined.h) and permission_rows (foundation/permissions/Permission.h
    and ReadView.h).  A tag that another header declares states its row with
    a permission_row member in its own definition, so no other file needs the
    namespace.  A namespace alias adds no member and does not count.

FAMILY D: THE ORPHAN RULE OF THE TWO LAYERS
    A concept of the two layers reads a class template or a variable template:
    IsExecCtx reads is_exec_ctx_v, Subrow reads is_subrow, IsGraded reads
    Graded.  An explicit or partial specialization of one of them, from any
    file, changes what the concept admits.  `template <> inline constexpr
    bool is_exec_ctx_v<Fake> = true;` makes a plain struct a context that
    owns every effect.  An explicit specialization of one member, such as
    `template <> constexpr bool X<Fake>::value = true;`, is the same forgery.
    A layer template is a class template or a variable template that a
    header of the two layers defines.  A specialization of a layer template
    is admitted in three places only:
      * the file that defines its primary template, with a body or an
        initializer.  A forward declaration gives a file no ownership,
        because any file can declare a template again.
      * test/, where a fixture forges on purpose to prove that a gate
        refuses the forgery.  A file under test/fuzz/ is not admitted: a
        fuzz harness drives production code with production types, and
        it has no reason to forge.
      * the authoring set of an extension point in EXTENSION_POINTS, a
        template that other files specialize by design, each with its
        reason.  An extension point that no site needs is stale.
    Name lookup of utils/scripts/tsast.py (NameIndex) finds the template that a
    spelling names.  A spelling that names a template outside the two layers,
    with the same last name, is not a site.  A spelling that lookup cannot
    resolve counts for each layer template of its last name, so an unknown
    case is refused.  An explicit specialization of a member whose qualifier
    holds no template-id, `using XF = X<Fake>;` and then
    `template <> const bool XF::value = true;`, names its class template
    through an alias that the guard does not follow, so it is refused
    everywhere outside test/.  An explicit specialization of a function
    template is the function-specialization rule of
    utils/scripts/check-proof-routes.py, which refuses each one in the tree.

WHAT READS THE SITES
    The parse tree of the pinned tree-sitter kit (utils/scripts/tsast.py), over each
    C++ file that git tracks.  An untracked file is out of scope: the export
    and the build of a guard run, or the scratch file of another tool, can
    appear under the tree while the guard runs.  A name is compared as the
    lexer spells it, after the line splices of phase 2.  A specialization is
    a template declaration whose class names a template-id of the relation,
    written plain or qualified (`struct ::fixy::retag_policy<...>`).
    A reopening is a namespace definition whose last name is the relation
    namespace.  A comment, a string, a raw string and a prose ledger hold no
    node.  A macro body is parsed on its own (tsast.macro_bodies), with every
    fragment joined, so a block comment inside it does not split a head.  A
    macro body has no scope until it expands, so a specialization in a body
    counts for each layer template of its last name.  A body that the
    parser cannot read is read from its preprocessing tokens: a `struct` or
    `class` head of a relation with `<` after it, a `namespace` head of a
    relation namespace with `{` after it, the machine macro with `(` after
    it, and `template` with the name of a layer template and `<` after
    it.  The files of tsast.UNPARSEABLE are not C++ and are out of scope.
    Any other file that the parser cannot read fails.

WHAT IT CANNOT SEE
    A name that a macro of another file forms, such as `#define R retag_policy`
    and then `template <> struct R<...>`.  The file that uses the macro does
    not spell the relation, and the kit does not expand macros.  A macro
    that the owner of a template defines and that specializes it for its
    arguments, such as CRUCIBLE_DIAG_INSIGHTS, lets each file that invokes it
    specialize the template.  Such a macro is an extension point by design.

AUTHORING SETS ARE PER RELATION
    A single shared set would admit a forged is_graded_specialization from
    each directory that may declare machine_transition.  Each relation
    carries its own globs in RELATIONS below, and `*` spans `/`.  Widening a
    set is a one-line edit that a reviewer sees.  test/ may declare Family B
    and C edges, because the negative-compile fixtures and the sentinels are
    the witnesses that the relations stay fail-closed.  No authoring set
    admits a file under test/fuzz/.

Usage
    check-trait-injection.py              scan the tree
    check-trait-injection.py --self-test  plant each forgery and each exemption

Exit 0 clean, 1 on a forged specialization or a file the parser cannot read,
2 on a stale extension point, a bad invocation or a failed self-test, 3 when
the kit is not installed.
"""

from __future__ import annotations

import contextlib
import fnmatch
import io
import os
import subprocess
import sys
import tempfile
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import throwaway_repo  # noqa: E402  (the path insert above has to come first)
import tsast  # noqa: E402

LAYER_ROOTS = ("include/foundation/", "include/fixy/")
TEST_TREE = "test/"
# The directory under test/ that takes no exemption of test/: the fuzz harnesses.
FUZZ_TREE = "test/fuzz/"
SUBSTRATE_PATHS = ("include/foundation/algebra/*", "include/fixy/*",
                   "test/fixy/test_cheat_probe.cpp",
                   "test/fixy/neg/neg_cheat_graded_modality_injection.cpp")
MACHINE_MACRO = "CRUCIBLE_ALLOW_MACHINE_TRANSITION"


@dataclass(frozen=True)
class Relation:
    """One scanned relation: what forges it and where it may be authored."""

    label: str
    kind: str
    names: tuple[str, ...]
    globs: tuple[str, ...]


RELATIONS = (
    Relation("substrate", "specialization",
             ("is_graded_specialization", "value_type_decoupled", "graded_modality", "is_numerical_tier_impl"),
             SUBSTRATE_PATHS),
    Relation("retag_policy", "specialization", ("retag_policy",), ("test/*",)),
    Relation("machine_transition", "specialization", ("machine_transition",), ("include/fixy/Machine.h", "test/*")),
    Relation("admitted_retags", "reopening", ("admitted_retags",), ("include/fixy/Tagged.h", "test/*")),
    Relation("admitted_policies", "reopening", ("admitted_policies",), ("include/fixy/Secret.h", "test/*")),
    Relation("admitted_transitions", "reopening", ("admitted_transitions",), ("include/fixy/Machine.h", "test/*")),
    Relation("admitted_implications", "reopening", ("admitted_implications",), ("include/fixy/Refined.h", "test/*")),
    Relation("permission_rows", "reopening", ("permission_rows",),
             ("include/foundation/permissions/Permission.h", "include/foundation/permissions/ReadView.h", "test/*")),
)
MACRO_OF = {MACHINE_MACRO: "machine_transition"}
NAMES = {name: relation for relation in RELATIONS for name in relation.names}
BY_LABEL = {relation.label: relation for relation in RELATIONS}

QualifiedName = tuple[str, ...]

# Each layer template that other files specialize by design, with the
# files that may do it and the reason.  `*` spans `/`.  An entry that no
# site needs is stale, and the check fails until it is removed.
IN_THE_LAYERS = ("include/foundation/*", "include/fixy/*")
SPLIT_REASON = ("utils/scripts/check-splits-orphan.py admits each split manifest only beside the tags it names, a "
                "stronger rule than an authoring set")
EXTENSION_POINTS: dict[QualifiedName, tuple[tuple[str, ...], str]] = {
    ("foundation", "contracts", "armed_cell"): (
        IN_THE_LAYERS, "a cell holds the witnesses that a gate predicate accepts and refuses, beside the predicate.  "
                       "It adds a check and grants nothing"),
    ("foundation", "contracts", "armed_instances"): (
        IN_THE_LAYERS, "the instances of a templated gate that the armed roster walks, beside the gate"),
    ("foundation", "diag", "row_hash_contribution"): (
        IN_THE_LAYERS, "a carrier states its row identity beside its definition.  A fold outside the two layers "
                       "could make two carriers share one cache key"),
    ("foundation", "diag", "lattice_canonical_id"): (
        IN_THE_LAYERS, "a lattice states its canonical identity beside its definition, for the row hash"),
    ("foundation", "diag", "insight_provider"): (
        IN_THE_LAYERS, "the diagnostic text of a category.  It changes a message and grants nothing"),
    ("fixy", "concurrent", "is_stage_inline_safe"): (
        ("include/*", "src/*", "bench/*"), "a stage author claims inline dispatch beside the stage.  Pipeline.h "
                                           "refuses the claim unless the stage states its working set"),
    ("foundation", "permissions", "can_split_into"): (("*",), SPLIT_REASON),
    ("foundation", "permissions", "can_split_into_pack"): (("*",), SPLIT_REASON),
    ("foundation", "permissions", "has_split_authoring_witness"): (("*",), SPLIT_REASON),
    ("foundation", "permissions", "has_split_pack_authoring_witness"): (("*",), SPLIT_REASON),
}


@dataclass(frozen=True)
class Site:
    """One forged specialization or reopening.

    label is a relation label of RELATIONS, or the qualified name of a
    layer template for a site of the orphan rule.
    """

    label: str
    path: str
    line: int
    text: str


def listed_files(root: Path) -> list[str]:
    """Return the tracked C++ files under the root, relative to the root, in sorted order."""
    return [path for path in tsast.tracked_files(root) if tsast.is_in_cpp_scope(path) and (root / path).is_file()]


def authored(relation: Relation, path: str) -> bool:
    """Report whether a path lies in the authoring set of a relation.  A path under FUZZ_TREE never does."""
    return not path.startswith(FUZZ_TREE) and any(fnmatch.fnmatchcase(path, glob) for glob in relation.globs)


def in_test_tree(path: str) -> bool:
    """Report whether a path takes the exemptions of test/: a path under TEST_TREE and outside FUZZ_TREE."""
    return path.startswith(TEST_TREE) and not path.startswith(FUZZ_TREE)


def template_name(node: tsast.Node) -> str | None:
    """Return the template name that a class head names as a template-id, plain or qualified, or None."""
    while node.type == "qualified_identifier":
        inner = node.child_by_field("name")
        if inner is None:
            return None
        node = inner
    if node.type != "template_type":
        return None
    named = node.child_by_field("name")
    return tsast.spelled(named) if named is not None else None


def root_sites(root: tsast.Node, path: str, line_of: Callable[[tsast.Node], int]) -> list[Site]:
    """Return the sites under the root of a file or of a macro body.

    Args:
        root: The root node
        path: The repo-relative path of the file that holds the root
        line_of: The one-based file line of a node under the root
    """
    sites: list[Site] = []
    for node in root.descendants("template_declaration"):
        for head in node.children_of_type("class_specifier", "struct_specifier", "union_specifier"):
            named = head.child_by_field("name")
            name = template_name(named) if named is not None else None
            relation = NAMES.get(name or "")
            if relation is not None and relation.kind == "specialization":
                sites.append(Site(relation.label, path, line_of(node), tsast.excerpt(node)))
    for node in root.descendants("namespace_definition"):
        named = node.child_by_field("name")
        if named is None:
            continue
        parts = [tsast.spelled(part) for part in named.descendants("namespace_identifier")] \
            if named.type == "nested_namespace_specifier" else [tsast.spelled(named)]
        relation = NAMES.get(parts[-1]) if parts else None
        if relation is not None and relation.kind == "reopening":
            sites.append(Site(relation.label, path, line_of(node), tsast.excerpt(node)))
    for node in root.descendants("macro_invocation", "call_expression"):
        callee = node.child_by_field("name") or node.child_by_field("function")
        name = tsast.spelled(callee) if callee is not None else ""
        if name in MACRO_OF:
            sites.append(Site(MACRO_OF[name], path, line_of(node), tsast.excerpt(node)))
    return sites


def head_name(tokens: list[tsast.Token], index: int) -> tuple[str, str]:
    """Return the last name of the class or namespace head that starts at a token, and the token after it.

    Attribute groups `[[...]]` and `alignas(...)` before the name are skipped,
    and the name may be qualified.
    """
    count = len(tokens)
    while index < count and tokens[index].text in ("[", "alignas"):
        closer = "]" if tokens[index].text == "[" else ")"
        depth = 0
        while index < count:
            depth += tokens[index].text in ("[", "(")
            depth -= tokens[index].text in ("]", ")")
            index += 1
            if depth == 0 and tokens[index - 1].text == closer:
                break
    name = ""
    while index < count and (tokens[index].text == "::" or tokens[index].kind == "identifier"):
        if tokens[index].kind == "identifier":
            name = tokens[index].text
        index += 1
    return name, tokens[index].text if index < count else ""


def token_sites(body: tsast.MacroBody, path: str) -> list[Site]:
    """Return the sites of a macro body that did not parse, read from its preprocessing tokens."""
    tokens = tsast.pp_tokens(body.text, body.first_row)
    sites: list[Site] = []
    for index, token in enumerate(tokens):
        after = tokens[index + 1].text if index + 1 < len(tokens) else ""
        label = None
        if token.text in MACRO_OF and after == "(":
            label = MACRO_OF[token.text]
        elif token.text in ("struct", "class", "namespace"):
            name, follower = head_name(tokens, index + 1)
            relation = NAMES.get(name)
            kind, opener = ("reopening", "{") if token.text == "namespace" else ("specialization", "<")
            if relation is not None and relation.kind == kind and follower == opener:
                label = relation.label
        if label is not None:
            sites.append(Site(label, path, token.row + 1, body.define.tree.line(token.row).strip()))
    return sites


@dataclass
class Orphans:
    """The facts of the orphan rule over one scan.

    A layer template is owned by each file of the two layers that defines
    it.  A template that the two layers declare and never define, such as a
    registry whose specializations are its only definitions, is owned by
    each file of the two layers that declares its primary.

    Attributes:
        extension_points: The table of extension points that this scan reads
        index: The declarations of include/, which name lookup reads
        defined: The files of the two layers that define each template, by qualified name
        declared: The files of the two layers that declare each primary, by qualified name
        by_last_name: The layer templates of each last name
        used_extensions: The extension points that admitted a site
    """

    extension_points: dict[QualifiedName, tuple[tuple[str, ...], str]] = field(default_factory=dict)
    index: tsast.NameIndex = field(default_factory=tsast.NameIndex)
    defined: dict[QualifiedName, set[str]] = field(default_factory=dict)
    declared: dict[QualifiedName, set[str]] = field(default_factory=dict)
    by_last_name: dict[str, set[QualifiedName]] = field(default_factory=dict)
    used_extensions: set[QualifiedName] = field(default_factory=set)

    def learn(self, tree: tsast.Tree, rel: str) -> None:
        """Add the declarations of one header, and the layer templates it declares."""
        if rel.startswith("include/"):
            self.index.add(tree, share_aliases=True)
        if rel.startswith(LAYER_ROOTS):
            for primary in tsast.template_primaries(tree.root):
                qualified = tsast.scope_levels(primary.template)[0] + (primary.name,)
                self.declared.setdefault(qualified, set()).add(rel)
                if primary.is_definition:
                    self.defined.setdefault(qualified, set()).add(rel)
                self.by_last_name.setdefault(primary.name, set()).add(qualified)

    def owners(self, qualified: QualifiedName) -> set[str]:
        """Return the files that own one layer template."""
        return self.defined.get(qualified) or self.declared.get(qualified, set())

    def is_admitted(self, qualified: QualifiedName, rel: str) -> bool:
        """Report whether a file may specialize a layer template, and record the extension point it uses."""
        if rel in self.owners(qualified) or in_test_tree(rel):
            return True
        point = self.extension_points.get(qualified)
        if point is not None and any(fnmatch.fnmatchcase(rel, glob) for glob in point[0]):
            self.used_extensions.add(qualified)
            return True
        return False

    def referents(self, site: tsast.LookupSite, local: frozenset[QualifiedName]) -> set[QualifiedName]:
        """Return the layer templates that one spelled name can name.

        A name that lookup cannot resolve counts for each layer template of
        its last name, so an unknown case is refused.  A name that resolves
        to a template outside the two layers is no layer template.
        """
        candidates = self.by_last_name.get(site.parts[-1], set())
        if not candidates:
            return set()
        found, _known = self.index.resolve(site, local)
        if not found:
            return set(candidates)
        return {qualified for qualified in found if qualified in self.declared}

    def refused(self, qualified_names: set[QualifiedName], rel: str, line: int, text: str) -> list[Site]:
        """Return one site for each template of a specialization that the file may not specialize."""
        return [Site("::".join(qualified), rel, line, text) for qualified in sorted(qualified_names)
                if not self.is_admitted(qualified, rel)]

    def through_alias(self, name: tsast.SpecializedName, rel: str, line: int) -> list[Site]:
        """Return a site for a member specialization whose class an alias names, outside the exemptions of test/.

        The guard does not follow a type alias, so it cannot tell which class
        template the qualifier names, and the unknown case is refused.
        """
        if in_test_tree(rel):
            return []
        label = "::".join(name.target) + " (a class template specialization named through an alias)"
        return [Site(label, rel, line, tsast.excerpt(name.template))]

    def file_sites(self, tree: tsast.Tree, rel: str) -> list[Site]:
        """Return each specialization of one file that the orphan rule refuses."""
        local = tsast.NameIndex()
        local.add(tree)
        aliases, usings = tsast.namespace_aliases(tree), tsast.using_names(tree)
        found: list[Site] = []
        for name in tsast.specialized_names(tree.root):
            if name.through_alias:
                found += self.through_alias(name, rel, name.template.line)
                continue
            if name.kind == "function" or name.target[-1] not in self.by_last_name:
                continue
            site = tsast.lookup_site_of_parts(name.template, name.is_global, name.target, aliases, usings)
            found += self.refused(self.referents(site, frozenset(local.names)), rel, name.template.line,
                                  tsast.excerpt(name.template))
        return found

    def body_sites(self, body: tsast.MacroBody, rel: str, aliases: list[tsast.NamespaceAlias]) -> list[Site]:
        """Return each specialization of one parsed macro body that the orphan rule refuses.

        A body has no scope until it expands.  Lookup starts in the
        namespaces that enclose the #define, with the namespaces of the body
        inside them, because a macro is expanded where its owner meant it.
        """
        outer = tsast.namespace_path(body.define, skip_inline=True)
        found: list[Site] = []
        for name in tsast.specialized_names(body.root):
            if name.through_alias:
                found += self.through_alias(name, rel, body.origin(name.template)[0] + 1)
                continue
            if name.kind == "function" or name.target[-1] not in self.by_last_name:
                continue
            full = outer + tsast.namespace_path(name.template, skip_inline=True)
            levels = tuple(full[:count] for count in range(len(full), -1, -1))
            parts = tsast.resolve_namespace(name.target, body.define, aliases, is_global=name.is_global)
            site = tsast.LookupSite(levels, parts, name.is_global, ())
            found += self.refused(self.referents(site, frozenset()), rel, body.origin(name.template)[0] + 1,
                                  tsast.excerpt(name.template))
        return found

    def token_sites(self, body: tsast.MacroBody, rel: str) -> list[Site]:
        """Return each specialization of a macro body that did not parse, read from its preprocessing tokens.

        After `template` and its closing `>`, a class head of a layer template
        name with `<` after it is a site.  So is a layer template name with `<`
        after it whose argument list ends in `=`, `;` or `{`, which is a
        variable.  A list that ends in `(` is a function, the rule of
        check-proof-routes.py.  The scope of such a body is unknown, so each
        site counts for every layer template of its name.
        """
        tokens = tsast.pp_tokens(body.text, body.first_row)
        found: list[Site] = []
        for start, token in enumerate(tokens):
            if token.text != "template" or start + 1 >= len(tokens) or tokens[start + 1].text != "<":
                continue
            cursor = closing_angle(tokens, start + 1)
            named = None
            while named is None and cursor < len(tokens) and tokens[cursor].text not in (";", "{", "="):
                if tokens[cursor].text in ("struct", "class", "union"):
                    name, follower = head_name(tokens, cursor + 1)
                    named = (name, cursor) if follower == "<" else ("", cursor)
                elif (tokens[cursor].kind == "identifier" and cursor + 1 < len(tokens)
                      and tokens[cursor + 1].text == "<"):
                    after = closing_angle(tokens, cursor + 1)
                    is_variable = after >= len(tokens) or tokens[after].text in ("=", ";", "{")
                    named = (tokens[cursor].text if is_variable else "", cursor)
                cursor += 1
            if named is not None and named[0] in self.by_last_name:
                row = tokens[named[1]].row
                found += self.refused(set(self.by_last_name[named[0]]), rel, row + 1,
                                      body.define.tree.line(row).strip())
        return found


def closing_angle(tokens: list[tsast.Token], opener: int) -> int:
    """Return the index after the `>` that closes the `<` at a token index, or the end of the list.

    `>>` closes two levels.  A `(` group is skipped, so a `>` inside it
    closes nothing.
    """
    depth = 0
    parens = 0
    for index in range(opener, len(tokens)):
        text = tokens[index].text
        parens += (text == "(") - (text == ")")
        if parens:
            continue
        depth += text.count("<") if text in ("<", "<<") else 0
        depth -= text.count(">") if text in (">", ">>") else 0
        if depth <= 0:
            return index + 1
    return len(tokens)


def scan(root: Path, extension_points: dict[QualifiedName, tuple[tuple[str, ...], str]] | None = None,
         ) -> tuple[list[Site], list[str], list[str]]:
    """Return each site outside its authoring set, each file the parser cannot read, and each stale extension point.

    Complexity: linear in the size of the tracked C++ files, and in the
    number of enclosing scopes of each specialization for its lookup.

    Args:
        root: The root of the tree
        extension_points: The extension points, or None for EXTENSION_POINTS

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    points = EXTENSION_POINTS if extension_points is None else extension_points
    files = listed_files(root)
    sites: list[Site] = []
    unread: list[str] = []
    trees: list[tuple[str, tsast.Tree]] = []
    orphans = Orphans(points)
    for tree in tsast.parse([root / path for path in files], strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            unread.append(f"trait_guard: {rel} does not parse, so the relations it may specialize are unknown."
                          f"\n  {tree.diagnostic}")
            continue
        trees.append((rel, tree))
        orphans.learn(tree, rel)
        sites += root_sites(tree.root, rel, lambda node: node.line)
    aliases_of: dict[str, list[tsast.NamespaceAlias]] = {}
    for body in tsast.macro_bodies(tree for _rel, tree in trees):
        rel = Path(body.define.tree.path).relative_to(root).as_posix()
        if body.is_parsed:
            sites += root_sites(body.root, rel, lambda node, body=body: body.origin(node)[0] + 1)
            if rel not in aliases_of:
                aliases_of[rel] = tsast.namespace_aliases(body.define.tree)
            sites += orphans.body_sites(body, rel, aliases_of[rel])
        else:
            sites += token_sites(body, rel)
            sites += orphans.token_sites(body, rel)
    for rel, tree in trees:
        sites += orphans.file_sites(tree, rel)
    forged = sorted({site for site in sites if site.label not in BY_LABEL or not authored(BY_LABEL[site.label], site.path)},
                    key=lambda site: (site.path, site.line, site.label))
    stale = [f"trait_guard: the extension point {'::'.join(point)} admits no specialization in the tree.  Remove "
             f"its row from EXTENSION_POINTS in utils/scripts/check-trait-injection.py." for point in points
             if point not in orphans.used_extensions]
    return forged, unread, stale


def run(root: Path, extension_points: dict[QualifiedName, tuple[tuple[str, ...], str]] | None = None) -> int:
    """Scan, print each finding, and return the exit code."""
    forged, unread, stale = scan(root, extension_points)
    for site in forged:
        relation = BY_LABEL.get(site.label)
        print(f"trait_guard[{site.label}]: forbidden specialization at {site.path}:{site.line}", file=sys.stderr)
        print(f"trait_guard[{site.label}]: {site.text}", file=sys.stderr)
        if relation is not None:
            print(f"trait_guard[{site.label}]: authoring set is: {' '.join(relation.globs)}", file=sys.stderr)
        else:
            print(f"trait_guard[{site.label}]: a template of include/foundation or include/fixy is specialized "
                  f"only in the file that defines it, in test/ outside {FUZZ_TREE}, or at an extension point of "
                  f"EXTENSION_POINTS", file=sys.stderr)
    for line in unread + stale:
        print(line, file=sys.stderr)
    if forged or unread:
        print("trait_guard: each scanned relation is specialized only inside its own authoring set.  C++ has no "
              "orphan rule, so a specialization outside the file that declares the named types forges a property "
              "or an authority that was never granted.  If the new place is legitimate, widen that relation's row, "
              "or add an extension point, in utils/scripts/check-trait-injection.py so the widening is reviewed.",
              file=sys.stderr)
        return 1
    if stale:
        return 2
    print(f"check-trait-injection: clean — {len(RELATIONS)} relations and the orphan rule of include/foundation "
          f"and include/fixy, each specialization inside its authoring set.", file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each forgery under a path outside its set and each edge inside its set, and examine each verdict.

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

    planted = {
        "src/planted/trait.cpp": (
            "namespace crucible {\n"
            "template <>\n"
            "struct is_graded_specialization<planted::Probe<int>> { static constexpr bool is_specialized = true; };\n"
            "}\n"),
        "src/planted/retag.cpp": (
            "namespace fixy {\n"
            "template <>\n"
            "struct retag_policy<source::FromDb, trust::Verified> { static constexpr bool allowed = true; };\n"
            "}\n"
            "// struct retag_policy<source::FromUser, trust::Verified> is prose.\n"
            "/* template <> struct retag_policy<A, B> {}; in a block comment\n"
            "   template <> struct retag_policy<C, D> {}; */\n"
            "const char* text = R\"(template <> struct retag_policy<E, F> {};)\";\n"),
        "src/planted/qualified.cpp": (
            "template <>\n"
            "struct ::fixy::retag_policy<source::FromDb, trust::Verified> : std::true_type {};\n"
            "template <>\n"
            "struct [[deprecated]] fixy::machine_transition<A, B> : std::true_type {};\n"),
        "src/planted/machine.cpp": (
            "CRUCIBLE_ALLOW_MACHINE_TRANSITION(Authenticated, Disconnected)\n"
            "namespace fixy {\n"
            "template <>\n"
            "struct machine_transition<Authenticated, Disconnected> : std::true_type {};\n"
            "}\n"),
        "src/planted/partial.cpp": (
            "namespace fixy {\n"
            "template <class T>\n"
            "struct retag_policy<T, PlantedNarrow> : std::true_type {};\n"
            "}\n"),
        "src/planted/retags.cpp": (
            "namespace fixy::tags::admitted_retags {\n"
            "inline constexpr ::foundation::fail_closed::edge<source::Sanitized, source::External> planted{};\n"
            "}\n"
            "namespace fixy::tags {\n"
            "namespace admitted_retags {\n"
            "}\n"
            "}\n"
            "namespace planted_alias = fixy::tags::admitted_retags;\n"),
        "src/planted/policies.cpp": "namespace fixy::tags::secret_policy::admitted_policies {\n}\n",
        "src/planted/transitions.cpp": "namespace fixy::machine::admitted_transitions {\n}\n",
        "src/planted/implications.cpp": "namespace fixy::refined::admitted_implications {\n}\n",
        "src/planted/macro.cpp": (
            "#define FORGE(T) template <> struct retag_policy<T, trust::Verified> : std::true_type {}\n"
            "#define MENTION retag_policy is named here, and nothing is specialized\n"),
        "src/planted/split.cpp": (
            "#define FORGE2(T) template <> /* why */ \\\n"
            "  struct retag_policy<T, trust::Verified> : std::true_type {};\n"),
        "src/planted/spliced.cpp": (
            "namespace fixy {\n"
            "template <>\n"
            "struct retag_\\\n"
            "policy<A, B> : std::true_type {};\n"
            "}\n"),
        "src/planted/rows.cpp": "namespace foundation::permissions::permission_rows {\n}\n",
        "src/planted/orphan_variable.cpp": (
            "template <> inline constexpr bool foundation::effects::is_exec_ctx_v<Fake> = true;\n"
            "namespace foundation::effects {\n"
            "template <class T> inline constexpr bool is_exec_ctx_v<T*> = true;\n"
            "}\n"
            "namespace fe = ::foundation::effects;\n"
            "template <> inline constexpr bool fe::is_exec_ctx_v<Other> = true;\n"
            "template <> inline constexpr bool mystery::is_exec_ctx_v<Unknown> = true;\n"),
        "src/planted/orphan_class.cpp": (
            "template <> class foundation::effects::ExecCtx<Bg, OddRow> { public: ExecCtx() = default; };\n"
            "template <> class foundation::algebra::Graded<0, Lattice, Payload> { public: Payload anything; };\n"
            "template <> constexpr bool foundation::effects::is_subrow<Wide, Narrow>::value = true;\n"
            "namespace foundation::effects { template <class T> extern const bool is_exec_ctx_v; }\n"
            "template <> const bool foundation::effects::is_exec_ctx_v<Forward> = true;\n"
            "template <> struct foundation::contracts::armed_cell<42> {};\n"
            "using FakeGate = foundation::effects::is_subrow<Wide, Narrow>;\n"
            "template <> const bool FakeGate::value = true;\n"),
        "src/planted/orphan_macro.cpp": (
            "#define FORGE_CTX(T) template <> inline constexpr bool foundation::effects::is_exec_ctx_v<T> = true\n"
            "#define FORGE_TOKENS(T) template <> inline constexpr bool is_exec_ctx_v<T> = (#T[0] != 0);\n"
            "#define USE_TOKENS(T) static_assert(is_exec_ctx_v<T>, #T)\n"
            "#define USE_PARSED(T) static_assert(foundation::effects::is_exec_ctx_v<T>)\n"),
        "src/planted/outside_layers.cpp": (
            "namespace crucible::planted {\n"
            "template <class T> inline constexpr bool is_exec_ctx_v = false;\n"
            "template <> inline constexpr bool is_exec_ctx_v<Fake> = true;\n"
            "}\n"
            "template <> void foundation::permissions::mint_permission_root<IoRegion>() {}\n"),
        # A fuzz harness takes no exemption of test/.
        "test/fuzz/planted_forge.cpp": (
            "namespace fixy { template <> struct retag_policy<X, Y> {}; }\n"
            "template <> inline constexpr bool foundation::effects::is_exec_ctx_v<Fake> = true;\n"
            "using FakeGate = foundation::effects::is_subrow<Wide, Narrow>;\n"
            "template <> const bool FakeGate::value = true;\n"),
    }
    expected = {
        ("substrate", "src/planted/trait.cpp", 2),
        ("retag_policy", "src/planted/retag.cpp", 2),
        ("retag_policy", "src/planted/qualified.cpp", 1),
        ("machine_transition", "src/planted/qualified.cpp", 3),
        ("machine_transition", "src/planted/machine.cpp", 1),
        ("machine_transition", "src/planted/machine.cpp", 3),
        ("retag_policy", "src/planted/partial.cpp", 2),
        ("admitted_retags", "src/planted/retags.cpp", 1),
        ("admitted_retags", "src/planted/retags.cpp", 5),
        ("admitted_policies", "src/planted/policies.cpp", 1),
        ("admitted_transitions", "src/planted/transitions.cpp", 1),
        ("admitted_implications", "src/planted/implications.cpp", 1),
        ("retag_policy", "src/planted/macro.cpp", 1),
        ("retag_policy", "src/planted/split.cpp", 1),
        ("retag_policy", "src/planted/spliced.cpp", 2),
        ("permission_rows", "src/planted/rows.cpp", 1),
        ("foundation::effects::is_exec_ctx_v", "src/planted/orphan_variable.cpp", 1),
        ("foundation::effects::is_exec_ctx_v", "src/planted/orphan_variable.cpp", 3),
        ("foundation::effects::is_exec_ctx_v", "src/planted/orphan_variable.cpp", 6),
        ("foundation::effects::is_exec_ctx_v", "src/planted/orphan_variable.cpp", 7),
        ("foundation::effects::ExecCtx", "src/planted/orphan_class.cpp", 1),
        ("foundation::algebra::Graded", "src/planted/orphan_class.cpp", 2),
        ("foundation::effects::is_subrow", "src/planted/orphan_class.cpp", 3),
        ("foundation::effects::is_exec_ctx_v", "src/planted/orphan_class.cpp", 5),
        ("foundation::contracts::armed_cell", "src/planted/orphan_class.cpp", 6),
        ("FakeGate (a class template specialization named through an alias)", "src/planted/orphan_class.cpp", 8),
        ("foundation::effects::is_exec_ctx_v", "src/planted/orphan_macro.cpp", 1),
        ("foundation::effects::is_exec_ctx_v", "src/planted/orphan_macro.cpp", 2),
        ("retag_policy", "test/fuzz/planted_forge.cpp", 1),
        ("foundation::effects::is_exec_ctx_v", "test/fuzz/planted_forge.cpp", 2),
        ("FakeGate (a class template specialization named through an alias)", "test/fuzz/planted_forge.cpp", 4),
    }
    # The layer templates of the orphan rule, each with a specialization
    # in the file that owns it, and the extension points of the planted tree.
    owners = {
        "include/foundation/effects/Ctx.h": (
            "namespace foundation::effects {\n"
            "template <class T> inline constexpr bool is_exec_ctx_v = false;\n"
            "template <> inline constexpr bool is_exec_ctx_v<int> = true;\n"
            "template <class Cap, class Row> class ExecCtx {};\n"
            "template <class R1, class R2> struct is_subrow { static constexpr bool value = false; };\n"
            "}\n"),
        "include/foundation/diag/RowHash.h": (
            "namespace foundation::effects { template <class Cap, class Row> class ExecCtx; }\n"),
        "include/foundation/algebra/Graded.h": (
            "namespace foundation::algebra { template <int M, class L, class T> class Graded {}; }\n"),
        "include/foundation/contracts/Armed.h": "namespace foundation::contracts { template <auto P> struct armed_cell; }\n",
        "include/fixy/Cells.h": "template <> struct foundation::contracts::armed_cell<7> {};\n",
        "include/foundation/permissions/Permission.h": "namespace foundation::permissions::permission_rows {\n}\n",
        "test/planted_orphan.cpp": ("template <> inline constexpr bool foundation::effects::is_exec_ctx_v<Fake> = true;\n"
                                    "using FakeGate = foundation::effects::is_subrow<Wide, Narrow>;\n"
                                    "template <> const bool FakeGate::value = true;\n"),
    }
    points = {
        ("foundation", "contracts", "armed_cell"): (("include/fixy/*",), "a cell of the planted tree"),
    }
    exempt = {
        "include/foundation/algebra/planted.h": (
            "namespace foundation::algebra { template <> struct is_graded_specialization<planted::Exempt<int>> {}; }\n"),
        "include/fixy/Tagged.h": "namespace fixy::tags::admitted_retags {\n}\n",
        "include/fixy/Secret.h": "namespace fixy::tags::secret_policy::admitted_policies {\n}\n",
        "include/fixy/Machine.h": (
            "namespace fixy::machine::admitted_transitions {\n}\n"
            "CRUCIBLE_ALLOW_MACHINE_TRANSITION(PlantedFrom, PlantedTo)\n"
            "namespace fixy::machine { template <> struct machine_transition<From, To> : std::true_type {}; }\n"),
        "include/fixy/Refined.h": "namespace fixy::refined::admitted_implications {\n}\n",
        "test/planted_test.cpp": "namespace fixy { template <> struct retag_policy<X, Y> {}; }\n",
    }
    ledger = ("fixy/Tagged.h:admitted_retags  — The prose names template <> struct retag_policy<From, To> and "
              "struct is_graded_specialization<W>, and it specializes nothing.\n")

    def captured(action) -> tuple[int, str]:
        """Run an action and return its code and its stderr."""
        buffer = io.StringIO()
        with contextlib.redirect_stderr(buffer):
            code = action()
        return code, buffer.getvalue()

    print("check-trait-injection --self-test")
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in {**planted, **exempt, **owners}.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        (root / "utils" / "scripts").mkdir(parents=True)
        (root / "utils" / "scripts" / "planted-ledger.txt").write_text(ledger, encoding="utf-8")
        (root / "build" / "Testing").mkdir(parents=True)
        (root / "build" / "Testing" / "stale.cpp").write_text(planted["src/planted/retag.cpp"], encoding="utf-8")

        forged, unread, stale = scan(root, points)
        found = {(site.label, site.path, site.line) for site in forged}
        expect("every file parses", not unread)
        for label, path, line in sorted(expected):
            expect(f"caught: {label} at {path}:{line}", (label, path, line) in found, True)
        expect("nothing else is reported", found == expected, True)
        expect("a comment, a block comment and a raw string are no specialization",
               not {site for site in forged if site.path == "src/planted/retag.cpp" and site.line > 2})
        expect("a namespace alias is no reopening", ("admitted_retags", "src/planted/retags.cpp", 8) not in found)
        expect("a macro body that only names a relation is no specialization",
               ("retag_policy", "src/planted/macro.cpp", 2) not in found)
        expect("a macro body that only reads a layer template is no specialization",
               not any(site.path == "src/planted/orphan_macro.cpp" and site.line > 2 for site in forged))
        expect("a template outside the two layers with the same last name is no site, and a function "
               "specialization is the rule of check-proof-routes.py",
               not any(site.path == "src/planted/outside_layers.cpp" for site in forged))
        for rel in [*exempt, *owners]:
            expect(f"exempt in its authoring set: {rel}", not any(site.path == rel for site in forged))
        expect("a used extension point is not stale", not stale)
        expect("a prose ledger is not scanned", not any(site.path.startswith("utils/scripts/") for site in forged))
        expect("a build tree is not scanned", not any(site.path.startswith("build/") for site in forged))
        unused = {**points, ("foundation", "diag", "unused_point"): (("*",), "no site needs it")}
        expect("an extension point that no site needs is stale",
               [line for line in scan(root, unused)[2] if "foundation::diag::unused_point" in line] != [], True)

        code, report = captured(lambda: run(root, points))
        expect("a forged tree exits 1", code == 1 and "forbidden specialization at src/planted/trait.cpp:2" in report,
               True)
        previous = Path.cwd()
        os.chdir(root / "build" / "Testing")
        try:
            from_build = captured(lambda: run(root, points))
        finally:
            os.chdir(previous)
        expect("the report from inside the build tree equals the report from the root", from_build == (code, report))

        rostered = root / next(iter(tsast.UNPARSEABLE))
        rostered.parent.mkdir(parents=True, exist_ok=True)
        rostered.write_text("void f() { g(1) { } } // retag_policy\n", encoding="utf-8")
        code, report = captured(lambda: run(root, points))
        expect("a file of the UNPARSEABLE roster is out of scope", "does not parse" not in report, True)
        (root / "src" / "planted" / "broken.cpp").write_text("void f() { g(1) { } } // retag_\\\npolicy\n",
                                                            encoding="utf-8")
        code, report = captured(lambda: run(root, points))
        expect("a file the parser cannot read fails, and a splice does not hide its relation name",
               code == 1 and "src/planted/broken.cpp does not parse" in report, True)
        for rel in [*planted, "src/planted/broken.cpp"]:
            (root / rel).unlink()
        expect("a tree with only authored edges exits 0", captured(lambda: run(root, points))[0] == 0)
        expect("a stale extension point exits 2", captured(lambda: run(root, unused))[0] == 2, True)

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        throwaway_repo.init(root)
        for rel, text in owners.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        subprocess.run(["git", "-C", str(root), "add", "-A"], check=True, capture_output=True)
        untracked = root / "grun" / "change" / "src" / "orphan_variable.cpp"
        untracked.parent.mkdir(parents=True)
        untracked.write_text(planted["src/planted/orphan_variable.cpp"], encoding="utf-8")
        expect("an untracked file, such as the export of a guard run, is out of scope",
               captured(lambda: run(root, points))[0] == 0, True)
        subprocess.run(["git", "-C", str(root), "add", "-A"], check=True, capture_output=True)
        code, report = captured(lambda: run(root, points))
        expect("the same file is refused once git tracks it",
               code == 1 and "grun/change/src/orphan_variable.cpp:1" in report, True)

    if failures:
        print(f"check-trait-injection --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-trait-injection --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode."""
    try:
        if argv == []:
            return run(tsast.REPO_ROOT)
        if argv == ["--self-test"]:
            return self_test()
    except tsast.KitMissing as exc:
        print(f"check-trait-injection: {exc}", file=sys.stderr)
        return 3
    print("usage: check-trait-injection.py [--self-test]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
