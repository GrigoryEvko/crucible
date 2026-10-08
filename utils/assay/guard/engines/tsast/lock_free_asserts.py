"""lock-free-asserts — each cross-thread atomic has a lock-free assert beside it.

THE RULE
    Each `std::atomic<T>` of a cross-thread header has a static_assert of
    `std::atomic<T>::is_always_lock_free` beside it (CLAUDE.md section IX).
    When std::atomic<T> is not lock-free on a target, libatomic puts a mutex
    behind it with no warning, and one acquire or release becomes a lock of
    200 to 300 ns.  The assert costs nothing at run time, and it fails the
    build on that target.  The rule refuses:
      no-lock-free-assert  an atomic of the scope whose type has no assert
                           in its file or in the check file of its file
    A header holds no static_assert at namespace scope (the rule
    header-checks), so the assert of a header is in its check file,
    test/layer/checks/LAYER/PATH.cpp for include/LAYER/PATH.h.  One unit of
    each build compiles the check file, so the assert still fails that build
    on such a target.

THE ATOMICS
    An atomic is a template-id named atomic or atomic_ref, with no qualifier
    or qualified by std, and with template arguments that nest.  A standard
    alias such as std::atomic_uint64_t is the atomic of its type.
    std::atomic_flag and the two lock-free aliases of the standard are
    lock-free by definition, so they need no assert.  An assert is a
    static_assert of the file or of its check file whose condition holds
    `ATOMIC::is_always_lock_free`, alone or in a conjunction.  One assert
    covers each atomic of the same type in the file.  Two types are the same
    when their tokens are the same after a first `std::` or `::std::`, so
    white space and comments inside a type are not important.

THE MACRO BODIES
    The type of an atomic in a macro body comes from the arguments of the
    macro.  So the rule reads the preprocessing tokens of each body, and it
    refuses each `atomic <` and `atomic_ref <` there.

THE ENTITY
    The entity of a finding is `HOLDER: TYPE`.  HOLDER is the qualified name
    of the declaration that holds the atomic, with the signature of its
    function for a local declaration, or `#define NAME` for a macro body.
    TYPE is the type of the atomic, or `an atomic in a macro body`.

THE SCOPE
    The directories canopy, cntp, topology and warden of include/crucible/,
    where each atomic is cross-thread by design.  An atomic of a smaller
    scope elsewhere, such as the SPSC ring and the one-shot flag, obeys its
    own discipline.  The units of the stage that move these atomics to
    fixy::Atomic delete this rule.

Complexity: linear in the size of the files of the scope, of their check files and of their macro bodies.
"""

from collections.abc import Iterator, Sequence
from pathlib import PurePosixPath

from assay.core import report as check_report
from assay.guard.engines.tsast.runner import TsastContext
from assay.readers import tsast

RULE = "lock-free-asserts"
KIND = "no-lock-free-assert"
INCLUDE = "include/"
CHECKS = "test/layer/checks/"
ATOMIC_TEMPLATES = frozenset({"atomic", "atomic_ref"})
# The standard aliases of std::atomic<T>, as the name of the alias and T, in the spelling of tsast.spelled().
ATOMIC_ALIASES = {
    "atomic_bool": "bool", "atomic_char": "char", "atomic_schar": "signed char", "atomic_uchar": "unsigned char",
    "atomic_short": "short", "atomic_ushort": "unsigned short", "atomic_int": "int", "atomic_uint": "unsigned int",
    "atomic_long": "long", "atomic_ulong": "unsigned long", "atomic_llong": "long long",
    "atomic_ullong": "unsigned long long", "atomic_char8_t": "char8_t", "atomic_char16_t": "char16_t",
    "atomic_char32_t": "char32_t", "atomic_wchar_t": "wchar_t", "atomic_int8_t": "int8_t",
    "atomic_uint8_t": "uint8_t", "atomic_int16_t": "int16_t", "atomic_uint16_t": "uint16_t",
    "atomic_int32_t": "int32_t", "atomic_uint32_t": "uint32_t", "atomic_int64_t": "int64_t",
    "atomic_uint64_t": "uint64_t", "atomic_intptr_t": "intptr_t", "atomic_uintptr_t": "uintptr_t",
    "atomic_size_t": "size_t", "atomic_ptrdiff_t": "ptrdiff_t", "atomic_intmax_t": "intmax_t",
    "atomic_uintmax_t": "uintmax_t",
}
# The declarations that hold an atomic, for the entity.
HOLDERS = ("field_declaration", "declaration", "alias_declaration", "parameter_declaration", "type_definition")
CLASSES = ("class_specifier", "struct_specifier", "union_specifier")
MACRO_TYPE = "an atomic in a macro body"
MESSAGE = ("std::atomic<{value}> has no static_assert(std::atomic<{value}>::is_always_lock_free) in its file or in "
           "the check file of its file.  Add the assert to the check file of the header, one for each type and "
           "header.  An atomic that is not cross-thread gets a ledger row with its reason")
MACRO_MESSAGE = ("a macro body names std::atomic or std::atomic_ref, and the arguments of the macro give its type.  "
                 "Declare the atomic outside a macro, with its lock-free assert")


def normalized(tokens: Sequence[str]) -> str:
    """Return the spelling of the tokens of a type, with no first `std::` or `::std::` (THE ATOMICS)."""
    for prefix in (("::", "std", "::"), ("std", "::")):
        if tuple(tokens[:len(prefix)]) == prefix:
            return tsast.spelled_tokens(tokens[len(prefix):])
    return tsast.spelled_tokens(tokens)


def qualifiers_before(node: tsast.Node) -> list[str]:
    """Return the scope names before a name or a scope in its qualified chain, outermost first.

    Each scope gives its leaf name, so a template scope gives its template name and a decltype scope gives `?`.
    """
    prefix: list[str] = []
    parent = node.parent
    if parent is None or parent.type != "qualified_identifier":
        return prefix
    scope = parent.child_by_field("scope")
    if node.field == "name" and scope is not None:
        prefix.insert(0, tsast.leaf_name(scope) or "?")
    current = parent
    while current.field == "name" and current.parent is not None and current.parent.type == "qualified_identifier":
        scope = current.parent.child_by_field("scope")
        if scope is not None:
            prefix.insert(0, tsast.leaf_name(scope) or "?")
        current = current.parent
    return prefix


def atomic_type(node: tsast.Node) -> str | None:
    """Return the type of an atomic spelling, or None when the node is not an atomic.

    Args:
        node: A template_type, a type_identifier or a namespace_identifier
    """
    if qualifiers_before(node) not in ([], ["std"]):
        return None
    if node.type == "template_type":
        named = node.child_by_field("name")
        arguments = node.child_by_field("arguments")
        if named is None or tsast.lexeme(named) not in ATOMIC_TEMPLATES or arguments is None:
            return None
        inner = tsast.non_comment_children(arguments)
        return normalized(inner[0].tokens()) if len(inner) == 1 else None
    if node.type in ("type_identifier", "namespace_identifier"):
        return ATOMIC_ALIASES.get(tsast.lexeme(node))
    return None


def asserted_types(tree: tsast.Tree) -> set[str]:
    """Return the type of each `ATOMIC::is_always_lock_free` in a static_assert of a file."""
    found: set[str] = set()
    for node in tree.find("static_assert_declaration"):
        for name in node.descendants("qualified_identifier"):
            last = name.child_by_field("name")
            scope = name.child_by_field("scope")
            if last is not None and scope is not None and last.text == "is_always_lock_free":
                value = atomic_type(scope)
                if value is not None:
                    found.add(value)
    return found


def holder_of(node: tsast.Node) -> str:
    """Return the HOLDER of the entity of an atomic (THE ENTITY)."""
    declaration = node.ancestor_of_type(*HOLDERS)
    name = None
    if declaration is not None:
        named = declaration.child_by_field("name") or declaration.child_by_field("declarator")
        name = tsast.leaf_name(named) if named is not None else None
    signature = tsast.function_signature(node)
    if signature is not None:
        return f"{signature}::{name}" if name is not None else signature
    scopes = [tsast.ANONYMOUS_NAMESPACE if part == "" else part for part in tsast.namespace_path(node)]
    classes: list[str] = []
    holder = node.ancestor_of_type(*CLASSES)
    while holder is not None:
        class_name = holder.child_by_field("name")
        classes.insert(0, (tsast.leaf_name(class_name) or "?") if class_name is not None else "?")
        holder = holder.ancestor_of_type(*CLASSES)
    return "::".join([*scopes, *classes, *([name] if name is not None else [])]) or "-"


def missing_in(tree: tsast.Tree, covered: set[str]) -> Iterator[tuple[int, str, str]]:
    """Yield each atomic of a file whose type has no assert, as (line, holder, type).

    Complexity: linear in the number of nodes of the file.
    """
    for node in tree.find("template_type", "type_identifier", "namespace_identifier"):
        if node.ancestor_of_type("static_assert_declaration") is not None:
            continue
        value = atomic_type(node)
        if value is not None and value not in covered:
            yield node.line, holder_of(node), value


def macro_atomics(body: tsast.MacroBody) -> Iterator[int]:
    """Yield the row of each `atomic <` or `atomic_ref <` in the tokens of one macro body.

    A comment and a string literal give no identifier token, so neither can match.
    """
    tokens = tsast.pp_tokens(body.text, body.first_row)
    for index, token in enumerate(tokens[:-1]):
        if token.kind == "identifier" and tsast.lexeme(token) in ATOMIC_TEMPLATES and tokens[index + 1].text == "<":
            yield token.row


def check_file_of(path: str) -> str | None:
    """Return the check file of a header, test/layer/checks/LAYER/PATH.cpp for include/LAYER/PATH.h, or None."""
    if not path.startswith(INCLUDE):
        return None
    return (PurePosixPath(CHECKS) / PurePosixPath(path[len(INCLUDE):]).with_suffix(".cpp")).as_posix()


def finding(path: str, line: int, holder: str, value: str) -> check_report.Finding:
    """Return the error of one atomic with no assert (THE ENTITY)."""
    message = MACRO_MESSAGE if value == MACRO_TYPE else MESSAGE.format(value=value)
    return check_report.Finding("error", path, line, RULE, message, KIND, f"{holder}: {value}")


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Give an error for each atomic of the scope whose type has no assert (THE RULE)."""
    files = context.scope_files()
    listed = frozenset(context.listed())
    checks = {path: check for path in files if (check := check_file_of(path)) is not None and check in listed}
    checked = {tree.path.as_posix(): asserted_types(tree) for tree in context.trees(sorted(set(checks.values())))}
    trees = list(context.trees(files))
    found: list[check_report.Finding] = []
    for tree in trees:
        path = tree.path.as_posix()
        covered = asserted_types(tree) | checked.get(checks.get(path, ""), set())
        found += [finding(path, line, holder, value) for line, holder, value in sorted(set(missing_in(tree, covered)))]
    for body in context.macro_bodies(trees):
        path = PurePosixPath(body.define.tree.path).as_posix()
        found += [finding(path, row + 1, f"#define {body.name}", MACRO_TYPE) for row in macro_atomics(body)]
    return found
