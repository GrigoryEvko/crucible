"""loop derive header-costs — measure the cost of each library header, and write the heavy and light rows.

usage: loop derive header-costs --measure BUILD_DIR [HEADER...] [--jobs N]
       loop derive header-costs --write
       loop derive header-costs --check

THE ROWS
    The rule table utils/rule-table.txt ends with the line MARKER.  A library
    header is a header that is not a file of the source root.  Each row after
    MARKER is a heavy row or a light row of a library header
    (assay.core.ruletable):

        heavy <HEADER> | INSTRUCTIONS G, LINES lines
        light <HEADER> | INSTRUCTIONS G, LINES lines
        heavy <HEADER> | unmeasured: REASON

    A unit that includes only the header compiles three times.  The
    instructions are the median of the user instructions of the three runs,
    in the unit G of the registry (10^9 instructions).  The hardware counter
    of assay.core.meter counts them (THE INSTRUCTION COUNT of that module).
    A count does not change with the load of the host.  The counter attaches
    to the thread that opens it.  Each thread of the pool of a measure then
    counts only its own compile.  Each run compiles with -fsyntax-only and with the flags of the reference
    unit of assay.loop.gauge_sample (test/foundation/test_row_hash.cpp) in
    the compile database.  The lines are the lines with text of the -E -P
    output of the same unit.

    A header is heavy when its instructions are more than the instructions of
    <meta> times the error threshold of the rule heavy-include of
    utils/registry.txt.  A header that does not compile alone with those
    flags is unmeasured.  It is heavy, because no measure shows that it is
    light.  The rule heavy-include of the quarantine plugin reads the rows
    (utils/tools/quarantine/include.cpp).  The plugin reads only the kind of
    a row, and a measure that moves no header between heavy and light then
    compiles nothing again.

THE MODES
    --measure   measure these headers:
                  * each header of the rows
                  * each library header that a header in include/ includes,
                    in a parse tree of assay.readers.tsast
                  * each HEADER of the command line, with no angle brackets.
                Then write the lines after MARKER again.  The first line is a
                comment line with the compiler, the host and the cost of an
                empty unit.  A host with no accurate instruction count
                measures nothing.
                The rows follow in the sequence of their headers.  The commit
                of the rows gives the date of the measure.  --jobs gives the
                number of compiles at one time.
    --write     write the kind of each row again from its measure and the
                threshold of the registry, and measure nothing.  `loop
                derive` runs it, and a change of the threshold then changes
                the kinds.
    --check     make sure that each row after MARKER is a heavy row or a
                light row, and that no such row is before MARKER.  Also make
                sure that the rule can read each measure, and that the kind
                of each row agrees with its measure.  It measures nothing.
                The rule derived-files (assay.guard.engines.meta.derived_files)
                reads the same findings (drift()).

Exit 0 when the mode did its work, and 1 when a check finds an error or a
measure fails.  Exit 2 on a usage error, and 3 when an input is missing.
"""

from __future__ import annotations

import os
import statistics
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import TYPE_CHECKING

from assay.core import proc, ruletable
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.exit import ExitCode
from assay.core.paths import REPO_ROOT, listing

if TYPE_CHECKING:
    from assay.loop import gauge_sample

PROGRAM = "loop derive header-costs"
# The rule of the registry that reads the findings of drift().
CHECK = "derived-files"
# The rule table of a tree, relative to its root.
TABLE = ruletable.TABLE.relative_to(REPO_ROOT)
# The line of the rule table after which this module writes each line.
MARKER = (
    "# The heavy and the light rows.  `python3 utils/bin/loop derive header-costs --measure BUILD_DIR` "
    "writes each line after this one."
)
# The rule of the registry with the error threshold that multiplies the instructions of REFERENCE.
RULE = "heavy-include"
# The header with the cost that sets the limit: the heaviest header that each layer can include.
REFERENCE = "<meta>"
UNMEASURED = "unmeasured"
HEAVY = "heavy"
LIGHT = "light"
INCLUDE = "include"
HEADER_SUFFIXES = (".h", ".hpp")
# The runs of each measure.  The median of three is the rule of the gauge.
MEASURE_RUNS = 3
MEASURE_JOBS = proc.DEFAULT_JOBS
# The instructions of one G, the unit of a measure (THE ROWS).
GIGA = 10**9
# The unit word of a measure cell.
UNIT = "G"


class CostError(ValueError):
    """The rule table, a measure of a row or the registry does not obey its format."""


class MeasureError(RuntimeError):
    """A compile of a measure cannot start, runs for longer than its time limit, or gives no accurate count."""


@dataclass(frozen=True, slots=True)
class Cost:
    """The measure of one header: its instructions in G and its lines, or no number and the reason."""

    header: str
    instructions: float | None
    lines: int | None
    reason: str

    def cell(self) -> str:
        """Return the measure cell of the row of the header."""
        if self.instructions is None:
            return f"{UNMEASURED}: {self.reason}"
        return f"{self.instructions:.3f} {UNIT}, {self.lines} lines"


@dataclass(frozen=True, slots=True)
class Costs:
    """The rule table of a tree divided at MARKER, and the measure of each heavy and light row.

    `head` is the text up to the marker line and with it, and `lines` is each line after it.  `costs` gives the
    measure of each header by its spelling with the angle brackets, and `rows` gives the row of each header.
    `problems` gives an error for each problem of THE ROWS, at its line of the table.  `cell_problems` is the part of
    `problems` that the measure cells give.  A measure writes each cell again, and it reads only the other problems.
    """

    head: str
    lines: tuple[str, ...]
    costs: dict[str, Cost]
    rows: dict[str, ruletable.HeaderCost]
    problems: tuple[check_report.Finding, ...]
    cell_problems: tuple[check_report.Finding, ...]


def kind_name(is_heavy_row: bool) -> str:
    """Return the kind of a row as the rule table spells it."""
    return HEAVY if is_heavy_row else LIGHT


def parse_cell(header: str, cell: str) -> Cost:
    """Read the measure cell of one row (THE ROWS).

    Raises:
        CostError: If the cell is no measure and no unmeasured reason
    """
    if cell.startswith(f"{UNMEASURED}:"):
        reason = cell[len(UNMEASURED) + 1 :].strip()
        if not reason:
            raise CostError(f"the unmeasured row of {header} gives no reason")
        return Cost(header, None, None, reason)
    words = cell.replace(",", " ").split()
    if len(words) != 4 or words[1] != UNIT or words[3] != "lines" or not ruletable.is_number(words[2]):
        raise CostError(
            f"the measure of {header} is {cell!r}.  A measure is 'INSTRUCTIONS {UNIT}, LINES lines' or "
            f"'{UNMEASURED}: REASON'"
        )
    try:
        instructions = float(words[0])
    except ValueError:
        raise CostError(f"the instructions of {header} are {words[0]!r}, and they must be a number") from None
    if not instructions >= 0.0:
        raise CostError(f"the instructions of {header} cannot be negative")
    return Cost(header, instructions, int(words[2]), "")


def threshold_of(root: Path) -> float:
    """Return the error threshold of the rule heavy-include of the registry of a tree.

    Raises:
        CostError: If the registry cannot be read or has no measured rule heavy-include
    """
    try:
        registry = rule_registry.load(root / rule_registry.REGISTRY_PATH)
    except (OSError, rows_grammar.RowsError) as problem:
        raise CostError(f"the registry cannot be read: {problem}") from None
    rule = registry.rules.get(RULE)
    if rule is None or rule.threshold is None:
        raise CostError(f"the registry has no measured rule {RULE}.  Give it warn=FACTOR, error=FACTOR and unit=x")
    return rule.threshold.error


def limit_of(costs: dict[str, Cost], threshold: float) -> float:
    """Return the limit of the instructions of a light header: the instructions of <meta> times the threshold.

    Raises:
        CostError: If no measured row gives <meta>
    """
    reference = costs.get(REFERENCE)
    if reference is None or reference.instructions is None:
        raise CostError(
            f"no measured row gives {REFERENCE}, and the rows have no limit.  Measure it: python3 utils/bin/loop "
            f"derive header-costs --measure BUILD_DIR"
        )
    return reference.instructions * threshold


def is_heavy(cost: Cost, limit: float) -> bool:
    """Return True when a header is heavy: its instructions are more than the limit, or no measure of it is available."""
    return cost.instructions is None or cost.instructions > limit


def row_text(cost: Cost, limit: float) -> str:
    """Return the line of the row of one header, with the kind that its measure gives."""
    return rows_grammar.render([kind_name(is_heavy(cost, limit)), cost.header], [cost.cell()])


def read_costs(root: Path) -> Costs:
    """Read the rule table of a tree, divide it at MARKER, and find each problem of THE ROWS.

    Complexity: linear in the rows of the table.

    Raises:
        CostError: If the table does not obey its format, or holds no marker line or more than one
        OSError: If the table cannot be read
    """
    path = root / TABLE
    text = rows_grammar.decode(path.read_bytes(), str(TABLE))
    try:
        table = ruletable.parse(text, str(TABLE))
    except ruletable.TableError as problem:
        raise CostError(str(problem)) from None
    lines = text.split("\n")
    places = [index for index, line in enumerate(lines) if line == MARKER]
    if len(places) != 1:
        raise CostError(f"{TABLE} holds the marker line {len(places)} times, and it must hold it one time: {MARKER}")
    first_line = places[0] + 2
    problems: list[check_report.Finding] = []
    for row in rows_grammar.parse(text, str(TABLE)):
        is_cost = row.words[0] in (HEAVY, LIGHT)
        if is_cost and row.line < first_line:
            message = (
                f"the {row.words[0]} row is before the marker line, and only `{PROGRAM}` writes such a row, after "
                f"the marker line"
            )
            problems.append(check_report.Finding("error", TABLE.as_posix(), row.line, CHECK, message))
        elif not is_cost and row.line >= first_line:
            message = (
                f"the {row.words[0]} row is after the marker line, and only a heavy row and a light row can be there"
            )
            problems.append(check_report.Finding("error", TABLE.as_posix(), row.line, CHECK, message))
    costs: dict[str, Cost] = {}
    rows: dict[str, ruletable.HeaderCost] = {}
    cells: list[check_report.Finding] = []
    for item in table.header_costs:
        rows[item.header] = item
        try:
            costs[item.header] = parse_cell(item.header, item.measure)
        except CostError as problem:
            cells.append(check_report.Finding("error", TABLE.as_posix(), item.line, CHECK, str(problem)))
    head = "\n".join(lines[: places[0] + 1]) + "\n"
    return Costs(
        head, tuple(line for line in lines[places[0] + 1 :] if line), costs, rows, (*problems, *cells), tuple(cells)
    )


def kind_problems(found: Costs, limit: float) -> list[check_report.Finding]:
    """Return an error for each row with a kind that does not agree with its measure and the limit."""
    return [
        check_report.Finding(
            "error",
            TABLE.as_posix(),
            found.rows[header].line,
            CHECK,
            f"the row of {header} is {kind_name(found.rows[header].is_heavy)}, and its measure {cost.cell()!r} makes "
            f"it {kind_name(is_heavy(cost, limit))} at the limit of {limit:.3f} {UNIT}.  Run: python3 utils/bin/loop "
            f"derive header-costs --write",
        )
        for header, cost in sorted(found.costs.items())
        if found.rows[header].is_heavy != is_heavy(cost, limit)
    ]


def drift(root: Path) -> list[check_report.Finding]:
    """Return an error for each problem of THE ROWS, or else for each row with a kind that its measure does not give.

    Complexity: linear in the rows of the table.

    Raises:
        CostError: If the table or the registry does not obey its format, or no measured row gives REFERENCE
        OSError: If the table or the registry cannot be read
        rows_grammar.RowsError: If the text of the table is not UTF-8
    """
    found = read_costs(root)
    if found.problems:
        return list(found.problems)
    return kind_problems(found, limit_of(found.costs, threshold_of(root)))


def checked(root: Path) -> ExitCode:
    """Make sure that the rows after MARKER obey THE ROWS, and write each problem.

    Returns:
        OK, FINDINGS when a row has a problem, or MISSING_INPUT when the table or the registry cannot be read
    """
    try:
        problems = drift(root)
    except (OSError, CostError, rows_grammar.RowsError) as problem:
        check_report.write_line(f"{PROGRAM}: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    for finding in problems:
        check_report.write_line(finding.text(), is_error=True)
    if problems:
        return ExitCode.FINDINGS
    check_report.write_line(f"{PROGRAM}: the rows agree with their measures")
    return ExitCode.OK


def store(root: Path, head: str, lines: list[str]) -> None:
    """Write the rule table of a tree again: the text up to the marker line, and the lines after it."""
    (root / TABLE).write_bytes((head + "".join(f"{line}\n" for line in lines)).encode("utf-8"))


def written(root: Path) -> ExitCode:
    """Write the kind of each row again from its measure and the threshold of the registry (THE MODES, --write).

    The comment lines after MARKER stay, and each row stays at its position.

    Returns:
        OK, FINDINGS when a row has a problem, or MISSING_INPUT when the table or the registry cannot be read
    """
    try:
        found = read_costs(root)
        limit = limit_of(found.costs, threshold_of(root)) if not found.problems else 0.0
    except (OSError, CostError, rows_grammar.RowsError) as problem:
        check_report.write_line(f"{PROGRAM}: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    if found.problems:
        for finding in found.problems:
            check_report.write_line(finding.text(), is_error=True)
        check_report.write_line(f"{PROGRAM}: the rows are not written while a row has a problem", is_error=True)
        return ExitCode.FINDINGS
    lines: list[str] = []
    for line in found.lines:
        rows = rows_grammar.parse(f"{line}\n", str(TABLE))
        lines.append(row_text(found.costs[rows[0].words[1]], limit) if rows else line)
    store(root, found.head, lines)
    changed = len(kind_problems(found, limit))
    check_report.write_line(f"{PROGRAM}: wrote {len(found.costs)} rows, and {changed} of them changed their kind")
    return ExitCode.OK


def included_headers(root: Path) -> set[str]:
    """Return each library header that a header in include/ includes, in each preprocessor arm.

    These includes name a header of the root, and the function ignores them:
      * a quoted include that resolves in the directory of the file or in include/
      * an angle include with a directory in include/ as its first directory
      * an angle include with a path that is the end of the path of a file that git holds.
    Complexity: linear in the bytes of the headers, and one read of the list of the files that git holds.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    # Only a measure reads the parse trees.  A check then imports no reader of C++.
    from assay.readers import tsast

    base = root / INCLUDE
    project_roots = frozenset(path.name for path in listing(base) if path.is_dir())
    headers = [
        path
        for path in listing(base, "**/*")
        if path.is_file() and path.suffix in HEADER_SUFFIXES and tsast.is_in_cpp_scope(path.relative_to(root))
    ]
    tracked = tsast.tracked_files(root)
    found: set[str] = set()
    for tree in tsast.parse(headers, strict=False):
        for node in tree.find("preproc_include"):
            path = node.child_by_field("path")
            if path is None or path.type not in ("system_lib_string", "string_literal"):
                continue
            spelled = tsast.prose_text(path).strip()
            body = spelled[1:-1].strip()
            if spelled.startswith('"') and ((Path(tree.path).parent / body).is_file() or (base / body).is_file()):
                continue
            parts = PurePosixPath(body).parts
            if len(parts) > 1 and parts[0] in project_roots:
                continue
            if any(path_text.endswith("/" + body) for path_text in tracked):
                continue
            found.add(f"<{body}>")
    return found


def has_accurate_count() -> bool:
    """Return True when the host gives an accurate count of the user instructions of a child (THE ROWS)."""
    from assay.core import meter

    probe = meter.open_instruction_counter()
    return probe is not None and meter.read_instruction_counter(probe) is not None


def counted_run(argv: list[str], directory: Path) -> tuple[int | None, int]:
    """Run one compile in the calling thread, and return its user instructions or None, and its status.

    The count is None when the host gives no accurate count.  The counter of assay.core.meter attaches to the
    calling thread, and the count then holds only this compile (THE ROWS).

    Raises:
        MeasureError: If the compiler cannot start, or the run is longer than the limit TOOL_S of assay.core.proc
    """
    from assay.core import meter

    counter = meter.open_instruction_counter()
    try:
        completed = proc.run(
            proc.Command(
                argv, timeout_s=proc.TOOL_S, cwd=directory, stdout=proc.Output.DISCARD, stderr=proc.Output.DISCARD
            )
        )
    except proc.ToolFailed as problem:
        if counter is not None:
            meter.read_instruction_counter(counter)
        raise MeasureError(str(problem)) from None
    count = meter.read_instruction_counter(counter) if counter is not None else None
    return count, completed.exit


def first_error(command: gauge_sample.Command, flags: list[str], source: Path) -> str:
    """Compile a unit with a status that is not 0 again with a SARIF log, and return the message of its first error.

    The first error is the first error head of the log.  The option of the head follows the message in brackets, as in
    the text output of GCC.  The return value is an empty text when the log holds no error head.  A log that cannot
    be read gives a text that tells it, and the measure of the other headers continues.  Only this compile writes a
    log.  The counted runs of a unit then stay the same (assay.readers.sarif).

    Raises:
        MeasureError: If the compiler cannot start, or the run is longer than its time limit
    """
    from assay.readers import sarif

    with tempfile.TemporaryDirectory(prefix="header-costs-log-") as scratch:
        log_path = Path(scratch) / "unit.sarif"
        argv = [command.compiler, *flags, "-fsyntax-only", f"{sarif.SARIF_FLAG}{log_path}", str(source)]
        try:
            proc.run(
                proc.Command(
                    argv,
                    timeout_s=proc.TOOL_S,
                    cwd=command.directory,
                    stdout=proc.Output.DISCARD,
                    stderr=proc.Output.DISCARD,
                )
            )
        except proc.ToolFailed as problem:
            raise MeasureError(str(problem)) from None
        try:
            log = sarif.read(log_path)
        except (OSError, sarif.SarifError) as problem:
            return f"its SARIF log cannot be read ({problem})"
    head = next((record for record in log.diagnostics if record.is_error_head), None)
    if head is None:
        return ""
    return f"{head.message} [{head.option}]" if head.option else head.message


def measure_header(command: gauge_sample.Command, source: Path) -> tuple[float, int, int, str]:
    """Measure one unit: the median instructions of its runs in G, its lines, the status and the first error message.

    Raises:
        MeasureError: If the compiler cannot start, a run is longer than its time limit, or a run with the status 0
            gives no accurate count
    """
    flags = [*command.flags, "-fdiagnostics-color=never"]
    runs = [
        counted_run([command.compiler, *flags, "-fsyntax-only", str(source)], command.directory)
        for _ in range(MEASURE_RUNS)
    ]
    failed = next((status for _count, status in runs if status != 0), None)
    if failed is not None:
        return 0.0, 0, failed, first_error(command, flags, source)
    counts = [count for count, _status in runs if count is not None]
    if len(counts) != len(runs):
        raise MeasureError(f"a compile of {source.name} gave no accurate count of its user instructions")
    instructions = statistics.median(counts) / GIGA
    try:
        done = proc.run(
            proc.Command(
                [command.compiler, *flags, "-E", "-P", str(source)], timeout_s=proc.TOOL_S, cwd=command.directory
            )
        )
    except proc.ToolFailed as problem:
        raise MeasureError(str(problem)) from None
    return instructions, sum(1 for line in done.out.splitlines() if line.strip()), done.exit, ""


def host_text(compiler: str) -> str:
    """Return the path of the compiler after its links, the processor and the threads of the host, for the comment line.

    The path starts with ~ for the home directory.  Then the line names no account of the host.
    """
    home = os.path.expanduser("~")
    path = os.path.realpath(compiler)
    shown = "~" + path[len(home) :] if path.startswith(home + os.sep) else path
    model = next(
        (
            line.split(":", 1)[1].strip()
            for line in Path("/proc/cpuinfo").read_text(encoding="utf-8", errors="replace").splitlines()
            if line.startswith("model name")
        ),
        os.uname().machine,
    )
    return f"{shown} on {model} with {os.cpu_count()} threads"


def measured(root: Path, build_dir: Path, extra: list[str], jobs: int) -> ExitCode:
    """Measure each header of THE MODES, --measure, and write the lines after MARKER again.

    Complexity: three -fsyntax-only runs and one -E run for each header, JOBS of them at the same time.

    Returns:
        OK, FINDINGS when the rows have a problem or a measure fails, or MISSING_INPUT when an input is missing
    """
    from concurrent.futures import ThreadPoolExecutor

    from assay.loop import gauge_sample

    reference_unit = gauge_sample.HEADER_REFERENCE
    try:
        found = read_costs(root)
        threshold = threshold_of(root)
        database = gauge_sample.load_database(build_dir)
    except (OSError, CostError, rows_grammar.RowsError, gauge_sample.SampleError) as problem:
        check_report.write_line(f"{PROGRAM}: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
    placement = [problem for problem in found.problems if problem not in found.cell_problems]
    if placement:
        for finding in placement:
            check_report.write_line(finding.text(), is_error=True)
        return ExitCode.FINDINGS
    reference_row = gauge_sample.find_row(database, reference_unit)
    if reference_row is None:
        check_report.write_line(
            f"{PROGRAM}: the compile database of {build_dir} has no single row of {reference_unit}", is_error=True
        )
        return ExitCode.MISSING_INPUT
    if not has_accurate_count():
        check_report.write_line(
            f"{PROGRAM}: this host gives no accurate count of the user instructions of a compile (THE INSTRUCTION "
            f"COUNT of assay.core.meter), and no header gets a measure.  Measure on a host with a hardware counter",
            is_error=True,
        )
        return ExitCode.MISSING_INPUT
    reference = gauge_sample.command_of(reference_row)
    headers = sorted({*found.rows, *included_headers(root), REFERENCE, *(f"<{name}>" for name in extra)})
    with tempfile.TemporaryDirectory(prefix="header-costs-") as scratch_name:
        scratch = Path(scratch_name)
        sources = [scratch / "empty.cpp"]
        sources[0].write_text("\n", encoding="utf-8")
        for index, header in enumerate(headers):
            sources.append(scratch / f"unit_{index}.cpp")
            sources[-1].write_text(f"#include {header}\n", encoding="utf-8")
        try:
            with ThreadPoolExecutor(max_workers=jobs) as pool:
                results = list(pool.map(lambda source: measure_header(reference, source), sources))
        except MeasureError as problem:
            check_report.write_line(f"{PROGRAM}: a measure failed: {problem}", is_error=True)
            return ExitCode.FINDINGS
    costs: dict[str, Cost] = {}
    for header, (instructions, lines, status, first) in zip(headers, results[1:], strict=True):
        if status != 0:
            detail = f": {first}" if first else ""
            costs[header] = Cost(
                header, None, None, f"it does not compile alone with the flags of {reference_unit}{detail}"
            )
        else:
            costs[header] = Cost(header, instructions, lines, "")
    try:
        limit = limit_of(costs, threshold)
    except CostError as problem:
        check_report.write_line(f"{PROGRAM}: {problem}", is_error=True)
        return ExitCode.FINDINGS
    note = (
        f"# Measured in user instructions with {host_text(reference.compiler)}.  An empty translation unit "
        f"costs {results[0][0]:.3f} {UNIT}."
    )
    store(root, found.head, [note, *(row_text(costs[header], limit) for header in headers)])
    heavy = sum(is_heavy(cost, limit) for cost in costs.values())
    check_report.write_line(
        f"{PROGRAM}: wrote {len(costs)} rows, {heavy} of them heavy, at the limit of {limit:.3f} {UNIT}, to {TABLE}"
    )
    return ExitCode.OK


def main(argv: list[str]) -> int:
    """Run one mode (THE MODES).

    Args:
        argv: The words after `loop derive header-costs`

    Returns:
        The exit code
    """
    from assay.core import cli

    parser = cli.parser(
        PROGRAM, "measure the cost of each library header, and write the heavy and light rows of the rule table"
    )
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument(
        "--measure",
        metavar="BUILD_DIR",
        type=Path,
        help="measure each header with the flags of the reference unit, and write the rows",
    )
    modes.add_argument("--write", action="store_true", help="write the kind of each row again from its measure")
    modes.add_argument("--check", action="store_true", help="make sure that each row agrees with its measure")
    parser.add_argument(
        "headers", nargs="*", metavar="HEADER", help="with --measure: more headers to measure, with no angle brackets"
    )
    parser.add_argument("--jobs", type=int, default=MEASURE_JOBS, help="with --measure: the compiles at one time")
    try:
        options = parser.parse_args(argv)
    except SystemExit as stop:
        return ExitCode.OK if stop.code == 0 else ExitCode.USAGE
    if options.headers and options.measure is None:
        check_report.write_line(f"{PROGRAM}: a HEADER goes only with --measure", is_error=True)
        return ExitCode.USAGE
    if options.jobs < 1:
        check_report.write_line(f"{PROGRAM}: --jobs takes a number of 1 or more", is_error=True)
        return ExitCode.USAGE
    if options.check:
        return checked(REPO_ROOT)
    if options.write:
        return written(REPO_ROOT)
    from assay.readers import tsast

    try:
        return measured(REPO_ROOT, options.measure.resolve(), options.headers, options.jobs)
    except tsast.KitMissing as problem:
        check_report.write_line(f"{PROGRAM}: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
