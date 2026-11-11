"""gauge_record — the record of `loop gauge`: the median of each step, its JSON lines, and the judge of the measured rule.

THE RECORD
    With --record FILE, `loop gauge` appends its result as one JSON line to
    FILE (assay.loop.gauge, THE REPORT).  read_records() reads the lines of
    such a file, and parsed_record() reads one line.  The judge reads only the
    median of each step of EDIT_STEPS.

THE MEASURED RULE
    The rule edit-build-instructions judges the median of the instruction
    counts of the edit builds (judge()).  That count does not change with the
    load of the host.  When an edit build has no exact instruction count, the
    judge judges nothing and gives a note.  `loop gauge` judges its own record
    with judged_record(), and the witness of the rule judges a planted record
    with the same function (assay.guard.engines.buildsys.held).

    This module reads no parse tree, so a rule that judges a record does not
    wait for the trees (assay.guard.engines.meta.test_registration).

`guard selftest loop.gauge` does a test of the module.

Complexity: linear in the bytes of a file of records.
"""

import json
from collections.abc import Mapping
from dataclasses import dataclass
from pathlib import Path

from assay.core import report as check_report
from assay.loop import history as loop_history

# The steps of each edit run.  A record holds the median of each step over the runs.
EDIT_STEPS = ("build", "tests")
# The measured rule that judges the median of the instruction counts of the edit builds (THE MEASURED RULE).
BUILD_RULE = "edit-build-instructions"


class GaugeError(Exception):
    """A condition that stops the gauge: a missing edit site, a clone of another commit, or too little memory."""


@dataclass(slots=True)
class Median:
    """The median of each measure of one step over the runs, and the largest other share of the runs."""

    wall_s: float
    cpu_s: float
    instructions_g: float | None
    worst_others_pct: float | None


@dataclass(frozen=True, slots=True)
class GaugeRecord:
    """The part of a record of --record that the gauge judges: the median of each step of the edit runs."""

    medians: Mapping[str, Median]


def judge(
    medians: Mapping[str, Median], budgets: Mapping[str, check_report.Budget], place: str
) -> tuple[list[check_report.Finding], list[str]]:
    """Compare the median of the instruction counts of the edit builds with its measured rule (THE MEASURED RULE).

    Args:
        medians: The median of the build and of the tests
        budgets: The measured rules of the registry
        place: The path that a finding names

    Returns:
        The findings, and a note when the build step has no exact instruction count
    """
    count = medians["build"].instructions_g
    if count is None:
        return [], [f"the build step has no exact instruction count, so the gauge does not judge {BUILD_RULE}"]
    budget = budgets[BUILD_RULE]
    level = check_report.classify(count, budget)
    if level is None:
        return [], []
    message = f"the median of the runs is {count:g} G instructions, above the limit of {budget.limit:g} G instructions"
    return [check_report.judged(level, place, 0, BUILD_RULE, message)], []


# ── The record ─────────────────────────────────────────────────────────────


def read_records(path: Path) -> list[GaugeRecord]:
    """Return each record of a file that --record writes, one JSON object on each line (THE REPORT).

    Complexity: linear in the bytes of the file.

    Raises:
        GaugeError: If the file cannot be read, or a line that is not empty is not a record
    """
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as problem:
        raise GaugeError(f"the records {path} cannot be read: {problem}") from None
    records: list[GaugeRecord] = []
    for number, line in enumerate(text.splitlines(), start=1):
        if not line.strip():
            continue
        try:
            value = json.loads(line)
        except ValueError as problem:
            raise GaugeError(f"{path}:{number}: the line is not JSON: {problem}") from None
        records.append(parsed_record(value, f"{path}:{number}"))
    return records


def record_number(block: Mapping[str, object], key: str, where: str) -> float:
    """Return a number of a part of a record.

    Raises:
        GaugeError: If the part has no number of that key
    """
    value = loop_history.number(block, key)
    if value is None:
        raise GaugeError(f"{where} has no number {key}")
    return value


def record_median(item: object, where: str) -> Median:
    """Return the median of one step of a record, as measure() writes it with asdict().

    Raises:
        GaugeError: If the median does not have that shape
    """
    if not isinstance(item, dict):
        raise GaugeError(f"{where} is not a median")
    return Median(
        record_number(item, "wall_s", where),
        record_number(item, "cpu_s", where),
        loop_history.number(item, "instructions_g"),
        loop_history.number(item, "worst_others_pct"),
    )


def parsed_record(value: object, where: str) -> GaugeRecord:
    """Return the part of a JSON record that the gauge judges, from a record that measure() writes.

    Raises:
        GaugeError: If the value does not have the shape that measure() writes
    """
    if not isinstance(value, dict):
        raise GaugeError(f"{where} is not a JSON object")
    medians = value.get("medians")
    if not isinstance(medians, dict):
        raise GaugeError(f"{where} has no object medians")
    missing = sorted(set(EDIT_STEPS) - set(medians))
    if missing:
        raise GaugeError(f"{where} has no median of the step {', '.join(missing)}")
    return GaugeRecord({name: record_median(medians[name], f"the median {name} of {where}") for name in EDIT_STEPS})


def judged_record(
    record: GaugeRecord, budgets: Mapping[str, check_report.Budget], position: str
) -> tuple[list[check_report.Finding], list[str]]:
    """Judge one record of the gauge with the rule edit-build-instructions (judge(), THE MEASURED RULE).

    measure() judges its own record with this function, and the witness of the rule judges a planted record with it
    (assay.guard.engines.buildsys.held).

    Args:
        record: The record
        budgets: The measured rules of the registry
        position: The path that a finding names

    Returns:
        The findings, and a note when the gauge did not judge the rule
    """
    return judge(record.medians, budgets, position)
