"""heavy-include — a header in include/ does not include a heavy library header.

Rule
    Each translation unit that includes a header compiles each header that it
    includes again.  A standard header such as <chrono> or <thread> costs up
    to 1 s of front-end time alone.  Then one include of it in a base header
    costs that time in each file of the tree.  CLAUDE.md section XV, "Compile
    time", rule 4 states the rule: a builtin before a header.

    A library header is a header that is not a file of the source root.  The
    heavy and light rows of the rule table utils/rule-table.txt give the cost
    of each library header (assay.loop.derive.header_costs).  A header is
    heavy when its compile alone costs more than the compile alone of <meta>
    times the limit of this rule in utils/registry.txt.  The
    quarantine plugin gives these findings to the includes of a file in
    include/ (include.cpp, the heavy headers):
      * a finding of the kind `heavy-include` for each include of a header
        with a heavy row
      * a finding of the kind `unmeasured-header` for each include of a
        library header that no heavy row and no light row gives.
    The rule reads those finding lines in the sections of the objects of the
    target all of one build (assay.guard.engines.plugin.runner, the
    sections).  The entity of a finding is the header in angle brackets.  The
    plugin reads only the arms of a preprocessor conditional that a build
    compiles.  An include in a different arm counts only in a build that
    compiles that arm.

Ledger
    utils/ledgers/heavy-include.txt counts the includes of each file and
    header (assay.guard.engines.plugin.counted, the counted rules).  The count
    of a key is its largest count over the configurations of the landing gate.
    A row with no reason is an include that the tree holds at this time.  A
    row with a reason is a header that owns the heavy header on purpose, such
    as the one door of a system header.  `guard write --rule heavy-include
    --admit --reason TEXT` writes such a row.  A reason row of a prefix also
    gives the reason of a row (assay.core.ledger, the reason of a prefix).
    A row gives no output, with a reason or with none.
    The plugin reads an include only in the arms that a build compiles, so
    the write of one build keeps a row that the build does not count.  But
    a row whose file is gone, or whose file holds no text of its header,
    can give no finding in any build (is_gone()).  Each write removes such
    a row, also a row with a reason.  An include of a header through a
    macro would hide its name from the text, and no header of include/
    holds such an include.  A text of the header in a comment keeps the
    row, which is the safe side.

Verdict
    * A count more than its ledger count is an error: a new include of a heavy
      header.
    * A count less than its ledger count gives no finding (the policies of
      assay.core.ledger).  The landing gate writes the lower count.
    * An include of an unmeasured header is an error in each case: measure the
      header.  The plugin then gives `heavy-include` or no finding.
    * A heavy or a light row is an error when its kind does not agree with
      its measure and the threshold of this rule (cost_problems).  A rule
      table with no such row is an error too.  The plugin reads the kinds
      that a measure gives.
    * Each problem of the census of the runner is an error.

Usage
    guard check --rule heavy-include --build-dir DIR [--warnings-dir DIR]
    guard write --rule heavy-include --build-dir DIR [--raise | --admit --reason TEXT]

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
    """Return a problem for each heavy or light row of the rule table that does not agree with its measure (the rows).

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


def evaluate(
    counts: Mapping[Key, int], ledger: rule_ledger.Ledger, _inputs: None, build_shown: str
) -> list[check_report.Finding]:
    """Compare each count of the build with its ledger count (the verdict).

    Complexity: O(k log k) for k counts of the build and the ledger.
    """
    found: list[check_report.Finding] = []
    expected = ledger.expected()
    shown = rule_ledger.shown_path(RULE)
    command = f"python3 utils/bin/guard write --rule {RULE} --build-dir {build_shown}"
    for key in sorted(set(counts) | set(expected)):
        measured, admitted = counts.get(key, 0), expected.get(key, 0)
        line = ledger.line_of(key)
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
    return found


def is_gone(root: Path, key: Key) -> bool:
    """Tell that no build can give a finding of a row: its file is gone, or its text holds no text of its header.

    The entity of a row is the header in angle brackets, and an include of it holds that text (the ledger).  A file
    that cannot be read keeps its row.  Complexity: one read of the file.
    """
    path = root / key.path
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return True
    except OSError:
        return False
    return bool(key.entity) and key.entity not in text


# The line that tells the includes, the rows and the files of one run or one write.
summary = counted.summary_of(RULE, "includes of a heavy or an unmeasured header", "files", "objects")

SPEC = counted.CountedRule(
    RULE, findings, counted.no_inputs, evaluate, summary, ADMITTED_KINDS, REFUSAL, is_gone=is_gone
)


def judge(rule: rule_registry.Rule, context: runner.PluginContext) -> list[check_report.Finding]:
    """Return the findings of the build judged against the ledger of the tree of the context (the verdict).

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
    """Write the counts of the builds of the request (the ledger).

    Returns:
        0 when the ledger is written.  1 when an object has a problem, a count increased or an input cannot be
        read.  SKIPPED when the rule does not apply to a build
    """
    return counted.write(SPEC, rule, request)
