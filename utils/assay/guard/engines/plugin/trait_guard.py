"""trait-guard — a trait of the two layers has specializations only in its home, and a gate reads no open template.

C++ has no orphan rule.  Each unit can specialize each template for each
type, also for types of a different owner.  Each fail-closed relation of the
tree is as strong as the discipline that keeps its specializations adjacent
to the declarations that they name.

THE SITES
    The quarantine plugin gives a finding of the kind `outside-home` for
    these sites (utils/tools/quarantine/home.cpp, THE SITES and THE DEFAULT
    HOME):
      * A specialization of a class template or a variable template of a
        header of include/foundation/ or include/fixy/, in a different file.
        An explicit specialization, a specialization with template
        parameters, and an explicit specialization of one member of an
        instance are sites.  The check file of a header stands where the
        header stands
      * The same sites of a template with a home row, in a file that is not
        in a path of the row.  Examples are a substrate trait such as
        is_graded_specialization, a fail-closed relation such as
        retag_policy, and an extension point such as armed_cell
      * A declaration in a fail-closed namespace, in a file that is not in a
        path of its home row: admitted_retags, admitted_policies,
        admitted_transitions, admitted_implications and permission_rows.  A
        row of one name names each namespace of that name.  A call of
        CRUCIBLE_ADMIT_MACHINE_TRANSITION stands where it expands.
    A home-args row of the rule splits-orphan controls can_split_into and the
    other traits of the subregions of a permission.

THE GATES
    A gate is a concept definition or a `requires` clause in a header of
    include/foundation/, include/fixy/ or include/crucible/.  A gate in the
    check file of such a header is a gate too.  The plugin gives a finding of
    the kind `open-read` for each template that a gate reads and that a unit
    can specialize (home.cpp, THE GATES).  Such a template is a variable
    template, a function template, or a member of a class template instance,
    such as trait<T>::value, also through an alias template.  A concept and a
    function that is not a template are closed.  A member of the instance
    that holds the gate is no open read.

THE LEDGER
    utils/ledgers/trait-guard.txt counts the findings of each file, kind and
    template (the policy count).  It holds a forged site that a test makes on
    purpose, and the open reads of each gate file.  A count only falls.  The
    row of an open read of a gate of include/crucible/ gives its reason, and
    a row with no reason is an error.  Each such read gives a warning, and
    the debt then stays in the output.

    Each path of a home row that admits no site is an error too
    (assay.guard.engines.plugin.home, THE PATHS OF A HOME ROW).

Usage
    guard check --rule trait-guard --build-dir DIR [--warnings-dir DIR]
    guard write --rule trait-guard --build-dir DIR [--raise] [--reason TEXT]

Complexity: linear in the finding lines of the build, and O(k log k) for k rows of the ledger.
"""

from collections.abc import Sequence
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import ruletable
from assay.guard.engines.plugin import home, runner
from assay.readers import sections as quarantine_sections

RULE = "trait-guard"
OPEN_READ = "open-read"
CRUCIBLE_TREE = "include/crucible/"


def message(item: quarantine_sections.Finding, homes: Sequence[ruletable.Home]) -> str:
    """Return the text of one finding line of the rule."""
    if item.kind == OPEN_READ:
        return (
            f"the gate reads the open template {item.entity}: a unit can specialize it, and the specialization "
            f"changes what the gate admits.  Read a concept, or a consteval function that is not a template"
        )
    row = home.row_of(item.entity.rsplit("::", 1)[-1], homes) or home.row_of(item.entity, homes)
    where = (
        f"its home row admits only {home.paths_text(row)}"
        if row is not None
        else "its home is the file that defines the template"
    )
    return (
        f"a specialization of {item.entity}, or a declaration of a namespace of its home row, stands here, and "
        f"{where}.  A specialization changes what a concept of the two layers admits.  Move the site to its home"
    )


def is_crucible_gate(path: str) -> bool:
    """Say if a gate file is a header of include/crucible/ or the check file of one."""
    return home.read_as(path).startswith(CRUCIBLE_TREE)


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give the errors of the rule (assay.guard.engines.plugin.home, THE FINDINGS).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return home.rule_findings(context, RULE, message)


def crucible_reads(
    rule: rule_registry.Rule, root: Path, found: Sequence[check_report.Finding]
) -> list[check_report.Finding]:
    """Give an error for each open read of a crucible gate with a row with no reason, and a warning for each other.

    Complexity: linear in the findings, with one read of the ledger.
    """
    try:
        ledger = rule_ledger.read(rule, root)
    except rows_grammar.RowsError, OSError:
        return []
    result: list[check_report.Finding] = []
    for item in found:
        if item.kind != OPEN_READ or not is_crucible_gate(item.path):
            continue
        entry = ledger.base.get(rule_ledger.key_of(item))
        if entry is None:
            continue
        if not entry.reason:
            result.append(
                check_report.Finding(
                    "error",
                    rule_ledger.shown_path(RULE),
                    entry.line,
                    RULE,
                    f"a gate of {CRUCIBLE_TREE} reads the open template {item.entity} at {item.path}:{item.line}, "
                    f"and its row gives no reason.  Give the reason in the REASON cell of its row",
                )
            )
        else:
            result.append(
                check_report.Finding(
                    "warning",
                    item.path,
                    item.line,
                    RULE,
                    f"the gate reads the open template {item.entity}, and a unit can specialize it.  The ledger holds "
                    f"the read as debt: {entry.reason}",
                )
            )
    return result


def judge(rule: rule_registry.Rule, context: runner.PluginContext) -> list[check_report.Finding]:
    """Return the findings of the rule judged against its ledger, with the reasons of the crucible gates (THE LEDGER).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    raw = context.findings_of(rule)
    return [*home.judge(rule, context, raw), *crucible_reads(rule, context.root, raw)]


def run(rule: rule_registry.Rule, context: runner.PluginContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build of the context and write its findings (assay.guard.engines.plugin.home.run)."""
    return home.run(rule, context, warnings_dir)


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the ledger of the rule (assay.guard.engines.plugin.home.write)."""
    return home.write(rule, request)
