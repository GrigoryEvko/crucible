#!/usr/bin/env python3
"""check-row-contains-discipline — a context capability check uses a named lift, read from the parse tree.

A check that a context owns an effect is written with a named lift:

    row_contains(^^row_type_of_t<Ctx>, Effect::X)          ->  CtxOwnsCapability<Ctx, Effect::X>
    row_contains(^^..., X) || row_contains(^^..., Y)       ->  CtxOwnsAnyOf<Ctx, Effect::X, Effect::Y>
    row_contains(^^..., X) && row_contains(^^..., Y)       ->  CtxOwnsAllOf<Ctx, Effect::X, Effect::Y>

`grep CtxOwns` then finds each capability admission, and a rename of the
row accessor reaches every check through the lift.

WHAT COUNTS AS A CONTEXT CAPABILITY CHECK
    The guard reads the parse tree of the pinned tree-sitter kit.  A check is
    a call of row_contains, bare or with any qualifier, so an alias of the
    effects namespace and a using-directive do not hide it, whose first
    argument reads the row of a context: a template-id named row_type_of_t,
    or a qualified name whose last part is row_type, such as
    `^^typename Ctx::row_type`.  A membership check on a concrete row, such
    as a required row that a static_assert names, is not a context
    capability check.

    A macro body is parsed on its own (tsast.macro_bodies), with every
    fragment joined, so a block comment inside the body does not split the
    check.  A body that the parser cannot read, such as one that pastes
    tokens with ##, is read from its preprocessing tokens: row_contains,
    `(`, and a first argument that holds row_type_of_t or `:: row_type`.

SCOPE
    Every C++ file of include/ and src/ that spells row_contains.  The test
    removes each line splice first, so a splice cannot hide the name, and a
    name is compared as the lexer spells it, after the splices.  These files
    are out of scope:
      * The definitions of the lifts, in include/foundation/effects/
      * The files of tsast.UNPARSEABLE, which are not C++.

EXEMPTION
    `// ROW-CONTAINS-OK: <reason>` on a line of the check or of the
    statement that holds it exempts the check.  The reason must not be
    empty.

Exit 0 clean, 1 on a check that does not use a lift or a parse failure, 2 on a
usage error or a failed self-test, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import sys
import tempfile
from collections.abc import Iterator
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402

ROOTS = ("include", "src")
EXCLUDED_PREFIXES = ("include/foundation/effects/",)
EXCLUDED_COMPONENTS = frozenset({"test", "bench", "examples", "third_party", "external", "vendor"})
NAME = "row_contains"
ROW_OF = "row_type_of_t"
ROW_MEMBER = "row_type"
ROW_NODES = ("template_type", "template_function", "qualified_identifier")
MARKER = re.compile(r"ROW-CONTAINS-OK:\s*\S")
LINE_SPLICE = re.compile(rb"\\\r?\n")
STATEMENTS = ("declaration", "field_declaration", "alias_declaration", "requires_clause", "condition_clause",
              "expression_statement", "return_statement", "static_assert_declaration", "template_declaration",
              "concept_definition")
OPENERS = {"(": 1, "[": 1, "{": 1, "<": 1, ")": -1, "]": -1, "}": -1, ">": -1, ">>": -2}


def scope_files(root: Path) -> list[Path]:
    """Return the C++ files in scope that spell row_contains, sorted.

    The test reads the bytes with each line splice removed, so a name that
    a splice cuts in two still counts.
    """
    found: list[Path] = []
    for top in ROOTS:
        base = root / top
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            rel = path.relative_to(root)
            posix = rel.as_posix()
            if path.is_file() and tsast.is_in_cpp_scope(rel) and not posix.startswith(EXCLUDED_PREFIXES) \
                    and not any(part in EXCLUDED_COMPONENTS or part.startswith("build") for part in rel.parts[:-1]) \
                    and NAME.encode() in LINE_SPLICE.sub(b"", path.read_bytes()):
                found.append(path)
    return sorted(found)


def called_name(call: tsast.Node) -> str | None:
    """Return the last part of the name that a call names, or None for a call of any other shape."""
    function = call.child_by_field("function")
    while function is not None and function.type == "qualified_identifier":
        function = function.child_by_field("name")
    return tsast.spelled(function) if function is not None and function.type == "identifier" else None


def first_argument(call: tsast.Node) -> tsast.Node | None:
    """Return the first argument of a call, or None."""
    arguments = call.child_by_field("arguments")
    if arguments is None:
        return None
    inner = tsast.non_comment_children(arguments)
    return inner[0] if inner else None


def reads_context_row(argument: tsast.Node) -> bool:
    """Report whether an argument reads the row of a context.

    The argument reads it through a template-id named row_type_of_t, or
    through a qualified name whose last part is row_type, such as
    `^^typename Ctx::row_type`.  A comment inside the argument is its own
    node, so it cannot supply either name.  A name is compared as the lexer
    spells it, after the line splices of phase 2.
    """
    for node in [argument, *argument.descendants(*ROW_NODES)]:
        named = node.child_by_field("name")
        if named is None:
            continue
        if node.type in ("template_type", "template_function") and tsast.spelled(named) == ROW_OF:
            return True
        if node.type == "qualified_identifier" and node.child_by_field("scope") is not None \
                and named.type in ("type_identifier", "identifier") and tsast.spelled(named) == ROW_MEMBER:
            return True
    return False


def statement_rows(node: tsast.Node) -> range:
    """Return the rows of a check and of the statement that holds it, for a marker."""
    holder = node.ancestor_of_type(*STATEMENTS)
    last = max(node.end[0], holder.end[0]) if holder is not None else node.end[0]
    first = min(node.start[0], holder.start[0]) if holder is not None else node.start[0]
    return range(first, last + 1)


def context_checks(nodes: Iterator[tsast.Node]) -> Iterator[tsast.Node]:
    """Yield each call among the nodes that is a context capability check through row_contains."""
    for node in nodes:
        argument = first_argument(node)
        if called_name(node) == NAME and argument is not None and reads_context_row(argument):
            yield node


def token_checks(tokens: list[tsast.Token]) -> Iterator[int]:
    """Yield the row of each context capability check in a token list: the tokens of a body that did not parse.

    The first argument runs from the `(` after row_contains to the first `,`
    or the closing `)` at the same depth.  It reads a context row when it
    holds row_type_of_t, or `::` followed by row_type.
    """
    for index, token in enumerate(tokens):
        if token.text != NAME or index + 1 >= len(tokens) or tokens[index + 1].text != "(":
            continue
        depth, cursor, first = 0, index + 2, []
        while cursor < len(tokens):
            text = tokens[cursor].text
            if text in (",", ")") and depth == 0:
                break
            depth += OPENERS.get(text, 0)
            first.append(tokens[cursor])
            cursor += 1
        texts = [piece.text for piece in first]
        if ROW_OF in texts or any(texts[at] == "::" and texts[at + 1] == ROW_MEMBER for at in range(len(texts) - 1)):
            yield token.row


def unlifted_checks(tree: tsast.Tree, marked: set[int]) -> Iterator[int]:
    """Yield the zero-based row of each context capability check in a file's own code that does not use a lift.

    Complexity: linear in the number of nodes of the file.
    """
    for node in context_checks(tree.find("call_expression")):
        if not any(row in marked for row in statement_rows(node)):
            yield node.start[0]


def unlifted_macro_checks(body: tsast.MacroBody, marked: set[int]) -> Iterator[int]:
    """Yield the file row of each context capability check in one macro body that does not use a lift.

    A marker on any row of the definition, to the row of the last token of
    its body, exempts the checks of its body.  A parsed body is read from
    its parse tree and from its tokens too, because a line splice inside a
    call can leave the parse with no call node, and the tokens still hold
    the call.
    """
    if any(row in marked for row in range(body.define.start[0], body.last_row + 1)):
        return
    if body.is_parsed:
        for node in context_checks(body.root.descendants("call_expression")):
            yield body.origin(node)[0]
    yield from token_checks(tsast.pp_tokens(body.text, body.first_row))


def scan(root: Path) -> list[str]:
    """Find each context capability check in scope that does not use a lift.

    Complexity: linear in the total size of the files in scope.
    """
    violations: list[str] = []
    trees: list[tsast.Tree] = []
    for tree in tsast.parse(scope_files(root), strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            violations.append(f"{rel}: the parser cannot read this file. {tree.diagnostic.strip()}")
            continue
        trees.append(tree)
    rows: dict[str, set[int]] = {}
    marks: dict[str, set[int]] = {}
    for tree in trees:
        rel = Path(tree.path).relative_to(root).as_posix()
        marks[rel] = {node.start[0] for node in tree.find("comment") if MARKER.search(tsast.prose_text(node))}
        rows[rel] = set(unlifted_checks(tree, marks[rel]))
    for body in tsast.macro_bodies(trees):
        rel = Path(body.define.tree.path).relative_to(root).as_posix()
        rows[rel].update(unlifted_macro_checks(body, marks[rel]))
    for rel in sorted(rows):
        violations.extend(f"{rel}:{row + 1}: a context capability check through row_contains"
                          for row in sorted(rows[rel]))
    return violations


def check(root: Path) -> int:
    """Run the scan and report.

    Returns:
        0 clean, 1 on a check that does not use a lift or a parse failure
    """
    violations = scan(root)
    for violation in violations:
        print(f"ROW-CONTAINS violation: {violation}", file=sys.stderr)
    if violations:
        print("check-row-contains-discipline: write CtxOwnsCapability<Ctx, Effect::X>, CtxOwnsAnyOf or "
              "CtxOwnsAllOf.  For a check that must stay inline, mark it `// ROW-CONTAINS-OK: <reason>`.",
              file=sys.stderr)
        return 1
    print("check-row-contains-discipline: clean — each context capability check uses a named lift.",
          file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each spelling of a context capability check and each shape that is not one, then check the verdicts.

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

    planted: list[tuple[str, bool | None, str]] = [
        ("#pragma once", None, ""),
        ("namespace crucible::planted {", None, ""),
        ("namespace eff = ::foundation::effects;", None, ""),
        ("template <class Ctx> requires (effects::row_contains(^^effects::row_type_of_t<Ctx>, X)) void a();", True,
         "a qualified check"),
        ("template <class Ctx> requires (eff::row_contains(^^eff::row_type_of_t<Ctx>, X)) void b();", True,
         "a check through an alias of the namespace"),
        ("template <class Ctx> requires (row_contains(^^row_type_of_t<Ctx>, X)) void c();", True, "a bare check"),
        ("template <class Ctx> requires (row_contains(^^typename Ctx::row_type, X)) void d();", True,
         "a check of a member row"),
        ("template <class Ctx>", None, ""),
        ("    requires (effects::row_contains(", True, "a check that spans lines"),
        ("        ^^effects::row_type_of_t<Ctx>, X))", None, ""),
        ("void e();", None, ""),
        ("template <class Ctx> requires (row_contains(^^row_type_of_t<Ctx>, X)) void f();  // ROW-CONTAINS-OK: fix",
         False, "a marked check"),
        ("template <class Ctx> requires (row_contains(^^row_type_of_t<Ctx>, X)) void g();  // ROW-CONTAINS-OK:", True,
         "a marker with no reason"),
        ("static_assert(effects::row_contains(^^required_row, X));", False, "a check of a concrete row"),
        ("template <class Ctx> requires CtxOwnsCapability<Ctx, X> void h();", False, "a named lift"),
        ("// requires effects::row_contains(^^effects::row_type_of_t<Ctx>, X)", False, "a comment"),
        ('inline const char* text = "row_contains(^^row_type_of_t<Ctx>, X)";', False, "a string literal"),
        ("#define OWNS(C, E) effects::row_contains(^^effects::row_type_of_t<C>, E)", True, "a macro body"),
        ("#define HAS(R, E) effects::row_contains(^^R, E)", False, "a macro over a concrete row"),
        ("#define SPLIT_OWNS(C, E) row_contains( /* ctx */ \\", True, "a macro body split by a block comment"),
        ("    ^^row_type_of_t<C>, E)", None, ""),
        ("#define PASTE_ROW(R, E) row_contains(^^R##_row, E)", False, "a pasting macro over a concrete row"),
        ("#define PASTE_OWNS(C, E) row_contains(^^row_type_of_t<C##_ctx>, E)", True,
         "a macro body that pastes tokens, read from its tokens, with a marked definition on the next row"),
        ("#define MARKED_OWNS(C, E) row_contains(^^row_type_of_t<C>, E)  // ROW-CONTAINS-OK: fixture", False,
         "a marked macro"),
        ("template <class Ctx> requires (row_cont\\", True, "a check whose name a line splice cuts in two"),
        ("ains(^^row_type_of_t<Ctx>, X)) void spliced();", None, ""),
        ("}", None, ""),
    ]
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in (
            ("include/crucible/planted/Row.h", "\n".join(line for line, _, _ in planted) + "\n"),
            ("include/foundation/effects/Lifts.h",
             "template <class C, class E> concept O = row_contains(^^row_type_of_t<C>, E);\n"),
            ("src/planted/Row.cpp", "template <class C> requires (row_contains(^^row_type_of_t<C>, X)) void s();\n"),
        ):
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        violations = scan(root)
        reported = {int(v.split(":")[1]) for v in violations if v.startswith("include/crucible/planted/Row.h:")}
        for line, (_, caught, label) in enumerate(planted, start=1):
            if caught is True:
                expect(f"caught: {label}", line in reported)
            elif caught is False:
                expect(f"not caught: {label}", line not in reported, True)
        expect("nothing else in the planted header is reported",
               reported <= {line for line, (_, caught, _) in enumerate(planted, start=1) if caught}, True)
        expect("caught: a check in src/", any(v.startswith("src/planted/Row.cpp") for v in violations))
        expect("not caught: the definitions of the lifts", not any("effects/Lifts.h" in v for v in violations), True)

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

        expect("the planted tree fails the check", captured(root)[0] == 1)
        expect("the report from / equals the report from the scan root", captured(Path("/")) == captured(root))
        (root / "include/crucible/planted/Row.h").unlink()
        (root / "src/planted/Row.cpp").unlink()
        expect("a clean tree passes", captured(root)[0] == 0, True)
        (root / "include/crucible/planted/Broken.h").write_text("void f() { g(1) { } }  // row_contains\n",
                                                                 encoding="utf-8")
        expect("a file the parser cannot read fails the check", captured(root)[0] == 1)
    if failures:
        print(f"check-row-contains-discipline --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-row-contains-discipline --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    if argv not in ([], ["--self-test"]):
        print("usage: check-row-contains-discipline.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-row-contains-discipline: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
