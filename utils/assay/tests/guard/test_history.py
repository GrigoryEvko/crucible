"""The test of the rule history: the measurement, the thresholds, the ledgers and the quarantine ledger in a planted history.

`guard selftest tests.guard.test_history` runs the test.  Each case builds a
throwaway repository with assay.core.git, runs the rule there, judges its
findings against the planted ledger of the rule as a run does, and prints one
line.  The plants of utils/plants/history/ hold the cases that a linear history
gives.  This test holds the cases that need a merge, a side commit, a move of
a file or many steps of the quarantine ledger.

Exit 0 when each case holds, else 1.
"""

import sys
import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.core import git
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.meta import history
from assay.guard.engines.meta.runner import SKIPPED, MetaContext, NotApplicable, judged

ADMISSIONS = "utils/ledgers/history.txt"
SLOW_LEDGER = "utils/ledgers/slow-row.txt"
BASE_LEDGER = "utils/ledgers/base-row.txt"
QUARANTINE_LEDGER = history.QUARANTINE_LEDGER
HISTORY_RULE = ("rule history engine=meta scope=utils/registry.txt ledger=admit doc=XV | a planted history rule | give "
                "the measurement")
BASE_RULE = ("rule base-row engine=build scope=builddir ledger=baseline warn=2 error=5 unit=% held-by=cmake doc=XV | a "
             "planted baseline | write the baseline again")
LEDGER_HEAD = "# a planted ledger\nformat 1\nrule {rule}\n"

Expect = Callable[[str, bool], None]


def registry_text(slow: tuple[int, int] = (10, 20), size: tuple[int, int] = (2, 4), slow_unit: str = "s") -> str:
    """Return a planted registry with slow-row, size-row, base-row and the rule history."""
    rows = [rule_registry.measured_rule("slow-row", *slow, slow_unit, "a time"),
            rule_registry.measured_rule("size-row", *size, "GB", "a size"), BASE_RULE, HISTORY_RULE]
    return rule_registry.planted(sorted(rows))


def admission_text(rows: list[str]) -> str:
    """Return the ledger of the rule history with the rows."""
    return LEDGER_HEAD.format(rule="history") + "".join(f"{row}\n" for row in rows)


class Scratch:
    """A throwaway repository with a planted registry, two ledgers and a history."""

    def __init__(self, root: Path) -> None:
        """Make the repository and its first commit."""
        self.root = root
        root.mkdir(parents=True)
        git.init(root)
        self.write(rule_registry.REGISTRY_PATH, registry_text())
        self.write(SLOW_LEDGER, LEDGER_HEAD.format(rule="slow-row"))
        self.write(BASE_LEDGER, LEDGER_HEAD.format(rule="base-row") + "total | - | 1 | - | -\n")
        self.commit("Add the planted registry", "The first registry.")

    def write(self, relative: str, text: str) -> None:
        """Write one file of the tree."""
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def commit(self, subject: str, body: str) -> str:
        """Commit each file of the tree, and return the hash."""
        return git.commit_all(self.root, subject, body)

    def side_commit(self, subject: str) -> str:
        """Make a commit of the tree with HEAD as its parent and with no body, and return its hash.  HEAD does not move."""
        git.run(self.root, "add", "-A", "--", ".")
        tree = git.run(self.root, "write-tree").decode().strip()
        return git.run(self.root, "commit-tree", tree, "-p", "HEAD", "-m", subject,
                       extra=git.SCRATCH_IDENTITY).decode().strip()

    def merge(self, sha: str) -> None:
        """Merge a side commit into HEAD."""
        git.run(self.root, "merge", "-q", "--no-edit", sha, extra=git.SCRATCH_IDENTITY)

    def run(self) -> tuple[int, list[check_report.Finding]]:
        """Run the rule, judge its findings against the planted ledger, and return the status and the findings."""
        return verdict(self.root)


def verdict(root: Path) -> tuple[int, list[check_report.Finding]]:
    """Run the rule on a tree as `guard check --rule history` does, and return the status and the findings."""
    context = MetaContext(root)
    try:
        raw = history.findings(context)
    except NotApplicable:
        return SKIPPED, []
    rule = rule_registry.load().rules[history.RULE]
    found = judged(rule, context.root, raw)
    return (1 if any(item.level == "error" for item in found) else 0), found


def measurement_cases(expect: Expect) -> None:
    """Check the reader of a measurement in a commit body."""
    known = {10.0, 20.0, 30.0}
    expect("a measurement in a sentence that names the rule counts",
           history.has_measurement("The slowest job of slow-row took 18.4 s on the host.", "slow-row", "s", known,
                                   20.0, 30.0))
    expect("a measurement above the old threshold and at most the new one counts in each sentence",
           history.has_measurement("Raise the row.\nThe slowest test took 20.2 s in a loaded run.", "slow-row", "s",
                                   known, 20.0, 30.0))
    expect("a body that only tells the change holds no measurement",
           not history.has_measurement("Raise slow-row from 20 s to 30 s.", "slow-row", "s", known, 20.0, 30.0))
    expect("a number outside the interval of the rise counts only in a sentence that names the rule",
           not history.has_measurement("Raise the row.\nEach preset stops a test at 300 s.", "slow-row", "s", known,
                                       20.0, 30.0))
    expect("a number in another unit is no measurement",
           not history.has_measurement("slow-row: the job read 25 GB.", "slow-row", "s", known, 20.0, 30.0))
    expect("a longer rule name is not the rule",
           not history.has_measurement("slow-row-two took 18.4 s.", "slow-row", "s", known, 20.0, 30.0))
    expect("a value that the rule had in an older commit is no measurement",
           not history.has_measurement("slow-row was 10 s in March.", "slow-row", "s", known, 20.0, 30.0))
    expect("a number with no unit, such as a count, is no measurement",
           not history.has_measurement("The change moves 25 tests of slow-row.", "slow-row", "s", known, 20.0, 30.0))
    expect("a percent and a multiple read with their own unit",
           history.has_measurement("Two builds gave instruction-total sums 0.003 % apart.", "instruction-total", "%",
                                   {2.0, 5.0}, 2.0, 5.0)
           and history.has_measurement("<chrono> costs heavy-include 2.4 x <meta>.", "heavy-include", "x", {1.25},
                                       1.25, 3.0))
    expect("a number with commas reads as one number",
           history.has_measurement("constexpr-ops needed 40,000,000 operations.", "constexpr-ops", "operations",
                                   {33554432.0}, 33554432.0, 50000000.0))
    expect("the unit G of the registry reads the old unit 'G instructions' of a body",
           history.has_measurement("The largest compile of compile-instructions took 61 G instructions.",
                                   "compile-instructions", "G", {55.0, 110.0}, 55.0, 120.0))
    expect("the white space after the end of a sentence ends it, also over a line break",
           history.sentences("One.\n  Two") == ["One.", "Two"] and history.sentences("a\n\nb") == ["a", "b"])
    expect("a number directly after a letter or a '.' is no number",
           history.quantities("v2 s, 1.5s and x3 s", "s") == [1.5])


def threshold_cases(expect: Expect, scratch: Path) -> Scratch:
    """Plant rises of a threshold in a throwaway repository, and check each verdict."""
    tree = Scratch(scratch / "repo")
    status, found = tree.run()
    expect("a history with no rise gives no finding", status == 0 and not found)
    slow_line = registry_text().splitlines().index(next(line for line in registry_text().splitlines()
                                                        if line.startswith("rule slow-row"))) + 1
    tree.write(rule_registry.REGISTRY_PATH, registry_text(slow=(10, 30)))
    status, found = tree.run()
    expect("a rise in the working tree gives a warning only, which names the working tree",
           status == 0 and [item.level for item in found] == ["warning"] and history.WORKING_TREE in found[0].message
           and "error threshold of slow-row is 30 s" in found[0].message and found[0].line == slow_line)
    unmeasured = tree.commit("Raise the slow row", "The tests are slower.")
    status, found = tree.run()
    errors = [item for item in found if item.level == "error"]
    warnings = [item for item in found if item.level == "warning"]
    expect("a committed rise with no measurement gives an error at the rule, and the warning names the commit",
           status == 1 and len(errors) == 1 and errors[0].path == rule_registry.REGISTRY_PATH
           and errors[0].line == slow_line and unmeasured[:9] in errors[0].message and len(warnings) == 1
           and f"{unmeasured[:9]} (Raise the slow row) from 20 to 30" in warnings[0].message)
    admitted_row = (f"{rule_registry.REGISTRY_PATH} | error | admit | {unmeasured[:9]} slow-row | the slowest job of "
                    f"slow-row took 26 s")
    tree.write(ADMISSIONS, admission_text([admitted_row]))
    tree.commit("Admit the raise of the slow row", "The admission row gives the slow-row measurement of 26 s.")
    status, found = tree.run()
    expect("an admission row changes the error to a warning of the ledger",
           status == 0 and any(item.path == ADMISSIONS and "rows of the ledger admit" in item.message for item in found))
    tree.write(rule_registry.REGISTRY_PATH, registry_text(slow=(10, 30), size=(2, 6)))
    measured = tree.commit("Raise the size row", "The largest link of size-row took 5.2 GB of memory.")
    status, found = tree.run()
    size = [item for item in found if "size-row" in item.message]
    expect("a committed rise with a measurement gives a warning only, which names the commit",
           status == 0 and len(size) == 1 and size[0].level == "warning"
           and f"{measured[:9]} (Raise the size row) from 4 to 6" in size[0].message)
    tree.write(rule_registry.REGISTRY_PATH, registry_text())
    tree.commit("Lower the two rows", "The rows go back to their first values.")
    status, found = tree.run()
    expect("a threshold back at its lowest value gives no warning, and the admission row still admits its rise",
           status == 0 and found and all(item.path == ADMISSIONS for item in found))
    tree.write(ADMISSIONS, admission_text([f"{rule_registry.REGISTRY_PATH} | error | admit | 012345678 slow-row | "
                                           f"a stale row"]))
    status, found = tree.run()
    expect("an admission row that names no rise is an error, and the rise it named fails again",
           status == 1 and any(item.path == ADMISSIONS and "admits no finding" in item.message for item in found)
           and any(item.path == rule_registry.REGISTRY_PATH and item.level == "error" for item in found))
    tree.write(ADMISSIONS, admission_text([f"{rule_registry.REGISTRY_PATH} | error | admit | 0123 slow-row | a short "
                                           f"hash"]))
    status, found = tree.run()
    expect("a malformed admission row is an error",
           status == 1 and any(item.path == ADMISSIONS and "a row is" in item.message for item in found))
    tree.write(ADMISSIONS, admission_text([admitted_row]))
    return tree


def ledger_cases(expect: Expect, tree: Scratch) -> None:
    """Grow the ledgers, change a unit and land a side commit, and check each verdict."""
    rows = LEDGER_HEAD.format(rule="slow-row")
    tree.write(SLOW_LEDGER, rows + "test/a | time | 21 | - | r\ntest/b | time | 22 | - | r\n")
    grown = tree.commit("Admit two slow tests", "The two tests take 21 s and 22 s.")
    tree.write(BASE_LEDGER, LEDGER_HEAD.format(rule="base-row") + "total | - | 1 | - | -\nfile | - | 1/2 | - | -\n")
    tree.commit("Write the baseline again", "The baseline holds one more file row.")
    status, found = tree.run()
    grew = [item for item in found if item.path in (SLOW_LEDGER, BASE_LEDGER)]
    expect("a ledger above its lowest row count gives a warning that names the commit, and a ledger of a rule with "
           "the policy baseline gives none",
           status == 0 and len(grew) == 1 and grew[0].path == SLOW_LEDGER
           and f"{grown[:9]} (Admit two slow tests) from 0 to 2" in grew[0].message)
    tree.write(SLOW_LEDGER, rows + "test/a | time | 21 | - | r\n")
    tree.commit("Make one slow test fast", "One row goes.")
    status, found = tree.run()
    expect("a ledger that fell but stays above its lowest count still names the commit that added rows",
           any(grown[:9] in item.message for item in found if item.path == SLOW_LEDGER))
    tree.write(rule_registry.REGISTRY_PATH, registry_text(slow_unit="G"))
    tree.commit("Count the slow row in instructions", "The unit changes.")
    status, found = tree.run()
    expect("a rule with a new unit starts a new history", not any("slow-row is" in item.message for item in found))
    tree.write(rule_registry.REGISTRY_PATH, registry_text(slow_unit="G", size=(2, 8)))
    side = tree.side_commit("Raise the size row in a work tree")
    tree.commit("Raise the size row", "The largest link took 7.1 GB.")
    tree.merge(side)
    status, found = tree.run()
    expect("a side commit with no body is measured by the landed commit of the same step",
           not any(side[:9] in item.message for item in found if item.level == "error")
           and any(side[:9] in item.message for item in found if "size-row is" in item.message))
    tree.write(rule_registry.REGISTRY_PATH, registry_text(slow_unit="G", size=(3, 8)))
    lone = tree.side_commit("Raise the size warning in a work tree")
    tree.write(rule_registry.REGISTRY_PATH, registry_text(slow_unit="G", size=(2, 8)))
    tree.merge(lone)
    status, found = tree.run()
    expect("a side commit with no body and no measured twin is an error",
           status == 1 and any(lone[:9] in item.message for item in found if item.level == "error"))


def move_cases(expect: Expect, scratch: Path) -> None:
    """Move the thresholds from the old table into the registry and the admissions into utils/ledgers/."""
    tree = Scratch(scratch / "move")
    old_admissions = history.EARLIER_PATHS[ADMISSIONS][0]
    tree.write(history.OLD_TABLE, "# a planted table\nslow-row | 10 | 20 | s | a time\n"
                                  "count-row | 55 | 110 | G instructions | a count\n")
    tree.write(old_admissions, "# COMMIT | ROW | COLUMN | REASON\n")
    (tree.root / rule_registry.REGISTRY_PATH).unlink()
    tree.commit("Use the old table", "The thresholds are in the old table.")
    tree.write(history.OLD_TABLE, "# a planted table\nslow-row | 10 | 20 | s | a time\n"
                                  "count-row | 55 | 120 | G instructions | a count\n")
    raised = tree.commit("Raise the count row", "The tests are slower.")
    (tree.root / history.OLD_TABLE).unlink()
    (tree.root / old_admissions).unlink()
    rows = [rule_registry.measured_rule("slow-row", 10, 20, "s", "a time"),
            rule_registry.measured_rule("count-row", 55, 120, "G", "a count"), HISTORY_RULE]
    tree.write(rule_registry.REGISTRY_PATH, rule_registry.planted(sorted(rows)))
    tree.write(ADMISSIONS, admission_text([f"{rule_registry.REGISTRY_PATH} | error | admit | {raised[:9]} count-row | "
                                           f"the largest compile of count-row took 117 G instructions"]))
    tree.commit("Move the thresholds into the registry", "Each threshold keeps its value.")
    status, found = tree.run()
    expect("a move of the thresholds into the registry keeps the history of each rule",
           status == 0 and any(raised[:9] in item.message and "count-row is 120 G" in item.message for item in found)
           and not any("slow-row" in item.message for item in found))
    expect("the rule does not count the rows of its own ledger, also after a move to utils/ledgers/",
           not any(item.path == ADMISSIONS and "rows, more than its lowest committed count" in item.message
                   for item in found))

    group = Scratch(scratch / "move-group")
    doors = "utils/ledgers/ctx-init-door.txt"
    first, second = history.EARLIER_PATHS[doors]
    group.write(first, "# the init doors\na\nb\n")
    group.write(second, "# the bg doors\nc\nd\ne\nf\ng\n")
    group.commit("Admit seven doors", "Two files hold the doors.")
    group.write(second, "# the bg doors\nc\nd\ne\nf\ng\nh\n")
    added = group.commit("Admit one more bg door", "Only the second file changes.")
    (group.root / first).unlink()
    (group.root / second).unlink()
    group.write(doors, LEDGER_HEAD.format(rule="ctx-init-door")
                + "".join(f"src/{name}.cpp | init | 1 | {name} | -\n" for name in "abcdefgh"))
    group.commit("Move the doors to one ledger", "The ledger holds each door.")
    status, found = group.run()
    grew = [item.message for item in found if item.path == doors]
    expect("a ledger with two earlier files counts the rows of the two, also at a commit that changed one",
           status == 0 and len(grew) == 1 and "holds 8 rows, more than its lowest committed count 7" in grew[0]
           and f"{added[:9]} (Admit one more bg door) from 7 to 8" in grew[0])


def rebase_row(path: str, sha: str) -> str:
    """Return a rebase row of the ledger of the rule history for a ledger and a commit."""
    return f"{path} | {history.REBASE_KIND} | admit | {sha[:9]} | the planted keys changed"


def grew(found: list[check_report.Finding]) -> list[check_report.Finding]:
    """Return the warnings of the slow ledger above its lowest count."""
    return [item for item in found if item.path == SLOW_LEDGER and item.kind == "ledger-grew"]


def rebase_cases(expect: Expect, scratch: Path) -> None:
    """Start the lowest count of a ledger again at a commit that changed its key or its format.

    Refuse each rebase row that names no rebase commit.
    """
    tree = Scratch(scratch / "rebase")
    head = LEDGER_HEAD.format(rule="slow-row")
    tree.write(SLOW_LEDGER, head + "test/a | time | 21 | - | r\n")
    added = tree.commit("Admit one slow test", "The test takes 21 s.")
    tree.write(SLOW_LEDGER, head + "test/x | time | 21 | first | r\ntest/x | time | 22 | second | r\n")
    tree.write("utils/assay/guard/engines/buildsys/slow_row.py", "# the planted rule\n")
    rekeyed = tree.commit("Key the slow tests by case", "Each case of a test has a row.")
    tree.write(ADMISSIONS, admission_text([rebase_row(SLOW_LEDGER, rekeyed)]))
    status, found = tree.run()
    expect("a rebase at a commit that kept no key and changed the code of the guards starts the lowest count again, "
           "and the row admits the rebase",
           status == 0 and not grew(found)
           and any(item.path == ADMISSIONS and f"{SLOW_LEDGER} has 1 findings" in item.message for item in found))
    tree.write(ADMISSIONS, admission_text([rebase_row(SLOW_LEDGER, added)]))
    status, found = tree.run()
    expect("a rebase at a commit that only added a row is an error, and the ledger above its lowest count still warns",
           status == 1 and len(grew(found)) == 1
           and any(item.kind == history.ROW_KIND and "did not change the key or the format" in item.message
                   for item in found))
    for ledger, said in ((QUARANTINE_LEDGER, "the tree holds no ledger"), (ADMISSIONS, "does not compare the row count"),
                         (BASE_LEDGER, "does not compare the row count")):
        tree.write(ADMISSIONS, admission_text([rebase_row(SLOW_LEDGER, rekeyed), rebase_row(ledger, rekeyed)]))
        status, found = tree.run()
        expect(f"a rebase row of {ledger} is an error",
               status == 1 and any(item.kind == history.ROW_KIND and said in item.message for item in found))
    size_ledger = "utils/ledgers/size-row.txt"
    tree.write(ADMISSIONS, admission_text([rebase_row(SLOW_LEDGER, rekeyed), rebase_row(size_ledger, rekeyed)]))
    tree.write(size_ledger, "# an earlier format\nlink/a\n")
    tree.commit("Admit one large link", "The link takes 5 GB.")
    tree.write(size_ledger, LEDGER_HEAD.format(rule="size-row") + "link/a | size | 5 | - | r\nlink/b | size | 6 | - | r\n")
    moved = tree.commit("Move the size ledger to the rows", "Each link has a row.")
    status, found = tree.run()
    expect("a rebase row at a commit that did not change its ledger is an error",
           status == 1 and any(item.kind == history.ROW_KIND and "did not change" in item.message for item in found))
    tree.write(ADMISSIONS, admission_text([rebase_row(SLOW_LEDGER, rekeyed), rebase_row(size_ledger, moved)]))
    status, found = tree.run()
    expect("a change of the format row is a change of the format, also with no change of the code of the guards",
           status == 0 and not any(item.path == size_ledger and item.kind == "ledger-grew" for item in found))
    tree.write(size_ledger, LEDGER_HEAD.format(rule="size-row") + "link/c | size | 5 | - | r\nlink/d | size | 6 | - | r\n"
                                                                  "link/e | size | 7 | - | r\n")
    tree.write("utils/assay/guard/engines/buildsys/size_row.py", "# the planted rule\n")
    renamed = tree.commit("Name the large links by their outputs", "Each link has a row.")
    rows = [rebase_row(SLOW_LEDGER, rekeyed), rebase_row(size_ledger, moved), rebase_row(size_ledger, renamed)]
    tree.write(ADMISSIONS, admission_text(rows))
    status, found = tree.run()
    expect("of two rebase rows of one ledger, the row at the newer commit applies, and the older row admits nothing",
           status == 1 and any(item.path == ADMISSIONS and "admits no finding" in item.message
                               and moved[:9] in item.message for item in found)
           and not any(item.path == size_ledger and item.kind == "ledger-grew" for item in found))
    tree.write(ADMISSIONS, admission_text([rebase_row(SLOW_LEDGER, rekeyed), rebase_row(size_ledger, renamed)]))
    tree.write(SLOW_LEDGER, head + "test/x | time | 21 | first | r\ntest/x | time | 22 | second | r\n"
                                   "test/y | time | 23 | - | r\n")
    later = tree.commit("Admit a third slow test", "The test takes 23 s.")
    status, found = tree.run()
    expect("a row added after the rebase gives a warning that names only the commits after the rebase",
           status == 0 and len(grew(found)) == 1 and f"after the rebase at {rekeyed[:9]}" in grew(found)[0].message
           and later[:9] in grew(found)[0].message and added[:9] not in grew(found)[0].message)
    tree.write(SLOW_LEDGER, head + "test/z | time | 23 | - | r\ntest/w | time | 24 | - | r\n"
                                   "test/v | time | 25 | - | r\n")
    plain = tree.commit("Key the slow tests by name again", "Each test has a row.")
    tree.write(ADMISSIONS, admission_text([rebase_row(SLOW_LEDGER, plain), rebase_row(size_ledger, renamed)]))
    status, found = tree.run()
    expect("a commit that kept no key and changed no code of the guards changed no key",
           status == 1 and any(item.kind == history.ROW_KIND and plain[:9] in item.message for item in found))
    tree.write(SLOW_LEDGER, head)
    tree.commit("Make each slow test fast", "Each row goes.")
    tree.write(ADMISSIONS, admission_text([rebase_row(SLOW_LEDGER, rekeyed), rebase_row(size_ledger, renamed)]))
    status, found = tree.run()
    expect("a rebase row of a ledger at its lowest count admits nothing, and the rule refuses it",
           status == 1 and any(item.path == ADMISSIONS and "admits no finding" in item.message for item in found))


def scope_cases(expect: Expect, scratch: Path) -> None:
    """Make the scope of a rule larger, and check the rows that a rebase row does not count as growth (THE REBASE).

    These are only the rows of the commit in files that it did not change.
    """
    tree = Scratch(scratch / "scope")
    head = LEDGER_HEAD.format(rule="slow-row")
    rule_module = "utils/assay/guard/engines/buildsys/slow_row.py"

    def slow(*tests: str) -> str:
        """Return the slow ledger with one row for each test."""
        return head + "".join(f"{test} | time | 21 | - | r\n" for test in tests)

    tree.write(SLOW_LEDGER, slow("test/a"))
    tree.commit("Admit one slow test", "The test takes 21 s.")
    tree.write("test/b", "a new slow test\n")
    tree.write(SLOW_LEDGER, slow("test/a", "test/b"))
    debt = tree.commit("Add a slow test", "The new test takes 21 s.")
    tree.write(rule_module, "# the planted rule reads each test\n")
    tree.write(SLOW_LEDGER, slow("test/a", "test/b", "test/c", "test/d"))
    wider = tree.commit("Time each test of the tree", "The rule reads two more tests.")
    tree.write(ADMISSIONS, admission_text([rebase_row(SLOW_LEDGER, wider)]))
    status, found = tree.run()
    expect("a rebase at a larger scope does not count its rows of files that it did not change, and the growth of an "
           "earlier commit still gives the warning",
           status == 0 and len(grew(found)) == 1 and debt[:9] in grew(found)[0].message
           and wider[:9] not in grew(found)[0].message.partition("Rows added:")[2]
           and f"after the rebase at {wider[:9]}" in grew(found)[0].message
           and any(item.path == ADMISSIONS and f"{SLOW_LEDGER} has 1 findings" in item.message for item in found))
    tree.write(rule_module, "# the planted rule reads each test and each probe\n")
    tree.write("test/f", "a new probe\n")
    tree.write(SLOW_LEDGER, slow("test/a", "test/b", "test/c", "test/d", "test/e", "test/f"))
    partial = tree.commit("Time each probe, and add a probe", "The rule reads one more test.")
    tree.write(ADMISSIONS, admission_text([rebase_row(SLOW_LEDGER, wider), rebase_row(SLOW_LEDGER, partial)]))
    status, found = tree.run()
    expect("a larger scope that also adds the row of a file that it changes counts that row as growth, and each of "
           "two rebase rows that changes the result admits its error",
           status == 0 and len(grew(found)) == 1
           and f"{partial[:9]} (Time each probe, and add a probe) from 5 to 6" in grew(found)[0].message
           and any(item.path == ADMISSIONS and f"{SLOW_LEDGER} has 2 findings" in item.message for item in found))
    tree.write(rule_module, "# the planted rule keys each test by its case\n")
    tree.write(SLOW_LEDGER, head + "".join(f"case/{name} | time | 21 | - | r\n" for name in "abcdefg"))
    rekeyed = tree.commit("Key each slow test by its case", "Each case has a row.")
    rows = [rebase_row(SLOW_LEDGER, wider), rebase_row(SLOW_LEDGER, partial), rebase_row(SLOW_LEDGER, rekeyed)]
    tree.write(ADMISSIONS, admission_text(rows))
    status, found = tree.run()
    stale = [item.message for item in found if item.path == ADMISSIONS and "admits no finding" in item.message]
    expect("a rebase row at a larger scope before a change of the key admits nothing, and the rule refuses it",
           status == 1 and len(stale) == 2 and any(wider[:9] in text for text in stale)
           and any(partial[:9] in text for text in stale) and not grew(found))

    quarantined = Scratch(scratch / "scope-quarantine")
    span, vector, probe = ("test/a.cpp", "std::span"), ("test/a.cpp", "std::vector"), ("test/p.cpp", "std::vector")
    for name in ("test/a.cpp", "test/p.cpp"):
        quarantined.write(name, f"// {name}\n")
    quarantined.write(QUARANTINE_LEDGER, rows_text({span: 1, probe: 1}))
    quarantined.commit("Count each entity", "The first counts.")
    quarantined.write("utils/tools/quarantine/plugin.cpp", "// one more name\n")
    quarantined.write("test/p.cpp", "// test/p.cpp holds one more object\n")
    quarantined.write(QUARANTINE_LEDGER, rows_text({span: 2, vector: 1, probe: 2}))
    plugin = quarantined.commit("Read one more name, and add an object to a probe", "The plugin finds more.")
    status, found = quarantined.run()
    expect("with no rebase row, each count that a change of the plugin raised gives a warning",
           status == 0 and len(standing_of(found)) == 3)
    quarantined.write(ADMISSIONS, admission_text([rebase_row(QUARANTINE_LEDGER, plugin)]))
    status, found = quarantined.run()
    expect("a rebase of the quarantine ledger at a larger scope gives no warning for the counts that the commit raised "
           "in files that it did not change, and the count of a file that it changed still warns",
           status == 0 and [item.message.split(" ")[0] for item in standing_of(found)] == ["test/p.cpp"]
           and any(item.path == ADMISSIONS and f"{QUARANTINE_LEDGER} has 1 findings" in item.message for item in found))


def quarantine_text(base: dict[str, int], deltas: dict[str, dict[str, int]] | None = None) -> str:
    """Return a quarantine ledger of format 2 with base counts of the kind std_object and the differences of more
    configurations, each keyed by file."""
    deltas = deltas or {}
    lines = ["# a planted ledger", "format 2", "configuration default machine x86_64"]
    lines += [f"configuration {name} machine aarch64" for name in deltas]
    lines += [f"{file} std_object {count}" for file, count in sorted(base.items()) if count]
    lines += [f"{name} {file} std_object {delta:+d}" for name, table in deltas.items()
              for file, delta in sorted(table.items()) if delta]
    return "\n".join(lines) + "\n"


def entity_text(base: dict[tuple[str, str], int], ledger_format: int = 3,
                regions: dict[tuple[str, str], int] | None = None) -> str:
    """Return a quarantine ledger with base counts of the kind std_object, each keyed by file and entity.

    `regions` gives the counts of the kind opted_out of a ledger of format 4, each keyed by file and entity.
    """
    lines = ["# a planted ledger", f"format {ledger_format}", "configuration default machine x86_64"]
    rows = [(file, "std_object", entity, count) for (file, entity), count in base.items() if count]
    rows += [(file, history.OPTED_OUT, entity, count) for (file, entity), count in (regions or {}).items() if count]
    lines += [f"{file} {kind} {count} {entity}" for file, kind, entity, count in sorted(rows)]
    return "\n".join(lines) + "\n"


def rows_text(base: dict[tuple[str, str], int], deltas: dict[str, dict[tuple[str, str], int]] | None = None) -> str:
    """Return a quarantine ledger in the rows of assay.core.ledger, with base counts of the kind std_object keyed by file
    and entity, and the differences of more configurations."""
    deltas = deltas or {}
    lines = ["# a planted ledger", f"format {rule_ledger.FORMAT}", f"rule {history.QUARANTINE_RULE}",
             "configuration default machine x86_64"]
    lines += [f"configuration {name} machine aarch64" for name in deltas]
    lines += [f"{file} | std_object | {count} | {entity} | -" for (file, entity), count in sorted(base.items()) if count]
    lines += [f"@{name} | {file} | std_object | {delta:+d} | {entity} | -" for name, table in deltas.items()
              for (file, entity), delta in sorted(table.items()) if delta]
    return "\n".join(lines) + "\n"


def errors_of(found: list[check_report.Finding]) -> list[check_report.Finding]:
    """Return the errors at the quarantine ledger and at the ledger of the rule."""
    return [item for item in found if item.level == "error" and item.path in (QUARANTINE_LEDGER, ADMISSIONS)]


def standing_of(found: list[check_report.Finding]) -> list[check_report.Finding]:
    """Return the warnings at the quarantine ledger."""
    return [item for item in found if item.level == "warning" and item.path == QUARANTINE_LEDGER]


def quarantine_cases(expect: Expect, scratch: Path) -> None:
    """Plant raises of the quarantine ledger in a throwaway repository, and check each verdict."""
    tree = Scratch(scratch / "quarantine")
    tree.write(QUARANTINE_LEDGER, "# a planted ledger\nconfiguration machine x86_64\ntest std_object 5\n")
    for name in ("test/a.cpp", "test/b.cpp"):
        tree.write(name, f"// {name}\n")
    tree.commit("Add the ledger of format 1", "The first counts.")
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 3, "test/b.cpp": 2}))
    tree.commit("Count each file", "The format changes, and each total stays.")
    status, found = tree.run()
    expect("a change of the format with the same totals is no raise", not errors_of(found) and not standing_of(found))
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 2, "test/b.cpp": 2}))
    tree.commit("Lower a count", "One finding goes.")
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 2, "test/b.cpp": 3}))
    raised = tree.commit("Raise a count by hand", "A finding comes back.")
    status, found = tree.run()
    expect("a raise with no change of the plugin or the rule table is an error that names the commit",
           status == 1 and [item.message.split(":")[0] for item in errors_of(found)]
           == [f"the commit {raised[:9]} (Raise a count by hand) raised 1 counts of {QUARANTINE_LEDGER}, and it "
               f"changes neither the plugin of utils/tools/quarantine/ nor the rule table utils/rule-table.txt"])
    expect("the raised row gives a warning that names the commit",
           [item.line for item in standing_of(found)] == [5] and raised[:9] in standing_of(found)[0].message
           and "from 2" not in standing_of(found)[0].message and "above 2" in standing_of(found)[0].message)
    tree.write(ADMISSIONS, admission_text([f"{QUARANTINE_LEDGER} | raise | admit | {raised[:9]} | a planted reason"]))
    tree.commit("Admit the raise", "The row admits it.")
    status, found = tree.run()
    expect("an admission row of a raise changes the error to a warning",
           status == 0 and not errors_of(found) and any(item.path == ADMISSIONS and "rows of the ledger admit"
                                                        in item.message for item in found))
    tree.write("utils/tools/quarantine/plugin.cpp", "// a new rule\n")
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 4, "test/b.cpp": 3}))
    ruled = tree.commit("Report a new kind in the plugin", "The plugin finds more.")
    status, found = tree.run()
    expect("a raise with a change of the plugin is no error, and its row gives a warning that names the commit",
           status == 0 and not errors_of(found) and any(ruled[:9] in item.message for item in standing_of(found)))
    git.run(tree.root, "mv", "test/b.cpp", "test/c.cpp")
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 4, "test/c.cpp": 3}))
    tree.commit("Move a file", "git moves the file, and its rows move with it.")
    status, found = tree.run()
    expect("a git move of a file is no raise", status == 0 and not errors_of(found))
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 4, "test/c.cpp": 3}, {"arm": {"test/c.cpp": 1}}))
    tree.commit("Add the configuration of a CI leg", "The leg counts one more finding.")
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 4, "test/c.cpp": 2}, {"arm": {"test/c.cpp": 1}}))
    tree.commit("Lower a count of each build", "Only the base rows change.")
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 4, "test/c.cpp": 2}, {"arm": {"test/c.cpp": 2}}))
    tree.commit("Import the rows of the CI leg", "The leg had the finding in an arm of its own.")
    status, found = tree.run()
    expect("a new configuration, and a later configuration that comes back to a count of its history, are no raise",
           status == 0 and not errors_of(found))
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 4, "test/c.cpp": 2}, {"arm": {"test/c.cpp": 3}}))
    above = tree.commit("Import more rows", "The leg finds one more.")
    status, found = tree.run()
    expect("a later configuration above each count of its history is an error",
           status == 1 and any(above[:9] in item.message for item in errors_of(found)))
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 5, "test/c.cpp": 2}, {"arm": {"test/c.cpp": 3}}))
    status, found = tree.run()
    expect("a raise in the working tree gives a warning only",
           any(history.WORKING_TREE in item.message and item.level == "warning" for item in found)
           and not any(history.WORKING_TREE in item.message for item in errors_of(found)))

    late = Scratch(scratch / "quarantine-stale")
    late.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 2}, {"arm": {"test/a.cpp": 1}}))
    late.commit("Count two configurations", "The first counts.")
    late.write("utils/tools/quarantine/plugin.cpp", "// a new rule\n")
    late.commit("Report a new kind in the plugin", "The base keeps its counts.")
    late.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 2}, {"arm": {"test/a.cpp": 3}}))
    caught_up = late.commit("Import the rows of the CI leg", "The leg sees the new kind.")
    status, found = late.run()
    expect("a raise of a later configuration after a change of the plugin that did not write it is no error",
           status == 0 and not errors_of(found) and any(caught_up[:9] in item.message for item in standing_of(found)))
    late.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 2}, {"arm": {"test/a.cpp": 4}}))
    again = late.commit("Import more rows", "The leg finds one more.")
    status, found = late.run()
    expect("the next raise of that configuration is an error, because its rows are current again",
           status == 1 and any(again[:9] in item.message for item in errors_of(found)))
    renamed = Scratch(scratch / "quarantine-rename")
    renamed.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 2}))
    renamed.commit("Count one configuration", "The first counts.")
    renamed.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 3}).replace("configuration default",
                                                                                "configuration x86_64-debug-asan"))
    hidden = renamed.commit("Rename the base and raise a count", "A new name must not hide the raise.")
    status, found = renamed.run()
    expect("a new name of the base keeps its history, so a raise in the same commit is an error",
           status == 1 and any(hidden[:9] in item.message for item in errors_of(found)))
    late.write("utils/tools/quarantine/plugin.h", "// a second rule\n")
    late.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 5}, {"arm": {"test/a.cpp": 4}}))
    base_raise = late.commit("Report a second kind in the plugin", "The base finds three more.")
    status, found = late.run()
    named = [item.message for item in standing_of(found) if base_raise[:9] in item.message]
    expect("a raise of a base count is one rise, and not one more for each later configuration",
           len(named) == 1 and "configuration default" in named[0])

    jump = Scratch(scratch / "quarantine-format")
    jump.write(QUARANTINE_LEDGER, "# a planted ledger\nconfiguration machine x86_64\ntest std_object 5\n")
    jump.commit("Add the ledger of format 1", "The first counts.")
    jump.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 4, "test/b.cpp": 2}))
    grown = jump.commit("Count each file", "The total grows.")
    status, found = jump.run()
    expect("a change of the format that raises a total is an error",
           status == 1 and any(grown[:9] in item.message and history.TOTAL in item.message for item in errors_of(found)))

    keyed = Scratch(scratch / "quarantine-entity")
    keyed.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 2}))
    keyed.commit("Count each file", "The first counts.")
    keyed.write("utils/tools/quarantine/plugin.cpp", "// a new rule\n")
    keyed.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 3}))
    plugin = keyed.commit("Report a new kind in the plugin", "The plugin finds one more.")
    keyed.write(QUARANTINE_LEDGER, entity_text({("test/a.cpp", "std::span"): 1, ("test/a.cpp", "std::vector"): 2}))
    keyed.commit("Count each entity", "The format changes, and each count of a file stays.")
    status, found = keyed.run()
    expect("a change to the format of entities with the same count of each file is no raise, and the history stays",
           status == 0 and not errors_of(found) and [item.line for item in standing_of(found)] == [4]
           and plugin[:9] in standing_of(found)[0].message
           and "has 3 findings, above 2" in standing_of(found)[0].message)
    keyed.write(QUARANTINE_LEDGER, entity_text({("test/a.cpp", "std::span"): 1, ("test/a.cpp", "std::vector"): 1,
                                                ("test/a.cpp", "std::map"): 1}))
    swapped = keyed.commit("Swap an entity", "One std::vector goes and std::map comes.")
    status, found = keyed.run()
    expect("an entity swap with the same count of the file and kind is an error that names the new entity",
           status == 1 and any(swapped[:9] in item.message and "test/a.cpp std-object std::map in default from 0 to 1"
                               in item.message for item in errors_of(found))
           and any("std-object std::map" in item.message and swapped[:9] in item.message
                   for item in standing_of(found)))

    covered = Scratch(scratch / "quarantine-regions")
    span = {("test/a.cpp", "std::span"): 1}
    covered.write(QUARANTINE_LEDGER, entity_text(span))
    covered.commit("Count each entity", "The first counts.")
    covered.write(QUARANTINE_LEDGER, entity_text(span, 4, {("test/a.cpp", "PROBE: a region | std_object std::map"): 2}))
    covered.commit("Count the findings of each region", "The format counts what each region covers.")
    status, found = covered.run()
    expect("the first version that counts the findings of each region raises no count",
           status == 0 and not errors_of(found) and not standing_of(found))
    covered.write(QUARANTINE_LEDGER, entity_text(span, 4, {("test/a.cpp", "PROBE: a region | std_object std::map"): 3}))
    admitted = covered.commit("Admit a region that covers more", "The region PROBE: a region needs one more object.")
    status, found = covered.run()
    expect("a region that covers more, with its reason in the message, is no error, and its row gives a warning",
           status == 0 and not errors_of(found) and any(admitted[:9] in item.message and history.OPTED_OUT
                                                        in item.message for item in standing_of(found)))
    kind_quarantine_cases(expect, scratch)
    reason_quarantine_cases(expect, scratch)
    move_quarantine_cases(expect, scratch)


def reason_quarantine_cases(expect: Expect, scratch: Path) -> None:
    """Raise a count of the kind region or opted-out with no reason of its region in the message (THE ADMITTED COUNTS)."""
    span = ("test/a.cpp", "std::span")
    counted = (rows_text({span: 2}).replace("| std_object |", "| std-object |")
               + "test/a.cpp | opted-out | 1 | PROBE: a raw probe \\| std-object std::map | -\n"
               + "test/a.cpp | region | 1 | PROBE: a raw probe | -\n")
    bare = Scratch(scratch / "quarantine-region-reason")
    bare.write(QUARANTINE_LEDGER, counted)
    bare.commit("Count each region", "The first counts.")
    bare.write(QUARANTINE_LEDGER, counted.replace("| region | 1 |", "| region | 2 |"))
    unreasoned = bare.commit("Admit a second region", "A probe needs a second region.")
    status, found = bare.run()
    expect("a new region with no reason in the message is an error of the kind region-reason that names the commit",
           status == 1 and any(item.kind == history.REASON_KIND and item.entity == unreasoned[:9]
                               and "'PROBE: a raw probe'" in item.message for item in errors_of(found)))
    grown = Scratch(scratch / "quarantine-opted-out-reason")
    grown.write(QUARANTINE_LEDGER, counted)
    grown.commit("Count each region", "The first counts.")
    grown.write(QUARANTINE_LEDGER, counted.replace("| opted-out | 1 |", "| opted-out | 2 |"))
    covering = grown.commit("Admit a region that covers more", "The class PROBE: and a reason of a different region.")
    status, found = grown.run()
    expect("a region that covers more with only its class in the message is an error of the kind region-reason",
           status == 1 and any(item.kind == history.REASON_KIND and item.entity == covering[:9]
                               for item in errors_of(found)))
    plugin = Scratch(scratch / "quarantine-region-plugin")
    plugin.write(QUARANTINE_LEDGER, counted)
    plugin.commit("Count each region", "The first counts.")
    plugin.write("utils/tools/quarantine/plugin.cpp", "// a new kind in a region\n")
    plugin.write(QUARANTINE_LEDGER, counted.replace("| opted-out | 1 |", "| opted-out | 2 |"))
    plugin.commit("Report a new kind in the plugin", "The plugin finds one more object in the region.")
    status, found = plugin.run()
    expect("a region count that a change of the plugin raises needs no reason in the message",
           status == 0 and not errors_of(found))


def kind_quarantine_cases(expect: Expect, scratch: Path) -> None:
    """Change the kind names, count the regions and change the rule table, and check each verdict."""
    span = ("test/a.cpp", "std::span")
    named = Scratch(scratch / "quarantine-kinds")
    named.write(QUARANTINE_LEDGER, rows_text({span: 2}))
    named.commit("Count each entity", "The first counts.")
    hyphens = rows_text({span: 2}).replace("| std_object |", "| std-object |")
    named.write(QUARANTINE_LEDGER, hyphens)
    named.commit("Name the kinds with hyphens", "The report of the plugin has format 2.")
    status, found = named.run()
    expect("a change of the kind names with the same counts raises no count",
           status == 0 and not errors_of(found) and not standing_of(found))
    regions = hyphens + "test/a.cpp | region | 1 | PROBE: a region | -\n"
    named.write(QUARANTINE_LEDGER, regions)
    named.commit("Count the regions", "The ledger counts each region.")
    status, found = named.run()
    expect("the first count of the kind region raises no count",
           status == 0 and not errors_of(found) and not standing_of(found))
    two_regions = regions.replace("| region | 1 |", "| region | 2 |")
    named.write(QUARANTINE_LEDGER, two_regions)
    added = named.commit("Admit a second region", "A probe needs a second region:\nPROBE:   a\nregion.")
    status, found = named.run()
    expect("a new region with its reason in the message, also over a line break, is no error, and gives a warning",
           status == 0 and not errors_of(found)
           and any(added[:9] in item.message and history.REGION in item.message for item in standing_of(found)))
    named.write(history.RULE_TABLE, "quarantine test/\n")
    named.write(QUARANTINE_LEDGER, two_regions.replace("| std-object | 2 |", "| std-object | 3 |"))
    tabled = named.commit("Admit one name less in the rule table", "The table refuses one more name.")
    status, found = named.run()
    expect("a raise with a change of the rule table is no error, and its row gives a warning that names the commit",
           status == 0 and not errors_of(found) and any(tabled[:9] in item.message for item in standing_of(found)))
    expect("each kind name of the plugin before format 2 reads as the name of this time",
           history.current_key("a.cpp", "replace_pending:std::bit_cast", "x")
           == ("a.cpp", "replace-pending:std::bit_cast", "x")
           and history.current_key("a.cpp", "opted_out", "ORACLE: a probe | c_library_call memcpy")
           == ("a.cpp", "opted-out", "ORACLE: a probe | c-library-call memcpy")
           and history.current_key("a.cpp", "std-entity", "std::move") == ("a.cpp", "std-entity", "std::move"))
    expect("an include entity of an earlier version reads as the header or the target with no layer",
           history.current_key("a.h", "layer_header", "<vector> in the layer X1_wrappers")
           == ("a.h", "layer-header", "<vector>")
           and history.current_key("a.h", "upward-include", "include/crucible/X.h (no layer) from the layer X3_os")
           == ("a.h", "upward-include", "include/crucible/X.h")
           and history.current_key("a.h", "opted_out", "C-HEADER: a struct | layer_header <span> in the layer X1")
           == ("a.h", "opted-out", "C-HEADER: a struct | layer-header <span>")
           and history.current_key("a.h", "door-header", "<pthread.h> has the door a.cpp")
           == ("a.h", "door-header", "<pthread.h> has the door a.cpp"))
    included = Scratch(scratch / "quarantine-include-keys")
    long_keys = ("# a planted ledger\nformat 1\nrule quarantine\nconfiguration default machine x86_64\n"
                 "include/fixy/A.h | layer-header | 1 | <vector> in the layer X1_wrappers | -\n"
                 "include/fixy/A.h | upward-include | 1 | include/crucible/X.h (no layer) from the layer X1_wrappers"
                 " | -\n")
    included.write(QUARANTINE_LEDGER, long_keys)
    included.commit("Count the includes with their layers", "The first counts.")
    included.write(QUARANTINE_LEDGER, long_keys.replace(" in the layer X1_wrappers", "")
                   .replace(" (no layer) from the layer X1_wrappers", ""))
    included.commit("Key each include with no layer", "The guard code changes the key.")
    status, found = included.run()
    expect("a change of the include keys with the same counts raises no count and gives no warning",
           status == 0 and not errors_of(found) and not standing_of(found))
    included.write(QUARANTINE_LEDGER, long_keys.replace(" in the layer X1_wrappers", "")
                   .replace(" (no layer) from the layer X1_wrappers", "").replace("| layer-header | 1 |",
                                                                                   "| layer-header | 2 |"))
    raised = included.commit("Raise an include count by hand", "A second include comes.")
    status, found = included.run()
    expect("a raise of an include count after the change of the key stays an error",
           status == 1 and any(raised[:9] in item.message for item in errors_of(found)))


def move_quarantine_cases(expect: Expect, scratch: Path) -> None:
    """Move the quarantine ledger from its earlier file to the rows of assay.core.ledger, and check each verdict."""
    earlier = history.EARLIER_QUARANTINE_LEDGER
    span, vector = ("test/a.cpp", "std::span"), ("test/a.cpp", "std::vector")
    moved = Scratch(scratch / "quarantine-move")
    moved.write(earlier, entity_text({span: 2}, 4))
    moved.commit("Count each entity", "The first counts.")
    moved.write(earlier, entity_text({span: 3}, 4))
    raised = moved.commit("Raise a count by hand", "A finding comes back.")
    (moved.root / earlier).unlink()
    moved.write(QUARANTINE_LEDGER, rows_text({span: 3}))
    status, found = moved.run()
    expect("a working tree that moves the ledger with the same counts raises nothing more",
           status == 1 and len(errors_of(found)) == 1 and raised[:9] in errors_of(found)[0].message
           and not any(history.WORKING_TREE in item.message for item in found))
    moved.commit("Move the ledger to the rows", "Each count stays.")
    status, found = moved.run()
    expect("a raise in the earlier file stays an error after the move, and its warning points at the new row",
           status == 1 and len(errors_of(found)) == 1 and raised[:9] in errors_of(found)[0].message
           and [item.line for item in standing_of(found)] == [5] and raised[:9] in standing_of(found)[0].message)
    moved.write(QUARANTINE_LEDGER, rows_text({span: 3}, {"arm": {span: 1}}))
    moved.commit("Add the configuration of a CI leg", "The leg counts one more finding.")
    moved.write(QUARANTINE_LEDGER, rows_text({span: 3, vector: 1}, {"arm": {span: 1}}))
    added = moved.commit("Add a finding by hand", "A new entity comes.")
    status, found = moved.run()
    expect("a raise in the rows is an error that names the new entity",
           status == 1 and any(added[:9] in item.message and "std-object std::vector in default from 0 to 1"
                               in item.message for item in errors_of(found)))

    jump = Scratch(scratch / "quarantine-move-raise")
    jump.write(earlier, entity_text({span: 2}, 4))
    jump.commit("Count each entity", "The first counts.")
    (jump.root / earlier).unlink()
    jump.write(QUARANTINE_LEDGER, rows_text({span: 3}))
    hidden = jump.commit("Move the ledger and raise a count", "A move must not hide the raise.")
    status, found = jump.run()
    expect("a move to the rows that raises a count is an error that names the commit",
           status == 1 and any(hidden[:9] in item.message and "from 2 to 3" in item.message
                               for item in errors_of(found)))


def applicability_cases(expect: Expect, tree: Scratch, scratch: Path) -> None:
    """Check that the rule does not apply to a shallow history or to a tree that is not a git work tree."""
    shallow = scratch / "shallow"
    git.run(scratch, "clone", "-q", "--depth", "1", f"file://{tree.root}", str(shallow))
    try:
        history.findings(MetaContext(shallow))
        is_refused = False
    except NotApplicable as reason:
        is_refused = "shallow" in str(reason)
    expect("the rule does not apply to a shallow history", is_refused)
    plain = scratch / "plain"
    plain.mkdir()
    expect("the rule does not apply to a tree that is not a git work tree", verdict(plain)[0] == SKIPPED)


def main(argv: list[str]) -> int:
    """Run each case, and print one line for each.

    Returns:
        0 when each case holds, else 1
    """
    if argv:
        print("usage: guard selftest tests.guard.test_history", file=sys.stderr)
        return 2
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    measurement_cases(expect)
    with tempfile.TemporaryDirectory(prefix="test-history-") as scratch_name:
        scratch = Path(scratch_name)
        tree = threshold_cases(expect, scratch)
        ledger_cases(expect, tree)
        move_cases(expect, scratch)
        rebase_cases(expect, scratch)
        scope_cases(expect, scratch)
        quarantine_cases(expect, scratch)
        applicability_cases(expect, tree, scratch)
    if failures:
        print(f"tests.guard.test_history: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("tests.guard.test_history: every case holds.")
    return 0
