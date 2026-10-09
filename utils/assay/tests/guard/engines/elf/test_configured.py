"""The test of the run and the write of a rule of the engine elf with configurations (assay.guard.engines.elf.configured).

`guard selftest tests.guard.engines.elf.test_configured` runs the test.  Each
case writes one line.  Exit 0 when each case holds, else 1.

The cases hold the refused counts of a write and the problems of a ledger.
They also hold the run of an inspection, and the writes that stop before the
ledger.  The tests of the rules artifact-symbols and artifact-links hold the
full write on planted builds.
"""

from pathlib import Path
from tempfile import TemporaryDirectory

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import testing
from assay.guard.engines.elf import configured, runner
from assay.tests.guard.engines.elf.test_runner import quiet

RULE = "planted-configured"
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
    """Accept a row of the kind fact with a count."""
    return row.kind == "fact" and entry.value != rule_ledger.ADMIT_VALUE


def risen_cases(expect: testing.Expect) -> None:
    """Hold the lines of risen_rows(): a new row and a risen count, and no line for a held count or a different kind."""
    inspection = configured.Inspection(
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
        configured.risen_rows(inspection, held, ("fact",))
        == ["@build/a gets the row fact new", "@build/a has 3 of fact risen, and the ledger holds 2"],
    )


def problem_cases(expect: testing.Expect) -> None:
    """Hold the problems of a ledger: a configuration shape, a row shape, no configuration and a count of 2."""

    def problems(text: str) -> list[str]:
        """Return the messages of the problems of a planted ledger."""
        found = configured.ledger_problems(RULE, parsed(text), is_plain_row, "a planted shape", lambda _key: True)
        return [item.message for item in found]

    expect("a good ledger has no problem", problems(f"{CONFIGURATION}@build/a | fact | 1 | x | -\n") == [])
    expect(
        "a configuration with no tier is a problem",
        any("configuration KIND tier TIER" in item for item in problems("configuration k kind k\n")),
    )
    expect(
        "a row that the rule refuses gives the shape",
        problems(f"{CONFIGURATION}@build/a | other | 1 | x | -\n") == ["a planted shape"],
    )
    expect(
        "a count row with no configuration is a problem",
        any("no configuration row" in item for item in problems("@build/a | fact | 1 | x | -\n")),
    )
    expect(
        "a count of 2 is a problem of a kind with the count 0 or 1",
        any("the count 2" in item for item in problems(f"{CONFIGURATION}@build/a | fact | 2 | x | -\n")),
    )
    first = configured.first_problem(
        lambda ledger: configured.ledger_problems(RULE, ledger, is_plain_row, "a planted shape", lambda _key: True)
    )
    expect(
        "first_problem gives the first message, or an empty text",
        first(parsed(f"{CONFIGURATION}@build/a | other | 1 | x | -\n")) == "a planted shape"
        and first(parsed(CONFIGURATION)) == "",
    )


def run_cases(expect: testing.Expect) -> None:
    """Hold the run of an inspection: a rule that does not apply, and a ledger that the rule cannot read."""

    def not_applicable() -> configured.Inspection:
        """Raise the reason of a build that the rule does not read."""
        raise runner.NotApplicable("a planted reason")

    status, text = quiet(lambda: configured.run_inspection(RULE, not_applicable, None))
    expect("a rule that does not apply exits 77, and names its reason", status == runner.SKIPPED and "planted" in text)
    unread = check_report.Finding("error", "ledger", 0, RULE, "the ledger cannot be read")
    inspection = configured.Inspection((unread,), rule_ledger.empty(planted_rule()))
    status, text = quiet(lambda: configured.run_inspection(RULE, lambda: inspection, None))
    expect(
        "an inspection with no configuration writes its findings, and no summary",
        status == 1 and "cannot be read" in text,
    )


def write_cases(expect: testing.Expect) -> None:
    """Hold the writes that stop: the mode admit, a build that does not apply and a build that fails.

    Each write goes through the function that writer() gives.  The cases then also hold writer().
    """
    rule = planted_rule()
    policy = configured.WritePolicy(lambda _ledger: [], lambda _inspection, _held: [], ("a reason",), "admit", "rows")
    with TemporaryDirectory(prefix="elf-configured-") as scratch_text:
        root = Path(scratch_text)

        def written(mode: rule_ledger.WriteMode, error: Exception) -> tuple[int, str]:
            """Write with an inspection that raises the error, and return the status and the text."""

            def examine(_rule: rule_registry.Rule, _context: runner.ElfContext) -> configured.Inspection:
                """Raise the planted error."""
                raise error

            request = runner.WriteRequest(root, root, mode)
            return quiet(lambda: configured.writer(examine, policy)(rule, request))

        status, text = written(rule_ledger.WriteMode.ADMIT, runner.BuildError("unused"))
        expect("the mode admit does not apply", status == configured.ADMIT_STATUS and "admit" in text)
        status, _ = written(rule_ledger.WriteMode.LOWER, runner.NotApplicable("planted"))
        expect("a build that the rule does not read writes nothing and exits 77", status == runner.SKIPPED)
        status, text = written(rule_ledger.WriteMode.LOWER, runner.BuildError("a planted failure"))
        expect("a build that the rule cannot read writes nothing", status == 1 and "a planted failure" in text)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (risen_cases, problem_cases, run_cases, write_cases))
