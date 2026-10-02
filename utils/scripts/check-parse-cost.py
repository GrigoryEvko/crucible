#!/usr/bin/env python3
"""check-parse-cost — the bytes that the compiles of one build and its fixtures read, against a baseline.

THE CHECKS
    parse-total     The sum, over each object of the target all and each
                    negative fixture, of the bytes of each file that its
                    compile reads.  The file lists come from
                    utils/scripts/build_census.py: the ninja dependency log
                    for an object, and the record of the fixture driver for a
                    fixture.  A ccache hit and a result of the fixture store
                    record their files too, so the total does not depend on
                    the caches, and it does not depend on the load of the
                    host.

    Each per-job budget can pass while the total increases: one more include
    in a base header costs a little in each of thousands of compiles.  On 192
    cores the wall time of a build is at least its total CPU time divided by
    192, so the total sets a limit on the wall time of the build.

    header-fanout   For each header of include/ that a sentinel of test/layer
                    compiles alone: the bytes that the header includes alone,
                    times the number of units (objects and fixtures) that read
                    it.  The product tells which headers cost the build the
                    most, and the check prints the ten largest products, the
                    list that a developer reads first.  The bytes alone are
                    the files of the sentinel after its source, as for
                    header-alone of check-compile-cost.py, so a check file
                    that includes more than its header counts those files
                    too.  This check gives warnings only.

    instruction-total
                    The sum, over each object of all and each fixture, of the
                    user instructions of its last real compile: the count of
                    the record of the build launcher (the cost block, or the
                    last cost that a ccache hit keeps), and the count of the
                    fixture record.  The count covers the template
                    instantiation and the constant evaluation that the bytes
                    do not see.  The check exits 3 when one compile has no
                    count (the host gives no exact counter, or a ccache hit
                    keeps no count of a real compile in this build
                    directory).  It compares the sum with the total row of
                    utils/scripts/instruction-total-ledger.txt, at the levels
                    of the row instruction-total.  The count does not change
                    with the load of the host: two clean builds of one tree
                    differ by less than 0.01 % in the sum.

THE BASELINE
    utils/scripts/parse-total-ledger.txt holds the baseline of each build
    kind, which --write regenerates:

        total | KIND | OBJECT BYTES | FIXTURE BYTES | reason
        file | KIND | FILE | BYTES | READERS

    A total row gives the two sums at the last --write.  A file row gives the
    size of one file and the number of units that read it, for each file
    whose product (size times readers) was at least 0.05 % of the total.  The
    sum of the products of all files is the total, so the file rows tell
    which files a change of the total comes from.  FILE is the path in the
    tree, <build>/PATH for a file of the build directory, or <system>/PATH
    after the first include/ component of the path of another file
    (build_census.file_key).

THE LEVELS
    The rows parse-total and instruction-total of utils/scripts/budgets.txt
    give two thresholds in percent of the baseline.  A total that increased
    by more than the warning threshold gives a warning, and by more than the
    error threshold an error.  A total that fell by more than the warning
    threshold is an error too, with the message to lower the baseline in the
    same commit, so the baseline does not stay above the total.  To accept a
    growth, raise the baseline in the same commit: --write --reason TEXT.
    --write refuses a growth over the warning threshold with no reason.  Each
    finding of parse-total names the files with the largest change of their
    product: the bytes, the readers and the product now, and the change from
    the baseline.  A file under the floor of the baseline counts as zero
    there.  Each finding of a growth of instruction-total names the largest
    compiles.

THE FAN-OUT BASELINE
    utils/scripts/header-fanout-ledger.txt holds the 30 largest products of
    each build kind at the last --write:

        header | KIND | HEADER | ALONE BYTES | READERS

    A header in the ten largest products of the build that was not in the ten
    largest of the baseline gives a warning.  A header whose product
    increased by more than the threshold of the row header-fanout of
    utils/scripts/budgets.txt gives a warning.  A header whose product fell
    by more than that threshold, and a header of the baseline that has no
    sentinel in the build, give a warning to write the baseline again, so
    that it does not stay above the product.

NOT APPLICABLE
    A check exits 3, with the reason, when the census does not apply to the
    build (build_census.NotApplicable: no ninja, only some targets built, no
    fixture ran), or when its ledger has no baseline of the kind of the
    build.  A kind with no baseline is a build that nobody measured, for
    example a build for another architecture.  Each other input that a check
    cannot read is an error.

Usage
    check-parse-cost.py --check CHECK --build-dir DIR [--warnings-dir DIR]
    check-parse-cost.py --check CHECK --build-dir DIR --write [--reason TEXT]
    check-parse-cost.py --self-test

Exit 0 with no finding or with warnings only, 1 with an error, 2 on a usage
error or a failed self-test, 3 when the check does not apply to the build.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import build_census  # noqa: E402
import check_report  # noqa: E402
import cost_meter  # noqa: E402
from repo_root import REPO_ROOT  # noqa: E402

SCRIPTS = Path(__file__).resolve().parent
# The ledger of each check.
LEDGERS = {"parse-total": "parse-total-ledger.txt", "header-fanout": "header-fanout-ledger.txt",
           "instruction-total": "instruction-total-ledger.txt"}
GIGA = 1e9
CHECKS = tuple(LEDGERS)
NOT_APPLICABLE = 3
# The part of the path of an object of a sentinel of test/layer, which compiles one header alone.
SENTINEL_MARK = "/layer_sentinel_"
# The headers that the fan-out lists, and the headers that its baseline keeps.
FANOUT_TOP = 10
FANOUT_KEPT = 30
# The product of a file row is at least this part of the total at the last --write.
FILE_FLOOR = 0.0005
# The number of files that a finding names.
NAMED_FILES = 8
DEFAULT_REASON = "the measure at the last --write"
MB = 1024 * 1024
KB = 1024


class LedgerError(ValueError):
    """The ledger does not exist or holds a malformed row."""


@dataclass(frozen=True, slots=True)
class Total:
    """The total row of one kind."""

    line: int
    objects: int
    fixtures: int
    reason: str

    @property
    def bytes(self) -> int:
        """The sum of the two totals."""
        return self.objects + self.fixtures


@dataclass(slots=True)
class Ledger:
    """The rows of the ledger: the header lines, the total of each kind and the file rows of each kind."""

    header: list[str]
    totals: dict[str, Total]
    files: dict[str, dict[str, tuple[int, int]]]


@dataclass(frozen=True, slots=True)
class Measure:
    """The totals of one build, and the size and the readers of each file."""

    objects: int
    fixtures: int
    object_units: int
    fixture_units: int
    files: dict[str, tuple[int, int]]

    @property
    def bytes(self) -> int:
        """The sum of the two totals."""
        return self.objects + self.fixtures


def size_text(count: int) -> str:
    """Return a number of bytes in KB, MB or GB of 2^10, 2^20 or 2^30 bytes."""
    if count >= 1024 * MB:
        return f"{count / (1024 * MB):.2f} GB"
    if count >= MB:
        return f"{count / MB:.1f} MB"
    return f"{count / KB:.1f} KB"


def read_ledger(path: Path) -> Ledger:
    """Read the ledger.

    Raises:
        LedgerError: If the file does not exist, or a row is malformed or repeated
    """
    if not path.is_file():
        raise LedgerError(f"{path.name} does not exist.  It holds a comment block and the rows that --write writes")
    ledger = Ledger([], {}, {})
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        text = raw.strip()
        if not text or text.startswith("#"):
            if not ledger.totals and not ledger.files:
                ledger.header.append(raw)
            continue
        cells = [cell.strip() for cell in text.split(" | ")]
        kind = cells[1] if len(cells) > 1 else ""
        try:
            if cells[0] == "total" and len(cells) == 5 and cells[4]:
                if kind in ledger.totals:
                    raise LedgerError(f"{path.name}:{number}: the kind {kind} has a second total row")
                ledger.totals[kind] = Total(number, int(cells[2]), int(cells[3]), cells[4])
            elif cells[0] == "file" and len(cells) == 5:
                rows = ledger.files.setdefault(kind, {})
                if cells[2] in rows:
                    raise LedgerError(f"{path.name}:{number}: the file {cells[2]} has a second row of the kind {kind}")
                rows[cells[2]] = (int(cells[3]), int(cells[4]))
            else:
                raise LedgerError(f"{path.name}:{number}: a row is `total | KIND | OBJECT BYTES | FIXTURE BYTES | "
                                  f"reason` or `file | KIND | FILE | BYTES | READERS`, and this row is: {text}")
        except ValueError as problem:
            if isinstance(problem, LedgerError):
                raise
            raise LedgerError(f"{path.name}:{number}: a count of this row is not a whole number: {text}") from None
        if not is_kind(kind):
            raise LedgerError(f"{path.name}:{number}: the kind {kind!r} is not lowercase words with hyphens")
    return ledger


def is_kind(kind: str) -> bool:
    """Return True when a text has the form of the build kind that cmake/BuildLauncher.cmake writes."""
    return bool(check_report.CHECK_NAME.fullmatch(kind.replace("_", "-")))


def measure(census: build_census.Census, root: Path) -> Measure:
    """Add the bytes of each unit, and find the size and the readers of each file.

    Complexity: linear in the total length of the file lists.
    """
    objects = sum(census.unit_bytes(unit) for unit in census.units if unit.kind == "object")
    fixtures = sum(census.unit_bytes(unit) for unit in census.units if unit.kind == "fixture")
    files: dict[str, tuple[int, int]] = {}
    for real, readers in census.readers().items():
        key = build_census.file_key(real, root, census.build_dir)
        size, count = files.get(key, (census.sizes[real], 0))
        files[key] = (size, count + readers)
    return Measure(objects, fixtures, sum(unit.kind == "object" for unit in census.units),
                   sum(unit.kind == "fixture" for unit in census.units), files)


def changes_text(now: dict[str, tuple[int, int]], before: dict[str, tuple[int, int]], is_growth: bool,
                 floor: int) -> str:
    """Return the files whose product changed the most in the direction of a change of the total, as words.

    Complexity: O(n log n) for n files.
    """
    deltas = []
    for key in set(now) | set(before):
        size, readers = now.get(key, (0, 0))
        old_size, old_readers = before.get(key, (0, 0))
        deltas.append((size * readers - old_size * old_readers, key, size, readers, old_size, old_readers))
    ordered = sorted((entry for entry in deltas if (entry[0] > 0) == is_growth and entry[0] != 0),
                     key=lambda entry: -abs(entry[0]))
    parts = []
    for delta, key, size, readers, old_size, old_readers in ordered[:NAMED_FILES]:
        sign = "+" if delta > 0 else "-"
        was = (f"was {size_text(old_size)} x {old_readers}" if key in before
               else f"under the floor of {size_text(floor)} at the last --write")
        parts.append(f"{key} {size_text(size)} x {readers} readers = {size_text(size * readers)} "
                     f"({sign}{size_text(abs(delta))}, {was})")
    return "; ".join(parts) if parts else "no file row changed"


def evaluate(measured: Measure, ledger: Ledger, ledger_shown: str, kind: str, budget: check_report.Budget,
             build_shown: str) -> list[check_report.Finding]:
    """Compare the total of a build with the baseline of its kind.

    Returns:
        The findings: an error or a warning for a growth, an error for a fall
    """
    total = ledger.totals[kind]
    baseline = total.bytes
    growth = (measured.bytes - baseline) / baseline * 100.0
    words = (f"the compiles of the build read {size_text(measured.bytes)} ({measured.bytes} bytes: objects "
             f"{size_text(measured.objects)} in {measured.object_units} units, fixtures {size_text(measured.fixtures)} "
             f"in {measured.fixture_units} units)")
    floor = int(baseline * FILE_FLOOR)
    write = (f"python3 utils/scripts/check-parse-cost.py --check parse-total --build-dir {build_shown} --write")
    level = check_report.classify(growth, budget)
    if level is not None:
        limit = budget.error if level == "error" else budget.warn
        changes = changes_text(measured.files, ledger.files.get(kind, {}), True, floor)
        return [check_report.Finding(
            level, ledger_shown, total.line, "parse-total",
            f"{words}, {growth:.2f}% more than the baseline of {size_text(baseline)}, over the {level} threshold "
            f"{limit:g}%.  Remove the cost, or raise the baseline in the same commit with a reason: {write} --reason "
            f"TEXT.  The largest changes: {changes}")]
    if growth < -budget.warn:
        changes = changes_text(measured.files, ledger.files.get(kind, {}), False, floor)
        return [check_report.Finding(
            "error", ledger_shown, total.line, "parse-total",
            f"{words}, {-growth:.2f}% less than the baseline of {size_text(baseline)}, more than the {budget.warn:g}% "
            f"that the baseline can hold.  Lower the baseline in the same commit, so that it does not stay above the total: {write}.  "
            f"The largest changes: {changes}")]
    return []


def write_ledger(path: Path, ledger: Ledger, kind: str, measured: Measure, reason: str | None,
                 budget: check_report.Budget) -> tuple[bool, str]:
    """Write the rows of one kind again from a measure, and keep the rows of the other kinds.

    Returns:
        True and a summary, or False and the reason that the write refuses
    """
    old = ledger.totals.get(kind)
    if old is not None and not reason and measured.bytes > old.bytes * (1 + budget.warn / 100.0):
        growth = (measured.bytes - old.bytes) / old.bytes * 100.0
        return False, (f"the total increased by {growth:.2f}% over the baseline of {size_text(old.bytes)}, more than the "
                       f"warning threshold {budget.warn:g}%.  Give the reason of the growth with --reason")
    new_reason = reason or (old.reason if old is not None else DEFAULT_REASON)
    totals = dict(ledger.totals)
    totals[kind] = Total(0, measured.objects, measured.fixtures, new_reason)
    files = dict(ledger.files)
    floor = measured.bytes * FILE_FLOOR
    files[kind] = {key: value for key, value in measured.files.items() if value[0] * value[1] >= floor}
    lines = list(ledger.header)
    for each_kind in sorted(totals):
        row = totals[each_kind]
        lines.append(f"total | {each_kind} | {row.objects} | {row.fixtures} | {row.reason}")
        ordered = sorted(files.get(each_kind, {}).items(), key=lambda item: (-item[1][0] * item[1][1], item[0]))
        lines.extend(f"file | {each_kind} | {key} | {size} | {readers}" for key, (size, readers) in ordered)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return True, (f"parse-total: {path.name} holds the baseline {measured.bytes} bytes and {len(files[kind])} file "
                  f"rows of the kind {kind}")


@dataclass(frozen=True, slots=True)
class Fan:
    """One header that a sentinel compiles alone: the bytes that it includes alone and the units that read it."""

    header: str
    alone: int
    readers: int

    @property
    def product(self) -> int:
        """The bytes alone times the readers."""
        return self.alone * self.readers


@dataclass(slots=True)
class FanLedger:
    """The rows of the fan-out ledger: the header lines and, for each kind, the line, bytes and readers of a header."""

    header: list[str]
    rows: dict[str, dict[str, tuple[int, int, int]]]


def fanout(census: build_census.Census, root: Path) -> list[Fan]:
    """Find the bytes alone and the readers of each header that a sentinel compiles, largest product first.

    The header of a sentinel is the first file of its list under include/
    after the source, as for header-alone of check-compile-cost.py, and the
    bytes alone are the sum of the files after the source.  Complexity: linear
    in the total length of the file lists, and O(n log n) for the sort.
    """
    readers = census.readers()
    include_root = f"{root / 'include'}{os.sep}"
    fans: dict[str, Fan] = {}
    for unit in census.units:
        if unit.kind != "object" or SENTINEL_MARK not in f"/{unit.item}":
            continue
        header = next((path for path in unit.files[1:] if path.startswith(include_root)), None)
        if header is None:
            continue
        key = build_census.file_key(header, root, census.build_dir)
        fans[key] = Fan(key, sum(census.sizes[path] for path in unit.files[1:]), readers[header])
    return sorted(fans.values(), key=lambda fan: (-fan.product, fan.header))


def read_fanout_ledger(path: Path) -> FanLedger:
    """Read the fan-out ledger.

    Raises:
        LedgerError: If the file does not exist, or a row is malformed or repeated
    """
    if not path.is_file():
        raise LedgerError(f"{path.name} does not exist.  It holds a comment block and the rows that --write writes")
    ledger = FanLedger([], {})
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        text = raw.strip()
        if not text or text.startswith("#"):
            if not ledger.rows:
                ledger.header.append(raw)
            continue
        cells = [cell.strip() for cell in text.split(" | ")]
        if len(cells) != 5 or cells[0] != "header" or not is_kind(cells[1]) or not cells[2]:
            raise LedgerError(f"{path.name}:{number}: a row is `header | KIND | HEADER | ALONE BYTES | READERS`, and "
                              f"this row is: {text}")
        try:
            alone, readers = int(cells[3]), int(cells[4])
        except ValueError:
            raise LedgerError(f"{path.name}:{number}: a count of this row is not a whole number: {text}") from None
        rows = ledger.rows.setdefault(cells[1], {})
        if cells[2] in rows or alone <= 0 or readers <= 0:
            raise LedgerError(f"{path.name}:{number}: the header {cells[2]} has a second row of the kind {cells[1]}, "
                              f"or a count that is not positive")
        rows[cells[2]] = (number, alone, readers)
    return ledger


def fan_text(fan: Fan) -> str:
    """Return the cost of one header as words: its bytes alone, its readers and their product."""
    return f"{size_text(fan.alone)} alone x {fan.readers} readers = {size_text(fan.product)}"


def evaluate_fanout(fans: list[Fan], rows: dict[str, tuple[int, int, int]], ledger_shown: str,
                    budget: check_report.Budget) -> list[check_report.Finding]:
    """Compare the fan-out of a build with the baseline rows of its kind.

    Returns:
        A warning for each header that enters the top list, for each header whose product changed by more than the
        threshold, and for each header of the baseline that has no sentinel
    """
    findings: list[check_report.Finding] = []
    old_top = set(sorted(rows, key=lambda header: (-rows[header][1] * rows[header][2], header))[:FANOUT_TOP])
    now = {fan.header: fan for fan in fans}
    for rank, fan in enumerate(fans[:FANOUT_TOP], start=1):
        if fan.header not in old_top:
            findings.append(check_report.Finding(
                "warning", fan.header, 0, "header-fanout",
                f"the header enters the top {FANOUT_TOP} of the fan-out at rank {rank}: {fan_text(fan)}.  Each unit "
                f"that reads the header pays the bytes that it includes.  Remove an include from it, or move code that "
                f"needs a heavy include to a source file.  When the cost stays, write the baseline again: --write"))
    for header, (line, alone, readers) in sorted(rows.items(), key=lambda item: item[1][0]):
        fan = now.get(header)
        if fan is None:
            findings.append(check_report.Finding(
                "warning", ledger_shown, line, "header-fanout",
                f"the build has no sentinel of {header}, which the baseline holds.  Write the baseline again: --write"))
            continue
        change = (fan.product - alone * readers) / (alone * readers) * 100.0
        was = f"was {size_text(alone)} alone x {readers} readers"
        if change > budget.warn:
            findings.append(check_report.Finding(
                "warning", fan.header, 0, "header-fanout",
                f"the fan-out of the header increased by {change:.1f}%, over the threshold {budget.warn:g}%: {fan_text(fan)} "
                f"({was}).  Remove an include from it, or write the baseline again when the cost stays: --write"))
        elif change < -budget.warn:
            findings.append(check_report.Finding(
                "warning", ledger_shown, line, "header-fanout",
                f"the fan-out of {header} fell by {-change:.1f}%, more than the threshold {budget.warn:g}%: "
                f"{fan_text(fan)} ({was}).  Write the baseline again, so that it does not stay above the product: --write"))
    return findings


def write_fanout_ledger(path: Path, ledger: FanLedger, kind: str, fans: list[Fan]) -> str:
    """Write the rows of one kind again from the largest products of a build, and keep the rows of the other kinds.

    Returns:
        The summary
    """
    rows = dict(ledger.rows)
    rows[kind] = {fan.header: (0, fan.alone, fan.readers) for fan in fans[:FANOUT_KEPT]}
    lines = list(ledger.header)
    for each_kind in sorted(rows):
        ordered = sorted(rows[each_kind].items(), key=lambda item: (-item[1][1] * item[1][2], item[0]))
        lines.extend(f"header | {each_kind} | {header} | {alone} | {readers}"
                     for header, (_, alone, readers) in ordered)
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return f"header-fanout: {path.name} holds {len(rows[kind])} rows of the kind {kind}"


def top_table(fans: list[Fan]) -> str:
    """Return the headers with the largest products as lines of text, for the output of the check."""
    lines = [f"header-fanout: the {FANOUT_TOP} headers whose bytes alone times readers are the largest:"]
    for rank, fan in enumerate(fans[:FANOUT_TOP], start=1):
        lines.append(f"  {rank:2d}. {size_text(fan.product):>9}  {size_text(fan.alone):>9} alone x {fan.readers:5d} "
                     f"readers  {fan.header}")
    return "\n".join(lines)


def count_text(count: int) -> str:
    """Return a number of instructions in G (10^9)."""
    return f"{count / GIGA:.1f} G"


def instruction_total(census: build_census.Census) -> tuple[int, int]:
    """Return the user instructions of the last real compile of the objects and of the fixtures.

    Raises:
        build_census.NotApplicable: If a unit has no instruction count
    """
    uncounted = [unit.item for unit in census.units if unit.cost.instructions is None]
    if uncounted:
        raise build_census.NotApplicable(
            f"{len(uncounted)} of the {len(census.units)} compiles have no instruction count, because the host gives no "
            f"exact counter, or a ccache hit keeps no count of a real compile in this build directory: "
            f"{build_census.names_text(uncounted)}.  utils/scripts/cost_meter.py tells when a count exists")
    objects = sum(unit.cost.instructions or 0 for unit in census.units if unit.kind == "object")
    fixtures = sum(unit.cost.instructions or 0 for unit in census.units if unit.kind == "fixture")
    return objects, fixtures


def evaluate_instructions(census: build_census.Census, objects: int, fixtures: int, total: Total, ledger_shown: str,
                          budget: check_report.Budget, build_shown: str) -> list[check_report.Finding]:
    """Compare the instruction total of a build with the baseline of its kind.

    Complexity: O(n log n) for n units, because of the sort of the largest compiles.

    Returns:
        The findings: an error or a warning for a growth, an error for a fall
    """
    count = objects + fixtures
    change = (count - total.bytes) / total.bytes * 100.0
    words = (f"the compiles of the build ran {count_text(count)} user instructions (objects {count_text(objects)}, "
             f"fixtures {count_text(fixtures)})")
    write = f"python3 utils/scripts/check-parse-cost.py --check instruction-total --build-dir {build_shown} --write"
    level = check_report.classify(change, budget)
    if level is not None:
        limit = budget.error if level == "error" else budget.warn
        largest = sorted(census.units, key=lambda unit: -(unit.cost.instructions or 0))[:NAMED_FILES]
        named = "; ".join(f"{unit.source} {count_text(unit.cost.instructions or 0)}" for unit in largest)
        return [check_report.Finding(
            level, ledger_shown, total.line, "instruction-total",
            f"{words}, {change:.2f}% more than the baseline of {count_text(total.bytes)}, over the {level} threshold "
            f"{limit:g}%.  Remove the cost, or raise the baseline in the same commit with a reason: {write} --reason "
            f"TEXT.  The largest compiles: {named}")]
    if change < -budget.warn:
        return [check_report.Finding(
            "error", ledger_shown, total.line, "instruction-total",
            f"{words}, {-change:.2f}% less than the baseline of {count_text(total.bytes)}, more than the "
            f"{budget.warn:g}% that the baseline can hold.  Lower the baseline in the same commit, so that it does not "
            f"stay above the total: {write}")]
    return []


def write_totals(path: Path, ledger: Ledger, kind: str, objects: int, fixtures: int, reason: str | None,
                 budget: check_report.Budget) -> tuple[bool, str]:
    """Write the total row of one kind again, and keep the total rows of the other kinds.

    Returns:
        True and a summary, or False and the reason that the write refuses
    """
    old = ledger.totals.get(kind)
    if old is not None and not reason and objects + fixtures > old.bytes * (1 + budget.warn / 100.0):
        growth = (objects + fixtures - old.bytes) / old.bytes * 100.0
        return False, (f"the total increased by {growth:.2f}% over the baseline of {count_text(old.bytes)}, more than "
                       f"the warning threshold {budget.warn:g}%.  Give the reason of the growth with --reason")
    totals = dict(ledger.totals)
    totals[kind] = Total(0, objects, fixtures, reason or (old.reason if old is not None else DEFAULT_REASON))
    lines = list(ledger.header)
    lines.extend(f"total | {each_kind} | {row.objects} | {row.fixtures} | {row.reason}"
                 for each_kind, row in sorted(totals.items()))
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return True, (f"instruction-total: {path.name} holds the baseline {objects + fixtures} instructions of the kind "
                  f"{kind}")


def run(check: str, build_dir: Path, root: Path, kind: str, ledger_path: Path, budgets_path: Path,
        warnings_dir: Path | None, write: bool = False, reason: str | None = None) -> int:
    """Run one check, or its --write, over one build.

    Returns:
        The exit status
    """
    ledger_shown = build_census.shown(ledger_path, root)
    build_shown = build_census.shown(build_dir, root)

    def fail(path: str, message: str, line: int = 0) -> int:
        if write:
            print(f"{check}: {message}", file=sys.stderr)
            return 1
        return check_report.emit([check_report.Finding("error", path, line, check, message)], check, warnings_dir)

    try:
        budget = check_report.read_budgets(budgets_path).get(check)
    except (OSError, ValueError) as problem:
        return fail(build_census.shown(budgets_path, root), str(problem))
    if budget is None:
        return fail(build_census.shown(budgets_path, root), f"{budgets_path.name} has no row {check}")
    try:
        if check == "header-fanout":
            ledger: Ledger | FanLedger = read_fanout_ledger(ledger_path)
            has_baseline = kind in ledger.rows  # type: ignore[union-attr]
        else:
            ledger = read_ledger(ledger_path)
            has_baseline = kind in ledger.totals  # type: ignore[union-attr]
    except LedgerError as problem:
        return fail(ledger_shown, str(problem))
    if not write and not has_baseline:
        print(f"{check}: {ledger_shown} has no baseline of the kind {kind}, so no baseline applies to this build.  "
              f"The check does not apply")
        return NOT_APPLICABLE
    try:
        census = build_census.read_census(build_dir, root, read_files=check != "instruction-total")
        if check == "instruction-total":
            objects, fixtures = instruction_total(census)
    except build_census.NotApplicable as problem:
        print(f"{check}: {problem}.  The check does not apply")
        return NOT_APPLICABLE
    except build_census.CensusError as problem:
        return fail(build_shown, str(problem))
    if check == "instruction-total" and isinstance(ledger, Ledger):
        if write:
            is_written, summary = write_totals(ledger_path, ledger, kind, objects, fixtures, reason, budget)
            print(summary, file=sys.stderr if not is_written else sys.stdout)
            return 0 if is_written else 1
        total = ledger.totals[kind]
        status = check_report.emit(evaluate_instructions(census, objects, fixtures, total, ledger_shown, budget,
                                                         build_shown), check, warnings_dir)
        print(f"instruction-total: {count_text(objects + fixtures)} user instructions in "
              f"{sum(unit.kind == 'object' for unit in census.units)} objects and "
              f"{sum(unit.kind == 'fixture' for unit in census.units)} fixtures, "
              f"{(objects + fixtures - total.bytes) / total.bytes * 100.0:+.2f}% against the baseline of the kind {kind}")
        return status
    if isinstance(ledger, FanLedger):
        fans = fanout(census, root)
        if write:
            print(write_fanout_ledger(ledger_path, ledger, kind, fans))
            return 0
        status = check_report.emit(evaluate_fanout(fans, ledger.rows[kind], ledger_shown, budget), check,
                                   warnings_dir)
        print(top_table(fans))
        return status
    measured = measure(census, root)
    if write:
        is_written, summary = write_ledger(ledger_path, ledger, kind, measured, reason, budget)
        print(summary, file=sys.stderr if not is_written else sys.stdout)
        return 0 if is_written else 1
    findings = evaluate(measured, ledger, ledger_shown, kind, budget, build_shown)
    status = check_report.emit(findings, check, warnings_dir)
    baseline = ledger.totals[kind].bytes
    print(f"parse-total: {measured.bytes} bytes ({size_text(measured.bytes)}) in {measured.object_units} objects and "
          f"{measured.fixture_units} fixtures, {(measured.bytes - baseline) / baseline * 100.0:+.2f}% against the "
          f"baseline of the kind {kind}")
    return status


# ── The self-test ──────────────────────────────────────────────────


FAKE_NINJA = """\
import sys
from pathlib import Path
build = Path(sys.argv[sys.argv.index("-C") + 1])
tool = sys.argv[sys.argv.index("-t") + 1]
sys.stdout.write((build / ("inputs.txt" if tool == "inputs" else "deps.txt")).read_text())
"""
FAKE_CTEST = """\
import sys
from pathlib import Path
build = Path(sys.argv[sys.argv.index("--test-dir") + 1])
sys.stdout.write((build / "tests.json").read_text())
"""


class Scratch:
    """A scratch tree, build directory, ninja, ctest, budget table and ledger for the self-test."""

    KIND = "x86_64-debug-asan"
    BUDGETS = ("parse-total | 2 | 5 | % | the growth of the total\n"
               "header-fanout | 10 | 10 | % | the change of the product of a header\n"
               "instruction-total | 2 | 5 | % | the growth of the instruction total\n")
    SENTINELS = "test/layer/CMakeFiles/layer_sentinel_fixy.dir"

    def __init__(self, root: Path) -> None:
        """Make the tree with one header, two objects and two fixtures."""
        self.root = root
        self.build = root / "build"
        self.build.mkdir(parents=True)
        (root / "include").mkdir()
        self.ledger = root / "parse-total-ledger.txt"
        self.ledger.write_text("# a planted ledger\n", encoding="utf-8")
        self.fan_ledger = root / "header-fanout-ledger.txt"
        self.fan_ledger.write_text("# a planted fan-out ledger\n", encoding="utf-8")
        self.instruction_ledger = root / "instruction-total-ledger.txt"
        self.instruction_ledger.write_text("# a planted instruction ledger\n", encoding="utf-8")
        self.budgets = root / "budgets.txt"
        self.budgets.write_text(self.BUDGETS, encoding="utf-8")
        for name, text in (("ninja.py", FAKE_NINJA), ("ctest.py", FAKE_CTEST)):
            tool = root / name
            tool.write_text(f"#!{sys.executable}\n{text}", encoding="utf-8")
            tool.chmod(0o755)
        (self.build / "CMakeCache.txt").write_text(
            f"CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_MAKE_PROGRAM:FILEPATH={root / 'ninja.py'}\n"
            f"CMAKE_CTEST_COMMAND:INTERNAL={root / 'ctest.py'}\n", encoding="utf-8")
        self.objects: dict[str, tuple[str, list[str]]] = {}
        self.fixtures: dict[str, list[str]] = {}
        self.write("include/Base.h", 1000)
        self.write("include/Big.h", 5000)
        self.write("src/a.cpp", 100)
        self.write("src/b.cpp", 100)
        self.write("neg/neg_one.cpp", 50)
        self.write("neg/neg_two.cpp", 50)
        self.add_object("a", ["src/a.cpp", "include/Base.h"])
        self.add_object("b", ["src/b.cpp", "include/Base.h"])
        self.add_fixture("neg_one", ["neg/neg_one.cpp", "include/Base.h"])
        self.add_fixture("neg_two", ["neg/neg_two.cpp", "include/Base.h"])

    def write(self, rel: str, size: int) -> None:
        """Write one file of the tree with a size."""
        path = self.root / rel
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b"x" * size)

    def add_object(self, name: str, files: list[str], state: str = "VALID", directory: str = "CMakeFiles",
                   instructions: int | None = 2_000_000_000) -> None:
        """Plant one object of all with its dependency list and its launcher record, in a directory of the build."""
        rel = f"{directory}/{name}.o"
        self.objects[name] = (rel, files)
        obj = self.build / rel
        obj.parent.mkdir(parents=True, exist_ok=True)
        obj.write_bytes(b"object")
        cost: dict[str, object] = {"cpu_s": 1.5}
        if instructions is not None:
            cost["instructions"] = instructions
        Path(f"{obj}.cost").write_text(json.dumps({"format": 1, "step": "compile", "result": "built", "cost": cost}),
                                       encoding="utf-8")
        self.flush(state)

    def remove_object(self, name: str) -> None:
        """Remove one planted object from all."""
        rel, _ = self.objects.pop(name)
        (self.build / rel).unlink()
        self.flush()

    def add_fixture(self, name: str, files: list[str], has_inputs: bool = True) -> None:
        """Plant one fixture with its record."""
        self.fixtures[name] = files
        scratch = self.build / "neg-compile" / name
        scratch.mkdir(parents=True, exist_ok=True)
        header = {"format": 1, "fixture": name, "result": "compiled", "user_s": 1.0, "system_s": 0.1,
                  "instructions": 5_000_000_000, "has_inputs": has_inputs}
        paths = "".join(f"{self.root / rel}\n" for rel in files) if has_inputs else ""
        (scratch / f"{name}.inputs").write_text(json.dumps(header) + "\n" + paths, encoding="utf-8")
        self.flush()

    def flush(self, state: str = "VALID") -> None:
        """Write the compile database, the inputs of all, the dependency log and the test list."""
        rows = [{"directory": str(self.build), "file": str(self.root / files[0]), "output": rel}
                for rel, files in self.objects.values()]
        (self.build / "compile_commands.json").write_text(json.dumps(rows), encoding="utf-8")
        (self.build / "inputs.txt").write_text("".join(f"{rel}\n" for rel, _ in self.objects.values()),
                                               encoding="utf-8")
        deps = []
        for rel, files in self.objects.values():
            listed = "".join(f"    {self.root / path}\n" for path in files)
            deps.append(f"{rel}: #deps {len(files)}, deps mtime 1 ({state})\n{listed}")
        (self.build / "deps.txt").write_text("\n".join(deps), encoding="utf-8")
        tests = [{"name": name, "command": [sys.executable, "/x/test/neg_compile_driver.py", "--warnings-dir", "/w",
                                            str(self.build), str(self.root / files[0]), name, "regex"]}
                 for name, files in self.fixtures.items()]
        tests.append({"name": "test_other", "command": ["/x/test_other"]})
        (self.build / "tests.json").write_text(json.dumps({"tests": tests}), encoding="utf-8")

    def run(self, kind: str = KIND, write: bool = False, reason: str | None = None,
            warnings_dir: Path | None = None, check: str = "parse-total") -> tuple[int, list[check_report.Finding], str]:
        """Run one check or its --write, and return its status, its findings and its output."""
        output = io.StringIO()
        ledger = {"parse-total": self.ledger, "header-fanout": self.fan_ledger,
                  "instruction-total": self.instruction_ledger}[check]
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            status = run(check, self.build.resolve(), self.root.resolve(), kind, ledger, self.budgets, warnings_dir,
                         write, reason)
        found = [parsed for line in output.getvalue().splitlines()
                 if (parsed := check_report.parse_line(line)) is not None]
        return status, found, output.getvalue()


def self_test() -> int:
    """Plant each verdict of the check and examine it.

    Returns:
        0 when every case holds, else 2
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="parse-cost-") as scratch_name:
        tree = Scratch(Path(scratch_name).resolve())
        status, found, output = tree.run()
        expect("a ledger with no total row of the kind exits 3", status == NOT_APPLICABLE and not found
               and "has no baseline of the kind" in output)
        status, _, output = tree.run(write=True)
        written = tree.ledger.read_text(encoding="utf-8")
        expect("--write writes the total and the file rows, and keeps the header",
               status == 0 and written.startswith("# a planted ledger\n")
               and f"total | {Scratch.KIND} | 2200 | 2100 | {DEFAULT_REASON}" in written
               and f"file | {Scratch.KIND} | include/Base.h | 1000 | 4" in written)
        status, found, output = tree.run()
        expect("an unchanged build passes with no finding", status == 0 and not found and "+0.00%" in output)

        tree.write("include/Base.h", 1030)
        status, found, _ = tree.run()
        expect("a growth of 2.8% gives a warning at the total row that names the header and its change",
               status == 0 and [f.level for f in found] == ["warning"] and found[0].path == "parse-total-ledger.txt"
               and found[0].line == 2 and "2.79% more than the baseline" in found[0].message
               and "include/Base.h 1.0 KB x 4 readers = 4.0 KB (+0.1 KB, was 1.0 KB x 4)" in found[0].message)
        tree.add_object("c", ["src/a.cpp", "include/Big.h"])
        status, found, _ = tree.run()
        expect("a growth over 5% gives an error that names the new header under the floor",
               status == 1 and [f.level for f in found] == ["error"]
               and "include/Big.h 4.9 KB x 1 readers" in found[0].message
               and "under the floor" in found[0].message)
        warnings_dir = tree.root / "warnings"
        status, found, _ = tree.run(warnings_dir=warnings_dir)
        expect("an error writes no warnings file", status == 1 and not (warnings_dir / "parse-total.txt").exists())
        status, _, output = tree.run(write=True)
        expect("--write refuses a growth over the warning threshold with no reason",
               status == 1 and "Give the reason" in output)
        status, _, _ = tree.run(write=True, reason="a planted reason")
        status_after, found, _ = tree.run()
        expect("--write with a reason raises the baseline, and the build then passes",
               status == 0 and status_after == 0 and not found
               and "| a planted reason" in tree.ledger.read_text(encoding="utf-8"))
        tree.remove_object("c")
        status, found, _ = tree.run()
        expect("a fall of more than 2% is an error that asks to lower the baseline",
               status == 1 and [f.level for f in found] == ["error"] and "Lower the baseline" in found[0].message
               and "include/Big.h" in found[0].message)
        status, _, _ = tree.run(write=True)
        status_after, found, _ = tree.run()
        expect("--write lowers the baseline with no reason, and keeps the last reason",
               status == 0 and status_after == 0 and "| a planted reason" in tree.ledger.read_text(encoding="utf-8"))
        with check_report.github_actions(True):
            tree.write("include/Base.h", 2000)
            status, found, _ = tree.run()
            expect("on a CI runner, a growth error stays an error", status == 1)
            tree.write("include/Base.h", 1030)

        status, found, output = tree.run(kind="aarch64-debug-asan")
        expect("a kind with no baseline exits 3", status == NOT_APPLICABLE and not found)
        good_ledger = tree.ledger.read_text(encoding="utf-8")
        for label, text in (("a row of four cells", f"total | {Scratch.KIND} | 1 | 2\n"),
                            ("a word count", f"file | {Scratch.KIND} | include/Base.h | many | 4\n"),
                            ("a second total row", good_ledger.split("\n", 1)[1]),
                            ("a total row with no reason", f"total | {Scratch.KIND} | 1 | 2 | \n")):
            tree.ledger.write_text(good_ledger + text, encoding="utf-8")
            status, found, _ = tree.run()
            expect(f"a ledger with {label} is an error", status == 1 and found[0].path == "parse-total-ledger.txt")
        tree.ledger.unlink()
        status, found, _ = tree.run()
        expect("a missing ledger is an error", status == 1 and "does not exist" in found[0].message)
        tree.ledger.write_text(good_ledger, encoding="utf-8")
        tree.budgets.write_text("other | 1 | 2 | % | m\n", encoding="utf-8")
        status, found, _ = tree.run()
        expect("a budget table with no row parse-total is an error", status == 1 and "no row parse-total"
               in found[0].message)
        tree.budgets.write_text(Scratch.BUDGETS, encoding="utf-8")

        (tree.build / "CMakeFiles" / "b.o").unlink()
        status, found, output = tree.run()
        expect("a build that made only some targets exits 3", status == NOT_APPLICABLE and not found
               and "only some targets" in output)
        tree.add_object("b", ["src/b.cpp", "include/Base.h"], state="STALE")
        status, found, _ = tree.run()
        expect("a stale dependency list is an error", status == 1 and "no valid dependency list" in found[0].message)
        tree.add_object("b", ["src/b.cpp", "include/Base.h"])
        (tree.build / "neg-compile" / "neg_two" / "neg_two.inputs").unlink()
        status, found, _ = tree.run()
        expect("a fixture with no record is an error that names it", status == 1 and "neg_two" in found[0].message)
        (tree.build / "neg-compile" / "neg_one" / "neg_one.inputs").unlink()
        status, found, output = tree.run()
        expect("a build whose fixtures did not run exits 3", status == NOT_APPLICABLE and "did not run" in output)
        tree.add_fixture("neg_one", ["neg/neg_one.cpp", "include/Base.h"])
        tree.add_fixture("neg_two", ["neg/neg_two.cpp"], has_inputs=False)
        status, found, _ = tree.run()
        expect("a record that does not know its files is an error", status == 1
               and "does not know the files" in found[0].message)
        tree.add_fixture("neg_two", ["neg/neg_two.cpp", "include/Gone.h"])
        status, found, _ = tree.run()
        expect("a file of a record that is gone is an error", status == 1 and "Gone.h" in found[0].message)
        (tree.build / "CMakeCache.txt").write_text("CMAKE_GENERATOR:INTERNAL=Unix Makefiles\n", encoding="utf-8")
        status, found, output = tree.run()
        expect("a build of another generator exits 3", status == NOT_APPLICABLE and "only ninja" in output)

        # header-fanout: twelve headers, each with a sentinel, and one object that reads each header.
        fan = Scratch(Path(scratch_name).resolve() / "fan")
        names = [f"H{index:02d}" for index in range(12)]
        for index, name in enumerate(names):
            fan.write(f"include/{name}.h", (index + 1) * 100)
            fan.write(f"test/layer/checks/{name}.cpp", 10)
            fan.add_object(name, [f"test/layer/checks/{name}.cpp", f"include/{name}.h"], directory=Scratch.SENTINELS)
        fan.add_object("user", ["src/a.cpp", *(f"include/{name}.h" for name in names)])

        def fan_run(**options: object) -> tuple[int, list[check_report.Finding], str]:
            return fan.run(check="header-fanout", **options)  # type: ignore[arg-type]

        status, found, output = fan_run()
        expect("header-fanout: a ledger with no row of the kind exits 3", status == NOT_APPLICABLE and not found)
        status, _, output = fan_run(write=True)
        written = fan.fan_ledger.read_text(encoding="utf-8")
        expect("header-fanout: --write writes a row for each header, the largest product first",
               status == 0 and written.startswith("# a planted fan-out ledger\n")
               and written.splitlines()[1] == f"header | {Scratch.KIND} | include/H11.h | 1200 | 2"
               and written.count("\nheader |") == 12)
        status, found, output = fan_run()
        expect("header-fanout: an unchanged build gives no warning and prints the top list",
               status == 0 and not found and "   1.    2.3 KB     1.2 KB alone x     2 readers  include/H11.h"
               in output and "include/H01.h" not in output)
        fan.write("include/H00.h", 5000)
        status, found, _ = fan_run()
        entered = [f for f in found if "enters the top 10" in f.message]
        increased = [f for f in found if "increased by" in f.message]
        expect("header-fanout: a header that enters the top 10 warns at its path with its rank, and its larger "
               "product warns",
               status == 0 and {f.level for f in found} == {"warning"} and len(entered) == 1
               and entered[0].path == "include/H00.h" and "at rank 1: 4.9 KB alone x 2 readers" in entered[0].message
               and len(increased) == 1 and "increased by 4900.0%" in increased[0].message)
        fan.write("include/H00.h", 100)
        fan.write("include/H11.h", 100)
        status, found, _ = fan_run()
        fell = [f for f in found if "fell by" in f.message]
        expect("header-fanout: a header whose product fell by more than 10% warns at its row, to write the baseline "
               "again, and the header that takes its place in the top 10 warns",
               status == 0 and len(fell) == 1 and fell[0].path == "header-fanout-ledger.txt" and fell[0].line == 2
               and "fell by 91.7%" in fell[0].message
               and [f.path for f in found if "enters the top 10" in f.message] == ["include/H01.h"])
        fan.write("include/H11.h", 1200)
        fan.remove_object("H05")
        status, found, _ = fan_run()
        expect("header-fanout: a header of the baseline with no sentinel warns at its row",
               status == 0 and [f.line for f in found if "has no sentinel of include/H05.h" in f.message] == [8])
        good_fan = fan.fan_ledger.read_text(encoding="utf-8")
        for label, text in (("a row of four cells", f"header | {Scratch.KIND} | include/H00.h | 1\n"),
                            ("a word count", f"header | {Scratch.KIND} | include/H99.h | many | 2\n"),
                            ("a second row of one header", f"header | {Scratch.KIND} | include/H00.h | 100 | 2\n"),
                            ("a count of zero", f"header | {Scratch.KIND} | include/H99.h | 0 | 2\n")):
            fan.fan_ledger.write_text(good_fan + text, encoding="utf-8")
            status, found, _ = fan_run()
            expect(f"header-fanout: a ledger with {label} is an error",
                   status == 1 and found[0].path == "header-fanout-ledger.txt")
        fan.fan_ledger.write_text(good_fan, encoding="utf-8")
        (fan.build / "neg-compile" / "neg_one" / "neg_one.inputs").unlink()
        status, found, _ = fan_run()
        expect("header-fanout: a fixture with no record is an error", status == 1 and "neg_one" in found[0].message)

        # instruction-total: two objects of 2 G and two fixtures of 5 G, a total of 14 G.
        counted = Scratch(Path(scratch_name).resolve() / "counted")

        def count_run(**options: object) -> tuple[int, list[check_report.Finding], str]:
            return counted.run(check="instruction-total", **options)  # type: ignore[arg-type]

        status, found, _ = count_run()
        expect("instruction-total: a ledger with no row of the kind exits 3", status == NOT_APPLICABLE and not found)
        status, _, output = count_run(write=True)
        expect("instruction-total: --write writes the total row",
               status == 0 and f"total | {Scratch.KIND} | 4000000000 | 10000000000 | {DEFAULT_REASON}"
               in counted.instruction_ledger.read_text(encoding="utf-8"))
        status, found, output = count_run()
        expect("instruction-total: an unchanged build gives no finding", status == 0 and not found
               and "14.0 G user instructions" in output)
        counted.add_object("a", ["src/a.cpp", "include/Base.h"], instructions=2_420_000_000)
        status, found, _ = count_run()
        expect("instruction-total: a growth of 3% gives a warning at the total row that names the largest compiles",
               status == 0 and [f.level for f in found] == ["warning"] and found[0].line == 2
               and "3.00% more" in found[0].message and "src/a.cpp 2.4 G" in found[0].message)
        counted.add_object("a", ["src/a.cpp", "include/Base.h"], instructions=2_840_000_000)
        warnings_dir = counted.root / "warnings"
        status, found, _ = count_run(warnings_dir=warnings_dir)
        expect("instruction-total: a growth of 6% gives an error that tells how to raise the baseline, and no "
               "warnings file",
               status == 1 and [f.level for f in found] == ["error"] and "6.00% more" in found[0].message
               and "--reason TEXT" in found[0].message
               and not (warnings_dir / "instruction-total.txt").exists())
        with check_report.github_actions(True):
            status, found, _ = count_run()
            expect("instruction-total: on a CI runner, a growth error stays an error", status == 1)
        status, _, output = count_run(write=True)
        expect("instruction-total: --write refuses a growth over the warning threshold with no reason",
               status == 1 and "Give the reason" in output)
        status, _, _ = count_run(write=True, reason="a planted reason")
        status_after, found, _ = count_run()
        expect("instruction-total: --write with a reason raises the baseline, and the build then passes",
               status == 0 and status_after == 0 and not found
               and "| a planted reason" in counted.instruction_ledger.read_text(encoding="utf-8"))
        counted.add_object("a", ["src/a.cpp", "include/Base.h"], instructions=2_000_000_000)
        status, found, _ = count_run()
        expect("instruction-total: a fall of more than 2% is an error that asks to lower the baseline",
               status == 1 and [f.level for f in found] == ["error"] and "5.66% less" in found[0].message
               and "Lower the baseline" in found[0].message)
        status, _, _ = count_run(write=True)
        status_after, found, _ = count_run()
        expect("instruction-total: --write lowers the baseline with no reason, and keeps the last reason",
               status == 0 and status_after == 0 and not found
               and "| a planted reason" in counted.instruction_ledger.read_text(encoding="utf-8"))
        counted.add_object("a", ["src/a.cpp", "include/Base.h"], instructions=None)
        status, found, output = count_run()
        expect("instruction-total: a compile with no count exits 3 and names it",
               status == NOT_APPLICABLE and not found and "CMakeFiles/a.o" in output)

    if failures:
        print(f"check-parse-cost --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-parse-cost --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Parse the command line and run the check, the write or the self-test."""
    parser = argparse.ArgumentParser(prog="check-parse-cost.py", description=__doc__.split("\n")[0])
    parser.add_argument("--check", choices=CHECKS)
    parser.add_argument("--build-dir", type=Path)
    parser.add_argument("--kind", help="the kind of the build, in place of the line of BUILD_DIR/build-kind.txt")
    parser.add_argument("--write", action="store_true", help="write the baseline of the kind again")
    parser.add_argument("--reason", help="the reason of the baseline that --write writes")
    parser.add_argument("--self-test", action="store_true", help="plant each verdict of the check")
    check_report.add_arguments(parser)
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return self_test()
    if arguments.check is None or arguments.build_dir is None:
        parser.error("--check and --build-dir are necessary")
    build_dir = arguments.build_dir.resolve()
    kind = arguments.kind or cost_meter.read_kind(str(build_dir))
    if kind is None:
        return check_report.emit([check_report.Finding(
            "error", build_census.shown(build_dir / cost_meter.KIND_FILE, REPO_ROOT), 0, arguments.check,
            "the build has no build kind.  cmake/BuildLauncher.cmake writes it at each configure.  Configure the build "
            "again")], arguments.check, arguments.warnings_dir)
    return run(arguments.check, build_dir, REPO_ROOT, kind, SCRIPTS / LEDGERS[arguments.check], check_report.BUDGETS,
               arguments.warnings_dir, arguments.write, arguments.reason)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
