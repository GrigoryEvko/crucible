"""header-constexpr — a header that compiles alone does no large constant evaluation, and each listed header fails alone.

THE INPUTS
    The two rules read the compile database of the build (assay.readers.compdb)
    and two lists of test/layer/.  test/layer/CMakeLists.txt reads the same
    lists at each configure, and the rows grammar of assay.core.rows reads them
    here.  Each rule compiles a header alone with -fsyntax-only, through
    assay.core.proc, and the compile writes no object and no dependency file.

THE RULE header-constexpr-ops
    test/layer/CMakeLists.txt compiles each header of include/ alone, in a
    one-line unit `#include <header>` in BUILD/test/layer/sentinels/LAYER/.
    The unit compiles with -fconstexpr-ops-limit at the error threshold of the
    measured rule header-constexpr-ops of utils/registry.txt.  An evaluation of
    more operations runs at the position of the header, in each unit that
    includes the header, and GCC stops the compile.  GCC gives no warning
    shape of the limit.  The limit is an error only.
    test/layer/header-constexpr-ops.txt gives a higher limit to a header, with
    its reason:

        <layer>/<path>.h | limit | reason

    That list holds the admitted items of the rule, because CMake reads it.
    No file of utils/ledgers/ holds them.  A header of
    test/layer/crucible-not-standalone.txt has no one-line unit.  The rule
    compiles each listed header two times: at the threshold and at the limit
    of its row.  It reads the diagnostics of each compile from its SARIF log
    (assay.readers.sarif).  The owner of an evaluation is the first constexpr
    context of its error, the expression where the evaluation starts.  The
    rule gives these kinds:
      threshold    the registry gives no threshold, or a threshold with a fraction
      list         the list is not there, or a row does not have its shape
      list         a row names no header, gives no reason, or names a header a second time
      list         a row gives a limit that is not more than the threshold
      database     the build has no compile database that the rule can read
      no-unit      a header with no one-line unit in the compile database
      unit-limit   a one-line unit with a last -fconstexpr-ops-limit that is not the limit of its header
      row-limit    a listed header that does not compile alone at the limit of its row
      unneeded     a listed header that compiles alone at the threshold.  Its row has no use
      other-error  a listed header that fails at the threshold for a cause other than the limit
      listed       a warning for each listed header, with each evaluation over the threshold
    The entity of a finding is the header, relative to include/, when the
    finding names one.

THE RULE header-standalone
    test/layer/crucible-not-standalone.txt names each crucible header that
    cannot compile alone, `crucible/<path>.h | reason`.  Such a header has no
    sentinel.  The rule compiles each listed header alone with the command of
    a unit of the target layer_sentinel_crucible from the compile database.
    The list can only become shorter.  The rule gives these kinds:
      list         the list is not there, or a row does not have its shape or gives no reason
      list         a row names no crucible header, or names a header a second time
      no-sentinel  the compile database holds no unit of layer_sentinel_crucible
      compile      the compiler of a listed header cannot start, or it runs for more than its time limit
      standalone   a listed header that compiles alone.  Remove its row, and the header gets its sentinel again
    The entity of a finding is the header, relative to include/.

WHAT THE RULES CANNOT SEE
    The operation count measures the steps of the constant evaluator.  It
    does not count the cost of a template instantiation or of a reflection
    query.  A walk that instantiates many templates in a small number of
    steps stays at or below the limit.  The rule header-checks of the engine
    tsast finds each eager evaluation in the parse tree.

Complexity: one read of the compile database and of the headers of include/.  Two compiles of each listed header of
header-constexpr-ops, and one compile of each listed header of header-standalone, with JOBS compiles at a time.
"""

import os
import tempfile
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import proc
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import compdb, sarif

OPS_RULE = "header-constexpr-ops"
STANDALONE_RULE = "header-standalone"
REGISTRY = "utils/registry.txt"
LIST = "test/layer/header-constexpr-ops.txt"
NOT_STANDALONE = "test/layer/crucible-not-standalone.txt"
INCLUDE = "include"
LAYERS = ("foundation", "fixy", "crucible")
CRUCIBLE_LAYER = "crucible"
# The directory of the one-line units in a build directory, as test/layer/CMakeLists.txt writes them.
SENTINELS = ("test", "layer", "sentinels")
# The object directory of the target of the crucible sentinels, one component of the path of each of its objects.
SENTINEL_OBJECTS = "layer_sentinel_crucible.dir"
LIMIT_FLAG = "-fconstexpr-ops-limit="
# The words of the GCC message of an evaluation that the operation limit stops.
OPS_ERROR = "evaluation operation count exceeds limit"
# The flags of a compile command that write a file: the first set takes the next word too.
WRITE_FLAGS_WITH_VALUE = frozenset({"-o", "-MF", "-MT", "-MQ"})
WRITE_FLAGS = frozenset({"-c", "-MD", "-MMD", "-MP"})
TEXT_FLAGS = ("-fdiagnostics-color=never", "-fno-diagnostics-show-caret")
# The compiles that run at the same time.
JOBS = 16
# The largest limit that a row of the list can give.
LIMIT_MAXIMUM = 10**12
# The characters of the error of a compile that a finding shows.
SHOWN_ERROR = 300


@dataclass(frozen=True, slots=True)
class LimitRow:
    """One row of test/layer/header-constexpr-ops.txt: a header relative to include/, its limit and its line."""

    header: str
    limit: int
    line: int


@dataclass(frozen=True, slots=True)
class Outcome:
    """The result of one compile of a header alone.

    `owners` holds the position that starts each evaluation over the limit.
    `others` holds the message of each other error.
    """

    passes: bool
    owners: tuple[str, ...]
    others: tuple[str, ...]


def finding(
    level: check_report.Level, path: str, line: int, rule: str, message: str, kind: str, entity: str = ""
) -> check_report.Finding:
    """Return one finding of a rule of the family."""
    return check_report.Finding(level, path, line, rule, message, kind, entity)


def c_identifier(text: str) -> str:
    """Return the name that string(MAKE_C_IDENTIFIER) of CMake gives a text.

    Each character that is not an ASCII letter, a digit or "_" becomes "_",
    and a "_" goes before a first digit.  Complexity: linear in the length of
    the text.
    """
    name = "".join(
        character if character.isascii() and (character.isalnum() or character == "_") else "_" for character in text
    )
    return f"_{name}" if name[:1].isdigit() else name


def unit_path(build: Path, header: str) -> Path:
    """Return the one-line unit of a header: BUILD/test/layer/sentinels/LAYER/IDENTIFIER.cpp."""
    return build.joinpath(*SENTINELS, header.split("/")[0], f"{c_identifier(header)}.cpp")


def syntax_argv(argv: Sequence[str], added: Sequence[str], swap: tuple[str, str] | None = None) -> list[str]:
    """Return a compile command as a syntax-only compile that writes no file.

    Args:
        argv: The compile command of one unit
        added: The words after -fsyntax-only
        swap: The source of the command and the source that the syntax-only compile reads, or None to keep the source

    Complexity: linear in the words of the command.
    """
    words: list[str] = []
    index = 0
    while index < len(argv):
        word = argv[index]
        if word in WRITE_FLAGS_WITH_VALUE:
            index += 2
            continue
        if word not in WRITE_FLAGS:
            words.append(swap[1] if swap is not None and word == swap[0] else word)
        index += 1
    return [*words, "-fsyntax-only", *added]


def last_limit(argv: Sequence[str]) -> int | None:
    """Return the value of the last -fconstexpr-ops-limit of a command, or None when it has none."""
    values = [word.removeprefix(LIMIT_FLAG) for word in argv if word.startswith(LIMIT_FLAG)]
    return int(values[-1]) if values and values[-1].isascii() and values[-1].isdigit() else None


def shown_position(path: str, directory: str, root: Path) -> str:
    """Return a path of a diagnostic relative to the root when it is in the tree, after each link resolves."""
    resolved = Path(os.path.realpath(Path(directory) / path))
    try:
        return resolved.relative_to(root.resolve()).as_posix()
    except ValueError:
        return resolved.as_posix()


def map_compiles[Item, Result](jobs: Sequence[Item], action: Callable[[Item], Result]) -> list[Result]:
    """Return the result of each job, with at most JOBS jobs at the same time, in the sequence of the jobs."""
    if not jobs:
        return []
    from concurrent.futures import ThreadPoolExecutor

    with ThreadPoolExecutor(max_workers=min(JOBS, len(jobs))) as pool:
        return list(pool.map(action, jobs))


def compile_alone(unit: compdb.CompileEntry, limit: int, root: Path) -> Outcome:
    """Compile the one-line unit of a header at one operation limit, and read its errors from the SARIF log.

    An evaluation with no constexpr context starts at its error.  A compiler
    that cannot start, or that runs for more than its time limit, gives one
    other error.
    """
    owners: list[str] = []
    others: list[str] = []
    with tempfile.TemporaryDirectory(prefix="header-constexpr-") as scratch:
        log_path = Path(scratch) / "unit.sarif"
        argv = [*syntax_argv(unit.argv(), [f"{LIMIT_FLAG}{limit}", *TEXT_FLAGS]), f"{sarif.SARIF_FLAG}{log_path}"]
        try:
            done = proc.run(proc.Command(argv, timeout_s=proc.TOOL_S, cwd=unit.directory))
        except proc.ToolFailed as problem:
            return Outcome(False, (), (str(problem),))
        try:
            log: sarif.Log | None = sarif.read(log_path)
        except (OSError, sarif.SarifError) as problem:
            log = None
            others.append(f"the compiler gives no SARIF log that the rule can read: {problem}")
    diagnostics = log.diagnostics if log is not None else ()
    tool = log.tool if log is not None else "cc1plus"
    for error in diagnostics:
        if not error.is_error_head:
            continue
        where = f"{shown_position(error.path, unit.directory, root)}:{error.line}" if error.path else tool
        if OPS_ERROR in error.message:
            starts = [line for line in error.contexts(0) if line.context is sarif.Context.CONSTEXPR and line.path]
            owners.append(
                f"{shown_position(starts[0].path, unit.directory, root)}:{starts[0].line}" if starts else where
            )
        else:
            kind = error.kind.value if error.kind is not None else "error"
            others.append(f"{where}: {kind}: {error.message}")
    if done.exit != 0 and not owners and not others:
        others.append(
            f"the compiler exits with {done.exit} and gives no error line: {' '.join(done.err.split())[:SHOWN_ERROR]}"
        )
    return Outcome(done.exit == 0, tuple(dict.fromkeys(owners)), tuple(others))


def list_rows(context: BuildContext, relative: str, rule: str) -> tuple[rows_grammar.Row, ...] | check_report.Finding:
    """Return the rows of a list of test/layer/, or the error of a list that is not there or does not read."""
    path = context.root / relative
    if not path.is_file():
        return finding(
            "error",
            relative,
            0,
            rule,
            "the list is not there.  test/layer/CMakeLists.txt reads it at each configure, and a configure with no "
            "list fails.  Put the list back",
            "list",
        )
    try:
        return rows_grammar.read(path, relative)
    except (rows_grammar.RowsError, OSError) as problem:
        return finding(
            "error", relative, getattr(problem, "line", 0), rule, f"the list does not read: {problem}", "list"
        )


def threshold_of(context: BuildContext) -> int | check_report.Finding:
    """Return the error threshold of the measured rule header-constexpr-ops, or the error of an incorrect threshold."""
    try:
        budget = context.budget(OPS_RULE)
    except meta_runner.NotApplicable as problem:
        return finding(
            "error",
            REGISTRY,
            0,
            OPS_RULE,
            f"{problem}.  Add the measured rule {OPS_RULE} with warn=LIMIT, error=LIMIT and unit=operations",
            "threshold",
        )
    if budget.error != int(budget.error) or budget.error < 1:
        return finding(
            "error",
            REGISTRY,
            0,
            OPS_RULE,
            f"the error threshold {budget.error} of {OPS_RULE} has a fraction.  Give a number of operations with no "
            f"fraction",
            "threshold",
        )
    return int(budget.error)


def limit_rows(
    context: BuildContext, rows: Sequence[rows_grammar.Row], threshold: int
) -> tuple[list[LimitRow], list[check_report.Finding]]:
    """Return the rows of test/layer/header-constexpr-ops.txt, and an error for each row that does not obey its shape.

    Complexity: linear in the rows.
    """
    found: list[LimitRow] = []
    problems: list[check_report.Finding] = []
    seen: set[str] = set()

    def refuse(row: rows_grammar.Row, message: str) -> None:
        """Add the error of one row."""
        problems.append(finding("error", LIST, row.line, OPS_RULE, message, "list", row.words[0]))

    for row in rows:
        header = row.words[0]
        try:
            limit = rows_grammar.number(row.cells[0], LIMIT_MAXIMUM, row, "limit") if len(row.cells) == 2 else -1
        except rows_grammar.RowsError:
            limit = -1
        if len(row.words) != 1 or limit < 0:
            refuse(row, "the row does not have its shape.  A row is `<layer>/<path>.h | limit | reason`")
        elif header.split("/")[0] not in LAYERS or not (context.root / INCLUDE / header).is_file():
            refuse(row, f"{header} is not a header in {INCLUDE}/.  Remove the row")
        elif not row.cells[1]:
            refuse(row, f"the row of {header} gives no reason.  Tell which evaluation makes the higher limit necessary")
        elif limit <= threshold:
            refuse(
                row,
                f"the limit {limit} of {header} is not more than the threshold {threshold} of {REGISTRY}.  "
                f"Remove the row",
            )
        elif header in seen:
            refuse(row, f"{header} has a row before this one.  Remove one of them")
        else:
            seen.add(header)
            found.append(LimitRow(header, limit, row.line))
    return found, problems


def headers_with_units(root: Path, not_standalone: Sequence[rows_grammar.Row]) -> list[str]:
    """Return each header of a layer that the not-standalone list does not name, relative to include/.

    Complexity: linear in the number of headers in include/.
    """
    listed = {row.words[0] for row in not_standalone}
    found: list[str] = []
    for layer in LAYERS:
        base = root / INCLUDE / layer
        for path in sorted(base.rglob("*.h")):
            header = path.relative_to(root / INCLUDE).as_posix()
            if header not in listed:
                found.append(header)
    return found


def ops_findings(context: BuildContext) -> list[check_report.Finding]:
    """Give each one-line unit with an incorrect limit, and each row of the list that is not correct or not necessary."""
    build = context.require_build()
    threshold = threshold_of(context)
    if isinstance(threshold, check_report.Finding):
        return [threshold]
    rows = list_rows(context, LIST, OPS_RULE)
    if isinstance(rows, check_report.Finding):
        return [rows]
    # A header of the not-standalone list has no one-line unit.  With no such list, each header has one.
    excluded = list_rows(context, NOT_STANDALONE, OPS_RULE) if (context.root / NOT_STANDALONE).is_file() else ()
    if isinstance(excluded, check_report.Finding):
        return [excluded]
    limited, found = limit_rows(context, rows, threshold)
    try:
        entries = context.entries()
    except compdb.CompdbError as problem:
        return [
            *found,
            finding(
                "error",
                context.shown(compdb.database_file(build)),
                0,
                OPS_RULE,
                f"the rule cannot read the one-line units: {problem}",
                "database",
            ),
        ]
    units = {os.path.normpath(entry.file): entry for entry in entries}
    limits = {row.header: row.limit for row in limited}
    chosen: dict[str, compdb.CompileEntry] = {}
    for header in headers_with_units(context.root, excluded):
        unit = units.get(os.path.normpath(unit_path(build, header)))
        expected = limits.get(header, threshold)
        if unit is None:
            found.append(
                finding(
                    "error",
                    f"{INCLUDE}/{header}",
                    0,
                    OPS_RULE,
                    f"the compile database holds no one-line unit {unit_path(build, header)} for this header, and "
                    f"no compile holds it to the operation limit.  Configure the build again",
                    "no-unit",
                    header,
                )
            )
            continue
        have = last_limit(unit.argv())
        if have != expected:
            found.append(
                finding(
                    "error",
                    f"{INCLUDE}/{header}",
                    0,
                    OPS_RULE,
                    f"the one-line unit of this header "
                    f"compiles with the operation limit {have}, and the limit is {expected}.  "
                    f"test/layer/CMakeLists.txt gives each one-line unit its limit",
                    "unit-limit",
                    header,
                )
            )
        chosen[header] = unit
    jobs = [(row, limit) for row in limited if row.header in chosen for limit in (threshold, row.limit)]
    outcomes = dict(
        zip(
            jobs,
            map_compiles(jobs, lambda job: compile_alone(chosen[job[0].header], job[1], context.root)),
            strict=True,
        )
    )
    for row in limited:
        if row.header in chosen:
            found.append(judged_row(row, threshold, outcomes[(row, threshold)], outcomes[(row, row.limit)]))
    return found


def judged_row(row: LimitRow, threshold: int, low: Outcome, high: Outcome) -> check_report.Finding:
    """Return the finding of one listed header from its compile at the threshold and at the limit of its row."""
    if not high.passes:
        needs = ", ".join(high.owners) or "; ".join(high.others[:2])
        return finding(
            "error",
            LIST,
            row.line,
            OPS_RULE,
            f"{row.header} does not compile alone at the limit "
            f"{row.limit} of its row: {needs}.  Make the evaluation lazy or smaller",
            "row-limit",
            row.header,
        )
    if low.passes:
        return finding(
            "error",
            LIST,
            row.line,
            OPS_RULE,
            f"{row.header} compiles alone at the threshold {threshold}.  The higher limit of its row is not "
            f"necessary.  Remove the row in this commit",
            "unneeded",
            row.header,
        )
    if low.others and not low.owners:
        return finding(
            "error",
            LIST,
            row.line,
            OPS_RULE,
            f"{row.header} does not compile alone at the threshold "
            f"{threshold}, and the cause is not the operation limit: {'; '.join(low.others[:2])}",
            "other-error",
            row.header,
        )
    return finding(
        "warning",
        LIST,
        row.line,
        OPS_RULE,
        f"{row.header} holds a constant evaluation of more than "
        f"{threshold} operations, which runs again in each unit that includes it: {', '.join(low.owners)}.  "
        f"Its row gives it the limit {row.limit}.  Make the evaluation a variable template or a member of a "
        f"template, and remove the row",
        "listed",
        row.header,
    )


def standalone_rows(
    context: BuildContext, rows: Sequence[rows_grammar.Row]
) -> tuple[list[rows_grammar.Row], list[check_report.Finding]]:
    """Return the rows of test/layer/crucible-not-standalone.txt, and an error for each row that is not correct.

    Complexity: linear in the rows.
    """
    found: list[rows_grammar.Row] = []
    problems: list[check_report.Finding] = []
    seen: set[str] = set()
    for row in rows:
        header = row.words[0]
        message = ""
        if len(row.words) != 1 or len(row.cells) != 1 or not row.cells[0]:
            message = "the row does not have its shape.  A row is `crucible/<path>.h | reason`, with a reason"
        elif header.split("/")[0] != CRUCIBLE_LAYER or not (context.root / INCLUDE / header).is_file():
            message = f"{header} is not a header under {INCLUDE}/{CRUCIBLE_LAYER}.  Remove the row"
        elif header in seen:
            message = f"{header} has a row before this one.  Remove one of them"
        if message:
            problems.append(finding("error", NOT_STANDALONE, row.line, STANDALONE_RULE, message, "list", header))
            continue
        seen.add(header)
        found.append(row)
    return found, problems


def sentinel_unit(entries: Sequence[compdb.CompileEntry]) -> compdb.CompileEntry | None:
    """Return the first compile of a unit of the target layer_sentinel_crucible, or None.

    Complexity: linear in the compiles.
    """
    for entry in entries:
        if entry.output is not None and SENTINEL_OBJECTS in Path(entry.output).parts:
            return entry
    return None


def standalone_findings(context: BuildContext) -> list[check_report.Finding]:
    """Give each header of the not-standalone list that compiles alone, and each row of the list that is not correct."""
    build = context.require_build()
    read = list_rows(context, NOT_STANDALONE, STANDALONE_RULE)
    if isinstance(read, check_report.Finding):
        return [read]
    rows, found = standalone_rows(context, read)
    try:
        unit = sentinel_unit(context.entries())
    except compdb.CompdbError as problem:
        return [
            *found,
            finding(
                "error",
                context.shown(compdb.database_file(build)),
                0,
                STANDALONE_RULE,
                f"the rule cannot read the compile database: {problem}",
                "no-sentinel",
            ),
        ]
    if unit is None:
        return [
            *found,
            finding(
                "error",
                context.shown(compdb.database_file(build)),
                0,
                STANDALONE_RULE,
                f"the compile database holds no unit of {SENTINEL_OBJECTS.removesuffix('.dir')}.  "
                f"Configure the build again",
                "no-sentinel",
            ),
        ]
    with tempfile.TemporaryDirectory(prefix="header-standalone-") as work:

        def compiles(item: tuple[int, rows_grammar.Row]) -> bool | str:
            """Return True when one listed header compiles alone, False when it does not, or the error of the run."""
            index, row = item
            alone = Path(work) / f"alone_{index}.cpp"
            alone.write_text(f"#include <{row.words[0]}>\n", encoding="utf-8")
            argv = syntax_argv(unit.argv(), [], (unit.file, str(alone)))
            try:
                return proc.run(proc.Command(argv, timeout_s=proc.TOOL_S, cwd=unit.directory)).exit == 0
            except proc.ToolFailed as problem:
                return str(problem)

        verdicts = map_compiles(list(enumerate(rows)), compiles)
    for row, verdict in zip(rows, verdicts, strict=True):
        header = row.words[0]
        if isinstance(verdict, str):
            found.append(
                finding(
                    "error",
                    NOT_STANDALONE,
                    row.line,
                    STANDALONE_RULE,
                    f"the compile of {INCLUDE}/{header} alone does not run: {verdict}",
                    "compile",
                    header,
                )
            )
        elif verdict:
            found.append(
                finding(
                    "error",
                    NOT_STANDALONE,
                    row.line,
                    STANDALONE_RULE,
                    f"{INCLUDE}/{header} compiles alone.  Remove its row in this commit.  The header then gets its "
                    f"sentinel again",
                    "standalone",
                    header,
                )
            )
    return found


# The keys are text constants, because assay.guard.engines.meta.runner reads the table with the ast module.
RULES = {
    "header-constexpr-ops": meta_runner.Member(ops_findings),
    "header-standalone": meta_runner.Member(standalone_findings),
}
