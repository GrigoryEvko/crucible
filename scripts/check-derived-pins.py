#!/usr/bin/env python3
"""check-derived-pins — a cardinality pin must have a derived operand, read from the parse tree.

The pattern this guard refuses:

    inline constexpr int surface_cardinality = 28;
    static_assert(surface_cardinality == 28, "the count and the list must move together");

The constant and the literal are written by the same hand in the same edit, so
the assertion compares a number with itself and can never fire.  A pin is
real only when one side is derived: a reflection query
(`enumerators_of(^^E).size()`), a `tuple_size_v`, an array's `.size()` or a
`sizeof`, so that a real edit moves one side and not the other.

WHAT COUNTS AS A PIN
    The guard reads the parse tree of the pinned tree-sitter kit.  A pin is an
    `==` comparison inside a static_assert, in a conjunction too, between a
    name and an integer literal, in either order.  The pin is vacuous when
    the name reaches a constexpr variable whose initializer is the same
    integer.  The name reaches the declaration of its innermost enclosing
    scope, as the compiler looks it up: a namespace, a class or a function
    body, above or below, at any distance.  A name that a template parameter
    binds reaches no constant.  An initializer and a literal count as the
    same integer through a suffix, a digit separator, a base prefix,
    parentheses, a static_cast, a braced initializer and a builtin-type
    conversion.  A different literal, a derived operand, a call and a
    comparison outside a static_assert are not pins.  A floor or a ceiling
    (`>=`, `<=`) that equals the constant is a bound, such as the upper limit
    of an API, and not a copy, so it is not read.

WHAT THE PARSER CANNOT READ
    A pin inside a macro body is not read.  A parse error in a file of the
    scope is a violation, unless scripts/tsast.py lists the file as
    unparseable.

SCOPE
    include/, src/, test/, bench/, tools/ and vessel/, without the
    negative-compile fixtures of test/*_neg/.

THE ALLOWLIST
    scripts/derived-pins-allowlist.txt admits a pin by `path:NAME — reason`.
    The key names a constant, not a line.  A row with no reason, a duplicate
    row and a row that matches no live pin fail the check.

Exit 0 clean, 1 on a vacuous pin or a parse failure, 2 on a malformed or stale
row, a usage error or a failed self-test, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import sys
import tempfile
from collections import defaultdict
from collections.abc import Iterator
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402

ROOTS = ("include", "src", "test", "bench", "tools", "vessel")
ALLOWLIST = "scripts/derived-pins-allowlist.txt"
FIXTURE_DIR = re.compile(r"^test/[^/]+_neg/")
COMPARISONS = frozenset({"=="})
SEPARATOR = " — "


def literal_of(node: tsast.Node | None) -> int | None:
    """Return the integer that an expression spells as a literal, through parentheses and casts, or None."""
    while node is not None:
        if node.type == "number_literal":
            return tsast.number_value(node)
        inner = [child for child in node.children if child.type != "comment"]
        if node.type in ("parenthesized_expression", "initializer_list", "argument_list") and len(inner) == 1:
            node = inner[0]
        elif node.type == "compound_literal_expression":
            node = node.child_by_field("value")
        elif node.type == "call_expression":
            # Only a spelling that is a conversion for certain: a builtin type
            # or static_cast.  `T(12)` with a class or a function name is a call.
            callee = node.child_by_field("function")
            is_cast = callee is not None and (callee.type == "primitive_type"
                                              or (callee.type == "template_function"
                                                  and callee.child_by_field("name") is not None
                                                  and callee.child_by_field("name").text == "static_cast"))
            node = node.child_by_field("arguments") if is_cast else None
        else:
            return None
    return None


def scope_of(node: tsast.Node) -> tuple[str, ...]:
    """Return the scopes that enclose a node, outermost first: namespaces, classes and function bodies.

    A function body is named by the row of its definition, so two functions
    never share a scope.
    """
    parts: list[str] = []
    owner = node.parent
    while owner is not None:
        if owner.type == "namespace_definition":
            named = owner.child_by_field("name")
            if named is None:
                parts.insert(0, "(anonymous)")
            elif named.type == "namespace_identifier":
                parts.insert(0, named.text)
            else:
                parts[:0] = [segment.text for segment in named.descendants("namespace_identifier")]
        elif owner.type in ("class_specifier", "struct_specifier", "union_specifier") \
                and owner.child_by_field("body") is not None:
            # A specialization reads as its template name, and a class
            # defined through a qualifier reads as each part of the qualifier.
            named = owner.child_by_field("name")
            spelled = tsast.qualified_parts(named) if named is not None else None
            parts[:0] = list(spelled[1]) if spelled is not None else [f"(class@{owner.start[0]})"]
        elif owner.type in ("function_definition", "lambda_expression"):
            parts.insert(0, f"(function@{owner.start[0]})")
        owner = owner.parent
    return tuple(parts)


def parameter_names(parameter: tsast.Node) -> Iterator[str]:
    """Yield the names that one template parameter binds."""
    if parameter.type in ("type_parameter_declaration", "variadic_type_parameter_declaration"):
        yield from (child.text for child in parameter.children_of_type("type_identifier"))
    elif parameter.type == "optional_type_parameter_declaration":
        named = parameter.child_by_field("name")
        if named is not None:
            yield named.text
    else:
        declarator = parameter.child_by_field("declarator")
        if declarator is not None:
            if declarator.type == "identifier":
                yield declarator.text
            yield from (child.text for child in declarator.descendants("identifier"))


def bound_names(node: tsast.Node) -> set[str]:
    """Return the names that the template parameter lists around a node bind."""
    names: set[str] = set()
    owner = node.ancestor_of_type("template_declaration")
    while owner is not None:
        listed = owner.child_by_field("parameters")
        for parameter in listed.children if listed is not None else ():
            names.update(parameter_names(parameter))
        owner = owner.ancestor_of_type("template_declaration")
    return names


def literal_constants(tree: tsast.Tree) -> dict[tuple[tuple[str, ...], str], set[int]]:
    """Return each constexpr variable of the file whose initializer is an integer literal, by scope and name."""
    found: dict[tuple[tuple[str, ...], str], set[int]] = defaultdict(set)
    for node in tree.find("declaration", "field_declaration"):
        if not any(child.text == "constexpr" for child in node.children
                   if child.type in ("type_qualifier", "storage_class_specifier")):
            continue
        for declarator in node.children:
            if declarator.type == "init_declarator":
                named = declarator.child_by_field("declarator")
                value = literal_of(declarator.child_by_field("value"))
            elif declarator.field == "declarator" and node.type == "field_declaration":
                named = declarator
                value = literal_of(node.child_by_field("default_value"))
            else:
                continue
            if named is not None and named.type in ("identifier", "field_identifier") and value is not None:
                found[(scope_of(node), named.text)].add(value)
    return found


def spelled_name(node: tsast.Node | None) -> tuple[bool, list[str], str] | None:
    """Return (written from `::`, the qualifier, the name) of an identifier or a qualified name, or None."""
    while node is not None and node.type == "parenthesized_expression":
        inner = [child for child in node.children if child.type != "comment"]
        node = inner[0] if len(inner) == 1 else None
    if node is None:
        return None
    if node.type == "identifier":
        return False, [], node.text
    if node.type != "qualified_identifier":
        return None
    is_global = False
    qualifier: list[str] = []
    current: tsast.Node | None = node
    while current is not None and current.type == "qualified_identifier":
        scope = current.child_by_field("scope")
        if scope is None:
            is_global = True
        elif scope.type in ("namespace_identifier", "type_identifier"):
            qualifier.append(scope.text)
        else:
            return None
        current = current.child_by_field("name")
    return (is_global, qualifier, current.text) if current is not None and current.type == "identifier" else None


def lookup(constants: dict[tuple[tuple[str, ...], str], set[int]], node: tsast.Node,
           spelling: tuple[bool, list[str], str]) -> set[int]:
    """Return the literal values of the declaration that a name reaches from a node, innermost scope first.

    A name that a template parameter binds reaches no constant.
    """
    is_global, qualifier, name = spelling
    if not qualifier and not is_global and name in bound_names(node):
        return set()
    scope = scope_of(node)
    depths = [0] if is_global else range(len(scope), -1, -1)
    for depth in depths:
        key = (tuple(scope[:depth]) + tuple(qualifier), name)
        if key in constants:
            return constants[key]
    return set()


def pins(tree: tsast.Tree) -> Iterator[tuple[int, str, int]]:
    """Yield each vacuous pin of a parsed file, as (row, name, literal).

    Complexity: linear in the number of nodes of the file.
    """
    constants = literal_constants(tree)
    for assertion in tree.find("static_assert_declaration"):
        condition = assertion.child_by_field("condition")
        if condition is None:
            continue
        for node in [condition, *condition.descendants("binary_expression")]:
            if node.type != "binary_expression":
                continue
            if tsast.operator_of(node) not in COMPARISONS:
                continue
            left, right = node.child_by_field("left"), node.child_by_field("right")
            for name_side, literal_side in ((left, right), (right, left)):
                spelling, value = spelled_name(name_side), literal_of(literal_side)
                if spelling is not None and value is not None and value in lookup(constants, node, spelling):
                    yield node.start[0], spelling[2], value


def scope_files(root: Path) -> list[Path]:
    """Return the C++ files in scope, sorted: tsast.is_in_cpp_scope, less the fixtures."""
    found: list[Path] = []
    for top in ROOTS:
        base = root / top
        if not base.is_dir():
            continue
        for path in base.rglob("*"):
            rel = path.relative_to(root).as_posix()
            if path.is_file() and tsast.is_in_cpp_scope(rel) and not FIXTURE_DIR.match(rel) \
                    and not any(part.startswith("build") for part in path.relative_to(root).parts[:-1]):
                found.append(path)
    return sorted(found)


def scan(root: Path) -> tuple[list[tuple[str, int, str, int]], list[str]]:
    """Find each vacuous pin in scope.

    Complexity: linear in the total size of the files in scope.

    Returns:
        Each pin as (path, line, name, literal), and each parse failure
    """
    found: list[tuple[str, int, str, int]] = []
    failures: list[str] = []
    for tree in tsast.parse(scope_files(root), strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            failures.append(f"{rel}: the parser cannot read this file. {tree.diagnostic.strip()}")
            continue
        found.extend((rel, row + 1, name, value) for row, name, value in sorted(set(pins(tree))))
    return found, failures


def read_allowlist(path: Path) -> tuple[set[str], list[str]]:
    """Return the keys of the allowlist, and each problem with a row."""
    keys: set[str] = set()
    problems: list[str] = []
    if not path.is_file():
        return keys, problems
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        key, found, reason = line.partition(SEPARATOR)
        if not found or not reason.strip() or ":" not in key:
            problems.append(f"{ALLOWLIST}:{number}: a row is `path:NAME — reason`, and the reason must not be empty.")
        elif key in keys:
            problems.append(f"{ALLOWLIST}:{number}: the key {key} is on two rows.")
        else:
            keys.add(key)
    return keys, problems


def check(root: Path) -> int:
    """Run the scan and report.

    Returns:
        0 clean, 1 on a vacuous pin or a parse failure, 2 on a malformed or stale row
    """
    found, failures = scan(root)
    keys, problems = read_allowlist(root / ALLOWLIST)
    live = {f"{rel}:{name}" for rel, _, name, _ in found}
    violations = [pin for pin in found if f"{pin[0]}:{pin[2]}" not in keys]
    for rel, line, name, value in violations:
        print(f"DERIVED-PIN violation: {rel}:{line} — `{name}` is pinned against the literal {value} that it was "
              f"assigned, so the assertion cannot fire.  Derive one side (reflection, tuple_size_v, .size()).  "
              f"Allowlist key: {rel}:{name}", file=sys.stderr)
    for failure in failures:
        print(f"DERIVED-PIN parse failure: {failure}", file=sys.stderr)
    stale = sorted(keys - live)
    for key in stale:
        print(f"DERIVED-PIN stale: {key} — no vacuous pin with this name is left in the file.  Remove the row.",
              file=sys.stderr)
    for problem in problems:
        print(f"DERIVED-PIN malformed row: {problem}", file=sys.stderr)
    if violations or failures:
        print("check-derived-pins: derive one side of the pin, or delete the pin if the count has no derivable "
              "source.", file=sys.stderr)
        return 1
    return 2 if stale or problems else 0


def self_test() -> int:
    """Plant each shape of a vacuous pin and each shape that is not one, then check the verdicts.

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
        ("namespace foundation::planted {", None, ""),
        ("inline constexpr int plain = 28;", None, ""),
        ("static_assert(plain == 28, \"plain\");", True, "a plain pin"),
        ("static_assert(plain /* n */ == 28);", True, "a comment beside the operator"),
        ("inline constexpr std::size_t reversed = 7;", None, ""),
        ("static_assert(7 == reversed);", True, "the literal on the left"),
        ("inline constexpr unsigned long long suffixed = 0x10ULL;", None, ""),
        ("static_assert(suffixed == 16u);", True, "another base and suffix"),
        ("inline constexpr int braced{1'000};", None, ""),
        ("static_assert(braced == 1000);", True, "a braced initializer and a digit separator"),
        ("inline constexpr auto cast = static_cast<std::size_t>(12);", None, ""),
        ("static_assert(cast == std::size_t{12});", True, "a static_cast and a braced conversion"),
        ("static_assert(cast <= 12);", False, "a ceiling that equals the constant"),
        ("static_assert(make(12) == cast);", False, "a call with the same argument"),
        ("static_assert(sizeof(int) == 4 && plain == 28);", True, "a pin in a conjunction"),
        ("static_assert(::foundation::planted::plain == (28));", True, "a qualified name and parentheses"),
        ("inline constexpr int derived = 3;", None, ""),
        ("static_assert(derived == std::tuple_size_v<Roster>);", False, "a derived operand"),
        ("inline constexpr int floor_value = 30;", None, ""),
        ("static_assert(floor_value >= 20);", False, "a floor against another literal"),
        ("inline constexpr int allowed = 5;", None, ""),
        ("static_assert(allowed == 5);", False, "an allowlisted pin"),
        ("inline int runtime = 9;", None, ""),
        ("static_assert(runtime == 9);", False, "a name that is not constexpr"),
        ("inline bool check() { return plain == 28; }", False, "a comparison outside a static_assert"),
        ("// static_assert(plain == 28);", False, "a comment"),
        ("struct Holder { static constexpr int member = 6; };", None, ""),
        ("static_assert(Holder::member == 6);", True, "a static member"),
        ("struct Pair { static constexpr int arity = 2; static_assert(arity == 2); };", True,
         "a pin inside the class of its constant"),
        ("template <class... Ts> struct Pack { static constexpr int arity = sizeof...(Ts); "
         "static_assert(arity == 2); };", False, "a name that another class declares with a literal"),
        ("inline constexpr int width = 4;", None, ""),
        ("template <int width> struct Wide { static_assert(width == 4); };", False,
         "a name that a template parameter binds"),
        ("inline void local() { constexpr int inner = 8; static_assert(inner == 8); }", True,
         "a pin in a function body"),
        ("inline void other() { static_assert(inner == 8); }", False, "a name of another function body"),
        ("}", None, ""),
        ("static_assert(far == 11);", True, "a pin above its declaration, at any distance"),
        ("inline constexpr int far = 11;", None, ""),
        ("namespace foundation::/* nested */ inner {", None, ""),
        ("inline constexpr int spaced = 5;", None, ""),
        ("}", None, ""),
        ("static_assert(foundation::inner::spaced == 5);", True, "a namespace name with a comment inside"),
    ]
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        header = root / "include/foundation/Planted.h"
        header.parent.mkdir(parents=True)
        header.write_text("\n".join(line for line, _, _ in planted) + "\n", encoding="utf-8")
        for rel, text in (
            ("test/sample_neg/neg_pin.cpp", "inline constexpr int fixture = 2;\nstatic_assert(fixture == 2);\n"),
            ("src/planted.cpp", "constexpr int in_source = 3;\nstatic_assert(in_source == 3);\n"),
            (ALLOWLIST, "# planted\ninclude/foundation/Planted.h:allowed — a planted pin\n"),
        ):
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        found, broken = scan(root)
        keys, _ = read_allowlist(root / ALLOWLIST)
        reported = {line for rel, line, name, _ in found
                    if rel == "include/foundation/Planted.h" and f"{rel}:{name}" not in keys}
        for line, (_, caught, label) in enumerate(planted, start=1):
            if caught is True:
                expect(f"caught: {label}", line in reported)
            elif caught is False:
                expect(f"not caught: {label}", line not in reported, True)
        expect("nothing else in the planted header is reported",
               reported <= {line for line, (_, caught, _) in enumerate(planted, start=1) if caught}, True)
        expect("caught: a pin in src/", any(rel == "src/planted.cpp" for rel, _, _, _ in found))
        expect("not caught: a negative fixture", not any("sample_neg" in rel for rel, _, _, _ in found), True)
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
        header.write_text("inline constexpr int allowed = 5;\nstatic_assert(allowed == 5);\n", encoding="utf-8")
        (root / "src/planted.cpp").unlink()
        expect("a tree whose pins all have rows passes", captured(root)[0] == 0, True)
        (root / ALLOWLIST).write_text("include/foundation/Planted.h:allowed — a planted pin\n"
                                      "include/foundation/Planted.h:gone — a stale row\n", encoding="utf-8")
        code, report = captured(root)
        expect("a stale row exits 2", code == 2 and "stale" in report)
        (root / ALLOWLIST).write_text("include/foundation/Planted.h:allowed\n", encoding="utf-8")
        code, report = captured(root)
        expect("a row with no reason admits nothing and is reported", code == 1 and "malformed row" in report)
        (root / ALLOWLIST).write_text("include/foundation/Planted.h:allowed — a planted pin\n", encoding="utf-8")
        (root / "src/Broken.cpp").write_text("void f() { g(1) { } }\nstatic_assert(true);\n", encoding="utf-8")
        expect("a file the parser cannot read fails the check", captured(root)[0] == 1)
    if failures:
        print(f"check-derived-pins --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-derived-pins --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    if argv not in ([], ["--self-test"]):
        print("usage: check-derived-pins.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-derived-pins: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
