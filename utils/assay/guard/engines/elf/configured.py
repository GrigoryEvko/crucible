"""configured — the run and the write of a rule of the engine elf with a ledger of configurations.

THE INSPECTION
    A rule examines one build and gives an Inspection.  The inspection
    holds the findings, judged against the ledger.  It also holds the facts
    that the run prints and the write uses.  The configuration of a build
    is its build kind, its tier and each option of NAME_OPTIONS of
    assay.core.configuration that the build turns on (configuration_of()).
    Its name is the build kind with the word of each such option, as in
    x86_64-debug-asan-bench-fuzz.  A build with CRUCIBLE_BENCH links the
    benches.  Its link facts are then different from the facts of the same
    build kind with no benches.  THE POLICY of assay.core.configuration gives
    the rows that judge the build.

THE RUN
    run_inspection() writes the findings and the summary line of the rule.
    It also writes the note of a configuration with facts that the build does
    not share.  It prints the rows of the configuration of the build for
    `guard write --import LOG` (assay.core.configuration, THE ROWS FOR AN
    IMPORT).  It prints them after a failure, and each time when the ledger
    does not hold the configuration.

THE WRITE
    write_inspection() writes the configuration and the count rows of the
    build:
      * --import LOG puts the rows of a configuration from a log into the
        ledger, and the import refuses a row that the rule refuses.
      * The mode admit does not apply.  A person writes each admit row with
        its reason.
      * A ledger with a row problem writes nothing.  A build that the rule
        cannot read writes nothing.  A build of a configuration that the
        ledger does not hold writes only with --add-configuration.
      * A ledger with no configuration gets the build as its base.
      * The refusals of the rule (WritePolicy.refusals) are rows that only
        --raise writes.  A configuration that the write adds is compared with
        the largest count of each key over the configurations.
      * The write keeps each row of a different configuration, and each
        admit row that admits a finding of the build.
    writer() gives the function write(rule, request) of a rule module from
    the function examine() and the WritePolicy of the module.

`guard selftest tests.guard.engines.elf.test_configured` does the test of the module.
"""

from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass, field, replace
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import git
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.elf import runner

# The mode admit of a write gives this status (THE WRITE).
ADMIT_STATUS = 2
# The facts of each configuration of a ledger of the engine, and the facts of the options that a build turns on
# (THE INSPECTION).
CONFIGURATION_FACTS = frozenset({build_config.KIND_KEY, build_config.TIER_KEY})
OPTION_FACTS = tuple(key for _word, key in build_config.NAME_OPTIONS)
OPTION_ON = "on"


@dataclass(frozen=True, slots=True)
class Inspection:
    """The findings of one build, judged, with the facts that the run prints and the write uses (THE INSPECTION).

    `configuration` is the build kind and the tier of the build, or None when the ledger cannot be read.  `judged`
    gives the rows that judge the build.  `counts` gives the count of each key that a write writes, and `admitted`
    the count of each key that an admit row admits.
    """

    findings: tuple[check_report.Finding, ...]
    ledger: rule_ledger.Ledger
    configuration: build_config.BuildConfiguration | None = None
    judged: build_config.Judging | None = None
    counts: Mapping[rule_ledger.Key, int] = field(default_factory=dict)
    admitted: Mapping[rule_ledger.Key, int] = field(default_factory=dict)
    summary: str = ""


@dataclass(frozen=True, slots=True)
class WritePolicy:
    """What a write of a rule refuses and writes (THE WRITE).

    `row_problems` gives an error for each row of a ledger that the rule refuses.  `refusals` gives one line for each
    count of the build that only --raise writes, from the inspection and the counts that the ledger holds.
    `raise_reasons` are the reasons for --raise, and `admit_text` tells why the mode admit does not apply.  `rows`
    names the rows of the build in the line of a written ledger.
    """

    row_problems: Callable[[rule_ledger.Ledger], list[check_report.Finding]]
    refusals: Callable[[Inspection, Mapping[rule_ledger.Key, int]], list[str]]
    raise_reasons: tuple[str, ...]
    admit_text: str
    rows: str


def configuration_name(kind: str, facts: Mapping[str, str]) -> str:
    """Return the name of a configuration: the build kind, and the word of each option that its facts turn on."""
    words = [word for word, key in build_config.NAME_OPTIONS if facts.get(key) == OPTION_ON]
    return "-".join([kind, *words])


def configuration_of(context: runner.ElfContext) -> build_config.BuildConfiguration:
    """Return the configuration of the build: its build kind, its tier and the options that it turns on (THE INSPECTION).

    Raises:
        runner.NotApplicable: If the build has no build kind
        runner.BuildError: If the tier of the build or its CMake cache cannot be read
    """
    kind = context.kind()
    facts = [(build_config.KIND_KEY, kind), (build_config.TIER_KEY, context.tier())]
    facts += [
        (key, OPTION_ON)
        for key in OPTION_FACTS
        if build_config.normalized("BOOL", context.cache_value(key)) == OPTION_ON
    ]
    return build_config.BuildConfiguration(configuration_name(kind, dict(facts)), tuple(facts))


def read_ledger(rule: rule_registry.Rule, root: Path) -> rule_ledger.Ledger | Inspection:
    """Read the ledger of a rule in a tree, or give an Inspection with the one error of a ledger that fails to read."""
    try:
        return rule_ledger.read(rule, root)
    except (OSError, rows_grammar.RowsError) as problem:
        unread = check_report.Finding(
            "error",
            rule_ledger.shown_path(rule.id),
            getattr(problem, "line", 0),
            rule.id,
            f"the ledger cannot be read: {problem}",
        )
        return Inspection((unread,), rule_ledger.empty(rule))


def run_inspection(
    rule_id: str, examine: Callable[[], Inspection], warnings_dir: Path | None, prints_rows: bool = True
) -> int:
    """Run the inspection of a build, write its findings and its summary, and print its rows (THE RUN).

    A rule with no rows for an import, such as a rule with a baseline, gives `prints_rows` False.

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    try:
        inspection = examine()
    except runner.NotApplicable as reason:
        check_report.write_line(f"{rule_id}: {reason}.  The rule does not apply")
        return runner.SKIPPED
    status = check_report.emit(list(inspection.findings), rule_id, warnings_dir)
    configuration, judged = inspection.configuration, inspection.judged
    if configuration is None or judged is None:
        return status
    if judged.note:
        check_report.write_line(judged.note)
    check_report.write_line(inspection.summary)
    ledger = inspection.ledger
    if prints_rows and (not build_config.is_held(ledger, configuration) or status != 0):
        build_config.print_rows(rule_id, ledger, configuration, inspection.counts, False)
    return status


def write_inspection(
    rule: rule_registry.Rule,
    request: runner.WriteRequest,
    examine: Callable[[runner.ElfContext], Inspection],
    policy: WritePolicy,
) -> int:
    """Write the configuration and the count rows of the build (THE WRITE).

    Returns:
        0 when the ledger is written.  1 when the write refuses a count or the build cannot be read.  2 for the mode
        admit.  SKIPPED when the rule does not apply
    """
    if request.import_log is not None:
        return build_config.import_rows(rule, request.root, request.import_log, first_problem(policy.row_problems))
    if request.mode == rule_ledger.WriteMode.ADMIT:
        check_report.write_line(f"guard write: {policy.admit_text}.  The mode admit does not apply", is_error=True)
        return ADMIT_STATUS
    try:
        inspection = examine(runner.ElfContext(request.root, request.build_dir))
    except runner.NotApplicable as reason:
        check_report.write_line(f"guard write: the rule {rule.id} does not apply: {reason}", is_error=True)
        return runner.SKIPPED
    except runner.BuildError as problem:
        check_report.write_line(f"guard write: the ledger of {rule.id} is not written: {problem}", is_error=True)
        return 1
    ledger, configuration = inspection.ledger, inspection.configuration
    problems = policy.row_problems(ledger) if configuration is not None else list(inspection.findings)
    if configuration is None or problems:
        check_report.write_line(
            f"guard write: the ledger of {rule.id} is not written: {problems[0].message}", is_error=True
        )
        return 1
    target = build_config.write_target(rule.id, ledger, configuration, request.add_configuration)
    if isinstance(target, str):
        check_report.write_line(f"guard write: {target}", is_error=True)
        return 1
    if target.name is None:
        # A count row belongs to a configuration.  A ledger with no configuration then gets the build as its base.
        base = rule_ledger.Configuration(configuration.name, configuration.facts)
        target = build_config.WriteTarget(replace(ledger, configurations=(base,)), configuration.name, False)
    held = ledger.expected(rule_ledger.MAXIMUM) if target.is_added else ledger.expected(target.name)
    refused = policy.refusals(inspection, held)
    for line in refused:
        check_report.write_line(f"guard write: {line}", is_error=True)
    if refused and request.mode != rule_ledger.WriteMode.RAISE:
        check_report.write_line(
            f"guard write: the ledger of {rule.id} is not written: {len(refused)} counts that only --raise writes.  "
            f"Remove their cause.  Give --raise only for {', for '.join(policy.raise_reasons)}, and give the reason "
            f"in the commit",
            is_error=True,
        )
        return 1
    written = rule_ledger.write_counts(
        rule,
        target.ledger,
        {**inspection.counts, **inspection.admitted},
        target.name,
        rule_ledger.WriteMode.RAISE,
        request.reason,
        git.moves(request.root),
        keep_others=True,
    )
    rule_ledger.store(written, request.root)
    check_report.write_line(
        f"guard write: wrote {len(inspection.counts)} {policy.rows} of {configuration.name} on {configuration.tier} "
        f"to {rule_ledger.shown_path(rule.id)}"
    )
    return 0


def writer(
    examine: Callable[[rule_registry.Rule, runner.ElfContext], Inspection], policy: WritePolicy
) -> Callable[[rule_registry.Rule, runner.WriteRequest], int]:
    """Return the function write(rule, request) of a rule module, which writes through write_inspection() (THE WRITE).

    The runner of the engine calls the function write of a rule module for `guard write`.
    """

    def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
        """Write the configuration and the count rows of the build of the request (THE WRITE).

        Returns:
            0 when the ledger is written.  1 when the write refuses a count or the build cannot be read.  2 for the
            mode admit.  SKIPPED when the rule does not apply
        """
        return write_inspection(rule, request, lambda context: examine(rule, context), policy)

    return write


def ledger_problems(
    rule_id: str,
    ledger: rule_ledger.Ledger,
    is_row: Callable[[rule_ledger.Key, rule_ledger.Entry], bool],
    row_shape: str,
    is_single: Callable[[rule_ledger.Key], bool],
) -> list[check_report.Finding]:
    """Return an error for each configuration and each row of a ledger that the rule refuses.

    A configuration has the facts kind and tier, and the fact `on` of each option of OPTION_FACTS that its build
    turns on.  Its name is configuration_name() of these facts.  `is_row` accepts a base row, and `row_shape` tells
    the shape of a base row.  A row of a ledger with no configuration is an error.  A key that `is_single` accepts has
    the count 0 or 1 in each configuration.  Complexity: linear in the rows times the configurations.
    """
    problems: list[tuple[int, str]] = []
    for configuration in ledger.configurations:
        facts = dict(configuration.facts)
        options = set(facts) - CONFIGURATION_FACTS
        is_shape = (
            CONFIGURATION_FACTS <= set(facts)
            and options <= set(OPTION_FACTS)
            and all(facts[key] == OPTION_ON for key in options)
            and configuration_name(facts.get(build_config.KIND_KEY, ""), facts) == configuration.name
        )
        if not is_shape:
            shape = (
                f"the configuration {configuration.name} has the rows 'configuration NAME kind KIND' and "
                f"'configuration NAME tier TIER', a row 'configuration NAME OPTION on' for each option of a word "
                f"of NAME, and no other.  NAME is KIND with the word of each such option"
            )
            problems.append((0, shape))
    problems += [
        (entry.line, row_shape)
        for key, entry in sorted(ledger.base.items(), key=lambda item: item[1].line)
        if not is_row(key, entry)
    ]
    if rule_ledger.base_counts(ledger.base) and not ledger.configurations:
        problems.append((0, "the ledger has count rows and no configuration row"))
    for configuration in ledger.configurations:
        for key, count in ledger.expected(configuration.name).items():
            if count != 1 and is_single(key):
                shape = (
                    f"the configuration {configuration.name} gives {key.kind} {key.entity} of {key.path} the count "
                    f"{count}, and a row of this kind has the count 0 or 1"
                )
                problems.append((ledger.line_of(key, configuration.name), shape))
    return [
        check_report.Finding("error", rule_ledger.shown_path(rule_id), line, rule_id, message)
        for line, message in problems
    ]


def first_problem(
    row_problems: Callable[[rule_ledger.Ledger], list[check_report.Finding]],
) -> Callable[[rule_ledger.Ledger], str]:
    """Return a refusal of an import: the message of the first row problem of a ledger, or an empty text."""

    def refusal(ledger: rule_ledger.Ledger) -> str:
        """Return the first problem of the ledger with the rows of a log."""
        problems = row_problems(ledger)
        return problems[0].message if problems else ""

    return refusal


def risen_rows(inspection: Inspection, held: Mapping[rule_ledger.Key, int], kinds: Sequence[str]) -> list[str]:
    """Return a refusal line for each count of a kind of `kinds` above its row: a new row or a risen count.

    Complexity: linear in the counts of the inspection.
    """
    return [
        f"{key.path} gets the row {key.kind} {key.entity}"
        if key not in held
        else f"{key.path} has {count} of {key.kind} {key.entity}, and the ledger holds {held[key]}"
        for key, count in sorted(inspection.counts.items())
        if key.kind in kinds and count > held.get(key, 0)
    ]
