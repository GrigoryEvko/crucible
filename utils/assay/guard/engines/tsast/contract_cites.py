"""The count of the production cites of each decide:: procedure, from the parse trees of include/ and src/.

THE COUNTS
    The production tree is include/ and src/.  A cite is a name with
    `decide` as its last namespace, such as `decide::in_range` or
    `::foundation::decide::positive<int>`.  Only a cite in use counts.  A
    cite in the body of a test namespace (`tsast.is_test_namespace`), in a
    `using` declaration or a `using` directive, or in a macro definition does
    not count.  An alias declaration (`using X = ...`) counts, because a
    refinement type can hold a predicate that is in use.  A comment and a
    literal are nodes of their own kinds, and a cite in them does not count.
    The catalog of the procedures comes from the parse of `Decide.h`
    (`assay.guard.engines.tsast.decide_catalog`), and each procedure of the
    catalog gets a count.  The rule decide-cite-ratio reads the counts
    (scan()).

Complexity: linear in the number of bytes of the files of include/ and src/.
"""

from collections import Counter
from pathlib import Path

from assay.guard.engines.tsast import decide_catalog
from assay.guard.engines.tsast.runner import TsastContext
from assay.readers import tsast

SCOPE = ("include/", "src/")


class AuditError(Exception):
    """The count is not correct: a directory of the scope is missing, or a file does not parse."""


def is_in_use(node: tsast.Node) -> bool:
    """Return True when a node is production code in use: in no test namespace and in no `using` declaration."""
    if node.ancestor_of_type("using_declaration") is not None:
        return False
    return not tsast.is_test_namespace(tsast.namespace_path(node))


def decide_leaf(name: tsast.Node) -> str | None:
    """Return the name after `decide::` when `decide` is the last namespace of a name, or None."""
    parts = tsast.qualified_parts(name)
    if parts is None:
        return None
    segments = parts[1]
    return segments[-1] if len(segments) >= 2 and segments[-2] == "decide" else None


def count_tree(tree: tsast.Tree) -> Counter[str]:
    """Return the cites in use of one file, by the name after `decide::`.

    Complexity: linear in the number of nodes of the file.
    """
    leaves: Counter[str] = Counter()
    for name_node in tree.find("qualified_identifier"):
        # Only the outermost node of a nested name is the name that the source spells.
        if name_node.parent is not None and name_node.parent.type == "qualified_identifier":
            continue
        leaf = decide_leaf(name_node)
        if leaf is not None and is_in_use(name_node):
            leaves[leaf] += 1
    return leaves


def parsed(tree: tsast.Tree) -> tsast.Tree:
    """Return a tree that parses with no error.

    Raises:
        AuditError: If the file does not parse, because a cite in it cannot be counted
    """
    error = tsast.first_error(tree)
    if error is not None:
        raise AuditError(f"{tree.path.as_posix()} does not parse at {error[1]}, so a cite in it is not counted")
    return tree


def scan(root: Path) -> dict[str, int]:
    """Return the count of the cites in use of each procedure of the catalog, in include/ and src/ (THE COUNTS).

    Raises:
        AuditError: If a directory of the scope is missing, or a file does not parse
        NotApplicable: If the root is not a git work tree
    """
    context = TsastContext(root)
    missing = [top for top in SCOPE if not (context.root / top).is_dir()]
    if missing:
        raise AuditError(f"the tree has no {' or '.join(missing)} directory, so the count would be a false zero")
    catalog = decide_catalog.DECIDE_HEADER
    procedures: list[str] = []
    for tree in context.trees([catalog] if catalog in context.listed() else [], with_broken=True):
        procedures = decide_catalog.procedures(parsed(tree))
    leaves: Counter[str] = Counter()
    for tree in context.trees(context.files(SCOPE), with_broken=True):
        leaves.update(count_tree(parsed(tree)))
    return {name: leaves[name] for name in procedures}
