"""bench — read the reports of a bench of the tree from the JSON tail of its output (bench/bench_harness.h).

Output
    With the variable JSON_VARIABLE set to 1, a bench prints its text
    reports, then a line MARKER, then one JSON array.  The array holds one
    object for each report (emit_reports_json() of bench/bench_harness.h).
    A bench can print more text before the marker.  The reader reads the text
    after the last line MARKER, and it reads no other line.  A bench that
    does not use the reports of the harness prints no marker, and reports()
    gives None for its output.

Report
    An object of the array gives the name of the report and the count of its
    samples.  It also gives its percentiles in nanoseconds, its coefficient
    of variation (cv), its wall time and its drift of the clock.  A report
    gives more fields, and the reader keeps only the fields of Report.  reports()
    refuses a tail that is not a JSON array, and an object with no name or
    with a field that is no number.

    The fields of the pause check of the harness are optional, because a
    program built before that check gives none: pause_ns, the time that the
    measuring thread lost, and paused, 1 when a thread of the run lost more
    than the bound of the harness (the pause check of bench/bench_harness.h).
    A report with no such field reads as a report with no pause.  The field
    window_spread is optional for the same reason: the spread of the p50 of
    the windows of the run, (largest - smallest) / median (the windows of
    bench/bench_harness.h).  A report with no such field reads as a report
    with no spread.

`guard selftest tests.readers.test_bench` does a test of the module.
"""

import json
from dataclasses import dataclass

# The variable of the JSON tail, and the line before the tail (the output).
JSON_VARIABLE = "CRUCIBLE_BENCH_JSON"
MARKER = "=== json ==="
# The fields of a report that the reader keeps, each a number (the report).
NUMBERS = ("n", "p50", "p99", "max", "cv", "wall_ns", "drift_pct")
# The optional fields of a report, each a number, and zero when the report does not give it (the report).
OPTIONAL_NUMBERS = ("pause_ns", "paused", "window_spread")


class BenchError(ValueError):
    """The output of a bench has no JSON tail, or its tail does not have the shape of the report."""


@dataclass(frozen=True, slots=True)
class Report:
    """One report of a bench: its name, its samples, its percentiles, its variation, its wall time, its drift, its
    pause and the spread of its windows."""

    name: str
    n: float
    p50: float
    p99: float
    max: float
    cv: float
    wall_ns: float
    drift_pct: float
    pause_ns: float = 0.0
    paused: float = 0.0
    window_spread: float = 0.0

    @property
    def is_paused(self) -> bool:
        """True when a thread of the run lost more time than the bound of the harness."""
        return self.paused > 0


def number_of(value: dict[str, object], field: str) -> float:
    """Return one number field of a report.

    Raises:
        BenchError: If the field is no number
    """
    found = value.get(field)
    if not isinstance(found, (int, float)) or isinstance(found, bool):
        raise BenchError(f"the field {field} of the report {value['name']!r} is no number")
    return float(found)


def report_of(value: object) -> Report:
    """Return the report of one JSON object of the array (the report).

    Raises:
        BenchError: If the object has no name, or a field of NUMBERS or a given field of OPTIONAL_NUMBERS is no number
    """
    if not isinstance(value, dict) or not isinstance(value.get("name"), str):
        raise BenchError(f"a report of the JSON tail has no name: {str(value)[:200]}")
    numbers = [number_of(value, field) for field in NUMBERS]
    optional = [number_of(value, field) if field in value else 0.0 for field in OPTIONAL_NUMBERS]
    return Report(str(value["name"]), *numbers, *optional)


def reports(output: str) -> tuple[Report, ...] | None:
    """Return each report of the JSON tail of the output of a bench, or None when it has no line MARKER (the output).

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
