"""One format for the warnings and errors of the compile-time checks, and the measured rules they read.

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
    removes that file when it has no warning.  `guard warnings` prints every
    file of DIR.  DIR is <build>/check-warnings, so each preset has its
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
    slower (utils/assay/core/meter.py, A CI RUNNER).  A check makes the
    finding of a judgment of a measured value with judged().  When
    GITHUB_ACTIONS is "true", judged() gives a warning for an error of a time
    rule, and the message tells so.  An error of a memory rule, of a size rule
    or of an input stays an error.  github_actions() sets or removes the
    variable for the cases of a self-test.

THE KIND AND THE ENTITY
    A finding of a rule with a ledger also has a kind and an entity, and
    assay.core.ledger keys its row by the path, the kind and the entity.  The
    entity holds no line, so a change that only moves code keeps each row.
    The text of the finding does not show the two fields.

THE MEASURED RULES
    A measured rule of utils/registry.txt gives the warning threshold, the
    error threshold and the unit of a quantity (assay.core.registry).
    read_budgets() reads the measured rules, and classify() compares one value
    with one rule.  A value that is equal to a threshold does not exceed it.
    A shell script gets the error threshold of one rule with `loop budget
    --error-threshold RULE`.

`guard selftest core.report` does a test of the module.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import os
import re
import sys
import tempfile
from collections.abc import Iterable, Iterator
from dataclasses import dataclass
from pathlib import Path
from typing import Literal

from assay.core import meter as cost_meter
from assay.core import registry as rule_registry
from assay.core import rows as rows_grammar

Level = Literal["warning", "error"]

REGISTRY = rule_registry.REGISTRY
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
    """One warning or one error of a check, at one place, with the kind and the entity of its ledger key."""

    level: Level
    path: str
    line: int
    check: str
    message: str
    kind: str = ""
    entity: str = ""

    def __post_init__(self) -> None:
        """Refuse a finding that the line format or a ledger row cannot hold.

        Raises:
            ValueError: If the level, the check name, the line, the message, the kind or the entity is not valid
        """
        if self.level not in LEVELS:
            raise ValueError(f"the level {self.level!r} is not one of {LEVELS}")
        if not CHECK_NAME.fullmatch(self.check):
            raise ValueError(f"the check name {self.check!r} is not lowercase words with hyphens")
        if self.line < 0:
            raise ValueError(f"the line {self.line} of {self.path} is negative")
        if not self.path or "\n" in self.path or "\n" in self.message:
            raise ValueError(f"a finding of {self.check} has an empty path or a line break")
        if any(character in self.kind for character in " \t|\n") or "\n" in self.entity:
            raise ValueError(f"the kind {self.kind!r} or the entity of a finding of {self.check} holds a space, a "
                             f"bar or a line break")

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
    """The thresholds of one measured rule.  The unit is its display text, such as "G instructions"."""

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


def write_line(text: str, *, is_error: bool = False) -> None:
    """Write one line to the standard output, or to the standard error when is_error is True.

    The rule job-owners holds that no different module of the package writes to a standard stream.
    """
    print(text, file=sys.stderr if is_error else sys.stdout)


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


def unit_text(unit: str) -> str:
    """Return the display text of the unit of a measured rule: G is 10^9 instructions, and each other unit is itself."""
    return "G instructions" if unit == "G" else unit


def read_budgets(path: Path = REGISTRY) -> dict[str, Budget]:
    """Read the measured rules of the registry.

    Args:
        path: The registry

    Returns:
        The thresholds of each measured rule, by its ID

    Raises:
        ValueError: If a row of the registry does not obey its schema (the error names the line)
        OSError: If the registry cannot be read
    """
    try:
        registry = rule_registry.load(path, str(path))
    except rows_grammar.RowsError as problem:
        raise ValueError(str(problem)) from None
    return {rule.id: Budget(rule.id, rule.threshold.warn, rule.threshold.error, unit_text(rule.threshold.unit),
                            rule.refuses)
            for rule in registry.rules.values() if rule.threshold is not None}


def classify(value: float, budget: Budget) -> Level | None:
    """Compare one measured value with the thresholds of its check.

    Args:
        value: The value, in the unit of the measured rule
        budget: The measured rule

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
                        help="write the warning lines to WARNINGS_DIR/CHECK.txt for `guard warnings`")


def judged(level: Level, path: str, line: int, check: str, message: str) -> Finding:
    """Return the finding of one judgment of a measured value against a threshold or a ledger row.

    On a GitHub runner, an error of a time rule becomes a warning that gives
    the reason (utils/assay/core/meter.py, ci_verdict).  A finding that
    reports an input that the check cannot read is not a judgment, and a
    check makes it with Finding.

    Args:
        level: The level of the judgment on the build host
        path: The place of the finding
        line: The line of the finding, or 0
        check: The name of the check, which is the ID of its measured rule
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
    expect("the measured rules of the repository registry read", bool(budgets))
    with tempfile.TemporaryDirectory(prefix="check-report-") as scratch:
        root = Path(scratch)
        table = root / "registry.txt"
        table.write_text(rule_registry.planted([rule_registry.measured_rule("alpha", 1, 2, "s", "a value"),
                                                rule_registry.measured_rule("beta", 1, 2, "G", "a count")]),
                         encoding="utf-8")
        row = read_budgets(table)["alpha"]
        expect("a measured rule reads", row == Budget("alpha", 1.0, 2.0, "s", "a value"))
        expect("the unit G reads as G instructions", read_budgets(table)["beta"].unit == "G instructions")
        expect("at the warning threshold: none", classify(1.0, row) is None)
        expect("over the warning threshold: warning", classify(1.5, row) == "warning")
        expect("at the error threshold: warning", classify(2.0, row) == "warning")
        expect("over the error threshold: error", classify(2.1, row) == "error")
        alpha = rule_registry.measured_rule("alpha", 1, 2, "s")
        for name, row_text in (("a word threshold", alpha.replace("warn=1", "warn=one")),
                               ("warn over error", alpha.replace("warn=1", "warn=3")),
                               ("an unknown unit", alpha.replace("unit=s", "unit=hours")),
                               ("a missing unit", alpha.replace(" unit=s", "")),
                               ("a second row", f"{alpha}\n{alpha}")):
            table.write_text(rule_registry.planted([row_text]), encoding="utf-8")
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
    expect("--error-threshold prints the rule constexpr-ops of the repository registry",
           found_row == 0 and printed.getvalue().strip().isdigit())
    with contextlib.redirect_stderr(io.StringIO()):
        expect("--error-threshold fails for a missing rule", print_error_threshold("no-such-check") == 1)

    expect("the time rows of cost_meter are the measured rules of unit s of the repository registry",
           cost_meter.TIME_ROWS == {row.check for row in budgets.values() if row.unit == "s"})
    expect("a finding keeps its kind and its entity, and its text does not show them",
           Finding("error", "a.py", 3, "alpha", "m", "regex-call", "f()").text() == "a.py:3: error: [alpha] m")
    refuses("Finding refuses a kind with a space", lambda: Finding("error", "p", 1, "alpha", "m", "a b"))
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
    """Print the error threshold of one measured rule of the registry, for a shell script.

    Args:
        check: The ID of the rule

    Returns:
        0 when the registry has the measured rule, else 1
    """
    try:
        budget = read_budgets().get(check)
    except (OSError, ValueError) as exc:
        print(f"loop budget: the registry cannot be read: {exc}", file=sys.stderr)
        return 1
    if budget is None:
        print(f"loop budget: the registry {REGISTRY} has no measured rule {check}", file=sys.stderr)
        return 1
    print(threshold_text(budget.error))
    return 0


def main(argv: list[str]) -> int:
    """Run the self-test, or print one error threshold (`loop budget --error-threshold RULE`).

    Args:
        argv: --self-test, or --error-threshold RULE

    Returns:
        The exit status of the mode, or 2 on a usage error
    """
    if argv == ["--self-test"]:
        return self_test()
    if len(argv) == 2 and argv[0] == "--error-threshold":
        return print_error_threshold(argv[1])
    print("usage: loop budget --error-threshold RULE | guard selftest core.report", file=sys.stderr)
    return 2
