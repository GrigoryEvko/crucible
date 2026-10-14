"""plan-coverage — each item of the plan of the tree has a tier, and each row of the plan names an item of the tree.

THE RULE
    The rule reads the plan of the tree with the build of --build-dir
    (assay.loop.plan, THE ITEMS).  It refuses:
      no-tier        an item with no cover, which no tier runs.  A row of
                     the ledger admits such an item with the reason why it
                     runs nowhere, and what a person runs
      no-target      a sampled item with no target of its evidence (THE
                     SAMPLED ITEMS of assay.loop.plan).  A row of the ledger
                     admits such an item with the reason why it has no class
      preset-block   a problem of the presets file or of the block of a
                     preset (assay.loop.tiers, THE PROBLEMS)
      stale-entry    a row of the plan that names no preset, rule, step or
                     subcommand of the tree
      no-class       a holder of a rule, or a subcommand of utils/bin/loop,
                     with no class in the plan
      configuration  a build with a configuration that is not the
                     configuration of the block of its preset
      input          a master or a module that does not read
    THE PROBLEMS of assay.loop.plan gives the four kinds after no-target.  No
    row of the ledger admits a finding of those kinds or of the kind input
    (REFUSED_KINDS).  Give the item a tier when an entry can run it.  A row
    is only for an item that no entry can run.  When the preset of the build
    has no tier, the rule gives only the problems and the sampled items with
    no target.  The problem of the preset then explains each item of the
    build with no cover.

THE INPUT
    The build of --build-dir gives the tests, the bench targets and the
    configuration of the plan (assay.loop.plan, THE BUILDS).  The rule does
    not apply to a call with no build directory, or to a build directory of
    no configure preset (NotApplicable).

THE PLANTS
    plant_context() copies the base control BASE and then the files of the
    plant over it (copy_overlay() of assay.guard.engines.meta.runner).  Then
    it configures the planted CMake project in the build directory of the
    planted default preset (assay.guard.engines.meta.test_registration).

Complexity: one run of ctest, and linear in the items of the plan.
"""

from pathlib import Path

from assay.core import report as check_report
from assay.guard.engines.meta import test_registration
from assay.guard.engines.meta.runner import MetaContext, NotApplicable, copy_overlay
from assay.loop import plan
from assay.loop.evidence import targets as evidence_targets
from assay.readers import python as python_reader

RULE = plan.RULE
NO_TIER = "no-tier"
NO_TARGET = "no-target"
INPUT = "input"
REFUSED_KINDS = frozenset({*(str(kind) for kind in plan.ProblemKind), INPUT})
LEDGER = f"utils/ledgers/{RULE}.txt"
# The base control of the plants (THE PLANTS).
BASE = "control-agree"


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each item of the plan with no cover, and each problem of the plan (THE RULE).

    Raises:
        NotApplicable: If the call gives no build directory, or the build directory is the build of no preset
    """
    if context.build_dir is None:
        raise NotApplicable("the rule reads the plan with the tests of a build.  Give --build-dir DIR")
    try:
        found = plan.tree_plan(context.root, context.registry, (context.build_dir,))
    except plan.PlanError as problem:
        raise NotApplicable(str(problem)) from None
    except (OSError, python_reader.PythonError) as problem:
        message = f"the plan of the tree does not read: {problem}.  Repair the master or the module"
        return [check_report.Finding("error", plan.LOOP_MASTER, 0, RULE, message, INPUT, "plan")]
    errors = [
        check_report.Finding("error", problem.path, problem.line, RULE, problem.message, problem.kind, problem.entity)
        for problem in found.problems
    ]
    for item in found.untargeted:
        message = (
            f"the {item.kind} {item.name} is a sampled item with no target of its evidence.  Give its class a rule with "
            f"the holder {evidence_targets.HOLDER} and a runner of `loop evidence run`, or give it a row of {LEDGER} with "
            f"the reason why it has no class"
        )
        errors.append(check_report.Finding("error", item.path, item.line, RULE, message, NO_TARGET, item.entity))
    if any(build.preset.tier is None for build in found.builds):
        return errors
    for item in found.uncovered:
        message = (
            f"no tier runs the {item.kind} {item.name}.  Give it a tier, or give it a row of {LEDGER} with the reason "
            f"why no entry can run it"
        )
        errors.append(check_report.Finding("error", item.path, item.line, RULE, message, NO_TIER, item.entity))
    return errors


def plant_context(plant: Path, scratch: Path) -> MetaContext:
    """Copy the base control and a plant over it, configure the planted project, and give its context (THE PLANTS).

    Raises:
        NotApplicable: If the pinned cmake is missing, or the project does not configure
    """
    return test_registration.configured(copy_overlay(plant, scratch, BASE), scratch)
