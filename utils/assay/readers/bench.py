"""bench — read the reports of a bench of the tree from the JSON tail of its output (bench/bench_harness.h).

THE OUTPUT
    With the variable JSON_VARIABLE set to 1, a bench prints its text
    reports, then a line MARKER, then one JSON array.  The array holds one
    object for each report (emit_reports_json() of bench/bench_harness.h).
    A bench can print more text before the marker.  The reader reads the text
    after the last line MARKER, and it reads no other line.  A bench that
    does not use the reports of the harness prints no marker, and reports()
    gives None for its output.

THE REPORT
    An object of the array gives the name of the report and the count of its
    samples.  It also gives its percentiles in nanoseconds, its coefficient
    of variation (cv), its wall time and its drift of the clock.  A report
    gives more fields, and the reader keeps only the fields of Report.  reports()
    refuses a tail that is not a JSON array, and an object with no name or
    with a field that is no number.

`guard selftest tests.readers.test_bench` does a test of the module.
"""

import json
from dataclasses import dataclass

# The variable of the JSON tail, and the line before the tail (THE OUTPUT).
JSON_VARIABLE = "CRUCIBLE_BENCH_JSON"
MARKER = "=== json ==="
# The fields of a report that the reader keeps, each a number (THE REPORT).
NUMBERS = ("n", "p50", "p99", "max", "cv", "wall_ns", "drift_pct")


class BenchError(ValueError):
    """The output of a bench has no JSON tail, or its tail does not have the shape of THE REPORT."""


@dataclass(frozen=True, slots=True)
class Report:
    """One report of a bench: its name, its samples, its percentiles, its variation, its wall time and its drift."""

    name: str
    n: float
    p50: float
    p99: float
    max: float
    cv: float
    wall_ns: float
    drift_pct: float


def report_of(value: object) -> Report:
    """Return the report of one JSON object of the array (THE REPORT).

    Raises:
        BenchError: If the object has no name, or a field of NUMBERS is no number
    """
    if not isinstance(value, dict) or not isinstance(value.get("name"), str):
        raise BenchError(f"a report of the JSON tail has no name: {str(value)[:200]}")
    numbers: list[float] = []
    for field in NUMBERS:
        found = value.get(field)
        if not isinstance(found, (int, float)) or isinstance(found, bool):
            raise BenchError(f"the field {field} of the report {value['name']!r} is no number")
        numbers.append(float(found))
    return Report(str(value["name"]), *numbers)


def reports(output: str) -> tuple[Report, ...] | None:
    """Return each report of the JSON tail of the output of a bench, or None when it has no line MARKER (THE OUTPUT).

    Complexity: linear in the length of the output.

    Raises:
        BenchError: If the tail is no JSON array of reports
    """
    lines = output.splitlines()
    starts = [index for index, line in enumerate(lines) if line.strip() == MARKER]
    if not starts:
        return None
    tail = "\n".join(lines[starts[-1] + 1 :])
    try:
        found = json.loads(tail)
    except ValueError as problem:
        raise BenchError(f"the JSON tail of the bench does not read: {problem}") from None
    if not isinstance(found, list):
        raise BenchError("the JSON tail of the bench is no array")
    return tuple(report_of(item) for item in found)
