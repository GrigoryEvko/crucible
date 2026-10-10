"""The test of the one reader and writer of the cost records (assay.core.record).

`guard selftest tests.core.test_record` runs the test.  Each case writes a
record with write(), as the step meters write it, or plants a record that the
writer does not give, and reads it back.  Exit 0 when each case holds, else 1.
"""

import tempfile
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import record as cost_record
from assay.core import testing
from assay.loop import step as step_meter


def run_of(cpu: float, instructions: int | None = None) -> cost_meter.Measurement:
    """Return the measurement of a planted step with a CPU time, a peak of 1 GB and an instruction count."""
    return cost_meter.Measurement(0, cpu, 0.0, cpu, cost_meter.KB_PER_GB, 1.0, 1.0, None, instructions)


def write(
    output: Path,
    step: cost_record.Step,
    result: str,
    cpu: float,
    instructions: int | None = None,
    last_cost: cost_record.Cost | None = None,
) -> None:
    """Write the record of one step as `loop step` writes it, with the last cost that the cost store gives a hit."""
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(b"object")
    run = run_of(cpu, instructions)
    counts = step_meter.compile_counts(run, False, None) if step == cost_record.COMPILE else ()
    source = str(output.with_suffix(".cpp")) if step == cost_record.COMPILE else None
    cost_record.write(
        cost_record.record_file(output),
        step_meter.step_record(step, str(output), source, run, result, counts, last_cost),
    )


def read_error(read: object, path: Path) -> str:
    """Return the class name of the error that a reader raises for a file, or "none"."""
    try:
        read(path)  # type: ignore[operator]
    except cost_record.RecordError as problem:
        return type(problem).__name__
    return "none"


def build_cases(expect: testing.Expect) -> None:
    """Read the records that the writer gives for a compile, a link and a ccache hit."""
    with tempfile.TemporaryDirectory(prefix="test-record-") as scratch:
        build = Path(scratch)
        unit = build / "unit.cpp.o"
        write(unit, cost_record.COMPILE, "built", 2.5, 7_000_000_000)
        record = cost_record.read_of(unit)
        expect(
            "a compile gives the step compile, the result built, the source and the size of the output",
            (record.step, record.result, record.source, record.output_bytes)
            == (cost_record.COMPILE, "built", str(unit.with_suffix(".cpp")), len(b"object")),
        )
        expect(
            "the cost block gives the CPU time, the peak memory and the instructions of the compiler run",
            record.cost is not None
            and record.cost.cpu_s == 2.5
            and record.cost.peak_rss_kb == cost_meter.KB_PER_GB
            and record.cost.compiler_instructions == 7_000_000_000,
        )
        expect(
            "record_file() is the output with the suffix of the records, in the directory of the output",
            cost_record.record_file(unit) == build / f"unit.cpp.o{cost_record.SUFFIX}",
        )
        write(unit, cost_record.COMPILE, cost_record.HIT, 0.1, last_cost=record.cost)
        hit = cost_record.read_of(unit)
        expect(
            "a ccache hit has no cost block, and it keeps the last cost that the cost store gives it, which is its "
            "real cost",
            hit.is_hit and hit.cost is None and hit.last_cost == record.cost and hit.real_cost == record.cost,
        )
        write(unit, cost_record.COMPILE, cost_record.HIT, 0.1)
        expect(
            "a hit with no last cost from the cost store keeps no cost of the earlier record of the output",
            cost_record.read_of(unit).last_cost is None and cost_record.read_of(unit).real_cost is None,
        )
        write(unit, cost_record.COMPILE, "built", 2.5, 7_000_000_000, last_cost=record.cost)
        expect("a compile that is not a hit keeps no last cost", cost_record.read_of(unit).last_cost is None)
        block_cases(expect, record)
        program = build / "bin" / "tool"
        write(program, cost_record.LINK, "built", 0.75)
        link = cost_record.read_of(program)
        expect(
            "a link gives the step link, no source and no instruction count",
            link.step == cost_record.LINK
            and link.source is None
            and link.cost is not None
            and link.cost.compiler_instructions is None,
        )


def block_cases(expect: testing.Expect, record: cost_record.CostRecord) -> None:
    """Read a cost block back from the text of cost_text(), as `loop step` reads a block of the cost store."""
    if record.cost is None:
        expect("the record of the compile has a cost block", False)
        return
    text = cost_record.cost_text(record.cost)
    read_back = cost_record.cost_from_text(text)
    expect(
        "cost_from_text() reads back the cost block that cost_text() writes, with its instruction count",
        read_back == record.cost and read_back is not None and cost_record.cost_text(read_back) == text,
    )
    uncounted = cost_record.measured(run_of(1.5))
    expect(
        "cost_from_text() reads back a cost block with no instruction count",
        cost_record.cost_from_text(cost_record.cost_text(uncounted)) == uncounted,
    )
    refused = {
        "a text that is no object": text[1:],
        "a key that a cost block does not hold": text.replace('"load"', '"lode"'),
        "a missing time": text.replace(', "end": 1.000', ""),
        "a time that is not finite": text.replace('"cpu_s": 2.500', '"cpu_s": nan'),
        "a negative count": text.replace(": 7000000000", ": -7"),
        "a count that is no integer": text.replace(": 7000000000", ": 7.5"),
        "a JSON text with other spaces": text.replace(", ", ","),
    }
    for label, damaged in refused.items():
        expect(f"cost_from_text() refuses {label}", damaged != text and cost_record.cost_from_text(damaged) is None)


def test_and_fixture_cases(expect: testing.Expect) -> None:
    """Write and read the record of a test and the record of a fixture, also when their directory is not there."""
    with tempfile.TemporaryDirectory(prefix="test-record-") as scratch:
        build = Path(scratch)
        cost = cost_record.measured(run_of(1.25))
        written = cost_record.TestRecord("run.sh", "rejected", "/b/test/run.sh", 0, cost)
        path = Path(cost_record.test_record_file(build, "run.sh"))
        cost_record.write(path, written)
        expect(
            "the record of a test is BUILD_DIR/test-records/NAME.test.cost, and it reads back equal",
            path == build / cost_record.TEST_DIRECTORY / f"run.sh{cost_record.TEST_SUFFIX}"
            and cost_record.read_test(path) == written,
        )
        inputs = ("/s/neg/neg_a.cpp", "/s/include/a.h")
        fixture = cost_record.FixtureRecord("neg_a", "stored", 0.5, 0.125, 3_000_000_000, inputs)
        fixture_path = Path(cost_record.fixture_record_file(build, "neg_a"))
        cost_record.write(fixture_path, fixture)
        expect(
            "the record of a fixture is BUILD_DIR/neg-compile/NAME/NAME.inputs, and it reads back equal",
            fixture_path == build / "neg-compile" / "neg_a" / "neg_a.inputs"
            and cost_record.read_fixture(fixture_path) == fixture
            and fixture.cpu_s == 0.625,
        )
        blind = cost_record.FixtureRecord("neg_a", "compiled", 0.5, 0.0, None, None)
        cost_record.write(fixture_path, blind)
        expect(
            "a fixture record with no inputs and no count reads back equal",
            cost_record.read_fixture(fixture_path) == blind,
        )
        broken = cost_record.FixtureRecord("neg_a", "compiled", 0.5, 0.0, None, ("/s/a\nb.h",))
        cost_record.write(fixture_path, broken)
        expect(
            "a path with a line break gives a fixture record with no inputs",
            cost_record.read_fixture(fixture_path).inputs is None,
        )
        census = (("std-object", 25, 9, "std::jthread"), ("c-library-call", 30, 5, 'abort "é"'))
        counted = cost_record.FixtureRecord("neg_a", "compiled", 0.5, 0.0, None, inputs, "0123abcd", census)
        cost_record.write(fixture_path, counted)
        expect(
            "a fixture record with a census stamp and a census of two findings reads back equal",
            cost_record.read_fixture(fixture_path) == counted,
        )
        empty = cost_record.FixtureRecord("neg_a", "stored", 0.5, 0.0, None, inputs, "0123abcd", ())
        cost_record.write(fixture_path, empty)
        expect(
            "a census with no finding reads back as an empty census", cost_record.read_fixture(fixture_path) == empty
        )
        expect(
            "a fixture record that the call gives no census has no census stamp and no census",
            blind.census_stamp is None and blind.census is None,
        )
        expect(
            "a record is a value: equal fields give an equal record, and a field does not change",
            written == cost_record.TestRecord("run.sh", "rejected", "/b/test/run.sh", 0, cost)
            and changes_refused(written),
        )
        window_cases(expect, build, cost)


def window_cases(expect: testing.Expect, build: Path, cost: cost_record.Cost) -> None:
    """Write and read the window of a test, with the JSON reader and with the text reader of the test launcher."""
    path = Path(cost_record.test_record_file(build, "run.sh"))
    expect(
        "a test record with no window holds the wall time of its cost block as its window",
        cost_record.TestRecord("run.sh", "passed", "sh", 0, cost).walls == (cost.wall_s,),
    )
    windowed = cost_record.TestRecord("run.sh", "passed", "sh", 0, cost, (6.5, 4.25, 5.0), "0123456789abcdef")
    cost_record.write(path, windowed)
    expect(
        "the window of a test and its version read back equal with each reader, the newest run first, and its least "
        "wall time is its smallest value",
        cost_record.read_test(path) == windowed
        and cost_record.recent_window(str(path)) == ("0123456789abcdef", (6.5, 4.25, 5.0))
        and windowed.least_wall_s == 4.25,
    )
    empty = cost_record.TestRecord("run.sh", "passed", "sh", 0, cost, ())
    cost_record.write(path, empty)
    expect(
        "an empty window reads back empty with no version, and its least wall time is the wall time of the cost block",
        cost_record.read_test(path).walls == ()
        and cost_record.read_test(path).version == ""
        and cost_record.recent_window(str(path)) == ("", ())
        and empty.least_wall_s == cost.wall_s,
    )
    expect(
        "the text reader gives no version and no window for a record that is not there",
        cost_record.recent_window(cost_record.test_record_file(build, "none")) == ("", ()),
    )
    shapes = (
        ("a word that is no number", '"version": "ab",\n"walls": [1.0, x]', ("ab", ())),
        ("a negative time", '"walls": [-1.0]', ("", ())),
        ("a version that is not hexadecimal", '"version": "a-b",\n"walls": [1.0]', ("", (1.0,))),
        ("a version with no quotation marks", '"version": ab,\n"walls": [1.0]', ("", (1.0,))),
    )
    for label, text, read in shapes:
        path.write_text(f'{{\n"format": 1,\n{text}\n}}\n', encoding="utf-8")
        expect(
            f"the text reader gives no window or no version for {label}", cost_record.recent_window(str(path)) == read
        )
    head = '{"format": 1, "step": "test", "result": "passed", "test": "run.sh", "command": "sh", "exit": 0, '
    block = '"cost": {"cpu_s": 1, "wall_s": 2.5, "peak_rss_kb": 3}'
    path.write_text(f"{head}{block}}}\n", encoding="utf-8")
    expect(
        "a test record of an earlier launcher, with no window and no version, holds the wall time of its cost block",
        cost_record.read_test(path).walls == (2.5,) and cost_record.read_test(path).version == "",
    )
    for label, tail in (
        ("a window of a value that is no time", '"walls": [1, true]'),
        ("a version that is not hexadecimal", '"version": "x"'),
        ("a version that is no text", '"version": 7'),
    ):
        path.write_text(f"{head}{block}, {tail}}}\n", encoding="utf-8")
        expect(
            f"a test record with {label} raises RecordError", read_error(cost_record.read_test, path) == "RecordError"
        )
    try:
        cost_record.TestRecord("run.sh", "passed", "sh", 0, cost, (), "X")
        is_refused = False
    except ValueError:
        is_refused = True
    expect("a test record refuses a version that is not hexadecimal", is_refused)


def changes_refused(record: cost_record.TestRecord) -> bool:
    """Return True when an assignment to each field of a record raises AttributeError."""
    for name in record.__match_args__:
        try:
            setattr(record, name, None)
        except AttributeError:
            continue
        return False
    return True


def refused_cases(expect: testing.Expect) -> None:
    """Refuse a missing record, a record that is not JSON, and a record of a different format, step or result."""
    with tempfile.TemporaryDirectory(prefix="test-record-") as scratch:
        build = Path(scratch)
        expect(
            "a missing record raises MissingRecord",
            read_error(cost_record.read, cost_record.record_file(build / "none.o")) == "MissingRecord",
        )
        planted = {
            "not-json": "{",
            "format": '{"format": 2, "step": "compile", "result": "built"}',
            "step": '{"format": 1, "step": "test", "result": "built"}',
            "result": '{"format": 1, "step": "compile", "result": "late"}',
            "block": '{"format": 1, "step": "compile", "result": "built", "cost": {"cpu_s": 1}}',
            "bool": '{"format": 1, "step": "link", "result": "built", "cost": {"cpu_s": true, "wall_s": 1, '
            '"peak_rss_kb": 1}}',
        }
        for name, text in planted.items():
            path = cost_record.record_file(build / f"{name}.o")
            path.write_text(text, encoding="utf-8")
            expect(
                f"a record with a bad {name} raises RecordError", read_error(cost_record.read, path) == "RecordError"
            )
        loose = cost_record.record_file(build / "loose.o")
        loose.write_text(
            '{"format": 1, "step": "compile", "result": "failed", "exit": 1, "cost": {"cpu_s": 1, '
            '"wall_s": 2, "peak_rss_kb": 3, "instructions": 9}}',
            encoding="utf-8",
        )
        record = cost_record.read(loose)
        expect(
            "a block of an earlier format gives no count from the key instructions",
            record.cost is not None and not record.cost.counts and record.exit == 1,
        )
        test_path = Path(cost_record.test_record_file(build, "planted_test"))
        test_path.parent.mkdir()
        test_path.write_text('{"format": 1, "step": "compile", "cost": {}}', encoding="utf-8")
        expect(
            "a record of a compile is no record of a test",
            read_error(cost_record.read_test, test_path) == "RecordError",
        )
        fixture_path = Path(cost_record.fixture_record_file(build, "neg_b"))
        expect(
            "a fixture with no record raises MissingRecord",
            read_error(cost_record.read_fixture, fixture_path) == "MissingRecord",
        )
        fixture_path.parent.mkdir(parents=True)
        fixture_path.write_text('{"format": 1, "fixture": "neg_other", "result": "compiled"}\n', encoding="utf-8")
        expect(
            "a fixture record of a different fixture raises RecordError",
            read_error(cost_record.read_fixture, fixture_path) == "RecordError",
        )
        head = '{"format": 1, "fixture": "neg_b", "result": "compiled", "user_s": 1, "system_s": 0, "has_inputs": false'
        bad_censuses = {
            "a stamp that is no text": ', "census_stamp": 7, "census": []}',
            "a census that is no list": ', "census_stamp": "s", "census": {}}',
            "a finding of three values": ', "census_stamp": "s", "census": [["std-object", 1, 2]]}',
            "a negative line": ', "census_stamp": "s", "census": [["std-object", -1, 2, "std::span"]]}',
        }
        for name, tail in bad_censuses.items():
            fixture_path.write_text(f"{head}{tail}\n", encoding="utf-8")
            expect(
                f"a fixture record with {name} raises RecordError",
                read_error(cost_record.read_fixture, fixture_path) == "RecordError",
            )
        fixture_path.write_text(f"{head}}}\n", encoding="utf-8")
        expect(
            "a fixture record of an earlier driver, with no census keys, has no census",
            cost_record.read_fixture(fixture_path).census_stamp is None
            and cost_record.read_fixture(fixture_path).census is None,
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (build_cases, test_and_fixture_cases, refused_cases))
