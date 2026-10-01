#!/usr/bin/env python3
"""check-lock-free-asserts — each cross-thread atomic has a sibling lock-free assert, read from the parse tree.

CLAUDE.md §IX: each `std::atomic<T>` in a cross-thread substrate header has a
sibling `static_assert(std::atomic<T>::is_always_lock_free)`.  When
std::atomic<T> is not lock-free on a target, libatomic puts a mutex behind
it with no warning, and a single acquire or release becomes a lock of
200-300 ns.  The assert costs nothing at run time and fails the build on
that target.

A header holds no static_assert at namespace scope
(utils/scripts/check-header-checks.py), so the sibling assert of a header
lives in its check file, test/layer/checks/<layer>/<path>.cpp for
include/<layer>/<path>.h.  One translation unit of each build compiles the
check file, so the assert still fails that build on such a target.

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
    A static_assert of the file, or of the check file of the file, whose
    condition holds `<atomic of T>::is_always_lock_free`, alone or in a
    conjunction, with the same spellings as above.  One assert covers each
    atomic of the same type in the file.  Two types match when their
    spellings from the parse tree match after a leading `std::` or `::std::`
    is removed, so white space and comments inside a type do not matter.  A
    check file that the parser cannot read is a violation.

WHAT THE PARSER CANNOT READ
    The type of an atomic in a macro body depends on the arguments of the
    macro.  The guard reads the preprocessing tokens of each body and
    reports each `atomic <` or `atomic_ref <` in it, because a macro can
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
import sys
import tempfile
from collections.abc import Iterator
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402

SCOPE = ("include/crucible/canopy", "include/crucible/cntp", "include/crucible/topology",
         "include/crucible/warden")
INCLUDE = "include"
CHECKS = "test/layer/checks"
ATOMIC_TEMPLATES = frozenset({"atomic", "atomic_ref"})
# The standard aliases of std::atomic<T>, as alias name to T, spelled as
# tsast.spelled spells a type: one space between two words.
ATOMIC_ALIASES = {
    "atomic_bool": "bool", "atomic_char": "char", "atomic_schar": "signed char", "atomic_uchar": "unsigned char",
    "atomic_short": "short", "atomic_ushort": "unsigned short", "atomic_int": "int", "atomic_uint": "unsigned int",
    "atomic_long": "long", "atomic_ulong": "unsigned long", "atomic_llong": "long long",
    "atomic_ullong": "unsigned long long", "atomic_char8_t": "char8_t", "atomic_char16_t": "char16_t",
    "atomic_char32_t": "char32_t", "atomic_wchar_t": "wchar_t", "atomic_int8_t": "int8_t",
    "atomic_uint8_t": "uint8_t", "atomic_int16_t": "int16_t", "atomic_uint16_t": "uint16_t",
    "atomic_int32_t": "int32_t", "atomic_uint32_t": "uint32_t", "atomic_int64_t": "int64_t",
    "atomic_uint64_t": "uint64_t", "atomic_intptr_t": "intptr_t", "atomic_uintptr_t": "uintptr_t",
    "atomic_size_t": "size_t", "atomic_ptrdiff_t": "ptrdiff_t", "atomic_intmax_t": "intmax_t",
    "atomic_uintmax_t": "uintmax_t",
}
LOCK_FREE_BY_DEFINITION = frozenset({"atomic_flag", "atomic_signed_lock_free", "atomic_unsigned_lock_free"})
MARKER = "LOCK-FREE-OK:"


def normalize(node: tsast.Node) -> str:
    """Return the spelling of a type from its tokens, without a leading std qualifier."""
    cleaned = tsast.spelled(node)
    for prefix in ("::std::", "std::"):
        cleaned = cleaned.removeprefix(prefix)
    return cleaned


def has_marker_reason(comment: tsast.Node) -> bool:
    """Return True when a comment holds the marker followed by a reason that is not empty."""
    text = comment.text
    at = text.find(MARKER)
    return at >= 0 and text[at + len(MARKER):].strip(" \t*/\n") != ""


def qualifiers_before(node: tsast.Node) -> list[str]:
    """Return the scope names that stand before a name or a scope in its qualified chain, outermost first.

    Each scope is read as its leaf name, so a template scope gives its
    template name and a decltype scope gives `?`.
    """
    prefix: list[str] = []
    parent = node.parent
    if parent is None or parent.type != "qualified_identifier":
        return prefix
    if node.field == "name" and (scope := parent.child_by_field("scope")) is not None:
        prefix.insert(0, tsast.leaf_name(scope) or "?")
    current = parent
    while current.field == "name" and current.parent is not None and current.parent.type == "qualified_identifier":
        scope = current.parent.child_by_field("scope")
        if scope is not None:
            prefix.insert(0, tsast.leaf_name(scope) or "?")
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
        return normalize(inner[0]) if len(inner) == 1 else None
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


def macro_atomics(body: tsast.MacroBody) -> Iterator[int]:
    """Yield the row of each `atomic <` or `atomic_ref <` in the tokens of one macro body.

    A comment and a string literal give no identifier token, so neither can
    match.

    Complexity: linear in the number of tokens of the body.
    """
    tokens = tsast.pp_tokens(body.text, body.first_row)
    for index, token in enumerate(tokens[:-1]):
        if token.kind == "identifier" and token.text in ATOMIC_TEMPLATES and tokens[index + 1].text == "<":
            yield token.row


def missing_in(tree: tsast.Tree, bodies: list[tsast.MacroBody],
               checked: frozenset[str] = frozenset()) -> Iterator[tuple[int, str]]:
    """Yield each atomic spelling of a parsed file whose type has no sibling assert, as (row, type).

    Complexity: linear in the number of nodes of the file.

    Args:
        tree: The parsed file
        bodies: The macro bodies of the file
        checked: The types that the check file of the file asserts
    """
    covered = asserted_types(tree) | checked
    marked = {node.start[0] for node in tree.find("comment") if has_marker_reason(node)}
    for node in tree.find("template_type", "type_identifier"):
        if node.ancestor_of_type("static_assert_declaration") is not None:
            continue
        value = atomic_type(node)
        if value is None or value in covered:
            continue
        if any(row in marked for row in declaration_rows(node)):
            continue
        yield node.start[0], value
    for body in bodies:
        for row in macro_atomics(body):
            if row not in marked:
                yield row, "an atomic in a macro body"


def scope_files(root: Path) -> list[Path]:
    """Return the C++ files of the scope, sorted."""
    found: list[Path] = []
    for top in SCOPE:
        base = root / top
        if base.is_dir():
            found.extend(path for path in base.rglob("*")
                         if path.is_file() and path.name.endswith(tsast.CPP_SUFFIXES))
    return sorted(found)


def check_file_of(root: Path, path: Path) -> Path:
    """Return the check file of a header of the scope.

    Args:
        root: The scan root
        path: A file under include/

    Returns:
        test/layer/checks/<layer>/<path>.cpp for include/<layer>/<path>.h
    """
    inner = path.relative_to(root / INCLUDE)
    return root / CHECKS / inner.with_suffix(".cpp")


def scan(root: Path) -> list[str]:
    """Find each atomic of the scope that has no sibling lock-free assert.

    Complexity: linear in the total size of the files in scope and of their check files.
    """
    violations: list[str] = []
    trees: list[tsast.Tree] = []
    files = scope_files(root)
    for tree in tsast.parse(files, strict=False):
        if tree.diagnostic is not None:
            rel = Path(tree.path).relative_to(root).as_posix()
            violations.append(f"{rel}: the parser cannot read this file. {tree.diagnostic.strip()}")
            continue
        trees.append(tree)
    checked: dict[Path, frozenset[str]] = {}
    check_files = [check_file_of(root, path) for path in files if check_file_of(root, path).is_file()]
    for tree in tsast.parse(check_files, strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            violations.append(f"{rel}: the parser cannot read this check file. {tree.diagnostic.strip()}")
            continue
        checked[Path(tree.path)] = frozenset(asserted_types(tree))
    bodies: dict[int, list[tsast.MacroBody]] = {}
    for body in tsast.macro_bodies(trees):
        bodies.setdefault(id(body.define.tree), []).append(body)
    for tree in trees:
        rel = Path(tree.path).relative_to(root).as_posix()
        from_check_file = checked.get(check_file_of(root, Path(tree.path)), frozenset())
        for row, value in sorted(set(missing_in(tree, bodies.get(id(tree), []), from_check_file))):
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
        print("check-lock-free-asserts: add static_assert(std::atomic<T>::is_always_lock_free, \"...\") to the "
              f"check file of the header, {CHECKS}/<layer>/<path>.cpp, one per type and header.  For an atomic "
              "that is not cross-thread, mark the declaration `// LOCK-FREE-OK: <reason>`.", file=sys.stderr)
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
        ("    std::atomic<Twin</*k*/int, int>> commented_arg_{};", False,
         "a comment inside a template argument of a covered type"),
        ("    std::atomic<float> block_marked_{0};  /* LOCK-FREE-OK: */", True,
         "a block comment marker with no reason"),
        ("    std::atomic<unsigned int> spelled_{0};", False, "a type covered through the spelling of its alias"),
        ("    std::atomic<long long> foreign_assert_{0};", True, "a type whose only assert names another atomic"),
        ("};", None, ""),
        ("#define ATOMIC_IN_MACRO(T) std::atomic<T> macro_field_", True, "a macro body"),
        ("#define SPLIT_MACRO(T) std::atomic /* a comment */ <T> split_field_", True,
         "a macro body that a block comment splits"),
        ("static_assert(std::atomic<std::uint64_t>::is_always_lock_free, \"covered\");", False, "the assert itself"),
        ("static_assert(::std::atomic<std::uint32_t>::is_always_lock_free && sizeof(int) == 4);", None, ""),
        ("static_assert(std::atomic_int64_t::is_always_lock_free);", None, ""),
        ("static_assert(std::atomic<Twin<int, int>>::is_always_lock_free);", None, ""),
        ("static_assert(std::atomic_uint::is_always_lock_free);", None, ""),
        ("static_assert(other::atomic<long long>::is_always_lock_free);", None, ""),
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

        # The assert of a header lives in its check file.
        header.write_text("#pragma once\nstruct Bar { std::atomic<int> head_{0}; std::atomic<long> tail_{0}; };\n",
                          encoding="utf-8")
        check_file = root / CHECKS / "crucible/canopy/Planted.cpp"
        check_file.parent.mkdir(parents=True)
        check_file.write_text("#include <crucible/canopy/Planted.h>\n"
                              "static_assert(std::atomic<int>::is_always_lock_free);\n", encoding="utf-8")
        reported = {violation for violation in scan(root) if violation.startswith("include/crucible/canopy/")}
        expect("an assert in the check file covers its type in the header",
               not any("std::atomic<int>" in violation for violation in reported), True)
        expect("an assert in the check file does not cover another type",
               any("std::atomic<long>" in violation for violation in reported))
        (root / "include/crucible/canopy/Other.h").write_text("#pragma once\nstd::atomic<int> other_{0};\n",
                                                               encoding="utf-8")
        expect("the check file of one header does not cover another header",
               any(violation.startswith("include/crucible/canopy/Other.h:") for violation in scan(root)))
        (root / "include/crucible/canopy/Other.h").unlink()
        check_file.write_text("#include <crucible/canopy/Planted.h>\n"
                              "static_assert(std::atomic<int>::is_always_lock_free);\n"
                              "static_assert(std::atomic<long>::is_always_lock_free);\n", encoding="utf-8")
        expect("a header whose check file asserts each type passes", captured(root)[0] == 0, True)
        check_file.write_text("void f() { g(1) { } }\n", encoding="utf-8")
        expect("a check file the parser cannot read fails the check", captured(root)[0] == 1)
        check_file.unlink()
        header.write_text("#pragma once\nstruct Bar { std::atomic<int> head_{0}; };\n"
                          "static_assert(std::atomic<int>::is_always_lock_free);\n", encoding="utf-8")
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
