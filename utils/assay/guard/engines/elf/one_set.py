"""one_set — the run and the write of a rule of the engine elf with one set of rows that judges each build.

THE SET
    The ledger of such a rule holds one set of rows.  A Debug build, a
    Release build and a build with a sanitizer give other facts, and the set
    holds the facts of each of them.  Each build is judged against the full
    set (assay.core.ledger.compare()).  A fact of the build with no row is an
    error.  A row
    that the build does not give passes, because a ledger fails only on a
    growth (THE POLICIES of assay.core.ledger).

THE RUN
    run_inspection() writes the findings and the summary line of the rule.

THE WRITE
    write_inspection() writes the set from one build:
      * The write keeps each row of the ledger.  One build cannot see the
        facts of a different build.  A write that removed a row that the
        build does not give could make the run of a different build fail.
      * It adds a row for each fact of the build with no row, and it raises
        each count of the build that is above its row.  The refusals of the
        rule (WritePolicy.refusal_kinds) are rows that only --raise writes.
      * The mode admit does not apply.  A person writes each admit row with
        its reason, and each row of a kind of WritePolicy.person_kinds.  The
        write refuses such a fact with no row, also with --raise.
      * A ledger with a row problem writes nothing.  A build that the rule
        cannot read writes nothing.  A build with an error that no row of the
        ledger admits writes nothing (Inspection.unadmitted).

THE REDUCE
    The landing gate gives the write a build of each of its configurations
    of objects (THE REDUCE of assay.core.ledger).  The write then takes the
    largest count of each key over these builds, and it lowers each row to
    that count.  It removes a row of a fact that no build gives, and it keeps
    each admit row.  It refuses each count above its row, because the check
    of each build refused it first.

`guard selftest tests.guard.engines.elf.test_one_set` does the test of the module.
"""

from collections.abc import Callable, Mapping
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.elf import runner

# The mode admit of a write gives this status (THE WRITE).
USAGE_STATUS = 2


@dataclass(frozen=True, slots=True)
class Inspection:
    """The findings of one build, judged against the set, with the counts that the write uses (THE SET).

    `counts` gives the count of each key of the build that a write writes.  `unadmitted` gives each error of
    `findings` that no row of the ledger can admit, and a write writes nothing while one holds (THE WRITE).
    """

    findings: tuple[check_report.Finding, ...]
    ledger: rule_ledger.Ledger
    counts: Mapping[rule_ledger.Key, int] = field(default_factory=dict)
    summary: str = ""
    unadmitted: tuple[check_report.Finding, ...] = ()


@dataclass(frozen=True, slots=True)
class WritePolicy:
    """What a write of a rule refuses and writes (THE WRITE).

    `row_problems` gives an error for each row of a ledger that the rule refuses.  `refusal_kinds` names the kinds of
    the keys that only --raise writes, when the count of the build is above the row.  `raise_reasons` are the reasons
    for --raise, and `admit_text` tells why the mode admit does not apply.  `rows` names the rows of the build in the
    line of a written ledger.  `person_kinds` names the kinds of the keys that only a person writes, with a reason,
    and no write writes their row: `person_text` tells the reason of each such row.
    """

    row_problems: Callable[[rule_ledger.Ledger], list[check_report.Finding]]
    refusal_kinds: tuple[str, ...]
    raise_reasons: tuple[str, ...]
    admit_text: str
    rows: str
    person_kinds: tuple[str, ...] = ()
    person_text: str = ""


def read_ledger(rule: rule_registry.Rule, root: Path) -> rule_ledger.Ledger | Inspection:
    """Read the ledger of a rule in a tree, or give an Inspection with the one error of a ledger that fails to read.

    The write refuses such an inspection, because its error is in `unadmitted`.
    """
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
        return Inspection((unread,), rule_ledger.empty(rule), unadmitted=(unread,))


def ledger_problems(
    rule_id: str,
    ledger: rule_ledger.Ledger,
    is_row: Callable[[rule_ledger.Key, rule_ledger.Entry], bool],
    row_shape: str,
    is_single: Callable[[rule_ledger.Key], bool],
) -> list[check_report.Finding]:
    """Return an error for each row of a ledger that the rule refuses (THE SET).

    `is_row` accepts a row, and `row_shape` tells the shape of a row.  A key that `is_single` accepts has the count
    1.  Complexity: linear in the rows.
    """
    problems: list[tuple[int, str]] = []
    for key, entry in sorted(ledger.base.items(), key=lambda item: item[1].line):
        if not is_row(key, entry):
            problems.append((entry.line, row_shape))
        elif entry.value != rule_ledger.ADMIT_VALUE and entry.value != 1 and is_single(key):
            problems.append(
                (entry.line, f"{key.path} gives {key.kind} {key.entity} the count {entry.value}, and such a row has 1")
            )
    return [
        check_report.Finding("error", rule_ledger.shown_path(rule_id), line, rule_id, message)
        for line, message in problems
    ]


def run_inspection(rule_id: str, examine: Callable[[], Inspection], warnings_dir: Path | None) -> int:
    """Run the inspection of a build, and write its findings and its summary (THE RUN).

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    try:
        inspection = examine()
    except runner.NotApplicable as reason:
        check_report.write_line(f"{rule_id}: {reason}.  The rule does not apply")
        return runner.SKIPPED
    status = check_report.emit(list(inspection.findings), rule_id, warnings_dir)
    if inspection.summary:
        check_report.write_line(inspection.summary)
    return status


def risen_rows(inspection: Inspection, held: Mapping[rule_ledger.Key, int], kinds: tuple[str, ...]) -> list[str]:
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


def set_counts(ledger: rule_ledger.Ledger, counts: Mapping[rule_ledger.Key, int]) -> dict[rule_ledger.Key, int]:
    """Return the count of each key of the written set: the larger of its row and of the build (THE WRITE).

    Each admit row stays with the count 1, and a write adds no admit row.  Complexity: linear in the rows and the
    counts.
    """
    merged = dict(ledger.expected())
    for key, count in counts.items():
        merged[key] = max(merged.get(key, 0), count)
    merged.update(dict.fromkeys(ledger.admits(), 1))
    return merged


def inspected_builds(
    rule: rule_registry.Rule,
    request: runner.WriteRequest,
    examine: Callable[[runner.ElfContext], Inspection],
    policy: WritePolicy,
) -> list[Inspection] | int:
    """Return the inspection of each build of a write, or the status of a build that the write cannot use.

    Complexity: one inspection of each build.

    Returns:
        The inspection of each build.  1 when a build cannot be read, its ledger has a row problem or it gives an
        error that no row admits.  SKIPPED when the rule does not apply to a build
    """
    if not request.builds:
        check_report.write_line(
            f"guard write: the rule {rule.id} does not apply: the call gives no --build-dir DIR", is_error=True
        )
        return runner.SKIPPED
    found: list[Inspection] = []
    for build in request.builds:
        try:
            inspection = examine(runner.ElfContext(request.root, build))
        except runner.NotApplicable as reason:
            check_report.write_line(
                f"guard write: the rule {rule.id} does not apply to {build}: {reason}.  The write writes nothing",
                is_error=True,
            )
            return runner.SKIPPED
        except runner.BuildError as problem:
            check_report.write_line(
                f"guard write: the ledger of {rule.id} is not written: {build}: {problem}", is_error=True
            )
            return 1
        problems = [*policy.row_problems(inspection.ledger), *inspection.unadmitted]
        if problems:
            check_report.write_line(
                f"guard write: the ledger of {rule.id} is not written: {build}: {problems[0].message}", is_error=True
            )
            return 1
        found.append(inspection)
    return found


def write_inspection(
    rule: rule_registry.Rule,
    request: runner.WriteRequest,
    examine: Callable[[runner.ElfContext], Inspection],
    policy: WritePolicy,
) -> int:
    """Write the set of the ledger with the counts of the builds of the request (THE WRITE, THE REDUCE).

    Returns:
        0 when the ledger is written.  1 when the write refuses a count or a build cannot be read.  2 for the mode
        admit.  SKIPPED when the rule does not apply to a build
    """
    if request.mode == rule_ledger.WriteMode.ADMIT:
        check_report.write_line(f"guard write: {policy.admit_text}.  The mode admit does not apply", is_error=True)
        return USAGE_STATUS
    inspections = inspected_builds(rule, request, examine, policy)
    if isinstance(inspections, int):
        return inspections
    ledger = inspections[0].ledger
    largest = Inspection((), ledger, rule_ledger.largest_counts(item.counts for item in inspections))
    is_reduce = bool(request.more_builds)
    person_rows = risen_rows(largest, ledger.expected(), policy.person_kinds)
    if person_rows and not is_reduce:
        for line in person_rows:
            check_report.write_line(f"guard write: {line}", is_error=True)
        check_report.write_line(
            f"guard write: the ledger of {rule.id} is not written: {len(person_rows)} rows that only a person writes, "
            f"also with --raise.  Remove their cause, or write each row by hand with {policy.person_text}",
            is_error=True,
        )
        return 1
    kinds = tuple(sorted({key.kind for key in largest.counts})) if is_reduce else policy.refusal_kinds
    refused = risen_rows(largest, ledger.expected(), kinds)
    for line in refused:
        check_report.write_line(f"guard write: {line}", is_error=True)
    if refused and (is_reduce or request.mode != rule_ledger.WriteMode.RAISE):
        check_report.write_line(
            f"guard write: the ledger of {rule.id} is not written: {len(refused)} counts that only --raise writes.  "
            f"Remove their cause.  Give --raise only for {', for '.join(policy.raise_reasons)}, and give the reason "
            f"in the commit",
            is_error=True,
        )
        return 1
    if is_reduce:
        counts = {**largest.counts, **dict.fromkeys(ledger.admits(), 1)}
        written = rule_ledger.write_counts(rule, ledger, counts, rule_ledger.WriteMode.LOWER, request.reason)
    else:
        written = rule_ledger.write_counts(
            rule, ledger, set_counts(ledger, largest.counts), rule_ledger.WriteMode.RAISE, request.reason
        )
    rule_ledger.store(written, request.root)
    check_report.write_line(
        f"guard write: wrote {len(largest.counts)} {policy.rows} of {len(inspections)} builds into the set of "
        f"{rule_ledger.shown_path(rule.id)}"
    )
    return 0


def writer(
    examine: Callable[[rule_registry.Rule, runner.ElfContext], Inspection], policy: WritePolicy
) -> Callable[[rule_registry.Rule, runner.WriteRequest], int]:
    """Return the function write(rule, request) of a rule module, which writes through write_inspection() (THE WRITE).

    The runner of the engine calls the function write of a rule module for `guard write`.
    """

    def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
        """Write the set of the ledger with the counts of the builds of the request (THE WRITE).

        Returns:
            0 when the ledger is written.  1 when the write refuses a count or a build cannot be read.  2 for the mode
            admit.  SKIPPED when the rule does not apply to a build
        """
        return write_inspection(rule, request, lambda context: examine(rule, context), policy)

    return write
