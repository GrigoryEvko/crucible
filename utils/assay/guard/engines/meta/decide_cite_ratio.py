"""decide-cite-ratio — each procedure of the catalog has two production cites.

The catalog is the namespace `decide` of Decide.h (CLAUDE.md section XII).  A
procedure of the catalog earns its row.  A site that cites it lands first,
and the named procedure lands second.  Each procedure has a minimum of
MIN_CITES production cites in each tree.

The rule reads no date and no history.  A commit that changes no cite then
does not change the verdict.  The change that removes a cite or adds a
procedure gets the error.

THE COUNTS
    The module of the rule contract-cites reads the catalog and the cite count
    of each procedure from the parse trees of include/ and src/
    (contract_cites.scan()).  The rule reads the catalog of the work tree.

THE KINDS
    violation   an error: a procedure with fewer than MIN_CITES cites
    unreadable  an error: a file of the scope does not parse, and the rule
                cannot count the cites
    The entity of a finding is the name of the procedure, or the catalog
    header when a file does not parse.

Complexity: one kit run parses the files of include/ and src/, and the count is
linear in their nodes.
"""

from assay.core import report as check_report
from assay.guard.engines.meta.runner import MetaContext
from assay.guard.engines.tsast import contract_cites, decide_catalog
from assay.readers import tsast

RULE = "decide-cite-ratio"
MIN_CITES = 2
CATALOG = decide_catalog.DECIDE_HEADERS[0]


def error(kind: str, entity: str, message: str) -> check_report.Finding:
    """Return one error of the rule at the catalog header."""
    return check_report.Finding("error", CATALOG, 0, RULE, message, kind, entity)


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each procedure with fewer than MIN_CITES cites (THE KINDS).

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    try:
        counts = contract_cites.scan(context.root).procedures
    except (contract_cites.AuditError, tsast.ParseError) as problem:
        shown_problem = " ".join(str(problem).split())
        message = f"the rule cannot count the cites of the procedures: {shown_problem}.  Correct the file"
        return [error("unreadable", CATALOG, message)]
    return [
        error(
            "violation",
            name,
            f"decide::{name} has {cites} production cite(s), and each procedure of {CATALOG} has {MIN_CITES}.  "
            f"Cite it at {MIN_CITES - cites} more site(s), or remove it from the catalog",
        )
        for name, cites in sorted(counts.items())
        if cites < MIN_CITES
    ]
