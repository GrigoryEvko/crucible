#!/usr/bin/env python3
"""check-header-checks — a header does no compile-time work again in each translation unit that includes it.

A self-test namespace, a static_assert at namespace scope, a static_assert
in the body of a function that is not a template, and a constant
evaluation that runs where the header defines it all run again in each
translation unit that includes their header.  On the tree of 2026-10-01
the checks alone cost about half of the front-end time of a file.  Such a
check lives in the check file of its header, which one translation unit
compiles one time.  Such an evaluation is lazy: a variable template, or a
member of a template, so that only a translation unit that reads it
evaluates it.  A static_assert inside a class or a template stays in the
header, because it applies to each instantiation.

THE CHECK FILE
    The check file of include/<layer>/<path>.h is
    test/layer/checks/<layer>/<path>.cpp.  Its first line of code
    includes its own header, so the file also shows that the header
    compiles alone.  It holds the self-test namespaces and the
    namespace-scope static_asserts of the header, unchanged and inside the
    same enclosing namespaces, so that each name resolves as before.
    test/layer/CMakeLists.txt compiles the check file in place of the
    one-line sentinel of its header, against the include root of its
    layer, in each build.

THE FOUR KINDS
    The guard reads the parse tree of the pinned tree-sitter kit
    (utils/scripts/tsast.py) for each header under include/ and each file
    under test/layer/checks/.
      * namespace.  A self-test namespace is a namespace definition whose
        name has a segment with the word `test` in it, when the segment is
        split at `_`: x_self_test, self_test and fn_test are self-test
        namespaces.  `testing` is a different word, so
        foundation::effects::testing is not one.  A self-test namespace
        inside another one counts with the outer one.
      * static_assert.  A namespace-scope static_assert is a static_assert
        declaration that no class, function, lambda or other block holds.
        A namespace body, an extern "C++" body and an arm of a
        preprocessor conditional are namespace scope.  A static_assert
        inside a self-test namespace counts with that namespace, not
        alone.
      * function static_assert.  A static_assert in the body of a function
        or a lambda that is not in a template context.  The compiler
        evaluates it when it reads the body, in each includer.
      * eager evaluation.  A constant evaluation outside each template
        context, of one of these forms:
          - a variable at namespace scope, or a static data member, that
            is constexpr, constinit or const, and whose initializer holds
            an evaluated call
          - an alias whose type holds an evaluated call, for example in a
            splice
          - in the body of a function or a lambda: a constexpr or
            constinit variable whose initializer holds an evaluated call,
            or an expansion statement (`template for`), which the compiler
            expands when it reads the body.
        A call is evaluated when no sizeof, alignof, decltype, noexcept,
        requires-expression or lambda body holds it.  A cast is not a call:
        static_cast, const_cast, reinterpret_cast, dynamic_cast, bit_cast,
        and a functional cast through a type name such as `unsigned(3)`.
        A literal, an enumerator, sizeof, an arithmetic expression, a
        reflection `^^X` and a braced initializer with no call in it hold
        no call.  A const variable counts only when the object itself is
        const: `const char* p` does not count, and `const char* const p`
        does.
    A template context is an enclosing template declaration with
    parameters, an enclosing generic lambda (a lambda with a template
    parameter list or a parameter of a placeholder type), an enclosing
    abbreviated function template (a function with a parameter of a
    placeholder type) and an enclosing function with a requires-clause.
    An explicit specialization (`template <>`) is not a template context,
    because the compiler reads its body where it stands.

    A macro that makes a check.  The guard reads the replacement list of
    each #define of each header under include/ as preprocessing tokens, and
    counts the self-test namespaces and the static_asserts that it writes
    outside each brace that is not a namespace or an extern "C++" body.  A
    macro that names such a macro makes its checks too.  An invocation of
    such a macro at namespace scope in a header, outside a self-test
    namespace, counts each check of one expansion.  The key of each one is
    the spelling of the invocation.  A static_assert whose condition is the
    literal `true` checks nothing.  It is the device that makes a macro end
    in a semicolon at its call site, and it does not count.

THE LEDGER
    utils/scripts/header-checks-ledger.txt holds three kinds of row.
      path | self-test namespaces | namespace-scope static_asserts
      function static_assert | path | count
      eager evaluation | path | count
          The work that the header still does in each includer.  Each item
          of a row gives a warning on each run, so the debt stays visible.
          A count above its row is an error: move the new check to the
          check file, or make the new evaluation lazy.  A count below its
          row is an error too: regenerate the ledger with --write in the
          same commit.  A header with no item of a kind has no row of that
          kind.
      keep | path | kind | key | reason
          One item that stays in its header, because its result depends on
          the translation unit that includes the header.  The kind is one
          of the four kinds.  The key of a namespace is its full name, the
          key of a static_assert is the spelling of its condition, and the
          key of an eager evaluation is the qualified name of what it
          defines, all as --list prints them.  A keep row that names no
          item is an error, and a keep row with no reason is an error.  A
          reason does not contain ` | `.

THE REPORT
    Each finding is one line in the format of utils/scripts/check_report.py,
    under the check name header-checks.  With --warnings-dir DIR the guard
    also writes its warnings to DIR/header-checks.txt.

THE CHECK FILES
    Each file under test/layer/checks/ is a .cpp file, its header exists,
    and its first line of code includes that header.

THE HEADERS THAT CANNOT COMPILE ALONE
    test/layer/crucible-not-standalone.txt names each crucible header that
    has no sentinel, one row `crucible/<path>.h | reason` each.  With
    --standalone BUILD_DIR, the guard compiles each listed header alone,
    with the command of the crucible sentinels from the compile database of
    BUILD_DIR, and it fails for each header that compiles.  A header that
    compiles alone gets its sentinel back, so the list can only become
    shorter.

WHAT THE GUARD CANNOT SEE
    A macro body has no scope until the macro expands, so the guard counts
    the checks of a macro at each invocation and not at its #define.  A
    macro that a file outside include/ defines, and a name that a macro
    builds with `##`, are not read.  The guard does not read a function
    static_assert or an eager evaluation that a macro writes.  Every arm of
    an #if counts, because the kit does not preprocess.  The syntax cannot
    tell a cheap call from an expensive one, and it cannot see an eager
    instantiation, for example a non-template function that reads a
    variable template.  The test header_constexpr_ops measures the exact
    form: each header alone compiles at a low -fconstexpr-ops-limit.

Usage
    check-header-checks.py [--warnings-dir DIR]    compare the tree with the ledger
    check-header-checks.py --list                  print each item that a header holds
    check-header-checks.py --write                 write the count rows of the ledger again from the tree
    check-header-checks.py --standalone BUILD_DIR  compile each listed header alone and refuse one that compiles
    check-header-checks.py --self-test             plant each kind of item in a scratch tree and examine each verdict

Exit 0 with no error, 1 on an error finding (a new item, a stale or
malformed row, a bad check file, a parse failure or a missing include
directory), 1 when a listed header compiles alone, 2 on a missing compile
command, a usage error or a failed self-test, 3 when the kit is not
installed.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
import shlex
import shutil
import subprocess
import sys
import tempfile
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
import tsast  # noqa: E402

SCRIPT = "utils/scripts/check-header-checks.py"
LEDGER = "utils/scripts/header-checks-ledger.txt"
CHECK = "header-checks"
INCLUDE = "include"
CHECKS = "test/layer/checks"
NOT_STANDALONE = "test/layer/crucible-not-standalone.txt"
SENTINEL_TARGET = "layer_sentinel_crucible"
HEADER_SUFFIXES = (".h", ".hpp")
NAMESPACE = "namespace"
ASSERT = "static_assert"
FUNCTION_ASSERT = "function static_assert"
EAGER = "eager evaluation"
KINDS = (NAMESPACE, ASSERT, FUNCTION_ASSERT, EAGER)
# The kinds that have a count row of their own, `kind | path | count`.
ROW_KINDS = (FUNCTION_ASSERT, EAGER)
SEPARATOR = " | "
# The node types whose body is namespace scope.  A static_assert with an
# ancestor of any other type sits in a class, a function, a lambda or a
# block.
NAMESPACE_SCOPE = frozenset({
    "translation_unit", "declaration_list", "namespace_definition", "linkage_specification",
    "preproc_if", "preproc_ifdef", "preproc_else", "preproc_elif", "preproc_elifdef",
})
FUNCTION_BODIES = ("function_definition", "lambda_expression")
PARAMETER_TYPES = ("parameter_declaration", "optional_parameter_declaration", "variadic_parameter_declaration")
# The operands that the compiler does not evaluate.  A lambda body runs only
# when a call runs it, and the call is the call_expression around it.
UNEVALUATED = frozenset({
    "sizeof_expression", "alignof_expression", "decltype", "noexcept_expression", "requires_expression",
    "requires_clause", "lambda_expression",
})
CAST_NAMES = frozenset({"static_cast", "const_cast", "reinterpret_cast", "dynamic_cast", "bit_cast"})
# A call through a type name is a functional cast or a constructor of a literal type.
TYPE_CALLEES = frozenset({"primitive_type", "sized_type_specifier", "type_identifier", "template_type"})
CLASS_SPECIFIERS = ("class_specifier", "struct_specifier", "union_specifier")
LEDGER_HEADER = (
    "# utils/scripts/header-checks-ledger.txt — the compile-time work that each header under include/\n"
    "# still does in each translation unit that includes it.  utils/scripts/check-header-checks.py\n"
    "# reads this ledger.\n"
    "#\n"
    "# A header holds no self-test namespace, no static_assert at namespace scope and no static_assert\n"
    "# in the body of a function that is not a template.  Such a check belongs in the check file of the\n"
    "# header, which one translation unit compiles one time: test/layer/checks/<layer>/<path>.cpp for\n"
    "# include/<layer>/<path>.h.  The first line of code of a check file includes its header.  A\n"
    "# constant evaluation outside each template is lazy: a variable template, or a member of a template.\n"
    "#\n"
    "# A count row:  path | self-test namespaces | namespace-scope static_asserts\n"
    "#               function static_assert | path | count\n"
    "#               eager evaluation | path | count\n"
    "#   Each item of a row gives a warning on each run.  The counts can only decrease.  A header with no\n"
    "#   item of a kind has no row of that kind.  When you move a check or make an evaluation lazy, run\n"
    "#   python3 utils/scripts/check-header-checks.py --write in the same commit.\n"
    "#\n"
    "# A keep row:   keep | path | kind | key | reason\n"
    "#   One item that stays in its header, because its result depends on the translation unit that\n"
    "#   includes the header.  The kind is namespace, static_assert, function static_assert or eager\n"
    "#   evaluation.  The key is the one that --list prints.  The reason is mandatory.\n"
)


@dataclass(frozen=True)
class Check:
    """One item of compile-time work that a header does in each includer.

    via names the macro whose invocation makes the item, or is empty for an
    item that the header writes itself.
    """

    path: str
    row: int
    kind: str
    key: str
    via: str = ""


@dataclass(frozen=True)
class Keep:
    """One keep row of the ledger."""

    path: str
    kind: str
    key: str
    reason: str
    line: int


@dataclass
class Ledger:
    """The rows of the ledger, and one (line, message) for each malformed row.

    counts maps each header to the count of each kind that its rows permit,
    and lines maps (header, kind) to the line of the row that permits it.
    bad_keeps holds the messages of the malformed keep rows only, because
    --write keeps each keep row and cannot keep one that it cannot read.
    """

    counts: dict[str, dict[str, int]] = field(default_factory=dict)
    lines: dict[tuple[str, str], int] = field(default_factory=dict)
    keeps: list[Keep] = field(default_factory=list)
    malformed: list[tuple[int, str]] = field(default_factory=list)
    bad_keeps: list[tuple[int, str]] = field(default_factory=list)


@dataclass(frozen=True)
class MacroChecks:
    """The checks that one expansion of a macro writes at namespace scope."""

    namespaces: int
    asserts: int


@dataclass(frozen=True)
class MacroBody:
    """What the replacement list of one #define writes outside each brace that is not a namespace body.

    uses holds each identifier there, with True when `(` follows it, so a
    name of another macro can add the checks of that macro.
    """

    is_function_like: bool
    namespaces: int
    asserts: int
    uses: tuple[tuple[str, bool], ...]


@dataclass
class Scan:
    """What the guard reads from one tree.

    failures holds (path, message) for each file that the guard cannot read.
    """

    checks: list[Check]
    headers: frozenset[str]
    bad_check_files: list[tuple[str, str]]
    failures: list[tuple[str, str]]
    has_include: bool = True


def is_self_test_segment(segment: str) -> bool:
    """Say whether one segment of a namespace name names a self-test namespace.

    Args:
        segment: One identifier of a namespace name

    Returns:
        True when `test` is one of the words of the segment, split at `_`
    """
    return "test" in segment.split("_")


def is_at_namespace_scope(node: tsast.Node) -> bool:
    """Say whether no class, function, lambda or block holds a node.

    Complexity: linear in the depth of the node.

    Args:
        node: A static_assert_declaration, or the statement or invocation of a macro

    Returns:
        True when each ancestor is a namespace body, a linkage body, an arm
        of a preprocessor conditional or the translation unit
    """
    owner = node.parent
    while owner is not None:
        if owner.type not in NAMESPACE_SCOPE:
            return False
        owner = owner.parent
    return True


def has_placeholder_parameter(parameters: tsast.Node | None) -> bool:
    """Say whether a parameter list has a parameter of a placeholder type, such as `auto x` or `Concept auto x`.

    Args:
        parameters: A parameter_list, or None

    Returns:
        True when one parameter has a placeholder type
    """
    if parameters is None:
        return False
    for parameter in parameters.children_of_type(*PARAMETER_TYPES):
        written = parameter.child_by_field("type")
        if written is not None and written.type == "placeholder_type_specifier":
            return True
    return False


def is_in_template_context(node: tsast.Node) -> bool:
    """Say whether a node is in a template context, where the compiler reads it only at an instantiation.

    Complexity: linear in the depth of the node.

    Args:
        node: Any node

    Returns:
        True when a template declaration with parameters, a generic lambda,
        an abbreviated function template or a function with a
        requires-clause encloses the node
    """
    owner = node.parent
    while owner is not None:
        if owner.type == "template_declaration":
            parameters = owner.child_by_field("parameters")
            if parameters is not None and tsast.non_comment_children(parameters):
                return True
        elif owner.type == "lambda_expression":
            declarator = owner.child_by_field("declarator")
            if owner.child_by_field("template_parameters") is not None or (
                    declarator is not None and has_placeholder_parameter(declarator.child_by_field("parameters"))):
                return True
        elif owner.type == "function_definition":
            declarator = owner.child_by_field("declarator")
            if declarator is not None and next(declarator.descendants("requires_clause"), None) is not None:
                return True
            listed = tsast.parameters(owner)
            if listed and has_placeholder_parameter(listed[0][1].parent):
                return True
        owner = owner.parent
    return False


def is_in_function_body(node: tsast.Node) -> bool:
    """Say whether a function or a lambda encloses a node."""
    return node.ancestor_of_type(*FUNCTION_BODIES) is not None


def is_cast(call: tsast.Node) -> bool:
    """Say whether a call_expression is a cast and not a call.

    Args:
        call: A call_expression

    Returns:
        True for a named cast, for bit_cast and for a call through a type name
    """
    callee = call.child_by_field("function")
    if callee is None:
        return False
    return callee.type in TYPE_CALLEES or tsast.leaf_name(callee) in CAST_NAMES


def evaluated_calls(node: tsast.Node) -> list[tsast.Node]:
    """Return each call under a node that a constant evaluation of the node runs.

    Complexity: linear in the size of the subtree.

    Args:
        node: An initializer, a type or a range expression

    Returns:
        Each call_expression that no unevaluated operand holds and that is not a cast
    """
    found: list[tsast.Node] = []
    stack = [node]
    while stack:
        current = stack.pop()
        if current.type in UNEVALUATED:
            continue
        if current.type == "call_expression" and not is_cast(current):
            found.append(current)
        stack.extend(current.children)
    return found


def has_qualifier(declaration: tsast.Node, *words: str) -> bool:
    """Say whether a declaration has one of the given specifiers or qualifiers directly, such as constexpr."""
    return any(child.type in ("type_qualifier", "storage_class_specifier") and tsast.spelled(child) in words
               for child in declaration.children)


def is_const_object(declaration: tsast.Node, declarator: tsast.Node) -> bool:
    """Say whether the object that a declarator declares is const, and not only what it points to.

    Args:
        declaration: The declaration or field_declaration
        declarator: The declarator inside its init_declarator, or the declarator of a field

    Returns:
        True for `const T x`, `const T x[]` and `T* const x`
    """
    current = declarator
    while current is not None and current.type == "array_declarator":
        current = current.child_by_field("declarator")
    if current is None or current.type == "reference_declarator":
        return False
    if current.type == "pointer_declarator":
        return any(child.type == "type_qualifier" and tsast.spelled(child) == "const" for child in current.children)
    return has_qualifier(declaration, "const")


def owner_parts(node: tsast.Node) -> tuple[str, ...]:
    """Return the qualified name of the scope that holds a node: its namespaces, then its function or its classes.

    A node inside a lambda that no function holds gets the segment `<lambda>`.

    Args:
        node: Any node

    Returns:
        The segments, outermost first
    """
    parts = list(tsast.namespace_path(node))
    function = tsast.enclosing_function(node)
    if function is not None:
        parts.extend(function)
        return tuple(parts)
    classes: list[str] = []
    holder = node.ancestor_of_type(*CLASS_SPECIFIERS)
    while holder is not None:
        name = holder.child_by_field("name")
        text = None if name is None else tsast.leaf_name(name)
        classes.append(text or "<class>")
        holder = holder.ancestor_of_type(*CLASS_SPECIFIERS)
    parts.extend(reversed(classes))
    if node.ancestor_of_type("lambda_expression") is not None:
        parts.append("<lambda>")
    return tuple(parts)


def is_at_eager_scope(node: tsast.Node) -> bool:
    """Say whether each ancestor of a declaration is namespace scope or an explicit specialization.

    Complexity: linear in the depth of the node.
    """
    owner = node.parent
    while owner is not None:
        if owner.type not in NAMESPACE_SCOPE and owner.type != "template_declaration":
            return False
        owner = owner.parent
    return True


def eager_sites(tree: tsast.Tree) -> list[tuple[tsast.Node, str]]:
    """Return each eager evaluation of a header, with its key.

    Complexity: linear in the number of nodes of the file, times the depth of each candidate.

    Args:
        tree: The parse tree of a header

    Returns:
        (the node, the key) for each site, in source order
    """
    sites: list[tuple[tsast.Node, str]] = []
    for declaration in tree.find("declaration"):
        is_constant = has_qualifier(declaration, "constexpr", "constinit")
        if not is_constant and not has_qualifier(declaration, "const"):
            continue
        items = [(item.child_by_field("declarator"), item.child_by_field("value")) for item in declaration.children
                 if item.field == "declarator" and item.type == "init_declarator"]
        items = [(target, value) for target, value in items if target is not None and value is not None]
        if not items:
            continue
        is_local = is_in_function_body(declaration)
        if (is_local and not is_constant) or (not is_local and not is_at_eager_scope(declaration)) \
                or is_in_template_context(declaration):
            continue
        for target, value in items:
            if (is_constant or is_const_object(declaration, target)) and evaluated_calls(value):
                name = tsast.leaf_name(target) or tsast.spelled(target)
                sites.append((target, "::".join((*owner_parts(declaration), name))))
    for member in tree.find("field_declaration"):
        if not has_qualifier(member, "static"):
            continue
        is_constant = has_qualifier(member, "constexpr", "constinit")
        declared: tsast.Node | None = None
        counts = False
        for item in member.children:
            if item.field == "declarator":
                declared = item
                counts = is_constant or is_const_object(member, item)
            elif item.field == "default_value" and declared is not None and counts and evaluated_calls(item) \
                    and not is_in_template_context(member):
                name = tsast.leaf_name(declared) or tsast.spelled(declared)
                sites.append((declared, "::".join((*owner_parts(member), name))))
    for alias in tree.find("alias_declaration", "type_definition"):
        written = alias.child_by_field("type")
        if written is None or not evaluated_calls(written) or is_in_template_context(alias):
            continue
        named = alias.child_by_field("name") if alias.type == "alias_declaration" else alias.child_by_field("declarator")
        name = "" if named is None else tsast.leaf_name(named) or tsast.spelled(named)
        sites.append((alias, "::".join((*owner_parts(alias), name))))
    for expansion in tree.find("expansion_statement"):
        if is_in_template_context(expansion):
            continue
        source = expansion.child_by_field("right")
        shown = tsast.spelled(source) if source is not None else tsast.spelled(expansion)
        sites.append((expansion, "::".join(owner_parts(expansion)) + f"::template for({shown})"))
    sites.sort(key=lambda site: site[0].start)
    return sites


def tests_literal_true(texts: list[str]) -> bool:
    """Say whether the tokens of a static_assert, from its keyword on, test the literal `true` and nothing else."""
    return len(texts) >= 4 and texts[1] == "(" and texts[2] == "true" and texts[3] in (",", ")")


def macro_body(define: tsast.Node) -> MacroBody:
    """Read what the replacement list of one #define writes outside each brace that is not a namespace body.

    A brace that follows `namespace NAME` or `extern "STRING"` opens a
    namespace scope, and each other brace opens a class, a function, a
    block or an initializer.  A self-test namespace counts at its brace,
    and a check inside it counts with it.  A parameter of the macro is not
    a use of another macro.

    Complexity: linear in the number of tokens of the replacement list.

    Args:
        define: A preproc_def or preproc_function_def node

    Returns:
        The checks and the identifiers that the replacement list writes at namespace scope
    """
    values = [child for child in define.children if child.field == "value"]
    tokens = tsast.pp_tokens(define.tree.slice(values[0].start, values[-1].end), values[0].start[0]) if values else []
    texts = [token.text for token in tokens]
    parameters = frozenset(tsast.macro_parameters(define))
    braces: list[str] = []
    opening = "other"
    namespaces = 0
    asserts = 0
    uses: list[tuple[str, bool]] = []
    index = 0
    while index < len(tokens):
        text = texts[index]
        is_at_scope = all(kind == "namespace" for kind in braces)
        if text == "{":
            braces.append(opening)
            opening = "other"
        elif text == "}":
            if braces:
                braces.pop()
        elif text == "namespace":
            cursor = index + 1
            names: list[str] = []
            while cursor < len(tokens) and (tokens[cursor].kind == "identifier" or texts[cursor] == "::"):
                if tokens[cursor].kind == "identifier":
                    names.append(texts[cursor])
                cursor += 1
            if cursor < len(tokens) and texts[cursor] == "{":
                is_self_test = any(is_self_test_segment(name) for name in names)
                namespaces += is_self_test and is_at_scope
                opening = "self-test" if is_self_test else "namespace"
            index = cursor
            continue
        elif text == "extern" and index + 2 < len(tokens) and tokens[index + 1].kind == "string" \
                and texts[index + 2] == "{":
            opening = "namespace"
            index += 2
            continue
        elif text == "static_assert":
            asserts += is_at_scope and not tests_literal_true(texts[index:index + 4])
        elif tokens[index].kind == "identifier" and is_at_scope and text not in parameters:
            uses.append((text, index + 1 < len(tokens) and texts[index + 1] == "("))
        index += 1
    return MacroBody(define.type == "preproc_function_def", namespaces, asserts, tuple(uses))


def macro_table(trees: list[tsast.Tree]) -> dict[str, MacroChecks]:
    """Return the checks that one expansion of each macro of the headers writes, for each macro that writes one.

    A macro that names another macro adds the checks of that macro, and a
    function-like macro adds them only when `(` follows its name.  A name
    that two headers define takes the larger count of each kind.  A chain
    that names a macro again stops there, as the preprocessor stops.

    Complexity: linear in the tokens of the replacement lists, times the
    depth of the longest chain of macros.

    Args:
        trees: The parse trees of the headers

    Returns:
        The checks of each macro that writes at least one check
    """
    bodies: dict[str, list[MacroBody]] = {}
    for tree in trees:
        for define in tree.find("preproc_def", "preproc_function_def"):
            name = define.child_by_field("name")
            if name is not None:
                bodies.setdefault(tsast.spelled(name), []).append(macro_body(define))
    memo: dict[str, MacroChecks] = {}

    def total(name: str, active: frozenset[str]) -> MacroChecks:
        """Return the checks of one macro, with the macros that it names."""
        if name in memo:
            return memo[name]
        namespaces = 0
        asserts = 0
        for body in bodies[name]:
            own_namespaces, own_asserts = body.namespaces, body.asserts
            for used, is_called in body.uses:
                if used in active or used not in bodies:
                    continue
                if any(other.is_function_like for other in bodies[used]) and not is_called:
                    continue
                inner = total(used, active | {used})
                own_namespaces += inner.namespaces
                own_asserts += inner.asserts
            namespaces = max(namespaces, own_namespaces)
            asserts = max(asserts, own_asserts)
        memo[name] = MacroChecks(namespaces, asserts)
        return memo[name]

    table = {name: total(name, frozenset({name})) for name in bodies}
    return {name: checks for name, checks in table.items() if checks.namespaces or checks.asserts}


def macro_sites(tree: tsast.Tree) -> list[tuple[tsast.Node, str]]:
    """Return each invocation of a macro in a tree that can stand at namespace scope, with the name of the macro.

    The kit reads `NAME(...)` at namespace scope as a macro invocation, and
    `NAME;` as an expression statement.

    Args:
        tree: The parse tree of a header

    Returns:
        (the invocation or its statement, the name of the macro) for each one
    """
    sites: list[tuple[tsast.Node, str]] = []
    for node in tree.find("macro_invocation"):
        name = node.child_by_field("name")
        if name is not None:
            sites.append((node, tsast.spelled(name)))
    for node in tree.find("expression_statement"):
        items = tsast.non_comment_children(node)
        head = items[0] if items else None
        if head is not None and head.type == "call_expression":
            head = head.child_by_field("function")
        if head is not None and head.type == "identifier":
            sites.append((node, tsast.spelled(head)))
    return sites


def check_file_of(header: str) -> str:
    """Return the check file of a header.

    Args:
        header: A repo-relative path under include/

    Returns:
        test/layer/checks/<layer>/<path>.cpp for include/<layer>/<path>.h
    """
    inner = Path(header).relative_to(INCLUDE)
    return (Path(CHECKS) / inner.with_suffix(".cpp")).as_posix()


def header_of_check(root: Path, check_file: str) -> str | None:
    """Return the header of a check file, or None when no header of that name exists.

    Args:
        root: The repository root
        check_file: A repo-relative path under test/layer/checks/

    Returns:
        The repo-relative header path
    """
    inner = Path(check_file).relative_to(CHECKS)
    for suffix in HEADER_SUFFIXES:
        header = (Path(INCLUDE) / inner.with_suffix(suffix)).as_posix()
        if (root / header).is_file():
            return header
    return None


def header_checks(tree: tsast.Tree, rel: str, macros: dict[str, MacroChecks]) -> tuple[list[Check], list[str]]:
    """Return each item of compile-time work that one header does in each includer.

    Complexity: linear in the number of nodes of the file, times the depth of each candidate.

    Args:
        tree: The parse tree of the header
        rel: The header, relative to the repository root
        macros: The checks of each macro of the headers that writes one

    Returns:
        The items in source order, and one message for each namespace whose
        name the guard cannot read
    """
    checks: list[Check] = []
    unread: list[str] = []
    for node in tree.find("namespace_definition"):
        name = node.child_by_field("name")
        if name is None:
            continue
        parts = tsast.qualified_parts(name)
        if parts is None:
            unread.append(f"the guard cannot read the name of the namespace at line {node.line}, so it cannot tell "
                          f"whether it is a self-test namespace")
            continue
        enclosing = tsast.namespace_path(node)
        if any(is_self_test_segment(segment) for segment in enclosing):
            continue
        if any(is_self_test_segment(segment) for segment in parts[1]):
            checks.append(Check(rel, node.line, NAMESPACE, "::".join((*enclosing, *parts[1]))))
    for node in tree.find("static_assert_declaration"):
        condition = node.child_by_field("condition")
        key = tsast.spelled(condition if condition is not None else node)
        if key == "true":
            continue
        if is_at_namespace_scope(node):
            if not any(is_self_test_segment(segment) for segment in tsast.namespace_path(node)):
                checks.append(Check(rel, node.line, ASSERT, key))
        elif is_in_function_body(node) and not is_in_template_context(node):
            checks.append(Check(rel, node.line, FUNCTION_ASSERT, key))
    for site, name in macro_sites(tree):
        made = macros.get(name)
        if made is None or not is_at_namespace_scope(site):
            continue
        if any(is_self_test_segment(segment) for segment in tsast.namespace_path(site)):
            continue
        key = tsast.spelled(site).removesuffix(";")
        checks.extend([Check(rel, site.line, NAMESPACE, key, name)] * made.namespaces)
        checks.extend([Check(rel, site.line, ASSERT, key, name)] * made.asserts)
    for site, key in eager_sites(tree):
        if not any(is_self_test_segment(segment) for segment in tsast.namespace_path(site)):
            checks.append(Check(rel, site.line, EAGER, key))
    checks.sort(key=lambda check: (check.row, KINDS.index(check.kind)))
    return checks, unread


def first_include(tree: tsast.Tree) -> str | None:
    """Return the path that the first line of code of a file includes, or None.

    Args:
        tree: The parse tree of a check file

    Returns:
        The include path without its delimiters, or None when the first
        item that is not a comment is not an include of a written path
    """
    items = tsast.non_comment_children(tree.root)
    if not items or items[0].type != "preproc_include":
        return None
    target = items[0].child_by_field("path")
    if target is None or target.type not in ("system_lib_string", "string_literal"):
        return None
    return tsast.prose_text(target).strip()[1:-1].strip()


def scan(root: Path) -> Scan:
    """Read every header under include/ and every file under test/layer/checks/.

    Complexity: linear in the total size of the files read.

    Args:
        root: The repository root

    Returns:
        The items, the set of headers, each bad check file and each parse failure

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    base = root / INCLUDE
    headers = sorted(path for path in base.rglob("*") if path.is_file() and path.suffix in HEADER_SUFFIXES
                     and tsast.is_in_cpp_scope(path.relative_to(root))) if base.is_dir() else []
    checks: list[Check] = []
    failures: list[tuple[str, str]] = []
    trees: list[tsast.Tree] = []
    for tree in tsast.parse(headers, strict=False):
        if tree.diagnostic is not None:
            failures.append((Path(tree.path).relative_to(root).as_posix(),
                             f"the parser cannot read this file, so the guard cannot count its items.  "
                             f"{' '.join(tree.diagnostic.split())}"))
            continue
        trees.append(tree)
    macros = macro_table(trees)
    for tree in trees:
        rel = Path(tree.path).relative_to(root).as_posix()
        found, unread = header_checks(tree, rel, macros)
        checks.extend(found)
        failures.extend((rel, message) for message in unread)

    bad: list[tuple[str, str]] = []
    check_root = root / CHECKS
    sources: list[Path] = []
    expected: dict[str, str] = {}
    for path in sorted(check_root.rglob("*")) if check_root.is_dir() else []:
        if not path.is_file():
            continue
        rel = path.relative_to(root).as_posix()
        if path.suffix != ".cpp":
            bad.append((rel, f"a check file is a .cpp file: {CHECKS}/<layer>/<path>.cpp for "
                             f"include/<layer>/<path>.h.  Rename the file, or move it out of {CHECKS}/."))
            continue
        header = header_of_check(root, rel)
        if header is None:
            bad.append((rel, f"no header include/{Path(rel).relative_to(CHECKS).with_suffix('.h').as_posix()} "
                             f"exists.  A check file belongs to one header.  Move it with its header, or delete it."))
            continue
        sources.append(path)
        expected[rel] = Path(header).relative_to(INCLUDE).as_posix()
    for tree in tsast.parse(sources, strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            failures.append((rel, f"the parser cannot read this check file.  {' '.join(tree.diagnostic.split())}"))
            continue
        included = first_include(tree)
        if included != expected[rel]:
            shown = f"<{included}>" if included is not None else "no written include path"
            bad.append((rel, f"the first line of code includes {shown}, not <{expected[rel]}>.  The first line "
                             f"of code of a check file includes its own header, so the file shows that the header "
                             f"compiles alone."))
    return Scan(checks, frozenset(Path(path).relative_to(root).as_posix() for path in headers), bad, failures,
                base.is_dir())


def read_ledger(path: Path) -> Ledger:
    """Read the ledger.

    Args:
        path: The ledger file

    Returns:
        The count rows, the keep rows and one (line, message) for each malformed row
    """
    ledger = Ledger()
    if not path.is_file():
        return ledger
    shape = (f"A count row is `path{SEPARATOR}self-test namespaces{SEPARATOR}static_asserts` or "
             f"`{FUNCTION_ASSERT} or {EAGER}{SEPARATOR}path{SEPARATOR}count`, and a keep row is "
             f"`keep{SEPARATOR}path{SEPARATOR}kind{SEPARATOR}key{SEPARATOR}reason`.")
    keys: set[tuple[str, str, str]] = set()
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        # The space after the entry keeps an empty last cell, so that a
        # keep row that ends in its separator has an empty reason.
        cells = [cell.strip() for cell in (entry + " ").split(SEPARATOR)]
        if cells[0] == "keep":
            problem = ""
            if len(cells) < 5 or cells[2] not in KINDS or not cells[1].startswith(f"{INCLUDE}/"):
                problem = f"the row is malformed: {entry}  {shape}"
            elif not cells[-1]:
                problem = "the keep row gives no reason.  Say why the item depends on the translation unit that " \
                          "includes the header."
            elif (cells[1], cells[2], SEPARATOR.join(cells[3:-1])) in keys:
                problem = "a keep row for this item comes before this one.  Remove one of them."
            if problem:
                ledger.malformed.append((number, problem))
                ledger.bad_keeps.append((number, problem))
                continue
            keep = Keep(cells[1], cells[2], SEPARATOR.join(cells[3:-1]), cells[-1], number)
            keys.add((keep.path, keep.kind, keep.key))
            ledger.keeps.append(keep)
            continue
        if cells[0] in ROW_KINDS:
            if len(cells) != 3 or not cells[1].startswith(f"{INCLUDE}/") or not cells[2].isdigit():
                ledger.malformed.append((number, f"the row is malformed: {entry}  {shape}"))
                continue
            permitted = {cells[0]: int(cells[2])}
            header = cells[1]
        else:
            if len(cells) != 3 or not cells[1].isdigit() or not cells[2].isdigit():
                ledger.malformed.append((number, f"the row is malformed: {entry}  {shape}"))
                continue
            permitted = {NAMESPACE: int(cells[1]), ASSERT: int(cells[2])}
            header = cells[0]
        if not any(permitted.values()):
            ledger.malformed.append((number, f"the row counts no item.  A header with no item of a kind has no row "
                                             f"of that kind.  Run: python3 {SCRIPT} --write"))
            continue
        slot = ledger.counts.setdefault(header, {})
        if any(kind in slot for kind in permitted):
            ledger.malformed.append((number, f"{header} has a row of this kind before this one.  Remove one of "
                                             f"them, or run: python3 {SCRIPT} --write"))
            continue
        slot.update(permitted)
        for kind in permitted:
            ledger.lines[(header, kind)] = number
    return ledger


def apply_keeps(checks: list[Check], keeps: list[Keep]) -> tuple[list[Check], list[Keep]]:
    """Remove each kept item.

    Args:
        checks: Every item of the tree
        keeps: The keep rows

    Returns:
        The items that no keep row names, and each keep row that names no item
    """
    wanted = {(keep.path, keep.kind, keep.key) for keep in keeps}
    matched: set[tuple[str, str, str]] = set()
    remaining: list[Check] = []
    for check in checks:
        identity = (check.path, check.kind, check.key)
        if identity in wanted:
            matched.add(identity)
        else:
            remaining.append(check)
    stale = [keep for keep in keeps if (keep.path, keep.kind, keep.key) not in matched]
    return remaining, stale


def per_header(checks: list[Check]) -> dict[str, dict[str, int]]:
    """Count the items of each kind in each header."""
    counts: dict[str, dict[str, int]] = {}
    for check in checks:
        slot = counts.setdefault(check.path, {})
        slot[check.kind] = slot.get(check.kind, 0) + 1
    return counts


def describe(check: Check, count: int = 1) -> str:
    """Return the sentence that names one item, its cost and its repair.

    Args:
        check: The item
        count: The number of equal items that one invocation of a macro makes
    """
    target = check_file_of(check.path)
    if check.via:
        made = "self-test namespace(s)" if check.kind == NAMESPACE else "namespace-scope static_assert(s)"
        return (f"the invocation of the macro {check.via} makes {count} {made}, and each one runs again in each "
                f"translation unit that includes this header.  Move each check that the macro writes to {target}")
    if check.kind == NAMESPACE:
        return (f"the self-test namespace {check.key} runs again in each translation unit that includes this header.  "
                f"Move it to {target}")
    if check.kind == ASSERT:
        return (f"the namespace-scope static_assert({check.key}) runs again in each translation unit that includes "
                f"this header.  Move it to {target}")
    if check.kind == FUNCTION_ASSERT:
        return (f"the static_assert({check.key}) in the body of a function that is not a template runs again in each "
                f"translation unit that includes this header.  Move it to {target}, or make the function a template")
    return (f"the constant evaluation of {check.key} runs again in each translation unit that includes this header.  "
            f"Make it a variable template or a member of a template, or move it to {target}")


def check(root: Path, ledger_path: Path, warnings_dir: Path | None = None) -> int:
    """Compare the tree with the ledger and print each finding.

    Args:
        root: The repository root
        ledger_path: The ledger file
        warnings_dir: The warnings directory of check_report, or None

    Returns:
        0 with no error finding, 1 with one or more
    """
    found = scan(root)
    ledger = read_ledger(ledger_path)
    checks, stale_keeps = apply_keeps(found.checks, ledger.keeps)
    counts = per_header(checks)
    findings: list[check_report.Finding] = []

    def report(level: check_report.Level, path: str, line: int, message: str) -> None:
        """Add one finding."""
        findings.append(check_report.Finding(level, path, line, CHECK, message))

    if not found.has_include:
        report("error", INCLUDE, 0, f"the directory {INCLUDE}/ does not exist, so the guard has no header to read")
    over = 0
    stale = 0
    grouped = Counter(checks)
    for path in sorted(set(counts) | set(ledger.counts)):
        if path in ledger.counts and path not in found.headers:
            stale += 1
            line = min(number for (header, _kind), number in ledger.lines.items() if header == path)
            report("error", LEDGER, line, f"the row names {path}, which is not a header under {INCLUDE}/.  Run: "
                                          f"python3 {SCRIPT} --write")
            continue
        for kind in KINDS:
            have = counts.get(path, {}).get(kind, 0)
            allowed = ledger.counts.get(path, {}).get(kind, 0)
            held = [(item, number) for item, number in grouped.items() if item.path == path and item.kind == kind]
            if have > allowed:
                over += 1
                for item, number in held:
                    report("error", path, item.row,
                           f"{describe(item, number)}.  The header holds {have} item(s) of the kind {kind}, and the "
                           f"ledger permits {allowed}.  An item whose result depends on the translation unit that "
                           f"includes the header can stay, with a keep row and its reason.")
                continue
            for item, number in held:
                report("warning", path, item.row, f"{describe(item, number)}.  The ledger holds it as debt.")
            if have < allowed:
                stale += 1
                report("error", LEDGER, ledger.lines[(path, kind)],
                       f"{path} holds {have} item(s) of the kind {kind}, and its row permits {allowed}.  Regenerate "
                       f"the ledger in the same commit: python3 {SCRIPT} --write")
    for keep in stale_keeps:
        stale += 1
        report("error", LEDGER, keep.line, f"the keep row names no {keep.kind} {keep.key} in {keep.path}.  Remove the "
                                           f"row, or correct its key: python3 {SCRIPT} --list prints each key.")
    for path, message in found.bad_check_files:
        report("error", path, 0, message)
    for path, message in found.failures:
        report("error", path, 0, message)
    for line, message in ledger.malformed:
        report("error", LEDGER, line, message)
    total = per_header(found.checks)
    shown = ", ".join(f"{sum(slot.get(kind, 0) for slot in total.values())} {kind}" for kind in KINDS)
    print(f"check-header-checks: {len(total)} header(s) hold {shown}, {len(found.checks) - len(checks)} of them "
          f"kept.  {over} over, {stale} stale, {len(found.bad_check_files)} bad check file(s).", file=sys.stderr)
    return check_report.emit(findings, CHECK, warnings_dir)


def write(root: Path, ledger_path: Path) -> int:
    """Write the count rows of the ledger again from the tree, and keep each keep row.

    Args:
        root: The repository root
        ledger_path: The ledger file

    Returns:
        0 when the ledger is written, 1 when a parse failure or a malformed keep row stops the write
    """
    found = scan(root)
    ledger = read_ledger(ledger_path)
    if found.failures or ledger.bad_keeps:
        for path, message in found.failures:
            print(f"{path}: {message}", file=sys.stderr)
        for line, message in ledger.bad_keeps:
            print(f"{LEDGER}:{line}: {message}", file=sys.stderr)
        print("check-header-checks: --write does not write the ledger while a file does not parse or a keep row "
              "is malformed.", file=sys.stderr)
        return 1
    checks, _stale_keeps = apply_keeps(found.checks, ledger.keeps)
    counts = per_header(checks)
    rows = [f"{path}{SEPARATOR}{slot.get(NAMESPACE, 0)}{SEPARATOR}{slot.get(ASSERT, 0)}\n"
            for path, slot in sorted(counts.items()) if slot.get(NAMESPACE, 0) or slot.get(ASSERT, 0)]
    for kind in ROW_KINDS:
        rows.extend(f"{kind}{SEPARATOR}{path}{SEPARATOR}{slot[kind]}\n"
                    for path, slot in sorted(counts.items()) if slot.get(kind, 0))
    keeps = [f"keep{SEPARATOR}{keep.path}{SEPARATOR}{keep.kind}{SEPARATOR}{keep.key}{SEPARATOR}{keep.reason}\n"
             for keep in sorted(ledger.keeps, key=lambda keep: (keep.path, keep.kind, keep.key))]
    ledger_path.parent.mkdir(parents=True, exist_ok=True)
    ledger_path.write_text(LEDGER_HEADER + "".join(rows) + ("\n" + "".join(keeps) if keeps else ""),
                           encoding="utf-8")
    print(f"check-header-checks: ledger written with {len(rows)} count row(s) and {len(keeps)} keep row(s).",
          file=sys.stderr)
    return 0


def list_checks(root: Path, ledger_path: Path) -> int:
    """Print each item that a header holds, with the key that a keep row names.

    Returns:
        0, or 1 on a parse failure
    """
    found = scan(root)
    kept = {(keep.path, keep.kind, keep.key) for keep in read_ledger(ledger_path).keeps}
    for item in found.checks:
        marker = "KEPT" if (item.path, item.kind, item.key) in kept else "HELD"
        print(f"{marker}  {item.path}:{item.row}  {item.kind}  {item.key}")
    for path, message in found.failures:
        print(f"{path}: {message}")
    return 1 if found.failures else 0


def read_not_standalone(root: Path) -> tuple[list[tuple[str, int]], list[str]]:
    """Read the list of the crucible headers that cannot compile alone.

    Args:
        root: The repository root

    Returns:
        (header, line) for each row, with the header relative to include/,
        and one message for each malformed row
    """
    rows: list[tuple[str, int]] = []
    problems: list[str] = []
    path = root / NOT_STANDALONE
    if not path.is_file():
        return rows, [f"MALFORMED {NOT_STANDALONE}: the list does not exist."]
    seen: set[str] = set()
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        header, separator, reason = entry.partition(SEPARATOR)
        header = header.strip()
        where = f"{NOT_STANDALONE}:{number}"
        if not separator or not reason.strip():
            problems.append(f"MALFORMED {where}: {entry}  A row is `crucible/<path>.h{SEPARATOR}reason`, and the "
                            f"reason is mandatory.")
        elif not header.startswith("crucible/") or not (root / INCLUDE / header).is_file():
            problems.append(f"STALE     {where}: {header} is not a header under include/crucible.  Remove the row.")
        elif header in seen:
            problems.append(f"DUPLICATE {where}: {header} has a row before this one.")
        else:
            seen.add(header)
            rows.append((header, number))
    return rows, problems


def sentinel_command(build_dir: Path) -> tuple[list[str], str, str] | None:
    """Return the compile command of one crucible sentinel from the compile database.

    Args:
        build_dir: A configured build directory

    Returns:
        (argv, source, directory), or None when the database holds no unit
        of the crucible sentinels
    """
    database = build_dir / "compile_commands.json"
    if not database.is_file():
        return None
    for row in json.loads(database.read_text(encoding="utf-8")):
        if f"/CMakeFiles/{SENTINEL_TARGET}.dir/" in row.get("output", ""):
            argv = row.get("arguments") or shlex.split(row["command"])
            return argv, row["file"], row["directory"]
    return None


def syntax_argv(argv: list[str], source: str, replacement: Path) -> list[str]:
    """Return a compile command that only checks the syntax of another source.

    The output and dependency-file flags go, so the run writes nothing.

    Args:
        argv: The compile command of one unit
        source: The source path that the command names
        replacement: The source to compile in its place

    Returns:
        The command with -fsyntax-only
    """
    out: list[str] = []
    index = 0
    while index < len(argv):
        flag = argv[index]
        if flag in ("-o", "-MF", "-MT", "-MQ"):
            index += 2
            continue
        if flag in ("-c", "-MD", "-MMD", "-MP"):
            index += 1
            continue
        out.append(str(replacement) if flag == source else flag)
        index += 1
    return out + ["-fsyntax-only"]


def standalone(root: Path, build_dir: Path) -> int:
    """Compile each listed header alone, and refuse each one that compiles.

    Complexity: one compiler run for each row, run in parallel.

    Args:
        root: The repository root
        build_dir: A configured build directory

    Returns:
        0 when each listed header still fails, 1 when one compiles, 2 on a
        malformed row or a missing compile command
    """
    rows, problems = read_not_standalone(root)
    for line in problems:
        print(line, file=sys.stderr)
    found = sentinel_command(build_dir)
    if found is None:
        print(f"check-header-checks: {build_dir}/compile_commands.json holds no unit of {SENTINEL_TARGET}.  "
              f"Configure the build first.", file=sys.stderr)
        return 2
    argv, source, directory = found
    with tempfile.TemporaryDirectory(prefix="header-checks-") as work:

        def compiles(item: tuple[int, tuple[str, int]]) -> bool:
            """Say whether one listed header compiles alone."""
            index, (header, _line) = item
            unit = Path(work) / f"alone_{index}.cpp"
            unit.write_text(f"#include <{header}>\n", encoding="utf-8")
            done = subprocess.run(syntax_argv(argv, source, unit), cwd=directory, capture_output=True, check=False)
            return done.returncode == 0

        with ThreadPoolExecutor(max_workers=min(16, max(1, len(rows)))) as pool:
            verdicts = list(pool.map(compiles, enumerate(rows)))
    alone = [row for row, verdict in zip(rows, verdicts) if verdict]
    for header, line in alone:
        print(f"STANDALONE {NOT_STANDALONE}:{line}: include/{header} compiles alone.  Remove its row in this commit, "
              f"so that the header gets its sentinel.", file=sys.stderr)
    print(f"check-header-checks: {len(rows)} listed header(s), {len(alone)} of them compile alone.",
          file=sys.stderr)
    if alone:
        return 1
    return 2 if problems else 0


# Each line of the planted header, and how the guard reads it: the kind of
# item that the line holds, or None for a line that counts nothing.
PLANTED: list[tuple[str, str | None, str]] = [
    ("#pragma once", None, ""),
    ("namespace foundation {", None, ""),
    ("static_assert(sizeof(int) == 4);", ASSERT, "a static_assert in a namespace body"),
    ("struct Holder { static_assert(sizeof(Holder*) == 8); };", None, "a static_assert in a class"),
    ("template <class T> struct Box { static_assert(sizeof(T) > 0); };", None, "a static_assert in a template"),
    ("inline void run() { static_assert(1 == 1); }", FUNCTION_ASSERT, "a static_assert in a function body"),
    ("inline constexpr int probe = [] { static_assert(2 == 2); return 0; }();", FUNCTION_ASSERT,
     "a static_assert in a lambda body at namespace scope"),
    ("// static_assert(false); in a comment", None, "a static_assert in a comment"),
    ('inline const char* text = "static_assert(false);";', None, "a static_assert in a string"),
    ("#if defined(PLANTED)", None, ""),
    ("static_assert(3 == 3);", ASSERT, "a static_assert in an arm of a preprocessor conditional"),
    ("#endif", None, ""),
    ('extern "C++" { static_assert(4 == 4); }', ASSERT, "a static_assert in an extern \"C++\" body"),
    ("namespace detail::planted_self_test {", NAMESPACE, "a namespace whose name ends in _self_test"),
    ("static_assert(5 == 5);", None, "a static_assert inside a self-test namespace"),
    ("namespace inner_self_test { static_assert(6 == 6); }", None,
     "a self-test namespace inside a self-test namespace"),
    ("inline constexpr int inside_self_test = compute();", None, "an eager evaluation inside a self-test namespace"),
    ("}  // namespace detail::planted_self_test", None, ""),
    ("namespace self_test { struct Probe {}; }", NAMESPACE, "a namespace named self_test"),
    ("namespace fn_test { inline void f0() {} }", NAMESPACE, "a namespace whose name ends in _test"),
    ("namespace testing { struct Witness; }", None, "a namespace named testing"),
    ("namespace contest { struct Entry {}; }", None, "a namespace whose name only contains test"),
    ("struct Member { void act() const { static_assert(10 == 10); } };", FUNCTION_ASSERT,
     "a static_assert in a member function of a class"),
    ("consteval bool walk() { static_assert(11 == 11); return true; }", FUNCTION_ASSERT,
     "a static_assert in a consteval function"),
    ("template <> inline void special<int>() { static_assert(12 == 12); }", FUNCTION_ASSERT,
     "a static_assert in an explicit specialization"),
    ("template <class T> void generic() { static_assert(13 == 13); }", None,
     "a static_assert in a function template"),
    ("template <class T> struct Shell { void act() { static_assert(14 == 14); } };", None,
     "a static_assert in a member function of a class template"),
    ("inline auto generic_lambda = [](auto value) { static_assert(15 == 15); return value; };", None,
     "a static_assert in a generic lambda"),
    ("inline auto listed_lambda = []<class T>(T value) { static_assert(16 == 16); return value; };", None,
     "a static_assert in a lambda with a template parameter list"),
    ("inline void abbreviated(auto value) { static_assert(17 == 17); (void)value; }", None,
     "a static_assert in an abbreviated function template"),
    ("inline void constrained() requires true { static_assert(18 == 18); }", None,
     "a static_assert in a function with a requires-clause"),
    ("inline constexpr int eager_call = compute();", EAGER, "a constexpr variable whose initializer calls"),
    ("inline constexpr unsigned members = std::meta::members_of(^^Holder, ctx).size();", EAGER,
     "a constexpr variable whose initializer walks reflection"),
    ("constinit int bound = compute();", EAGER, "a constinit variable whose initializer calls"),
    ("const int table_size = compute();", EAGER, "a const variable whose initializer calls"),
    ("inline const char* const fixed_name = name_of();", EAGER, "a const pointer whose initializer calls"),
    ("inline const char* moving_name = name_of();", None, "a pointer to const whose initializer calls"),
    ("struct Counted { static constexpr int count = compute(); static constexpr int plain = 3; };", EAGER,
     "a static data member whose initializer calls"),
    ("using Spliced = [:pick_type():];", EAGER, "an alias whose splice calls"),
    ("inline void local_walk() { constexpr auto total = compute(); (void)total; }", EAGER,
     "a constexpr local of a function whose initializer calls"),
    ("consteval bool expand() { template for (constexpr auto item : items) { (void)item; } return true; }", EAGER,
     "an expansion statement in a function body"),
    ("template <> inline constexpr int lazy<int> = compute();", EAGER,
     "an explicit specialization of a variable template whose initializer calls"),
    ("inline constexpr int literal = 3 + 4 * 2;", None, "an arithmetic expression on literals"),
    ("inline constexpr int casts = static_cast<int>(Kind::one) + int(2) + unsigned(3) + sizeof(Holder);", None,
     "a cast, a functional cast, an enumerator and sizeof"),
    ("inline constexpr auto bits = std::bit_cast<unsigned>(1.0f);", None, "a bit_cast"),
    ("inline constexpr bool unevaluated = noexcept(compute()) && sizeof(decltype(compute())) > 0;", None,
     "a call in noexcept, decltype and sizeof"),
    ("inline constexpr bool required = requires { compute(); };", None, "a call in a requires-expression"),
    ("inline constexpr auto deferred = [] { return compute(); };", None, "a call in a lambda that no code calls"),
    ("inline constexpr auto reflected = ^^Holder;", None, "a reflection with no call"),
    ("inline constexpr Box<int> braced{1, 2};", None, "a braced initializer with no call"),
    ("template <class T> inline constexpr int lazy = compute<T>();", None, "a variable template"),
    ("template <class T> struct Lazy { static constexpr int count = compute<T>(); };", None,
     "a static data member of a class template"),
    ("template <class T> void lazy_local() { constexpr auto total = compute<T>(); (void)total; }", None,
     "a constexpr local of a function template"),
    ("inline auto lazy_expand = [](auto pack) { template for (constexpr auto item : pack) { (void)item; } };", None,
     "an expansion statement in a generic lambda"),
    ("inline void runtime_local() { const int value = compute(); (void)value; }", None,
     "a const local of a function, which needs no constant evaluation"),
    ("}  // namespace foundation", None, ""),
    ('static_assert(7 == 7, "global scope");', ASSERT, "a static_assert at global scope"),
    ("#define PLANTED_CHECK static_assert(8 == 8)", None, "a static_assert in a macro body"),
    ("#define PLANTED_PAIR(T) static_assert(sizeof(T) > 0, #T); static_assert(alignof(T) > 0)", None,
     "two static_asserts in the body of a function-like macro"),
    ("#define PLANTED_NESTED(T) PLANTED_PAIR(T)", None, "a macro body that names another macro"),
    ('#define PLANTED_TRUE(T) struct T##_tag {}; static_assert(true, "a semicolon at the call site")', None,
     "a macro body whose one static_assert tests the literal true"),
    ("#define PLANTED_IN_CLASS(T) struct T##_holder { static_assert(sizeof(T) > 0); }", None,
     "a macro body that writes a static_assert inside a class"),
    ("#define PLANTED_SELF_TEST namespace planted_macro_self_test { static_assert(9 == 9); }", None,
     "a macro body that writes a self-test namespace"),
    ("PLANTED_CHECK;", ASSERT, "an object-like macro that writes a static_assert, at global scope"),
    ("namespace foundation {", None, ""),
    ("PLANTED_PAIR(int);", ASSERT, "a function-like macro that writes two static_asserts"),
    ("PLANTED_NESTED(long)", ASSERT, "a macro that names a macro that writes static_asserts"),
    ("PLANTED_TRUE(planted);", None, "a macro whose one static_assert tests the literal true"),
    ("PLANTED_IN_CLASS(char);", None, "a macro that writes a static_assert inside a class"),
    ("PLANTED_SELF_TEST;", NAMESPACE, "a macro that writes a self-test namespace"),
    ("struct Macro { PLANTED_PAIR(int); };", None, "a macro that writes static_asserts, in a class body"),
    ("inline void macro_body() { PLANTED_PAIR(int); }", None,
     "a macro that writes static_asserts, in a function body"),
    ("namespace detail::macro_self_test { PLANTED_PAIR(short); }", NAMESPACE,
     "a self-test namespace that holds a macro that writes static_asserts"),
    ("}  // namespace foundation", None, ""),
]


def self_test() -> int:
    """Plant each kind of item in a scratch tree and make sure that each verdict is correct.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def expect(name: str, holds: bool, negative: bool = False) -> None:
        """Record one case."""
        nonlocal negatives
        negatives += negative
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    kept_header = ("#pragma once\nnamespace crucible {\nstatic_assert(sizeof(long) == 8);\n"
                   "static_assert(sizeof(char) == 1);\ninline constexpr int kept_value = read_unit();\n"
                   "}  // namespace crucible\n")
    keep_row = "keep | include/crucible/Kept.h | static_assert | sizeof(long)==8 | the planted reason"
    eager_keep_row = "keep | include/crucible/Kept.h | eager evaluation | crucible::kept_value | the planted reason"
    planted_rows = ("include/foundation/Planted.h | 5 | 9\n"
                    "function static_assert | include/foundation/Planted.h | 5\n"
                    "eager evaluation | include/foundation/Planted.h | 11\n")
    matching = "include/crucible/Kept.h | 0 | 1\n" + planted_rows + keep_row + "\n" + eager_keep_row + "\n"
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        files = {
            "include/foundation/Planted.h": "\n".join(line for line, _, _ in PLANTED) + "\n",
            "include/fixy/Clean.h": "#pragma once\nnamespace fixy { struct Clean {}; }\n",
            "include/crucible/Kept.h": kept_header,
            f"{CHECKS}/fixy/Clean.cpp": "// The checks of fixy/Clean.h.\n#include <fixy/Clean.h>\n\n"
                                        "namespace fixy { static_assert(sizeof(Clean) == 1); }\n",
        }
        for rel, text in files.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        ledger = root / LEDGER
        ledger.parent.mkdir(parents=True, exist_ok=True)
        warnings_dir = root / "build" / check_report.WARNINGS_SUBDIR
        warnings_file = warnings_dir / f"{CHECK}.txt"

        def captured(action) -> tuple[int, str]:
            """Run one action and keep its report and its findings."""
            buffer = io.StringIO()
            with contextlib.redirect_stderr(buffer), contextlib.redirect_stdout(buffer):
                code = action()
            return code, buffer.getvalue()

        def verdict() -> tuple[int, str]:
            """Compare the scratch tree with its ledger."""
            return captured(lambda: check(root, ledger, warnings_dir))

        found = scan(root)
        rows = {(item.row, item.kind) for item in found.checks if item.path == "include/foundation/Planted.h"}
        for line, (_, kind, label) in enumerate(PLANTED, start=1):
            if not label:
                continue
            if kind is None:
                expect(f"not counted: {label}", not any(row == line for row, _ in rows), True)
            else:
                expect(f"counted as {kind}: {label}", (line, kind) in rows)
        expect("the keys name each namespace in full, each condition by its spelling, each macro by its "
               "invocation and each evaluation by its qualified name",
               {item.key for item in found.checks if item.path == "include/foundation/Planted.h"} == {
                   "foundation::detail::planted_self_test", "foundation::self_test", "foundation::fn_test",
                   "sizeof(int)==4", "3==3", "4==4", "7==7", "PLANTED_CHECK", "PLANTED_PAIR(int)",
                   "PLANTED_NESTED(long)", "PLANTED_SELF_TEST", "foundation::detail::macro_self_test",
                   "1==1", "2==2", "10==10", "11==11", "12==12", "foundation::probe", "foundation::eager_call",
                   "foundation::members",
                   "foundation::bound", "foundation::table_size", "foundation::fixed_name",
                   "foundation::Counted::count", "foundation::Spliced", "foundation::local_walk::total",
                   "foundation::expand::template for(items)", "foundation::lazy"})
        made = [(item.key, item.kind, item.via) for item in found.checks if item.via]
        expect("an invocation counts each check of one expansion, through a macro that it names too",
               sorted(made) == sorted([("PLANTED_CHECK", ASSERT, "PLANTED_CHECK"),
                                       ("PLANTED_PAIR(int)", ASSERT, "PLANTED_PAIR"),
                                       ("PLANTED_PAIR(int)", ASSERT, "PLANTED_PAIR"),
                                       ("PLANTED_NESTED(long)", ASSERT, "PLANTED_NESTED"),
                                       ("PLANTED_NESTED(long)", ASSERT, "PLANTED_NESTED"),
                                       ("PLANTED_SELF_TEST", NAMESPACE, "PLANTED_SELF_TEST")]))
        inside = next(line for line, (text, _, _) in enumerate(PLANTED, start=1) if "detail::macro_self_test" in text)
        expect("a macro inside a self-test namespace counts with the namespace", (inside, ASSERT) not in rows, True)
        expect("the planted tree has no parse failure and no bad check file",
               not found.failures and not found.bad_check_files, True)

        ledger.write_text(matching, encoding="utf-8")
        code, report = verdict()
        expect("a ledger that agrees with the tree passes, with a warning for each held item",
               code == 0 and "error:" not in report
               and "include/foundation/Planted.h:6: warning: [header-checks] the static_assert(1==1)" in report
               and "include/foundation/Planted.h:32: warning: [header-checks] the constant evaluation of "
                   "foundation::eager_call" in report)
        written = warnings_file.read_text(encoding="utf-8") if warnings_file.is_file() else ""
        expect("the warnings file holds one line in the format for each warning, and nothing else",
               written.count("\n") == report.count(": warning: [header-checks]")
               and all(check_report.parse_line(text) is not None for text in written.splitlines()))
        expect("the kept items give no finding", "crucible::kept_value" not in report and "sizeof(long)" not in report,
               True)
        clean_planted = (root / "include/fixy/Clean.h").read_text(encoding="utf-8")
        for planted_line, label, kind in (
                ("static_assert(sizeof(int) == 4);", "a namespace-scope static_assert", ASSERT),
                ("namespace fixy::detail::clean_self_test { struct Probe {}; }", "a self-test namespace", NAMESPACE),
                ("inline void clean_body() { static_assert(sizeof(int) == 4); }", "a function static_assert",
                 FUNCTION_ASSERT),
                ("inline constexpr int clean_eager = compute();", "an eager evaluation", EAGER)):
            (root / "include/fixy/Clean.h").write_text(clean_planted + planted_line + "\n", encoding="utf-8")
            code, report = verdict()
            expect(f"{label} planted in a clean header is an error",
                   code == 1 and "include/fixy/Clean.h:3: error: [header-checks]" in report
                   and f"of the kind {kind}, and the ledger permits 0" in report)
        (root / "include/fixy/Clean.h").write_text(clean_planted + "template <class T> inline void clean_body() "
                                                   "{ static_assert(sizeof(T) > 0); }\ntemplate <class T> inline "
                                                   "constexpr int clean_eager = compute<T>();\n", encoding="utf-8")
        code, report = verdict()
        expect("the same function static_assert and evaluation in templates are no finding",
               code == 0 and "include/fixy/Clean.h" not in report, True)
        (root / "include/fixy/Clean.h").write_text(clean_planted + "PLANTED_PAIR(Clean);\n", encoding="utf-8")
        code, report = verdict()
        expect("a macro that writes static_asserts, planted in a clean header, is an error",
               code == 1 and "include/fixy/Clean.h:3: error: [header-checks] the invocation of the macro PLANTED_PAIR "
                             "makes 2 namespace-scope static_assert(s)" in report, True)
        (root / "include/fixy/Clean.h").write_text(clean_planted, encoding="utf-8")
        for row, label in (("include/foundation/Planted.h | 5 | 8", "static_assert"),
                           ("include/foundation/Planted.h | 4 | 9", "self-test namespace"),
                           ("function static_assert | include/foundation/Planted.h | 4", "function static_assert"),
                           ("eager evaluation | include/foundation/Planted.h | 10", "eager evaluation")):
            first = row.split(SEPARATOR)[0]
            original = next(text for text in planted_rows.splitlines() if text.split(SEPARATOR)[0] == first)
            ledger.write_text(matching.replace(original, row), encoding="utf-8")
            code, report = verdict()
            expect(f"one more {label} than the row permits is an error",
                   code == 1 and ": error: [header-checks]" in report and "include/foundation/Planted.h:" in report)
        for row, label in (("include/foundation/Planted.h | 5 | 10", "static_assert"),
                           ("function static_assert | include/foundation/Planted.h | 6", "function static_assert"),
                           ("eager evaluation | include/foundation/Planted.h | 12", "eager evaluation")):
            first = row.split(SEPARATOR)[0]
            original = next(text for text in planted_rows.splitlines() if text.split(SEPARATOR)[0] == first)
            ledger.write_text(matching.replace(original, row), encoding="utf-8")
            code, report = verdict()
            expect(f"a {label} row above the tree is an error that asks for a regenerate in the same commit",
                   code == 1 and f"{LEDGER}:" in report and "Regenerate the ledger in the same commit" in report,
                   True)
        ledger.write_text(matching.replace(planted_rows, ""), encoding="utf-8")
        code, report = verdict()
        expect("a header with items and no row is an error", code == 1 and "error:" in report)
        ledger.write_text(matching + "include/foundation/Gone.h | 1 | 1\n", encoding="utf-8")
        code, report = verdict()
        expect("a row that names no header is an error", code == 1 and "include/foundation/Gone.h" in report, True)
        ledger.write_text(matching + "eager evaluation | include/foundation/Gone.h | 1\n", encoding="utf-8")
        code, report = verdict()
        expect("an eager row that names no header is an error",
               code == 1 and "the row names include/foundation/Gone.h" in report, True)
        for extra, label in (("include/fixy/Clean.h | 0 | 0", "a row that counts no item"),
                             ("eager evaluation | include/fixy/Clean.h | 0", "an eager row that counts no item"),
                             ("include/foundation/Planted.h | 9 | 9", "a second count row for one header"),
                             ("eager evaluation | include/foundation/Planted.h | 11", "a second eager row"),
                             ("include/fixy/Clean.h 1 1", "a row without its separators"),
                             ("eager evaluation | include/fixy/Clean.h | many", "an eager row with no number"),
                             ("function static_assert | fixy/Clean.h | 1", "a row with a path outside include/"),
                             ("keep | include/fixy/Clean.h | constexpr | key | reason", "a keep row of no kind")):
            ledger.write_text(matching + extra + "\n", encoding="utf-8")
            code, report = verdict()
            expect(f"{label} is an error", code == 1 and f"{LEDGER}:" in report and "error:" in report, True)
        ledger.write_text(matching.replace(" | the planted reason", " | ", 1), encoding="utf-8")
        code, report = verdict()
        expect("a keep row with no reason is an error", code == 1 and "gives no reason" in report, True)
        ledger.write_text(matching.replace("sizeof(long)==8", "sizeof(long)==4"), encoding="utf-8")
        code, report = verdict()
        expect("a keep row that names no item is an error", code == 1 and "the keep row names no static_assert" in report,
               True)
        ledger.write_text(matching.replace(eager_keep_row + "\n", ""), encoding="utf-8")
        code, report = verdict()
        expect("without its keep row, the kept evaluation counts",
               code == 1 and "include/crucible/Kept.h:5: error: [header-checks]" in report, True)
        ledger.write_text("include/foundation/Planted.h | 1 | 1\n" + keep_row + "\n" + eager_keep_row + "\n",
                          encoding="utf-8")
        code, _ = captured(lambda: write(root, ledger))
        written = ledger.read_text(encoding="utf-8")
        expect("--write gives a ledger that passes, with the header, each row kind and each keep row",
               code == 0 and verdict()[0] == 0 and written.startswith(LEDGER_HEADER)
               and keep_row in written and eager_keep_row in written and all(text in written for text in
                                                                              planted_rows.splitlines()))
        for rel in ("include/foundation/Planted.h", "include/crucible/Kept.h"):
            (root / rel).unlink()
        ledger.write_text("", encoding="utf-8")
        code, report = verdict()
        expect("a run with no warning passes and removes the warnings file",
               code == 0 and "warning:" not in report and not warnings_file.exists(), True)
        for rel in ("include/foundation/Planted.h", "include/crucible/Kept.h"):
            (root / rel).write_text(files[rel], encoding="utf-8")
        ledger.write_text(matching, encoding="utf-8")

        clean_check = (root / f"{CHECKS}/fixy/Clean.cpp").read_text(encoding="utf-8")
        for text, label in ((clean_check.replace("<fixy/Clean.h>", "<foundation/Planted.h>"),
                             "a check file whose first include is not its header"),
                            ("#define CLEAN_ALTERED 1\n" + clean_check, "a check file with a line of code before the "
                                                                         "include of its header"),
                            ("namespace fixy {}\n" + clean_check, "a check file whose first line of code is not an "
                                                                  "include")):
            (root / f"{CHECKS}/fixy/Clean.cpp").write_text(text, encoding="utf-8")
            code, report = verdict()
            expect(f"{label} is an error", code == 1 and f"{CHECKS}/fixy/Clean.cpp:0: error:" in report)
        (root / f"{CHECKS}/fixy/Clean.cpp").write_text(clean_check.replace("<fixy/Clean.h>", '"fixy/Clean.h"'),
                                                       encoding="utf-8")
        expect("a check file that includes its header in quotes passes", verdict()[0] == 0, True)
        (root / f"{CHECKS}/fixy/Clean.cpp").write_text(clean_check, encoding="utf-8")
        (root / f"{CHECKS}/fixy/Gone.cpp").write_text("#include <fixy/Gone.h>\n", encoding="utf-8")
        code, report = verdict()
        expect("a check file with no header is an error", code == 1 and "no header include/fixy/Gone.h" in report)
        (root / f"{CHECKS}/fixy/Gone.cpp").unlink()
        (root / f"{CHECKS}/fixy/Notes.txt").write_text("notes\n", encoding="utf-8")
        code, report = verdict()
        expect("a file under the check root that is not a .cpp file is an error", code == 1 and "Notes.txt" in report)
        (root / f"{CHECKS}/fixy/Notes.txt").unlink()
        (root / "include/fixy/Broken.h").write_text("void f() { g(1) { } }\n", encoding="utf-8")
        code, report = verdict()
        expect("a header the parser cannot read is an error",
               code == 1 and "include/fixy/Broken.h:0: error: [header-checks] the parser cannot read" in report)
        expect("--write refuses while a header does not parse", captured(lambda: write(root, ledger))[0] == 1, True)
        (root / "include/fixy/Broken.h").unlink()

        previous = Path.cwd()
        os.chdir("/")
        try:
            from_slash = verdict()
        finally:
            os.chdir(previous)
        expect("the report from / equals the report from the repository", from_slash == verdict(), True)

        empty = root / "empty"
        empty.mkdir()
        code, report = captured(lambda: check(empty, empty / LEDGER, None))
        expect("a tree with no include directory is an error", code == 1 and "include:0: error:" in report)
        ledger.unlink()
        code, report = verdict()
        expect("a missing ledger permits nothing, so each held item is an error",
               code == 1 and "include/foundation/Planted.h:3: error:" in report)
        for rel in ("include/foundation/Planted.h", "include/crucible/Kept.h"):
            (root / rel).unlink()
        ledger.write_text("", encoding="utf-8")
        expect("a tree with no item in a header and an empty ledger passes", verdict()[0] == 0, True)

        # The list of the headers that cannot compile alone, against a
        # compile database whose crucible sentinel runs the host compiler.
        compiler = shutil.which("c++") or shutil.which("g++")
        if compiler is None:
            expect("a host C++ compiler exists for the cases of --standalone", False)
        else:
            build = root / "build"
            build.mkdir(exist_ok=True)
            sentinel = build / "sentinel.cpp"
            sentinel.write_text("#include <crucible/Alone.h>\n", encoding="utf-8")
            (build / "compile_commands.json").write_text(json.dumps([{
                "directory": str(build), "file": str(sentinel),
                "output": f"{build}/test/layer/CMakeFiles/{SENTINEL_TARGET}.dir/sentinel.cpp.o",
                "arguments": [compiler, "-I", str(root / INCLUDE), "-MD", "-MF", "sentinel.d", "-o", "sentinel.o",
                              "-c", str(sentinel)],
            }]), encoding="utf-8")
            (root / "include/crucible/Alone.h").write_text("#pragma once\ninline int alone = 1;\n", encoding="utf-8")
            (root / "include/crucible/Needs.h").write_text("#pragma once\n#include <header_checks_absent.h>\n",
                                                           encoding="utf-8")
            listing = root / NOT_STANDALONE
            listing.parent.mkdir(parents=True, exist_ok=True)
            needs_row = "crucible/Needs.h | it includes a header that is not on the include path\n"
            listing.write_text(needs_row, encoding="utf-8")
            expect("a listed header that still fails passes", captured(lambda: standalone(root, build))[0] == 0, True)
            listing.write_text(needs_row + "crucible/Alone.h | it compiles alone\n", encoding="utf-8")
            code, report = captured(lambda: standalone(root, build))
            expect("a listed header that compiles alone fails",
                   code == 1 and f"STANDALONE {NOT_STANDALONE}:2: include/crucible/Alone.h" in report)
            expect("the run writes no object and no dependency file",
                   not (build / "sentinel.o").exists() and not (build / "sentinel.d").exists(), True)
            listing.write_text(needs_row + "crucible/Needs.h |  \n", encoding="utf-8")
            code, report = captured(lambda: standalone(root, build))
            expect("a row with no reason is malformed", code == 2 and "MALFORMED" in report, True)
            listing.write_text(needs_row + "crucible/Gone.h | it is gone\n", encoding="utf-8")
            code, report = captured(lambda: standalone(root, build))
            expect("a row that names no header is stale", code == 2 and "crucible/Gone.h" in report, True)
            listing.write_text(needs_row, encoding="utf-8")
            (build / "compile_commands.json").write_text("[]", encoding="utf-8")
            expect("a compile database with no crucible sentinel fails",
                   captured(lambda: standalone(root, build))[0] == 2, True)
    if failures:
        print(f"check-header-checks --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-header-checks --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    parser = argparse.ArgumentParser(prog="check-header-checks.py", add_help=True)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--list", action="store_true", help="print each item that a header holds")
    modes.add_argument("--write", action="store_true", help="write the count rows of the ledger again")
    modes.add_argument("--standalone", metavar="BUILD_DIR", type=Path,
                       help="compile each listed header alone and refuse one that compiles")
    modes.add_argument("--self-test", action="store_true", help="plant each kind of item and examine each verdict")
    check_report.add_arguments(parser)
    try:
        options = parser.parse_args(argv)
    except SystemExit as stop:
        return 0 if stop.code == 0 else 2
    root = tsast.REPO_ROOT
    ledger = root / LEDGER
    try:
        if options.self_test:
            return self_test()
        if options.write:
            return write(root, ledger)
        if options.list:
            return list_checks(root, ledger)
        if options.standalone is not None:
            return standalone(root, options.standalone.resolve())
        return check(root, ledger, options.warnings_dir)
    except tsast.KitMissing as exc:
        print(f"check-header-checks: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
