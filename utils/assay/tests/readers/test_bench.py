"""The test of the reader of the reports of a bench (assay.readers.bench).

`guard selftest tests.readers.test_bench` runs the test.  Each case writes one line.  Exit 0 when each case holds,
else 1.

The cases read a planted output with text, a marker and a JSON tail, and an output with no marker.  They hold each
refusal.
"""

import json

from assay.core import testing
from assay.readers import bench as bench_reader

REPORT = {
    "name": "try_push",
    "batch": 1,
    "n": 1000,
    "cpu": 88,
    "p50": 7.0,
    "p99": 10.5,
    "max": 142.0,
    "cv": 0.021,
    "wall_ns": 2.5e8,
    "drift_pct": 0.3,
}


def read_cases(expect: testing.Expect) -> None:
    """Hold the reports of the last JSON tail of an output (THE OUTPUT, THE REPORT)."""
    tail = json.dumps([REPORT, dict(REPORT, name="try_pop", cv=0.07)])
    output = f"try_push: p50=7 ns\n{bench_reader.MARKER}\n[]\nmore text\n{bench_reader.MARKER}\n{tail}\n"
    found = bench_reader.reports(output) or ()
    expect(
        "the reader reads the tail after the last marker, and keeps the fields of a report",
        [item.name for item in found] == ["try_push", "try_pop"] and found[0].p50 == 7.0 and found[1].cv == 0.07,
    )
    expect("an output with no marker gives None", bench_reader.reports("try_push: p50=7 ns\n") is None)


def refusal_cases(expect: testing.Expect) -> None:
    """Hold the refusal of a tail that does not read, and of a bad report."""
    for name, output in (
        ("a tail that is not JSON", f"{bench_reader.MARKER}\n[{{\n"),
        ("a tail that is no array", f"{bench_reader.MARKER}\n{{}}\n"),
        ("a report with no name", f'{bench_reader.MARKER}\n[{{"n": 1}}]\n'),
        ("a report with a field that is no number", f"{bench_reader.MARKER}\n{json.dumps([dict(REPORT, cv='low')])}\n"),
    ):
        try:
            bench_reader.reports(output)
            expect(f"the reader refuses {name}", False)
        except bench_reader.BenchError:
            expect(f"the reader refuses {name}", True)


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(__name__.removeprefix("assay."), argv, (read_cases, refusal_cases))
