"""The rules that read the names of the source: the finding lines of one rule in its scope, and the rows of its ledger.

THE NAMES OF THE SOURCE
    The quarantine plugin reads the events of the parser for the rules
    ctx-init-door, ctx-testing-boundary, detail-namespace and
    no-unchecked-access (utils/tools/quarantine/owner.cpp, THE NAMES OF THE
    SOURCE).  Each finding line of such a rule is one use, at the position of
    the name.  Its entity is `HOLDER: SHAPE` (THE PLACE AND THE ENTITY of
    owner.cpp).

THE SCOPE
    `scoped_errors()` keeps the lines of a file in the scope of the rule.  The
    row of the rule in utils/registry.txt gives the scope
    (assay.guard.engines.plugin.declarations, THE SCOPE).  Each line in the
    scope gives one error, and the ledger of the rule judges it.  Each problem
    of the census of the runner is an error too.

THE REASONS
    Each row of the ledger of such a rule gives the reason of its uses.  A row
    with no reason is an error, also with the policy count.

The plants of each rule in utils/plants/ hold the module to its findings.

Complexity: linear in the finding lines of the rule, times the items of its scope, and linear in the rows.
"""

from collections.abc import Callable

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.plugin import declarations, runner
from assay.readers import sections as plugin_sections


def reason_errors(rule: rule_registry.Rule, rows: rule_ledger.Ledger) -> list[check_report.Finding]:
    """Give one error for each row of the ledger with no reason (THE REASONS).  Complexity: linear in the rows."""
    shown = rule_ledger.shown_path(rule.id)
    return [
        check_report.Finding(
            "error",
            shown,
            entry.line,
            rule.id,
            f"the row of {key.path} ({key.entity}) gives no reason.  Give the reason of the reviewed use in the "
            f"REASON cell",
        )
        for key, entry in sorted(rows.base.items())
        if not entry.reason
    ]


def scoped_errors(
    context: runner.PluginContext,
    rule: rule_registry.Rule,
    message: Callable[[plugin_sections.Finding], str],
    row_check: Callable[[rule_ledger.Ledger], list[check_report.Finding]] | None = None,
) -> list[check_report.Finding]:
    """Return each problem of the census, one error for each line of the rule in its scope, and each row error.

    `row_check` gives the errors of the rows of a rule with more limits than a reason, such as a path that the rule
    refuses.  A ledger that cannot be read gives no row error here: the judgement of the ledger reports it.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    found = census.errors(rule.id)
    found += [
        check_report.Finding("error", item.file, item.line, rule.id, message(item), item.kind, item.entity)
        for item in declarations.lines_of(rule, context)
    ]
    try:
        rows = rule_ledger.read(rule, context.root)
    except rows_grammar.RowsError, OSError:
        return found
    return [*found, *reason_errors(rule, rows), *(row_check(rows) if row_check is not None else [])]
