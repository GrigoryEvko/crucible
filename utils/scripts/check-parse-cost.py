#!/usr/bin/env python3
"""check-parse-cost — the total of the bytes that the compiles of one build and its fixtures read, against a baseline.

THE CHECK
    parse-total     The sum, over each object of the target all and each
                    negative fixture, of the bytes of each file that its
                    compile reads.  The file lists come from
                    utils/scripts/build_census.py: the ninja dependency log
                    for an object, and the record of the fixture driver for a
                    fixture.  A ccache hit and a result of the fixture store
                    record their files too, so the total does not depend on
                    the caches, and it does not depend on the load of the
                    host.

    Each per-job budget can pass while the total grows: one more include in
    a base header costs a little in each of thousands of compiles.  On 192
    cores the wall time of a build is at least its total CPU divided by 192,
    so the total is the budget of a snappy build.

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
    The row parse-total of utils/scripts/budgets.txt gives two thresholds in
    percent of the baseline.  A total that grew by more than the warning
    threshold gives a warning, and by more than the error threshold an error.
    A total that fell by more than the warning threshold is an error too,
    with the message to lower the baseline in the same commit, so the
    baseline keeps no slack.  To accept a growth, raise the baseline in the
    same commit: --write --reason TEXT.  --write refuses a growth over the
    warning threshold with no reason.  Each finding names the files with the
    largest change of their product: the bytes, the readers and the product
    now, and the change from the baseline.  A file under the floor of the
    baseline counts as zero there.

NOT APPLICABLE
    The check exits 3, with the reason, when the census does not apply to the
    build (build_census.NotApplicable: no ninja, only some targets built, no
    fixture ran), or when the ledger has no total row of the kind of the
    build.  A kind with no row is a build that nobody measured, for example a
    build for another architecture.  Each other input that the check cannot
    read is an error.

Usage
    check-parse-cost.py --check parse-total --build-dir DIR [--warnings-dir DIR]
    check-parse-cost.py --check parse-total --build-dir DIR --write [--reason TEXT]
    check-parse-cost.py --self-test

Exit 0 with no finding or with warnings only, 1 with an error, 2 on a usage
error or a failed self-test, 3 when the check does not apply to the build.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
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
CHECKS = ("parse-total",)
NOT_APPLICABLE = 3
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
            f"that the baseline can hold.  Lower the baseline in the same commit, so that it keeps no slack: {write}.  "
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
        return False, (f"the total grew by {growth:.2f}% over the baseline of {size_text(old.bytes)}, more than the "
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


def run(build_dir: Path, root: Path, kind: str, ledger_path: Path, budgets_path: Path, warnings_dir: Path | None,
        write: bool = False, reason: str | None = None) -> int:
    """Run the check, or its --write, over one build.

    Returns:
        The exit status
    """
    ledger_shown = build_census.shown(ledger_path, root)
    build_shown = build_census.shown(build_dir, root)

    def fail(path: str, message: str, line: int = 0) -> int:
        if write:
            print(f"parse-total: {message}", file=sys.stderr)
            return 1
        return check_report.emit([check_report.Finding("error", path, line, "parse-total", message)], "parse-total",
                                 warnings_dir)

    try:
        budget = check_report.read_budgets(budgets_path).get("parse-total")
    except (OSError, ValueError) as problem:
        return fail(build_census.shown(budgets_path, root), str(problem))
    if budget is None:
        return fail(build_census.shown(budgets_path, root), f"{budgets_path.name} has no row parse-total")
    try:
        ledger = read_ledger(ledger_path)
    except LedgerError as problem:
        return fail(ledger_shown, str(problem))
    if not write and kind not in ledger.totals:
        print(f"parse-total: {ledger_shown} has no total row of the kind {kind}, so no baseline applies to this "
              f"build.  The check does not apply")
        return NOT_APPLICABLE
    try:
        census = build_census.read_census(build_dir, root)
    except build_census.NotApplicable as problem:
        print(f"parse-total: {problem}.  The check does not apply")
        return NOT_APPLICABLE
    except build_census.CensusError as problem:
        return fail(build_shown, str(problem))
    measured = measure(census, root)
    if write:
        is_written, summary = write_ledger(ledger_path, ledger, kind, measured, reason, budget)
        print(summary, file=sys.stderr if not is_written else sys.stdout)
        return 0 if is_written else 1
    findings = evaluate(measured, ledger, ledger_shown, kind, budget, build_shown)
    status = check_report.emit(findings, "parse-total", warnings_dir)
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

    def __init__(self, root: Path) -> None:
        """Make the tree with one header, two objects and two fixtures."""
        self.root = root
        self.build = root / "build"
        self.build.mkdir(parents=True)
        (root / "include").mkdir()
        self.ledger = root / "parse-total-ledger.txt"
        self.ledger.write_text("# a planted ledger\n", encoding="utf-8")
        self.budgets = root / "budgets.txt"
        self.budgets.write_text("parse-total | 2 | 5 | % | the growth of the total\n", encoding="utf-8")
        for name, text in (("ninja.py", FAKE_NINJA), ("ctest.py", FAKE_CTEST)):
            tool = root / name
            tool.write_text(f"#!{sys.executable}\n{text}", encoding="utf-8")
            tool.chmod(0o755)
        (self.build / "CMakeCache.txt").write_text(
            f"CMAKE_GENERATOR:INTERNAL=Ninja\nCMAKE_MAKE_PROGRAM:FILEPATH={root / 'ninja.py'}\n"
            f"CMAKE_CTEST_COMMAND:INTERNAL={root / 'ctest.py'}\n", encoding="utf-8")
        self.objects: dict[str, list[str]] = {}
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

    def add_object(self, name: str, files: list[str], state: str = "VALID") -> None:
        """Plant one object of all with its dependency list."""
        self.objects[name] = files
        obj = self.build / "CMakeFiles" / f"{name}.o"
        obj.parent.mkdir(parents=True, exist_ok=True)
        obj.write_bytes(b"object")
        self.flush(state)

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
        rows = [{"directory": str(self.build), "file": str(self.root / files[0]), "output": f"CMakeFiles/{name}.o"}
                for name, files in self.objects.items()]
        (self.build / "compile_commands.json").write_text(json.dumps(rows), encoding="utf-8")
        (self.build / "inputs.txt").write_text("".join(f"CMakeFiles/{name}.o\n" for name in self.objects),
                                               encoding="utf-8")
        deps = []
        for name, files in self.objects.items():
            listed = "".join(f"    {self.root / rel}\n" for rel in files)
            deps.append(f"CMakeFiles/{name}.o: #deps {len(files)}, deps mtime 1 ({state})\n{listed}")
        (self.build / "deps.txt").write_text("\n".join(deps), encoding="utf-8")
        tests = [{"name": name, "command": [sys.executable, "/x/test/neg_compile_driver.py", "--warnings-dir", "/w",
                                            str(self.build), str(self.root / files[0]), name, "regex"]}
                 for name, files in self.fixtures.items()]
        tests.append({"name": "test_other", "command": ["/x/test_other"]})
        (self.build / "tests.json").write_text(json.dumps({"tests": tests}), encoding="utf-8")

    def run(self, kind: str = KIND, write: bool = False, reason: str | None = None,
            warnings_dir: Path | None = None) -> tuple[int, list[check_report.Finding], str]:
        """Run the check or its --write, and return its status, its findings and its output."""
        output = io.StringIO()
        with contextlib.redirect_stdout(output), contextlib.redirect_stderr(output):
            status = run(self.build.resolve(), self.root.resolve(), kind, self.ledger, self.budgets, warnings_dir,
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
               and "has no total row" in output)
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
        del tree.objects["c"]
        tree.flush()
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
        tree.budgets.write_text("parse-total | 2 | 5 | % | the growth of the total\n", encoding="utf-8")

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
    return run(build_dir, REPO_ROOT, kind, SCRIPTS / "parse-total-ledger.txt", check_report.BUDGETS,
               arguments.warnings_dir, arguments.write, arguments.reason)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
