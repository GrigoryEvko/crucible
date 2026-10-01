#!/usr/bin/env python3
"""One format for the warnings and errors of the compile-time checks, and the budget table they read.

THE FORMAT
    A check reports each finding on one line of its standard output:

        PATH:LINE: warning: [CHECK] MESSAGE
        PATH:LINE: error: [CHECK] MESSAGE

    PATH is relative to the repository root when the finding is in the tree.
    LINE is 0 when no line applies.  CHECK is the name of the check, for
    example compile-cpu.  An error fails the check, and the exit status is 1.
    A warning does not fail the check.  With warnings only, the exit status
    is 0.

THE WARNINGS DIRECTORY
    ctest shows no output of a test that passes, so the ctest output does not
    show the warnings of a check that passes.  A check that gets
    --warnings-dir DIR also writes its warning lines to DIR/CHECK.txt.  It
    removes that file when it has no warning.  report-check-warnings.py prints
    every file of DIR.  DIR is <build>/check-warnings, so each preset has its
    own directory.

    A check of which many processes run at the same time, for example one
    for each test or each fixture, gives each process a key.  Each process
    then writes DIR/CHECK.KEY.txt, and it removes only that file.  A key is a
    word of letters, digits, '_' and '-', so the check name is the part of
    the file name before the first '.'.

GITHUB ANNOTATIONS
    When the environment sets GITHUB_ACTIONS to "true", the check also prints
    each finding as a workflow command.  The CI run then shows the finding as
    an annotation on its file.

A CI RUNNER
    The time thresholds apply to the build host, and a GitHub runner is
    slower (utils/scripts/cost_meter.py, A CI RUNNER).  A check makes the
    finding of a judgment of a measured value with judged().  When
    GITHUB_ACTIONS is "true", judged() gives a warning for an error of a time
    row, and the message tells so.  An error of a memory row, of a size row
    or of an input stays an error.  github_actions() sets or removes the
    variable for the cases of a self-test.

THE BUDGET TABLE
    utils/scripts/budgets.txt gives the warning threshold and the error
    threshold of each check that measures a quantity.  read_budgets() reads
    the table, and classify() compares one value with one row.  A value that
    is equal to a threshold does not exceed it.  A shell script gets the
    error threshold of one row with `check_report.py --error-threshold CHECK`.

Run this file with --self-test to do a test of the module.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import math
import os
import re
import sys
import tempfile
from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from pathlib import Path
from typing import Literal

import cost_meter

Level = Literal["warning", "error"]

BUDGETS = Path(__file__).resolve().parent / "budgets.txt"
# The name of the warnings directory inside a build directory.
WARNINGS_SUBDIR = "check-warnings"
LEVELS: tuple[Level, ...] = ("warning", "error")
# A check name is lowercase words with hyphens, so it is also a safe file name.
CHECK_NAME = re.compile(r"[a-z][a-z0-9]*(?:-[a-z0-9]+)*")
# The key of one writer of a check, for example the name of one test.  It has
# no '.', so the check name of a warnings file is the part before the first '.'.
WRITER_KEY = re.compile(r"[A-Za-z0-9_][A-Za-z0-9_-]*")
FINDING_LINE = re.compile(
    r"(?P<path>.+?):(?P<line>\d+): (?P<level>warning|error): \[(?P<check>[a-z0-9-]+)\] (?P<message>.*)"
)


@dataclass(frozen=True, slots=True)
class Finding:
    """One warning or one error of a check, at one place."""

    level: Level
    path: str
    line: int
    check: str
    message: str

    def __post_init__(self) -> None:
        """Refuse a finding that the line format cannot hold.

        Raises:
            ValueError: If the level, the check name, the line or the message is not valid
        """
        if self.level not in LEVELS:
            raise ValueError(f"the level {self.level!r} is not one of {LEVELS}")
        if not CHECK_NAME.fullmatch(self.check):
            raise ValueError(f"the check name {self.check!r} is not lowercase words with hyphens")
        if self.line < 0:
            raise ValueError(f"the line {self.line} of {self.path} is negative")
        if not self.path or "\n" in self.path or "\n" in self.message:
            raise ValueError(f"a finding of {self.check} has an empty path or a line break")

    def text(self) -> str:
        """Return the finding as one line of the format.

        Returns:
            The line, with no line break at its end
        """
        return f"{self.path}:{self.line}: {self.level}: [{self.check}] {self.message}"

    def annotation(self) -> str:
        """Return the finding as a GitHub workflow command.

        Returns:
            The command, with its properties and its message escaped
        """
        properties = f"file={escape_property(self.path)},title={escape_property(self.check)}"
        if self.line > 0:
            properties += f",line={self.line}"
        return f"::{self.level} {properties}::{escape_data(self.message)}"


@dataclass(frozen=True, slots=True)
class Budget:
    """One row of the budget table: the two thresholds of one check."""

    check: str
    warn: float
    error: float
    unit: str
    meaning: str


def escape_data(text: str) -> str:
    """Escape the message of a workflow command.

    Args:
        text: The message

    Returns:
        The message, with %, carriage return and line feed escaped
    """
    return text.replace("%", "%25").replace("\r", "%0D").replace("\n", "%0A")


def escape_property(text: str) -> str:
    """Escape one property value of a workflow command.

    Args:
        text: The value

    Returns:
        The value, with the characters of escape_data and also ':' and ',' escaped
    """
    return escape_data(text).replace(":", "%3A").replace(",", "%2C")


def parse_line(text: str) -> Finding | None:
    """Read one line of the format back into a finding.

    Args:
        text: One line, with or without its line break

    Returns:
        The finding, or None when the line does not have the format
    """
    match = FINDING_LINE.fullmatch(text.rstrip("\n"))
    if match is None:
        return None
    level: Level = "error" if match["level"] == "error" else "warning"
    return Finding(level, match["path"], int(match["line"]), match["check"], match["message"])


def read_budgets(path: Path = BUDGETS) -> dict[str, Budget]:
    """Read the budget table.

    A row is: check | warn | error | unit | meaning.  A line that starts with
    '#' and an empty line are not rows.

    Args:
        path: The table

    Returns:
        Each row, by its check name

    Raises:
        ValueError: If a row does not have five cells, a threshold is not a finite number,
            the warning threshold is more than the error threshold, or a check has two rows
    """
    budgets: dict[str, Budget] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        cells = [cell.strip() for cell in stripped.split("|")]
        if len(cells) != 5:
            raise ValueError(f"{path}:{number}: a row has five cells (check | warn | error | unit | meaning), "
                             f"and this row has {len(cells)}")
        check, warn_text, error_text, unit, meaning = cells
        if not CHECK_NAME.fullmatch(check):
            raise ValueError(f"{path}:{number}: the check name {check!r} is not lowercase words with hyphens")
        try:
            warn, error = float(warn_text), float(error_text)
        except ValueError:
            raise ValueError(f"{path}:{number}: the thresholds {warn_text!r} and {error_text!r} of {check} "
                             f"must be numbers") from None
        if not (math.isfinite(warn) and math.isfinite(error)) or warn > error:
            raise ValueError(f"{path}:{number}: the warning threshold {warn_text} of {check} must be finite and "
                             f"at most its error threshold {error_text}")
        if check in budgets:
            raise ValueError(f"{path}:{number}: the check {check} has a second row")
        budgets[check] = Budget(check, warn, error, unit, meaning)
    return budgets


def classify(value: float, budget: Budget) -> Level | None:
    """Compare one measured value with the thresholds of its check.

    Args:
        value: The value, in the unit of the row
        budget: The row

    Returns:
        "error" when the value exceeds the error threshold, "warning" when it exceeds only the
        warning threshold, and None when it exceeds neither
    """
    if value > budget.error:
        return "error"
    if value > budget.warn:
        return "warning"
    return None


def add_arguments(parser: argparse.ArgumentParser) -> None:
    """Add the --warnings-dir option that every check takes.

    Args:
        parser: The parser of the check
    """
    parser.add_argument("--warnings-dir", type=Path, default=None,
                        help="write the warning lines to WARNINGS_DIR/CHECK.txt for report-check-warnings.py")


def judged(level: Level, path: str, line: int, check: str, message: str) -> Finding:
    """Return the finding of one judgment of a measured value against a threshold or a ledger row.

    On a GitHub runner, an error of a time row becomes a warning that gives
    the reason (utils/scripts/cost_meter.py, ci_verdict).  A finding that
    reports an input that the check cannot read is not a judgment, and a
    check makes it with Finding.

    Args:
        level: The level of the judgment on the build host
        path: The place of the finding
        line: The line of the finding, or 0
        check: The name of the check, which is the row of the budget table
        message: The message

    Returns:
        The finding
    """
    shown_level, shown_message = cost_meter.ci_verdict(level, check, message)
    return Finding("error" if shown_level == "error" else "warning", path, line, check, shown_message)


@contextlib.contextmanager
def github_actions(is_active: bool) -> Iterator[None]:
    """Set or remove GITHUB_ACTIONS for the body of a with statement, and put the earlier value back after it.

    A self-test uses it, so that each case gives the same verdict on a CI
    runner and on the build host.

    Args:
        is_active: True to set GITHUB_ACTIONS to "true", False to remove it
    """
    earlier = os.environ.get(cost_meter.GITHUB_ACTIONS_ENV)
    if is_active:
        os.environ[cost_meter.GITHUB_ACTIONS_ENV] = "true"
    else:
        os.environ.pop(cost_meter.GITHUB_ACTIONS_ENV, None)
    try:
        yield
    finally:
        if earlier is None:
            os.environ.pop(cost_meter.GITHUB_ACTIONS_ENV, None)
        else:
            os.environ[cost_meter.GITHUB_ACTIONS_ENV] = earlier


def emit(findings: Iterable[Finding], check: str, warnings_dir: Path | None, key: str | None = None) -> int:
    """Print the findings of one check, write its warnings file, and give its exit status.

    Complexity: O(n log n) for n findings, because of the sort.

    Args:
        findings: The findings.  Each one must name this check
        check: The name of the check
        warnings_dir: The warnings directory, or None to write no file
        key: The key of this writer, when many processes of the check run at the same time

    Returns:
        1 when one finding or more is an error, else 0

    Raises:
        ValueError: If a finding names a different check, or the key is not a word of WRITER_KEY
    """
    ordered = sorted(findings, key=lambda found: (found.path, found.line, found.level, found.message))
    strangers = sorted({found.check for found in ordered if found.check != check})
    if strangers:
        raise ValueError(f"the findings of {check} name the checks {strangers}")
    if key is not None and not WRITER_KEY.fullmatch(key):
        raise ValueError(f"the writer key {key!r} of {check} is not a word of letters, digits, '_' and '-'")
    annotate = cost_meter.is_github_actions()
    for found in ordered:
        print(found.text())
        if annotate:
            print(found.annotation())
    if warnings_dir is not None:
        write_warnings(warnings_dir, check, [found for found in ordered if found.level == "warning"], key)
    return 1 if any(found.level == "error" for found in ordered) else 0


def write_warnings(warnings_dir: Path, check: str, warnings: list[Finding], key: str | None = None) -> None:
    """Write the warnings file of one check, or remove it when the check has no warning.

    A reader in another process sees the whole file or no file.

    Args:
        warnings_dir: The warnings directory
        check: The name of the check
        warnings: The warnings of this run
        key: The key of this writer, or None for the one file of the check

    Raises:
        ValueError: If the key is not a word of WRITER_KEY
    """
    if key is not None and not WRITER_KEY.fullmatch(key):
        raise ValueError(f"the writer key {key!r} of {check} is not a word of letters, digits, '_' and '-'")
    target = warnings_dir / (f"{check}.txt" if key is None else f"{check}.{key}.txt")
    if not warnings:
        target.unlink(missing_ok=True)
        return
    warnings_dir.mkdir(parents=True, exist_ok=True)
    staging = target.with_name(f".{target.name}.{os.getpid()}.tmp")
    staging.write_text("".join(f"{found.text()}\n" for found in warnings), encoding="utf-8")
    os.replace(staging, target)


def read_warnings_dir(warnings_dir: Path) -> tuple[list[Finding], list[str]]:
    """Read every warnings file of one warnings directory.

    Complexity: O(n) in the number of lines of the files.

    Args:
        warnings_dir: The directory.  It can be missing

    Returns:
        The warnings in file order, and one problem text for each line that does not have the format
    """
    findings: list[Finding] = []
    problems: list[str] = []
    if not warnings_dir.is_dir():
        return findings, problems
    for path in sorted(warnings_dir.glob("*.txt")):
        check = path.name.split(".", 1)[0]
        for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
            found = parse_line(raw)
            if found is None or found.check != check:
                problems.append(f"{path}:{number}: the line is not a warning of {check}: {raw}")
            else:
                findings.append(found)
    return findings, problems


def self_test() -> int:
    """Do a test of each function, with negative controls.

    Returns:
        0 when every case holds, else 1
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    def refuses(name: str, action: object) -> None:
        try:
            action()  # type: ignore[operator]
        except ValueError:
            expect(name, True)
            return
        expect(name, False)

    sample = Finding("warning", "include/fixy/Refined.h", 12, "compile-cpu", "12.4 s CPU, 50% over 10 s")
    expect("text has the format", sample.text()
           == "include/fixy/Refined.h:12: warning: [compile-cpu] 12.4 s CPU, 50% over 10 s")
    expect("parse_line reads text back", parse_line(sample.text()) == sample)
    expect("parse_line refuses a free line", parse_line("STALE something") is None)
    expect("annotation escapes", Finding("error", "a,b:c.h", 3, "function-size", "100% of x").annotation()
           == "::error file=a%2Cb%3Ac.h,title=function-size,line=3::100%25 of x")
    expect("escape_data escapes line breaks", escape_data("a\r\nb") == "a%0D%0Ab")
    expect("annotation omits line 0", "line=" not in Finding("warning", "build/x.o", 0, "object-text", "m")
           .annotation())
    refuses("Finding refuses a level", lambda: Finding("info", "p", 1, "compile-cpu", "m"))  # type: ignore[arg-type]
    refuses("Finding refuses a check name", lambda: Finding("error", "p", 1, "Compile_CPU", "m"))
    refuses("Finding refuses a line break", lambda: Finding("error", "p", 1, "compile-cpu", "a\nb"))

    budgets = read_budgets()
    expect("the repository table reads", bool(budgets))
    with tempfile.TemporaryDirectory(prefix="check-report-") as scratch:
        root = Path(scratch)
        table = root / "budgets.txt"
        table.write_text("# comment\n\nalpha | 1 | 2 | s | a value\n", encoding="utf-8")
        row = read_budgets(table)["alpha"]
        expect("a row reads", row == Budget("alpha", 1.0, 2.0, "s", "a value"))
        expect("at the warning threshold: none", classify(1.0, row) is None)
        expect("over the warning threshold: warning", classify(1.5, row) == "warning")
        expect("at the error threshold: warning", classify(2.0, row) == "warning")
        expect("over the error threshold: error", classify(2.1, row) == "error")
        for name, body in (("four cells", "alpha | 1 | 2 | s\n"),
                           ("a word threshold", "alpha | one | 2 | s | m\n"),
                           ("warn over error", "alpha | 3 | 2 | s | m\n"),
                           ("an infinite threshold", "alpha | 1 | inf | s | m\n"),
                           ("a second row", "alpha | 1 | 2 | s | m\nalpha | 1 | 2 | s | m\n")):
            table.write_text(body, encoding="utf-8")
            refuses(f"read_budgets refuses {name}", lambda: read_budgets(table))

        warnings_dir = root / WARNINGS_SUBDIR
        warning = Finding("warning", "b.h", 2, "alpha", "warn")
        error = Finding("error", "a.h", 1, "alpha", "fail")
        expect("an error gives status 1", emit([warning, error], "alpha", warnings_dir) == 1)
        written = (warnings_dir / "alpha.txt").read_text(encoding="utf-8")
        expect("the file holds the warning only", written == f"{warning.text()}\n")
        found, problems = read_warnings_dir(warnings_dir)
        expect("read_warnings_dir reads it", found == [warning] and not problems)
        expect("warnings only give status 0", emit([warning], "alpha", warnings_dir) == 0)
        expect("no warning removes the file", emit([], "alpha", warnings_dir) == 0
               and not (warnings_dir / "alpha.txt").exists())
        refuses("emit refuses a finding of another check",
                lambda: emit([Finding("error", "a.h", 1, "beta", "m")], "alpha", None))
        warnings_dir.mkdir(exist_ok=True)
        (warnings_dir / "alpha.txt").write_text("not a finding\n", encoding="utf-8")
        found, problems = read_warnings_dir(warnings_dir)
        expect("read_warnings_dir reports a bad line", not found and len(problems) == 1)
        expect("a missing directory reads empty", read_warnings_dir(root / "missing") == ([], []))
        (warnings_dir / "alpha.txt").unlink()

        first = Finding("warning", "neg/one.cpp", 0, "alpha", "one")
        second = Finding("warning", "neg/two.cpp", 0, "alpha", "two")
        expect("a writer with a key gives status 0", emit([first], "alpha", warnings_dir, "neg_one") == 0
               and emit([second], "alpha", warnings_dir, "neg-two") == 0)
        expect("each writer writes its own file", sorted(path.name for path in warnings_dir.glob("*.txt"))
               == ["alpha.neg-two.txt", "alpha.neg_one.txt"])
        found, problems = read_warnings_dir(warnings_dir)
        expect("read_warnings_dir reads the file of each writer", sorted(found, key=lambda item: item.message)
               == [first, second] and not problems)
        expect("a writer with no warning removes only its own file", emit([], "alpha", warnings_dir, "neg_one") == 0
               and [path.name for path in warnings_dir.glob("*.txt")] == ["alpha.neg-two.txt"])
        (warnings_dir / "alpha.neg-two.txt").write_text(Finding("warning", "x", 0, "beta", "m").text() + "\n",
                                                         encoding="utf-8")
        found, problems = read_warnings_dir(warnings_dir)
        expect("read_warnings_dir reports a line of another check in the file of a writer",
               not found and len(problems) == 1)
        refuses("emit refuses a key with a dot", lambda: emit([first], "alpha", warnings_dir, "a.b"))
        refuses("emit refuses an empty key", lambda: emit([first], "alpha", warnings_dir, ""))

    expect("threshold_text writes a whole number with no exponent", threshold_text(33554432.0) == "33554432")
    expect("threshold_text keeps a fraction", threshold_text(1.25) == "1.25")
    with contextlib.redirect_stdout(io.StringIO()) as printed:
        found_row = print_error_threshold("constexpr-ops")
    expect("--error-threshold prints the row constexpr-ops of the repository table",
           found_row == 0 and printed.getvalue().strip().isdigit())
    with contextlib.redirect_stderr(io.StringIO()):
        expect("--error-threshold fails for a missing row", print_error_threshold("no-such-check") == 1)

    expect("the time rows of cost_meter are the rows of unit s of the repository table",
           cost_meter.TIME_ROWS == {row.check for row in budgets.values() if row.unit == "s"})
    earlier = os.environ.get(cost_meter.GITHUB_ACTIONS_ENV)
    with github_actions(False):
        expect("off a CI runner, a time error stays an error",
               judged("error", "t.cpp", 0, "test-time", "slow") == Finding("error", "t.cpp", 0, "test-time", "slow"))
        os.environ[cost_meter.GITHUB_ACTIONS_ENV] = "false"
        expect("GITHUB_ACTIONS=false is not a CI runner", judged("error", "t.cpp", 0, "compile-cpu", "m").level == "error")
    with github_actions(True):
        demoted = judged("error", "t.cpp", 0, "test-time", "slow")
        expect("on a CI runner, a time error is a warning that says that it was demoted",
               demoted.level == "warning" and demoted.message == f"slow.  {cost_meter.CI_DEMOTION}.")
        expect("on a CI runner, a memory error stays an error",
               judged("error", "t.cpp", 0, "test-memory", "big") == Finding("error", "t.cpp", 0, "test-memory", "big"))
        expect("on a CI runner, an error of a row that is not a time stays an error",
               judged("error", "t.o", 0, "function-size", "f").level == "error")
        expect("on a CI runner, a time warning keeps its message",
               judged("warning", "t.cpp", 0, "link-time", "w") == Finding("warning", "t.cpp", 0, "link-time", "w"))
        with contextlib.redirect_stdout(io.StringIO()) as printed:
            status = emit([demoted], "test-time", None)
        expect("on a CI runner, a demoted time error gives status 0 and an annotation",
               status == 0 and "::warning file=t.cpp,title=test-time::" in printed.getvalue())
    expect("github_actions() puts the earlier value back", os.environ.get(cost_meter.GITHUB_ACTIONS_ENV) == earlier)

    if failures:
        print(f"check_report --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("check_report --self-test: every case holds.")
    return 0


def threshold_text(value: float) -> str:
    """Return a threshold as the text of a number, with no exponent and no '.0' for a whole number.

    Args:
        value: The threshold

    Returns:
        The text
    """
    return str(int(value)) if value.is_integer() else repr(value)


def print_error_threshold(check: str) -> int:
    """Print the error threshold of one row of the budget table, for a shell script.

    Args:
        check: The check name of the row

    Returns:
        0 when the table has the row, else 1
    """
    try:
        budget = read_budgets().get(check)
    except (OSError, ValueError) as exc:
        print(f"check_report.py: the budget table cannot be read: {exc}", file=sys.stderr)
        return 1
    if budget is None:
        print(f"check_report.py: the budget table {BUDGETS} has no row {check}", file=sys.stderr)
        return 1
    print(threshold_text(budget.error))
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    if len(sys.argv) == 3 and sys.argv[1] == "--error-threshold":
        sys.exit(print_error_threshold(sys.argv[2]))
    print("usage: check_report.py --self-test | --error-threshold CHECK", file=sys.stderr)
    sys.exit(2)
