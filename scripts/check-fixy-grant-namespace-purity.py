#!/usr/bin/env python3
"""check-fixy-grant-namespace-purity — only the grant catalogs open namespace crucible::fixy::grant.

`crucible::fixy::grant` is the closed authoring namespace of every shipped grant
tag.  C++ has no access control on a namespace: a file that reopens it, or that
declares a name into it from outside, can register a foreign type as a
`which_dim<T>` specialization and thread it through the engagement check.

THE RULE
    A C++ file does not:
      * define a namespace whose full name reaches crucible::fixy::grant, in
        any spelling: `namespace crucible::fixy::grant {`, the nested form
        `namespace crucible { namespace fixy { namespace grant {`, or an
        inline namespace on the way
      * declare a name into crucible::fixy::grant from outside it: a class,
        a function or a variable whose declared name is qualified into the
        namespace, as `template <> struct crucible::fixy::grant::which_dim<T>`,
        also through an enclosing namespace or a namespace alias
    Exempt:
      * the authoring catalogs, listed in AUTHORING below
      * the attack fixtures, cheat probes and per-domain negative fixtures that
        exercise this residual gap on purpose.  Each one must carry a comment
        that holds the words `known residual gap`
    A file under misc/ is out of scope.  So is a file that git does not
    track: the export of a guard run under the tree holds a copy of each
    catalog at a different path, and it can disappear while the guard runs.

WHAT READS THE CODE
    The parse tree of the pinned tree-sitter kit (scripts/tsast.py).  A
    comment or a string that spells the namespace holds no namespace node, so
    it counts for nothing.  The acknowledgement must be a comment node.

Usage
    check-fixy-grant-namespace-purity.py              scan the tree
    check-fixy-grant-namespace-purity.py --self-test  plant each case and examine each verdict

Exit 0 clean, 1 on a violation or a file that does not parse, 2 on a usage
error or a failed self-test, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import fnmatch
import io
import os
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import throwaway_repo  # noqa: E402  (the path insert above has to come first)
import tsast  # noqa: E402

GRANT = ("crucible", "fixy", "grant")
ACK = "known residual gap"
AUTHORING = frozenset({
    "include/crucible/fixy/_Grant.h",
    "include/crucible/fixy/grant/_Ctrl.h",
    "include/crucible/fixy/_Fs.h",
    "include/crucible/fixy/_Mmap.h",
    "include/crucible/fixy/_Io.h",
    "include/crucible/fixy/_Hw.h",
    "include/crucible/fixy/_Time.h",
    "include/crucible/fixy/_Sched.h",
})
ACKNOWLEDGED = (
    "test/safety_attack/attack_fixy_grant_*.cpp",
    "test/test_fixy_cheat_probe.cpp",
    "test/test_fixy_cheat_probe_theory.cpp",
    "test/fixy_neg/neg_fixy_project_per_domain_*.cpp",
)
OUT_OF_SCOPE = ("misc/",)
DECLARED_NAME_PARENTS = {
    "struct_specifier": "name", "class_specifier": "name", "union_specifier": "name", "enum_specifier": "name",
    "function_declarator": "declarator", "init_declarator": "declarator", "declaration": "declarator",
}


def holds_grant(parts: tuple[str, ...]) -> bool:
    """Report whether a namespace path from the root lies in crucible::fixy::grant.

    Args:
        parts: A namespace path from the root, inline namespaces left out

    Returns:
        True when the path starts with crucible, fixy, grant
    """
    return parts[:len(GRANT)] == GRANT


def definition_path(node: tsast.Node) -> tuple[str, ...]:
    """Return the full path that one namespace definition opens, inline namespaces left out.

    An inline namespace is transparent to lookup, so `namespace crucible {
    inline namespace v1 { namespace fixy::grant {` counts as the grant
    namespace.

    Args:
        node: A namespace_definition

    Returns:
        The path of the namespace body, which namespace_path() reads with the
        definition itself as its innermost owner
    """
    body = node.child_by_field("body")
    return tsast.namespace_path(body if body is not None else node, skip_inline=True)


def declared_scope(name: tsast.Node) -> tuple[bool, tuple[str, ...]] | None:
    """Return the scope parts of a qualified declared name, or None for a plain name.

    Args:
        name: The qualified_identifier that a declaration declares

    Returns:
        Whether the name starts at `::`, and the parts before the declared name
    """
    spelled = tsast.qualified_parts(name)
    if spelled is None or len(spelled[1]) < 2:
        return None
    return spelled[0], spelled[1][:-1]


def violations_in(tree: tsast.Tree) -> list[tuple[int, str]]:
    """Return each line of a parsed file that opens or reaches into the grant namespace.

    Complexity: linear in the node count of the file, times the alias chain length.

    Args:
        tree: The parsed file

    Returns:
        (line, what) pairs in line order
    """
    found: set[tuple[int, str]] = set()
    for node in tree.find("namespace_definition"):
        if holds_grant(definition_path(node)) and not holds_grant(tsast.namespace_path(node, skip_inline=True)):
            found.add((node.line, "namespace reopen"))
    aliases = tsast.namespace_aliases(tree)
    for name in tree.find("qualified_identifier"):
        parent = name.parent
        declared_field = DECLARED_NAME_PARENTS.get(parent.type) if parent is not None else None
        if declared_field is None or name.field != declared_field:
            continue
        scope = declared_scope(name)
        if scope is None:
            continue
        from_root, parts = scope
        enclosing = tsast.namespace_path(name, skip_inline=True)
        if holds_grant(enclosing):
            continue
        # A relative scope can resolve in any enclosing namespace, so each
        # prefix of the enclosing path is a candidate.
        resolved = tsast.resolve_namespace(parts, name, aliases, is_global=from_root)
        candidates = [resolved] if from_root else [enclosing[:depth] + resolved
                                                   for depth in range(len(enclosing), -1, -1)]
        if any(holds_grant(candidate) for candidate in candidates):
            found.add((name.line, "declaration into the namespace"))
    return sorted(found)


def acknowledged(tree: tsast.Tree) -> bool:
    """Report whether a comment node of the file holds the acknowledgement words."""
    return any(ACK in tsast.prose_text(node) for node in tree.find("comment"))


def candidate_files(root: Path) -> list[str]:
    """Return the C++ files that git tracks under the root, relative to it and sorted (tsast.tracked_files).

    An untracked file is out of scope.  The export of a guard run under the
    tree holds a copy of each catalog at a different path, and another tool
    can remove an untracked file while this guard reads it.

    Complexity: linear in the file count of the tree.
    """
    return [path for path in tsast.tracked_files(root) if tsast.is_in_cpp_scope(path) and (root / path).is_file()
            and not path.startswith(OUT_OF_SCOPE)]


def check(root: Path) -> int:
    """Scan the tree and report.

    Args:
        root: The repository root

    Returns:
        0 clean, 1 on a violation or a parse failure
    """
    status = 0
    files = candidate_files(root)
    for tree in tsast.parse([root / path for path in files], strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            print(f"fixy_grant_purity: parse failure: {rel} — the parser cannot read it, so its namespaces are "
                  f"unknown.\n  {tree.diagnostic}", file=sys.stderr)
            status = 1
            continue
        found = violations_in(tree)
        if not found or rel in AUTHORING:
            continue
        if any(fnmatch.fnmatchcase(rel, pattern) for pattern in ACKNOWLEDGED):
            if not acknowledged(tree):
                print(f"fixy_grant_purity: {rel} missing acknowledgement comment — add a comment that holds "
                      f"\"{ACK}\" near the reopen.", file=sys.stderr)
                status = 1
            continue
        for line, what in found:
            print(f"fixy_grant_purity: forbidden {what} at {rel}:{line}", file=sys.stderr)
        status = 1
    if status:
        print("fixy_grant_purity: only the grant authoring catalogs can open or declare into namespace "
              "crucible::fixy::grant.  An attack fixture that exercises the gap on purpose must carry a "
              f"comment that holds \"{ACK}\".", file=sys.stderr)
        return status
    print(f"fixy_grant_purity: clean — {len(files)} C++ file(s), and only the grant catalogs open or declare "
          f"into crucible::fixy::grant.", file=sys.stderr)
    return status


def self_test() -> int:
    """Plant each case and examine each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, ok: bool) -> None:
        """Record one case result and print it."""
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    cases: list[tuple[str, str, list[int]]] = [
        ("a nested-name reopen is caught", "namespace crucible::fixy::grant {\nstruct evil {};\n}\n", [1]),
        ("a nested reopen is caught at the definition that reaches the namespace",
         "namespace crucible { namespace fixy {\nnamespace grant {\nstruct evil {};\n} } }\n", [2]),
        ("a brace on the next line is caught", "namespace crucible::fixy::grant\n{\nstruct evil {};\n}\n", [1]),
        ("an inline namespace on the way is caught",
         "namespace crucible::fixy { inline namespace v1 {} }\nnamespace crucible::fixy::grant { int x; }\n", [2]),
        ("a qualified specialization at global scope is caught",
         "template <> struct crucible::fixy::grant::which_dim<int> {};\n", [1]),
        ("a specialization through an enclosing namespace is caught",
         "namespace crucible { template <> struct fixy::grant::which_dim<long> {}; }\n", [1]),
        ("a specialization through a namespace alias is caught",
         "namespace cf = crucible::fixy;\ntemplate <> struct cf::grant::which_dim<char> {};\n", [2]),
        ("a namespace nested inside the grant namespace is one reopen",
         "namespace crucible::fixy::grant {\nnamespace hw {\nstruct evil {};\n}\n}\n", [1]),
        ("the reopen text in a comment or a string is clean",
         "// namespace crucible::fixy::grant { is closed\nconst char* note = \"namespace crucible::fixy::grant {\";\n",
         []),
        ("a sibling namespace is clean", "namespace crucible::fixy::atom { struct fine {}; }\n", []),
        ("an inline namespace between crucible and fixy is transparent, so the reopen is caught",
         "namespace crucible {\ninline namespace v1 {\nnamespace fixy::grant { int x; }\n}\n}\n", [3]),
        ("a specialization that resolves in an outer enclosing namespace is caught",
         "namespace crucible::cntp {\ntemplate <> struct fixy::grant::which_dim<short> {};\n}\n", [2]),
        ("a grant namespace nested inside another namespace is a different namespace",
         "namespace other::crucible::fixy::grant { struct fine {}; }\n", []),
        ("a use of a grant name in a using-declaration, a base clause or an expression is clean",
         "using ::crucible::fixy::grant::kAxes;\nstruct mock final : ::crucible::fixy::grant::grant_base {};\n"
         "bool engaged = ::crucible::fixy::grant::which_dim_v<int> == 0;\n", []),
    ]
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        planted = root / "src/planted.cpp"
        planted.parent.mkdir(parents=True)
        for name, text, lines in cases:
            planted.write_text(text)
            tree = next(tsast.parse([planted], strict=False))
            got = sorted({line for line, _ in violations_in(tree)})
            expect(name, tree.diagnostic is None and got == lines)
            if got != lines:
                print(f"       expected lines {lines}, got {got}")
        planted.unlink()

        def plant(rel: str, text: str) -> None:
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text)

        def captured() -> tuple[int, str]:
            buffer = io.StringIO()
            with contextlib.redirect_stderr(buffer):
                code = check(root)
            return code, buffer.getvalue()

        reopen = "namespace crucible::fixy::grant {\nstruct planted {};\n}\n"
        plant("include/crucible/fixy/_Grant.h", reopen)
        plant("include/crucible/fixy/_Io.h", reopen)
        plant("test/safety_attack/attack_fixy_grant_ack.cpp", "// known residual gap: planted\n" + reopen)
        plant("misc/planted_misc.h", reopen)
        code, report = captured()
        expect("the catalogs, an acknowledged fixture and misc/ are exempt", code == 0 and "clean" in report)
        plant("test/safety_attack/attack_fixy_grant_noack.cpp", reopen)
        code, report = captured()
        expect("an attack fixture without the acknowledgement fails",
               code == 1 and "attack_fixy_grant_noack.cpp missing acknowledgement" in report)
        plant("test/safety_attack/attack_fixy_grant_noack.cpp",
              'const char* note = "known residual gap";\n' + reopen)
        code, report = captured()
        expect("the acknowledgement in a string literal counts for nothing",
               code == 1 and "attack_fixy_grant_noack.cpp missing acknowledgement" in report)
        (root / "test/safety_attack/attack_fixy_grant_noack.cpp").unlink()
        plant("src/Foreign.cpp", reopen)
        code, report = captured()
        expect("a foreign reopen fails and names the line", code == 1 and "reopen at src/Foreign.cpp:1" in report)
        previous = Path.cwd()
        os.chdir("/")
        try:
            from_slash = captured()
        finally:
            os.chdir(previous)
        expect("the report from / equals the report from the repository", from_slash == captured())
        plant("src/Foreign.cpp", "namespace broken { void f() { g(1) { } } }\n")
        code, report = captured()
        expect("a file the parser cannot read fails", code == 1 and "parse failure" in report)

    # In a work tree, an untracked file is out of scope: a guard-run export
    # holds a catalog copy at a path outside AUTHORING.
    with tempfile.TemporaryDirectory() as work:
        root = Path(work).resolve()
        throwaway_repo.init(root)
        (root / "include/crucible/fixy").mkdir(parents=True)
        (root / "include/crucible/fixy/_Grant.h").write_text("namespace crucible::fixy::grant { struct tag {}; }\n")
        subprocess.run(["git", "-C", str(root), "add", "-A"], check=True, capture_output=True)
        (root / "grun/change/include/crucible/fixy").mkdir(parents=True)
        (root / "grun/change/include/crucible/fixy/_Grant.h").write_text(
            "namespace crucible::fixy::grant { struct tag {}; }\n")
        buffer = io.StringIO()
        with contextlib.redirect_stderr(buffer):
            code = check(root)
        expect("an untracked copy of a catalog is out of scope", code == 0 and "clean" in buffer.getvalue())
        subprocess.run(["git", "-C", str(root), "add", "-A"], check=True, capture_output=True)
        buffer = io.StringIO()
        with contextlib.redirect_stderr(buffer):
            code = check(root)
        expect("the same copy fails once git tracks it",
               code == 1 and "reopen at grun/change/include/crucible/fixy/_Grant.h:1" in buffer.getvalue())

    if failures:
        print(f"check-fixy-grant-namespace-purity --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print("check-fixy-grant-namespace-purity --self-test: every case passes.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode.

    Args:
        argv: The arguments after the script name

    Returns:
        The exit code
    """
    try:
        if argv == []:
            return check(tsast.REPO_ROOT)
        if argv == ["--self-test"]:
            return self_test()
    except tsast.KitMissing as exc:
        print(f"fixy_grant_purity: {exc}", file=sys.stderr)
        return 3
    print("usage: check-fixy-grant-namespace-purity.py [--self-test]", file=sys.stderr)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
