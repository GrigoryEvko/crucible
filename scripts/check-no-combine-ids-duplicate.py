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
        call of fmix64.  That is the body of combine_ids.  The salt and the
        shift counts are read as number values, so a digit separator or a
        suffix does not hide them.
      * The same shapes in a macro body.  A body that parses is read as
        nodes.  A body that does not parse, for example because it pastes
        tokens with `##`, is read as preprocessing tokens.
    A comment and a string literal name nothing.  Every C++ file under the
    scan roots is parsed, with no text test first.

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
import sys
import tempfile
from collections.abc import Iterator
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402

CANONICAL = ("include/foundation/reflect/Hash.h", ("foundation", "reflect"))
# Each path that may hold a second body, with its reason.
EXEMPT: dict[str, str] = {
    "include/crucible/Expr.h": "the copy in crucible::detail that the runtime headers still fold through",
}
NAME = "combine_ids"
ROOTS = ("include", "src", "test", "bench", "tools", "vessel")
EXCLUDED_COMPONENTS = frozenset({"third_party", "external", "vendor"})
SALT = 0x9E3779B97F4A7C15
MIX = "fmix64"


def body_shape(node: tsast.Node) -> bool:
    """Return True when a function or lambda body has the shape of combine_ids.

    The shape is the salt as a number value, a shift left by 6, a shift right
    by 2 and a call of fmix64, in any order.  A shift is a binary or a compound
    assignment expression whose right operand has the value.

    Complexity: linear in the number of nodes under the body.
    """
    has_salt = any(tsast.number_value(literal) == SALT for literal in node.descendants("number_literal"))
    shifts: set[tuple[str, int | None]] = set()
    for expression in node.descendants("binary_expression", "assignment_expression"):
        right = expression.child_by_field("right")
        if right is not None:
            shifts.add((tsast.operator_of(expression).rstrip("="), tsast.number_value(right)))
    has_mix = any(tsast.leaf_name(call) == MIX for call in node.descendants("call_expression"))
    return has_salt and ("<<", 6) in shifts and (">>", 2) in shifts and has_mix


def token_value(text: str) -> int | None:
    """Return the value of an integer literal token, or None for any other token.

    The rule is tsast.number_value's rule, for a token of a macro body that
    did not parse: digit separators, the base prefixes and the integer
    suffixes are handled.
    """
    digits = text.replace("'", "").lower()
    digits = digits.rstrip("ulz")
    try:
        if digits[:2] == "0x":
            return int(digits[2:], 16)
        if digits[:2] == "0b":
            return int(digits[2:], 2)
        if len(digits) > 1 and digits[0] == "0" and digits.isdigit():
            return int(digits, 8)
        return int(digits, 10)
    except ValueError:
        return None


def token_shape(tokens: list[tsast.Token]) -> bool:
    """Return True when the tokens of an unparsed macro body have the shape of combine_ids."""
    values = [token_value(token.text) if token.kind == "number" else None for token in tokens]
    has_salt = SALT in values
    shifts = {(tsast.lexeme(tokens[index]).rstrip("="), values[index + 1]) for index in range(len(tokens) - 1)
              if tokens[index].text in ("<<", "<<=", ">>", ">>=")}
    has_mix = any(token.text == MIX and index + 1 < len(tokens) and tokens[index + 1].text == "("
                  for index, token in enumerate(tokens))
    return has_salt and ("<<", 6) in shifts and (">>", 2) in shifts and has_mix


def is_derived_name(text: str) -> bool:
    """Return True for a name that holds combine_ids with a prefix or a suffix."""
    return NAME in text and text != NAME


def second_bodies(rel: str, tree: tsast.Tree) -> Iterator[tuple[int, str]]:
    """Yield each second body of combine_ids in a parsed file, as (row, form).

    Complexity: linear in the size of the file.
    """
    for function in tree.find("function_definition"):
        declarator = function.child_by_field("declarator")
        text = (tsast.leaf_name(declarator) if declarator is not None else None) or ""
        body = function.child_by_field("body")
        if NAME in text:
            if not (rel == CANONICAL[0] and text == NAME and tsast.namespace_path(function) == CANONICAL[1]):
                yield function.start[0], f"a definition named {text}"
        elif body is not None and body_shape(body):
            yield function.start[0], f"a body with the shape of combine_ids, named {text}"
    for lambda_node in tree.find("lambda_expression"):
        body = lambda_node.child_by_field("body")
        if body is not None and body_shape(body):
            yield lambda_node.start[0], "a lambda with the shape of combine_ids"
    for node in tree.find("identifier", "field_identifier", "type_identifier", "namespace_identifier"):
        is_definition_name = node.parent is not None and node.parent.type == "function_declarator"
        name = tsast.leaf_name(node) or ""
        if is_derived_name(name) and not is_definition_name:
            yield node.start[0], f"the name {name}"


def macro_hits(body: tsast.MacroBody) -> Iterator[tuple[int, str]]:
    """Yield each derived name and each combine_ids shape in one macro body, as (row, form).

    A macro parameter is a name of the macro, not of the program, so it does
    not count.
    """
    tokens = tsast.pp_tokens(body.text, body.first_row)
    for token in tokens:
        if token.kind == "identifier" and is_derived_name(token.text) and token.text not in body.params:
            yield token.row, f"the name {token.text} in a macro"
    shaped = body_shape(body.root) if body.is_parsed else token_shape(tokens)
    if shaped:
        yield body.first_row, "a macro body with the shape of combine_ids"


def scope_files(root: Path) -> list[Path]:
    """Return every C++ file under the scan roots, sorted.

    A file of the UNPARSEABLE roster is not C++, so it is out of scope.
    """
    found: list[Path] = []
    for top in ROOTS:
        base = root / top
        if base.is_dir():
            for path in base.rglob("*"):
                rel = path.relative_to(root)
                if path.is_file() and path.name.endswith(tsast.CPP_SUFFIXES) \
                        and tsast.is_in_cpp_scope(rel) \
                        and not any(part in EXCLUDED_COMPONENTS or part.startswith("build")
                                    for part in rel.parts[:-1]):
                    found.append(path)
    return sorted(found)


# The files whose trees stay alive at one time, so the macro bodies of a
# batch parse in one run and the memory of the scan stays bounded.
BATCH = 256


def scan(root: Path) -> tuple[list[str], list[str], set[str]]:
    """Find each second body of combine_ids.

    Complexity: linear in the total size of the files in scope.  The trees
    are kept for one batch of BATCH files at a time.

    Returns:
        Each violation, each parse failure, and each exempt path that held a second body
    """
    violations: list[str] = []
    failures: list[str] = []
    used: set[str] = set()
    batch: list[tuple[str, tsast.Tree, list[tuple[int, str]]]] = []

    def flush() -> None:
        """Add the macro-body hits of the batch, then report each file of it."""
        by_tree = {id(tree): found for _, tree, found in batch}
        for body in tsast.macro_bodies([tree for _, tree, _ in batch]):
            by_tree[id(body.define.tree)].extend(macro_hits(body))
        for rel, _, found in batch:
            for row, form in sorted(set(found)):
                if rel in EXEMPT:
                    used.add(rel)
                else:
                    violations.append(f"{rel}:{row + 1}: {form}")
        batch.clear()

    for tree in tsast.parse(scope_files(root), strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            failures.append(f"{rel}: the parser cannot read this file. {tree.diagnostic.strip()}")
            continue
        batch.append((rel, tree, list(second_bodies(rel, tree))))
        if len(batch) == BATCH:
            flush()
    flush()
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
        ("inline unsigned long split_salt(unsigned long a) { a ^= 0x9e37'79b9'7f4a'7c15ULL + (a << 6U) + "
         "(a >> 2U); return fmix64(a); }", True, "a body with a separated salt and suffixed shift counts"),
        ("#define SEPARATED_MACRO(a) ((a) ^ 0x9e37'79b9'7f4a'7c15ULL + ((a) << 6) + ((a) >> 2) + fmix64(a))",
         True, "a macro body with a separated salt"),
        ("#define PASTED_MACRO(a) a##_mix ^ 0x9e37'79b9'7f4a'7c15ULL + ((a) << 6) + ((a) >> 2) + fmix64(a)",
         True, "a macro body that pastes tokens, so it does not parse, with a separated salt"),
        ("inline unsigned long six_shift(unsigned long a) { a ^= 0x9e3779b97f4a7c15ULL + (a << 16) + (a >> 2); "
         "return fmix64(a); }", False, "a shift by 16, which is not a shift by 6"),
        ("}", None, ""),
    ]
    canonical = ("#pragma once\nnamespace foundation::reflect {\n"
                 f"constexpr unsigned long combine_ids(unsigned long a, unsigned long b) {body}\n}}\n")
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        separated = ("namespace crucible::planted {\n"
                     "inline unsigned long alone(unsigned long a) { a ^= 0x9e37'79b9'7f4a'7c15ULL + (a << 6) + "
                     "(a >> 2); return fmix64(a); }\n}\n")
        for rel, text in (("include/foundation/reflect/Hash.h", canonical),
                          ("src/planted/Planted.cpp", "\n".join(line for line, _, _ in planted) + "\n"),
                          ("src/planted/Separated.cpp", separated),
                          ("include/crucible/Expr.h", canonical.replace("foundation::reflect", "crucible"))):
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        violations, broken, used = scan(root)
        reported = {int(v.split(":")[1]) for v in violations if v.startswith("src/planted/Planted.cpp:")}
        expect("caught: a file whose only trace is a separated salt",
               any(v.startswith("src/planted/Separated.cpp:2") for v in violations))
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
        (root / "src/planted/Separated.cpp").unlink()
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
