"""host-owners — each class of a namespace named host stands only in the file of its own home row.

THE RULE
    A class of a namespace named host owns the key of a context.  Each context
    names its owner as a friend, and a member of the owner is the one door to
    the context.  A unit that gives an owner a second definition gets that
    door, and it can then make each context.

    The quarantine plugin gives a finding of the kind `outside-home` for each
    definition of a class in a namespace named host that is not in the file
    of its home row (utils/tools/quarantine/home.cpp).  It also gives one for
    each such specialization of a class template of that namespace.  The row
    `home host-owners host` names each namespace with that name and admits no
    file.  Each owner then must have a home row of its own in
    utils/rule-table.txt.

    The rule has no ledger, and each finding is an error.  Each path of a
    home row that admits no site is an error too
    (assay.guard.engines.plugin.home, THE PATHS OF A HOME ROW).

Usage
    guard check --rule host-owners --build-dir DIR [--warnings-dir DIR]
    guard check --engine plugin --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build.
"""

from collections.abc import Sequence
from pathlib import Path

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import ruletable
from assay.guard.engines.plugin import home, runner
from assay.readers import sections as quarantine_sections

RULE = "host-owners"


def message(item: quarantine_sections.Finding, homes: Sequence[ruletable.Home]) -> str:
    """Return the text of one finding line of the rule."""
    row = home.row_of(item.entity, homes)
    if row is None:
        return (
            f"the class {item.entity} of a namespace named host stands here, and no home row names it.  A class of "
            f"a host namespace owns the key of a context, and a second owner can make each context.  Remove the "
            f"class, or give it a home row in utils/rule-table.txt with its reason"
        )
    return (
        f"a definition or a specialization of {item.entity} stands here, and its home row admits only "
        f"{home.paths_text(row)}.  A second definition of an owner can make each context.  Remove it"
    )


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give the errors of the rule (assay.guard.engines.plugin.home, THE FINDINGS).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return home.rule_findings(context, RULE, message)


def judge(rule: rule_registry.Rule, context: runner.PluginContext) -> list[check_report.Finding]:
    """Return the findings of the rule judged against its ledger (assay.guard.engines.plugin.home.judge)."""
    return home.judge(rule, context)


def run(rule: rule_registry.Rule, context: runner.PluginContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build of the context and write its findings (assay.guard.engines.plugin.home.run)."""
    return home.run(rule, context, warnings_dir)
