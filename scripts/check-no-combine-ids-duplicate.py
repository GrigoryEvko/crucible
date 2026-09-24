#!/usr/bin/env python3
"""check-no-combine-ids-duplicate — row_hash folds through one combine_ids body, read from the parse tree.

The row_hash machinery folds through one function,
::foundation::reflect::combine_ids in include/foundation/reflect/Hash.h.  A
second body is a drift surface: a change to the salt 0x9e3779b97f4a7c15, to
the shift mix or to the fmix64 finalizer leaves the copy stale, and the
wire-format witness breaks with no failed assert.  The function is
constexpr, so one body serves the compile-time and the run-time fold, and
no runtime copy is necessary.

WHAT COUNTS AS A SECOND BODY
    The guard reads the parse tree of the pinned tree-sitter kit.
      * A function definition whose name contains combine_ids, the exact
        name in any other namespace included.
      * Any other identifier that contains combine_ids with a prefix or a
        suffix, such as combine_ids_runtime, a variable, a lambda or an
        alias.  The exact name in a call or a using-declaration is a use.
      * A function or a lambda, under any name, whose body holds the salt
        0x9e3779b97f4a7c15, a shift left by 6, a shift right by 2 and a
        call of fmix64.  That is the body of combine_ids.
      * The same shapes in a macro body, read as text after the lexer
        blanks comments and literals.
    A comment and a string literal name nothing.

EXEMPTIONS
    The canonical definition, and each path of EXEMPT with its reason.  An
    exempt path that holds no second body is stale, and the check fails
    until the entry is removed.

Exit 0 clean, 1 on a second body or a parse failure, 2 on a stale exemption,
a usage error or a failed self-test, 3 when the kit is not installed.
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

CANONICAL = ("include/foundation/reflect/Hash.h", ("foundation", "reflect"))
# Each path that may hold a second body, with its reason.
EXEMPT: dict[str, str] = {
    "include/crucible/Expr.h": "a frozen copy of the old tree, which the consumer migration moves onto Hash.h",
}
NAME = "combine_ids"
ROOTS = ("include", "src", "test", "bench", "tools", "vessel", "fuzz")
SUFFIXES = (".h", ".hpp", ".cpp", ".cc", ".inl", ".ipp")
EXCLUDED_COMPONENTS = frozenset({"third_party", "external", "vendor"})
NEEDLE = re.compile(rb"combine_ids|9e3779b97f4a7c15", re.IGNORECASE)
SALT = re.compile(r"0x9e3779b97f4a7c15", re.IGNORECASE)
LEXICAL_NAME = re.compile(r"\b(?:\w+combine_ids\w*|combine_ids\w+)\b")
SHIFT_LEFT_6 = re.compile(r"<<\s*6\b")
SHIFT_RIGHT_2 = re.compile(r">>\s*2\b")
FMIX = re.compile(r"\bfmix64\s*\(")


def namespace_path(node: tsast.Node) -> tuple[str, ...]:
    """Return the names of the namespaces that enclose a node, outermost first."""
    parts: list[str] = []
    owner = node.parent
    while owner is not None:
        if owner.type == "namespace_definition":
            named = owner.child_by_field("name")
            text = named.text if named is not None else "(anonymous)"
            parts[:0] = [part for part in text.replace(" ", "").split("::") if part]
        owner = owner.parent
    return tuple(parts)


def function_name(function: tsast.Node) -> tsast.Node | None:
    """Return the name node that a function definition declares, through its nested declarators."""
    current = function.child_by_field("declarator")
    while current is not None and current.type not in ("identifier", "field_identifier", "qualified_identifier",
                                                         "destructor_name", "operator_name"):
        current = current.child_by_field("declarator")
    return current


def is_combine_body(text: str) -> bool:
    """Return True when a body, blanked of comments and literals except numbers, has the shape of combine_ids."""
    return all(pattern.search(text) for pattern in (SALT, SHIFT_LEFT_6, SHIFT_RIGHT_2, FMIX))


def second_bodies(rel: str, tree: tsast.Tree) -> Iterator[tuple[int, str]]:
    """Yield each second body of combine_ids in a parsed file, as (row, form).

    Complexity: linear in the size of the file.
    """
    for function in tree.find("function_definition"):
        named = function_name(function)
        if named is None:
            continue
        last = named.child_by_field("name") if named.type == "qualified_identifier" else named
        text = last.text if last is not None else named.text
        body = function.child_by_field("body")
        if NAME in text:
            if not (rel == CANONICAL[0] and text == NAME and namespace_path(function) == CANONICAL[1]):
                yield function.start[0], f"a definition named {text}"
        elif body is not None and is_combine_body(code_of(body.text)):
            yield function.start[0], f"a body with the shape of combine_ids, named {text}"
    for lambda_node in tree.find("lambda_expression"):
        body = lambda_node.child_by_field("body")
        if body is not None and is_combine_body(code_of(body.text)):
            yield lambda_node.start[0], "a lambda with the shape of combine_ids"
    for node in tree.find("identifier", "field_identifier", "type_identifier", "namespace_identifier"):
        is_definition_name = node.parent is not None and node.parent.type == "function_declarator"
        if NAME in node.text and node.text != NAME and not is_definition_name:
            yield node.start[0], f"the name {node.text}"
    for body in tree.find("preproc_arg"):
        code = code_of(body.text)
        joined, joins = splice(body.text)
        for match in LEXICAL_NAME.finditer(blank(joined, blank_literals=True)[0]):
            yield body.start[0] + line_of(joined, joins, match.start()) - 1, f"the name {match.group(0)} in a macro"
        if is_combine_body(code):
            yield body.start[0], "a macro body with the shape of combine_ids"


def code_of(text: str) -> str:
    """Return text with its comments and its string and character literals blanked, and its numbers kept."""
    joined, _ = splice(text)
    return blank(joined, blank_literals=True)[0]


def scope_files(root: Path) -> list[Path]:
    """Return every C++ file under the scan roots whose bytes name combine_ids or its salt, sorted."""
    found: list[Path] = []
    for top in ROOTS:
        base = root / top
        if base.is_dir():
            for path in base.rglob("*"):
                parts = path.relative_to(root).parts[:-1]
                if path.is_file() and path.suffix in SUFFIXES \
                        and not any(part in EXCLUDED_COMPONENTS or part.startswith("build") for part in parts) \
                        and NEEDLE.search(path.read_bytes()):
                    found.append(path)
    return sorted(found)


def scan(root: Path) -> tuple[list[str], list[str], set[str]]:
    """Find each second body of combine_ids.

    Complexity: linear in the total size of the files in scope.

    Returns:
        Each violation, each parse failure, and each exempt path that held a second body
    """
    violations: list[str] = []
    failures: list[str] = []
    used: set[str] = set()
    for tree in tsast.parse(scope_files(root), strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None and rel not in tsast.UNPARSEABLE:
            failures.append(f"{rel}: the parser cannot read this file. {tree.diagnostic.strip()}")
            continue
        if tree.diagnostic is not None:
            source = tree.source.decode("utf-8", "replace")
            hits = [(line_of(*splice(source), match.start()) - 1, f"the name {match.group(0)}")
                    for match in LEXICAL_NAME.finditer(blank(splice(source)[0], blank_literals=True)[0])]
        else:
            hits = list(second_bodies(rel, tree))
        for row, form in sorted(set(hits)):
            if rel in EXEMPT:
                used.add(rel)
            else:
                violations.append(f"{rel}:{row + 1}: {form}")
    return violations, failures, used


def check(root: Path) -> int:
    """Run the scan and report.

    Returns:
        0 clean, 1 on a second body or a parse failure, 2 on a stale exemption
    """
    violations, failures, used = scan(root)
    for violation in violations:
        print(f"COMBINE-IDS second body: {violation}", file=sys.stderr)
    for failure in failures:
        print(f"COMBINE-IDS parse failure: {failure}", file=sys.stderr)
    stale = sorted(set(EXEMPT) - used)
    for rel in stale:
        print(f"COMBINE-IDS stale exemption: {rel} holds no second body. Remove it from EXEMPT.", file=sys.stderr)
    if violations or failures:
        print("check-no-combine-ids-duplicate: fold through ::foundation::reflect::combine_ids(a, b).  It is "
              "constexpr, so one body serves the compile-time and the run-time fold.", file=sys.stderr)
        return 1
    if stale:
        return 2
    print("check-no-combine-ids-duplicate: clean — one combine_ids body.", file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each shape of a second body and each shape of a use, then check the verdicts.

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

    body = "{ a ^= b + 0x9e3779b97f4a7c15ULL + (a << 6) + (a >> 2); return fmix64(a); }"
    planted: list[tuple[str, bool | None, str]] = [
        ("namespace crucible::planted {", None, ""),
        (f"constexpr unsigned long combine_ids_runtime(unsigned long a, unsigned long b) {body}", True,
         "a suffix name"),
        ("constexpr unsigned long runtime_combine_ids(unsigned long a, unsigned long b) { return a ^ b; }", True,
         "a prefix name"),
        ("constexpr unsigned long combine_ids(unsigned long a, unsigned long b) { return a ^ b; }", True,
         "the exact name in another namespace"),
        (f"constexpr unsigned long mix_pair(unsigned long a, unsigned long b) {body}", True,
         "the body of combine_ids under another name"),
        (f"inline auto lambda_copy = [](unsigned long a, unsigned long b) {body};", True, "a lambda copy"),
        ("inline auto pointer = &::foundation::reflect::combine_ids;", False, "an address of the canonical"),
        ("inline unsigned long caller(unsigned long a) { return ::foundation::reflect::combine_ids(a, a); }", False,
         "a call of the canonical"),
        ("using ::foundation::reflect::combine_ids;", False, "a using-declaration"),
        ("// constexpr unsigned long combine_ids_in_comment(unsigned long a);", False, "a comment"),
        ('inline const char* text = "combine_ids_in_string";', False, "a string literal"),
        ("inline unsigned long digest(unsigned long a) { return a + 0x9e3779b97f4a7c15ULL + (a << 6); }", False,
         "a salt without the full shape"),
        ("#define COMBINE_MACRO(a, b) combine_ids_macro(a, b)", True, "a name in a macro body"),
        ("}", None, ""),
    ]
    canonical = ("#pragma once\nnamespace foundation::reflect {\n"
                 f"constexpr unsigned long combine_ids(unsigned long a, unsigned long b) {body}\n}}\n")
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in (("include/foundation/reflect/Hash.h", canonical),
                          ("src/planted/Planted.cpp", "\n".join(line for line, _, _ in planted) + "\n"),
                          ("include/crucible/Expr.h", canonical.replace("foundation::reflect", "crucible"))):
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        violations, broken, used = scan(root)
        reported = {int(v.split(":")[1]) for v in violations if v.startswith("src/planted/Planted.cpp:")}
        for line, (_, caught, label) in enumerate(planted, start=1):
            if caught is True:
                expect(f"caught: {label}", line in reported)
            elif caught is False:
                expect(f"not caught: {label}", line not in reported, True)
        expect("the canonical definition is not reported",
               not any(v.startswith("include/foundation/reflect/Hash.h") for v in violations), True)
        expect("an exempt path is not reported, and it counts as used",
               not any(v.startswith("include/crucible/Expr.h") for v in violations) and
               "include/crucible/Expr.h" in used, True)
        expect("the planted tree parses", not broken, True)

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
        (root / "src/planted/Planted.cpp").unlink()
        (root / "include/crucible/Expr.h").write_text("#pragma once\n#include <foundation/reflect/Hash.h>\n"
                                                       "// combine_ids now comes from Hash.h.\n", encoding="utf-8")
        code, report = captured(root)
        expect("an exempt path with no second body is stale", code == 2 and "stale exemption" in report)
        (root / "src/planted/Broken.cpp").write_text("void f() { g(1) { } }  // combine_ids\n", encoding="utf-8")
        expect("a file the parser cannot read fails the check", captured(root)[0] == 1)
    if failures:
        print(f"check-no-combine-ids-duplicate --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-no-combine-ids-duplicate --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    if argv not in ([], ["--self-test"]):
        print("usage: check-no-combine-ids-duplicate.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-no-combine-ids-duplicate: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
