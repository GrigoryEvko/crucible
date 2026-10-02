#!/usr/bin/env python3
"""check-process-wide-state — each object with static storage duration is one per process, or says why not.

The build compiles with -fvisibility=hidden.  Each shared library that
compiles a header then defines its own copy of every inline variable and of
every static of an inline function in that header.  crucible_native.py loads
libcrucible_vessel.so and libcrucible_dispatch.so with dlopen and RTLD_LOCAL,
so a write into the copy of one library is not seen in the other library.

THE PATTERN
    A process-wide object carries CRUCIBLE_PROCESS_WIDE, from
    foundation/Platform.h, which gives it default visibility.  GCC then emits
    the object, and the guard of a static local, as a unique global symbol.
    The dynamic linker binds every copy of a unique symbol to the first copy
    that it loads, also across RTLD_LOCAL.  The marker goes on one of:
      * an inline variable, at namespace scope or as a static data member
      * the inline function that holds the object as a static local.
    On a thread_local object, each thread then has one object for the
    process.  test/test_process_wide_across_libraries.cpp shows each case
    with two libraries that dlopen loads with RTLD_LOCAL.
    The marker has no effect in a template, because an instantiation takes
    the smallest visibility of the template and its arguments, and each type
    of this tree is hidden.  The marker has no effect with internal linkage,
    in a function that is not inline, or on a static of a lambda.  The check
    refuses the marker in each of these places, and on a declaration that
    holds no candidate.

WHAT THE CHECK READS
    Every C++ file under include/, src/ and vessel/.  It does not read the
    check files under test/layer/checks/.  A check file holds the self-test
    namespaces and the namespace-scope static_asserts of one header.  Each
    build compiles it only into object libraries that nothing links: the
    sentinel of its layer, and for a check file of
    test/layer/walk-checks.txt, one of the walk units of
    test/layer/walks_across_headers.cpp.  An object of a check file
    therefore has no copy in a shared library, and the hazard of THE
    PATTERN does not exist for it.  The move of a check from its header
    to its check file removes the copies that each library made.  A
    candidate is a
    variable with static or thread storage duration whose object is not
    const: a variable at namespace scope, an inline static data member, the
    out-of-class definition of a static data member, and a static or
    thread_local local variable.  A constexpr object and a const object are
    not candidates, because each copy holds the same value.  A reference is
    not a candidate, because it names another object.  An extern declaration
    with no initializer is not a definition.  A static data member in its
    class without inline is a declaration, and its definition out of the
    class is the candidate.

EACH CANDIDATE IS ONE OF
    process-wide  it carries the marker, as THE PATTERN says
    per library   a row of utils/scripts/process-wide-roster.txt names it, with the
                  reason why one copy in each library is correct.
    A row that names no candidate, or names a marked candidate, fails the
    check.

WHAT THE CHECK CANNOT SEE
    A const object whose address is its meaning, such as a sentinel.  Declare
    such an object without const.  A declaration inside a macro body, which
    the kit does not parse.  A parse error in a file of the scope fails the
    check.

Exit 0 clean, 1 on a violation or a parse failure, 2 on a usage error or a
failed self-test, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import os
import sys
import tempfile
from collections.abc import Iterator
from pathlib import Path
from typing import NamedTuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402

SCOPE = ("include", "src", "vessel")
ROSTER = Path("utils/scripts/process-wide-roster.txt")
MARKER = "CRUCIBLE_PROCESS_WIDE"
ANONYMOUS = "(anonymous namespace)"

# The declarators that wrap the name of a declared object.
_WRAPPERS = ("init_declarator", "attributed_declarator", "parenthesized_declarator", "array_declarator")
_CLASS_SPECIFIERS = ("class_specifier", "struct_specifier", "union_specifier")


class Candidate(NamedTuple):
    """One object with static or thread storage duration whose object is not const.

    holder is the node that carries the marker for this object: the
    declaration itself, or the function that holds a static local.
    """

    path: str
    line: int
    name: str
    holder: tsast.Node
    is_local: bool
    declaration: tsast.Node


def words_of(declaration: tsast.Node) -> set[str]:
    """Return the storage class and qualifier words that stand directly in a declaration."""
    return {child.text for child in declaration.children
            if child.type in ("storage_class_specifier", "type_qualifier")}


def is_marker(node: tsast.Node) -> bool:
    """Return True when a node is the marker, which the kit reads as an attribute macro."""
    if node.type != "attribute_macro":
        return False
    name = node.child_by_field("name")
    return name is not None and name.text == MARKER


def has_marker(holder: tsast.Node) -> bool:
    """Return True when a declaration or a function carries the marker."""
    return any(is_marker(child) for child in holder.children)


def unwrapped(declarator: tsast.Node) -> tsast.Node:
    """Return the declarator under every wrapper that does not change what kind of entity it declares."""
    current = declarator
    while current.type in _WRAPPERS:
        inner = current.child_by_field("declarator")
        if inner is None:
            break
        current = inner
    return current


def names_function(declarator: tsast.Node) -> bool:
    """Return True when a declarator declares a function, through pointer and reference wrappers too."""
    current = unwrapped(declarator)
    while current.type in ("pointer_declarator", "reference_declarator"):
        inner = current.child_by_field("declarator")
        if inner is None:
            return False
        current = unwrapped(inner)
    return current.type == "function_declarator"


def is_const_object(words: set[str], declarator: tsast.Node) -> bool:
    """Return True when the declared object itself cannot change, or when it is a reference.

    `const T* p` declares a pointer that can change, and `T* const p` declares
    one that cannot.  A reference names another object, so it counts as
    const here.
    """
    inner = unwrapped(declarator)
    if inner.type == "reference_declarator":
        return True
    if inner.type == "pointer_declarator":
        return any(child.type == "type_qualifier" and child.text == "const" for child in inner.children)
    return "const" in words


def scope_kind(declaration: tsast.Node) -> str:
    """Return local, member or namespace for the scope that holds a declaration."""
    parent = declaration.parent
    while parent is not None:
        if parent.type in ("compound_statement", "lambda_expression"):
            return "local"
        if parent.type == "field_declaration_list":
            return "member"
        if parent.type in ("declaration_list", "translation_unit", "linkage_specification"):
            return "namespace"
        parent = parent.parent
    return "namespace"


def class_chain(node: tsast.Node) -> tuple[str, ...]:
    """Return the names of the classes that enclose a node, outermost first."""
    names: list[str] = []
    holder = node.ancestor_of_type(*_CLASS_SPECIFIERS)
    while holder is not None:
        name = holder.child_by_field("name")
        text = None if name is None else tsast.leaf_name(name)
        names.append(text or "(unnamed)")
        holder = holder.ancestor_of_type(*_CLASS_SPECIFIERS)
    return tuple(reversed(names))


def namespace_prefix(node: tsast.Node) -> tuple[str, ...]:
    """Return the namespaces that enclose a node, with an anonymous namespace spelled as the demangler spells it."""
    return tuple(segment or ANONYMOUS for segment in tsast.namespace_path(node))


def declared_name(declarator: tsast.Node) -> tuple[str, ...]:
    """Return the parts of the name that a declarator declares, qualifier first."""
    inner = unwrapped(declarator)
    while inner.type in ("pointer_declarator", "reference_declarator"):
        next_inner = inner.child_by_field("declarator")
        if next_inner is None:
            break
        inner = unwrapped(next_inner)
    parts = tsast.qualified_parts(inner)
    if parts is not None and parts[1]:
        return parts[1]
    text = tsast.leaf_name(inner)
    return (text or "?",)


def candidate_name(declaration: tsast.Node, declarator: tsast.Node, kind: str) -> str:
    """Return the key of a candidate: the qualified name, with `f()::` before a static local of f."""
    own = declared_name(declarator)
    prefix = namespace_prefix(declaration)
    if kind == "local":
        function = tsast.enclosing_function(declaration)
        owner = "::".join(function) if function is not None else "(unknown)"
        return "::".join((*prefix, f"{owner}()", *own))
    return "::".join((*prefix, *class_chain(declaration), *own))


def candidates(tree: tsast.Tree, rel: str) -> Iterator[Candidate]:
    """Yield each candidate of one parsed file, in source order.

    Complexity: linear in the number of nodes of the file.
    """
    for declaration in tree.find("declaration", "field_declaration"):
        if declaration.ancestor_of_type("parameter_list", "template_parameter_list") is not None:
            continue
        words = words_of(declaration)
        if "constexpr" in words or "consteval" in words:
            continue
        kind = scope_kind(declaration)
        if kind == "local" and not {"static", "thread_local"} & words:
            continue
        if kind == "member" and not {"static", "inline"} <= words:
            continue
        for declarator in declaration.children:
            if declarator.field != "declarator" or names_function(declarator):
                continue
            if kind == "namespace" and "extern" in words and declarator.type != "init_declarator":
                continue
            if is_const_object(words, declarator):
                continue
            holder = declaration
            if kind == "local":
                function = declaration.ancestor_of_type("function_definition", "lambda_expression")
                holder = function if function is not None else declaration
            yield Candidate(rel, declarator.line, candidate_name(declaration, declarator, kind), holder,
                            kind == "local", declaration)


def marker_faults(candidate: Candidate) -> list[str]:
    """Return why the marker cannot make this candidate one per process, or an empty list."""
    faults: list[str] = []
    holder = candidate.holder
    if holder.type == "lambda_expression":
        return ["a static of a lambda has no linkage that the marker can make unique"]
    if holder.ancestor_of_type("template_declaration") is not None or holder.type == "template_declaration":
        faults.append("it sits in a template, and an instantiation over a hidden type is hidden")
    if ANONYMOUS in namespace_prefix(holder):
        faults.append("it has internal linkage, in an anonymous namespace")
    holder_words = words_of(holder)
    is_in_class = holder.ancestor_of_type("field_declaration_list") is not None
    if "static" in holder_words and not is_in_class:
        faults.append("it has internal linkage, through static at namespace scope")
    is_inline = "inline" in holder_words or "constexpr" in holder_words or "consteval" in holder_words
    if not is_inline and not (candidate.is_local and is_in_class):
        what = "function" if candidate.is_local else "variable"
        faults.append(f"the {what} is not inline, so its object is not a unique symbol")
    return faults


class RosterRow(NamedTuple):
    """One row of the roster: a candidate that is correct with one copy in each library."""

    line: int
    path: str
    name: str
    reason: str


def read_roster(root: Path) -> tuple[list[RosterRow], list[str]]:
    """Read the roster rows, and the errors of each row that cannot be read.

    One row is `path | qualified name | reason`.  A `#` starts a comment line,
    and a blank line is skipped.
    """
    rows: list[RosterRow] = []
    errors: list[str] = []
    listing = root / ROSTER
    if not listing.is_file():
        return rows, [f"{ROSTER}: the roster does not exist"]
    for number, raw in enumerate(listing.read_text(encoding="utf-8").splitlines(), start=1):
        text = raw.strip()
        if not text or text.startswith("#"):
            continue
        cells = [cell.strip() for cell in text.split("|")]
        if len(cells) != 3 or not all(cells):
            errors.append(f"{ROSTER}:{number}: a row is `path | qualified name | reason`, with no empty cell")
            continue
        rows.append(RosterRow(number, cells[0], cells[1], cells[2]))
    return rows, errors


def scope_files(root: Path) -> list[Path]:
    """Return the C++ files of the scope, sorted, relative to the scan root when it is the repository."""
    found: list[Path] = []
    for top in SCOPE:
        base = root / top
        if base.is_dir():
            found.extend(path for path in base.rglob("*")
                         if path.is_file() and path.name.endswith(tsast.CPP_SUFFIXES)
                         and tsast.is_in_cpp_scope(path.relative_to(root)))
    return sorted(found)


def scan(root: Path) -> list[str]:
    """Find each candidate that is neither marked nor rostered, each misplaced marker and each stale row.

    Complexity: linear in the total size of the files in scope, plus the roster.
    """
    violations: list[str] = []
    rows, errors = read_roster(root)
    violations.extend(errors)
    rostered = {(row.path, row.name): row for row in rows}
    seen: set[tuple[str, str]] = set()
    marked_holders: set[tsast.Node] = set()
    for tree in tsast.parse(scope_files(root), strict=False):
        rel = Path(tree.path).relative_to(root).as_posix() if Path(tree.path).is_absolute() \
            else Path(tree.path).as_posix()
        if tree.diagnostic is not None:
            violations.append(f"{rel}: the parser cannot read this file. {tree.diagnostic.strip()}")
            continue
        holders_with_candidate: set[tsast.Node] = set()
        for candidate in candidates(tree, rel):
            key = (rel, candidate.name)
            seen.add(key)
            holders_with_candidate.add(candidate.holder)
            is_marked = has_marker(candidate.holder)
            if is_marked:
                marked_holders.add(candidate.holder)
                for fault in marker_faults(candidate):
                    violations.append(f"{rel}:{candidate.line}: {candidate.name} carries {MARKER}, but {fault}.")
                if key in rostered:
                    violations.append(f"{ROSTER}:{rostered[key].line}: {candidate.name} carries {MARKER}, so the "
                                      f"roster must not name it.")
            elif key not in rostered:
                violations.append(f"{rel}:{candidate.line}: {candidate.name} has static storage duration and is "
                                  f"neither process-wide ({MARKER}) nor named in {ROSTER}.")
        for macro in tree.find("attribute_macro"):
            if not is_marker(macro):
                continue
            owner = macro.parent
            if owner is None or owner not in holders_with_candidate:
                violations.append(f"{rel}:{macro.line}: {MARKER} marks a declaration that holds no candidate, so "
                                  f"it makes nothing one per process.")
    for row in rows:
        if (row.path, row.name) not in seen:
            violations.append(f"{ROSTER}:{row.line}: {row.path} holds no candidate named {row.name}.")
    return violations


def check(root: Path) -> int:
    """Run the scan and report.

    Returns:
        0 clean, 1 on a violation or a parse failure
    """
    violations = scan(root)
    for violation in violations:
        print(f"PROCESS-WIDE: {violation}", file=sys.stderr)
    if violations:
        print(f"check-process-wide-state: put {MARKER} on each object that must be one per process, or name the "
              f"object in {ROSTER} with the reason why one copy in each shared library is correct.",
              file=sys.stderr)
        return 1
    print("check-process-wide-state: clean — each object with static storage duration is process-wide or named "
          "in the roster.", file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each kind of candidate and each misplaced marker, then check the verdicts.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def expect(name: str, is_holding: bool, is_negative: bool = False) -> None:
        """Record one case."""
        nonlocal negatives
        negatives += is_negative
        print(f"  {'ok  ' if is_holding else 'FAIL'} {name}")
        if not is_holding:
            failures.append(name)

    planted: list[tuple[str, bool | None, str]] = [
        ("#pragma once", None, ""),
        ("namespace crucible::planted {", None, ""),
        ("inline int unmarked_variable = 0;", True, "an unmarked inline variable"),
        ("CRUCIBLE_PROCESS_WIDE inline constinit int marked_variable = 0;", False, "a marked inline variable"),
        ("CRUCIBLE_PROCESS_WIDE inline constinit thread_local int marked_per_thread = 0;", False,
         "a marked inline thread_local variable"),
        ("inline thread_local int unmarked_per_thread = 0;", True, "an unmarked inline thread_local variable"),
        ("inline int rostered_variable = 0;", False, "a rostered variable"),
        ("inline constexpr int constant = 0;", False, "a constexpr variable"),
        ("inline const int constant_object = 0;", False, "a const variable"),
        ("inline const char* movable_pointer = nullptr;", True, "a pointer to const, which can change"),
        ("inline char* const fixed_pointer = nullptr;", False, "a const pointer"),
        ("extern int declared_only;", False, "an extern declaration"),
        ("CRUCIBLE_PROCESS_WIDE inline int& marked_function() {", False, "a marked function"),
        ("    static int instance = 0;", False, "the static local of a marked function"),
        ("    return instance;", None, ""),
        ("}", None, ""),
        ("inline int& unmarked_function() {", None, ""),
        ("    static int instance = 0;", True, "the static local of an unmarked function"),
        ("    static const int fixed = 0;", False, "a const static local"),
        ("    thread_local int per_thread = 0;", True, "a thread_local local"),
        ("    int automatic = 0;", False, "an automatic local"),
        ("    return instance;", None, ""),
        ("}", None, ""),
        ("CRUCIBLE_PROCESS_WIDE inline int nothing_held() { return 0; }", True, "a marker that holds no candidate"),
        ("CRUCIBLE_PROCESS_WIDE int not_inline_function() {", None, ""),
        ("    static int instance = 0;", True, "a marked function that is not inline"),
        ("    return instance;", None, ""),
        ("}", None, ""),
        ("template <class T>", None, ""),
        ("CRUCIBLE_PROCESS_WIDE inline int templated_variable = 0;", True, "a marked variable template"),
        ("template <class T>", None, ""),
        ("struct Holder {", None, ""),
        ("    CRUCIBLE_PROCESS_WIDE static int& record() {", None, ""),
        ("        static int instance = 0;", True, "a marked member of a class template"),
        ("        return instance;", None, ""),
        ("    }", None, ""),
        ("};", None, ""),
        ("struct Plain {", None, ""),
        ("    CRUCIBLE_PROCESS_WIDE static inline int marked_member = 0;", False, "a marked inline member"),
        ("    static inline int unmarked_member = 0;", True, "an unmarked inline member"),
        ("    static int declared_member;", False, "a member declaration"),
        ("    CRUCIBLE_PROCESS_WIDE static int& local_holder() {", None, ""),
        ("        static int instance = 0;", False, "the static local of a marked member function"),
        ("        return instance;", None, ""),
        ("    }", None, ""),
        ("};", None, ""),
        ("CRUCIBLE_PROCESS_WIDE inline int Plain::declared_member = 0;", False,
         "a marked out-of-class member definition"),
        ("namespace {", None, ""),
        ("CRUCIBLE_PROCESS_WIDE inline int hidden_variable = 0;", True, "a marked variable with internal linkage"),
        ("}", None, ""),
        ("CRUCIBLE_PROCESS_WIDE static int static_variable = 0;", True, "a marked variable declared static"),
        ("inline void lambda_holder() {", None, ""),
        ("    []() { static int in_lambda = 0; (void)in_lambda; }();", True, "an unmarked static of a lambda"),
        ("}", None, ""),
        ("}", None, ""),
    ]
    roster_rows = [
        "# planted",
        "include/crucible/Planted.h | crucible::planted::rostered_variable | a fixture",
        "include/crucible/Planted.h | crucible::planted::gone_variable | a stale row",
        "include/crucible/Planted.h | crucible::planted::marked_variable | a row for a marked object",
    ]
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        header = root / "include/crucible/Planted.h"
        header.parent.mkdir(parents=True)
        header.write_text("\n".join(line for line, _, _ in planted) + "\n", encoding="utf-8")
        (root / "utils" / "scripts").mkdir(parents=True)
        (root / ROSTER).write_text("\n".join(roster_rows) + "\n", encoding="utf-8")
        (root / "test").mkdir()
        (root / "test/Out.cpp").write_text("inline int out_of_scope = 0;\n", encoding="utf-8")
        violations = scan(root)
        reported = {int(violation.split(":")[1]) for violation in violations
                    if violation.startswith("include/crucible/Planted.h:")}
        for line, (_, is_caught, label) in enumerate(planted, start=1):
            if is_caught is True:
                expect(f"caught: {label}", line in reported)
            elif is_caught is False:
                expect(f"not caught: {label}", line not in reported, True)
        expect("nothing else in the planted header is reported",
               reported <= {line for line, (_, is_caught, _) in enumerate(planted, start=1) if is_caught}, True)
        expect("a stale roster row is caught", any("gone_variable" in violation for violation in violations))
        expect("a roster row for a marked object is caught",
               any("roster must not name it" in violation for violation in violations))
        expect("a file outside the scope is not read", not any("out_of_scope" in v for v in violations), True)

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
        header.write_text("#pragma once\nCRUCIBLE_PROCESS_WIDE inline int shared = 0;\n", encoding="utf-8")
        (root / ROSTER).write_text("# empty\n", encoding="utf-8")
        expect("a clean tree passes", captured(root)[0] == 0, True)
        (root / ROSTER).write_text("include/x.h | name\n", encoding="utf-8")
        expect("a roster row with two cells fails the check", captured(root)[0] == 1)
        (root / ROSTER).write_text("# empty\n", encoding="utf-8")
        (root / "src").mkdir()
        (root / "src/Broken.cpp").write_text("void f() { g(1) { } }\n", encoding="utf-8")
        expect("a file the parser cannot read fails the check", captured(root)[0] == 1)
    if failures:
        print(f"check-process-wide-state --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-process-wide-state --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    if argv not in ([], ["--self-test"]):
        print("usage: check-process-wide-state.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-process-wide-state: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
