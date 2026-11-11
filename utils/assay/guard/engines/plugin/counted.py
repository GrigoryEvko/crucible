"""counted — the verdict and the write of a plugin rule with a ledger that counts the findings of each key.

THE COUNTED RULES
    The rules quarantine, heavy-include and fixture-quarantine count the
    findings of each key in the rows of assay.core.ledger (the policy count).
    A key is the path, the kind and the entity of a finding.  The rule
    quarantine gives the directory of a file as the path, and the other rules
    give the file.  The arms of a preprocessor conditional change with the
    configuration of a build (assay.core.configuration, THE CONFIGURATION).
    One set of rows judges each build, and the count of a key is its largest
    count over the configurations of the landing gate (THE REDUCE of
    assay.core.ledger):

        format 1
        rule RULE
        PATH | KIND | COUNT | ENTITY | REASON

    A CountedRule gives the findings, the inputs, the verdict and the summary
    of one rule.  This module gives the run and the write that the rules
    share.

THE CONTEXT
    A rule reads its build through a runner.PluginContext: root, build_dir,
    build_shown() and census().  census() gives the number of units of the
    build in its field objects, and the summary line names them.  The rule
    fixture-quarantine of the engine build counts the census of each
    negative fixture.  Its CountedRule gives a subclass of the context in
    `context_of`, and each other rule takes runner.PluginContext.

THE RUN
    A run reads the ledger and the inputs of the rule first.  A ledger or an
    input that cannot be read gives one error.  The run judges the counts of
    the build against the rows of the ledger.  A count below its row passes
    (THE POLICIES of assay.core.ledger).

THE WRITE
    `guard write --rule RULE --build-dir DIR` writes the counts of one build.
    The counts of one build are partial, and the write lowers no row (THE
    WRITE of assay.core.ledger).  The landing gate gives the write a build
    of each of its configurations, and the write then lowers each row to the
    largest count over these builds (THE REDUCE of assay.core.ledger).  A
    build with a problem makes the write write nothing.  The write gives the
    ledger the head comment of the rule, and with no such comment it keeps
    the head of the file.  It refuses a count more than its row, with two
    exceptions.  --raise permits each such count, and --admit permits a count
    of an admitted kind of the rule.  The write reads no git history, so a
    change that moves a finding to a key with no row writes it with --raise.

Complexity: linear in the finding lines of the builds, and O(k log k) for k rows of the ledger and counts of the builds.
"""

import dataclasses
from collections import Counter
from collections.abc import Callable, Iterable, Mapping
from pathlib import Path
from typing import Any

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import ruletable
from assay.guard.engines.plugin import runner

# The path, the kind and the entity of one count: the key of a row of the ledger.
Key = rule_ledger.Key
# The errors of a ledger or an input that cannot be read.
INPUT_ERRORS = (OSError, rows_grammar.RowsError, ruletable.TableError)


@dataclasses.dataclass(frozen=True, slots=True)
class CountedRule[Inputs]:
    """One rule with a ledger that counts the findings of each key: a path, a kind and an entity (THE COUNTED RULES).

    `findings` gives the findings of the build: each finding has a kind, and a problem has no kind.  `inputs` reads
    the inputs of the verdict from the root of the tree, and it can raise an error of INPUT_ERRORS.  `evaluate`
    compares the counts of the build with the ledger.  It takes the counts, the ledger, the inputs and the build
    directory as a message shows it.  A count below its row is no error.

    `summary` gives the line of a run or a write: it takes the counts, the number of units and a verb.
    `admitted_kinds` are the kinds of a count that --admit can write above its row.  `refusal` is the advice of a
    write that refuses a count above its row.  `context_of` makes the context of a build from the root and the build
    directory (THE CONTEXT).  `comment` is the head comment that each write gives the ledger (assay.core.ledger, THE
    HEAD COMMENT).  An empty text keeps the head of the file.
    """

    rule_id: str
    findings: Callable[[Any], list[check_report.Finding]]
    inputs: Callable[[Path], Inputs]
    evaluate: Callable[[Mapping[Key, int], rule_ledger.Ledger, Inputs, str], list[check_report.Finding]]
    summary: Callable[[Mapping[Key, int], int, str], str]
    admitted_kinds: frozenset[str]
    refusal: str
    context_of: Callable[[Path, Path | None], runner.PluginContext] = runner.PluginContext
    comment: str = ""


def no_inputs(_root: Path) -> None:
    """Read no input: the verdict of the rule reads only the counts and the ledger."""


def described(kind: str, entity: str) -> str:
    """Return the kind and the entity of a row as words for a message."""
    return f"the kind {kind} and the entity {entity}" if entity else f"the kind {kind}"


def summary_of(
    rule_id: str, item_words: str, file_words: str, unit_words: str
) -> Callable[[Mapping[Key, int], int, str], str]:
    """Return the summary of a CountedRule that tells its counts with the words of the rule.

    Args:
        rule_id: The rule
        item_words: The words of the counted items, for example "findings"
        file_words: The words of the files of the rows, for example "files"
        unit_words: The words of the units of the build, for example "objects"
    """

    def summary(counts: Mapping[Key, int], units: int, verb: str) -> str:
        """Return the line that tells the counts, the rows, the files and the units of one run or one write."""
        files = {key.path for key in counts}
        return (
            f"{rule_id}: {verb} {sum(counts.values())} {item_words} in {len(counts)} rows of {len(files)} "
            f"{file_words}, from {units} {unit_words}"
        )

    return summary


def counts_of(found: Iterable[check_report.Finding]) -> Counter[Key]:
    """Return the number of findings of each path, kind and entity, with no problem.  Complexity: linear."""
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
    """Return the findings of the build judged against the rows of the ledger of the tree of the context.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    try:
        ledger = rule_ledger.read(rule, context.root)
        inputs = spec.inputs(context.root)
    except INPUT_ERRORS as problem:
        return [input_problem(spec.rule_id, problem)]
    found = spec.findings(context)
    return [*problems_of(found), *spec.evaluate(counts_of(found), ledger, inputs, context.build_shown())]


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
    found = spec.findings(context)
    counts, problems = counts_of(found), problems_of(found)
    status = check_report.emit(
        [*problems, *spec.evaluate(counts, ledger, inputs, context.build_shown())], spec.rule_id, warnings_dir
    )
    check_report.write_line(spec.summary(counts, census.objects, "counted"))
    return status


def build_counts[Inputs](spec: CountedRule[Inputs], context: runner.PluginContext) -> Counter[Key]:
    """Return the counts of the build of the context for a write (THE WRITE).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read, or an object of the build has a problem
    """
    found = spec.findings(context)
    problems = problems_of(found)
    if problems:
        raise runner.BuildError(
            f"the build has {len(problems)} problems, such as {problems[0].path}: {problems[0].message}"
        )
    return counts_of(found)


def write[Inputs](spec: CountedRule[Inputs], rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the counts of the builds of the request (THE WRITE).

    Returns:
        0 when the ledger is written.  1 when an object has a problem, a count increased or an input cannot be read.
        SKIPPED when the rule does not apply to a build
    """
    try:
        ledger = rule_ledger.read(rule, request.root)
    except (OSError, rows_grammar.RowsError) as problem:
        check_report.write_line(f"{spec.rule_id}: the ledger is not written: {problem}", is_error=True)
        return 1
    units = 0

    def counted_build(context: runner.PluginContext) -> Counter[Key]:
        """Return the counts of one build, and add the number of its units to the units of the write."""
        nonlocal units
        counts = build_counts(spec, context)
        units += context.census().objects
        return counts

    per_build = runner.counts_of_builds(spec.rule_id, request, counted_build, spec.context_of)
    if isinstance(per_build, int):
        return per_build
    counts = rule_ledger.largest_counts(per_build)
    increased = increases_permitted(spec, ledger, counts, request.mode)
    if increased is None:
        return 1
    written = rule_ledger.write_counts(
        rule,
        ledger,
        counts,
        rule_ledger.WriteMode.RAISE if increased else rule_ledger.WriteMode.LOWER,
        request.reason,
        is_partial=request.is_partial,
    )
    if spec.comment:
        written = dataclasses.replace(written, comment=spec.comment)
    rule_ledger.store(written, request.root)
    check_report.write_line(
        f"{spec.summary(counts, units, 'wrote')} of {len(request.builds)} builds, to "
        f"{rule_ledger.ledger_path(spec.rule_id, request.root)}"
    )
    return 0


def increases_permitted[Inputs](
    spec: CountedRule[Inputs],
    ledger: rule_ledger.Ledger,
    counts: Mapping[Key, int],
    mode: rule_ledger.WriteMode,
) -> bool | None:
    """Write each count of the builds that is more than its row, and tell if the mode of the write permits each one.

    --raise permits each increase, and --admit permits an increase of a kind of the admitted kinds of the rule.

    Returns:
        True when a count increased and the mode permits each increase.  False when no count increased.  None when
        the mode refuses an increase
    """
    expected = ledger.expected()
    held_paths = {key.path for key in expected}
    increases = sorted(
        (key, count, expected.get(key, 0)) for key, count in counts.items() if count > expected.get(key, 0)
    )
    for key, count, row in increases:
        words = "a path that the ledger does not hold" if key.path not in held_paths else f"rises from {row}"
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
