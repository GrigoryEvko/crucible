"""counted — the verdict and the write of a plugin rule with a ledger that counts findings in each configuration.

THE COUNTED RULES
    The rule quarantine and the rule heavy-include count the findings of each
    file, kind and entity in the rows of assay.core.ledger (the policy count).
    The arms of a preprocessor conditional change with the configuration of
    a build (assay.core.configuration, THE CONFIGURATION).  The ledger of such
    a rule then holds one set of counts for each configuration:

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
    different configuration is the base count plus its difference.  A write
    changes only the counts of the configuration of its build (THE WRITE of
    assay.core.ledger).  A change that lowers a count in each configuration
    then writes the rows of each configuration in its build.
    A CountedRule gives the findings, the inputs, the verdict and the summary
    of one rule.  This module gives the run, the write and the import that
    the rules share.

THE CONTEXT
    A rule reads its build through a runner.PluginContext: root, build_dir,
    build_shown(), census() and configuration().  census() gives the number of
    units of the build in its field objects, and the summary line names them.
    The rule fixture-quarantine of the engine build counts the census of each
    negative fixture.  Its CountedRule gives a subclass of the context in
    `context_of`, and each other rule takes runner.PluginContext.

THE RUN
    A run reads the ledger and the inputs of the rule first.  A ledger or an
    input that cannot be read gives one error.  The run judges the counts of
    the build against the rows of THE POLICY of assay.core.configuration, and
    its summary line names those rows.  The run writes the rows of a
    configuration other than the base for an import (THE ROWS FOR AN IMPORT
    of the same module).  Then --import or --add-configuration can put the
    configuration into the ledger.

THE WRITE
    `guard write --rule RULE --build-dir DIR` writes the rows of the
    configuration of the build, where THE POLICY permits a write.  For the
    base, it writes the counts.  For a different configuration, it writes the
    differences from the base rows.  The write keeps the other configurations
    and their counts.  It gives the ledger the head comment of the rule, and
    with no such comment it keeps the head of the file.  It refuses a count more than its
    row, with two exceptions.  --raise permits each such count, and --admit
    permits a count of an admitted kind of the rule.  A file that git reports
    as moved takes the rows of its earlier path.
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
from typing import Any

from assay.core import configuration as build_config
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
    that judge the build, assay.core.ledger.MAXIMUM, or None for the base rows (THE POLICY of
    assay.core.configuration).  With MAXIMUM, a count below its row is no error.  `context_of` makes the context of
    a write from the root and the build directory (THE CONTEXT).  `comment` is the head comment that each write gives
    the ledger (assay.core.ledger, THE HEAD COMMENT).  An empty text keeps the head of the file.
    """

    rule_id: str
    findings: Callable[[Any], list[check_report.Finding]]
    inputs: Callable[[Path], Inputs]
    evaluate: Callable[[Mapping[Key, int], rule_ledger.Ledger, str | None, Inputs, str], list[check_report.Finding]]
    summary: Callable[[Mapping[Key, int], build_config.BuildConfiguration, int, str], str]
    admitted_kinds: frozenset[str]
    refusal: str
    context_of: Callable[[Path, Path | None], runner.PluginContext] = runner.PluginContext
    comment: str = ""


def described(kind: str, entity: str) -> str:
    """Return the kind and the entity of a row as words for a message."""
    return f"the kind {kind} and the entity {entity}" if entity else f"the kind {kind}"


def summary_of(
    rule_id: str, item_words: str, file_words: str, unit_words: str
) -> Callable[[Mapping[Key, int], build_config.BuildConfiguration, int, str], str]:
    """Return the summary of a CountedRule that tells its counts with the words of the rule.

    Args:
        rule_id: The rule
        item_words: The words of the counted items, for example "findings"
        file_words: The words of the files of the rows, for example "files"
        unit_words: The words of the units of the build, for example "objects"
    """

    def summary(
        counts: Mapping[Key, int], configuration: build_config.BuildConfiguration, units: int, verb: str
    ) -> str:
        """Return the line that tells the counts, the rows, the files and the units of one run or one write."""
        files = {key.path for key in counts}
        return (
            f"{rule_id}: {verb} {sum(counts.values())} {item_words} in {len(counts)} rows of {len(files)} "
            f"{file_words}, configuration {configuration.name} on {configuration.tier}, from {units} {unit_words}"
        )

    return summary


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


def judge[Inputs](
    spec: CountedRule[Inputs], rule: rule_registry.Rule, context: runner.PluginContext
) -> list[check_report.Finding]:
    """Return the findings of the build judged against the rows of the policy in the ledger of the tree of the context.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    try:
        ledger = rule_ledger.read(rule, context.root)
        inputs = spec.inputs(context.root)
    except INPUT_ERRORS as problem:
        return [input_problem(spec.rule_id, problem)]
    judged = build_config.judging(spec.rule_id, ledger, context.configuration())
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
    except runner.NotApplicable as reason:
        check_report.write_line(f"{rule.id}: {reason}.  The rule does not apply")
        return runner.SKIPPED
    except runner.BuildError as problem:
        return check_report.emit(
            [check_report.Finding("error", context.build_shown(), 0, spec.rule_id, str(problem))],
            spec.rule_id,
            warnings_dir,
        )
    found = spec.findings(context)
    counts, problems = counts_of(found), problems_of(found)
    judged = build_config.judging(spec.rule_id, ledger, configuration)
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
    if status != 0 or not build_config.is_held(ledger, configuration):
        build_config.print_rows(spec.rule_id, ledger, configuration, counts, bool(problems))
    return status


def drop_gone_files(ledger: rule_ledger.Ledger, root: Path, counts: Mapping[Key, int]) -> rule_ledger.Ledger:
    """Remove the difference rows of each source file that the tree does not hold.

    The write of the base before this step keeps each count of each other configuration (assay.core.ledger, THE
    WRITE).  A source file that the tree does not hold has no finding in the build of a configuration.  Its rows then
    go.  A file of the build directory (ruletable.BUILD_PREFIX) is in no source tree.  A
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
        return build_config.import_rows(rule, request.root, request.import_log)
    context = spec.context_of(request.root, request.build_dir)
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
    target = build_config.write_target(spec.rule_id, ledger, configuration, request.add_configuration)
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
    if spec.comment:
        written = dataclasses.replace(written, comment=spec.comment)
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
