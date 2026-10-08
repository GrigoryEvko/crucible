"""banned-calls — no reserve call on a std container, no file stream in a runtime directory, and no process start.

THE THREE KINDS
    vector-reserve  CLAUDE.md section IV bans `std::vector::reserve`.  The
                    kind reads each call with a member access named `reserve`
                    as its callee: `v.reserve(n)`, `p->reserve(n)`,
                    `v.Base::reserve(n)` and `v.template reserve<T>(n)`.  It
                    refuses the call when the receiver has a std type.  It
                    also refuses the call when the parse tree does not show
                    the type of the receiver (THE RECEIVER TYPES).  It reads
                    include/, src/ and bench/.
    file-stream     A runtime directory of `RUNTIME_DIRECTORIES` opens a file
                    only through `fixy::fs::mint_file` or
                    `fixy::mint_owned_file`.  The kind reads each name of a
                    C++ file stream or file buffer (`FILE_STREAMS`), with or
                    without a namespace before it.  It also reads each
                    `#include <fstream>`.  A member name does not count.
    process-start   CLAUDE.md section IX bans a raw process start in
                    production code, because the child holds no Permission and
                    no effect row.  The kind reads each reference to a C
                    library function that makes a process, replaces its image
                    or reaps a child.  Examples are `fork`, `clone`, the exec
                    family, `posix_spawn`, `system`, `popen` and the wait
                    family.  It also reads each system call number of the same
                    kernel entries, such as `SYS_clone` and `__NR_execve`.  A
                    reference is a name in an expression or a template
                    argument.  Before the name, the source has no namespace,
                    or `::`, or std, or a namespace alias of std.  The kind
                    then finds a call, an address, a function pointer and a
                    `using` declaration.  It does not find a member, the name
                    of a declaration or an enumerator, or a name after a
                    different namespace or a class.  It reads include/, src/
                    and vessel/, less the kernel dump of
                    include/crucible/perf/bpf/.
    The rule quarantine refuses the same items in a quarantined file that a
    counted configuration compiles.  But the plugin gives no finding in the
    body of a generic lambda, and no counted configuration compiles
    vessel/torch.  For these two reasons, this rule also reads the
    quarantined files of its scope.

THE MACRO BODIES
    The rule reads the parse of each macro body (`tsast.macro_bodies`) with
    the same node rules.  In a body, the name of a declaration also counts
    for process-start, because a use site completes a body such as
    `macro_name fork`.  A namespace alias of std comes from the file that
    holds the `#define`.  When a body does not parse, for example a body with
    `#` or `##`, the rule reads its preprocessing tokens.  A banned name then
    counts when no `.` or `->` is before it.  A name of a process start counts
    only when no namespace other than std or an alias of std is before it.

THE RECEIVER TYPES
    The receiver of a reserve call is a name, a name in parentheses, or
    `this->name`.  The rule finds the declaration of the name in the scopes
    around the call, from the innermost scope out:
      * A local variable before the call, also in the head of a for, if,
        while or switch statement
      * A parameter of the function, of the lambda or of the catch clause
      * A data member of the class of the member function.  For a member
        function with its body out of its class, the class comes from the
        files that the rule reads.
      * A variable at namespace scope before the call, in the same file.
    The rule reads the declared type through the type aliases and the
    typedefs of the scopes around the declaration.  It also reads the aliases
    and the classes that the files of the kind declare.  A name in namespace
    std, or in a namespace alias of std, is a std type.  A name with no
    namespace before it is a std type too when a `using` declaration or a
    `using` directive of std applies to it.  The rule refuses the call when
    the parse tree does not show the type.  These cases do not show it: a
    receiver of a different shape, a name with no declaration in these
    scopes, a `structured binding`, an init-capture, `auto`, `decltype`, a
    template parameter, and a type name that no file of the kind declares.
    The rule refuses a type name that has an alias to a std type and a class
    of the same name.  The message of each refused call gives its case.

WHAT THE RULE CANNOT SEE
    A name that a macro makes with `##`, a system call number that the source
    writes as a literal, and a call through a namespace of a different file
    that holds a `using` directive of std.

THE ENTITY
    The entity of a finding is `HOLDER: CALLEE` for vector-reserve, and
    `HOLDER: NAME` for file-stream and process-start, with the spelling of
    the source (`tsast.spelled()`).  An include of `<fstream>` has the name
    `#include <fstream>`.  HOLDER is the signature of the function that holds
    the site (`tsast.function_signature`).  For a site in no function, HOLDER
    is the scope of the site.  For a site in a macro body, HOLDER is
    `#define NAME`.

THE SCOPE
    The files of the scope of the registry in the directories of each kind,
    less each directory test, examples, third_party, external and vendor.

Complexity: linear in the bytes of the scope and of its macro bodies, plus a walk of the scopes around each reserve call.
"""

import enum
import functools
from collections import defaultdict
from collections.abc import Iterator, Sequence
from dataclasses import dataclass
from pathlib import PurePosixPath
from typing import Self

from assay.core import report as check_report
from assay.guard.engines.tsast.banned_spellings import scope_holder
from assay.guard.engines.tsast.runner import TsastContext, has_directory
from assay.readers import tsast

RULE = "banned-calls"
RESERVE_KIND = "vector-reserve"
STREAM_KIND = "file-stream"
START_KIND = "process-start"
EXCLUDED_DIRECTORIES = frozenset({"test", "examples", "third_party", "external", "vendor"})
# The directories that each kind reads, in the scope of the registry.
RESERVE_ROOTS = ("include/", "src/", "bench/")
START_ROOTS = ("include/", "src/", "vessel/")
START_EXCLUDED = ("include/crucible/perf/bpf/",)
# The runtime directories that open a file only through a fixy door, with the directory of src/ of the same name.
RUNTIME_DIRECTORIES = tuple(
    f"{top}/{name}/"
    for name in ("canopy", "cntp", "cog", "forge", "mimic", "observe", "topology", "warden")
    for top in ("include/crucible", "src")
)
FILE_STREAMS = frozenset(
    {
        "ofstream",
        "ifstream",
        "fstream",
        "filebuf",
        "wofstream",
        "wifstream",
        "wfstream",
        "wfilebuf",
        "basic_ofstream",
        "basic_ifstream",
        "basic_fstream",
        "basic_filebuf",
    }
)
STREAM_HEADER = "<fstream>"
# The C library functions that make a process, replace its image or reap a child.  The set also holds the system
# call numbers of the same kernel entries, for a call of syscall(2).
START_NAMES = frozenset(
    {
        "fork",
        "vfork",
        "_Fork",
        "forkpty",
        "daemon",
        "clone",
        "clone3",
        "__clone",
        "__clone2",
        "execve",
        "execveat",
        "fexecve",
        "execv",
        "execvp",
        "execvpe",
        "execl",
        "execlp",
        "execle",
        "posix_spawn",
        "posix_spawnp",
        "system",
        "popen",
        "pclose",
        "waitpid",
        "wait3",
        "wait4",
        "waitid",
        "SYS_fork",
        "SYS_vfork",
        "SYS_clone",
        "SYS_clone3",
        "SYS_execve",
        "SYS_execveat",
        "__NR_fork",
        "__NR_vfork",
        "__NR_clone",
        "__NR_clone3",
        "__NR_execve",
        "__NR_execveat",
    }
)
# The node types of a class head, and of each declaration of a template parameter.
CLASS_SPECIFIERS = ("class_specifier", "struct_specifier", "union_specifier", "enum_specifier")
TEMPLATE_PARAMETERS = (
    "type_parameter_declaration",
    "optional_type_parameter_declaration",
    "variadic_type_parameter_declaration",
    "template_template_parameter_declaration",
    "parameter_declaration",
    "optional_parameter_declaration",
    "variadic_parameter_declaration",
)
# The node types of the parameters of a function, a lambda and a catch clause.
PARAMETERS = ("parameter_declaration", "optional_parameter_declaration", "variadic_parameter_declaration")
# The node types of a preprocessor conditional and of each of its arms.
PREPROCESSOR_ARMS = ("preproc_if", "preproc_ifdef", "preproc_else", "preproc_elif", "preproc_elifdef")
# The largest number of type aliases that the rule reads for one type.
MAX_ALIAS_DEPTH = 16
RESERVE_MESSAGE = (
    "`{entity}` calls reserve on a std container, which CLAUDE.md section IV bans: {why}.  Use "
    "std::inplace_vector, a sized constructor, or arena storage"
)
START_MESSAGE = (
    "`{entity}` starts a raw process, which CLAUDE.md section IX bans in production code.  The child "
    "holds no Permission and no effect row.  Use std::jthread with permission_fork"
)
STREAM_MESSAGE = (
    "`{entity}` uses a C++ file stream in a runtime directory, which opens a file only through a fixy "
    "door.  Use fixy::fs::mint_file or fixy::mint_owned_file"
)
TOKEN_RESERVE_WHY = "the macro body does not parse, so the parse tree does not show the type of the receiver"


class TypeIndex:
    """The named classes and the type aliases of the files of a kind, by the last part of each name.

    A declaration in a function body is local, and the index does not hold
    it.  The receiver rule finds a local declaration through the scopes
    around its use.  The index makes its tables on the first query.
    """

    def __init__(self, trees: Sequence[tsast.Tree]) -> None:
        """Keep the trees of the files of the kind."""
        self._trees = tuple(trees)
        self._std_names: dict[str, frozenset[str]] = {}
        self._aliases: dict[str, list[tsast.NamespaceAlias]] = {}

    @staticmethod
    def place(node: tsast.Node) -> str:
        """Return the file and the line of a node as `PATH:LINE`."""
        return f"{PurePosixPath(node.tree.path).as_posix()}:{node.line}"

    @functools.cached_property
    def _tables(self) -> tuple[dict[str, list[tsast.Node]], dict[str, list[tsast.Node]]]:
        """Return the class heads by name and the alias and typedef declarations by name.

        Complexity: O(total nodes of the trees), one time.
        """
        classes: defaultdict[str, list[tsast.Node]] = defaultdict(list)
        aliases: defaultdict[str, list[tsast.Node]] = defaultdict(list)
        for tree in self._trees:
            for node in tree.find(*CLASS_SPECIFIERS, "alias_declaration", "type_definition"):
                if is_local(node):
                    continue
                for name in declared_type_names(node):
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
        key = PurePosixPath(tree.path).as_posix()
        if key not in self._std_names:
            self._std_names[key] = std_names(tree)
        return self._std_names[key]

    def namespace_aliases(self, tree: tsast.Tree) -> list[tsast.NamespaceAlias]:
        """Return the namespace aliases of one tree, one time for each tree."""
        key = PurePosixPath(tree.path).as_posix()
        if key not in self._aliases:
            self._aliases[key] = tsast.namespace_aliases(tree)
        return self._aliases[key]


class Shown(enum.IntEnum):
    """What the parse tree shows about the type of a receiver.  A larger value is a stronger reason to refuse a call."""

    NOT_STD = 0
    UNKNOWN = 1
    STD = 2


@dataclass(frozen=True, slots=True)
class Verdict:
    """The type verdict on one receiver, or on one type that a receiver can have, with the reason of a refusal."""

    shown: Shown
    why: str = ""


def after_member_access(tokens: Sequence[tsast.Token], index: int) -> bool:
    """Return True when a member access (`.` or `->`) is before a token.  The search goes back over `template` and `X ::`."""
    at = index - 1
    while at >= 1 and tokens[at].text == "::" and tokens[at - 1].kind == "identifier":
        at -= 2
    if at >= 0 and tokens[at].text == "template":
        at -= 1
    return at >= 0 and tokens[at].text in (".", "->")


def final_name(node: tsast.Node) -> tsast.Node | None:
    """Return the field_identifier or identifier that the field of a member access finally names, or None."""
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


def worst(verdicts: Iterator[Verdict | None], empty: str) -> Verdict:
    """Return the strongest verdict, or UNKNOWN with the reason EMPTY.  Of two equal verdicts, the first one wins.

    A None adds nothing: it is an alias that the chain read before.
    """
    found = [verdict for verdict in verdicts if verdict is not None]
    return max(found, key=lambda verdict: verdict.shown) if found else Verdict(Shown.UNKNOWN, empty)


def is_local(node: tsast.Node) -> bool:
    """Return True when a node is in a function body, a lambda or a `requires` expression."""
    return node.ancestor_of_type("compound_statement", "lambda_expression", "requires_expression") is not None


def declared_type_names(node: tsast.Node) -> list[str]:
    """Return the last part of each type name that a class head, an alias declaration or a typedef declares."""
    if node.type == "type_definition":
        names = (tsast.leaf_name(child) for child in node.children if child.field == "declarator")
        return [name for name in names if name]
    named = node.child_by_field("name")
    name = None if named is None else tsast.leaf_name(named)
    return [name] if name else []


def is_function_declarator(declarator: tsast.Node) -> bool:
    """Return True when a declarator declares a function, through pointer, reference and init declarators."""
    current: tsast.Node | None = declarator
    while current is not None and current.type in (
        "init_declarator",
        "pointer_declarator",
        "reference_declarator",
        "attributed_declarator",
    ):
        current = current.child_by_field("declarator")
    return current is not None and current.type == "function_declarator"


# The result of a search for a declaration: its type nodes, the reason when it cannot show a type, or None.
Declared = list[tsast.Node | None] | str | None


def declaration_types(name: str, item: tsast.Node) -> Declared:
    """Return the declared type of NAME when a declaration or a field_declaration declares it (Declared)."""
    if item.type not in ("declaration", "field_declaration"):
        return None
    for declarator in item.children:
        if declarator.field != "declarator" or is_function_declarator(declarator):
            continue
        bound = declarator.child_by_field("declarator") if declarator.type == "init_declarator" else declarator
        if bound is not None and bound.type == "structured_binding_declarator":
            if any(child.text == name for child in bound.children):
                return "a structured binding declares it"
            continue
        if tsast.leaf_name(declarator) == name:
            return [item.child_by_field("type")]
    return None


def items_of(container: tsast.Node) -> Iterator[tsast.Node]:
    """Yield the items of a block, a namespace body or a class body, and the items of each preprocessor arm in it.

    Complexity: O(items), and the depth of the nested arms.
    """
    for item in container.children:
        if item.type in PREPROCESSOR_ARMS:
            yield item
            yield from items_of(item)
        elif item.field != "condition":
            yield item


def items_before(name: str, container: tsast.Node, child: tsast.Node) -> Declared:
    """Return the type of the last declaration of NAME in the items of a block before CHILD.

    An item in a preprocessor arm counts, because a build can select the arm.  Complexity: O(items of the block).
    """
    found: Declared = None
    for item in items_of(container):
        if item == child:
            break
        declared = declaration_types(name, item)
        if declared is not None:
            found = declared
    return found


def member_types(name: str, body: tsast.Node) -> Declared:
    """Return the type of each data member named NAME in a class body, also in a preprocessor arm.

    Complexity: O(members of the class).
    """
    found: list[tsast.Node | None] = []
    for item in items_of(body):
        declared = declaration_types(name, item)
        if isinstance(declared, str):
            return declared
        found += declared or []
    return found or None


def parameter_types(name: str, holder: tsast.Node | None) -> list[tsast.Node | None] | None:
    """Return the type of the parameter named NAME of a parameter list, or None."""
    if holder is None:
        return None
    for parameter in holder.children:
        if parameter.type in PARAMETERS:
            declarator = parameter.child_by_field("declarator")
            if declarator is not None and tsast.leaf_name(declarator) == name:
                return [parameter.child_by_field("type")]
    return None


def function_parameters(function: tsast.Node) -> tsast.Node | None:
    """Return the parameter list of a function definition, through pointer and reference declarators."""
    current = function.child_by_field("declarator")
    while current is not None and current.type != "function_declarator":
        current = current.child_by_field("declarator")
    return None if current is None else current.child_by_field("parameters")


def template_parameter_names(parameters: tsast.Node | None) -> set[str]:
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


def template_parameters_around(node: tsast.Node) -> set[str]:
    """Return the name of each parameter of each template declaration around a node."""
    names: set[str] = set()
    owner = node.ancestor_of_type("template_declaration")
    while owner is not None:
        names |= template_parameter_names(owner.child_by_field("parameters"))
        owner = owner.ancestor_of_type("template_declaration")
    return names


def head_declarations(statement: tsast.Node) -> Iterator[tsast.Node]:
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


def scope_types(name: str, node: tsast.Node, child: tsast.Node) -> Declared:
    """Return the declared type of NAME when the scope NODE declares it, seen from its part CHILD (Declared)."""
    kind = node.type
    if kind in ("compound_statement", "translation_unit", "declaration_list", *PREPROCESSOR_ARMS):
        return items_before(name, node, child)
    if kind in ("for_statement", "if_statement", "while_statement", "switch_statement"):
        found: Declared = None
        for declaration in head_declarations(node):
            if declaration != child:
                found = declaration_types(name, declaration) or found
        return found
    if kind == "for_range_loop" and child.field == "body":
        declarator = node.child_by_field("declarator")
        if declarator is not None and declarator.type == "structured_binding_declarator":
            return (
                "a structured binding declares it" if any(part.text == name for part in declarator.children) else None
            )
        if declarator is not None and tsast.leaf_name(declarator) == name:
            return [node.child_by_field("type")]
        return None
    if kind == "catch_clause" and child.field == "body":
        return parameter_types(name, node.child_by_field("parameters"))
    if kind == "lambda_expression":
        captures = node.child_by_field("captures")
        for capture in [] if captures is None else captures.children:
            left = capture.child_by_field("left") if capture.type == "lambda_capture_initializer" else None
            if left is not None and left.text == name:
                return "an init-capture of the lambda declares it"
        declarator = node.child_by_field("declarator")
        return parameter_types(name, None if declarator is None else declarator.child_by_field("parameters"))
    if kind == "function_definition":
        return parameter_types(name, function_parameters(node))
    if kind == "template_declaration" and name in template_parameter_names(node.child_by_field("parameters")):
        return "a template parameter declares it"
    return None


def out_of_class_members(name: str, function: tsast.Node, types: TypeIndex) -> Declared:
    """Return the type of the member NAME of the class of a member function with its body out of its class.

    The class is each class head of the index with the name of the last scope of the function name.
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
        members = None if body is None else member_types(name, body)
        if isinstance(members, str):
            return members
        found += members or []
    return found or None


def receiver_types(name: str, at: tsast.Node, types: TypeIndex, *, is_member: bool) -> list[tsast.Node | None] | str:
    """Return the type nodes of the declaration of a receiver name, from the innermost scope out, or the reason.

    Complexity: O(depth of the node times the items of each scope on the path).

    Args:
        name: The receiver name
        at: The receiver node
        types: The classes and the aliases of the files of the kind
        is_member: True for `this->name`, which only a data member declares
    """
    child, node = at, at.parent
    while node is not None:
        found: Declared = None
        if node.type == "field_declaration_list":
            found = member_types(name, node)
        elif not is_member:
            found = scope_types(name, node, child)
        if found is None and node.type == "function_definition":
            found = out_of_class_members(name, node, types)
        if found is not None:
            return found
        child, node = node, node.parent
    return f"no declaration of `{name}` is in a scope around the call"


def local_type_heads(name: str, at: tsast.Node) -> list[tsast.Node]:
    """Return the class heads and the aliases named NAME in the blocks and the class bodies around a node.

    The innermost scope that declares the name gives the result.  In a block, only a declaration before the node
    counts.
    """
    child, node = at, at.parent
    while node is not None:
        if node.type in ("compound_statement", "field_declaration_list"):
            heads = []
            for item in items_of(node):
                if item == child and node.type == "compound_statement":
                    break
                for head in (item, item.child_by_field("type")):
                    if (
                        head is not None
                        and head.type in (*CLASS_SPECIFIERS, "alias_declaration", "type_definition")
                        and name in declared_type_names(head)
                    ):
                        heads.append(head)
            if heads:
                return heads
        child, node = node, node.parent
    return []


def std_using(name: str, at: tsast.Node, types: TypeIndex) -> str | None:
    """Return the reason when a using-declaration or a using-directive of std applies to NAME at a node, or None."""
    aliases = types.namespace_aliases(at.tree)
    names = types.std_names(at.tree)
    for using in tsast.using_names(at.tree):
        if using.kind == "enum" or not tsast.encloses(using.scope, at) or using.node.end > at.start:
            continue
        target = tsast.resolve_namespace(using.target, using.node, aliases, is_global=using.is_global)
        if not target or (target[0] != "std" and using.target[0] not in names):
            continue
        if using.is_directive or target[-1] == name:
            return (
                f"the name can be `std::{name}`, because `{tsast.spelled(using.node)}` "
                f"at {types.place(using.node)} applies to it"
            )
    return None


def head_verdict(head: tsast.Node, name: str, types: TypeIndex, seen: frozenset[tsast.Node]) -> Verdict | None:
    """Return the verdict of one declaration of a type name, or None for an alias that the chain read before."""
    if head.type in CLASS_SPECIFIERS:
        if tsast.namespace_path(head)[:1] == ("std",):
            return Verdict(Shown.STD, f"`{name}` is a class of namespace std")
        return Verdict(Shown.NOT_STD)
    if head in seen:
        return None
    if len(seen) >= MAX_ALIAS_DEPTH:
        return Verdict(Shown.UNKNOWN, f"the aliases of `{name}` form a chain longer than {MAX_ALIAS_DEPTH}")
    inner = type_verdict(head.child_by_field("type"), types, seen | {head})
    if inner.shown is Shown.NOT_STD:
        return inner
    return Verdict(inner.shown, f"{inner.why}, through the alias `{name}` at {types.place(head)}")


def named_verdict(
    at: tsast.Node, is_global: bool, parts: tuple[str, ...], types: TypeIndex, seen: frozenset[tsast.Node]
) -> Verdict:
    """Return the verdict of a type that a name gives.

    Complexity: O(declarations of the name in the index), and one walk to the root of the tree for each alias in the
    chain.
    """
    resolved = tsast.resolve_namespace(parts, at, types.namespace_aliases(at.tree), is_global=is_global)
    if len(resolved) > 1 and (resolved[0] == "std" or (not is_global and parts[0] in types.std_names(at.tree))):
        return Verdict(Shown.STD, f"`{tsast.spelled(at)}` is a type of namespace std")
    if not is_global and resolved[0] in template_parameters_around(at):
        return Verdict(Shown.UNKNOWN, f"`{resolved[0]}` is a template parameter")
    name = resolved[-1]
    if len(resolved) == 1 and not is_global:
        local = local_type_heads(name, at)
        if local:
            return worst(
                (head_verdict(head, name, types, seen) for head in local), f"the aliases of `{name}` form a cycle"
            )
        using = std_using(name, at, types)
        if using is not None:
            return Verdict(Shown.STD, using)
    heads = types.aliases(name) + types.classes(name)
    if not heads:
        return Verdict(Shown.UNKNOWN, f"no file that the rule reads declares the type `{name}`")
    return worst((head_verdict(head, name, types, seen) for head in heads), f"the aliases of `{name}` form a cycle")


def type_verdict(type_node: tsast.Node | None, types: TypeIndex, seen: frozenset[tsast.Node]) -> Verdict:
    """Return the verdict of one declared type: the `type` field of a declaration, a parameter, a member or an alias."""
    if type_node is None:
        return Verdict(Shown.UNKNOWN, "the declaration has no type that the parser reads")
    kind = type_node.type
    if kind == "type_descriptor":
        return type_verdict(type_node.child_by_field("type"), types, seen)
    if kind in ("primitive_type", "sized_type_specifier"):
        return Verdict(Shown.NOT_STD)
    if kind in ("placeholder_type_specifier", "decltype", "auto"):
        return Verdict(Shown.UNKNOWN, f"the declaration spells `{tsast.spelled(type_node)}` and not a type")
    if kind in CLASS_SPECIFIERS:
        named = type_node.child_by_field("name")
        parts = None if named is None else tsast.qualified_parts(named)
        is_std = parts is not None and len(parts[1]) > 1 and parts[1][0] in types.std_names(type_node.tree)
        return (
            Verdict(Shown.STD, f"`{tsast.spelled(type_node)}` is a class of namespace std")
            if is_std
            else Verdict(Shown.NOT_STD)
        )
    parts = tsast.qualified_parts(type_node)
    if parts is None or not parts[1]:
        return Verdict(Shown.UNKNOWN, f"the type `{tsast.spelled(type_node)}` is not a name")
    return named_verdict(type_node, parts[0], parts[1], types, seen)


def receiver_detail(receiver: tsast.Node | None, types: TypeIndex) -> str | None:
    """Return the reason to refuse a reserve call on a receiver, or None when its type is not in std.

    THE RECEIVER TYPES gives the rules.

    Args:
        receiver: The `argument` field of the member access of the call
        types: The classes and the aliases of the files of the kind
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
        return (
            f"the parse tree does not show the type of the receiver `{tsast.spelled(target)}`: "
            "it is not a name or `this->name`"
        )
    declared = receiver_types(name, target, types, is_member=is_member)
    if isinstance(declared, str):
        return f"the parse tree does not show the type of the receiver `{name}`: {declared}"
    verdict = worst(
        (type_verdict(type_node, types, frozenset()) for type_node in declared), "no declaration gives a type"
    )
    if verdict.shown is Shown.STD:
        return f"the receiver `{name}` has a std type: {verdict.why}"
    if verdict.shown is Shown.UNKNOWN:
        return f"the parse tree does not show the type of the receiver `{name}`: {verdict.why}"
    return None


def reserve_calls(root: tsast.Node, types: TypeIndex) -> Iterator[tuple[tsast.Node, str, str]]:
    """Yield the `reserve` name node, the spelling of the callee and the reason of each refused reserve call."""
    for call in root.descendants("call_expression"):
        callee = call.child_by_field("function")
        if callee is None or callee.type != "field_expression":
            continue
        field = callee.child_by_field("field")
        name = final_name(field) if field is not None else None
        if name is None or name.text != "reserve":
            continue
        detail = receiver_detail(callee.child_by_field("argument"), types)
        if detail is not None:
            yield name, tsast.spelled(callee), detail


def is_reserve_token(tokens: Sequence[tsast.Token], index: int) -> bool:
    """Return True when a token is `reserve` after a member access and before `(` or `<`, in an unparsed body."""
    following = tokens[index + 1].text if index + 1 < len(tokens) else ""
    return tokens[index].text == "reserve" and following in ("(", "<") and after_member_access(tokens, index)


def std_names(tree: tsast.Tree) -> frozenset[str]:
    """Return std and each namespace alias of the file that names std, through chains of aliases."""
    return frozenset({"std"}) | tsast.alias_closure(tsast.namespace_aliases(tree), [("std",)])


def outer_qualified(node: tsast.Node) -> tsast.Node:
    """Return the outermost `qualified_identifier` with NODE as its last name, or NODE itself."""
    outer = node
    while outer.field == "name" and outer.parent is not None and outer.parent.type == "qualified_identifier":
        outer = outer.parent
    return outer


def start_references(root: tsast.Node, names: frozenset[str], is_fragment: bool) -> Iterator[tuple[tsast.Node, str]]:
    """Yield the name node and the spelling of each reference to a process start in a root (THE THREE KINDS).

    Args:
        root: The root of a file or of a macro body
        names: std and each namespace alias of std of the file
        is_fragment: True for a macro body, where the name of a declaration is text that a use site completes
    """
    for node in root.descendants_named(START_NAMES, "identifier", "type_identifier"):
        outer = outer_qualified(node)
        if node.type == "type_identifier" and (outer.parent is None or outer.parent.type != "type_descriptor"):
            continue
        if node.parent is not None and node.parent.type == "enumerator":
            continue
        if node.field == "declarator" and not is_fragment:
            continue
        parts = tsast.qualified_parts(outer)
        if parts is None:
            continue
        qualifiers = parts[1][:-1]
        if not qualifiers or (len(qualifiers) == 1 and qualifiers[0] in names):
            yield node, tsast.spelled(outer)


def is_start_token(tokens: Sequence[tsast.Token], index: int, names: frozenset[str]) -> bool:
    """Return True when a token names a process start in an unparsed body, after no member access and no namespace.

    A namespace before the token is permitted when it is std or an alias of std.
    """
    if tokens[index].text not in START_NAMES or after_member_access(tokens, index):
        return False
    if index >= 1 and tokens[index - 1].text == "::":
        qualifier = tokens[index - 2] if index >= 2 else None
        return qualifier is None or qualifier.kind != "identifier" or qualifier.text in names
    return True


def stream_references(root: tsast.Node) -> Iterator[tuple[tsast.Node, str]]:
    """Yield the node and the spelling of each name of a file stream and each include of `<fstream>` in a root.

    The name counts with or without a namespace before it.  A `using` directive or a namespace alias then does not
    hide it.
    """
    for node in root.descendants_named(FILE_STREAMS, "identifier", "type_identifier"):
        yield node, tsast.spelled(outer_qualified(node))
    for node in root.descendants("preproc_include"):
        target = node.child_by_field("path")
        if target is not None and target.text == STREAM_HEADER:
            yield node, f"#include {STREAM_HEADER}"


def is_stream_token(tokens: Sequence[tsast.Token], index: int) -> bool:
    """Return True when a token names a file stream in an unparsed body, with no member access before it."""
    return tokens[index].text in FILE_STREAMS and not after_member_access(tokens, index)


def holds(roots: tuple[str, ...], path: str, others: tuple[str, ...] = ()) -> bool:
    """Return True when a file is in one of the roots of a kind, and in none of the other directories."""
    return path.startswith(roots) and not path.startswith(others)


def holder_of(node: tsast.Node) -> str:
    """Return the holder of a site of a file: the signature of its function, or its scope (THE ENTITY)."""
    return tsast.function_signature(node) or scope_holder(node)


def finding(kind: str, path: str, line: int, entity: str, why: str = "") -> check_report.Finding:
    """Return the error of one site of a kind (THE ENTITY)."""
    message = {RESERVE_KIND: RESERVE_MESSAGE, STREAM_KIND: STREAM_MESSAGE, START_KIND: START_MESSAGE}[kind]
    return check_report.Finding("error", path, line, RULE, message.format(entity=entity, why=why), kind, entity)


@dataclass(frozen=True, slots=True)
class Kinds:
    """The kinds that read one file."""

    reserve: bool
    stream: bool
    start: bool

    @classmethod
    def of(cls, path: str) -> Self:
        """Return the kinds with directories that hold a file."""
        return cls(
            holds(RESERVE_ROOTS, path), holds(RUNTIME_DIRECTORIES, path), holds(START_ROOTS, path, START_EXCLUDED)
        )


def file_findings(tree: tsast.Tree, kinds: Kinds, types: TypeIndex) -> Iterator[check_report.Finding]:
    """Yield the error of each site of the kinds of one file, but not of its macro bodies."""
    path = tree.path.as_posix()
    if kinds.reserve:
        for node, callee, why in reserve_calls(tree.root, types):
            yield finding(RESERVE_KIND, path, node.line, f"{holder_of(node)}: {callee}", why)
    if kinds.stream:
        for node, spelling in stream_references(tree.root):
            yield finding(STREAM_KIND, path, node.line, f"{holder_of(node)}: {spelling}")
    if kinds.start:
        for node, spelling in start_references(tree.root, types.std_names(tree), False):
            yield finding(START_KIND, path, node.line, f"{holder_of(node)}: {spelling}")


def body_findings(body: tsast.MacroBody, kinds: Kinds, types: TypeIndex) -> Iterator[check_report.Finding]:
    """Yield the error of each site of the kinds of the file of one macro body, in the body (THE MACRO BODIES)."""
    path = PurePosixPath(body.define.tree.path).as_posix()
    holder = f"#define {body.name}"
    names = types.std_names(body.define.tree)
    if body.is_parsed:
        sites: list[tuple[str, tsast.Node, str, str]] = []
        if kinds.reserve:
            sites += [(RESERVE_KIND, node, callee, why) for node, callee, why in reserve_calls(body.tree.root, types)]
        if kinds.stream:
            sites += [(STREAM_KIND, node, spelling, "") for node, spelling in stream_references(body.tree.root)]
        if kinds.start:
            sites += [
                (START_KIND, node, spelling, "") for node, spelling in start_references(body.tree.root, names, True)
            ]
        for kind, node, spelling, why in sites:
            yield finding(kind, path, body.origin(node)[0] + 1, f"{holder}: {spelling}", why)
        return
    tokens = tsast.pp_tokens(body.text, body.first_row)
    for index, token in enumerate(tokens):
        if token.kind != "identifier":
            continue
        entity = f"{holder}: {tsast.lexeme(token)}"
        if kinds.reserve and is_reserve_token(tokens, index):
            yield finding(RESERVE_KIND, path, token.row + 1, entity, TOKEN_RESERVE_WHY)
        if kinds.stream and is_stream_token(tokens, index):
            yield finding(STREAM_KIND, path, token.row + 1, entity)
        if kinds.start and is_start_token(tokens, index, names):
            yield finding(START_KIND, path, token.row + 1, entity)


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Give an error for each site of each kind in the files of its directories (THE THREE KINDS)."""
    trees = list(
        context.trees([path for path in context.scope_files() if not has_directory(path, EXCLUDED_DIRECTORIES)])
    )
    kinds = {tree.path.as_posix(): Kinds.of(tree.path.as_posix()) for tree in trees}
    types = TypeIndex([tree for tree in trees if kinds[tree.path.as_posix()].reserve])
    found: list[check_report.Finding] = []
    for tree in trees:
        found += file_findings(tree, kinds[tree.path.as_posix()], types)
    for body in context.macro_bodies(trees):
        found += body_findings(body, kinds[PurePosixPath(body.define.tree.path).as_posix()], types)
    return found
