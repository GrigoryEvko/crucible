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

THE SIX KINDS
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
      * eager evaluation.  A reflection walk outside each template
        context, of one of these forms:
          - a variable at namespace scope, or a static data member, that
            is constexpr, constinit or const, and whose initializer holds
            a reflection query
          - an alias whose type holds a reflection query, for example in
            a splice
          - in the body of a function or a lambda: a constexpr or
            constinit variable whose initializer holds a reflection query,
            or an expansion statement (`template for`), which the compiler
            expands when it reads the body.
        A reflection query is one of these:
          - a call of a function of std::meta, or of a std::define_static_*
            function, also with no qualifier through argument-dependent
            lookup: members_of, enumerators_of, substitute and the others
          - a call that takes a reflection `^^X` as an argument or as a
            template argument, because such a call walks reflection
          - a splice inside a loop.
        The walk reads the evaluated operands only: no sizeof, alignof,
        decltype, noexcept, requires-expression or lambda body holds the
        query, except the body of a lambda that the initializer calls.  A
        plain call, a cast, a cheap constructor and a `^^X` alone are no
        reflection query.  The compiler form sees the cost of a plain call
        exactly (WHAT THE GUARD CANNOT SEE), so this kind reads only the
        work that the operation count of GCC does not see: template
        instantiation and reflection queries.  A const variable counts
        only when the object itself is const: `const char* p` does not
        count, and `const char* const p` does.
      * eager instantiation.  A call of std::define_static_array or
        std::define_static_string outside each template context, or in a
        template context when no argument has a type that depends on a
        template parameter.  GCC resolves such a call where the header
        stands, and it instantiates the function there, in each includer.
        The first instantiation of define_static_array costs about 68 M
        instructions in a unit, and that of define_static_string about
        19 M.  An argument depends on a template parameter when a type in
        it names one: a template argument, the type of a cast or of a
        braced initializer, or the declared type of a variable or a
        parameter that the argument names.  A template parameter, and a
        local that a template parameter gives its type or its value, is
        such a name.  foundation/reflect/Anchor.h gives the type that
        makes a list or a text depend on a template parameter.
      * eager fold.  A call in the body of a consteval function that is
        not in a template context, when each argument is constant: a
        literal, a reflection `^^X`, a name that the function does not
        declare, or a call of such arguments.  GCC evaluates such a call
        when it reads the body, in each includer, and no operation limit
        reports the cost.  The guard counts a reflection query with one
        argument or more, and a call with no argument of a function
        outside std that the header does not declare without constexpr
        or consteval.  A call in a static_assert, in an expansion
        statement or in the initializer of a constexpr local belongs to
        the kinds above, and only the outermost constant call counts.
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
      eager instantiation | path | count
      eager fold | path | count
          The same, for the two kinds of a call.
      keep | path | kind | key | reason
          One item that stays in its header, because its result depends on
          the translation unit that includes the header, or because a
          template in its place would let a translation unit specialize a
          verdict.  The kind is one of the six kinds.  The key of a
          namespace is its full name, the key of a static_assert is the
          spelling of its condition, the key of an eager evaluation is the
          qualified name of what it defines, and the key of a call is the
          qualified name of its scope and the spelling of the call, all as
          --list prints them.  A keep row that names no item is an error,
          and a keep row with no reason is an error.  A reason does not
          contain ` | `.

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

THE FIX MODE
    --fix HEADER... does the repairs that keep their meaning, on the parse
    tree, and refuses each other item with the reason:
      * A self-test namespace and a namespace-scope static_assert move to
        the check file, with the comments above them, in the same
        enclosing namespaces.  A namespace whose whole body moves goes too,
        and its own comment moves with its first item.
      * A function static_assert moves to namespace scope in the check
        file when its condition names nothing that the function or its
        classes declare, and no class holds a base class.
      * A function with a walk moves to the check file when no file but
        the check file names it.  A function or a function template that
        only moved code reads moves with it.  The fix puts a definition
        before the first text of the check file that names it.
      * A namespace variable whose initializer is
        std::meta::enumerators_of(^^E).size(), for an enum E of the same
        header with no preprocessor directive in its body, gets the count
        as its literal.  The check file derives the count and pins the
        literal to it.
      * An eager instantiation or an eager fold in a function that moves
        with a walk moves too.  Each other one is refused, with its repair.
        The fix does not move a function for such an item alone, because
        a walk over a namespace can read a function that no file names,
        such as a rule of a relation.
    A check that the check file states already, in the same namespace,
    does not move again.  A header with a row in
    test/layer/crucible-not-standalone.txt has no check file, so --into
    FILE names the file that receives its checks, and that file must
    include the header.  --dry-run prints each change and writes nothing.
    Read each change before you commit it, then write the ledger again
    with --write and the walk list with check-walk-units.py --write.

WHAT THE GUARD CANNOT SEE
    A macro body has no scope until the macro expands, so the guard counts
    the checks of a macro at each invocation and not at its #define.  A
    macro that a file outside include/ defines, and a name that a macro
    builds with `##`, are not read.  The guard does not read a function
    static_assert or an eager evaluation that a macro writes.  Every arm of
    an #if counts, because the kit does not preprocess.  The syntax cannot
    tell a cheap call from an expensive one, and it cannot see an eager
    instantiation, for example a non-template function that reads a
    variable template, or a plain call into a function that walks
    reflection.  A function that is not a template and calls a template
    with a concrete argument instantiates that template where the function
    stands, so a name function of one enum that calls enum_name
    instantiates the walk over the enum in each includer.  The kinds of a
    call read one call, and they do not follow such a chain.  The eager
    fold reads the body of a consteval function only.  A reflection query
    in a constexpr function that is not a template is an immediate
    invocation, which GCC also evaluates where the header stands, and the
    guard does not count it.  The
    dependence of an argument is read from its syntax, so a member of a
    class template that names no template parameter in its type reads as
    not dependent.  The build gives the exact form for the operations of a
    constant evaluation: test/layer compiles each header alone at a low
    -fconstexpr-ops-limit, and the test header_constexpr_ops holds the list
    of the higher limits.

Usage
    check-header-checks.py [--warnings-dir DIR]    compare the tree with the ledger
    check-header-checks.py --list                  print each item that a header holds
    check-header-checks.py --write                 write the count rows of the ledger again from the tree
    check-header-checks.py --standalone BUILD_DIR  compile each listed header alone and refuse one that compiles
    check-header-checks.py --self-test             plant each kind of item in a scratch tree and examine each verdict
    check-header-checks.py --fix HEADER... [--into FILE] [--dry-run]
                                                   move the checks of each header and make each count a literal

Exit 0 with no error, 1 on an error finding (a new item, a stale or
malformed row, a bad check file, a parse failure or a missing include
directory), 1 when a listed header compiles alone, 1 when --fix refuses an
item, 2 on a missing compile command, a usage error or a failed self-test,
3 when the kit is not installed.
"""

from __future__ import annotations

import argparse
import contextlib
import difflib
import io
import json
import os
import re
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
INSTANCE = "eager instantiation"
FOLD = "eager fold"
KINDS = (NAMESPACE, ASSERT, FUNCTION_ASSERT, EAGER, INSTANCE, FOLD)
# The kinds that have a count row of their own, `kind | path | count`.
ROW_KINDS = (FUNCTION_ASSERT, EAGER, INSTANCE, FOLD)
# The two functions of std whose first instantiation in a unit is expensive.
STATIC_DEFINERS = frozenset({"define_static_array", "define_static_string"})
# The node types of the parameters of a template parameter list.  The first
# four declare a type, a template or a pack of types.
TYPE_PARAMETERS = ("type_parameter_declaration", "variadic_type_parameter_declaration",
                   "optional_type_parameter_declaration", "template_template_parameter_declaration")
VALUE_PARAMETERS = ("parameter_declaration", "optional_parameter_declaration", "variadic_parameter_declaration")
LITERALS = frozenset({"number_literal", "string_literal", "char_literal", "raw_string_literal", "concatenated_string",
                      "true", "false", "nullptr", "user_defined_literal"})
CASTS = frozenset({"static_cast", "const_cast", "reinterpret_cast", "dynamic_cast"})
# The operators between the operands of an expression, which are no operands.
OPERATOR_TOKENS = frozenset({"+", "-", "*", "/", "%", "!", "~", "&&", "||", "==", "!=", "<", ">", "<=", ">=", "&", "|",
                             "^", "<<", ">>", "?", ":", ",", "(", ")", "{", "}", "<=>"})
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
# The reflection functions that a header can call with no qualifier, through
# argument-dependent lookup on std::meta::info or a using-directive.  A call
# with the qualifier std::meta counts for any name.
REFLECTION_FUNCTIONS = frozenset({
    "members_of", "static_data_members_of", "nonstatic_data_members_of", "enumerators_of", "bases_of",
    "define_static_array", "define_static_string", "define_static_object", "substitute", "can_substitute",
    "template_arguments_of", "template_of", "parameters_of", "annotations_of", "annotations_of_with_type",
    "reflect_constant", "reflect_object", "reflect_function", "reflect_constant_array", "reflect_constant_string",
    "extract", "identifier_of", "display_string_of", "type_of", "parent_of", "dealias", "constant_of",
})
LOOPS = frozenset({"for_statement", "for_range_loop", "while_statement", "do_statement"})
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
    "# reflection walk outside each template is lazy: a variable template, or a member of a template.\n"
    "# The argument of a call of std::define_static_array or std::define_static_string has a type that\n"
    "# depends on a template parameter (foundation/reflect/Anchor.h).  A consteval function that is not a\n"
    "# template calls no reflection query with constant arguments.\n"
    "#\n"
    "# A count row:  path | self-test namespaces | namespace-scope static_asserts\n"
    "#               function static_assert | path | count\n"
    "#               eager evaluation | path | count\n"
    "#               eager instantiation | path | count\n"
    "#               eager fold | path | count\n"
    "#   Each item of a row gives a warning on each run.  The counts can only decrease.  A header with no\n"
    "#   item of a kind has no row of that kind.  When you move a check or make an evaluation lazy, run\n"
    "#   python3 utils/scripts/check-header-checks.py --write in the same commit.\n"
    "#\n"
    "# A keep row:   keep | path | kind | key | reason\n"
    "#   One item that stays in its header, because its result depends on the translation unit that\n"
    "#   includes the header, or because a template in its place would let a translation unit specialize\n"
    "#   a verdict.  The kind is namespace, static_assert, function static_assert, eager evaluation,\n"
    "#   eager instantiation or eager fold.  The key is the one that --list prints.  The reason is\n"
    "#   mandatory.\n"
)


@dataclass(frozen=True)
class Check:
    """One item of compile-time work that a header does in each includer.

    via names the macro whose invocation makes the item, or is empty for an
    item that the header writes itself.  detail tells what makes an eager
    evaluation a reflection walk, and it is not part of the identity.
    """

    path: str
    row: int
    kind: str
    key: str
    via: str = ""
    detail: str = field(default="", compare=False)


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


def reflection_function(call: tsast.Node) -> str:
    """Return the name of the reflection function that a call calls, or "" for any other call.

    Args:
        call: A call_expression

    Returns:
        The spelled name, for a callee qualified with std::meta, for a
        std::define_static_* function, and for a name of REFLECTION_FUNCTIONS
        with no qualifier
    """
    callee = call.child_by_field("function")
    parts = None if callee is None else tsast.qualified_parts(callee)
    if parts is None or not parts[1]:
        return ""
    names = parts[1]
    is_reflection = "meta" in names[:-1] or (names[-1] in REFLECTION_FUNCTIONS and names[:-1] in ((), ("std",)))
    return "::".join(names) if is_reflection else ""


def takes_reflection(call: tsast.Node) -> bool:
    """Say whether a call takes a reflection `^^X` as an argument or as a template argument.

    The body of a lambda that the call calls is not an argument, and the
    object of a member call is not one either: in `members_of(^^T).size()`
    the inner call takes the reflection.

    Args:
        call: A call_expression
    """
    callee = call.child_by_field("function")
    if callee is not None and callee.type == "field_expression":
        callee = callee.child_by_field("field")
    holders = [call.child_by_field("arguments"), callee]
    return any(holder is not None and holder.type != "lambda_expression"
               and next(holder.descendants("reflect_expression"), None) is not None for holder in holders)


def reflection_work(node: tsast.Node) -> str:
    """Return what makes the constant evaluation of a node a reflection walk, or "" when nothing does.

    The walk reads the evaluated operands only.  It enters the body of a
    lambda that the node calls, and no other lambda body.  An expansion
    statement inside the node is an item of its own, so the walk does not
    enter it.

    Complexity: linear in the size of the subtree.

    Args:
        node: An initializer, a default member value or the type of an alias

    Returns:
        A short phrase that names the first reflection query, or ""
    """
    stack = [(node, False)]
    while stack:
        current, is_in_loop = stack.pop()
        kind = current.type
        if kind in UNEVALUATED or kind == "expansion_statement":
            continue
        if kind == "call_expression":
            name = reflection_function(current)
            if name:
                return f"a call of {name}"
            if takes_reflection(current):
                return "a call that takes a reflection"
            callee = current.child_by_field("function")
            body = None if callee is None or callee.type != "lambda_expression" else callee.child_by_field("body")
            if body is not None:
                stack.append((body, is_in_loop))
        elif kind == "splice_specifier" and is_in_loop:
            return "a splice in a loop"
        loop = is_in_loop or kind in LOOPS
        stack.extend((child, loop) for child in current.children)
    return ""


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


def eager_sites(tree: tsast.Tree) -> list[tuple[tsast.Node, str, str]]:
    """Return each reflection walk of a header that runs where the header stands, with its key and its query.

    A declaration inside an expansion statement belongs to the walk of that
    statement, so it is not a site of its own.

    Complexity: linear in the number of nodes of the file, times the depth of each candidate.

    Args:
        tree: The parse tree of a header

    Returns:
        (the node, the key, the reflection query) for each site, in source order
    """
    sites: list[tuple[tsast.Node, str, str]] = []
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
                or declaration.ancestor_of_type("expansion_statement") is not None \
                or is_in_template_context(declaration):
            continue
        for target, value in items:
            query = reflection_work(value) if is_constant or is_const_object(declaration, target) else ""
            if query:
                name = tsast.leaf_name(target) or tsast.spelled(target)
                sites.append((target, "::".join((*owner_parts(declaration), name)), query))
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
            elif item.field == "default_value" and declared is not None and counts:
                query = reflection_work(item)
                if query and not is_in_template_context(member):
                    name = tsast.leaf_name(declared) or tsast.spelled(declared)
                    sites.append((declared, "::".join((*owner_parts(member), name)), query))
    for alias in tree.find("alias_declaration", "type_definition"):
        written = alias.child_by_field("type")
        query = "" if written is None else reflection_work(written)
        if not query or alias.ancestor_of_type("expansion_statement") is not None or is_in_template_context(alias):
            continue
        named = alias.child_by_field("name") if alias.type == "alias_declaration" else alias.child_by_field("declarator")
        name = "" if named is None else tsast.leaf_name(named) or tsast.spelled(named)
        sites.append((alias, "::".join((*owner_parts(alias), name)), query))
    for expansion in tree.find("expansion_statement"):
        if is_in_template_context(expansion):
            continue
        source = expansion.child_by_field("right")
        shown = tsast.spelled(source) if source is not None else tsast.spelled(expansion)
        sites.append((expansion, "::".join(owner_parts(expansion)) + f"::template for({shown})",
                      "an expansion statement"))
    sites.sort(key=lambda site: site[0].start)
    return sites


def static_definer(call: tsast.Node) -> str:
    """Return the name of std::define_static_array or std::define_static_string when a call calls one, or "".

    Args:
        call: A call_expression

    Returns:
        The qualified name, also for a call with no qualifier through
        argument-dependent lookup
    """
    callee = call.child_by_field("function")
    parts = None if callee is None else tsast.qualified_parts(callee)
    if parts is None or not parts[1] or parts[1][-1] not in STATIC_DEFINERS or parts[1][:-1] not in ((), ("std",)):
        return ""
    return f"std::{parts[1][-1]}"


@dataclass
class Dependence:
    """The names at one point of a header whose meaning depends on a template parameter.

    names holds each name whose value or type depends on one, and typed
    holds each name whose type depends on one.
    """

    names: set[str] = field(default_factory=set)
    typed: set[str] = field(default_factory=set)


def parameter_name(parameter: tsast.Node) -> str:
    """Return the name that a template parameter or a function parameter declares, or "" for no name."""
    for field_name in ("name", "declarator"):
        named = parameter.child_by_field(field_name)
        text = None if named is None else tsast.leaf_name(named)
        if text:
            return text
    if parameter.type in TYPE_PARAMETERS:
        identifiers = parameter.children_of_type("type_identifier", "identifier")
        if identifiers:
            return tsast.spelled(identifiers[-1])
    return ""


def refers_to(node: tsast.Node | None, names: set[str]) -> bool:
    """Say whether a node, or a name below it, is one of the names.

    A member name after `.` or `->` is a field_identifier, which the walk
    does not read.  A template parameter in front of `::` reads as a
    namespace_identifier, which the walk reads.  Complexity: linear in the
    size of the subtree.
    """
    if node is None or not names:
        return False
    leaves = ("identifier", "type_identifier", "namespace_identifier")
    if node.type in leaves and tsast.spelled(node) in names:
        return True
    return any(tsast.spelled(leaf) in names for leaf in node.descendants(*leaves))


def add_parameters(parameters: tsast.Node | None, dependence: Dependence) -> None:
    """Add the names of a template parameter list to a dependence.

    A type parameter has a dependent type, and so does a value parameter of
    a placeholder type.  Each other value parameter has a dependent value.
    """
    if parameters is None:
        return
    for parameter in tsast.non_comment_children(parameters):
        name = parameter_name(parameter)
        if not name:
            continue
        dependence.names.add(name)
        if parameter.type in TYPE_PARAMETERS or (
                parameter.type in VALUE_PARAMETERS and is_placeholder(parameter.child_by_field("type"))):
            dependence.typed.add(name)


def is_placeholder(written: tsast.Node | None) -> bool:
    """Say whether a written type is a placeholder, such as `auto`."""
    return written is not None and written.type == "placeholder_type_specifier"


def declared_items(declaration: tsast.Node) -> list[tuple[str, tsast.Node | None]]:
    """Return (name, initializer) for each declarator of a declaration."""
    items: list[tuple[str, tsast.Node | None]] = []
    for child in declaration.children:
        if child.field != "declarator":
            continue
        if child.type == "init_declarator":
            target = child.child_by_field("declarator")
            items.append((tsast.leaf_name(target) or "" if target is not None else "", child.child_by_field("value")))
        else:
            items.append((tsast.leaf_name(child) or "", None))
    return [(name, value) for name, value in items if name]


def dependence_at(node: tsast.Node) -> Dependence:
    """Return the names whose meaning depends on a template parameter, in scope at a node.

    The template parameters of each enclosing template declaration and
    generic lambda count, and so does each parameter of a placeholder type.
    Then each member alias and static data member of an enclosing class
    counts when its type or its initializer names a counted name.  Then
    each parameter, local and alias of an enclosing function or lambda that
    comes before the node counts in the same way, in source order.

    Complexity: linear in the size of the enclosing classes and functions,
    times the depth of the node.
    """
    dependence = Dependence()
    scopes: list[tsast.Node] = []
    classes: list[tsast.Node] = []
    owner = node.parent
    while owner is not None:
        if owner.type == "template_declaration":
            add_parameters(owner.child_by_field("parameters"), dependence)
        elif owner.type in FUNCTION_BODIES:
            if owner.type == "lambda_expression":
                add_parameters(owner.child_by_field("template_parameters"), dependence)
            scopes.append(owner)
        elif owner.type in CLASS_SPECIFIERS:
            classes.append(owner)
        owner = owner.parent
    for holder in reversed(classes):
        body = holder.child_by_field("body")
        for member in [] if body is None else body.children:
            if member.type == "alias_declaration" or member.type == "type_definition":
                named = member.child_by_field("name") if member.type == "alias_declaration" \
                    else member.child_by_field("declarator")
                name = "" if named is None else tsast.leaf_name(named) or ""
                if name and refers_to(member.child_by_field("type"), dependence.names):
                    dependence.names.add(name)
                    dependence.typed.add(name)
            elif member.type == "field_declaration":
                written = member.child_by_field("type")
                declared = member.child_by_field("declarator")
                name = "" if declared is None else tsast.leaf_name(declared) or ""
                value = member.child_by_field("default_value")
                is_typed = is_type_dependent(value, dependence) if is_placeholder(written) \
                    else refers_to(written, dependence.names)
                if name and (is_typed or refers_to(value, dependence.names)):
                    dependence.names.add(name)
                if name and is_typed:
                    dependence.typed.add(name)
    for scope in reversed(scopes):
        for name, parameter in tsast.parameters(scope) if scope.type == "function_definition" else []:
            if name and (is_placeholder(parameter.child_by_field("type"))
                         or refers_to(parameter.child_by_field("type"), dependence.names)):
                dependence.names.add(name)
                dependence.typed.add(name)
        declarator = scope.child_by_field("declarator") if scope.type == "lambda_expression" else None
        listed = None if declarator is None else declarator.child_by_field("parameters")
        for parameter in [] if listed is None else listed.children_of_type(*PARAMETER_TYPES):
            name = parameter_name(parameter)
            if name and (is_placeholder(parameter.child_by_field("type"))
                         or refers_to(parameter.child_by_field("type"), dependence.names)):
                dependence.names.add(name)
                dependence.typed.add(name)
        body = scope.child_by_field("body")
        if body is None:
            continue
        for item in body.descendants("declaration", "alias_declaration", "type_definition", "for_range_loop",
                                     "expansion_statement"):
            if item.start >= node.start:
                break
            if item.type in ("alias_declaration", "type_definition"):
                written = item.child_by_field("type")
                named = item.child_by_field("name") if item.type == "alias_declaration" \
                    else item.child_by_field("declarator")
                name = "" if named is None else tsast.leaf_name(named) or ""
                if name and refers_to(written, dependence.names):
                    dependence.names.add(name)
                    dependence.typed.add(name)
            elif item.type in ("for_range_loop", "expansion_statement"):
                declared = next((child for child in item.children if child.field in ("declarator", "left")), None)
                name = "" if declared is None else tsast.leaf_name(declared) or ""
                ranged = item.child_by_field("right")
                if name and refers_to(ranged, dependence.names):
                    dependence.names.add(name)
                    if is_type_dependent(ranged, dependence):
                        dependence.typed.add(name)
            else:
                written = item.child_by_field("type")
                for name, value in declared_items(item):
                    is_typed = is_type_dependent(value, dependence) if is_placeholder(written) \
                        else refers_to(written, dependence.names)
                    if is_typed or refers_to(value, dependence.names):
                        dependence.names.add(name)
                    if is_typed:
                        dependence.typed.add(name)
    return dependence


def is_type_dependent(node: tsast.Node | None, dependence: Dependence) -> bool:
    """Say whether the type of an expression depends on a template parameter, by its syntax.

    A reflection `^^X` has the type std::meta::info, so the template
    parameter that it names makes its value dependent and not its type.  A
    call depends when its callee names a dependent name in a template
    argument list, when its object or its callee has a dependent type, or
    when an argument does.  A cast or a braced initializer depends when its
    type names a dependent name.

    Complexity: linear in the size of the subtree.
    """
    if node is None:
        return False
    kind = node.type
    if kind in LITERALS or kind in ("reflect_expression", "lambda_expression", "sizeof_expression",
                                    "alignof_expression", "noexcept_expression", "requires_expression", "this"):
        return False
    if kind == "identifier":
        return refers_to(node, dependence.typed)
    if kind == "qualified_identifier":
        return refers_to(node, dependence.typed) or any(
            refers_to(arguments, dependence.names) for arguments in node.descendants("template_argument_list"))
    if kind == "call_expression":
        callee = node.child_by_field("function")
        if callee is not None and callee.type == "field_expression":
            if is_type_dependent(callee.child_by_field("argument"), dependence):
                return True
        elif callee is not None and (any(refers_to(arguments, dependence.names)
                                         for arguments in callee.descendants("template_argument_list"))
                                     or is_type_dependent(callee, dependence)):
            return True
        arguments = node.child_by_field("arguments")
        return arguments is not None and any(is_type_dependent(argument, dependence)
                                             for argument in tsast.non_comment_children(arguments))
    if kind == "template_function":
        return any(refers_to(arguments, dependence.names) for arguments in node.descendants("template_argument_list"))
    if kind in ("compound_literal_expression", "cast_expression"):
        return refers_to(node.child_by_field("type"), dependence.names)
    if kind in ("field_expression", "subscript_expression"):
        return is_type_dependent(node.child_by_field("argument"), dependence)
    return any(is_type_dependent(child, dependence) for child in tsast.non_comment_children(node))


def instance_sites(tree: tsast.Tree) -> list[tuple[tsast.Node, str, str]]:
    """Return each call of a static definer that GCC instantiates where the header stands, with its key and why.

    Complexity: linear in the number of calls, times the size of the
    functions that enclose each definer call.

    Args:
        tree: The parse tree of a header

    Returns:
        (the call, the key, the reason) for each site, in source order
    """
    sites: list[tuple[tsast.Node, str, str]] = []
    for call in tree.find("call_expression"):
        name = static_definer(call)
        if not name:
            continue
        if not is_in_template_context(call):
            reason = "a call outside each template"
        else:
            dependence = dependence_at(call)
            arguments = call.child_by_field("arguments")
            listed = [] if arguments is None else tsast.non_comment_children(arguments)
            if any(is_type_dependent(argument, dependence) for argument in listed):
                continue
            reason = "no argument has a type that depends on a template parameter"
        sites.append((call, "::".join((*owner_parts(call), tsast.spelled(call))), reason))
    return sites


def has_consteval(function: tsast.Node) -> bool:
    """Say whether a function definition has the consteval specifier."""
    return any(not child.children and tsast.spelled(child) == "consteval" for child in function.children)


def constant_functions(tree: tsast.Tree) -> tuple[set[str], set[str]]:
    """Return the names that the header declares as constexpr or consteval functions, and as other functions.

    A name of the second set and not of the first names a function that no
    constant evaluation can call, such as the stub that a refusal calls to
    stop a constant evaluation.
    """
    constant: set[str] = set()
    plain: set[str] = set()
    for node in tree.find("function_definition", "declaration", "field_declaration"):
        declarator = node.child_by_field("declarator")
        if declarator is None or next(iter([declarator] if declarator.type == "function_declarator" else
                                           declarator.descendants("function_declarator")), None) is None:
            continue
        name = tsast.leaf_name(declarator)
        if not name:
            continue
        words = {tsast.spelled(child) for child in node.children if not child.children}
        (constant if words & {"constexpr", "consteval"} else plain).add(name)
    return constant, plain


def is_constant(node: tsast.Node, declared: set[str]) -> bool:
    """Say whether an expression is constant by its syntax.

    Literals, reflections, names that the function does not declare, and calls of them are constant.
    """
    kind = node.type
    if kind in LITERALS or kind in ("reflect_expression", "sizeof_expression", "alignof_expression"):
        return True
    if kind == "identifier":
        return tsast.spelled(node) not in declared
    if kind == "qualified_identifier":
        return True
    if kind == "call_expression":
        return named_call(node, declared) is not None
    if kind in ("parenthesized_expression", "binary_expression", "unary_expression", "conditional_expression",
                "initializer_list", "compound_literal_expression"):
        return all(is_constant(child, declared) for child in tsast.non_comment_children(node)
                   if child.field != "type")
    return False


def named_call(call: tsast.Node, declared: set[str]) -> tuple[str, ...] | None:
    """Return the name parts of a call of a named function whose arguments are all constant, or None.

    A cast, a member call and a call through a local name are no such call.
    """
    callee = call.child_by_field("function")
    if callee is None or callee.type not in ("identifier", "qualified_identifier", "template_function"):
        return None
    parts = tsast.qualified_parts(callee)
    if parts is None or not parts[1] or parts[1][-1] in CASTS:
        return None
    if len(parts[1]) == 1 and not parts[0] and parts[1][0] in declared:
        return None
    arguments = call.child_by_field("arguments")
    if arguments is None or not all(is_constant(argument, declared)
                                    for argument in tsast.non_comment_children(arguments)):
        return None
    return parts[1]


def fold_sites(tree: tsast.Tree) -> list[tuple[tsast.Node, str, str]]:
    """Return each call that GCC evaluates where the header stands, in a consteval function that is not a template.

    Complexity: linear in the size of the consteval functions of the header.

    Args:
        tree: The parse tree of a header

    Returns:
        (the call, the key, the reason) for each site, in source order
    """
    constant, plain = constant_functions(tree)
    stubs = plain - constant
    sites: list[tuple[tsast.Node, str, str]] = []
    for function in tree.find("function_definition"):
        if not has_consteval(function) or is_in_template_context(function):
            continue
        body = function.child_by_field("body")
        if body is None:
            continue
        declared = names_declared(function)
        for loop in function.descendants("for_range_loop", "expansion_statement"):
            for child in loop.children:
                if child.field in ("declarator", "left"):
                    declared.update(tsast.spelled(leaf) for leaf in [child, *child.descendants("identifier")]
                                    if leaf.type == "identifier")
        holder = function.ancestor_of_type(*CLASS_SPECIFIERS)
        while holder is not None:
            declared |= names_declared(holder)
            holder = holder.ancestor_of_type(*CLASS_SPECIFIERS)
        for call in body.descendants("call_expression"):
            parts = named_call(call, declared)
            if parts is None or not is_folded(call, body) or is_inside_constant_call(call, body, declared) \
                    or is_in_template_context(call):
                continue
            arguments = call.child_by_field("arguments")
            has_arguments = arguments is not None and bool(tsast.non_comment_children(arguments))
            if has_arguments and (reflection_function(call) or takes_reflection(call)):
                reason = "a reflection query with constant arguments"
            elif not has_arguments and parts[0] != "std" and parts[-1] not in stubs:
                reason = "a call with no argument"
            else:
                continue
            sites.append((call, "::".join((*owner_parts(call), tsast.spelled(call))), reason))
    return sites


def is_folded(call: tsast.Node, body: tsast.Node) -> bool:
    """Say whether GCC folds a call where it reads the body, and no other kind counts it.

    A call in an unevaluated operand is not evaluated.  A call in a
    static_assert, in an expansion statement or in the initializer of a
    constexpr or constinit local belongs to another kind.
    """
    owner = call.parent
    while owner is not None and owner != body:
        if owner.type in UNEVALUATED - {"lambda_expression"} or owner.type in ("static_assert_declaration",
                                                                               "expansion_statement"):
            return False
        if owner.type == "declaration" and has_qualifier(owner, "constexpr", "constinit"):
            return False
        owner = owner.parent
    return True


def is_inside_constant_call(call: tsast.Node, body: tsast.Node, declared: set[str]) -> bool:
    """Say whether a constant call is an argument of a larger constant call, which counts in its place."""
    owner = call.parent
    while owner is not None and owner != body and owner.type in (
            "argument_list", "parenthesized_expression", "binary_expression", "unary_expression",
            "conditional_expression", "initializer_list"):
        owner = owner.parent
    return owner is not None and owner.type == "call_expression" and named_call(owner, declared) is not None


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
    for kind, found in ((EAGER, eager_sites(tree)), (INSTANCE, instance_sites(tree)), (FOLD, fold_sites(tree))):
        for site, key, query in found:
            if not any(is_self_test_segment(segment) for segment in tsast.namespace_path(site)):
                checks.append(Check(rel, site.line, kind, key, detail=query))
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
    if check.kind == INSTANCE:
        return (f"the call {check.key} instantiates its function in each translation unit that includes this header "
                f"({check.detail}).  Give the argument a type that depends on a template parameter of the enclosing "
                f"template, with foundation/reflect/Anchor.h, or move the call into a template")
    if check.kind == FOLD:
        return (f"the call {check.key} runs in each translation unit that includes this header, because the "
                f"enclosing consteval function is not a template ({check.detail}).  Make the function a template "
                f"that its callers name with a dependent argument, or move it to {target}")
    query = f" ({check.detail})" if check.detail else ""
    return (f"the reflection walk of {check.key}{query} runs again in each translation unit that includes this "
            f"header.  Make it a variable template or a member of a template, or move it to {target}")


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


@dataclass(frozen=True)
class Placement:
    """One piece of text that the fix puts in the destination file.

    target is the namespace path that the text stands in.  A definition goes
    before the first text of the destination that names it, and a check goes
    at the end.  condition is the spelling of the condition of a
    static_assert, with no white space, or "" for other text.  name is the
    name that a definition defines.  is_adjacent is True when the text
    followed the text before it in the header with no blank line between.
    """

    target: tuple[str, ...]
    text: str
    is_definition: bool
    condition: str = ""
    name: str = ""
    is_adjacent: bool = False


@dataclass
class FixPlan:
    """What the fix does to one header.

    cuts holds (start, end, replacement) in the bytes of the header.  done
    and refused hold one line for each item.
    """

    header: str
    destination: str
    cuts: list[tuple[int, int, str]] = field(default_factory=list)
    placements: list[Placement] = field(default_factory=list)
    done: list[str] = field(default_factory=list)
    refused: list[str] = field(default_factory=list)


# The leaves that can name a declaration.
NAME_LEAVES = ("identifier", "field_identifier", "type_identifier", "namespace_identifier")
# The widest piece of the message of a pin, so that each line of the pin
# stays inside the 120 columns of .clang-format.
MESSAGE_WIDTH = 100
PREPROCESSOR_ARMS = frozenset({"preproc_if", "preproc_ifdef", "preproc_else", "preproc_elif", "preproc_elifdef"})


def line_starts(data: bytes) -> list[int]:
    """Return the byte offset of the start of each line of a file.

    Complexity: linear in the size of the file.
    """
    starts = [0]
    position = data.find(b"\n")
    while position != -1:
        starts.append(position + 1)
        position = data.find(b"\n", position + 1)
    return starts


def offset_of(starts: list[int], point: tuple[int, int]) -> int:
    """Return the byte offset of a (row, byte column) point."""
    return starts[point[0]] + point[1]


def leading_comments(node: tsast.Node) -> tsast.Node:
    """Return the first comment of the run of comments directly above a node, or the node itself.

    A comment belongs to the node when no blank line separates them, and
    when it does not end a line of other code.

    Args:
        node: A child of a namespace body or of the translation unit
    """
    owner = node.parent
    if owner is None:
        return node
    siblings = owner.children
    index = siblings.index(node)
    first = node
    while index > 0:
        above = siblings[index - 1]
        if above.type != "comment" or above.end[0] + 1 < first.start[0]:
            break
        if index > 1 and siblings[index - 2].end[0] == above.start[0]:
            break
        first = above
        index -= 1
    return first


def whole_lines(data: bytes, starts: list[int], first: tsast.Node, last: tsast.Node) -> tuple[int, int] | str:
    """Return the byte range of the lines that hold the nodes from first to last.

    A comment after the last node on its line belongs to the range.

    Args:
        data: The bytes of the file
        starts: The line starts of the file
        first: The first node of the range
        last: The last node of the range

    Returns:
        (start, end), or the reason why the nodes do not stand on lines of their own
    """
    low = offset_of(starts, first.start)
    line_low = starts[first.start[0]]
    if data[line_low:low].strip():
        return "it shares its first line with other code"
    high = offset_of(starts, last.end)
    newline = data.find(b"\n", high)
    line_high = len(data) if newline == -1 else newline + 1
    rest = data[high:line_high].strip()
    if rest and not rest.startswith(b"//"):
        return "it shares its last line with other code"
    return line_low, line_high


def placement_obstacle(node: tsast.Node) -> str:
    """Return why the enclosing scopes of a node cannot stand in the destination, or "".

    Args:
        node: The node that moves
    """
    owner = node.parent
    while owner is not None:
        if owner.type in PREPROCESSOR_ARMS:
            return f"it stands in an arm of the preprocessor conditional at line {owner.line}, so the move must " \
                   f"keep the condition"
        if owner.type == "linkage_specification":
            return f"it stands in the linkage specification at line {owner.line}"
        owner = owner.parent
    path = tsast.namespace_path(node)
    if "" in path:
        return "it stands in an anonymous namespace, which no other file can open"
    if path != tsast.namespace_path(node, skip_inline=True):
        return "it stands in an inline namespace"
    return ""


def compact(text: str) -> str:
    """Return a spelling with no white space, for the comparison of two conditions."""
    return "".join(text.split())


def names_used(node: tsast.Node) -> set[str]:
    """Return each name that an expression looks up from its own scope.

    The name after a scope and the name of a member are left out, because
    they resolve through what comes before them.
    """
    used: set[str] = set()
    for leaf in node.descendants(*NAME_LEAVES):
        owner = leaf.parent
        if owner is not None and owner.type == "qualified_identifier" and leaf.field == "name":
            continue
        if leaf.type == "field_identifier":
            continue
        used.add(tsast.spelled(leaf))
    return used


def names_declared(scope: tsast.Node) -> set[str]:
    """Return each name that a function or a class declares, at any depth.

    The set is larger than the exact set of declarations, so a move that it
    admits is safe.
    """
    declared: set[str] = set()
    for name, _parameter in tsast.parameters(scope) if scope.type == "function_definition" else []:
        declared.add(name)
    for node in scope.descendants("init_declarator", "declaration", "field_declaration", "parameter_declaration",
                                  "optional_parameter_declaration", "alias_declaration", "type_definition",
                                  "class_specifier", "struct_specifier", "union_specifier", "enum_specifier",
                                  "enumerator", "function_definition", "using_declaration",
                                  "structured_binding_declarator", "template_parameter_list"):
        for part in ("declarator", "name"):
            child = node.child_by_field(part)
            name = None if child is None else tsast.leaf_name(child)
            if name:
                declared.add(name)
        if node.type in ("structured_binding_declarator", "using_declaration", "template_parameter_list"):
            declared.update(tsast.spelled(leaf) for leaf in node.descendants(*NAME_LEAVES))
    return declared


def mentions(root: Path, name: str) -> list[tuple[str, int, str]]:
    """Return each place of the tree that names a declaration, outside each comment.

    A C++ file counts an identifier of that name and a macro body that
    holds the name.  Any other tracked file counts each occurrence of the
    word, because a script or a fixture can pin it.

    Complexity: linear in the total size of the tracked files.

    Args:
        root: The repository root
        name: The name

    Returns:
        (repo-relative path, line, kind) for each place, where kind is
        "code" for a C++ file and "text" for any other file
    """
    pattern = re.compile(rb"\b" + re.escape(name.encode("utf-8")) + rb"\b")
    found: list[tuple[str, int, str]] = []
    sources: list[Path] = []
    for rel in tsast.tracked_files(root):
        path = root / rel
        try:
            data = path.read_bytes()
        except OSError:
            continue
        match = pattern.search(data)
        if match is None:
            continue
        if tsast.is_in_cpp_scope(rel):
            sources.append(path)
        else:
            found.append((rel, data.count(b"\n", 0, match.start()) + 1, "text"))
    for tree in tsast.parse(sources, strict=False):
        rel = Path(tree.path).relative_to(root).as_posix() if Path(tree.path).is_absolute() else str(tree.path)
        for leaf in tree.root.descendants_named(frozenset({name}), *NAME_LEAVES):
            found.append((rel, leaf.line, "code"))
        for body in tree.find("preproc_arg"):
            if any(token.kind == "identifier" and token.text == name
                   for token in tsast.pp_tokens(tree.slice(body.start, body.end), body.start[0])):
                found.append((rel, body.line, "code"))
    return found


def enumerator_count(tree: tsast.Tree, value: tsast.Node) -> tuple[int, str] | str:
    """Return the count that `std::meta::enumerators_of(^^E).size()` gives, read from the enum in the same header.

    Args:
        tree: The parse tree of the header
        value: The initializer

    Returns:
        (count, the spelling of E), or the reason why the initializer is not that count
    """
    shape = "the initializer is not std::meta::enumerators_of(^^E).size() over an enum of this header"
    callee = value.child_by_field("function") if value.type == "call_expression" else None
    arguments = value.child_by_field("arguments") if value.type == "call_expression" else None
    if callee is None or callee.type != "field_expression" or arguments is None \
            or tsast.non_comment_children(arguments):
        return shape
    field_name = callee.child_by_field("field")
    inner = callee.child_by_field("argument")
    if field_name is None or tsast.spelled(field_name) != "size" or inner is None or inner.type != "call_expression":
        return shape
    function = inner.child_by_field("function")
    parts = None if function is None else tsast.qualified_parts(function)
    listed = inner.child_by_field("arguments")
    operands = [] if listed is None else tsast.non_comment_children(listed)
    if parts is None or parts[1] not in (("std", "meta", "enumerators_of"), ("enumerators_of",)) \
            or len(operands) != 1 or operands[0].type != "reflect_expression":
        return shape
    named = [leaf for leaf in operands[0].descendants("type_identifier", "identifier", "qualified_identifier")
             if leaf.parent is not None and leaf.parent.type == "type_descriptor"]
    wanted = None if len(named) != 1 else tsast.qualified_parts(named[0])
    if wanted is None:
        return shape
    enums = []
    for enum in tree.find("enum_specifier"):
        name = enum.child_by_field("name")
        body = enum.child_by_field("body")
        if name is None or body is None:
            continue
        full = (*tsast.namespace_path(enum), tsast.spelled(name))
        if full[len(full) - len(wanted[1]):] == wanted[1]:
            enums.append(body)
    if len(enums) != 1:
        return f"{shape}: the header defines {len(enums)} enum(s) of the name {'::'.join(wanted[1])}"
    arms = [child.line for child in enums[0].children if child.type.startswith("preproc")]
    if arms:
        return f"the enum holds a preprocessor directive at line {arms[0]}, so its count depends on the build"
    count = sum(1 for child in enums[0].children if child.type == "enumerator")
    return count, tsast.spelled(named[0])


def item_node(tree: tsast.Tree, sites: dict[tuple[int, str], tsast.Node], item: Check) -> tsast.Node | None:
    """Return the node of one item, or None when the parse tree holds no such node.

    Args:
        tree: The parse tree of the header
        sites: The eager sites of the header, by (line, key)
        item: The item
    """
    if item.kind == NAMESPACE:
        for candidate in tree.find("namespace_definition"):
            name = candidate.child_by_field("name")
            parts = None if name is None else tsast.qualified_parts(name)
            if candidate.line == item.row and parts is not None \
                    and "::".join((*tsast.namespace_path(candidate), *parts[1])) == item.key:
                return candidate
        return None
    if item.kind in (ASSERT, FUNCTION_ASSERT):
        for candidate in tree.find("static_assert_declaration"):
            condition = candidate.child_by_field("condition")
            if candidate.line == item.row and condition is not None and tsast.spelled(condition) == item.key:
                return candidate
        return None
    return sites.get((item.row, item.key))


def outermost_function(node: tsast.Node) -> tsast.Node | None:
    """Return the outermost function definition or lambda that holds a node, or None."""
    outer = node.ancestor_of_type(*FUNCTION_BODIES)
    while outer is not None:
        further = outer.ancestor_of_type(*FUNCTION_BODIES)
        if further is None:
            return outer
        outer = further
    return None


def hoist_obstacle(node: tsast.Node, function: tsast.Node) -> str:
    """Return why the condition of a function static_assert can change meaning at namespace scope, or "".

    The condition keeps its meaning when it names nothing that the function
    or one of its classes declares, and when no class holds a base class,
    whose members the parse tree cannot see.

    Args:
        node: The static_assert_declaration
        function: The function that holds it
    """
    scopes = [function]
    holder = function.ancestor_of_type(*CLASS_SPECIFIERS)
    while holder is not None:
        if holder.children_of_type("base_class_clause"):
            return "a class with a base class holds the function, so a name of the condition can be a member of the base"
        scopes.append(holder)
        holder = holder.ancestor_of_type(*CLASS_SPECIFIERS)
    condition = node.child_by_field("condition")
    clash = sorted(names_used(condition) & set().union(*(names_declared(scope) for scope in scopes)))
    if clash:
        return f"the function or its class declares {', '.join(clash)}, which the condition names"
    return ""


def count_literal(tree: tsast.Tree, starts: list[int], node: tsast.Node, destination: str,
                  plan: FixPlan) -> str:
    """Write a namespace count of enumerators as a literal, and pin the literal in the destination.

    Args:
        tree: The parse tree of the header
        starts: The line starts of the header
        node: The declarator of the eager variable
        destination: The destination, repo-relative
        plan: The plan that receives the cut and the pin

    Returns:
        "" when the plan takes the rewrite, or the reason why the variable is not such a count
    """
    if node.type in ("alias_declaration", "type_definition"):
        return "an alias splices the walk"
    holder = node.parent
    value = holder.child_by_field("value") if holder is not None and holder.type == "init_declarator" else None
    if value is None or node.field != "declarator":
        return "it is not a namespace variable with an initializer"
    counted = enumerator_count(tree, value)
    if isinstance(counted, str):
        return counted
    count, spelled_enum = counted
    name = tsast.leaf_name(node) or tsast.spelled(node)
    plan.cuts.append((offset_of(starts, value.start), offset_of(starts, value.end), str(count)))
    condition = f"{name} == std::meta::enumerators_of(^^{spelled_enum}).size()"
    message = (f"{name} is a literal count of the enumerators of {spelled_enum}, so that no includer of the "
               f"header walks the enum.  Write the new count in its initializer.")
    pieces = [""]
    for word in message.split(" "):
        if pieces[-1] and len(pieces[-1]) + len(word) + 1 > MESSAGE_WIDTH:
            pieces.append("")
        pieces[-1] += word + " "
    literal = "\n              ".join(f'"{piece}"' for piece in [*pieces[:-1], pieces[-1].rstrip()])
    plan.placements.append(Placement(tsast.namespace_path(node), f"static_assert({condition},\n"
                                     f"              {literal});\n", False, compact(condition)))
    plan.done.append(f"{plan.header}:{node.line}: the initializer of {name} is the literal {count}, and "
                     f"{destination} derives it")
    return ""


def moved_helpers(root: Path, tree: tsast.Tree, starts: list[int],
                  moved: list[tuple[tsast.Node, tsast.Node, Placement]], header: str,
                  destination: str) -> list[tuple[tsast.Node, str]]:
    """Return each function or function template of the header that only moved definitions and the destination read.

    A moved definition takes such a helper with it, so the header keeps no
    definition that only the destination reads.  The search reaches a fixed
    point, so a helper of a helper moves too.

    Complexity: one read of the tracked files for each name that a moved
    definition holds and the header defines.

    Args:
        root: The repository root
        tree: The parse tree of the header
        starts: The line starts of the header
        moved: The nodes that move, as (first node, last node, placement)
        header: The header, repo-relative
        destination: The destination, repo-relative

    Returns:
        (node, name) for each helper, in source order
    """
    candidates: dict[str, tsast.Node] = {}
    twice: set[str] = set()
    for function in tree.find("function_definition"):
        holder = function.parent if function.parent is not None and function.parent.type == "template_declaration" \
            else function
        body = function.child_by_field("body")
        name = None if body is None else tsast.enclosing_function(body)
        if name is None or len(name) != 1 or not is_at_eager_scope(holder) or placement_obstacle(holder):
            continue
        if name[0] in candidates:
            twice.add(name[0])
        candidates[name[0]] = holder
    spans = [whole_lines(tree.source, starts, first, last) for first, last, _placement in moved]
    spans = [span for span in spans if not isinstance(span, str)]
    nodes = [last for _first, last, _placement in moved]
    found: list[tuple[tsast.Node, str]] = []
    checked: set[str] = set()
    grew = True
    while grew:
        grew = False
        inside = {tsast.spelled(leaf) for node in nodes for leaf in node.descendants(*NAME_LEAVES)}
        for name in sorted(inside & set(candidates) - checked - twice):
            checked.add(name)
            node = candidates[name]
            span = whole_lines(tree.source, starts, leading_comments(node), node)
            if isinstance(span, str) or any(low <= span[0] < high for low, high in spans):
                continue
            readers = [(path, line) for path, line, kind in mentions(root, name)
                       if path != destination and not (kind == "text" and path.endswith(".md"))]
            if all(path == header and any(low <= starts[line - 1] < high for low, high in (*spans, span))
                   for path, line in readers):
                found.append((node, name))
                spans.append(span)
                nodes.append(node)
                grew = True
    return sorted(found, key=lambda entry: entry[0].start)


def plan_fix(root: Path, header: str, items: list[Check], destination: str) -> FixPlan:
    """Decide what the fix does to the items of one header.

    A self-test namespace and a namespace-scope static_assert move.  A
    function static_assert moves when its condition means the same at
    namespace scope.  A walk in a function moves with its function when no
    file but the destination names the function.  A namespace count of the
    enumerators of an enum becomes a literal, and the destination derives
    it.  The fix refuses each other item, with the reason.

    Complexity: linear in the size of the header, plus one read of the
    tracked files for each function that moves.

    Args:
        root: The repository root
        header: The header, repo-relative
        items: The items of the header that no keep row names
        destination: The file that receives the checks, repo-relative

    Returns:
        The plan
    """
    plan = FixPlan(header, destination)
    tree = next(iter(tsast.parse([root / header])))
    data = tree.source
    starts = line_starts(data)
    sites = {(node.line, key): node for found in (eager_sites(tree), instance_sites(tree), fold_sites(tree))
             for node, key, _query in found}
    # (first node, last node, the placement without its text)
    moved: list[tuple[tsast.Node, tsast.Node, Placement]] = []
    walks: dict[int, tuple[tsast.Node, list[Check]]] = {}

    def refuse(item: Check, reason: str) -> None:
        """Record one item that the fix leaves in the header."""
        plan.refused.append(f"{header}:{item.row}: {item.kind} {item.key}: {reason}")

    nodes = {item: item_node(tree, sites, item) for item in items}
    for item, node in nodes.items():
        function = None if node is None else outermost_function(node)
        if item.kind == EAGER and function is not None and function.type == "function_definition":
            walks.setdefault(function.index, (function, []))
    for item, node in nodes.items():
        if item.via:
            refuse(item, f"the macro {item.via} writes it, so move the invocation by hand")
            continue
        if node is None:
            refuse(item, "the fix cannot find the item in the parse tree")
            continue
        obstacle = placement_obstacle(node)
        if obstacle:
            refuse(item, obstacle)
            continue
        function = outermost_function(node)
        if item.kind in (INSTANCE, FOLD) and (function is None or function.index not in walks):
            refuse(item, "give the argument a type that depends on a template parameter of the enclosing template "
                         "(foundation/reflect/Anchor.h), or make the function a template that its callers name with "
                         "a dependent argument, by hand" if item.kind == INSTANCE else
                   "make the function a template that its callers name with a dependent argument, by hand")
            continue
        if item.kind in (NAMESPACE, ASSERT):
            condition = node.child_by_field("condition")
            moved.append((leading_comments(node), node, Placement(
                tsast.namespace_path(node), "", False, compact(tsast.spelled(condition)) if condition else "")))
        elif function is not None and function.index in walks:
            walks[function.index][1].append(item)
        elif item.kind == EAGER and function is None:
            reason = count_literal(tree, starts, node, destination, plan)
            if reason:
                refuse(item, f"{reason}.  Make it lazy by hand")
        elif function is None or function.type != "function_definition":
            refuse(item, "a lambda that no function holds encloses it")
        else:
            obstacle = hoist_obstacle(node, node.ancestor_of_type("function_definition"))
            if obstacle:
                refuse(item, obstacle)
                continue
            moved.append((node, node, Placement(tsast.namespace_path(function), "", False,
                                                compact(tsast.spelled(node.child_by_field("condition"))))))

    for function, owned in walks.values():
        body = function.child_by_field("body")
        name = () if body is None else tsast.enclosing_function(body) or ()
        same_name = [other for other in tree.find("function_definition", "declaration")
                     if len(name) == 1 and tsast.leaf_name(other.child_by_field("declarator") or other) == name[0]]
        reason = placement_obstacle(function)
        if not reason and function.ancestor_of_type(*CLASS_SPECIFIERS) is not None:
            reason = "it is in a member function, so move the walk by hand"
        if not reason and (len(name) != 1 or len(same_name) != 1):
            reason = "the header declares the function more than one time, or the fix cannot read its name"
        first = leading_comments(function)
        span = whole_lines(data, starts, first, function)
        if not reason and isinstance(span, str):
            reason = f"the function {span}"
        if not reason:
            outside = [(path, line) for path, line, kind in mentions(root, name[0])
                       if not (path == header and span[0] <= starts[line - 1] < span[1]) and path != destination
                       and not (kind == "text" and path.endswith(".md"))]
            if outside:
                shown = ", ".join(f"{path}:{line}" for path, line in outside[:4])
                reason = f"the function {name[0]} has readers outside {destination}: {shown}.  Make it lazy by hand"
        if reason:
            for item in owned:
                refuse(item, reason)
            continue
        moved.append((first, function, Placement(tsast.namespace_path(function), "", True, name=name[0])))
        plan.done.extend(f"{header}:{item.row}: {item.kind} {item.key}: the function {name[0]} moves to "
                         f"{destination}" for item in owned)

    for helper, name in moved_helpers(root, tree, starts, moved, header, destination):
        moved.append((leading_comments(helper), helper, Placement(tsast.namespace_path(helper), "", True, name=name)))
        plan.done.append(f"{header}:{helper.line}: the helper {name} moves to {destination}, because only moved "
                         f"code reads it")
    moved.sort(key=lambda entry: entry[0].start)

    # (span, index of its placement) for each moved node
    spans: list[tuple[tuple[int, int], int]] = []
    previous: tuple[int, tuple[str, ...], bool] = (-1, (), False)
    for first, last, placement in moved:
        span = whole_lines(data, starts, first, last)
        if isinstance(span, str):
            plan.refused.append(f"{header}:{last.line}: the item {span}")
            continue
        text = data[span[0]:span[1]].decode("utf-8")
        if last.type == "static_assert_declaration" and last.ancestor_of_type(*FUNCTION_BODIES) is not None:
            indent = len(text) - len(text.lstrip(" "))
            text = "\n".join(line[indent:] if line.startswith(" " * indent) else line for line in text.split("\n"))
        is_adjacent = previous == (span[0], placement.target, placement.is_definition)
        previous = (span[1], placement.target, placement.is_definition)
        spans.append((span, len(plan.placements)))
        plan.placements.append(Placement(placement.target, text if text.endswith("\n") else text + "\n",
                                         placement.is_definition, placement.condition, placement.name, is_adjacent))
        if not placement.is_definition:
            plan.done.append(f"{header}:{last.line}: the check moves to {destination}")

    # A namespace whose whole body moves goes too.  Its own leading comment
    # moves with the first item that leaves it.
    covered = [span for span, _index in spans]
    for space in reversed(list(tree.find("namespace_definition"))):
        body = space.child_by_field("body")
        kids = [] if body is None else body.children
        if not kids or not all(any(low <= offset_of(starts, kid.start) and offset_of(starts, kid.end) <= high
                                   for low, high in covered) for kid in kids):
            continue
        first = leading_comments(space)
        span = whole_lines(data, starts, first, space)
        if isinstance(span, str):
            continue
        covered.append(span)
        inner = [(placed, index) for placed, index in spans if span[0] <= placed[0] < span[1]]
        if first is not space and inner:
            comment = data[span[0]:starts[space.start[0]]].decode("utf-8")
            index = min(inner)[1]
            kept = plan.placements[index]
            plan.placements[index] = Placement(kept.target, comment + kept.text, kept.is_definition,
                                               kept.condition, kept.name, kept.is_adjacent)
    plan.cuts.extend((low, high, "") for low, high in covered)
    return plan


def apply_cuts(data: bytes, cuts: list[tuple[int, int, str]]) -> bytes:
    """Apply the cuts of a plan to the bytes of a header.

    The cuts that remove whole lines merge when they overlap or touch, and a
    blank line that the removal leaves beside another blank line goes too.

    Args:
        data: The bytes of the header
        cuts: (start, end, replacement) in those bytes

    Returns:
        The new bytes
    """
    merged: list[list] = []
    for low, high, text in sorted(cuts):
        if merged and not text and not merged[-1][2] and low <= merged[-1][1]:
            merged[-1][1] = max(merged[-1][1], high)
            continue
        merged.append([low, high, text])
    out = data
    for low, high, text in reversed(merged):
        if not text:
            before_blank = low == 0 or out[:low].endswith(b"\n\n")
            while before_blank and out[high:high + 1] == b"\n":
                high += 1
        out = out[:low] + text.encode("utf-8") + out[high:]
    return out


def wrap(path: tuple[str, ...], body: str) -> str:
    """Return a body in the namespace blocks of a path."""
    if not path:
        return body
    name = "::".join(path)
    return f"namespace {name} {{\n\n{body}\n}}  // namespace {name}\n"


def place(root: Path, destination: str, header: str, placements: list[Placement]) -> tuple[str, list[str]]:
    """Return the new text of the destination, and one line for each check that it already states.

    A definition goes into the first namespace block that it fits, before
    each text that names it, and otherwise after the includes.  A check goes
    at the end of the last namespace block that it fits, and otherwise at the
    end of the file.  A check whose condition the destination already states
    in the same namespace is dropped.

    Args:
        root: The repository root
        destination: The destination, repo-relative
        header: The header, relative to include/
        placements: The text to place, in source order

    Returns:
        The text, and the dropped duplicates
    """
    path = root / destination
    # (namespace path, the start of the line after its `{`, the start of the line of its `}`)
    blocks: list[tuple[tuple[str, ...], int, int]] = []
    stated: set[tuple[tuple[str, ...], str]] = set()
    readers: dict[str, int] = {}
    include_end = 0
    # The start of a top-level main, before which a check that fits no block goes, or -1.
    main_start = -1
    if path.is_file():
        tree = next(iter(tsast.parse([path])))
        text = tree.source.decode("utf-8")
        source = tree.source
        starts = line_starts(source)
        for space in tree.find("namespace_definition"):
            body = space.child_by_field("body")
            if body is None or "" in tsast.namespace_path(body) or body.start[0] + 1 >= len(starts):
                continue
            opened = starts[body.start[0] + 1]
            closed = starts[body.end[0]]
            if source[offset_of(starts, body.start) + 1:opened].strip() \
                    or source[closed:offset_of(starts, body.end)].strip() != b"}":
                continue
            blocks.append((tsast.namespace_path(body), opened, closed))
        for check_node in tree.find("static_assert_declaration"):
            condition = check_node.child_by_field("condition")
            if condition is not None:
                stated.add((tsast.namespace_path(check_node), compact(tsast.spelled(condition))))
        names = frozenset(placement.name for placement in placements if placement.name)
        for leaf in tree.root.descendants_named(names, *NAME_LEAVES) if names else []:
            readers.setdefault(tsast.spelled(leaf), offset_of(starts, leaf.start))
        for include in tree.root.children_of_type("preproc_include"):
            include_end = offset_of(starts, include.end)
            include_end = include_end if tree.source[include_end - 1:include_end] == b"\n" else include_end + 1
        for function in tree.root.children_of_type("function_definition"):
            body = function.child_by_field("body")
            if body is not None and tsast.enclosing_function(body) == ("main",):
                main_start = starts[leading_comments(function).start[0]]
    else:
        text = f"// The compile-time checks of {header}.\n\n#include <{header}>\n"
        include_end = len(text.encode("utf-8"))
    data = text.encode("utf-8")
    dropped: list[str] = []
    # (position, 0 for a definition and 1 for a check, order, text)
    insertions: list[tuple[int, int, int, str]] = []
    groups: dict[tuple[bool, tuple[str, ...]], list[Placement]] = {}
    for placement in placements:
        key = (placement.target, placement.condition)
        if placement.condition and key in stated:
            dropped.append(f"the destination states the check {placement.condition} already")
            continue
        if placement.condition:
            stated.add(key)
        groups.setdefault((placement.is_definition, placement.target), []).append(placement)
    for order, ((is_definition, target), group) in enumerate(groups.items()):
        body = ""
        for placement in group:
            body += ("" if not body or placement.is_adjacent else "\n") + placement.text
        fits = [block for block in blocks if block[0] == target[:len(block[0])]]
        if is_definition:
            first_reader = min((readers[placement.name] for placement in group if placement.name in readers),
                               default=len(data))
            fits = sorted((block for block in fits if block[1] <= first_reader), key=lambda b: (-len(b[0]), b[1]))
            at, outer = (fits[0][1], fits[0][0]) if fits else (include_end, ())
            tail = "" if data[at:at + 1] == b"\n" else "\n"
            insertions.append((at, 0, order, "\n" + wrap(target[len(outer):], body) + tail))
            continue
        fits = sorted((block for block in fits if main_start < 0 or block[2] < main_start),
                      key=lambda b: (len(b[0]), b[2]))
        if fits:
            at, outer = fits[-1][2], fits[-1][0]
            lead = "" if data[:at].endswith(b"\n\n") else "\n"
            insertions.append((at, 1, order, lead + wrap(target[len(outer):], body) + "\n"))
        elif main_start >= 0:
            lead = "" if data[:main_start].endswith(b"\n\n") else "\n"
            insertions.append((main_start, 1, order, lead + wrap(target, body) + "\n"))
        else:
            lead = "\n" if data.endswith(b"\n") else "\n\n"
            insertions.append((len(data), 1, order, lead + wrap(target, body)))
    for position, _rank, _order, inserted in sorted(insertions, reverse=True):
        data = data[:position] + inserted.encode("utf-8") + data[position:]
    return data.decode("utf-8"), dropped


def fix(root: Path, ledger_path: Path, headers: list[str], into: str | None, dry_run: bool) -> int:
    """Move the checks of each header to its check file, and make each count a literal.

    Args:
        root: The repository root
        ledger_path: The ledger file
        headers: The headers, repo-relative or relative to include/
        into: The destination for one header that has no check file, or None
        dry_run: When true, print the changes and write nothing

    Returns:
        0 when each item is handled, 1 when the fix refuses an item, 2 on a usage error
    """
    found = scan(root)
    keeps = read_ledger(ledger_path).keeps
    checks, _stale = apply_keeps(found.checks, keeps)
    listed = {row for row, _line in read_not_standalone(root)[0]}
    if into is not None and len(headers) != 1:
        print("check-header-checks: --into takes exactly one header.", file=sys.stderr)
        return 2
    status = 0
    for given in headers:
        header = given if given.startswith(f"{INCLUDE}/") else f"{INCLUDE}/{given}"
        if header not in found.headers:
            print(f"check-header-checks: {header} is not a header under {INCLUDE}/.", file=sys.stderr)
            return 2
        inner = Path(header).relative_to(INCLUDE).as_posix()
        failures = [message for path, message in found.failures if path == header]
        if failures:
            print(f"{header}: {failures[0]}", file=sys.stderr)
            status = 1
            continue
        destination = into if into is not None else check_file_of(header)
        if into is None and inner in listed:
            print(f"{header}: the header has a row in {NOT_STANDALONE}, so it has no check file.  Name the file "
                  f"that receives its checks with --into, for example the test of the header.", file=sys.stderr)
            status = 1
            continue
        if into is not None and not any(
                include.child_by_field("path") is not None
                and tsast.prose_text(include.child_by_field("path")).strip()[1:-1].strip() == inner
                for tree in tsast.parse([root / into]) for include in tree.find("preproc_include")):
            print(f"{into}: the file does not include <{inner}>, so it cannot receive the checks of that header.",
                  file=sys.stderr)
            return 2
        items = [item for item in checks if item.path == header]
        plan = plan_fix(root, header, list(dict.fromkeys(items)), destination)
        header_bytes = (root / header).read_bytes()
        new_header = apply_cuts(header_bytes, plan.cuts)
        old_destination = (root / destination).read_text(encoding="utf-8") if (root / destination).is_file() else ""
        new_destination, dropped = place(root, destination, inner, plan.placements) if plan.placements \
            else (old_destination, [])
        for line in plan.done:
            print(f"done     {line}")
        for line in dropped:
            print(f"dropped  {header}: {line}")
        for line in plan.refused:
            print(f"REFUSED  {line}")
        status = status or (1 if plan.refused else 0)
        changes = [(header, header_bytes.decode("utf-8"), new_header.decode("utf-8")),
                   (destination, old_destination, new_destination)]
        for rel, before, after in changes:
            if before == after:
                continue
            if dry_run:
                sys.stdout.writelines(difflib.unified_diff(before.splitlines(keepends=True),
                                                           after.splitlines(keepends=True), f"a/{rel}", f"b/{rel}"))
            else:
                (root / rel).parent.mkdir(parents=True, exist_ok=True)
                (root / rel).write_text(after, encoding="utf-8")
    if not dry_run:
        print(f"check-header-checks: write the ledger again with python3 {SCRIPT} --write, and the walk list with "
              f"python3 utils/scripts/check-walk-units.py --write.", file=sys.stderr)
    return status


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
    ("inline constexpr auto inside_self_test = std::meta::members_of(^^Probe, ctx).size();", None,
     "a reflection walk inside a self-test namespace"),
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
    ("inline constexpr unsigned walked = std::meta::members_of(^^Holder, ctx).size();", EAGER,
     "a constexpr variable whose initializer calls a function of std::meta"),
    ("inline constexpr unsigned adl_walked = nonstatic_data_members_of(^^Holder, ctx).size();", EAGER,
     "a call of a reflection function with no qualifier"),
    ("constinit unsigned bound = std::define_static_array(enumerators_of(^^Kind)).size();", EAGER,
     "a constinit variable whose initializer calls std::define_static_array"),
    ("const unsigned table_size = edge_count<^^Holder>();", EAGER,
     "a const variable whose initializer calls a function that takes a reflection"),
    ("inline const char* const fixed_name = name_of(^^Holder);", EAGER,
     "a const pointer whose initializer takes a reflection"),
    ("inline const char* moving_name = name_of(^^Holder);", None,
     "a pointer to const whose initializer takes a reflection"),
    ("struct Counted { static constexpr auto count = std::meta::bases_of(^^Holder, ctx).size(); "
     "static constexpr int plain = compute(); };", EAGER, "a static data member whose initializer walks reflection"),
    ("using Spliced = [:std::meta::substitute(^^Box, {^^int}):];", EAGER, "an alias whose splice calls substitute"),
    ("inline void local_walk() { static constexpr auto members = std::define_static_array(std::meta::members_of("
     "^^Holder, ctx)); (void)members; }", EAGER, "a constexpr local of a function that walks reflection"),
    ("consteval bool expand() { template for (constexpr auto item : items) { (void)item; } return true; }", EAGER,
     "an expansion statement in a function body"),
    ("template <> inline constexpr int lazy<int> = enum_count(^^Kind);", EAGER,
     "an explicit specialization of a variable template whose initializer takes a reflection"),
    ("inline constexpr int looped = [] { int total = 0; for (auto item : items) total += [:item:]; return total; }();",
     EAGER, "a splice in a loop of a lambda that the initializer calls"),
    ("inline constexpr auto called_walk = [] { return std::meta::members_of(^^Holder, ctx).size(); }();", EAGER,
     "a reflection query in the body of a lambda that the initializer calls"),
    ("consteval bool expand_locals() { template for (constexpr auto item : items) { constexpr auto inner = "
     "std::meta::members_of(item, ctx).size(); (void)inner; } return true; }", EAGER,
     "an expansion statement whose body holds a reflection query, which counts one time"),
    ("inline constexpr int eager_call = compute();", None, "a plain call in a constexpr variable"),
    ("constinit int plain_bound = compute();", None, "a plain call in a constinit variable"),
    ("inline constexpr Color red = Color::hex(0xff0000);", None, "a cheap constexpr constructor"),
    ("using Plain = [:pick_type():];", None, "a splice of a plain call outside a loop"),
    ("inline void plain_local() { constexpr auto total = compute(); (void)total; }", None,
     "a constexpr local of a function with a plain call"),
    ("inline constexpr int literal = 3 + 4 * 2;", None, "an arithmetic expression on literals"),
    ("inline constexpr int casts = static_cast<int>(Kind::one) + int(2) + unsigned(3) + sizeof(Holder);", None,
     "a cast, a functional cast, an enumerator and sizeof"),
    ("inline constexpr auto bits = std::bit_cast<unsigned>(1.0f);", None, "a bit_cast"),
    ("inline constexpr bool unevaluated = noexcept(compute()) && sizeof(std::meta::members_of(^^Holder, ctx)) > 0;",
     None, "a reflection query in noexcept and sizeof"),
    ("inline constexpr bool required = requires { std::meta::members_of(^^Holder, ctx); };", None,
     "a reflection query in a requires-expression"),
    ("inline constexpr auto deferred = [] { return std::meta::members_of(^^Holder, ctx).size(); };", None,
     "a reflection query in a lambda that no code calls"),
    ("inline constexpr auto reflected = ^^Holder;", None, "a reflection with no call"),
    ("inline constexpr Box<int> braced{1, 2};", None, "a braced initializer with no call"),
    ("template <class T> inline constexpr auto lazy = std::meta::members_of(^^T, ctx).size();", None,
     "a reflection walk in a variable template"),
    ("template <class T> struct Lazy { static constexpr auto count = std::meta::members_of(^^T, ctx).size(); };",
     None, "a reflection walk in a static data member of a class template"),
    ("template <class T> void lazy_local() { constexpr auto total = std::meta::members_of(^^T, ctx).size(); "
     "(void)total; }", None, "a reflection walk in a constexpr local of a function template"),
    ("inline auto lazy_expand = [](auto pack) { template for (constexpr auto item : pack) { (void)item; } };", None,
     "an expansion statement in a generic lambda"),
    ("inline void runtime_local() { const auto value = std::meta::members_of(^^Holder, ctx).size(); (void)value; }",
     None, "a const local of a function, which needs no constant evaluation"),
    ("inline void plant_text() { std::string text; (void)std::define_static_string(text); }", INSTANCE,
     "a call of define_static_string outside each template"),
    ("template <class T> consteval auto plant_list() { return std::define_static_array("
     "std::meta::enumerators_of(^^T)); }", INSTANCE, "a call of define_static_array in a template, whose argument names T only in a reflection"),
    ("template <class T> consteval auto plant_string() { std::string text; return std::define_static_string(text); }",
     INSTANCE, "a call of define_static_string in a template, on a local of a type that names no parameter"),
    ("template <class T> consteval auto plant_anchored() { return std::define_static_array(static_cast<anchored_t<^^T, "
     "std::vector<std::meta::info>>>(std::meta::enumerators_of(^^T))); }", None,
     "a call whose argument is cast to a type that names T"),
    ("template <class T> consteval auto plant_span(std::span<const T> values) { return "
     "std::define_static_array(values); }", None, "a call on a parameter whose type names T"),
    ("template <class T> consteval auto plant_called() { return std::define_static_string(text_of<T>()); }", None,
     "a call on a call whose template argument names T"),
    ("template <class T> consteval auto plant_local() { anchored_t<^^T, std::string> text; return "
     "std::define_static_string(text); }", None, "a call on a local whose type names T"),
    ("template <class T> consteval auto plant_value() { constexpr std::meta::info bare = ^^T; return "
     "std::define_static_array(static_cast<anchored_t<bare, std::vector<std::meta::info>>>(std::meta::members_of(bare, "
     "ctx))); }", None, "a call whose cast names a local that T gives its value"),
    ("template <class... Ts> consteval auto plant_pack() { using list = anchored_t<std::meta::reflect_constant("
     "sizeof...(Ts)), std::vector<std::meta::info>>; return std::define_static_array(list{^^Ts...}); }", None,
     "a call on a braced initializer of a local alias that names the pack"),
    ("template <class T> struct PlantMember { using item_type = T; static constexpr auto item = ^^item_type; static "
     "constexpr auto list = std::define_static_array(static_cast<anchored_t<item, std::vector<std::meta::info>>>("
     "std::meta::members_of(item, ctx))); };", None,
     "a call in a class template whose cast names a static member that T gives its value"),
    ("consteval std::size_t plant_fold() { return std::meta::members_of(^^Holder, ctx).size(); }", FOLD,
     "a reflection query with constant arguments in a consteval function"),
    ("consteval bool plant_every(); consteval bool plant_fold_call() { return plant_every(); }", FOLD,
     "a call with no argument of a consteval function"),
    ("consteval int plant_loop() { int total = 0; for (auto member : std::meta::members_of(^^Holder, ctx)) total += "
     "member == ^^int; return total; }", FOLD, "a reflection query in the range of a loop"),
    ("void plant_stub() noexcept; consteval bool plant_error(std::meta::info type) { if (type == ^^void) plant_stub(); "
     "return std::meta::is_type(type); }", None, "a call of a stub and a reflection query of a parameter"),
    ("template <class T> consteval std::size_t plant_lazy_fold() { return std::meta::members_of(^^Holder, "
     "ctx).size(); }", None, "a reflection query with constant arguments in a consteval function template"),
    ("consteval int plant_current() { return use(std::meta::access_context::current()); }", None,
     "a plain call whose argument is a call of std with no argument"),
    ("constexpr std::size_t plant_constexpr() { return std::meta::members_of(^^Holder, ctx).size(); }", None,
     "a reflection query in a constexpr function that is not consteval"),
    ("consteval bool plant_asserted() { static_assert(std::meta::members_of(^^Holder, ctx).size() > 0); return true; }",
     FUNCTION_ASSERT, "a reflection query in a static_assert of a consteval function"),
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


# The planted header of the cases of --fix.  The function in the namespace
# detail is read only by the check file, so it moves.  The function that the
# header reads stays.
FIX_HEADER = """#pragma once
#include <meta>
namespace foundation {

enum class Shade : unsigned char {
    Red,
    Green,
    Blue,
};

enum class Mode : unsigned char {
    Plain,
#if defined(FIXED_WIDE)
    Wide,
#endif
};

inline constexpr unsigned shade_count = std::meta::enumerators_of(^^Shade).size();
inline constexpr unsigned mode_count = std::meta::enumerators_of(^^Mode).size();

// The rule that the moved check states.
static_assert(sizeof(int) == 4);
static_assert(sizeof(short) == 2);

template <class Kind>
consteval unsigned shared_size_() noexcept { return 3; }

// The namespace of the witness.
namespace detail {

template <class Kind>
consteval bool is_kind_read_() noexcept { return sizeof(Kind) == 1; }

// The witness, which only the check file reads.
consteval bool every_shade_read_() noexcept {
    static constexpr auto shades = std::define_static_array(std::meta::enumerators_of(^^Shade));
    template for (constexpr auto shade : shades) { (void)shade; }
    return is_kind_read_<Shade>() && shared_size_<Shade>() == 3;
}

}  // namespace detail

namespace fixed_detail::self_test {
static_assert(shade_count == 3);
}  // namespace fixed_detail::self_test

struct Holder {
    static int act(int width) {
        static_assert(sizeof(Shade) == 1);
        static_assert(sizeof(width) == 4);
        return width;
    }
};

consteval bool read_by_header_() noexcept {
    static constexpr auto shades = std::define_static_array(std::meta::enumerators_of(^^Shade));
    return shades.size() == shared_size_<Shade>();
}
inline constexpr bool header_reads = read_by_header_();

#if defined(FIXED_ARM)
static_assert(sizeof(long) == 8);
#endif

#define FIXED_CHECK static_assert(sizeof(char) == 1)
FIXED_CHECK;

}  // namespace foundation
"""
FIX_CHECK_FILE = """// The compile-time checks of foundation/Fixed.h.

#include <foundation/Fixed.h>

namespace foundation {

static_assert(detail::every_shade_read_());
static_assert(sizeof(Shade) == 1, "one byte");

}  // namespace foundation
"""
FIX_WITNESS = """// The witness, which only the check file reads.
consteval bool every_shade_read_() noexcept {
    static constexpr auto shades = std::define_static_array(std::meta::enumerators_of(^^Shade));
    template for (constexpr auto shade : shades) { (void)shade; }
    return is_kind_read_<Shade>() && shared_size_<Shade>() == 3;
}
"""
FIX_HELPER = """template <class Kind>
consteval bool is_kind_read_() noexcept { return sizeof(Kind) == 1; }
"""


def fix_self_test(expect) -> None:
    """Run the cases of --fix in a scratch tree of their own.

    Args:
        expect: The recorder of the self-test, expect(name, holds, negative)
    """
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        files = {
            "include/foundation/Fixed.h": FIX_HEADER,
            f"{CHECKS}/foundation/Fixed.cpp": FIX_CHECK_FILE,
            "include/fixy/Lone.h": "#pragma once\nnamespace fixy::inner {\nstatic_assert(sizeof(char) == 1);\n}\n",
            "include/crucible/Probe.h": "#pragma once\nnamespace crucible {\nnamespace probe_detail::self_test {\n"
                                        "static_assert(sizeof(short) == 2);\n}\n}\n",
            NOT_STANDALONE: "crucible/Probe.h | it includes a header that is not on the include path\n",
            "include/fixy/Walked.h": "#pragma once\n#include <meta>\nnamespace fixy {\nenum class Tone { Low };\n"
                                     "consteval bool walk_tones_() {\n    static constexpr auto tones = "
                                     "std::define_static_array(std::meta::enumerators_of(^^Tone));\n"
                                     "    return tones.size() == 1;\n}\n}\n",
            "include/fixy/Caller.h": "#pragma once\n#define FIXY_CALL_WALK ::fixy::walk_tones_()\n",
            "test/test_probe.cpp": "#include <crucible/Probe.h>\n\nint main() { return 0; }\n",
            "test/test_other.cpp": "int main() { return 0; }\n",
        }
        for rel, text in files.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        ledger = root / LEDGER
        ledger.parent.mkdir(parents=True, exist_ok=True)

        def run(*arguments: str) -> tuple[int, str]:
            """Run --fix in the scratch tree and keep its report."""
            buffer = io.StringIO()
            headers = [argument for argument in arguments if not argument.startswith("--")]
            into = next((argument.removeprefix("--into=") for argument in arguments
                         if argument.startswith("--into=")), None)
            with contextlib.redirect_stderr(buffer), contextlib.redirect_stdout(buffer):
                code = fix(root, ledger, headers, into, "--dry-run" in arguments)
            return code, buffer.getvalue()

        def read(rel: str) -> str:
            """Return the text of one file of the scratch tree."""
            return (root / rel).read_text(encoding="utf-8")

        code, report = run("include/foundation/Fixed.h", "--dry-run")
        expect("--dry-run writes nothing and prints each change",
               read("include/foundation/Fixed.h") == FIX_HEADER and read(f"{CHECKS}/foundation/Fixed.cpp")
               == FIX_CHECK_FILE and "+++ b/test/layer/checks/foundation/Fixed.cpp" in report, True)
        code, report = run("foundation/Fixed.h")
        header = read("include/foundation/Fixed.h")
        checks = read(f"{CHECKS}/foundation/Fixed.cpp")
        expect("the fix refuses an item and still handles the others", code == 1 and "REFUSED" in report)
        expect("a count of the enumerators of an enum of the header becomes its literal, and the check file "
               "derives it",
               "inline constexpr unsigned shade_count = 3;" in header
               and "static_assert(shade_count == std::meta::enumerators_of(^^Shade).size()," in checks)
        expect("a count over an enum with a preprocessor directive is refused",
               "std::meta::enumerators_of(^^Mode).size()" in header and "directive at line" in report, True)
        expect("a moved function keeps its text and its comment, and stands before its first reader",
               FIX_WITNESS in checks and checks.index(FIX_WITNESS) < checks.index("detail::every_shade_read_()"))
        expect("a helper that only the moved function reads moves before it, and a helper that the header reads stays",
               FIX_HELPER in checks and checks.index(FIX_HELPER) < checks.index(FIX_WITNESS)
               and "is_kind_read_" not in header and "consteval unsigned shared_size_()" in header
               and "consteval unsigned shared_size_()" not in checks)
        expect("a namespace that holds only moved items goes, and its comment moves with the first item",
               "namespace detail" not in header and "// The namespace of the witness.\n" + FIX_HELPER in checks
               and "every_shade_read_" not in header)
        expect("a namespace-scope check and a self-test namespace move with their comments, in their namespaces, "
               "and two adjacent checks stay adjacent",
               "// The rule that the moved check states.\nstatic_assert(sizeof(int) == 4);\n"
               "static_assert(sizeof(short) == 2);\n" in checks
               and "namespace fixed_detail::self_test {" in checks and "self_test" not in header
               and "sizeof(int) == 4" not in header)
        expect("a function static_assert whose names resolve at namespace scope leaves the function, and a check "
               "that the check file states already is dropped",
               "sizeof(Shade) == 1);" not in header and checks.count("sizeof(Shade) == 1") == 1
               and "states the check sizeof(Shade)==1 already" in report)
        expect("a function static_assert that names a parameter is refused",
               "static_assert(sizeof(width) == 4);" in header and "declares width" in report, True)
        expect("a function that the header reads is refused, with the reader",
               "consteval bool read_by_header_()" in header and "include/foundation/Fixed.h:" in report
               and "readers outside" in report, True)
        expect("a check in an arm of a preprocessor conditional is refused",
               "static_assert(sizeof(long) == 8);" in header and "preprocessor conditional" in report, True)
        expect("a check that a macro writes is refused", "FIXED_CHECK;" in header and "the macro FIXED_CHECK" in report,
               True)
        rescan = scan(root)
        expect("the check file is well formed after the fix", not rescan.bad_check_files and not rescan.failures)
        left = sorted(item.key for item in rescan.checks if item.path == "include/foundation/Fixed.h")
        expect("the header holds only the refused items after the fix",
               left == sorted(["FIXED_CHECK", "foundation::mode_count", "foundation::read_by_header_::shades",
                               "foundation::read_by_header_::shared_size_<Shade>()",
                               "foundation::read_by_header_::std::define_static_array("
                               "std::meta::enumerators_of(^^Shade))", "sizeof(long)==8", "sizeof(width)==4"]))
        expect("a call in a function that the header reads is refused, with its repair",
               "shared_size_<Shade>()" in report and "eager fold" in report and "eager instantiation" in report, True)
        expect("a second run changes nothing",
               run("include/foundation/Fixed.h")[0] == 1 and read("include/foundation/Fixed.h") == header
               and read(f"{CHECKS}/foundation/Fixed.cpp") == checks, True)
        code, report = run("include/fixy/Walked.h")
        expect("a function that a macro body of another header names is refused, with the macro",
               code == 1 and "include/fixy/Caller.h:2" in report and "walk_tones_" in read("include/fixy/Walked.h"),
               True)
        code, report = run("include/fixy/Lone.h")
        lone = read(f"{CHECKS}/fixy/Lone.cpp") if (root / CHECKS / "fixy/Lone.cpp").is_file() else ""
        expect("a header with no check file gets one, which includes the header first",
               code == 0 and lone.startswith("// The compile-time checks of fixy/Lone.h.\n\n#include <fixy/Lone.h>\n")
               and "namespace fixy::inner {\n\nstatic_assert(sizeof(char) == 1);" in lone
               and "static_assert" not in read("include/fixy/Lone.h"))
        code, report = run("include/crucible/Probe.h")
        expect("a header that cannot compile alone needs --into",
               code == 1 and "--into" in report and "self_test" in read("include/crucible/Probe.h"), True)
        code, report = run("include/crucible/Probe.h", "--into=test/test_other.cpp")
        expect("--into refuses a file that does not include the header",
               code == 2 and "self_test" in read("include/crucible/Probe.h"), True)
        code, report = run("include/crucible/Probe.h", "--into=test/test_probe.cpp")
        probe_test = read("test/test_probe.cpp")
        expect("--into moves the checks into the named file, before its main",
               code == 0 and "self_test" not in read("include/crucible/Probe.h")
               and 0 <= probe_test.find("namespace probe_detail::self_test {") < probe_test.find("int main()"))
        with contextlib.redirect_stderr(io.StringIO()), contextlib.redirect_stdout(io.StringIO()):
            agrees = write(root, ledger) == 0 and check(root, ledger) == 0
        expect("--write and the check agree with the tree after the fix", agrees)


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
                   "static_assert(sizeof(char) == 1);\ninline constexpr auto kept_value = "
                   "std::meta::members_of(^^crucible, ctx).size();\n}  // namespace crucible\n")
    keep_row = "keep | include/crucible/Kept.h | static_assert | sizeof(long)==8 | the planted reason"
    eager_keep_row = "keep | include/crucible/Kept.h | eager evaluation | crucible::kept_value | the planted reason"
    planted_rows = ("include/foundation/Planted.h | 5 | 9\n"
                    "function static_assert | include/foundation/Planted.h | 6\n"
                    "eager evaluation | include/foundation/Planted.h | 13\n"
                    "eager instantiation | include/foundation/Planted.h | 5\n"
                    "eager fold | include/foundation/Planted.h | 3\n")
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
                   "1==1", "2==2", "10==10", "11==11", "12==12", "foundation::walked", "foundation::adl_walked",
                   "foundation::bound", "foundation::table_size", "foundation::fixed_name",
                   "foundation::Counted::count", "foundation::Spliced", "foundation::local_walk::members",
                   "foundation::expand::template for(items)", "foundation::lazy", "foundation::looped",
                   "foundation::called_walk", "foundation::expand_locals::template for(items)",
                   "foundation::std::define_static_array(enumerators_of(^^Kind))",
                   "foundation::local_walk::std::define_static_array(std::meta::members_of(^^Holder,ctx))",
                   "foundation::plant_text::std::define_static_string(text)",
                   "foundation::plant_list::std::define_static_array(std::meta::enumerators_of(^^T))",
                   "foundation::plant_string::std::define_static_string(text)",
                   "foundation::plant_fold::std::meta::members_of(^^Holder,ctx)",
                   "foundation::plant_fold_call::plant_every()",
                   "foundation::plant_loop::std::meta::members_of(^^Holder,ctx)",
                   "std::meta::members_of(^^Holder,ctx).size()>0"})
        asserted = next(line for line, (text, _, _) in enumerate(PLANTED, start=1) if "plant_asserted" in text)
        expect("a reflection query in a static_assert of a consteval function counts as a function static_assert "
               "and not as an eager fold", (asserted, FOLD) not in rows and (asserted, FUNCTION_ASSERT) in rows, True)
        reasons = {item.key: item.detail for item in found.checks if item.kind in (INSTANCE, FOLD)}
        expect("each call names why it counts",
               reasons.get("foundation::plant_text::std::define_static_string(text)") == "a call outside each template"
               and reasons.get("foundation::plant_list::std::define_static_array(std::meta::enumerators_of(^^T))")
               == "no argument has a type that depends on a template parameter"
               and reasons.get("foundation::plant_fold::std::meta::members_of(^^Holder,ctx)")
               == "a reflection query with constant arguments"
               and reasons.get("foundation::plant_fold_call::plant_every()") == "a call with no argument")
        queries = {item.key: item.detail for item in found.checks if item.kind == EAGER}
        expect("each eager evaluation names its reflection query",
               queries.get("foundation::walked") == "a call of std::meta::members_of"
               and queries.get("foundation::adl_walked") == "a call of nonstatic_data_members_of"
               and queries.get("foundation::table_size") == "a call that takes a reflection"
               and queries.get("foundation::looped") == "a splice in a loop"
               and queries.get("foundation::expand::template for(items)") == "an expansion statement")
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
               and "include/foundation/Planted.h:32: warning: [header-checks] the reflection walk of "
                   "foundation::walked (a call of std::meta::members_of)" in report
               and "eager_call" not in report and "foundation::red" not in report)
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
                ("inline constexpr auto clean_eager = std::meta::members_of(^^Clean, ctx).size();",
                 "a reflection walk", EAGER),
                ("inline void clean_text() { std::string text; (void)std::define_static_string(text); }",
                 "a static string outside a template", INSTANCE),
                ("consteval std::size_t clean_fold() { return std::meta::members_of(^^Clean, ctx).size(); }",
                 "a reflection query with constant arguments in a consteval function", FOLD)):
            (root / "include/fixy/Clean.h").write_text(clean_planted + planted_line + "\n", encoding="utf-8")
            code, report = verdict()
            expect(f"{label} planted in a clean header is an error",
                   code == 1 and "include/fixy/Clean.h:3: error: [header-checks]" in report
                   and f"of the kind {kind}, and the ledger permits 0" in report)
        (root / "include/fixy/Clean.h").write_text(clean_planted + "template <class T> inline void clean_body() "
                                                   "{ static_assert(sizeof(T) > 0); }\ntemplate <class T> inline "
                                                   "constexpr auto clean_eager = std::meta::members_of(^^T, ctx)"
                                                   ".size();\ntemplate <class T> consteval auto clean_text() { "
                                                   "anchored_t<^^T, std::string> text; return "
                                                   "std::define_static_string(text); }\ntemplate <class T> consteval "
                                                   "std::size_t clean_fold() { return std::meta::members_of(^^Clean, "
                                                   "ctx).size(); }\n", encoding="utf-8")
        code, report = verdict()
        expect("the same function static_assert, reflection walk, static string and reflection query in templates "
               "are no finding",
               code == 0 and "include/fixy/Clean.h" not in report, True)
        (root / "include/fixy/Clean.h").write_text(clean_planted + "inline constexpr Color clean_red = "
                                                   "Color::hex(0xff0000);\ninline constexpr int clean_count = "
                                                   "compute();\n", encoding="utf-8")
        code, report = verdict()
        expect("a cheap constexpr constant and a plain call, planted in a clean header, are no finding",
               code == 0 and "include/fixy/Clean.h" not in report, True)
        (root / "include/fixy/Clean.h").write_text(clean_planted + "PLANTED_PAIR(Clean);\n", encoding="utf-8")
        code, report = verdict()
        expect("a macro that writes static_asserts, planted in a clean header, is an error",
               code == 1 and "include/fixy/Clean.h:3: error: [header-checks] the invocation of the macro PLANTED_PAIR "
                             "makes 2 namespace-scope static_assert(s)" in report, True)
        (root / "include/fixy/Clean.h").write_text(clean_planted, encoding="utf-8")
        for row, label in (("include/foundation/Planted.h | 5 | 8", "static_assert"),
                           ("include/foundation/Planted.h | 4 | 9", "self-test namespace"),
                           ("function static_assert | include/foundation/Planted.h | 5", "function static_assert"),
                           ("eager evaluation | include/foundation/Planted.h | 12", "eager evaluation"),
                           ("eager instantiation | include/foundation/Planted.h | 4", "eager instantiation"),
                           ("eager fold | include/foundation/Planted.h | 2", "eager fold")):
            first = row.split(SEPARATOR)[0]
            original = next(text for text in planted_rows.splitlines() if text.split(SEPARATOR)[0] == first)
            ledger.write_text(matching.replace(original, row), encoding="utf-8")
            code, report = verdict()
            expect(f"one more {label} than the row permits is an error",
                   code == 1 and ": error: [header-checks]" in report and "include/foundation/Planted.h:" in report)
        for row, label in (("include/foundation/Planted.h | 5 | 10", "static_assert"),
                           ("function static_assert | include/foundation/Planted.h | 7", "function static_assert"),
                           ("eager evaluation | include/foundation/Planted.h | 14", "eager evaluation"),
                           ("eager instantiation | include/foundation/Planted.h | 6", "eager instantiation"),
                           ("eager fold | include/foundation/Planted.h | 4", "eager fold")):
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
                             ("eager evaluation | include/foundation/Planted.h | 13", "a second eager row"),
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
    fix_self_test(expect)
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
    modes.add_argument("--fix", metavar="HEADER", nargs="+",
                       help="move the checks of each header to its check file, and make each count a literal")
    parser.add_argument("--into", metavar="FILE", help="with --fix: the file that receives the checks of a header "
                                                       "that has no check file")
    parser.add_argument("--dry-run", action="store_true", help="with --fix: print the changes and write nothing")
    check_report.add_arguments(parser)
    try:
        options = parser.parse_args(argv)
    except SystemExit as stop:
        return 0 if stop.code == 0 else 2
    if (options.into is not None or options.dry_run) and not options.fix:
        print("check-header-checks: --into and --dry-run take --fix.", file=sys.stderr)
        return 2
    root = tsast.REPO_ROOT
    ledger = root / LEDGER
    try:
        if options.self_test:
            return self_test()
        if options.fix:
            return fix(root, ledger, options.fix, options.into, options.dry_run)
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
