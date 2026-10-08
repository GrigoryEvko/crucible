"""no-combine-ids-duplicate — row_hash folds through one body of combine_ids.

THE RULE
    The row_hash machinery folds through one function,
    ::foundation::reflect::combine_ids in include/foundation/reflect/Hash.h.  A
    second body can become different from the first body.  A change of the
    salt 0x9e3779b97f4a7c15, of the mix or of the finalizer fmix64 does not
    change the copy.  The witness of the wire format then breaks, and no
    check gives an error.  The function is constexpr, and one body does the
    fold at compile time and at run time.

    The quarantine plugin gives these findings (utils/tools/quarantine/
    home.cpp, THE BODY OF combine_ids):
      `outside-home`  the definition of the canonical function, in a file
                      that is not in the path of its home row
      `second-body`   a function named combine_ids in a different scope.  A
                      function or a lambda that holds the salt, the two moves
                      of the bits and a call of fmix64.  The moves are left by
                      6 and right by 2
      `derived-name`  a declaration with a name that holds combine_ids and
                      more, such as combine_ids_runtime
    The plugin reads a body in one unit of each build.  Each body of the
    build then counts one time (home.cpp, THE SCOPE).

    The ledger utils/ledgers/no-combine-ids-duplicate.txt admits a body with
    its reason (the policy admit).  A row that admits no finding is an error.
    Each path of the home row that admits no site is an error too
    (assay.guard.engines.plugin.home, THE PATHS OF A HOME ROW).

Usage
    guard check --rule no-combine-ids-duplicate --build-dir DIR [--warnings-dir DIR]
    guard write --rule no-combine-ids-duplicate --build-dir DIR --admit --reason TEXT

Complexity: linear in the finding lines of the build.
"""

from collections.abc import Sequence
from pathlib import Path

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import ruletable
from assay.guard.engines.plugin import home, runner
from assay.readers import sections as quarantine_sections

RULE = "no-combine-ids-duplicate"
CANONICAL = "foundation::reflect::combine_ids"


def message(item: quarantine_sections.Finding, homes: Sequence[ruletable.Home]) -> str:
    """Return the text of one finding line of the rule."""
    if item.kind == "derived-name":
        return (
            f"the name {item.entity} holds combine_ids and more, the name of a copy of combine_ids.  Call "
            f"{CANONICAL}, or give the declaration a name that does not hold combine_ids"
        )
    if item.kind == "second-body":
        return (
            f"{item.entity} is a second body of combine_ids: a function named combine_ids, or a body with its salt "
            f"0x9e3779b97f4a7c15, its shifts by 6 and by 2 and a call of fmix64.  A copy can drift from the one body "
            f"with no failed check.  Call {CANONICAL}"
        )
    return (
        f"the definition of {item.entity} stands here, and its home row admits only "
        f"{home.paths_text(home.row_of(item.entity, homes))}.  Define it only there"
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
