"""The reader of ctest: the tests that a build directory registers, the results of a run, and the record of each test.

THE INPUTS
    * The test list.  `ctest --show-only=json-v1` prints the registered tests
      of a configured build directory as JSON.  CMake has evaluated each
      function, loop, subdirectory and property call, so the command and the
      properties of each test are the values that ctest uses.  The reader runs
      the ctest that the CMake cache of the build directory names
      (CMAKE_CTEST_COMMAND, assay.readers.cmake), and no ctest of PATH.
    * The results of a run.  `ctest --output-junit FILE` writes one element
      testcase for each test that the run selected, with the name of the test
      and its status: run, fail, notrun or disabled.
    * The record of each test.  `loop test-step` and `loop fixture` write
      BUILD_DIR/test-records/NAME.test.cost after each run of a test
      (assay.core.meter, THE RECORD).  It holds the result, the status, the
      program and the cost of the last run.

THE CALL OF A TEST
    The backtrace of a test in the JSON gives the stack of CMake calls that
    registered it, from the add_test.  A test of the tree comes from add_test,
    or from crucible_script_test() of cmake/TestLauncher.cmake, which puts the
    test launcher in front of a script test.  The reader gives the outermost
    call of that run of registration calls at the top of the stack.  For a
    script test, that call is its call of crucible_script_test(), in the CMake
    file that registers the test.  The add_test in the function is not that
    call.

WHAT THE READER GIVES
    tests() gives each registered test as a TestEntry: its name, its command,
    its properties, and the CMake file and the line of its call.  With a
    label, it gives only the tests with that label.  parse() reads the JSON of
    a listing.  results() gives the status of each test of a JUnit report.
    costs() gives the last record of each test by its name, as a TestRecord.

    A file of the build directory that the reader cannot read gives a
    CtestError, which names the file and the command that writes it.

Complexity: linear in the size of the JSON, of the report and of the records.
"""

import enum
import json
from collections.abc import Mapping
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import paths, proc
from assay.readers import cmake

TIMEOUT_S = 300.0
# The calls that register a test, from the innermost frame of a backtrace (THE CALL OF A TEST).
REGISTRATION_COMMANDS = frozenset({"add_test", "crucible_script_test"})
LABELS = "LABELS"
# The command of ctest that writes each input, for the message of an error.
LISTING_COMMAND = "ctest --show-only=json-v1"
RECORD_COMMAND = "`loop test-step` or `loop fixture`"


class CtestError(RuntimeError):
    """An input of ctest cannot be read: the test list, a JUnit report or a record of a test."""


class TestStatus(enum.StrEnum):
    """The status of one test in a JUnit report of ctest."""

    RUN = "run"
    FAIL = "fail"
    NOTRUN = "notrun"
    DISABLED = "disabled"


@dataclass(frozen=True, slots=True)
class TestEntry:
    """One registered test: its name, its command, its properties, and the CMake file and line of its registration."""

    name: str
    command: tuple[str, ...]
    properties: Mapping[str, tuple[str, ...]] = field(default_factory=dict)
    file: str = ""
    line: int = 0

    def words_of(self, name: str) -> tuple[str, ...]:
        """Return the value of one property as words, empty when the test has no such property."""
        return self.properties.get(name, ())


@dataclass(frozen=True, slots=True)
class TestRecord:
    """The record of the last run of one test (assay.core.meter, THE RECORD)."""

    name: str
    result: str
    program: str
    exit_code: int
    cpu_s: float
    wall_s: float
    peak_rss_kb: int
    end: float


def _frames(record: Mapping[str, object], graph: Mapping[str, object]) -> list[tuple[str, int, str]]:
    """Return the file, the line and the command of each frame of the backtrace of one test, innermost first."""
    nodes, files, commands = graph.get("nodes", []), graph.get("files", []), graph.get("commands", [])
    if not (isinstance(nodes, list) and isinstance(files, list) and isinstance(commands, list)):
        return []
    frames: list[tuple[str, int, str]] = []
    index = record.get("backtrace")
    seen: set[int] = set()
    while isinstance(index, int) and 0 <= index < len(nodes) and index not in seen:
        seen.add(index)
        node = nodes[index]
        if not isinstance(node, dict):
            break
        file_index, line, command_index = node.get("file"), node.get("line"), node.get("command")
        if isinstance(file_index, int) and 0 <= file_index < len(files) and isinstance(line, int):
            command = (
                commands[command_index] if isinstance(command_index, int) and 0 <= command_index < len(commands) else ""
            )
            frames.append((str(files[file_index]), line, str(command)))
        index = node.get("parent")
    return frames


def call_of(record: Mapping[str, object], graph: Mapping[str, object]) -> tuple[str, int]:
    """Return the CMake file and the line of the call that registers one test (THE CALL OF A TEST), or "" and 0."""
    call = ("", 0)
    for file, line, command in _frames(record, graph):
        if command not in REGISTRATION_COMMANDS:
            break
        call = (file, line)
    return call


def properties_of(record: Mapping[str, object]) -> dict[str, tuple[str, ...]]:
    """Return each property of one test record as a tuple of words."""
    found: dict[str, tuple[str, ...]] = {}
    items = record.get("properties")
    for item in items if isinstance(items, list) else []:
        if isinstance(item, dict) and isinstance(item.get("name"), str):
            value = item.get("value", [])
            found[item["name"]] = tuple(str(word) for word in value) if isinstance(value, list) else (str(value),)
    return found


def parse(text: str) -> tuple[TestEntry, ...]:
    """Read the JSON of `ctest --show-only=json-v1`.

    Raises:
        CtestError: If the text is not JSON, or holds no array tests
    """
    try:
        listing = json.loads(text)
    except json.JSONDecodeError as problem:
        raise CtestError(f"ctest gave output that is not JSON: {problem}") from None
    records = listing.get("tests") if isinstance(listing, dict) else None
    if not isinstance(records, list):
        raise CtestError("the ctest JSON holds no array tests")
    graph = listing.get("backtraceGraph", {})
    graph = graph if isinstance(graph, dict) else {}
    tests: list[TestEntry] = []
    for record in records:
        if not isinstance(record, dict):
            continue
        command = record.get("command", []) or []
        file, line = call_of(record, graph)
        tests.append(
            TestEntry(
                str(record.get("name", "<unnamed>")),
                tuple(str(word) for word in command) if isinstance(command, list) else (),
                properties_of(record),
                file,
                line,
            )
        )
    return tuple(tests)


def _ctest_program(build_dir: Path) -> str:
    """Return the ctest of the CMake cache of a build directory.

    Raises:
        CtestError: If the cache cannot be read, or it names no ctest file
    """
    try:
        program = cmake.cache_value(build_dir, "CMAKE_CTEST_COMMAND")
    except cmake.CmakeError as problem:
        raise CtestError(
            f"the reader does not know the ctest of {build_dir}, because its cache cannot be read: {problem}"
        ) from None
    if not program:
        raise CtestError(f"the cache of {build_dir} has no entry CMAKE_CTEST_COMMAND.  {cmake.AGAIN}")
    if not Path(program).is_file():
        raise CtestError(f"the cache of {build_dir} names the ctest {program}, which does not exist.  {cmake.AGAIN}")
    return program


def tests(build_dir: Path, label: str | None = None) -> tuple[TestEntry, ...]:
    """Return each test that a build directory registers, or with a label each test that has the label.

    Raises:
        CtestError: If ctest cannot run or fails, or its output is not the JSON of the listing
    """
    command = proc.Command(
        [_ctest_program(build_dir), "--show-only=json-v1", "--test-dir", str(build_dir)], timeout_s=TIMEOUT_S
    )
    try:
        completed = proc.require(proc.run(command))
    except proc.CannotStart as problem:
        raise CtestError(f"the ctest of {build_dir} cannot start: {problem.error}") from None
    except proc.ToolFailed as problem:
        raise CtestError(f"`{LISTING_COMMAND}` failed for {build_dir}: {problem}") from None
    found = parse(completed.out)
    return found if label is None else tuple(test for test in found if label in test.words_of(LABELS))


def results(report: Path) -> dict[str, TestStatus]:
    """Return the status of each test of a JUnit report that `ctest --output-junit` wrote.

    A test case with no status and a child failure has the status fail.  A test case with no status and no child
    failure has the status run.

    Raises:
        CtestError: If the report cannot be read, is not XML, or names a status that ctest does not write
    """
    from xml.etree import ElementTree

    try:
        root = ElementTree.parse(report).getroot()
    except (OSError, ElementTree.ParseError) as problem:
        raise CtestError(
            f"the JUnit report {report} cannot be read ({problem}).  `ctest --output-junit FILE` writes it"
        ) from None
    statuses: dict[str, TestStatus] = {}
    for case in root.iter("testcase"):
        name = case.get("name")
        if name is None:
            continue
        status = case.get("status") or (TestStatus.FAIL if case.find("failure") is not None else TestStatus.RUN)
        try:
            statuses[name] = TestStatus(status)
        except ValueError:
            raise CtestError(
                f"the JUnit report {report} gives the test {name} the status {status!r}, which ctest does not write"
            ) from None
    return statuses


def _number(block: Mapping[str, object], key: str, path: Path) -> float:
    """Return one number of the cost block of a record.

    Raises:
        CtestError: If the block has no number of that key
    """
    value = block.get(key)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise CtestError(f"the record {path} has no number {key} in its cost.  {RECORD_COMMAND} writes it")
    return float(value)


def read_record(path: Path) -> TestRecord:
    """Read one record of a test.

    Raises:
        CtestError: If the record cannot be read, or it is not a record of a test of the format of assay.core.meter
    """
    try:
        record = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as problem:
        raise CtestError(f"the record {path} cannot be read ({problem}).  {RECORD_COMMAND} writes it") from None
    block = record.get("cost") if isinstance(record, dict) else None
    if (
        not isinstance(record, dict)
        or record.get("format") != cost_meter.RECORD_FORMAT
        or record.get("step") != "test"
        or not isinstance(block, dict)
    ):
        raise CtestError(
            f"the record {path} is not a record of a test of format {cost_meter.RECORD_FORMAT}.  "
            f"{RECORD_COMMAND} writes it"
        )
    name, result, program, exit_code = (record.get(key) for key in ("test", "result", "command", "exit"))
    if not (
        isinstance(name, str)
        and isinstance(result, str)
        and isinstance(program, str)
        and isinstance(exit_code, int)
        and not isinstance(exit_code, bool)
    ):
        raise CtestError(f"the record {path} has no test, result, command or exit.  {RECORD_COMMAND} writes it")
    if path.name != name + cost_meter.TEST_RECORD_SUFFIX:
        raise CtestError(f"the record {path} is the record of the test {name}, and its file has another name")
    return TestRecord(
        name,
        result,
        program,
        exit_code,
        _number(block, "cpu_s", path),
        _number(block, "wall_s", path),
        int(_number(block, "peak_rss_kb", path)),
        _number(block, "end", path),
    )


def costs(build_dir: Path) -> dict[str, TestRecord]:
    """Return the record of the last run of each test of a build directory, by the name of the test.

    A test that did not run in the build directory has no record.  The record of a test that the build does not
    register at this time stays until a person removes it.

    Complexity: linear in the records of the build directory.

    Raises:
        CtestError: If a record cannot be read
    """
    directory = build_dir / cost_meter.TEST_RECORD_DIRECTORY
    records: dict[str, TestRecord] = {}
    for path in paths.listing(directory, f"*{cost_meter.TEST_RECORD_SUFFIX}"):
        record = read_record(path)
        records[record.name] = record
    return records
