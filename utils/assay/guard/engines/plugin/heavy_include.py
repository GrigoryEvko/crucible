"""heavy-include — a header in include/ does not include a heavy library header.

THE RULE
    Each translation unit that includes a header compiles each header that it
    includes again.  A standard header such as <chrono> or <thread> costs up
    to 1 s of front-end time alone.  Then one include of it in a base header
    costs that time in each file of the tree.  CLAUDE.md section XV, "Compile
    time", rule 4 states the rule: a builtin before a header.

    A library header is a header that is not a file of the source root.  The
    heavy and light rows of the rule table utils/rule-table.txt give the cost
    of each library header (assay.loop.derive.header_costs).  A header is
    heavy when its compile alone costs more than the compile alone of <meta>
    times the error threshold of this rule in utils/registry.txt.  The
    quarantine plugin gives these findings to the includes of a file in
    include/ (include.cpp, THE HEAVY HEADERS):
      * a finding of the kind `heavy-include` for each include of a header
        with a heavy row
      * a finding of the kind `unmeasured-header` for each include of a
        library header that no heavy row and no light row gives.
    The rule reads those finding lines in the sections of the objects of the
    target all of one build (assay.guard.engines.plugin.runner, THE
    SECTIONS).  The entity of a finding is the header in angle brackets.  The
    plugin reads only the arms of a preprocessor conditional that a build
    compiles.  An include in a different arm counts only in a configuration
    that compiles that arm.

THE LEDGER
    utils/ledgers/heavy-include.txt counts the includes of each file and
    header in each configuration of a build (assay.guard.engines.plugin.counted,
    THE COUNTED RULES).  A row with no reason is an include that the tree
    holds at this time, and each run gives one warning for it.  A row with a
    reason is a header that owns the heavy header on purpose, such as the one
    door of a system header.  Such a row gives no warning.  `guard write
    --rule heavy-include --admit --reason TEXT` writes such a row.  A reason
    row of a prefix also gives the reason of a row (assay.core.ledger, THE
    REASON OF A PREFIX).  The base
    rows judge a build of a configuration that the ledger does not hold (THE
    POLICY of assay.core.configuration).

THE VERDICT
    * A count more than its ledger count is an error: a new include of a heavy
      header.
    * A count less than its ledger count is an error, until the commit writes
      the ledger again.
    * An include of an unmeasured header is an error in each case: measure the
      header.  The plugin then gives `heavy-include` or no finding.
    * Each row with no reason of its own or of a prefix that the build holds
      gives a warning.
    * A heavy or a light row is an error when its kind does not agree with
      its measure and the threshold of this rule (cost_problems).  A rule
      table with no such row is an error too.  The plugin reads the kinds
      that a measure gives.
    * Each problem of the census of the runner is an error.

Usage
    guard check --rule heavy-include --build-dir DIR [--warnings-dir DIR]
    guard write --rule heavy-include --build-dir DIR [--raise | --admit --reason TEXT] [--add-configuration]
    guard write --rule heavy-include --import LOG

Complexity: linear in the finding lines of the build, and O(k log k) for k rows of the ledger and counts of the build.
"""

from collections.abc import Mapping
from dataclasses import replace
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.plugin import counted, runner
from assay.loop.derive import header_costs

RULE = "heavy-include"
HEAVY = "heavy-include"
UNMEASURED = "unmeasured-header"
# The kinds of a count that --admit can write above its row, with its reason.
ADMITTED_KINDS = frozenset((HEAVY,))
REFUSAL = (
    "Remove the new includes of a heavy header.  Give --admit with --reason TEXT only for a header that owns the heavy "
    "header on purpose, and --raise only when the plugin or the rule table changed what it reports"
)
ADVICE = "Use a builtin or a lower header, or move the code that uses the header to a source file"

Key = rule_ledger.Key


def cost_problems(root: Path) -> list[check_report.Finding]:
    """Return a problem for each heavy or light row of the rule table that does not agree with its measure (THE ROWS).

    A table with no heavy row and no light row is a problem too, because the plugin then gives no finding of the
    rule (assay.loop.derive.header_costs).
    """
    table = header_costs.TABLE.as_posix()
    try:
        found = header_costs.read_costs(root)
        problems = list(found.problems)
        if not found.costs:
            message = f"{table} holds no heavy row and no light row, and the plugin judges no include"
            problems.append(check_report.Finding("error", table, 0, RULE, message))
        if not problems:
            problems += header_costs.kind_problems(
                found, header_costs.limit_of(found.costs, header_costs.threshold_of(root))
            )
    except (OSError, header_costs.CostError, rows_grammar.RowsError) as problem:
        problems = [check_report.Finding("error", table, 0, RULE, str(problem))]
    measure = "Measure the headers with `python3 utils/bin/loop derive header-costs --measure BUILD_DIR`"
    return [replace(problem, check=RULE, message=f"{problem.message}.  {measure}") for problem in problems]


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each finding line of the rule, keyed by its file, kind and header, and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    return [
        *census.errors(RULE),
        *cost_problems(context.root),
        *(
            check_report.Finding("error", item.file, item.line, RULE, item.text(), item.kind, item.entity)
            for item in census.of_rule(RULE)
        ),
    ]


def no_inputs(_root: Path) -> None:
    """Read no input: the verdict reads only the counts and the ledger."""


def evaluate(
    counts: Mapping[Key, int], ledger: rule_ledger.Ledger, name: str | None, _inputs: None, build_shown: str
) -> list[check_report.Finding]:
    """Compare each count of the build with its ledger count (THE VERDICT).

    Complexity: O(k log k) for k counts of the build and the ledger.
    """
    found: list[check_report.Finding] = []
    expected = ledger.expected(name)
    shown = rule_ledger.shown_path(RULE)
    command = f"python3 utils/bin/guard write --rule {RULE} --build-dir {build_shown}"
    for key in sorted(set(counts) | set(expected)):
        measured, admitted = counts.get(key, 0), expected.get(key, 0)
        line = ledger.line_of(key, name)
        header = key.entity
        if key.kind == UNMEASURED and measured > 0:
            found.append(
                check_report.Finding(
                    "error",
                    shown,
                    line,
                    RULE,
                    f"{key.path} includes {header}, and no heavy row and no light row of utils/rule-table.txt gives its "
                    f"cost.  The rule cannot judge the include.  Measure the header: python3 utils/bin/loop derive "
                    f"header-costs --measure BUILD_DIR {header[1:-1]}",
                )
            )
        elif measured > admitted:
            found.append(
                check_report.Finding(
                    "error",
                    shown,
                    line,
                    RULE,
                    f"{key.path} includes {header} {measured} times, and the ledger admits {admitted}.  {header} costs more "
                    f"alone than <meta> times the threshold of the rule {RULE}, and each translation unit that includes "
                    f"{key.path} pays it.  {ADVICE}.  A header that owns {header} on purpose gets a row with its reason: "
                    f"`{command} --admit --reason TEXT`",
                )
            )
        elif measured < admitted and name != rule_ledger.MAXIMUM:
            found.append(
                check_report.Finding(
                    "error",
                    shown,
                    line,
                    RULE,
                    f"the ledger admits {admitted} includes of {header} in {key.path}, and the build has {measured}.  Write "
                    f"the ledger again in the same commit: `{command}`",
                )
            )
        elif measured > 0 and not ledger.kept_reason(key, name):
            found.append(
                check_report.Finding(
                    "warning",
                    shown,
                    line,
                    RULE,
                    f"{key.path} includes {header}, which costs more alone than <meta> times the threshold of the rule "
                    f"{RULE}.  Each translation unit that includes {key.path} pays it.  {ADVICE}, and write the ledger "
                    f"again",
                )
            )
    return found


# The line that tells the includes, the rows and the files of one run or one write.
summary = counted.summary_of(RULE, "includes of a heavy or an unmeasured header", "files", "objects")

SPEC = counted.CountedRule(RULE, findings, no_inputs, evaluate, summary, ADMITTED_KINDS, REFUSAL)


def judge(rule: rule_registry.Rule, context: runner.PluginContext) -> list[check_report.Finding]:
    """Return the findings of the build judged against the ledger of the tree of the context (THE VERDICT).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return counted.judge(SPEC, rule, context)


def run(rule: rule_registry.Rule, context: runner.PluginContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build of the context, judge its counts against the ledger, and write the findings.

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    return counted.run(SPEC, rule, context, warnings_dir)


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the rows of the configuration of the build (THE LEDGER), or import the rows of a log.

    Returns:
        0 when the ledger is written.  1 when an object has a problem, a count increased or an input cannot be
        read.  1 also when the ledger does not hold the configuration of the build and the write does not add it.
        SKIPPED when the rule does not apply
    """
    return counted.write(SPEC, rule, request)
