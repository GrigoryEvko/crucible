"""mint-floor — each mint of include/ has two negative-compile fixtures of its own layer (HS14), or a ledger row.

THE RULE
    The HS14 floor of CLAUDE.md section XVIII: each mint factory has a
    minimum of HS14_FLOOR negative-compile fixtures.  Each fixture is a
    mismatch that the constraint of the mint refuses.  The rule reads the
    mint records of the quarantine plugin in the objects of the target all.
    It counts the fixtures of the layer of each mint
    (assay.loop.derive.mint_inventory, THE FIXTURES), and the inventory
    misc/mint-inventory.md shows the same count.

THE LEDGER
    A row of utils/ledgers/mint-floor.txt admits one mint with fewer
    fixtures: `PATH | - | admit | ENTITY | REASON`.  The entity is the full
    name of the mint with its namespaces and the types of its parameters.
    The ledger of the rule mint-pattern has the same entities.  A row that admits no finding
    fails the rule, and the ledger only drains.

THE PLANTS
    A plant compiles a header with mints, and its tree holds the fixture
    directories of the layers.  The witness of the engine plugin gives the
    finding lines of the compile to this module (a judge row).

Usage
    guard check --rule mint-floor --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the mint records and the fixtures, and one kit run parses the fixtures.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import runner
from assay.loop.derive import mint_inventory

RULE = "mint-floor"


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each mint with fewer than HS14_FLOOR fixtures of its layer (THE RULE).

    Raises:
        NotApplicable: If the rule does not apply to the build
        tsast.KitMissing: If the pinned kit is not installed
    """
    try:
        rows = mint_inventory.rows_of(context)
    except mint_inventory.InventoryError as problem:
        return [check_report.Finding("error", context.build_shown(), 0, RULE, f"the mints cannot be read: {problem}")]
    found: list[check_report.Finding] = []
    for row in rows:
        if row.hs14 >= mint_inventory.HS14_FLOOR:
            continue
        message = (
            f"the mint {row.mint.entity} has {row.hs14} negative-compile fixture(s) of the {row.family} layer, and "
            f"HS14 asks for {mint_inventory.HS14_FLOOR}.  Write a fixture that its constraint refuses, in a fixture "
            f"directory of its layer, or add a ledger row with the reason"
        )
        found.append(check_report.Finding("error", row.mint.path, row.mint.line, RULE, message, "", row.mint.entity))
    return found
