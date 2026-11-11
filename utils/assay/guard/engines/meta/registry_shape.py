"""The part shape of the rule registry: each row of the registry obeys its schema, and each doc field names a section.

THE PART
    assay.core.registry.examine() reads each row of utils/registry.txt and
    gives each problem:
      - A line that does not obey the rows grammar
      - A row kind that is not format, rule or path
      - A missing field, an unknown field, and a second field with the same key
      - An engine or a policy that is not one of its words
      - A measured rule with only one of the fields `limit` and `unit`
      - A limit that is not a decimal number, and an unknown unit
      - A rule with no two cells
      - A second row with the same key.
    The part gives each problem with the kind row.  It also refuses these
    rows:
      doc     a doc field that names no section of the guide
              (assay.core.registry.doc_section)
      holder  a holder that names no rule of the registry and no holding
              command
    No ledger row admits a finding of the three kinds (KINDS).

THE GUIDE
    The guide is the file that the context names: CLAUDE.md at the root of the
    tree.  Only the root of the repository holds a file with the name
    CLAUDE.md (the part layout).  A plant holds its guide in the file guide.md
    for that reason, and plant_context() gives the context of a plant that
    path.

Complexity: linear in the length of the registry and of the guide.
"""

from pathlib import Path

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.meta.runner import MetaContext, copy_tree

PART = "shape"
PLANT_GUIDE = "guide.md"
ROW = "row"
DOC = "doc"
HOLDER = "holder"
# The kinds of the part.  No ledger row admits them.
KINDS = frozenset({ROW, DOC, HOLDER})


def doc_findings(registry: rule_registry.Registry, guide: str, guide_name: str) -> list[check_report.Finding]:
    """Give each doc field that names no section of the text of a guide, and each holder of no rule."""
    shown = rule_registry.REGISTRY_PATH
    found: list[check_report.Finding] = []
    for rule in registry.rules.values():
        if rule_registry.doc_section(guide, rule.doc) is None:
            message = (
                f"the doc {rule.doc} of the rule {rule.id} names no section of {guide_name}.  Give the section that "
                f"states the rule, such as XV, XV.4 or XXII.R11"
            )
            found.append(check_report.Finding("error", shown, rule.line, PART, message, DOC, rule.id))
        if (
            rule.held_by is not None
            and rule.held_by not in rule_registry.HOLDING_COMMANDS
            and rule.held_by not in registry.rules
        ):
            message = (
                f"the holder {rule.held_by} of the rule {rule.id} is not a rule of the registry.  Name the rule or "
                f"the command that reads its limit"
            )
            found.append(check_report.Finding("error", shown, rule.line, PART, message, HOLDER, rule.id))
    return found


def shape_findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each problem of the rows of the registry of the tree, and each doc field that the guide does not hold."""
    shown = rule_registry.REGISTRY_PATH
    found = [
        check_report.Finding(
            "error", shown, problem.line, PART, str(problem).split(": ", 1)[-1], ROW, str(problem.line)
        )
        for problem in context.problems
    ]
    guide_path = context.root / context.guide
    guide = guide_path.read_text(encoding="utf-8") if guide_path.is_file() else ""
    return found + doc_findings(context.registry, guide, context.guide)


def plant_context(plant: Path, scratch: Path) -> MetaContext:
    """Copy a plant to a throwaway repository, and give the context that reads the guide of the plant, guide.md."""
    context = copy_tree(plant, scratch)
    return MetaContext(context.root, context.build_dir, PLANT_GUIDE)
