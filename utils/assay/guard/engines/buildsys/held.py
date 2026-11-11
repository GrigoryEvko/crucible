"""The witness of a measured rule with a holder: the judge of the holder on the planted build of each plant.

THE HOLDERS
    A measured rule with the field held-by has no run of its own
    (assay.core.registry).  Its holder judges it while a step runs, or when a
    command reads a build.  The witness of such a rule runs the judge of the
    holder on the planted build of each plant and control of
    utils/plants/RULE/ (assay.guard.engines.buildsys.planted).  The judge reads
    the planted input with the reader of the holder, and the thresholds of the
    registry of the copy of the plant (BuildContext.budget()):
      loop-gauge      judged_record() of assay.loop.gauge for each record of
                      the file GAUGE_RECORDS of the build
      loop-test-step  judge() of assay.loop.teststep for the record of each
                      test of the build, with the ledgers of the copy
      loop-fixture    cost_findings() and own_reason_findings() of
                      assay.loop.fixture.store for each fixture of the build
      a rule ID       the findings of that rule on the plant
    The field holder of a held rule names the function of its holder that
    judges it.  The rule rule-implementations makes sure that the judge of the
    holder in HOLDERS calls that function.  For a holder that is a rule ID,
    the function must be in the module of that rule.

    The witness keeps each finding of the held rule.  When the holder is a
    rule, the witness keeps each finding of the holder.  The plants of the
    held rule then hold only the items that the thresholds of the held rule
    judge.

THE RECORDS OF A FIXTURE
    The census (assay.readers.census) gives the CPU time and the instruction
    count of the record of a fixture.  It does not give the user time and the
    system time apart, and the witness gives their sum as the user time.  The
    judge reads only the instruction count.  The driver writes the log of
    each compile of a fixture to BUILD/neg-compile/NAME/NAME.sarif.  The
    regexes of the fixture are the words of its test command.

THE WRITE
    A held rule of the policy max has no module of its own.  `guard write`
    then calls written() of this module.  WRITERS gives the function that writes
    the ledger of each such rule.  The function of test-memory and
    test-instructions is written() of assay.guard.engines.buildsys.test_time.
    That function writes the ledgers of the rules of the test launcher from
    the records of the tests.  The function of
    fixture-instructions is fixture_written(), which writes its ledger from
    the records of the fixtures of the build.  A fixture gets a row when its
    instruction count is over the error threshold.  The write refuses a
    build with a fixture that has no record.  A held rule with no row in
    WRITERS has no write, and `guard write` refuses it.

THE FINDINGS
    A measured rule with two equal thresholds gives a warning only.  The
    witness of a held rule then counts each finding of a plant, also a
    warning (assay.guard.engines.meta.runner, THE WITNESS).  A plant must give
    a finding, and a control must give no finding.  An input of a plant that
    the holder cannot read makes the plant fail, with the reason.

`guard selftest tests.guard.engines.buildsys.test_held` does a test of the module.

Complexity: one read of the planted inputs of the holder for each plant.
"""

import contextlib
import io
import sys
from collections.abc import Callable
from dataclasses import replace
from pathlib import Path

from assay.core import budget as cost_budget
from assay.core import ledger as rule_ledger
from assay.core import meter as cost_meter
from assay.core import record as cost_record
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core.paths import REPO_ROOT
from assay.guard.engines.buildsys import planted, test_time
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import census as build_census
from assay.readers import ctest as ctest_reader

# The file of the planted records of the gauge in a planted build, one record of `loop gauge --record` on each line.
GAUGE_RECORDS = "gauge-records.jsonl"
# The rule of the fixture driver that judges the errors that no regex of a fixture matches.
OWN_REASON = "fixture-own-reason"
# The measured rule of the fixture driver (THE WRITE).  The reason of a new row of its ledger when the write gives
# none, and the instructions in one G, follow.
FIXTURE_INSTRUCTIONS = "fixture-instructions"
FIXTURE_REASON = "the measure of one cold fixture compile"
GIGA = 1e9

type Judge = Callable[[rule_registry.Rule, BuildContext], list[check_report.Finding]]


def unreadable(what: str, problem: object) -> meta_runner.NotApplicable:
    """Return the reason of a plant with an input that the holder cannot read (THE FINDINGS)."""
    return meta_runner.NotApplicable(f"the holder cannot read {what}: {problem}")


def gauge_findings(rule: rule_registry.Rule, context: BuildContext) -> list[check_report.Finding]:
    """Return the findings of each planted record of the gauge, as `loop gauge` judges its own record.

    Raises:
        NotApplicable: If the plant has no build directory, or its records do not read
    """
    from assay.loop import gauge

    path = context.require_build() / GAUGE_RECORDS
    budgets = context.budgets()
    try:
        records = gauge.read_records(path)
        return [item for record in records for item in gauge.judged_record(record, budgets, context.shown(path))[0]]
    except gauge.GaugeError as problem:
        raise unreadable("the planted records of the gauge", problem) from None


def test_step_findings(rule: rule_registry.Rule, context: BuildContext) -> list[check_report.Finding]:
    """Return the findings of the record of each test of the planted build, as `loop test-step` judges a test.

    Complexity: linear in the records of the tests.

    Raises:
        NotApplicable: If the plant has no build directory or no build kind, or a record does not read
    """
    from assay.loop import teststep

    build = context.require_build()
    kind = context.kind()
    configuration = cost_meter.read_configuration(str(build))
    budget = {row: (context.budget(row).warn, context.budget(row).error) for row in teststep.ROWS}
    ledgers = str(rule_ledger.ledger_path(rule.id, context.root).parent)
    try:
        records = ctest_reader.costs(build)
    except ctest_reader.CtestError as problem:
        raise unreadable("the records of the tests", problem) from None
    found: list[check_report.Finding] = []
    for name, record in sorted(records.items()):
        cost = record.cost
        run = cost_meter.Measurement(
            record.exit,
            cost.user_s,
            cost.system_s,
            cost.wall_s,
            cost.peak_rss_kb,
            cost.load,
            cost.end,
            None,
            cost.test_instructions,
        )
        place = context.shown(cost_record.test_record_file(build, name))
        for level, row, message in teststep.judge(name, run, budget, kind, ledgers, configuration, walls=record.walls):
            shown_level: check_report.Level = "error" if level == "error" else "warning"
            found.append(check_report.Finding(shown_level, place, 0, row, message))
    return found


def fixture_tests(build: Path) -> list[tuple[str, Path, list[str]]]:
    """Return the name, the source and the regexes of each fixture test of a build, in the sequence of the test list.

    Complexity: linear in the tests of the build.

    Raises:
        ctest_reader.CtestError: If the tests of the build do not read
    """
    from assay.loop.fixture import store

    found: list[tuple[str, Path, list[str]]] = []
    for test in ctest_reader.tests(build):
        words = build_census.fixture_words(list(test.command))
        parsed = None if words is None else store.fixture_arguments(words)
        if parsed is not None:
            arguments = parsed[1]
            found.append((arguments[2], Path(arguments[1]), list(arguments[3:])))
    return found


def fixture_findings(rule: rule_registry.Rule, context: BuildContext) -> list[check_report.Finding]:
    """Return the findings of the cost and of the errors of each fixture of the planted build, as `loop fixture`.

    THE RECORDS OF A FIXTURE gives the inputs.  A fixture with no record has
    no cost to judge, and a fixture with no log has no errors to judge.
    Complexity: linear in the tests of the build and in the records of the fixtures.

    Raises:
        NotApplicable: If the plant has no build directory, its tests do not read, or a record does not read
    """
    from assay.loop.fixture import store

    build = context.require_build()
    registry = context.root / rule_registry.REGISTRY_PATH
    ledgers = rule_ledger.ledger_path(rule.id, context.root).parent
    budget = store.cost_budget_at(registry if registry.is_file() else rule_registry.REGISTRY, ledgers)
    try:
        tests = fixture_tests(build)
    except ctest_reader.CtestError as problem:
        raise unreadable("the tests of the build", problem) from None
    found: list[object] = []
    for name, source, regexes in tests:
        unit, _, reason = build_census.read_fixture(build, name, str(source), context.root, False)
        if unit is not None:
            result = store.CompileResult(1, "", unit.cost.cpu_s or 0.0, 0.0, unit.cost.instructions)
            for checked in store.cost_findings(name, source, result, False, budget).values():
                found += checked
        elif reason != "missing":
            raise unreadable(f"the record of the fixture {name}", reason)
        log, _ = store.read_log(build / cost_record.FIXTURE_DIRECTORY / name / f"{name}.sarif")
        if log is not None:
            # evaluate() prints the output of the compile and its count of errors for the reader of a test.  The
            # witness prints only its verdicts.
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                compiled = store.CompileResult(1, "", 0.0, 0.0, None, None, log)
                _, unmatched = store.evaluate(name, source, regexes, compiled)
            if unmatched is not None:
                path = rule_ledger.ledger_path(OWN_REASON, context.root)
                found += store.own_reason_findings(name, source, unmatched, False, path)
    return [item for item in found if isinstance(item, check_report.Finding)]


HOLDERS: dict[str, Judge] = {
    "loop-gauge": gauge_findings,
    "loop-test-step": test_step_findings,
    "loop-fixture": fixture_findings,
}


def fixture_written(context: BuildContext, ledger: rule_ledger.Ledger, reason: str | None) -> rule_ledger.Ledger:
    """Return the ledger of fixture-instructions written again from the records of the build (THE WRITE).

    A fixture with an instruction count over the error threshold gets the row `- | - | VALUE | FIXTURE | REASON`,
    with VALUE in G.  A row keeps its fixture over the warning threshold, and its value becomes the count, rounded
    up.  The row keeps its earlier reason, else it gets REASON or FIXTURE_REASON.  Each other row goes, also the row
    of a fixture with no count.  Complexity: linear in the tests of the build and in the records of the fixtures.

    Raises:
        NotApplicable: If the call gives no build directory
        rule_ledger.LedgerError: If the ledger is not the ledger of fixture-instructions, the tests do not read, or a
            fixture has no record that counts
    """
    if ledger.rule != FIXTURE_INSTRUCTIONS:
        raise rule_ledger.LedgerError(f"the ledger of {ledger.rule} is not the ledger of {FIXTURE_INSTRUCTIONS}")
    build = context.require_build()
    budget = context.budget(ledger.rule)
    try:
        tests = fixture_tests(build)
    except ctest_reader.CtestError as problem:
        raise rule_ledger.LedgerError(f"the tests of the build do not read: {problem}") from None
    base: dict[rule_ledger.Key, rule_ledger.Entry] = {}
    unread: list[str] = []
    for name, _, _ in tests:
        try:
            record = cost_record.read_fixture(Path(cost_record.fixture_record_file(build, name)))
        except cost_record.MissingRecord:
            unread.append(f"{name} (no record)")
            continue
        except cost_record.RecordError as problem:
            unread.append(f"{name} ({problem})")
            continue
        key = rule_ledger.Key("", "", name)
        earlier = ledger.base.get(key)
        # A row keeps a fixture over the warning threshold.  A fixture with no row gets one over the error threshold.
        limit = budget.warn if earlier is not None else budget.error
        measure = None if record.instructions is None else record.instructions / GIGA
        if measure is None or measure <= limit:
            continue
        base[key] = rule_ledger.Entry(
            key, cost_budget.kept_value(measure), earlier.reason if earlier is not None else reason or FIXTURE_REASON
        )
    if unread:
        raise rule_ledger.LedgerError(
            f"{len(unread)} fixtures have no record that counts, such as {unread[0]}.  Run each fixture of the build, "
            f"then the write"
        )
    return replace(ledger, base=base)


type Writer = Callable[[BuildContext, rule_ledger.Ledger, str | None], rule_ledger.Ledger]

# The function that writes the ledger of each held rule of the policy max (THE WRITE).
WRITERS: dict[str, Writer] = {
    "test-memory": test_time.written,
    "test-instructions": test_time.written,
    FIXTURE_INSTRUCTIONS: fixture_written,
}


def written(
    rule: rule_registry.Rule, context: meta_runner.RuleContext, ledger: rule_ledger.Ledger, reason: str | None
) -> rule_ledger.Ledger:
    """Return the new ledger of a held rule from the function of WRITERS (THE WRITE).

    Raises:
        rule_ledger.LedgerError: If WRITERS has no function of the rule, or the context is not the context of a build
        NotApplicable: If the build of the context gives no input of the write
    """
    writer = WRITERS.get(rule.id)
    if writer is None:
        raise rule_ledger.LedgerError(
            f"the rule {rule.id} has the holder {rule.held_by}, and no function writes its ledger.  Add the function "
            f"to WRITERS of assay.guard.engines.buildsys.held"
        )
    if not isinstance(context, BuildContext):
        raise rule_ledger.LedgerError(f"the rule {rule.id} reads a build, and the context of the write has no build")
    return writer(context, ledger, reason)


def held_findings(rule: rule_registry.Rule, context: meta_runner.RuleContext) -> list[check_report.Finding]:
    """Return the findings that the holder of a rule gives on the planted build of one plant (THE HOLDERS).

    Raises:
        NotApplicable: If the rule has no holder, the witness has no judge of the holder, or an input does not read
    """
    holder = rule.held_by
    if holder is None or not isinstance(context, BuildContext):
        raise meta_runner.NotApplicable(f"the rule {rule.id} has no holder, or its plant has no planted build")
    if holder not in rule_registry.HOLDING_COMMANDS:
        holding = rule_registry.load().rules.get(holder)
        if holding is None:
            raise meta_runner.NotApplicable(f"the holder {holder} of {rule.id} is no rule of the registry")
        return meta_runner.module_judged(holding, context)
    judge = HOLDERS.get(holder)
    if judge is None:
        raise meta_runner.NotApplicable(f"the witness has no judge of the holder {holder} of {rule.id}")
    return [item for item in judge(rule, context) if item.check == rule.id]


def witness(rule: rule_registry.Rule, root: Path = REPO_ROOT) -> int:
    """Run the judge of the holder of a rule on each of its plants and controls (THE FINDINGS).

    Returns:
        The status of assay.guard.engines.meta.runner.witness()
    """
    return meta_runner.witness(
        rule, root, planted.plant_context, held_findings, module=sys.modules[__name__], counts_warnings=True
    )
