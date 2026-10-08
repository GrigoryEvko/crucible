"""header-checks — a header does no compile-time work again in each translation unit that includes it.

These items of work run again in each translation unit that includes their
header:
  - a self-test namespace
  - a static_assert at namespace scope
  - a static_assert in the body of a function that is not a template
  - a constant evaluation that runs where the header declares it.
On the tree of 2026-10-01 the checks alone cost about half of the front-end
time of a file.

Such a check lives in the check file of its header, which one translation
unit compiles one time.  Such an evaluation is lazy: a variable template, or
a member of a template.  Then only a translation unit that reads it
evaluates it.  A static_assert in a class or in a template stays in the
header, because it applies to each instantiation.

THE CHECK FILE
    The check file of include/LAYER/PATH.h is test/layer/checks/LAYER/PATH.cpp.
    Its first line of code includes its own header, and the file then also
    shows that the header compiles alone.  It holds the self-test namespaces
    and the namespace-scope static_asserts of the header.  Each one is
    unchanged and in the same outer namespaces, and each name resolves
    as before.  In each build, test/layer/CMakeLists.txt compiles the check
    file in the position of the one-line sentinel of its header.

THE SIX KINDS
    The rule reads the parse tree of each header of its scope, include/,
    and of each file in test/layer/checks/.  An item of work that a header
    does in each includer has one of these kinds:
      namespace.  A self-test namespace is a namespace definition with a
        segment of its name that has the word `test`, when `_` divides the
        segment into words.  x_self_test, self_test and fn_test are
        self-test namespaces.  `testing` is a different word, and
        foundation::effects::testing is not a self-test namespace.  A
        self-test namespace in a self-test namespace counts with the outer
        one.
      static_assert.  A namespace-scope static_assert is a static_assert
        declaration that no class, function, lambda or other block holds.
        A namespace body, an extern "C++" body and an arm of a
        preprocessor conditional are namespace scope.  A static_assert in
        a self-test namespace counts with that namespace, and not alone.
      function-static_assert.  A static_assert in the body of a function
        or a lambda that is not in a template context.  The compiler
        evaluates it when it reads the body, in each includer.
      eager-evaluation.  A reflection walk that no template context holds,
        in one of these shapes:
          - a variable at namespace scope, or a static data member, that
            is constexpr, constinit or const, with a reflection query in
            its initializer
          - an alias with a reflection query in its type, for example in a
            splice
          - a constexpr or constinit variable in the body of a function or
            a lambda, with a reflection query in its initializer
          - an expansion statement (`template for`) in the body of a
            function or a lambda, which the compiler expands when it reads
            the body.
        A reflection query is one of these:
          - a call of a function of std::meta, or of a std::define_static_*
            function, also with no qualifier through argument-dependent
            lookup: members_of, enumerators_of, `substitute` and the others
          - a call that takes a reflection `^^X` as an argument or as a
            template argument, because such a call walks reflection
          - a splice in a loop.
        The walk reads the evaluated operands only, and the body of a
        lambda that the initializer calls.  It does not read sizeof,
        alignof, decltype, noexcept, a `requires` expression or a different
        lambda body.  A plain call, a cast, a cheap constructor and a `^^X`
        alone are no reflection query.  The compiler sees the cost of a
        plain call accurately (WHAT THE RULE CANNOT SEE).  This kind reads
        only the work that the operation count of GCC does not see:
        template instantiation and reflection queries.
        A const variable counts only when the object itself is const:
        `const char* p` does not count, and `const char* const p` does.
      eager-instantiation.  A call of std::define_static_array or
        std::define_static_string that no template context holds.  Also
        such a call in a template context when no argument has a dependent
        type.  GCC resolves such a call where the header stands, and it
        instantiates the function there, in each includer.  The first
        instantiation of define_static_array costs about 68 M instructions
        in a unit, and that of define_static_string about 19 M.
        An argument has a dependent type when one of these names a
        dependent name:
          - a template argument in the argument
          - the type of a cast or of an initializer list in the argument
          - the declared type of a variable or a parameter that the
            argument names.
        A template parameter is a dependent name.  A local that a template
        parameter gives its type or its value is a dependent name too.
        foundation/reflect/Anchor.h gives the type that makes a list or a
        text dependent on a template parameter.
      eager-fold.  A call in the body of a consteval function that is not
        in a template context, when each argument is constant: a literal, a
        reflection `^^X`, a name that the function does not declare, or a
        call of such arguments.  GCC evaluates such a call when it reads
        the body, in each includer, and no operation limit reports the
        cost.  The rule counts a reflection query with one argument or
        more.  It also counts a call with no argument of a function that is
        not in std.  A function that the header declares without constexpr
        or consteval is a stub, and a call of a stub does not count.
        A call of an exit of the Report family through fixy or
        foundation::core does not count, when
        include/foundation/core/Report.h declares the exit without
        constexpr or consteval.  A call in a static_assert, in an expansion
        statement or in the initializer of a constexpr local belongs to
        the kinds above.  Only the outermost constant call counts.
    A template context is one of these, when it holds the node:
      - a template declaration with parameters
      - a generic lambda: a lambda with a template parameter list or a
        parameter of a placeholder type
      - an abbreviated function template: a function with a parameter of
        a placeholder type
      - a function with a `requires` clause.
    An explicit specialization (`template <>`) is not a template context,
    because the compiler reads its body where it stands.

    A macro that makes a check.  The rule reads the replacement list of
    each `#define` of each header of the scope as preprocessing tokens.  It
    counts the self-test namespaces and the static_asserts that the list
    writes at namespace scope.  A namespace body and an extern "C++" body
    are namespace scope, and the body of a class, a function, a block or an
    initializer list is not.  A macro that names such a macro makes its
    checks too.  An invocation of such a macro at namespace scope in a
    header gives one item for each check of one expansion.  An invocation
    in a self-test namespace counts with that namespace.
    The key of each item is the spelling of the invocation.  A
    static_assert with the literal `true` as its condition checks no fact.
    It is the device that makes a macro end in a semicolon at its call
    site, and it does not count.

THE OTHER FINDINGS
    The rule also refuses these, and no row of the ledger admits them:
      check-file  a check file that is not a .cpp file, that has no
                  header, or that does not include its header first
      unread      a tree with no directory include/, and a namespace with a
                  name that the parse tree does not give
      ledger      a row of the ledger that the rule refuses (THE LEDGER)
    A file that does not parse gives the kind parse (the engine runner).

THE ENTITY
    The entity of an item is its key, and the message of its finding gives
    the key.  The key of a namespace is its full name.  The key of a
    static_assert is the spelling of its condition.  The key of an eager
    evaluation is the full name of the variable, the alias or the expansion
    statement.  The key of a call is the full name of its scope and the
    spelling of the call.  The key of an item that a macro makes is the
    spelling of the invocation, and one invocation can make more than one
    item.

THE LEDGER
    utils/ledgers/header-checks.txt has the policy count (assay.core.ledger).
    A row `PATH | KIND | COUNT | KEY | REASON` holds the items of one key
    that stay in their header.  Its reason is necessary.  A reason of one
    type tells why the result of each item changes with the translation
    unit that includes the header.  A reason of the other type tells why a
    template in its position lets a translation unit specialize a verdict.
    An item with no row is an error, and a row with more items than the
    tree holds is an error too.  The rule also refuses these rows:
      - a configuration row, and a difference row of a configuration
      - a row with a kind that is not one of the six kinds
      - a row with a path that is not in include/
      - a row with no reason.

WHAT THE RULE CANNOT SEE
    A macro body has no scope until the macro expands.  The rule then
    counts the checks of a macro at each invocation and not at its
    `#define`.  A macro of a file that is not in the scope, and a name that
    a macro builds with `##`, are not read.  The rule does not read a
    function static_assert or an eager evaluation that a macro writes.
    Each arm of an `#if` counts, because the kit does not preprocess.

    The syntax cannot tell a cheap call from an expensive one.  It cannot
    see an eager instantiation such as a function that is not a template
    and reads a variable template.  It also cannot see a plain call into a
    function that walks reflection.  A function that is not a template and calls a
    template with a concrete argument instantiates that template where the
    function stands.  A name function of one enum that calls enum_name
    then instantiates the walk over the enum in each includer.  The kinds
    of a call read one call, and they do not follow such a chain.

    The eager fold reads the body of a consteval function only.  A
    reflection query in a constexpr function that is not a template is an
    invocation that GCC also evaluates where the header stands.  The rule
    does not count it.  The rule reads the dependence of an argument
    from its syntax.  A member of a class template that names no template
    parameter in its type then reads as not dependent.

    The build gives the accurate measure of the operations of a constant
    evaluation: test/layer compiles each header alone at a low
    -fconstexpr-ops-limit.  The rule header-constexpr-ops holds the list of
    the higher limits.

Complexity: linear in the nodes of the headers, times the depth of a node.  The macro table adds the tokens of the
macro bodies, times the depth of the longest chain of macros.
"""

from collections.abc import Sequence
from dataclasses import dataclass
from pathlib import PurePosixPath

from assay.core import git
from assay.core import ledger as rule_ledger
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.meta.runner import NotApplicable
from assay.guard.engines.tsast.runner import TsastContext
from assay.readers import tsast

RULE = "header-checks"
INCLUDE = "include/"
INCLUDE_DIRECTORY = "include"
CHECKS = "test/layer/checks/"
CHECK_SUFFIX = ".cpp"
HEADER_SUFFIXES = (".h", ".hpp")
NAMESPACE = "namespace"
ASSERT = "static_assert"
FUNCTION_ASSERT = "function-static_assert"
EAGER = "eager-evaluation"
INSTANCE = "eager-instantiation"
FOLD = "eager-fold"
KINDS = (NAMESPACE, ASSERT, FUNCTION_ASSERT, EAGER, INSTANCE, FOLD)
CHECK_FILE = "check-file"
UNREAD = "unread"
LEDGER_KIND = "ledger"
# The two functions of std with an expensive first instantiation in a unit.
STATIC_DEFINERS = frozenset({"define_static_array", "define_static_string"})
# The node types of the parameters of a template parameter list.  The first
# four declare a type, a template or a pack of types.
TYPE_PARAMETERS = (
    "type_parameter_declaration",
    "variadic_type_parameter_declaration",
    "optional_type_parameter_declaration",
    "template_template_parameter_declaration",
)
VALUE_PARAMETERS = ("parameter_declaration", "optional_parameter_declaration", "variadic_parameter_declaration")
LITERALS = frozenset(
    {
        "number_literal",
        "string_literal",
        "char_literal",
        "raw_string_literal",
        "concatenated_string",
        "true",
        "false",
        "nullptr",
        "user_defined_literal",
    }
)
CASTS = frozenset({"static_cast", "const_cast", "reinterpret_cast", "dynamic_cast"})
# The header of the exits of the Report family, and the qualifiers of a call
# of an exit.  include/fixy/Core.h names each exit in namespace fixy.
REPORT_HEADER = "include/foundation/core/Report.h"
REPORT_QUALIFIERS = frozenset({("fixy",), ("foundation", "core")})
# The node types with a body at namespace scope.  A static_assert with an
# ancestor of a different type is in a class, a function, a lambda or a
# block.
NAMESPACE_SCOPE = frozenset(
    {
        "translation_unit",
        "declaration_list",
        "namespace_definition",
        "linkage_specification",
        "preproc_if",
        "preproc_ifdef",
        "preproc_else",
        "preproc_elif",
        "preproc_elifdef",
    }
)
FUNCTION_BODIES = ("function_definition", "lambda_expression")
# The operands that the compiler does not evaluate.  A lambda body runs only
# when a call runs it, and the call is the call_expression around it.
UNEVALUATED = frozenset(
    {
        "sizeof_expression",
        "alignof_expression",
        "decltype",
        "noexcept_expression",
        "requires_expression",
        "requires_clause",
        "lambda_expression",
    }
)
# The reflection functions that a header can call with no qualifier, through
# argument-dependent lookup on std::meta::info or a `using namespace`.  A call
# with the qualifier std::meta counts for each name.
REFLECTION_FUNCTIONS = frozenset(
    {
        "members_of",
        "static_data_members_of",
        "nonstatic_data_members_of",
        "enumerators_of",
        "bases_of",
        "define_static_array",
        "define_static_string",
        "define_static_object",
        "substitute",
        "can_substitute",
        "template_arguments_of",
        "template_of",
        "parameters_of",
        "annotations_of",
        "annotations_of_with_type",
        "reflect_constant",
        "reflect_object",
        "reflect_function",
        "reflect_constant_array",
        "reflect_constant_string",
        "extract",
        "identifier_of",
        "display_string_of",
        "type_of",
        "parent_of",
        "dealias",
        "constant_of",
    }
)
LOOPS = frozenset({"for_statement", "for_range_loop", "while_statement", "do_statement"})
CLASS_SPECIFIERS = ("class_specifier", "struct_specifier", "union_specifier")
NAME_LEAVES = ("identifier", "field_identifier", "type_identifier", "namespace_identifier")
# The node types with a name that a function or a class declares (names_declared()).
DECLARING = (
    "init_declarator",
    "declaration",
    "field_declaration",
    "parameter_declaration",
    "optional_parameter_declaration",
    "alias_declaration",
    "type_definition",
    "class_specifier",
    "struct_specifier",
    "union_specifier",
    "enum_specifier",
    "enumerator",
    "function_definition",
    "using_declaration",
    "structured_binding_declarator",
    "template_parameter_list",
)
# The parent nodes of a constant call that is an operand of a larger constant call.
OPERAND_HOLDERS = (
    "argument_list",
    "parenthesized_expression",
    "binary_expression",
    "unary_expression",
    "conditional_expression",
    "initializer_list",
)
REPAIR = (
    "An item with a result that changes with the translation unit that includes the header can stay, with a row "
    "of the ledger and its reason"
)


@dataclass(frozen=True, slots=True)
class Item:
    """One item of compile-time work that a header does in each includer (THE SIX KINDS, THE ENTITY).

    `via` names the macro with the invocation that makes the item, or is
    empty for an item that the header writes itself.  `made` is the number of
    items of the kind that one invocation of that macro makes.  `detail`
    tells what makes an eager item, or is empty.
    """

    path: str
    line: int
    kind: str
    key: str
    via: str = ""
    made: int = 1
    detail: str = ""


@dataclass(frozen=True, slots=True)
class MacroChecks:
    """The checks that one expansion of a macro writes at namespace scope."""

    namespaces: int
    asserts: int


@dataclass(frozen=True, slots=True)
class MacroWrites:
    """What the replacement list of one `#define` writes at namespace scope.

    `uses` holds each identifier at namespace scope, with True when `(`
    comes after it.  A name of a different macro then adds the checks of
    that macro.
    """

    namespaces: int
    asserts: int
    uses: tuple[tuple[str, bool], ...]


class Dependence:
    """The names at one point of a header that a template parameter changes.

    `names` holds each name with a dependent value or type, and `typed`
    holds each name with a dependent type.  dependence_at() adds to the two
    sets in the sequence of the source.
    """

    __slots__ = ("names", "typed")

    def __init__(self) -> None:
        """Start with no name."""
        self.names: set[str] = set()
        self.typed: set[str] = set()


def is_self_test_segment(segment: str) -> bool:
    """Return True when `test` is one of the words of a segment of a namespace name, which `_` divides."""
    return "test" in segment.split("_")


def is_at_namespace_scope(node: tsast.Node) -> bool:
    """Return True when each ancestor of a node is a namespace body, a linkage body, an arm of a conditional or the unit.

    Complexity: linear in the depth of the node.
    """
    owner = node.parent
    while owner is not None:
        if owner.type not in NAMESPACE_SCOPE:
            return False
        owner = owner.parent
    return True


def has_placeholder_parameter(parameters: tsast.Node | None) -> bool:
    """Return True when a parameter list has a parameter of a placeholder type, such as `auto x` or `Concept auto x`."""
    if parameters is None:
        return False
    for parameter in parameters.children_of_type(*VALUE_PARAMETERS):
        written = parameter.child_by_field("type")
        if written is not None and written.type == "placeholder_type_specifier":
            return True
    return False


def is_in_template_context(node: tsast.Node) -> bool:
    """Return True when a template, a generic lambda, an abbreviated template or a constrained function encloses a node.

    Complexity: linear in the depth of the node.
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
                declarator is not None and has_placeholder_parameter(declarator.child_by_field("parameters"))
            ):
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
    """Return True when a function or a lambda encloses a node."""
    return node.ancestor_of_type(*FUNCTION_BODIES) is not None


def reflection_function(call: tsast.Node) -> str:
    """Return the name of the reflection function that a call calls, or "" for each other call.

    The name counts for a callee with the qualifier std::meta, for a
    std::define_static_* function, and for a name of REFLECTION_FUNCTIONS
    with no qualifier.
    """
    callee = call.child_by_field("function")
    parts = None if callee is None else tsast.qualified_parts(callee)
    if parts is None or not parts[1]:
        return ""
    names = parts[1]
    is_reflection = "meta" in names[:-1] or (names[-1] in REFLECTION_FUNCTIONS and names[:-1] in ((), ("std",)))
    return "::".join(names) if is_reflection else ""


def takes_reflection(call: tsast.Node) -> bool:
    """Return True when a call takes a reflection `^^X` as an argument or as a template argument.

    The body of a lambda that the call calls is not an argument.  The
    object of a member call is not an argument too: in
    `members_of(^^T).size()` the inner call takes the reflection.
    """
    callee = call.child_by_field("function")
    if callee is not None and callee.type == "field_expression":
        callee = callee.child_by_field("field")
    holders = [call.child_by_field("arguments"), callee]
    return any(
        holder is not None
        and holder.type != "lambda_expression"
        and next(holder.descendants("reflect_expression"), None) is not None
        for holder in holders
    )


def reflection_work(node: tsast.Node) -> str:
    """Return a phrase that names the first reflection query of a constant evaluation, or "" when it has none.

    The walk reads the evaluated operands only.  It goes into the body of a
    lambda that the node calls, and into no different lambda body.  An
    expansion statement in the node is an item of its own, and the walk
    does not go into it.

    Complexity: linear in the number of nodes of the subtree.

    Args:
        node: An initializer, a default member value or the type of an alias
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
    """Return True when a declaration has one of the given specifiers or qualifiers directly, such as constexpr."""
    return any(
        child.type in ("type_qualifier", "storage_class_specifier") and tsast.spelled(child) in words
        for child in declaration.children
    )


def is_const_object(declaration: tsast.Node, declarator: tsast.Node) -> bool:
    """Return True for `const T x`, `const T x[]` and `T* const x`: the object itself is const.

    Args:
        declaration: The declaration or field_declaration
        declarator: The declarator in its init_declarator, or the declarator of a field
    """
    current: tsast.Node | None = declarator
    while current is not None and current.type == "array_declarator":
        current = current.child_by_field("declarator")
    if current is None or current.type == "reference_declarator":
        return False
    if current.type == "pointer_declarator":
        return any(child.type == "type_qualifier" and tsast.spelled(child) == "const" for child in current.children)
    return has_qualifier(declaration, "const")


def owner_parts(node: tsast.Node) -> tuple[str, ...]:
    """Return the full name of the scope that holds a node: its namespaces, then its function or its classes.

    A node in a lambda that no function holds gets the segment `<lambda>`.
    The segments come outermost first.
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
    """Return True when each ancestor of a declaration is namespace scope or an explicit specialization.

    Complexity: linear in the depth of the node.
    """
    owner = node.parent
    while owner is not None:
        if owner.type not in NAMESPACE_SCOPE and owner.type != "template_declaration":
            return False
        owner = owner.parent
    return True


def declared_name(target: tsast.Node) -> str:
    """Return the name that a declarator declares, or its spelling when the parse tree gives no name."""
    return tsast.leaf_name(target) or tsast.spelled(target)


def eager_sites(tree: tsast.Tree) -> list[tuple[tsast.Node, str, str]]:
    """Return each reflection walk of a header that runs where the header stands, as (node, key, query).

    A declaration in an expansion statement belongs to the walk of that
    statement, and it is not a site of its own.  The sites come in the
    sequence of the source.

    Complexity: linear in the number of nodes of the file, times the depth of each candidate.
    """
    sites: list[tuple[tsast.Node, str, str]] = []
    for declaration in tree.find("declaration"):
        is_constant = has_qualifier(declaration, "constexpr", "constinit")
        if not is_constant and not has_qualifier(declaration, "const"):
            continue
        pairs = [
            (item.child_by_field("declarator"), item.child_by_field("value"))
            for item in declaration.children
            if item.field == "declarator" and item.type == "init_declarator"
        ]
        items = [(target, value) for target, value in pairs if target is not None and value is not None]
        if not items:
            continue
        is_local = is_in_function_body(declaration)
        if (
            (is_local and not is_constant)
            or (not is_local and not is_at_eager_scope(declaration))
            or declaration.ancestor_of_type("expansion_statement") is not None
            or is_in_template_context(declaration)
        ):
            continue
        for target, value in items:
            query = reflection_work(value) if is_constant or is_const_object(declaration, target) else ""
            if query:
                sites.append((target, "::".join((*owner_parts(declaration), declared_name(target))), query))
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
                    sites.append((declared, "::".join((*owner_parts(member), declared_name(declared))), query))
    for alias in tree.find("alias_declaration", "type_definition"):
        written = alias.child_by_field("type")
        query = "" if written is None else reflection_work(written)
        if not query or alias.ancestor_of_type("expansion_statement") is not None or is_in_template_context(alias):
            continue
        named = (
            alias.child_by_field("name") if alias.type == "alias_declaration" else alias.child_by_field("declarator")
        )
        name = "" if named is None else declared_name(named)
        sites.append((alias, "::".join((*owner_parts(alias), name)), query))
    for expansion in tree.find("expansion_statement"):
        if is_in_template_context(expansion):
            continue
        source = expansion.child_by_field("right")
        shown = tsast.spelled(source) if source is not None else tsast.spelled(expansion)
        sites.append(
            (expansion, "::".join(owner_parts(expansion)) + f"::template for({shown})", "an expansion statement")
        )
    sites.sort(key=lambda site: site[0].start)
    return sites


def static_definer(call: tsast.Node) -> str:
    """Return std::define_static_array or std::define_static_string when a call calls one, also through ADL, or ""."""
    callee = call.child_by_field("function")
    parts = None if callee is None else tsast.qualified_parts(callee)
    if parts is None or not parts[1] or parts[1][-1] not in STATIC_DEFINERS or parts[1][:-1] not in ((), ("std",)):
        return ""
    return f"std::{parts[1][-1]}"


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
    """Return True when a node, or a name below it, is one of the names.

    A member name after `.` or `->` is a field_identifier, which the walk
    does not read.  A template parameter in front of `::` reads as a
    namespace_identifier, which the walk reads.  Complexity: linear in the
    number of nodes of the subtree.
    """
    if node is None or not names:
        return False
    leaves = ("identifier", "type_identifier", "namespace_identifier")
    if node.type in leaves and tsast.spelled(node) in names:
        return True
    return any(tsast.spelled(leaf) in names for leaf in node.descendants(*leaves))


def is_placeholder(written: tsast.Node | None) -> bool:
    """Return True when a written type is a placeholder, such as `auto`."""
    return written is not None and written.type == "placeholder_type_specifier"


def add_parameters(parameters: tsast.Node | None, dependence: Dependence) -> None:
    """Add the names of a template parameter list to a dependence.

    A type parameter has a dependent type, and a value parameter of a
    placeholder type has one too.  Each other value parameter has a
    dependent value.
    """
    if parameters is None:
        return
    for parameter in tsast.non_comment_children(parameters):
        name = parameter_name(parameter)
        if not name:
            continue
        dependence.names.add(name)
        if parameter.type in TYPE_PARAMETERS or (
            parameter.type in VALUE_PARAMETERS and is_placeholder(parameter.child_by_field("type"))
        ):
            dependence.typed.add(name)


def declared_items(declaration: tsast.Node) -> list[tuple[str, tsast.Node | None]]:
    """Return (name, initializer) for each declarator of a declaration."""
    items: list[tuple[str, tsast.Node | None]] = []
    for child in declaration.children:
        if child.field != "declarator":
            continue
        if child.type == "init_declarator":
            target = child.child_by_field("declarator")
            items.append(((tsast.leaf_name(target) or "") if target is not None else "", child.child_by_field("value")))
        else:
            items.append((tsast.leaf_name(child) or "", None))
    return [(name, value) for name, value in items if name]


def alias_name(alias: tsast.Node) -> str:
    """Return the name that an alias_declaration or a type_definition declares, or ""."""
    named = alias.child_by_field("name") if alias.type == "alias_declaration" else alias.child_by_field("declarator")
    return "" if named is None else tsast.leaf_name(named) or ""


def add_class_members(holder: tsast.Node, dependence: Dependence) -> None:
    """Add each member alias and static data member of a class with a type or an initializer that names a dependent name."""
    body = holder.child_by_field("body")
    for member in [] if body is None else body.children:
        if member.type in ("alias_declaration", "type_definition"):
            name = alias_name(member)
            if name and refers_to(member.child_by_field("type"), dependence.names):
                dependence.names.add(name)
                dependence.typed.add(name)
        elif member.type == "field_declaration":
            written = member.child_by_field("type")
            declared = member.child_by_field("declarator")
            name = "" if declared is None else tsast.leaf_name(declared) or ""
            value = member.child_by_field("default_value")
            is_typed = (
                is_type_dependent(value, dependence)
                if is_placeholder(written)
                else refers_to(written, dependence.names)
            )
            if name and (is_typed or refers_to(value, dependence.names)):
                dependence.names.add(name)
            if name and is_typed:
                dependence.typed.add(name)


def add_function_names(scope: tsast.Node, node: tsast.Node, dependence: Dependence) -> None:
    """Add each parameter, local and alias of a function or a lambda before a node that names a dependent name."""
    for name, parameter in tsast.parameters(scope) if scope.type == "function_definition" else []:
        if name and (
            is_placeholder(parameter.child_by_field("type"))
            or refers_to(parameter.child_by_field("type"), dependence.names)
        ):
            dependence.names.add(name)
            dependence.typed.add(name)
    declarator = scope.child_by_field("declarator") if scope.type == "lambda_expression" else None
    listed = None if declarator is None else declarator.child_by_field("parameters")
    for parameter in [] if listed is None else listed.children_of_type(*VALUE_PARAMETERS):
        name = parameter_name(parameter)
        if name and (
            is_placeholder(parameter.child_by_field("type"))
            or refers_to(parameter.child_by_field("type"), dependence.names)
        ):
            dependence.names.add(name)
            dependence.typed.add(name)
    body = scope.child_by_field("body")
    if body is None:
        return
    for item in body.descendants(
        "declaration", "alias_declaration", "type_definition", "for_range_loop", "expansion_statement"
    ):
        if item.start >= node.start:
            break
        if item.type in ("alias_declaration", "type_definition"):
            name = alias_name(item)
            if name and refers_to(item.child_by_field("type"), dependence.names):
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
                is_typed = (
                    is_type_dependent(value, dependence)
                    if is_placeholder(written)
                    else refers_to(written, dependence.names)
                )
                if is_typed or refers_to(value, dependence.names):
                    dependence.names.add(name)
                if is_typed:
                    dependence.typed.add(name)


def dependence_at(node: tsast.Node) -> Dependence:
    """Return the names in scope at a node that a template parameter changes.

    The walk reads the scopes that hold the node, in the sequence of the
    source:
      - the template parameters of each template declaration and generic
        lambda, and each parameter of a placeholder type
      - each member alias and static data member of a class, when its type
        or its initializer names a counted name
      - each parameter, local and alias of a function or a lambda before
        the node, when its type or its initializer names a counted name.

    Complexity: linear in the nodes of the classes and the functions that hold the node, times the depth of the node.
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
        add_class_members(holder, dependence)
    for scope in reversed(scopes):
        add_function_names(scope, node, dependence)
    return dependence


def is_type_dependent(node: tsast.Node | None, dependence: Dependence) -> bool:
    """Return True when an expression has a dependent type, by its syntax.

    A reflection `^^X` has the type std::meta::info.  The template parameter
    that it names makes its value dependent and not its type.  A call has a
    dependent type in three conditions:
      - its callee names a dependent name in a template argument list
      - its object or its callee has a dependent type
      - an argument has a dependent type.
    A cast or an initializer list has a dependent type when its type names a
    dependent name.

    Complexity: linear in the number of nodes of the subtree.
    """
    if node is None:
        return False
    kind = node.type
    if kind in LITERALS or kind in (
        "reflect_expression",
        "lambda_expression",
        "sizeof_expression",
        "alignof_expression",
        "noexcept_expression",
        "requires_expression",
        "this",
    ):
        return False
    if kind == "identifier":
        return refers_to(node, dependence.typed)
    if kind == "qualified_identifier":
        return refers_to(node, dependence.typed) or any(
            refers_to(arguments, dependence.names) for arguments in node.descendants("template_argument_list")
        )
    if kind == "call_expression":
        callee = node.child_by_field("function")
        if callee is not None and callee.type == "field_expression":
            if is_type_dependent(callee.child_by_field("argument"), dependence):
                return True
        elif callee is not None and (
            any(refers_to(arguments, dependence.names) for arguments in callee.descendants("template_argument_list"))
            or is_type_dependent(callee, dependence)
        ):
            return True
        arguments = node.child_by_field("arguments")
        return arguments is not None and any(
            is_type_dependent(argument, dependence) for argument in tsast.non_comment_children(arguments)
        )
    if kind == "template_function":
        return any(refers_to(arguments, dependence.names) for arguments in node.descendants("template_argument_list"))
    if kind in ("compound_literal_expression", "cast_expression"):
        return refers_to(node.child_by_field("type"), dependence.names)
    if kind in ("field_expression", "subscript_expression"):
        return is_type_dependent(node.child_by_field("argument"), dependence)
    return any(is_type_dependent(child, dependence) for child in tsast.non_comment_children(node))


def instance_sites(tree: tsast.Tree) -> list[tuple[tsast.Node, str, str]]:
    """Return each call of a static definer that GCC instantiates where the header stands, as (call, key, reason).

    Complexity: linear in the number of calls, times the nodes of the functions that hold each definer call.
    """
    sites: list[tuple[tsast.Node, str, str]] = []
    for call in tree.find("call_expression"):
        if not static_definer(call):
            continue
        if not is_in_template_context(call):
            reason = "a call that no template holds"
        else:
            dependence = dependence_at(call)
            arguments = call.child_by_field("arguments")
            listed = [] if arguments is None else tsast.non_comment_children(arguments)
            if any(is_type_dependent(argument, dependence) for argument in listed):
                continue
            reason = "no argument has a dependent type"
        sites.append((call, "::".join((*owner_parts(call), tsast.spelled(call))), reason))
    return sites


def has_consteval(function: tsast.Node) -> bool:
    """Return True when a function definition has the consteval specifier."""
    return any(not child.children and tsast.spelled(child) == "consteval" for child in function.children)


def constant_functions(tree: tsast.Tree) -> tuple[set[str], set[str]]:
    """Return the names that a header declares as constexpr or consteval functions, and as other functions.

    A name of the second set and not of the first names a function that no
    constant evaluation can call.  An example is the stub that a refusal
    calls to stop a constant evaluation.
    """
    constant: set[str] = set()
    plain: set[str] = set()
    for node in tree.find("function_definition", "declaration", "field_declaration"):
        declarator = node.child_by_field("declarator")
        if (
            declarator is None
            or next(
                iter(
                    [declarator]
                    if declarator.type == "function_declarator"
                    else declarator.descendants("function_declarator")
                ),
                None,
            )
            is None
        ):
            continue
        name = tsast.leaf_name(declarator)
        if not name:
            continue
        words = {tsast.spelled(child) for child in node.children if not child.children}
        (constant if words & {"constexpr", "consteval"} else plain).add(name)
    return constant, plain


def is_constant(node: tsast.Node, declared: set[str]) -> bool:
    """Return True when an expression is constant by its syntax: literals, reflections, outer names and their calls."""
    kind = node.type
    if kind in LITERALS or kind in ("reflect_expression", "sizeof_expression", "alignof_expression"):
        return True
    if kind == "identifier":
        return tsast.spelled(node) not in declared
    if kind == "qualified_identifier":
        return True
    if kind == "call_expression":
        return named_call(node, declared) is not None
    if kind in (
        "parenthesized_expression",
        "binary_expression",
        "unary_expression",
        "conditional_expression",
        "initializer_list",
        "compound_literal_expression",
    ):
        return all(is_constant(child, declared) for child in tsast.non_comment_children(node) if child.field != "type")
    return False


def named_call(call: tsast.Node, declared: set[str]) -> tuple[str, ...] | None:
    """Return the name parts of a call of a named function with only constant arguments, or None.

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
    if arguments is None or not all(
        is_constant(argument, declared) for argument in tsast.non_comment_children(arguments)
    ):
        return None
    return parts[1]


def report_exits(tree: tsast.Tree | None) -> frozenset[str]:
    """Return the names of the functions that the Report header declares without constexpr or consteval.

    No constant evaluation can call such a function, and GCC folds no call
    of one.  With no tree, the set is empty, and each call of an exit
    counts.
    """
    if tree is None:
        return frozenset()
    constant, plain = constant_functions(tree)
    return frozenset(plain - constant)


def names_declared(scope: tsast.Node) -> set[str]:
    """Return each name that a function or a class declares, at each depth.

    The set can hold more names than the function or the class declares.
    A call that it makes not constant is then a call that the compiler does
    not fold too.
    """
    declared: set[str] = set()
    for declared_parameter, _parameter in tsast.parameters(scope) if scope.type == "function_definition" else []:
        declared.add(declared_parameter)
    for node in scope.descendants(*DECLARING):
        for part in ("declarator", "name"):
            child = node.child_by_field(part)
            name = None if child is None else tsast.leaf_name(child)
            if name:
                declared.add(name)
        if node.type in ("structured_binding_declarator", "using_declaration", "template_parameter_list"):
            declared.update(tsast.spelled(leaf) for leaf in node.descendants(*NAME_LEAVES))
    return declared


def is_folded(call: tsast.Node, body: tsast.Node) -> bool:
    """Return True when GCC folds a call where it reads the body, and no other kind counts the call.

    A call in an unevaluated operand is not evaluated.  A call in a
    static_assert, in an expansion statement or in the initializer of a
    constexpr or constinit local belongs to a different kind.
    """
    owner = call.parent
    while owner is not None and owner != body:
        if owner.type in UNEVALUATED - {"lambda_expression"} or owner.type in (
            "static_assert_declaration",
            "expansion_statement",
        ):
            return False
        if owner.type == "declaration" and has_qualifier(owner, "constexpr", "constinit"):
            return False
        owner = owner.parent
    return True


def is_inside_constant_call(call: tsast.Node, body: tsast.Node, declared: set[str]) -> bool:
    """Return True when a constant call is an operand of a larger constant call, which counts in its position."""
    owner = call.parent
    while owner is not None and owner != body and owner.type in OPERAND_HOLDERS:
        owner = owner.parent
    return owner is not None and owner.type == "call_expression" and named_call(owner, declared) is not None


def fold_sites(tree: tsast.Tree, exits: frozenset[str]) -> list[tuple[tsast.Node, str, str]]:
    """Return each call that GCC evaluates where the header stands, in a consteval function that is not a template.

    A call of an exit of the Report family (report_exits()) through fixy or
    foundation::core is the call of a stub, and it does not count.  The
    sites are (call, key, reason), in the sequence of the source.

    Complexity: linear in the nodes of the consteval functions of the header.
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
                    declared.update(
                        tsast.spelled(leaf)
                        for leaf in [child, *child.descendants("identifier")]
                        if leaf.type == "identifier"
                    )
        holder = function.ancestor_of_type(*CLASS_SPECIFIERS)
        while holder is not None:
            declared |= names_declared(holder)
            holder = holder.ancestor_of_type(*CLASS_SPECIFIERS)
        for call in body.descendants("call_expression"):
            parts = named_call(call, declared)
            if (
                parts is None
                or not is_folded(call, body)
                or is_inside_constant_call(call, body, declared)
                or is_in_template_context(call)
            ):
                continue
            arguments = call.child_by_field("arguments")
            has_arguments = arguments is not None and bool(tsast.non_comment_children(arguments))
            is_exit = tuple(parts[:-1]) in REPORT_QUALIFIERS and parts[-1] in exits
            if has_arguments and (reflection_function(call) or takes_reflection(call)):
                reason = "a reflection query with constant arguments"
            elif not has_arguments and parts[0] != "std" and parts[-1] not in stubs and not is_exit:
                reason = "a call with no argument"
            else:
                continue
            sites.append((call, "::".join((*owner_parts(call), tsast.spelled(call))), reason))
    return sites


def tests_literal_true(texts: Sequence[str]) -> bool:
    """Return True when the tokens of a static_assert, from its keyword on, test only the literal `true`."""
    return len(texts) >= 4 and texts[1] == "(" and texts[2] == "true" and texts[3] in (",", ")")


def macro_writes(body: tsast.MacroBody) -> MacroWrites:
    """Read what the replacement list of one `#define` writes at namespace scope.

    A `{` after `namespace NAME` or after `extern "STRING"` opens a namespace
    scope.  Each other `{` opens a class, a function, a block or an
    initializer list.  A self-test namespace counts at its `{`, and a check
    in it counts with it.  A parameter of the macro is not a use of a
    different macro.

    Complexity: linear in the number of tokens of the replacement list.
    """
    tokens = tsast.pp_tokens(body.text, body.first_row)
    texts = [tsast.lexeme(token) for token in tokens]
    parameters = frozenset(body.params)
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
        elif (
            text == "extern"
            and index + 2 < len(tokens)
            and tokens[index + 1].kind == "string"
            and texts[index + 2] == "{"
        ):
            opening = "namespace"
            index += 2
            continue
        elif text == "static_assert":
            asserts += is_at_scope and not tests_literal_true(texts[index : index + 4])
        elif tokens[index].kind == "identifier" and is_at_scope and text not in parameters:
            uses.append((text, index + 1 < len(tokens) and texts[index + 1] == "("))
        index += 1
    return MacroWrites(namespaces, asserts, tuple(uses))


def macro_table(trees: Sequence[tsast.Tree], bodies: Sequence[tsast.MacroBody]) -> dict[str, MacroChecks]:
    """Return the checks that one expansion of each macro of the headers writes, for each macro that writes one.

    A macro that names a different macro adds the checks of that macro.  A
    function-like macro adds them only when `(` comes after its name.  A
    name with a `#define` in two headers takes the larger count of each
    kind.  A chain that names a macro again stops there, as the preprocessor
    stops.  A `#define` with no replacement list writes no check, but it can
    make its name function-like.

    Complexity: linear in the tokens of the replacement lists, times the depth of the longest chain of macros.
    """
    function_like: dict[str, bool] = {}
    for tree in trees:
        for define in tree.find("preproc_def", "preproc_function_def"):
            named = define.child_by_field("name")
            name = None if named is None else tsast.leaf_name(named)
            if name:
                function_like[name] = function_like.get(name, False) or define.type == "preproc_function_def"
    writes: dict[str, list[MacroWrites]] = {}
    for body in bodies:
        if body.name:
            writes.setdefault(body.name, []).append(macro_writes(body))
    memo: dict[str, MacroChecks] = {}

    def total(name: str, active: frozenset[str]) -> MacroChecks:
        """Return the checks of one macro, with the macros that it names."""
        if name in memo:
            return memo[name]
        namespaces = 0
        asserts = 0
        for written in writes[name]:
            own_namespaces, own_asserts = written.namespaces, written.asserts
            for used, is_called in written.uses:
                if used in active or used not in writes:
                    continue
                if function_like.get(used, False) and not is_called:
                    continue
                inner = total(used, active | {used})
                own_namespaces += inner.namespaces
                own_asserts += inner.asserts
            namespaces = max(namespaces, own_namespaces)
            asserts = max(asserts, own_asserts)
        memo[name] = MacroChecks(namespaces, asserts)
        return memo[name]

    table = {name: total(name, frozenset({name})) for name in writes}
    return {name: checks for name, checks in table.items() if checks.namespaces or checks.asserts}


def macro_sites(tree: tsast.Tree) -> list[tuple[tsast.Node, str]]:
    """Return each invocation of a macro that can stand at namespace scope, with the name of the macro.

    The kit reads `NAME(...)` at namespace scope as a macro invocation, and
    `NAME;` as an expression statement.  Each site is the invocation or its
    statement.
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


def invocation_key(site: tsast.Node) -> str:
    """Return the key of the items that an invocation of a macro makes: its spelling, with no last `;`."""
    tokens = site.tokens()
    return tsast.spelled_tokens(tokens[:-1] if tokens and tokens[-1] == ";" else tokens)


def is_in_self_test(node: tsast.Node) -> bool:
    """Return True when a self-test namespace encloses a node, and the node then counts with that namespace."""
    return any(is_self_test_segment(segment) for segment in tsast.namespace_path(node))


def namespace_items(tree: tsast.Tree, path: str) -> tuple[list[Item], list[check_report.Finding]]:
    """Return each self-test namespace of a header, and an error for each namespace with a name that the tree hides."""
    items: list[Item] = []
    unread: list[check_report.Finding] = []
    for node in tree.find("namespace_definition"):
        name = node.child_by_field("name")
        if name is None:
            continue
        parts = tsast.qualified_parts(name)
        if parts is None:
            unread.append(
                check_report.Finding(
                    "error",
                    path,
                    node.line,
                    RULE,
                    f"the rule cannot read the name {tsast.spelled(name)} of the namespace, and it cannot tell if it "
                    f"is a self-test namespace.  Write the name as identifiers and `::`",
                    UNREAD,
                    tsast.spelled(name),
                )
            )
            continue
        if is_in_self_test(node):
            continue
        if any(is_self_test_segment(segment) for segment in parts[1]):
            items.append(Item(path, node.line, NAMESPACE, "::".join((*tsast.namespace_path(node), *parts[1]))))
    return items, unread


def header_items(
    tree: tsast.Tree, macros: dict[str, MacroChecks], exits: frozenset[str]
) -> tuple[list[Item], list[check_report.Finding]]:
    """Return each item of compile-time work that one header does in each includer (THE SIX KINDS).

    The second list holds an error for each namespace with a name that the
    rule cannot read.

    Complexity: linear in the number of nodes of the file, times the depth of each candidate.
    """
    path = tree.path.as_posix()
    items, unread = namespace_items(tree, path)
    for node in tree.find("static_assert_declaration"):
        condition = node.child_by_field("condition")
        key = tsast.spelled(condition if condition is not None else node)
        if key == "true":
            continue
        if is_at_namespace_scope(node):
            if not is_in_self_test(node):
                items.append(Item(path, node.line, ASSERT, key))
        elif is_in_function_body(node) and not is_in_template_context(node):
            items.append(Item(path, node.line, FUNCTION_ASSERT, key))
    for site, name in macro_sites(tree):
        made = macros.get(name)
        if made is None or not is_at_namespace_scope(site) or is_in_self_test(site):
            continue
        key = invocation_key(site)
        items += [Item(path, site.line, NAMESPACE, key, name, made.namespaces)] * made.namespaces
        items += [Item(path, site.line, ASSERT, key, name, made.asserts)] * made.asserts
    for kind, found in ((EAGER, eager_sites(tree)), (INSTANCE, instance_sites(tree)), (FOLD, fold_sites(tree, exits))):
        items += [
            Item(path, site.line, kind, key, detail=detail) for site, key, detail in found if not is_in_self_test(site)
        ]
    return items, unread


def check_file_of(header: str) -> str:
    """Return the check file of a header: test/layer/checks/<layer>/<path>.cpp for include/<layer>/<path>.h."""
    inner = PurePosixPath(header).relative_to(INCLUDE_DIRECTORY)
    return (PurePosixPath(CHECKS) / inner.with_suffix(CHECK_SUFFIX)).as_posix()


def describe(item: Item) -> str:
    """Return the sentence that names one item, its key, its cost and its repair."""
    target = check_file_of(item.path)
    if item.via:
        made = "self-test namespace(s)" if item.kind == NAMESPACE else "namespace-scope static_assert(s)"
        return (
            f"the invocation {item.key} of the macro {item.via} makes {item.made} {made}, and each one runs again "
            f"in each translation unit that includes this header.  Move each check that the macro writes to "
            f"{target}"
        )
    if item.kind == NAMESPACE:
        return (
            f"the self-test namespace {item.key} runs again in each translation unit that includes this header.  "
            f"Move it to {target}"
        )
    if item.kind == ASSERT:
        return (
            f"the namespace-scope static_assert({item.key}) runs again in each translation unit that includes "
            f"this header.  Move it to {target}"
        )
    if item.kind == FUNCTION_ASSERT:
        return (
            f"the static_assert({item.key}) in the body of a function that is not a template runs again in each "
            f"translation unit that includes this header.  Move it to {target}, or make the function a template"
        )
    if item.kind == INSTANCE:
        return (
            f"the call {item.key} instantiates its function in each translation unit that includes this header "
            f"({item.detail}).  Give the argument a dependent type of the template that holds it, with "
            f"foundation/reflect/Anchor.h, or move the call into a template"
        )
    if item.kind == FOLD:
        return (
            f"the call {item.key} runs in each translation unit that includes this header, because the "
            f"consteval function that holds it is not a template ({item.detail}).  Make the function a template "
            f"that its callers name with a dependent argument, or move it to {target}"
        )
    return (
        f"the reflection walk of {item.key} ({item.detail}) runs again in each translation unit that includes this "
        f"header.  Make it a variable template or a member of a template, or move it to {target}"
    )


def item_finding(item: Item) -> check_report.Finding:
    """Return the error of one item, with its key as the entity (THE ENTITY)."""
    return check_report.Finding(
        "error", item.path, item.line, RULE, f"{describe(item)}.  {REPAIR}", item.kind, item.key
    )


def first_include(tree: tsast.Tree) -> str | None:
    """Return the path that the first line of code of a file includes, with no delimiters, or None for no such include."""
    items = tsast.non_comment_children(tree.root)
    if not items or items[0].type != "preproc_include":
        return None
    target = items[0].child_by_field("path")
    if target is None or target.type not in ("system_lib_string", "string_literal"):
        return None
    return tsast.prose_text(target).strip()[1:-1].strip()


def header_of_check(check_file: str, listed: frozenset[str]) -> str | None:
    """Return the header of a check file that the tree holds, or None when the tree has no header of that name."""
    inner = PurePosixPath(check_file).relative_to(CHECKS)
    for suffix in HEADER_SUFFIXES:
        header = (PurePosixPath(INCLUDE) / inner.with_suffix(suffix)).as_posix()
        if header in listed:
            return header
    return None


def check_file_findings(context: TsastContext) -> list[check_report.Finding]:
    """Return an error for each file in test/layer/checks/ that is not the check file of a header.

    A file that does not parse gives the error of the kind parse of the
    runner.  Complexity: one list of the directory and one load of each
    check file.

    Raises:
        NotApplicable: If the root is not a git work tree
    """
    try:
        files = git.tracked_files(context.root, (CHECKS,), with_new=True)
    except git.GitError as problem:
        raise NotApplicable(f"the tree {context.root} is not a git work tree: {problem}") from None
    listed = frozenset(context.listed())
    found: list[check_report.Finding] = []
    expected: dict[str, str] = {}
    for path in files:
        if not path.endswith(CHECK_SUFFIX):
            found.append(
                check_report.Finding(
                    "error",
                    path,
                    0,
                    RULE,
                    f"a check file is a .cpp file: {CHECKS}<layer>/<path>.cpp for "
                    f"include/<layer>/<path>.h.  Rename the file, or move it out of {CHECKS}",
                    CHECK_FILE,
                )
            )
            continue
        header = header_of_check(path, listed)
        if header is None:
            shown = (PurePosixPath(INCLUDE) / PurePosixPath(path).relative_to(CHECKS).with_suffix(".h")).as_posix()
            found.append(
                check_report.Finding(
                    "error",
                    path,
                    0,
                    RULE,
                    f"the tree has no header {shown}.  A check file belongs to one header.  Move it with its header, "
                    f"or delete it",
                    CHECK_FILE,
                )
            )
            continue
        expected[path] = PurePosixPath(header).relative_to(INCLUDE_DIRECTORY).as_posix()
    for tree in context.trees(sorted(expected)):
        path = tree.path.as_posix()
        included = first_include(tree)
        if included != expected[path]:
            shown = f"<{included}>" if included is not None else "no written include path"
            found.append(
                check_report.Finding(
                    "error",
                    path,
                    0,
                    RULE,
                    f"the first line of code includes {shown}, not <{expected[path]}>.  The first line of code of a "
                    f"check file includes its own header, and the file then shows that the header compiles alone",
                    CHECK_FILE,
                )
            )
    return found


def ledger_findings(context: TsastContext) -> list[check_report.Finding]:
    """Return an error for each row of the ledger that no item of a header can match, and each configuration row.

    A ledger that does not obey the rows grammar gives the error of
    judged() of assay.guard.engines.meta.runner.  This function then reads
    no row of it.
    """
    rule = context.rule or rule_ledger.rule_of(RULE)
    try:
        ledger = rule_ledger.read(rule, context.root)
    except rows_grammar.RowsError, OSError:
        return []
    shown = rule_ledger.shown_path(RULE)
    found = [
        check_report.Finding(
            "error",
            shown,
            0,
            RULE,
            f"the ledger names the configuration {item.name}, and the ledger of {RULE} has no "
            f"configuration.  Remove the configuration rows and each difference row",
            LEDGER_KIND,
            item.name,
        )
        for item in ledger.configurations
    ]
    for key, entry in sorted(ledger.base.items(), key=lambda pair: pair[1].line):
        if key.kind not in KINDS or not key.path.startswith(INCLUDE):
            problem = (
                f"the row of {key.path or 'no path'} has the kind {key.kind or 'none'}.  A row has one of the kinds "
                f"{', '.join(KINDS)}, and a path in {INCLUDE}"
            )
        elif not entry.reason:
            problem = (
                f"the row of {key.path} ({key.kind} {key.entity or 'with no key'}) gives no reason.  Say why the "
                f"result of the item changes with the translation unit that includes the header"
            )
        else:
            continue
        found.append(
            check_report.Finding(
                "error", shown, entry.line, RULE, problem, LEDGER_KIND, f"{key.path} {key.kind} {key.entity}"
            )
        )
    return found


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Give an error for each item of work of each header, each bad check file and each bad ledger row.

    The rule reads each header of the scope one time, and it makes the
    macro table of all headers.  Then it reads the items of each header.
    """
    found: list[check_report.Finding] = []
    if not (context.root / INCLUDE_DIRECTORY).is_dir():
        found.append(
            check_report.Finding(
                "error",
                INCLUDE_DIRECTORY,
                0,
                RULE,
                f"the tree has no directory {INCLUDE}, and the rule has no header to read",
                UNREAD,
                INCLUDE_DIRECTORY,
            )
        )
    headers = [path for path in context.scope_files() if path.endswith(HEADER_SUFFIXES)]
    trees = list(context.trees(headers))
    macros = macro_table(trees, context.macro_bodies(trees))
    exits = report_exits(next((tree for tree in trees if tree.path.as_posix() == REPORT_HEADER), None))
    for tree in trees:
        items, unread = header_items(tree, macros, exits)
        found += unread
        found += [item_finding(item) for item in items]
    found += check_file_findings(context)
    found += ledger_findings(context)
    return sorted(found, key=lambda finding: (finding.path, finding.line, finding.kind, finding.entity))
