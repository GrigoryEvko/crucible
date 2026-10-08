"""parse-clean — each C++ file of the tree parses with the kit, and the rule fills the tree cache.

THE RULE
    Each rule of the engine tsast reads the parse trees of the kit
    (assay.readers.tsast).  A file that does not parse is a gap in each of
    those rules at the same time, so this rule refuses each such file:
      unparseable  a C++ file whose parse gives an error
    The ledger admits the files of tsast.UNPARSEABLE, each with its reason.  A
    row that admits no finding is an error.  So a file of that roster that
    parses clean, or that the tree does not hold, fails the rule.  The scope
    of the rule is each C++ file of the tree, also the files of the roster.

THE FILL
    The rule first gives the files of its part to tsast.fill_trees(), which
    parses each file that the tree cache does not hold, with its macro
    bodies.  ctest runs the rule in parts (--part K/N) as the setup tests of
    the fixture tsast_trees, so each rule that starts after the fill finds
    its trees in the cache.  A part starts a share of the workers of the
    fill.

Complexity: one hash of each file of the part, one parse of each file and of each macro body that the cache does not
hold, and one load of each tree of the part.
"""

from assay.core import report as check_report
from assay.guard.engines.tsast.runner import TsastContext
from assay.readers import tsast

RULE = "parse-clean"
KIND = "unparseable"
# The workers of the fill of all parts together, as tsast gives them to one call.
FILL_WORKERS = 16


def findings(context: TsastContext) -> list[check_report.Finding]:
    """Fill the tree cache with the files of the part of the call, and give an error for each file that does not parse.

    Raises:
        KitMissing: If the pinned kit is not installed
    """
    files = context.part_of(context.scope_files(include_unparseable=True))
    count = context.part[1] if context.part is not None else 1
    tsast.fill_trees(context.root, files, max(1, FILL_WORKERS // count))
    found: list[check_report.Finding] = []
    for tree in context.trees(files, with_broken=True):
        error = tsast.first_error(tree)
        if error is not None:
            line, node = error
            found.append(check_report.Finding(
                "error", tree.path.as_posix(), line, RULE, f"the parse kit cannot read the file at {node}.  Write the "
                f"file in a form that the grammar of the kit reads, or report the construct as a change of the "
                f"grammar.  A file that is not C++ gets a row of tsast.UNPARSEABLE and of the ledger, with its reason",
                KIND))
    return found
