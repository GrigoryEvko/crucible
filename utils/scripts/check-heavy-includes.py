#!/usr/bin/env python3
"""check-heavy-includes — a header under include/ does not include a heavy standard header.

Each translation unit that includes a header compiles each header that it
includes again.  A standard header such as <chrono> or <thread> costs up to
1 s of front-end time alone, so one include of it in a base header costs that
time in every file of the tree.  CLAUDE.md §XV "Compile time" rule 4 states
the rule: a builtin before a header.

THE COST TABLE
    utils/scripts/header-costs.txt gives the measured cost of each standard
    and system header, one row each:

        <header> | seconds | preprocessed lines
        <header> | unmeasured | reason

    The seconds are the median of three runs of the user plus system CPU
    time of a translation unit that includes only that header, with
    -fsyntax-only and the flags of a reference unit of the compile
    database.  The lines are the non-empty lines of the -E -P output of the
    same unit.  --measure BUILD_DIR writes the table again.  A row is
    `unmeasured` when the header does not compile alone with those flags,
    for example a BPF header or a header of another architecture.  A row
    changes only with a measurement.

THE THRESHOLD
    Each layer may include <meta>, because reflection needs it.  <meta> and
    the containers share one core of libstdc++, and <meta> costs the most of
    that core.  A header is heavy when its cost alone is more than the cost
    alone of <meta>, times the factor of the row heavy-include of
    utils/scripts/budgets.txt.  The two costs come from one measurement, so
    a change of the host load moves the two costs together.  An unmeasured
    header is heavy too, because the check cannot show that it is light.

WHAT THE CHECK READS
    The parse tree of each header under include/ (utils/scripts/tsast.py).
    Each include directive names one header:
      * An angle include whose first component is a directory under include/
        names a project header.
      * A quoted include that resolves beside the file or under include/
        names a project header.  Another quoted include reads as an angle
        include.
      * An angle include that the table lists names that standard or system
        header.  An angle include that the table does not list names a
        project header when a tracked file of the repository ends with its
        path, for example <bench_harness.h>.  Else the check cannot tell
        its cost, and that is an error: measure the header.
      * A computed include (`#include MACRO`) is an error, because the check
        cannot see the header that it names.
    Each arm of a preprocessor conditional counts, because the kit does not
    preprocess.

THE LEDGER
    utils/scripts/heavy-includes-ledger.txt holds two kinds of row.
      path | <header>
          An include of a heavy header that the tree holds today.  Each run
          gives a warning for it, so the debt stays visible.  An include of
          a heavy header with no row is an error.  A row whose include is
          gone, or whose header is no longer heavy, is an error: run
          --write in the same commit.
      keep | path | <header> | reason
          A header that owns a heavy header on purpose, for example the one
          door of a system facility.  It gives no finding.  A keep row whose
          include is gone or is no longer heavy is an error.

Usage
    check-heavy-includes.py [--warnings-dir DIR]   compare the tree with the ledger
    check-heavy-includes.py --write                write the ledger rows again from the tree
    check-heavy-includes.py --measure BUILD_DIR [HEADER...]
                                                   measure each header and write the table again
    check-heavy-includes.py --self-test            plant each case in a scratch tree

Exit 0 with no error, 1 on an error, 2 on a usage error or a failed
self-test, 3 when the tree-sitter kit is not installed.
"""

from __future__ import annotations

import argparse
import contextlib
import datetime
import importlib.util
import io
import os
import statistics
import subprocess
import sys
import tempfile
from collections.abc import Iterator
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
import tsast  # noqa: E402

CHECK = "heavy-include"
SCRIPT = "utils/scripts/check-heavy-includes.py"
TABLE = "utils/scripts/header-costs.txt"
LEDGER = "utils/scripts/heavy-includes-ledger.txt"
TU_SAMPLE = "utils/scripts/tu-sample.py"
INCLUDE = "include"
HEADER_SUFFIXES = (".h", ".hpp")
SEPARATOR = " | "
UNMEASURED = "unmeasured"
# The header whose cost sets the limit: the heaviest header that each layer may include.
REFERENCE = "<meta>"
# The runs of each measurement.  The median of three is the rule of the gauge.
MEASURE_RUNS = 3
MEASURE_JOBS = 16

TABLE_HEADER = (
    "# utils/scripts/header-costs.txt — the measured cost of each standard and system header.\n"
    "# utils/scripts/check-heavy-includes.py reads this table, and --measure BUILD_DIR writes it.\n"
    "#\n"
    "# A row:  <header> | seconds | preprocessed lines\n"
    "#         <header> | unmeasured | reason\n"
    "#\n"
    "# The seconds are the median of three runs of the user plus system CPU time of a translation\n"
    "# unit that includes only the header, with -fsyntax-only and the flags of {reference}.\n"
    "# The lines are the non-empty lines of the -E -P output of the same unit.  A header is heavy when\n"
    "# it costs more than <meta> times the factor of the row heavy-include of utils/scripts/budgets.txt.\n"
    "#\n"
    "# Measured {date} on {host}.  An empty translation unit costs {empty:.2f} s.\n"
)
LEDGER_HEADER = (
    "# utils/scripts/heavy-includes-ledger.txt — the includes of heavy standard headers in the headers\n"
    "# under include/.  utils/scripts/check-heavy-includes.py reads this ledger.\n"
    "#\n"
    "# A header that each translation unit compiles must not include a heavy header for one function.\n"
    "# utils/scripts/header-costs.txt gives the cost of each header.  A header is heavy when it costs\n"
    "# more than <meta> times the factor of the row heavy-include of utils/scripts/budgets.txt.\n"
    "#\n"
    "# A count row:  path | <header>\n"
    "#   An include that the tree holds today.  Each run gives a warning for it.  When you remove the\n"
    "#   include, run python3 utils/scripts/check-heavy-includes.py --write in the same commit.\n"
    "#\n"
    "# A keep row:   keep | path | <header> | reason\n"
    "#   A header that owns a heavy header on purpose, for example the one door of a system facility.\n"
    "#   The reason is mandatory.\n"
)


class TableError(ValueError):
    """The cost table does not exist or holds a malformed row."""


@dataclass(frozen=True)
class Cost:
    """One row of the cost table."""

    header: str
    seconds: float | None
    lines: int | None
    reason: str


@dataclass(frozen=True)
class Include:
    """One include directive of a header under include/."""

    path: str
    line: int
    target: str
    kind: str


@dataclass(frozen=True)
class Keep:
    """One keep row of the ledger."""

    path: str
    target: str
    reason: str
    line: int


@dataclass
class Ledger:
    """The rows of the ledger, and one finding for each malformed row."""

    rows: dict[tuple[str, str], int]
    keeps: list[Keep]
    malformed: list[check_report.Finding]


def read_table(path: Path) -> dict[str, Cost]:
    """Read the cost table.

    Args:
        path: The table file

    Returns:
        Each row, by its header spelling with the angle brackets

    Raises:
        TableError: If the file does not exist, or a row is malformed or repeated
    """
    if not path.is_file():
        raise TableError(f"{TABLE} does not exist.  Run: python3 {SCRIPT} --measure BUILD_DIR")
    costs: dict[str, Cost] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        cells = [cell.strip() for cell in entry.split(SEPARATOR)]
        header = cells[0]
        if len(cells) != 3 or not (header.startswith("<") and header.endswith(">") and len(header) > 2):
            raise TableError(f"{TABLE}:{number}: a row is `<header>{SEPARATOR}seconds{SEPARATOR}lines` or "
                             f"`<header>{SEPARATOR}{UNMEASURED}{SEPARATOR}reason`, and this row is: {entry}")
        if header in costs:
            raise TableError(f"{TABLE}:{number}: {header} has a row before this one.")
        if cells[1] == UNMEASURED:
            if not cells[2]:
                raise TableError(f"{TABLE}:{number}: the unmeasured row of {header} gives no reason.")
            costs[header] = Cost(header, None, None, cells[2])
            continue
        try:
            seconds, lines = float(cells[1]), int(cells[2])
        except ValueError:
            raise TableError(f"{TABLE}:{number}: the cost of {header} must be a number of seconds and a number "
                             f"of lines, and it is {cells[1]!r} and {cells[2]!r}.") from None
        if not seconds >= 0.0 or lines < 0:
            raise TableError(f"{TABLE}:{number}: the cost of {header} cannot be negative.")
        costs[header] = Cost(header, seconds, lines, "")
    return costs


def heavy_limit(costs: dict[str, Cost], factor: float) -> float:
    """Return the cost in seconds above which a header is heavy: the cost of <meta> times the factor.

    Raises:
        TableError: If the table has no measured row of <meta>
    """
    reference = costs.get(REFERENCE)
    if reference is None or reference.seconds is None:
        raise TableError(f"{TABLE} has no measured row of {REFERENCE}, so the check has no limit.  Run: "
                         f"python3 {SCRIPT} --measure BUILD_DIR")
    return reference.seconds * factor


def is_heavy(cost: Cost, limit: float) -> bool:
    """Say whether a header is heavy: its cost is above the limit, or nobody could measure it."""
    return cost.seconds is None or cost.seconds > limit


def project_roots(root: Path) -> frozenset[str]:
    """Return the name of each directory directly under include/."""
    base = root / INCLUDE
    return frozenset(path.name for path in base.iterdir() if path.is_dir()) if base.is_dir() else frozenset()


def headers_of(root: Path) -> list[Path]:
    """Return each header under include/ that the C++ scans read, sorted."""
    base = root / INCLUDE
    if not base.is_dir():
        return []
    return sorted(path for path in base.rglob("*") if path.is_file() and path.suffix in HEADER_SUFFIXES
                  and tsast.is_in_cpp_scope(path.relative_to(root)))


def includes_of(root: Path, rel: str, tree: tsast.Tree, roots: frozenset[str]) -> Iterator[Include]:
    """Yield each include directive of one header that does not name a project header under include/.

    Complexity: linear in the number of nodes of the file.

    Args:
        root: The repository root
        rel: The header, relative to the root
        tree: The parse tree of the header
        roots: The directories under include/

    Yields:
        An Include of kind "angle" for a header that a search path finds, or "computed" for an
        include whose header the check cannot see
    """
    for node in tree.find("preproc_include"):
        path = node.child_by_field("path")
        if path is None:
            continue
        if path.type not in ("system_lib_string", "string_literal"):
            yield Include(rel, node.line, tsast.lexeme(path), "computed")
            continue
        spelled = tsast.prose_text(path).strip()
        body = spelled[1:-1].strip()
        if spelled.startswith('"'):
            beside = (root / rel).parent / body
            if beside.is_file() or (root / INCLUDE / body).is_file():
                continue
        if PurePosixPath(body).parts[:1] and PurePosixPath(body).parts[0] in roots \
                and len(PurePosixPath(body).parts) > 1:
            continue
        yield Include(rel, node.line, f"<{body}>", "angle")


def is_tracked_header(target: str, tracked: list[str]) -> bool:
    """Say whether an angle include names a tracked file of the repository by the end of its path."""
    tail = "/" + target[1:-1]
    return any(path.endswith(tail) for path in tracked)


@dataclass
class Scan:
    """What the check reads from one tree."""

    findings: dict[tuple[str, str], Include]
    errors: list[check_report.Finding]
    all_pairs: set[tuple[str, str]]


def scan(root: Path, costs: dict[str, Cost], limit: float) -> Scan:
    """Read each header under include/ and find each include of a heavy header.

    Complexity: linear in the total size of the headers, plus one pass over the tracked files for
    each angle include that the table does not list.

    Args:
        root: The repository root
        costs: The cost table
        limit: The cost in seconds above which a header is heavy

    Returns:
        The include of each heavy header by (path, header), the errors, and each (path, header) of
        an include that the table lists

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    roots = project_roots(root)
    result = Scan({}, [], set())
    tracked: list[str] | None = None
    for tree in tsast.parse(headers_of(root), strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        if tree.diagnostic is not None:
            result.errors.append(check_report.Finding(
                "error", rel, 0, CHECK, f"the parser cannot read this header, so the check cannot see its "
                                        f"includes.  {tree.diagnostic.strip()}"))
            continue
        for include in includes_of(root, rel, tree, roots):
            if include.kind == "computed":
                result.errors.append(check_report.Finding(
                    "error", rel, include.line, CHECK,
                    f"the computed include `{include.target}` hides the header that it names, so the check "
                    f"cannot tell its cost.  Write the header path in the include directive."))
                continue
            cost = costs.get(include.target)
            if cost is None:
                if tracked is None:
                    tracked = tsast.tracked_files(root)
                if is_tracked_header(include.target, tracked):
                    continue
                result.errors.append(check_report.Finding(
                    "error", rel, include.line, CHECK,
                    f"{TABLE} has no row for {include.target}, so the check cannot tell its cost.  Measure "
                    f"it: python3 {SCRIPT} --measure BUILD_DIR {include.target[1:-1]}"))
                continue
            result.all_pairs.add((rel, include.target))
            if is_heavy(cost, limit):
                result.findings.setdefault((rel, include.target), include)
    return result


def read_ledger(path: Path) -> Ledger:
    """Read the ledger.

    Args:
        path: The ledger file.  A missing ledger has no row

    Returns:
        The count rows by (path, header) with their line, the keep rows, and the malformed rows
    """
    ledger = Ledger({}, [], [])
    if not path.is_file():
        return ledger
    seen: set[tuple[str, str]] = set()
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        cells = [cell.strip() for cell in (entry + " ").split(SEPARATOR)]
        is_keep = cells[0] == "keep"
        body = cells[1:] if is_keep else cells
        problem = ""
        if len(body) != (3 if is_keep else 2) or not body[0].startswith(f"{INCLUDE}/") \
                or not (body[1].startswith("<") and body[1].endswith(">")):
            problem = (f"the row is malformed: {entry}.  A count row is `path{SEPARATOR}<header>`, and a keep "
                       f"row is `keep{SEPARATOR}path{SEPARATOR}<header>{SEPARATOR}reason`.")
        elif is_keep and not body[2]:
            problem = "the keep row gives no reason.  Say why the header owns this heavy header on purpose."
        elif (body[0], body[1]) in seen:
            problem = f"a row for {body[0]} and {body[1]} comes before this one.  Remove one of them."
        if problem:
            ledger.malformed.append(check_report.Finding("error", LEDGER, number, CHECK, problem))
            continue
        seen.add((body[0], body[1]))
        if is_keep:
            ledger.keeps.append(Keep(body[0], body[1], body[2], number))
        else:
            ledger.rows[(body[0], body[1])] = number
    return ledger


def evaluate(root: Path, table: Path, ledger_path: Path, factor: float) -> list[check_report.Finding]:
    """Compare the tree with the ledger and return each finding.

    Args:
        root: The repository root
        table: The cost table
        ledger_path: The ledger file
        factor: The factor of the cost of <meta> that gives the limit

    Returns:
        The findings: a warning for each count row that the tree holds, an error for each other
        problem
    """
    try:
        costs = read_table(table)
        limit = heavy_limit(costs, factor)
    except TableError as exc:
        return [check_report.Finding("error", TABLE, 0, CHECK, str(exc))]
    found = scan(root, costs, limit)
    ledger = read_ledger(ledger_path)
    findings = list(found.errors) + list(ledger.malformed)
    kept = {(keep.path, keep.target) for keep in ledger.keeps}
    for (path, target), include in sorted(found.findings.items()):
        cost = costs[target]
        shown = (f"costs {cost.seconds:.2f} s alone, more than the limit of {limit:.2f} s that {REFERENCE} gives"
                 if cost.seconds is not None else f"has no measured cost ({cost.reason})")
        if (path, target) in kept:
            continue
        if (path, target) in ledger.rows:
            findings.append(check_report.Finding(
                "warning", path, include.line, CHECK,
                f"the header includes {target}, which {shown}.  Each translation unit that includes the "
                f"header pays it.  Use a builtin or a lower header, and remove the row from {LEDGER}."))
            continue
        findings.append(check_report.Finding(
            "error", path, include.line, CHECK,
            f"the header includes {target}, which {shown}.  Each translation unit that includes the header "
            f"pays it.  Use a builtin or a lower header, or move the code that needs {target} to a source "
            f"file.  A header that owns {target} on purpose gets a keep row with its reason in {LEDGER}."))
    for (path, target), number in sorted(ledger.rows.items(), key=lambda item: item[1]):
        if (path, target) not in found.findings:
            why = "is no longer heavy" if (path, target) in found.all_pairs else "no longer holds the include"
            findings.append(check_report.Finding(
                "error", LEDGER, number, CHECK,
                f"the row names {path} and {target}, and the header {why}.  Regenerate the ledger in the same "
                f"commit: python3 {SCRIPT} --write"))
    for keep in ledger.keeps:
        if (keep.path, keep.target) not in found.findings:
            why = "is no longer heavy" if (keep.path, keep.target) in found.all_pairs else \
                "no longer holds the include"
            findings.append(check_report.Finding(
                "error", LEDGER, keep.line, CHECK,
                f"the keep row names {keep.path} and {keep.target}, and the header {why}.  Remove the row."))
    return findings


def factor_of(budgets: Path) -> float:
    """Return the factor of the cost of <meta> that gives the limit, from the budget table.

    Raises:
        ValueError: If the table is malformed or has no row for this check
    """
    rows = check_report.read_budgets(budgets)
    if CHECK not in rows:
        raise ValueError(f"{budgets} has no row {CHECK}.  Add `{CHECK} | factor | factor | unit | meaning`.")
    return rows[CHECK].error


def check(root: Path, table: Path, ledger_path: Path, budgets: Path, warnings_dir: Path | None) -> int:
    """Run the check and print each finding.

    Returns:
        1 when one finding is an error, else 0
    """
    try:
        factor = factor_of(budgets)
    except ValueError as exc:
        findings = [check_report.Finding("error", "utils/scripts/budgets.txt", 0, CHECK, str(exc))]
    else:
        findings = evaluate(root, table, ledger_path, factor)
    code = check_report.emit(findings, CHECK, warnings_dir)
    warnings = sum(found.level == "warning" for found in findings)
    print(f"check-heavy-includes: {warnings} include(s) of a heavy header in the ledger, "
          f"{len(findings) - warnings} error(s).", file=sys.stderr)
    return code


def write(root: Path, table: Path, ledger_path: Path, budgets: Path) -> int:
    """Write the count rows of the ledger again from the tree, and keep each keep row.

    Returns:
        0 when the ledger is written, 1 when an error of the tree, the table or a row stops the write
    """
    try:
        costs = read_table(table)
        limit = heavy_limit(costs, factor_of(budgets))
    except ValueError as exc:
        print(f"check-heavy-includes: {exc}", file=sys.stderr)
        return 1
    found = scan(root, costs, limit)
    ledger = read_ledger(ledger_path)
    if found.errors or ledger.malformed:
        for item in found.errors + ledger.malformed:
            print(item.text(), file=sys.stderr)
        print("check-heavy-includes: --write does not write the ledger while the tree or the ledger has an "
              "error.", file=sys.stderr)
        return 1
    kept = {(keep.path, keep.target) for keep in ledger.keeps}
    rows = [f"{path}{SEPARATOR}{target}\n" for path, target in sorted(found.findings) if (path, target) not in kept]
    keeps = [f"keep{SEPARATOR}{keep.path}{SEPARATOR}{keep.target}{SEPARATOR}{keep.reason}\n"
             for keep in sorted(ledger.keeps, key=lambda keep: (keep.path, keep.target))]
    ledger_path.write_text(LEDGER_HEADER + "\n" + "".join(rows) + ("\n" + "".join(keeps) if keeps else ""),
                           encoding="utf-8")
    print(f"check-heavy-includes: ledger written with {len(rows)} count row(s) and {len(keeps)} keep row(s).",
          file=sys.stderr)
    return 0


def load_tu_sample(root: Path) -> object:
    """Import utils/scripts/tu-sample.py, whose name is not a module name, for its compile helpers."""
    spec = importlib.util.spec_from_file_location("tu_sample", root / TU_SAMPLE)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"{TU_SAMPLE} cannot be imported.")
    module = importlib.util.module_from_spec(spec)
    sys.modules["tu_sample"] = module
    spec.loader.exec_module(module)
    return module


def measure(root: Path, build_dir: Path, extra: list[str], table: Path) -> int:
    """Measure each header of the table, of the includes under include/ and of the command line, and write the table.

    Complexity: three -fsyntax-only runs and one -E run for each header, sixteen at the same time.

    Args:
        root: The repository root
        build_dir: A configured build directory with compile_commands.json
        extra: More headers to measure, as an include directive names them, without brackets
        table: The table file to write

    Returns:
        0 when the table is written, 1 when the reference unit is missing
    """
    sample = load_tu_sample(root)
    database = sample.load_database(build_dir / "compile_commands.json")  # type: ignore[attr-defined]
    reference_row = sample.find_row(database, sample.HEADER_REFERENCE)  # type: ignore[attr-defined]
    if reference_row is None:
        print(f"check-heavy-includes: {build_dir}/compile_commands.json has no row of "
              f"{sample.HEADER_REFERENCE}.", file=sys.stderr)  # type: ignore[attr-defined]
        return 1
    reference = sample.command_of(reference_row)  # type: ignore[attr-defined]
    wanted: set[str] = set()
    with contextlib.suppress(TableError):
        wanted.update(read_table(table))
    roots = project_roots(root)
    tracked = tsast.tracked_files(root)
    for tree in tsast.parse(headers_of(root), strict=False):
        rel = Path(tree.path).relative_to(root).as_posix()
        for include in includes_of(root, rel, tree, roots):
            if include.kind == "angle" and (include.target in wanted or not is_tracked_header(include.target,
                                                                                                tracked)):
                wanted.add(include.target)
    wanted.update(f"<{name}>" for name in extra)
    headers = sorted(wanted)
    with tempfile.TemporaryDirectory(prefix="heavy-includes-") as scratch_name:
        scratch = Path(scratch_name)
        empty = scratch / "empty.cpp"
        empty.write_text("\n", encoding="utf-8")

        def unit_of(index: int, header: str) -> Path:
            unit = scratch / f"unit_{index}.cpp"
            unit.write_text(f"#include {header}\n", encoding="utf-8")
            return unit

        units = [unit_of(index, header) for index, header in enumerate(headers)]
        flags = list(reference.flags) + ["-fdiagnostics-color=never"]

        def one(source: Path) -> tuple[float, int, int, str]:
            runs = [sample.measure([reference.compiler, *flags, "-fsyntax-only", str(source)],  # type: ignore
                                   reference.directory, scratch) for _ in range(MEASURE_RUNS)]
            failed = next((run for run in runs if run.exit_code != 0), None)
            seconds = statistics.median(run.cpu_seconds for run in runs)
            if failed is not None:
                first = next((line.strip() for line in failed.diagnostics.splitlines() if "error" in line), "")
                return seconds, 0, failed.exit_code, first
            done = subprocess.run([reference.compiler, *flags, "-E", "-P", str(source)], cwd=reference.directory,
                                  capture_output=True, text=True, check=False)
            lines = sum(1 for line in done.stdout.splitlines() if line.strip())
            return seconds, lines, done.returncode, ""

        with ThreadPoolExecutor(max_workers=MEASURE_JOBS) as pool:
            results = list(pool.map(one, [empty, *units]))
    empty_seconds = results[0][0]
    rows: list[str] = []
    for header, (seconds, lines, code, first) in zip(headers, results[1:]):
        if code != 0:
            reason = (f"it does not compile alone with the flags of {sample.HEADER_REFERENCE}"  # type: ignore
                      + (f": {first.split('error:', 1)[-1].strip()}" if first else ""))
            rows.append(f"{header}{SEPARATOR}{UNMEASURED}{SEPARATOR}{reason.replace(SEPARATOR, ' ')}\n")
        else:
            rows.append(f"{header}{SEPARATOR}{seconds:.2f}{SEPARATOR}{lines}\n")
    model = next((line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text(
        encoding="utf-8", errors="replace").splitlines() if line.startswith("model name")), os.uname().machine)
    host = f"{model}, {os.cpu_count()} threads, load average {os.getloadavg()[0]:.0f}"
    stamp = datetime.datetime.now(datetime.UTC).date().isoformat()
    table.write_text(TABLE_HEADER.format(reference=sample.HEADER_REFERENCE, date=stamp,  # type: ignore
                                         host=host, empty=empty_seconds) + "\n" + "".join(rows), encoding="utf-8")
    print(f"check-heavy-includes: {table} written with {len(rows)} row(s).", file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each case in a scratch tree, and make sure that each verdict is correct.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="heavy-includes-") as work:
        root = Path(work)
        table = root / TABLE
        ledger = root / LEDGER
        budgets = root / "utils/scripts/budgets.txt"
        files = {
            TABLE: "# planted\n<chrono> | 0.97 | 30000\n<cstdint> | 0.01 | 40\n<arm_neon.h> | unmeasured | "
                   "it does not compile on this architecture\n<thread> | 0.85 | 28000\n<meta> | 0.36 | 34799\n",
            "utils/scripts/budgets.txt": f"{CHECK} | 1.0 | 1.0 | x <meta> | the planted factor\n",
            "include/fixy/Light.h": "#pragma once\n#include <cstdint>\n#include <fixy/Other.h>\n"
                                    '#include "Other.h"\n#include <bench_harness.h>\n#include <meta>\n',
            "include/fixy/Other.h": "#pragma once\n",
            "include/fixy/Debt.h": "#pragma once\n#include <cstdint>\n#include <chrono>\n",
            "include/fixy/os/Door.h": "#pragma once\n#include <thread>\n",
            "include/fixy/Neon.h": "#pragma once\n#if defined(__aarch64__)\n#include <arm_neon.h>\n#endif\n",
            "bench/bench_harness.h": "#pragma once\n",
        }
        for rel, text in files.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        matching = ("include/fixy/Debt.h | <chrono>\ninclude/fixy/Neon.h | <arm_neon.h>\n"
                    "keep | include/fixy/os/Door.h | <thread> | the planted door\n")
        ledger.write_text(matching, encoding="utf-8")

        def run() -> tuple[list[check_report.Finding], int]:
            """Evaluate the scratch tree, and give its findings and its exit status."""
            found = evaluate(root, table, ledger, factor_of(budgets))
            return found, 1 if any(item.level == "error" for item in found) else 0

        found, code = run()
        warned = {(item.path, item.line) for item in found if item.level == "warning"}
        expect("a ledger row gives a warning, and the run passes",
               code == 0 and warned == {("include/fixy/Debt.h", 3), ("include/fixy/Neon.h", 3)})
        expect("a keep row gives no finding", not any(item.path == "include/fixy/os/Door.h" for item in found))
        expect("a light header, <meta> itself, a project header and a tracked header outside include/ give no "
               "finding", not any(item.path == "include/fixy/Light.h" for item in found))
        expect("an unmeasured header counts as heavy", ("include/fixy/Neon.h", 3) in warned)

        (root / "include/fixy/Light.h").write_text(files["include/fixy/Light.h"] + "#include <chrono>\n",
                                                   encoding="utf-8")
        found, code = run()
        expect("a new include of a heavy header is an error",
               code == 1 and any(item.level == "error" and item.path == "include/fixy/Light.h" and item.line == 7
                                 for item in found))
        (root / "include/fixy/Light.h").write_text(files["include/fixy/Light.h"] + "#include <unknown_thing>\n",
                                                   encoding="utf-8")
        found, code = run()
        expect("a header that the table does not list is an error",
               code == 1 and any("has no row for <unknown_thing>" in item.message for item in found))
        (root / "include/fixy/Light.h").write_text(files["include/fixy/Light.h"] + "#include HEADER_NAME\n",
                                                   encoding="utf-8")
        found, code = run()
        expect("a computed include is an error", code == 1 and any("computed include" in item.message
                                                                   for item in found))
        (root / "include/fixy/Light.h").write_text(files["include/fixy/Light.h"], encoding="utf-8")

        (root / "include/fixy/Debt.h").write_text("#pragma once\n#include <cstdint>\n", encoding="utf-8")
        found, code = run()
        expect("a row whose include is gone is an error that asks for --write",
               code == 1 and any(item.path == LEDGER and "--write" in item.message for item in found))
        (root / "include/fixy/Debt.h").write_text(files["include/fixy/Debt.h"], encoding="utf-8")
        budgets.write_text(f"{CHECK} | 3.0 | 3.0 | x <meta> | a high factor\n", encoding="utf-8")
        found, code = run()
        expect("a row whose header is no longer heavy is an error",
               code == 1 and any("is no longer heavy" in item.message and item.line == 1 for item in found))
        expect("a keep row whose header is no longer heavy is an error",
               any("keep row" in item.message and "no longer heavy" in item.message for item in found))
        budgets.write_text(files["utils/scripts/budgets.txt"], encoding="utf-8")

        for body, label in ((matching + "include/fixy/Debt.h\n", "a row with no header"),
                            (matching + "keep | include/fixy/Debt.h | <chrono> | \n", "a keep row with no reason"),
                            (matching + "include/fixy/Debt.h | <chrono>\n", "a second row for one include")):
            ledger.write_text(body, encoding="utf-8")
            found, code = run()
            expect(f"{label} is an error", code == 1 and any(item.path == LEDGER for item in found))
        ledger.write_text(matching, encoding="utf-8")

        table.write_text(files[TABLE] + "<chrono> | 1.0 | 1\n", encoding="utf-8")
        found, code = run()
        expect("a second table row for one header is an error", code == 1 and found[0].path == TABLE)
        table.write_text(files[TABLE] + "<vector> | fast | 1\n", encoding="utf-8")
        found, code = run()
        expect("a table row with a word for its cost is an error", code == 1 and found[0].path == TABLE)
        table.write_text(files[TABLE].replace("<meta> | 0.36 | 34799\n", ""), encoding="utf-8")
        found, code = run()
        expect("a table with no measured row of <meta> is an error",
               code == 1 and found[0].path == TABLE and "<meta>" in found[0].message)
        table.unlink()
        found, code = run()
        expect("a missing table is an error", code == 1 and found[0].path == TABLE)
        table.write_text(files[TABLE], encoding="utf-8")

        ledger.write_text("include/fixy/Debt.h | <chrono>\n", encoding="utf-8")
        with contextlib.redirect_stderr(io.StringIO()):
            written = write(root, table, ledger, budgets)
        text = ledger.read_text(encoding="utf-8")
        found, code = run()
        expect("--write gives a ledger that passes, and it keeps no row of a keep",
               written == 0 and code == 0 and "include/fixy/Neon.h | <arm_neon.h>" in text
               and text.startswith(LEDGER_HEADER))
        ledger.write_text(matching, encoding="utf-8")

        warnings_dir = root / "warnings"
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            status = check(root, table, ledger, budgets, warnings_dir)
        lines = (warnings_dir / f"{CHECK}.txt").read_text(encoding="utf-8").splitlines()
        expect("the warnings file holds each warning", status == 0 and len(lines) == 2
               and all(check_report.parse_line(line) is not None for line in lines))

        previous = Path.cwd()
        os.chdir("/")
        try:
            from_slash = [item.text() for item in evaluate(root, table, ledger, factor_of(budgets))]
        finally:
            os.chdir(previous)
        expect("the findings from / equal the findings from the root",
               from_slash == [item.text() for item in evaluate(root, table, ledger, factor_of(budgets))])
        (root / "include/fixy/Broken.h").write_text("void f() { g(1) { } }\n", encoding="utf-8")
        found, code = run()
        expect("a header that the parser cannot read is an error",
               code == 1 and any(item.path == "include/fixy/Broken.h" for item in found))
    if failures:
        print(f"check-heavy-includes --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-heavy-includes --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    parser = argparse.ArgumentParser(prog="check-heavy-includes.py", description=__doc__.split("\n", 1)[0])
    check_report.add_arguments(parser)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--self-test", action="store_true", help="plant each case in a scratch tree")
    mode.add_argument("--write", action="store_true", help="write the ledger rows again from the tree")
    mode.add_argument("--measure", metavar="BUILD_DIR", type=Path, help="measure each header and write the table")
    parser.add_argument("headers", nargs="*", help="with --measure: more headers to measure, without brackets")
    arguments = parser.parse_args(argv)
    if arguments.headers and arguments.measure is None:
        parser.error("a header name goes only with --measure")
    root = tsast.REPO_ROOT
    table, ledger, budgets = root / TABLE, root / LEDGER, check_report.BUDGETS
    try:
        if arguments.self_test:
            return self_test()
        if arguments.write:
            return write(root, table, ledger, budgets)
        if arguments.measure is not None:
            return measure(root, arguments.measure.resolve(), arguments.headers, table)
        return check(root, table, ledger, budgets, arguments.warnings_dir)
    except tsast.KitMissing as exc:
        print(f"check-heavy-includes: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
