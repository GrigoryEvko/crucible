#!/usr/bin/env python3
"""check-banned-calls — the reinterpret_cast, vector reserve, file-stream and process-spawn bans, read from the AST.

CLAUDE.md §III bans `reinterpret_cast`, and CLAUDE.md §IV bans
`std::vector::reserve`.  The fixy-only band-3 directories ban the C++ file
streams.  CLAUDE.md §IX bans a raw process spawn in production code,
because the child carries no Permission and no effect row.  This guard
finds each use in the parse tree of the pinned
tree-sitter kit (utils/scripts/tsast.py), so a comment, a string literal or a raw
string cannot hold a false hit.  A use that spans lines, a use with a space
before its `<` or `(`, and a qualified member call are still found.

THE FOUR BANS
    reinterpret_cast   every identifier token spelled `reinterpret_cast`,
                       because the word is a keyword and has no other use.
                       Roots: include/, src/ and vessel/.
    reserve            every call whose callee is a member access that names
                       `reserve`: `v.reserve(n)`, `p->reserve(n)`,
                       `v.Base::reserve(n)` and `v.template reserve<T>(n)`.
                       The ban rejects the call when the receiver has a std
                       type, and when the parse tree does not show the type
                       of the receiver.  RECEIVER TYPES gives the rules.
                       Roots: include/, src/ and bench/.
    file stream        every name of a C++ file stream or file buffer
                       (ofstream, ifstream, fstream, filebuf, their wide and
                       basic_ spellings), with or without `std::`, and every
                       `#include <fstream>`.  A file there goes through
                       fixy::fs::mint_file or fixy::mint_owned_file.
                       Roots: each include/crucible directory that
                       utils/scripts/fixy-only-paths.txt lists, and its src/
                       twin.  A list that names no such directory fails
                       the guard, because the ban would then read nothing.
    process spawn      every reference to a C library function that creates
                       a process, replaces its image or reaps a child (fork,
                       vfork, clone, the exec family, posix_spawn, system,
                       popen, daemon, the wait family and more), and every
                       syscall number of the same kernel entries (SYS_clone,
                       __NR_execve and more).  A reference is a name in an
                       expression or a template argument, bare, qualified
                       from `::`, or qualified by std or a namespace alias
                       of std, so a call, an address, a function pointer
                       and a using-declaration are all found.  A member, the
                       name of a declaration or an enumerator, and a name
                       qualified by another namespace or a class are not.
                       Roots: include/, src/ and vessel/, without the kernel
                       dump under include/crucible/perf/bpf/.
    A path with a component named test, examples, third_party, external or
    vendor, or a component that starts with build, is out of scope.  The
    reinterpret_cast ban also skips bench/.

MACRO BODIES
    The body of each #define is parsed as a C++ fragment (tsast.macro_bodies),
    and the same node rules run on that tree.  In a fragment the name of a
    declaration also counts for the spawn ban, because a body such as
    `macro_name fork` is text that a use site completes.  A namespace alias
    of std comes from the file that holds the #define.  A body that does not
    parse, for example one with `#` or `##`, is read as preprocessing tokens:
    a banned name counts unless `.` or `->` stands before it, and a spawn name
    also counts only when no qualifier other than std or an alias of std
    stands before it.  A file that the parser cannot read is a guard failure.
    A file that is not C++ (tsast.is_in_cpp_scope) is out of scope.

RECEIVER TYPES
    The receiver of a reserve call is a name, a name in parentheses, or
    `this->name`.  The ban finds the declaration of the name in the scopes
    around the call, from the innermost scope out:
    - A local variable before the call, also in the head of a for, if,
      while or switch statement
    - A parameter of the function, of the lambda or of the catch clause
    - A data member of the class of the member function.  For a member
      function that a file defines out of its class, the class comes from
      the files that the ban reads.
    - A variable at namespace scope before the call, in the same file.
    The ban reads the declared type through the type aliases and the
    typedefs of the scopes around the declaration, and through the aliases
    and the classes that the files of the ban declare.  A name in namespace
    std, or in a namespace alias of std, is a std type.  An unqualified name
    is a std type too when a using-declaration or a using-directive of std
    applies to it.
    The ban also rejects the call when the parse tree does not show the type.
    These cases apply: a receiver of another shape, a name with no
    declaration in these scopes, a structured binding, an init-capture,
    `auto`, `decltype`, a template parameter, and a type name that no file
    of the ban declares.  The ban rejects a type name that has an alias to a
    std type and a class of the same name.  The message of each rejected
    call names its case.

WHAT THE GUARD CANNOT SEE
    A name that a macro builds with `##`, a syscall number written as a
    literal, and a call through a namespace of another file that holds a
    using-directive for std.

EXEMPTIONS
    A comment on the line of the banned token that says
    `NO-REINTERPRET-OK: <reason>` or `NO-RESERVE-OK: <reason>` exempts that
    line.  The reason must not be empty.
    `NO-FILE-STREAM-OK: <reason>` and `SPAWN-PROCESS-OK: <reason>` exempt a
    line of their ban the same way.
    An entry `path:text` in the ban's allowlist exempts the site whose line,
    trimmed, equals `text`.  The key is the content of the line, not its
    number, so an edit above the site does not move it.  An entry that
    matches no live site is stale.

Exit 0 clean, 1 on a violation or a parse error, 2 on a stale entry or a
usage error, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import enum
import functools
import io
import os
import re
import sys
import tempfile
from collections import defaultdict
from collections.abc import Callable, Iterator, Sequence
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402

EXCLUDED_COMPONENTS = frozenset({"test", "examples", "third_party", "external", "vendor"})
FILE_STREAMS = frozenset({
    "ofstream", "ifstream", "fstream", "filebuf",
    "wofstream", "wifstream", "wfstream", "wfilebuf",
    "basic_ofstream", "basic_ifstream", "basic_fstream", "basic_filebuf",
})
# The C library functions that create a process, replace its image or reap
# a child, and the syscall numbers that reach the same kernel entry through
# syscall(2).
SPAWN_NAMES = frozenset({
    "fork", "vfork", "_Fork", "forkpty", "daemon", "clone", "clone3", "__clone", "__clone2",
    "execve", "execveat", "fexecve", "execv", "execvp", "execvpe", "execl", "execlp", "execle",
    "posix_spawn", "posix_spawnp", "system", "popen", "pclose", "waitpid", "wait3", "wait4", "waitid",
    "SYS_fork", "SYS_vfork", "SYS_clone", "SYS_clone3", "SYS_execve", "SYS_execveat",
    "__NR_fork", "__NR_vfork", "__NR_clone", "__NR_clone3", "__NR_execve", "__NR_execveat",
})
# The data file that lists the fixy-only directories, one per line.
FIXY_ONLY_PATHS = "utils/scripts/fixy-only-paths.txt"
# The node types of a class head, and of each declaration of a template parameter.
CLASS_SPECIFIERS = ("class_specifier", "struct_specifier", "union_specifier", "enum_specifier")
TEMPLATE_PARAMETERS = (
    "type_parameter_declaration", "optional_type_parameter_declaration",
    "variadic_type_parameter_declaration", "template_template_parameter_declaration",
    "parameter_declaration", "optional_parameter_declaration", "variadic_parameter_declaration",
)
# The node types of the parameters of a function, a lambda and a catch clause.
PARAMETERS = ("parameter_declaration", "optional_parameter_declaration", "variadic_parameter_declaration")
# The node types of a preprocessor conditional and of each of its arms.
PREPROCESSOR_ARMS = ("preproc_if", "preproc_ifdef", "preproc_else", "preproc_elif", "preproc_elifdef")
# The maximum number of type aliases that the ban reads for one type.
MAX_ALIAS_DEPTH = 16


@dataclass(frozen=True)
class Hit:
    """One use of a banned construct.

    Attributes:
        details: Why the ban rejects the use, one text for each use on the row.
            The bans other than reserve give no detail.
    """

    path: str
    row: int
    key: str
    details: tuple[str, ...] = ()


class TypeIndex:
    """The named classes and the type aliases of the files that a ban reads, by the last part of each name.

    A declaration in a function body is local, so the index leaves it out.
    The receiver rule of the reserve ban finds a local declaration through
    the scopes around its use.  The index builds its tables on the first
    query, so a ban that makes no query does no work for the index.
    """

    def __init__(self, trees: Sequence[tsast.Tree], root: Path) -> None:
        """Keep the trees of the files of one ban.

        Args:
            trees: The parsed files
            root: The scan root, which the place of a declaration in a message is relative to
        """
        self._trees = tuple(trees)
        self._root = root
        self._std_names: dict[tsast.Tree, frozenset[str]] = {}
        self._aliases: dict[tsast.Tree, list[tsast.NamespaceAlias]] = {}

    def place(self, node: tsast.Node) -> str:
        """Return the file and the line of a node as `PATH:LINE`, with PATH relative to the scan root."""
        path = Path(node.tree.path)
        shown = path.relative_to(self._root) if path.is_relative_to(self._root) else path
        return f"{shown.as_posix()}:{node.start[0] + 1}"

    @functools.cached_property
    def _tables(self) -> tuple[dict[str, list[tsast.Node]], dict[str, list[tsast.Node]]]:
        """Build the table of the classes and the table of the aliases.

        Complexity: O(total nodes of the trees), one time.

        Returns:
            (the class heads by name, the alias and typedef declarations by name)
        """
        classes: defaultdict[str, list[tsast.Node]] = defaultdict(list)
        aliases: defaultdict[str, list[tsast.Node]] = defaultdict(list)
        for tree in self._trees:
            for node in tree.find(*CLASS_SPECIFIERS, "alias_declaration", "type_definition"):
                if _is_local(node):
                    continue
                for name in _declared_type_names(node):
                    (classes if node.type in CLASS_SPECIFIERS else aliases)[name].append(node)
        return dict(classes), dict(aliases)

    def classes(self, name: str) -> list[tsast.Node]:
        """Return each class, struct, union and enum head at namespace or class scope with the name."""
        return self._tables[0].get(name, [])

    def aliases(self, name: str) -> list[tsast.Node]:
        """Return each alias declaration and typedef at namespace or class scope that declares the name."""
        return self._tables[1].get(name, [])

    def std_names(self, tree: tsast.Tree) -> frozenset[str]:
        """Return std and the namespace aliases of std of one tree, one time for each tree."""
        if tree not in self._std_names:
            self._std_names[tree] = _std_names(tree)
        return self._std_names[tree]

    def namespace_aliases(self, tree: tsast.Tree) -> list[tsast.NamespaceAlias]:
        """Return the namespace aliases of one tree, one time for each tree."""
        if tree not in self._aliases:
            self._aliases[tree] = tsast.namespace_aliases(tree)
        return self._aliases[tree]


@dataclass(frozen=True)
class Scope:
    """What a node rule needs to know about the text it reads.

    Attributes:
        std_names: std and every namespace alias of std in the file that
            holds the text
        is_fragment: True for the parse of a macro body, where the name of a
            declaration is text that a use site completes
        types: The classes and the type aliases of each file that the ban reads
    """

    std_names: frozenset[str]
    is_fragment: bool
    types: TypeIndex


@dataclass(frozen=True)
class Ban:
    """One banned construct, where it is banned, and how it is exempted.

    Attributes:
        node_hits: The node rule, over a parse tree.  It gives each use with
            the reason why the ban rejects it, or with "" when the rule gives none
        token_hit: The rule over the preprocessing tokens of a macro body
            that does not parse: the token list and an index, true when the
            token at the index is a use
        token_detail: The reason why the ban rejects a use that the token rule finds
        excluded_prefixes: Repo-relative path prefixes that the ban skips
    """

    name: str
    roots: Callable[[Path], tuple[str, ...]]
    extra_excluded: frozenset[str]
    marker: str
    allowlist: str
    rule: str
    node_hits: Callable[[tsast.Tree, Scope], Iterator[tuple[tsast.Node, str]]]
    token_hit: Callable[[list[tsast.Token], int, Scope], bool]
    token_detail: str = ""
    excluded_prefixes: tuple[str, ...] = ()


def _after_member_access(tokens: list[tsast.Token], index: int) -> bool:
    """Return True when a member access (`.` or `->`) stands before the token, past `template` and `X ::` pairs."""
    at = index - 1
    while at >= 1 and tokens[at].text == "::" and tokens[at - 1].kind == "identifier":
        at -= 2
    if at >= 0 and tokens[at].text == "template":
        at -= 1
    return at >= 0 and tokens[at].text in (".", "->")


def _reinterpret_nodes(tree: tsast.Tree, scope: Scope) -> Iterator[tuple[tsast.Node, str]]:
    """Yield each identifier token spelled reinterpret_cast.

    Args:
        tree: One parsed file or macro body
        scope: Unused, the keyword has no other use

    Yields:
        The identifier node of each cast, with no detail
    """
    for node in tree.find("identifier"):
        if node.text == "reinterpret_cast":
            yield node, ""


def _reinterpret_token(tokens: list[tsast.Token], index: int, scope: Scope) -> bool:
    """Return True for the keyword reinterpret_cast in the tokens of an unparsed macro body."""
    return tokens[index].text == "reinterpret_cast"


def _final_name(node: tsast.Node) -> tsast.Node | None:
    """Return the name that a member-access field finally names.

    Args:
        node: The `field` child of a field_expression

    Returns:
        The field_identifier or identifier node, or None for another shape
    """
    current: tsast.Node | None = node
    while current is not None:
        if current.type in ("field_identifier", "identifier"):
            return current
        if current.type in ("qualified_identifier", "template_method"):
            current = current.child_by_field("name")
        elif current.type == "dependent_name":
            named = current.children
            current = named[-1] if named else None
        else:
            return None
    return None


class Shown(enum.IntEnum):
    """What the parse tree shows about the type of a receiver.  A larger value is a stronger reason to reject a call."""

    NOT_STD = 0
    UNKNOWN = 1
    STD = 2


@dataclass(frozen=True)
class Verdict:
    """The type verdict on one receiver, or on one type that a receiver can have.

    Attributes:
        shown: What the parse tree shows
        why: The reason, a phrase at the end of the message of a rejected call
    """

    shown: Shown
    why: str = ""


def _worst(verdicts: Iterator[Verdict | None], empty: str) -> Verdict:
    """Return the strongest verdict, the first one in source order on a tie.

    Args:
        verdicts: The verdicts.  A None adds nothing, for an alias that the
            chain read before
        empty: The reason when no verdict remains

    Returns:
        The strongest verdict, or UNKNOWN with the reason EMPTY
    """
    found = [verdict for verdict in verdicts if verdict is not None]
    return max(found, key=lambda verdict: verdict.shown) if found else Verdict(Shown.UNKNOWN, empty)


def _is_local(node: tsast.Node) -> bool:
    """Return True when a node lies in a function body, a lambda or a requires-expression."""
    return node.ancestor_of_type("compound_statement", "lambda_expression", "requires_expression") is not None


def _encloses(outer: tsast.Node, inner: tsast.Node) -> bool:
    """Return True when the span of a node holds a node of the same tree."""
    return outer.tree is inner.tree and outer.start <= inner.start and inner.end <= outer.end


def _declared_type_names(node: tsast.Node) -> list[str]:
    """Return the type names that a class head, an alias declaration or a typedef declares.

    Args:
        node: A class head, an alias_declaration or a type_definition

    Returns:
        The last part of each declared name.  A typedef can declare more than one
    """
    if node.type == "type_definition":
        names = (tsast.leaf_name(child) for child in node.children if child.field == "declarator")
        return [name for name in names if name]
    named = node.child_by_field("name")
    name = None if named is None else tsast.leaf_name(named)
    return [name] if name else []


def _is_function_declarator(declarator: tsast.Node) -> bool:
    """Return True when a declarator declares a function, through pointer, reference and init declarators."""
    current: tsast.Node | None = declarator
    while current is not None and current.type in (
            "init_declarator", "pointer_declarator", "reference_declarator", "attributed_declarator"):
        current = current.child_by_field("declarator")
    return current is not None and current.type == "function_declarator"


def _declaration_types(name: str, item: tsast.Node) -> list[tsast.Node | None] | str | None:
    """Return the declared type of NAME when one declaration declares it.

    Args:
        name: The receiver name
        item: Any node.  Only a declaration and a field_declaration can declare it

    Returns:
        The type nodes of the declaration (None for a declaration with no type
        node), a reason when the declaration cannot show a type, or None when
        the item does not declare the name
    """
    if item.type not in ("declaration", "field_declaration"):
        return None
    for declarator in item.children:
        if declarator.field != "declarator" or _is_function_declarator(declarator):
            continue
        bound = declarator.child_by_field("declarator") if declarator.type == "init_declarator" else declarator
        if bound is not None and bound.type == "structured_binding_declarator":
            if any(child.text == name for child in bound.children):
                return "a structured binding declares it"
            continue
        if tsast.leaf_name(declarator) == name:
            return [item.child_by_field("type")]
    return None


def _items(container: tsast.Node) -> Iterator[tsast.Node]:
    """Yield the items of a block, a namespace body or a class body, and the items of each preprocessor arm in it.

    Complexity: O(items), and the depth of the nested arms.
    """
    for item in container.children:
        if item.type in PREPROCESSOR_ARMS:
            yield item
            yield from _items(item)
        elif item.field != "condition":
            yield item


def _items_before(name: str, container: tsast.Node, child: tsast.Node) -> list[tsast.Node | None] | str | None:
    """Return the type of the last declaration of NAME among the items of a block before CHILD.

    An item in a preprocessor arm counts, because a build can select the arm.

    Complexity: O(items of the block).
    """
    found: list[tsast.Node | None] | str | None = None
    for item in _items(container):
        if item == child:
            break
        declared = _declaration_types(name, item)
        if declared is not None:
            found = declared
    return found


def _member_types(name: str, body: tsast.Node) -> list[tsast.Node | None] | str | None:
    """Return the type of each data member named NAME in a class body, also in a preprocessor arm.

    Complexity: O(members of the class).
    """
    found: list[tsast.Node | None] = []
    for item in _items(body):
        declared = _declaration_types(name, item)
        if isinstance(declared, str):
            return declared
        found += declared or []
    return found or None


def _parameter_types(name: str, holder: tsast.Node | None) -> list[tsast.Node | None] | None:
    """Return the type of the parameter named NAME of a parameter list, or None."""
    if holder is None:
        return None
    for parameter in holder.children:
        if parameter.type in PARAMETERS:
            declarator = parameter.child_by_field("declarator")
            if declarator is not None and tsast.leaf_name(declarator) == name:
                return [parameter.child_by_field("type")]
    return None


def _function_parameters(function: tsast.Node) -> tsast.Node | None:
    """Return the parameter list of a function definition, through pointer and reference declarators."""
    current = function.child_by_field("declarator")
    while current is not None and current.type != "function_declarator":
        current = current.child_by_field("declarator")
    return None if current is None else current.child_by_field("parameters")


def _template_parameter_names(parameters: tsast.Node | None) -> set[str]:
    """Return the name of each parameter of a template parameter list."""
    names: set[str] = set()
    for parameter in [] if parameters is None else parameters.children:
        if parameter.type not in TEMPLATE_PARAMETERS:
            continue
        if parameter.type == "template_template_parameter_declaration":
            heads = [inner for inner in parameter.children if inner.type == "type_parameter_declaration"]
            parameter = heads[-1] if heads else parameter
        named = parameter.child_by_field("name") or parameter.child_by_field("declarator")
        if named is None:
            leaves = [inner for inner in parameter.children if inner.type == "type_identifier"]
            named = leaves[-1] if leaves else None
        name = None if named is None else tsast.leaf_name(named)
        if name:
            names.add(name)
    return names


def _template_parameters_around(node: tsast.Node) -> set[str]:
    """Return the name of each parameter of each template declaration around a node."""
    names: set[str] = set()
    owner = node.ancestor_of_type("template_declaration")
    while owner is not None:
        names |= _template_parameter_names(owner.child_by_field("parameters"))
        owner = owner.ancestor_of_type("template_declaration")
    return names


def _head_declarations(statement: tsast.Node) -> Iterator[tsast.Node]:
    """Yield the declarations in the head of a for, if, while or switch statement."""
    initializer = statement.child_by_field("initializer")
    if initializer is not None:
        yield initializer
    condition = statement.child_by_field("condition")
    if condition is None or condition.type != "condition_clause":
        return
    for part in condition.children:
        if part.type == "init_statement":
            yield from part.children
        else:
            yield part


def _scope_types(name: str, node: tsast.Node, child: tsast.Node) -> list[tsast.Node | None] | str | None:
    """Return the declared type of NAME when the scope NODE declares it, seen from its part CHILD.

    Args:
        name: The receiver name
        node: One node on the path from the receiver to the root
        child: The child of NODE on that path

    Returns:
        The type nodes, a reason when the scope cannot show a type, or None
        when the scope does not declare the name
    """
    kind = node.type
    if kind in ("compound_statement", "translation_unit", "declaration_list", *PREPROCESSOR_ARMS):
        return _items_before(name, node, child)
    if kind in ("for_statement", "if_statement", "while_statement", "switch_statement"):
        found = None
        for declaration in _head_declarations(node):
            if declaration != child:
                found = _declaration_types(name, declaration) or found
        return found
    if kind == "for_range_loop" and child.field == "body":
        declarator = node.child_by_field("declarator")
        if declarator is not None and declarator.type == "structured_binding_declarator":
            return "a structured binding declares it" if any(
                part.text == name for part in declarator.children) else None
        if declarator is not None and tsast.leaf_name(declarator) == name:
            return [node.child_by_field("type")]
        return None
    if kind == "catch_clause" and child.field == "body":
        return _parameter_types(name, node.child_by_field("parameters"))
    if kind == "lambda_expression":
        captures = node.child_by_field("captures")
        for capture in [] if captures is None else captures.children:
            left = capture.child_by_field("left") if capture.type == "lambda_capture_initializer" else None
            if left is not None and left.text == name:
                return "an init-capture of the lambda declares it"
        declarator = node.child_by_field("declarator")
        return _parameter_types(name, None if declarator is None else declarator.child_by_field("parameters"))
    if kind == "function_definition":
        return _parameter_types(name, _function_parameters(node))
    if kind == "template_declaration":
        if name in _template_parameter_names(node.child_by_field("parameters")):
            return "a template parameter declares it"
    return None


def _out_of_class_members(name: str, function: tsast.Node, types: TypeIndex) -> list[tsast.Node | None] | str | None:
    """Return the type of the member NAME of the class of a member function that a file defines out of its class.

    The class is each class head of the index whose name is the last
    qualifier of the function name.
    """
    current = function.child_by_field("declarator")
    while current is not None and current.type != "function_declarator":
        current = current.child_by_field("declarator")
    named = None if current is None else current.child_by_field("declarator")
    parts = None if named is None else tsast.qualified_parts(named)
    if parts is None or len(parts[1]) < 2:
        return None
    found: list[tsast.Node | None] = []
    for head in types.classes(parts[1][-2]):
        body = head.child_by_field("body")
        members = None if body is None else _member_types(name, body)
        if isinstance(members, str):
            return members
        found += members or []
    return found or None


def _receiver_types(name: str, at: tsast.Node, types: TypeIndex, *, is_member: bool) -> list[tsast.Node | None] | str:
    """Find the declaration of a receiver name in the scopes around it, from the innermost scope out.

    Complexity: O(depth of the node times the items of each scope on the path).

    Args:
        name: The receiver name
        at: The receiver node
        types: The classes and the aliases of the files of the ban
        is_member: True for `this->name`, which only a data member declares

    Returns:
        The type nodes of the declaration, or the reason why the parse tree
        does not show the type
    """
    child, node = at, at.parent
    while node is not None:
        found: list[tsast.Node | None] | str | None = None
        if node.type == "field_declaration_list":
            found = _member_types(name, node)
        elif not is_member:
            found = _scope_types(name, node, child)
        if found is None and node.type == "function_definition":
            found = _out_of_class_members(name, node, types)
        if found is not None:
            return found
        child, node = node, node.parent
    return f"no declaration of `{name}` is in a scope around the call"


def _local_type_heads(name: str, at: tsast.Node) -> list[tsast.Node]:
    """Return the class heads and the aliases named NAME in the blocks and the class bodies around a node.

    The innermost scope that declares the name gives the result.  In a block,
    only a declaration before the node counts.
    """
    child, node = at, at.parent
    while node is not None:
        if node.type in ("compound_statement", "field_declaration_list"):
            heads = []
            for item in _items(node):
                if item == child and node.type == "compound_statement":
                    break
                for head in (item, item.child_by_field("type")):
                    if head is not None and head.type in (*CLASS_SPECIFIERS, "alias_declaration", "type_definition") \
                            and name in _declared_type_names(head):
                        heads.append(head)
            if heads:
                return heads
        child, node = node, node.parent
    return []


def _std_using(name: str, at: tsast.Node, types: TypeIndex) -> str | None:
    """Return the reason when a using-declaration or a using-directive of std applies to NAME at a node, or None."""
    aliases = types.namespace_aliases(at.tree)
    std_names = types.std_names(at.tree)
    for using in tsast.using_names(at.tree):
        if using.kind == "enum" or not _encloses(using.scope, at) or using.node.end > at.start:
            continue
        target = tsast.resolve_namespace(using.target, using.node, aliases, is_global=using.is_global)
        if not target or (target[0] != "std" and using.target[0] not in std_names):
            continue
        if using.is_directive or target[-1] == name:
            return (f"the name can be `std::{name}`, because `{tsast.spelled(using.node)}` "
                    f"at {types.place(using.node)} applies to it")
    return None


def _head_verdict(head: tsast.Node, name: str, types: TypeIndex, seen: frozenset[tsast.Node]) -> Verdict | None:
    """Return the verdict of one declaration of a type name: a class head, an alias or a typedef.

    Returns:
        The verdict, or None for an alias that the chain read before
    """
    if head.type in CLASS_SPECIFIERS:
        if tsast.namespace_path(head)[:1] == ("std",):
            return Verdict(Shown.STD, f"`{name}` is a class of namespace std")
        return Verdict(Shown.NOT_STD)
    if head in seen:
        return None
    if len(seen) >= MAX_ALIAS_DEPTH:
        return Verdict(Shown.UNKNOWN, f"the aliases of `{name}` form a chain longer than {MAX_ALIAS_DEPTH}")
    inner = _type_verdict(head.child_by_field("type"), types, seen | {head})
    if inner.shown is Shown.NOT_STD:
        return inner
    return Verdict(inner.shown, f"{inner.why}, through the alias `{name}` at {types.place(head)}")


def _named_verdict(at: tsast.Node, is_global: bool, parts: tuple[str, ...], types: TypeIndex,
                   seen: frozenset[tsast.Node]) -> Verdict:
    """Return the verdict of a type that a name gives.

    Complexity: O(declarations of the name in the index), and one walk to
    the root of the tree for each alias in the chain.

    Args:
        at: The type node
        is_global: True when the name starts at `::`
        parts: The parts of the name
        types: The classes and the aliases of the files of the ban
        seen: The aliases that the chain read before

    Returns:
        The verdict
    """
    resolved = tsast.resolve_namespace(parts, at, types.namespace_aliases(at.tree), is_global=is_global)
    if len(resolved) > 1 and (resolved[0] == "std" or (not is_global and parts[0] in types.std_names(at.tree))):
        return Verdict(Shown.STD, f"`{tsast.spelled(at)}` is a type of namespace std")
    if not is_global and resolved[0] in _template_parameters_around(at):
        return Verdict(Shown.UNKNOWN, f"`{resolved[0]}` is a template parameter")
    name = resolved[-1]
    if len(resolved) == 1 and not is_global:
        local = _local_type_heads(name, at)
        if local:
            return _worst((_head_verdict(head, name, types, seen) for head in local),
                          f"the aliases of `{name}` form a cycle")
        using = _std_using(name, at, types)
        if using is not None:
            return Verdict(Shown.STD, using)
    heads = types.aliases(name) + types.classes(name)
    if not heads:
        return Verdict(Shown.UNKNOWN, f"no file that the ban reads declares the type `{name}`")
    return _worst((_head_verdict(head, name, types, seen) for head in heads), f"the aliases of `{name}` form a cycle")


def _type_verdict(type_node: tsast.Node | None, types: TypeIndex, seen: frozenset[tsast.Node]) -> Verdict:
    """Return the verdict of one declared type.

    Args:
        type_node: The `type` field of a declaration, a parameter, a member or an alias
        types: The classes and the aliases of the files of the ban
        seen: The aliases that the chain read before

    Returns:
        The verdict
    """
    if type_node is None:
        return Verdict(Shown.UNKNOWN, "the declaration has no type that the parser reads")
    kind = type_node.type
    if kind == "type_descriptor":
        return _type_verdict(type_node.child_by_field("type"), types, seen)
    if kind in ("primitive_type", "sized_type_specifier"):
        return Verdict(Shown.NOT_STD)
    if kind in ("placeholder_type_specifier", "decltype", "auto"):
        return Verdict(Shown.UNKNOWN, f"the declaration spells `{tsast.spelled(type_node)}` and not a type")
    if kind in CLASS_SPECIFIERS:
        named = type_node.child_by_field("name")
        parts = None if named is None else tsast.qualified_parts(named)
        is_std = parts is not None and len(parts[1]) > 1 and parts[1][0] in types.std_names(type_node.tree)
        return Verdict(Shown.STD, f"`{tsast.spelled(type_node)}` is a class of namespace std") if is_std \
            else Verdict(Shown.NOT_STD)
    parts = tsast.qualified_parts(type_node)
    if parts is None or not parts[1]:
        return Verdict(Shown.UNKNOWN, f"the type `{tsast.spelled(type_node)}` is not a name")
    return _named_verdict(type_node, parts[0], parts[1], types, seen)


def _receiver_detail(receiver: tsast.Node | None, types: TypeIndex) -> str | None:
    """Return the reason to reject a reserve call on a receiver, or None when the receiver has a type outside std.

    RECEIVER TYPES in the module docstring gives the rules.

    Args:
        receiver: The `argument` field of the member access of the call
        types: The classes and the aliases of the files of the ban

    Returns:
        The reason why the ban rejects the call, or None
    """
    target = receiver
    while target is not None and target.type == "parenthesized_expression":
        inner = tsast.non_comment_children(target)
        target = inner[0] if len(inner) == 1 else None
    if target is None:
        return "the parse tree does not show the type of the receiver: it is not a name"
    field = target.child_by_field("field") if target.type == "field_expression" else None
    this = target.child_by_field("argument") if field is not None else None
    if target.type == "identifier":
        name, is_member = target.text, False
    elif field is not None and field.type == "field_identifier" and this is not None and this.type == "this":
        name, is_member = field.text, True
    else:
        return (f"the parse tree does not show the type of the receiver `{tsast.spelled(target)}`: "
                "it is not a name or `this->name`")
    declared = _receiver_types(name, target, types, is_member=is_member)
    if isinstance(declared, str):
        return f"the parse tree does not show the type of the receiver `{name}`: {declared}"
    verdict = _worst((_type_verdict(type_node, types, frozenset()) for type_node in declared),
                     "no declaration gives a type")
    if verdict.shown is Shown.STD:
        return f"the receiver `{name}` has a std type: {verdict.why}"
    if verdict.shown is Shown.UNKNOWN:
        return f"the parse tree does not show the type of the receiver `{name}`: {verdict.why}"
    return None


def _reserve_nodes(tree: tsast.Tree, scope: Scope) -> Iterator[tuple[tsast.Node, str]]:
    """Yield the name token of each member call named reserve whose receiver can have a std type.

    Args:
        tree: One parsed file or macro body
        scope: The type index of the files of the ban

    Yields:
        The `reserve` name node of each such call, with the reason why the ban rejects it
    """
    for call in tree.find("call_expression"):
        callee = call.child_by_field("function")
        if callee is None or callee.type != "field_expression":
            continue
        field = callee.child_by_field("field")
        name = _final_name(field) if field is not None else None
        if name is None or name.text != "reserve":
            continue
        detail = _receiver_detail(callee.child_by_field("argument"), scope.types)
        if detail is not None:
            yield name, detail


def _reserve_token(tokens: list[tsast.Token], index: int, scope: Scope) -> bool:
    """Return True for `reserve` after a member access and before `(` or `<` in an unparsed macro body."""
    following = tokens[index + 1].text if index + 1 < len(tokens) else ""
    return tokens[index].text == "reserve" and following in ("(", "<") and _after_member_access(tokens, index)


def _file_stream_nodes(tree: tsast.Tree, scope: Scope) -> Iterator[tuple[tsast.Node, str]]:
    """Yield each name of a file stream and each include of <fstream>.

    The name counts with or without a `std::` qualifier, so a
    using-directive or a namespace alias does not hide it.

    Args:
        tree: One parsed file or macro body
        scope: Unused, every spelling counts

    Yields:
        The name node or the include node of each use, with no detail
    """
    for node in tree.find("identifier", "type_identifier"):
        if node.text in FILE_STREAMS:
            yield node, ""
    for node in tree.find("preproc_include"):
        target = node.child_by_field("path")
        if target is not None and target.text == "<fstream>":
            yield node, ""


def _file_stream_token(tokens: list[tsast.Token], index: int, scope: Scope) -> bool:
    """Return True for the name of a file stream in the tokens of an unparsed macro body, unless it is a member."""
    return tokens[index].text in FILE_STREAMS and not _after_member_access(tokens, index)


def _std_names(tree: tsast.Tree) -> frozenset[str]:
    """Return std and every namespace alias of the file that denotes std, through chains of aliases."""
    return frozenset({"std"}) | tsast.alias_closure(tsast.namespace_aliases(tree), [("std",)])


def _outer_qualified(node: tsast.Node) -> tsast.Node:
    """Return the outermost qualified_identifier whose final name is NODE, or NODE itself."""
    outer = node
    while outer.field == "name" and outer.parent is not None and outer.parent.type == "qualified_identifier":
        outer = outer.parent
    return outer


def _spawn_nodes(tree: tsast.Tree, scope: Scope) -> Iterator[tuple[tsast.Node, str]]:
    """Yield each reference to a process-spawn function or syscall number of the C library.

    A reference is a name in an expression, bare, qualified from `::`, or
    qualified by std or an alias of std.  A call, an address, a function
    pointer and a using-declaration all hold one.  A name in a template
    argument counts as well, because `call<fork>()` parses it as a type.
    A member name does not count, and a name qualified by any other
    namespace or by a class names a project entity.  The name of a
    declaration counts only in a macro body.

    Args:
        tree: One parsed file or macro body
        scope: The std aliases of the file, and whether TREE is a fragment

    Yields:
        The name node of each reference, with no detail
    """
    for node in tree.find("identifier", "type_identifier"):
        if node.text not in SPAWN_NAMES:
            continue
        outer = _outer_qualified(node)
        if node.type == "type_identifier" and (outer.parent is None or outer.parent.type != "type_descriptor"):
            continue
        if node.parent is not None and node.parent.type == "enumerator":
            continue
        if node.field == "declarator" and not scope.is_fragment:
            continue
        parts = tsast.qualified_parts(outer)
        if parts is None:
            continue
        qualifiers = parts[1][:-1]
        if not qualifiers or (len(qualifiers) == 1 and qualifiers[0] in scope.std_names):
            yield node, ""


def _spawn_token(tokens: list[tsast.Token], index: int, scope: Scope) -> bool:
    """Return True for a spawn name in an unparsed macro body, unless a member access or a foreign qualifier precedes it."""
    if tokens[index].text not in SPAWN_NAMES or _after_member_access(tokens, index):
        return False
    if index >= 1 and tokens[index - 1].text == "::":
        qualifier = tokens[index - 2] if index >= 2 else None
        return qualifier is None or qualifier.kind != "identifier" or qualifier.text in scope.std_names
    return True


def _band3_roots(root: Path) -> tuple[str, ...]:
    """Return each fixy-only include/crucible directory that utils/scripts/fixy-only-paths.txt lists, and its src/ twin.

    The file holds one repository-relative directory per line.  A `#` starts
    a comment, and a blank line is skipped.

    Args:
        root: The scan root

    Returns:
        The repo-relative directories, or () when the list names none
    """
    listing = root / FIXY_ONLY_PATHS
    lines = listing.read_text(encoding="utf-8").splitlines() if listing.is_file() else []
    listed = [line.split("#", 1)[0].strip() for line in lines]
    subsystems = [Path(rel).name for rel in listed if rel and Path(rel).parent == Path("include/crucible")]
    return tuple(f"{top}/{name}" for name in subsystems for top in ("include/crucible", "src"))


BANS = (
    Ban(
        name="reinterpret_cast",
        roots=lambda root: ("include", "src", "vessel"),
        extra_excluded=frozenset({"bench"}),
        marker="NO-REINTERPRET-OK",
        allowlist="utils/scripts/no-reinterpret-allowlist.txt",
        rule="reinterpret_cast is banned (CLAUDE.md §III). Use std::bit_cast or std::start_lifetime_as",
        node_hits=_reinterpret_nodes,
        token_hit=_reinterpret_token,
    ),
    Ban(
        name="reserve",
        roots=lambda root: ("include", "src", "bench"),
        extra_excluded=frozenset(),
        marker="NO-RESERVE-OK",
        allowlist="utils/scripts/no-reserve-allowlist.txt",
        rule="a reserve call on a std container is banned (CLAUDE.md §IV). Use std::inplace_vector, "
             "a sized constructor, or arena storage",
        node_hits=_reserve_nodes,
        token_hit=_reserve_token,
        token_detail="the macro body does not parse, so the parse tree does not show the type of the receiver",
    ),
    Ban(
        name="file stream",
        roots=_band3_roots,
        extra_excluded=frozenset(),
        marker="NO-FILE-STREAM-OK",
        allowlist="utils/scripts/no-file-stream-allowlist.txt",
        rule="a C++ file stream is banned in a fixy-only band-3 directory. Use fixy::fs::mint_file or "
             "fixy::mint_owned_file",
        node_hits=_file_stream_nodes,
        token_hit=_file_stream_token,
    ),
    Ban(
        name="process spawn",
        roots=lambda root: ("include", "src", "vessel"),
        extra_excluded=frozenset(),
        marker="SPAWN-PROCESS-OK",
        allowlist="utils/scripts/no-spawn-process-allowlist.txt",
        rule="a raw process spawn is banned in production code (CLAUDE.md §IX). The child carries no "
             "Permission and no effect row. Use std::jthread with permission_fork",
        node_hits=_spawn_nodes,
        token_hit=_spawn_token,
        excluded_prefixes=("include/crucible/perf/bpf/",),
    ),
)


def macro_body_rows(body: tsast.MacroBody, ban: Ban, scope: Scope) -> Iterator[tuple[int, str]]:
    """Yield the zero-based row in its file of each use of the ban in one macro body, with its detail.

    A body that parses gives its rows through the node rule.  A body that
    does not parse gives its rows through the token rule over its
    preprocessing tokens.
    """
    if body.is_parsed:
        for node, detail in ban.node_hits(body.tree, scope):
            yield body.origin(node)[0], detail
        return
    tokens = tsast.pp_tokens(body.text, body.first_row)
    for index, token in enumerate(tokens):
        if token.kind == "identifier" and ban.token_hit(tokens, index, scope):
            yield token.row, ban.token_detail


def in_scope(rel: Path, ban: Ban, roots: tuple[str, ...]) -> bool:
    """Return True when a repo-relative path is inside the ban's scope.

    Args:
        rel: The path relative to the scan root
        ban: The ban
        roots: The root directories of the ban, relative to the scan root

    Returns:
        Whether the ban reads the file
    """
    if not tsast.is_in_cpp_scope(rel) or not any(rel.is_relative_to(top) for top in roots) \
            or rel.as_posix().startswith(ban.excluded_prefixes):
        return False
    excluded = EXCLUDED_COMPONENTS | ban.extra_excluded
    return not any(part in excluded or part.startswith("build") for part in rel.parts[:-1])


def scope_files(root: Path, ban: Ban, roots: tuple[str, ...]) -> list[Path]:
    """Return every file the ban reads under a scan root, sorted and without duplicates.

    Args:
        root: The scan root
        ban: The ban
        roots: The root directories of the ban, relative to the scan root

    Returns:
        Absolute paths, in sorted order
    """
    found: set[Path] = set()
    for top in roots:
        base = root / top
        if base.is_dir():
            found.update(p for p in base.rglob("*") if p.is_file() and in_scope(p.relative_to(root), ban, roots))
    return sorted(found)


def _marked(tree: tsast.Tree, row: int, marker: str) -> bool:
    """Return True when a comment node that starts on the row carries the marker and a reason.

    Args:
        tree: The parsed file
        row: The zero-based row of the banned token
        marker: The marker word

    Returns:
        Whether the row is exempt
    """
    pattern = re.compile(re.escape(marker) + r":\s*\S")
    return any(comment.start[0] == row and pattern.search(tsast.prose_text(comment))
               for comment in tsast.comments_by_row(tree).get(row, ()))


def scan(root: Path, ban: Ban) -> tuple[list[Hit], list[str]]:
    """Find every use of one banned construct under a scan root.

    Complexity: O(total size of the files in scope).

    Args:
        root: The scan root
        ban: The ban

    Returns:
        The unexempted hits before the allowlist, and the parse failures

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    roots = ban.roots(root)
    if not roots:
        return [], [f"the {ban.name} ban has no root directory, so it reads nothing. For the file-stream ban, "
                    f"{FIXY_ONLY_PATHS} must list the band-3 directories under include/crucible"]
    files = scope_files(root, ban, roots)
    hits: list[Hit] = []
    failures: list[str] = []
    parsed: dict[str, tsast.Tree] = {}
    for tree in tsast.parse(files, strict=False):
        rel = str(Path(tree.path).relative_to(root))
        if tree.diagnostic is not None:
            failures.append(f"{rel}: the parser cannot read this file, so the ban cannot see it. "
                            f"{tree.diagnostic.strip()}")
            continue
        parsed[rel] = tree
    types = TypeIndex(list(parsed.values()), root)
    rows_by_tree: dict[str, tuple[tsast.Tree, defaultdict[int, list[str]], Scope]] = {}
    for rel, tree in parsed.items():
        scope = Scope(_std_names(tree), is_fragment=False, types=types)
        rows: defaultdict[int, list[str]] = defaultdict(list)
        for node, detail in ban.node_hits(tree, scope):
            rows[node.start[0]].append(detail)
        rows_by_tree[rel] = (tree, rows, scope)
    for body in tsast.macro_bodies(list(parsed.values())):
        rel = str(Path(body.define.tree.path).relative_to(root))
        tree, rows, scope = rows_by_tree[rel]
        for row, detail in macro_body_rows(body, ban, Scope(scope.std_names, is_fragment=True, types=types)):
            rows[row].append(detail)
    for rel, (tree, rows, _) in rows_by_tree.items():
        for row in sorted(rows):
            if not _marked(tree, row, ban.marker):
                details = tuple(dict.fromkeys(detail for detail in rows[row] if detail))
                hits.append(Hit(rel, row + 1, tree.line(row).strip(), details))
    return hits, failures


def load_allowlist(path: Path) -> list[str]:
    """Return the entries of an allowlist, without comments or blank lines.

    Args:
        path: The allowlist file

    Returns:
        Each `path:text` entry, trimmed
    """
    if not path.is_file():
        return []
    entries = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            entries.append(stripped)
    return entries


def check(root: Path) -> int:
    """Run every ban under a scan root and report.

    Args:
        root: The scan root

    Returns:
        0 clean, 1 on a violation or a parse failure, 2 on a stale entry
    """
    violations = stale = 0
    for ban in BANS:
        hits, failures = scan(root, ban)
        entries = load_allowlist(root / ban.allowlist)
        admitted = set(entries)
        live = {f"{hit.path}:{hit.key}" for hit in hits}
        tag = ban.marker.removesuffix("-OK")
        for failure in failures:
            print(f"{tag} parse failure: {failure}", file=sys.stderr)
            violations += 1
        for hit in hits:
            key = f"{hit.path}:{hit.key}"
            if key in admitted:
                continue
            reason = "".join(f"  The site: {detail}." for detail in hit.details)
            print(f"{tag} violation: {hit.path}:{hit.row} — {ban.rule}.{reason}  Allowlist key: {key}",
                  file=sys.stderr)
            violations += 1
        for entry in entries:
            if entry not in live:
                print(f"{tag} stale: {entry} — no {ban.name} site has this line text. "
                      f"Remove the entry from {ban.allowlist}.", file=sys.stderr)
                stale += 1
    if violations:
        print(f"check-banned-calls: {violations} violation(s). Rewrite the site, mark the line with "
              "`// <MARKER>: <reason>`, or add the printed allowlist key with the migration it waits for.",
              file=sys.stderr)
        return 1
    if stale:
        print(f"check-banned-calls: {stale} stale allowlist entr(y/ies).", file=sys.stderr)
        return 2
    print("check-banned-calls: clean — no new banned call, no stale allowlist entry.", file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each shape the bans must see and must not see, then check the verdicts.

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

    def keys(root: Path, ban: Ban) -> set[str]:
        """Return the content keys of the unexempted hits of one ban."""
        hits, _ = scan(root, ban)
        return {hit.key for hit in hits}

    cast_fixture = (
        "#pragma once\n"
        "#define CAST_IN_MACRO(p) reinterpret_cast<long*>(p)\n"
        "#define QUOTE_THEN_CAST(p) (u8'\"', reinterpret_cast<char*>(p))\n"
        "#define SEPARATOR_THEN_CAST(p) (1'000, reinterpret_cast<short*>(p))\n"
        "inline int* plain(void* p) { return reinterpret_cast<int*>(p); }\n"
        "inline char* spanning(void* p) { return reinterpret_cast\n"
        "    <char*>(p); }\n"
        "inline short* spaced(void* p) { return reinterpret_cast <short*>(p); }\n"
        "inline float* admitted(void* p) { return reinterpret_cast<float*>(p); }\n"
        "inline double* marked(void* p) { return reinterpret_cast<double*>(p); }  // NO-REINTERPRET-OK: fixture\n"
        "inline unsigned* bare_marker(void* p) { return reinterpret_cast<unsigned*>(p); }  // NO-REINTERPRET-OK:\n"
        "// return reinterpret_cast<int*>(in_line_comment);\n"
        "/* a block comment\n"
        "   return reinterpret_cast<int*>(in_block_comment);\n"
        "*/\n"
        'inline const char* text = "reinterpret_cast<int*>(in_string)";\n'
        'inline const char* raw = R"x(\n'
        "reinterpret_cast<int*>(in_raw_string)\n"
        ')x";\n'
    )
    reserve_fixture = (
        "#pragma once\n"
        "#define RESERVE_IN_MACRO(v) (v).reserve(1)\n"
        "inline void plain(V& v) { v.reserve(7); }\n"
        "inline void arrow(V* p) { p->reserve(8); }\n"
        "inline void spanning(V& v) { v\n"
        "    .reserve(9); }\n"
        "inline void spaced(V& v) { v . reserve (10); }\n"
        "inline void qualified(V& v) { v.Base::reserve(12); }\n"
        "inline void admitted(V& v) { v.reserve(11); }\n"
        "inline void marked(V& v) { v.reserve(15); }  // NO-RESERVE-OK: fixture\n"
        "// v.reserve(18);\n"
        'inline const char* text = "v.reserve(19);";\n'
        "inline void reserved_name(V& v) { v.reserved_slots(20); }\n"
        "#define QUALIFIED_RESERVE(v) (v).Base::reserve(24)\n"
        "#define TEMPLATE_RESERVE(v) (v).template reserve<int>(25)\n"
        "#define PASTED_RESERVE(v) v ## _tail; (v).reserve(26)\n"
        "#define RESERVE_FIELD(v) (v).reserve_count\n"
    )
    # A class with a member named reserve, in a file other than its callers.
    # The twin name has a class here and a std alias in a second namespace.
    pools_header = (
        "#pragma once\n"
        "namespace planted {\n"
        "struct Pool { void reserve(int count); };\n"
        "using PoolAlias = Pool;\n"
        "struct Twin { void reserve(int count); };\n"
        "}  // namespace planted\n"
        "namespace elsewhere {\n"
        "using Twin = std::vector<int>;\n"
        "}  // namespace elsewhere\n"
    )
    receiver_fixture = (
        "#include <crucible/planted/Pools.h>\n"
        "using Vec = std::vector<int>;\n"
        "using VecChain = Vec;\n"
        "typedef std::vector<int> VecTypedef;\n"
        "namespace s = std;\n"
        "inline void pool_local() { planted::Pool pool; pool.reserve(30); }\n"
        "inline void pool_parameter(planted::Pool& pool) { pool.reserve(31); }\n"
        "inline void pool_alias(planted::PoolAlias& pool) { pool.reserve(32); }\n"
        "struct PoolHolder {\n"
        "    planted::Pool member_pool;\n"
        "    void in_class() { member_pool.reserve(33); }\n"
        "    void through_this() { this->member_pool.reserve(34); }\n"
        "    void out_of_class();\n"
        "};\n"
        "void PoolHolder::out_of_class() { member_pool.reserve(35); }\n"
        "inline void pool_in_lambda() { planted::Pool pool; auto body = [&pool] { pool.reserve(36); }; body(); }\n"
        "inline void pool_in_head() { if (planted::Pool pool; true) { pool.reserve(37); } }\n"
        "inline void vector_alias(Vec& v) { v.reserve(40); }\n"
        "inline void vector_chain(VecChain& v) { v.reserve(41); }\n"
        "inline void vector_typedef(VecTypedef& v) { v.reserve(42); }\n"
        "inline void vector_local_alias() { using Local = std::vector<int>; Local v; v.reserve(43); }\n"
        "inline void vector_namespace_alias(s::vector<int>& v) { v.reserve(44); }\n"
        "inline void plain_string(std::string& text) { text.reserve(45); }\n"
        "template <class C> void templated(C& c) { c.reserve(46); }\n"
        "inline void deduced() { auto v = make(); v.reserve(47); }\n"
        "inline void undeclared_type(Mystery& m) { m.reserve(48); }\n"
        "struct VecHolder { Vec held; void member() { held.reserve(49); } };\n"
        "inline void ambiguous(planted::Twin& twin) { twin.reserve(50); }\n"
        "inline void bound() { auto [first, second] = pair(); first.reserve(51); }\n"
        "inline void captured() { auto body = [held = Vec{}]() mutable { held.reserve(52); }; body(); }\n"
        "inline void chained(PoolHolder& holder) { holder.member_pool.reserve(53); }\n"
        "using std::vector;\n"
        "inline void using_declared(vector<int>& v) { v.reserve(54); }\n"
        "inline void through_pointer(Vec* v) { v->reserve(55); }\n"
    )
    stream_fixture = (
        "#pragma once\n"
        "#include <fstream>\n"
        "#include <iostream>\n"
        "#define STREAM_IN_MACRO(p) std::ofstream macro_stream{p}\n"
        "inline void plain() { std::ofstream plain_out{\"x\"}; }\n"
        "inline void spaced() { std :: ifstream spaced_in{\"x\"}; }\n"
        "using namespace std;\n"
        "inline void bare() { fstream bare_io{\"x\"}; }\n"
        "namespace s = std;\n"
        "inline void aliased() { s::basic_ofstream<char> aliased_out{\"x\"}; }\n"
        "inline void wide() { std::wofstream wide_out{L\"x\"}; }\n"
        "inline void buffer() { std::filebuf buffer_io; }\n"
        "inline void marked() { std::ofstream marked_out{\"x\"}; }  // NO-FILE-STREAM-OK: fixture\n"
        "// std::ofstream in_comment{\"x\"};\n"
        "inline const char* text = \"std::ofstream in_string\";\n"
        "inline void other_name() { std::ofstreams_are_not_this plural; }\n"
    )
    spawn_fixture = (
        "#define SPAWN_IN_MACRO() fork(macro_call)\n"
        "#define SPAWN_NAME_IN_MACRO macro_name fork\n"
        "#define STD_SYSTEM_IN_MACRO(c) std::system(c)\n"
        "#define MEMBER_IN_MACRO(p) (p).clone(macro_member)\n"
        "#define PROJECT_IN_MACRO() crucible::fork(macro_project)\n"
        "namespace s = std;\n"
        "namespace s2 = s;\n"
        "inline int bare() { return fork(); }\n"
        "inline int global() { return ::vfork(); }\n"
        "inline int spanning() { return ::posix_spawn\n"
        "    (nullptr, nullptr, nullptr, nullptr, nullptr, nullptr); }\n"
        "inline int std_system() { return std::system(\"std_system\"); }\n"
        "inline int aliased() { return s2::system(\"aliased\"); }\n"
        "inline int global_std() { return ::std::system(\"global_std\"); }\n"
        "inline auto address() { return &::execve; }\n"
        "inline auto pointer() { auto p = execvp; return p; }\n"
        "using ::popen;\n"
        "inline long raw_syscall() { return syscall(SYS_clone3, 0); }\n"
        "inline int template_arg() { return call<fork>(); }\n"
        "inline int reap() { return ::waitpid(-1, nullptr, 0); }\n"
        "inline int marked() { return fork(); }  // SPAWN-PROCESS-OK: fixture\n"
        "inline int admitted() { return daemon(0, 0); }\n"
        "inline int member(T& t) { return t.clone(); }\n"
        "inline int arrow(T* t) { return t->fork(); }\n"
        "inline int project() { return crucible::detail::fork(); }\n"
        "inline int enum_value() { return int(Syscall::execve); }\n"
        "enum class Syscall { clone = 1, execve = 2 };\n"
        "inline int permission() { return permission_fork(0); }\n"
        "inline int declared(int popen) { return 0; }\n"
        "// return fork(in_comment);\n"
        "inline const char* text = \"system(in_string)\";\n"
        "#define ALIASED_IN_MACRO(c) s2::system(c)\n"
        "namespace s3 = /* a comment inside the alias */ std;\n"
        "inline int commented_alias() { return s3::system(\"commented_alias\"); }\n"
        "#define PASTED_THEN_SPAWN(x) x ## _tail; fork(pasted_spawn)\n"
        "#define PASTED_MEMBER(x) x ## _tail; (x).fork(pasted_member)\n"
        "#define PASTED_PROJECT(x) x ## _tail; crucible::fork(pasted_project)\n"
    )
    cast_ban, reserve_ban, stream_ban, spawn_ban = BANS
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in (
            (FIXY_ONLY_PATHS, "examples/fn\n"
                              "# include/crucible/commented\n"
                              "include/crucible/cntp  # a trailing comment\n"),
            ("include/crucible/cntp/Streams.h", stream_fixture),
            ("src/cntp/streams.cpp", "void g() { std::ifstream src_twin{\"x\"}; }\n"),
            ("include/crucible/cntp/test/planted.h", "void g() { std::ifstream band_test_dir{\"x\"}; }\n"),
            ("include/crucible/commented/Streams.h", "void g() { std::ifstream commented_dir{\"x\"}; }\n"),
            ("include/crucible/planted/Streams.h", "void g() { std::ifstream not_band3{\"x\"}; }\n"),
            ("include/crucible/planted/Casts.h", cast_fixture),
            ("include/crucible/planted/Reserve.h", reserve_fixture),
            ("vessel/planted.cpp", "int* f(void* p) { return reinterpret_cast<int*>(p); }\n"),
            ("bench/planted.cpp", "void g(V& v) { v.reserve(23); }\n"
                                  "int* f(void* p) { return reinterpret_cast<int*>(bench_cast); }\n"),
            ("include/crucible/test/planted.h", "int* f(void* p) { return reinterpret_cast<int*>(test_dir); }\n"),
            ("utils/scripts/no-reinterpret-allowlist.txt",
             "include/crucible/planted/Casts.h:inline float* admitted(void* p) "
             "{ return reinterpret_cast<float*>(p); }\n"),
            ("utils/scripts/no-reserve-allowlist.txt",
             "include/crucible/planted/Reserve.h:inline void admitted(V& v) { v.reserve(11); }\n"),
            ("src/planted/Spawn.cpp", spawn_fixture),
            ("vessel/spawn.cpp", "int vessel_spawn() { return fork(); }\n"),
            ("include/crucible/perf/bpf/vmlinux.h", "int kernel_dump() { return fork(); }\n"),
            ("include/crucible/test/spawn.h", "int test_dir_spawn() { return fork(); }\n"),
            ("utils/scripts/no-spawn-process-allowlist.txt",
             "src/planted/Spawn.cpp:inline int admitted() { return daemon(0, 0); }\n"),
            ("include/crucible/planted/Pools.h", pools_header),
            ("bench/receivers.cpp", receiver_fixture),
            ("src/planted/Calls.cpp", "int* cast_in_src(void* p) { return reinterpret_cast<int*>(src_cast); }\n"
                                      "void reserve_in_src(std::vector<int>& v) { v.reserve(60); }\n"),
        ):
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")

        casts = keys(root, cast_ban)
        expect("a plain cast is caught", any("<int*>(p)" in key and "plain" in key for key in casts))
        expect("a cast that spans two lines is caught", any("spanning" in key for key in casts))
        expect("a cast with a space before < is caught", any("spaced" in key for key in casts))
        expect("a cast in a macro body is caught", any("CAST_IN_MACRO" in key for key in casts))
        expect("a cast after a prefixed character literal that holds a quote is caught",
               any("QUOTE_THEN_CAST" in key for key in casts))
        expect("a cast after a number with a digit separator is caught",
               any("SEPARATOR_THEN_CAST" in key for key in casts))
        expect("a cast in vessel/ is caught", any("reinterpret_cast<int*>(p)" in key and "f(" in key
                                                     for key in casts))
        expect("a marker with a reason exempts its line", not any("double" in key for key in casts), True)
        expect("a marker with no reason exempts nothing", any("bare_marker" in key for key in casts))
        expect("a cast in a line comment is not caught", not any("in_line_comment" in key for key in casts), True)
        expect("a cast in a block comment is not caught",
               not any("in_block_comment" in key for key in casts), True)
        expect("a cast in a string literal is not caught", not any("in_string" in key for key in casts), True)
        expect("a cast in a raw string is not caught", not any("in_raw_string" in key for key in casts), True)
        expect("a cast in bench/ is out of scope", not any("bench_cast" in key for key in casts), True)
        expect("a cast under a test/ directory is out of scope", not any("test_dir" in key for key in casts), True)
        expect("a cast in src/ is caught", any("src_cast" in key for key in casts))

        reserves = keys(root, reserve_ban)
        for label, needle in (("a plain call", "reserve(7)"), ("a call through ->", "reserve(8)"),
                              ("a call that spans two lines", "reserve(9)"),
                              ("a call with spaces around . and (", "reserve (10)"),
                              ("a qualified member call", "reserve(12)"), ("a call in a macro body", "reserve(1)"),
                              ("a call in bench/", "reserve(23)"),
                              ("a qualified member call in a macro body", "reserve(24)"),
                              ("a template member call in a macro body", "reserve<int>(25)"),
                              ("a call in a macro body that does not parse", "reserve(26)")):
            expect(f"reserve: {label} is caught", any(needle in key for key in reserves))
        for label, needle in (("a marked call", "reserve(15)"), ("a call in a comment", "reserve(18)"),
                              ("a call in a string literal", "reserve(19)"),
                              ("a longer member name", "reserved_slots"),
                              ("a longer field name in a macro body", "reserve_count")):
            expect(f"reserve: {label} is not caught", not any(needle in key for key in reserves), True)
        for label, needle in (("a local of a class of another file", "reserve(30)"),
                              ("a parameter of a class of another file", "reserve(31)"),
                              ("a parameter through an alias of a class", "reserve(32)"),
                              ("a member in its class", "reserve(33)"), ("a member through this->", "reserve(34)"),
                              ("a member in a function defined out of its class", "reserve(35)"),
                              ("a local that a lambda captures by reference", "reserve(36)"),
                              ("a local of an if head", "reserve(37)")):
            expect(f"reserve: a call on {label} is not caught", not any(needle in key for key in reserves), True)
        for label, needle in (("a std::vector through an alias", "reserve(40)"),
                              ("a std::vector through a chain of aliases", "reserve(41)"),
                              ("a std::vector through a typedef", "reserve(42)"),
                              ("a std::vector through a local alias", "reserve(43)"),
                              ("a std::vector through a namespace alias of std", "reserve(44)"),
                              ("a std::string", "reserve(45)"), ("a template parameter", "reserve(46)"),
                              ("a local declared with auto", "reserve(47)"),
                              ("a type that no file declares", "reserve(48)"),
                              ("a member through an alias of std::vector", "reserve(49)"),
                              ("a name with a class and a std alias", "reserve(50)"),
                              ("a structured binding", "reserve(51)"), ("an init-capture", "reserve(52)"),
                              ("a member of another object", "reserve(53)"),
                              ("a name that a using-declaration of std applies to", "reserve(54)"),
                              ("a pointer to a std::vector through ->", "reserve(55)"),
                              ("a std::vector in src/", "reserve(60)")):
            expect(f"reserve: a call on {label} is caught", any(needle in key for key in reserves))

        streams = keys(root, stream_ban)
        for label, needle in (("an include of <fstream>", "#include <fstream>"),
                              ("a use in a macro body", "STREAM_IN_MACRO"), ("a plain use", "plain_out"),
                              ("a use with spaces around ::", "spaced_in"),
                              ("a use through a using-directive", "bare_io"),
                              ("a basic_ use through a namespace alias", "aliased_out"),
                              ("a wide stream", "wide_out"), ("a file buffer", "buffer_io"),
                              ("a use in the src/ twin of a band-3 directory", "src_twin")):
            expect(f"file stream: {label} is caught", any(needle in key for key in streams))
        for label, needle in (("another standard header", "<iostream>"), ("a marked use", "marked_out"),
                              ("a use in a comment", "in_comment"), ("a use in a string literal", "in_string"),
                              ("a longer name", "plural"), ("a use under a test/ directory", "band_test_dir"),
                              ("a directory that only a comment line lists", "commented_dir"),
                              ("a directory that is not band-3", "not_band3")):
            expect(f"file stream: {label} is not caught", not any(needle in key for key in streams), True)
        spawns = keys(root, spawn_ban)
        for label, needle in (("a call in a macro body", "SPAWN_IN_MACRO"),
                              ("a name with no call in a macro body", "SPAWN_NAME_IN_MACRO"),
                              ("a std-qualified call in a macro body", "STD_SYSTEM_IN_MACRO"),
                              ("a bare call", "bare()"), ("a call qualified from ::", "global()"),
                              ("a call that spans two lines", "spanning()"),
                              ("a std-qualified call", "std_system()"),
                              ("a call through a chain of aliases of std", "aliased()"),
                              ("a call qualified by ::std", "global_std()"), ("an address", "address()"),
                              ("a function pointer", "pointer()"), ("a using-declaration", "using ::popen"),
                              ("a syscall number", "raw_syscall()"), ("a template argument", "template_arg()"),
                              ("a reaping call", "reap()"), ("a call in vessel/", "vessel_spawn"),
                              ("a call through an alias of std in a macro body", "ALIASED_IN_MACRO"),
                              ("a call through an alias whose target holds a comment", "commented_alias()"),
                              ("a call in a macro body that does not parse", "PASTED_THEN_SPAWN")):
            expect(f"process spawn: {label} is caught", any(needle in key for key in spawns))
        for label, needle in (("a marked call", "marked()"), ("a member call", "member("),
                              ("a call through ->", "arrow("), ("a call qualified by a project namespace", "project()"),
                              ("an enumerator qualified by its enum", "enum_value()"),
                              ("the enumerators of an enum", "enum class"), ("a longer name", "permission()"),
                              ("the name of a parameter", "declared("), ("a member call in a macro body",
                                                                         "MEMBER_IN_MACRO"),
                              ("a project call in a macro body", "PROJECT_IN_MACRO"),
                              ("a call in a comment", "in_comment"), ("a call in a string literal", "in_string"),
                              ("the kernel dump under perf/bpf", "kernel_dump"),
                              ("a call under a test/ directory", "test_dir_spawn"),
                              ("a member call in a macro body that does not parse", "PASTED_MEMBER"),
                              ("a project call in a macro body that does not parse", "PASTED_PROJECT")):
            expect(f"process spawn: {label} is not caught", not any(needle in key for key in spawns), True)
        report = io.StringIO()
        with contextlib.redirect_stderr(report):
            check(root)
        expect("process spawn: the report names an unadmitted site by its content key",
               "Allowlist key: src/planted/Spawn.cpp:inline int bare()" in report.getvalue())
        expect("process spawn: an allowlist row keyed by content admits its site",
               "Allowlist key: src/planted/Spawn.cpp:inline int admitted()" not in report.getvalue(), True)
        expect("reserve: the report names the std type behind an alias",
               "the receiver `v` has a std type: `std::vector<int>` is a type of namespace std, through the alias "
               "`Vec` at bench/receivers.cpp:2." in report.getvalue())
        expect("reserve: the report says when the parse tree does not show the type",
               "the parse tree does not show the type of the receiver `v`: the declaration spells `auto` and not "
               "a type." in report.getvalue())

        registry = (root / FIXY_ONLY_PATHS).read_text(encoding="utf-8")
        (root / FIXY_ONLY_PATHS).write_text("examples/fn\n", encoding="utf-8")
        unregistered_hits, unregistered_failures = scan(root, stream_ban)
        expect("a registry with no band-3 directory fails the ban",
               not unregistered_hits
               and any("has no root directory" in failure for failure in unregistered_failures), True)
        (root / FIXY_ONLY_PATHS).write_text(registry, encoding="utf-8")

        expect("the full check reports violations", check(root) == 1)

        def captured(cwd: Path) -> tuple[int, str]:
            """Run the full check from one working directory and keep its report."""
            previous = Path.cwd()
            buffer = io.StringIO()
            os.chdir(cwd)
            try:
                with contextlib.redirect_stderr(buffer):
                    code = check(root)
            finally:
                os.chdir(previous)
            return code, buffer.getvalue()

        expect("the report from / equals the report from the scan root", captured(Path("/")) == captured(root))

        allow = root / "utils/scripts/no-reserve-allowlist.txt"
        live_keys = sorted(f"{hit.path}:{hit.key}" for hit in scan(root, reserve_ban)[0]
                           if hit.path == "include/crucible/planted/Reserve.h")
        (root / "bench/planted.cpp").unlink()
        (root / "bench/receivers.cpp").unlink()
        (root / "src/planted/Calls.cpp").unlink()
        (root / "include/crucible/planted/Casts.h").unlink()
        (root / "vessel/planted.cpp").unlink()
        (root / "include/crucible/cntp/Streams.h").unlink()
        (root / "src/cntp/streams.cpp").unlink()
        (root / "src/planted/Spawn.cpp").unlink()
        (root / "vessel/spawn.cpp").unlink()
        (root / "utils/scripts/no-reinterpret-allowlist.txt").write_text("", encoding="utf-8")
        (root / "utils/scripts/no-spawn-process-allowlist.txt").write_text("", encoding="utf-8")
        allow.write_text("\n".join(live_keys) + "\n", encoding="utf-8")
        shifted = root / "include/crucible/planted/Reserve.h"
        shifted.write_text("\n\n" + reserve_fixture, encoding="utf-8")
        expect("a content key survives a line shift", check(root) == 0)
        allow.write_text("\n".join(live_keys) + "\ninclude/crucible/planted/Reserve.h:v.reserve(99);\n",
                         encoding="utf-8")
        expect("a stale entry exits 2", check(root) == 2, True)

        broken = root / "include/crucible/planted/Broken.h"
        broken.write_text("void f() { g(1) { } }\n", encoding="utf-8")
        allow.write_text("\n".join(live_keys) + "\n", encoding="utf-8")
        expect("a file the parser cannot read fails the check", check(root) == 1, True)

    if failures:
        print(f"check-banned-calls --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-banned-calls --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    if argv not in ([], ["--self-test"]):
        print("usage: check-banned-calls.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-banned-calls: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
