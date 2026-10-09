"""test-time — each registered test runs behind the judge of its time and its memory, and each ledger row names a test.

THE JUDGE
    The test launcher `loop test-step` (assay.loop.teststep) and the fixture
    driver `loop fixture` judge each test while it runs.  They compare the
    wall time of the test with the measured rule test-time of
    utils/registry.txt.  They compare the peak memory of its largest process
    with the measured rule test-memory.  A test over an error threshold that
    no ledger row admits fails.  Each run writes the record of the test,
    BUILD_DIR/test-records/NAME.test.cost (assay.core.record, THE RECORDS).
    Each test writes its warnings to the file RULE.NAME.txt of the warnings
    directory.  The step meter judges with verdict() of assay.core.budget.
    It gives its own messages, because the import closure of a step meter
    does not hold assay.core.report (assay.core.budget, THE STEP METERS).

THE RULE
    A test cannot see a test that runs with no judge, or a ledger row that
    names a test of no build.  The rule reads the tests that the build
    registers and the records of their last runs (assay.readers.ctest).  It
    also reads the rows of the ledgers of test-time and test-memory for the
    kind of the build (assay.core.budget).  It refuses:
      unjudged  a test with no test launcher and no fixture driver in its
                command.  Its time and its memory have no judge.  CMake gives
                the launcher to a test of an executable target, and
                crucible_script_test() of cmake/TestLauncher.cmake gives it to
                a script test
      unknown   a ledger row of one of the two rules for the kind of the build
                that names a test that the build does not register
      input     the build has no kind, the test list or a record does not
                read, or a ledger row does not have its shape
    The entity of a finding is the name of the test.  The judge refuses a row
    of a test that runs at or below the error threshold of its rule.  The rule
    judges no measure itself.  Its findings go to test-time.txt of the
    warnings directory, apart from the files of the tests.

    A ledger row is '- | KIND | VALUE | TEST | REASON' (assay.core.meter, THE
    REGISTRY AND THE LEDGERS).  A ledger that is not there has no row.

THE WRITE
    `guard write --rule test-time --build-dir BUILD_DIR` writes the rows of
    the kind of the build again, from the records of the build directory.
    Each registered test with a last record over the error threshold of
    test-time gets a row with its wall time.  `guard write --rule test-memory`
    writes the ledger of test-memory in the same way, with the peak memory of
    each record in GB.  test-memory has a holder and no module of its own.
    The write then calls written() through assay.guard.engines.buildsys.held
    (THE WRITE of that module).  A row keeps its reason, and a new row takes
    the reason of --reason, or DEFAULT_REASON.  A row of a different kind
    stays.

Complexity: linear in the words of the registered tests, the records and the ledger rows.
"""

import itertools
import os
from collections.abc import Sequence
from dataclasses import replace

from assay.core import budget as cost_budget
from assay.core import ledger as rule_ledger
from assay.core import meter as cost_meter
from assay.core import record as cost_record
from assay.core import report as check_report
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import ctest as ctest_reader

RULE = "test-time"
MEMORY_RULE = "test-memory"
# The two rules with ledgers that name a test (THE RULE).
LEDGER_RULES = (RULE, MEMORY_RULE)
# The file name of the master loop, and the subcommands of the judge: the test launcher and the fixture driver.
MASTER = "loop"
JUDGES = frozenset({"test-step", "fixture"})
# The reason of a new row of a write with no --reason.
DEFAULT_REASON = "the measure of one ctest run"


def is_judged(command: Sequence[str]) -> bool:
    """Return True when a command starts the test launcher or the fixture driver: the master loop and one of JUDGES.

    Complexity: linear in the words of the command.
    """
    return any(
        os.path.basename(word) == MASTER and following in JUDGES for word, following in itertools.pairwise(command)
    )


def input_error(context: BuildContext, problem: object) -> check_report.Finding:
    """Return the error of an input of the rule that does not read."""
    build = context.require_build()
    return check_report.Finding(
        "error", context.shown(build), 0, RULE, f"the rule cannot read its input: {problem}", "input"
    )


def kind_of(context: BuildContext) -> str | check_report.Finding:
    """Return the build kind of the build directory, or the error of a build with no kind."""
    try:
        return context.kind()
    except meta_runner.NotApplicable as problem:
        return input_error(context, problem)


def ledger_rows(context: BuildContext, kind: str) -> tuple[dict[str, cost_budget.Rows], list[check_report.Finding]]:
    """Return the ledger rows of the kind of the build for each rule of LEDGER_RULES, and an error for each bad row."""
    found: dict[str, cost_budget.Rows] = {}
    problems: list[check_report.Finding] = []
    for rule_id in LEDGER_RULES:
        path = rule_ledger.ledger_path(rule_id, context.root)
        rows = cost_budget.read_rows(rule_id, cost_meter.ITEM_NAME, path, kind)
        found[rule_id] = rows.replaced(shown=context.shown(path))
        problems += [replace(problem, path=context.shown(path), check=RULE, kind="input") for problem in rows.problems]
    return found, problems


def findings(context: BuildContext) -> list[check_report.Finding]:
    """Give each registered test with no judge, and each ledger row of a test that the build does not register."""
    build = context.require_build()
    kind = kind_of(context)
    if isinstance(kind, check_report.Finding):
        return [kind]
    try:
        tests = ctest_reader.tests(build)
        ctest_reader.costs(build)
    except ctest_reader.CtestError as problem:
        return [input_error(context, problem)]
    rows, found = ledger_rows(context, kind)
    for test in tests:
        if is_judged(test.command):
            continue
        found.append(
            check_report.Finding(
                "error",
                context.shown(test.file) if test.file else context.shown(build),
                test.line,
                RULE,
                f"the test {test.name} runs `{' '.join(test.command[:3])}` with no judge of its time and its memory.  "
                f"Register it with crucible_script_test() of cmake/TestLauncher.cmake",
                "unjudged",
                test.name,
            )
        )
    names = {test.name for test in tests}
    for rule_id in LEDGER_RULES:
        table = rows[rule_id]
        for name, row in sorted(table.admitted.items(), key=lambda pair: pair[1].line):
            if name not in names:
                found.append(
                    check_report.Finding(
                        "error",
                        table.shown,
                        row.line,
                        RULE,
                        f"the ledger of {rule_id} names the test {name} for the "
                        f"kind {kind}, and the build does not register it.  Remove the row",
                        "unknown",
                        name,
                    )
                )
    return found


def measure_of(rule_id: str, record: cost_record.TestRecord) -> float:
    """Return the measure of a record in the unit of the rule: the wall time for test-time, the peak GB for test-memory."""
    return record.cost.wall_s if rule_id == RULE else record.cost.peak_rss_kb / cost_meter.KB_PER_GB


def written(context: BuildContext, ledger: rule_ledger.Ledger, reason: str | None) -> rule_ledger.Ledger:
    """Return the ledger of test-time or test-memory with the rows of the kind of the build written again (THE WRITE).

    Raises:
        NotApplicable: If the call gives no build directory, or the build has no kind
        rule_ledger.LedgerError: If the ledger is not the ledger of a rule of LEDGER_RULES, or the test list or a record
            does not read
    """
    if ledger.rule not in LEDGER_RULES:
        raise rule_ledger.LedgerError(f"the ledger of {ledger.rule} is not a ledger of {' or '.join(LEDGER_RULES)}")
    build = context.require_build()
    kind = context.kind()
    budget = context.budget(ledger.rule)
    try:
        names = {test.name for test in ctest_reader.tests(build)}
        records = ctest_reader.costs(build)
    except ctest_reader.CtestError as problem:
        raise rule_ledger.LedgerError(f"the records of the build do not read: {problem}") from None
    base = {key: entry for key, entry in ledger.base.items() if key.kind != kind}
    for name, record in sorted(records.items()):
        measure = measure_of(ledger.rule, record)
        if name not in names or check_report.classify(measure, budget) != "error":
            continue
        key = rule_ledger.Key("", kind, name)
        earlier = ledger.base.get(key)
        kept = earlier.reason if earlier is not None else reason or DEFAULT_REASON
        base[key] = rule_ledger.Entry(key, round(measure, 2), kept)
    return replace(ledger, base=base)
