"""padded-list — the element type of a large fixed list has no padding bit.

With -ftrivial-auto-var-init=zero, GCC writes zero to each padding hole of an
automatic object that has an initializer, and of each object that a return
statement makes.  It writes one store for each hole of each element of a
fixed list, with no loop.  A list of 256 padded ledger entries, returned
through std::expected, gave a function of 934 KB of machine code and 75 s of
variable tracking in each file that included it.  Make each padding byte a
member, as include/crucible/ledger/Verdict.h does with `pad`.

WHAT THE CHECK FINDS
    A declaration whose type is a fixed list of more elements than the
    measured rule padded-list of utils/registry.txt permits, and whose element
    type has a padding bit.  A fixed list is an array, or an array that a class
    template specialization or an unnamed class holds at any depth: a C
    array, std::array, std::inplace_vector, and each fixed container of the
    tree that stores an array.  The count of a list of lists is the product
    of the counts.  The declarations are type aliases, variables, data
    members, and the return types and the parameters that a function takes
    by value, at namespace scope and in classes, in the headers under
    include/ and in the sources under src/.

HOW
    1. The parse tree (utils/assay/readers/tsast.py) selects each file that
       can hold such a declaration outside a function body: it names
       std::array, std::inplace_vector, a class template of include/ that
       holds a fixed list, or a type alias of such a list, or it declares a C
       array with a size.  A list of scalars or pointers gives no finding,
       and a list whose size is a literal below the limit cannot give one, so
       neither selects a file.
    2. For each selected file, the check writes a probe translation unit that
       includes the file and walks each namespace that the file declares
       with reflection.  The compiler decides each count and each padding
       bit: the value bits of a type are the bit ranges that its scalar
       members, its bit-fields and its bases hold, and the rest are padding.
       A floating-point member has no padding bit, and long double holds 80
       value bits.  The probe prints its findings as a compile-time message
       (__builtin_constexpr_diag), so the compile succeeds and ccache can
       keep its result.
    3. The probe compiles with the command of a crucible sentinel of the
       compile database (a header), or with the command of the source (a
       source of src/), through ccache when it is installed.

WHAT THE CHECK CANNOT SEE
    A local variable, a declaration inside a template, and a declaration in
    a header of test/layer/crucible-not-standalone.txt, which does not
    compile alone.  A list that only a declaration of another file names
    through an alias is seen at the alias.

THE LEDGER
    utils/ledgers/padded-list.txt (assay.core.ledger) holds two kinds of
    row.
      path | role | 1 | declaration: element type | -
          A padded list that the tree holds today.  Each run gives a
          warning for it.  A padded list with no row is an error.  A row
          that names no padded list is an error: run --write in the same
          commit.
      path | role | 1 | declaration: element type | reason
          A keep row: an element type with padding on purpose, for example
          a slot that fills a cache line.  It gives no finding.
    The role is alias, member, return, or `parameter N`.  The declaration
    and the element type are the spellings of the compiler, with `%`, `|`
    and control characters percent-encoded.  In the ledger, the first ": "
    divides the declaration from the element type, and a ": " in the
    declaration is written "%3A ".  A row that the rows grammar refuses, a
    row with a count other than 1, a path outside include/ and src/ or an
    empty part, and a configuration row are errors.

Usage
    guard check --rule padded-list --build-dir BUILD_DIR [--warnings-dir DIR]
    guard check --rule padded-list --build-dir BUILD_DIR --write
    guard check --rule padded-list --build-dir BUILD_DIR --self-test

Exit 0 with no error, 1 on an error, 2 on a usage error or a failed
self-test, 3 when the tree-sitter kit is not installed.
"""

from __future__ import annotations

import argparse
import contextlib
import hashlib
import io
import json
import os
import shutil
import sys
import tempfile
from collections.abc import Iterator
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field, replace
from pathlib import Path
from urllib.parse import unquote

from assay.core import cache as cache_dir
from assay.core import ledger as rule_ledger
from assay.core import proc
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.readers import compdb, tsast

CHECK = "padded-list"
SCRIPT = "utils/assay/guard/legacy/check_padded_lists.py"
LEDGER = rule_ledger.shown_path(CHECK)
# The text between the declaration and the element type in the entity of a ledger row, and its form in a
# declaration.  The probe writes `%` as %25, so %3A comes from no spelling of the compiler.
ENTITY_SEPARATOR = ": "
ESCAPED_SEPARATOR = "%3A "
NOT_STANDALONE = "test/layer/crucible-not-standalone.txt"
SENTINEL_TARGET = "layer_sentinel_crucible"
PROBE_DIRECTORY = "padded-list-probes"
PROBE_HEADER = "crucible_padded_list_probe.h"
SEPARATOR = " | "
HEADER_SUFFIXES = (".h", ".hpp")
SOURCE_SUFFIXES = (".cpp", ".cc")
# The probes that compile at the same time.  The tree has about 30 targets,
# and a probe that ccache does not hold compiles for about 1.4 s, so all of
# them start at once after an edit of a base header.
PROBE_JOBS = 32
# The fixed lists of the standard library.  The scan adds each class template of
# include/ that holds a fixed list.
STD_LIST_TEMPLATES = frozenset({"array", "inplace_vector"})
# The leaf names of the scalar types that a list of the tree spells as its element.
SCALAR_NAMES = frozenset({
    "bool", "char", "char8_t", "char16_t", "char32_t", "wchar_t", "short", "int", "long", "float", "double",
    "int8_t", "int16_t", "int32_t", "int64_t", "uint8_t", "uint16_t", "uint32_t", "uint64_t", "size_t",
    "ptrdiff_t", "intptr_t", "uintptr_t", "byte", "ssize_t", "off_t", "pid_t", "uid_t", "gid_t",
})
BODIES = ("compound_statement", "lambda_expression")
DECLARATIONS = ("field_declaration", "declaration", "parameter_declaration", "type_definition",
                "optional_parameter_declaration")
# The head comment of the ledger.  --write writes it into a new ledger, and keeps the comment of a ledger that
# exists.
COMMENT = (
    "# The ledger of the rule padded-list: the fixed lists whose element type has a padding bit.\n"
    "# `python3 utils/bin/guard explain padded-list` tells the rule.\n"
    "#\n"
    "# With -ftrivial-auto-var-init=zero, GCC writes one store for each padding hole of each element of\n"
    "# a fixed list in an automatic object, with no loop.  Make each padding byte a member.  The measured\n"
    "# rule padded-list of utils/registry.txt gives the element count above which a list counts.\n"
    "#\n"
    "# A count row:  PATH | ROLE | 1 | DECLARATION: ELEMENT TYPE | -\n"
    "#   A padded list that the tree holds today.  Each run gives a warning for it.  When you remove the\n"
    "#   padding, run python3 utils/bin/guard check --rule padded-list --build-dir BUILD_DIR --write in\n"
    "#   the same commit.\n"
    "#\n"
    "# A keep row:   PATH | ROLE | 1 | DECLARATION: ELEMENT TYPE | REASON\n"
    "#   An element type with padding on purpose.  The reason makes the row a keep row, and the row gives\n"
    "#   no warning.\n"
    "#\n"
    "# The role is alias, member, return or `parameter N`.  The declaration and the element type are the\n"
    "# spellings of the compiler, with `%`, `|` and control characters percent-encoded.  The first \": \"\n"
    "# divides the declaration from the element type, and a \": \" in the declaration is written \"%3A \".\n"
)

# The walk that each probe runs.  The check writes it into the probe directory,
# and each probe includes it after the file that the probe reads.
PROBE_TEXT = r"""// The padding walk of the rule padded-list (guard check --rule padded-list), which writes this file.
#pragma once

#include <algorithm>
#include <cstddef>
#include <meta>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crucible_padded_list_probe {

using std::meta::info;

consteval std::string number(std::size_t value) {
    std::string out;
    do {
        out.insert(out.begin(), static_cast<char>('0' + value % 10));
        value /= 10;
    } while (value != 0);
    return out;
}

// Each field of a record is percent-encoded, so a type name cannot hold the separator.
consteval std::string field(std::string_view text) {
    constexpr std::string_view digits = "0123456789ABCDEF";
    std::string out;
    for (char const symbol : text) {
        auto const code = static_cast<unsigned char>(symbol);
        if (symbol == '%' || symbol == '|' || code < 0x20) {
            out += '%';
            out += digits[code / 16];
            out += digits[code % 16];
        } else {
            out += symbol;
        }
    }
    return out;
}

using Interval = std::pair<std::size_t, std::size_t>;

consteval std::size_t value_bits(info type);

// Each bit range of an object of the type that holds a value, at base_bit.
consteval void collect(info type, std::size_t base_bit, std::vector<Interval>& intervals) {
    type = std::meta::remove_cv(std::meta::dealias(type));
    if (std::meta::is_reference_type(type)) {
        intervals.push_back({base_bit, base_bit + sizeof(void*) * 8});
        return;
    }
    if (std::meta::is_array_type(type)) {
        info const element = std::meta::remove_extent(type);
        std::size_t const count = std::meta::extent(type);
        std::size_t const element_bits = std::meta::size_of(element) * 8;
        if (value_bits(element) == element_bits) {
            intervals.push_back({base_bit, base_bit + count * element_bits});
            return;
        }
        for (std::size_t index = 0; index < count; ++index) {
            collect(element, base_bit + index * element_bits, intervals);
        }
        return;
    }
    if (std::meta::is_class_type(type) || std::meta::is_union_type(type)) {
        auto const context = std::meta::access_context::unchecked();
        for (info const base : std::meta::bases_of(type, context)) {
            auto const offset = static_cast<std::size_t>(std::meta::offset_of(base).bytes);
            collect(std::meta::type_of(base), base_bit + offset * 8, intervals);
        }
        for (info const member : std::meta::nonstatic_data_members_of(type, context)) {
            auto const offset = std::meta::offset_of(member);
            std::size_t const first =
                base_bit + static_cast<std::size_t>(offset.bytes) * 8 + static_cast<std::size_t>(offset.bits);
            if (std::meta::is_bit_field(member)) {
                intervals.push_back({first, first + std::meta::bit_size_of(member)});
                continue;
            }
            collect(std::meta::type_of(member), first, intervals);
        }
        return;
    }
    std::size_t const bits = type == ^^long double ? 80 : std::meta::size_of(type) * 8;
    intervals.push_back({base_bit, base_bit + bits});
}

// The number of bits of an object of the type that hold a value.  The rest are padding.
consteval std::size_t value_bits(info type) {
    std::vector<Interval> intervals;
    collect(type, 0, intervals);
    std::sort(intervals.begin(), intervals.end());
    std::size_t covered = 0;
    std::size_t reach = 0;
    for (auto const& [start, end] : intervals) {
        std::size_t const from = start > reach ? start : reach;
        if (end > from) {
            covered += end - from;
            reach = end;
        }
    }
    return covered;
}

struct Walk {
    std::string_view target;
    std::size_t limit = 0;
    std::string out;
    std::size_t entities = 0;
};

consteval bool is_in_target(info entity, Walk const& walk) {
    return std::string_view(std::meta::source_location_of(entity).file_name()) == walk.target;
}

consteval std::string identifier(info entity) {
    return std::meta::has_identifier(entity) ? std::string(std::meta::identifier_of(entity)) : "(unnamed)";
}

// The name of an entity with each enclosing scope.  An entity in an extern "C"
// block can have no parent that reflection represents, so the walk stops there.
consteval std::string qualified_name(info entity) {
    std::string name = identifier(entity);
    info owner = entity;
    while (std::meta::has_parent(owner)) {
        owner = std::meta::parent_of(owner);
        if (owner == ^^::) {
            break;
        }
        name = identifier(owner) + "::" + name;
    }
    return name;
}

consteval void report(info site, std::string_view role, std::string_view key, info element, std::size_t count,
                      Walk& walk) {
    auto const where = std::meta::source_location_of(site);
    std::string const head = field(where.file_name()) + "|" + number(where.line()) + "|" + std::string(role) + "|"
                             + field(key) + "|" + field(std::meta::display_string_of(element));
    if (std::meta::is_polymorphic_type(element)) {
        walk.out += "PADPROBE-UNDECIDED|" + head + "\n";
        return;
    }
    std::size_t const holes = std::meta::size_of(element) * 8 - value_bits(element);
    if (holes != 0) {
        walk.out += "PADPROBE|" + head + "|" + number(count) + "|" + number(holes) + "\n";
    }
}

// Find each fixed list in a declared type: an array, or an array that a class
// template specialization or an unnamed class holds, at any depth.
consteval void scan(info site, std::string_view role, std::string_view key, info type, std::size_t multiplicity,
                    int depth, Walk& walk) {
    if (depth > 16) {
        return;
    }
    type = std::meta::remove_cv(std::meta::dealias(type));
    if (std::meta::is_array_type(type)) {
        std::size_t total = multiplicity;
        info leaf = type;
        while (std::meta::is_array_type(leaf)) {
            total *= std::meta::extent(leaf);
            leaf = std::meta::remove_cv(std::meta::dealias(std::meta::remove_extent(leaf)));
        }
        if (std::meta::is_class_type(leaf) || std::meta::is_union_type(leaf)) {
            if (total >= walk.limit) {
                report(site, role, key, leaf, total, walk);
            }
            scan(site, role, key, leaf, total, depth + 1, walk);
        }
        return;
    }
    if (!(std::meta::is_class_type(type) || std::meta::is_union_type(type)) || !std::meta::is_complete_type(type)) {
        return;
    }
    if (std::meta::has_identifier(type) && !std::meta::has_template_arguments(type)) {
        return;
    }
    auto const context = std::meta::access_context::unchecked();
    for (info const base : std::meta::bases_of(type, context)) {
        scan(site, role, key, std::meta::type_of(base), multiplicity, depth + 1, walk);
    }
    for (info const member : std::meta::nonstatic_data_members_of(type, context)) {
        scan(site, role, key, std::meta::type_of(member), multiplicity, depth + 1, walk);
    }
}

consteval void inspect(info site, std::string_view role, std::string_view key, info type, Walk& walk) {
    info const bare = std::meta::dealias(type);
    if (std::meta::is_reference_type(bare) || std::meta::is_pointer_type(bare)) {
        return;
    }
    scan(site, role, key, type, 1, 0, walk);
}

consteval void walk_function(info function, Walk& walk) {
    std::string const name(std::meta::display_string_of(function));
    if (!std::meta::is_constructor(function) && !std::meta::is_destructor(function)) {
        inspect(function, "return", name, std::meta::return_type_of(function), walk);
    }
    std::size_t index = 0;
    for (info const parameter : std::meta::parameters_of(function)) {
        inspect(function, "parameter " + number(index), name, std::meta::type_of(parameter), walk);
        ++index;
    }
}

consteval void walk_scope(info scope, Walk& walk, int depth, bool is_unnamed);

// The probe names each named namespace that the file declares, so the walk
// enters a named namespace only inside an unnamed one, where no name reaches it.
consteval void walk_member(info member, Walk& walk, int depth, bool is_unnamed) {
    ++walk.entities;
    if (std::meta::is_namespace(member)) {
        if (std::meta::is_namespace_alias(member)) {
            return;
        }
        if (!std::meta::has_identifier(member) || is_unnamed) {
            walk_scope(member, walk, depth + 1, true);
        }
        return;
    }
    if (std::meta::is_template(member) || !is_in_target(member, walk)) {
        return;
    }
    if (std::meta::is_type_alias(member)) {
        inspect(member, "alias", qualified_name(member), member, walk);
    } else if (std::meta::is_nonstatic_data_member(member) || std::meta::is_variable(member)) {
        inspect(member, "member", qualified_name(member), std::meta::type_of(member), walk);
    } else if (std::meta::is_function(member)) {
        walk_function(member, walk);
    } else if (std::meta::is_type(member) && (std::meta::is_class_type(member) || std::meta::is_union_type(member))
               && std::meta::is_complete_type(member)) {
        walk_scope(member, walk, depth + 1, false);
    }
}

consteval void walk_scope(info scope, Walk& walk, int depth, bool is_unnamed) {
    if (depth > 32) {
        return;
    }
    for (info const member : std::meta::members_of(scope, std::meta::access_context::unchecked())) {
        try {
            walk_member(member, walk, depth, is_unnamed);
        } catch (std::meta::exception const& error) {
            walk.out += "PADPROBE-ERROR|" + field(std::meta::display_string_of(member)) + "|" + field(error.what())
                        + "\n";
        }
    }
}

template <info... Scopes>
consteval bool run(std::string_view target, std::size_t limit) {
    Walk walk{target, limit, "\n", 0};
    (walk_scope(Scopes, walk, 0, false), ...);
    walk.out += "PADPROBE-END|" + number(walk.entities) + "\n";
    __builtin_constexpr_diag(0, "padded_list", std::string_view(walk.out));
    return true;
}

}  // namespace crucible_padded_list_probe
"""


@dataclass(frozen=True)
class Target:
    """One file that a probe reads, with the namespaces that it declares."""

    rel: str
    scopes: tuple[tuple[str, ...], ...]
    needs_global: bool


@dataclass(frozen=True)
class Site:
    """One padded fixed list that a probe found."""

    path: str
    line: int
    role: str
    entity: str
    element: str
    count: int
    holes: int

    def key(self) -> tuple[str, str, str, str]:
        """Return the identity of the list that a ledger row names: no line, so a line shift moves nothing."""
        return (self.path, self.role, self.entity, self.element)


@dataclass(frozen=True)
class Keep:
    """One keep row of the ledger."""

    key: tuple[str, str, str, str]
    reason: str
    line: int


@dataclass
class Ledger:
    """The rows of the ledger, and one finding for each malformed row.

    file is the ledger as assay.core.ledger reads it, or None when it
    cannot be read.
    """

    rows: dict[tuple[str, str, str, str], int] = field(default_factory=dict)
    keeps: list[Keep] = field(default_factory=list)
    malformed: list[check_report.Finding] = field(default_factory=list)
    file: rule_ledger.Ledger | None = None


@dataclass
class ProbeResult:
    """What the probes of one tree found."""

    sites: dict[tuple[str, str, str, str], Site] = field(default_factory=dict)
    errors: list[check_report.Finding] = field(default_factory=list)
    probed: int = 0


# ── The selection of the files, from the parse tree ──────────────────────────


def is_scalar_type(node: tsast.Node | None) -> bool:
    """Say whether a type node spells a scalar type that has no padding bit."""
    if node is None:
        return False
    if node.type in ("primitive_type", "sized_type_specifier"):
        return True
    if node.type in ("type_identifier", "qualified_identifier"):
        return tsast.leaf_name(node) in SCALAR_NAMES
    return False


def is_small_count(node: tsast.Node | None, limit: int) -> bool:
    """Say whether a count is a literal below the limit."""
    if node is None or node.type != "number_literal":
        return False
    value = tsast.number_value(node)
    return value is not None and value < limit


def template_parts(node: tsast.Node) -> tuple[tsast.Node | None, bool, tsast.Node | None]:
    """Return the element type node, whether the element is a pointer, and the count node of a template-id."""
    arguments = node.child_by_field("arguments")
    items = tsast.non_comment_children(arguments) if arguments is not None else []
    element: tsast.Node | None = None
    is_pointer = False
    count: tsast.Node | None = None
    for item in items:
        if item.type == "type_descriptor" and element is None:
            element = item.child_by_field("type")
            is_pointer = item.child_by_field("declarator") is not None
        elif item.type != "type_descriptor" and element is not None and count is None:
            count = item
    return element, is_pointer, count


def array_parts(node: tsast.Node) -> tuple[tsast.Node | None, bool, tsast.Node | None]:
    """Return the element type node, whether the element is a pointer, and the size node of an array declarator.

    An abstract array declarator, as in `using List = T[N];` or a template
    argument `T[N]`, takes its element type from its type descriptor.
    """
    is_pointer = False
    owner = node.parent
    while owner is not None and owner.type not in (*DECLARATIONS, "type_descriptor"):
        if owner.type in ("pointer_declarator", "reference_declarator", "function_declarator",
                          "abstract_pointer_declarator", "abstract_reference_declarator",
                          "abstract_function_declarator"):
            is_pointer = True
        owner = owner.parent
    element = owner.child_by_field("type") if owner is not None else None
    return element, is_pointer, node.child_by_field("size")


def list_sites(tree: tsast.Tree, templates: frozenset[str], aliases: frozenset[str], limit: int, *,
               is_walkable_only: bool = False) -> Iterator[tsast.Node]:
    """Yield each node outside a function body that can declare a padded fixed list.

    The walk of a probe skips each template, so with is_walkable_only a site
    inside a template declaration does not count.

    Complexity: linear in the number of nodes of the file.  Only a type name
    that is one of the names gets its text decoded.

    Args:
        tree: The parse tree of one file
        templates: The names of the class templates that hold a fixed list
        aliases: The names of the type aliases of a fixed list
        limit: The element count from which a list counts
        is_walkable_only: Whether a site inside a template declaration is left out

    Yields:
        A template-id, an array declarator or a type name
    """
    outside = (*BODIES, "template_declaration") if is_walkable_only else BODIES
    for name_node in tree.root.descendants_named(templates | aliases, "type_identifier"):
        parent = name_node.parent
        site: tsast.Node = name_node
        leaf = tsast.leaf_name(name_node)
        if parent is not None and parent.type == "template_type" and name_node.field == "name":
            if leaf not in templates:
                continue
            site = parent
            if leaf in STD_LIST_TEMPLATES:
                element, is_pointer, count = template_parts(parent)
                if is_pointer or is_scalar_type(element) or is_small_count(count, limit):
                    continue
        elif leaf not in aliases:
            continue
        if site.ancestor_of_type(*outside) is None:
            yield site
    for node in tree.find("array_declarator", "abstract_array_declarator"):
        element, is_pointer, count = array_parts(node)
        if count is None or is_pointer or is_scalar_type(element) or is_small_count(count, limit):
            continue
        if node.ancestor_of_type(*outside) is None:
            yield node


def declared_name(node: tsast.Node) -> str | None:
    """Return the name that an alias declaration, a typedef or a class template declares, or None."""
    if node.type == "alias_declaration":
        name = node.child_by_field("name")
        return tsast.leaf_name(name) if name is not None else None
    if node.type == "type_definition":
        declarator = node.child_by_field("declarator")
        return tsast.leaf_name(declarator) if declarator is not None else None
    if node.type in ("class_specifier", "struct_specifier", "union_specifier"):
        name = node.child_by_field("name")
        return tsast.leaf_name(name) if name is not None else None
    return None


def list_names(trees: list[tsast.Tree], limit: int) -> tuple[frozenset[str], frozenset[str]]:
    """Return the class templates of include/ that hold a fixed list, and the type aliases of a fixed list.

    The two sets grow until no pass adds a name, so a template that holds
    another such template, and an alias of an alias, count too.

    Complexity: one pass over each tree for each round, and the rounds end when no name is new.

    Args:
        trees: The parse trees of the files
        limit: The element count from which a list counts

    Returns:
        The template names and the alias names
    """
    templates = set(STD_LIST_TEMPLATES)
    aliases: set[str] = set()
    while True:
        before = (len(templates), len(aliases))
        frozen_templates, frozen_aliases = frozenset(templates), frozenset(aliases)
        for tree in trees:
            for site in list_sites(tree, frozen_templates, frozen_aliases, limit):
                owner = site.ancestor_of_type("alias_declaration", "type_definition", "template_declaration")
                if owner is None:
                    continue
                if owner.type == "template_declaration":
                    specifier = site.ancestor_of_type("class_specifier", "struct_specifier", "union_specifier")
                    if specifier is not None and specifier.ancestor_of_type("template_declaration") == owner:
                        name = declared_name(specifier)
                        if name:
                            templates.add(name)
                    continue
                name = declared_name(owner)
                if name:
                    aliases.add(name)
        if (len(templates), len(aliases)) == before:
            return frozenset(templates), frozenset(aliases)


def namespace_scopes(tree: tsast.Tree) -> tuple[tuple[tuple[str, ...], ...], bool]:
    """Return each named namespace that a file declares, and whether it declares something at global scope.

    An unnamed namespace is walked from the named namespace that holds it.
    An unnamed namespace at global scope, and a declaration at global
    scope, need a walk of the global namespace.

    Args:
        tree: The parse tree of one file

    Returns:
        The namespace paths, sorted, and the flag
    """
    scopes: set[tuple[str, ...]] = set()
    needs_global = False
    for node in tree.find("namespace_definition"):
        enclosing = tsast.namespace_path(node)
        name = node.child_by_field("name")
        if name is None:
            needs_global = needs_global or not enclosing
            continue
        parts = tsast.qualified_parts(name)
        # A namespace inside an unnamed namespace has no name that a probe can
        # spell.  The walk reaches it from the unnamed namespace.
        if parts is None or "" in enclosing:
            continue
        scopes.add((*enclosing, *parts[1]))
    needs_global = needs_global or any(not declaration.ns for declaration in tsast.namespace_scope_declarations(tree))
    return tuple(sorted(scopes)), needs_global


def read_not_standalone(root: Path) -> frozenset[str]:
    """Return each header of test/layer/crucible-not-standalone.txt, relative to the root."""
    path = root / NOT_STANDALONE
    if not path.is_file():
        return frozenset()
    headers: set[str] = set()
    for raw in path.read_text(encoding="utf-8").splitlines():
        entry = raw.strip()
        if entry and not entry.startswith("#") and SEPARATOR in entry:
            headers.add("include/" + entry.split(SEPARATOR, 1)[0].strip())
    return frozenset(headers)


def select_targets(root: Path, limit: int) -> tuple[list[Target], list[check_report.Finding], list[str]]:
    """Read the headers of include/ and the sources of src/, and select the files that a probe reads.

    Args:
        root: The repository root
        limit: The element count from which a list counts

    Returns:
        The targets, one error for each file that the parser cannot read, and each selected header
        that the check cannot probe because it does not compile alone
    """
    excluded = read_not_standalone(root)
    paths: list[Path] = []
    for directory, suffixes in (("include", HEADER_SUFFIXES), ("src", SOURCE_SUFFIXES)):
        base = root / directory
        if base.is_dir():
            paths.extend(path for path in base.rglob("*") if path.is_file() and path.suffix in suffixes
                         and tsast.is_in_cpp_scope(path.relative_to(root)))
    trees: list[tsast.Tree] = []
    errors: list[check_report.Finding] = []
    for tree in tsast.parse(sorted(paths), strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            if rel not in excluded:
                errors.append(check_report.Finding("error", rel, 0, CHECK, f"the parser cannot read this file, so "
                                                                           f"the check cannot select it.  "
                                                                           f"{tree.diagnostic.strip()}"))
            continue
        trees.append(tree)
    templates, aliases = list_names(trees, limit)
    targets: list[Target] = []
    skipped: list[str] = []
    for tree in trees:
        rel = Path(tree.path).relative_to(root).as_posix()
        if next(list_sites(tree, templates, aliases, limit, is_walkable_only=True), None) is None:
            continue
        if rel in excluded:
            skipped.append(rel)
            continue
        scopes, needs_global = namespace_scopes(tree)
        targets.append(Target(rel, scopes, needs_global))
    return targets, errors, skipped


# ── The probes ───────────────────────────────────────────────────────────────


@dataclass(frozen=True)
class Command:
    """One compile command of the compile database, without its source, output and dependency words."""

    directory: str
    compiler: str
    flags: tuple[str, ...]


def command_of(row: compdb.CompileEntry) -> Command:
    """Return the compile command of one entry, without the words that name its source, output or dependency file.

    Raises:
        ValueError: If the command line does not obey the shell quoting rules
    """
    directory = row.directory
    words = row.argv()
    source = os.path.realpath(row.file)
    flags: list[str] = []
    skip = False
    for word in words[1:]:
        if skip:
            skip = False
            continue
        if word in ("-o", "-MF", "-MT", "-MQ"):
            skip = True
            continue
        if word in ("-c", "-MD", "-MMD", "-MP"):
            continue
        if not word.startswith("-") and os.path.realpath(os.path.join(directory, word)) == source:
            continue
        flags.append(word)
    return Command(directory, words[0], tuple(flags))


def load_commands(build_dir: Path, root: Path) -> tuple[Command | None, dict[str, Command]]:
    """Return the command of one crucible sentinel, and the command of each source of src/.

    Raises:
        compdb.CompdbError: If the compile database cannot be read
        ValueError: If a command line does not obey the shell quoting rules
    """
    sentinel: Command | None = None
    sources: dict[str, Command] = {}
    resolved_root = root.resolve()
    for row in compdb.entries(build_dir):
        if sentinel is None and f"/CMakeFiles/{SENTINEL_TARGET}.dir/" in (row.output or ""):
            sentinel = command_of(row)
        named = row.file
        # A source of src/ has a directory named src in its path, so the
        # check resolves no other path.  The resolve costs most of the read.
        if f"{os.sep}src{os.sep}" not in named:
            continue
        source = Path(os.path.realpath(named))
        if source.is_relative_to(resolved_root / "src"):
            sources.setdefault(source.relative_to(resolved_root).as_posix(), command_of(row))
    return sentinel, sources


def write_if_changed(path: Path, text: str) -> None:
    """Write a file only when its text changes, through a rename, so a reader in another process sees all of it."""
    if path.is_file() and path.read_text(encoding="utf-8") == text:
        return
    staging = path.with_name(f".{path.name}.{os.getpid()}.tmp")
    staging.write_text(text, encoding="utf-8")
    os.replace(staging, path)


def compiler_identity(compiler: str) -> str:
    """Return the SHA-256 of the compiler driver and of the cc1plus that it runs, as the build gives ccache.

    The digests stay in the cache directory, keyed by the path, the size and
    the modification time of each binary, so a run hashes each binary one time.
    """
    done = proc.run(proc.Command([compiler, "-print-prog-name=cc1plus"], timeout_s=proc.QUICK_S))
    binaries = [shutil.which(compiler) or compiler]
    if os.path.isabs(done.out.strip()) and os.path.isfile(done.out.strip()):
        binaries.append(done.out.strip())
    store = cache_dir.cache_root("padded-list")
    digests: list[str] = []
    for binary in binaries:
        status = os.stat(binary)
        memo_name = hashlib.sha256(f"{binary}|{status.st_size}|{status.st_mtime_ns}".encode()).hexdigest()
        memo = store / memo_name if store is not None else None
        if memo is not None and memo.is_file():
            digests.append(memo.read_text(encoding="utf-8").strip())
            continue
        hasher = hashlib.sha256()
        with open(binary, "rb") as handle:
            for chunk in iter(lambda: handle.read(1 << 20), b""):
                hasher.update(chunk)
        digests.append(hasher.hexdigest())
        if memo is not None:
            write_if_changed(memo, digests[-1] + "\n")
    return "-".join(digests)


def probe_text(target_path: Path, target: Target, header: Path, limit: int) -> str:
    """Return the text of the probe translation unit of one target."""
    # Each reflection goes in parentheses, because `^^::>` does not parse as a template argument.
    scopes = ["(^^::" + "::".join(scope) + ")" for scope in target.scopes]
    if target.needs_global:
        scopes.append("(^^::)")
    spelled = json.dumps(str(target_path))
    return (f"// The padded-list probe of {target.rel}.  The rule padded-list (guard check --rule padded-list) "
            f"writes this file.\n"
            f"#include {spelled}\n"
            f"#include {json.dumps(str(header))}\n"
            f"[[maybe_unused]] constexpr bool crucible_padded_list_probe_done =\n"
            f"    ::crucible_padded_list_probe::run<{', '.join(scopes)}>({spelled}, {limit});\n")


def parse_probe_output(output: str, target: Target, root: Path) -> tuple[list[Site], list[check_report.Finding]]:
    """Read the records of one probe from its compile output.

    Args:
        output: The standard error of the compile
        target: The target of the probe
        root: The repository root, to name a file of the tree by its relative path

    Returns:
        The padded lists in files under include/ or src/, and one error for each record that shows a problem
    """
    sites: list[Site] = []
    errors: list[check_report.Finding] = []
    has_end = False
    resolved_root = root.resolve()
    for line in output.splitlines():
        if not line.startswith("PADPROBE"):
            continue
        cells = line.split("|")
        if cells[0] == "PADPROBE-END":
            has_end = True
        elif cells[0] == "PADPROBE-ERROR" and len(cells) >= 3:
            errors.append(check_report.Finding(
                "error", target.rel, 0, CHECK,
                f"the probe cannot read {unquote(cells[1])}: {unquote(cells[2])}.  Correct the walk of {SCRIPT}."))
        elif cells[0] in ("PADPROBE", "PADPROBE-UNDECIDED") and len(cells) >= 6:
            path = Path(unquote(cells[1]))
            if not (path.is_absolute() and path.resolve().is_relative_to(resolved_root)):
                continue
            rel = path.resolve().relative_to(resolved_root).as_posix()
            if cells[0] == "PADPROBE-UNDECIDED":
                errors.append(check_report.Finding(
                    "error", rel, int(cells[2]), CHECK,
                    f"the element type {unquote(cells[5])} of the fixed list of {unquote(cells[4])} is polymorphic, "
                    f"and the check cannot tell the padding of its vtable pointer."))
                continue
            if len(cells) >= 8:
                sites.append(Site(rel, int(cells[2]), cells[3], cells[4], cells[5], int(cells[6]), int(cells[7])))
    if not has_end:
        errors.append(check_report.Finding(
            "error", target.rel, 0, CHECK, f"the probe of this file gave no end record, so the check cannot show that "
                                           f"it read the file.  The output ends: {output[-300:]!r}"))
    return sites, errors


def run_probes(root: Path, build_dir: Path, targets: list[Target], limit: int) -> ProbeResult:
    """Compile the probe of each target, and collect the padded lists.

    Complexity: one compile for each target, PROBE_JOBS at the same time.  ccache returns the result of
    an unchanged probe.

    Args:
        root: The repository root
        build_dir: The configured build directory
        targets: The files to probe
        limit: The element count from which a list counts

    Returns:
        The padded lists and the errors
    """
    result = ProbeResult()
    try:
        sentinel, sources = load_commands(build_dir, root)
    except (compdb.CompdbError, ValueError) as exc:
        result.errors.append(check_report.Finding("error", str(compdb.database_file(build_dir)), 0, CHECK,
                                                  f"the compile database cannot be read: {exc}"))
        return result
    directory = build_dir / PROBE_DIRECTORY
    directory.mkdir(parents=True, exist_ok=True)
    stems = {target.rel.replace("/", "_") for target in targets}
    for stale in directory.iterdir():
        is_staging = stale.name.startswith(".")
        if not is_staging and stale.name != PROBE_HEADER and stale.name.rsplit(".", 1)[0] not in stems:
            stale.unlink(missing_ok=True)
    header = directory / PROBE_HEADER
    write_if_changed(header, PROBE_TEXT)
    launcher = shutil.which("ccache")
    identities: dict[str, str] = {}

    def probe(target: Target) -> tuple[Target, int, str]:
        command = sources.get(target.rel) if target.rel.startswith("src/") else sentinel
        if command is None:
            return target, -1, "the compile database has no command for this file"
        stem = target.rel.replace("/", "_")
        source = directory / f"{stem}.cpp"
        write_if_changed(source, probe_text((root / target.rel).resolve(), target, header, limit))
        # A probe of a source includes that source, so GCC reads its classes as
        # the classes of a header and warns when one of them names a type of an
        # unnamed namespace.  The warning changes no layout.
        argv = [command.compiler, *command.flags, "-fdiagnostics-color=never", "-Wno-subobject-linkage", "-c",
                str(source), "-o", str(directory / f"{stem}.o"), "-MD", "-MF", str(directory / f"{stem}.d")]
        if launcher is not None:
            argv = [launcher, f"compiler_check=string:{identities[command.compiler]}", *argv]
        done = proc.run(proc.Command(argv, timeout_s=proc.TOOL_S, cwd=command.directory))
        return target, done.exit, done.err

    compilers = {command.compiler for command in [sentinel, *sources.values()] if command is not None}
    if launcher is not None:
        identities.update({compiler: compiler_identity(compiler) for compiler in compilers})
    with ThreadPoolExecutor(max_workers=PROBE_JOBS) as pool:
        outcomes = list(pool.map(probe, targets))
    for target, code, output in outcomes:
        result.probed += 1
        if code == -1:
            result.errors.append(check_report.Finding("error", target.rel, 0, CHECK, f"the check cannot probe this "
                                                                                     f"file: {output}."))
            continue
        if code != 0:
            first = next((line.strip() for line in output.splitlines() if "error" in line), output.strip()[:300])
            result.errors.append(check_report.Finding(
                "error", target.rel, 0, CHECK, f"the probe of this file does not compile, so the check cannot read "
                                               f"it: {first}"))
            continue
        sites, errors = parse_probe_output(output, target, root)
        result.errors.extend(errors)
        for site in sites:
            result.sites.setdefault(site.key(), site)
    return result


# ── The ledger and the report ────────────────────────────────────────────────


def ledger_key(key: tuple[str, str, str, str]) -> rule_ledger.Key:
    """Return the ledger key of a padded list: its path, its role, and its declaration and element type."""
    path, role, declaration, element = key
    escaped = declaration.replace(ENTITY_SEPARATOR, ESCAPED_SEPARATOR)
    return rule_ledger.Key(path, role, f"{escaped}{ENTITY_SEPARATOR}{element}")


def read_ledger(root: Path) -> Ledger:
    """Read the ledger of the tree at a root (THE LEDGER).

    A missing ledger has no row.  A ledger that the rows grammar refuses
    holds no row.
    """
    ledger = Ledger()
    try:
        file = rule_ledger.read(rule_ledger.rule_of(CHECK), root)
    except rows_grammar.RowsError as problem:
        message = str(problem).removeprefix(f"{problem.path}:{problem.line}: ")
        ledger.malformed.append(check_report.Finding("error", LEDGER, problem.line, CHECK, message))
        return ledger
    except (rule_ledger.LedgerError, OSError) as problem:
        ledger.malformed.append(check_report.Finding("error", LEDGER, 0, CHECK, str(problem)))
        return ledger
    ledger.file = file
    ledger.malformed += [check_report.Finding("error", LEDGER, 0, CHECK, f"the ledger names the configuration "
                                              f"{item.name}, and a ledger of {CHECK} has no configuration")
                         for item in file.configurations]
    for key, entry in sorted(file.base.items(), key=lambda item: item[1].line):
        declaration, separator, element = key.entity.partition(ENTITY_SEPARATOR)
        if entry.value != 1 or not key.path.startswith(("include/", "src/")) or not key.kind or not separator \
                or not declaration or not element:
            ledger.malformed.append(check_report.Finding(
                "error", LEDGER, entry.line, CHECK,
                f"the row of {key.path or 'no path'} is malformed.  A count row is `PATH | ROLE | 1 | DECLARATION: "
                f"ELEMENT TYPE | -`, with a path under include/ or src/, and a keep row ends with its reason."))
            continue
        list_key = (key.path, key.kind, declaration.replace(ESCAPED_SEPARATOR, ENTITY_SEPARATOR), element)
        if entry.reason:
            ledger.keeps.append(Keep(list_key, entry.reason, entry.line))
        else:
            ledger.rows[list_key] = entry.line
    return ledger


def limit_of(registry: Path) -> int:
    """Return the element count from which a list counts: one more than the error threshold of the measured rule.

    Raises:
        ValueError: If the registry is malformed or has no measured rule for this check
    """
    rows = check_report.read_budgets(registry)
    if CHECK not in rows:
        raise ValueError(f"{registry} has no measured rule {CHECK}.  Add the rule {CHECK} with warn=COUNT, "
                         f"error=COUNT and unit=elements.")
    return int(rows[CHECK].error) + 1


def describe(site: Site) -> str:
    """Return the part of a message that names one padded list."""
    return (f"the {site.role} {unquote(site.entity)} is a fixed list of {site.count} elements of "
            f"{unquote(site.element)}, which has {site.holes} padding bit(s)")


def compare(found: ProbeResult, ledger: Ledger) -> list[check_report.Finding]:
    """Compare the padded lists with the ledger, and return each finding."""
    findings = list(found.errors) + list(ledger.malformed)
    kept = {keep.key for keep in ledger.keeps}
    for key, site in sorted(found.sites.items()):
        if key in kept:
            continue
        if key in ledger.rows:
            findings.append(check_report.Finding(
                "warning", site.path, site.line, CHECK,
                f"{describe(site)}.  With -ftrivial-auto-var-init=zero, each automatic object of it gets one store "
                f"for each hole of each element.  Make each padding byte a member, and remove the row from "
                f"{LEDGER}."))
            continue
        findings.append(check_report.Finding(
            "error", site.path, site.line, CHECK,
            f"{describe(site)}.  With -ftrivial-auto-var-init=zero, each automatic object of it gets one store for "
            f"each hole of each element, with no loop.  Make each padding byte a member, as "
            f"include/crucible/ledger/Verdict.h does with pad.  An element type with padding on purpose gets a keep "
            f"row with its reason in {LEDGER}."))
    for key, number in sorted(ledger.rows.items(), key=lambda item: item[1]):
        if key not in found.sites:
            findings.append(check_report.Finding(
                "error", LEDGER, number, CHECK,
                f"the row names the {key[1]} {unquote(key[2])} of {key[0]}, and the tree has no such padded list.  "
                f"Regenerate the ledger in the same commit: python3 utils/bin/guard check --rule padded-list "
                f"--build-dir BUILD_DIR --write"))
    for keep in ledger.keeps:
        if keep.key not in found.sites:
            findings.append(check_report.Finding(
                "error", LEDGER, keep.line, CHECK,
                f"the keep row names the {keep.key[1]} {unquote(keep.key[2])} of {keep.key[0]}, and the tree has no "
                f"such padded list.  Remove the row."))
    return findings


def evaluate(root: Path, build_dir: Path, registry: Path) -> tuple[list[check_report.Finding], ProbeResult]:
    """Select the files, run the probes and compare the result with the ledger.

    Returns:
        The findings, and the result of the probes
    """
    try:
        limit = limit_of(registry)
    except ValueError as exc:
        failed = ProbeResult(errors=[check_report.Finding("error", rule_registry.REGISTRY_PATH, 0, CHECK, str(exc))])
        return list(failed.errors), failed
    database = compdb.database_file(build_dir)
    if not database.is_file():
        failed = ProbeResult(errors=[check_report.Finding(
            "error", str(database), 0, CHECK,
            "the compile database does not exist, so the check cannot compile its probes.  Configure the build "
            "first.")])
        return list(failed.errors), failed
    targets, errors, skipped = select_targets(root, limit)
    found = run_probes(root, build_dir, targets, limit)
    found.errors[:0] = errors
    for rel in skipped:
        print(f"check-padded-lists: {rel} is not probed, because it does not compile alone ({NOT_STANDALONE}).",
              file=sys.stderr)
    return compare(found, read_ledger(root)), found


def write(root: Path, build_dir: Path, registry: Path) -> int:
    """Write the count rows of the ledger again from the probes, and keep each keep row.

    A new ledger gets the head comment COMMENT, and a ledger that exists
    keeps its own.

    Returns:
        0 when the ledger is written, 1 when an error of the probes or of a row stops the write
    """
    _, found = evaluate(root, build_dir, registry)
    ledger = read_ledger(root)
    blocking = list(found.errors) + list(ledger.malformed)
    if blocking or ledger.file is None:
        for item in blocking:
            print(item.text(), file=sys.stderr)
        print("check-padded-lists: --write does not write the ledger while a probe or a row has an error.",
              file=sys.stderr)
        return 1
    kept = {keep.key for keep in ledger.keeps}
    base: dict[rule_ledger.Key, rule_ledger.Entry] = {}
    for list_key in found.sites:
        if list_key not in kept:
            key = ledger_key(list_key)
            base[key] = rule_ledger.Entry(key, 1, "")
    rows = len(base)
    for keep in ledger.keeps:
        key = ledger_key(keep.key)
        base[key] = rule_ledger.Entry(key, 1, keep.reason)
    comment = ledger.file.comment if rule_ledger.ledger_path(CHECK, root).is_file() else COMMENT
    rule_ledger.store(replace(ledger.file, comment=comment, base=base), root)
    print(f"check-padded-lists: ledger written with {rows} count row(s) and {len(ledger.keeps)} keep row(s).",
          file=sys.stderr)
    return 0


def check(root: Path, build_dir: Path, registry: Path, warnings_dir: Path | None) -> int:
    """Run the check and print each finding.

    Returns:
        1 when one finding is an error, else 0
    """
    findings, found = evaluate(root, build_dir, registry)
    code = check_report.emit(findings, CHECK, warnings_dir)
    warnings = sum(item.level == "warning" for item in findings)
    print(f"check-padded-lists: {found.probed} file(s) probed, {warnings} padded list(s) in the ledger, "
          f"{len(findings) - warnings} error(s).", file=sys.stderr)
    return code


# ── The self-test ────────────────────────────────────────────────────────────


SELF_TEST_HEADER = """#pragma once
#include <array>
#include <cstdint>
#include <inplace_vector>

namespace fixy {
struct Padded {
    std::uint64_t wide = 0;
    std::uint8_t narrow = 0;
};
struct Packed {
    std::uint64_t wide = 0;
    std::uint8_t narrow[8]{};
};
struct Floats {
    double first = 0.0;
    float second = 0.0f;
    float third = 0.0f;
};
struct Bits {
    std::uint32_t low : 3 = 0;
};
struct Wide {
    long double value = 0.0L;
};
template <class T, std::size_t N>
struct Ring {
    T slots[N]{};
};
using PaddedList = std::inplace_vector<Padded, 64>;
using ShortList = std::array<Padded, 63>;
using PackedList = std::array<Packed, 256>;
using FloatList = std::array<Floats, 256>;
using BitList = std::array<Bits, 128>;
using WideList = std::array<Wide, 64>;
using Nested = std::array<std::array<Padded, 16>, 8>;
using Wrapped = Ring<Padded, 100>;
using Raw = Padded[65];
struct Holder {
    Padded raw[70];
};
inline Padded table[80]{};
inline std::array<Padded, 90> make_list() noexcept { return {}; }
inline void take_list(std::array<Padded, 91> list) noexcept { (void)list; }
inline void borrow_list(std::array<Padded, 92> const& list) noexcept { (void)list; }
template <class T>
struct Template {
    std::array<Padded, 93> inside{};
};
inline void local_list() noexcept {
    std::array<Padded, 94> local{};
    (void)local;
}
}  // namespace fixy
"""
# The planted lists that give a finding, by (role, declaration).
SELF_TEST_PLANTED = {
    ("alias", "fixy::PaddedList"), ("alias", "fixy::BitList"), ("alias", "fixy::WideList"),
    ("alias", "fixy::Nested"), ("alias", "fixy::Wrapped"), ("alias", "fixy::Raw"), ("member", "fixy::Holder::raw"),
    ("member", "fixy::table"), ("return", "std::array<fixy::Padded, 90> fixy::make_list()"),
    ("parameter 0", "void fixy::take_list(std::array<Padded, 91>)"),
}


def self_test(build_dir: Path) -> int:
    """Plant each case in a scratch tree, compile its probes with the compiler of the build, and check each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    try:
        sentinel, _ = load_commands(build_dir, tsast.REPO_ROOT)
    except (compdb.CompdbError, ValueError) as exc:
        print(f"check-padded-lists --self-test: the compile database of {build_dir} cannot be read: {exc}")
        return 2
    if sentinel is None:
        print(f"check-padded-lists --self-test: the compile database of {build_dir} has no crucible sentinel.")
        return 2
    with tempfile.TemporaryDirectory(prefix="padded-lists-") as work:
        root = Path(work) / "tree"
        build = Path(work) / "build"
        (root / "include/fixy").mkdir(parents=True)
        (root / "src").mkdir()
        build.mkdir()
        registry = root / "registry.txt"
        registry.write_text(rule_registry.planted([rule_registry.measured_rule(CHECK, 63, 63, "elements",
                                                                               "the planted limit")]),
                            encoding="utf-8")
        ledger = root / LEDGER
        ledger.parent.mkdir(parents=True)
        (root / "include/fixy/Planted.h").write_text(SELF_TEST_HEADER, encoding="utf-8")
        (root / "include/fixy/Clean.h").write_text("#pragma once\n#include <array>\nnamespace fixy {\n"
                                                   "inline std::array<char, 4096> text{};\n}\n", encoding="utf-8")
        (root / "include/fixy/Abstract.h").write_text('#pragma once\n#include "fixy/Planted.h"\nnamespace fixy {\n'
                                                      "using Spelled = Padded[66];\n}\n", encoding="utf-8")
        (root / "src/Planted.cpp").write_text('#include "fixy/Planted.h"\nnamespace {\n'
                                              "fixy::Padded source_table[200]{};\n"
                                              "namespace inner { fixy::Padded inner_table[100]{}; }\n}\n"
                                              'extern "C" {\nfixy::Padded planted_c_table[70]{};\n}\n',
                                              encoding="utf-8")
        flags = [flag for flag in sentinel.flags if not flag.startswith("-I")] + [f"-I{root / 'include'}"]
        rows = [{"directory": str(build), "file": str(build / "s.cpp"), "arguments": [sentinel.compiler, *flags,
                 "-c", str(build / "s.cpp")], "output": f"{build}/CMakeFiles/{SENTINEL_TARGET}.dir/s.cpp.o"},
                {"directory": str(build), "file": str(root / "src/Planted.cpp"),
                 "arguments": [sentinel.compiler, *flags, "-c", str(root / "src/Planted.cpp")]}]
        database = compdb.plant(build, rows)

        def run() -> tuple[list[check_report.Finding], ProbeResult]:
            with contextlib.redirect_stderr(io.StringIO()):
                return evaluate(root, build, registry)

        def plant(rows: str) -> None:
            """Write a ledger of format 1 with the rows of the text."""
            ledger.write_text(f"# The planted ledger.\nformat 1\nrule {CHECK}\n{rows}", encoding="utf-8")

        def row(key: rule_ledger.Key, value: str = "1", reason: str = "") -> str:
            """Return one ledger row, with its line break."""
            return rows_grammar.render([key.path], [key.kind, value, key.entity, reason]) + "\n"

        findings, found = run()
        planted = {(site.role, unquote(site.entity)) for site in found.sites.values()
                   if site.path == "include/fixy/Planted.h"}
        expect("the probes compile with no error", not found.errors)
        expect("the selection skips a header whose lists hold scalars", found.probed == 3)
        expect("found: an alias whose type is an array with no name, in a header that selects only through it",
               any(site.path == "include/fixy/Abstract.h" and unquote(site.entity) == "fixy::Spelled"
                   for site in found.sites.values()))
        for role, entity in sorted(SELF_TEST_PLANTED):
            expect(f"found: the {role} {entity}", (role, entity) in planted)
        expect("not found: a short list, a packed element, float members, a reference parameter, a template and a "
               "local", planted <= SELF_TEST_PLANTED)
        sourced = {unquote(site.entity) for site in found.sites.values() if site.path == "src/Planted.cpp"}
        expect("found: an unnamed namespace of a source, a named namespace inside it, and an extern \"C\" block",
               sourced == {"(unnamed)::source_table", "(unnamed)::inner::inner_table", "planted_c_table"})
        bits = {unquote(site.entity): site.holes for site in found.sites.values()}
        expect("the padding of a bit-field and of long double counts in bits",
               bits.get("fixy::BitList") == 29 and bits.get("fixy::WideList") == 48)
        expect("with no ledger, each padded list is an error",
               findings and all(item.level == "error" for item in findings))

        with contextlib.redirect_stderr(io.StringIO()):
            written = write(root, build, registry)
        findings, found = run()
        text = ledger.read_text(encoding="utf-8")
        expect("--write gives a ledger whose rows give warnings and no error, with the head comment of a new ledger",
               written == 0 and findings and all(item.level == "warning" for item in findings)
               and text.startswith(COMMENT + "format 1\n"))
        entries = sorted(rule_ledger.read(rule_ledger.rule_of(CHECK), root).base)
        counted = "".join(row(key) for key in entries)
        first = entries[0]
        plant(counted.replace(row(first), row(first, reason="the planted reason")))
        findings, _ = run()
        expect("a keep row gives no finding", len(findings) == len(found.sites) - 1)
        with contextlib.redirect_stderr(io.StringIO()):
            written = write(root, build, registry)
        expect("--write keeps the head comment, each keep row and its reason",
               written == 0 and ledger.read_text(encoding="utf-8").startswith("# The planted ledger.\n")
               and row(first, reason="the planted reason") in ledger.read_text(encoding="utf-8"))
        colon = rule_ledger.Key("include/fixy/Planted.h", "alias", "fixy::With%3A colon: fixy::Padded")
        expect("the entity of a ledger row escapes a \": \" of the declaration",
               ledger_key(("include/fixy/Planted.h", "alias", "fixy::With: colon", "fixy::Padded")) == colon)
        plant(counted + row(colon))
        findings, _ = run()
        expect("a row reads back the \": \" of its declaration",
               any(item.level == "error" and item.path == LEDGER and "fixy::With: colon" in item.message
                   for item in findings))
        plant(counted + row(rule_ledger.Key("include/fixy/Planted.h", "alias", "fixy::Gone: fixy::Padded")))
        findings, _ = run()
        expect("a row that names no padded list is an error",
               any(item.level == "error" and item.path == LEDGER for item in findings))
        for body, label in ((counted + "include/fixy/Planted.h | alias\n", "a short row"),
                            (counted + row(first, reason="a reason"), "a second row for one list"),
                            (counted + row(rule_ledger.Key(first.path, first.kind, "fixy::Padded")),
                             "a row with no element type"),
                            (counted + row(rule_ledger.Key(first.path, "", first.entity)), "a row with no role"),
                            (counted + row(rule_ledger.Key("bench/Planted.h", first.kind, first.entity)),
                             "a row with a path outside include/ and src/"),
                            (counted.replace(row(first), row(first, value="2")), "a row with a count of 2"),
                            ("configuration debug kind debug\n" + counted, "a configuration row")):
            plant(body)
            findings, _ = run()
            expect(f"{label} is an error", any(item.level == "error" and item.path == LEDGER for item in findings))
        plant(counted + "include/fixy/Planted.h | alias\n")
        with contextlib.redirect_stderr(io.StringIO()):
            written = write(root, build, registry)
        expect("--write refuses while the ledger does not obey the rows grammar",
               written == 1 and ledger.read_text(encoding="utf-8").endswith("include/fixy/Planted.h | alias\n"))
        plant(counted)

        (root / "include/fixy/Clean.h").write_text("#pragma once\n#include <array>\nnamespace fixy {\n"
                                                   "struct Hole { long wide; char narrow; };\n"
                                                   "inline std::array<Hole, 300> holes{};\n}\n", encoding="utf-8")
        findings, _ = run()
        expect("a new padded list is an error", any(item.level == "error" and item.path == "include/fixy/Clean.h"
                                                    for item in findings))
        (root / "include/fixy/Clean.h").write_text("#pragma once\n#include <array>\nnamespace fixy {\n"
                                                   "struct Hole { long wide; char narrow; };\n"
                                                   "inline std::array<Hole, 300> holes{};\n"
                                                   "inline int broken = undeclared_name;\n}\n", encoding="utf-8")
        findings, _ = run()
        expect("a probe that does not compile is an error",
               any(item.level == "error" and "does not compile" in item.message for item in findings))
        (root / "include/fixy/Clean.h").write_text("#pragma once\n#include <array>\nnamespace fixy {\n"
                                                   "struct Shape { virtual ~Shape() = default; };\n"
                                                   "inline std::array<Shape, 300> shapes{};\n}\n", encoding="utf-8")
        findings, _ = run()
        expect("a polymorphic element type is an error", any("polymorphic" in item.message for item in findings))
        (root / "include/fixy/Clean.h").unlink()

        database.unlink()
        findings, _ = run()
        expect("a missing compile database is an error",
               len(findings) == 1 and "does not exist" in findings[0].message)
    if failures:
        print(f"check-padded-lists --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-padded-lists --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    parser = argparse.ArgumentParser(prog="guard check --rule padded-list", description=__doc__.split("\n", 1)[0])
    check_report.add_arguments(parser)
    parser.add_argument("--build-dir", type=Path, required=True,
                        help="the configured build directory whose compile database gives the probe commands")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--self-test", action="store_true", help="plant each case in a scratch tree")
    mode.add_argument("--write", action="store_true", help="write the ledger rows again from the probes")
    arguments = parser.parse_args(argv)
    root = tsast.REPO_ROOT
    build_dir = arguments.build_dir.resolve()
    try:
        if arguments.self_test:
            return self_test(build_dir)
        if arguments.write:
            return write(root, build_dir, check_report.REGISTRY)
        return check(root, build_dir, check_report.REGISTRY, arguments.warnings_dir)
    except tsast.KitMissing as exc:
        print(f"check-padded-lists: {exc}", file=sys.stderr)
        return 3
