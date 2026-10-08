"""home — the shared part of the rules of the family home: the sites, the paths of the home rows and the ledger.

THE FAMILY
    The quarantine plugin gives the finding lines of the rules
    federation-admission, host-owners, no-combine-ids-duplicate,
    splits-orphan and trait-guard (utils/tools/quarantine/home.cpp, THE
    SITES).  The rule table holds the home row of each entity
    (assay.core.ruletable, Home).  The module of each rule gives the text of
    its findings.  This module gives the findings, the paths that admit no
    site, the judgement of the ledger and the write of the ledger.

THE FINDINGS
    Each finding line of a rule is one error at its file and its line.  Its
    kind and its entity are the key of a ledger row.  A line of the kind
    at-home is no finding: it says that a path of a home row admitted a
    site.  Each problem of the census of the runner is an error too.

THE PATHS OF A HOME ROW
    Each path of a home row of a rule must admit a site.  The build then
    gives an at-home line for the entity of the row in a file of the path.
    The check file of a header, test/layer/checks/LAYER/PATH.cpp, stands where
    include/LAYER/PATH.h stands, as the plugin reads it.  The rule does not
    judge a path that holds no file of the build.  A site of such a path can
    be in a different configuration, such as a bench build.  A path that
    admits no site is an error in each build that compiles a file of the
    path.  The row then lists only the paths that hold a site.

THE BUILD
    A file that the build compiles is a source of the compile database, or a
    header in include/.  test/layer gives each header of include/ a sentinel
    unit in each build.  The run does not judge a ledger row of a file that
    the build does not compile, and a write keeps the row.  The run judges a
    row with no file in each build.

Complexity: linear in the finding lines of the build and in the entries of the compile database.
O(r * l) for r paths of home rows and l at-home lines.
"""

import dataclasses
from collections import Counter
from collections.abc import Callable, Sequence
from pathlib import Path

from assay.core import git, ruletable
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT
from assay.guard.engines.meta import runner as meta_runner
from assay.guard.engines.plugin import runner
from assay.readers import compdb
from assay.readers import sections as quarantine_sections

AT_HOME = "at-home"
HEADER_TREE = "include/"
CHECK_TREE = "test/layer/checks/"
CHECK_SUFFIX = ".cpp"
HEADER_SUFFIX = ".h"
# The rule table of a tree, relative to its root.
TABLE = ruletable.TABLE.relative_to(REPO_ROOT)

# The text of one finding line of a rule: its message, from the line and the home rows of the rule.
type Message = Callable[[quarantine_sections.Finding, Sequence[ruletable.Home]], str]


@dataclasses.dataclass(frozen=True, slots=True)
class Build:
    """The source files of the compile database of a build, relative to the root of the tree (THE BUILD)."""

    sources: frozenset[str]

    def compiles(self, path: str) -> bool:
        """Say if the build compiles a file of PATH: a file in the directory PATH when PATH ends with '/'."""
        if path.startswith(HEADER_TREE):
            return True
        if path.endswith("/"):
            return any(source.startswith(path) for source in self.sources)
        return path in self.sources

    def judges(self, key: rule_ledger.Key) -> bool:
        """Say if the run judges a ledger row: a row with no file, or a row of a file that the build compiles."""
        return not key.path or self.compiles(key.path)


def read_as(path: str) -> str:
    """Return the path that a home compares: the header of a check file, and the path of each other file."""
    if path.startswith(CHECK_TREE) and path.endswith(CHECK_SUFFIX):
        return HEADER_TREE + path[len(CHECK_TREE) : -len(CHECK_SUFFIX)] + HEADER_SUFFIX
    return path


def build_of(context: runner.PluginContext) -> Build:
    """Return the source files of the build of the context (THE BUILD).

    Raises:
        NotApplicable: If the context has no build directory
        BuildError: If the compile database cannot be read
    """
    if context.build_dir is None:
        raise runner.NotApplicable("the family home reads the compile database of a build, and the call gives none")
    try:
        rows = compdb.entries(context.build_dir)
    except compdb.CompdbError as problem:
        raise runner.BuildError(f"the compile database cannot be read: {problem}") from None
    root = str(context.root.resolve())
    prefix = f"{root}/"
    return Build(frozenset(row.file[len(prefix) :] for row in rows if row.file.startswith(prefix)))


def homes_of(root: Path, rule_id: str) -> tuple[ruletable.Home, ...]:
    """Return the home rows of one rule in the rule table of the tree of ROOT.

    Raises:
        ruletable.TableError: If the table does not obey its grammar
        OSError: If the table cannot be read
    """
    return ruletable.load(root / TABLE).homes_of(rule_id)


def row_of(entity: str, homes: Sequence[ruletable.Home]) -> ruletable.Home | None:
    """Return the home row of ENTITY, or None."""
    return next((home for home in homes if home.entity == entity), None)


def paths_text(home: ruletable.Home | None) -> str:
    """Return the paths of a home row as words for a message."""
    if home is None or not home.paths:
        return "no file"
    return ", ".join(home.paths)


def message_of(text: str) -> Message:
    """Return the message of a rule from a text with the fields {entity} and {paths}.

    The field {paths} gives the paths of the home row of the entity of the line (paths_text()).
    """

    def message(item: quarantine_sections.Finding, homes: Sequence[ruletable.Home]) -> str:
        """Return the text of one finding line of the rule."""
        return text.format(entity=item.entity, paths=paths_text(row_of(item.entity, homes)))

    return message


def site_findings(
    census: runner.Census, rule_id: str, homes: Sequence[ruletable.Home], message: Message
) -> list[check_report.Finding]:
    """Give one error for each finding line of the rule, with its kind and its entity (THE FINDINGS)."""
    return [
        check_report.Finding(
            "error", item.file, item.line, rule_id, message(item, homes), item.kind, " ".join(item.entity.split())
        )
        for item in census.of_rule(rule_id)
        if item.kind != AT_HOME
    ]


def stale_findings(
    census: runner.Census, rule_id: str, homes: Sequence[ruletable.Home], build: Build
) -> list[check_report.Finding]:
    """Give one error for each path of a home row that admits no site in a build that compiles a file of the path.

    Complexity: O(r * l) for r paths of the rows and l at-home lines of the rule.
    """
    sites: dict[str, list[str]] = {}
    for item in census.of_rule(rule_id):
        if item.kind == AT_HOME:
            sites.setdefault(" ".join(item.entity.split()), []).append(read_as(item.file))
    found: list[check_report.Finding] = []
    for home in homes:
        for path in home.paths:
            if not build.compiles(path) or any(ruletable.holds(path, file) for file in sites.get(home.entity, ())):
                continue
            found.append(
                check_report.Finding(
                    "error",
                    str(TABLE),
                    home.line,
                    rule_id,
                    f"the home row of {home.entity} admits the path {path}, and no site of {home.entity} stands "
                    f"under it.  Remove the path from the row, so that the row lists only the places of its sites",
                )
            )
    return found


def input_problem(rule_id: str, problem: Exception) -> check_report.Finding:
    """Return the error of a rule table that cannot be read."""
    return check_report.Finding("error", str(TABLE), getattr(problem, "line", 0), rule_id, str(problem))


def rule_findings(context: runner.PluginContext, rule_id: str, message: Message) -> list[check_report.Finding]:
    """Give the errors of one rule: each problem of the census, each finding line and each path with no site.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    try:
        homes = homes_of(context.root, rule_id)
    except (OSError, ruletable.TableError) as problem:
        return [*census.errors(rule_id), input_problem(rule_id, problem)]
    return [
        *census.errors(rule_id),
        *site_findings(census, rule_id, homes, message),
        *stale_findings(census, rule_id, homes, build_of(context)),
    ]


def judge(
    rule: rule_registry.Rule, context: runner.PluginContext, raw: Sequence[check_report.Finding] | None = None
) -> list[check_report.Finding]:
    """Return the findings of the rule judged against its ledger, with the rows of the files that the build compiles.

    A finding with no kind, such as a problem of the census or a path with no site, stays an error.  RAW gives the
    findings of the rule when the caller has them, and None reads them.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    if raw is None:
        raw = context.findings_of(rule)
    keyed = [item for item in raw if item.kind]
    rest = [item for item in raw if not item.kind]
    return [*rest, *meta_runner.judged(rule, context.root, keyed, build_of(context).judges)]


def run(rule: rule_registry.Rule, context: runner.PluginContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build of the context, judge it (judge()) and write its findings.

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    census = runner.census_or_status(rule, context, warnings_dir)
    if isinstance(census, int):
        return census
    try:
        found = meta_runner.rule_module(rule).judge(rule, context)
    except runner.NotApplicable as reason:
        check_report.write_line(f"{rule.id}: {reason}.  The rule does not apply")
        return runner.SKIPPED
    except runner.BuildError as problem:
        found = [check_report.Finding("error", context.build_shown(), 0, rule.id, str(problem))]
    status = check_report.emit(found, rule.id, warnings_dir)
    errors = sum(item.level == "error" for item in found)
    check_report.write_line(f"{rule.id}: {errors} error(s), {len(found) - errors} warning(s)")
    return status


def counts_to_write(
    rule: rule_registry.Rule, ledger: rule_ledger.Ledger, found: Sequence[check_report.Finding], build: Build
) -> Counter[rule_ledger.Key]:
    """Return the count of each key that a write writes: the findings, and each row that the build does not judge."""
    counts = Counter(rule_ledger.key_of(item) for item in found if item.level == "error" and item.kind)
    for key, entry in ledger.base.items():
        if not build.judges(key):
            counts[key] = int(entry.value) if rule.policy == rule_registry.Policy.COUNT else 1
    return counts


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the ledger of the rule from the findings of a build, and keep each row that the build does not judge.

    Returns:
        0 when the ledger is written.  1 when the census has a problem, a count rose in the mode lower or an input
        cannot be read.  2 on a usage error.  SKIPPED when the rule does not apply
    """
    if request.add_configuration or request.import_log is not None:
        check_report.write_line(
            f"guard write: the rule {rule.id} has no configurations, so --add-configuration and --import do not "
            f"apply to it",
            is_error=True,
        )
        return 2
    context = runner.PluginContext(request.root, request.build_dir)
    try:
        ledger = rule_ledger.read(rule, request.root)
        found = context.findings_of(rule)
        build = build_of(context)
    except runner.NotApplicable as reason:
        check_report.write_line(f"guard write: the rule {rule.id} does not apply: {reason}", is_error=True)
        return runner.SKIPPED
    except (runner.BuildError, rows_grammar.RowsError, OSError) as problem:
        check_report.write_line(f"guard write: the ledger of {rule.id} is not written: {problem}", is_error=True)
        return 1
    problems = [item for item in found if item.level == "error" and not item.kind]
    if problems:
        check_report.write_line(
            f"guard write: the ledger of {rule.id} is not written, because the build has a problem", is_error=True
        )
        return check_report.emit(problems, rule.id, None)
    try:
        written = rule_ledger.write_counts(
            rule,
            ledger,
            counts_to_write(rule, ledger, found, build),
            None,
            request.mode,
            request.reason,
            git.moves(request.root),
        )
    except rule_ledger.LedgerError as problem:
        check_report.write_line(f"guard write: the ledger of {rule.id} is not written: {problem}", is_error=True)
        return 1
    rule_ledger.store(written, request.root)
    check_report.write_line(f"guard write: {rule_ledger.shown_path(rule.id)} holds {len(written.base)} rows")
    return 0
