"""ledger-owners — each file of utils/ledgers/ is the ledger of one rule, and each of its rows obeys the policy of that rule.

THE RULE
    The file utils/ledgers/RULE.txt is the ledger of the rule RULE of the
    registry (assay.core.ledger).  The rule refuses:
      owner   a file of utils/ledgers/ whose name is not RULE.txt for a rule
              of the registry, or whose rule has the policy none
      schema  a file that assay.core.ledger.parse refuses for the policy of its
              rule: the rows 'format 1' and 'rule RULE' at the start, the five
              cells of an entry row, a VALUE of the type of the policy (a count
              above 0, the word admit, a number, or BYTES/READERS), a difference
              row only for the policy count, a second row with the same key, and
              a row of the policy admit or max with no reason

WHY
    A ledger that no rule reads, or that its rule cannot read, judges
    nothing.  A row of a file that the tree does not hold gives no finding.
    A ledger fails only on a growth (THE POLICIES of assay.core.ledger).  A file that moves with no row is a new file of
    the rule that owns the ledger, and that rule refuses its findings.  A
    write of the rule removes the rows of the file that went.

Complexity: linear in the rows of the ledgers.
"""

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.meta.runner import MetaContext

RULE = "ledger-owners"
SUFFIX = ".txt"


def ledger_findings(context: MetaContext, file_name: str) -> list[check_report.Finding]:
    """Give the problems of one file of utils/ledgers/: its owner and its schema."""
    shown = f"{rule_ledger.LEDGER_DIRECTORY}/{file_name}"
    stem = file_name.removesuffix(SUFFIX)
    rule = context.registry.rules.get(stem) if file_name.endswith(SUFFIX) else None
    if rule is None:
        return [
            check_report.Finding(
                "error",
                shown,
                0,
                RULE,
                f"the file is not RULE{SUFFIX} for a rule of the registry.  Give it the ID of "
                f"the rule that reads it, or remove it",
                "owner",
                file_name,
            )
        ]
    if rule.policy == rule_registry.Policy.NONE:
        return [
            check_report.Finding(
                "error",
                shown,
                0,
                RULE,
                f"the rule {rule.id} has the policy none, and such a rule has no ledger.  Give the "
                f"rule a policy in {rule_registry.REGISTRY_PATH}, or remove the file",
                "owner",
                file_name,
            )
        ]
    try:
        rule_ledger.read(rule, context.root)
    except rows_grammar.RowsError as problem:
        return [
            check_report.Finding(
                "error",
                shown,
                problem.line,
                RULE,
                f"the file does not obey the ledger schema of the policy "
                f"{rule.policy}: {str(problem).split(': ', 1)[-1]}",
                "schema",
                file_name,
            )
        ]
    return []


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give the problems of each file of utils/ledgers/ of the tree."""
    directory = context.root / rule_ledger.LEDGER_DIRECTORY
    if not directory.is_dir():
        return []
    found: list[check_report.Finding] = []
    for entry in sorted(directory.iterdir()):
        if entry.is_dir():
            found.append(
                check_report.Finding(
                    "error",
                    f"{rule_ledger.LEDGER_DIRECTORY}/{entry.name}",
                    0,
                    RULE,
                    f"the directory {entry.name} is in "
                    f"{rule_ledger.LEDGER_DIRECTORY}/, which holds only the file RULE{SUFFIX} of each rule.  Move it",
                    "owner",
                    entry.name,
                )
            )
            continue
        found += ledger_findings(context, entry.name)
    return found
