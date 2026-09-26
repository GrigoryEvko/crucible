"""The decide:: catalog, read from the parse of Decide.h.

A decide:: procedure is a function definition inside a namespace whose last
name is `decide`.  The two contract audits read the catalog through this
module, so they agree on what a procedure is.  A comment or a string that
names a procedure is not a definition node, so it never counts.
"""

from __future__ import annotations

import tsast

# The live catalog header first, then the paths that it had in earlier
# commits.  The history walk of the cite-ratio audit reads all three.
DECIDE_HEADERS = (
    "include/foundation/contracts/Decide.h",
    "include/crucible/safety/_Decide.h",
    "include/crucible/safety/Decide.h",
)

# The declarator shapes that stand between a function definition and the
# function_declarator that holds its name.
_DECLARATOR_WRAPPERS = frozenset({"pointer_declarator", "reference_declarator", "function_declarator"})


def _namespace_path(node: tsast.Node) -> tuple[str, ...]:
    """The names of the namespaces that enclose a node, outermost first."""
    path: list[str] = []
    ancestor = node.parent
    while ancestor is not None:
        if ancestor.type == "namespace_definition":
            name = ancestor.child_by_field("name")
            if name is not None:
                parts = [name] if name.type == "namespace_identifier" else list(name.descendants("namespace_identifier"))
                path[:0] = [part.text for part in parts]
        ancestor = ancestor.parent
    return tuple(path)


def _defined_name(function: tsast.Node) -> str | None:
    """The plain identifier that a function definition defines, or None for any other name shape."""
    declarator = function.child_by_field("declarator")
    while declarator is not None and declarator.type in _DECLARATOR_WRAPPERS:
        if declarator.type == "function_declarator":
            name = declarator.child_by_field("declarator")
            return name.text if name is not None and name.type == "identifier" else None
        declarator = declarator.child_by_field("declarator")
    return None


def procedures(tree: tsast.Tree) -> list[str]:
    """The names of the decide:: procedures that one parse defines, in source order, each one time.

    Complexity: linear in the number of function definitions of the tree."""
    names: list[str] = []
    for function in tree.find("function_definition"):
        path = _namespace_path(function)
        name = _defined_name(function)
        if path and path[-1] == "decide" and name is not None and name not in names:
            names.append(name)
    return names
