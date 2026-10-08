"""The reader of the cost record of a build step: OUTPUT.cost, which `loop step` writes adjacent to each output.

THE RECORD
    assay.core.meter writes each record (THE RECORD of that module).  A
    record is a JSON object with the keys format, step, result, source,
    output, output_bytes, exit and cost, and last_cost for a ccache hit.
    This module is the one reader of the record of a compile or a link.  The
    record of a test has its own reader, assay.readers.ctest.  The census of
    assay.readers.census reads the record of a negative fixture.

THE COST BLOCK
    A cost block holds cpu_s, user_s, system_s, wall_s, peak_rss_kb, load and
    end.  It also holds the accurate instruction counts of a compile:
    compiler_instructions for the compiler run, and ccache_instructions for
    the remaining part of a ccache miss.  A block of an earlier format holds
    the key instructions, the count of the full step, and the reader takes no
    count from it.  A record of a ccache hit holds no cost block.  Its
    last_cost gives the block of the last compile that ran the compiler, when the earlier
    record had one.

THE ERRORS
    read() raises MissingRecord when the output has no record.  It raises
    RecordError when the record cannot be read, or when it does not have the
    shape of a record of a build step.  A cost block that does not have the
    shape of a block also raises RecordError.

`guard selftest tests.core.test_record` does a test of the module.
"""

import enum
import json
from collections.abc import Mapping
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import meter as cost_meter

# The keys of the accurate instruction counts of a cost block (THE COST BLOCK).
COUNT_KEYS = (cost_meter.COMPILER_COUNT_KEY, cost_meter.CCACHE_COUNT_KEY)
RESULTS = frozenset({"built", "failed", "rejected", "hit"})


class Step(enum.StrEnum):
    """The step of a record of the build launcher."""

    COMPILE = "compile"
    LINK = "link"


class RecordError(ValueError):
    """A record cannot be read, or it does not have the shape of a record of a build step (THE ERRORS)."""


class MissingRecord(RecordError):
    """The output has no record.  The output did not come through the build launcher."""


@dataclass(frozen=True, slots=True)
class Cost:
    """One cost block (THE COST BLOCK): the times in seconds, the peak memory, the load, the end and the counts."""

    cpu_s: float
    user_s: float
    system_s: float
    wall_s: float
    peak_rss_kb: int
    load: float
    end: float
    instructions: Mapping[str, int] = field(default_factory=dict)

    @property
    def compiler_instructions(self) -> int | None:
        """The accurate user instructions of the compiler run of a compile, or None when the block holds no count."""
        return self.instructions.get(cost_meter.COMPILER_COUNT_KEY)


@dataclass(frozen=True, slots=True)
class CostRecord:
    """One record of a compile or a link (THE RECORD)."""

    step: Step
    result: str
    source: str | None
    output: str | None
    output_bytes: int | None
    exit: int | None
    cost: Cost | None
    last_cost: Cost | None

    @property
    def is_hit(self) -> bool:
        """True when ccache gave the output.  The record then holds no time of its own."""
        return self.result == "hit"

    @property
    def real_cost(self) -> Cost | None:
        """The cost of the last compile that ran the compiler: the cost block, or the last cost of a hit."""
        return self.last_cost if self.is_hit else self.cost


def record_file(output: Path) -> Path:
    """Return the record of an output: OUTPUT.cost in the directory of the output."""
    return Path(f"{output}{cost_meter.RECORD_SUFFIX}")


def _number(block: Mapping[str, object], key: str, default: float | None, path: Path) -> float:
    """Return one number of a cost block.

    Raises:
        RecordError: If the block has no number of that key and the key has no default
    """
    value = block.get(key, default)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise RecordError(f"the record {path.name} has a cost block with no number {key}")
    return float(value)


def _cost(block: object, path: Path) -> Cost | None:
    """Return the cost block of a record, or None when the record has no block.

    Raises:
        RecordError: If the block does not have the shape of a cost block
    """
    if block is None:
        return None
    if not isinstance(block, dict):
        raise RecordError(f"the record {path.name} has a cost block that is not a JSON object")
    counts = {
        key: value
        for key in COUNT_KEYS
        if isinstance(value := block.get(key), int) and not isinstance(value, bool) and value >= 0
    }
    return Cost(
        _number(block, "cpu_s", None, path),
        _number(block, "user_s", 0.0, path),
        _number(block, "system_s", 0.0, path),
        _number(block, "wall_s", None, path),
        int(_number(block, "peak_rss_kb", None, path)),
        _number(block, "load", -1.0, path),
        _number(block, "end", 0.0, path),
        counts,
    )


def read(path: Path) -> CostRecord:
    """Read one record of a compile or a link (THE ERRORS).

    Raises:
        MissingRecord: If the output has no record
        RecordError: If the file cannot be read or does not have the shape of a record of a build step
    """
    try:
        record = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise MissingRecord(f"the output has no record {path.name}") from None
    except (OSError, ValueError) as problem:
        raise RecordError(f"the record {path.name} cannot be read ({problem})") from None
    if not isinstance(record, dict) or record.get("format") != cost_meter.RECORD_FORMAT:
        raise RecordError(f"the record {path.name} is not a record of format {cost_meter.RECORD_FORMAT}")
    step = next((member for member in Step if member.value == record.get("step")), None)
    if step is None:
        raise RecordError(f"the record {path.name} is not a record of a compile or a link")
    result = record.get("result")
    if result not in RESULTS:
        raise RecordError(f"the record {path.name} gives the result {result!r}, which `loop step` does not write")
    size, exit_code = record.get("output_bytes"), record.get("exit")
    source, output = record.get("source"), record.get("output")
    return CostRecord(
        step,
        str(result),
        source if isinstance(source, str) else None,
        output if isinstance(output, str) else None,
        size if isinstance(size, int) and not isinstance(size, bool) else None,
        exit_code if isinstance(exit_code, int) and not isinstance(exit_code, bool) else None,
        _cost(record.get("cost"), path),
        _cost(record.get("last_cost"), path),
    )


def read_of(output: Path) -> CostRecord:
    """Read the record of an output (record_file()).

    Raises:
        MissingRecord: If the output has no record
        RecordError: If the record cannot be read or does not have the shape of a record of a build step
    """
    return read(record_file(output))
