"""test-time — each registered test runs behind the judge of its time and its memory, and each ledger row names a test.

THE JUDGE
    The test launcher `loop test-step` (assay.loop.teststep) and the fixture
    driver `loop fixture` judge each test while it runs.  They compare the
    wall time of the test with the measured rule test-time of
    utils/registry.txt.  They compare the peak memory of its largest process
    with the measured rule test-memory, and its user instructions with the
    measured rule test-instructions.  With an accurate count, test-instructions
    holds the error level, and test-time gives a warning only
    (assay.loop.teststep, THE MEASURED RULES).  A test over an error
    threshold that no ledger row keeps fails (assay.core.budget, THE
    VERDICT).  Each run
    writes the record of the test,
    BUILD_DIR/test-records/NAME.test.cost (assay.core.record, THE RECORDS).
    Each test writes its warnings to the file RULE.NAME.txt of the warnings
    directory.  The step meter judges with verdict() of assay.core.budget.
    It gives its own messages, because the import closure of a step meter
    does not hold assay.core.report (assay.core.budget, THE STEP METERS).

THE RULE
    A test cannot see a test that runs with no judge, or a ledger row that
    names a test of no build.  The rule reads the tests that the build
    registers and the records of their last runs (assay.readers.ctest).  It
    also reads the rows of the ledgers of test-time, test-memory and
    test-instructions that judge the build (assay.core.budget, THE ROWS).  It
    refuses:
      unjudged  a test with no test launcher and no fixture driver in its
                command.  Its time and its memory have no judge.  CMake gives
                the launcher to a test of an executable target, and
                crucible_script_test() of cmake/TestLauncher.cmake gives it to
                a script test
      unknown   a row of the name of the configuration of the build that names
                a test that the build does not register
      input     the build has no kind or no name of its configuration, an
                input does not read, or a ledger row does not have its shape
    The entity of a finding is the name of the test.  The judge refuses a row
    of a test that runs at or below the warning threshold of its rule.  The
    rule judges no measure itself.  Its findings go to test-time.txt of the
    warnings directory, apart from the files of the tests.

    A ledger row is '- | KIND | VALUE | TEST | REASON' (assay.core.meter, THE
    REGISTRY AND THE LEDGERS).  A ledger that is not there has no row.

THE WRITE
    `guard write --rule test-time --build-dir BUILD_DIR` writes the rows of
    the name of the configuration of the build again, from the records of the
    build directory.  A row of a test with a last record over the warning
    threshold of test-time gets a new value.  The value is the smallest wall
    time of the window of the record, which the judge of the test reads.  A
    row of a test at or below that threshold goes.  A registered test with a
    last record over the error threshold, no row and no instruction count gets
    a new row.  With a count, the wall time gives a warning only, and no row
    is necessary.  A test with no record keeps its row.  `guard write --rule
    test-memory` writes the ledger of test-memory in the same way, with the
    peak memory of each record in GB.  `guard write --rule test-instructions`
    writes the ledger of test-instructions with the instruction count of each
    record in G.  A test with no count in its record keeps its row.  The
    two rules have a holder and no module of their own.  The write then calls
    written() through assay.guard.engines.buildsys.held (THE WRITE of that
    module).  A row keeps its reason, and a new row takes the reason of
    --reason, or DEFAULT_REASON.  A row of a different name stays, and a row
    of the build kind keeps a test of a build with options.

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
INSTRUCTIONS_RULE = "test-instructions"
# The rules with ledgers that name a test (THE RULE).
LEDGER_RULES = (RULE, MEMORY_RULE, INSTRUCTIONS_RULE)
GIGA = 1e9
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


def names_of(context: BuildContext) -> tuple[str, str] | check_report.Finding:
    """Return the build kind and the name of the configuration of the build, or the error of a build with no names."""
    try:
        kind = context.kind()
    except meta_runner.NotApplicable as problem:
        return input_error(context, problem)
    name = cost_meter.read_configuration(str(context.require_build()))
    if name is None:
        return input_error(
            context,
            f"the build has no {cost_meter.CONFIGURATION_FILE}, which cmake/BuildLauncher.cmake writes at each "
            f"configure.  Configure the build again",
        )
    return kind, name


def ledger_rows(
    context: BuildContext, kind: str, name: str
) -> tuple[dict[str, cost_budget.Rows], list[check_report.Finding]]:
    """Return the ledger rows that judge the build for each rule of LEDGER_RULES, and an error for each bad row."""
    found: dict[str, cost_budget.Rows] = {}
    problems: list[check_report.Finding] = []
    for rule_id in LEDGER_RULES:
        path = rule_ledger.ledger_path(rule_id, context.root)
        rows = cost_budget.read_rows(rule_id, cost_meter.ITEM_NAME, path, kind, name)
        found[rule_id] = rows.replaced(shown=context.shown(path))
        problems += [replace(problem, path=context.shown(path), check=RULE, kind="input") for problem in rows.problems]
    return found, problems


def findings(context: BuildContext) -> list[check_report.Finding]:
    """Give each registered test with no judge, and each ledger row of a test that the build does not register."""
    build = context.require_build()
    names_found = names_of(context)
    if isinstance(names_found, check_report.Finding):
        return [names_found]
    kind, name = names_found
    try:
        tests = ctest_reader.tests(build)
        ctest_reader.costs(build)
    except ctest_reader.CtestError as problem:
        return [input_error(context, problem)]
    rows, found = ledger_rows(context, kind, name)
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
    registered = {test.name for test in tests}
    for rule_id in LEDGER_RULES:
        table = rows[rule_id]
        for test_name, row in sorted(table.admitted.items(), key=lambda pair: pair[1].line):
            if test_name not in registered and test_name in table.own:
                found.append(
                    check_report.Finding(
                        "error",
                        table.shown,
                        row.line,
                        RULE,
                        f"the ledger of {rule_id} names the test {test_name} for {name}, and the build does not "
                        f"register it.  Remove the row",
                        "unknown",
                        test_name,
                    )
                )
    return found


def measure_of(rule_id: str, record: cost_record.TestRecord) -> float | None:
    """Return the measure of a record in the unit of the rule, as the judge of the test reads it, or None.

    test-time reads the smallest wall time of the window of the test (assay.loop.teststep, THE MEASURED RULES).
    test-memory reads the peak memory of the last run in GB.  test-instructions reads the instruction count of the
    last run in G, and a record with no count gives None.
    """
    if rule_id == RULE:
        return record.least_wall_s
    if rule_id == INSTRUCTIONS_RULE:
        count = record.cost.test_instructions
        return None if count is None else count / GIGA
    return record.cost.peak_rss_kb / cost_meter.KB_PER_GB


def written(context: BuildContext, ledger: rule_ledger.Ledger, reason: str | None) -> rule_ledger.Ledger:
    """Return the ledger of a rule of LEDGER_RULES with the rows of the configuration written again (THE WRITE).

    Raises:
        NotApplicable: If the call gives no build directory, or the build has no kind
        rule_ledger.LedgerError: If the ledger is not a ledger of LEDGER_RULES, or an input does not read.  An input is
            the name of the configuration, the test list or a record
    """
    if ledger.rule not in LEDGER_RULES:
        raise rule_ledger.LedgerError(f"the ledger of {ledger.rule} is not a ledger of {' or '.join(LEDGER_RULES)}")
    build = context.require_build()
    names_found = names_of(context)
    if isinstance(names_found, check_report.Finding):
        raise rule_ledger.LedgerError(names_found.message)
    kind, name = names_found
    budget = context.budget(ledger.rule)
    try:
        registered = {test.name for test in ctest_reader.tests(build)}
        records = ctest_reader.costs(build)
    except ctest_reader.CtestError as problem:
        raise rule_ledger.LedgerError(f"the records of the build do not read: {problem}") from None
    # A record with no measure of the rule, such as a run with no instruction count, keeps the row of its test.
    measures = {test_name: measure_of(ledger.rule, record) for test_name, record in records.items()}
    base = {
        key: entry
        for key, entry in ledger.base.items()
        if key.kind != name or (key.entity in registered and measures.get(key.entity) is None)
    }
    for test_name, record in sorted(records.items()):
        measure = measures[test_name]
        if measure is None:
            continue
        key = rule_ledger.Key("", name, test_name)
        earlier = ledger.base.get(key)
        level = check_report.classify(measure, budget)
        has_kind_row = name != kind and rule_ledger.Key("", kind, test_name) in ledger.base
        # With an instruction count, a wall time over the error threshold gives a warning only (THE WRITE).
        is_error = level == "error" and not (ledger.rule == RULE and record.cost.test_instructions is not None)
        if test_name not in registered or level is None or (earlier is None and (not is_error or has_kind_row)):
            continue
        kept = earlier.reason if earlier is not None else reason or DEFAULT_REASON
        base[key] = rule_ledger.Entry(key, cost_budget.kept_value(measure), kept)
    return replace(ledger, base=base)
