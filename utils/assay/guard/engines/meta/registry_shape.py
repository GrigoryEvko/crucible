"""registry-shape — each row of the registry obeys its schema, and each doc field names a section of the guide.

THE RULE
    assay.core.registry.examine() reads each row of utils/registry.txt and
    gives each problem:
      - A line that does not obey the rows grammar
      - A row kind that is not format, rule, job, path or term
      - A missing field, an unknown field, and a second field with the same key
      - An engine or a policy that is not one of its words
      - A measured rule with only some of the fields `warn`, `error` and `unit`
      - A warning threshold above the error threshold, and an unknown unit
      - A rule with no two cells
      - A term with no category of rule 1.5 or rule 1.12
      - A second row with the same key.
    This rule also refuses these rows:
      - A doc field that names no section of the guide
        (assay.core.registry.doc_section)
      - A holder that names no rule of the registry and no holding command
      - A term row with a word that the table of the STE dictionary
        (assay.guard.engines.meta.ste_dictionary) does not hold.  The
        dictionary gives that word as an approved word.

THE GUIDE
    The guide is the file that the context names: CLAUDE.md at the root of the
    tree.  Only the root of the repository holds a file with the name
    CLAUDE.md (the rule layout).  A plant holds its guide in the file guide.md
    for that reason, and plant_context() gives the context of a plant that
    path.

Complexity: linear in the length of the registry and of the guide.
"""

from pathlib import Path

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.meta import ste_dictionary
from assay.guard.engines.meta.runner import MetaContext, copy_tree

RULE = "registry-shape"
PLANT_GUIDE = "guide.md"


def doc_findings(registry: rule_registry.Registry, guide: str, guide_name: str) -> list[check_report.Finding]:
    """Give each doc field that names no section of the text of a guide, each holder of no rule, and each bad term."""
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
    found += [check_report.Finding(
        "error", shown, term.line, RULE, f"the term {term.word} names a word that the STE dictionary approves, and the "
        f"glossary holds only refused words.  Remove the row", "term", term.word)
        for term in registry.terms if term.word not in ste_dictionary.REFUSED]
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
