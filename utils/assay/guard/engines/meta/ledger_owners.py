"""ledger-owners — each file of utils/ledgers/ is the ledger of one rule, and each of its rows obeys the policy of that rule.

THE RULE
    The file utils/ledgers/RULE.txt is the ledger of the rule RULE of the
    registry (assay.core.ledger).  The rule refuses:
      owner   a file of utils/ledgers/ whose name is not RULE.txt for a rule
              of the registry, or whose rule has the policy none
      schema  a file that assay.core.ledger.parse refuses for the policy of its
              rule: the rows 'format 1' and 'rule RULE' at the start, the five
              cells of an entry row, a VALUE of the type of the policy (a count
              above 0, the word admit, a number, or BYTES/READERS), a difference
              row only for the policy count, a second row with the same key, and
              a row of the policy admit or max with no reason
      path    a PATH cell of an entry row or a difference row that names no file
              of the tree.  The cell "-" names no file, and a PATH with the
              prefix @build/ names a file of a build directory, which the tree
              does not hold.  When a file with the same name is in the tree, the
              message names it, because the file most likely moved there.

WHY
    A row keyed on a path that no longer exists admits nothing, and the rule
    that reads the row can no longer find its site.  A rename makes such a row
    with no other sign.  This rule makes the move loud before the rule that
    owns the row goes quietly wrong.

Complexity: linear in the rows of the ledgers and in the tracked files.
"""

from pathlib import PurePosixPath

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.meta.runner import MetaContext

RULE = "ledger-owners"
SUFFIX = ".txt"
BUILD_PREFIX = "@build/"


def moved_to(path: str, tracked: tuple[str, ...]) -> list[str]:
    """Return each tracked file with the name of a path that is not in the tree, sorted."""
    name = PurePosixPath(path).name
    return [candidate for candidate in tracked if PurePosixPath(candidate).name == name and candidate != path]


def path_finding(shown: str, entry: rule_ledger.Entry, tracked: tuple[str, ...]) -> check_report.Finding:
    """Return the error of an entry whose PATH names no file of the tree, with the files that it most likely moved to."""
    path = entry.key.path
    candidates = moved_to(path, tracked)
    where = (f"  The file most likely moved to {', '.join(candidates)}: give the row that path"
             if candidates else "  No file with that name is in the tree: remove the row, or give it the new path")
    return check_report.Finding(
        "error", shown, entry.line, RULE, f"the PATH {path} of a row names no file of the tree.{where}", "path", path)


def ledger_findings(context: MetaContext, file_name: str, tracked: tuple[str, ...],
                    tracked_set: frozenset[str]) -> list[check_report.Finding]:
    """Give the problems of one file of utils/ledgers/: its owner, its schema and the PATH of each row."""
    shown = f"{rule_ledger.LEDGER_DIRECTORY}/{file_name}"
    stem = file_name.removesuffix(SUFFIX)
    rule = context.registry.rules.get(stem) if file_name.endswith(SUFFIX) else None
    if rule is None:
        return [check_report.Finding(
            "error", shown, 0, RULE, f"the file is not RULE{SUFFIX} for a rule of the registry.  Give it the ID of "
            f"the rule that reads it, or remove it", "owner", file_name)]
    if rule.policy == rule_registry.Policy.NONE:
        return [check_report.Finding(
            "error", shown, 0, RULE, f"the rule {rule.id} has the policy none, and such a rule has no ledger.  Give the "
            f"rule a policy in {rule_registry.REGISTRY_PATH}, or remove the file", "owner", file_name)]
    try:
        ledger = rule_ledger.read(rule, context.root)
    except rows_grammar.RowsError as problem:
        return [check_report.Finding(
            "error", shown, problem.line, RULE, f"the file does not obey the ledger schema of the policy "
            f"{rule.policy}: {str(problem).split(': ', 1)[-1]}", "schema", file_name)]
    entries = [*ledger.base.values(), *(entry for table in ledger.deltas.values() for entry in table.values())]
    return [path_finding(shown, entry, tracked) for entry in sorted(entries, key=lambda item: item.line)
            if entry.key.path and not entry.key.path.startswith(BUILD_PREFIX) and entry.key.path not in tracked_set]


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give the problems of each file of utils/ledgers/ of the tree."""
    directory = context.root / rule_ledger.LEDGER_DIRECTORY
    if not directory.is_dir():
        return []
    tracked = context.tracked()
    tracked_set = frozenset(tracked)
    found: list[check_report.Finding] = []
    for entry in sorted(directory.iterdir()):
        if entry.is_dir():
            found.append(check_report.Finding(
                "error", f"{rule_ledger.LEDGER_DIRECTORY}/{entry.name}", 0, RULE, f"the directory {entry.name} is in "
                f"{rule_ledger.LEDGER_DIRECTORY}/, which holds only the file RULE{SUFFIX} of each rule.  Move it", "owner",
                entry.name))
            continue
        found += ledger_findings(context, entry.name, tracked, tracked_set)
    return found
