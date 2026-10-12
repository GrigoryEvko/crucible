"""The test of the emitted tests of the session oracle: each clean unit exits 0, and each planted row fails.

THE UNITS
    The test compiles the tests that test/session_oracle/golden.csv emits,
    against the include tree of the working tree, with the patched compiler
    of assay.core.testing.  A static test gives one unit for each shard,
    because each shard compiles alone.  A runtime test gives one unit for
    each program, because a program links and runs all its shards.
    PROGRAM_SHARD_BUDGET gives the length of the shards of a program.  Each
    clean unit must exit 0.  A copy of the rows holds one incorrect agree row
    in each emitted test (_plant()).  Each planted unit holds the rows of the
    case of its planted row, and it must fail and name the planted case.

THE PARTS
    With `--part K/N`, the test does only the units that unit_part() of
    assay.tests.oracle gives to part K.  The part of CHECK_PART checks the
    type grammar of the emitter and the drift of assay.oracle.runner, which
    are Python work.  It builds no unit when N is more than FIRST_UNIT_PART.  Only golden.csv gives the sequence of the units, and the
    parts are then disjoint.  The group oracle of `guard selftest` gives this
    module its part (THE PARTS of assay.tests.oracle).

THE STORE
    The store of assay.oracle.unit_store keeps the result of each unit.  A
    unit with inputs that did not change then builds nothing.  The part of
    CHECK_PART removes the expired sources of the store.

`guard selftest tests.oracle.test_runner [--part K/N]` runs the test alone.
"""

import dataclasses
import os
import tempfile
from collections.abc import Callable, Iterator
from pathlib import Path

from assay.core import proc, testing
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.guard import selftest
from assay.oracle import runner
from assay.oracle.emit import (
    SHARD_BUDGET,
    WIRE_FAMILIES,
    Row,
    emit_family,
    emit_files,
    emit_self_check,
    read_golden,
    shard_file,
)
from assay.oracle.unit_store import UnitResult, UnitStore
from assay.tests import oracle as oracle_parts

# The group oracle gives this module the part of the call (THE PARTS).
PARTED = True
# The emitted tests with assertions that run, with the families of their rows.
# The test is a program that fails when a row is different.  Each other
# emitted test fails to compile.
RUNTIME_TESTS: dict[str, tuple[str, ...]] = {"fixy_wire": WIRE_FAMILIES}
# The units of one part at one time: the job count of the package (THE WORKER
# POOLS of assay.guard.engines.buildsys.job_pools).  A program compiles its
# shards in a pool of its own.  The compile slots of the store hold the
# compiles of all units to the same count (THE COMPILE SLOTS of
# assay.oracle.unit_store).  Each compile parses its headers, and the longest
# compile sets the wall time of a part.
JOBS = proc.DEFAULT_JOBS
# The measured rows of one program of a runtime test.  A program holds full
# cases, links its shards and runs its rows.  Each program is one unit, and
# the programs of one test can go to different parts.
PROGRAM_ROWS = 120
# The length of one shard of a program, in characters of emitted case
# text.  The shards of a program compile at the same time, and its longest
# compile sets the wall time of its part.  On 2026-10-09, a wire shard with
# no rows compiled in 1.3 s.  A shard of the length of the build compiled
# in 2.5 to 4 s.  Half that length made the compiles of each program 0.6 s
# to 0.7 s shorter.  A third of that length made them almost no shorter: a
# keyed pair stays in one shard, and the heaviest pairs set the time.
PROGRAM_SHARD_BUDGET = SHARD_BUDGET["fixy_wire"] // 2


@dataclasses.dataclass(frozen=True, slots=True)
class Outcome:
    """The failures of each kind of one part, and the counts of its units."""

    grammar: tuple[str, ...]
    drift: tuple[str, ...]
    clean: tuple[str, ...]
    planted: tuple[str, ...]
    units: int
    planted_units: int
    stored: int


def _plant(rows: list[Row]) -> tuple[list[Row], dict[str, str]]:
    """Return a copy of ``rows`` with one incorrect agree row in each emitted test, and the label of each planted row.

    The map gives, for the stem of each emitted test, the label that the
    assertion of the planted row prints.

    Raises:
        RuntimeError: If an emitted test has no agree row to plant
    """
    planted = list(rows)
    labels: dict[str, str] = {}

    def wrong_bool(row: Row) -> str:
        """Return the other answer of a boolean row."""
        return "false" if row.ours == "true" else "true"

    def plant_one(stem: str, picked: Callable[[Row], bool], wrong: Callable[[Row], str]) -> None:
        """Make the first row that ``picked`` accepts incorrect, and keep its label for ``stem``."""
        for index, row in enumerate(planted):
            if picked(row):
                planted[index] = dataclasses.replace(row, ours=wrong(row))
                role = f" role {row.role}" if row.role != "-" else ""
                labels[stem] = f"session_oracle {row.family} case {row.case}{role}:"
                return
        raise RuntimeError(f"no agree row to plant in the test {stem}")

    plant_one("fixy_duality", lambda row: row.family == "fixy.is_dual" and row.status == "agree", wrong_bool)
    plant_one("fixy_projection", lambda row: row.family == "fixy.global_wf" and row.status == "agree", wrong_bool)
    plant_one("fixy_subtype", lambda row: row.family == "fixy.subtype_sync" and row.status == "agree", wrong_bool)
    plant_one(
        "fixy_keyed_subtype", lambda row: row.family == "fixy.keyed_subtype_sync" and row.status == "agree", wrong_bool
    )
    plant_one(
        "fixy_keyed_projection",
        lambda row: row.family == "fixy.keyed_live" and row.status == "agree" and row.ours in ("true", "false"),
        wrong_bool,
    )
    plant_one("fixy_crash", lambda row: row.family == "fixy.crash_live" and row.status == "agree", wrong_bool)
    plant_one("fixy_enroute", lambda row: row.family == "fixy.enroute_live" and row.status == "agree", wrong_bool)
    plant_one("fixy_wire", lambda row: row.family == "fixy.wire" and row.status == "agree", lambda row: "abort")
    missing = set(SHARD_BUDGET) - set(labels)
    if missing:
        raise RuntimeError(f"no planted row for {sorted(missing)}")
    return planted, labels


def wire_run_is_stable(result: UnitResult) -> bool:
    """Return True when the run of a wire program gives the same result on the next run with the same inputs.

    A run of a row that runs for more time than its limit gives the outcome
    stuck (wire_driver.h).  The load of the host changes that time.  Each
    other outcome comes from the protocol alone.
    """
    return " the run gives stuck," not in result.stderr


def _drift_failures(files: dict[str, str], copy: Path) -> list[str]:
    """Return the failures of the check of the drift of assay.oracle.runner on planted drift in ``copy``.

    ``files`` holds the name and the text of each file that the golden file
    emits.  The check must see a change of one line in a committed file, and
    a file that the golden file does not emit.
    """
    copy.mkdir()
    for name, text in files.items():
        (copy / name).write_text(text, encoding="utf-8")
    victim = min(files)
    (copy / victim).write_text((copy / victim).read_text(encoding="utf-8") + "// drift\n", encoding="utf-8")
    stale = shard_file("fixy_stale", 0)
    (copy / stale).write_text("// a shard that the golden file does not emit\n", encoding="utf-8")
    if runner.drifted(files, copy) != [victim, stale]:
        return [f"the drift check does not report the changed {victim} and the stale {stale}"]
    return []


def runtime_programs(by_case: dict[str, list[Row]], families: tuple[str, ...]) -> Iterator[list[Row]]:
    """Yield the rows of each runtime program: full cases in sequence, approximately PROGRAM_ROWS measured rows each.

    A measured row is a row of ``families`` with a status that is not
    `gap`.  A case with no measured row goes to no program.  Complexity:
    O(rows).
    """
    program: list[Row] = []
    measured = 0
    for case_rows in by_case.values():
        count = sum(row.family in families and row.status != "gap" for row in case_rows)
        if count == 0:
            continue
        if program and measured + count > PROGRAM_ROWS:
            yield program
            program, measured = [], 0
        program.extend(case_rows)
        measured += count
    if program:
        yield program


def _units_of(
    rows: list[Row], planted: list[Row], labels: dict[str, str], emitted: dict[str, list[str]]
) -> Iterator[tuple[str, str, list[str], str | None]]:
    """Yield each unit in one sequence: its name, its stem, its shards and its label (THE UNITS).

    The label is None for a unit of clean rows.  For a unit of planted rows,
    it is the text that the planted row prints.  The shards of each static
    test go into ``emitted`` with its stem.  The tests with the smallest
    shards go first, because their rows cost the most and their builds must
    start first.  Complexity: O(rows).
    """
    by_case: dict[str, list[Row]] = {}
    for row in rows:
        by_case.setdefault(row.case, []).append(row)
    for stem in sorted(SHARD_BUDGET, key=lambda name: (SHARD_BUDGET[name], name)):
        families = RUNTIME_TESTS.get(stem)
        if families is None:
            emitted[stem] = emit_family(rows, stem)
            for index, shard in enumerate(emitted[stem]):
                yield f"clean {stem} shard {index}", stem, [shard], None
            continue
        for index, program in enumerate(runtime_programs(by_case, families)):
            yield f"clean {stem} program {index}", stem, emit_family(program, stem, PROGRAM_SHARD_BUDGET), None
    for stem, label in labels.items():
        case = label.split(" case ", 1)[1].split()[0].rstrip(":")
        budget = PROGRAM_SHARD_BUDGET if stem in RUNTIME_TESTS else None
        yield f"planted {stem}", stem, emit_family([row for row in planted if row.case == case], stem, budget), label


def run_part(cxx: str, include: Path, jobs: int, part: tuple[int, int], rows: list[Row]) -> Outcome:
    """Build and judge each unit of one part (THE UNITS, THE PARTS), and give the failures and the counts.

    Raises:
        RuntimeError: If an emitted test has no agree row to plant
    """
    from concurrent.futures import Future, ThreadPoolExecutor

    index_of_part, count = part
    is_check_part = index_of_part == oracle_parts.role_part(oracle_parts.CHECK_PART, count)
    grammar = tuple(emit_self_check()) if is_check_part else ()
    drift: tuple[str, ...] = ()
    planted, labels = _plant(rows)
    emitted: dict[str, list[str]] = {}
    units: list[tuple[str, str | None, UnitResult | Future[UnitResult]]] = []
    with (
        tempfile.TemporaryDirectory(prefix="session_oracle_units_") as scratch,
        ThreadPoolExecutor(max_workers=jobs) as pool,
    ):
        root = Path(scratch)
        store = UnitStore(
            cxx,
            runner.cxx_flags(include),
            runner.runtime_flags(cxx),
            root,
            caller=f"self-test-{index_of_part}-of-{count}",
        )
        # The lookups run in this thread, and only a unit that the store does
        # not hold goes to the pool.  A lookup is Python work, and many of
        # them in many threads cost more CPU time than one after the other.
        for position, (name, stem, shards, label) in enumerate(_units_of(rows, planted, labels, emitted)):
            if oracle_parts.unit_part(position, count) != index_of_part:
                continue
            files = [(shard_file(stem, index), text) for index, text in enumerate(shards)]
            unit = (
                store.program_unit(files, (), wire_run_is_stable) if stem in RUNTIME_TESTS else store.static_unit(files)
            )
            stored = store.stored(unit)
            units.append((name, label, stored if stored is not None else pool.submit(store.build, unit)))
        # The check of the drift is Python work, and it runs while the compiles run.
        if is_check_part:
            for stem in RUNTIME_TESTS:
                emitted[stem] = emit_family(rows, stem)
            drift = tuple(_drift_failures(emit_files({stem: emitted[stem] for stem in SHARD_BUDGET}), root / "drift"))
            store.sweep_sources()
        results = [
            (name, label, result.result() if isinstance(result, Future) else result) for name, label, result in units
        ]
        store.save_memo()
    clean: list[str] = []
    planted_failures: list[str] = []
    for name, label, result in results:
        if label is None and result.returncode != 0:
            clean.append(f"{name} fails:\n{(result.stdout + result.stderr)[-2000:]}")
        elif label is not None and result.returncode == 0:
            planted_failures.append(f"{name} passes, and the planted row is not caught")
        elif label is not None and label not in result.stderr:
            planted_failures.append(f"{name} fails, but not on the planted row ({label})")
    return Outcome(
        grammar,
        drift,
        tuple(clean),
        tuple(planted_failures),
        len(results),
        sum(label is not None for _, label, _ in results),
        sum(result.is_stored for _, _, result in results),
    )


def emitted_test_cases(expect: testing.Expect, part: tuple[int, int]) -> None:
    """Build one part of the units, and judge each kind of failure (THE UNITS, THE PARTS)."""
    cxx = testing.patched_compiler()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the patched compiler", bool(cxx) and Path(cxx).is_file())
    if not cxx or not Path(cxx).is_file():
        return
    index_of_part, count = part
    _, rows = read_golden(runner.GOLDEN)
    try:
        with runner.measured_tree(None) as (include, _):
            outcome = run_part(cxx, include, min(JOBS, os.cpu_count() or 1), part, rows)
    except RuntimeError as problem:
        expect(f"each emitted test has an agree row to plant: {problem}", False)
        return
    for failure in (*outcome.grammar, *outcome.drift, *outcome.clean, *outcome.planted):
        check_report.write_line(f"session_oracle units part {index_of_part}/{count}: {failure}", is_error=True)
    if index_of_part == oracle_parts.role_part(oracle_parts.CHECK_PART, count):
        expect("the emitter accepts its type grammar", not outcome.grammar)
        expect("the drift comparison sees a one-line change and a stale shard", not outcome.drift)
    if outcome.units == 0:
        return
    clean = outcome.units - outcome.planted_units
    expect(
        f"each of the {clean} clean units of part {index_of_part}/{count} passes "
        f"({outcome.stored} of {outcome.units} results come from the store)",
        not outcome.clean,
    )
    expect(
        f"each of the {outcome.planted_units} planted units of part {index_of_part}/{count} fails on its planted row",
        not outcome.planted,
    )


def main(argv: list[str]) -> int:
    """Run the cases of one part, and write one line for each (THE PARTS)."""
    module = __name__.removeprefix("assay.")
    part = selftest.parsed_part(argv)
    if part is None:
        check_report.write_line(f"usage: guard selftest {module} [--part K/N], with 0 <= K < N", is_error=True)
        return ExitCode.USAGE
    index, count = part
    has_work = index == oracle_parts.role_part(oracle_parts.CHECK_PART, count) or oracle_parts.holds_units(index, count)
    groups: list[testing.Group] = [lambda expect: emitted_test_cases(expect, part)] if has_work else []
    return testing.run(module, [], groups)
