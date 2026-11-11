"""The test of the run and the write of a rule of the engine elf with one set of rows (assay.guard.engines.elf.one_set).

`guard selftest tests.guard.engines.elf.test_one_set` runs the test.  Each
case writes one line.  Exit 0 when each case holds, else 1.

The cases hold the refused counts of a write, the problems of a ledger and
the counts of the written set.  They also hold the run of an inspection, the
writes that stop before the ledger, and a write that keeps each row of the
set.  The tests of the rules artifact-symbols and artifact-links hold the
write on planted builds.
"""

from pathlib import Path
from tempfile import TemporaryDirectory

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import testing
from assay.guard.engines.elf import one_set, runner
from assay.tests.guard.engines.elf.test_runner import quiet

RULE = "planted-one-set"
REGISTRY_ROWS = (f"rule {RULE} engine=elf scope=builddir ledger=count doc=XII | a planted rule | lower it",)
HEAD = f"# a planted ledger\nformat {rule_ledger.FORMAT}\nrule {RULE}\n"
CONFIGURATION = "configuration k kind k\nconfiguration k tier t\n"


def planted_rule() -> rule_registry.Rule:
    """Return the rule of the planted registry."""
    return rule_registry.planted_rules(REGISTRY_ROWS)[RULE]


def parsed(text: str) -> rule_ledger.Ledger:
    """Read a planted ledger with the policy of the planted rule."""
    rule = planted_rule()
    return rule_ledger.parse(HEAD + text, "planted.txt", rule.id, rule.policy)


def key(path: str, kind: str, entity: str) -> rule_ledger.Key:
    """Return the key of one count row."""
    return rule_ledger.Key(path, kind, entity)


def is_plain_row(row: rule_ledger.Key, entry: rule_ledger.Entry) -> bool:
    """Accept a row of the kind fact or count with a count, and a row of the kind site with the value admit."""
    if row.kind == "site":
        return entry.value == rule_ledger.ADMIT_VALUE
    return row.kind in ("fact", "count") and entry.value != rule_ledger.ADMIT_VALUE


def problems(text: str) -> list[str]:
    """Return the messages of the problems of a planted ledger.  A row of the kind fact has the count 1."""
    found = one_set.ledger_problems(RULE, parsed(text), is_plain_row, "a planted shape", lambda row: row.kind == "fact")
    return [item.message for item in found]


def risen_cases(expect: testing.Expect) -> None:
    """Hold the lines of risen_rows(): a new row and a risen count, and no line for a held count or a different kind."""
    inspection = one_set.Inspection(
        (),
        rule_ledger.empty(planted_rule()),
        counts={
            key("@build/a", "fact", "new"): 1,
            key("@build/a", "fact", "risen"): 3,
            key("@build/a", "fact", "held"): 1,
            key("@build/a", "other", "new"): 1,
        },
    )
    held = {key("@build/a", "fact", "risen"): 2, key("@build/a", "fact", "held"): 1}
    expect(
        "risen_rows gives a line for a new row and a risen count of the kinds, and nothing for a held count",
        one_set.risen_rows(inspection, held, ("fact",))
        == ["@build/a gets the row fact new", "@build/a has 3 of fact risen, and the ledger holds 2"],
    )


def problem_cases(expect: testing.Expect) -> None:
    """Hold the problems of a ledger: a configuration row, a row shape and a count of 2 of a single kind."""
    good = "@build/a | fact | 1 | x | -\n@build/a | count | 5 | y | -\n@build/a | site | admit | z | a reason\n"
    expect("a set of good rows has no problem", problems(good) == [])
    expect(
        "a configuration row is a problem",
        any("no configuration row" in item for item in problems(f"{CONFIGURATION}@build/a | fact | 1 | x | -\n")),
    )
    expect(
        "a row that the rule refuses gives the shape", problems("@build/a | other | 1 | x | -\n") == ["a planted shape"]
    )
    expect(
        "a count of 2 is a problem of a kind with the count 1",
        any("the count 2" in item for item in problems("@build/a | fact | 2 | x | -\n")),
    )


def count_cases(expect: testing.Expect) -> None:
    """Hold the counts of the written set: each row stays, and each count is the larger of its row and the build."""
    ledger = parsed(
        "@build/a | fact | 1 | other-build | -\n@build/a | count | 5 | higher-row | -\n"
        "@build/a | count | 2 | lower-row | -\n@build/a | site | admit | z | a reason\n"
    )
    counts = {
        key("@build/a", "fact", "new"): 1,
        key("@build/a", "count", "higher-row"): 3,
        key("@build/a", "count", "lower-row"): 4,
    }
    expect(
        "the set keeps a row that the build does not give, adds a new row, keeps each admit row, and takes the larger "
        "count",
        one_set.set_counts(ledger, counts)
        == {
            key("@build/a", "fact", "other-build"): 1,
            key("@build/a", "fact", "new"): 1,
            key("@build/a", "count", "higher-row"): 5,
            key("@build/a", "count", "lower-row"): 4,
            key("@build/a", "site", "z"): 1,
        },
    )


def run_cases(expect: testing.Expect) -> None:
    """Hold the run of an inspection: a rule that does not apply, and an inspection with an error and a summary."""

    def not_applicable() -> one_set.Inspection:
        """Raise the reason of a build that the rule does not read."""
        raise runner.NotApplicable("a planted reason")

    status, text = quiet(lambda: one_set.run_inspection(RULE, not_applicable, None))
    expect("a rule that does not apply exits 77, and names its reason", status == runner.SKIPPED and "planted" in text)
    unread = check_report.Finding("error", "ledger", 0, RULE, "the ledger cannot be read")
    inspection = one_set.Inspection((unread,), rule_ledger.empty(planted_rule()), summary="a planted summary")
    status, text = quiet(lambda: one_set.run_inspection(RULE, lambda: inspection, None))
    expect(
        "an inspection with an error exits 1, and writes its findings and its summary",
        status == 1 and "cannot be read" in text and "a planted summary" in text,
    )


def stop_cases(expect: testing.Expect, root: Path, policy: one_set.WritePolicy) -> None:
    """Hold the writes that stop: the mode admit, --import, --add-configuration, a build that fails, an unadmitted error.

    Each write goes through the function that writer() gives.  The cases then also hold writer().
    """
    rule = planted_rule()

    def written(request: runner.WriteRequest, error: Exception) -> tuple[int, str]:
        """Write with an inspection that raises the error, and return the status and the text."""

        def examine(_rule: rule_registry.Rule, _context: runner.ElfContext) -> one_set.Inspection:
            """Raise the planted error."""
            raise error

        return quiet(lambda: one_set.writer(examine, policy)(rule, request))

    unused = runner.BuildError("unused")
    status, text = written(runner.WriteRequest(root, root, rule_ledger.WriteMode.ADMIT), unused)
    expect("the mode admit does not apply", status == one_set.USAGE_STATUS and "admit" in text)
    status, text = written(runner.WriteRequest(root, root, import_log=root / "ci.log"), unused)
    expect("--import does not apply", status == one_set.USAGE_STATUS and "no configuration" in text)
    status, text = written(runner.WriteRequest(root, root, add_configuration=True), unused)
    expect("--add-configuration does not apply", status == one_set.USAGE_STATUS and "no configuration" in text)
    status, _ = written(runner.WriteRequest(root, root), runner.NotApplicable("planted"))
    expect("a build that the rule does not read writes nothing and exits 77", status == runner.SKIPPED)
    status, text = written(runner.WriteRequest(root, root), runner.BuildError("a planted failure"))
    expect("a build that the rule cannot read writes nothing", status == 1 and "a planted failure" in text)
    unadmitted = check_report.Finding("error", "build/tool", 0, RULE, "a planted error that no row admits")
    inspection = one_set.Inspection(
        (unadmitted,), rule_ledger.empty(rule), counts={key("@build/a", "fact", "x"): 1}, unadmitted=(unadmitted,)
    )
    request = runner.WriteRequest(root, root, rule_ledger.WriteMode.RAISE, reason="a planted reason")
    status, text = quiet(lambda: one_set.writer(lambda _rule, _context: inspection, policy)(rule, request))
    expect(
        "a build with an error that no row admits writes nothing, also with --raise",
        status == 1 and "no row admits" in text and not rule_ledger.ledger_path(RULE, root).exists(),
    )


def write_cases(expect: testing.Expect) -> None:
    """Hold the writes that stop, a refused count, a write with --raise, and a write that keeps each row of the set."""
    rule = planted_rule()
    policy = one_set.WritePolicy(lambda _ledger: [], ("count",), ("a reason",), "admit", "rows")
    with TemporaryDirectory(prefix="elf-one-set-") as scratch_text:
        root = Path(scratch_text)
        stop_cases(expect, root, policy)
        ledger_file = rule_ledger.ledger_path(RULE, root)
        ledger_file.parent.mkdir(parents=True)
        before = (
            f"{HEAD}@build/a | fact | 1 | other-build | -\n@build/a | count | 2 | helper | -\n"
            "@build/a | site | admit | z | a reason\n"
        )
        ledger_file.write_text(before, encoding="utf-8")

        def write(counts: dict[rule_ledger.Key, int], mode: rule_ledger.WriteMode) -> tuple[int, str]:
            """Write the counts of a planted build into the ledger of the tree, and return the status and the text."""

            def examine(_rule: rule_registry.Rule, _context: runner.ElfContext) -> one_set.Inspection:
                """Give an inspection of the counts against the ledger of the tree."""
                return one_set.Inspection((), rule_ledger.read(rule, root), counts)

            request = runner.WriteRequest(root, root, mode, reason="a planted reason")
            return quiet(lambda: one_set.writer(examine, policy)(rule, request))

        status, text = write({key("@build/a", "count", "helper"): 3}, rule_ledger.WriteMode.LOWER)
        expect(
            "a write refuses a count of a refused kind above its row with no --raise, and writes nothing",
            status == 1
            and "@build/a has 3 of count helper, and the ledger holds 2" in text
            and ledger_file.read_text(encoding="utf-8") == before,
        )
        status, _ = write(
            {key("@build/a", "count", "helper"): 3, key("@build/a", "fact", "new"): 1}, rule_ledger.WriteMode.RAISE
        )
        written = rule_ledger.read(rule, root)
        expect(
            "a write with --raise raises the count, adds the new row with the reason, and keeps each other row",
            status == 0
            and written.expected()
            == {
                key("@build/a", "fact", "other-build"): 1,
                key("@build/a", "fact", "new"): 1,
                key("@build/a", "count", "helper"): 3,
            }
            and written.base[key("@build/a", "fact", "new")].reason == "a planted reason"
            and key("@build/a", "site", "z") in written.admits()
            and not written.configurations,
        )
        status, _ = write({key("@build/a", "fact", "new"): 1}, rule_ledger.WriteMode.LOWER)
        expect(
            "a write of a build with fewer facts and a lower count keeps each row and each count",
            status == 0 and rule_ledger.read(rule, root).expected() == written.expected(),
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."), argv, (risen_cases, problem_cases, count_cases, run_cases, write_cases)
    )
