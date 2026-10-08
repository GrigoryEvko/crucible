"""detsafe-ledger — no header on the hashing path gets to the hardware ledger.

THE RULE
    A measured verdict can change the speed of Crucible.  It must not change
    the result that Crucible calculates (axiom 8, DetSafe).  The ledger keeps
    answers such as "is AVX-512 faster than AVX2 on this part", and each
    answer is a property of one machine.  The ledger can steer a scheduler,
    set the dimension of a tile or select a thread count.  But it must not
    get to these items:
      * content_hash and merkle_hash
      * the memory plan
      * the selection of a BITEXACT recipe
      * the identity of a Cog that keys the kernel cache.

    The seal rows of the rule table utils/rule-table.txt give each header of
    the hashing path and the sealed directories (`seal ROOT PREFIX...`).  The
    quarantine plugin follows the include closure of each ROOT in each unit,
    through each #include directive that the unit compiles (include.cpp, THE
    SEALS).  The plugin gives these findings:
      * a finding of the kind `sealed-include` for each directive of the
        closure that includes a file in a PREFIX
      * a finding of the kind `sealed-name` for each declaration of the
        namespace of a PREFIX in a file of the closure
      * a finding of the kind `sealed-name` for each reference to a
        declaration of that namespace that a file of the closure spells.
    The rule reads those finding lines in the sections of the objects of the
    target all of one build (assay.guard.engines.plugin.runner, THE
    SECTIONS).  The sentinel unit of each header of include/ compiles the
    header alone, and each ROOT is then the main file of one unit.

THE SEAL ROWS
    The rule also reads the seal rows.  It gives an error for each of these
    problems, at the line of the row:
      * a ROOT that the tree does not hold, because a rule with an incorrect
        root list judges nothing
      * a row that does not seal the directory include/R/ledger/ of each
        project root R, a directory in include/
      * a row that does not seal a directory with the name ledger in include/
        that the tree holds.
    A table with no seal row is an error too.

The rule has no ledger, and each finding is an error.  An exception here is
a DetSafe violation with paperwork: move the consumer off the hashing path.

Usage
    guard check --rule detsafe-ledger --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build, and in the rows of the table times the directories in include/.
"""

from pathlib import Path

from assay.core import report as check_report
from assay.core import ruletable
from assay.core.paths import REPO_ROOT, listing
from assay.guard.engines.plugin import runner
from assay.readers import sections as plugin_sections

RULE = "detsafe-ledger"
# The name of each sealed directory, in each project root.
LEDGER = "ledger"
TABLE = ruletable.TABLE.relative_to(REPO_ROOT)
ACTION = (
    "A ledger verdict is a property of one machine, and it must not get to the hashing path.  Move the consumer off "
    "the hashing path, or give the verdict to the code above it (CLAUDE.md section II, DetSafe)"
)


def ledger_directories(root: Path) -> list[str]:
    """Return include/R/ledger/ for each project root R, and each directory with the name ledger in include/, sorted."""
    base = root / ruletable.INCLUDE_PREFIX
    found = {f"{ruletable.INCLUDE_PREFIX}{path.name}/{LEDGER}/" for path in listing(base) if path.is_dir()}
    found |= {f"{path.relative_to(root).as_posix()}/" for path in listing(base, f"**/{LEDGER}") if path.is_dir()}
    return sorted(found)


def seal_problems(root: Path) -> list[check_report.Finding]:
    """Return an error for each problem of THE SEAL ROWS of the rule table of the tree."""
    try:
        table = ruletable.load(root / TABLE)
    except (OSError, ruletable.TableError) as problem:
        return [check_report.Finding("error", str(TABLE), 0, RULE, f"the rule table cannot be read: {problem}")]
    if not table.seals:
        return [
            check_report.Finding(
                "error",
                str(TABLE),
                0,
                RULE,
                "the rule table has no seal row, and no header of the hashing path is sealed",
            )
        ]
    wanted = ledger_directories(root)
    found: list[check_report.Finding] = []
    for seal in table.seals:
        if not (root / seal.root).is_file():
            found.append(
                check_report.Finding(
                    "error",
                    str(TABLE),
                    seal.line,
                    RULE,
                    f"the seal row names the root {seal.root}, and the tree does not hold it.  Change the row in the "
                    f"commit that moves the file",
                )
            )
        missing = [directory for directory in wanted if directory not in seal.prefixes]
        if missing:
            found.append(
                check_report.Finding(
                    "error",
                    str(TABLE),
                    seal.line,
                    RULE,
                    f"the seal row of {seal.root} does not seal {', '.join(missing)}.  Each seal row seals the "
                    f"directory {LEDGER} of each project root and each directory with the name {LEDGER} in "
                    f"{ruletable.INCLUDE_PREFIX}",
                )
            )
    return found


def message(item: plugin_sections.Finding) -> str:
    """Return the message of the error of one finding line."""
    return f"{item.kind} {item.entity}.  {ACTION}"


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each finding line of the rule, each problem of the seal rows and each problem of the census.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return [*context.census().line_errors(RULE, message), *seal_problems(context.root)]
