"""loop derive walk-units — write test/layer/walk-checks.txt from the tree, measure its weights, or show its paths.

usage: loop derive walk-units --write
       loop derive walk-units --check
       loop derive walk-units --measure BUILD_DIR [--jobs N]
       loop derive walk-units --explain CHECK_FILE

The rule walk-units (assay.guard.engines.tsast.walk_units) derives the list
of the check files that the walk units compile a second time.  The rule
compares the list with the tree.  This module writes the list, and it gives
the analysis of that rule to a person.

THE MODES
    --write     write the list again from the tree.  The write keeps the
                weight of each row that stays, and it gives a new row the
                weight unmeasured.  It writes nothing while a file of the
                analysis does not parse.
    --check     run the rule walk-units, as `guard check --rule walk-units`
                does.
    --measure   compile the base unit, which holds only walk_headers.h, and
                one unit for each check file of the list.  Each compile uses
                the command of a walk unit from the compile database of
                BUILD_DIR.  Then write each weight again: the user
                instructions that the check file adds to the base unit, in
                units of 10^9 with one decimal.  --jobs gives the compiles at
                one time.
    --explain   print the path of each walk of one check file, from the
                check to the call of members_of.

THE MEASURE
    Each compile runs through assay.core.proc in a thread of its own.  A
    counter of assay.core.meter counts the user instructions of the compiler
    and of each process that it starts.  The counter of one thread counts only
    the processes that the thread starts.  The count is accurate or missing,
    and a measure on a host with a high load gives the same weights.  A
    measure with no accurate count for a unit writes nothing.

Exit 0 when the mode did its work, and 1 when the list cannot be written or
a measure fails.  Exit 2 on a usage error, and 3 when an input is missing.
"""

import tempfile
from dataclasses import dataclass
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import proc
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.exit import ExitCode
from assay.core.paths import REPO_ROOT
from assay.guard.engines.tsast import walk_units as rule
from assay.guard.engines.tsast.runner import TsastContext
from assay.readers import compdb, tsast

PROGRAM = "loop derive walk-units"
# The words of the walk unit objects in the compile database, and the directory of the walk_checks.h of each unit.
UNIT_OUTPUT = "/layer_walks_across_headers_"
UNIT_PARENT = "/test/layer/walk_units"
INCLUDE_FLAG = "-I"
OUTPUT_FLAG = "-o"
# The default number of measure compiles at one time.
MEASURE_JOBS = 96
GIGA = 1e9
# The number of the heaviest check files that the report of a measure names.
SHOWN_HEAVIEST = 5


class MeasureError(Exception):
    """A measure that cannot run, with the reason and the repair."""


@dataclass(frozen=True, slots=True)
class UnitCommand:
    """The compile command of a walk unit: its words, its directory and the include directory of its walk_checks.h."""

    argv: tuple[str, ...]
    directory: str
    unit_dir: str


def written(root: Path) -> ExitCode:
    """Write the list again from the tree, with the weight of each row that stays.

    Returns:
        ExitCode.OK when the list is written, ExitCode.FINDINGS when a parse failure stops the write
    """
    derivation = rule.derive(TsastContext(root))
    if derivation.failures:
        for failure in derivation.failures:
            check_report.write_line(
                f"{failure.path}:{failure.line}: the parse kit cannot read the file at {failure.node}", is_error=True
            )
        check_report.write_line(
            f"{PROGRAM}: --write does not write the list while a file does not parse.", is_error=True
        )
        return ExitCode.FINDINGS
    rows = rule.read_list(root / rule.LIST).rows
    cells = [
        (rel, rows[rel].weight if rel in rows else rule.UNMEASURED, rule.namespaces_of(tags))
        for rel, tags in derivation.selected.items()
    ]
    store(root / rule.LIST, cells)
    check_report.write_line(f"{PROGRAM}: {rule.LIST} written with {len(cells)} row(s).", is_error=True)
    return ExitCode.OK


def store(path: Path, cells: list[tuple[str, str, str]]) -> None:
    """Write the list of (check file, weight, namespace cell) triples, and only when its bytes change."""
    path.parent.mkdir(parents=True, exist_ok=True)
    rows_grammar.write(path, rule.LIST_HEADER, [], rule.list_rows(cells))


def unit_include(argument: str) -> str | None:
    """Return the include directory of the walk_checks.h of one unit, when an argument is -I of it, or None.

    The directory is a directory UNIT_PARENT/N of the build, for a number N.
    """
    if not argument.startswith(INCLUDE_FLAG):
        return None
    directory = argument.removeprefix(INCLUDE_FLAG)
    head, slash, number = directory.rpartition("/")
    is_unit = bool(slash) and bool(number) and all("0" <= character <= "9" for character in number)
    return directory if is_unit and head.endswith(UNIT_PARENT) else None


def walk_unit_command(build_dir: Path) -> UnitCommand:
    """Find the compile command of a walk unit in the compile database of a build.

    Complexity: linear in the length of the compile database.

    Raises:
        MeasureError: If the build has no compile database, or the database holds no walk unit
    """
    database = compdb.database_file(build_dir)
    try:
        compiles = compdb.entries(build_dir)
    except compdb.CompdbError as problem:
        raise MeasureError(str(problem)) from None
    for row in compiles:
        argv = row.argv()
        if UNIT_OUTPUT not in (row.output or "") and not any(UNIT_OUTPUT in argument for argument in argv):
            continue
        for argument in argv:
            directory = unit_include(argument)
            if directory is not None:
                return UnitCommand(argv, row.directory, directory)
        raise MeasureError(
            f"the walk unit command of {database} has no -I of a walk_units directory, so the measure "
            f"cannot give it another walk_checks.h.  Read test/layer/CMakeLists.txt."
        )
    raise MeasureError(
        f"{database} holds no walk unit of test/layer/walks_across_headers.cpp.  Configure the build again."
    )


def measure_unit(command: UnitCommand, work: Path, checks: list[str]) -> int | None:
    """Compile one walk unit that holds the given check files, and count its user instructions.

    Returns:
        The user instructions of the compile, or None when the compile gives an error or the host gives no
        accurate count
    """
    work.mkdir(parents=True, exist_ok=True)
    (work / "walk_checks.h").write_text(
        "#pragma once\n" + "".join(f'#include "checks/{check}"\n' for check in checks), encoding="utf-8"
    )
    argv = [
        f"{INCLUDE_FLAG}{work}" if argument == f"{INCLUDE_FLAG}{command.unit_dir}" else argument
        for argument in command.argv
    ]
    if OUTPUT_FLAG not in argv[:-1]:
        return None
    argv[argv.index(OUTPUT_FLAG) + 1] = str(work / "unit.o")
    counter = cost_meter.open_instruction_counter()
    if counter is None:
        return None
    try:
        completed = proc.run(
            proc.Command(
                argv,
                timeout_s=proc.TOOL_S,
                cwd=command.directory,
                stdout=proc.Output.DISCARD,
                stderr=proc.Output.DISCARD,
            )
        )
    except proc.ToolFailed:
        cost_meter.read_instruction_counter(counter)
        return None
    count = cost_meter.read_instruction_counter(counter)
    return count if completed.exit == 0 else None


def measured(build_dir: Path, list_path: Path, jobs: int) -> ExitCode:
    """Measure the weight of each row of the list, and write the list again with the weights.

    Complexity: one compile for each row and one more for the base, with jobs compiles at one time.

    Returns:
        ExitCode.OK when the list is written, ExitCode.FINDINGS when the measure cannot run or a compile fails
    """
    from concurrent.futures import ThreadPoolExecutor

    listed = rule.read_list(list_path)
    for problem in listed.problems:
        check_report.write_line(f"{rule.LIST}:{problem.line}: {problem.message}", is_error=True)
    if listed.problems or not listed.rows:
        check_report.write_line(
            f"{PROGRAM}: --measure measures the rows of a list with no malformed row.  Run: {rule.WRITE_COMMAND}",
            is_error=True,
        )
        return ExitCode.FINDINGS
    try:
        command = walk_unit_command(build_dir)
    except MeasureError as problem:
        check_report.write_line(f"{PROGRAM}: {problem}", is_error=True)
        return ExitCode.FINDINGS
    units: dict[str, list[str]] = {"": [], **{rel: [rel] for rel in sorted(listed.rows)}}
    with tempfile.TemporaryDirectory(prefix="walk-weights-") as work, ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = {
            rel: pool.submit(measure_unit, command, Path(work) / f"unit{index}", checks)
            for index, (rel, checks) in enumerate(units.items())
        }
        counts = {rel: future.result() for rel, future in futures.items()}
    failed = sorted(rel or "the base unit" for rel, count in counts.items() if count is None)
    if failed:
        check_report.write_line(
            f"{PROGRAM}: no exact count for {', '.join(failed)}.  A compile failed, or the host "
            f"gives no instruction counter (utils/assay/core/meter.py).  The list stays as it was.",
            is_error=True,
        )
        return ExitCode.FINDINGS
    base = counts[""] or 0
    weights = {rel: max(0, (counts[rel] or 0) - base) for rel in listed.rows}
    store(list_path, [(rel, f"{weights[rel] / GIGA:.1f}", row.namespaces) for rel, row in listed.rows.items()])
    heaviest = sorted(weights.items(), key=lambda item: (-item[1], item[0]))[:SHOWN_HEAVIEST]
    check_report.write_line(
        f"{PROGRAM}: {rule.LIST} written with {len(weights)} weight(s).  The base unit costs "
        f"{base / GIGA:.1f} G instructions, and the check files add "
        f"{sum(weights.values()) / GIGA:.1f} G.  The heaviest: "
        + ", ".join(f"{rel} {count / GIGA:.1f} G" for rel, count in heaviest),
        is_error=True,
    )
    return ExitCode.OK


def path_of(why: rule.Why) -> list[str]:
    """Return the steps of the path of one tag, from the first reason to the call of members_of.

    Complexity: linear in the length of the path.  The walk stops at a summary that it read before.
    """
    steps: list[str] = []
    seen: set[tuple[int, object]] = set()
    step: rule.Why | None = why
    while step is not None:
        steps.append(step.text)
        entity, key = step.entity, step.key
        if entity is None or key is None or (id(entity), key) in seen:
            break
        seen.add((id(entity), key))
        if rule.is_param_key(key):
            step = entity.walked_params.get(key)
        else:
            step = entity.walked.get(key) or entity.constructed.get(key)
    return steps


def explained(root: Path, target: str) -> ExitCode:
    """Print the path of each walk of one check file, from the check to the call of members_of.

    Returns:
        ExitCode.OK, or ExitCode.USAGE when the target names no check file
    """
    derivation = rule.derive(TsastContext(root))
    rel = Path(target).as_posix().removeprefix(f"{rule.CHECKS}/")
    root_entity = derivation.universe.roots.get(rel)
    if root_entity is None:
        check_report.write_line(f"{PROGRAM}: {rel} is not a check file under {rule.CHECKS}/.", is_error=True)
        return ExitCode.USAGE
    check_report.write_line(f"{rel}: {'joins' if rel in derivation.selected else 'does not join'} the walk units")
    for tag, why in sorted(root_entity.walked.items(), key=lambda item: rule.shown_tag(item[0])):
        mark = "" if rule.is_extensible(derivation.universe, tag) else "  (only check files open it)"
        check_report.write_line(f"  {rule.shown_tag(tag)}{mark}")
        for text in path_of(why):
            check_report.write_line(f"    {text}")
    for entity in rule.unnamed_walks(derivation.universe):
        check_report.write_line(
            f"  {rule.UNKNOWN}: {entity.label()} can walk a namespace, and a use calls it with no name"
        )
    return ExitCode.OK


def checked() -> int:
    """Run the rule walk-units through the command guard check, and give its status."""
    from assay.guard import check

    return check.main(["--rule", rule.RULE])


def main(argv: list[str]) -> int:
    """Run one mode (THE MODES).

    Args:
        argv: The words after `loop derive walk-units`

    Returns:
        The exit code
    """
    from assay.core import cli

    parser = cli.parser(
        PROGRAM, "write test/layer/walk-checks.txt from the tree, measure its weights, or show its paths"
    )
    modes = parser.add_mutually_exclusive_group(required=True)
    modes.add_argument("--write", action="store_true", help="write the list again from the tree")
    modes.add_argument("--check", action="store_true", help="run the rule walk-units")
    modes.add_argument(
        "--measure",
        metavar="BUILD_DIR",
        type=Path,
        help="measure the weight of each row with the walk unit command of the build",
    )
    modes.add_argument("--explain", metavar="CHECK_FILE", help="print the path of each walk of one check file")
    parser.add_argument("--jobs", type=int, default=MEASURE_JOBS, help="with --measure: the compiles at one time")
    try:
        options = parser.parse_args(argv)
    except SystemExit as stop:
        return ExitCode.OK if stop.code == 0 else ExitCode.USAGE
    if options.jobs < 1:
        check_report.write_line(f"{PROGRAM}: --jobs takes a number of 1 or more.", is_error=True)
        return ExitCode.USAGE
    try:
        if options.check:
            return checked()
        if options.write:
            return written(REPO_ROOT)
        if options.measure is not None:
            return measured(options.measure.resolve(), REPO_ROOT / rule.LIST, options.jobs)
        return explained(REPO_ROOT, options.explain)
    except tsast.KitMissing as problem:
        check_report.write_line(f"{PROGRAM}: {problem}", is_error=True)
        return ExitCode.MISSING_INPUT
