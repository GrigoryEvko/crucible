"""The judge and the write that the rules of the family linear share.

THE FAMILY
    The quarantine plugin of utils/tools/quarantine/ holds the family linear
    (linear.cpp): the rule must-consume, and the six rules of THE SHAPES,
    linear-containment, linear-capture, drop-door, linear-forge, brand-escape
    and brand-reentry.  Each rule is in report mode: a line is counted and is no
    compile error.

THE LEDGER
    The ledger of each rule counts the lines of each directory and kind (the
    policy count), with the key of the rule quarantine
    (assay.guard.engines.plugin.quarantine, THE KEY).  A row holds no entity and
    no line.  Each count row gives a reason: in its own REASON cell, or in a row
    `reason PREFIX | REASON` of a prefix that holds its directory (THE REASON OF
    A PREFIX of assay.core.ledger).  A count row with no reason is the kind
    no-reason, which no ledger row admits (REFUSED_KINDS).  A count can only
    fall.  `guard list --build-dir DIR --path DIRECTORY/` prints the lines of a
    directory.

THE WRITE
    A write counts the lines of the rule in each build.  A count row with no
    reason is a problem of the ledger and not of a build, so a write does not
    count it, and the check reports it until the row gets its reason.

Complexity: linear in the lines of the rule, and linear in the rows of the ledger times its reason rows.
"""

from collections import Counter
from collections.abc import Callable, Iterable, Mapping
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.plugin import declarations, runner
from assay.guard.engines.plugin.quarantine import directory_of

# The kind of a count row with no reason.  No ledger row admits it.
NO_REASON_KIND = "no-reason"
REFUSED_KINDS = frozenset({NO_REASON_KIND})


def message(item_text: str, kind: str, actions: Mapping[str, str]) -> str:
    """Return the message of one line of a rule: the line and the action of its kind."""
    return f"{item_text}: {actions.get(kind, 'is a finding of the rule')}"


def prefix_reason(ledger: rule_ledger.Ledger, path: str) -> str:
    """Return the reason of the longest reason row that holds a path, or "" (THE REASON OF A PREFIX)."""
    held = [item for item in ledger.reasons if item.holds(path)]
    return max(held, key=lambda item: len(item.prefix)).reason if held else ""


def reason_errors(rule: rule_registry.Rule, root: Path) -> list[check_report.Finding]:
    """Give one error for each count row of the ledger of RULE with no reason (THE LEDGER).

    A ledger that cannot be read gives no error here: the judgement of the ledger reports it.  Complexity: linear in
    the count rows times the reason rows.
    """
    try:
        ledger = rule_ledger.read(rule, root)
    except rows_grammar.RowsError, OSError:
        return []
    shown = rule_ledger.shown_path(rule.id)
    return [
        check_report.Finding(
            "error",
            shown,
            ledger.line_of(key),
            rule.id,
            f"the row of {key.path} and the kind {key.kind} gives no reason.  Give the reason in its REASON cell, or "
            f"in a row `reason PREFIX | REASON` of a prefix that holds the directory",
            NO_REASON_KIND,
            f"{key.path} {key.kind}",
        )
        for key in sorted(ledger.count_keys())
        if not ledger.reason_of(key) and not prefix_reason(ledger, key.path)
    ]


def line_findings(
    context: runner.PluginContext, rule_id: str, actions: Mapping[str, str], records: Iterable[str] = ()
) -> list[check_report.Finding]:
    """Give an error for each line of RULE_ID other than a record, each count row with no reason and each problem.

    The path of the error of a line is the directory of its file, the key of the ledger (THE LEDGER).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    skipped = frozenset(records)
    found = census.errors(rule_id)
    found += [
        check_report.Finding(
            "error", directory_of(item.file), item.line, rule_id, message(item.text(), item.kind, actions), item.kind
        )
        for item in census.of_rule(rule_id)
        if item.kind not in skipped
    ]
    found += reason_errors(declarations.rule_of(context, rule_id), context.root)
    return found


def build_counts(
    rule: rule_registry.Rule, context: runner.PluginContext, problem_kinds: Iterable[str] = ()
) -> Counter[rule_ledger.Key]:
    """Return the count of each key of the lines of the rule in the build of the context (THE WRITE).

    A line of a kind of PROBLEM_KINDS, and a line with no kind, is a problem of the build.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read, or it has a problem
    """
    refused = frozenset(problem_kinds)
    found = [item for item in context.findings_of(rule) if item.level == "error" and item.kind != NO_REASON_KIND]
    problems = [item for item in found if not item.kind or item.kind in refused]
    if problems:
        raise runner.BuildError(
            f"the build has {len(problems)} problems, such as {problems[0].path}:{problems[0].line}: "
            f"{problems[0].message}.  Correct them first"
        )
    return Counter(rule_ledger.key_of(item) for item in found)


def write(
    rule: rule_registry.Rule,
    request: runner.WriteRequest,
    counts: Callable[[runner.PluginContext], Counter[rule_ledger.Key]] | None = None,
) -> int:
    """Write the ledger of the rule from the lines of the builds of the request (THE WRITE).

    Returns:
        0 when the ledger is written.  1 when a build has a problem, a count rose in the mode lower or an input cannot
        be read.  SKIPPED when the rule does not apply to a build
    """
    try:
        ledger = rule_ledger.read(rule, request.root)
    except (rows_grammar.RowsError, OSError) as problem:
        check_report.write_line(f"guard write: the ledger of {rule.id} is not written: {problem}", is_error=True)
        return 1
    count_of = counts if counts is not None else (lambda context: build_counts(rule, context))
    per_build = runner.counts_of_builds(rule.id, request, count_of)
    if isinstance(per_build, int):
        return per_build
    try:
        written = rule_ledger.write_counts(
            rule, ledger, rule_ledger.largest_counts(per_build), request.mode, request.reason, request.is_partial
        )
    except rule_ledger.LedgerError as problem:
        check_report.write_line(f"guard write: the ledger of {rule.id} is not written: {problem}", is_error=True)
        return 1
    rule_ledger.store(written, request.root)
    check_report.write_line(f"guard write: {rule_ledger.shown_path(rule.id)} holds {len(written.base)} rows")
    return 0
