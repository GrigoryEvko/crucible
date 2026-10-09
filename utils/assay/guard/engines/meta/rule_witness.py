"""rule-witness — each rule has its plants in the tree, and each plants directory belongs to a rule.

THE RULE
    The plants of the rule ID are in utils/plants/ID/: a minimum of one plant,
    whose name starts with "plant-", and one control, whose name starts with
    "control-".  A plant can also start with "warning-" or "skip-"
    (assay.guard.engines.meta.runner, THE WITNESS).  `guard witness --rule ID`
    shows that each plant is red and each control is green, and this rule
    shows that they are in the tree.  A rule with no plant then fails.  The
    rule refuses:
      no-plant   a rule with no plant or no control.  During the migration of the utils system, the ledger admits each
                 rule that has no plant yet, and the change that brings its plants removes its row
      orphan     a directory of utils/plants/ that names no rule of the registry
      name       an entry of a plants directory whose name starts with no prefix of PREFIXES

Complexity: linear in the entries of utils/plants/.
"""

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.meta.runner import CONTROL_PREFIX, PLANT_PREFIX, PLANTS, PREFIXES, MetaContext

RULE = "rule-witness"


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each rule with no plant or no control, and each plants directory or entry that no rule owns."""
    registry = context.registry
    plants_dir = context.root / PLANTS
    found: list[check_report.Finding] = []
    directories = sorted(path for path in plants_dir.iterdir() if path.is_dir()) if plants_dir.is_dir() else []
    for directory in directories:
        relative = directory.relative_to(context.root).as_posix()
        if directory.name not in registry.rules:
            found.append(check_report.Finding(
                "error", relative, 0, RULE, f"the plants directory names no rule of the registry.  Give it the ID "
                f"of its rule, or remove it", "orphan", directory.name))
        for entry in sorted(directory.iterdir()):
            if not entry.name.startswith(PREFIXES):
                found.append(check_report.Finding(
                    "error", entry.relative_to(context.root).as_posix(), 0, RULE, f"the entry of the plants of "
                    f"{directory.name} has a name that starts with no prefix of {', '.join(PREFIXES)}",
                    "name", entry.name))
    for rule in registry.rules.values():
        directory = plants_dir / rule.id
        names = [entry.name for entry in directory.iterdir()] if directory.is_dir() else []
        has_plant = any(name.startswith(PLANT_PREFIX) for name in names)
        has_control = any(name.startswith(CONTROL_PREFIX) for name in names)
        if not (has_plant and has_control):
            missing = " and ".join(f"no {word}" for word, present in (("plant", has_plant), ("control", has_control))
                                   if not present)
            found.append(check_report.Finding(
                "error", rule_registry.REGISTRY_PATH, rule.line, RULE, f"the rule {rule.id} has {missing} in "
                f"{PLANTS}/{rule.id}/.  Write a tree that the rule refuses and a tree that it accepts", "no-plant",
                rule.id))
    return found
