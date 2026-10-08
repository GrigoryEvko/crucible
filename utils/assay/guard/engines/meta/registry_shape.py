"""registry-shape — each row of the registry obeys its schema, and each doc field names a section of the guide.

THE RULE
    assay.core.registry.examine() reads each row of utils/registry.txt and
    gives each problem: a line that does not obey the rows grammar, a row
    kind that is not format, rule, job or path, a missing field, an unknown
    field, a second field with the same key, an engine or a policy that is
    not one of its words, a measured rule with only some of warn, error and
    unit, a warning threshold above the error threshold, an unknown unit, a
    rule with no two cells, and a second row with the same key.  This rule
    also refuses a doc field that names no section of the guide
    (assay.core.registry.doc_section), and a holder that names no rule of the
    registry and no holding command.

THE GUIDE
    The guide is the file that the context names: CLAUDE.md at the root of the
    tree.  A plant holds its guide in the file guide.md, because only the root
    of the repository holds a file with the name CLAUDE.md (the rule layout),
    and plant_context() gives the context of a plant that path.

Complexity: linear in the length of the registry and of the guide.
"""

from pathlib import Path

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.meta.runner import MetaContext, copy_tree

RULE = "registry-shape"
PLANT_GUIDE = "guide.md"


def doc_findings(registry: rule_registry.Registry, guide: str, guide_name: str) -> list[check_report.Finding]:
    """Give each rule whose doc field names no section of the text of a guide, and each holder of no rule."""
    shown = rule_registry.REGISTRY_PATH
    found: list[check_report.Finding] = []
    for rule in registry.rules.values():
        if rule_registry.doc_section(guide, rule.doc) is None:
            found.append(check_report.Finding(
                "error", shown, rule.line, RULE, f"the doc {rule.doc} of the rule {rule.id} names no section of "
                f"{guide_name}.  Give the section that states the rule, such as XV, XV.4 or XXII.R11", "doc", rule.id))
        if (rule.held_by is not None and rule.held_by not in rule_registry.HOLDING_COMMANDS
                and rule.held_by not in registry.rules):
            found.append(check_report.Finding(
                "error", shown, rule.line, RULE, f"the holder {rule.held_by} of the rule {rule.id} is not a rule of "
                f"the registry.  Name the rule or the command that reads its thresholds", "holder", rule.id))
    return found


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each problem of the rows of the registry of the tree, and each doc field that the guide does not hold."""
    shown = rule_registry.REGISTRY_PATH
    found = [check_report.Finding("error", shown, problem.line, RULE, str(problem).split(": ", 1)[-1], "row",
                                  str(problem.line)) for problem in context.problems]
    guide_path = context.root / context.guide
    guide = guide_path.read_text(encoding="utf-8") if guide_path.is_file() else ""
    return found + doc_findings(context.registry, guide, context.guide)


def plant_context(plant: Path, scratch: Path) -> MetaContext:
    """Copy a plant to a throwaway repository, and give the context that reads the guide of the plant, guide.md."""
    context = copy_tree(plant, scratch)
    return MetaContext(context.root, context.build_dir, PLANT_GUIDE)
