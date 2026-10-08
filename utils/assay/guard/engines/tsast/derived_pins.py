"""derived-pins — a static_assert does not pin a constant to its own literal.

THE PATTERN
    The rule refuses this pattern:

        inline constexpr int surface_cardinality = 28;
        static_assert(surface_cardinality == 28, "the count and the list must move together");

    One hand writes the constant and the literal in the same edit, so the
    assertion compares a number with itself and cannot fail.  A pin is real
    only when one side is derived: a reflection query
    (`enumerators_of(^^E).size()`), a `tuple_size_v`, the `.size()` of an
    array or a `sizeof`.  Then a real edit moves one side and not the other.

THE RULE
    A pin is an `==` in the condition of a static_assert, also in a
    conjunction, between a name and an integer literal, in each order.  The
    rule refuses:
      vacuous-pin  a pin whose name gets to a constexpr variable with the same
                   integer as its initializer
    The name gets to the declaration of its innermost enclosing scope, as the
    compiler looks it up: a namespace, a class or a function body, above or
    below the pin, at each distance.  A name of a template parameter gets to
    no constant.  The entity of a finding is the name.

    The declaration can be in the file of the pin, or in a header that the pin
    can see.  The check file test/layer/checks/LAYER/PATH.cpp sees its header
    include/LAYER/PATH.h.  Each file sees each header of the scope that an
    #include of its tree names: include/PATH for an angle-bracket path, and
    the file beside it or include/PATH for a quoted path.  A header that the
    file includes through another header is not read.  A constant of a
    function body or of a class with no name is local to its header, so no
    other file sees it.

    A qualifier that starts with a namespace alias gets the target of the
    alias.  The alias of the innermost scope that encloses the pin wins, in
    the file or in a header that the pin can see.  An alias of an alias gets
    its target too, and a cycle of aliases gets to no constant.  An alias of
    a function body of a header is local to that header.

    An initializer and a literal are the same integer through a suffix, a
    digit separator, a base prefix, parentheses, a static_cast, a braced
    initializer and a conversion to a built-in type.  A different literal, a
    derived operand, a call and a comparison outside a static_assert are not
    pins.  A floor or a ceiling (`>=`, `<=`) is a bound, such as the limit of
    an API, and the rule does not read it.

THE SCOPE
    The scope of the registry, less the negative-compile fixtures of the
    directories test/NAME_neg/.  A pin in a macro body is not read.

Complexity: linear in the size of the files of the scope, plus the constants and the aliases of each seen header for
each file.
"""

import os
from collections import defaultdict
from collections.abc import Iterable, Iterator
from dataclasses import dataclass, replace
from pathlib import PurePosixPath

from assay.core import report as check_report
from assay.guard.engines.tsast.runner import TsastContext
from assay.readers import tsast

RULE = "derived-pins"
KIND = "vacuous-pin"
COMPARISONS = frozenset({"=="})
# The directory of the check files, and the directory of their headers.
CHECK_FILES = "test/layer/checks/"
CHECKED_HEADERS = "include/"
FIXTURE_SUFFIX = "_neg"
# The number of aliases that one qualifier goes through at most, so that a cycle of aliases ends.
ALIAS_DEPTH = 8


@dataclass(frozen=True, slots=True)
class LocalScope:
    """A scope that no other file can name: a function body or a class with no name, by the row of its start."""

    kind: str
    row: int


# A part of a scope: the name of a namespace or a class, or a local scope.
ScopePart = str | LocalScope
# A constant: its scope and its name, then each literal value with the file that declares it.
Constants = dict[tuple[tuple[ScopePart, ...], str], set[tuple[int, str]]]
# A namespace alias: its scope and its name, then whether its target starts at the global scope, and the parts of
# the target.
Aliases = dict[tuple[tuple[ScopePart, ...], str], tuple[bool, tuple[str, ...]]]


@dataclass(frozen=True, slots=True)
class Comparison:
    """An `==` in a static_assert between a name and an integer literal, with the facts of its lookup."""

    row: int
    is_global: bool
    qualifier: tuple[str, ...]
    name: str
    value: int
    scope: tuple[ScopePart, ...]
    is_template_parameter: bool


@dataclass(frozen=True, slots=True)
class FileFacts:
    """What the rule reads from one file: its constants, its aliases, the headers that it sees and its comparisons."""

    constants: Constants
    aliases: Aliases
    headers: tuple[str, ...]
    comparisons: tuple[Comparison, ...]


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
            # Only a spelling that is a conversion for certain: a built-in type or static_cast.  `T(12)` with a
            # class or a function name is a call.
            callee = node.child_by_field("function")
            name = callee.child_by_field("name") if callee is not None else None
            is_cast = callee is not None and (callee.type == "primitive_type" or (
                callee.type == "template_function" and name is not None and name.text == "static_cast"))
            node = node.child_by_field("arguments") if is_cast else None
        else:
            return None
    return None


def scope_of(node: tsast.Node) -> tuple[ScopePart, ...]:
    """Return the scopes that enclose a node, outermost first: namespaces, classes and function bodies.

    A function body has the row of its definition, so two functions never share a scope.
    """
    parts: list[ScopePart] = []
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
            # A specialization has the name of its template, and a class with a qualifier has each part of the
            # qualifier.
            named = owner.child_by_field("name")
            spelled = tsast.qualified_parts(named) if named is not None else None
            parts[:0] = list(spelled[1]) if spelled is not None else [LocalScope("class", owner.start[0])]
        elif owner.type in ("function_definition", "lambda_expression"):
            parts.insert(0, LocalScope("function", owner.start[0]))
        owner = owner.parent
    return tuple(parts)


def is_exported(scope: tuple[ScopePart, ...]) -> bool:
    """Return whether another file can see a declaration of a scope: no part of the scope is local."""
    return not any(isinstance(part, LocalScope) for part in scope)


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


def literal_constants(tree: tsast.Tree, path: str) -> Constants:
    """Return each constexpr variable of a file whose initializer is an integer literal, by scope and name.

    Args:
        tree: The parse tree of the file
        path: The path of the file, which each value gives as its origin
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
                found[(scope_of(node), named.text)].add((value, path))
    return found


def namespace_aliases(tree: tsast.Tree) -> Aliases:
    """Return each namespace alias of a file, by its scope and its name, with the target that it names.

    Complexity: linear in the number of nodes of the file.
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


def resolve_aliases(comparison: Comparison, aliases: Aliases) -> Comparison:
    """Return the comparison with the namespace alias at the head of its qualifier replaced by its target.

    The alias of the innermost scope that encloses the comparison wins, as the
    compiler looks it up.  The names of a target that does not start at the
    global scope resolve from the scope of the alias, so the comparison then
    looks up its name from that scope.  An alias whose target starts with
    another alias resolves again, at most ALIAS_DEPTH times.

    Complexity: O(ALIAS_DEPTH * the depth of the scope).
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
        comparison = replace(comparison, is_global=is_global, qualifier=target + comparison.qualifier[1:],
                             scope=found)
    return comparison


def header_of_check_file(path: str) -> str | None:
    """Return the header of a check file, test/layer/checks/LAYER/PATH.cpp, or None for another file."""
    if not path.startswith(CHECK_FILES) or not path.endswith(".cpp"):
        return None
    return CHECKED_HEADERS + path[len(CHECK_FILES):-len(".cpp")] + ".h"


def included_headers(tree: tsast.Tree, path: str, known: frozenset[str]) -> list[str]:
    """Return each C++ file of the tree that an #include of the parse tree names.

    An angle-bracket path names include/PATH.  A quoted path names the file
    beside the file that includes it, and then include/PATH.  A path that
    names no C++ file of the tree is not a header of the scope.

    Args:
        tree: The parse tree of the file
        path: The path of the file
        known: The C++ files of the tree
    """
    found: list[str] = []
    for include in tree.find("preproc_include"):
        target = include.child_by_field("path")
        if target is None or target.type not in ("system_lib_string", "string_literal"):
            continue
        inner = tsast.prose_text(target).strip()[1:-1].strip()
        places = [f"{CHECKED_HEADERS}{inner}"]
        if target.type == "string_literal":
            places.insert(0, (PurePosixPath(path).parent / inner).as_posix())
        for place in places:
            normal = os.path.normpath(place)
            if normal in known:
                found.append(normal)
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


def lookup(constants: Constants, comparison: Comparison) -> set[tuple[int, str]]:
    """Return the values and the origins of the declaration that the name of a comparison gets to.

    The innermost scope comes first.  A name of a template parameter gets to no constant.
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
    """Yield each `==` in a static_assert of a file between a name and an integer literal.

    Complexity: linear in the number of nodes of the file.
    """
    for assertion in tree.find("static_assert_declaration"):
        condition = assertion.child_by_field("condition")
        if condition is None:
            continue
        for node in [condition, *condition.descendants("binary_expression")]:
            if node.type != "binary_expression" or tsast.operator_of(node) not in COMPARISONS:
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


def is_fixture(path: str) -> bool:
    """Return whether a file is in a directory of negative-compile fixtures, test/NAME_neg/."""
    parts = PurePosixPath(path).parts
    return len(parts) > 2 and parts[0] == "test" and parts[1].endswith(FIXTURE_SUFFIX)


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Give an error for each vacuous pin of the scope.

    The rule reads each file one time.  Then it resolves the namespace alias
    at the head of the qualifier of each comparison, and it looks up the name
    in the constants of its file and of the headers that the file sees.
    """
    files = [path for path in context.scope_files() if not is_fixture(path)]
    known = frozenset(files)
    facts: dict[str, FileFacts] = {}
    for tree in context.trees(files):
        path = tree.path.as_posix()
        headers = included_headers(tree, path, known)
        checked = header_of_check_file(path)
        if checked is not None:
            headers.append(checked)
        facts[path] = FileFacts(literal_constants(tree, path), namespace_aliases(tree), tuple(dict.fromkeys(headers)),
                                tuple(comparisons(tree)))
    shared = {path: {key: values for key, values in fact.constants.items() if is_exported(key[0])}
              for path, fact in facts.items()}
    shared_aliases = {path: {key: target for key, target in fact.aliases.items() if is_exported(key[0])}
                      for path, fact in facts.items()}
    found: list[check_report.Finding] = []
    for path, fact in sorted(facts.items()):
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
        for row, name, value, origins in sorted(set(pins(resolved, constants))):
            found.append(check_report.Finding(
                "error", path, row + 1, RULE, f"`{name}` is pinned against the literal {value} that "
                f"{' and '.join(origins)} gives it, so the assertion cannot fail.  Derive one side (reflection, "
                f"tuple_size_v, .size()), or delete the pin when the count has no source to derive it from", KIND,
                name))
    return found
