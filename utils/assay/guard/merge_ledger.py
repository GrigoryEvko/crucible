"""merge-ledger — the git merge driver of the ledgers: merge three copies of a ledger by the keys of its rows.

Driver
    git runs the driver for a file with the attribute merge=ledger, such as
    in a `git rebase` of a work tree onto main:

        guard merge-ledger BASE OURS THEIRS PATH [BASE_LABEL OURS_LABEL THEIRS_LABEL]

    BASE, OURS and THEIRS are the files of the three copies (%O, %A and %B
    of gitattributes(5)).  PATH is the path of the file in the tree (%P),
    such as utils/ledgers/step-cost.txt.  The three labels (%S, %X and %Y)
    name the copies in the conflict markers.  git runs the driver in the top
    of the work tree, so the driver reads the registry of the work tree
    there.  The rule of the ledger is the rule of the name of the file.

    The driver merges the three copies with merge() of assay.core.ledger
    (the merge), which the composition of the landing gate also calls.  In a
    rebase, OURS is the copy of main and of the commits that the rebase
    already wrote, and THEIRS is the copy of the commit that the rebase
    writes again.  A merged ledger goes into the file OURS, and the exit is
    0.  So two changes of different keys merge, also on adjacent lines, and
    two raises of one row of a max ledger merge to the larger value.

Refusal
    The driver refuses these merges:
      * a conflict of the merge, such as a row of a max ledger that one
        side lowers or removes
      * a key whose counts merge by a sum.  The gate writes the measured
        count of such a key (the reduce of assay.loop.gate), but a rebase
        runs no reduce, and a sum can be too high
      * a merged ledger with no row, because such a ledger has no file
      * a path that is not the ledger of a rule of the registry, a registry
        that does not read, and a file that cannot be read.
    For a refusal, the driver writes its reason and merges the three files
    by their lines with `git merge-file`, which writes the conflict markers
    into OURS.  The exit is 1 when the lines conflict, else 0.  The conflict
    then goes as it goes with no driver: take the copy of main, and write
    the ledger again with `guard write` on a build.

Activation
    .gitattributes gives the attribute merge=ledger to utils/ledgers/*.txt.
    The configuration of the repository defines the driver:

        git config merge.ledger.name "the rows of a ledger by their keys"
        git config merge.ledger.driver "python3 utils/bin/guard merge-ledger %O %A %B %P %S %X %Y"

    With no such configuration, git merges each ledger by its lines.

`guard selftest tests.core.test_ledger` does a test of the driver in a planted repository.
"""

from collections.abc import Sequence
from pathlib import Path, PurePosixPath

from assay.core import git
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.exit import ExitCode

# The count of the words of a call with no labels and with the three labels (the driver).
PLAIN_WORDS = 4
LABELED_WORDS = 7
# The labels of the conflict markers when git gives none (the refusal).
DEFAULT_LABELS = ("base", "ours", "theirs")
# The largest exit of `git merge-file`, the count of the conflicts.  An exit above it, or below 0, is an error.
MOST_CONFLICTS = 127
USAGE = "usage: guard merge-ledger BASE OURS THEIRS PATH [BASE_LABEL OURS_LABEL THEIRS_LABEL]"


class RefusedMerge(Exception):
    """The driver does not merge the copies by their rows (the refusal)."""


def ledger_rule(root: Path, path: str) -> rule_registry.Rule:
    """Return the rule of the ledger at a path of the tree, from the registry of the tree (the driver).

    Raises:
        RefusedMerge: If the path is not a ledger, the registry does not read, or it has no rule of the ledger
    """
    pure = PurePosixPath(path)
    if pure.parent.as_posix() != rule_ledger.LEDGER_DIRECTORY or pure.suffix != ".txt":
        raise RefusedMerge(f"{path} is not a file of {rule_ledger.LEDGER_DIRECTORY}")
    try:
        text = (root / rule_registry.REGISTRY_PATH).read_text(encoding="utf-8")
    except OSError as problem:
        raise RefusedMerge(f"the registry of {root} cannot be read: {problem}") from None
    registry, problems = rule_registry.examine(text)
    if problems:
        raise RefusedMerge(f"the registry of {root} does not read: {problems[0]}")
    rule = registry.rules.get(pure.stem)
    if rule is None or rule.policy == rule_registry.Policy.NONE:
        raise RefusedMerge(f"the registry of {root} has no rule with the ledger {path}")
    return rule


def merged_text(rule: rule_registry.Rule, files: tuple[Path, Path, Path], path: str) -> str:
    """Return the text of the merge of the base, ours and theirs files of a ledger by its rows (the driver).

    Raises:
        RefusedMerge: If a file cannot be read, or the merge is refused (the refusal)
    """
    try:
        texts = tuple(item.read_text(encoding="utf-8") for item in files)
    except (OSError, UnicodeDecodeError) as problem:
        raise RefusedMerge(f"a copy of {path} cannot be read: {problem}") from None
    try:
        merged = rule_ledger.merge(rule, (texts[0], texts[1], texts[2]), path)
    except rule_ledger.LedgerError as problem:
        raise RefusedMerge(str(problem)) from None
    if merged.summed:
        first = merged.summed[0]
        raise RefusedMerge(
            f"the two sides change the count of the row {rule_ledger.described(first)} of {first.path or '-'}, and "
            f"a sum of two counts needs the reduce of the gate, which a rebase does not run"
        )
    if merged.text is None:
        raise RefusedMerge(f"the merged {path} has no row, and a ledger with no row has no file")
    return merged.text


def line_merge(files: tuple[Path, Path, Path], labels: Sequence[str]) -> int:
    """Merge the three files by their lines into the file ours with `git merge-file`, and give the exit (the refusal).

    Raises:
        git.GitError: If git cannot run, or `git merge-file` gives an error
    """
    base, ours, theirs = files
    base_label, ours_label, theirs_label = labels
    labeled = ("-L", ours_label, "-L", base_label, "-L", theirs_label)
    completed = git.run_any_exit(Path.cwd(), "merge-file", *labeled, str(ours), str(base), str(theirs))
    if not 0 <= completed.exit <= MOST_CONFLICTS:
        raise git.GitError(f"`git merge-file` gives an error: {completed.err.strip()}")
    return ExitCode.OK if completed.exit == 0 else ExitCode.FINDINGS


def main(argv: list[str]) -> int:
    """Merge three copies of a ledger by their rows into the file ours, or by their lines on a refusal (the driver)."""
    if len(argv) not in (PLAIN_WORDS, LABELED_WORDS):
        check_report.write_line(USAGE, is_error=True)
        return ExitCode.USAGE
    files = (Path(argv[0]), Path(argv[1]), Path(argv[2]))
    path = argv[3]
    labels = tuple(argv[PLAIN_WORDS:]) if len(argv) == LABELED_WORDS else DEFAULT_LABELS
    try:
        text = merged_text(ledger_rule(Path.cwd(), path), files, path)
    except RefusedMerge as reason:
        check_report.write_line(f"guard merge-ledger: {path} merges by its lines: {reason}", is_error=True)
        try:
            return line_merge(files, labels)
        except git.GitError as problem:
            check_report.write_line(f"guard merge-ledger: {problem}", is_error=True)
            return ExitCode.FINDINGS
    files[1].write_text(text, encoding="utf-8")
    return ExitCode.OK
