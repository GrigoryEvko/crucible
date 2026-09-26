#!/usr/bin/env python3
"""check-lifetime-twin — every parameter that claims a lifetime bound has the deleted overload that enforces it.

CRUCIBLE_LIFETIMEBOUND expands to nothing.  GCC 16 recognises neither
clang::lifetimebound nor gnu::lifetimebound, so an annotated parameter carries
a claim that no compiler reads.  Two such claims were live bugs, both proven by
compiling the call: a borrow proof minted from a temporary permission, and a
scoped view over a temporary carrier.  The mechanism that GCC does honour is an
overload that takes an rvalue reference in the same position, declared
`= delete`: it is the better match for a prvalue argument, so it refuses the
temporary at the call.

THE RULE
    For each parameter annotated CRUCIBLE_LIFETIMEBOUND at index i of a
    function f with n parameters, the same file declares a deleted overload of
    f in the same scope (the same class, or the same namespace) that:
      * takes n parameters, or ends in a parameter pack, so that the call
        which passes the temporary can pick it
      * takes an rvalue reference at index i, to the same type as the
        annotated parameter (qualifiers aside), or a forwarding reference to a
        template parameter of the overload, which binds every prvalue
    A constructor's name is its class.  An annotation outside a parameter
    list has no twin to check and fails.

WHAT READS THE DECLARATIONS
    The parse tree of the pinned tree-sitter kit (scripts/tsast.py), over the
    headers of include/foundation and include/fixy.  The kit reads the macro
    as an attribute of the parameter.  The line scan it replaces matched a
    deleted overload by name alone, so a twin with another arity, or with the
    rvalue reference at another position, or over an unrelated type,
    satisfied it while the annotated parameter stayed open.

WHAT IT DOES NOT CHECK
    The types of the other parameters of the twin, and a twin in another
    file.  A site whose twin lives elsewhere is restructured, so that the
    twin sits beside it.

Usage
    check-lifetime-twin.py [--quiet]   check the tree
    check-lifetime-twin.py --list      print every site and its verdict
    check-lifetime-twin.py --self-test plant each twin shape and each miss

Exit 0 when every claim has its twin, 1 on a claim with no twin, an annotation
outside a parameter list or a header the parser cannot read, 2 on a bad
invocation or a failed self-test, 3 when the kit is not installed.
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

REPO = tsast.REPO_ROOT
ROOTS = (REPO / "include" / "foundation", REPO / "include" / "fixy")
TOKEN = "CRUCIBLE_LIFETIMEBOUND"
PARAMETERS = ("parameter_declaration", "optional_parameter_declaration", "variadic_parameter_declaration")
FUNCTIONS = ("function_definition", "declaration", "field_declaration")
SCOPES = frozenset({"namespace_definition", "class_specifier", "struct_specifier", "union_specifier"})
REFERENCES = ("reference_declarator", "abstract_reference_declarator")


@dataclass(frozen=True)
class Verdict:
    """One annotated parameter and what the guard found."""

    path: str
    line: int
    name: str
    status: str
    detail: str


def function_of(node: tsast.Node) -> tuple[tsast.Node, tsast.Node] | None:
    """Return the declaration and the function declarator that own a parameter list."""
    declarator = node.parent
    if declarator is None or declarator.type != "function_declarator":
        return None
    owner = declarator.parent
    while owner is not None and owner.type in ("pointer_declarator", "reference_declarator"):
        owner = owner.parent
    return (owner, declarator) if owner is not None and owner.type in FUNCTIONS else None


def name_of(declarator: tsast.Node) -> str:
    """Return the name of a function declarator, with no template arguments or qualifier.

    `ns::Box<T>::get` gives `get`, `operator<=>` gives `operator<=>` and a
    constructor gives its class name.
    """
    named = declarator.child_by_field("declarator")
    leaf = tsast.leaf_name(named) if named is not None else None
    return leaf or ""


def scope_of(node: tsast.Node) -> str:
    """Return the chain of enclosing namespaces and classes of a declaration, as a key.

    A friend declaration names a function of the innermost enclosing
    namespace, so its classes are not part of its scope.  Each name is
    spelled from its tokens, so a comment inside a nested namespace name does
    not change the key.
    """
    in_friend = node.ancestor_of_type("friend_declaration") is not None
    parts: list[str] = []
    owner = node.parent
    while owner is not None:
        if owner.type in SCOPES and not (in_friend and owner.type != "namespace_definition"):
            named = owner.child_by_field("name")
            parts.insert(0, tsast.spelled(named) if named is not None else "(anonymous)")
        owner = owner.parent
    return "::".join(parts)


def parameters_of(declarator: tsast.Node) -> list[tsast.Node]:
    """Return the parameters of a function declarator, in order."""
    listed = declarator.child_by_field("parameters")
    return [child for child in listed.children if child.type in PARAMETERS] if listed is not None else []


def base_type(parameter: tsast.Node) -> str:
    """Return the type of a parameter, spelled from its tokens, with its qualifiers left out.

    A comment inside a template argument does not change the spelling.
    """
    kind = parameter.child_by_field("type")
    return tsast.spelled(kind) if kind is not None else ""


def is_rvalue_reference(parameter: tsast.Node) -> bool:
    """Report whether a parameter is declared as an rvalue reference.

    The `&&` is the first token of the reference declarator.
    """
    declarator = parameter.child_by_field("declarator")
    while declarator is not None and declarator.type == "attributed_declarator":
        declarator = next((child for child in declarator.children if child.type in REFERENCES), None)
    return declarator is not None and declarator.type in REFERENCES and declarator.tokens()[:1] == ["&&"]


def template_parameters(node: tsast.Node) -> set[str]:
    """Return the names that the template declarations around a declaration bind."""
    names: set[str] = set()
    owner = node.parent
    while owner is not None and owner.type == "template_declaration":
        listed = owner.child_by_field("parameters")
        if listed is not None:
            for parameter in listed.children:
                named = parameter.child_by_field("name")
                if named is not None:
                    names.add(named.text)
                names.update(child.text for child in parameter.children if child.type == "type_identifier")
        owner = owner.parent
    return names


@dataclass(frozen=True)
class Twin:
    """One deleted overload."""

    scope: str
    name: str
    parameters: tuple[tsast.Node, ...]
    variadic: bool
    forwarding: frozenset[str]


def twins_of(tree: tsast.Tree) -> list[Twin]:
    """Return every deleted function declaration of a file."""
    found: list[Twin] = []
    for node in tree.find(*FUNCTIONS):
        if not node.children_of_type("delete_method_clause"):
            continue
        declarator = node.child_by_field("declarator")
        while declarator is not None and declarator.type in ("pointer_declarator", "reference_declarator"):
            declarator = declarator.child_by_field("declarator")
        if declarator is None or declarator.type != "function_declarator":
            continue
        parameters = parameters_of(declarator)
        # A pack parameter parses as its own node type, so a `sizeof...` in
        # the type of a plain parameter is no pack.
        variadic = any(parameter.type == "variadic_parameter_declaration" for parameter in parameters)
        found.append(Twin(scope_of(node), name_of(declarator), tuple(parameters), variadic,
                          frozenset(template_parameters(node))))
    return found


def matches(twin: Twin, index: int, arity: int, wanted: str) -> bool:
    """Report whether a deleted overload refuses a temporary at one parameter index."""
    if not (len(twin.parameters) == arity or (twin.variadic and len(twin.parameters) <= arity + 1)):
        return False
    if index >= len(twin.parameters):
        return False
    parameter = twin.parameters[index]
    if not is_rvalue_reference(parameter):
        return False
    kind = base_type(parameter)
    return kind == wanted or kind in twin.forwarding


def verdicts_of(tree: tsast.Tree, shown: str) -> list[Verdict]:
    """Return the verdict of each annotated parameter of one parsed header."""
    twins = twins_of(tree)
    verdicts: list[Verdict] = []
    for macro in tree.find("attribute_macro"):
        named = macro.child_by_field("name")
        if named is None or named.text != TOKEN:
            continue
        parameter = macro.ancestor_of_type(*PARAMETERS)
        owner = function_of(parameter.parent) if parameter is not None and parameter.parent is not None else None
        if parameter is None or owner is None:
            verdicts.append(Verdict(shown, macro.line, "", "unreadable",
                                    "the annotation sits outside a parameter list, so no twin can enforce it"))
            continue
        declaration, declarator = owner
        name = name_of(declarator)
        parameters = parameters_of(declarator)
        index = next(position for position, item in enumerate(parameters) if item.index == parameter.index)
        wanted = base_type(parameter)
        scope = scope_of(declaration)
        candidates = [twin for twin in twins if twin.name == name and twin.scope == scope]
        if any(matches(twin, index, len(parameters), wanted) for twin in candidates):
            verdicts.append(Verdict(shown, macro.line, name, "ok", ""))
        elif candidates:
            verdicts.append(Verdict(shown, macro.line, name, "missing",
                                    f"a deleted overload of {name} exists, but none takes {len(parameters)} "
                                    f"parameter(s) with `{wanted}&&` or a forwarding reference at position "
                                    f"{index + 1}"))
        else:
            verdicts.append(Verdict(shown, macro.line, name, "missing",
                                    f"no deleted overload of {name} in the same scope of this file"))
    return verdicts


def collect(roots: tuple[Path, ...] | list[Path]) -> tuple[list[Verdict], list[str]]:
    """Return the verdict of each site under the roots, and each header the parser cannot read.

    Every C++ file under the roots is parsed, with no text test first, so a
    file whose token a line splice or a macro hides is still read.

    Complexity: linear in the size of the files under the roots.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    files = sorted(path for root in roots if root.is_dir() for path in root.rglob("*")
                   if path.is_file() and path.name.endswith(tsast.CPP_SUFFIXES))
    verdicts: list[Verdict] = []
    unread: list[str] = []
    for tree in tsast.parse(files, strict=False):
        path = Path(tree.path)
        shown = path.relative_to(REPO).as_posix() if path.is_relative_to(REPO) else path.as_posix()
        if tree.diagnostic is not None:
            unread.append(f"UNREADABLE {shown} does not parse, so its claims are unknown.\n  {tree.diagnostic}")
            continue
        verdicts += verdicts_of(tree, shown)
    return verdicts, unread


def run(roots: tuple[Path, ...] | list[Path], listing: bool = False) -> int:
    """Check the claims, print each failure, and return the exit code."""
    verdicts, unread = collect(roots)
    for verdict in verdicts:
        if verdict.status == "missing":
            print(f"NO TWIN    {verdict.path}:{verdict.line}  {verdict.name} — {verdict.detail}.")
        elif verdict.status == "unreadable":
            print(f"UNREADABLE {verdict.path}:{verdict.line} — {verdict.detail}.")
        elif listing:
            print(f"OK         {verdict.path}:{verdict.line}  {verdict.name}")
    for line in unread:
        print(line)
    failing = sum(verdict.status != "ok" for verdict in verdicts) + len(unread)
    print(f"check-lifetime-twin: {len(verdicts)} site(s), {failing} failing.", file=sys.stderr)
    return 1 if failing else 0


def self_test() -> int:
    """Plant each twin shape and each miss, and examine each verdict.

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

    planted = (
        "#pragma once\n"
        "struct Carrier {};\n"
        "struct Other {};\n"
        "struct Guarded {\n"
        "    explicit Guarded(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept;\n"
        "    explicit Guarded(Carrier const&&) = delete(\"the twin\");\n"
        "};\n"
        "template <class T>\n"
        "constexpr int forwarded(T& ref CRUCIBLE_LIFETIMEBOUND) noexcept;\n"
        "template <class T>\n"
        "    requires(!std::is_lvalue_reference_v<T>)\n"
        "constexpr auto forwarded(T&&) = delete(\"a forwarding twin\");\n"
        "template <class T>\n"
        "constexpr int wrapped(Carrier const& first,\n"
        "                      T& second CRUCIBLE_LIFETIMEBOUND) noexcept;\n"
        "template <class T>\n"
        "constexpr auto wrapped(Carrier const&, T&&) = delete(\"a twin at the second position\");\n"
        "struct Unguarded {\n"
        "    explicit Unguarded(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept;\n"
        "};\n"
        "int wrong_arity(Carrier const& c CRUCIBLE_LIFETIMEBOUND, int n) noexcept;\n"
        "int wrong_arity(Carrier const&&) = delete;\n"
        "int wrong_position(int n, Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept;\n"
        "int wrong_position(Carrier const&&, int) = delete;\n"
        "int wrong_type(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept;\n"
        "int wrong_type(Other const&&) = delete;\n"
        "int lvalue_twin(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept;\n"
        "int lvalue_twin(Carrier&) = delete;\n"
        "namespace elsewhere { int other_scope(Carrier const&&) = delete; }\n"
        "int other_scope(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept;\n"
        "int packed(Carrier const& c CRUCIBLE_LIFETIMEBOUND, int n) noexcept;\n"
        "template <class... Rest> int packed(Carrier const&&, Rest&&...) = delete;\n"
        "// int in_a_comment(Carrier const& c CRUCIBLE_LIFETIMEBOUND);\n"
        "namespace ns {\n"
        "template <class T> int befriended(T& ref CRUCIBLE_LIFETIMEBOUND) noexcept;\n"
        "template <class T> auto befriended(T&&) = delete;\n"
        "struct Holder { template <class U> friend int befriended(U& ref CRUCIBLE_LIFETIMEBOUND) noexcept; };\n"
        "}\n"
        "int spanned(std::span<int> const& s CRUCIBLE_LIFETIMEBOUND) noexcept;\n"
        "int spanned(std::span</*T*/int> const&&) = delete;\n"
        "int packish(Carrier const& c CRUCIBLE_LIFETIMEBOUND, int n) noexcept;\n"
        "template <class... Ts> int packish(Carrier const&&, decltype(sizeof...(Ts)) extra, long more) = delete;\n"
        "struct Ordered {\n"
        "    auto operator<=>(Carrier const& c CRUCIBLE_LIFETIMEBOUND) const noexcept;\n"
        "    auto operator<=>(Carrier const&&) const = delete;\n"
        "};\n"
    )
    with tempfile.TemporaryDirectory() as work:
        root = Path(work) / "inc"
        root.mkdir()
        (root / "Planted.h").write_text(planted, encoding="utf-8")
        verdicts, unread = collect([root])
        status = {verdict.name: verdict.status for verdict in verdicts}
        expect("the planted header parses", not unread)
        for name in ("Guarded", "forwarded", "wrapped", "packed"):
            expect(f"a twin enforces {name}", status.get(name) == "ok")
        for name, label in (("Unguarded", "no twin at all"), ("wrong_arity", "a twin with another arity"),
                            ("wrong_position", "a twin with the rvalue reference at another position"),
                            ("wrong_type", "a twin over another type"), ("lvalue_twin", "a twin with an lvalue"),
                            ("other_scope", "a twin in another namespace")):
            expect(f"refused: {label}", status.get(name) == "missing", True)
        expect("a comment is no site", "in_a_comment" not in status)
        expect("a comment inside a template argument does not change the type",
               status.get("spanned") == "ok")
        expect("refused: a twin whose parameter only mentions a pack, with another arity",
               status.get("packish") == "missing", True)
        expect("an operator keeps its whole name, and its twin enforces it",
               status.get("operator<=>") == "ok" and "operator" not in status)
        befriended = [verdict.status for verdict in verdicts if verdict.name == "befriended"]
        expect("a friend re-declaration finds the twin of its namespace",
               befriended == ["ok", "ok"])
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(io.StringIO()):
            code = run([root])
        expect("a missing twin exits 1", code == 1 and "NO TWIN" in buffer.getvalue(), True)
        previous = Path.cwd()
        os.chdir("/")
        try:
            again, _ = collect([root])
        finally:
            os.chdir(previous)
        expect("the verdicts from / equal the verdicts from the repository", again == verdicts)
        (root / "Planted.h").write_text(
            "#pragma once\nstruct Carrier {};\n"
            "struct Guarded {\n    explicit Guarded(Carrier const& c CRUCIBLE_LIFETIMEBOUND) noexcept;\n"
            "    explicit Guarded(Carrier const&&) = delete(\"the twin\");\n};\n", encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            code = run([root])
        expect("a fully twinned set exits 0", code == 0)

    if failures:
        print(f"check-lifetime-twin --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-lifetime-twin --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode."""
    try:
        if argv in ([], ["--quiet"]):
            return run(ROOTS)
        if argv == ["--list"]:
            return run(ROOTS, listing=True)
        if argv == ["--self-test"]:
            return self_test()
    except tsast.KitMissing as exc:
        print(f"check-lifetime-twin: {exc}", file=sys.stderr)
        return 3
    print("usage: check-lifetime-twin.py [--quiet | --list | --self-test]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
