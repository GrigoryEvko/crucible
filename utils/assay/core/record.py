"""record — the one reader and writer of the cost records of the build steps, the tests and the fixtures.

THE RECORDS
    A cost record holds the result and the cost of the last run of one step:
      compile, link  OUTPUT.cost in the directory of the output.  `loop step`
                     writes it after each compile and each link
      test           BUILD_DIR/test-records/NAME.test.cost.  `loop test-step`
                     writes it after each test, and `loop fixture` writes it
                     after a run of a fixture that no launcher runs
      fixture        BUILD_DIR/neg-compile/NAME/NAME.inputs.  `loop fixture`
                     writes it after each run of a fixture: the cost of its
                     compile and each file that the compile read.  It also
                     holds the findings of the quarantine plugin in the source
    write() writes each record.  read(), read_test() and read_fixture() read
    it.  No other module reads or writes a record (the job cost-record of
    utils/registry.txt).

THE FORMAT
    A record of a build step or of a test is a JSON object with one key on
    each line:
      format        1
      step          "compile", "link" or "test"
      result        a compile or a link: "built", "failed", "rejected" (the
                    launcher refused the output after the step) or "hit"
                    (ccache gave the output).  A test: "passed", "failed" or
                    "rejected" (the judge refused a test with the status 0)
      source        the source of a compile
      output        the absolute path of the output of a compile or a link
      output_bytes  the number of bytes of the output after the step, when
                    the output is there
      test          the name of a test
      command       the program of a test
      exit          the status of the command, or minus the signal number
      cost          the cost block.  A hit has no cost block
      last_cost     for "hit": the cost block of the compile that made the
                    object, when the cost store holds it
                    (assay.core.cost_store).  The compile ran the compiler,
                    in this build directory or in a different one
      version       for a test: the version of the test that ran the runs of
                    the key walls, in hexadecimal digits (assay.loop.teststep,
                    THE VERSION).  A record with no such key has no version
      walls         for a test: the wall times in seconds of its last runs
                    that did the measured work, the newest first.  The judge
                    of test-time reads their smallest value (assay.loop.
                    teststep, THE MEASURED RULES).  A record with no such key
                    has the wall time of its cost block as its one value
    The first line of a record of a fixture holds a JSON object.  Each line
    after it holds the path of `realpath()` of one file that the compile read.
    The record names each file one time.  A header that the compile opened
    through a link of layer-roots/ has the path of the file of the tree.  The
    object holds format (1), fixture (the name of the fixture) and result
    ("compiled", or "stored" for a result of the store of the driver).  It
    also holds user_s and system_s (the CPU times of the compile), and
    instructions (the user instructions of the compile, or null).  The key
    has_inputs tells if the driver knows the files of the compile.  The key
    census_stamp holds the stamp argument of the quarantine plugin of the
    compile, or null when the driver knows no census.  The last key, census,
    holds one list [KIND, LINE, COLUMN, ENTITY] for each finding of the plugin
    in the source of the fixture, or null with no census.  A record with no
    census key has no census.

THE COST BLOCK
    A cost block holds cpu_s, user_s, system_s, wall_s, peak_rss_kb, load
    (the one-minute load average at the start) and end (seconds since the
    epoch).  A compile also holds compiler_instructions, the user
    instructions of the compiler run, when the host gives the count
    (assay.core.meter, A COMPILE THROUGH CCACHE).  A ccache miss also holds
    ccache_instructions, the instructions of the remaining part of the step:
    the preprocessor run, ccache and the meter.  A test holds
    test_instructions, the user instructions of the test and of each process
    that it starts.  The key is there only when the host gives an accurate
    count (assay.loop.teststep, THE MEASURED RULES).  A block of an earlier
    format holds the key
    instructions, the count of the full step, and the reader takes no count
    from it.

THE WRITE
    The step meters write the records, and each import of this module is a
    cost of each step (assay.core.value, THE COST).  This module imports only
    assay.core.value.
    write() makes the text of
    a record with no JSON module, and quote() escapes each character that is
    not printable ASCII.  write() writes a scratch file and renames it.  A
    reader then sees the full record or the earlier record.  The write makes
    the directory of the record only when it finds no directory.

THE READ
    A reader imports json.  read() raises MissingRecord when the output has
    no record.  Each reader raises RecordError when the record cannot be read,
    or when it does not have the shape of a record of its step.  A step meter
    reads with no JSON module.  `loop test-step` reads the version and the
    wall times of the earlier record of a test with recent_window(), from the
    text of the keys version and walls.  `loop step` reads a cost block of
    the cost store with cost_from_text(), which accepts only the text that
    cost_text() writes.

`guard selftest tests.core.test_record` does a test of the module.
"""

import os

from assay.core.value import Value

# The names are only for the types, and a step meter does not pay their imports.
TYPE_CHECKING = False
if TYPE_CHECKING:
    from collections.abc import Mapping, Sequence
    from pathlib import Path
    from typing import Final, Literal

    from assay.core.meter import Measurement

FORMAT = 1
# The suffix of the record of a build step, and the directory and the suffix of the record of a test (THE RECORDS).
SUFFIX = ".cost"
TEST_DIRECTORY = "test-records"
TEST_SUFFIX = ".test" + SUFFIX
# The directory of the scratch directories of the fixtures in a build directory, and the suffix of a fixture record.
FIXTURE_DIRECTORY = "neg-compile"
FIXTURE_SUFFIX = ".inputs"
# The keys of the instruction counts of a cost block (THE COST BLOCK).
COMPILER_COUNT_KEY = "compiler_instructions"
CCACHE_COUNT_KEY = "ccache_instructions"
TEST_COUNT_KEY = "test_instructions"
COUNT_KEYS = (COMPILER_COUNT_KEY, CCACHE_COUNT_KEY, TEST_COUNT_KEY)
# The keys of the times of a cost block, with the load and the end, in the sequence of cost_text() (THE READ).
TIME_KEYS = ("cpu_s", "user_s", "system_s", "wall_s", "load", "end")
PEAK_KEY = "peak_rss_kb"
# The keys of the version and of the recent wall times of a test, and their starts of a line in a record (THE READ).
VERSION_FIELD = "version"
VERSION_KEY = f'"{VERSION_FIELD}": '
WALLS_FIELD = "walls"
WALLS_KEY = f'"{WALLS_FIELD}": '
# The characters of a version.
VERSION_DIGITS = frozenset("0123456789abcdef")

type Step = Literal["compile", "link", "test", "fixture"]
# One finding of the quarantine plugin in the source of a fixture: its kind, its line, its column and its entity.
type CensusFinding = tuple[str, int, int, str]
COMPILE: Final = "compile"
LINK: Final = "link"
TEST: Final = "test"
FIXTURE: Final = "fixture"
BUILD_STEPS: Final = frozenset({COMPILE, LINK})
HIT: Final = "hit"
# The results of each step (THE FORMAT).
BUILD_RESULTS: Final = frozenset({"built", "failed", "rejected", HIT})
TEST_RESULTS: Final = frozenset({"passed", "failed", "rejected"})
FIXTURE_RESULTS: Final = frozenset({"compiled", "stored"})


class RecordError(ValueError):
    """A record cannot be read, or it does not have the shape of a record of its step (THE READ)."""


class MissingRecord(RecordError):
    """The output has no record.  The output did not come through the build launcher."""


class Cost(Value):
    """One cost block (THE COST BLOCK): the times in seconds, the peak memory, the load, the end and the counts."""

    __match_args__ = ("cpu_s", "user_s", "system_s", "wall_s", "peak_rss_kb", "load", "end", "counts")
    cpu_s: float
    user_s: float
    system_s: float
    wall_s: float
    peak_rss_kb: int
    load: float
    end: float
    counts: tuple[tuple[str, int], ...]

    def __init__(
        self,
        cpu_s: float,
        user_s: float,
        system_s: float,
        wall_s: float,
        peak_rss_kb: int,
        load: float,
        end: float,
        counts: Sequence[tuple[str, int]] = (),
    ) -> None:
        """Hold the values of one block.  `counts` holds the key and the value of each instruction count."""
        super().__init__(cpu_s, user_s, system_s, wall_s, peak_rss_kb, load, end, tuple(counts))

    def count(self, key: str) -> int | None:
        """Return the instruction count of a key of COUNT_KEYS, or None when the block holds no such count."""
        return next((value for name, value in self.counts if name == key), None)

    @property
    def compiler_instructions(self) -> int | None:
        """The user instructions of the compiler run of a compile, or None when the block holds no count."""
        return self.count(COMPILER_COUNT_KEY)

    @property
    def test_instructions(self) -> int | None:
        """The user instructions of a test and of each process that it starts, or None when the block holds no count."""
        return self.count(TEST_COUNT_KEY)


class CostRecord(Value):
    """One record of a compile or a link (THE FORMAT)."""

    __match_args__ = ("step", "result", "source", "output", "output_bytes", "exit", "cost", "last_cost")
    step: Step
    result: str
    source: str | None
    output: str | None
    output_bytes: int | None
    exit: int | None
    cost: Cost | None
    last_cost: Cost | None

    def __init__(
        self,
        step: Step,
        result: str,
        source: str | None,
        output: str | None,
        output_bytes: int | None,
        exit: int | None,
        cost: Cost | None,
        last_cost: Cost | None = None,
    ) -> None:
        """Hold the fields of one record of a compile or a link."""
        super().__init__(step, result, source, output, output_bytes, exit, cost, last_cost)

    @property
    def is_hit(self) -> bool:
        """True when ccache gave the output.  The record then holds no time of its own."""
        return self.result == HIT

    @property
    def real_cost(self) -> Cost | None:
        """The cost of the compile that made the output: the cost block, or for a hit the last cost of the cost store."""
        return self.last_cost if self.is_hit else self.cost


class TestRecord(Value):
    """One record of the last run of a test (THE FORMAT).

    `walls` holds the wall times of the last runs that did the measured work, the newest first.  With no walls, the
    record holds the wall time of its cost block as its one value.  `version` is the version of the test that ran
    these runs, or the empty text when the record has no version.
    """

    __match_args__ = ("name", "result", "program", "exit", "cost", "walls", "version")
    name: str
    result: str
    program: str
    exit: int
    cost: Cost
    walls: tuple[float, ...]
    version: str

    def __init__(
        self,
        name: str,
        result: str,
        program: str,
        exit: int,
        cost: Cost,
        walls: Sequence[float] | None = None,
        version: str = "",
    ) -> None:
        """Hold the name, the result, the program, the status, the cost, the recent wall times and the version.

        Raises:
            ValueError: If the version holds a character that is not a lowercase hexadecimal digit
        """
        if not set(version) <= VERSION_DIGITS:
            raise ValueError(
                f"the version {version!r} of the test {name} is not a text of lowercase hexadecimal digits"
            )
        super().__init__(name, result, program, exit, cost, (cost.wall_s,) if walls is None else tuple(walls), version)

    @property
    def least_wall_s(self) -> float:
        """The smallest wall time of `walls`, or the wall time of the cost block when `walls` is empty."""
        return min(self.walls, default=self.cost.wall_s)


class FixtureRecord(Value):
    """One record of the last run of a negative fixture: the cost of its compile, its inputs and its census (THE FORMAT).

    `inputs` holds each file that the compile read, by the path of
    `realpath()`, or it is None when the driver does not know the files.  `census_stamp` is the
    stamp argument of the quarantine plugin of the compile, and `census`
    holds the kind, the line, the column and the entity of each finding of
    the plugin in the source of the fixture.  The two are None when the
    driver knows no census.
    """

    __match_args__ = ("name", "result", "user_s", "system_s", "instructions", "inputs", "census_stamp", "census")
    name: str
    result: str
    user_s: float
    system_s: float
    instructions: int | None
    inputs: tuple[str, ...] | None
    census_stamp: str | None
    census: tuple[CensusFinding, ...] | None

    def __init__(
        self,
        name: str,
        result: str,
        user_s: float,
        system_s: float,
        instructions: int | None,
        inputs: tuple[str, ...] | None,
        census_stamp: str | None = None,
        census: Sequence[CensusFinding] | None = None,
    ) -> None:
        """Hold the name, the result, the CPU times, the instruction count, the inputs and the census of one run.

        The record keeps the census as a tuple, and a value does not change after its constructor.
        """
        held = tuple(census) if census is not None else None
        super().__init__(name, result, user_s, system_s, instructions, inputs, census_stamp, held)

    @property
    def cpu_s(self) -> float:
        """The sum of the user and the system CPU time of the compile.  The kernel measures the sum accurately."""
        return self.user_s + self.system_s


# ── The files of the records ───────────────────────────────────────


def record_file(output: str | os.PathLike[str]) -> Path:
    """Return the record of the output of a build step: OUTPUT.cost in the directory of the output."""
    from pathlib import Path

    return Path(f"{os.fspath(output)}{SUFFIX}")


def test_record_file(build_dir: str | os.PathLike[str], name: str) -> str:
    """Return the record of one test of a build directory: BUILD_DIR/test-records/NAME.test.cost."""
    return os.path.join(build_dir, TEST_DIRECTORY, name + TEST_SUFFIX)


def fixture_record_file(build_dir: str | os.PathLike[str], name: str) -> str:
    """Return the record of one fixture of a build directory: BUILD_DIR/neg-compile/NAME/NAME.inputs."""
    return os.path.join(build_dir, FIXTURE_DIRECTORY, name, name + FIXTURE_SUFFIX)


# ── The write ──────────────────────────────────────────────────────


def measured(run: Measurement, counts: Sequence[tuple[str, int]] = ()) -> Cost:
    """Return the cost block of one measurement of assay.core.meter, with the instruction counts `counts`."""
    return Cost(run.cpu_s, run.user_s, run.system_s, run.wall_s, run.peak_rss_kb, run.load, run.end, counts)


def quote(text: str) -> str:
    """Return text as a JSON string, and escape each character that is not printable ASCII.

    Args:
        text: The text, which can hold surrogate escapes of a file name

    Returns:
        The JSON string, with its two quotation marks
    """
    parts = ['"']
    for char in text:
        code = ord(char)
        if char in '"\\':
            parts.append("\\" + char)
        elif 0x20 <= code < 0x7F:
            parts.append(char)
        elif code > 0xFFFF:
            code -= 0x10000
            parts.append(f"\\u{0xD800 + (code >> 10):04x}\\u{0xDC00 + (code & 0x3FF):04x}")
        else:
            parts.append(f"\\u{code:04x}")
    parts.append('"')
    return "".join(parts)


def cost_text(cost: Cost) -> str:
    """Return a cost block as JSON text, with the times, the peak memory and each count (THE COST BLOCK)."""
    extra = "".join(f", {quote(key)}: {value}" for key, value in cost.counts)
    return (
        f'{{"cpu_s": {cost.cpu_s:.3f}, "user_s": {cost.user_s:.3f}, "system_s": {cost.system_s:.3f}, '
        f'"wall_s": {cost.wall_s:.3f}, "peak_rss_kb": {cost.peak_rss_kb}, "load": {cost.load:.2f}, '
        f'"end": {cost.end:.3f}{extra}}}'
    )


def cost_from_text(text: str) -> Cost | None:
    """Return the cost block of a text that cost_text() wrote, or None for each other text (THE READ).

    The function reads the text with no JSON module.  It refuses a key that a
    cost block does not hold, a missing time, a time that is not finite, and a
    negative count.  Complexity: linear in the length of the text.
    """
    body = text.strip()
    if not (body.startswith("{") and body.endswith("}")):
        return None
    values: dict[str, str] = {}
    for part in body[1:-1].split(", "):
        quoted, separator, value = part.partition(": ")
        if not separator or len(quoted) < 2 or quoted[0] != '"' or quoted[-1] != '"':
            return None
        values[quoted[1:-1]] = value
    if not set(values) <= {*TIME_KEYS, PEAK_KEY, *COUNT_KEYS}:
        return None
    try:
        times = [float(values[key]) for key in TIME_KEYS]
        peak = int(values[PEAK_KEY])
        counts = tuple((key, int(values[key])) for key in COUNT_KEYS if key in values)
    except KeyError, ValueError:
        return None
    if not all(abs(value) < float("inf") for value in times) or any(count < 0 for _, count in counts):
        return None
    return Cost(times[0], times[1], times[2], times[3], peak, times[4], times[5], counts)


def recent_window(path: str) -> tuple[str, tuple[float, ...]]:
    """Return the version and the wall times of the earlier record of a test at a path, the newest first (THE READ).

    The function reads the text, with no JSON module, because each test pays for its imports.  The version and the
    list are each on one line, after VERSION_KEY and WALLS_KEY.  A record that is not there or cannot be read gives
    no version and no wall time.  A version or a list of a different shape gives no version or no wall time.
    Complexity: linear in the length of the record.
    """
    version_line = walls_line = ""
    try:
        with open(path, encoding="utf-8") as handle:
            for line in handle:
                if line.startswith(VERSION_KEY):
                    version_line = line[len(VERSION_KEY) :].rstrip().rstrip(",")
                elif line.startswith(WALLS_KEY):
                    walls_line = line[len(WALLS_KEY) :].rstrip().rstrip(",")
    except OSError, UnicodeDecodeError:
        return "", ()
    version = version_line[1:-1] if len(version_line) >= 2 and version_line[0] == version_line[-1] == '"' else ""
    return (version if set(version) <= VERSION_DIGITS else ""), _listed_walls(walls_line)


def _listed_walls(listed: str) -> tuple[float, ...]:
    """Return the wall times of the text of a list of the key walls, or no wall time for a list of a different shape."""
    if not (listed.startswith("[") and listed.endswith("]")):
        return ()
    words = [word.strip() for word in listed[1:-1].split(",") if word.strip()]
    try:
        walls = tuple(float(word) for word in words)
    except ValueError:
        return ()
    return walls if all(wall >= 0.0 for wall in walls) else ()


def object_text(fields: Sequence[tuple[str, str]]) -> str:
    """Return a JSON object with one key on each line, from each key and its value as JSON text."""
    return "{\n" + ",\n".join(f"{quote(key)}: {value}" for key, value in fields) + "\n}\n"


def build_text(record: CostRecord) -> str:
    """Return the text of the record of a compile or a link (THE FORMAT)."""
    fields = [("format", str(FORMAT)), ("step", quote(record.step)), ("result", quote(record.result))]
    if record.source is not None:
        fields.append(("source", quote(record.source)))
    if record.output is not None:
        fields.append(("output", quote(record.output)))
    if record.output_bytes is not None:
        fields.append(("output_bytes", str(record.output_bytes)))
    if record.exit is not None:
        fields.append(("exit", str(record.exit)))
    if record.cost is not None:
        fields.append(("cost", cost_text(record.cost)))
    if record.last_cost is not None:
        fields.append(("last_cost", cost_text(record.last_cost)))
    return object_text(fields)


def test_text(record: TestRecord) -> str:
    """Return the text of the record of a test (THE FORMAT)."""
    return object_text(
        [
            ("format", str(FORMAT)),
            ("step", quote(TEST)),
            ("result", quote(record.result)),
            ("test", quote(record.name)),
            ("command", quote(record.program)),
            ("exit", str(record.exit)),
            ("cost", cost_text(record.cost)),
            (VERSION_FIELD, quote(record.version)),
            (WALLS_FIELD, "[" + ", ".join(f"{wall:.3f}" for wall in record.walls) + "]"),
        ]
    )


def fixture_text(record: FixtureRecord) -> str:
    """Return the text of the record of a fixture (THE FORMAT).

    A path with a line break, which no file of the tree has, gives a record with no inputs.
    """
    inputs = record.inputs
    if inputs is not None and any("\n" in path for path in inputs):
        inputs = None
    instructions = "null" if record.instructions is None else str(record.instructions)
    stamp = "null" if record.census_stamp is None else quote(record.census_stamp)
    census = "null"
    if record.census is not None:
        census = (
            "["
            + ", ".join(
                f"[{quote(kind)}, {line}, {column}, {quote(entity)}]" for kind, line, column, entity in record.census
            )
            + "]"
        )
    header = (
        f'{{"format": {FORMAT}, "fixture": {quote(record.name)}, "result": {quote(record.result)}, '
        f'"user_s": {record.user_s:.3f}, "system_s": {record.system_s:.3f}, "instructions": {instructions}, '
        f'"has_inputs": {"true" if inputs is not None else "false"}, "census_stamp": {stamp}, "census": {census}}}'
    )
    return header + "\n" + "".join(f"{path}\n" for path in inputs or ())


def write(path: str | os.PathLike[str], record: CostRecord | TestRecord | FixtureRecord) -> None:
    """Write one record at a path, through a scratch file and a rename (THE WRITE).

    The path of a record is record_file(), test_record_file() or fixture_record_file() of its step.

    Raises:
        OSError: If the record cannot be written
    """
    target = os.fspath(path)
    if isinstance(record, CostRecord):
        text = build_text(record)
    elif isinstance(record, TestRecord):
        text = test_text(record)
    else:
        text = fixture_text(record)
    data = text.encode("utf-8", "surrogateescape")
    staging = f"{target}.{os.getpid()}.tmp"
    flags = os.O_WRONLY | os.O_CREAT | os.O_TRUNC
    try:
        descriptor = os.open(staging, flags, 0o666)
    except FileNotFoundError:
        os.makedirs(os.path.dirname(target), exist_ok=True)
        descriptor = os.open(staging, flags, 0o666)
    try:
        try:
            view = memoryview(data)
            while view:
                view = view[os.write(descriptor, view) :]
        finally:
            os.close(descriptor)
        os.replace(staging, target)
    except BaseException:
        try:
            os.unlink(staging)
        except OSError:
            pass
        raise


# ── The read ───────────────────────────────────────────────────────


def _loaded(path: Path) -> dict[str, object]:
    """Return the JSON object of a record of a build step or of a test.

    Raises:
        MissingRecord: If the file is not there
        RecordError: If the file cannot be read, or it is not a JSON object of format FORMAT
    """
    import json

    try:
        found = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        raise MissingRecord(f"the output has no record {path.name}") from None
    except (OSError, ValueError) as problem:
        raise RecordError(f"the record {path.name} cannot be read ({problem})") from None
    if not isinstance(found, dict) or found.get("format") != FORMAT:
        raise RecordError(f"the record {path.name} is not a record of format {FORMAT}")
    return found


def _number(block: Mapping[str, object], key: str, default: float | None, path: Path) -> float:
    """Return one number of a cost block or of a fixture record.

    Raises:
        RecordError: If the block has no number of that key and the key has no default
    """
    value = block.get(key, default)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise RecordError(f"the record {path.name} has no number {key}")
    return float(value)


def _count(value: object) -> int | None:
    """Return a value as an instruction count when it is an integer of 0 or more, else None."""
    return value if isinstance(value, int) and not isinstance(value, bool) and value >= 0 else None


def _integer(value: object) -> int | None:
    """Return a value when it is an integer and not a boolean, else None."""
    return value if isinstance(value, int) and not isinstance(value, bool) else None


def _cost(block: object, path: Path) -> Cost | None:
    """Return the cost block of a record, or None when the record has no block.

    Raises:
        RecordError: If the block does not have the shape of a cost block
    """
    if block is None:
        return None
    if not isinstance(block, dict):
        raise RecordError(f"the record {path.name} has a cost block that is not a JSON object")
    counts = tuple((key, value) for key in COUNT_KEYS if (value := _count(block.get(key))) is not None)
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
    """Read one record of a compile or a link (THE READ).

    Raises:
        MissingRecord: If the output has no record
        RecordError: If the file cannot be read or does not have the shape of a record of a build step
    """
    found = _loaded(path)
    step = found.get("step")
    if step not in BUILD_STEPS:
        raise RecordError(f"the record {path.name} is not a record of a compile or a link")
    result = found.get("result")
    if result not in BUILD_RESULTS:
        raise RecordError(f"the record {path.name} gives the result {result!r}, which `loop step` does not write")
    source, output = found.get("source"), found.get("output")
    return CostRecord(
        COMPILE if step == COMPILE else LINK,
        str(result),
        source if isinstance(source, str) else None,
        output if isinstance(output, str) else None,
        _integer(found.get("output_bytes")),
        _integer(found.get("exit")),
        _cost(found.get("cost"), path),
        _cost(found.get("last_cost"), path),
    )


def read_of(output: Path) -> CostRecord:
    """Read the record of the output of a build step (record_file()).

    Raises:
        MissingRecord: If the output has no record
        RecordError: If the record cannot be read or does not have the shape of a record of a build step
    """
    return read(record_file(output))


def read_test(path: Path) -> TestRecord:
    """Read one record of a test (THE READ).

    Raises:
        MissingRecord: If the file is not there
        RecordError: If the record cannot be read or is not a record of a test.  Also if its file has the name of a
            different test
    """
    found = _loaded(path)
    cost = _cost(found.get("cost"), path) if found.get("step") == TEST else None
    name, result, program = (found.get(key) for key in ("test", "result", "command"))
    exit_code = _integer(found.get("exit"))
    if (
        cost is None
        or not isinstance(name, str)
        or not isinstance(result, str)
        or result not in TEST_RESULTS
        or not isinstance(program, str)
        or exit_code is None
    ):
        raise RecordError(f"the record {path.name} is not a record of a test with its result, command, exit and cost")
    if path.name != name + TEST_SUFFIX:
        raise RecordError(f"the record {path} is the record of the test {name}, and its file has another name")
    version = found.get(VERSION_FIELD, "")
    if not isinstance(version, str) or not set(version) <= VERSION_DIGITS:
        raise RecordError(f"the record {path.name} has a key {VERSION_FIELD} that is not a text of hexadecimal digits")
    return TestRecord(name, result, program, exit_code, cost, _walls(found.get(WALLS_FIELD), path), version)


def _walls(value: object, path: Path) -> tuple[float, ...] | None:
    """Return the wall times of the key walls of a test record, or None when the record has no such key.

    Raises:
        RecordError: If the value is not a list of numbers of 0 or more
    """
    if value is None:
        return None
    refusal = RecordError(f"the record {path.name} has a key {WALLS_FIELD} that is not a list of times of 0 or more")
    if not isinstance(value, list):
        raise refusal
    walls: list[float] = []
    for wall in value:
        if not isinstance(wall, (int, float)) or isinstance(wall, bool) or wall < 0:
            raise refusal
        walls.append(float(wall))
    return tuple(walls)


def read_fixture(path: Path) -> FixtureRecord:
    """Read one record of a fixture (THE READ).

    Raises:
        MissingRecord: If the file is not there
        RecordError: If the record cannot be read, or its first line is not the object of a fixture record of the
            name of its file
    """
    import json

    try:
        text = path.read_text(encoding="utf-8", errors="surrogateescape")
    except FileNotFoundError:
        raise MissingRecord(f"the fixture has no record {path}") from None
    except OSError as problem:
        raise RecordError(f"the record {path} cannot be read ({problem})") from None
    first, _, body = text.partition("\n")
    try:
        header = json.loads(first)
    except ValueError:
        header = None
    name = path.name.removesuffix(FIXTURE_SUFFIX)
    if not isinstance(header, dict) or header.get("format") != FORMAT or header.get("fixture") != name:
        raise RecordError(f"the record {path} is not a record of format {FORMAT} of the fixture {name}")
    result = header.get("result")
    if not isinstance(result, str) or result not in FIXTURE_RESULTS or not isinstance(header.get("has_inputs"), bool):
        raise RecordError(f"the record {path} gives no result {' or '.join(sorted(FIXTURE_RESULTS))} and no has_inputs")
    instructions = header.get("instructions")
    if instructions is not None and _count(instructions) is None:
        raise RecordError(f"the record {path} gives an instruction count that is not an integer of 0 or more")
    inputs = tuple(body.splitlines()) if header["has_inputs"] else None
    stamp, census = _census(header, path)
    return FixtureRecord(
        name,
        result,
        _number(header, "user_s", None, path),
        _number(header, "system_s", None, path),
        _count(instructions),
        inputs,
        stamp,
        census,
    )


def _census(header: Mapping[str, object], path: Path) -> tuple[str | None, tuple[CensusFinding, ...] | None]:
    """Return the census stamp and the census of the object of a fixture record, each None when it gives none.

    Raises:
        RecordError: If the stamp is not a text, or a finding is not a list of a kind, a line, a column and an entity
    """
    stamp, census = header.get("census_stamp"), header.get("census")
    if stamp is None or census is None:
        return None, None
    if not isinstance(stamp, str) or not isinstance(census, list):
        raise RecordError(f"the record {path} gives a census stamp that is not a text or a census that is not a list")
    findings: list[CensusFinding] = []
    for item in census:
        is_finding = isinstance(item, list) and len(item) == 4
        kind, line, column, entity = item if is_finding else (None, None, None, None)
        line_number, column_number = _count(line), _count(column)
        if not isinstance(kind, str) or not isinstance(entity, str) or line_number is None or column_number is None:
            raise RecordError(f"the record {path} gives a census finding that is not [KIND, LINE, COLUMN, ENTITY]")
        findings.append((kind, line_number, column_number, entity))
    return stamp, tuple(findings)
