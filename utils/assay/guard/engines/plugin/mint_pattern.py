"""mint-pattern — each mint of include/ has the shape of section XXI of CLAUDE.md, or a ledger row with the reason.

THE RULE
    The quarantine plugin gives a record of the kind mint for each mint.  It
    gives a finding for each axis of section XXI that the mint does not meet
    (utils/tools/quarantine/declaration.cpp, THE RULE mint-pattern):
      mint-not-nodiscard   no [[nodiscard]]
      mint-not-constexpr   no constexpr, and the body is no borrow projection
      mint-not-noexcept    no noexcept, or noexcept(false)
      mint-unconstrained   a template with no constraint
      mint-unfit-context   a mint that takes a context first, and no
                           constraint names that context
    The rule reads those lines in the sections of the objects of the target
    all (assay.guard.engines.plugin.runner, THE SECTIONS).  It keeps the
    lines in the scope of its row of utils/registry.txt.  A record is no
    finding.

THE POSITION OF A MINT
    A mint with a declaration in one header and its definition in a
    different header has a record of each unit.  The finding of each axis is
    at the position of the record with the body (assay.guard.engines.plugin.
    declarations, THE MINTS).

THE LEDGER
    A row of utils/ledgers/mint-pattern.txt admits one axis of one mint:
    `PATH | KIND | admit | ENTITY | REASON`.  The entity is the full name of
    the mint, with its namespaces and the types of its parameters.  The
    reason says why the mint keeps its shape.  An example: the mint
    allocates, calls the kernel or reads runtime state, and
    -Winvalid-constexpr refuses constexpr on such a function.  A row that
    admits no finding fails the rule.

Usage
    guard check --rule mint-pattern --build-dir DIR [--warnings-dir DIR]
    guard write --rule mint-pattern --build-dir DIR

Complexity: linear in the lines of the rule, and O(r log r) for r records.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, runner

RULE = declarations.MINT_RULE
# The action for each kind of finding (CLAUDE.md section XXI).
ACTIONS = {
    "mint-not-nodiscard": "has no [[nodiscard]].  Give it [[nodiscard]]",
    "mint-not-constexpr": "is not constexpr.  Give it constexpr, or add a ledger row that says why it allocates or "
    "reads runtime state",
    "mint-not-noexcept": "is not noexcept.  Give it noexcept",
    "mint-unconstrained": "is a template with no constraint.  Give it one concept that holds the fit check",
    "mint-unfit-context": "takes a context, and no constraint names that context.  Write a CtxFits... concept on "
    "the context into its constraint",
}


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each axis that a mint does not meet, at the position of its chosen record, and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    lines = declarations.lines_of(declarations.rule_of(context, RULE), context)
    found = census.errors(RULE)
    try:
        chosen = declarations.chosen_mints(item for item in lines if item.kind == declarations.MINT_RECORD)
    except declarations.RecordError as problem:
        return [*found, check_report.Finding("error", context.build_shown(), 0, RULE, str(problem))]
    seen: set[tuple[str, str]] = set()
    for item in lines:
        if item.kind == declarations.MINT_RECORD or (item.kind, item.entity) in seen:
            continue
        seen.add((item.kind, item.entity))
        mint = chosen.get(item.entity)
        path, line = (mint.path, mint.line) if mint is not None else (item.file, item.line)
        action = ACTIONS.get(item.kind, "falls short of section XXI")
        found.append(
            check_report.Finding(
                "error",
                path,
                line,
                RULE,
                f"the mint {item.entity} {action} (CLAUDE.md section XXI)",
                item.kind,
                item.entity,
            )
        )
    return found
