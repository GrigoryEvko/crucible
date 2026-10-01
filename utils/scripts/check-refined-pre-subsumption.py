#!/usr/bin/env python3
"""check-refined-pre-subsumption — a Refined parameter needs no precondition that re-tests its predicate.

A parameter of type Refined<Pred, T> or SealedRefined<Pred, T> carries a
proof: the predicate held when the value was built.  A precondition in the
same function that cites the decide:: procedure of that predicate on that
parameter re-tests a proved fact.  It costs a branch, and it hides the proof
from a reader, who then believes the check is needed.

WHAT THE GUARD READS
    The parse tree of the pinned tree-sitter kit (utils/scripts/tsast.py), over
    include/ and src/.  test/ and bench/ are out of scope, because the
    fixtures there spell the pattern on purpose.

    A refined parameter is a parameter of a function definition whose type
    is a template-id named Refined or SealedRefined, qualified or not, or an
    alias declared anywhere in the scope with such a type (`using PositiveInt
    = Refined<positive, int>;`, an alias template too).  Its predicate is
    the leaf name of the first template argument, so `bounded_above<8>`
    reads as bounded_above.

    A precondition is a CRUCIBLE_PRE, CRUCIBLE_PRE_FAST or CRUCIBLE_PRE_MSG
    call in the body of that function.  A precondition in a lambda inside the
    body belongs to the lambda, not to the function.  The tree has no P2900
    pre specifier: the quarantine plugin and check-contract-form.py reject
    one.

    A re-test is a call inside a precondition whose callee is decide::NAME,
    where NAME cites the predicate (CITES below), and whose arguments name
    the parameter.

SUPPRESSION
    A comment `REFINED-PRE-OK: <reason>` on a row of the statement that holds
    the precondition, or at the end of its last row, keeps a deliberate
    re-test (tsast.has_marker).

Exit 0 clean, 1 on a violation or a file that does not parse, 2 on a failed
self-test or a bad invocation, 3 when the kit is missing.
"""

from __future__ import annotations

import contextlib
import io
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402

ROOTS = ("include", "src")
REFINED_TEMPLATES = frozenset({"Refined", "SealedRefined"})
PRE_MACROS = frozenset({"CRUCIBLE_PRE", "CRUCIBLE_PRE_FAST", "CRUCIBLE_PRE_MSG"})
MARKER = "REFINED-PRE-OK"
# The decide:: procedures that re-test each refinement predicate.
CITES: dict[str, frozenset[str]] = {
    "positive": frozenset({"positive"}),
    "non_negative": frozenset({"non_negative"}),
    "non_zero": frozenset({"is_non_zero", "non_zero"}),
    "bounded_above": frozenset({"in_range", "bounded_above"}),
    "bounded_below": frozenset({"in_range", "bounded_below"}),
    "in_range": frozenset({"in_range", "all_in_range"}),
}


@dataclass(frozen=True)
class Violation:
    """One precondition that re-tests the predicate of a refined parameter."""

    path: str
    line: int
    parameter: str
    predicate: str
    cite: str


def refined_template(kind: tsast.Node | None) -> tsast.Node | None:
    """The template-id of a parameter type, through its qualifier, or None for any other type."""
    while kind is not None and kind.type == "qualified_identifier":
        kind = kind.child_by_field("name")
    return kind if kind is not None and kind.type == "template_type" else None


def first_argument_leaf(template: tsast.Node) -> str | None:
    """The leaf name of the first template argument of a template-id, or None."""
    arguments = template.child_by_field("arguments")
    if arguments is None:
        return None
    for argument in arguments.children:
        if argument.type == "comment":
            continue
        inner = argument.child_by_field("type") if argument.type == "type_descriptor" else argument
        return tsast.leaf_name(inner) if inner is not None else None
    return None


def refined_aliases(trees: list[tsast.Tree]) -> dict[str, set[str]]:
    """Each alias whose type is a refinement, with the predicates that its declarations name.

    Complexity: linear in the number of alias declarations of the scope."""
    aliases: dict[str, set[str]] = {}
    for tree in trees:
        for alias in tree.find("alias_declaration"):
            name = alias.child_by_field("name")
            descriptor = alias.child_by_field("type")
            template = refined_template(descriptor.child_by_field("type") if descriptor is not None else None)
            if name is None or template is None or tsast.leaf_name(template) not in REFINED_TEMPLATES:
                continue
            predicate = first_argument_leaf(template)
            if predicate is not None:
                aliases.setdefault(name.text, set()).add(predicate)
    return aliases


def parameter_predicates(parameter: tsast.Node, aliases: dict[str, set[str]]) -> set[str]:
    """The refinement predicates that a parameter's type proves, or an empty set."""
    kind = parameter.child_by_field("type")
    template = refined_template(kind)
    if template is not None and tsast.leaf_name(template) in REFINED_TEMPLATES:
        predicate = first_argument_leaf(template)
        return {predicate} if predicate is not None else set()
    leaf = tsast.leaf_name(template if template is not None else kind) if kind is not None else None
    # Two declarations of one alias name with different predicates make the
    # alias ambiguous, and an ambiguous alias proves nothing here.
    predicates = aliases.get(leaf, set()) if leaf is not None else set()
    return predicates if len(predicates) == 1 else set()


def is_owned_by(node: tsast.Node, function: tsast.Node) -> bool:
    """Report whether a function definition is the nearest function or lambda that holds a node.

    A Node is a view that each walk builds again, so the check compares node
    indices of the one tree, not the view objects."""
    owner = node.ancestor_of_type("function_definition", "lambda_expression")
    return owner is not None and owner.index == function.index


def preconditions(function: tsast.Node) -> list[tsast.Node]:
    """The precondition nodes of one function definition: its CRUCIBLE_PRE calls."""
    found: list[tsast.Node] = []
    body = function.child_by_field("body")
    if body is not None:
        for call in body.descendants("call_expression"):
            callee = call.child_by_field("function")
            if callee is not None and callee.type == "identifier" and callee.text in PRE_MACROS \
                    and is_owned_by(call, function):
                found.append(call)
    return found


def retests(precondition: tsast.Node, name: str, cites: frozenset[str]) -> str | None:
    """The decide:: cite inside a precondition that tests the named parameter, or None."""
    for call in precondition.descendants("call_expression"):
        parts = tsast.qualified_parts(call.child_by_field("function")) if call.child_by_field("function") else None
        if parts is None:
            continue
        segments = parts[1]
        if len(segments) < 2 or segments[-2] != "decide" or segments[-1] not in cites:
            continue
        arguments = call.child_by_field("arguments")
        if arguments is not None and any(identifier.text == name for identifier in arguments.descendants("identifier")):
            return segments[-1]
    return None


def is_marked(precondition: tsast.Node) -> bool:
    """Report whether a precondition carries the suppression marker on its statement."""
    return tsast.has_marker(tsast.enclosing_statement(precondition), MARKER)


def violations_of(tree: tsast.Tree, shown: str, aliases: dict[str, set[str]]) -> list[Violation]:
    """Every re-test of a refined parameter in one parsed file.

    Complexity: linear in the size of each function body, times the refined
    parameters of that function."""
    found: list[Violation] = []
    for function in tree.find("function_definition"):
        refined = [(name, predicate) for name, parameter in tsast.parameters(function)
                   for predicate in parameter_predicates(parameter, aliases) if predicate in CITES]
        if not refined:
            continue
        for precondition in preconditions(function):
            for name, predicate in refined:
                cite = retests(precondition, name, CITES[predicate])
                if cite is not None and not is_marked(precondition):
                    found.append(Violation(shown, precondition.line, name, predicate, cite))
    return found


def scan(root: Path) -> tuple[list[Violation], list[str]]:
    """Every re-test under the roots of one tree, and each file that does not parse."""
    files = [path for top in ROOTS if (root / top).is_dir() for path in sorted((root / top).rglob("*"))
             if path.is_file() and path.suffix in tsast.CPP_SUFFIXES
             and path.relative_to(root).as_posix() not in tsast.UNPARSEABLE]
    trees: list[tsast.Tree] = []
    unread: list[str] = []
    for tree in tsast.parse(files, strict=False):
        shown = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            unread.append(f"{shown} does not parse, so its preconditions are unknown.  {tree.diagnostic.strip()}")
            continue
        trees.append(tree)
    aliases = refined_aliases(trees)
    found = [violation for tree in trees
             for violation in violations_of(tree, Path(tree.path).relative_to(root).as_posix(), aliases)]
    return found, unread


def check(root: Path) -> int:
    """Scan one tree and report each re-test and each file that does not parse."""
    found, unread = scan(root)
    for violation in found:
        print(f"REFINED-PRE violation: {violation.path}:{violation.line} — parameter `{violation.parameter}` is "
              f"Refined<{violation.predicate}, ...>, and a precondition re-tests it with "
              f"decide::{violation.cite}.", file=sys.stderr)
    for line in unread:
        print(f"REFINED-PRE parse failure: {line}", file=sys.stderr)
    if found:
        print("\ncheck-refined-pre-subsumption: the type already proves each predicate above.  Drop the "
              "precondition, or make it test a relation that the type does not carry, or mark a deliberate "
              f"re-test with `// {MARKER}: <reason>` on its statement.", file=sys.stderr)
    if found or unread:
        return 1
    print("check-refined-pre-subsumption: clean, no precondition re-tests a refined parameter.", file=sys.stderr)
    return 0


# Six shapes that re-test nothing: no cite, a plain parameter, a cite of a
# different name, a cite in a comment, a cite that a lambda owns, and a
# marked re-test.
CLEAN_FIXTURE = """#pragma once
namespace crucible::planted {
template <typename P, typename T> struct Refined { T value_; };
struct positive {};
inline void no_cite(Refined<positive, int> n) { (void)n; }
inline void bare_parameter(int n) { CRUCIBLE_PRE(decide::positive(n)); }
inline void other_identifier(Refined<positive, int> n, int m) { CRUCIBLE_PRE(decide::positive(m)); (void)n; }
inline void comment_only(Refined<positive, int> n) { /* CRUCIBLE_PRE(decide::positive(n)); */ (void)n; }
inline void lambda_owns_it(Refined<positive, int> n) {
    auto inner = [](int n) { CRUCIBLE_PRE(decide::positive(n)); };
    inner(1);
    (void)n;
}
inline void marked(Refined<positive, int> n) {
    CRUCIBLE_PRE(decide::positive(n));  // REFINED-PRE-OK: kept while the caller moves over.
}
}  // namespace crucible::planted
"""

# Six re-tests, one on each line of CAUGHT_LINES: a plain parameter, a nested
# predicate, a const parameter, an alias, a SealedRefined parameter, and a
# cite split over two lines.
CAUGHT_FIXTURE = """#pragma once
namespace crucible::planted {
template <typename P, typename T> struct Refined { T value_; };
template <typename P, typename T> struct SealedRefined { T value_; };
template <int N> struct bounded_above {};
struct positive {};
struct non_zero {};
using PositiveInt = Refined<positive, int>;
inline void plain(Refined<positive, int> n) {
    CRUCIBLE_PRE(decide::positive(n));
}
inline void nested_bound(Refined<bounded_above<8>, unsigned> idx) {
    CRUCIBLE_PRE(decide::in_range(idx, 0u, 7u));
}
inline void const_parameter(Refined<positive, int> const n) {
    CRUCIBLE_PRE_FAST(decide::positive(n));
}
inline void through_alias(PositiveInt n) {
    CRUCIBLE_PRE_MSG(decide::positive(n), "re-test");
}
inline int sealed(SealedRefined<non_zero, int> d) {
    CRUCIBLE_PRE(decide::is_non_zero(d));
    return 10;
}
inline void split_over_lines(Refined<positive, int> n) {
    CRUCIBLE_PRE(::crucible::decide::
        positive(n));
}
}  // namespace crucible::planted
"""
CAUGHT_LINES = {10, 13, 16, 19, 22, 26}


def plant(root: Path) -> None:
    """Write the two fixtures into a tree under include/planted/, with an empty src/."""
    (root / "include" / "planted").mkdir(parents=True)
    (root / "include" / "planted" / "Clean.h").write_text(CLEAN_FIXTURE)
    (root / "include" / "planted" / "Caught.h").write_text(CAUGHT_FIXTURE)
    (root / "src").mkdir()


def self_test() -> int:
    """Plant each shape of a re-test and each shape that is not one, and check every verdict."""
    tsast.kit_dir()
    failures: list[str] = []
    with tempfile.TemporaryDirectory(prefix="refined-pre-self-test-") as tmp_name:
        root = Path(tmp_name)
        plant(root)
        found, unread = scan(root)
        if unread:
            failures.append(f"the planted tree must parse: {unread}")
        clean = [violation for violation in found if violation.path.endswith("Clean.h")]
        if clean:
            failures.append(f"a clean shape was flagged: {clean}")
        caught = {violation.line for violation in found if violation.path.endswith("Caught.h")}
        if caught != CAUGHT_LINES:
            failures.append(f"Caught.h flags lines {sorted(caught)}, expected {sorted(CAUGHT_LINES)}")
        (root / "src" / "Broken.cpp").write_text("void f() { g(1) { } }\n")
        with contextlib.redirect_stderr(io.StringIO()) as report:
            status = check(root)
        if status != 1 or "src/Broken.cpp does not parse" not in report.getvalue():
            failures.append(f"a file that does not parse must fail the check, not exit {status}")
    for line in failures:
        print(f"check-refined-pre-subsumption: SELF-TEST FAILED, {line}", file=sys.stderr)
    if failures:
        return 2
    print("check-refined-pre-subsumption: self-test passed, six clean shapes pass and six re-tests are caught.",
          file=sys.stderr)
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test."""
    if argv not in ([], ["--self-test"]):
        print("usage: check-refined-pre-subsumption.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as missing:
        print(f"check-refined-pre-subsumption: SKIP, {missing}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
