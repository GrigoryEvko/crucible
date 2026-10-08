"""The test of the reader of the cost record of a build step (assay.core.record).

`guard selftest tests.core.test_record` runs the test.  Each case writes the
record with the writer of `loop step`, or plants a record that the writer
does not give, and reads it back.  Exit 0 when each case holds, else 1.
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


def write(output: Path, step: str, result: str, cpu: float, instructions: int | None = None) -> None:
    """Write the record of one step with the writer of `loop step`."""
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(b"object")
    run = run_of(cpu, instructions)
    counts = step_meter.compile_counts(run, False, None) if step == "compile" else ()
    step_meter.write(
        step, str(output), str(output.with_suffix(".cpp")) if step == "compile" else None, run, result, counts
    )


def read_error(path: Path) -> str:
    """Return the class name of the error that read() raises for a file, or "none"."""
    try:
        cost_record.read(path)
    except cost_record.RecordError as problem:
        return type(problem).__name__
    return "none"


def written_cases(expect: testing.Expect) -> None:
    """Read the records that the writer of `loop step` gives: a compile, a link and a ccache hit."""
    with tempfile.TemporaryDirectory(prefix="test-record-") as scratch:
        build = Path(scratch)
        unit = build / "unit.cpp.o"
        write(unit, "compile", "built", 2.5, 7_000_000_000)
        record = cost_record.read_of(unit)
        expect(
            "a compile gives the step compile and the result built",
            (record.step, record.result) == (cost_record.Step.COMPILE, "built"),
        )
        expect(
            "the cost block gives the CPU time and the peak memory",
            record.cost is not None and record.cost.cpu_s == 2.5 and record.cost.peak_rss_kb == cost_meter.KB_PER_GB,
        )
        expect(
            "the cost block gives the instructions of the compiler run",
            record.cost is not None and record.cost.compiler_instructions == 7_000_000_000,
        )
        expect(
            "record_file() is the output with the suffix of the meter, in the directory of the output",
            cost_record.record_file(unit) == build / f"unit.cpp.o{cost_meter.RECORD_SUFFIX}",
        )
        write(unit, "compile", "hit", 0.1)
        hit = cost_record.read_of(unit)
        expect(
            "a ccache hit has no cost block, and it keeps the last real cost",
            hit.is_hit and hit.cost is None and hit.last_cost is not None and hit.last_cost.cpu_s == 2.5,
        )
        expect(
            "the real cost of a hit is the last real cost",
            hit.real_cost is not None and hit.real_cost.compiler_instructions == 7_000_000_000,
        )
        program = build / "bin" / "tool"
        write(program, "link", "built", 0.75)
        link = cost_record.read_of(program)
        expect(
            "a link gives the step link, no source and no instruction count",
            link.step == cost_record.Step.LINK
            and link.source is None
            and link.cost is not None
            and link.cost.compiler_instructions is None,
        )


def refused_cases(expect: testing.Expect) -> None:
    """Refuse a missing record, a record that is not JSON, and a record of a different format, step or result."""
    with tempfile.TemporaryDirectory(prefix="test-record-") as scratch:
        build = Path(scratch)
        expect(
            "a missing record raises MissingRecord",
            read_error(cost_record.record_file(build / "none.o")) == "MissingRecord",
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
            expect(f"a record with a bad {name} raises RecordError", read_error(path) == "RecordError")
        loose = cost_record.record_file(build / "loose.o")
        loose.write_text(
            '{"format": 1, "step": "compile", "result": "failed", "exit": 1, "cost": {"cpu_s": 1, '
            '"wall_s": 2, "peak_rss_kb": 3, "instructions": 9}}',
            encoding="utf-8",
        )
        record = cost_record.read(loose)
        expect(
            "a block of an earlier format gives no count from the key instructions",
            record.cost is not None and not record.cost.instructions and record.exit == 1,
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (written_cases, refused_cases))
