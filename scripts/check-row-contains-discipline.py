#!/usr/bin/env python3
"""check-row-contains-discipline — a context capability check uses a named lift, read from the parse tree.

A check that a context owns an effect is written with a named lift:

    row_contains_v<row_type_of_t<Ctx>, Effect::X>        ->  CtxOwnsCapability<Ctx, Effect::X>
    row_contains_v<..., X> || row_contains_v<..., Y>     ->  CtxOwnsAnyOf<Ctx, Effect::X, Effect::Y>
    row_contains_v<..., X> && row_contains_v<..., Y>     ->  CtxOwnsAllOf<Ctx, Effect::X, Effect::Y>

`grep CtxOwns` then finds each capability admission, and a rename of the
row accessor reaches every check through the lift.

WHAT COUNTS AS A CONTEXT CAPABILITY CHECK
    The guard reads the parse tree of the pinned tree-sitter kit.  A check is
    a template-id named row_contains_v, bare or with any qualifier, so an
    alias of the effects namespace and a using-directive do not hide it,
    whose first template argument reads the row of a context:
    row_type_of_t<...>, or a member row_type such as `typename Ctx::row_type`.
    A membership check on a concrete row, such as a required row that a
    static_assert names, is not a context capability check.  The same shape
    in a macro body is read as text after the lexer blanks comments and
    literals.

SCOPE
    include/ and src/, without the definitions of the lifts
    (include/foundation/effects/ and include/crucible/effects/) and without
    the frozen paths of scripts/frozen-paths.txt, which cannot change.

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
from cxx_lex import blank, line_of, splice  # noqa: E402

ROOTS = ("include", "src")
SUFFIXES = (".h", ".hpp", ".cpp", ".cc", ".inl", ".ipp")
EXCLUDED_PREFIXES = ("include/foundation/effects/", "include/crucible/effects/")
EXCLUDED_COMPONENTS = frozenset({"test", "bench", "examples", "third_party", "external", "vendor"})
FROZEN = "scripts/frozen-paths.txt"
NAME = "row_contains_v"
CONTEXT_ROW = re.compile(r"\brow_type_of_t\b|::\s*row_type\b")
MARKER = re.compile(r"ROW-CONTAINS-OK:\s*\S")
LEXICAL = re.compile(r"\brow_contains_v\s*<\s*(?:typename\s+)?(?P<first>[^,<>]*(?:<[^<>]*>)?[^,<>]*)")
STATEMENTS = ("declaration", "field_declaration", "alias_declaration", "requires_clause", "condition_clause",
              "expression_statement", "return_statement", "static_assert_declaration", "template_declaration",
              "concept_definition")


def frozen_prefixes(root: Path) -> tuple[str, ...]:
    """Return the frozen path prefixes, or none when the list is absent."""
    listed = root / FROZEN
    if not listed.is_file():
        return ()
    return tuple(line.strip() for line in listed.read_text(encoding="utf-8").splitlines()
                 if line.strip() and not line.lstrip().startswith("#"))


def scope_files(root: Path) -> list[Path]:
    """Return the C++ files in scope that name row_contains_v, sorted."""
    frozen = frozen_prefixes(root)
    found: list[Path] = []
    for top in ROOTS:
        base = root / top
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            rel = path.relative_to(root)
            posix = rel.as_posix()
            if path.is_file() and path.suffix in SUFFIXES and not posix.startswith(EXCLUDED_PREFIXES + frozen) \
                    and not any(part in EXCLUDED_COMPONENTS or part.startswith("build") for part in rel.parts[:-1]) \
                    and NAME.encode() in path.read_bytes():
                found.append(path)
    return sorted(found)


def first_argument(node: tsast.Node) -> tsast.Node | None:
    """Return the first template argument of a template-id, or None."""
    arguments = node.child_by_field("arguments")
    if arguments is None:
        return None
    inner = [child for child in arguments.children if child.type != "comment"]
    return inner[0] if inner else None


def statement_rows(node: tsast.Node) -> range:
    """Return the rows of a check and of the statement that holds it, for a marker."""
    holder = node.ancestor_of_type(*STATEMENTS)
    last = max(node.end[0], holder.end[0]) if holder is not None else node.end[0]
    first = min(node.start[0], holder.start[0]) if holder is not None else node.start[0]
    return range(first, last + 1)


def unlifted_checks(tree: tsast.Tree) -> Iterator[int]:
    """Yield the zero-based row of each context capability check that does not use a lift.

    Complexity: linear in the number of nodes of the file.
    """
    marked = {node.start[0] for node in tree.find("comment") if MARKER.search(node.text)}
    for node in tree.find("template_type", "template_function"):
        named = node.child_by_field("name")
        argument = first_argument(node)
        if named is None or named.text != NAME or argument is None or not CONTEXT_ROW.search(argument.text):
            continue
        if not any(row in marked for row in statement_rows(node)):
            yield node.start[0]
    for body in tree.find("preproc_arg"):
        joined, joins = splice(body.text)
        code, _ = blank(joined, blank_literals=True)
        for match in LEXICAL.finditer(code):
            row = body.start[0] + line_of(joined, joins, match.start()) - 1
            if CONTEXT_ROW.search(match.group("first")) and row not in marked:
                yield row


def scan(root: Path) -> list[str]:
    """Find each context capability check in scope that does not use a lift.

    Complexity: linear in the total size of the files in scope.
    """
    violations: list[str] = []
    for tree in tsast.parse(scope_files(root), strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            if rel not in tsast.UNPARSEABLE:
                violations.append(f"{rel}: the parser cannot read this file. {tree.diagnostic.strip()}")
            continue
        violations.extend(f"{rel}:{row + 1}: a context capability check through row_contains_v"
                          for row in sorted(set(unlifted_checks(tree))))
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
        ("template <class Ctx> requires effects::row_contains_v<effects::row_type_of_t<Ctx>, X> void a();", True,
         "a qualified check"),
        ("template <class Ctx> requires eff::row_contains_v<eff::row_type_of_t<Ctx>, X> void b();", True,
         "a check through an alias of the namespace"),
        ("template <class Ctx> requires row_contains_v<row_type_of_t<Ctx>, X> void c();", True, "a bare check"),
        ("template <class Ctx> requires row_contains_v<typename Ctx::row_type, X> void d();", True,
         "a check of a member row"),
        ("template <class Ctx>", None, ""),
        ("    requires effects::row_contains_v<", True, "a check that spans lines"),
        ("        effects::row_type_of_t<Ctx>, X>", None, ""),
        ("void e();", None, ""),
        ("template <class Ctx> requires row_contains_v<row_type_of_t<Ctx>, X> void f();  // ROW-CONTAINS-OK: fixture",
         False, "a marked check"),
        ("template <class Ctx> requires row_contains_v<row_type_of_t<Ctx>, X> void g();  // ROW-CONTAINS-OK:", True,
         "a marker with no reason"),
        ("static_assert(effects::row_contains_v<required_row, X>);", False, "a check of a concrete row"),
        ("template <class Ctx> requires CtxOwnsCapability<Ctx, X> void h();", False, "a named lift"),
        ("// requires effects::row_contains_v<effects::row_type_of_t<Ctx>, X>", False, "a comment"),
        ('inline const char* text = "row_contains_v<row_type_of_t<Ctx>, X>";', False, "a string literal"),
        ("#define OWNS(C, E) effects::row_contains_v<effects::row_type_of_t<C>, E>", True, "a macro body"),
        ("#define HAS(R, E) effects::row_contains_v<R, E>", False, "a macro over a concrete row"),
        ("}", None, ""),
    ]
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in (
            ("include/crucible/planted/Row.h", "\n".join(line for line, _, _ in planted) + "\n"),
            ("include/foundation/effects/Lifts.h",
             "template <class C, class E> concept O = row_contains_v<row_type_of_t<C>, E>;\n"),
            ("include/crucible/frozen/Old.h", "template <class C> requires row_contains_v<row_type_of_t<C>, X> "
                                              "void old();\n"),
            (FROZEN, "include/crucible/frozen/\n"),
            ("src/planted/Row.cpp", "template <class C> requires row_contains_v<row_type_of_t<C>, X> void s();\n"),
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
        expect("not caught: a frozen path", not any("frozen/Old.h" in v for v in violations), True)

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
        (root / "include/crucible/planted/Broken.h").write_text("void f() { g(1) { } }  // row_contains_v\n",
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
