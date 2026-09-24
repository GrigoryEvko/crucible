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

WHAT THE PARSER CANNOT READ
    A macro body is raw text.  The guard lexes it with scripts/cxx_lex.py,
    which drops comments and literals, and reports each `struct` or `class`
    head and each trait variable name with template arguments in it.  A file
    in tsast.UNPARSEABLE gets the same lexical scan over its whole text.  A
    parse error in any other file is a guard failure.

AUTHORING LOCATIONS
    include/crucible/permissions/*.h, include/foundation/permissions/*.h,
    include/crucible/concurrent/*.h, include/fixy/concurrent/*.h,
    include/crucible/safety/PermissionTreeGenerator.h,
    include/crucible/safety/PermissionGridGenerator.h,
    include/fixy/OwnedRegion.h, and every file under test/.

Exit 0 clean, 1 on an orphan specialization or a parse failure, 2 on a usage
error or a failed self-test, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import sys
import tempfile
from collections.abc import Iterator
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402
from cxx_lex import blank, line_of, splice  # noqa: E402

TRAITS = frozenset({
    "can_split_into", "can_split_into_pack", "has_split_authoring_witness", "has_split_pack_authoring_witness",
    "splits_into", "splits_into_pack", "splits_into_authoring_witness", "splits_into_pack_authoring_witness",
})
VARIABLES = frozenset({name + "_v" for name in TRAITS} | {"well_authored_split_v", "well_authored_split_pack_v"})
# A header directly in one of these directories is an authoring location.
AUTHORING_DIRS = frozenset({
    "include/crucible/permissions", "include/foundation/permissions",
    "include/crucible/concurrent", "include/fixy/concurrent",
})
AUTHORING_FILES = frozenset({
    "include/crucible/safety/PermissionTreeGenerator.h", "include/crucible/safety/PermissionGridGenerator.h",
    "include/fixy/OwnedRegion.h",
})
ROOTS = ("include", "src", "vessel", "bench", "tools", "fuzz", "examples", "test")
SUFFIXES = (".h", ".hpp", ".cpp", ".cc", ".inl", ".ipp")
LEXICAL = re.compile(r"\b(?:struct|class)\s+(?:::\s*)?(?:\w+\s*::\s*)*(?P<trait>\w+)\s*<"
                     r"|\b(?P<variable>\w+)\s*<")


def authored_at(rel: str) -> bool:
    """Return True when a repo-relative path is an authoring location.

    Args:
        rel: The path relative to the scan root

    Returns:
        Whether the file may specialize a split trait
    """
    path = PurePosixPath(rel)
    return (rel.startswith("test/") or rel in AUTHORING_FILES
            or (path.suffix == ".h" and str(path.parent) in AUTHORING_DIRS))


def final_name(node: tsast.Node | None) -> tuple[str, bool] | None:
    """Return the last name of a declared name and whether it carries template arguments.

    Args:
        node: A name node: an identifier, a template-id or a qualified name

    Returns:
        The name and True for a template-id, or None for another shape
    """
    while node is not None:
        if node.type in ("type_identifier", "identifier", "field_identifier"):
            return node.text, False
        if node.type in ("template_type", "template_function"):
            name = node.child_by_field("name")
            return (name.text, True) if name is not None else None
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


def ast_rows(tree: tsast.Tree) -> Iterator[int]:
    """Yield the zero-based row of each split-trait specialization in a parsed file.

    Args:
        tree: One parsed file

    Yields:
        The row of each specialization
    """
    for node in tree.find("struct_specifier", "class_specifier"):
        found = final_name(node.child_by_field("name"))
        if found is not None and found[1] and found[0] in TRAITS:
            yield node.start[0]
    for node in tree.find("declaration"):
        for declarator in node.children:
            if declarator.field != "declarator":
                continue
            found = final_name(declared_name(declarator))
            if found is not None and found[1] and found[0] in VARIABLES:
                yield declarator.start[0]


def lexical_rows(text: str) -> Iterator[int]:
    """Yield the zero-based row of each specialization shape in raw text, after the lexer blanks it.

    Args:
        text: C++ text that the parser does not read, such as a macro body

    Yields:
        The row of each hit, in the text
    """
    joined, joins = splice(text)
    code, _ = blank(joined, blank_literals=True)
    for match in LEXICAL.finditer(code):
        if match.group("trait") in TRAITS or match.group("variable") in VARIABLES:
            yield line_of(joined, joins, match.start()) - 1


def scope_files(root: Path) -> list[Path]:
    """Return every C++ file under the scan roots that is not an authoring location, sorted."""
    found: list[Path] = []
    for top in ROOTS:
        base = root / top
        if base.is_dir():
            for path in base.rglob("*"):
                rel = path.relative_to(root).as_posix()
                if path.is_file() and path.suffix in SUFFIXES and not authored_at(rel) \
                        and not any(part.startswith("build") for part in path.relative_to(root).parts):
                    found.append(path)
    return sorted(found)


def scan(root: Path) -> tuple[list[tuple[str, int, str]], list[str]]:
    """Find every split-trait specialization outside the authoring locations.

    Complexity: linear in the total size of the files in scope.

    Args:
        root: The scan root

    Returns:
        Each orphan as (path, line, line text), and each parse failure
    """
    orphans: list[tuple[str, int, str]] = []
    failures: list[str] = []
    for tree in tsast.parse(scope_files(root), strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        source = tree.source.decode("utf-8", "replace")
        if tree.diagnostic is not None and rel not in tsast.UNPARSEABLE:
            failures.append(f"{rel}: the parser cannot read this file. {tree.diagnostic.strip()}")
            continue
        if tree.diagnostic is not None:
            rows = set(lexical_rows(source))
        else:
            rows = set(ast_rows(tree))
            for body in tree.find("preproc_arg"):
                rows.update(body.start[0] + row for row in lexical_rows(body.text))
        lines = source.split("\n")
        orphans.extend((rel, row + 1, lines[row].strip()) for row in sorted(rows))
    return orphans, failures


def check(root: Path) -> int:
    """Run the scan and report.

    Args:
        root: The scan root

    Returns:
        0 clean, 1 on an orphan or a parse failure
    """
    orphans, failures = scan(root)
    for rel, line, text in orphans:
        print(f"splits_into_orphan: forbidden specialization at {rel}:{line}\n  {text}", file=sys.stderr)
    for failure in failures:
        print(f"splits_into_orphan: parse failure: {failure}", file=sys.stderr)
    if orphans or failures:
        print("splits_into_orphan: a split trait is specialized only beside its tags, in "
              "include/{crucible,foundation}/permissions/, include/{crucible,fixy}/concurrent/, the two "
              "permission generators, include/fixy/OwnedRegion.h or test/ (CLAUDE.md §IX).", file=sys.stderr)
        return 1
    print("check-splits-orphan: clean — no split trait is specialized outside its authoring locations.",
          file=sys.stderr)
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
        "namespace crucible { struct P {}; struct L {}; struct R {}; }\n"            # 1
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
    )
    expected = {4, 7, 10, 11, 12, 13, 14}
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in (
            ("src/planted/Forge.cpp", planted),
            ("include/crucible/concurrent/Channel.h", "template <> struct splits_into<P, L, R> {};\n"),
            ("include/fixy/concurrent/Channel.h",
             "template <> struct foundation::permissions::can_split_into<P, L, R> {};\n"),
            ("include/fixy/concurrent/deeper/Channel.h", "template <> struct splits_into<P, L, R> {};\n"),
            ("test/fixy/Local.cpp", "template <> struct splits_into<P, L, R> {};\n"),
        ):
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        orphans, broken = scan(root)
        lines = {line for rel, line, _ in orphans if rel == "src/planted/Forge.cpp"}
        for line, label in ((4, "an unqualified full specialization"),
                            (7, "a specialization whose head spans three lines"),
                            (10, "a specialization qualified from the global namespace"),
                            (11, "a partial specialization"),
                            (12, "a specialization declared with no body"),
                            (13, "a specialization of a trait variable"),
                            (14, "a specialization inside a macro body")):
            expect(f"caught: {label}", line in lines)
        expect("nothing else in the planted file is reported", lines <= expected, True)
        for line, label in ((15, "a line comment"), (16, "a block comment"), (17, "a string literal"),
                            (18, "a different template name"), (19, "a member with a trait's name")):
            expect(f"not caught: {label}", line not in lines, True)
        exempt = {rel for rel, _, _ in orphans}
        expect("the old concurrent tree is an authoring location",
               "include/crucible/concurrent/Channel.h" not in exempt, True)
        expect("the new concurrent tree is an authoring location",
               "include/fixy/concurrent/Channel.h" not in exempt, True)
        expect("a subdirectory of an authoring directory is not", "include/fixy/concurrent/deeper/Channel.h" in exempt)
        expect("test/ is an authoring location", "test/fixy/Local.cpp" not in exempt, True)
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
        (root / "src/planted/Broken.cpp").write_text("void f() { g(1) { } }\n", encoding="utf-8")
        with contextlib.redirect_stderr(io.StringIO()):
            code = check(root)
        expect("a file the parser cannot read fails the check", code == 1 and bool(scan(root)[1]), True)
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
