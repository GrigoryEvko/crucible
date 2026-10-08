"""contract-semantic — the contract semantic of each unit agrees with the macro CRUCIBLE_CONTRACT_SEMANTIC_IGNORE.

THE RULE
    GCC uses the last -fcontract-evaluation-semantic of a compile line.
    CRUCIBLE_PRE and CRUCIBLE_POST (include/foundation/contracts/Pre.h,
    Post.h) cannot read the semantic.  The macro
    CRUCIBLE_CONTRACT_SEMANTIC_IGNORE tells them the semantic.  With the
    macro, each precondition gives its condition to the optimizer as an
    assumption.  The language clauses of the same compile can do their
    checks, and a handler can return.  When the condition does not hold, the
    assumption is then undefined behavior.  Without the macro, the ignore
    semantic keeps no hint for the optimizer.

    The quarantine plugin reads the semantic of the unit (unit.cpp, THE
    CONTRACT SEMANTIC).  It also reads the macro of the command line and of
    the files of -include.  The plugin gives a finding of the kind
    contract-semantic at the main file of each of these units:
      * a unit with the macro and with a semantic that is not ignore
      * a unit with the ignore semantic and with no macro.
    The finding is a compile error in each build and each mode, and such a
    unit does not build.  The configure step also rejects a scope that has
    only the flag or only the macro (cmake/ContractSemantic.cmake).  The rule
    reads the finding lines in the sections of the objects of the target all
    of one build (assay.guard.engines.plugin.runner, THE SECTIONS).

The rule has no ledger, and each finding is an error.

Usage
    guard check --rule contract-semantic --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import runner
from assay.readers import sections as plugin_sections

RULE = "contract-semantic"
ACTION = (
    "Give a target the ignore semantic with crucible_contract_ignore_target() of cmake/ContractSemantic.cmake, or "
    "give a source file the COMPILE_OPTIONS property CRUCIBLE_CONTRACT_IGNORE_OPTIONS (CLAUDE.md section XII)"
)


def message(item: plugin_sections.Finding) -> str:
    """Return the message of the error of one finding line."""
    return f"the unit has {item.entity}.  {ACTION}"


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each finding line of the rule and each problem of the census.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return context.census().line_errors(RULE, message)
