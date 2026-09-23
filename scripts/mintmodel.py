#!/usr/bin/env python3
"""Derive the §XXI mint model from the AST, for the guard and the inventory.

CLAUDE.md §XXI names every cross-tier composition factory `mint_<noun>`, and two
consumers ask the same question of that set: `check-mint-pattern.sh` asks whether
each one carries `[[nodiscard]]`, `constexpr`, `noexcept` and a `requires`
clause, and `gen-mint-inventory.sh` renders the same four flags plus the
authorization shape into `misc/mint-inventory.md`.  Both find their sites with a
broad regex over `mint_[a-z_]+\\s*\\(` and then filter out comments, friend
forwards, using-declarations, method calls and call positions by hand.  A regex
cannot tell a declaration from a call, so the filters need an allowlist for the
sites they get wrong, and a name key cannot tell two functions with one name
apart.

This module answers the question from the parse tree instead.  A site is a
`function_declarator` whose `declarator` field names an identifier that starts
with `mint_`.  Four rules then exclude what is not a live mint, and each one is a
node type rather than a heuristic:

  * a `friend_declaration` ancestor — a friend re-declares a mint defined
    elsewhere, so the canonical site is the definition
  * a `delete_method_clause` — `= delete("reason")` removes an overload
  * no `type` field on the definition — that is a constructor, which is how
    `class mint_permission_inherit_key` reaches the scan at all
  * a trailing underscore on the name — §XXI reserves that for an internal
    detail helper, not a mint

PARITY.  Measured 2026-09-21 against the 216 rows of `misc/mint-inventory.md`:
these rules reproduce every row, miss none, and add three sites the snapshot
does not hold.  All three are real: two independent
definitions that share a name, and one row whose fixy cell names a different
function than the row's own site.
"""

from __future__ import annotations

import re
import sys
from dataclasses import dataclass, replace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402  (the path insert above has to come first)

# The substrate trees the §XXI surface covers, plus the top-level headers, which
# belong to no tree, plus fixy/ for the mints that originate there.
SUBSTRATE_TREES = (
    "safety", "effects", "algebra", "concurrent", "sessions", "permissions",
    "bridges", "handles", "cipher", "warden", "perf", "cntp", "topology",
    "canopy", "observe", "cog", "mimic", "ledger",
)

# A leading underscore on a FILE name marks an old-substrate header that a port
# has superseded.  Its mints live on in the ported file, so counting both would
# double every ported mint.  A leading underscore on a DIRECTORY carries no such
# meaning — `cntp/_wip` holds live mints and stays in scope.
SUPERSEDED_PREFIX = "_"

# §XXI carve-out markers, placed in a comment above the signature.  Each states
# that an absent flag is a documented decision rather than a shortfall.
CARVE_OUT_CX = "§XXI carve-out: cx=alloc"
CARVE_OUT_RQ = "§XXI carve-out: rq=pre"

# A third exemption mechanism, read by the guard rather than the inventory: an
# inline marker on the signature line itself.  The tree carries all three at
# once — 138 allowlist entries, 39 carve-out comments and 9 inline markers, for
# 97 real axis failures.  The model reads every one, so the guard can reproduce
# the old verdict, and so a later consolidation can count what it removes.
INLINE_OK = "MINT-PATTERN-OK"

# `IsExecCtx` states that a template parameter IS a context.  That is the
# parameter's SHAPE, not its FIT, so it never counts as a context gate — a mint
# constrained only by it accepts every context in the tree.  `_shape` already
# reads it as the shape marker that identifies a ctx-bound mint.
CTX_SHAPE_CONCEPT = "IsExecCtx"


@dataclass(frozen=True, slots=True)
class Mint:
    """One live §XXI mint factory and its compliance facts.

    Attributes:
        name: The factory identifier, for example `mint_endpoint`
        path: The declaration site's path, relative to the repo root
        line: The one-based line of the identifier that names the factory
        owner: The enclosing class for a member mint, otherwise None
        inline_ok: Whether a `MINT-PATTERN-OK` marker sits on the signature line
        borrow_projection: Whether the body's only return delegates to `mint_view`
        nodiscard: Whether the site carries `[[nodiscard]]`
        constexpr: Whether the site carries `constexpr` or `consteval`
        noexcept_: Whether the declarator carries `noexcept`
        requires_: Whether a `requires` CLAUSE constrains the site
        constraint_concepts: The concepts constraining its template parameters
        ctx_token: The type name of the first parameter of a ctx-bound mint
        ctx_gated: Whether a clause names that context type
        shape: `ctx`, `token` or `member` — the authorization shape
        templated: Whether a template declaration encloses the site
        carve_out_cx: Whether a comment documents the absent `constexpr`
        carve_out_rq: Whether a comment documents the absent `requires`
    """

    name: str
    path: str
    line: int
    owner: str | None
    namespace: str
    inline_ok: bool
    borrow_projection: bool
    nodiscard: bool
    constexpr: bool
    noexcept_: bool
    requires_: bool
    constraint_concepts: frozenset[str]
    ctx_token: str | None
    ctx_gated: bool
    shape: str
    templated: bool
    carve_out_cx: bool
    carve_out_rq: bool

    @property
    def key(self) -> tuple[str, str, int]:
        """Return the site key: name, path and line.

        A mint is keyed by its site, never by its name alone.  Two functions can
        share a name, and a name key reports them as one.
        """
        return (self.name, self.path, self.line)

    @property
    def qualified(self) -> str:
        """Return `Class::name` for a member mint, or the bare name."""
        return f"{self.owner}::{self.name}" if self.owner else self.name

    @property
    def linkage_name(self) -> str:
        """Return the fully qualified name a using-declaration would name.

        A re-export names the mint through its namespace, so this is the key a
        `using` has to match.  Matching the bare name instead reports a
        re-export that names a different function as if it named this one.
        """
        return f"::{self.namespace}::{self.name}" if self.namespace else f"::{self.name}"


def _is_borrow_projection(site: tsast.Node) -> bool:
    """Report whether this mint's body does nothing but delegate to `mint_view`.

    Such a function hands out a `ScopedView` over its own carrier.  It composes
    nothing and synthesises no authority, so it is a borrow projection rather
    than a §XXI factory, and it can never be constant-evaluated: the carrier
    would have to be a constant expression, and every carrier in this tree holds
    state set by a syscall, by an allocation, or by a cross-thread store.

    `fixy/ScopedView.h` already treats `mint_view` as the borrow door — it
    carries a documented `rq=pre` carve-out, because the gate inspects runtime
    state and a concept cannot see that.  A function that delegates to it
    inherits the same character.

    Args:
        site: The declaration or definition node of the mint

    Returns:
        True when the body is a single return that calls `mint_view`
    """
    body = site.child_by_field("body")
    if body is None:
        return False
    returns = [r for r in body.descendants("return_statement")]
    if len(returns) != 1:
        return False
    called = [c for c in returns[0].descendants("template_function", "identifier")
              if c.text.split("<")[0].rsplit("::", 1)[-1] == "mint_view"]
    if not called:
        return False
    # The carrier must be the mint's OWN object.  A free function that delegates
    # to `mint_view` with a PARAMETER carrier can legitimately be a compile-time
    # factory, so the projection claim needs `*this` rather than the delegation
    # alone.  Without this, the rule also matches mint_linear_view and
    # mint_session_view, which are not projections of their own state.
    for call in returns[0].descendants("call_expression"):
        function = call.child_by_field("function")
        if function is None:
            continue
        if function.text.split("<")[0].rsplit("::", 1)[-1] != "mint_view":
            continue
        arguments = call.child_by_field("arguments")
        if arguments is not None and "this" in arguments.text:
            return True
    return False


def constexpr_applies(mint: Mint) -> bool:
    """Report whether the `constexpr` axis is meaningful for this mint.

    It is not meaningful for a borrow projection.  This is a COSMETIC axis, and
    the distinction matters: a heuristic is acceptable here because the worst
    outcome is that a dead keyword goes undemanded.  On a SOUNDNESS axis such as
    `requires` no heuristic is acceptable, because the worst outcome there is an
    unguarded gate.

    Args:
        mint: The mint to judge

    Returns:
        True when `constexpr` could carry meaning at this site
    """
    return not mint.borrow_projection


def requires_applies(mint: Mint) -> bool:
    """Report whether the `requires` axis is meaningful for this mint.

    A `requires` clause constrains a template.  A non-template function cannot
    carry one, so demanding it there would report a shortfall no edit can fix.
    The bash guard reaches the same rule through `has_requires_if_templated`,
    which walks fifteen lines up looking for the word `template` — and so misses
    a template whose parameter list sits further above behind a comment block.
    The enclosing `template_declaration` answers it exactly.

    Args:
        mint: The mint to judge

    Returns:
        True when a template encloses the site
    """
    return mint.templated


def has_fit_constraint(mint: Mint) -> bool:
    """Report whether a type-level constraint reaches this mint, in any position.

    C++ spells one constraint two ways, and they mean the same thing to the
    compiler: `template <typename T> requires C<T>` and `template <C T>`.  Reading
    only the first reported 49 of the tree's 332 mints as unconstrained when every
    one of them carries a concept on a template parameter.  `mint_bpf_map_spec`
    is the plainest case — its gate is `template <BpfScalar Key, BpfScalar Value>`,
    which says exactly what may be a BPF map key, and the old read called it
    absent.

    This axis asks only whether a constraint is PRESENT.  Whether it is the RIGHT
    constraint is a different question, and for a ctx-bound mint `ctxfit_applies`
    asks it separately — because a concept on some other parameter says nothing
    about the context.

    Args:
        mint: The mint to judge

    Returns:
        True when a clause or a constrained template parameter reaches the site
    """
    return mint.requires_ or bool(mint.constraint_concepts)


def ctxfit_applies(mint: Mint) -> bool:
    """Report whether the context-fit axis is meaningful for this mint.

    It applies to a ctx-bound mint, which §XXI defines as one threading ctx-driven
    policy through the constructed type.  A token mint derives authority from a
    parent token and holds no context to gate.  A member mint's authority is the
    object whose method it is.

    Args:
        mint: The mint to judge

    Returns:
        True when the mint takes a context first
    """
    return mint.shape == "ctx"


def scan_files() -> list[Path]:
    """Return every header the §XXI mint surface covers, sorted.

    Sorted order keeps a gate's report stable between runs.

    Returns:
        Repo-relative paths, in sorted order
    """
    found: list[Path] = []
    for tree in SUBSTRATE_TREES:
        found += tsast.cpp_files(f"include/crucible/{tree}")
    found += tsast.cpp_files("include/crucible/fixy")
    root = Path("include/crucible")
    found += [p for p in tsast.cpp_files("include/crucible") if p.parent == root]
    return sorted({p for p in found if not p.name.startswith(SUPERSEDED_PREFIX)})


def guard_files() -> list[Path]:
    """Return every header the §XXI GUARD enforces over, sorted.

    The guard and the inventory have different scopes, and conflating them was
    the source of two wrong numbers.  The guard scans all of `include/`, so it
    reaches the canonical new substrate at `include/foundation/` and
    `include/fixy/` as well as the old tree.  The inventory documents the
    substrate trees it lists, which is a narrower set.

    Superseded `_*.h` headers are excluded from both: they are frozen, so a
    shortfall there cannot be repaired, and they go with the old tree.

    Returns:
        Repo-relative paths, in sorted order
    """
    found = tsast.cpp_files("include")
    return sorted({p for p in found if not p.name.startswith(SUPERSEDED_PREFIX)})


def _has_attribute(site: tsast.Node, word: str) -> bool:
    """Report whether an attribute on this site names a word.

    Args:
        site: The declaration or definition node
        word: The attribute to look for, for example `nodiscard`

    Returns:
        True when an `attribute_declaration` child mentions the word
    """
    return any(word in child.text for child in site.children_of_type("attribute_declaration"))


def _has_qualifier(site: tsast.Node, *words: str) -> bool:
    """Report whether a specifier on this site is one of the given words.

    `constexpr` and `consteval` both parse as a `type_qualifier` child of the
    definition, so the test reads the child text rather than the node type.

    Args:
        site: The declaration or definition node
        words: The specifiers to accept

    Returns:
        True when a qualifier or storage-class child matches
    """
    want = frozenset(words)
    kinds = ("type_qualifier", "storage_class_specifier")
    return any(child.text.strip() in want for child in site.children_of_type(*kinds))


def _has_requires(site: tsast.Node, declarator: tsast.Node) -> bool:
    """Report whether a `requires` clause constrains this site.

    A clause reaches a mint from three positions: after the template parameter
    list, after the declarator as a trailing clause, or on the definition itself.
    All three are a `requires_clause` node, so the test looks in all three.

    Args:
        site: The declaration or definition node
        declarator: The function declarator of the mint

    Returns:
        True when any of the three positions carries a clause
    """
    if declarator.children_of_type("requires_clause"):
        return True
    if site.children_of_type("requires_clause"):
        return True
    owner = site.parent
    if owner is not None and owner.type == "template_declaration":
        return bool(owner.children_of_type("requires_clause"))
    return False


def _template_parameters(site: tsast.Node) -> list[tsast.Node]:
    """Return every template parameter declared above this site.

    Three node types carry a type and a name: the plain form, the defaulted form
    and the pack form.  A plain `typename T` is a `type_parameter_declaration`
    instead and carries neither, so it never appears here.

    Args:
        site: The declaration or definition node

    Returns:
        The parameter nodes, in source order
    """
    owner = site.parent
    if owner is None or owner.type != "template_declaration":
        return []
    found: list[tsast.Node] = []
    for plist in owner.children_of_type("template_parameter_list"):
        found += plist.children_of_type(
            "parameter_declaration",
            "optional_parameter_declaration",
            "variadic_parameter_declaration",
        )
    return found


def _constraint_candidates(site: tsast.Node) -> dict[str, str]:
    """Return each template parameter's constraining type name, unresolved.

    A concept-constrained type parameter and a NON-TYPE parameter parse
    identically.  `CalendarGridSessionSurface Grid` and `std::size_t P` are both a
    `parameter_declaration` with a `type` and a `declarator`, so the node shape
    cannot tell a constraint from a value.  Reading the shape alone would count
    `P` as a gate, which is a false pass on a soundness axis.  The caller settles
    it against the tree's own roster of `concept_definition` names: `std::size_t`
    is not among them and `CalendarGridSessionSurface` is.

    Args:
        site: The declaration or definition node

    Returns:
        Parameter name to the leaf of its declared type
    """
    found: dict[str, str] = {}
    for param in _template_parameters(site):
        kind = param.child_by_field("type")
        named = param.child_by_field("declarator")
        if kind is None or named is None:
            continue
        found[named.text] = kind.text.strip().split("::")[-1]
    return found


def _clause_text(site: tsast.Node, declarator: tsast.Node) -> str:
    """Return the text of every `requires` clause that reaches a site.

    Args:
        site: The declaration or definition node
        declarator: The function declarator of the mint

    Returns:
        The clauses joined by a space, or the empty string when there are none
    """
    parts = [c.text for c in declarator.children_of_type("requires_clause")]
    parts += [c.text for c in site.children_of_type("requires_clause")]
    owner = site.parent
    if owner is not None and owner.type == "template_declaration":
        parts += [c.text for c in owner.children_of_type("requires_clause")]
    return " ".join(parts)


def _ctx_token(declarator: tsast.Node) -> str | None:
    """Return the type name of the first parameter, which `_shape` read as a context.

    Args:
        declarator: The function declarator of the mint

    Returns:
        The leaf of the first parameter's type, or None when there is no parameter
    """
    params = declarator.child_by_field("parameters")
    if params is None:
        return None
    first = next(iter(params.children_of_type("parameter_declaration")), None)
    if first is None:
        return None
    kind = first.child_by_field("type")
    return kind.text.strip().split("::")[-1] if kind is not None else None


def _ctx_gated(site: tsast.Node, declarator: tsast.Node, token: str | None) -> bool:
    """Report whether a constraint on this site gates the context it takes.

    Two positions can carry the gate.  A clause that names the context type does
    it — `CtxFitsPipeline<Ctx, Stages...>` and `SubstrateFitsCtxResidency<Substr,
    Ctx>` both name `Ctx`.  A concept written onto the context parameter itself
    does it too, unless that concept is `IsExecCtx`, which states the parameter's
    shape and admits every context.

    A clause that constrains only the OTHER parameters does not gate the context.
    `mint_writer_session` constrains its channel surface and takes any context at
    all, and 24 session mints share that shape.

    Complexity: O(n) in the clause length, from one word-boundary search.

    Args:
        site: The declaration or definition node
        declarator: The function declarator of the mint
        token: The context type name, from `_ctx_token`

    Returns:
        True when a clause names the context, or a non-shape concept constrains it
    """
    if token is None:
        return False
    for name, concept in _constraint_candidates(site).items():
        if name == token and concept != CTX_SHAPE_CONCEPT:
            return True
    text = _clause_text(site, declarator)
    if not text:
        return False
    return re.search(rf"\b{re.escape(token)}\b", text) is not None


def _ctx_parameter_names(site: tsast.Node) -> frozenset[str]:
    """Return the template parameter names this site constrains with IsExecCtx.

    A constrained template parameter parses as a `parameter_declaration` whose
    `type` is the concept and whose `declarator` is the parameter name, so
    `::crucible::effects::IsExecCtx Ctx` yields `Ctx`.  Reading the constraint
    rather than the spelling matters: the tree names the parameter `Ctx` in most
    places and `C` in others, and `mint_endpoint` is one of the others.

    Args:
        site: The declaration or definition node

    Returns:
        Every template parameter name constrained by IsExecCtx
    """
    owner = site.parent
    if owner is None or owner.type != "template_declaration":
        return frozenset()
    names: set[str] = set()
    for plist in owner.children_of_type("template_parameter_list"):
        for param in plist.children_of_type("parameter_declaration"):
            kind = param.child_by_field("type")
            named = param.child_by_field("declarator")
            if kind is None or named is None:
                continue
            if kind.text.split("::")[-1] == "IsExecCtx":
                names.add(named.text)
    return frozenset(names)


def _shape(site: tsast.Node, declarator: tsast.Node, owner: str | None) -> str:
    """Return the authorization shape of a mint.

    §XXI has two flavours.  A ctx-bound mint threads ctx-driven policy and takes
    the context first.  A token mint derives its authority from a parent token
    and takes no context.  A class method is neither, because its authority is
    the object whose method it is.

    Args:
        site: The declaration or definition node
        declarator: The function declarator of the mint
        owner: The enclosing class name for a member mint, otherwise None

    Returns:
        `member`, `ctx` or `token`
    """
    if owner is not None:
        return "member"
    params = declarator.child_by_field("parameters")
    if params is None:
        return "token"
    first = next(iter(params.children_of_type("parameter_declaration")), None)
    if first is None:
        return "token"
    kind = first.child_by_field("type")
    if kind is None:
        return "token"
    # §XXI states the shape exactly: a ctx-bound mint takes `Ctx const&` first.
    # Both halves are load-bearing.  A by-value `Ctx` is a different shape —
    # `mint_hyparview(Ctx, ...)` takes the context by value and the inventory
    # calls it a token.  And the type name must BE the context, not merely
    # mention one: `SessionHandle<Proto, Resource, LoopCtx>` holds "Ctx" as a
    # substring of `LoopCtx`, and a substring test reads six token mints as
    # ctx-bound.
    whole = first.text
    if "&" not in whole or "const" not in whole:
        return "token"
    named = kind.text.strip().split("::")[-1]
    if named in _ctx_parameter_names(site) or named == "Ctx":
        return "ctx"
    return "token"


def _namespace_of(node: tsast.Node) -> str:
    """Return the enclosing namespace of a node, as `a::b::c`.

    A `namespace_definition` names itself either with a single `identifier` or
    with a `nested_namespace_specifier` whose text already reads `a::b::c`, so
    the walk collects the name fields outward and joins them.  An anonymous
    namespace contributes nothing.

    Args:
        node: Any node inside the namespace

    Returns:
        The namespace path, or the empty string at global scope
    """
    parts: list[str] = []
    owner = node.ancestor_of_type("namespace_definition")
    while owner is not None:
        named = owner.child_by_field("name")
        if named is not None:
            parts.append(named.text)
        owner = owner.ancestor_of_type("namespace_definition")
    return "::".join(reversed(parts))


def reexports(paths: list[Path] | None = None) -> dict[str, tuple[str, int]]:
    """Return every fixy re-export, keyed by the qualified name it names.

    A re-export is a `using_declaration` whose one child is a
    `qualified_identifier`.  The key is that qualified name verbatim, so a
    lookup matches the mint the using actually names.  A bare-name key cannot:
    `fixy/Bridge.h:55` reads `using ::crucible::mint_vigil_mode_bridge;`, which
    names the overload in namespace `crucible`, not the one in
    `crucible::vigil_mode` that the inventory pairs it with.

    Args:
        paths: The files to scan, or None to scan include/crucible/fixy

    Returns:
        Qualified name to the site that re-exports it
    """
    files = tsast.cpp_files("include/crucible/fixy") if paths is None else paths
    found: dict[str, tuple[str, int]] = {}
    for tree in tsast.parse(sorted(files), strict=False):
        # A re-export may name its target through a namespace alias, so collect
        # the aliases of the file first.  `fixy/Time.h` writes
        # `namespace sf = ::crucible::safety;` and then `using sf::mint_clock_source;`,
        # and a lookup that demands the full spelling misses it.
        alias: dict[str, str] = {}
        for definition in tree.find("namespace_alias_definition"):
            named = definition.child_by_field("name")
            target = next(iter(definition.children_of_type("nested_namespace_specifier")), None)
            if named is not None and target is not None:
                alias[named.text] = target.text
        for using in tree.find("using_declaration"):
            named = next(iter(using.children_of_type("qualified_identifier")), None)
            if named is None:
                continue
            key = named.text
            if not key.rsplit("::", 1)[-1].startswith("mint_"):
                continue
            head, _, rest = key.partition("::")
            if head in alias:
                key = f"{alias[head]}::{rest}"
            elif not key.startswith("::"):
                key = f"::{key}"
            found.setdefault(key, (str(tree.path), using.line))
    return found


def fixture_counts(paths: list[Path] | None = None) -> dict[str, int]:
    """Return, for each mint name, how many negative-compile fixtures use it.

    HS14 sets a floor of two fixtures per mint.  The count has to come from the
    AST rather than a text scan, because a fixture's own doc comment names the
    mint it is about, and a text scan counts that mention as a use.  An
    identifier node cannot appear inside a comment, so parsing removes the whole
    class of inflation by construction.

    Args:
        paths: The fixture files to scan, or None to find every `*_neg` tree

    Returns:
        Mint name to the number of fixture files that reference it
    """
    if paths is None:
        found: list[Path] = []
        for tree_dir in sorted(tsast.REPO_ROOT.glob("test/*_neg")):
            found += tsast.cpp_files(str(tree_dir.relative_to(tsast.REPO_ROOT)))
        paths = sorted(found)
    counts: dict[str, int] = {}
    for tree in tsast.parse(paths, strict=False):
        seen: set[str] = set()
        for node in tree.find("identifier", "field_identifier"):
            name = node.text
            if name.startswith("mint_"):
                seen.add(name)
        for name in seen:
            counts[name] = counts.get(name, 0) + 1
    return counts


def _carve_outs(tree: tsast.Tree, line: int) -> tuple[bool, bool]:
    """Return the two §XXI carve-out markers above a signature.

    A marker belongs to the comment block that sits IMMEDIATELY above its own
    signature.  So the walk goes up from the signature, steps over the template,
    requires and attribute lines that are part of the same declaration, collects
    the contiguous comment run, and stops at the first line that is neither.

    A fixed-size window cannot do this.  `mint_chaselev_thief` has two
    overloads eight lines apart, the first carries a `cx=alloc` marker and the
    second carries only a prose comment.  A ten-line window reaches past the
    first overload's body and reports its marker on the second one.

    Args:
        tree: The parsed file
        line: The one-based line of the mint identifier

    Returns:
        Whether the `cx=alloc` marker is present, and whether `rq=pre` is
    """
    lines = tree.source.decode("utf-8", "replace").splitlines()
    block: list[str] = []
    index = line - 2  # zero-based, the line directly above the signature
    while index >= 0:
        stripped = lines[index].strip()
        if stripped.startswith("//"):
            block.append(stripped)
        elif stripped.startswith(("template", "requires", "[[")):
            pass  # still the same declaration, keep walking up
        else:
            break
        index -= 1
    text = "\n".join(block)
    return (CARVE_OUT_CX in text, CARVE_OUT_RQ in text)


def extract(tree: tsast.Tree) -> list[Mint]:
    """Return every live §XXI mint declared in one parsed file.

    Complexity: O(n) in the node count of the file.

    Args:
        tree: The parsed file

    Returns:
        The mints, in source order
    """
    mints: list[Mint] = []
    for node in tree.find("identifier", "field_identifier"):
        if node.field != "declarator":
            continue
        declarator = node.parent
        if declarator is None or declarator.type != "function_declarator":
            continue
        name = node.text
        if not name.startswith("mint_") or name.endswith("_"):
            continue
        if node.ancestor_of_type("friend_declaration") is not None:
            continue
        site = declarator.parent
        if site is None:
            continue
        if site.children_of_type("delete_method_clause"):
            continue
        if site.child_by_field("type") is None:
            continue

        owner = None
        if site.parent is not None and site.parent.type == "field_declaration_list":
            enclosing = node.ancestor_of_type("class_specifier", "struct_specifier")
            if enclosing is not None:
                named = enclosing.child_by_field("name")
                owner = named.text if named is not None else None

        carve_cx, carve_rq = _carve_outs(tree, node.line)
        shape = _shape(site, declarator, owner)
        token = _ctx_token(declarator) if shape == "ctx" else None
        mints.append(
            Mint(
                name=name,
                path=str(tree.path),
                line=node.line,
                owner=owner,
                namespace=_namespace_of(node),
                inline_ok=INLINE_OK in tree.source.decode("utf-8", "replace").splitlines()[node.line - 1],
                borrow_projection=_is_borrow_projection(site),
                nodiscard=_has_attribute(site, "nodiscard"),
                constexpr=_has_qualifier(site, "constexpr", "consteval"),
                noexcept_=bool(declarator.children_of_type("noexcept")),
                requires_=_has_requires(site, declarator),
                constraint_concepts=frozenset(_constraint_candidates(site).values()),
                ctx_token=token,
                ctx_gated=_ctx_gated(site, declarator, token),
                shape=shape,
                templated=site.parent is not None and site.parent.type == "template_declaration",
                carve_out_cx=carve_cx,
                carve_out_rq=carve_rq,
            )
        )
    return mints


def concept_names(tree: tsast.Tree) -> set[str]:
    """Return every concept this file declares.

    The roster is derived from the tree, never listed, so a concept added
    anywhere under `include/` counts the moment it is written.

    Args:
        tree: The parsed file

    Returns:
        The declared concept names
    """
    found: set[str] = set()
    for node in tree.root.descendants("concept_definition"):
        named = node.child_by_field("name")
        if named is None:
            named = next((k for k in node.children if k.type == "identifier"), None)
        if named is not None:
            found.add(named.text)
    return found


def collect(paths: list[Path] | None = None) -> list[Mint]:
    """Return every live §XXI mint across the scanned surface, in sorted order.

    A constraining type name is resolved against the concept roster of the WHOLE
    scanned surface, not of the file that uses it, because a mint is constrained
    by concepts its own header only includes.  Both are gathered in one pass and
    the resolution happens after it, so the surface is parsed once.

    Complexity: O(n) in the node count of the surface, plus O(m) in the mint count
    for the resolution.

    Args:
        paths: The files to scan, or None to scan the whole §XXI surface

    Returns:
        Every mint, sorted by name then path then line
    """
    files = scan_files() if paths is None else paths
    raw: list[Mint] = []
    roster: set[str] = set()
    for tree in tsast.parse(files, strict=False):
        raw += extract(tree)
        roster |= concept_names(tree)
    mints = [replace(m, constraint_concepts=m.constraint_concepts & roster) for m in raw]
    return sorted(mints, key=lambda m: (m.name, m.path, m.line))


def _self_test() -> int:
    """Check the extraction against planted fixtures, positive and negative.

    Returns:
        0 when every case holds, 2 otherwise
    """
    import tempfile

    failures: list[str] = []

    def check(name: str, ok: bool) -> None:
        """Record one case result and print it.

        Args:
            name: What the case asserts
            ok: Whether it held
        """
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    print("mintmodel --self-test")
    fixture = """
#include <cstddef>
namespace probe {

template <typename> concept IsExecCtx = true;
template <typename> concept Surface = true;
template <typename> concept Scalar = true;
template <typename> concept CtxFitsProbe = true;
template <typename, typename> concept FitsCtx = true;
struct Ctx {};
struct Thing {};

// A fully compliant ctx-bound mint. The parameter is named C, not Ctx, so the
// shape must come from the IsExecCtx constraint rather than the spelling.
template <IsExecCtx C>
    requires true
[[nodiscard]] constexpr Thing mint_compliant(C const&) noexcept { return {}; }

// The other spelling: a plain Ctx const& with no template at all.
[[nodiscard]] constexpr Thing mint_plain_ctx(Ctx const&) noexcept { return {}; }

// A token mint with no ctx and no requires clause.
[[nodiscard]] constexpr Thing mint_token(Thing) noexcept { return {}; }

// Every flag absent.
Thing mint_bare(Thing) { return {}; }

// §XXI carve-out: cx=alloc — this one allocates.
[[nodiscard]] Thing mint_allocating(Ctx const&) noexcept { return {}; }

// A removed overload is not a live mint.
[[nodiscard]] constexpr Thing mint_removed(int) noexcept = delete("gone");

struct Holder {
    [[nodiscard]] Thing mint_member() const noexcept { return {}; }
    // A friend re-declares a mint defined elsewhere.
    friend constexpr Thing mint_compliant(Ctx const&) noexcept;
};

// A class whose own name starts with mint_ is not a factory.
class mint_not_a_factory {
    constexpr mint_not_a_factory() noexcept = default;
};

// A trailing underscore marks an internal helper.
[[nodiscard]] constexpr Thing mint_internal_(Thing) noexcept { return {}; }

// The constraint sits on the template parameter, not in a clause. The two spell
// one thing, so the presence axis must read both.
template <Scalar K>
[[nodiscard]] constexpr Thing mint_param_constrained(Thing) noexcept { return {}; }

// A NON-TYPE parameter parses exactly like a constrained one. `size_t` is no
// concept, so this mint carries no constraint at all.
template <std::size_t N>
[[nodiscard]] constexpr Thing mint_sized(Thing) noexcept { return {}; }

// A clause that constrains the surface and takes any context whatsoever.
template <Surface S, IsExecCtx C>
    requires Surface<S>
[[nodiscard]] constexpr Thing mint_surface_only(C const&) noexcept { return {}; }

// The same shape, with the context in the clause.
template <Surface S, IsExecCtx C>
    requires FitsCtx<S, C>
[[nodiscard]] constexpr Thing mint_ctx_in_clause(C const&) noexcept { return {}; }

// IsExecCtx alone states the parameter's shape and admits every context.
template <IsExecCtx C>
[[nodiscard]] constexpr Thing mint_shape_only(C const&) noexcept { return {}; }

// A fit concept written onto the context parameter gates it without a clause.
template <CtxFitsProbe Ctx>
[[nodiscard]] constexpr Thing mint_ctx_in_parameter(Ctx const&) noexcept { return {}; }

}  // namespace probe
"""
    with tempfile.TemporaryDirectory() as work:
        path = Path(work) / "fixture.cpp"
        path.write_text(fixture, encoding="utf-8")
        mints = collect([path])
        by_name = {m.name: m for m in mints}

        # Positive control: every live mint is found, and only those.
        check(
            "finds exactly the twelve live mints",
            sorted(by_name) == [
                "mint_allocating", "mint_bare", "mint_compliant",
                "mint_ctx_in_clause", "mint_ctx_in_parameter", "mint_member",
                "mint_param_constrained", "mint_plain_ctx", "mint_shape_only",
                "mint_sized", "mint_surface_only", "mint_token",
            ],
        )
        # Negative controls: each exclusion rule fires.
        check("excludes a deleted overload", "mint_removed" not in by_name)
        check("excludes a constructor of a class named mint_*", "mint_not_a_factory" not in by_name)
        check("excludes a trailing-underscore helper", "mint_internal_" not in by_name)
        check(
            "counts the friend re-declaration as one site, not two",
            len([m for m in mints if m.name == "mint_compliant"]) == 1,
        )

        good = by_name.get("mint_compliant")
        check(
            "reads all four flags present",
            good is not None and good.nodiscard and good.constexpr
            and good.noexcept_ and good.requires_,
        )
        check(
            "reads the ctx shape from the IsExecCtx constraint, not the spelling",
            good is not None and good.shape == "ctx",
        )
        plain = by_name.get("mint_plain_ctx")
        check(
            "reads the ctx shape from a plain Ctx const& parameter",
            plain is not None and plain.shape == "ctx",
        )

        bare = by_name.get("mint_bare")
        check(
            "reads all four flags absent",
            bare is not None and not bare.nodiscard and not bare.constexpr
            and not bare.noexcept_ and not bare.requires_,
        )
        token = by_name.get("mint_token")
        check("reads the token shape when no ctx leads", token is not None and token.shape == "token")
        member = by_name.get("mint_member")
        check(
            "reads the member shape and its owning class",
            member is not None and member.shape == "member" and member.owner == "Holder",
        )
        alloc = by_name.get("mint_allocating")
        check(
            "reads the cx carve-out marker above the signature",
            alloc is not None and alloc.carve_out_cx and not alloc.carve_out_rq,
        )
        check(
            "does not claim a carve-out where no marker sits",
            good is not None and not good.carve_out_cx and not good.carve_out_rq,
        )

        # The presence axis reads a constraint in either position.
        param = by_name.get("mint_param_constrained")
        check(
            "reads a constraint written on a template parameter",
            param is not None and not param.requires_
            and param.constraint_concepts == frozenset({"Scalar"})
            and has_fit_constraint(param),
        )
        # Negative control: a non-type parameter parses the same and is no gate.
        sized = by_name.get("mint_sized")
        check(
            "does NOT read a non-type parameter as a constraint",
            sized is not None and not sized.constraint_concepts
            and not has_fit_constraint(sized),
        )
        check(
            "reads a clause as a constraint with no parameter concept",
            good is not None and has_fit_constraint(good),
        )

        # The context-fit axis: the clause must name the context it gates.
        in_clause = by_name.get("mint_ctx_in_clause")
        check(
            "reads a clause that names the context as gating it",
            in_clause is not None and in_clause.shape == "ctx"
            and in_clause.ctx_token == "C" and in_clause.ctx_gated,
        )
        # Negative control: a clause on another parameter gates no context.
        surface = by_name.get("mint_surface_only")
        check(
            "does NOT read a clause on another parameter as a context gate",
            surface is not None and surface.shape == "ctx"
            and surface.requires_ and not surface.ctx_gated,
        )
        # Negative control: IsExecCtx is the shape, never the fit.
        shape_only = by_name.get("mint_shape_only")
        check(
            "does NOT read IsExecCtx alone as a context gate",
            shape_only is not None and shape_only.shape == "ctx"
            and has_fit_constraint(shape_only) and not shape_only.ctx_gated,
        )
        in_param = by_name.get("mint_ctx_in_parameter")
        check(
            "reads a fit concept on the context parameter as gating it",
            in_param is not None and in_param.shape == "ctx" and in_param.ctx_gated,
        )
        # Negative control: the axis does not apply where there is no context.
        check(
            "does not ask a token mint to gate a context",
            token is not None and not ctxfit_applies(token),
        )

    if failures:
        print(f"mintmodel --self-test: FAILED — {len(failures)} case(s)")
        return 2
    print("mintmodel --self-test: 21 cases pass, 9 of them negative controls.")
    return 0


if __name__ == "__main__":
    try:
        if len(sys.argv) > 1 and sys.argv[1] == "--self-test":
            sys.exit(_self_test())
        for mint in collect():
            flags = "".join("Y" if f else "-" for f in
                            (mint.nodiscard, mint.constexpr, mint.noexcept_, mint.requires_))
            print(f"{mint.qualified:52} {mint.path}:{mint.line:<5} {flags} {mint.shape}")
    except tsast.KitMissing as exc:
        print(f"mintmodel: {exc}", file=sys.stderr)
        sys.exit(3)
