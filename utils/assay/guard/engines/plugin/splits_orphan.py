"""splits-orphan — a specialization of can_split_into stands only in the file that holds the definition of each tag.

THE RULE
    The traits of the subregions of a permission, such as can_split_into,
    gate mint_permission_split, mint_permission_combine and
    mint_permission_fork.  A unit that specializes one of them for tags of a
    different owner makes authority over a region of that owner (CLAUDE.md
    section IX).  C++ has no orphan rule, and this rule gives one.

    A home-args row of utils/rule-table.txt names each such trait and each
    variable of one.  The quarantine plugin gives a finding of the kind
    `away-from-arguments` for each specialization of such a template that is
    not in the file of its arguments and not in a path of its row
    (utils/tools/quarantine/home.cpp, THE ARGUMENTS).  An explicit
    specialization, a specialization with template parameters, and an
    explicit specialization of one member of an instance are sites.  Each
    top-level template argument of a site must be a class that the file of
    the site holds the definition of.  A template parameter, a pack
    expansion, a pointer and a reference are no tags.  The plugin finds each
    name by lookup, and an alias of a tag of the file is a tag of the file.

    The ledger utils/ledgers/splits-orphan.txt counts the findings of each
    file and template (the policy count), each with its reason.  Each path of
    a home row that admits no site is an error too
    (assay.guard.engines.plugin.home, THE PATHS OF A HOME ROW).

Usage
    guard check --rule splits-orphan --build-dir DIR [--warnings-dir DIR]
    guard write --rule splits-orphan --build-dir DIR [--raise] [--reason TEXT]

Complexity: linear in the finding lines of the build.
"""

from pathlib import Path

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.plugin import home, runner

RULE = "splits-orphan"
message = home.message_of(
    "a specialization of {entity} stands here, and the file does not define each of its tags.  Its home row admits "
    "only {paths}.  A split for tags of a different file gives authority over a region that the file does not own.  "
    "Specialize the trait in the file that defines its tags"
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


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the ledger of the rule (assay.guard.engines.plugin.home.write)."""
    return home.write(rule, request)
