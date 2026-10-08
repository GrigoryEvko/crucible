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
    binds reaches no constant.

    The declaration can be in the file of the pin, or in a header that the
    pin can see.  The check file test/layer/checks/<layer>/<path>.cpp sees
    its header include/<layer>/<path>.h.  Each file sees each header of the
    scope that an #include of its parse tree names, as include/<path> for an
    angle-bracket path and beside the file for a quoted path.  A header that
    the file reaches only through another header is not read.  A constant of
    a function body or of an unnamed class of a header is local to that
    header, so no other file sees it.

    A qualifier that starts with a namespace alias resolves to the target of
    the alias.  The alias of the innermost scope that encloses the pin wins,
    in the file or in a header that the pin can see.  An alias of an alias
    resolves too, and a cycle of aliases reaches no constant.  An alias of a
    function body of a header is local to that header.

    An initializer and a literal count as the same integer through a suffix,
    a digit separator, a base prefix, parentheses, a static_cast, a braced
    initializer and a builtin-type conversion.  A different literal, a
    derived operand, a call and a comparison outside a static_assert are not
    pins.  A floor or a ceiling (`>=`, `<=`) that equals the constant is a
    bound, such as the upper limit of an API, and not a copy, so it is not
    read.

WHAT THE PARSER CANNOT READ
    A pin inside a macro body is not read.  A parse error in a file of the
    scope is a violation, unless utils/assay/readers/tsast.py lists the file as
    unparseable.

SCOPE
    include/, src/, test/, bench/, utils/tools/ and vessel/, without the
    negative-compile fixtures of test/*_neg/.

THE ALLOWLIST
    utils/scripts/derived-pins-allowlist.txt admits a pin by `path:NAME — reason`.
    The key names a constant, not a line.  A row with no reason, a duplicate
    row and a row that matches no live pin fail the check.

USAGE
    guard check --rule derived-pins [--self-test]

Exit 0 clean, 1 on a vacuous pin or a parse failure, 2 on a malformed or stale
row, a usage error or a failed self-test, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import dataclasses
import io
import os
import re
import sys
import tempfile
from collections import defaultdict
from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from pathlib import Path

from assay.readers import tsast

ROOTS = ("include", "src", "test", "bench", "utils/tools", "vessel")
ALLOWLIST = "utils/scripts/derived-pins-allowlist.txt"
FIXTURE_DIR = re.compile(r"^test/[^/]+_neg/")
COMPARISONS = frozenset({"=="})
SEPARATOR = " — "
# The directory of the check files, and the directory of their headers.
CHECK_FILES = "test/layer/checks/"
CHECKED_HEADERS = "include/"
# The scope parts of a function body and of an unnamed class (scope_of).
LOCAL_SCOPE_PREFIXES = ("(function@", "(class@")

# A constant: its scope and its name, then each literal value with the file that declares it.
Constants = dict[tuple[tuple[str, ...], str], set[tuple[int, str]]]
# A namespace alias: its scope and its name, then whether its target starts at the global scope, and the parts
# of the target.
Aliases = dict[tuple[tuple[str, ...], str], tuple[bool, tuple[str, ...]]]
# The number of aliases that one qualifier passes through at most, so that a cycle of aliases ends.
ALIAS_DEPTH = 8


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


def literal_constants(tree: tsast.Tree, rel: str) -> Constants:
    """Return each constexpr variable of the file whose initializer is an integer literal, by scope and name.

    Args:
        tree: The parse tree of the file
        rel: The path of the file, relative to the scan root, which each value names as its origin
    """
    found: Constants = defaultdict(set)
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
                found[(scope_of(node), named.text)].add((value, rel))
    return found


def exported(constants: Constants) -> Constants:
    """Return the constants of a header that another file can see: each one outside a function body and an
    unnamed class.  The scope parts of those two name a line of the header, which means nothing in another file."""
    return {key: values for key, values in constants.items()
            if not any(part.startswith(LOCAL_SCOPE_PREFIXES) for part in key[0])}


def namespace_aliases(tree: tsast.Tree) -> Aliases:
    """Return each namespace alias of a file, by its scope and its name, with the target that it names.

    Complexity: linear in the number of nodes of the file.

    Args:
        tree: The parse tree of the file
    """
    found: Aliases = {}
    for node in tree.find("namespace_alias_definition"):
        named = node.child_by_field("name")
        targets = [child for child in node.children if child.type != "comment" and child.field != "name"]
        if named is None or len(targets) != 1:
            continue
        target = tsast.qualified_parts(targets[0])
        if target is not None:
            found[(scope_of(node), named.text)] = target
    return found


def exported_aliases(aliases: Aliases) -> Aliases:
    """Return the aliases of a header that another file can see: each one outside a function body."""
    return {key: target for key, target in aliases.items()
            if not any(part.startswith(LOCAL_SCOPE_PREFIXES) for part in key[0])}


def resolve_aliases(comparison: Comparison, aliases: Aliases) -> Comparison:
    """Return the comparison with the namespace alias at the head of its qualifier replaced by its target.

    The alias of the innermost scope that encloses the comparison wins, as the
    compiler looks it up.  The names of a target that does not start at the
    global scope resolve from the scope of the alias, so the comparison then
    looks up its name from that scope.  An alias whose target starts with
    another alias resolves again, at most ALIAS_DEPTH times.

    Complexity: O(ALIAS_DEPTH * the depth of the scope).

    Args:
        comparison: A comparison of the file
        aliases: The aliases that the file can see
    """
    for _ in range(ALIAS_DEPTH):
        if comparison.is_global or not comparison.qualifier:
            return comparison
        head = comparison.qualifier[0]
        scope = comparison.scope
        found = next((scope[:depth] for depth in range(len(scope), -1, -1) if (scope[:depth], head) in aliases),
                     None)
        if found is None:
            return comparison
        is_global, target = aliases[(found, head)]
        comparison = dataclasses.replace(comparison, is_global=is_global, qualifier=target + comparison.qualifier[1:],
                                         scope=found)
    return comparison


def header_of_check_file(rel: str) -> str | None:
    """Return the header of a check file, test/layer/checks/<layer>/<path>.cpp, or None for another file."""
    if not rel.startswith(CHECK_FILES) or not rel.endswith(".cpp"):
        return None
    return CHECKED_HEADERS + rel[len(CHECK_FILES):-len(".cpp")] + ".h"


def included_headers(tree: tsast.Tree, rel: str, root: Path) -> list[str]:
    """Return each file of the scan root that an #include of the parse tree names, relative to the root.

    An angle-bracket path names include/<path>.  A quoted path names the
    file beside the including file, and then include/<path>.  A path that
    names no file of the root is not a header of the scope.
    """
    found: list[str] = []
    for include in tree.find("preproc_include"):
        target = include.child_by_field("path")
        if target is None or target.type not in ("system_lib_string", "string_literal"):
            continue
        inner = tsast.prose_text(target).strip()[1:-1].strip()
        places = [f"{CHECKED_HEADERS}{inner}"]
        if target.type == "string_literal":
            places.insert(0, (Path(rel).parent / inner).as_posix())
        for place in places:
            if (root / place).is_file():
                found.append(os.path.normpath(place))
                break
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


@dataclass(frozen=True, slots=True)
class Comparison:
    """An `==` in a static_assert between a name and an integer literal, with the facts of its lookup."""

    row: int
    is_global: bool
    qualifier: tuple[str, ...]
    name: str
    value: int
    scope: tuple[str, ...]
    is_template_parameter: bool


def lookup(constants: Constants, comparison: Comparison) -> set[tuple[int, str]]:
    """Return the values and the origins of the declaration that the name of a comparison reaches.

    The innermost scope comes first.  A name that a template parameter
    binds reaches no constant.
    """
    if comparison.is_template_parameter:
        return set()
    scope = comparison.scope
    depths = [0] if comparison.is_global else range(len(scope), -1, -1)
    for depth in depths:
        key = (tuple(scope[:depth]) + comparison.qualifier, comparison.name)
        if key in constants:
            return constants[key]
    return set()


def comparisons(tree: tsast.Tree) -> Iterator[Comparison]:
    """Yield each `==` in a static_assert of a parsed file between a name and an integer literal.

    Complexity: linear in the number of nodes of the file.
    """
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
                if spelling is None or value is None:
                    continue
                is_global, qualifier, name = spelling
                is_template_parameter = not qualifier and not is_global and name in bound_names(node)
                yield Comparison(node.start[0], is_global, tuple(qualifier), name, value, scope_of(node),
                                 is_template_parameter)


def pins(found: Iterable[Comparison], constants: Constants) -> Iterator[tuple[int, str, int, tuple[str, ...]]]:
    """Yield each vacuous pin of the comparisons, as (row, name, literal, the files that declare the literal)."""
    for comparison in found:
        origins = sorted(origin for value, origin in lookup(constants, comparison) if value == comparison.value)
        if origins:
            yield comparison.row, comparison.name, comparison.value, tuple(origins)


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


@dataclass(frozen=True, slots=True)
class FileFacts:
    """What the scan reads from one file: its constants, its aliases, the headers that it sees, and its comparisons."""

    constants: Constants
    aliases: Aliases
    headers: tuple[str, ...]
    comparisons: tuple[Comparison, ...]


def scan(root: Path) -> tuple[list[tuple[str, int, str, int, tuple[str, ...]]], list[str]]:
    """Find each vacuous pin in scope.

    The scan reads each file one time.  Then it resolves the namespace alias
    at the head of the qualifier of each comparison, and it looks up the name
    in the constants of its file and of the headers that the file sees.
    Complexity: linear in the total size of the files in scope, plus the
    constants and the aliases of each seen header for each file.

    Returns:
        Each pin as (path, line, name, literal, the files that declare the literal), and each parse failure
    """
    facts: dict[str, FileFacts] = {}
    failures: list[str] = []
    for tree in tsast.parse(scope_files(root), strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            failures.append(f"{rel}: the parser cannot read this file. {tree.diagnostic.strip()}")
            continue
        headers = included_headers(tree, rel, root)
        checked = header_of_check_file(rel)
        if checked is not None:
            headers.append(checked)
        facts[rel] = FileFacts(literal_constants(tree, rel), namespace_aliases(tree), tuple(dict.fromkeys(headers)),
                               tuple(comparisons(tree)))
    shared = {rel: exported(fact.constants) for rel, fact in facts.items()}
    shared_aliases = {rel: exported_aliases(fact.aliases) for rel, fact in facts.items()}
    found: list[tuple[str, int, str, int, tuple[str, ...]]] = []
    for rel, fact in facts.items():
        if not fact.comparisons:
            continue
        constants: Constants = defaultdict(set)
        for source in (fact.constants, *(shared[header] for header in fact.headers if header in shared)):
            for key, values in source.items():
                constants[key] |= values
        # An alias of the file hides an alias of a header with the same scope and name.
        aliases: Aliases = dict(fact.aliases)
        for header in fact.headers:
            for key, target in shared_aliases.get(header, {}).items():
                aliases.setdefault(key, target)
        resolved = [resolve_aliases(comparison, aliases) for comparison in fact.comparisons]
        found.extend((rel, row + 1, name, value, origins)
                     for row, name, value, origins in sorted(set(pins(resolved, constants))))
    return sorted(found), failures


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
    live = {f"{rel}:{name}" for rel, _, name, _, _ in found}
    violations = [pin for pin in found if f"{pin[0]}:{pin[2]}" not in keys]
    for rel, line, name, value, origins in violations:
        print(f"DERIVED-PIN violation: {rel}:{line} — `{name}` is pinned against the literal {value} that "
              f"{' and '.join(origins)} assigns to it, so the assertion cannot fire.  Derive one side (reflection, "
              f"tuple_size_v, .size()).  Allowlist key: {rel}:{name}", file=sys.stderr)
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


# A tree for the pins against a constant of another file.  The function
# hidden() of Seen.h and the function user() of local_hidden.cpp start on
# the same row, so their scopes have the same name.
SEEN_HEADER_TREE = {
    "include/fixy/Seen.h": ("#pragma once\n"
                            "namespace fixy::seen_space {\n"
                            "inline constexpr int seen = 12;\n"
                            "inline void hidden() { constexpr int local_only = 4; (void)local_only; }\n"
                            "}\n"),
    "test/layer/checks/fixy/Seen.cpp": ("// The check file reaches its header with no #include.\n"
                                        "namespace fixy::seen_space {\n"
                                        "static_assert(seen == 12);\n"
                                        "}\n"),
    "test/includer.cpp": "#include <fixy/Seen.h>\nstatic_assert(fixy::seen_space::seen == 12);\n",
    "test/quoted/Local.h": "#pragma once\ninline constexpr int seen = 3;\n",
    "test/quoted/user.cpp": "#include \"Local.h\"\nstatic_assert(seen == 3);\n",
    "test/not_included.cpp": "static_assert(fixy::seen_space::seen == 12);\n",
    "test/other_value.cpp": "#include <fixy/Seen.h>\nstatic_assert(fixy::seen_space::seen == 13);\n",
    "test/local_hidden.cpp": ("#include <fixy/Seen.h>\n"
                              "namespace fixy::seen_space {\n"
                              "// The function below starts on the row of hidden() in Seen.h.\n"
                              "inline void user() { static_assert(local_only == 4); }\n"
                              "}\n"),
}
SEEN_HEADER_PLANTS = (
    ("test/layer/checks/fixy/Seen.cpp", "a pin in a check file against a constant of its header"),
    ("test/includer.cpp", "a pin against a constant of a header of an angle-bracket #include"),
    ("test/quoted/user.cpp", "a pin against a constant of a header of a quoted #include"),
)
SEEN_HEADER_CONTROLS = (
    ("test/not_included.cpp", "a pin against a constant of a header that the file does not include"),
    ("test/other_value.cpp", "a pin against another literal than the constant of a header"),
    ("test/local_hidden.cpp", "a pin against a constant of a function body of a header"),
)

# A tree for the pins through a namespace alias.  It goes into the root with
# SEEN_HEADER_TREE, so its files see include/fixy/Seen.h.  The two functions
# of alias_other_function.cpp start on different rows, so their scopes have
# different names.  The function with_alias() of Aliased.h and the function
# user() of alias_header_local.cpp start on the same row, so their scopes
# have the same name.
ALIAS_TREE = {
    "include/fixy/Aliased.h": ("#pragma once\n"
                               "namespace fixy::aliased_space {\n"
                               "inline constexpr int deep = 21;\n"
                               "}\n"
                               "namespace fixy { namespace ahead = ::fixy::aliased_space; }\n"
                               "inline void with_alias() { namespace hidden_alias = ::fixy::aliased_space; }\n"),
    "test/alias_header_local.cpp": ("#include <fixy/Aliased.h>\n"
                                    "// The function below starts on the row of with_alias() in Aliased.h.\n"
                                    "\n"
                                    "\n"
                                    "\n"
                                    "inline void user() { static_assert(hidden_alias::deep == 21); }\n"),
    "test/alias_file_scope.cpp": ("#include <fixy/Seen.h>\n"
                                  "namespace sp = fixy::seen_space;\n"
                                  "static_assert(sp::seen == 12);\n"),
    "test/alias_global.cpp": ("#include <fixy/Seen.h>\n"
                              "namespace gs = ::fixy::seen_space;\n"
                              "static_assert(gs::seen == 12);\n"),
    "test/alias_anonymous.cpp": ("#include <fixy/Seen.h>\n"
                                 "namespace {\n"
                                 "namespace an = fixy::seen_space;\n"
                                 "static_assert(an::seen == 12);\n"
                                 "}\n"),
    "test/alias_function.cpp": ("#include <fixy/Seen.h>\n"
                                "inline void user() {\n"
                                "    namespace fn_alias = fixy::seen_space;\n"
                                "    static_assert(fn_alias::seen == 12);\n"
                                "}\n"),
    "test/alias_chain.cpp": ("#include <fixy/Seen.h>\n"
                             "namespace first = fixy;\n"
                             "namespace second = first::seen_space;\n"
                             "static_assert(second::seen == 12);\n"),
    "test/alias_header.cpp": "#include <fixy/Aliased.h>\nnamespace fixy {\nstatic_assert(ahead::deep == 21);\n}\n",
    "test/alias_other_value.cpp": ("#include <fixy/Seen.h>\n"
                                   "namespace sp = fixy::seen_space;\n"
                                   "static_assert(sp::seen == 13);\n"),
    "test/alias_other_function.cpp": ("#include <fixy/Seen.h>\n"
                                      "inline void owner() { namespace hidden = fixy::seen_space; }\n"
                                      "inline void other() { static_assert(hidden::seen == 12); }\n"),
    "test/alias_elsewhere.cpp": ("#include <fixy/Seen.h>\n"
                                 "namespace elsewhere = fixy::other_space;\n"
                                 "static_assert(elsewhere::seen == 12);\n"),
    "test/alias_cycle.cpp": ("#include <fixy/Seen.h>\n"
                             "namespace loop_a = loop_b;\n"
                             "namespace loop_b = loop_a;\n"
                             "static_assert(loop_a::seen == 12);\n"),
}
ALIAS_PLANTS = (
    ("test/alias_file_scope.cpp", "seen", "a pin through an alias at file scope"),
    ("test/alias_global.cpp", "seen", "a pin through an alias whose target starts at the global scope"),
    ("test/alias_anonymous.cpp", "seen", "a pin through an alias of an unnamed namespace"),
    ("test/alias_function.cpp", "seen", "a pin through an alias of the function body of the pin"),
    ("test/alias_chain.cpp", "seen", "a pin through an alias of an alias"),
    ("test/alias_header.cpp", "deep", "a pin through an alias that a seen header declares"),
)
ALIAS_CONTROLS = (
    ("test/alias_other_value.cpp", "a pin through an alias against another literal"),
    ("test/alias_other_function.cpp", "a pin through an alias of another function body"),
    ("test/alias_elsewhere.cpp", "a pin through an alias of a namespace with no such constant"),
    ("test/alias_cycle.cpp", "a pin through a cycle of aliases"),
    ("test/alias_header_local.cpp", "a pin through an alias of a function body of a header"),
)


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
        reported = {line for rel, line, name, _, _ in found
                    if rel == "include/foundation/Planted.h" and f"{rel}:{name}" not in keys}
        for line, (_, caught, label) in enumerate(planted, start=1):
            if caught is True:
                expect(f"caught: {label}", line in reported)
            elif caught is False:
                expect(f"not caught: {label}", line not in reported, True)
        expect("nothing else in the planted header is reported",
               reported <= {line for line, (_, caught, _) in enumerate(planted, start=1) if caught}, True)
        expect("caught: a pin in src/", any(rel == "src/planted.cpp" for rel, _, _, _, _ in found))
        expect("not caught: a negative fixture", not any("sample_neg" in rel for rel, _, _, _, _ in found), True)
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
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in SEEN_HEADER_TREE.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        found, broken = scan(root)
        expect("the tree of the seen headers parses", not broken, True)
        for rel, label in SEEN_HEADER_PLANTS:
            expect(f"caught: {label}", any(pin[0] == rel and pin[2] == "seen" for pin in found))
        for rel, label in SEEN_HEADER_CONTROLS:
            expect(f"not caught: {label}", not any(pin[0] == rel for pin in found), True)
        expect("the report of a pin against a constant of a header names the header",
               any(pin[0] == "test/layer/checks/fixy/Seen.cpp" and pin[4] == ("include/fixy/Seen.h",)
                   for pin in found))
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in {**SEEN_HEADER_TREE, **ALIAS_TREE}.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        found, broken = scan(root)
        expect("the tree of the aliases parses", not broken, True)
        for rel, name, label in ALIAS_PLANTS:
            expect(f"caught: {label}", any(pin[0] == rel and pin[2] == name for pin in found))
        for rel, label in ALIAS_CONTROLS:
            expect(f"not caught: {label}", not any(pin[0] == rel for pin in found), True)
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
        print("usage: guard check --rule derived-pins [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-derived-pins: {exc}", file=sys.stderr)
        return 3
