"""mint — each mint of include/ has the shape of section XXI and two negative-compile fixtures of its layer (HS14).

THE RULE
    The quarantine plugin gives a record of the kind mint for each mint, and
    a record of the kind mint-alias for each alias of a class with a member
    mint (utils/tools/quarantine/declaration.cpp, THE RULE mint).  The rule
    reads those lines in the sections of the objects of the target all
    (assay.guard.engines.plugin.runner, THE SECTIONS).  It keeps the lines in
    the scope of its row of utils/registry.txt.  A record is no finding.  The
    rule has two parts:
      the axes    the plugin gives one finding for each axis of section XXI
                  that a mint does not meet:
                    mint-not-nodiscard   no [[nodiscard]]
                    mint-not-noexcept    no noexcept, or noexcept(false)
                    mint-unconstrained   a template with no constraint
                    mint-unfit-context   a mint that takes a context first,
                                         and no constraint names that context
      the floor   the HS14 floor of CLAUDE.md section XVIII: each mint has a
                  minimum of HS14_FLOOR negative-compile fixtures.  Each
                  fixture is a mismatch that the constraint of the mint
                  refuses.  The rule counts the fixtures of the layer of each
                  mint (assay.loop.derive.mint_inventory, THE FIXTURES), and
                  the inventory misc/mint-inventory.md shows the same count.
                  A mint under the floor is a finding with no kind.

    constexpr is no axis of the rule.  Section XXI gives no constexpr to a
    mint that allocates or reads runtime state, and -Winvalid-constexpr
    refuses an incorrect constexpr.  The record keeps the fact for the
    inventory.

THE POSITION OF A MINT
    A mint with a declaration in one header and its definition in a
    different header has a record of each unit.  The finding of each axis and
    of the floor is at the position of the record with the body
    (assay.guard.engines.plugin.declarations, THE MINTS).

THE LEDGER
    A row of utils/ledgers/mint.txt admits one finding of one mint:
    `PATH | KIND | admit | ENTITY | REASON`.  The entity is the full name of
    the mint, with its namespaces and the types of its parameters.  The kind
    "-" admits a mint under the floor, and the kind of an axis admits that
    axis.  The key holds the kind, so a row of one part admits no finding of
    the other part.  The reason says why the mint keeps its shape or its
    fixtures.  An example: the constraint of the mint is a gate on values that
    no concept can see.  The tree holds no row of an axis.  A row that admits
    no finding gives no finding, and the write of the landing gate removes it.

THE PLANTS
    A plant compiles a header with mints, and its tree holds the fixture
    directories of the layers.  The witness of the engine plugin gives the
    finding lines of the compile of a judge row to this module.

Usage
    guard check --rule mint --build-dir DIR [--warnings-dir DIR]
    guard write --rule mint --build-dir DIR

Complexity: linear in the lines of the rule and the fixtures, O(r log r) for r records, and one kit run parses the
fixtures.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, runner
from assay.loop.derive import mint_inventory
from assay.readers import sections as plugin_sections

RULE = declarations.MINT_RULE
# The action for each kind of finding of the axes (CLAUDE.md section XXI).
ACTIONS = {
    "mint-not-nodiscard": "has no [[nodiscard]].  Give it [[nodiscard]]",
    "mint-not-noexcept": "is not noexcept.  Give it noexcept",
    "mint-unconstrained": "is a template with no constraint.  Give it one concept that holds the fit check",
    "mint-unfit-context": "takes a context, and no constraint names that context.  Write a CtxFits... concept on "
    "the context into its constraint",
}


def axis_findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each axis that a mint does not meet, at the position of its chosen record (THE RULE).

    Complexity: linear in the lines of the rule, and O(r log r) for r records.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
        EntityError: If a mint record does not have its grammar
    """
    lines = declarations.lines_of(declarations.rule_of(context, RULE), context)
    chosen = declarations.chosen_mints(item for item in lines if item.kind == declarations.MINT_RECORD)
    found: list[check_report.Finding] = []
    seen: set[tuple[str, str]] = set()
    for item in lines:
        if item.kind in declarations.RECORD_KINDS or (item.kind, item.entity) in seen:
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


def floor_findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error with no kind for each mint with fewer than HS14_FLOOR fixtures of its layer (THE RULE).

    Complexity: linear in the mint records and the fixtures, and one kit run parses the fixtures.

    Raises:
        NotApplicable: If the rule does not apply to the build
        InventoryError: If the mints, the rule table or the ledger of the rule cannot be read
        tsast.KitMissing: If the pinned kit is not installed
    """
    found: list[check_report.Finding] = []
    for row in mint_inventory.rows_of(context):
        if row.hs14 >= mint_inventory.HS14_FLOOR:
            continue
        entity = row.mint.record.entity
        message = (
            f"the mint {entity} has {row.hs14} negative-compile fixture(s) of the {row.family} layer, and HS14 asks "
            f"for {mint_inventory.HS14_FLOOR}.  Write a fixture that its constraint refuses, in a fixture directory of "
            f"its layer, or add a ledger row with the reason"
        )
        found.append(check_report.Finding("error", row.mint.path, row.mint.line, RULE, message, "", entity))
    return found


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give the errors of the axes and of the floor, and each problem of the census (THE RULE).

    A record or an input that does not read gives one error, and the rule then gives no finding of a mint.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
        tsast.KitMissing: If the pinned kit is not installed
    """
    found = context.census().errors(RULE)
    try:
        return [*found, *axis_findings(context), *floor_findings(context)]
    except plugin_sections.EntityError as problem:
        return [*found, check_report.Finding("error", context.build_shown(), 0, RULE, str(problem))]
    except mint_inventory.InventoryError as problem:
        message = f"the mints cannot be read: {problem}"
        return [*found, check_report.Finding("error", context.build_shown(), 0, RULE, message)]
