#!/usr/bin/env python3
"""tu-sample — the compile CPU time of a fixed sample of translation units.

THE SAMPLE
    The sample is fixed in SAMPLE below, so each run measures the same files.
    It holds eleven translation units that cover the cost range of the tree,
    from a small foundation test to the slowest tool, and five negative
    fixtures.  A change to the sample makes each earlier table incomparable,
    so change it only together with a new baseline.

WHAT THE SCRIPT MEASURES
    For each source, the script takes the command of the compile database and
    runs it two times over: with -fsyntax-only, which stops after the front
    end, and in full, which writes the object to a scratch directory.  It
    removes the output and dependency options of the command, so a run
    changes no file of the build tree.  It does each run N times (three by
    default) and prints the median of the user plus system CPU seconds.  The
    back-end column is the full median minus the syntax median.  The peak
    column is the median of the largest resident set of one process in the
    full runs.

    CPU time and not wall time is the measure, because the host is shared
    and the wall time of a run depends on the other load.  The CPU time
    comes from wait4, and it includes the compiler proper and the assembler
    that the driver runs.

    With --headers, the script also measures each named header alone: a
    scratch translation unit that includes only that header, compiled with
    -fsyntax-only and the flags of a reference translation unit
    (test/foundation/test_row_hash.cpp by default).

    A translation unit of the sample must compile, and a negative fixture must
    fail its full compile.  A different result means that the command does
    not measure what the row says, and the script reports it.

    --save writes the results as JSON.  --compare reads such a file and adds
    the change of each median to the table.

    --db can name the compile database of another copy of the tree, such as
    the main checkout.  The script finds each row by the end of its source
    path, so two copies can run side by side, under the same load, for a
    before and after table.

EXIT CODES
    0  Each row has its numbers, and each command gave the expected result
    1  A source is missing from the compile database, a translation unit or
       a header did not compile, a fixture compiled, or a compare file has a
       different shape
    2  A usage error
"""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import shlex
import statistics
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from repo_root import REPO_ROOT  # noqa: E402

# The fixed sample: the path of each source, and whether it is a negative
# fixture.  The order is the order of the table.
SAMPLE: tuple[tuple[str, bool], ...] = (
    ("test/foundation/test_row_hash.cpp", False),
    ("test/test_arena.cpp", False),
    ("src/canopy/Lifeguard.cpp", False),
    ("test/fixy/test_collision.cpp", False),
    ("test/test_region_cache_repeated.cpp", False),
    ("test/test_cipher.cpp", False),
    ("test/fixy/test_session_handle.cpp", False),
    ("test/fixy/test_owned_region_every_total_0.cpp", False),
    ("test/test_ledger.cpp", False),
    ("utils/tools/crucible_hwprobe.cpp", False),
    ("test/fixy/test_session_global_attack_two_roles_1.cpp", False),
    ("test/fixy/neg/neg_atom_cv_qualified_rejected.cpp", True),
    ("test/foundation/neg/neg_aligned_buffer_copied.cpp", True),
    ("test/foundation/neg/neg_permission_brand_not_empty.cpp", True),
    ("test/fixy/neg/neg_rule_l003_borrow_raw_clone.cpp", True),
    ("test/vigil_neg/neg_vigil_ring_other_brand.cpp", True),
)
HEADER_REFERENCE = "test/foundation/test_row_hash.cpp"
DEFAULT_DATABASE = REPO_ROOT / "build" / "compile_commands.json"

# The options that write a file beside the object: the object itself and the
# dependency file that the build tool reads.  The script removes each one, so
# a measurement leaves the build tree as it was.
OPTIONS_WITH_VALUE = frozenset({"-o", "-MF", "-MT", "-MQ"})
OPTIONS_ALONE = frozenset({"-c", "-MD", "-MMD"})
MODES = ("syntax", "full")


class SampleError(RuntimeError):
    """A sample source or header cannot be measured as the row says."""


@dataclass(frozen=True)
class Command:
    """One compile command of the database, without its output and dependency options."""

    directory: Path
    compiler: str
    flags: tuple[str, ...]
    source: str


@dataclass(frozen=True)
class Run:
    """The resource use of one compiler run."""

    cpu_seconds: float
    peak_mb: int
    exit_code: int
    diagnostics: str


@dataclass(frozen=True)
class Row:
    """The medians of one sample row."""

    name: str
    kind: str
    syntax_seconds: float
    full_seconds: float | None
    peak_mb: int | None
    problem: str


def load_database(path: Path) -> list[tuple[Path, dict[str, object]]]:
    """Read a compile database and pair each row with the resolved path of its source.

    Complexity: linear in the number of rows.

    Args:
        path: The compile_commands.json file

    Returns:
        (resolved source path, row) for each row

    Raises:
        SampleError: If the file does not exist or holds no list of rows
    """
    if not path.is_file():
        raise SampleError(f"the compile database {path} does not exist. Configure a build first, for example "
                          f"`cmake --preset default`, or give --db.")
    rows = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(rows, list):
        raise SampleError(f"the compile database {path} does not hold a list of rows.")
    return [((Path(str(row["directory"])) / str(row["file"])).resolve(), row) for row in rows]


def find_row(database: list[tuple[Path, dict[str, object]]], path: str) -> dict[str, object] | None:
    """Return the one row whose source ends with the repository-relative path.

    The match is on the end of the path, so a database of a different copy of
    the tree (the main checkout, a worktree) gives the same rows.  This lets
    two copies of the tree be measured side by side under the same load.
    Complexity: linear in the number of rows.

    Args:
        database: The rows of load_database
        path: A path relative to the root of the tree

    Returns:
        The row, or None when no row or more than one row matches
    """
    tail = Path(path).parts
    hits = [row for source, row in database if source.parts[-len(tail):] == tail]
    return hits[0] if len(hits) == 1 else None


def command_of(row: dict[str, object]) -> Command:
    """Return the compile command of one row, without its output, dependency and source words.

    Args:
        row: One row of the compile database

    Returns:
        The command, with the source path kept apart from the flags
    """
    directory = Path(str(row["directory"]))
    words = list(row["arguments"]) if "arguments" in row else shlex.split(str(row["command"]))
    source = (directory / str(row["file"])).resolve()
    flags: list[str] = []
    skip_next = False
    for word in words[1:]:
        if skip_next:
            skip_next = False
            continue
        if word in OPTIONS_WITH_VALUE:
            skip_next = True
            continue
        if word in OPTIONS_ALONE:
            continue
        if not word.startswith("-") and (directory / word).resolve() == source:
            continue
        flags.append(word)
    return Command(directory, str(words[0]), tuple(flags), str(source))


def measure(argv: list[str], directory: Path, scratch: Path) -> Run:
    """Run one compiler command and return its CPU time, its peak memory and its exit code.

    The CPU time is the user plus system time that wait4 gives, which includes
    each child that the driver waited for.  The diagnostics go to a scratch
    file, so a long error text cannot fill a pipe.

    Args:
        argv: The full command
        directory: The working directory of the command
        scratch: A directory for the diagnostics file

    Returns:
        The resource use of the run
    """
    with tempfile.NamedTemporaryFile(dir=scratch, suffix=".err", delete=False) as err_file:
        err_path = Path(err_file.name)
        process = subprocess.Popen(argv, cwd=directory, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                                   stderr=err_file)
        _, status, usage = os.wait4(process.pid, 0)
    process.returncode = os.waitstatus_to_exitcode(status)
    diagnostics = err_path.read_text(encoding="utf-8", errors="replace")
    err_path.unlink()
    return Run(usage.ru_utime + usage.ru_stime, usage.ru_maxrss // 1024, process.returncode, diagnostics)


def argv_for(command: Command, mode: str, scratch: Path, stem: str) -> list[str]:
    """Build the command line of one mode.

    Args:
        command: The prepared compile command
        mode: "syntax" or "full"
        scratch: The directory for the object of a full run
        stem: A name that is unique for the sample row and the run

    Returns:
        The command line, with the source last
    """
    tail = ["-fsyntax-only"] if mode == "syntax" else ["-c", "-o", str(scratch / f"{stem}.o")]
    return [command.compiler, *command.flags, *tail, command.source]


def last_lines(text: str, count: int = 6) -> str:
    """Return the last lines of a diagnostics text, indented for the report."""
    lines = [line for line in text.splitlines() if line.strip()]
    return "\n".join("      " + line for line in lines[-count:])


def median_row(name: str, kind: str, runs: dict[str, list[Run]]) -> Row:
    """Reduce the runs of one sample row to its medians, and say what went wrong, if anything.

    Args:
        name: The source or header path
        kind: "source", "fixture" or "header"
        runs: The runs of each mode

    Returns:
        The row of the table
    """
    problems: list[str] = []
    syntax_runs = runs["syntax"]
    full_runs = runs.get("full", [])
    if kind == "fixture":
        if any(run.exit_code == 0 for run in full_runs):
            problems.append("the fixture compiled, so it no longer measures a rejection")
    else:
        failed = [run for run in syntax_runs + full_runs if run.exit_code != 0]
        if failed:
            problems.append(f"the compile failed (exit {failed[0].exit_code}):\n{last_lines(failed[0].diagnostics)}")
    syntax_seconds = statistics.median(run.cpu_seconds for run in syntax_runs)
    full_seconds = statistics.median(run.cpu_seconds for run in full_runs) if full_runs else None
    peak_mb = int(statistics.median(run.peak_mb for run in full_runs)) if full_runs else None
    return Row(name, kind, syntax_seconds, full_seconds, peak_mb, "\n".join(problems))


def header_command(reference: Command, header: str, scratch: Path) -> Command:
    """Return the command that compiles one header alone, with the flags of the reference unit.

    Args:
        reference: The command of the reference translation unit
        header: The header, as an include directive names it
        scratch: The directory for the scratch translation unit

    Returns:
        A command whose source includes only the header
    """
    stem = "".join(char if char.isalnum() else "_" for char in header)
    source = scratch / f"header_{stem}.cpp"
    source.write_text(f"#include <{header}>\n", encoding="utf-8")
    return Command(reference.directory, reference.compiler, reference.flags, str(source))


def measure_all(jobs: list[tuple[str, str, Command, tuple[str, ...]]], runs: int, workers: int,
                scratch: Path) -> list[Row]:
    """Run every command of every row the given number of times, in parallel, and reduce each row.

    Complexity: rows times modes times runs compiler runs.

    Args:
        jobs: (name, kind, command, modes) for each row
        runs: The number of runs of each command
        workers: The number of compiler runs at the same time
        scratch: The scratch directory

    Returns:
        The rows, in the order of the jobs
    """
    work = [(index, mode, repeat) for index, (_, _, _, modes) in enumerate(jobs) for mode in modes
            for repeat in range(runs)]
    results: dict[int, dict[str, list[Run]]] = {index: {} for index in range(len(jobs))}

    def one(item: tuple[int, str, int]) -> tuple[int, str, Run]:
        index, mode, repeat = item
        _, _, command, _ = jobs[index]
        stem = f"row{index}_{mode}_{repeat}"
        run = measure(argv_for(command, mode, scratch, stem), command.directory, scratch)
        (scratch / f"{stem}.o").unlink(missing_ok=True)
        return index, mode, run

    with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
        for index, mode, run in pool.map(one, work):
            results[index].setdefault(mode, []).append(run)
    return [median_row(name, kind, results[index]) for index, (name, kind, _, _) in enumerate(jobs)]


def change(before: float | None, after: float | None) -> str:
    """Format the change from one median to another as a percentage."""
    if before is None or after is None or before <= 0.0:
        return ""
    return f"{(after - before) / before * 100.0:+.0f}%"


def print_table(rows: list[Row], baseline: dict[str, dict[str, object]] | None) -> None:
    """Print the table of medians, with the change against a baseline when one is given."""
    width = max(len(row.name) for row in rows) + 2
    header = f"{'row':<{width}}{'syntax s':>10}{'full s':>10}{'back end s':>12}{'peak MB':>9}"
    if baseline is not None:
        header += f"{'syntax':>9}{'full':>9}"
    print(header)
    for kind, label in (("source", "sources"), ("fixture", "fixtures"), ("header", "headers")):
        group = [row for row in rows if row.kind == kind]
        if not group:
            continue
        for row in group:
            full = f"{row.full_seconds:10.2f}" if row.full_seconds is not None else f"{'-':>10}"
            # A fixture stops in the front end, so it has no back end to show.
            back = (f"{row.full_seconds - row.syntax_seconds:12.2f}"
                    if row.full_seconds is not None and kind == "source" else f"{'-':>12}")
            peak = f"{row.peak_mb:9d}" if row.peak_mb is not None else f"{'-':>9}"
            line = f"{row.name:<{width}}{row.syntax_seconds:10.2f}{full}{back}{peak}"
            if baseline is not None:
                old = baseline.get(row.name, {})
                old_syntax = old.get("syntax_seconds")
                old_full = old.get("full_seconds")
                line += f"{change(old_syntax, row.syntax_seconds):>9}{change(old_full, row.full_seconds):>9}"
            print(line)
        syntax_total = sum(row.syntax_seconds for row in group)
        fulls = [row.full_seconds for row in group if row.full_seconds is not None]
        full_total = f"{sum(fulls):10.2f}" if fulls else f"{'-':>10}"
        print(f"{'total (' + str(len(group)) + ' ' + label + ')':<{width}}{syntax_total:10.2f}{full_total}")


def parse_arguments(argv: list[str]) -> argparse.Namespace:
    """Parse the command line."""
    parser = argparse.ArgumentParser(
        prog="tu-sample.py",
        description="Measure the compile CPU time of a fixed sample of translation units, with -fsyntax-only "
                    "and in full, as the median of several runs.",
    )
    parser.add_argument("--db", type=Path, default=DEFAULT_DATABASE,
                        help="the compile database (default: build/compile_commands.json of this tree)")
    parser.add_argument("--runs", type=int, default=3, help="the runs of each command (default: 3)")
    parser.add_argument("--jobs", type=int, default=8,
                        help="the compiler runs at the same time (default: 8)")
    parser.add_argument("--only", action="append", default=[], metavar="TEXT",
                        help="measure only the sample rows whose path contains TEXT (repeatable)")
    parser.add_argument("--no-sample", action="store_true",
                        help="measure no sample row, only the headers of --headers")
    parser.add_argument("--headers", nargs="+", default=[], metavar="HEADER",
                        help="also measure each header alone, as an include directive names it")
    parser.add_argument("--reference", default=HEADER_REFERENCE,
                        help=f"the translation unit whose flags --headers uses (default: {HEADER_REFERENCE})")
    parser.add_argument("--save", type=Path, help="write the results as JSON to this file")
    parser.add_argument("--compare", type=Path, help="add the change against the results in this JSON file")
    parser.add_argument("--list", action="store_true", help="print the sample and exit")
    arguments = parser.parse_args(argv)
    if arguments.runs < 1 or arguments.jobs < 1:
        parser.error("--runs and --jobs take a positive number")
    return arguments


def main(argv: list[str]) -> int:
    """Measure the sample and print the table.

    Args:
        argv: The command-line arguments, without the program name

    Returns:
        The exit code
    """
    arguments = parse_arguments(argv)
    if arguments.list:
        for path, is_fixture in SAMPLE:
            print(f"{'fixture' if is_fixture else 'source ':8s} {path}")
        return 0
    selected = [] if arguments.no_sample else [
        (path, is_fixture) for path, is_fixture in SAMPLE
        if not arguments.only or any(text in path for text in arguments.only)
    ]
    if not selected and not arguments.headers:
        print("tu-sample: no row to measure. Check --only, or give --headers.", file=sys.stderr)
        return 2
    baseline: dict[str, dict[str, object]] | None = None
    if arguments.compare is not None:
        try:
            saved = json.loads(arguments.compare.read_text(encoding="utf-8"))
            baseline = {str(row["name"]): row for row in saved["rows"]}
        except (OSError, ValueError, KeyError, TypeError) as exc:
            print(f"tu-sample: the compare file {arguments.compare} is not a file that --save wrote: {exc}",
                  file=sys.stderr)
            return 1
    try:
        database = load_database(arguments.db)
        jobs: list[tuple[str, str, Command, tuple[str, ...]]] = []
        missing: list[str] = []
        for path, is_fixture in selected:
            row = find_row(database, path)
            if row is None:
                missing.append(path)
                continue
            jobs.append((path, "fixture" if is_fixture else "source", command_of(row), MODES))
        with tempfile.TemporaryDirectory(prefix="tu-sample-") as scratch_name:
            scratch = Path(scratch_name)
            if arguments.headers:
                reference_row = find_row(database, arguments.reference)
                if reference_row is None:
                    raise SampleError(f"no row of {arguments.db} names the reference unit {arguments.reference}, "
                                      f"or more than one row names it.")
                reference = command_of(reference_row)
                for header in arguments.headers:
                    jobs.append((header, "header", header_command(reference, header, scratch), ("syntax",)))
            print(f"tu-sample: {arguments.runs} runs of each command, {arguments.jobs} at the same time, "
                  f"compile database {arguments.db}", flush=True)
            rows = measure_all(jobs, arguments.runs, arguments.jobs, scratch)
    except SampleError as exc:
        print(f"tu-sample: {exc}", file=sys.stderr)
        return 1
    print_table(rows, baseline)
    if arguments.save is not None:
        arguments.save.write_text(json.dumps({
            "runs": arguments.runs,
            "database": str(arguments.db),
            "rows": [{"name": row.name, "kind": row.kind, "syntax_seconds": row.syntax_seconds,
                      "full_seconds": row.full_seconds, "peak_mb": row.peak_mb} for row in rows],
        }, indent=2) + "\n", encoding="utf-8")
    problems = [(row.name, row.problem) for row in rows if row.problem]
    for path in missing:
        problems.append((path, f"no row of {arguments.db} names the source, or more than one row names it"))
    for name, problem in problems:
        print(f"tu-sample: {name}: {problem}", file=sys.stderr)
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
