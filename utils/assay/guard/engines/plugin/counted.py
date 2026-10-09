"""counted — the verdict and the write of a plugin rule with a ledger that counts findings in each configuration.

THE COUNTED RULES
    The rule quarantine and the rule heavy-include count the findings of each
    file, kind and entity in the rows of assay.core.ledger (the policy count).
    The arms of a preprocessor conditional change with the configuration of
    a build (THE CONFIGURATION of the runner).  The ledger of such a rule
    then holds one set of counts for each configuration:

        format 1
        rule RULE
        configuration NAME KEY VALUE           One row for each key of the
                                               configuration NAME
        FILE | KIND | COUNT | ENTITY | REASON  The count of the base
                                               configuration
        @NAME | FILE | KIND | DELTA | ENTITY | REASON
                                               The count of the configuration
                                               NAME, as a difference from the
                                               base count: +N or -N

    The first configuration of the ledger is the base.  The count of a
    different configuration is the base count plus its difference.  A change
    that lowers a count in each configuration then writes only the base rows.
    A CountedRule gives the findings, the inputs, the verdict and the summary
    of one rule.  This module gives the run, the write and the import that
    the two rules share.

THE POLICY
    Each rule with a ledger of configurations judges each build, and no such
    rule skips a build because of its configuration.  These rows judge a
    build (judging()):
      * the rows of the configuration with the name of the build, when the
        ledger holds one.  A configuration row can give only some facts: the
        rows of the rule syscall-capability give only the build kind.  Each
        fact of the row that the build does not share gives one line, because
        the build can then compile other preprocessor arms
      * the base rows, for a build of a name that the ledger does not hold,
        and for a ledger with no configuration.
    A build of a new kind then fails on each finding of an arm that only it
    compiles.  No finding with no row gets through.  The rules quarantine,
    heavy-include and syscall-capability use this policy.

    A write from a different build puts the counts of other arms into the
    rows.  The write of a build is then possible only in two conditions
    (write_target()):
      * the ledger holds the configuration of the build, with each fact of
        its row
      * the ledger holds no configuration.

THE RUN
    A run reads the ledger and the inputs of the rule first.  A ledger or an
    input that cannot be read gives one error.  The run judges the counts of
    the build against the rows of THE POLICY, and its summary line names
    those rows.  A run of a configuration other than the base writes the rows
    of the build after a failure, each with the prefix "ledger-row: "
    (assay.core.ledger.rows_of_counts).  A run of a build of a configuration
    that the ledger does not hold writes them each time.  Then --import or
    --add-configuration can put the configuration into the ledger.

THE WRITE
    `guard write --rule RULE --build-dir DIR` writes the rows of the
    configuration of the build.  For the base, it writes the counts.  For a
    different configuration, it writes the differences from the base rows.
    The write keeps the head comment, the other configurations and their rows.
    It refuses a count more than its row, with two exceptions.  --raise
    permits each such count, and --admit permits a count of an admitted kind
    of the rule.  --add-configuration writes the configuration of the build
    with its name.  A write of a build of a configuration that the ledger
    does not hold, with no --add-configuration, writes nothing and fails.  A
    file that git reports as moved takes the rows of its earlier path.
    A write of the base removes the difference rows of each source file that
    the tree does not hold, because no build compiles it.  A file of the build
    directory keeps its rows.  `guard write --rule RULE --import LOG` puts the
    rows that a run wrote to a log into the ledger.

Complexity: linear in the finding lines of the build, and O(k log k) for k rows of the ledger and counts of the build.
"""

import dataclasses
from collections import Counter
from collections.abc import Callable, Iterable, Mapping
from pathlib import Path

from assay.core import git, ruletable
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.plugin import runner

# The file, the kind and the entity of one count: the key of a row of the ledger.
Key = rule_ledger.Key
# The errors of a ledger or an input that cannot be read.
INPUT_ERRORS = (OSError, rows_grammar.RowsError, ruletable.TableError)


@dataclasses.dataclass(frozen=True, slots=True)
class CountedRule[Inputs]:
    """One rule with a ledger that counts findings in each configuration (THE COUNTED RULES).

    `findings` gives the findings of the build: each finding has a kind, and a problem has no kind.  `inputs` reads
    the inputs of the verdict from the root of the tree, and it can raise an error of INPUT_ERRORS.  `evaluate`
    compares the counts of the build with the ledger.  It takes the counts, the ledger, the name of the
    configuration, the inputs and the build directory as a message shows it.

    `summary` gives the line of a run or a write: it takes the counts, the configuration, the number of objects and
    a verb.  `admitted_kinds` are the kinds of a count that --admit can write above its row.  `refusal` is the advice
    of a write that refuses a count above its row.  The name that `evaluate` takes is the configuration of the rows
    that judge the build, or None for the base rows (THE POLICY).
    """

    rule_id: str
    findings: Callable[[runner.PluginContext], list[check_report.Finding]]
    inputs: Callable[[Path], Inputs]
    evaluate: Callable[[Mapping[Key, int], rule_ledger.Ledger, str | None, Inputs, str], list[check_report.Finding]]
    summary: Callable[[Mapping[Key, int], runner.BuildConfiguration, int, str], str]
    admitted_kinds: frozenset[str]
    refusal: str


def described(kind: str, entity: str) -> str:
    """Return the kind and the entity of a row as words for a message."""
    return f"the kind {kind} and the entity {entity}" if entity else f"the kind {kind}"


def counts_of(found: Iterable[check_report.Finding]) -> Counter[Key]:
    """Return the number of findings of each file, kind and entity, with no problem.  Complexity: linear."""
    return Counter(Key(item.path, item.kind, item.entity) for item in found if item.kind)


def problems_of(found: Iterable[check_report.Finding]) -> list[check_report.Finding]:
    """Return the problems of the findings: the findings with no kind."""
    return [item for item in found if not item.kind]


def input_problem(rule_id: str, problem: Exception) -> check_report.Finding:
    """Return the error of a ledger or an input that cannot be read, at the line of the problem."""
    return check_report.Finding(
        "error", rule_ledger.shown_path(rule_id), getattr(problem, "line", 0), rule_id, str(problem)
    )


@dataclasses.dataclass(frozen=True, slots=True)
class Judging:
    """The rows that judge a build (THE POLICY).

    `name` is the configuration of the rows, or None for the base rows.  `words` name the rows for a summary line.
    `note` is the line of the facts that the build does not share with the configuration of the rows, or "".
    """

    name: str | None
    words: str
    note: str


def judging(rule_id: str, ledger: rule_ledger.Ledger, configuration: runner.BuildConfiguration) -> Judging:
    """Return the rows that judge the build of a configuration (THE POLICY).  Complexity: linear in the facts."""
    facts = runner.facts_of(ledger, configuration.name)
    if facts is None:
        if not ledger.configurations:
            return Judging(None, "the base rows", "")
        return Judging(None, f"the base rows, because the ledger holds no configuration {configuration.name}", "")
    note = ""
    if not runner.is_held(ledger, configuration):
        note = runner.held_line(
            rule_id,
            ledger,
            configuration,
            f"The rows of the configuration {configuration.name} judge the build, and the build can compile other "
            f"preprocessor arms",
        )
    return Judging(configuration.name, f"the rows of the configuration {configuration.name}", note)


@dataclasses.dataclass(frozen=True, slots=True)
class WriteTarget:
    """Where a write puts the rows of a build (THE WRITE).

    `ledger` is the ledger with the configuration of the build when the write adds it.  `name` is the configuration
    of the rows, or None for the base rows of a ledger with no configuration.  `is_added` is True when the write adds
    the configuration, and each of its rows is then new.
    """

    ledger: rule_ledger.Ledger
    name: str | None
    is_added: bool


def write_target(
    rule_id: str, ledger: rule_ledger.Ledger, configuration: runner.BuildConfiguration, add_configuration: bool
) -> WriteTarget | str:
    """Return where a write puts the rows of a build, or the line that tells why the write writes nothing (THE POLICY).

    A write is possible when the ledger holds the configuration of the build with each fact of its row, or no
    configuration.  --add-configuration adds the configuration of the build with each of its facts.
    """
    name = configuration.name
    is_held = runner.is_held(ledger, configuration)
    if add_configuration:
        refusal = add_refusal(ledger, name if is_held else None, name)
        if refusal:
            return f"{rule_id}: the ledger is not written: {refusal}"
        others = tuple(item for item in ledger.configurations if item.name != name)
        added = dataclasses.replace(
            ledger,
            configurations=(*others, rule_ledger.Configuration(name, configuration.facts)),
            deltas={key: table for key, table in ledger.deltas.items() if key != name},
        )
        return WriteTarget(added, name, True)
    if is_held:
        return WriteTarget(ledger, name, False)
    if not ledger.configurations:
        return WriteTarget(ledger, None, False)
    return runner.held_line(
        rule_id,
        ledger,
        configuration,
        "A write from that build puts the counts of other preprocessor arms into the rows, so the write writes "
        "nothing.  Give --add-configuration to write the configuration of the build",
    )


def judge[Inputs](
    spec: CountedRule[Inputs], rule: rule_registry.Rule, context: runner.PluginContext
) -> list[check_report.Finding]:
    """Return the findings of the build judged against the rows of THE POLICY in the ledger of the tree of the context.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    try:
        ledger = rule_ledger.read(rule, context.root)
        inputs = spec.inputs(context.root)
    except INPUT_ERRORS as problem:
        return [input_problem(spec.rule_id, problem)]
    judged = judging(spec.rule_id, ledger, context.configuration())
    found = spec.findings(context)
    return [
        *problems_of(found),
        *spec.evaluate(counts_of(found), ledger, judged.name, inputs, context.build_shown()),
    ]


def run[Inputs](
    spec: CountedRule[Inputs], rule: rule_registry.Rule, context: runner.PluginContext, warnings_dir: Path | None
) -> int:
    """Run the rule on the build of the context, judge its counts against the ledger, and write the findings (THE RUN).

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply to the build (THE STATUS of the runner)
    """
    try:
        ledger = rule_ledger.read(rule, context.root)
        inputs = spec.inputs(context.root)
    except INPUT_ERRORS as problem:
        return check_report.emit([input_problem(spec.rule_id, problem)], spec.rule_id, warnings_dir)
    census = runner.census_or_status(rule, context, warnings_dir)
    if isinstance(census, int):
        return census
    try:
        configuration = context.configuration()
    except runner.BuildError as problem:
        return check_report.emit(
            [check_report.Finding("error", context.build_shown(), 0, spec.rule_id, str(problem))],
            spec.rule_id,
            warnings_dir,
        )
    found = spec.findings(context)
    counts, problems = counts_of(found), problems_of(found)
    judged = judging(spec.rule_id, ledger, configuration)
    if judged.note:
        check_report.write_line(judged.note)
    status = check_report.emit(
        [*problems, *spec.evaluate(counts, ledger, judged.name, inputs, context.build_shown())],
        spec.rule_id,
        warnings_dir,
    )
    check_report.write_line(
        f"{spec.summary(counts, configuration, census.objects, 'counted')}, judged against {judged.words}"
    )
    if status != 0 or not runner.is_held(ledger, configuration):
        print_rows(spec.rule_id, ledger, configuration, counts, bool(problems))
    return status


def print_rows(
    rule_id: str,
    ledger: rule_ledger.Ledger,
    configuration: runner.BuildConfiguration,
    counts: Mapping[Key, int],
    has_problems: bool,
) -> None:
    """Write the rows of the configuration of the build, as --import reads them from a log.

    The base has no such rows, because a write writes it, and a build with a problem has no rows to write.  Each
    row has the prefix assay.core.ledger.ROW_MARK.
    """
    if configuration.name == ledger.base_name or ledger.base_name is None or has_problems:
        return
    for line in rule_ledger.rows_of_counts(ledger, configuration.name, list(configuration.facts), counts):
        check_report.write_line(line)
    check_report.write_line(
        f"{rule_id}: the lines above give the rows of the configuration {configuration.name}.  "
        f"Import them from the log with `python3 utils/bin/guard write --rule {rule_id} --import "
        f"LOG`"
    )


def import_rows(
    rule: rule_registry.Rule,
    root: Path,
    source: Path,
    refusal: Callable[[rule_ledger.Ledger], str] | None = None,
) -> int:
    """Put the rows of one configuration that print_rows wrote into the ledger.  They replace its earlier rows.

    `refusal` gives the first problem of the ledger with the rows of the log, or an empty text.  A rule with a shape
    of its own for its rows gives it, and the import then writes no ledger that the run of the rule refuses.

    Returns:
        0 when the ledger is written, 1 when the rows or the ledger do not have the format
    """
    try:
        ledger = rule_ledger.import_rows(
            rule_ledger.read(rule, root), source.read_text(encoding="utf-8", errors="replace")
        )
    except (OSError, rows_grammar.RowsError, rule_ledger.LedgerError) as problem:
        check_report.write_line(f"{rule.id}: the ledger is not written: {problem}", is_error=True)
        return 1
    problem_text = refusal(ledger) if refusal is not None else ""
    if problem_text:
        check_report.write_line(f"{rule.id}: the ledger is not written: {problem_text}", is_error=True)
        return 1
    rule_ledger.store(ledger, root)
    name = ledger.configurations[-1].name
    check_report.write_line(
        f"{rule.id}: imported the configuration {name}: {len(ledger.configurations[-1].facts)} keys "
        f"and {len(ledger.deltas.get(name, {}))} difference rows, to "
        f"{rule_ledger.ledger_path(rule.id, root)}"
    )
    return 0


def add_refusal(ledger: rule_ledger.Ledger, held: str | None, name: str) -> str:
    """Return the reason that --add-configuration cannot write the configuration of the build, or an empty text."""
    if not rule_ledger.rows_grammar_name(name):
        return f"the name {name!r} of the configuration is not a lowercase word"
    if held is not None:
        return f"the ledger holds the configuration {name} of the build.  Write its rows with no --add-configuration"
    if name == ledger.base_name:
        return (
            f"the build has other keys than the base configuration {name}.  A change of the base keys makes the "
            f"ledger again: keep its head, remove its rows, then write each configuration with "
            f"--add-configuration in its build, the base first"
        )
    return ""


def drop_gone_files(ledger: rule_ledger.Ledger, root: Path, counts: Mapping[Key, int]) -> rule_ledger.Ledger:
    """Remove the difference rows of each source file that the tree does not hold.

    The write of the base before this step keeps each count of each other configuration at 0 or above
    (assay.core.ledger, THE WRITE).  A file of the build directory (ruletable.BUILD_PREFIX) is in no source tree.  A
    write of the base cannot know if a build of a different configuration writes the file.  The rows of the file
    stay, and the run of that configuration compares them as it compares each other row.  When a build of the
    configuration does not write the file, its run fails on the row until its own write removes the row.
    Complexity: linear in the difference rows.
    """
    deltas = {
        name: {
            key: entry
            for key, entry in table.items()
            if key in counts or key.path.startswith(ruletable.BUILD_PREFIX) or (root / key.path).exists()
        }
        for name, table in ledger.deltas.items()
    }
    return dataclasses.replace(ledger, deltas=deltas)


def write[Inputs](spec: CountedRule[Inputs], rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the rows of the configuration of the build (THE WRITE), or import the rows of a log.

    Returns:
        0 when the ledger is written.  1 when an object has a problem, a count increased or an input cannot be read.
        1 also when the ledger does not hold the configuration of the build and the write does not add it.  SKIPPED
        when the rule does not apply to the build
    """
    if request.import_log is not None:
        return import_rows(rule, request.root, request.import_log)
    context = runner.PluginContext(request.root, request.build_dir)
    try:
        ledger = rule_ledger.read(rule, request.root)
        census = context.census()
        configuration = context.configuration()
    except runner.NotApplicable as reason:
        check_report.write_line(f"{spec.rule_id}: {reason}.  The rule does not apply")
        return runner.SKIPPED
    except (OSError, rows_grammar.RowsError, runner.BuildError) as problem:
        check_report.write_line(f"{spec.rule_id}: the ledger is not written: {problem}", is_error=True)
        return 1
    found = spec.findings(context)
    problems = problems_of(found)
    if problems:
        check_report.write_line(
            f"{spec.rule_id}: the ledger is not written, because an object has a problem", is_error=True
        )
        return check_report.emit(problems, spec.rule_id, None)
    counts = counts_of(found)
    target = write_target(spec.rule_id, ledger, configuration, request.add_configuration)
    if isinstance(target, str):
        check_report.write_line(target, is_error=True)
        return 1
    moves: dict[str, str] = {}
    if target.is_added:
        mode = rule_ledger.WriteMode.RAISE
    else:
        moves = git.moves(request.root)
        increased = increases_permitted(spec, target.ledger, target.name, counts, moves, request.mode)
        if increased is None:
            return 1
        mode = rule_ledger.WriteMode.RAISE if increased else rule_ledger.WriteMode.LOWER
    written = rule_ledger.write_counts(rule, target.ledger, counts, target.name, mode, request.reason, moved=moves)
    if target.name is None or target.name == written.base_name:
        written = drop_gone_files(written, request.root, counts)
    rule_ledger.store(written, request.root)
    check_report.write_line(
        f"{spec.summary(counts, configuration, census.objects, 'wrote')}, to "
        f"{rule_ledger.ledger_path(spec.rule_id, request.root)}"
    )
    return 0


def increases_permitted[Inputs](
    spec: CountedRule[Inputs],
    ledger: rule_ledger.Ledger,
    name: str | None,
    counts: Mapping[Key, int],
    moves: Mapping[str, str],
    mode: rule_ledger.WriteMode,
) -> bool | None:
    """Write each count of the build that is more than its row, and tell if the mode of the write permits each one.

    --raise permits each increase, and --admit permits an increase of a kind of the admitted kinds of the rule.

    Returns:
        True when a count increased and the mode permits each increase.  False when no count increased.  None when
        the mode refuses an increase
    """
    expected = rule_ledger.moved_counts(ledger.expected(name), moves)
    held_files = {key.path for key in expected}
    increases = sorted(
        (key, count, expected.get(key, 0)) for key, count in counts.items() if count > expected.get(key, 0)
    )
    for key, count, admitted in increases:
        words = "a file that the ledger does not hold" if key.path not in held_files else f"rises from {admitted}"
        check_report.write_line(
            f"{spec.rule_id}: {key.path} has {count} findings of {described(key.kind, key.entity)}, {words}",
            is_error=True,
        )
    refused = [
        increase
        for increase in increases
        if mode != rule_ledger.WriteMode.RAISE
        and not (mode == rule_ledger.WriteMode.ADMIT and increase[0].kind in spec.admitted_kinds)
    ]
    if refused:
        check_report.write_line(
            f"{spec.rule_id}: the ledger is not written: {len(refused)} counts rose.  {spec.refusal}", is_error=True
        )
        return None
    return bool(increases)
