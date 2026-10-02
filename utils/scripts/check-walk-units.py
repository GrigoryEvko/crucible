#!/usr/bin/env python3
"""check-walk-units — select the check files that the walk units compile a second time.

A check file (test/layer/checks/<layer>/<path>.cpp) holds the compile-time
checks of one header.  It includes its header and the includes of that
header, and nothing more.  A reflection walk over a namespace,
std::meta::members_of(^^name), reads the members that the translation unit
declares before the walk runs.  So a walk in a check file does not see a
member that another header adds to the namespace that it walks.
test/layer/walks_across_headers.cpp compiles such a check file a second time,
after each header that can add to such a namespace.  These are the walk
units.  This script selects the check files that the walk units compile.

THE RULE
    A check file joins the walk units when one of its checks can call
    members_of on the reflection of a namespace that a header under include/
    opens.  A header can add a member to each such namespace, also a header
    that does not exist yet.  A walk over a class, and a walk over a
    namespace that only check files open, do not make a check file join.

THE ANALYSIS
    The script reads the parse tree of each header under include/ and of
    each check file (utils/scripts/tsast.py).  It gives a summary to each
    function, function template, variable, concept, alias, class and macro:
      * the namespaces that an evaluation of it can walk.  The tag unknown
        stands for a walk whose namespace the analysis cannot name.
      * the parameters whose argument it can walk.  A template parameter
        and a function parameter each have a position.
    A check file is one entity, and each function that it defines is one
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
        first argument.  A function of std::meta that can never give a
        namespace, for example type_of or substitute, gives no walk.
      * each other expression gives unknown: parent_of, a member access,
        the result of a function of the tree, or a parameter of a lambda.

THE NAMES
    The summaries flow through the names, to a fixed point.  An entity
    that names an entity with a namespace in its summary gets that
    namespace.  An entity that calls an entity with a parameter in its
    summary gets the walk of the argument at that position.  It uses the
    default argument when the call gives none, and a name with no
    arguments gives unknown.  `std::meta::substitute(^^name, {...})` gives
    the listed values as the template arguments of name.
    The analysis has no types.  It resolves a name with these rules:
      * a qualified name is the entity with that qualified name.
      * an unqualified name is the entity that the innermost enclosing
        scope declares, with the using-declarations and using-directives in
        force.  An unqualified call that no scope resolves can reach a
        function through argument-dependent lookup.  It is then each
        function with that name that no class owns.
      * a file names only its own layer and the layers below it.
        utils/scripts/check-layer-boundary.py and the layer sentinels make
        sure of that.
      * a member that a use reaches through an object, `obj.f()`, or
        through a qualifier that the analysis cannot resolve, `T::f()`, is
        a member edge.  The analysis does not know the class, so a member
        edge in a header gives nothing.  In a check file, a member edge
        reaches each member function with that name in a class that the
        check file names.  It gives only the summary that the member gets
        from the other names.  A member function with a common name then
        cannot carry a walk from one class to another.

THE CLASSES
    A class template gets the walks of its own body: its static_asserts,
    its bases and its member aliases.  A use that needs an instance of the
    class runs these.  The body of a class that is not a template runs
    where the header defines it.  A declaration of a member function or of
    a friend runs nothing until a use calls the function.
    The constructors, the destructor, the operators and the conversion
    functions of a class run with no name at the use.  They give a second
    summary of the class, the construction summary, and an alias of a
    class gives the construction summary of the class.  A use that makes
    an object gets the construction summary: `C{...}`, `C(...)`, `new C`,
    and a declaration of an object with an initializer.  In a check file,
    each name of a class gets it, because a check that only completes the
    class, for example with sizeof, runs the constraints of its special
    members.  In a header, a name where a declaration needs a type gives
    only the walks of an alias template and of a class.
    A variable at namespace scope, an alias that is not a template, and a
    class that is not a template run where the header defines them.  They
    give no walk to the entities that name them.

WHY THE FORM IS A DERIVED LIST
    A marker in each check file would need the same analysis to find a
    check file that walks a namespace with no marker.  The marker adds a
    hand step and no safety.  The list is a derived file instead.  The test
    walk_units derives the list again on each run, and it fails when the
    list and the tree disagree.  A new walk then cannot go into the tree
    without its row.
    The analysis fails closed on each value that it cannot read.  An
    unresolved name, a parameter of a lambda, a call through a value, a
    member access and a macro body that does not parse each count as a walk
    of unknown, and their check file joins.  A free operator whose summary
    is not empty makes each check file join, and the test refuses it,
    because a use calls it with no name.

THE LIST
    test/layer/walk-checks.txt holds one row for each selected check file:

        <layer>/<path>.cpp | weight | namespace, namespace, ...

    The namespaces are the namespaces that the checks of the file can walk,
    each with its leading `::`, and unknown for a walk whose namespace the
    analysis cannot name.  `--write` writes the list again from the tree.
    It keeps the weight of each row that stays, and it gives a new row the
    weight unmeasured.

THE WEIGHTS
    The weight of a row is the share of its check file in a walk unit: the
    user instructions, in units of 10^9 with one decimal, that the check
    file adds to a unit that holds only walk_headers.h.
    test/layer/CMakeLists.txt reads the paths and the weights, and it
    divides the check files into units by weight.  `--measure BUILD_DIR`
    compiles the base unit and one unit for each check file of the list,
    with the command of a walk unit from the compile database of the build,
    and it writes each weight again.  The count is exact or absent
    (utils/scripts/cost_meter.py), so a measure on a loaded host gives the
    same weights.  A weight goes stale when a check file or a header that
    it reads changes, and a stale weight makes the units uneven.  When a
    unit then costs more than the warning threshold of the row
    compile-instructions, the test compile_instructions warns.  Measure
    again then.  The test walk_units warns about each unmeasured row, and it
    refuses a weight that is not a number with one decimal.

WHAT THE ANALYSIS CANNOT SEE
    With no types, the analysis does not follow these routes.  A walk that
    only such a route reaches does not make a check file join:
      * a member call in a header, on an object or through a template
        parameter
      * a member call in a check file on an object of a class that the file
        does not name
      * argument-dependent lookup of a function when ordinary lookup also
        finds a function with that name
      * a data member that a use reads through an object or through a
        template parameter
      * a class that a header names only as the type of a declaration.
    The kit does not preprocess, so each arm of an #if counts, and the
    analysis does not read a name that `##` builds.  The analysis does not
    follow a value through a function return.  It gives unknown for that
    value, and the route then makes more files join, and never fewer.

Usage
    check-walk-units.py [--warnings-dir DIR]   compare the tree with the list
    check-walk-units.py --write                write the list again from the tree
    check-walk-units.py --measure BUILD_DIR [--jobs N]
                                               measure the weight of each row and write the list again
    check-walk-units.py --explain CHECK_FILE   print the route of each walk of one check file
    check-walk-units.py --self-test            plant each route in a scratch tree and examine each verdict

Exit 0 when the list agrees with the tree, 1 on a difference, a malformed
row, a parse failure or a failed measure, 2 on a usage error or a failed
self-test, 3 when the kit is not installed.
"""

from __future__ import annotations

import argparse
import bisect
import concurrent.futures
import contextlib
import io
import json
import multiprocessing
import os
import re
import shlex
import sys
import tempfile
from collections.abc import Iterator
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
import cost_meter  # noqa: E402
import tsast  # noqa: E402

SCRIPT = "utils/scripts/check-walk-units.py"
LIST = "test/layer/walk-checks.txt"
CHECK = "walk-units"
INCLUDE = "include"
CHECKS = "test/layer/checks"
SEPARATOR = " | "
# The tag of a walk whose namespace the analysis cannot name.
UNKNOWN = "unknown"
# The weight of a row that no measure gave a weight.
UNMEASURED = "unmeasured"
# A measured weight: instructions in units of 10^9, with one decimal.
WEIGHT_FORM = re.compile(r"[0-9]+\.[0-9]")
# The walk unit objects in the compile database, and the include directory of
# the walk_checks.h of one unit.
UNIT_OUTPUT = "/layer_walks_across_headers_"
UNIT_INCLUDE = re.compile(r"-I(.*/test/layer/walk_units/[0-9]+)")
# The default number of measure compiles at one time.
MEASURE_JOBS = 8
# The functions that walk the members of a namespace.  Only members_of takes
# a namespace.  The other member queries of std::meta take a class.
WALKS = frozenset({"members_of"})
# The functions whose result holds the reflections of their first argument,
# or the members of it.
FOLLOWS_ARGUMENT = frozenset({"members_of", "define_static_array", "dealias"})
# The functions of std::meta whose result is never the reflection of a
# namespace.
NEVER_NAMESPACE = frozenset({
    "type_of", "remove_cvref", "remove_cv", "remove_const", "remove_volatile", "remove_reference",
    "remove_pointer", "remove_extent", "remove_all_extents", "add_const", "add_volatile", "add_cv",
    "add_pointer", "add_lvalue_reference", "add_rvalue_reference", "decay", "underlying_type",
    "template_of", "substitute", "reflect_constant", "reflect_object", "reflect_function",
    "reflect_constant_array", "reflect_constant_string", "return_type_of", "template_arguments_of",
    "nonstatic_data_members_of", "static_data_members_of", "enumerators_of", "bases_of", "subobjects_of",
    "parameters_of", "annotations_of", "annotations_of_with_type", "object_of", "constant_of",
    "variable_of", "identifier_of", "display_string_of", "u8identifier_of", "u8display_string_of",
    "size_of", "alignment_of", "bit_size_of", "offset_of", "source_location_of", "can_substitute",
    "define_static_string", "define_static_object",
})
# The leaves that can name an entity.
NAME_LEAVES = ("identifier", "type_identifier", "field_identifier")
CLASS_SPECIFIERS = ("class_specifier", "struct_specifier", "union_specifier")
CONTAINERS = frozenset({
    "translation_unit", "declaration_list", "preproc_if", "preproc_ifdef", "preproc_else", "preproc_elif",
    "preproc_elifdef",
})
TEMPLATE_NAMES = frozenset({"template_function", "template_type", "template_method"})
DECLARATORS = frozenset({
    "function_declarator", "init_declarator", "pointer_declarator", "reference_declarator", "array_declarator",
    "attributed_declarator", "parenthesized_declarator",
})
LITERALS = frozenset({
    "number_literal", "string_literal", "raw_string_literal", "concatenated_string", "char_literal", "true",
    "false", "nullptr", "user_defined_literal",
})
OPERATORS = frozenset({"unary_expression", "binary_expression", "sizeof_expression", "alignof_expression"})
LIST_HEADER = (
    "# test/layer/walk-checks.txt — the check files that the walk units compile a second time.\n"
    "# utils/scripts/check-walk-units.py --write writes this file from the tree, and the test\n"
    "# walk_units fails when the file and the tree disagree.  Do not edit it by hand.\n"
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

NsTag = tuple[str, ...] | str
ParamKey = tuple[str, int]


@dataclass(frozen=True)
class Why:
    """The first reason for one tag of a summary.

    text names the step.  entity and key name the summary that the step
    reads, or are None when the step is a call of members_of.
    """

    text: str
    entity: Entity | None = None
    key: NsTag | ParamKey | None = None


@dataclass(eq=False)
class Param:
    """One template parameter or function parameter of an entity.

    key is ("t", position) for a template parameter and ("f", position) for
    a function parameter or a macro parameter.  A type parameter can never
    hold a namespace.
    """

    name: str
    key: ParamKey
    default: tsast.Node | None
    is_pack: bool
    is_type: bool


@dataclass(eq=False)
class Entity:
    """One thing that a name can reach, with its summary.

    kind is function, variable, concept, alias, class, macro or file.  The
    body is the node that the analysis reads, and skips holds the index
    ranges of the body tree that belong to another entity.  An eager entity
    runs where the header defines it, so a name of it gives nothing.  A
    class that is not a template does not scan its body for the same
    reason, and gives only its construction summary.  own_template is the index of
    the template header whose parameters are the parameters of the entity,
    and own_function is the index of the function definition whose
    parameters are its function parameters.  Each one is -1 when the entity
    has none.
    """

    name: str
    qual: tuple[str, ...]
    kind: str
    path: str
    body: tsast.Node | None
    params: list[Param] = field(default_factory=list)
    skips: list[tuple[int, int]] = field(default_factory=list)
    is_eager: bool = False
    is_local: bool = False
    owner: Entity | None = None
    is_unnamed_member: bool = False
    scans_body: bool = True
    own_template: int = -1
    own_function: int = -1
    tokens: tuple[str, ...] = ()
    walked: dict[NsTag, Why] = field(default_factory=dict)
    walked_params: dict[ParamKey, Why] = field(default_factory=dict)
    precise_walked: dict[NsTag, Why] = field(default_factory=dict)
    precise_params: dict[ParamKey, Why] = field(default_factory=dict)
    constructed: dict[NsTag, Why] = field(default_factory=dict)
    precise_constructed: dict[NsTag, Why] = field(default_factory=dict)
    names: frozenset[str] = frozenset()
    leaves: list[tuple[tsast.Node, str]] = field(default_factory=list)
    walk_calls: list[tsast.Node] | None = None
    unnamed_members: list[Entity] = field(default_factory=list)

    def size(self) -> tuple[int, int, int]:
        """Return the sizes of the summary, which only grow."""
        return len(self.walked), len(self.walked_params), len(self.constructed)

    def is_skipped(self, index: int) -> bool:
        """Say whether a node of the body belongs to another entity."""
        if not self.skips:
            return False
        at = bisect.bisect_right(self.skips, (index, 1 << 62)) - 1
        return at >= 0 and self.skips[at][0] <= index < self.skips[at][1]

    def nodes(self, *types: str) -> Iterator[tsast.Node]:
        """Yield each node of the body of one of the types, outside the skipped ranges."""
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


@dataclass
class Flow:
    """What an expression can carry to a walk: namespaces, unknown, and parameters of its entity."""

    namespaces: dict[NsTag, str] = field(default_factory=dict)
    params: dict[ParamKey, str] = field(default_factory=dict)

    def merge(self, other: Flow) -> None:
        """Add the tags of another flow, and keep the first reason of each tag."""
        for tag, reason in other.namespaces.items():
            self.namespaces.setdefault(tag, reason)
        for key, reason in other.params.items():
            self.params.setdefault(key, reason)


def unknown(reason: str) -> Flow:
    """Return a flow that holds only the unknown tag, with its reason."""
    return Flow({UNKNOWN: reason})


@dataclass
class Universe:
    """Everything that the analysis reads from the tree.

    namespaces maps each namespace to the files that open it.  quals holds
    the qualified name of each class, enumeration, enumerator, function,
    variable, alias and concept.  by_name maps a last name to the entities
    of the headers.  local maps each check file to its own entities by
    last name, and roots maps each check file to its file entity.
    """

    namespaces: dict[tuple[str, ...], set[str]] = field(default_factory=dict)
    quals: set[tuple[str, ...]] = field(default_factory=set)
    entities: list[Entity] = field(default_factory=list)
    by_name: dict[str, list[Entity]] = field(default_factory=dict)
    local: dict[str, dict[str, list[Entity]]] = field(default_factory=dict)
    roots: dict[str, Entity] = field(default_factory=dict)
    aliases: dict[str, list[tsast.NamespaceAlias]] = field(default_factory=dict)
    shared_aliases: list[tsast.NamespaceAlias] = field(default_factory=list)
    usings: dict[str, list[tsast.UsingDecl]] = field(default_factory=dict)
    failures: list[tuple[str, str]] = field(default_factory=list)


# ── Collection ───────────────────────────────────────────────────────────────

def function_declarator(node: tsast.Node) -> tsast.Node | None:
    """Return the function_declarator of a function definition or a declarator, or None."""
    current: tsast.Node | None = node
    if current.type in ("function_definition", "declaration", "field_declaration"):
        current = current.child_by_field("declarator")
    while current is not None and current.type != "function_declarator":
        if current.type not in DECLARATORS:
            return None
        inner = current.child_by_field("declarator")
        if inner is None and current.type == "parenthesized_declarator":
            named = tsast.non_comment_children(current)
            inner = named[0] if named else None
        current = inner
    return current


def template_item(template: tsast.Node) -> tsast.Node | None:
    """Return the declaration that a template_declaration introduces."""
    for child in tsast.non_comment_children(template):
        if child.field == "parameters" or child.type == "requires_clause":
            continue
        return child
    return None


def template_params(template: tsast.Node) -> list[Param]:
    """Return the template parameters of a template_declaration, in order."""
    found: list[Param] = []
    holder = template.child_by_field("parameters")
    if holder is None:
        return found
    for position, item in enumerate(tsast.non_comment_children(holder)):
        kind = item.type
        is_type = "type_parameter" in kind or kind == "template_template_parameter_declaration"
        is_pack = kind.startswith("variadic")
        default = item.child_by_field("default_value") or item.child_by_field("default_type")
        name_node = item.child_by_field("declarator") or item.child_by_field("name")
        if name_node is None:
            leaves = item.children_of_type("type_identifier", "identifier")
            name_node = leaves[-1] if leaves and is_type else None
        name = "" if name_node is None else tsast.leaf_name(name_node) or ""
        found.append(Param(name, ("t", position), default, is_pack, is_type))
    return found


def function_params(function: tsast.Node) -> list[Param]:
    """Return the function parameters of a function, in order."""
    found: list[Param] = []
    for position, (name, node) in enumerate(tsast.parameters(function)):
        found.append(Param(name, ("f", position), node.child_by_field("default_value"),
                           node.type == "variadic_parameter_declaration", False))
    return found


def declared_name(declarator: tsast.Node | None) -> tuple[str, tuple[str, ...]]:
    """Return the last part of a declared name and the qualifier before it."""
    if declarator is None:
        return "", ()
    parts = tsast.qualified_parts(declarator)
    if parts is None or not parts[1]:
        return tsast.leaf_name(declarator) or "", ()
    return parts[1][-1], parts[1][:-1]


def is_declaration_only(member: tsast.Node) -> bool:
    """Say whether a member of a class body declares a function or a friend and defines nothing.

    An instance of the class does not run the constraints of such a
    declaration.  A use of the function runs them, and the analysis reads
    the definition of the function for that use.
    """
    item = member
    if item.type == "template_declaration":
        item = template_item(item) or item
    if item.type == "friend_declaration":
        return True
    return item.type in ("field_declaration", "declaration") and function_declarator(item) is not None


def is_unnamed_call(name: str) -> bool:
    """Say whether a use calls a function of this name with no name: an operator, a conversion or a destructor."""
    return name.startswith("operator") or name.startswith("~")


def runs_unnamed(name: str, class_name: str) -> bool:
    """Say whether an object of a class runs a member of this name with no name: a constructor or an unnamed call."""
    return name == class_name or is_unnamed_call(name)


class Collector:
    """Collect the entities of one tree."""

    def __init__(self, tree: tsast.Tree, path: str, is_local: bool, universe: Universe) -> None:
        """Bind the collector to one tree."""
        self.tree = tree
        self.path = path
        self.is_local = is_local
        self.universe = universe
        self.found: list[Entity] = []

    def add(self, entity: Entity) -> Entity:
        """Record one entity and its qualified name."""
        self.found.append(entity)
        if entity.qual:
            self.universe.quals.add(entity.qual)
        return entity

    def scope(self, node: tsast.Node) -> tuple[str, ...]:
        """Return the qualified name of the innermost scope around a node."""
        return tsast.scope_levels(node)[0]

    def visit(self, container: tsast.Node, owner: Entity | None, in_template: bool) -> None:
        """Collect the entities of each item of a namespace body or a translation unit."""
        for item in tsast.non_comment_children(container):
            self.item(item, item, [], -1, owner, in_template)

    def item(self, item: tsast.Node, outer: tsast.Node, params: list[Param], own_template: int,
             owner: Entity | None, in_template: bool) -> None:
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
            inner = template_item(item)
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
            self.add(Entity(name, self.scope(item) + scope + (name,), "alias", self.path, outer, params,
                            is_eager=not in_template and not self.is_local, is_local=self.is_local,
                            owner=owner, own_template=own_template))
        elif kind == "concept_definition":
            name, scope = declared_name(item.child_by_field("name"))
            self.add(Entity(name, self.scope(item) + scope + (name,), "concept", self.path, outer, params,
                            is_local=self.is_local, owner=owner, own_template=own_template))
        elif kind == "friend_declaration":
            # A hidden friend is a function of the enclosing namespace that
            # argument-dependent lookup finds, so it has no owner.
            for inner in tsast.non_comment_children(item):
                if inner.type in ("function_definition", "template_declaration"):
                    self.item(inner, outer if outer is not item else inner, params, own_template, None, in_template)

    def enumeration(self, item: tsast.Node) -> None:
        """Record the qualified names of an enumeration and its enumerators."""
        name, scope = declared_name(item.child_by_field("name"))
        if not name:
            return
        qual = self.scope(item) + scope + (name,)
        self.universe.quals.add(qual)
        body = item.child_by_field("body")
        for enumerator in body.children_of_type("enumerator") if body is not None else []:
            label = enumerator.child_by_field("name")
            if label is not None:
                self.universe.quals.add(qual + (tsast.leaf_name(label) or "",))
                self.universe.quals.add(qual[:-1] + (tsast.leaf_name(label) or "",))

    def klass(self, item: tsast.Node, outer: tsast.Node, params: list[Param], own_template: int,
              owner: Entity | None, in_template: bool) -> None:
        """Collect a class, its members and the ranges of its body that its members own."""
        body = item.child_by_field("body")
        name, scope = declared_name(item.child_by_field("name"))
        if body is None:
            return
        is_template = in_template or bool(params)
        entity = None
        if name:
            # The body of a class that is not a template runs where the
            # header defines it.  The body of a class template runs where a
            # use needs an instance of it: its static_asserts, its bases and
            # its member aliases.
            entity = self.add(Entity(name, self.scope(item) + scope + (name,), "class", self.path, outer, params,
                                     is_local=self.is_local, owner=owner, own_template=own_template,
                                     scans_body=is_template or self.is_local))
        before = len(self.found)
        for member in tsast.non_comment_children(body):
            self.item(member, member, [], -1, entity if entity is not None else owner, is_template)
        if entity is None:
            return
        for member in tsast.non_comment_children(body):
            if is_declaration_only(member):
                entity.skips.append((member.index, self.tree.subtree_end(member.index)))
        for member in self.found[before:]:
            if member.body is not None and member.body.tree is self.tree and member.kind not in ("class", "alias"):
                entity.skips.append((member.body.index, self.tree.subtree_end(member.body.index)))
            if member.owner is entity and member.kind == "function" and runs_unnamed(member.name, name):
                member.is_unnamed_member = True
        for nested in body.descendants(*CLASS_SPECIFIERS):
            if nested.child_by_field("body") is not None:
                entity.skips.append((nested.index, self.tree.subtree_end(nested.index)))

    def function(self, item: tsast.Node, outer: tsast.Node, params: list[Param], own_template: int,
                 owner: Entity | None) -> None:
        """Collect a function definition."""
        declarator = function_declarator(item)
        name, scope = declared_name(None if declarator is None else declarator.child_by_field("declarator"))
        if not name:
            return
        self.add(Entity(name, self.scope(item) + scope + (name,), "function", self.path, outer,
                        params + function_params(item), is_local=self.is_local, owner=owner,
                        own_template=own_template, own_function=item.index))

    def declaration(self, item: tsast.Node, outer: tsast.Node, params: list[Param], own_template: int,
                    owner: Entity | None, in_template: bool) -> None:
        """Collect the class of a declaration and each variable that it defines."""
        type_node = item.child_by_field("type")
        if type_node is not None and type_node.type in (*CLASS_SPECIFIERS, "enum_specifier") \
                and type_node.child_by_field("body") is not None:
            self.item(type_node, type_node, params, own_template, owner, in_template)
        for declarator in item.children:
            if declarator.field != "declarator" or function_declarator(declarator) is not None:
                continue
            if declarator.type != "init_declarator" and item.child_by_field("default_value") is None:
                continue
            target = declarator.child_by_field("declarator") if declarator.type == "init_declarator" else declarator
            name, scope = declared_name(target)
            if not name:
                continue
            is_eager = not in_template and not self.is_local and (owner is None or owner.own_template < 0)
            self.add(Entity(name, self.scope(item) + scope + (name,), "variable", self.path, outer, params,
                            is_eager=is_eager, is_local=self.is_local, owner=owner, own_template=own_template))


def merge_ranges(ranges: list[tuple[int, int]]) -> list[tuple[int, int]]:
    """Return the union of index ranges as sorted ranges that do not overlap."""
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
    params = [Param(name, ("f", position), None, name == "__VA_ARGS__", False)
              for position, name in enumerate(body.params)]
    if body.is_parsed:
        return Entity(body.name, (body.name,), "macro", path, body.root, params, is_local=is_local)
    tokens = tuple(token.text for token in tsast.pp_tokens(body.text, body.first_row) if token.kind == "identifier")
    return Entity(body.name, (body.name,), "macro", path, None, params, is_local=is_local, tokens=tokens)


def collect(root: Path) -> Universe:
    """Read every header under include/ and every check file, and collect their entities.

    Complexity: linear in the number of nodes of the trees.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    universe = Universe()
    base = root / INCLUDE
    headers = sorted(path for path in base.rglob("*.h") if tsast.is_in_cpp_scope(path.relative_to(root))) \
        if base.is_dir() else []
    checks_root = root / CHECKS
    checks = sorted(checks_root.rglob("*.cpp")) if checks_root.is_dir() else []
    header_trees: list[tsast.Tree] = []
    check_trees: list[tsast.Tree] = []
    for paths, kept in ((headers, header_trees), (checks, check_trees)):
        for tree in tsast.parse(paths, strict=False):
            rel = Path(tree.path).relative_to(root).as_posix()
            if tree.diagnostic is not None:
                universe.failures.append((rel, f"the parser cannot read this file, so the analysis cannot read its "
                                               f"walks.  {' '.join(tree.diagnostic.split())}"))
                continue
            kept.append(tree)
    for tree in header_trees + check_trees:
        rel = Path(tree.path).relative_to(root).as_posix()
        is_local = rel.startswith(CHECKS + "/")
        record_namespaces(tree, rel, universe)
        universe.aliases[rel] = tsast.namespace_aliases(tree)
        universe.usings[rel] = tsast.using_names(tree)
        if not is_local:
            universe.shared_aliases += [alias for alias in universe.aliases[rel]
                                        if alias.scope.type in ("translation_unit", "declaration_list")]
        collector = Collector(tree, rel, is_local, universe)
        collector.visit(tree.root, None, False)
        for entity in collector.found:
            entity.skips = merge_ranges(entity.skips)
        if is_local:
            rel_check = Path(rel).relative_to(CHECKS).as_posix()
            skips = [(entity.body.index, tree.subtree_end(entity.body.index)) for entity in collector.found
                     if entity.kind in ("function", "concept") and entity.body is not None]
            file_entity = Entity(rel_check, (), "file", rel, tree.root, skips=merge_ranges(skips), is_local=True)
            universe.roots[rel_check] = file_entity
            collector.found.append(file_entity)
            local = universe.local.setdefault(rel, {})
            for entity in collector.found:
                if entity.kind != "file":
                    local.setdefault(entity.name, []).append(entity)
        universe.entities.extend(collector.found)
    for tree_list, is_local in ((header_trees, False), (check_trees, True)):
        for body in tsast.macro_bodies(tree_list):
            rel = Path(body.define.tree.path).relative_to(root).as_posix()
            entity = macro_entity(body, rel, is_local)
            universe.entities.append(entity)
            if is_local:
                universe.local.setdefault(rel, {}).setdefault(entity.name, []).append(entity)
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
                entity.is_unnamed_member = runs_unnamed(entity.name, klass.name)
                break
        if entity.owner is not None and entity.is_unnamed_member:
            entity.owner.unnamed_members.append(entity)
    return universe


# ── Names and flow ───────────────────────────────────────────────────────────

class Analysis:
    """The summaries of all entities, and the questions that build them."""

    def __init__(self, universe: Universe) -> None:
        """Start an analysis over a collected universe."""
        self.universe = universe
        self.binding_memo: dict[tuple[int, int, int], tuple[str, object] | None] = {}
        self.resolve_memo: dict[tuple[int, int, int, str], tuple[list[Entity], bool]] = {}
        self.flow_memo: dict[tuple[int, int, int], Flow] = {}
        self.active: set[tuple[int, int, int]] = set()
        self.named_memo: dict[str, set[int]] = {}

    # Name lookup.

    def candidates(self, entity: Entity, leaf: tsast.Node, name: str) -> tuple[list[Entity], bool]:
        """Return the entities that a leaf can name, and whether the name reaches them through a member edge.

        THE NAMES in the docstring of this file gives the rules.
        """
        key = (id(entity), id(leaf.tree), leaf.index, name)
        found = self.resolve_memo.get(key)
        if found is None:
            found = self.resolve(entity, leaf, name)
            self.resolve_memo[key] = found
        return found

    def resolve(self, entity: Entity, leaf: tsast.Node, name: str) -> tuple[list[Entity], bool]:
        """Resolve one leaf with no memo.

        A file names only its own layer and the layers below it, so a
        candidate of a higher layer never matches.
        """
        level = layer_of(entity.path)
        pool = [candidate for candidate in name_pool(self.universe, entity, name)
                if layer_of(candidate.path) <= level]
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
        if entity.kind == "macro":
            # A macro body has no scope until it expands, so an unqualified
            # name in it is each free entity with that name.
            if len(parts) == 1:
                return free, False
            parts = tsast.resolve_namespace(parts, None, self.universe.shared_aliases, is_global=is_global)
            levels: tuple[tuple[str, ...], ...] = ((),)
            usings: tuple[tuple[tuple[str, ...], bool, tuple[str, ...]], ...] = ()
        else:
            aliases = self.universe.aliases.get(entity.path, []) + self.universe.shared_aliases
            if len(parts) >= 2:
                parts = tsast.resolve_namespace(parts, leaf, aliases, is_global=is_global)
            site = tsast.lookup_site_of_parts(leaf, is_global, tuple(parts), aliases,
                                              self.universe.usings.get(entity.path, []))
            levels = ((),) if is_global else site.levels
            usings = site.usings
        for level in levels:
            tried = [level + tuple(parts)]
            for using_level, is_directive, target in usings:
                if using_level != level:
                    continue
                if is_directive:
                    tried.append(target + tuple(parts))
                elif target and target[-1] == parts[0]:
                    tried.append(target + tuple(parts[1:]))
            hits = [candidate for candidate in pool if candidate.qual in tried]
            if hits:
                # Inside a class, its own name is the injected class name,
                # so it names the class and not a constructor.
                classes = [candidate.owner for candidate in hits
                           if candidate.owner is not None and candidate.name == candidate.owner.name]
                kept = [candidate for candidate in hits if candidate.owner is None or candidate.name
                        != candidate.owner.name]
                return kept + [klass for klass in classes if klass not in kept], False
            if len(parts) >= 2 and any(name[:-1] in self.universe.namespaces for name in tried):
                return [], False
        if len(parts) >= 2:
            return (members if is_called else []), True
        return (free if is_called else []), False

    def named_classes(self, entity: Entity) -> set[int]:
        """Return the identity of each class that the check file of a local entity names, through a precise name.

        A member call on an object in a check file reaches a member of one
        of these classes, because the file names the class of the object
        to make it.
        """
        found = self.named_memo.get(entity.path)
        if found is not None:
            return found
        found = set()
        root_entity = next((item for item in self.universe.roots.values() if item.path == entity.path), None)
        if root_entity is not None and root_entity.body is not None:
            for leaf in root_entity.body.descendants(*NAME_LEAVES):
                name = tsast.leaf_name(leaf)
                if name is None or not any(candidate.kind == "class"
                                           for candidate in name_pool(self.universe, root_entity, name)):
                    continue
                candidates, is_member_edge = self.candidates(root_entity, leaf, name)
                if not is_member_edge:
                    found.update(id(candidate) for candidate in candidates if candidate.kind == "class")
        self.named_memo[entity.path] = found
        return found

    def binding(self, entity: Entity, node: tsast.Node, name: str) -> tuple[str, object] | None:
        """Return what binds a name inside the body of an entity, or None when the body does not bind it.

        The kinds: ("param", Param) for a parameter of the entity, ("element",
        range node) for the variable of a range loop, ("local", list of value
        nodes) for a local variable, ("opaque", text) for a binding whose
        value the analysis cannot read, and ("type", text) for a type
        parameter or a local type alias.
        """
        key = (id(entity), id(node.tree), node.index)
        if key in self.binding_memo:
            return self.binding_memo[key]
        found = self.find_binding(entity, node, name)
        self.binding_memo[key] = found
        return found

    def find_binding(self, entity: Entity, node: tsast.Node, name: str) -> tuple[str, object] | None:
        """Walk the scopes around a node, innermost first, and return the binding of a name.

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
                    return ("element", current.child_by_field("right"))
            elif kind in ("compound_statement", "condition_clause", "for_statement"):
                value = local_value(current, child, name)
                if value is not None:
                    return value
            elif kind in ("lambda_expression", "requires_expression"):
                if name in parameter_names(current):
                    return ("opaque", "a parameter of a lambda or of a requires-expression")
            elif kind == "function_definition":
                if name in {parameter for parameter, _node in tsast.parameters(current)}:
                    own = next((param for param in entity.params if param.key[0] == "f" and param.name == name), None)
                    if current.index == entity.own_function and own is not None:
                        return ("param", own)
                    return ("opaque", "a parameter of an enclosing function")
            elif kind == "template_declaration":
                for param in template_params(current):
                    if param.name != name:
                        continue
                    own = next((item for item in entity.params if item.key[0] == "t" and item.name == name), None)
                    if current.index == entity.own_template and own is not None:
                        return ("param", own)
                    return ("type", "a template parameter") if param.is_type else \
                        ("opaque", "a template parameter of an enclosing template")
            if entity.body is not None and current == entity.body:
                break
            child = current
            current = current.parent
        if entity.kind == "macro":
            own = next((param for param in entity.params if param.name == name), None)
            if own is not None:
                return ("param", own)
        return None

    # Reflections.

    def reflected(self, entity: Entity, expression: tsast.Node) -> Flow:
        """Return the flow of a reflection `^^X`."""
        inner = next(iter(tsast.non_comment_children(expression)), None)
        if inner is None:
            return Flow({("",): "the reflection of the global namespace"}) \
                if tsast.spelled(expression) == "^^::" else unknown(f"the reflection {tsast.excerpt(expression)}")
        target = inner.child_by_field("type") if inner.type == "type_descriptor" else inner
        if target is None:
            return unknown(f"the reflection {tsast.excerpt(expression)}")
        if target.type in ("primitive_type", "sized_type_specifier", "template_type", "template_function",
                           "decltype", "placeholder_type_specifier"):
            return Flow()
        if inner.type == "type_descriptor" and len(tsast.non_comment_children(inner)) > 1:
            return Flow()
        last = target
        while last.type == "qualified_identifier" and last.child_by_field("name") is not None:
            last = last.child_by_field("name")
        if last.type in TEMPLATE_NAMES:
            return Flow()
        spelled = tsast.qualified_parts(target)
        if spelled is None:
            return unknown(f"the reflection {tsast.excerpt(expression)}")
        is_global, parts = spelled
        if not is_global and len(parts) == 1:
            bound = self.binding(entity, target, parts[0])
            if bound is not None:
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
        if is_global or entity.kind == "macro":
            levels: tuple[tuple[str, ...], ...] = ((),)
            usings: list[tuple[tuple[str, ...], tuple[str, ...]]] = []
        else:
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
        """Return what an expression in the body of an entity can carry to a walk."""
        key = (id(entity), id(expression.tree), expression.index)
        found = self.flow_memo.get(key)
        if found is not None:
            return found
        if key in self.active or depth > 24:
            return unknown(f"a value that depends on itself: {tsast.excerpt(expression)}")
        self.active.add(key)
        try:
            found = self.compute_flow(entity, expression, depth)
        finally:
            self.active.discard(key)
        self.flow_memo[key] = found
        return found

    def compute_flow(self, entity: Entity, expression: tsast.Node, depth: int) -> Flow:
        """Compute the flow of one expression, with no memo."""
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
        if kind == "cast_expression" or kind == "static_cast_expression":
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
        if name in NEVER_NAMESPACE or (name is not None and (name.startswith("is_") or name.startswith("has_"))):
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
        variables = [candidate for candidate in self.candidates(entity, expression, parts[-1])[0]
                     if candidate.kind == "variable"]
        if not variables:
            return unknown(f"the value {'::'.join(parts)}, which names no variable that the analysis reads")
        for candidate in variables:
            value = initializer(candidate)
            if value is None:
                result.merge(unknown(f"the variable {candidate.label()}, which has no initializer"))
                continue
            inner = self.flow(candidate, value, depth + 1)
            result.merge(Flow(dict(inner.namespaces), {}))
            if inner.params:
                result.merge(unknown(f"the variable {candidate.label()}, whose value depends on a parameter"))
        return result

    def bound_flow(self, entity: Entity, bound: tuple[str, object], name: str, depth: int) -> Flow:
        """Return the flow of a name that the body of an entity binds."""
        kind, detail = bound
        if kind == "param":
            param = detail
            assert isinstance(param, Param)
            return Flow() if param.is_type else Flow({}, {param.key: f"the parameter {name}"})
        if kind == "type":
            return Flow()
        if kind == "element":
            if detail is None:
                return unknown(f"the variable {name} of a range loop with no range")
            assert isinstance(detail, tsast.Node)
            return self.flow(entity, detail, depth + 1)
        if kind == "local":
            assert isinstance(detail, list)
            if not detail:
                return unknown(f"the local {name}, which gets no value that the analysis reads")
            result = Flow()
            for value in detail:
                result.merge(self.flow(entity, value, depth + 1))
            return result
        return unknown(f"{detail} ({name})")


# The member functions that put a value into a container.
MUTATORS = frozenset({"push_back", "emplace_back", "push_front", "emplace_front", "insert", "emplace", "append",
                      "assign"})


def local_value(scope: tsast.Node, child: tsast.Node, name: str) -> tuple[str, object] | None:
    """Return the binding of a name that a declaration in a block declares before a child, or None.

    The value of a local binding is its initializer, with each value that an
    assignment or a call of a mutator such as push_back puts into it later
    in the block.
    """
    for item in tsast.non_comment_children(scope):
        if item.start >= child.start and item != child:
            break
        if item.type in ("alias_declaration", "type_definition"):
            declared = item.child_by_field("name") or item.child_by_field("declarator")
            if declared is not None and tsast.leaf_name(declared) == name:
                return ("type", "a local type alias")
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
                    return ("local", ([] if value is None else [value]) + stored_values(scope, name))
            elif declarator.type == "structured_binding_declarator":
                if name in {tsast.leaf_name(part) for part in tsast.non_comment_children(declarator)}:
                    return ("opaque", "a structured binding")
            elif tsast.leaf_name(declarator) == name:
                return ("local", stored_values(scope, name))
    return None


def stored_values(scope: tsast.Node, name: str) -> list[tsast.Node]:
    """Return each value that a block assigns to a local, or puts into it with a mutator."""
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
    """Return the names of the parameters and template parameters of a lambda or of a requires-expression."""
    names: set[str] = set()
    holders: list[tsast.Node] = []
    declarator = owner.child_by_field("declarator")
    if declarator is not None and declarator.child_by_field("parameters") is not None:
        holders.append(declarator.child_by_field("parameters"))
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


DECLARED_NAME_OWNERS = frozenset({
    "function_declarator", "init_declarator", "parameter_declaration", "optional_parameter_declaration",
    "variadic_parameter_declaration", "field_declaration", "declaration", "class_specifier", "struct_specifier",
    "union_specifier", "enum_specifier", "concept_definition", "alias_declaration", "type_definition",
    "pointer_declarator", "reference_declarator", "array_declarator", "attributed_declarator",
    "type_parameter_declaration", "optional_type_parameter_declaration", "variadic_type_parameter_declaration",
    "enumerator", "namespace_alias_definition", "structured_binding_declarator",
})


def is_declared_name(leaf: tsast.Node) -> bool:
    """Say whether a leaf is the name that a declaration declares, or the name in a reflection that runs nothing.

    A reflection `^^name` names an entity and runs no part of it.  Only
    std::meta::substitute makes an instance of a reflected template, and
    use_site() reads that form.
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
        holder = template_item(holder) or holder
    for declarator in holder.children:
        if declarator.field != "declarator":
            continue
        if declarator.type == "init_declarator":
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
        # The kit does not split the arguments of a macro invocation, so the
        # whole token tree is the first argument, and the analysis cannot
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
    while parent is not None and parent.type in ("qualified_identifier", "type_descriptor") \
            and node.field in ("name", "type", None):
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
            name = None if item.child_by_field("function") is None else tsast.leaf_name(item.child_by_field("function"))
            given = item.child_by_field("arguments")
            first = next(iter(tsast.non_comment_children(given)), None) if given is not None else None
            if name in ("reflect_constant", "reflect_object") and first is not None:
                inner = first
        values.append(inner)
    return values


def arguments_at(callee: Entity, key: ParamKey, template_arguments: list[tsast.Node] | None,
                 function_arguments: list[tsast.Node] | None) -> list[tsast.Node] | None:
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
    return given[position:position + 1] if position < len(given) else None


# ── The fixed point ──────────────────────────────────────────────────────────

def summarize(universe: Universe) -> Analysis:
    """Compute the summary of each entity, to a fixed point, in two layers.

    The precise layer follows each name except a member edge.  The full
    layer starts from the precise summaries.  It follows the same names,
    and in a check file each member edge gives the precise summary of its
    target.  A member edge, whose class the analysis does not know, then
    cannot carry a walk that another member edge reached.

    Complexity: the analysis reads an entity again only when an entity that
    it names grows.  A summary only grows, so the work is at most the
    number of tags times the number of references.
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
                if name in all_names and name not in WALKS and not is_declared_name(leaf):
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
    """Read each entity again until no summary grows."""
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
            entity.constructed.setdefault(tag, Why(f"the member {member.label()}, which an object of the class runs "
                                                   f"with no name", member, tag))
        for key in member.walked_params:
            entity.constructed.setdefault(UNKNOWN, Why(f"the member {member.label()} walks a parameter, and the "
                                                       f"analysis does not map the arguments of a call with no "
                                                       f"name", member, key))
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
    names = {name for name in entity.names
             if any(candidate.walked or candidate.walked_params or candidate.constructed
                    for candidate in name_pool(analysis.universe, entity, name))}
    if not names:
        return
    for leaf, name in entity.leaves:
        if name not in names:
            continue
        if analysis.binding(entity, leaf, name) is not None:
            continue
        found, is_member_edge = analysis.candidates(entity, leaf, name)
        if is_member_edge:
            if not (is_full and entity.is_local):
                continue
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
                for tag, why in constructed.items():
                    entity.constructed.setdefault(tag, Why(f"{candidate.label()} at {entity.path}:{leaf.line}",
                                                           candidate, tag))
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


TYPE_POSITION_ENDS = frozenset({
    "declaration", "field_declaration", "parameter_declaration", "optional_parameter_declaration",
    "function_definition", "template_declaration", "compound_statement", "base_class_clause", "alias_declaration",
    "type_definition", "translation_unit",
})


def is_type_position(leaf: tsast.Node) -> bool:
    """Say whether a leaf names a type where a declaration, a cast or a base clause needs one.

    A name in a template argument list or in a reflection can be a value,
    so it is not a type position.
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
    """Say whether a call expression calls members_of, with a qualifier, with none, or through std::meta."""
    function = call.child_by_field("function")
    return function is not None and function.type != "field_expression" and tsast.leaf_name(function) in WALKS


def is_construction(leaf: tsast.Node) -> bool:
    """Say whether a name stands where a use constructs an object of the class that it names.

    The forms are `C{...}`, `C(...)`, `new C`, and a declaration of an
    object of type C with an initializer.
    """
    node = leaf
    parent = node.parent
    while parent is not None and (parent.type in ("qualified_identifier", *TEMPLATE_NAMES)) and node.field == "name":
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
            child.field == "declarator" and child.type == "init_declarator" for child in parent.children)
    return False


LAYERS = ("foundation", "fixy", "crucible")


def layer_of(path: str) -> int:
    """Return the rank of the layer of a file: 0 for foundation, 1 for fixy, 2 for each other file."""
    for prefix in (f"{INCLUDE}/", f"{CHECKS}/"):
        if path.startswith(prefix):
            head = path[len(prefix):].split("/", 1)[0]
            return LAYERS.index(head) if head in LAYERS else len(LAYERS) - 1
    return len(LAYERS) - 1


def name_pool(universe: Universe, entity: Entity, name: str) -> list[Entity]:
    """Return each entity with a last name, from the headers and the check file of an entity."""
    pool = universe.by_name.get(name, [])
    if entity.is_local:
        pool = pool + universe.local.get(entity.path, {}).get(name, [])
    return pool


def propagate(analysis: Analysis, entity: Entity, candidate: Entity, leaf: tsast.Node,
              walked: dict[NsTag, Why], walked_params: dict[ParamKey, Why]) -> None:
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
            if param is not None and param.default is not None and (template_arguments is not None
                                                                    or function_arguments is not None):
                flow = analysis.flow(candidate, param.default)
                flow = Flow(dict(flow.namespaces), {}) if not flow.params else unknown("a default argument that "
                                                                                       "depends on a parameter")
                for tag, reason in flow.namespaces.items():
                    entity.walked.setdefault(tag, Why(f"{candidate.label()} at {where}, with its default argument: "
                                                      f"{reason}", candidate, key))
                continue
            entity.walked.setdefault(UNKNOWN, Why(f"{candidate.label()} at {where}, with no argument for the "
                                                  f"parameter that it walks", candidate, key))
            continue
        for argument in given:
            flow = analysis.flow(entity, argument)
            for tag, reason in flow.namespaces.items():
                entity.walked.setdefault(tag, Why(f"{candidate.label()} at {where}: {reason}", candidate, key))
            for own_key, reason in flow.params.items():
                entity.walked_params.setdefault(own_key, Why(f"{candidate.label()} at {where}: {reason}", candidate,
                                                             key))


def evaluate_tokens(analysis: Analysis, entity: Entity) -> None:
    """Add to the summary of a macro whose body does not parse what each name of its body gives."""
    if "members_of" in entity.tokens:
        entity.walked.setdefault(UNKNOWN, Why(f"members_of in the body of the macro {entity.name}, which does not "
                                              f"parse"))
    for name in entity.names:
        for candidate in analysis.universe.by_name.get(name, []):
            if candidate.is_eager or candidate is entity:
                continue
            for tag in candidate.walked:
                entity.walked.setdefault(tag, Why(f"{candidate.label()} in the macro {entity.name}", candidate, tag))
            if candidate.walked_params:
                key = next(iter(candidate.walked_params))
                entity.walked.setdefault(UNKNOWN, Why(f"{candidate.label()} in the macro {entity.name}, whose body "
                                                      f"does not parse", candidate, key))


# ── Selection ────────────────────────────────────────────────────────────────

def is_extensible(universe: Universe, tag: NsTag) -> bool:
    """Say whether a header under include/ can add a member to a walked namespace."""
    if tag == UNKNOWN or tag == ("",):
        return True
    assert isinstance(tag, tuple)
    return any(not opener.startswith(CHECKS + "/") for opener in universe.namespaces.get(tag, ()))


def shown_tag(tag: NsTag) -> str:
    """Return a tag as a row of the list shows it."""
    if tag == UNKNOWN:
        return UNKNOWN
    assert isinstance(tag, tuple)
    return "::" + "::".join(part for part in tag if part)


def unnamed_walks(universe: Universe) -> list[tuple[Entity, str]]:
    """Return each free operator whose summary is not empty, with a reason.

    A use calls a free operator with no name, and no class owns it, so
    the analysis cannot see which check file reaches it.  Each one makes
    each check file join, and the check refuses it.  An operator, a
    conversion or a destructor that a class owns joins the construction
    summary of the class instead.
    """
    found: list[tuple[Entity, str]] = []
    for entity in universe.entities:
        if entity.kind == "function" and entity.owner is None and is_unnamed_call(entity.name) \
                and (entity.walked or entity.walked_params):
            found.append((entity, f"{entity.label()} can walk a namespace, and a use calls it with no name, so the "
                                  f"analysis cannot select the check files that reach it.  Move the walk into a "
                                  f"named function"))
    return found


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


def derive(root: Path) -> tuple[Universe, dict[str, list[NsTag]]]:
    """Collect, summarize and select."""
    universe = collect(root)
    summarize(universe)
    return universe, select(universe)


@dataclass(frozen=True)
class ListRow:
    """One row of the list: its line, the weight cell and the namespace cell."""

    line: int
    weight: str
    namespaces: str


def namespaces_of(tags: list[NsTag]) -> str:
    """Return the namespace cell of a row for the tags of one check file."""
    return ", ".join(shown_tag(tag) for tag in tags)


def list_text(cells: list[tuple[str, str, str]]) -> str:
    """Return the text of the list for (check file, weight, namespace cell) triples, in the order given."""
    return LIST_HEADER + "".join(f"{rel}{SEPARATOR}{weight}{SEPARATOR}{namespaces}\n"
                                 for rel, weight, namespaces in cells)


def is_weight(cell: str) -> bool:
    """Say whether a cell is a weight: a number with one decimal, or unmeasured."""
    return cell == UNMEASURED or WEIGHT_FORM.fullmatch(cell) is not None


def read_list(path: Path) -> tuple[dict[str, ListRow], list[tuple[int, str]]]:
    """Read the list: each row by its check file, and each malformed row with its line."""
    rows: dict[str, ListRow] = {}
    malformed: list[tuple[int, str]] = []
    if not path.is_file():
        return rows, [(0, f"{LIST} does not exist.  Run: python3 {SCRIPT} --write")]
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        cells = entry.split(SEPARATOR)
        if len(cells) != 3 or not cells[0].endswith(".cpp") or not is_weight(cells[1]) or not cells[2].strip():
            malformed.append((number, f"the row is malformed: {entry}.  A row is `<layer>/<path>.cpp{SEPARATOR}weight"
                                      f"{SEPARATOR}namespace, ...`, and a weight is a number with one decimal or "
                                      f"{UNMEASURED}.  Run: python3 {SCRIPT} --write"))
            continue
        if cells[0] in rows:
            malformed.append((number, f"{cells[0]} has a row before this one.  Run: python3 {SCRIPT} --write"))
            continue
        rows[cells[0]] = ListRow(number, cells[1], cells[2])
    return rows, malformed


def check(root: Path, list_path: Path, warnings_dir: Path | None = None) -> int:
    """Compare the tree with the list and print each finding.

    Returns:
        0 when the list agrees with the tree, 1 otherwise
    """
    universe, selected = derive(root)
    rows, malformed = read_list(list_path)
    findings: list[check_report.Finding] = []

    def report(path: str, line: int, message: str, level: str = "error") -> None:
        """Add one finding."""
        findings.append(check_report.Finding(level, path, line, CHECK, message))

    wanted = {rel: namespaces_of(tags) for rel, tags in selected.items()}
    for rel, namespaces in sorted(wanted.items()):
        if rel not in rows:
            report(f"{CHECKS}/{rel}", 0, f"this check file can walk {namespaces}, and {LIST} has no row for it, so no "
                                         f"walk unit compiles it after the headers that can add to that namespace.  "
                                         f"Run: python3 {SCRIPT} --write")
        elif rows[rel].namespaces != namespaces:
            report(LIST, rows[rel].line, f"the row of {rel} names `{rows[rel].namespaces}`, and the tree gives "
                                         f"`{namespaces}`.  Run: python3 {SCRIPT} --write")
    for rel, row in sorted(rows.items()):
        if rel not in wanted:
            report(LIST, row.line, f"{rel} walks no namespace that a header can add to, or it is not a check file, so "
                                   f"a walk unit compiles it for nothing.  Run: python3 {SCRIPT} --write")
        elif row.weight == UNMEASURED:
            report(LIST, row.line, f"the row of {rel} has no measured weight, so test/layer/CMakeLists.txt gives it "
                                   f"the mean weight of the measured rows.  Run: python3 {SCRIPT} --measure "
                                   f"BUILD_DIR", "warning")
    for line, message in malformed:
        report(LIST, line, message)
    for path, message in universe.failures:
        report(path, 0, message)
    for entity, reason in unnamed_walks(universe):
        report(entity.path, entity.body.line if entity.body is not None else 0, reason)
    print(f"check-walk-units: {len(selected)} of {len(universe.roots)} check files join the walk units.",
          file=sys.stderr)
    return check_report.emit(findings, CHECK, warnings_dir)


def write(root: Path, list_path: Path) -> int:
    """Write the list again from the tree, with the weight of each row that stays.

    Returns:
        0 when the list is written, 1 when a parse failure stops the write
    """
    universe, selected = derive(root)
    if universe.failures:
        for path, message in universe.failures:
            print(f"{path}: {message}", file=sys.stderr)
        print("check-walk-units: --write does not write the list while a file does not parse.", file=sys.stderr)
        return 1
    rows, _malformed = read_list(list_path)
    cells = [(rel, rows[rel].weight if rel in rows else UNMEASURED, namespaces_of(tags))
             for rel, tags in sorted(selected.items())]
    list_path.parent.mkdir(parents=True, exist_ok=True)
    list_path.write_text(list_text(cells), encoding="utf-8")
    print(f"check-walk-units: {LIST} written with {len(selected)} row(s).", file=sys.stderr)
    return 0


class MeasureError(Exception):
    """A measure that cannot run, with the reason and the repair."""


def walk_unit_command(build_dir: Path) -> tuple[list[str], str, str]:
    """Find the compile command of a walk unit in the compile database of a build.

    Returns:
        The arguments of the command, its directory, and the include directory of its walk_checks.h

    Raises:
        MeasureError: If the build has no compile database, or the database holds no walk unit
    """
    database = build_dir / "compile_commands.json"
    if not database.is_file():
        raise MeasureError(f"{database} does not exist.  Configure the build first, for example: cmake --preset "
                           f"default")
    for row in json.loads(database.read_text(encoding="utf-8")):
        argv = row["arguments"] if "arguments" in row else shlex.split(row["command"])
        if UNIT_OUTPUT not in row.get("output", "") and not any(UNIT_OUTPUT in arg for arg in argv):
            continue
        for arg in argv:
            found = UNIT_INCLUDE.fullmatch(arg)
            if found is not None:
                return argv, row["directory"], found.group(1)
        raise MeasureError(f"the walk unit command of {database} has no -I of a walk_units directory, so the measure "
                           f"cannot give it another walk_checks.h.  Read test/layer/CMakeLists.txt.")
    raise MeasureError(f"{database} holds no walk unit of test/layer/walks_across_headers.cpp.  Configure the build "
                       f"again.")


def measure_unit(command: list[str], directory: str, unit_dir: str, work: Path, checks: list[str]) -> int | None:
    """Compile one walk unit that holds the given check files, and count its user instructions.

    The function runs in a worker process, because cost_meter.measure sets a
    signal handler, which only the main thread of a process can set.

    Returns:
        The user instructions of the compile, or None when it failed or the host gives no exact count
    """
    work.mkdir(parents=True, exist_ok=True)
    (work / "walk_checks.h").write_text("#pragma once\n" + "".join(f'#include "checks/{check}"\n' for check in checks),
                                        encoding="utf-8")
    argv = [f"-I{work}" if arg == f"-I{unit_dir}" else arg for arg in command]
    argv[argv.index("-o") + 1] = str(work / "unit.o")
    os.chdir(directory)
    run = cost_meter.measure(argv, count_instructions=True)
    return run.instructions if run.exit_code == 0 else None


def measure(build_dir: Path, list_path: Path, jobs: int) -> int:
    """Measure the weight of each row of the list, and write the list again with the weights.

    Complexity: one compile for each row and one more for the base, with jobs compiles at one time.

    Returns:
        0 when the list is written, 1 when the measure cannot run or a compile fails
    """
    rows, malformed = read_list(list_path)
    for line, message in malformed:
        print(f"{LIST}:{line}: {message}", file=sys.stderr)
    if malformed or not rows:
        print(f"check-walk-units: --measure measures the rows of a list with no malformed row.  Run: python3 {SCRIPT} "
              f"--write", file=sys.stderr)
        return 1
    try:
        command, directory, unit_dir = walk_unit_command(build_dir)
    except MeasureError as exc:
        print(f"check-walk-units: {exc}", file=sys.stderr)
        return 1
    units: dict[str, list[str]] = {"": [], **{rel: [rel] for rel in rows}}
    counts: dict[str, int | None] = {}
    with tempfile.TemporaryDirectory(prefix="walk-weights-") as work:
        with concurrent.futures.ProcessPoolExecutor(max_workers=jobs,
                                                    mp_context=multiprocessing.get_context("fork")) as pool:
            futures = {pool.submit(measure_unit, command, directory, unit_dir, Path(work) / f"unit{index}", checks): rel
                       for index, (rel, checks) in enumerate(units.items())}
            for future in concurrent.futures.as_completed(futures):
                counts[futures[future]] = future.result()
    failed = sorted(rel or "the base unit" for rel, count in counts.items() if count is None)
    if failed:
        print(f"check-walk-units: no exact count for {', '.join(failed)}.  A compile failed, or the host gives no "
              f"instruction counter (utils/scripts/cost_meter.py).  The list stays as it was.", file=sys.stderr)
        return 1
    base = counts[""] or 0
    weights = {rel: max(0, (counts[rel] or 0) - base) for rel in rows}
    cells = [(rel, f"{weights[rel] / 1e9:.1f}", rows[rel].namespaces) for rel in sorted(rows)]
    list_path.write_text(list_text(cells), encoding="utf-8")
    heaviest = sorted(weights.items(), key=lambda item: (-item[1], item[0]))[:5]
    print(f"check-walk-units: {LIST} written with {len(rows)} weight(s).  The base unit costs {base / 1e9:.1f} G "
          f"instructions, and the check files add {sum(weights.values()) / 1e9:.1f} G.  The heaviest: "
          + ", ".join(f"{rel} {count / 1e9:.1f} G" for rel, count in heaviest), file=sys.stderr)
    return 0


def explain(root: Path, target: str) -> int:
    """Print the route of each walk of one check file, from the check to the call of members_of.

    Returns:
        0, or 2 when the check file does not exist
    """
    universe, selected = derive(root)
    rel = Path(target).as_posix()
    rel = rel[len(CHECKS) + 1:] if rel.startswith(CHECKS + "/") else rel
    root_entity = universe.roots.get(rel)
    if root_entity is None:
        print(f"check-walk-units: {rel} is not a check file under {CHECKS}/.", file=sys.stderr)
        return 2
    print(f"{rel}: {'joins' if rel in selected else 'does not join'} the walk units")
    for tag, why in sorted(root_entity.walked.items(), key=lambda item: shown_tag(item[0])):
        mark = "" if is_extensible(universe, tag) else "  (only check files open it)"
        print(f"  {shown_tag(tag)}{mark}")
        step: Why | None = why
        seen: set[tuple[int, object]] = set()
        while step is not None:
            print(f"    {step.text}")
            if step.entity is None or (id(step.entity), step.key) in seen:
                break
            seen.add((id(step.entity), step.key))
            following = None
            for summary in (step.entity.walked, step.entity.walked_params, step.entity.constructed):
                if step.key in summary:
                    following = summary[step.key]  # type: ignore[index]
                    break
            step = following
    for _entity, reason in unnamed_walks(universe):
        print(f"  unknown: {reason}")
    return 0


# ── Self-test ────────────────────────────────────────────────────────────────

SELF_TEST_FILES = {
    "include/foundation/Walk.h": (
        "#pragma once\n"
        "#include <meta>\n"
        "namespace foundation::tags { struct First {}; }\n"
        "namespace foundation::rows { struct Row {}; }\n"
        "namespace foundation {\n"
        "template <std::meta::info Ns>\n"
        "consteval std::size_t count_of() { return std::meta::members_of(Ns, std::meta::access_context::unchecked())"
        ".size(); }\n"
        "consteval std::size_t count_in(std::meta::info ns) { return std::meta::members_of(ns, "
        "std::meta::access_context::unchecked()).size(); }\n"
        "template <class = void>\n"
        "consteval bool every_tag_is_empty() { return count_in(^^::foundation::tags) > 0; }\n"
        "inline constexpr std::meta::info row_registry = ^^::foundation::rows;\n"
        "consteval std::size_t row_count() { return count_in(row_registry); }\n"
        "template <std::meta::info Ns>\n"
        "concept Populated = count_of<Ns>() > 0;\n"
        "template <std::meta::info Ns = ^^::foundation::rows>\n"
        "consteval bool defaulted() { return count_of<Ns>() > 0; }\n"
        "struct Shape { int field = 0; };\n"
        "consteval std::size_t shape_members() { return std::meta::members_of(^^Shape, "
        "std::meta::access_context::unchecked()).size(); }\n"
        "inline constexpr std::size_t eager_count = count_in(^^::foundation::tags);\n"
        "consteval std::meta::info pick() { return ^^::foundation::tags; }\n"
        "template <std::meta::info Ns>\n"
        "struct Walker { constexpr Walker() { (void)count_of<Ns>(); } };\n"
        "#define FOUNDATION_WALK_TAGS() static_assert(::foundation::count_in(^^::foundation::tags) > 0)\n"
        "template <std::meta::info Ns>\n"
        "inline constexpr std::size_t count_v = count_of<Ns>();\n"
        "consteval std::size_t substituted() { return std::meta::extract<std::size_t>(std::meta::substitute(\n"
        "    ^^count_v, {std::meta::reflect_constant(^^::foundation::rows)})); }\n"
        "consteval std::size_t pushed() {\n"
        "    std::vector<std::meta::info> found{};\n"
        "    found.push_back(^^::foundation::tags);\n"
        "    return count_in(found[0]);\n"
        "}\n"
        "struct Box { consteval std::size_t tags() const { return count_in(^^::foundation::tags); } };\n"
        "struct Ordered { consteval bool operator<(Ordered) const { return count_in(^^::foundation::tags) > 0; } };\n"
        "struct Walking { consteval std::size_t size() const { return count_in(^^::foundation::tags); } };\n"
        "template <class T>\n"
        "struct Friendly {\n"
        "    template <class U>\n"
        "        requires Populated<^^::foundation::rows>\n"
        "    friend void befriend(U);\n"
        "};\n"
        "}  // namespace foundation\n"
    ),
    "include/fixy/Uses.h": "#pragma once\n#include <foundation/Walk.h>\nnamespace fixy { struct Uses {}; }\n",
    "include/fixy/Shadow.h": (
        "#pragma once\n#include <foundation/Walk.h>\n"
        "namespace fixy { consteval std::size_t shadow_walk() { return ::foundation::count_in(^^::foundation::tags); "
        "} }\n"
    ),
    "include/foundation/Layer.h": "#pragma once\n#include <foundation/Walk.h>\nnamespace foundation {}\n",
}

# A free operator that walks a namespace, which a use calls with no name.
FREE_OPERATOR_FILES = {
    "include/fixy/FreeOp.h": (
        "#pragma once\n#include <foundation/Walk.h>\n"
        "namespace fixy {\nstruct Pair {};\n"
        "consteval bool operator==(Pair, Pair) { return ::foundation::count_in(^^::foundation::tags) > 0; }\n}\n"
    ),
    f"{CHECKS}/fixy/FreeOp.cpp": "// The checks of fixy/FreeOp.h.\n#include <fixy/FreeOp.h>\n\n"
                                 "static_assert(sizeof(::fixy::Pair) == 1);\n",
}

# Each planted check file, the verdict that the analysis must give, and the
# route it plants.  The include of the header is the first line of code.
SELF_TEST_CHECKS: tuple[tuple[str, str, bool, str], ...] = (
    ("fixy/Uses.cpp", "static_assert(::foundation::count_of<^^::foundation::tags>() == 1);", True,
     "a template argument `^^namespace` to a walk of its parameter"),
    ("crucible/NoReflection.cpp", "static_assert(::foundation::every_tag_is_empty<>());", True,
     "a helper that walks a namespace of its own, with no reflection in the check file"),
    ("crucible/Registry.cpp", "static_assert(::foundation::row_count() == 1);", True,
     "a variable at namespace scope that holds the reflection of a namespace"),
    ("crucible/Concept.cpp", "static_assert(::foundation::Populated<^^::foundation::rows>);", True,
     "a concept that walks its parameter"),
    ("crucible/Defaulted.cpp", "static_assert(::foundation::defaulted<>());", True,
     "a default template argument that names a namespace"),
    ("crucible/Klass.cpp", "constexpr ::foundation::Walker<^^::foundation::tags> walker{};", True,
     "a constructor that a use of its class calls with no name"),
    ("crucible/Macro.cpp", "FOUNDATION_WALK_TAGS();", True, "a macro that walks a namespace"),
    ("crucible/Unknown.cpp", "static_assert(::foundation::count_in(::foundation::pick()) == 1);", True,
     "the result of a function, which the analysis cannot read"),
    ("crucible/Lambda.cpp",
     "static_assert([](std::meta::info ns) consteval { return std::meta::members_of(ns, "
     "std::meta::access_context::unchecked()).size(); }(^^::foundation::tags) == 1);", True,
     "a walk of a parameter of a lambda"),
    ("crucible/TypeWalk.cpp", "static_assert(::foundation::shape_members() == 1);", False,
     "a walk of a class"),
    ("crucible/Eager.cpp", "static_assert(::foundation::eager_count == 1);", False,
     "a variable that the header evaluates where it defines it"),
    ("crucible/Private.cpp",
     "namespace crucible::private_test { struct Mine {}; }\n"
     "static_assert(::foundation::count_of<^^::crucible::private_test>() == 1);", False,
     "a walk of a namespace that only check files open"),
    ("crucible/NoWalk.cpp", "static_assert(sizeof(::fixy::Uses) == 1);", False, "no walk"),
    ("crucible/Substitute.cpp", "static_assert(::foundation::substituted() == 1);", True,
     "a variable template that std::meta::substitute instantiates with the reflection of a namespace"),
    ("crucible/Pushed.cpp", "static_assert(::foundation::pushed() == 1);", True,
     "a reflection that push_back puts into a local container"),
    ("crucible/MemberCall.cpp", "static_assert(::foundation::Box{}.tags() == 1);", True,
     "a member call on an object of a class that the check file names"),
    ("crucible/Operator.cpp", "static_assert(!(::foundation::Ordered{} < ::foundation::Ordered{}));", True,
     "an operator that an object of the class runs with no name"),
    ("crucible/OtherSize.cpp",
     "struct Mine { constexpr int size() const { return 2; } };\nstatic_assert(Mine{}.size() == 2);", False,
     "a member call whose name a member with a walk also has, in a class that the check file does not name"),
    ("crucible/Friend.cpp", "static_assert(sizeof(::foundation::Friendly<int>) == 1);", False,
     "the constraint of a friend declaration, which an instantiation of the class does not evaluate"),
    ("foundation/Layer.cpp", "namespace foundation { static_assert(shadow_walk() == 1); }", False,
     "a name that only a higher layer declares"),
)


def self_test() -> int:
    """Plant each route in a scratch tree and make sure that each verdict is correct.

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

    def plant(root: Path, files: dict[str, str]) -> None:
        """Write each file of a scratch tree."""
        for rel, text in files.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")

    def captured(action) -> tuple[int, str]:
        """Run one action and keep its report."""
        buffer = io.StringIO()
        with contextlib.redirect_stderr(buffer), contextlib.redirect_stdout(buffer):
            code = action()
        return code, buffer.getvalue()

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        files = dict(SELF_TEST_FILES)
        for rel, body, _joins, _label in SELF_TEST_CHECKS:
            header = Path(rel).with_suffix(".h").as_posix()
            files.setdefault(f"{INCLUDE}/{header}", "#pragma once\n#include <fixy/Uses.h>\nnamespace crucible {}\n")
            files[f"{CHECKS}/{rel}"] = f"// The checks of {header}.\n#include <{header}>\n\n{body}\n"
        plant(root, files)
        universe, selected = derive(root)
        expect("the planted tree has no parse failure", not universe.failures, True)
        for rel, _body, joins, label in SELF_TEST_CHECKS:
            if joins:
                expect(f"joins: {label} ({rel})", rel in selected)
            else:
                expect(f"does not join: {label} ({rel})", rel not in selected, True)
        expect("a namespace walk names its namespace in the row",
               selected.get("fixy/Uses.cpp") == [("foundation", "tags")])
        expect("a walk that the analysis cannot read is unknown", UNKNOWN in selected.get("crucible/Unknown.cpp", []))
        expect("a substituted argument and a pushed value name their namespaces, not unknown",
               selected.get("crucible/Substitute.cpp") == [("foundation", "rows")]
               and selected.get("crucible/Pushed.cpp") == [("foundation", "tags")])

        list_path = root / LIST
        code, report = captured(lambda: check(root, list_path))
        expect("a missing list fails", code == 1 and "does not exist" in report, True)
        expect("--write writes the list", captured(lambda: write(root, list_path))[0] == 0 and list_path.is_file())
        code, report = captured(lambda: check(root, list_path))
        expect("a list that agrees with the tree passes, and warns about each unmeasured row",
               code == 0 and f"{LIST}:" in report and "has no measured weight" in report)
        unmeasured = list_path.read_text(encoding="utf-8")
        uses_row = f"fixy/Uses.cpp | {UNMEASURED} | ::foundation::tags\n"
        expect("--write gives a new row the weight unmeasured", uses_row in unmeasured)
        written = unmeasured.replace(uses_row, "fixy/Uses.cpp | 2.5 | ::foundation::tags\n")
        list_path.write_text(written, encoding="utf-8")
        code, report = captured(lambda: check(root, list_path))
        expect("a measured row passes with no warning about it",
               code == 0 and "the row of fixy/Uses.cpp has no measured weight" not in report)
        captured(lambda: write(root, list_path))
        expect("--write keeps the weight of a row that stays",
               "fixy/Uses.cpp | 2.5 | ::foundation::tags\n" in list_path.read_text(encoding="utf-8"))
        for weight in ("2.55", "2", "-1.0", "x"):
            list_path.write_text(written.replace("fixy/Uses.cpp | 2.5 |", f"fixy/Uses.cpp | {weight} |"),
                                 encoding="utf-8")
            code, report = captured(lambda: check(root, list_path))
            expect(f"a weight that is not a number with one decimal fails: {weight}",
                   code == 1 and "the row is malformed" in report, True)
        list_path.write_text(written.replace("fixy/Uses.cpp | 2.5 | ::foundation::tags\n", ""), encoding="utf-8")
        code, report = captured(lambda: check(root, list_path))
        expect("a check file that walks a namespace with no row fails",
               code == 1 and f"{CHECKS}/fixy/Uses.cpp:0: error: [{CHECK}]" in report, True)
        list_path.write_text(written.replace("fixy/Uses.cpp | 2.5 | ::foundation::tags\n",
                                             "fixy/Uses.cpp | 2.5 | ::foundation::rows\n"), encoding="utf-8")
        code, report = captured(lambda: check(root, list_path))
        expect("a row that names other namespaces than the tree gives fails",
               code == 1 and "the row of fixy/Uses.cpp names `::foundation::rows`" in report, True)
        list_path.write_text(written + "crucible/NoWalk.cpp | 0.1 | ::foundation::tags\n", encoding="utf-8")
        code, report = captured(lambda: check(root, list_path))
        expect("a row of a check file that walks no namespace fails",
               code == 1 and "crucible/NoWalk.cpp walks no namespace" in report, True)
        list_path.write_text(written + "a row with no separator\n", encoding="utf-8")
        code, report = captured(lambda: check(root, list_path))
        expect("a malformed row fails", code == 1 and "the row is malformed" in report, True)
        list_path.write_text(written + "crucible/Registry.cpp | ::foundation::rows\n", encoding="utf-8")
        code, report = captured(lambda: check(root, list_path))
        expect("a row with no weight cell fails", code == 1 and "the row is malformed" in report, True)
        code, report = captured(lambda: measure(root / "no-build", list_path, 1))
        expect("--measure refuses a list with a malformed row", code == 1 and "no malformed row" in report, True)
        list_path.write_text(written, encoding="utf-8")
        code, report = captured(lambda: measure(root / "no-build", list_path, 1))
        expect("--measure refuses a build with no compile database",
               code == 1 and "compile_commands.json does not exist" in report
               and list_path.read_text(encoding="utf-8") == written, True)
        (root / f"{INCLUDE}/fixy/Planted.h").write_text(
            "#pragma once\nnamespace foundation::tags { struct Planted {}; }\n", encoding="utf-8")
        list_path.write_text(written, encoding="utf-8")
        code, report = captured(lambda: check(root, list_path))
        expect("a new header that adds to a walked namespace changes no row", code == 0)
        code, report = captured(lambda: explain(root, f"{CHECKS}/crucible/NoReflection.cpp"))
        expect("--explain prints the route to the call of members_of",
               code == 0 and "::foundation::tags" in report and "members_of at include/foundation/Walk.h" in report)
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        plant(root, {"include/foundation/Walk.h": SELF_TEST_FILES["include/foundation/Walk.h"], **FREE_OPERATOR_FILES})
        universe, selected = derive(root)
        expect("a free operator that walks a namespace makes each check file join, as unknown",
               selected.get("fixy/FreeOp.cpp") == [UNKNOWN])
        captured(lambda: write(root, root / LIST))
        code, report = captured(lambda: check(root, root / LIST))
        expect("a free operator that walks a namespace fails the check, also with a list that agrees",
               code == 1 and "fixy::operator== can walk a namespace" in report, True)
    if failures:
        print(f"check-walk-units --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-walk-units --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    parser = argparse.ArgumentParser(prog="check-walk-units.py", add_help=True)
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--write", action="store_true", help="write the list again from the tree")
    modes.add_argument("--measure", metavar="BUILD_DIR", type=Path,
                       help="measure the weight of each row with the walk unit command of the build")
    modes.add_argument("--explain", metavar="CHECK_FILE", help="print the route of each walk of one check file")
    modes.add_argument("--self-test", action="store_true", help="plant each route and examine each verdict")
    parser.add_argument("--jobs", type=int, default=MEASURE_JOBS, help="with --measure: the compiles at one time")
    check_report.add_arguments(parser)
    try:
        options = parser.parse_args(argv)
    except SystemExit as stop:
        return 0 if stop.code == 0 else 2
    if options.jobs < 1:
        print("check-walk-units: --jobs takes a number of 1 or more.", file=sys.stderr)
        return 2
    root = tsast.REPO_ROOT
    list_path = root / LIST
    try:
        if options.self_test:
            return self_test()
        if options.write:
            return write(root, list_path)
        if options.measure is not None:
            return measure(options.measure.resolve(), list_path, options.jobs)
        if options.explain is not None:
            return explain(root, options.explain)
        return check(root, list_path, options.warnings_dir)
    except tsast.KitMissing as exc:
        print(f"check-walk-units: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
