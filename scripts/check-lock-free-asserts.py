#!/usr/bin/env python3
"""check-lock-free-asserts — each cross-thread atomic has a sibling lock-free assert, read from the parse tree.

CLAUDE.md §IX: each `std::atomic<T>` in a cross-thread substrate header has a
sibling `static_assert(std::atomic<T>::is_always_lock_free)` in the same
file.  When std::atomic<T> is not lock-free on a target, libatomic puts a
mutex behind it with no warning, and a single acquire or release becomes a
lock of 200-300 ns.  The assert costs nothing at run time and fails the
build on that target.

SCOPE
    include/crucible/{canopy,cntp,topology,warden}/, where each atomic is
    cross-thread by design.  Tighter-scoped atomics elsewhere, such as the
    SPSC ring and the one-shot flag, follow their own discipline.

WHAT COUNTS AS AN ATOMIC
    The guard reads the parse tree of the pinned tree-sitter kit.  An atomic
    is a template-id named atomic or atomic_ref, bare or qualified by std,
    with any spacing, and with template arguments that nest.  A standard
    alias such as std::atomic_uint64_t counts as the atomic of its type.
    std::atomic_flag and the two lock-free aliases of the standard are
    lock-free by definition, so they need no assert.

WHAT COUNTS AS AN ASSERT
    A static_assert of the file whose condition holds
    `<atomic of T>::is_always_lock_free`, alone or in a conjunction, with
    the same spellings as above.  One assert covers each atomic of the same
    type in the file.  Two types match when their spellings match after
    whitespace and a leading `std::` or `::std::` are removed.

WHAT THE PARSER CANNOT READ
    A macro body is raw text.  The guard lexes it with scripts/cxx_lex.py
    and reports each `atomic<` or `atomic_ref<` in it, because a macro can
    declare an atomic that the parser does not see.  A parse error in a
    file of the scope is a violation.

EXEMPTION
    `// LOCK-FREE-OK: <reason>` on a line of the declaration exempts it.
    The reason must not be empty.

Exit 0 clean, 1 on a missing assert or a parse failure, 2 on a usage error or
a failed self-test, 3 when the kit is not installed.
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

SCOPE = ("include/crucible/canopy", "include/crucible/cntp", "include/crucible/topology",
         "include/crucible/warden")
SUFFIXES = (".h", ".hpp")
ATOMIC_TEMPLATES = frozenset({"atomic", "atomic_ref"})
# The standard aliases of std::atomic<T>, as alias name to T.
ATOMIC_ALIASES = {
    "atomic_bool": "bool", "atomic_char": "char", "atomic_schar": "signedchar", "atomic_uchar": "unsignedchar",
    "atomic_short": "short", "atomic_ushort": "unsignedshort", "atomic_int": "int", "atomic_uint": "unsignedint",
    "atomic_long": "long", "atomic_ulong": "unsignedlong", "atomic_llong": "longlong",
    "atomic_ullong": "unsignedlonglong", "atomic_char8_t": "char8_t", "atomic_char16_t": "char16_t",
    "atomic_char32_t": "char32_t", "atomic_wchar_t": "wchar_t", "atomic_int8_t": "int8_t",
    "atomic_uint8_t": "uint8_t", "atomic_int16_t": "int16_t", "atomic_uint16_t": "uint16_t",
    "atomic_int32_t": "int32_t", "atomic_uint32_t": "uint32_t", "atomic_int64_t": "int64_t",
    "atomic_uint64_t": "uint64_t", "atomic_intptr_t": "intptr_t", "atomic_uintptr_t": "uintptr_t",
    "atomic_size_t": "size_t", "atomic_ptrdiff_t": "ptrdiff_t", "atomic_intmax_t": "intmax_t",
    "atomic_uintmax_t": "uintmax_t",
}
LOCK_FREE_BY_DEFINITION = frozenset({"atomic_flag", "atomic_signed_lock_free", "atomic_unsigned_lock_free"})
MARKER = re.compile(r"LOCK-FREE-OK:\s*\S")
LEXICAL = re.compile(r"\batomic(?:_ref)?\s*<")


def normalize(text: str) -> str:
    """Return a type spelling without whitespace and without a leading std qualifier."""
    cleaned = re.sub(r"\s+", "", text)
    for prefix in ("::std::", "std::"):
        cleaned = cleaned.removeprefix(prefix)
    return cleaned


def qualifiers_before(node: tsast.Node) -> list[str]:
    """Return the scope names that stand before a name or a scope in its qualified chain, outermost first."""
    prefix: list[str] = []
    parent = node.parent
    if parent is None or parent.type != "qualified_identifier":
        return prefix
    if node.field == "name" and (scope := parent.child_by_field("scope")) is not None:
        prefix.insert(0, scope.text.strip())
    current = parent
    while current.field == "name" and current.parent is not None and current.parent.type == "qualified_identifier":
        scope = current.parent.child_by_field("scope")
        if scope is not None:
            prefix.insert(0, scope.text.strip())
        current = current.parent
    return prefix


def std_or_bare(node: tsast.Node) -> bool:
    """Return True when a name is bare, or qualified by std or ::std only."""
    return qualifiers_before(node) in ([], ["std"])


def atomic_type(node: tsast.Node) -> str | None:
    """Return the normalized value type of an atomic spelling, or None when the node is not an atomic.

    Args:
        node: A template_type or a type_identifier
    """
    if node.type == "template_type":
        named = node.child_by_field("name")
        arguments = node.child_by_field("arguments")
        if named is None or named.text not in ATOMIC_TEMPLATES or arguments is None or not std_or_bare(node):
            return None
        inner = [child for child in arguments.children if child.type != "comment"]
        return normalize(inner[0].text) if len(inner) == 1 else None
    if node.type in ("type_identifier", "namespace_identifier") and node.text in ATOMIC_ALIASES \
            and std_or_bare(node):
        return ATOMIC_ALIASES[node.text]
    return None


def asserted_types(tree: tsast.Tree) -> set[str]:
    """Return the value type of each `<atomic of T>::is_always_lock_free` inside a static_assert of the file."""
    found: set[str] = set()
    for node in tree.find("static_assert_declaration"):
        for name in node.descendants("qualified_identifier"):
            last = name.child_by_field("name")
            scope = name.child_by_field("scope")
            if last is not None and last.text == "is_always_lock_free" and scope is not None:
                value = atomic_type(scope)
                if value is not None:
                    found.add(value)
    return found


def declaration_rows(node: tsast.Node) -> range:
    """Return the rows of the declaration that holds an atomic spelling, for a marker."""
    holder = node.ancestor_of_type("field_declaration", "declaration", "alias_declaration", "parameter_declaration",
                                   "type_definition", "template_declaration")
    top = holder if holder is not None else node
    return range(top.start[0], top.end[0] + 1)


def missing_in(tree: tsast.Tree) -> Iterator[tuple[int, str]]:
    """Yield each atomic spelling of a parsed file whose type has no sibling assert, as (row, type).

    Complexity: linear in the number of nodes of the file.
    """
    covered = asserted_types(tree)
    marked = {node.start[0] for node in tree.find("comment") if MARKER.search(node.text)}
    for node in tree.find("template_type", "type_identifier"):
        if node.ancestor_of_type("static_assert_declaration") is not None:
            continue
        value = atomic_type(node)
        if value is None or value in covered:
            continue
        if any(row in marked for row in declaration_rows(node)):
            continue
        yield node.start[0], value
    for body in tree.find("preproc_arg"):
        joined, joins = splice(body.text)
        code, _ = blank(joined, blank_literals=True)
        for match in LEXICAL.finditer(code):
            row = body.start[0] + line_of(joined, joins, match.start()) - 1
            if row not in marked:
                yield row, "an atomic in a macro body"


def scope_files(root: Path) -> list[Path]:
    """Return the headers of the scope, sorted."""
    found: list[Path] = []
    for top in SCOPE:
        base = root / top
        if base.is_dir():
            found.extend(path for path in base.rglob("*") if path.is_file() and path.suffix in SUFFIXES)
    return sorted(found)


def scan(root: Path) -> list[str]:
    """Find each atomic of the scope that has no sibling lock-free assert.

    Complexity: linear in the total size of the headers in scope.
    """
    violations: list[str] = []
    for tree in tsast.parse(scope_files(root), strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            violations.append(f"{rel}: the parser cannot read this file. {tree.diagnostic.strip()}")
            continue
        for row, value in sorted(set(missing_in(tree))):
            violations.append(f"{rel}:{row + 1}: std::atomic<{value}> has no sibling "
                              f"static_assert(std::atomic<{value}>::is_always_lock_free) in the file.")
    return violations


def check(root: Path) -> int:
    """Run the scan and report.

    Returns:
        0 clean, 1 on a missing assert or a parse failure
    """
    violations = scan(root)
    for violation in violations:
        print(f"LOCK-FREE-MISSING: {violation}", file=sys.stderr)
    if violations:
        print("check-lock-free-asserts: add static_assert(std::atomic<T>::is_always_lock_free, \"...\") beside "
              "the declarations of that type, one per type and file.  For an atomic that is not cross-thread, "
              "mark the declaration `// LOCK-FREE-OK: <reason>`.", file=sys.stderr)
        return 1
    print("check-lock-free-asserts: clean — each atomic in canopy, cntp, topology and warden has a sibling "
          "lock-free assert.", file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each atomic spelling and each way to cover it, then check the verdicts.

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
        ("#include <atomic>", None, ""),
        ("namespace crucible::planted {", None, ""),
        ("using std::atomic;", None, ""),
        ("struct Foo {", None, ""),
        ("    std::atomic<std::uint64_t> covered_{0};", False, "a type that has its assert"),
        ("    std::atomic<int> missing_{0};", True, "a plain atomic"),
        ("    std :: atomic < short > spaced_{0};", True, "an atomic with spaces"),
        ("    atomic<long> bare_{0};", True, "an atomic after a using-declaration"),
        ("    std::atomic<Pair<int, int>> nested_{};", True, "an atomic of a template type"),
        ("    std::atomic_uint16_t alias_{0};", True, "a standard alias"),
        ("    std::atomic_ref<unsigned> reference_;", True, "an atomic_ref"),
        ("    std::atomic<char> marked_{0};  // LOCK-FREE-OK: fixture", False, "a marked declaration"),
        ("    std::atomic<wchar_t> bare_marker_{0};  // LOCK-FREE-OK:", True, "a marker with no reason"),
        ("    std::atomic<char16_t> wrapped_{", False, "a marker on the last line of a declaration"),
        ("        0};  // LOCK-FREE-OK: fixture", None, ""),
        ("    std::atomic_flag flag_;", False, "atomic_flag"),
        ("    std::atomic_signed_lock_free lock_free_{0};", False, "a lock-free alias of the standard"),
        ("    std::atomic<std::uint32_t> both_{0};", False, "a type covered by a conjunction"),
        ("    std::atomic_int64_t covered_alias_{0};", False, "a standard alias covered by the atomic spelling"),
        ("    other::atomic<float> foreign_{};", False, "an atomic of another namespace"),
        ("    // std::atomic<double> in_comment_;", False, "a comment"),
        ('    const char* text = "std::atomic<double>";', False, "a string literal"),
        ("};", None, ""),
        ("#define ATOMIC_IN_MACRO(T) std::atomic<T> macro_field_", True, "a macro body"),
        ("static_assert(std::atomic<std::uint64_t>::is_always_lock_free, \"covered\");", False, "the assert itself"),
        ("static_assert(::std::atomic<std::uint32_t>::is_always_lock_free && sizeof(int) == 4);", None, ""),
        ("static_assert(std::atomic_int64_t::is_always_lock_free);", None, ""),
        ("}", None, ""),
    ]
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        header = root / "include/crucible/canopy/Planted.h"
        header.parent.mkdir(parents=True)
        header.write_text("\n".join(line for line, _, _ in planted) + "\n", encoding="utf-8")
        (root / "include/crucible/mimic").mkdir(parents=True)
        (root / "include/crucible/mimic/Out.h").write_text("std::atomic<int> out_of_scope_;\n", encoding="utf-8")
        reported = {int(violation.split(":")[1]) for violation in scan(root)
                    if violation.startswith("include/crucible/canopy/Planted.h:")}
        for line, (_, caught, label) in enumerate(planted, start=1):
            if caught is True:
                expect(f"caught: {label}", line in reported)
            elif caught is False:
                expect(f"not caught: {label}", line not in reported, True)
        expect("nothing else in the planted header is reported",
               reported <= {line for line, (_, caught, _) in enumerate(planted, start=1) if caught}, True)
        expect("a header outside the scope is not read",
               not any(v.startswith("include/crucible/mimic") for v in scan(root)), True)

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
        header.write_text("#pragma once\nstruct Bar { std::atomic<int> head_{0}; };\n"
                          "static_assert(std::atomic<int>::is_always_lock_free);\n", encoding="utf-8")
        expect("a covered tree passes", captured(root)[0] == 0, True)
        (root / "include/crucible/warden/Broken.h").parent.mkdir(parents=True)
        (root / "include/crucible/warden/Broken.h").write_text("void f() { g(1) { } }\n", encoding="utf-8")
        expect("a file the parser cannot read fails the check", captured(root)[0] == 1)
    if failures:
        print(f"check-lock-free-asserts --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-lock-free-asserts --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    if argv not in ([], ["--self-test"]):
        print("usage: check-lock-free-asserts.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-lock-free-asserts: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
