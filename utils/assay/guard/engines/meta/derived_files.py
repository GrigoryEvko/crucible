"""derived-files — each target of `loop derive` gives the bytes that the tree holds.

THE TARGETS
    A target of `loop derive` writes a file of the tree from other files.  The
    rule reads the findings of the function drift() of each target, and it
    gives them as its own findings:
        witness-fixtures  the negative fixtures of the witness rows and their
                          manifest (assay.loop.derive.witness_fixtures)
        header-costs      the kind of each heavy and light row of the rule
                          table (assay.loop.derive.header_costs)
        mint-inventory    misc/mint-inventory.md, from the mint records of the
                          quarantine plugin in the objects of a build
                          (assay.loop.derive.mint_inventory)
        row-hashes        utils/tools/row_hash_golden.txt, the output of the
                          program dump_row_hashes of a build
                          (assay.loop.derive.row_hashes)
    The rule walk-units keeps the check of test/layer/walk-checks.txt,
    because it reads the parse trees of the tree.  The targets aten-ops and
    session-oracle read inputs that are not in the tree, and their own checks
    hold them.

THE BUILD
    The targets mint-inventory and row-hashes read the build of --build-dir:
    the records of its objects, and the output of its program
    dump_row_hashes.  With no build directory, or with a build that does not
    apply, the rule gives a warning at the file of the target.  It compares
    the other targets.  A build for a machine other than x86_64 does not
    apply to the golden (assay.loop.derive.row_hashes, THE MACHINE).

THE PLANTS
    The control control-agree holds a rule table with a witness row and the
    heavy and light rows.  It also holds a registry, the header of the
    witness row, its fixture and the manifest.  The copy of each other plant
    starts with that tree, and the files of the plant go over it
    (runner.copy_overlay()).  A plant has no build.  The plants hold the
    targets witness-fixtures and header-costs.  The test of the module
    mint_inventory compares the inventory with a planted census.  The test
    of the module row_hashes compares the golden with the output of a
    planted program.

Complexity: linear in the rows of the rule table, the fixtures, the mint records and the lines of the golden.
"""

from pathlib import Path

from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import ruletable
from assay.guard.engines.meta.runner import MetaContext, copy_overlay
from assay.guard.engines.plugin import runner as plugin_runner
from assay.loop.derive import header_costs, mint_inventory, row_hashes, witness_fixtures
from assay.readers import tsast

RULE = "derived-files"
# The control that gives the first tree of each plant (THE PLANTS).
BASE = "control-agree"


def plant_context(plant: Path, scratch: Path) -> MetaContext:
    """Copy the tree of the base control and then the files of a plant to a throwaway repository (THE PLANTS)."""
    return copy_overlay(plant, scratch, BASE)


def witness_part(context: MetaContext) -> list[check_report.Finding]:
    """Return the findings of the target witness-fixtures, or one error when its rule table cannot be read."""
    try:
        return witness_fixtures.drift(context.root)
    except (ruletable.TableError, OSError) as problem:
        message = f"the witness rows cannot be read: {problem}"
        return [check_report.Finding("error", witness_fixtures.TABLE.as_posix(), 0, RULE, message)]


def header_part(context: MetaContext) -> list[check_report.Finding]:
    """Return the findings of the target header-costs, or one error when its rows or the registry cannot be read."""
    try:
        return header_costs.drift(context.root)
    except (header_costs.CostError, OSError, rows_grammar.RowsError) as problem:
        message = f"the heavy and light rows cannot be read: {problem}"
        return [check_report.Finding("error", header_costs.TABLE.as_posix(), 0, RULE, message)]


def inventory_part(context: MetaContext) -> list[check_report.Finding]:
    """Return the finding of the target mint-inventory, or a warning when the build does not apply (THE BUILD)."""
    if context.build_dir is None:
        message = "the inventory was not compared: it needs the mint records of a build.  Give --build-dir"
        return [check_report.Finding("warning", mint_inventory.INVENTORY, 0, RULE, message)]
    try:
        return mint_inventory.drift(plugin_runner.PluginContext(context.root, context.build_dir))
    except (plugin_runner.NotApplicable, tsast.KitMissing) as problem:
        message = f"the inventory was not compared: {problem}"
        return [check_report.Finding("warning", mint_inventory.INVENTORY, 0, RULE, message)]
    except mint_inventory.InventoryError as problem:
        message = f"the inventory cannot be made: {problem}"
        return [check_report.Finding("error", mint_inventory.INVENTORY, 0, RULE, message)]


def golden_part(context: MetaContext) -> list[check_report.Finding]:
    """Return the finding of the target row-hashes, or a warning when the build does not apply (THE BUILD)."""
    if context.build_dir is None:
        message = "the golden was not compared: it needs the program dump_row_hashes of a build.  Give --build-dir"
        return [check_report.Finding("warning", row_hashes.GOLDEN, 0, RULE, message)]
    try:
        return row_hashes.drift(context.root, context.build_dir)
    except plugin_runner.NotApplicable as problem:
        message = f"the golden was not compared: {problem}"
        return [check_report.Finding("warning", row_hashes.GOLDEN, 0, RULE, message)]
    except row_hashes.GoldenError as problem:
        message = f"the golden cannot be compared: {problem}"
        return [check_report.Finding("error", row_hashes.GOLDEN, 0, RULE, message)]


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give the findings of each target of THE TARGETS."""
    return [*witness_part(context), *header_part(context), *inventory_part(context), *golden_part(context)]
