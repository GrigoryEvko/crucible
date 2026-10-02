#!/usr/bin/env python3
"""check-budget-history — a budget threshold or a ledger that grew stays visible, and a threshold rises only with a measurement.

CLAUDE.md §XV ("The budgets of the tree") gives the rule: change a threshold
only with a measurement, and give the numbers in the commit.  This guard reads
the git history and holds the rule after the commit lands.

WHAT THE GUARD READS
    * Each commit that is not a merge and that changed utils/scripts/budgets.txt
      or a tracked utils/scripts/*-ledger.txt: its first parent, its subject,
      its body, and each such file at the commit and at the parent.  The guard
      runs only `git rev-parse`, `git ls-files`, `git log` and `git cat-file`.
      A merge brings a change that a commit of the other side made, and the
      guard reads that commit.
    * Each path that a commit of the quarantine ledger changed, and each file
      that git reports as moved in it (`git log --full-diff --find-renames`).
    * The working-tree copy of each of these files.  It can hold an edit that
      no commit holds yet.
    * utils/scripts/budget-history-ledger.txt: the rises of the past whose
      commit body gives no measurement.

THE FINDINGS
    * A threshold (the warning or the error column of one row) above its
      lowest committed value gives a warning on each run.  The warning names
      each commit that raised the threshold after the newest commit at the
      lowest value, with its subject and the old and the new value.  A value
      of a row with another unit starts a new history of that row.
    * A ledger whose row count is above its lowest committed count gives a
      warning on each run, which names each commit that added rows after the
      newest commit at the lowest count.  A ledger of a row with the unit %
      holds the baselines of a check against a baseline, which --write writes
      again.  It admits nothing, and the guard does not count its rows.  The
      rows of utils/scripts/quarantine-ledger.txt are counts, so the guard
      reads that ledger as the next item says, and it does not count its rows.
    * A commit that raised a count of the quarantine ledger, and that changes
      neither the plugin of utils/tools/quarantine/ nor the rule table
      utils/scripts/layer-rules.txt, gives an error.  A row of the admission
      ledger with the row name quarantine-ledger and the column error changes
      the error to a warning.  Each raised count that stays above its count
      before the commit gives a warning on each run, which names the commit.
      No count rises in a change of the ledger format if no total of a kind
      rises, in a file that git moved, or in a new configuration.  A count of
      a configuration other than the base rises only with its difference
      from the base, and not when it comes back to a count of its history.
      Its raise is no error after a change of the plugin or the rule table
      that did not write its rows (utils/scripts/check-quarantine-ratchet.py,
      A CONFIGURATION THAT ONLY CI BUILDS).  A change of the format starts a
      new history of each count.
    * A commit that raised a threshold, and whose body holds no measurement of
      the row, gives an error.  A row of the admission ledger that names the
      commit, the row and the column changes the error to a warning.
    * A row of the admission ledger that names no such rise gives an error.
    The guard reads only a row that the budget table holds, with its unit in
    the working tree.  The working tree has no commit body.  A rise in the
    working tree gives a warning only, and the error comes when the rise lands
    without a measurement.

THE MEASUREMENT
    A measurement of a row is a number with the unit of the row: "s" for
    seconds, "G" for G instructions, "GB", "MB", "KB", "%", "operations",
    "elements", or "x" for a multiple.  The number is none of the values that
    the row had in a column at a commit or in the working tree.  It counts in
    a sentence that holds the name of the row as a word.  It also counts in
    each other sentence when it is above the old threshold and at most the
    new one: the measured value that the old threshold refused.
    A squash landing makes a second commit with the same change.  A rise is
    measured when one commit of the history that made the same step (the
    row, the column, the old and the new value) gives a measurement.

WHY THE RULE CANNOT BE MET BY ACCIDENT
    A number with no unit, a count of files, a date or a commit hash is not a
    number with the unit of the row.  A body that only tells the change
    ("raise compile-cpu from 10 s to 20 s") fails, because the guard does not
    count a value that the row ever had.  A body passes only with a quantity
    in the unit of the row that no threshold of the row ever was.  That
    quantity is in a sentence about the row, or inside the interval of the
    rise.  Such a quantity is a value that someone measured.  A body can hold
    an invented number, but no body passes that gives no quantity of the row.

NOT APPLICABLE
    The guard exits 3, with the reason, when the tree is not a git work tree,
    or when the history is shallow.  A shallow history does not hold the
    lowest values.

Usage
    check-budget-history.py [--root DIR] [--warnings-dir DIR]
    check-budget-history.py --self-test

Exit 0 with no finding or with warnings only, 1 with an error, 2 on a usage error
or a failed self-test, 3 when the guard does not apply to the tree.
"""

from __future__ import annotations

import argparse
import contextlib
import importlib.util
import io
import math
import os
import re
import subprocess
import sys
import tempfile
from dataclasses import dataclass, field
from typing import Any
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
import throwaway_repo  # noqa: E402
from repo_root import REPO_ROOT  # noqa: E402

CHECK = "budget-history"
BUDGETS = "utils/scripts/budgets.txt"
LEDGER_GLOB = "utils/scripts/*-ledger.txt"
LEDGER_SUFFIX = "-ledger.txt"
ADMISSIONS = "utils/scripts/budget-history-ledger.txt"
QUARANTINE_LEDGER = "utils/scripts/quarantine-ledger.txt"
# The admission row name of a raise of the quarantine ledger.
QUARANTINE_ROW = "quarantine-ledger"
# A commit that changes one of these paths can change what the quarantine plugin reports.
QUARANTINE_MACHINERY = ("utils/tools/quarantine/", "utils/scripts/layer-rules.txt")
RATCHET = Path(__file__).resolve().parent / "check-quarantine-ratchet.py"
TOTAL = "(the total of the kind)"
SHOWN_RISES = 8
COLUMNS = ("warn", "error")
WORKING_TREE = "the working tree"
NOT_APPLICABLE = 3
BASELINE_UNIT = "%"
# The separators of the fields and the records of `git log`, which no subject or body holds.
FIELD = "\x1f"
RECORD = "\x1e"
COMMIT_PREFIX = re.compile(r"[0-9a-f]{9,40}")
SENTENCE_END = re.compile(r"(?<=[.!?])\s+|\n+")
NUMBER = r"(\d+(?:[.,]\d+)*)"


class NotApplicable(Exception):
    """The tree has no whole git history."""


class AdmissionError(ValueError):
    """A row of the admission ledger is malformed."""


@dataclass(frozen=True, slots=True)
class Commit:
    """One commit that changed a watched file: its hash, its subject, its body and the watched files it changed."""

    sha: str
    subject: str
    body: str
    paths: tuple[str, ...]

    @property
    def short(self) -> str:
        """The hash in 9 characters."""
        return self.sha[:9]


@dataclass(frozen=True, slots=True)
class Row:
    """The two thresholds and the unit of one row of the budget table."""

    warn: float
    error: float
    unit: str

    def value(self, column: str) -> float:
        """Return the threshold of one column."""
        return self.warn if column == "warn" else self.error


@dataclass(frozen=True, slots=True)
class Rise:
    """One commit, or the working tree, that raised a threshold or the row count of a ledger."""

    where: str
    subject: str
    old: float
    new: float
    sha: str = ""

    def text(self) -> str:
        """Return the rise as words for a message."""
        return f"{self.where} ({self.subject}) from {number_text(self.old)} to {number_text(self.new)}"


@dataclass(frozen=True, slots=True)
class Change:
    """The paths that one commit changed, and the old path of each file that it moved."""

    paths: frozenset[str]
    moves: dict[str, str]


@dataclass(slots=True)
class History:
    """The states of the watched files: at each commit (newest first), at its parent, at HEAD and in the working tree.

    `changes` holds each path that a commit of the quarantine ledger changed.
    """

    commits: list[Commit]
    at: dict[tuple[str, str], str | None]
    before: dict[tuple[str, str], str | None]
    head: dict[str, str | None]
    working: dict[str, str | None]
    ledgers: list[str] = field(default_factory=list)
    changes: dict[str, Change] = field(default_factory=dict)


@dataclass(frozen=True, slots=True)
class Admission:
    """One row of the admission ledger."""

    line: int
    commit: str
    row: str
    column: str
    reason: str


def number_text(value: float) -> str:
    """Return a value with no exponent, and with no '.0' for a whole number."""
    return check_report.threshold_text(float(value))


def git(root: Path, *arguments: str, stdin: str | None = None) -> str:
    """Run one read-only git command in the tree, and return its output.

    Raises:
        NotApplicable: If git cannot run in the tree
    """
    try:
        proc = subprocess.run(["git", "-C", str(root), *arguments], input=stdin, capture_output=True, text=True,
                              check=False)
    except OSError as exc:
        raise NotApplicable(f"git cannot run: {exc}") from None
    if proc.returncode != 0:
        raise NotApplicable(f"`git {' '.join(arguments[:2])}` failed in {root}: {proc.stderr.strip()}")
    return proc.stdout


def read_objects(root: Path, names: list[str]) -> list[str | None]:
    """Read the content of each object name (REVISION:PATH) with one `git cat-file --batch`, or None for a missing one.

    Complexity: linear in the size of the objects.
    """
    if not names:
        return []
    try:
        proc = subprocess.run(["git", "-C", str(root), "cat-file", "--batch"], input="".join(f"{name}\n" for name in
                                                                                              names).encode(),
                              capture_output=True, check=False)
    except OSError as exc:
        raise NotApplicable(f"git cannot run: {exc}") from None
    if proc.returncode != 0:
        raise NotApplicable(f"`git cat-file --batch` failed in {root}: {proc.stderr.decode(errors='replace').strip()}")
    output = proc.stdout
    contents: list[str | None] = []
    position = 0
    for _ in names:
        end = output.find(b"\n", position)
        if end < 0:
            raise NotApplicable(f"the output of `git cat-file --batch` in {root} ends before its last object")
        header = output[position:end].decode(errors="replace").split()
        position = end + 1
        if len(header) == 3 and header[1] == "blob":
            size = int(header[2])
            contents.append(output[position:position + size].decode("utf-8", errors="replace"))
            position += size + 1
        elif len(header) == 3:
            position += int(header[2]) + 1
            contents.append(None)
        else:
            contents.append(None)
    return contents


def read_history(root: Path) -> History:
    """Read the commits that changed the watched files, and each watched file at each commit, its parent and the working tree.

    Raises:
        NotApplicable: If the tree is not a git work tree, or its history is shallow
    """
    top = git(root, "rev-parse", "--show-toplevel").strip()
    if Path(top).resolve() != root.resolve():
        raise NotApplicable(f"{root} is not the top of a git work tree")
    if git(root, "rev-parse", "--is-shallow-repository").strip() == "true":
        raise NotApplicable("the history is shallow, and a shallow history does not hold the lowest values")
    ledgers = sorted(path for path in git(root, "ls-files", "--", LEDGER_GLOB).split() if path.endswith(LEDGER_SUFFIX))
    watched = [BUDGETS, *ledgers]
    log = git(root, "log", "--no-merges", "--full-history", "--topo-order",
              f"--format={RECORD}%H{FIELD}%s{FIELD}%b{FIELD}", "--name-only", "--", *watched)
    commits: list[Commit] = []
    for record in log.split(RECORD)[1:]:
        sha, subject, body, names = record.split(FIELD, 3)
        paths = tuple(name for name in names.split() if name in watched)
        commits.append(Commit(sha, subject.strip(), body.strip(), paths))
    requests = [(commit.sha, path) for commit in commits for path in commit.paths]
    names = ([f"{sha}:{path}" for sha, path in requests] + [f"{sha}^1:{path}" for sha, path in requests]
             + [f"HEAD:{path}" for path in watched])
    contents = read_objects(root, names)
    count = len(requests)
    at = {request: contents[index] for index, request in enumerate(requests)}
    before = {request: contents[count + index] for index, request in enumerate(requests)}
    head = {path: contents[2 * count + index] for index, path in enumerate(watched)}
    working: dict[str, str | None] = {}
    for path in watched:
        file = root / path
        working[path] = file.read_text(encoding="utf-8", errors="replace") if file.is_file() else None
    changes = read_changes(root) if QUARANTINE_LEDGER in ledgers else {}
    return History(commits, at, before, head, working, ledgers, changes)


def read_changes(root: Path) -> dict[str, Change]:
    """Read each path that each commit of the quarantine ledger, the plugin or the rule table changed.

    The result keeps the order of `git log --topo-order`, the newest commit first, and it holds the moves that git
    finds.  Complexity: linear in the size of the log.
    """
    log = git(root, "log", "--no-merges", "--full-history", "--topo-order", "--full-diff", "--find-renames",
              "--name-status", f"--format={RECORD}%H", "--", QUARANTINE_LEDGER, *QUARANTINE_MACHINERY)
    changes: dict[str, Change] = {}
    for record in log.split(RECORD)[1:]:
        lines = record.splitlines()
        paths: set[str] = set()
        moves: dict[str, str] = {}
        for line in lines[1:]:
            cells = line.split("\t")
            if len(cells) == 3 and cells[0].startswith("R"):
                moves[cells[2]] = cells[1]
                paths.update(cells[1:])
            elif len(cells) >= 2:
                paths.update(cells[1:])
        changes[lines[0].strip()] = Change(frozenset(paths), moves)
    return changes


def parse_budgets(text: str | None) -> dict[str, Row]:
    """Read the rows of one version of the budget table, and skip a line that is not a row of five cells with numbers."""
    rows: dict[str, Row] = {}
    for raw in (text or "").splitlines():
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            continue
        cells = [cell.strip() for cell in stripped.split("|")]
        if len(cells) != 5:
            continue
        try:
            warn, error = float(cells[1]), float(cells[2])
        except ValueError:
            continue
        if math.isfinite(warn) and math.isfinite(error):
            rows[cells[0]] = Row(warn, error, cells[3])
    return rows


def count_rows(text: str | None) -> int | None:
    """Return the number of rows of one version of a ledger: each line that is not empty and not a comment."""
    if text is None:
        return None
    return sum(1 for raw in text.splitlines() if raw.strip() and not raw.strip().startswith("#"))


def budget_line(text: str | None, row: str) -> int:
    """Return the line of a row in the budget table, or 0."""
    for number, raw in enumerate((text or "").splitlines(), start=1):
        if raw.split("|", 1)[0].strip() == row and "|" in raw:
            return number
    return 0


def unit_pattern(unit: str) -> re.Pattern[str]:
    """Return the pattern of a number with the unit of a row: its first word, or "x" for a multiple."""
    word = unit.split()[0] if unit.split() else unit
    tail = r"(?![\w%])" if word[-1:].isalnum() else ""
    return re.compile(rf"(?<![\w.]){NUMBER}\s?{re.escape(word)}{tail}")


def has_measurement(body: str, row: str, unit: str, known: set[float], old: float, new: float) -> bool:
    """Tell whether the body holds a measurement of the row for a rise from `old` to `new`.

    A measurement is a number with the unit of the row that no threshold of
    the row was (`known`).  It counts in a sentence that names the row.  It
    counts in each sentence when it is above `old` and at most `new`: the
    measured value that the old threshold refused.

    Complexity: linear in the length of the body.
    """
    name = re.compile(rf"(?<![\w-]){re.escape(row)}(?![\w-])")
    quantity = unit_pattern(unit)
    for sentence in SENTENCE_END.split(body):
        names_row = name.search(sentence) is not None
        for match in quantity.finditer(sentence):
            value = float(match[1].replace(",", ""))
            if any(math.isclose(value, item, rel_tol=1e-9, abs_tol=1e-12) for item in known):
                continue
            if names_row or old < value <= new:
                return True
    return False


def read_admissions(path: Path) -> list[Admission]:
    """Read the admission ledger: COMMIT | ROW | COLUMN | REASON.

    Raises:
        AdmissionError: If a row is malformed or repeated
    """
    admissions: list[Admission] = []
    if not path.is_file():
        return admissions
    seen: set[tuple[str, str, str]] = set()
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        text = raw.strip()
        if not text or text.startswith("#"):
            continue
        cells = [cell.strip() for cell in text.split(" | ")]
        if (len(cells) != 4 or not COMMIT_PREFIX.fullmatch(cells[0]) or cells[2] not in COLUMNS or not cells[1]
                or not cells[3]):
            raise AdmissionError(f"{ADMISSIONS}:{number}: a row is `COMMIT | ROW | warn|error | REASON`, with a commit "
                                 f"of at least 9 hexadecimal characters, and this row is: {text}")
        key = (cells[0], cells[1], cells[2])
        if key in seen:
            raise AdmissionError(f"{ADMISSIONS}:{number}: the commit {cells[0]} has a second row for the column "
                                 f"{cells[2]} of {cells[1]}")
        seen.add(key)
        admissions.append(Admission(number, cells[0], cells[1], cells[2], cells[3]))
    return admissions


def rises_after_lowest(states: list[float | None], rises: list[tuple[int, Rise]], current: float | None,
                       working_rise: Rise | None) -> tuple[float, list[Rise]] | None:
    """Return the lowest committed value and the rises after the newest state at it, when the current value is above it.

    `states` holds the value at each commit, newest first, and None where no value applies.  `rises` holds each rise
    with the index of its commit.
    """
    committed = [value for value in states if value is not None]
    if current is None or not committed:
        return None
    lowest = min(committed)
    if current <= lowest:
        return None
    newest_lowest = next(index for index, value in enumerate(states) if value is not None and value == lowest)
    named = [rise for index, rise in rises if index < newest_lowest]
    if working_rise is not None:
        named.append(working_rise)
    return lowest, named


def evaluate(history: History, admissions: list[Admission]) -> list[check_report.Finding]:
    """Compare each threshold and each ledger with its history, and each rise with the body of its commit.

    Complexity: O(c * r) for c commits and r rows of the budget table.
    """
    findings: list[check_report.Finding] = []
    commits = history.commits
    current_text = history.working.get(BUDGETS)
    current = parse_budgets(current_text)
    tables = [parse_budgets(history.at.get((commit.sha, BUDGETS))) if BUDGETS in commit.paths else None
              for commit in commits]
    parents = [parse_budgets(history.before.get((commit.sha, BUDGETS))) if BUDGETS in commit.paths else None
               for commit in commits]
    last_committed = parse_budgets(history.head.get(BUDGETS))
    bodies = {commit.sha: commit.body for commit in commits}
    used: set[int] = set()
    for row_name, row in sorted(current.items()):
        known = {value for table in [*tables, *parents, current] if table
                 for other in [table.get(row_name)] if other is not None for value in (other.warn, other.error)}
        line = budget_line(current_text, row_name)
        for column in COLUMNS:
            states: list[float | None] = []
            rises: list[tuple[int, Rise]] = []
            for index, (commit, table, parent) in enumerate(zip(commits, tables, parents)):
                if table is None or parent is None:
                    states.append(None)
                    continue
                new_row, old_row = table.get(row_name), parent.get(row_name)
                states.append(new_row.value(column) if new_row is not None and new_row.unit == row.unit else None)
                if (new_row is None or old_row is None or new_row.unit != row.unit or old_row.unit != row.unit
                        or new_row.value(column) <= old_row.value(column)):
                    continue
                rises.append((index, Rise(commit.short, commit.subject, old_row.value(column), new_row.value(column),
                                          commit.sha)))
            # A squash landing makes a second commit with the same change.  One measured commit covers each
            # commit that made the same step.
            measured = {(rise.old, rise.new) for _, rise in rises
                        if has_measurement(bodies[rise.sha], row_name, row.unit, known, rise.old, rise.new)}
            for _, rise in rises:
                if (rise.old, rise.new) in measured:
                    continue
                admitted = next((item for item in admissions if rise.sha.startswith(item.commit)
                                 and (item.row, item.column) == (row_name, column)), None)
                said = (f"the commit {rise.text()} raised the {column} threshold of {row_name}, and no commit of "
                        f"this step names a measurement: a number in {row.unit} that no threshold of the row was, in a "
                        f"sentence with the row name or above the old threshold and at most the new one")
                if admitted is not None:
                    used.add(admitted.line)
                    findings.append(check_report.Finding("warning", ADMISSIONS, admitted.line, CHECK,
                                                         f"{said}.  The row admits it: {admitted.reason}"))
                else:
                    findings.append(check_report.Finding(
                        "error", BUDGETS, line, CHECK,
                        f"{said}.  Measure the quantity, and add a row to {ADMISSIONS} that gives the commit, the "
                        f"row, the column and the measurement"))
            old_row = last_committed.get(row_name)
            working_rise = (Rise(WORKING_TREE, "not committed", old_row.value(column), row.value(column))
                            if old_row is not None and old_row.unit == row.unit
                            and row.value(column) > old_row.value(column) else None)
            above = rises_after_lowest(states, rises, row.value(column), working_rise)
            if above is not None:
                lowest, named = above
                raised = "; ".join(rise.text() for rise in named) if named else "by no single commit"
                findings.append(check_report.Finding(
                    "warning", BUDGETS, line, CHECK,
                    f"the {column} threshold of {row_name} is {number_text(row.value(column))} {row.unit}, above its "
                    f"lowest committed value {number_text(lowest)}.  Raised: {raised}.  Lower it again when a "
                    f"measurement permits"))
    findings.extend(evaluate_quarantine(history, admissions, used))
    for item in admissions:
        if item.line not in used:
            findings.append(check_report.Finding(
                "error", ADMISSIONS, item.line, CHECK,
                f"the row names the commit {item.commit}, the row {item.row} and the column {item.column}, and no "
                f"rise of the history without a measurement agrees with it.  Remove the row"))
    findings.extend(evaluate_ledgers(history, current))
    return findings


def evaluate_ledgers(history: History, budgets: dict[str, Row]) -> list[check_report.Finding]:
    """Compare the row count of each ledger that admits items with its lowest committed count."""
    findings: list[check_report.Finding] = []
    for path in history.ledgers:
        row_name = Path(path).name.removesuffix(LEDGER_SUFFIX)
        if path == QUARANTINE_LEDGER or budgets.get(row_name, Row(0, 0, "")).unit == BASELINE_UNIT:
            continue
        states: list[float | None] = []
        rises: list[tuple[int, Rise]] = []
        for index, commit in enumerate(history.commits):
            if path not in commit.paths:
                states.append(None)
                continue
            new, old = count_rows(history.at.get((commit.sha, path))), count_rows(history.before.get((commit.sha, path)))
            states.append(float(new) if new is not None else None)
            if new is not None and old is not None and new > old:
                rises.append((index, Rise(commit.short, commit.subject, old, new, commit.sha)))
        committed = count_rows(history.head.get(path))
        current = count_rows(history.working.get(path))
        working_rise = (Rise(WORKING_TREE, "not committed", committed, current)
                        if committed is not None and current is not None and current > committed else None)
        above = rises_after_lowest(states, rises, float(current) if current is not None else None, working_rise)
        if above is not None:
            lowest, named = above
            raised = "; ".join(rise.text() for rise in named) if named else "by no single commit"
            findings.append(check_report.Finding(
                "warning", path, 0, CHECK,
                f"the ledger holds {current} rows, more than its lowest committed count {number_text(lowest)}.  Rows "
                f"added: {raised}.  Remove a row when its item is fixed"))
    return findings


# ── The quarantine ledger ──────────────────────────────────────────

# The configuration, the file and the kind of one count of the quarantine ledger.
CountKey = tuple[str, str, str]


@dataclass(frozen=True, slots=True)
class LedgerRise:
    """One count of the quarantine ledger that a commit raised: its configuration, file, kind and the two counts."""

    configuration: str
    file: str
    kind: str
    old: int
    new: int

    def text(self) -> str:
        """Return the rise as words for a message."""
        return f"{self.file} {self.kind} in {self.configuration} from {self.old} to {self.new}"


def load_ratchet() -> Any:
    """Load utils/scripts/check-quarantine-ratchet.py as a module, for its reader of the ledger.

    Its Ledger objects have the type Any here, because the module loads at run time.
    """
    loaded = sys.modules.get("check_quarantine_ratchet")
    if loaded is not None:
        return loaded
    spec = importlib.util.spec_from_file_location("check_quarantine_ratchet", RATCHET)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    # A dataclass of the module looks up its module in sys.modules.
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


def absolute_counts(ledger: Any) -> dict[CountKey, int]:
    """Return the count of each configuration, file and kind of a ledger: the base count plus the difference."""
    return {(name, file, kind): count for name in ledger.configurations
            for (file, kind), count in ledger.expected(name).items()}


def kind_totals(ledger: Any) -> dict[str, int]:
    """Return the sum of the base counts of each kind of a ledger."""
    totals: dict[str, int] = {}
    for (_file, kind), count in ledger.counts.items():
        totals[kind] = totals.get(kind, 0) + count
    return totals


def ledger_rises(before: Any, after: Any, moves: dict[str, str], peaks: dict[CountKey, int]) -> list[LedgerRise]:
    """Return each count that rose from one version of the quarantine ledger to the next.

    * Two versions of different formats compare the total of each kind of the base, because their rows count
      different things.
    * A configuration that the old version does not have is new coverage, and no rise.  The base keeps its history
      when its name changes.
    * A file that git moved keeps the rows of its old path: no rise when the old path has no row after the commit
      and had at least the count before it.
    * A count of a configuration other than the base rises only when its difference from the base rises, so a
      raise of a base count is one rise.  Such a count that comes back to a value of its history, `peaks`, is no
      rise.  A CI leg or a later --write can write such a configuration after a commit that lowered the base rows.

    Complexity: O(r log r) for r counts.
    """
    if before.format != after.format:
        old_totals, new_totals = kind_totals(before), kind_totals(after)
        return [LedgerRise(after.base, TOTAL, kind, old_totals.get(kind, 0), count)
                for kind, count in sorted(new_totals.items()) if count > old_totals.get(kind, 0)]
    # A new name of the base keeps the history of the base.
    old_counts = {(after.base if name == before.base else name, file, kind): count
                  for (name, file, kind), count in absolute_counts(before).items()}
    new_counts = absolute_counts(after)
    rises: list[LedgerRise] = []
    for key in sorted(new_counts):
        name, file, kind = key
        new, old = new_counts[key], old_counts.get(key, 0)
        if new <= old or (name not in before.configurations and name != after.base):
            continue
        source = moves.get(file)
        if (source is not None and old == 0 and new_counts.get((name, source, kind), 0) == 0
                and old_counts.get((name, source, kind), 0) >= new):
            continue
        if name != after.base and (new <= peaks.get(key, 0) or after.deltas.get(name, {}).get((file, kind), 0)
                                   <= before.deltas.get(name, {}).get((file, kind), 0)):
            continue
        rises.append(LedgerRise(name, file, kind, old, new))
    return rises


def changes_machinery(change: Change) -> bool:
    """Tell whether a commit changed the quarantine plugin or the rule table."""
    return any(path == item or path.startswith(item) for path in change.paths for item in QUARANTINE_MACHINERY)


def evaluate_quarantine(history: History, admissions: list[Admission], used: set[int]) -> list[check_report.Finding]:
    """Read each raise of the quarantine ledger in the history and in the working tree.

    * A commit that raised a count and that changes neither the plugin nor the rule table gives an error, unless a
      row of the admission ledger with the row name quarantine-ledger names the commit.
    * A configuration other than the base is stale after a commit of the plugin or the rule table that did not
      write its rows.  A raise of a stale configuration is no error, and the commit that writes its rows makes it
      current again.  A CI leg writes the rows of an aarch64 configuration only after the change of the plugin.
    * A raised count that stays above its value before the commit gives a warning on each run, which names the
      commit.  A change of the format starts a new history of each row.
    * A raise in the working tree gives a warning only.

    Complexity: O(c * r) for c commits of the ledger, the plugin and the rule table, and r counts.
    """
    if QUARANTINE_LEDGER not in history.ledgers:
        return []
    ratchet = load_ratchet()
    parsed: dict[str, Any] = {}

    def parse(text: str | None, where: str) -> Any:
        """Read one version of the ledger, one time for each text, or None for no text or a malformed text."""
        if text is None:
            return None
        if text not in parsed:
            try:
                parsed[text] = ratchet.parse_ledger(text, where)
            except ratchet.LedgerError:
                parsed[text] = None
        return parsed[text]

    findings: list[check_report.Finding] = []
    peaks: dict[CountKey, int] = {}
    standing: list[tuple[Commit, LedgerRise]] = []
    stale: set[str] = set()
    known: set[str] = set()
    last_format = 0
    by_sha = {commit.sha: commit for commit in history.commits if QUARANTINE_LEDGER in commit.paths}
    for sha, change in reversed(list(history.changes.items())):
        commit = by_sha.get(sha)
        if commit is None:
            stale |= known if changes_machinery(change) else set()
            continue
        after = parse(history.at.get((commit.sha, QUARANTINE_LEDGER)), f"{commit.short}:{QUARANTINE_LEDGER}")
        before = parse(history.before.get((commit.sha, QUARANTINE_LEDGER)), f"{commit.short}^:{QUARANTINE_LEDGER}")
        if after is None:
            continue
        if before is None or before.format != after.format:
            peaks, standing = {}, []
        last_format = after.format
        rises = ledger_rises(before, after, change.moves, peaks) if before is not None else []
        for key, count in absolute_counts(after).items():
            peaks[key] = max(peaks.get(key, 0), count)
        standing += [(commit, rise) for rise in rises if rise.file != TOTAL]
        later = {name for name in after.configurations if name != after.base}
        written = {name for name in later if before is None or after.deltas.get(name) != before.deltas.get(name)
                   or after.configurations[name] != before.configurations.get(name)}
        blocking = [rise for rise in rises if rise.configuration not in stale or rise.configuration == after.base]
        stale = (stale - written) | (later - written if changes_machinery(change) else set())
        known = later
        if not blocking or changes_machinery(change):
            continue
        shown = "; ".join(rise.text() for rise in blocking[:SHOWN_RISES])
        more = f"; and {len(blocking) - SHOWN_RISES} more" if len(blocking) > SHOWN_RISES else ""
        said = (f"the commit {commit.short} ({commit.subject}) raised {len(blocking)} counts of "
                f"{QUARANTINE_LEDGER}, and it changes neither the plugin of utils/tools/quarantine/ nor the rule table "
                f"utils/scripts/layer-rules.txt: {shown}{more}")
        admitted = next((item for item in admissions
                         if commit.sha.startswith(item.commit) and item.row == QUARANTINE_ROW), None)
        if admitted is not None:
            used.add(admitted.line)
            findings.append(check_report.Finding("warning", ADMISSIONS, admitted.line, CHECK,
                                                 f"{said}.  The row admits it: {admitted.reason}"))
        else:
            findings.append(check_report.Finding(
                "error", QUARANTINE_LEDGER, 0, CHECK,
                f"{said}.  A change can only lower a count.  Remove the new findings, or add a row to {ADMISSIONS} "
                f"that gives the commit, {QUARANTINE_ROW}, error and the reason"))
    current = parse(history.working.get(QUARANTINE_LEDGER), QUARANTINE_LEDGER)
    if current is None or current.format != last_format:
        return findings
    counts = absolute_counts(current)
    for commit, rise in standing:
        value = counts.get((rise.configuration, rise.file, rise.kind), 0)
        if value > rise.old:
            findings.append(check_report.Finding(
                "warning", QUARANTINE_LEDGER, current.line_of(rise.configuration, (rise.file, rise.kind)), CHECK,
                f"{rise.file} {rise.kind} in the configuration {rise.configuration} has {value} findings, above "
                f"{rise.old} before the commit {commit.short} ({commit.subject}) raised it to {rise.new}.  Lower it "
                f"again"))
    head = parse(history.head.get(QUARANTINE_LEDGER), f"HEAD:{QUARANTINE_LEDGER}")
    if head is not None and head.format == current.format:
        for rise in ledger_rises(head, current, {}, peaks):
            findings.append(check_report.Finding(
                "warning", QUARANTINE_LEDGER, current.line_of(rise.configuration, (rise.file, rise.kind)), CHECK,
                f"{WORKING_TREE} (not committed) raises {rise.text()}.  The commit must change the plugin or the "
                f"rule table, or lower the count again"))
    return findings


def run(root: Path, warnings_dir: Path | None) -> int:
    """Run the guard over the history of one tree.

    Returns:
        The exit status
    """
    throwaway_repo.isolate()
    try:
        history = read_history(root)
        admissions = read_admissions(root / ADMISSIONS)
    except NotApplicable as problem:
        print(f"{CHECK}: {problem}.  The guard does not apply")
        return NOT_APPLICABLE
    except (AdmissionError, OSError) as problem:
        return check_report.emit([check_report.Finding("error", ADMISSIONS, 0, CHECK, str(problem))], CHECK,
                                 warnings_dir)
    findings = evaluate(history, admissions)
    status = check_report.emit(findings, CHECK, warnings_dir)
    print(f"{CHECK}: {len(history.commits)} commits of {1 + len(history.ledgers)} files, "
          f"{sum(item.level == 'error' for item in findings)} error(s), "
          f"{sum(item.level == 'warning' for item in findings)} warning(s)")
    return status


# ── The self-test ──────────────────────────────────────────────────


class Scratch:
    """A throwaway repository with a budget table, two ledgers and an admission ledger."""

    def __init__(self, root: Path) -> None:
        """Make the repository and its first commit."""
        self.root = root
        root.mkdir(parents=True)
        throwaway_repo.init(root)
        self.environment = dict(os.environ, GIT_AUTHOR_NAME="crucible-selftest",
                                GIT_AUTHOR_EMAIL="crucible-selftest@invalid", GIT_COMMITTER_NAME="crucible-selftest",
                                GIT_COMMITTER_EMAIL="crucible-selftest@invalid")
        self.write(BUDGETS, "# a planted table\nslow-row | 10 | 20 | s | a time\nsize-row | 2 | 4 | GB | a size\n"
                            "base-row | 2 | 5 | % | a baseline\n")
        self.write("utils/scripts/slow-row-ledger.txt", "# kind | item | value | reason\n")
        self.write("utils/scripts/base-row-ledger.txt", "total | k | 1 | 2 | r\n")
        self.commit("Add the planted table", "The first table.")

    def write(self, rel: str, text: str) -> None:
        """Write one file of the tree."""
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")

    def commit(self, subject: str, body: str) -> str:
        """Commit each file of the tree, and return the hash."""
        for command in (["add", "-A", "--", "."], ["commit", "-q", "-m", subject, "-m", body]):
            subprocess.run(["git", "-C", str(self.root), *command], check=True, capture_output=True,
                           env=self.environment)
        return git(self.root, "rev-parse", "HEAD").strip()

    def side_commit(self, subject: str) -> str:
        """Make a commit of the tree on a side line, with HEAD as its parent and with no body, and return the hash.

        HEAD does not move.  The side commit stands for a commit of a work tree that a squash landing copies.
        """
        subprocess.run(["git", "-C", str(self.root), "add", "-A", "--", "."], check=True, capture_output=True,
                       env=self.environment)
        tree = git(self.root, "write-tree").strip()
        proc = subprocess.run(["git", "-C", str(self.root), "commit-tree", tree, "-p", "HEAD", "-m", subject],
                              check=True, capture_output=True, text=True, env=self.environment)
        return proc.stdout.strip()

    def merge(self, sha: str) -> None:
        """Merge a side commit into HEAD."""
        subprocess.run(["git", "-C", str(self.root), "merge", "-q", "--no-edit", sha], check=True,
                       capture_output=True, env=self.environment)

    def table(self, slow: str, size: str = "2 | 4") -> None:
        """Write the budget table with the two thresholds of slow-row and of size-row."""
        self.write(BUDGETS, f"# a planted table\nslow-row | {slow} | s | a time\nsize-row | {size} | GB | a size\n"
                            "base-row | 2 | 5 | % | a baseline\n")

    def run(self) -> tuple[int, list[check_report.Finding], str]:
        """Run the guard, and return its status, its findings and its output."""
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            status = run(self.root, None)
        found = [parsed for line in output.getvalue().splitlines()
                 if (parsed := check_report.parse_line(line)) is not None]
        return status, found, output.getvalue()


def quarantine_text(base: dict[str, int], deltas: dict[str, dict[str, int]] | None = None) -> str:
    """Return a format 2 quarantine ledger with base counts of the kind std_object and the differences of more
    configurations, each keyed by file."""
    deltas = deltas or {}
    lines = ["# a planted ledger", "format 2", "configuration default machine x86_64"]
    lines += [f"configuration {name} machine aarch64" for name in deltas]
    lines += [f"{file} std_object {count}" for file, count in sorted(base.items()) if count]
    lines += [f"{name} {file} std_object {delta:+d}" for name, table in deltas.items()
              for file, delta in sorted(table.items()) if delta]
    return "\n".join(lines) + "\n"


def quarantine_cases(expect: Any, scratch: Path) -> None:
    """Plant raises of the quarantine ledger in a throwaway repository, and check each verdict."""

    def errors_of(found: list[check_report.Finding]) -> list[check_report.Finding]:
        return [f for f in found if f.level == "error" and f.path in (QUARANTINE_LEDGER, ADMISSIONS)]

    def standing_of(found: list[check_report.Finding]) -> list[check_report.Finding]:
        return [f for f in found if f.level == "warning" and f.path == QUARANTINE_LEDGER]

    tree = Scratch(scratch / "quarantine")
    tree.write(QUARANTINE_LEDGER, "# a planted ledger\nconfiguration machine x86_64\ntest std_object 5\n")
    for name in ("test/a.cpp", "test/b.cpp"):
        tree.write(name, f"// {name}\n")
    tree.commit("Add the ledger of format 1", "The first counts.")
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 3, "test/b.cpp": 2}))
    tree.commit("Count each file", "The format changes, and each total stays.")
    status, found, _ = tree.run()
    expect("a change of the format with the same totals is no raise", not errors_of(found) and not standing_of(found))
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 2, "test/b.cpp": 2}))
    tree.commit("Lower a count", "One finding goes.")
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 2, "test/b.cpp": 3}))
    raised = tree.commit("Raise a count by hand", "A finding comes back.")
    status, found, _ = tree.run()
    expect("a raise with no change of the plugin or the rule table is an error that names the commit",
           status == 1 and [f.message.split(":")[0] for f in errors_of(found)]
           == [f"the commit {raised[:9]} (Raise a count by hand) raised 1 counts of {QUARANTINE_LEDGER}, and it "
               f"changes neither the plugin of utils/tools/quarantine/ nor the rule table utils/scripts/layer-rules.txt"])
    expect("the raised row gives a warning that names the commit",
           [f.line for f in standing_of(found)] == [5] and raised[:9] in standing_of(found)[0].message
           and "from 2" not in standing_of(found)[0].message and "above 2" in standing_of(found)[0].message)
    tree.write(ADMISSIONS, f"# COMMIT | ROW | COLUMN | REASON\n{raised[:12]} | {QUARANTINE_ROW} | error | a "
                           f"planted reason\n")
    tree.commit("Admit the raise", "The row admits it.")
    status, found, _ = tree.run()
    expect("an admission row of quarantine-ledger changes the error to a warning",
           status == 0 and not errors_of(found) and any(f.path == ADMISSIONS and "admits it" in f.message
                                                        for f in found))
    tree.write("utils/tools/quarantine/quarantine.cpp", "// a new rule\n")
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 4, "test/b.cpp": 3}))
    ruled = tree.commit("Report a new kind in the plugin", "The plugin finds more.")
    status, found, _ = tree.run()
    expect("a raise with a change of the plugin is no error, and its row gives a warning that names the commit",
           status == 0 and not errors_of(found) and any(ruled[:9] in f.message for f in standing_of(found)))
    subprocess.run(["git", "-C", str(tree.root), "mv", "test/b.cpp", "test/c.cpp"], check=True, capture_output=True)
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 4, "test/c.cpp": 3}))
    tree.commit("Move a file", "git moves the file, and its rows move with it.")
    status, found, _ = tree.run()
    expect("a git move of a file is no raise", status == 0 and not errors_of(found))
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 4, "test/c.cpp": 3}, {"arm": {"test/c.cpp": 1}}))
    tree.commit("Add the configuration of a CI leg", "The leg counts one more finding.")
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 4, "test/c.cpp": 2}, {"arm": {"test/c.cpp": 1}}))
    tree.commit("Lower a count of each build", "Only the base rows change.")
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 4, "test/c.cpp": 2}, {"arm": {"test/c.cpp": 2}}))
    tree.commit("Import the rows of the CI leg", "The leg had the finding in an arm of its own.")
    status, found, _ = tree.run()
    expect("a new configuration, and a later configuration that comes back to a count of its history, are no raise",
           status == 0 and not errors_of(found))
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 4, "test/c.cpp": 2}, {"arm": {"test/c.cpp": 3}}))
    above = tree.commit("Import more rows", "The leg finds one more.")
    status, found, _ = tree.run()
    expect("a later configuration above each count of its history is an error",
           status == 1 and any(above[:9] in f.message for f in errors_of(found)))
    tree.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 5, "test/c.cpp": 2}, {"arm": {"test/c.cpp": 3}}))
    status, found, _ = tree.run()
    expect("a raise in the working tree gives a warning only",
           any(WORKING_TREE in f.message and f.level == "warning" for f in found)
           and not any(WORKING_TREE in f.message for f in errors_of(found)))

    late = Scratch(scratch / "quarantine-stale")
    late.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 2}, {"arm": {"test/a.cpp": 1}}))
    late.commit("Count two configurations", "The first counts.")
    late.write("utils/tools/quarantine/quarantine.cpp", "// a new rule\n")
    late.commit("Report a new kind in the plugin", "The base keeps its counts.")
    late.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 2}, {"arm": {"test/a.cpp": 3}}))
    caught_up = late.commit("Import the rows of the CI leg", "The leg sees the new kind.")
    status, found, _ = late.run()
    expect("a raise of a later configuration after a change of the plugin that did not write it is no error",
           status == 0 and not errors_of(found) and any(caught_up[:9] in f.message for f in standing_of(found)))
    late.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 2}, {"arm": {"test/a.cpp": 4}}))
    again = late.commit("Import more rows", "The leg finds one more.")
    status, found, _ = late.run()
    expect("the next raise of that configuration is an error, because its rows are current again",
           status == 1 and any(again[:9] in f.message for f in errors_of(found)))
    renamed = Scratch(scratch / "quarantine-rename")
    renamed.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 2}))
    renamed.commit("Count one configuration", "The first counts.")
    renamed.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 3}).replace("configuration default",
                                                                                "configuration x86_64-debug-asan"))
    hidden = renamed.commit("Rename the base and raise a count", "A new name must not hide the raise.")
    status, found, _ = renamed.run()
    expect("a new name of the base keeps its history, so a raise in the same commit is an error",
           status == 1 and any(hidden[:9] in f.message for f in errors_of(found)))

    late.write("utils/tools/quarantine/plugin_core.h", "// a second rule\n")
    late.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 5}, {"arm": {"test/a.cpp": 4}}))
    base_raise = late.commit("Report a second kind in the plugin", "The base finds three more.")
    status, found, _ = late.run()
    named = [f.message for f in standing_of(found) if base_raise[:9] in f.message]
    expect("a raise of a base count is one rise, and not one more for each later configuration",
           len(named) == 1 and "configuration default" in named[0])

    jump = Scratch(scratch / "quarantine-format")
    jump.write(QUARANTINE_LEDGER, "# a planted ledger\nconfiguration machine x86_64\ntest std_object 5\n")
    jump.commit("Add the ledger of format 1", "The first counts.")
    jump.write(QUARANTINE_LEDGER, quarantine_text({"test/a.cpp": 4, "test/b.cpp": 2}))
    grown = jump.commit("Count each file", "The total grows.")
    status, found, _ = jump.run()
    expect("a change of the format that raises a total is an error",
           status == 1 and any(grown[:9] in f.message and TOTAL in f.message for f in errors_of(found)))


def self_test() -> int:
    """Plant each finding of the guard in a throwaway repository, and check each verdict.

    Returns:
        0 when every case holds, else 2
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    known = {10.0, 20.0, 30.0}
    expect("a measurement in a sentence that names the row counts",
           has_measurement("The slowest job of slow-row took 18.4 s on the host.", "slow-row", "s", known, 20.0, 30.0))
    expect("a measurement above the old threshold and at most the new one counts in each sentence",
           has_measurement("Raise the row.\nThe slowest test took 20.2 s in a loaded run.", "slow-row", "s", known,
                           20.0, 30.0))
    expect("a body that only tells the change holds no measurement",
           not has_measurement("Raise slow-row from 20 s to 30 s.", "slow-row", "s", known, 20.0, 30.0))
    expect("a number outside the interval of the rise counts only in a sentence that names the row",
           not has_measurement("Raise the row.\nEach preset stops a test at 300 s.", "slow-row", "s", known, 20.0,
                               30.0))
    expect("a number in another unit is no measurement",
           not has_measurement("slow-row: the job read 25 GB.", "slow-row", "s", known, 20.0, 30.0))
    expect("a longer row name is not the row",
           not has_measurement("slow-row-two took 18.4 s.", "slow-row", "s", known, 20.0, 30.0))
    expect("a value that the row had in an older commit is no measurement",
           not has_measurement("slow-row was 10 s in March.", "slow-row", "s", known, 20.0, 30.0))
    expect("a number with no unit, such as a count, is no measurement",
           not has_measurement("The change moves 25 tests of slow-row.", "slow-row", "s", known, 20.0, 30.0))
    expect("a percent and a multiple read with their own unit",
           has_measurement("Two builds gave instruction-total sums 0.003 % apart.", "instruction-total", "%",
                           {2.0, 5.0}, 2.0, 5.0)
           and has_measurement("<chrono> costs heavy-include 2.4 x <meta>.", "heavy-include", "x <meta>", {1.25},
                               1.25, 3.0))
    expect("a number with commas reads as one number",
           has_measurement("constexpr-ops needed 40,000,000 operations.", "constexpr-ops", "operations", {33554432.0},
                           33554432.0, 50000000.0))

    with tempfile.TemporaryDirectory(prefix="budget-history-") as scratch_name:
        tree = Scratch(Path(scratch_name) / "repo")
        status, found, output = tree.run()
        expect("a history with no rise gives no finding", status == 0 and not found and "1 commits" in output)

        tree.table("10 | 30")
        status, found, _ = tree.run()
        expect("a rise in the working tree gives a warning only, which names the working tree",
               status == 0 and [f.level for f in found] == ["warning"] and "the working tree" in found[0].message
               and "error threshold of slow-row is 30 s" in found[0].message and found[0].line == 2)
        unmeasured = tree.commit("Raise the slow row", "The tests are slower.")
        status, found, _ = tree.run()
        errors = [f for f in found if f.level == "error"]
        warnings = [f for f in found if f.level == "warning"]
        expect("a committed rise with no measurement gives an error at the row, and the warning names the commit",
               status == 1 and len(errors) == 1 and errors[0].path == BUDGETS and errors[0].line == 2
               and unmeasured[:9] in errors[0].message and len(warnings) == 1
               and f"{unmeasured[:9]} (Raise the slow row) from 20 to 30" in warnings[0].message)
        tree.write(ADMISSIONS, f"# COMMIT | ROW | COLUMN | REASON\n{unmeasured[:12]} | slow-row | error | the slowest "
                               f"job of slow-row took 26 s\n")
        tree.commit("Admit the raise of the slow row", "The admission row gives the slow-row measurement of 26 s.")
        status, found, _ = tree.run()
        expect("an admission row changes the error to a warning at the row",
               status == 0 and any(f.path == ADMISSIONS and f.line == 2 and "admits it" in f.message for f in found)
               and not any(f.level == "error" for f in found))
        tree.table("10 | 30", "2 | 6")
        measured = tree.commit("Raise the size row", "The largest link of size-row took 5.2 GB of memory.")
        status, found, _ = tree.run()
        size = [f for f in found if "size-row" in f.message]
        expect("a committed rise with a measurement gives a warning only, which names the commit",
               status == 0 and len(size) == 1 and size[0].level == "warning"
               and f"{measured[:9]} (Raise the size row) from 4 to 6" in size[0].message)
        tree.table("10 | 20", "2 | 4")
        tree.commit("Lower the two rows", "The rows go back to their first values.")
        status, found, _ = tree.run()
        expect("a threshold back at its lowest value gives no warning, and the admission row still matches",
               status == 0 and all(f.path == ADMISSIONS for f in found))
        tree.write(ADMISSIONS, "# COMMIT | ROW | COLUMN | REASON\n0123456789ab | slow-row | error | a stale row\n")
        status, found, _ = tree.run()
        expect("an admission row that names no rise is an error, and the rise it named fails again",
               status == 1 and any(f.path == ADMISSIONS and "Remove the row" in f.message for f in found)
               and any(f.path == BUDGETS and f.level == "error" for f in found))
        tree.write(ADMISSIONS, "0123 | slow-row | error | a short hash\n")
        status, found, _ = tree.run()
        expect("a malformed admission row is an error", status == 1 and "a row is" in found[0].message)
        tree.write(ADMISSIONS, f"# COMMIT | ROW | COLUMN | REASON\n{unmeasured[:12]} | slow-row | error | the slowest "
                               f"job of slow-row took 26 s\n")

        tree.write("utils/scripts/slow-row-ledger.txt", "# kind | item | value | reason\nk | a | 21 | r\nk | b | 22 | r\n")
        grown = tree.commit("Admit two slow tests", "The two tests take 21 s and 22 s.")
        tree.write("utils/scripts/base-row-ledger.txt", "total | k | 1 | 2 | r\nfile | k | f | 1 | 1\n")
        tree.commit("Write the baseline again", "The baseline holds one more file row.")
        status, found, _ = tree.run()
        ledger = [f for f in found if f.path.endswith("-ledger.txt") and f.path != ADMISSIONS]
        expect("a ledger above its lowest row count gives a warning that names the commit, and a ledger of a row "
               "with the unit % gives none",
               status == 0 and len(ledger) == 1 and ledger[0].path == "utils/scripts/slow-row-ledger.txt"
               and f"{grown[:9]} (Admit two slow tests) from 0 to 2" in ledger[0].message)
        tree.write("utils/scripts/slow-row-ledger.txt", "# kind | item | value | reason\nk | a | 21 | r\n")
        tree.commit("Make one slow test fast", "One row goes.")
        status, found, _ = tree.run()
        expect("a ledger that fell but stays above its lowest count still names the commit that added rows",
               any(f"{grown[:9]}" in f.message for f in found if f.path == "utils/scripts/slow-row-ledger.txt"))

        tree.write(BUDGETS, "# a planted table\nslow-row | 10 | 20 | G instructions | a count\n"
                            "size-row | 2 | 4 | GB | a size\nbase-row | 2 | 5 | % | a baseline\n")
        tree.commit("Count the slow row in instructions", "The unit changes.")
        status, found, _ = tree.run()
        expect("a row with a new unit starts a new history", not any("slow-row is" in f.message for f in found))

        def sized(warn: int, error: int) -> str:
            return (f"# a planted table\nslow-row | 10 | 20 | G instructions | a count\nsize-row | {warn} | {error} | "
                    f"GB | a size\nbase-row | 2 | 5 | % | a baseline\n")

        tree.write(BUDGETS, sized(2, 8))
        side = tree.side_commit("Raise the size row in a work tree")
        tree.commit("Raise the size row", "The largest link took 7.1 GB.")
        tree.merge(side)
        status, found, _ = tree.run()
        expect("a side commit with no body is measured by the landed commit of the same step",
               not any(side[:9] in f.message for f in found if f.level == "error")
               and any(side[:9] in f.message for f in found if "size-row is" in f.message))
        tree.write(BUDGETS, sized(3, 8))
        lone = tree.side_commit("Raise the size warning in a work tree")
        tree.write(BUDGETS, sized(2, 8))
        tree.merge(lone)
        status, found, _ = tree.run()
        expect("a side commit with no body and no measured twin is an error",
               status == 1 and any(lone[:9] in f.message for f in found if f.level == "error"))

        quarantine_cases(expect, Path(scratch_name))

        shallow = Path(scratch_name) / "shallow"
        subprocess.run(["git", "clone", "-q", "--depth", "1", f"file://{tree.root}", str(shallow)], check=True,
                       capture_output=True)
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            status = run(shallow, None)
        expect("a shallow history exits 3", status == NOT_APPLICABLE and "shallow" in output.getvalue())
        plain = Path(scratch_name) / "plain"
        plain.mkdir()
        with contextlib.redirect_stdout(io.StringIO()):
            status = run(plain, None)
        expect("a tree that is not a git work tree exits 3", status == NOT_APPLICABLE)

    if failures:
        print(f"check-budget-history --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-budget-history --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Parse the command line and run the guard or its self-test."""
    parser = argparse.ArgumentParser(prog="check-budget-history.py", description=__doc__.split("\n")[0])
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help="the top of the git work tree")
    parser.add_argument("--self-test", action="store_true", help="plant each finding in a throwaway repository")
    check_report.add_arguments(parser)
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return self_test()
    return run(arguments.root.resolve(), arguments.warnings_dir)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
