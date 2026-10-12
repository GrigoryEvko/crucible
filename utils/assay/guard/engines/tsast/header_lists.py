"""header-lists — the two header lists of CMake hold the headers that the parse trees of include/ give.

THE LISTS
    CMake reads no header.  Two fragments of CMake give it the headers of two
    reflection walks, and each fragment is one set() command:
      test/layer/walk-headers.cmake        CRUCIBLE_WALK_HEADERS: the crucible
                                           headers that the walk units of
                                           test/layer/CMakeLists.txt include
      test/permission-row-headers.cmake    CRUCIBLE_PERMISSION_ROW_HEADERS: the
                                           headers that the roster test of the
                                           permission tags includes
    A walk unit includes each foundation and fixy header too.  A crucible
    header joins the walk list when it opens a namespace of foundation or
    fixy.  It also joins when it names a macro of foundation or fixy with a
    body that opens a namespace.  The body can open it directly or through
    such a macro.  A header joins the roster list when it declares an alias permission_row or
    parent_type, or when it opens the namespace permission_rows.  `python3
    utils/bin/loop derive header-lists --write` writes the two fragments
    (assay.loop.derive.header_lists).

THE RULE
    The rule derives the text of each fragment from the parse trees, and it
    compares the text with the file.  It refuses:
      `header-list`    a fragment with a text that is not the text of the
                       parse trees, also a missing fragment
      `parse`          a header that the kit cannot parse, through the runner

THE PARSE
    A namespace opens at a namespace_definition with a declaration list, when
    no function body and no lambda holds it.  The names of a macro body that
    parses are its nodes of NAME_LEAVES.  The names of a different body are
    its identifier tokens of pp_tokens().  A body opens a namespace when it
    holds a namespace_definition, or the word namespace when it does not
    parse.  A header names a macro when an identifier node of its tree is the
    name.

Complexity: linear in the number of nodes of the trees of include/.  The macro bodies of foundation and fixy take
O(p * m * w) for p steps of the closure, m macros and w names in a body.
"""

from dataclasses import dataclass
from pathlib import PurePosixPath

from assay.core import report as check_report
from assay.guard.engines.tsast.runner import TsastContext
from assay.readers import tsast

RULE = "header-lists"
INCLUDE = "include"
WRITE_COMMAND = "python3 utils/bin/loop derive header-lists --write"
# The two fragments of THE LISTS, the variable that each one sets, and the lines of its head comment.
WALK_HEADERS = "test/layer/walk-headers.cmake"
ROSTER_HEADERS = "test/permission-row-headers.cmake"
WALK_VARIABLE = "CRUCIBLE_WALK_HEADERS"
ROSTER_VARIABLE = "CRUCIBLE_PERMISSION_ROW_HEADERS"
FRAGMENT_SOURCE = (
    f"`{WRITE_COMMAND}` writes this file from the parse trees,",
    "and the test guard_header_lists fails when the file and the tree disagree.  Do not edit it by hand.",
    "",
)
WALK_COMMENT = (
    f"{WALK_HEADERS} — the crucible headers that the walk units of test/layer/CMakeLists.txt include.",
    *FRAGMENT_SOURCE,
    "A crucible header joins when it opens a namespace of foundation or fixy, or when it names a macro",
    "of foundation or fixy whose body opens a namespace.  Each foundation and fixy header joins too.",
)
ROSTER_COMMENT = (
    f"{ROSTER_HEADERS} — the headers that the roster test of the permission tags includes.",
    *FRAGMENT_SOURCE,
    "A header joins when it declares an alias permission_row or parent_type, or when it opens the",
    "namespace permission_rows.",
)
# The roots of the macros of the walk list, the root of its headers, and the namespaces of a layer.
LAYER_ROOTS = ("include/foundation/", "include/fixy/")
CRUCIBLE_ROOT = "include/crucible/"
LAYER_NAMESPACES = frozenset({"foundation", "fixy"})
# The nodes that give a name in a macro body, and the word that a macro body uses to open a namespace.
NAME_LEAVES = ("identifier", "type_identifier", "namespace_identifier", "field_identifier")
NAMESPACE_WORD = "namespace"
# The aliases and the namespace that put a header in the roster list.
ROSTER_ALIASES = frozenset({"permission_row", "parent_type"})
ROSTER_NAMESPACE = "permission_rows"


@dataclass(frozen=True, slots=True)
class HeaderLists:
    """The crucible headers of the walk units, and the headers of the roster of the permission tags (THE LISTS).

    Each path is relative to include/, in the sequence of the paths.
    """

    walk: tuple[str, ...]
    roster: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class Fragment:
    """One fragment of THE LISTS: its path in the tree and its text."""

    path: str
    text: str


def opened_namespaces(tree: tsast.Tree) -> list[tuple[str, ...]]:
    """Return the full name of each namespace that a tree opens with no function body and no lambda around it."""
    opened: list[tuple[str, ...]] = []
    for node in tree.find("namespace_definition"):
        body = node.child_by_field("body")
        if body is None or body.type != "declaration_list":
            continue
        if node.ancestor_of_type("compound_statement", "lambda_expression") is None:
            opened.append(tuple(tsast.namespace_path(body, skip_inline=True)))
    return opened


def body_words(body: tsast.MacroBody) -> set[str]:
    """Return each name in a macro body, and the word namespace when the body opens a namespace (THE PARSE)."""
    if not body.is_parsed:
        return {token.text for token in tsast.pp_tokens(body.text, body.first_row) if token.kind == "identifier"}
    words = {tsast.lexeme(leaf) for leaf in body.root.descendants(*NAME_LEAVES)}
    if next(body.root.descendants("namespace_definition"), None) is not None:
        words.add(NAMESPACE_WORD)
    return words


def namespace_macros(bodies: list[tsast.MacroBody]) -> frozenset[str]:
    """Return the name of each macro with a body that opens a namespace, directly or through such a macro.

    Complexity: O(p * m * w) for p steps of the closure, m macros and w names in a body.
    """
    words: dict[str, set[str]] = {}
    for body in bodies:
        words.setdefault(body.name, set()).update(body_words(body))
    found: set[str] = set()
    is_growing = True
    while is_growing:
        is_growing = False
        for name, used in sorted(words.items()):
            if name not in found and (NAMESPACE_WORD in used or not used.isdisjoint(found)):
                found.add(name)
                is_growing = True
    return frozenset(found)


def declares_roster_entry(tree: tsast.Tree) -> bool:
    """Tell if a header declares an alias of ROSTER_ALIASES, or opens the namespace ROSTER_NAMESPACE."""
    for node in tree.find("alias_declaration"):
        name = node.child_by_field("name")
        if name is not None and tsast.lexeme(name) in ROSTER_ALIASES:
            return True
    return any(path[-1:] == (ROSTER_NAMESPACE,) for path in opened_namespaces(tree))


def header_lists(context: TsastContext) -> HeaderLists:
    """Return the two header lists that the parse trees of include/ give (THE LISTS).

    Complexity: linear in the number of nodes of the trees of include/.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    headers = sorted((path for path in context.files([f"{INCLUDE}/"]) if path.endswith(".h")), key=PurePosixPath)
    trees = list(context.trees(headers))
    lower = [tree for tree in trees if PurePosixPath(tree.path).as_posix().startswith(LAYER_ROOTS)]
    macros = namespace_macros(context.macro_bodies(lower))
    walk: list[str] = []
    roster: list[str] = []
    for tree in trees:
        rel = PurePosixPath(tree.path).as_posix()
        shown = PurePosixPath(rel).relative_to(INCLUDE).as_posix()
        if rel.startswith(CRUCIBLE_ROOT) and (
            any(path[:1] and path[0] in LAYER_NAMESPACES for path in opened_namespaces(tree))
            or next(tree.root.descendants_named(macros, "identifier"), None) is not None
        ):
            walk.append(shown)
        if declares_roster_entry(tree):
            roster.append(shown)
    return HeaderLists(tuple(walk), tuple(roster))


def fragment_text(variable: str, headers: tuple[str, ...], comment: tuple[str, ...]) -> str:
    """Return the CMake fragment that sets one variable to the headers, after the lines of its head comment."""
    lines = [f"# {line}".rstrip() for line in comment]
    return "\n".join([*lines, f"set({variable}", *(f"    {header}" for header in headers), ")", ""])


def fragments(lists: HeaderLists) -> tuple[Fragment, Fragment]:
    """Return the two fragments of THE LISTS."""
    return (
        Fragment(WALK_HEADERS, fragment_text(WALK_VARIABLE, lists.walk, WALK_COMMENT)),
        Fragment(ROSTER_HEADERS, fragment_text(ROSTER_VARIABLE, lists.roster, ROSTER_COMMENT)),
    )


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Give an error for each fragment with a text that is not the text of the parse trees (THE RULE)."""
    found: list[check_report.Finding] = []
    for fragment in fragments(header_lists(context)):
        file = context.root / fragment.path
        if not file.is_file() or file.read_text(encoding="utf-8") != fragment.text:
            found.append(
                check_report.Finding(
                    "error",
                    fragment.path,
                    0,
                    RULE,
                    f"the file does not hold the headers that the parse trees give, and CMake then gives a unit the "
                    f"wrong headers.  Run: {WRITE_COMMAND}",
                    "header-list",
                    fragment.path,
                )
            )
    return found
