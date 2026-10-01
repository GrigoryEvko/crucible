#!/usr/bin/env python3
"""check-header-constexpr-ops — each header compiles alone at a low operation limit, and each higher limit is still necessary.

THE LIMIT
    A constant evaluation that runs where a header stands runs again in each
    translation unit that includes the header.  test/layer/CMakeLists.txt
    compiles each header of include/ alone, in a one-line unit
    `#include <header>`, with -fconstexpr-ops-limit at the error threshold
    of the row header-constexpr-ops in utils/scripts/budgets.txt.  GCC
    stops the compile at an evaluation of more operations, and it names the
    location.  A variable template and a template that no code
    instantiates cost nothing there.  The one-line unit of a header with no
    check file is its sentinel, which each build compiles.  The one-line
    units of the headers with a check file are the target layer_alone,
    which the default leg of CI builds.  GCC gives no warning form of the
    limit, so the limit is an error only.

THE LIST
    test/layer/header-constexpr-ops.txt gives a higher limit to each header
    that needs one, one row each:

        <layer>/<path>.h | limit | reason

    The limit of a row is more than the threshold, and the reason is
    mandatory.

WHAT THE GUARD CHECKS
    It reads the budget table, the list and the compile database of
    BUILD_DIR.
      * The database holds one one-line unit for each header that has a
        sentinel, and the last -fconstexpr-ops-limit of each unit is the
        threshold, or the limit of the row of its header.
      * Each listed header compiles alone at the limit of its row.
      * Each listed header does not compile alone at the threshold, because
        an evaluation of more operations stops it.  A listed header that
        compiles at the threshold is an error: remove its row.
    Each listed header gives a warning on each run, with the evaluations
    that need more than the threshold, so the debt stays visible.  A
    malformed row, a missing unit, a wrong limit, a missing database and a
    compile that fails for another reason are errors.  Each finding is one
    line in the format of utils/scripts/check_report.py, under the check
    name header-constexpr-ops.

WHAT THE GUARD CANNOT SEE
    The operation count measures the steps of the constant evaluator.  It
    does not count the cost of a template instantiation or of a reflection
    query, so a walk that instantiates many templates in few steps passes
    the limit.  A header in test/layer/crucible-not-standalone.txt has no
    unit.

Usage
    check-header-constexpr-ops.py BUILD_DIR [--warnings-dir DIR]
    check-header-constexpr-ops.py --self-test

Exit 0 with no error finding, 1 with one or more, 2 on a usage error or a
failed self-test.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
from repo_root import REPO_ROOT  # noqa: E402

CHECK = "header-constexpr-ops"
SCRIPT = "utils/scripts/check-header-constexpr-ops.py"
BUDGETS = "utils/scripts/budgets.txt"
LIST = "test/layer/header-constexpr-ops.txt"
NOT_STANDALONE = "test/layer/crucible-not-standalone.txt"
INCLUDE = "include"
LAYERS = ("foundation", "fixy", "crucible")
SEPARATOR = " | "
LIMIT_FLAG = "-fconstexpr-ops-limit="
OPS_ERROR = "evaluation operation count exceeds limit"
# A diagnostic line of GCC: the location, then the rest.
LOCATION = re.compile(r"^(?P<path>[^ :][^:]*):(?P<line>\d+):(?P<column>\d+):\s+(?P<rest>.*)$")


@dataclass(frozen=True)
class Row:
    """One row of the list: a header, its limit and the line of the row."""

    header: str
    limit: int
    line: int


@dataclass(frozen=True)
class Unit:
    """The compile command of the one-line unit of one header."""

    argv: tuple[str, ...]
    source: str
    directory: str


@dataclass(frozen=True)
class Outcome:
    """The result of one compile of a header alone.

    owners holds the location that starts each evaluation that exceeds the
    limit, and others holds the first lines of each other error.
    """

    passes: bool
    owners: tuple[str, ...]
    others: tuple[str, ...]


@dataclass(frozen=True)
class Paths:
    """The inputs of one run, so that the self-test can point them at a scratch tree."""

    root: Path
    budgets: Path
    listing: Path
    build: Path


def c_identifier(text: str) -> str:
    """Return the name that CMake string(MAKE_C_IDENTIFIER) gives a text.

    Args:
        text: A header path relative to include/

    Returns:
        The text with each character that is not a letter, a digit or `_` as
        `_`, and with `_` in front of a first digit
    """
    name = re.sub(r"[^A-Za-z0-9_]", "_", text)
    return f"_{name}" if name[:1].isdigit() else name


def unit_path(build: Path, header: str) -> Path:
    """Return the path of the one-line unit of a header in a build directory.

    Args:
        build: The build directory
        header: The header, relative to include/

    Returns:
        <build>/test/layer/sentinels/<layer>/<identifier>.cpp
    """
    return build / "test" / "layer" / "sentinels" / header.split("/")[0] / f"{c_identifier(header)}.cpp"


def threshold_of(budgets: Path) -> int:
    """Return the error threshold of the row header-constexpr-ops.

    Raises:
        ValueError: If the table is missing or malformed, has no such row, or the threshold is not a whole number
        OSError: If the table cannot be read
    """
    row = check_report.read_budgets(budgets).get(CHECK)
    if row is None:
        raise ValueError(f"{budgets} has no row {CHECK}.  Add `{CHECK} | limit | limit | operations | meaning`.")
    if row.error != int(row.error) or row.error < 1:
        raise ValueError(f"the error threshold {row.error} of {CHECK} in {budgets} is not a whole number of "
                         f"operations")
    return int(row.error)


def read_rows(paths: Paths, threshold: int) -> tuple[list[Row], list[tuple[int, str]]]:
    """Read the list of the headers with a higher limit.

    Args:
        paths: The inputs of the run
        threshold: The limit of a header alone

    Returns:
        The rows, and (line, message) for each malformed row
    """
    rows: list[Row] = []
    problems: list[tuple[int, str]] = []
    seen: set[str] = set()
    for number, raw in enumerate(paths.listing.read_text(encoding="utf-8").splitlines(), 1):
        entry = raw.strip()
        if not entry or entry.startswith("#"):
            continue
        cells = [cell.strip() for cell in (entry + " ").split(SEPARATOR)]
        if len(cells) < 3 or not cells[1].isdigit():
            problems.append((number, f"the row is malformed: {entry}  A row is `<layer>/<path>.h{SEPARATOR}limit"
                                     f"{SEPARATOR}reason`."))
            continue
        header, limit, reason = cells[0], int(cells[1]), SEPARATOR.join(cells[2:]).strip()
        if header.split("/")[0] not in LAYERS or not (paths.root / INCLUDE / header).is_file():
            problems.append((number, f"{header} is not a header under {INCLUDE}/.  Remove the row."))
        elif not reason:
            problems.append((number, f"the row of {header} gives no reason.  Say which evaluation needs the higher "
                                     f"limit."))
        elif limit <= threshold:
            problems.append((number, f"the limit {limit} of {header} is not more than the threshold {threshold} of "
                                     f"{BUDGETS}.  Remove the row."))
        elif header in seen:
            problems.append((number, f"{header} has a row before this one.  Remove one of them."))
        else:
            seen.add(header)
            rows.append(Row(header, limit, number))
    return rows, problems


def headers_with_units(root: Path) -> list[str]:
    """Return each header that has a sentinel, relative to include/: each header of a layer that no row of the not-standalone list names.

    Complexity: linear in the number of files under include/.
    """
    listed: set[str] = set()
    not_standalone = root / NOT_STANDALONE
    if not_standalone.is_file():
        for raw in not_standalone.read_text(encoding="utf-8").splitlines():
            entry = raw.strip()
            if entry and not entry.startswith("#"):
                listed.add(entry.split(SEPARATOR)[0].strip())
    found: list[str] = []
    for layer in LAYERS:
        base = root / INCLUDE / layer
        for path in sorted(base.rglob("*.h")) if base.is_dir() else []:
            header = path.relative_to(root / INCLUDE).as_posix()
            if header not in listed:
                found.append(header)
    return found


def read_units(build: Path) -> dict[str, Unit] | None:
    """Return the compile command of each one-line unit in the compile database, by its source path.

    Returns:
        The commands, or None when the database does not exist
    """
    database = build / "compile_commands.json"
    if not database.is_file():
        return None
    units: dict[str, Unit] = {}
    for row in json.loads(database.read_text(encoding="utf-8")):
        source = row.get("file", "")
        if "/test/layer/sentinels/" in source:
            argv = row.get("arguments") or shlex.split(row["command"])
            units[os.path.normpath(source)] = Unit(tuple(argv), source, row["directory"])
    return units


def last_limit(argv: tuple[str, ...]) -> int | None:
    """Return the value of the last -fconstexpr-ops-limit of a command, or None."""
    values = [flag.removeprefix(LIMIT_FLAG) for flag in argv if flag.startswith(LIMIT_FLAG)]
    return int(values[-1]) if values and values[-1].isdigit() else None


def syntax_argv(unit: Unit, limit: int) -> list[str]:
    """Return the command of a unit as a syntax-only compile at one limit, which writes no file.

    Args:
        unit: The compile command of the unit
        limit: The operation limit

    Returns:
        The command
    """
    out: list[str] = []
    index = 0
    while index < len(unit.argv):
        flag = unit.argv[index]
        if flag in ("-o", "-MF", "-MT", "-MQ"):
            index += 2
            continue
        if flag not in ("-c", "-MD", "-MMD", "-MP"):
            out.append(flag)
        index += 1
    return out + ["-fsyntax-only", f"{LIMIT_FLAG}{limit}", "-fdiagnostics-color=never", "-fno-diagnostics-show-caret"]


def shown_path(path: str, directory: str, root: Path) -> str:
    """Return a diagnostic path relative to the repository root when it is in the tree, after each link resolves."""
    resolved = Path(os.path.realpath(Path(directory) / path))
    try:
        return resolved.relative_to(root.resolve()).as_posix()
    except ValueError:
        return resolved.as_posix()


def compile_alone(unit: Unit, limit: int, root: Path) -> Outcome:
    """Compile one header alone at one limit and read the evaluations that exceed it.

    The owner of an evaluation is the first `in 'constexpr' expansion of`
    location before its error: the expression where the evaluation starts.
    An evaluation with no such line starts at its error.

    Args:
        unit: The compile command of the one-line unit
        limit: The operation limit
        root: The repository root

    Returns:
        Whether the compile passes, each owner, and the other errors
    """
    done = subprocess.run(syntax_argv(unit, limit), cwd=unit.directory, capture_output=True, text=True, check=False)
    owners: list[str] = []
    others: list[str] = []
    first: str | None = None
    for line in done.stderr.splitlines():
        match = LOCATION.match(line)
        if match is None:
            continue
        where = f"{shown_path(match['path'], unit.directory, root)}:{match['line']}"
        rest = match["rest"]
        if rest.startswith("in ") and "expansion of" in rest:
            first = first or where
        elif rest.startswith("error:"):
            if OPS_ERROR in rest:
                owners.append(first or where)
            else:
                others.append(f"{where}: {rest}")
            first = None
    if done.returncode != 0 and not owners and not others:
        others.append(f"the compiler exits with {done.returncode} and gives no error line: "
                      f"{' '.join(done.stderr.split())[:300]}")
    return Outcome(done.returncode == 0, tuple(dict.fromkeys(owners)), tuple(others))


def run(paths: Paths, warnings_dir: Path | None) -> int:
    """Compare the list and the compile database with the tree, and print each finding.

    Complexity: two compiles of each listed header, run in parallel, and a
    pass over the compile database.

    Args:
        paths: The inputs of the run
        warnings_dir: The warnings directory of check_report, or None

    Returns:
        0 with no error finding, 1 with one or more
    """
    findings: list[check_report.Finding] = []

    def report(level: check_report.Level, path: str, line: int, message: str) -> None:
        """Add one finding."""
        findings.append(check_report.Finding(level, path, line, CHECK, message))

    try:
        threshold = threshold_of(paths.budgets)
    except (OSError, ValueError) as problem:
        report("error", BUDGETS, 0, f"the guard cannot read the threshold: {problem}")
        return check_report.emit(findings, CHECK, warnings_dir)
    if not paths.listing.is_file():
        report("error", LIST, 0, f"the list does not exist.  test/layer/CMakeLists.txt reads it, so the build "
                                 f"needs it too.")
        return check_report.emit(findings, CHECK, warnings_dir)
    rows, problems = read_rows(paths, threshold)
    for line, message in problems:
        report("error", LIST, line, message)
    units = read_units(paths.build)
    if units is None:
        report("error", LIST, 0, f"{paths.build}/compile_commands.json does not exist, so the guard cannot read the "
                                 f"one-line units.  Configure the build first.")
        return check_report.emit(findings, CHECK, warnings_dir)
    limits = {row.header: row.limit for row in rows}
    chosen: dict[str, Unit] = {}
    for header in headers_with_units(paths.root):
        unit = units.get(os.path.normpath(unit_path(paths.build, header)))
        expected = limits.get(header, threshold)
        if unit is None:
            report("error", f"{INCLUDE}/{header}", 0,
                   f"the compile database holds no one-line unit {unit_path(paths.build, header)} for this header, "
                   f"so no compile holds it to the operation limit.  Configure the build again.")
            continue
        have = last_limit(unit.argv)
        if have != expected:
            report("error", f"{INCLUDE}/{header}", 0,
                   f"the one-line unit of this header compiles with the operation limit {have}, and the limit is "
                   f"{expected}.  test/layer/CMakeLists.txt gives each one-line unit its limit.")
        chosen[header] = unit
    jobs = [(row, limit) for row in rows if row.header in chosen for limit in (threshold, row.limit)]
    with ThreadPoolExecutor(max_workers=max(1, min(16, len(jobs)))) as pool:
        outcomes = dict(zip(jobs, pool.map(lambda job: compile_alone(chosen[job[0].header], job[1], paths.root),
                                           jobs)))
    for row in rows:
        if row.header not in chosen:
            continue
        low, high = outcomes[(row, threshold)], outcomes[(row, row.limit)]
        if not high.passes:
            needs = ", ".join(high.owners) or "; ".join(high.others[:2])
            report("error", LIST, row.line,
                   f"{row.header} does not compile alone at the limit {row.limit} of its row: {needs}.  Make the "
                   f"evaluation lazy or smaller.")
        elif low.passes:
            report("error", LIST, row.line,
                   f"{row.header} compiles alone at the threshold {threshold}, so the higher limit of its row is not "
                   f"necessary.  Remove the row in this commit.")
        elif low.others and not low.owners:
            report("error", LIST, row.line,
                   f"{row.header} does not compile alone at the threshold {threshold}, and the cause is not the "
                   f"operation limit: {'; '.join(low.others[:2])}")
        else:
            report("warning", LIST, row.line,
                   f"{row.header} holds a constant evaluation of more than {threshold} operations, which runs again "
                   f"in each translation unit that includes it: {', '.join(low.owners)}.  Its row gives it the "
                   f"limit {row.limit}.  Make the evaluation a variable template or a member of a template, and "
                   f"remove the row.")
    print(f"check-header-constexpr-ops: threshold {threshold} operations, {len(chosen)} one-line unit(s), "
          f"{len(rows)} row(s).", file=sys.stderr)
    return check_report.emit(findings, CHECK, warnings_dir)


def self_test() -> int:
    """Plant each case in a scratch tree, with the host compiler, and examine each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def expect(name: str, holds: bool, negative: bool = False) -> None:
        """Record one case."""
        nonlocal negatives
        negatives += negative
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    compiler = shutil.which("c++") or shutil.which("g++")
    if compiler is None:
        print("check-header-constexpr-ops --self-test: FAILED — no host C++ compiler")
        return 2
    spin = ("constexpr unsigned spin(unsigned rounds) {\n    unsigned total = 0;\n"
            "    for (unsigned index = 0; index < rounds; ++index) total += index;\n    return total;\n}\n")
    files = {
        "include/crucible/Heavy.h": "#pragma once\n" + spin + "inline constexpr unsigned heavy = spin(2000u);\n",
        "include/crucible/Light.h": "#pragma once\ninline constexpr unsigned light = 3u;\n",
        "include/fixy/Lazy.h": "#pragma once\n" + spin + "template <class T> inline constexpr unsigned lazy = "
                               "spin(2000u + sizeof(T));\n",
        "include/foundation/Body.h": "#pragma once\n" + spin + "inline unsigned body() { constexpr unsigned local = "
                                     "spin(2000u); return local; }\n",
        "include/crucible/Away.h": "#pragma once\n#include <constexpr_ops_absent.h>\n",
    }
    with tempfile.TemporaryDirectory(prefix="constexpr-ops-") as work:
        root = Path(work)
        for rel, text in files.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        (root / NOT_STANDALONE).parent.mkdir(parents=True, exist_ok=True)
        (root / NOT_STANDALONE).write_text("crucible/Away.h | it includes a header that is not on the path\n",
                                           encoding="utf-8")
        budgets = root / BUDGETS
        budgets.parent.mkdir(parents=True, exist_ok=True)
        budget_row = f"{CHECK} | 1000 | 1000 | operations | the planted threshold\n"
        budgets.write_text(budget_row, encoding="utf-8")
        listing = root / LIST
        heavy_row = "crucible/Heavy.h | 100000 | spin(2000) at namespace scope\n"
        build = root / "build"
        paths = Paths(root, budgets, listing, build)
        limits = {"crucible/Heavy.h": 100000}

        def write_database(overrides: dict[str, int | None] | None = None, drop: str = "") -> None:
            """Write a compile database with one one-line unit for each header that has a sentinel."""
            rows = []
            for header in headers_with_units(root):
                if header == drop:
                    continue
                source = unit_path(build, header)
                source.parent.mkdir(parents=True, exist_ok=True)
                source.write_text(f"#include <{header}>\n", encoding="utf-8")
                limit = (overrides or {}).get(header, limits.get(header, 1000))
                argv = [compiler, "-std=c++17", "-I", str(root / INCLUDE), "-fconstexpr-ops-limit=100000000"]
                argv += [] if limit is None else [f"{LIMIT_FLAG}{limit}"]
                rows.append({"directory": str(build), "file": str(source), "output": f"{source}.o",
                             "arguments": argv + ["-c", str(source), "-o", f"{source}.o"]})
            build.mkdir(parents=True, exist_ok=True)
            (build / "compile_commands.json").write_text(json.dumps(rows), encoding="utf-8")

        def verdict() -> tuple[int, str]:
            """Run the guard on the scratch tree and keep its report."""
            buffer = io.StringIO()
            with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(buffer):
                code = run(paths, root / "warnings")
            return code, buffer.getvalue()

        listing.write_text("# planted\n" + heavy_row, encoding="utf-8")
        write_database()
        units = read_units(build) or {}

        def unit_of(header: str) -> Unit:
            """Return the planted unit of one header."""
            return units[os.path.normpath(unit_path(build, header))]

        heavy_low = compile_alone(unit_of("crucible/Heavy.h"), 1000, root)
        expect("an inline constexpr variable at namespace scope that runs 2000 rounds fails at 1000 operations, and "
               "the owner is its initializer", not heavy_low.passes and heavy_low.owners == ("include/crucible/Heavy.h:7",))
        expect("the same header compiles at 100000 operations", compile_alone(unit_of("crucible/Heavy.h"), 100000,
                                                                              root).passes)
        body = compile_alone(unit_of("foundation/Body.h"), 1000, root)
        expect("a constexpr local in a function body that is not a template fails at 1000 operations",
               not body.passes and body.owners == ("include/foundation/Body.h:7",))
        expect("the same evaluation in a variable template that no code instantiates compiles at 1000 operations",
               compile_alone(unit_of("fixy/Lazy.h"), 1000, root).passes, True)
        expect("a header with a constant only compiles at 1000 operations",
               compile_alone(unit_of("crucible/Light.h"), 1000, root).passes, True)

        code, report = verdict()
        expect("a list and a database that agree with the tree pass, with a warning for the listed header",
               code == 0 and f"{LIST}:2: warning: [{CHECK}] crucible/Heavy.h holds a constant evaluation of more "
                             f"than 1000 operations" in report and "include/crucible/Heavy.h:7" in report)
        expect("the warning goes to the warnings file", (root / "warnings" / f"{CHECK}.txt").is_file())
        expect("the unit of the header in the not-standalone list is not asked for", "Away.h" not in report, True)
        listing.write_text(heavy_row + "crucible/Light.h | 5000 | it once held a table\n", encoding="utf-8")
        limits["crucible/Light.h"] = 5000
        write_database()
        code, report = verdict()
        expect("a row whose header compiles at the threshold is an error that asks to remove it",
               code == 1 and f"{LIST}:2: error: [{CHECK}] crucible/Light.h compiles alone at the threshold 1000"
               in report)
        limits.pop("crucible/Light.h")
        listing.write_text("crucible/Heavy.h | 1500 | a limit that is too low\n", encoding="utf-8")
        limits["crucible/Heavy.h"] = 1500
        write_database()
        code, report = verdict()
        expect("a header that needs more than the limit of its row is an error",
               code == 1 and "crucible/Heavy.h does not compile alone at the limit 1500" in report)
        limits["crucible/Heavy.h"] = 100000
        listing.write_text(heavy_row, encoding="utf-8")
        for row, label in (("crucible/Light.h | 500 | lower than the threshold", "a limit not more than the threshold"),
                           ("crucible/Light.h | 5000 | ", "a row with no reason"),
                           ("crucible/Gone.h | 5000 | no such header", "a row that names no header"),
                           ("crucible/Heavy.h | 200000 | a second row", "a second row for one header"),
                           ("crucible/Light.h 5000 reason", "a row without its separators")):
            listing.write_text(heavy_row + row + "\n", encoding="utf-8")
            write_database()
            code, report = verdict()
            expect(f"{label} is an error", code == 1 and f"{LIST}:2: error: [{CHECK}]" in report)
        listing.write_text(heavy_row, encoding="utf-8")
        write_database({"crucible/Light.h": 5000})
        code, report = verdict()
        expect("a unit with a limit other than the threshold is an error",
               code == 1 and "include/crucible/Light.h:0: error:" in report and "limit 5000" in report)
        write_database({"crucible/Light.h": None})
        code, report = verdict()
        expect("a unit with only the limit of the whole build is an error",
               code == 1 and "include/crucible/Light.h:0: error:" in report and "limit 100000000" in report)
        write_database({"crucible/Heavy.h": 1000})
        code, report = verdict()
        expect("a listed header whose unit has the threshold and not the limit of its row is an error",
               code == 1 and "include/crucible/Heavy.h:0: error:" in report)
        write_database(drop="fixy/Lazy.h")
        code, report = verdict()
        expect("a header with no unit in the database is an error",
               code == 1 and "include/fixy/Lazy.h:0: error:" in report and "holds no one-line unit" in report)
        write_database()
        (root / "include/crucible/Broken.h").write_text("#pragma once\n#include <constexpr_ops_absent.h>\n"
                                                        "inline constexpr unsigned broken = spin(5u);\n",
                                                        encoding="utf-8")
        listing.write_text(heavy_row + "crucible/Broken.h | 5000 | it fails for another reason\n", encoding="utf-8")
        limits["crucible/Broken.h"] = 5000
        write_database()
        code, report = verdict()
        expect("a listed header that fails for another reason is an error that names the error",
               code == 1 and "crucible/Broken.h does not compile alone at the limit 5000" in report
               and "constexpr_ops_absent.h" in report)
        limits.pop("crucible/Broken.h")
        (root / "include/crucible/Broken.h").unlink()
        listing.write_text(heavy_row, encoding="utf-8")
        write_database()
        expect("the planted tree passes again", verdict()[0] == 0, True)
        (build / "compile_commands.json").unlink()
        code, report = verdict()
        expect("a missing compile database is an error", code == 1 and "compile_commands.json does not exist" in report)
        write_database()
        listing.unlink()
        code, report = verdict()
        expect("a missing list is an error", code == 1 and "the list does not exist" in report)
        listing.write_text(heavy_row, encoding="utf-8")
        budgets.write_text("other-check | 1 | 2 | s | another row\n", encoding="utf-8")
        code, report = verdict()
        expect("a budget table with no row of the check is an error",
               code == 1 and f"{BUDGETS}:0: error:" in report and f"has no row {CHECK}" in report)
        budgets.write_text(budget_row.replace("1000 | 1000", "1000 | 1000.5"), encoding="utf-8")
        code, report = verdict()
        expect("a threshold that is not a whole number is an error", code == 1 and "not a whole number" in report)
        budgets.write_text(budget_row, encoding="utf-8")
        expect("the identifier of a path agrees with CMake", c_identifier("crucible/cntp/Fec.h") == "crucible_cntp_Fec_h"
               and c_identifier("9lives/x.h") == "_9lives_x_h")
    if failures:
        print(f"check-header-constexpr-ops --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-header-constexpr-ops --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run one mode.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    parser = argparse.ArgumentParser(prog="check-header-constexpr-ops.py")
    parser.add_argument("build_dir", nargs="?", type=Path, help="the configured build directory")
    parser.add_argument("--self-test", action="store_true", help="plant each case and examine each verdict")
    check_report.add_arguments(parser)
    try:
        options = parser.parse_args(argv)
    except SystemExit as stop:
        return 0 if stop.code == 0 else 2
    if options.self_test:
        return self_test()
    if options.build_dir is None:
        print(f"usage: {SCRIPT} BUILD_DIR [--warnings-dir DIR] | --self-test", file=sys.stderr)
        return 2
    paths = Paths(REPO_ROOT, REPO_ROOT / BUDGETS, REPO_ROOT / LIST, options.build_dir.resolve())
    return run(paths, options.warnings_dir)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
