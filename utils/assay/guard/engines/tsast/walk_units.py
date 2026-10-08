"""walk-units — the list of the walk units names each check file that can walk a namespace that a header opens.

A check file (test/layer/checks/LAYER/PATH.cpp) holds the compile-time
checks of one header.  It includes its header and the includes of that
header, and nothing more.  A reflection walk over a namespace,
std::meta::members_of(^^name), reads the members that the translation unit
declares before the walk runs.  Then a walk in a check file does not see a
member that a different header adds to the namespace that it walks.
test/layer/walks_across_headers.cpp compiles such a check file a second time,
after each header that can add to such a namespace.  These are the walk
units.  test/layer/walk-checks.txt, the list, names the check files that the
walk units compile.  This rule derives the list from the parse trees, and it
compares the list with the tree.  `python3 utils/bin/loop derive walk-units
--write` writes the list again (assay.loop.derive.walk_units).

THE RULE
    A walk is a call of members_of on the reflection of a namespace.  A
    check file joins the walk units when one of its checks can walk a
    namespace that a header in include/ opens.  Each header can add a member
    to such a namespace, also a header of a change that comes after this run.
    A walk over a class, and a walk over a namespace that only check files
    open, do not make a check file join.  The rule refuses:
      `missing-row`    a check file that joins, when the list has no row for it
      `other-row`      a row with namespaces that are not the namespaces that
                       the tree gives
      `extra-row`      a row of a file that does not join, or that is not a
                       check file
      `malformed-row`  a row that does not have the shape of THE LIST, also a
                       weight that is not a number with one decimal
      `second-row`     a second row of one check file
      `no-list`        a tree with no list
      `free-operator`  a free operator that can walk a namespace (THE WALKS)
      `parse`          a header or a check file that the kit cannot parse.
                       The analysis cannot read the walks of such a file
    It gives a warning:
      `unmeasured`     a row with the weight unmeasured (THE WEIGHTS)
    The entity of a finding is the check file of its row, the text of a
    malformed row, or the signature of a free operator.

THE ANALYSIS
    The rule reads the parse tree of each header in include/ and of each
    check file.  It gives a summary to each function, function template,
    variable, concept, alias, class and macro:
      * the namespaces that an evaluation of it can walk.  The tag unknown
        is a walk of a namespace that the analysis cannot name.
      * the parameters of it that it can walk.  A template parameter and a
        function parameter each have a position.
    A check file is one entity, and each function that it declares is one
    more entity.

THE WALKS
    A summary starts at the calls of members_of in the body.  The first
    argument of each call gives the namespace:
      * `^^name` gives the namespace that the name resolves to, in the
        scopes that enclose the call.  A name of a class, an enumeration, a
        function, a variable, a parameter or a local gives no walk.  A name
        that the analysis cannot resolve gives unknown.
      * a parameter of the entity gives that parameter.
      * a local variable gives the walk of its initializer, and of each
        value that an assignment or a call such as push_back puts into it.
        The variable of a range loop gives the walk of its range.
      * a variable at namespace scope gives the walk of its initializer,
        for example `inline constexpr std::meta::info registry = ^^name;`.
      * members_of, define_static_array and dealias give the walk of their
        first argument.  A function of std::meta that cannot give a
        namespace, for example type_of or `substitute`, gives no walk.
      * each other expression gives unknown: parent_of, a member access,
        the result of a function of the tree, or a parameter of a lambda.
    A use calls a free operator with no name, and no class owns it.  The
    analysis then cannot see which check file gets to it.  A free operator
    with a summary that is not empty makes each check file join, and the
    rule refuses it.

THE NAMES
    The summaries flow through the names, until no summary changes.  An
    entity that names an entity with a namespace in its summary gets that
    namespace.  An entity that calls an entity with a parameter in its
    summary gets the walk of the argument at that position.  It uses the
    default argument when the call gives none, and a name with no
    arguments gives unknown.  `std::meta::substitute(^^name, {...})` gives
    the listed values as the template arguments of name.
    The analysis has no types.  It resolves a name with these rules:
      * a name with a qualifier is the entity with that full name.
      * a name with no qualifier is the entity that the innermost enclosing
        scope declares, with the using-declarations and using-directives in
        force.  A call with no qualifier that no scope resolves can get to a
        function through argument-dependent lookup.  It is then each
        function with that name that no class owns.
      * a file names only its own layer and the layers below it.  The rules
        layer-boundary and layer-residual and the layer sentinels make sure
        of that.
      * a member that a use gets to through an object, `obj.f()`, or
        through a qualifier that the analysis cannot resolve, `T::f()`, is
        a member edge.  The analysis does not know the class, and a member
        edge in a header then gives nothing.  In a check file, a member edge
        gets to each member function with that name in a class that the
        check file names.  It gives only the summary that the member gets
        from the other names.  Then a member function with a name that many
        classes use cannot move a walk from one class to a different class.

THE CLASSES
    A class template gets the walks of its own body: its static_asserts,
    its bases and its member aliases.  A use that makes an instance of the
    class runs these.  The body of a class that is not a template runs
    where the header declares it.  A declaration of a member function or of
    a friend runs nothing until a use calls the function.
    The constructors, the destructor, the operators and the conversion
    functions of a class run with no name at the use.  The kit gives each of
    these names, other than a constructor, a node of its own kind, and the
    analysis reads the kind.  A name of one token, such as operator_count,
    is an identifier, and a use calls it by that name.
    These functions give a second summary of the class, the construction
    summary.  An alias of a class gives the construction summary of the
    class.  A use that makes an object gets the construction summary:
    `C{...}`, `C(...)`, `new C`, and a declaration of an object with an
    initializer.  In a check file, each name of a class gets it.  A check
    that only completes the class, for example with sizeof, runs the
    constraints of its special members.  In a header, a name at the type of
    a declaration gives only the walks of an alias template and of a class.
    A variable at namespace scope runs where the header declares it.  An
    alias and a class that are not templates run there too.  They give no
    walk to the entities that name them.

WHY THE SHAPE IS A DERIVED LIST
    A marker in each check file is not sufficient.  A check of the markers
    must do the same analysis to find a check file that walks a namespace
    with no marker.  The marker adds a hand step and no safety.  The list is
    a derived file for that cause.  The rule derives the list again on each
    run, and it fails when the list and the tree disagree.  A new walk then
    cannot go into the tree without its row.
    The analysis fails closed on each value that it cannot read.  Each of
    these values counts as a walk of unknown, and its check file joins:
      * an unresolved name
      * a parameter of a lambda
      * a call through a value
      * a member access
      * a macro body that does not parse.

THE LIST
    test/layer/walk-checks.txt holds one row for each selected check file,
    in the rows grammar of assay.core.rows:

        <layer>/<path>.cpp | weight | namespace, namespace, ...

    The namespaces are the namespaces that the checks of the file can walk,
    each with `::` at its start, and unknown for a walk of a namespace that
    the analysis cannot name.  The rule reads each line of the list as a
    file of one line.  A line that does not obey the grammar is then one
    malformed row.

THE WEIGHTS
    The weight of a row is the share of its check file in a walk unit.  It
    counts the user instructions that the check file adds to a unit that
    holds only walk_headers.h.  Its unit is 10^9 instructions, with one
    decimal.
    test/layer/CMakeLists.txt reads the paths and the weights, and it
    divides the check files into units by weight.  `loop derive walk-units
    --measure BUILD_DIR` measures each weight again.  A weight goes stale
    when a check file or a header that it reads changes.  A stale weight
    makes units that are not equal.  When a unit then costs more than the
    warning threshold of the rule compile-instructions, the test
    compile_instructions gives a warning.  Measure again then.

WHAT THE ANALYSIS CANNOT SEE
    With no types, the analysis does not follow these paths.  A walk that
    only such a path gets to does not make a check file join:
      * a member call in a header, on an object or through a template
        parameter
      * a member call in a check file on an object of a class that the file
        does not name
      * argument-dependent lookup of a function when the usual lookup also
        finds a function with that name
      * a data member that a use reads through an object or through a
        template parameter
      * a class that a header names only as the type of a declaration.
    The kit does not preprocess.  Each arm of an #if then counts, and the
    analysis does not read a name that `##` builds.  The analysis does not
    follow a value through a function return.  It gives unknown for that
    value, and the path then makes more files join, and not fewer.

Complexity: linear in the number of nodes of the trees for the collection.  The summaries take at most the number of
tags times the number of references.
"""

import bisect
import functools
from collections.abc import Iterator
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import TypeIs

from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.tsast.runner import PARSE_KIND, TsastContext
from assay.readers import tsast

RULE = "walk-units"
LIST = "test/layer/walk-checks.txt"
INCLUDE = "include"
CHECKS = "test/layer/checks"
WRITE_COMMAND = "python3 utils/bin/loop derive walk-units --write"
MEASURE_COMMAND = "python3 utils/bin/loop derive walk-units --measure BUILD_DIR"
# The tag of a walk of a namespace that the analysis cannot name.
UNKNOWN = "unknown"
# The weight of a row that no measure gave a weight.
UNMEASURED = "unmeasured"
# The functions that walk the members of a namespace.  Only members_of takes a namespace.  The other member queries
# of std::meta take a class.
WALKS = frozenset({"members_of"})
# The functions that give the reflections of their first argument, or the members of it.
FOLLOWS_ARGUMENT = frozenset({"members_of", "define_static_array", "dealias"})
# The functions of std::meta that cannot give the reflection of a namespace.
NEVER_NAMESPACE = frozenset(
    {
        "type_of",
        "remove_cvref",
        "remove_cv",
        "remove_const",
        "remove_volatile",
        "remove_reference",
        "remove_pointer",
        "remove_extent",
        "remove_all_extents",
        "add_const",
        "add_volatile",
        "add_cv",
        "add_pointer",
        "add_lvalue_reference",
        "add_rvalue_reference",
        "decay",
        "underlying_type",
        "template_of",
        "substitute",
        "reflect_constant",
        "reflect_object",
        "reflect_function",
        "reflect_constant_array",
        "reflect_constant_string",
        "return_type_of",
        "template_arguments_of",
        "nonstatic_data_members_of",
        "static_data_members_of",
        "enumerators_of",
        "bases_of",
        "subobjects_of",
        "parameters_of",
        "annotations_of",
        "annotations_of_with_type",
        "object_of",
        "constant_of",
        "variable_of",
        "identifier_of",
        "display_string_of",
        "u8identifier_of",
        "u8display_string_of",
        "size_of",
        "alignment_of",
        "bit_size_of",
        "offset_of",
        "source_location_of",
        "can_substitute",
        "define_static_string",
        "define_static_object",
    }
)
# The start of the name of a predicate of std::meta.  A predicate cannot give the reflection of a namespace.
PREDICATE_PREFIXES = ("is_", "has_")
# The name nodes that can name an entity.
NAME_LEAVES = ("identifier", "type_identifier", "field_identifier")
CLASS_SPECIFIERS = ("class_specifier", "struct_specifier", "union_specifier")
CONTAINERS = frozenset(
    {
        "translation_unit",
        "declaration_list",
        "preproc_if",
        "preproc_ifdef",
        "preproc_else",
        "preproc_elif",
        "preproc_elifdef",
    }
)
TEMPLATE_NAMES = frozenset({"template_function", "template_type", "template_method"})
# The names that a use calls with no name: an operator, a destructor and a conversion function.
UNNAMED_CALLS = frozenset({"operator_name", "destructor_name", "operator_cast"})
# The name nodes of one token, as tsast.leaf_name() reads them.
ONE_TOKEN_NAMES = frozenset(
    {
        "identifier",
        "field_identifier",
        "type_identifier",
        "namespace_identifier",
        "statement_identifier",
        "primitive_type",
    }
)
DECLARATORS = frozenset(
    {
        "function_declarator",
        "init_declarator",
        "pointer_declarator",
        "reference_declarator",
        "array_declarator",
        "attributed_declarator",
        "parenthesized_declarator",
    }
)
# The template parameters that take a type, and the template parameters and function parameters that are a pack.
TYPE_PARAMETERS = frozenset(
    {
        "type_parameter_declaration",
        "optional_type_parameter_declaration",
        "variadic_type_parameter_declaration",
        "template_template_parameter_declaration",
    }
)
PACK_PARAMETERS = frozenset({"variadic_type_parameter_declaration", "variadic_parameter_declaration"})
LITERALS = frozenset(
    {
        "number_literal",
        "string_literal",
        "raw_string_literal",
        "concatenated_string",
        "char_literal",
        "true",
        "false",
        "nullptr",
        "user_defined_literal",
    }
)
OPERATORS = frozenset({"unary_expression", "binary_expression", "sizeof_expression", "alignof_expression"})
# The member functions that put a value into a container.
MUTATORS = frozenset(
    {"push_back", "emplace_back", "push_front", "emplace_front", "insert", "emplace", "append", "assign"}
)
DECLARED_NAME_OWNERS = frozenset(
    {
        "function_declarator",
        "init_declarator",
        "parameter_declaration",
        "optional_parameter_declaration",
        "variadic_parameter_declaration",
        "field_declaration",
        "declaration",
        "class_specifier",
        "struct_specifier",
        "union_specifier",
        "enum_specifier",
        "concept_definition",
        "alias_declaration",
        "type_definition",
        "pointer_declarator",
        "reference_declarator",
        "array_declarator",
        "attributed_declarator",
        "type_parameter_declaration",
        "optional_type_parameter_declaration",
        "variadic_type_parameter_declaration",
        "enumerator",
        "namespace_alias_definition",
        "structured_binding_declarator",
    }
)
TYPE_POSITION_ENDS = frozenset(
    {
        "declaration",
        "field_declaration",
        "parameter_declaration",
        "optional_parameter_declaration",
        "function_definition",
        "template_declaration",
        "compound_statement",
        "base_class_clause",
        "alias_declaration",
        "type_definition",
        "translation_unit",
    }
)
LAYERS = ("foundation", "fixy", "crucible")
# The depth of nested values at which the flow of an expression stops with unknown.
FLOW_DEPTH = 24
# The head comment of the list.
LIST_HEADER = (
    "# test/layer/walk-checks.txt — the check files that the walk units compile a second time.\n"
    "# `python3 utils/bin/loop derive walk-units --write` writes this file from the tree, and the\n"
    "# test guard_walk_units fails when the file and the tree disagree.  Do not edit it by hand.\n"
    "#\n"
    "# A check file joins when one of its checks can call members_of on the reflection of a\n"
    "# namespace that a header under include/ opens, because a header can add a member to it.\n"
    "# test/layer/CMakeLists.txt compiles each listed file after each header that can add to\n"
    "# such a namespace, in test/layer/walks_across_headers.cpp.\n"
    "#\n"
    "# A row:  <layer>/<path>.cpp | weight | the namespaces that the checks can walk\n"
    "#   unknown stands for a walk whose namespace the analysis cannot name.\n"
    "#   The weight is the share of the check file in a walk unit, in units of 10^9 instructions.\n"
    "#   test/layer/CMakeLists.txt divides the check files into units by weight.  --write keeps\n"
    "#   each weight, and --measure BUILD_DIR measures each weight again.\n"
)
# The kinds of the bindings of a name (Analysis.binding()).
BOUND_PARAM = "param"
BOUND_ELEMENT = "element"
BOUND_LOCAL = "local"
BOUND_OPAQUE = "opaque"
BOUND_TYPE = "type"

type NsTag = tuple[str, ...] | str
type ParamKey = tuple[str, int]


# ── The records ──────────────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Param:
    """One template parameter or function parameter of an entity.

    key is ("t", position) for a template parameter and ("f", position) for
    a function parameter or a macro parameter.  A type parameter cannot hold
    a namespace.
    """

    name: str
    key: ParamKey
    default: tsast.Node | None
    is_pack: bool
    is_type: bool


class Entity:
    """One thing that a name can get to, with its summary.

    kind is function, variable, concept, alias, class, macro or file.  The
    body is the node that the analysis reads, and skips holds the index
    ranges of the body tree that belong to a different entity.  An eager
    entity runs where the header declares it, and a name of it gives
    nothing.  A class that is not a template does not scan its body for the
    same cause, and gives only its construction summary.  own_template is
    the index of the template header that holds the template parameters of
    the entity.  own_function is the index of the function definition that
    holds its function parameters.  Each one is -1 when the entity has none.
    is_unnamed_call is True for an operator, a destructor and a conversion
    function, which a use calls with no name.
    """

    __slots__ = (
        "body",
        "constructed",
        "is_eager",
        "is_local",
        "is_unnamed_call",
        "is_unnamed_member",
        "kind",
        "leaves",
        "name",
        "names",
        "own_function",
        "own_template",
        "owner",
        "params",
        "path",
        "precise_constructed",
        "precise_params",
        "precise_walked",
        "qual",
        "scans_body",
        "skips",
        "tokens",
        "unnamed_members",
        "walk_calls",
        "walked",
        "walked_params",
    )

    def __init__(
        self,
        name: str,
        qual: tuple[str, ...],
        kind: str,
        path: str,
        body: tsast.Node | None,
        params: list[Param] | None = None,
        *,
        skips: list[tuple[int, int]] | None = None,
        is_eager: bool = False,
        is_local: bool = False,
        owner: Entity | None = None,
        is_unnamed_call: bool = False,
        scans_body: bool = True,
        own_template: int = -1,
        own_function: int = -1,
        tokens: tuple[str, ...] = (),
    ) -> None:
        """Hold the facts of one entity, with an empty summary."""
        self.name = name
        self.qual = qual
        self.kind = kind
        self.path = path
        self.body = body
        self.params: list[Param] = [] if params is None else params
        self.skips: list[tuple[int, int]] = [] if skips is None else skips
        self.is_eager = is_eager
        self.is_local = is_local
        self.owner = owner
        self.is_unnamed_call = is_unnamed_call
        self.is_unnamed_member = False
        self.scans_body = scans_body
        self.own_template = own_template
        self.own_function = own_function
        self.tokens = tokens
        self.walked: dict[NsTag, Why] = {}
        self.walked_params: dict[ParamKey, Why] = {}
        self.precise_walked: dict[NsTag, Why] = {}
        self.precise_params: dict[ParamKey, Why] = {}
        self.constructed: dict[NsTag, Why] = {}
        self.precise_constructed: dict[NsTag, Why] = {}
        self.names: frozenset[str] = frozenset()
        self.leaves: list[tuple[tsast.Node, str]] = []
        self.walk_calls: list[tsast.Node] | None = None
        self.unnamed_members: list[Entity] = []

    def size(self) -> tuple[int, int, int]:
        """Return the number of tags in each part of the summary.  Each number only increases."""
        return len(self.walked), len(self.walked_params), len(self.constructed)

    def is_skipped(self, index: int) -> bool:
        """Say if a node of the body belongs to a different entity.

        Complexity: O(log s) for s skipped ranges.
        """
        if not self.skips:
            return False
        at = bisect.bisect_right(self.skips, (index, 1 << 62)) - 1
        return at >= 0 and self.skips[at][0] <= index < self.skips[at][1]

    def nodes(self, *types: str) -> Iterator[tsast.Node]:
        """Yield each node of the body of one of the types that is not in a skipped range."""
        if self.body is None:
            return
        if self.body.type in types:
            yield self.body
        for node in self.body.descendants(*types):
            if not self.is_skipped(node.index):
                yield node

    def label(self) -> str:
        """Return the name of the entity as a report shows it."""
        return "::".join(self.qual) if self.qual else self.name

    def runs_unnamed(self, class_name: str) -> bool:
        """Say if an object of a class runs this member with no name: a constructor or an unnamed call."""
        return self.name == class_name or self.is_unnamed_call


@dataclass(frozen=True, slots=True)
class Why:
    """The first reason for one tag of a summary.

    text names the step.  entity and key name the summary that the step
    reads, or are None when the step is a call of members_of.
    """

    text: str
    entity: Entity | None = None
    key: NsTag | ParamKey | None = None


class Flow:
    """What an expression can give to a walk: namespaces, unknown, and parameters of its entity."""

    __slots__ = ("namespaces", "params")

    def __init__(self, namespaces: dict[NsTag, str] | None = None, params: dict[ParamKey, str] | None = None) -> None:
        """Hold the tags of the flow, each with its first reason."""
        self.namespaces: dict[NsTag, str] = {} if namespaces is None else namespaces
        self.params: dict[ParamKey, str] = {} if params is None else params

    def merge(self, other: Flow) -> None:
        """Add the tags of a different flow, and keep the first reason of each tag."""
        for tag, reason in other.namespaces.items():
            self.namespaces.setdefault(tag, reason)
        for key, reason in other.params.items():
            self.params.setdefault(key, reason)


def unknown(reason: str) -> Flow:
    """Return a flow that holds only the unknown tag, with its reason."""
    return Flow({UNKNOWN: reason})


@dataclass(frozen=True, slots=True)
class Binding:
    """The declaration of a name in the body of an entity (Analysis.binding()).

    kind is one of these values:
      * BOUND_PARAM for a parameter of the entity
      * BOUND_ELEMENT for the variable of a range loop
      * BOUND_LOCAL for a local variable
      * BOUND_OPAQUE for a declaration with a value that the analysis cannot read
      * BOUND_TYPE for a type parameter or a local type alias.
    values holds the range of a loop, or each value of a local.
    """

    kind: str
    param: Param | None = None
    values: tuple[tsast.Node, ...] = ()
    reason: str = ""


class Universe:
    """Everything that the analysis reads from the tree.

    namespaces maps each namespace to the files that open it.  quals holds
    the full name of each class, enumeration, enumerator, function,
    variable, alias and concept.  by_name maps a last name to the entities
    of the headers.  local maps each check file to its own entities by
    last name, and roots maps each check file to its file entity.
    """

    __slots__ = ("aliases", "by_name", "entities", "local", "namespaces", "quals", "roots", "shared_aliases", "usings")

    def __init__(self) -> None:
        """Start an empty universe."""
        self.namespaces: dict[tuple[str, ...], set[str]] = {}
        self.quals: set[tuple[str, ...]] = set()
        self.entities: list[Entity] = []
        self.by_name: dict[str, list[Entity]] = {}
        self.local: dict[str, dict[str, list[Entity]]] = {}
        self.roots: dict[str, Entity] = {}
        self.aliases: dict[str, list[tsast.NamespaceAlias]] = {}
        self.shared_aliases: list[tsast.NamespaceAlias] = []
        self.usings: dict[str, list[tsast.UsingDecl]] = {}


@dataclass(frozen=True, slots=True)
class Failure:
    """A file that the kit cannot parse: its path, the line and the text of its first bad node."""

    path: str
    line: int
    node: str


@dataclass(frozen=True, slots=True)
class Derivation:
    """The result of the analysis: the universe, the selected check files with their tags, and the parse failures."""

    universe: Universe
    selected: dict[str, list[NsTag]]
    failures: tuple[Failure, ...]


@dataclass(frozen=True, slots=True)
class ListRow:
    """One row of the list: its line, the weight cell and the namespace cell."""

    line: int
    weight: str
    namespaces: str


@dataclass(frozen=True, slots=True)
class ListProblem:
    """One problem of the list: its line, its kind, its entity and its message."""

    line: int
    kind: str
    entity: str
    message: str


@dataclass(frozen=True, slots=True)
class ListRead:
    """The rows of the list by their check file, and each problem of the list."""

    rows: dict[str, ListRow]
    problems: tuple[ListProblem, ...]


def is_param_key(key: NsTag | ParamKey) -> TypeIs[ParamKey]:
    """Say if a key of a reason is a parameter key: a kind and an integer position."""
    return isinstance(key, tuple) and len(key) == 2 and isinstance(key[1], int)


# ── Collection ───────────────────────────────────────────────────────────────


def template_params(template: tsast.Node) -> list[Param]:
    """Return the template parameters of a template_declaration, in their sequence."""
    found: list[Param] = []
    holder = template.child_by_field("parameters")
    if holder is None:
        return found
    for position, item in enumerate(tsast.non_comment_children(holder)):
        is_type = item.type in TYPE_PARAMETERS
        default = item.child_by_field("default_value") or item.child_by_field("default_type")
        name_node = item.child_by_field("declarator") or item.child_by_field("name")
        if name_node is None:
            leaves = item.children_of_type("type_identifier", "identifier")
            name_node = leaves[-1] if leaves and is_type else None
        name = "" if name_node is None else tsast.leaf_name(name_node) or ""
        found.append(Param(name, ("t", position), default, item.type in PACK_PARAMETERS, is_type))
    return found


def function_params(function: tsast.Node) -> list[Param]:
    """Return the function parameters of a function, in their sequence."""
    return [
        Param(
            name,
            ("f", position),
            node.child_by_field("default_value"),
            node.type == "variadic_parameter_declaration",
            False,
        )
        for position, (name, node) in enumerate(tsast.parameters(function))
    ]


def declared_name(declarator: tsast.Node | None) -> tuple[str, tuple[str, ...]]:
    """Return the last part of a declared name and the qualifier before it."""
    if declarator is None:
        return "", ()
    parts = tsast.qualified_parts(declarator)
    if parts is None or not parts[1]:
        return tsast.leaf_name(declarator) or "", ()
    return parts[1][-1], parts[1][:-1]


def name_leaf(node: tsast.Node | None) -> tsast.Node | None:
    """Return the node of the last name in a name, a declarator, a field access or a call, as tsast.leaf_name() reads it.

    The result is a leaf of one token, a node of UNNAMED_CALLS, or None when the node has no name.
    """
    current = node
    while current is not None and current.type not in ONE_TOKEN_NAMES and current.type not in UNNAMED_CALLS:
        kind = current.type
        if kind in ("qualified_identifier", *TEMPLATE_NAMES):
            current = current.child_by_field("name")
        elif kind in ("dependent_name", "dependent_type", "parenthesized_declarator", "variadic_declarator"):
            inner = tsast.non_comment_children(current)
            current = inner[-1] if inner else None
        elif kind == "field_expression":
            current = current.child_by_field("field")
        elif kind == "call_expression":
            current = current.child_by_field("function")
        elif kind in DECLARATORS:
            current = current.child_by_field("declarator")
        else:
            return None
    return current


def is_predicate_call(function: tsast.Node | None) -> bool:
    """Say if a call names a predicate of std::meta: a function with a name that starts with is_ or has_.

    The name is the token of the last name of the function (name_leaf()).
    """
    leaf = name_leaf(function)
    return leaf is not None and leaf.type in ONE_TOKEN_NAMES and tsast.lexeme(leaf).startswith(PREDICATE_PREFIXES)


def is_unnamed_call(declarator: tsast.Node | None) -> bool:
    """Say if a declared name is an operator, a destructor or a conversion function, through its qualifier.

    The kit gives each of these names a node of its own kind (UNNAMED_CALLS), also `operator new` and a literal
    operator.  A name of one token, such as operator_count, is an identifier, and a use calls it by that name.
    """
    leaf = name_leaf(declarator)
    return leaf is not None and leaf.type in UNNAMED_CALLS


def is_declaration_only(member: tsast.Node) -> bool:
    """Say if a member of a class body declares a function or a friend, with no definition.

    An instance of the class does not run the constraints of such a
    declaration.  A use of the function runs them, and the analysis reads
    the definition of the function for that use.
    """
    item = member
    if item.type == "template_declaration":
        item = tsast.template_item(item) or item
    if item.type == "friend_declaration":
        return True
    return item.type in ("field_declaration", "declaration") and tsast.function_declarator(item) is not None


class Collector:
    """Collect the entities of one tree."""

    __slots__ = ("found", "is_local", "path", "tree", "universe")

    def __init__(self, tree: tsast.Tree, path: str, is_local: bool, universe: Universe) -> None:
        """Keep the tree, the path and the universe of the collector."""
        self.tree = tree
        self.path = path
        self.is_local = is_local
        self.universe = universe
        self.found: list[Entity] = []

    def add(self, entity: Entity) -> Entity:
        """Record one entity and its full name."""
        self.found.append(entity)
        if entity.qual:
            self.universe.quals.add(entity.qual)
        return entity

    def scope(self, node: tsast.Node) -> tuple[str, ...]:
        """Return the full name of the innermost scope around a node."""
        return tsast.scope_levels(node)[0]

    def visit(self, container: tsast.Node, owner: Entity | None, in_template: bool) -> None:
        """Collect the entities of each item of a namespace body or a translation unit."""
        for item in tsast.non_comment_children(container):
            self.item(item, item, [], -1, owner, in_template)

    def item(
        self,
        item: tsast.Node,
        outer: tsast.Node,
        params: list[Param],
        own_template: int,
        owner: Entity | None,
        in_template: bool,
    ) -> None:
        """Collect the entity of one item.

        outer is the node that the body of the entity spans: the outermost
        template header around the item, or the item itself.
        """
        kind = item.type
        if kind in CONTAINERS:
            self.visit(item, owner, in_template)
        elif kind in ("namespace_definition", "linkage_specification"):
            body = item.child_by_field("body")
            if body is not None and body.type == "declaration_list":
                self.visit(body, owner, in_template)
            elif body is not None:
                self.item(body, body, [], -1, owner, in_template)
        elif kind == "template_declaration":
            inner = tsast.template_item(item)
            if inner is not None:
                self.item(inner, outer, template_params(item), item.index, owner, True)
        elif kind in CLASS_SPECIFIERS:
            self.klass(item, outer, params, own_template, owner, in_template)
        elif kind == "enum_specifier":
            self.enumeration(item)
        elif kind == "function_definition":
            self.function(item, outer, params, own_template, owner)
        elif kind in ("declaration", "field_declaration"):
            self.declaration(item, outer, params, own_template, owner, in_template)
        elif kind in ("alias_declaration", "type_definition"):
            name, scope = declared_name(item.child_by_field("name") or item.child_by_field("declarator"))
            self.add(
                Entity(
                    name,
                    self.scope(item) + scope + (name,),
                    "alias",
                    self.path,
                    outer,
                    params,
                    is_eager=not in_template and not self.is_local,
                    is_local=self.is_local,
                    owner=owner,
                    own_template=own_template,
                )
            )
        elif kind == "concept_definition":
            name, scope = declared_name(item.child_by_field("name"))
            self.add(
                Entity(
                    name,
                    self.scope(item) + scope + (name,),
                    "concept",
                    self.path,
                    outer,
                    params,
                    is_local=self.is_local,
                    owner=owner,
                    own_template=own_template,
                )
            )
        elif kind == "friend_declaration":
            # A hidden friend is a function of the enclosing namespace that
            # argument-dependent lookup finds.  It has no owner.
            for inner in tsast.non_comment_children(item):
                if inner.type in ("function_definition", "template_declaration"):
                    self.item(inner, outer if outer != item else inner, params, own_template, None, in_template)

    def enumeration(self, item: tsast.Node) -> None:
        """Record the full names of an enumeration and its enumerators."""
        name, scope = declared_name(item.child_by_field("name"))
        if not name:
            return
        qual = self.scope(item) + scope + (name,)
        self.universe.quals.add(qual)
        body = item.child_by_field("body")
        for enumerator in body.children_of_type("enumerator") if body is not None else []:
            label = enumerator.child_by_field("name")
            if label is not None:
                self.universe.quals.add((*qual, tsast.leaf_name(label) or ""))
                self.universe.quals.add((*qual[:-1], tsast.leaf_name(label) or ""))

    def klass(
        self,
        item: tsast.Node,
        outer: tsast.Node,
        params: list[Param],
        own_template: int,
        owner: Entity | None,
        in_template: bool,
    ) -> None:
        """Collect a class, its members and the ranges of its body that its members own."""
        body = item.child_by_field("body")
        name, scope = declared_name(item.child_by_field("name"))
        if body is None:
            return
        is_template = in_template or bool(params)
        entity = None
        if name:
            # The body of a class that is not a template runs where the
            # header declares it.  The body of a class template runs where a
            # use makes an instance of it: its static_asserts, its bases and
            # its member aliases.
            entity = self.add(
                Entity(
                    name,
                    self.scope(item) + scope + (name,),
                    "class",
                    self.path,
                    outer,
                    params,
                    is_local=self.is_local,
                    owner=owner,
                    own_template=own_template,
                    scans_body=is_template or self.is_local,
                )
            )
        before = len(self.found)
        for member in tsast.non_comment_children(body):
            self.item(member, member, [], -1, entity if entity is not None else owner, is_template)
        if entity is None:
            return
        for member in tsast.non_comment_children(body):
            if is_declaration_only(member):
                entity.skips.append((member.index, self.tree.subtree_end(member.index)))
        for found in self.found[before:]:
            if found.body is not None and found.body.tree is self.tree and found.kind not in ("class", "alias"):
                entity.skips.append((found.body.index, self.tree.subtree_end(found.body.index)))
            if found.owner is entity and found.kind == "function" and found.runs_unnamed(name):
                found.is_unnamed_member = True
        for nested in body.descendants(*CLASS_SPECIFIERS):
            if nested.child_by_field("body") is not None:
                entity.skips.append((nested.index, self.tree.subtree_end(nested.index)))

    def function(
        self, item: tsast.Node, outer: tsast.Node, params: list[Param], own_template: int, owner: Entity | None
    ) -> None:
        """Collect a function definition."""
        declarator = tsast.function_declarator(item)
        name_node = None if declarator is None else declarator.child_by_field("declarator")
        name, scope = declared_name(name_node)
        if not name:
            return
        self.add(
            Entity(
                name,
                self.scope(item) + scope + (name,),
                "function",
                self.path,
                outer,
                params + function_params(item),
                is_local=self.is_local,
                owner=owner,
                is_unnamed_call=is_unnamed_call(name_node),
                own_template=own_template,
                own_function=item.index,
            )
        )

    def declaration(
        self,
        item: tsast.Node,
        outer: tsast.Node,
        params: list[Param],
        own_template: int,
        owner: Entity | None,
        in_template: bool,
    ) -> None:
        """Collect the class of a declaration and each variable that it declares with a value."""
        type_node = item.child_by_field("type")
        if (
            type_node is not None
            and type_node.type in (*CLASS_SPECIFIERS, "enum_specifier")
            and type_node.child_by_field("body") is not None
        ):
            self.item(type_node, type_node, params, own_template, owner, in_template)
        for declarator in item.children:
            if declarator.field != "declarator" or tsast.function_declarator(declarator) is not None:
                continue
            if declarator.type != "init_declarator" and item.child_by_field("default_value") is None:
                continue
            target = declarator.child_by_field("declarator") if declarator.type == "init_declarator" else declarator
            name, scope = declared_name(target)
            if not name:
                continue
            is_eager = not in_template and not self.is_local and (owner is None or owner.own_template < 0)
            self.add(
                Entity(
                    name,
                    self.scope(item) + scope + (name,),
                    "variable",
                    self.path,
                    outer,
                    params,
                    is_eager=is_eager,
                    is_local=self.is_local,
                    owner=owner,
                    own_template=own_template,
                )
            )


def merge_ranges(ranges: list[tuple[int, int]]) -> list[tuple[int, int]]:
    """Return the union of index ranges as sorted ranges that do not overlap.

    Complexity: O(r log r) for r ranges.
    """
    merged: list[tuple[int, int]] = []
    for start, end in sorted(ranges):
        if merged and start < merged[-1][1]:
            merged[-1] = (merged[-1][0], max(merged[-1][1], end))
        else:
            merged.append((start, end))
    return merged


def record_namespaces(tree: tsast.Tree, path: str, universe: Universe) -> None:
    """Record each namespace that a tree opens, with each prefix of its name."""
    for node in tree.find("namespace_definition"):
        if node.ancestor_of_type("compound_statement", "lambda_expression") is not None:
            continue
        body = node.child_by_field("body")
        if body is None or body.type != "declaration_list":
            continue
        own = tsast.namespace_path(body, skip_inline=True)
        for count in range(1, len(own) + 1):
            universe.namespaces.setdefault(own[:count], set()).add(path)


def macro_entity(body: tsast.MacroBody, path: str, is_local: bool) -> Entity:
    """Return the entity of one #define, with its parameters as function parameters."""
    params = [
        Param(name, ("f", position), None, name == "__VA_ARGS__", False) for position, name in enumerate(body.params)
    ]
    if body.is_parsed:
        return Entity(body.name, (body.name,), "macro", path, body.root, params, is_local=is_local)
    tokens = tuple(
        tsast.lexeme(token) for token in tsast.pp_tokens(body.text, body.first_row) if token.kind == "identifier"
    )
    return Entity(body.name, (body.name,), "macro", path, None, params, is_local=is_local, tokens=tokens)


def collect(context: TsastContext) -> tuple[Universe, tuple[Failure, ...]]:
    """Read each header in include/ and each check file, and collect their entities.

    A file that the kit cannot parse gives a failure and no entity.  The
    files go in the sequence of their paths, as the parts of a path sort.

    Complexity: linear in the number of nodes of the trees.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    universe = Universe()
    failures: list[Failure] = []
    headers = sorted((path for path in context.files([f"{INCLUDE}/"]) if path.endswith(".h")), key=PurePosixPath)
    checks = sorted((path for path in context.files([f"{CHECKS}/"]) if path.endswith(".cpp")), key=PurePosixPath)
    header_trees: list[tsast.Tree] = []
    check_trees: list[tsast.Tree] = []
    for paths, kept in ((headers, header_trees), (checks, check_trees)):
        for tree in context.trees(paths, with_broken=True):
            error = tsast.first_error(tree)
            if error is not None:
                failures.append(Failure(PurePosixPath(tree.path).as_posix(), error[0], error[1]))
                continue
            kept.append(tree)
    for tree in header_trees + check_trees:
        rel = PurePosixPath(tree.path).as_posix()
        is_local = rel.startswith(f"{CHECKS}/")
        record_namespaces(tree, rel, universe)
        universe.aliases[rel] = tsast.namespace_aliases(tree)
        universe.usings[rel] = tsast.using_names(tree)
        if not is_local:
            universe.shared_aliases += [
                alias for alias in universe.aliases[rel] if alias.scope.type in ("translation_unit", "declaration_list")
            ]
        collector = Collector(tree, rel, is_local, universe)
        collector.visit(tree.root, None, False)
        for entity in collector.found:
            entity.skips = merge_ranges(entity.skips)
        if is_local:
            rel_check = PurePosixPath(rel).relative_to(CHECKS).as_posix()
            skips = [
                (entity.body.index, tree.subtree_end(entity.body.index))
                for entity in collector.found
                if entity.kind in ("function", "concept") and entity.body is not None
            ]
            file_entity = Entity(rel_check, (), "file", rel, tree.root, skips=merge_ranges(skips), is_local=True)
            universe.roots[rel_check] = file_entity
            collector.found.append(file_entity)
            local = universe.local.setdefault(rel, {})
            for entity in collector.found:
                if entity.kind != "file":
                    local.setdefault(entity.name, []).append(entity)
        universe.entities.extend(collector.found)
    for tree_list, is_local in ((header_trees, False), (check_trees, True)):
        for body in context.macro_bodies(tree_list):
            rel = PurePosixPath(body.define.tree.path).as_posix()
            macro = macro_entity(body, rel, is_local)
            universe.entities.append(macro)
            if is_local:
                universe.local.setdefault(rel, {}).setdefault(macro.name, []).append(macro)
    classes: dict[tuple[str, ...], list[Entity]] = {}
    for entity in universe.entities:
        if entity.kind == "class":
            classes.setdefault(entity.qual, []).append(entity)
    for entity in universe.entities:
        if entity.kind != "file" and not entity.is_local:
            universe.by_name.setdefault(entity.name, []).append(entity)
        if entity.owner is None and entity.kind == "function" and len(entity.qual) >= 2:
            # An out-of-line definition of a member names its class in its
            # qualifier.
            for klass in classes.get(entity.qual[:-1], []):
                entity.owner = klass
                entity.is_unnamed_member = entity.runs_unnamed(klass.name)
                break
        if entity.owner is not None and entity.is_unnamed_member:
            entity.owner.unnamed_members.append(entity)
    return universe, tuple(failures)


# ── Names and flow ───────────────────────────────────────────────────────────


class Analysis:
    """The summaries of all entities, and the questions that build them."""

    __slots__ = ("active", "binding_memo", "flow_memo", "named_memo", "resolve_memo", "universe")

    def __init__(self, universe: Universe) -> None:
        """Start an analysis over a collected universe."""
        self.universe = universe
        self.binding_memo: dict[tuple[int, int, int], Binding | None] = {}
        self.resolve_memo: dict[tuple[int, int, int, str], tuple[list[Entity], bool]] = {}
        self.flow_memo: dict[tuple[int, int, int], Flow] = {}
        self.active: set[tuple[int, int, int]] = set()
        self.named_memo: dict[str, set[int]] = {}

    # Name lookup.

    def candidates(self, entity: Entity, leaf: tsast.Node, name: str) -> tuple[list[Entity], bool]:
        """Return the entities that a leaf can name, and if the name gets to them through a member edge.

        THE NAMES in the docstring of this module gives the rules.
        """
        key = (id(entity), id(leaf.tree), leaf.index, name)
        found = self.resolve_memo.get(key)
        if found is None:
            found = self.resolve(entity, leaf, name)
            self.resolve_memo[key] = found
        return found

    def resolve(self, entity: Entity, leaf: tsast.Node, name: str) -> tuple[list[Entity], bool]:
        """Resolve one leaf with no memo.

        A file names only its own layer and the layers below it.  A
        candidate of a higher layer then does not match.
        """
        level = layer_of(entity.path)
        pool = [candidate for candidate in name_pool(self.universe, entity, name) if layer_of(candidate.path) <= level]
        template_arguments, function_arguments = use_site(leaf)
        is_called = template_arguments is not None or function_arguments is not None
        members = [candidate for candidate in pool if candidate.owner is not None and candidate.kind == "function"]
        free = [candidate for candidate in pool if candidate.owner is None]
        parent = leaf.parent
        if parent is not None and parent.type == "field_expression" and leaf.field == "field":
            return (members if is_called else []), True
        spelled = tsast.qualified_path(leaf)
        if spelled is None:
            return (members if is_called else []), True
        is_global, parts = spelled
        if parts[:1] in (("std",), ("__gnu_cxx",)):
            return [], False
        levels: tuple[tuple[str, ...], ...] = ((),)
        usings: tuple[tuple[tuple[str, ...], bool, tuple[str, ...]], ...] = ()
        if entity.kind == "macro":
            # A macro body has no scope until it expands.  A name with no
            # qualifier in it is then each free entity with that name.
            if len(parts) == 1:
                return free, False
            parts = tsast.resolve_namespace(parts, None, self.universe.shared_aliases, is_global=is_global)
        else:
            aliases = self.universe.aliases.get(entity.path, []) + self.universe.shared_aliases
            if len(parts) >= 2:
                parts = tsast.resolve_namespace(parts, leaf, aliases, is_global=is_global)
            site = tsast.lookup_site_of_parts(
                leaf, is_global, tuple(parts), aliases, self.universe.usings.get(entity.path, [])
            )
            levels = ((),) if is_global else site.levels
            usings = site.usings
        for scope_level in levels:
            tried = [scope_level + tuple(parts)]
            for using_level, is_directive, target in usings:
                if using_level != scope_level:
                    continue
                if is_directive:
                    tried.append(target + tuple(parts))
                elif target and target[-1] == parts[0]:
                    tried.append(target + tuple(parts[1:]))
            hits = [candidate for candidate in pool if candidate.qual in tried]
            if hits:
                # In a class, its own name is the injected class name.  It
                # names the class and not a constructor.
                classes = [
                    candidate.owner
                    for candidate in hits
                    if candidate.owner is not None and candidate.name == candidate.owner.name
                ]
                kept = [
                    candidate for candidate in hits if candidate.owner is None or candidate.name != candidate.owner.name
                ]
                return kept + [klass for klass in classes if klass not in kept], False
            if len(parts) >= 2 and any(qual[:-1] in self.universe.namespaces for qual in tried):
                return [], False
        if len(parts) >= 2:
            return (members if is_called else []), True
        return (free if is_called else []), False

    def named_classes(self, entity: Entity) -> set[int]:
        """Return the identity of each class that the check file of a local entity names, through an accurate name.

        A member call on an object in a check file gets to a member of one
        of these classes.  The file names the class of the object to make
        the object.
        """
        found = self.named_memo.get(entity.path)
        if found is not None:
            return found
        found = set()
        root_entity = next((item for item in self.universe.roots.values() if item.path == entity.path), None)
        if root_entity is not None and root_entity.body is not None:
            for leaf in root_entity.body.descendants(*NAME_LEAVES):
                name = tsast.leaf_name(leaf)
                if name is None or not any(
                    candidate.kind == "class" for candidate in name_pool(self.universe, root_entity, name)
                ):
                    continue
                candidates, is_member_edge = self.candidates(root_entity, leaf, name)
                if not is_member_edge:
                    found.update(id(candidate) for candidate in candidates if candidate.kind == "class")
        self.named_memo[entity.path] = found
        return found

    def binding(self, entity: Entity, node: tsast.Node, name: str) -> Binding | None:
        """Return the declaration of a name in the body of an entity, or None when the body does not declare it."""
        key = (id(entity), id(node.tree), node.index)
        if key in self.binding_memo:
            return self.binding_memo[key]
        found = self.find_binding(entity, node, name)
        self.binding_memo[key] = found
        return found

    def find_binding(self, entity: Entity, node: tsast.Node, name: str) -> Binding | None:
        """Walk the scopes around a node, innermost first, and return the declaration of a name.

        Complexity: linear in the depth of the node and in the items of
        each block on the way.
        """
        child = node
        current = node.parent
        while current is not None:
            kind = current.type
            if kind in ("for_range_loop", "expansion_statement"):
                declarator = current.child_by_field("declarator")
                if declarator is not None and tsast.leaf_name(declarator) == name and child.field != "right":
                    right = current.child_by_field("right")
                    return Binding(BOUND_ELEMENT, values=() if right is None else (right,))
            elif kind in ("compound_statement", "condition_clause", "for_statement"):
                value = local_value(current, child, name)
                if value is not None:
                    return value
            elif kind in ("lambda_expression", "requires_expression"):
                if name in parameter_names(current):
                    return Binding(BOUND_OPAQUE, reason="a parameter of a lambda or of a requires-expression")
            elif kind == "function_definition":
                if name in {parameter for parameter, _node in tsast.parameters(current)}:
                    own = next((param for param in entity.params if param.key[0] == "f" and param.name == name), None)
                    if current.index == entity.own_function and own is not None:
                        return Binding(BOUND_PARAM, param=own)
                    return Binding(BOUND_OPAQUE, reason="a parameter of an enclosing function")
            elif kind == "template_declaration":
                for param in template_params(current):
                    if param.name != name:
                        continue
                    own = next((item for item in entity.params if item.key[0] == "t" and item.name == name), None)
                    if current.index == entity.own_template and own is not None:
                        return Binding(BOUND_PARAM, param=own)
                    if param.is_type:
                        return Binding(BOUND_TYPE, reason="a template parameter")
                    return Binding(BOUND_OPAQUE, reason="a template parameter of an enclosing template")
            if entity.body is not None and current == entity.body:
                break
            child = current
            current = current.parent
        if entity.kind == "macro":
            own = next((param for param in entity.params if param.name == name), None)
            if own is not None:
                return Binding(BOUND_PARAM, param=own)
        return None

    # Reflections.

    def reflected(self, entity: Entity, expression: tsast.Node) -> Flow:
        """Return the flow of a reflection `^^X`."""
        inner = next(iter(tsast.non_comment_children(expression)), None)
        if inner is None:
            if tsast.spelled(expression) == "^^::":
                return Flow({("",): "the reflection of the global namespace"})
            return unknown(f"the reflection {tsast.excerpt(expression)}")
        target = inner.child_by_field("type") if inner.type == "type_descriptor" else inner
        if target is None:
            return unknown(f"the reflection {tsast.excerpt(expression)}")
        if target.type in (
            "primitive_type",
            "sized_type_specifier",
            "template_type",
            "template_function",
            "decltype",
            "placeholder_type_specifier",
        ):
            return Flow()
        if inner.type == "type_descriptor" and len(tsast.non_comment_children(inner)) > 1:
            return Flow()
        last = target
        while last.type == "qualified_identifier":
            name = last.child_by_field("name")
            if name is None:
                break
            last = name
        if last.type in TEMPLATE_NAMES:
            return Flow()
        spelled = tsast.qualified_parts(target)
        if spelled is None:
            return unknown(f"the reflection {tsast.excerpt(expression)}")
        is_global, parts = spelled
        if not is_global and len(parts) == 1 and self.binding(entity, target, parts[0]) is not None:
            return Flow()
        return self.namespace_flow(entity, target, is_global, parts)

    def namespace_flow(self, entity: Entity, at: tsast.Node, is_global: bool, parts: tuple[str, ...]) -> Flow:
        """Return the flow of a name that a reflection or a qualifier spells: its namespace, or nothing."""
        aliases = self.universe.aliases.get(entity.path, []) + self.universe.shared_aliases
        if entity.kind != "macro":
            parts = tsast.resolve_namespace(parts, at, aliases, is_global=is_global)
        else:
            parts = tsast.resolve_namespace(parts, None, self.universe.shared_aliases, is_global=is_global)
        if parts[:1] in (("std",), ("__gnu_cxx",)):
            return Flow()
        levels: tuple[tuple[str, ...], ...] = ((),)
        usings: list[tuple[tuple[str, ...], tuple[str, ...]]] = []
        if not is_global and entity.kind != "macro":
            site = tsast.lookup_site_of_parts(at, False, parts, aliases, self.universe.usings.get(entity.path, []))
            levels = site.levels
            usings = [(level, target) for level, is_directive, target in site.usings if is_directive]
        for level in levels:
            tried = [level + parts] + [target + parts for using_level, target in usings if using_level == level]
            for name in tried:
                if name in self.universe.namespaces:
                    return Flow({name: f"the reflection of the namespace ::{'::'.join(name)}"})
            if any(name in self.universe.quals for name in tried):
                return Flow()
        return unknown(f"the reflection of {'::'.join(parts)}, which the analysis cannot resolve")

    # Values.

    def flow(self, entity: Entity, expression: tsast.Node, depth: int = 0) -> Flow:
        """Return what an expression in the body of an entity can give to a walk."""
        key = (id(entity), id(expression.tree), expression.index)
        found = self.flow_memo.get(key)
        if found is not None:
            return found
        if key in self.active or depth > FLOW_DEPTH:
            return unknown(f"a value that depends on itself: {tsast.excerpt(expression)}")
        self.active.add(key)
        try:
            found = self.compute_flow(entity, expression, depth)
        finally:
            self.active.discard(key)
        self.flow_memo[key] = found
        return found

    def compute_flow(self, entity: Entity, expression: tsast.Node, depth: int) -> Flow:
        """Calculate the flow of one expression, with no memo."""
        kind = expression.type
        if kind == "reflect_expression":
            return self.reflected(entity, expression)
        if kind in LITERALS or kind in OPERATORS:
            return Flow()
        if kind in ("parenthesized_expression", "type_descriptor"):
            inner = tsast.non_comment_children(expression)
            return self.flow(entity, inner[0], depth + 1) if inner else Flow()
        if kind == "conditional_expression":
            result = Flow()
            for name in ("consequence", "alternative"):
                branch = expression.child_by_field(name)
                if branch is not None:
                    result.merge(self.flow(entity, branch, depth + 1))
            return result
        if kind in ("initializer_list", "argument_list"):
            result = Flow()
            for item in tsast.non_comment_children(expression):
                result.merge(self.flow(entity, item, depth + 1))
            return result
        if kind == "subscript_expression":
            base = expression.child_by_field("argument")
            return self.flow(entity, base, depth + 1) if base is not None else unknown("a subscript")
        if kind in ("cast_expression", "static_cast_expression"):
            value = expression.child_by_field("value")
            return self.flow(entity, value, depth + 1) if value is not None else unknown("a cast")
        if kind == "call_expression":
            return self.call_flow(entity, expression, depth)
        if kind in ("identifier", "qualified_identifier", "type_identifier"):
            # A template argument that is one name parses as a type, also
            # when the name is a value.
            return self.name_flow(entity, expression, depth)
        return unknown(f"the expression {tsast.excerpt(expression)}")

    def call_flow(self, entity: Entity, call: tsast.Node, depth: int) -> Flow:
        """Return the flow of the result of a call."""
        function = call.child_by_field("function")
        name = None if function is None else tsast.leaf_name(function)
        arguments = call.child_by_field("arguments")
        first = next(iter(tsast.non_comment_children(arguments)), None) if arguments is not None else None
        if function is not None and function.type == "field_expression":
            return unknown(f"the result of the member call {tsast.excerpt(call)}")
        if name in FOLLOWS_ARGUMENT:
            return self.flow(entity, first, depth + 1) if first is not None else unknown(f"a call of {name}")
        if name in NEVER_NAMESPACE or is_predicate_call(function):
            return Flow()
        return unknown(f"the result of {name or tsast.excerpt(call)}")

    def name_flow(self, entity: Entity, expression: tsast.Node, depth: int) -> Flow:
        """Return the flow of a name used as a value."""
        spelled = tsast.qualified_parts(expression)
        if spelled is None:
            return unknown(f"the name {tsast.excerpt(expression)}")
        is_global, parts = spelled
        if not is_global and len(parts) == 1:
            bound = self.binding(entity, expression, parts[0])
            if bound is not None:
                return self.bound_flow(entity, bound, parts[0], depth)
        result = Flow()
        variables = [
            candidate for candidate in self.candidates(entity, expression, parts[-1])[0] if candidate.kind == "variable"
        ]
        if not variables:
            return unknown(f"the value {'::'.join(parts)}, which names no variable that the analysis reads")
        for candidate in variables:
            value = initializer(candidate)
            if value is None:
                result.merge(unknown(f"the variable {candidate.label()}, which has no initializer"))
                continue
            inner = self.flow(candidate, value, depth + 1)
            result.merge(Flow(dict(inner.namespaces)))
            if inner.params:
                result.merge(unknown(f"the variable {candidate.label()}, whose value depends on a parameter"))
        return result

    def bound_flow(self, entity: Entity, bound: Binding, name: str, depth: int) -> Flow:
        """Return the flow of a name that the body of an entity declares."""
        if bound.kind == BOUND_PARAM and bound.param is not None:
            return Flow() if bound.param.is_type else Flow({}, {bound.param.key: f"the parameter {name}"})
        if bound.kind == BOUND_TYPE:
            return Flow()
        if bound.kind == BOUND_ELEMENT:
            if not bound.values:
                return unknown(f"the variable {name} of a range loop with no range")
            return self.flow(entity, bound.values[0], depth + 1)
        if bound.kind == BOUND_LOCAL:
            if not bound.values:
                return unknown(f"the local {name}, which gets no value that the analysis reads")
            result = Flow()
            for value in bound.values:
                result.merge(self.flow(entity, value, depth + 1))
            return result
        return unknown(f"{bound.reason} ({name})")


def local_value(scope: tsast.Node, child: tsast.Node, name: str) -> Binding | None:
    """Return the declaration of a name in a block before a child, or None.

    The value of a local is its initializer.  An assignment in the block,
    and a call of a mutator such as push_back, give it more values.
    """
    for item in tsast.non_comment_children(scope):
        if item.start >= child.start and item != child:
            break
        if item.type in ("alias_declaration", "type_definition"):
            declared = item.child_by_field("name") or item.child_by_field("declarator")
            if declared is not None and tsast.leaf_name(declared) == name:
                return Binding(BOUND_TYPE, reason="a local type alias")
            continue
        if item.type != "declaration":
            continue
        for declarator in item.children:
            if declarator.field != "declarator":
                continue
            if declarator.type == "init_declarator":
                target = declarator.child_by_field("declarator")
                if target is not None and tsast.leaf_name(target) == name:
                    value = declarator.child_by_field("value")
                    stored = stored_values(scope, name)
                    return Binding(BOUND_LOCAL, values=(*(() if value is None else (value,)), *stored))
            elif declarator.type == "structured_binding_declarator":
                if name in {tsast.leaf_name(part) for part in tsast.non_comment_children(declarator)}:
                    return Binding(BOUND_OPAQUE, reason="a structured binding")
            elif tsast.leaf_name(declarator) == name:
                return Binding(BOUND_LOCAL, values=tuple(stored_values(scope, name)))
    return None


def stored_values(scope: tsast.Node, name: str) -> list[tsast.Node]:
    """Return each value of an assignment to a local in a block, and each value that a mutator puts into it."""
    values: list[tsast.Node] = []
    for assignment in scope.descendants("assignment_expression"):
        left = assignment.child_by_field("left")
        while left is not None and left.type == "subscript_expression":
            left = left.child_by_field("argument")
        right = assignment.child_by_field("right")
        if left is not None and left.type == "identifier" and tsast.leaf_name(left) == name and right is not None:
            values.append(right)
    for call in scope.descendants("call_expression"):
        function = call.child_by_field("function")
        if function is None or function.type != "field_expression":
            continue
        receiver = function.child_by_field("argument")
        member = function.child_by_field("field")
        if receiver is None or receiver.type != "identifier" or tsast.leaf_name(receiver) != name:
            continue
        if member is not None and tsast.leaf_name(member) in MUTATORS:
            arguments = call.child_by_field("arguments")
            values.extend(tsast.non_comment_children(arguments) if arguments is not None else [])
    return values


def parameter_names(owner: tsast.Node) -> set[str]:
    """Return the names of the parameters and template parameters of a lambda or of a `requires` expression."""
    names: set[str] = set()
    holders: list[tsast.Node] = []
    declarator = owner.child_by_field("declarator")
    parameters = None if declarator is None else declarator.child_by_field("parameters")
    if parameters is not None:
        holders.append(parameters)
    for field_name in ("parameters", "template_parameters"):
        holder = owner.child_by_field(field_name)
        if holder is not None:
            holders.append(holder)
    for holder in holders:
        for item in tsast.non_comment_children(holder):
            inner = item.child_by_field("declarator") or item.child_by_field("name")
            if inner is None:
                leaves = item.children_of_type("type_identifier", "identifier")
                inner = leaves[-1] if leaves else None
            leaf = None if inner is None else tsast.leaf_name(inner)
            if leaf:
                names.add(leaf)
    return names


def is_declared_name(leaf: tsast.Node) -> bool:
    """Say if a leaf is the name that a declaration declares, or the name in a reflection that runs nothing.

    A reflection `^^name` names an entity and runs no part of it.  Only
    std::meta::substitute makes an instance of a reflected template, and
    use_site() reads that shape.
    """
    node = leaf
    parent = node.parent
    while parent is not None and parent.type in ("qualified_identifier", *TEMPLATE_NAMES) and node.field == "name":
        node, parent = parent, parent.parent
    if parent is None:
        return False
    if parent.type in ("destructor_name", "operator_name"):
        return True
    if leaf.ancestor_of_type("reflect_expression") is not None and substitute_arguments(leaf) is None:
        return True
    if node.field in ("declarator", "name") and parent.type in DECLARED_NAME_OWNERS:
        return True
    return parent.type == "structured_binding_declarator"


def initializer(variable: Entity) -> tsast.Node | None:
    """Return the initializer expression of a variable entity, or None."""
    if variable.body is None:
        return None
    holder = variable.body
    if holder.type == "template_declaration":
        holder = tsast.template_item(holder) or holder
    for declarator in holder.children:
        if declarator.field != "declarator" or declarator.type != "init_declarator":
            continue
        target = declarator.child_by_field("declarator")
        if target is not None and tsast.leaf_name(target) == variable.name:
            return declarator.child_by_field("value")
    return holder.child_by_field("default_value")


def use_site(leaf: tsast.Node) -> tuple[list[tsast.Node] | None, list[tsast.Node] | None]:
    """Return the template arguments and the function arguments that a name receives where it stands.

    Each list is None when the name receives no list of that kind.
    """
    template_arguments: list[tsast.Node] | None = None
    function_arguments: list[tsast.Node] | None = None
    substituted = substitute_arguments(leaf)
    if substituted is not None:
        return substituted, None
    node = leaf
    parent = node.parent
    if parent is not None and parent.type == "macro_invocation" and node.field == "name":
        # The kit does not divide the arguments of a macro invocation.  The
        # full token tree is then the first argument, and the analysis cannot
        # read it.
        holder = parent.child_by_field("arguments")
        return None, [] if holder is None else [holder]
    if parent is not None and parent.type in TEMPLATE_NAMES and node.field == "name":
        holder = parent.child_by_field("arguments")
        template_arguments = [] if holder is None else tsast.non_comment_children(holder)
        node, parent = parent, parent.parent
    while parent is not None and parent.type == "qualified_identifier" and node.field == "name":
        node, parent = parent, parent.parent
    if parent is not None and parent.type == "field_expression" and node.field == "field":
        node, parent = parent, parent.parent
    if parent is not None and parent.type == "call_expression" and node.field == "function":
        holder = parent.child_by_field("arguments")
        function_arguments = [] if holder is None else tsast.non_comment_children(holder)
    return template_arguments, function_arguments


def substitute_arguments(leaf: tsast.Node) -> list[tsast.Node] | None:
    """Return the template arguments of `std::meta::substitute(^^name, {args...})` when a leaf is that name.

    An argument `reflect_constant(value)` or `reflect_object(value)` gives
    its value.  The result is None when the leaf does not stand there.
    """
    node = leaf
    parent = node.parent
    while (
        parent is not None
        and parent.type in ("qualified_identifier", "type_descriptor")
        and node.field in ("name", "type", None)
    ):
        node, parent = parent, parent.parent
    if parent is None or parent.type != "reflect_expression":
        return None
    holder = parent.parent
    call = None if holder is None else holder.parent
    if holder is None or holder.type != "argument_list" or call is None or call.type != "call_expression":
        return None
    function = call.child_by_field("function")
    arguments = tsast.non_comment_children(holder)
    if function is None or tsast.leaf_name(function) != "substitute" or len(arguments) < 2 or arguments[0] != parent:
        return None
    if arguments[1].type != "initializer_list":
        return None
    values: list[tsast.Node] = []
    for item in tsast.non_comment_children(arguments[1]):
        inner = item
        if item.type == "call_expression":
            callee = item.child_by_field("function")
            name = None if callee is None else tsast.leaf_name(callee)
            given = item.child_by_field("arguments")
            first = next(iter(tsast.non_comment_children(given)), None) if given is not None else None
            if name in ("reflect_constant", "reflect_object") and first is not None:
                inner = first
        values.append(inner)
    return values


def arguments_at(
    callee: Entity,
    key: ParamKey,
    template_arguments: list[tsast.Node] | None,
    function_arguments: list[tsast.Node] | None,
) -> list[tsast.Node] | None:
    """Return the arguments that a use gives to one parameter of a callee, or None when it gives none."""
    given = template_arguments if key[0] == "t" else function_arguments
    if given is None:
        return None
    ordered = [param for param in callee.params if param.key[0] == key[0]]
    position = next((index for index, param in enumerate(ordered) if param.key == key), None)
    if position is None:
        return None
    if ordered[position].is_pack:
        return given[position:]
    return given[position : position + 1] if position < len(given) else None


# ── The summaries ────────────────────────────────────────────────────────────


def summarize(universe: Universe) -> Analysis:
    """Calculate the summary of each entity, until no summary changes, in two layers.

    The accurate layer follows each name other than a member edge.  The full
    layer starts from the accurate summaries.  It follows the same names,
    and in a check file each member edge gives the accurate summary of its
    target.  The analysis does not know the class of a member edge.  A
    member edge then cannot move a walk that a different member edge got to.

    Complexity: the analysis reads an entity again only when the summary of
    an entity that it names increases.  A summary only increases, and the
    work is at most the number of tags times the number of references.
    """
    analysis = Analysis(universe)
    all_names = set(universe.by_name)
    for names in universe.local.values():
        all_names.update(names)
    referrers: dict[str, list[Entity]] = {}
    for entity in universe.entities:
        if entity.tokens:
            entity.names = frozenset(name for name in entity.tokens if name in all_names)
        elif entity.body is not None:
            for leaf in entity.nodes(*NAME_LEAVES):
                name = tsast.leaf_name(leaf)
                if name is not None and name in all_names and name not in WALKS and not is_declared_name(leaf):
                    entity.leaves.append((leaf, name))
            entity.names = frozenset(name for _leaf, name in entity.leaves)
        for name in entity.names:
            referrers.setdefault(name, []).append(entity)
    fixed_point(analysis, referrers, is_full=False)
    for entity in universe.entities:
        entity.precise_walked = dict(entity.walked)
        entity.precise_params = dict(entity.walked_params)
        entity.precise_constructed = dict(entity.constructed)
    fixed_point(analysis, referrers, is_full=True)
    return analysis


def fixed_point(analysis: Analysis, referrers: dict[str, list[Entity]], is_full: bool) -> None:
    """Read each entity again until no summary increases."""
    pending = list(analysis.universe.entities)
    queued = {id(entity) for entity in pending}
    while pending:
        entity = pending.pop()
        queued.discard(id(entity))
        before = entity.size()
        evaluate(analysis, entity, is_full)
        if entity.size() == before:
            continue
        followers = list(referrers.get(entity.name, []))
        if entity.owner is not None and entity.is_unnamed_member:
            followers.append(entity.owner)
        for follower in followers:
            if id(follower) not in queued:
                queued.add(id(follower))
                pending.append(follower)


def evaluate(analysis: Analysis, entity: Entity, is_full: bool) -> None:
    """Add to the summary of one entity what its body and the summaries that it names give."""
    if entity.tokens or (entity.body is None and entity.kind == "macro"):
        evaluate_tokens(analysis, entity)
        return
    for member in entity.unnamed_members:
        for tag in member.walked:
            entity.constructed.setdefault(
                tag, Why(f"the member {member.label()}, which an object of the class runs with no name", member, tag)
            )
        for key in member.walked_params:
            entity.constructed.setdefault(
                UNKNOWN,
                Why(
                    f"the member {member.label()} walks a parameter, and the "
                    f"analysis does not map the arguments of a call with no "
                    f"name",
                    member,
                    key,
                ),
            )
    if not entity.scans_body:
        return
    if entity.walk_calls is None:
        entity.walk_calls = [call for call in entity.nodes("call_expression") if is_walk_call(call)]
    for call in entity.walk_calls:
        arguments = call.child_by_field("arguments")
        first = next(iter(tsast.non_comment_children(arguments)), None) if arguments is not None else None
        flow = analysis.flow(entity, first) if first is not None else unknown("a call of members_of with no argument")
        for tag, reason in flow.namespaces.items():
            entity.walked.setdefault(tag, Why(f"members_of at {entity.path}:{call.line}: {reason}"))
        for key, reason in flow.params.items():
            entity.walked_params.setdefault(key, Why(f"members_of at {entity.path}:{call.line}: {reason}"))
    names = {
        name
        for name in entity.names
        if any(
            candidate.walked or candidate.walked_params or candidate.constructed
            for candidate in name_pool(analysis.universe, entity, name)
        )
    }
    if not names:
        return
    for leaf, name in entity.leaves:
        if name in names and analysis.binding(entity, leaf, name) is None:
            follow_leaf(analysis, entity, leaf, name, is_full)


def follow_leaf(analysis: Analysis, entity: Entity, leaf: tsast.Node, name: str, is_full: bool) -> None:
    """Add to the summary of an entity what each candidate of one leaf gives (THE NAMES and THE CLASSES)."""
    found, is_member_edge = analysis.candidates(entity, leaf, name)
    if is_member_edge:
        if not (is_full and entity.is_local):
            return
        named = analysis.named_classes(entity)
        found = [candidate for candidate in found if candidate.owner is not None and id(candidate.owner) in named]
    constructs = entity.is_local or is_construction(leaf)
    for candidate in found:
        if candidate is entity:
            continue
        constructed = candidate.precise_constructed if is_member_edge else candidate.constructed
        if entity.kind == "alias" and candidate.kind in ("alias", "class"):
            # An alias of a class gives the construction summary of the
            # class.
            for tag in constructed:
                entity.constructed.setdefault(
                    tag, Why(f"{candidate.label()} at {entity.path}:{leaf.line}", candidate, tag)
                )
        if not constructs and candidate.kind not in ("alias", "class") and is_type_position(leaf):
            continue
        walked: dict[NsTag, Why] = {}
        walked_params: dict[ParamKey, Why] = {}
        if not candidate.is_eager:
            walked = dict(candidate.precise_walked if is_member_edge else candidate.walked)
            walked_params = candidate.precise_params if is_member_edge else candidate.walked_params
        if constructs and candidate.kind in ("alias", "class"):
            for tag, why in constructed.items():
                walked.setdefault(tag, why)
        if walked or walked_params:
            propagate(analysis, entity, candidate, leaf, walked, walked_params)


def is_type_position(leaf: tsast.Node) -> bool:
    """Say if a leaf names a type at the type of a declaration, of a cast or of a base clause.

    A name in a template argument list or in a reflection can be a value.
    Such a name is not a type position.
    """
    if leaf.type != "type_identifier":
        return False
    current = leaf.parent
    while current is not None and current.type not in TYPE_POSITION_ENDS:
        if current.type in ("template_argument_list", "reflect_expression"):
            return False
        current = current.parent
    return True


def is_walk_call(call: tsast.Node) -> bool:
    """Say if a call expression calls members_of, with a qualifier, with none, or through std::meta."""
    function = call.child_by_field("function")
    return function is not None and function.type != "field_expression" and tsast.leaf_name(function) in WALKS


def is_construction(leaf: tsast.Node) -> bool:
    """Say if a name stands where a use makes an object of the class that it names.

    The shapes are `C{...}`, `C(...)`, `new C`, and a declaration of an
    object of type C with an initializer.
    """
    node = leaf
    parent = node.parent
    while parent is not None and parent.type in ("qualified_identifier", *TEMPLATE_NAMES) and node.field == "name":
        node, parent = parent, parent.parent
    if parent is None:
        return False
    if parent.type == "compound_literal_expression" and node.field == "type":
        return True
    if parent.type == "call_expression" and node.field == "function":
        return True
    if parent.type == "new_expression" and node.field == "type":
        return True
    if parent.type in ("declaration", "field_declaration") and node.field == "type":
        return parent.child_by_field("default_value") is not None or any(
            child.field == "declarator" and child.type == "init_declarator" for child in parent.children
        )
    return False


@functools.cache
def layer_of(path: str) -> int:
    """Return the rank of the layer of a file: 0 for foundation, 1 for fixy, 2 for each other file.

    The lookup reads the rank of each candidate, and the cache holds one rank for each path of the tree.
    """
    for prefix in (f"{INCLUDE}/", f"{CHECKS}/"):
        if path.startswith(prefix):
            head = path.removeprefix(prefix).partition("/")[0]
            return LAYERS.index(head) if head in LAYERS else len(LAYERS) - 1
    return len(LAYERS) - 1


def name_pool(universe: Universe, entity: Entity, name: str) -> list[Entity]:
    """Return each entity with a last name, from the headers and the check file of an entity."""
    pool = universe.by_name.get(name, [])
    if entity.is_local:
        pool = pool + universe.local.get(entity.path, {}).get(name, [])
    return pool


def propagate(
    analysis: Analysis,
    entity: Entity,
    candidate: Entity,
    leaf: tsast.Node,
    walked: dict[NsTag, Why],
    walked_params: dict[ParamKey, Why],
) -> None:
    """Add to the summary of an entity what one named candidate gives at one leaf, from one summary of it."""
    where = f"{entity.path}:{leaf.line}"
    for tag in walked:
        entity.walked.setdefault(tag, Why(f"{candidate.label()} at {where}", candidate, tag))
    if not walked_params:
        return
    template_arguments, function_arguments = use_site(leaf)
    for key in walked_params:
        given = arguments_at(candidate, key, template_arguments, function_arguments)
        if given is None:
            param = next((param for param in candidate.params if param.key == key), None)
            if (
                param is not None
                and param.default is not None
                and (template_arguments is not None or function_arguments is not None)
            ):
                flow = analysis.flow(candidate, param.default)
                flow = (
                    Flow(dict(flow.namespaces))
                    if not flow.params
                    else unknown("a default argument that depends on a parameter")
                )
                for tag, reason in flow.namespaces.items():
                    entity.walked.setdefault(
                        tag, Why(f"{candidate.label()} at {where}, with its default argument: {reason}", candidate, key)
                    )
                continue
            entity.walked.setdefault(
                UNKNOWN,
                Why(
                    f"{candidate.label()} at {where}, with no argument for the parameter that it walks", candidate, key
                ),
            )
            continue
        for argument in given:
            flow = analysis.flow(entity, argument)
            for tag, reason in flow.namespaces.items():
                entity.walked.setdefault(tag, Why(f"{candidate.label()} at {where}: {reason}", candidate, key))
            for own_key, reason in flow.params.items():
                entity.walked_params.setdefault(
                    own_key, Why(f"{candidate.label()} at {where}: {reason}", candidate, key)
                )


def evaluate_tokens(analysis: Analysis, entity: Entity) -> None:
    """Add to the summary of a macro with a body that does not parse what each name of its body gives."""
    if not WALKS.isdisjoint(entity.tokens):
        entity.walked.setdefault(
            UNKNOWN, Why(f"members_of in the body of the macro {entity.name}, which does not parse")
        )
    for name in entity.names:
        for candidate in analysis.universe.by_name.get(name, []):
            if candidate.is_eager or candidate is entity:
                continue
            for tag in candidate.walked:
                entity.walked.setdefault(tag, Why(f"{candidate.label()} in the macro {entity.name}", candidate, tag))
            if candidate.walked_params:
                key = next(iter(candidate.walked_params))
                entity.walked.setdefault(
                    UNKNOWN,
                    Why(f"{candidate.label()} in the macro {entity.name}, whose body does not parse", candidate, key),
                )


# ── Selection ────────────────────────────────────────────────────────────────


def is_extensible(universe: Universe, tag: NsTag) -> bool:
    """Say if a header in include/ can add a member to a walked namespace."""
    if tag == UNKNOWN or tag == ("",) or not isinstance(tag, tuple):
        return True
    return any(not opener.startswith(f"{CHECKS}/") for opener in universe.namespaces.get(tag, ()))


def shown_tag(tag: NsTag) -> str:
    """Return a tag as a row of the list shows it."""
    if not isinstance(tag, tuple):
        return UNKNOWN
    return "::" + "::".join(part for part in tag if part)


def unnamed_walks(universe: Universe) -> list[Entity]:
    """Return each free operator with a summary that is not empty.

    A use calls a free operator with no name, and no class owns it.  The
    analysis then cannot see which check file gets to it.  Each one makes
    each check file join, and the rule refuses it.  An operator, a
    conversion or a destructor that a class owns joins the construction
    summary of the class, and the rule does not refuse it.
    """
    return [
        entity
        for entity in universe.entities
        if entity.kind == "function"
        and entity.owner is None
        and entity.is_unnamed_call
        and (entity.walked or entity.walked_params)
    ]


def select(universe: Universe) -> dict[str, list[NsTag]]:
    """Return each check file that joins the walk units, with the namespaces that it can walk."""
    forced = unnamed_walks(universe)
    selected: dict[str, list[NsTag]] = {}
    for rel, root_entity in sorted(universe.roots.items()):
        tags = [tag for tag in root_entity.walked if is_extensible(universe, tag)]
        if forced and UNKNOWN not in tags:
            tags.append(UNKNOWN)
        if tags:
            selected[rel] = sorted(tags, key=lambda tag: (tag == UNKNOWN, shown_tag(tag)))
    return selected


def derive(context: TsastContext) -> Derivation:
    """Collect, summarize and select (THE ANALYSIS).

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    universe, failures = collect(context)
    summarize(universe)
    return Derivation(universe, select(universe), failures)


# ── The list ─────────────────────────────────────────────────────────────────


def namespaces_of(tags: list[NsTag]) -> str:
    """Return the namespace cell of a row for the tags of one check file."""
    return ", ".join(shown_tag(tag) for tag in tags)


def list_rows(cells: list[tuple[str, str, str]]) -> list[tuple[tuple[str, ...], str]]:
    """Return the rows of the list for (check file, weight, namespace cell) triples, each with its key.

    Raises:
        ValueError: If a cell cannot be written in the rows grammar
    """
    return [((rel,), rows_grammar.render([rel], [weight, namespaces])) for rel, weight, namespaces in cells]


def list_text(cells: list[tuple[str, str, str]]) -> str:
    """Return the text of the list for (check file, weight, namespace cell) triples, sorted by the check file.

    Raises:
        ValueError: If a cell cannot be written in the rows grammar, or two triples name one check file
    """
    return rows_grammar.compose(LIST_HEADER, [], list_rows(cells))


def is_weight(cell: str) -> bool:
    """Say if a cell is a weight: a number with one decimal, or unmeasured."""
    whole, point, tenths = cell.partition(".")
    return cell == UNMEASURED or (
        bool(whole)
        and bool(point)
        and len(tenths) == 1
        and all("0" <= character <= "9" for character in whole + tenths)
    )


def read_list(path: Path) -> ListRead:
    """Read the list: each row by its check file, and each problem of the list.

    The rule reads each line as a file of one line in the rows grammar.  A
    line that does not obey the grammar is then one problem, and the rule
    reads the other lines too.

    Complexity: linear in the length of the list.
    """
    if not path.is_file():
        return ListRead({}, (ListProblem(0, "no-list", "", f"{LIST} does not exist.  Run: {WRITE_COMMAND}"),))
    rows: dict[str, ListRow] = {}
    problems: list[ListProblem] = []
    form = (
        f"A row is `<layer>/<path>.cpp | weight | namespace, ...`, and a weight is a number with one decimal or "
        f"{UNMEASURED}.  Run: {WRITE_COMMAND}"
    )
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        entry = raw.strip()
        try:
            parsed = rows_grammar.parse(f"{entry}\n", LIST)
        except rows_grammar.RowsError as problem:
            detail = str(problem).removeprefix(f"{LIST}:1: ")
            problems.append(
                ListProblem(number, "malformed-row", entry, f"the row is malformed: {entry}.  {detail}.  {form}")
            )
            continue
        if not parsed:
            continue
        row = parsed[0]
        if (
            len(row.words) != 1
            or not row.words[0].endswith(".cpp")
            or len(row.cells) != 2
            or not is_weight(row.cells[0])
            or not row.cells[1]
        ):
            problems.append(ListProblem(number, "malformed-row", entry, f"the row is malformed: {entry}.  {form}"))
            continue
        check_file = row.words[0]
        if check_file in rows:
            problems.append(
                ListProblem(
                    number, "second-row", check_file, f"{check_file} has a row before this one.  Run: {WRITE_COMMAND}"
                )
            )
            continue
        rows[check_file] = ListRow(number, row.cells[0], row.cells[1])
    return ListRead(rows, tuple(problems))


# ── The findings ─────────────────────────────────────────────────────────────


def free_operator_finding(entity: Entity) -> check_report.Finding:
    """Return the error of one free operator that can walk a namespace, with its signature as the entity."""
    body = entity.body
    signature = None
    if body is not None and entity.own_function >= 0:
        definition = tsast.Node(body.tree, entity.own_function)
        inner = definition.child_by_field("declarator")
        signature = None if inner is None else tsast.function_signature(inner)
    return check_report.Finding(
        "error",
        entity.path,
        body.line if body is not None else 0,
        RULE,
        f"{entity.label()} can walk a namespace, and a use calls it with no name, so the analysis cannot select the "
        f"check files that get to it.  Move the walk into a named function",
        "free-operator",
        signature or entity.label(),
    )


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Give an error for each difference between the list and the tree, and a warning for each unmeasured row."""
    derivation = derive(context)
    listed = read_list(context.root / LIST)
    found: list[check_report.Finding] = []
    wanted = {rel: namespaces_of(tags) for rel, tags in derivation.selected.items()}
    for rel, namespaces in sorted(wanted.items()):
        row = listed.rows.get(rel)
        if row is None:
            found.append(
                check_report.Finding(
                    "error",
                    f"{CHECKS}/{rel}",
                    0,
                    RULE,
                    f"this check file can walk {namespaces}, and {LIST} has no row "
                    f"for it, so no walk unit compiles it after the headers that can add to that namespace.  Run: "
                    f"{WRITE_COMMAND}",
                    "missing-row",
                    rel,
                )
            )
        elif row.namespaces != namespaces:
            found.append(
                check_report.Finding(
                    "error",
                    LIST,
                    row.line,
                    RULE,
                    f"the row of {rel} names `{row.namespaces}`, and the tree gives "
                    f"`{namespaces}`.  Run: {WRITE_COMMAND}",
                    "other-row",
                    rel,
                )
            )
    for rel, row in sorted(listed.rows.items()):
        if rel not in wanted:
            found.append(
                check_report.Finding(
                    "error",
                    LIST,
                    row.line,
                    RULE,
                    f"{rel} walks no namespace that a header can add to, or it is not a "
                    f"check file, so a walk unit compiles it for nothing.  Run: {WRITE_COMMAND}",
                    "extra-row",
                    rel,
                )
            )
        elif row.weight == UNMEASURED:
            found.append(
                check_report.Finding(
                    "warning",
                    LIST,
                    row.line,
                    RULE,
                    f"the row of {rel} has no measured weight, so "
                    f"test/layer/CMakeLists.txt gives it the mean weight of the measured rows.  Run: {MEASURE_COMMAND}",
                    "unmeasured",
                    rel,
                )
            )
    found += [
        check_report.Finding("error", LIST, problem.line, RULE, problem.message, problem.kind, problem.entity)
        for problem in listed.problems
    ]
    found += [
        check_report.Finding(
            "error",
            failure.path,
            failure.line,
            RULE,
            f"the parse kit cannot read the file at {failure.node}, so the analysis cannot "
            f"read its walks.  The rule parse-clean gives the construct",
            PARSE_KIND,
        )
        for failure in derivation.failures
    ]
    found += [free_operator_finding(entity) for entity in unnamed_walks(derivation.universe)]
    return found
