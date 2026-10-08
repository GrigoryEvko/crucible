"""The reader of ctest: the tests that it registers for a build directory, and the output of a test run.

THE INPUT
    ctest prints the registered tests of a configured build directory as
    JSON (`ctest --show-only=json-v1`).  CMake has evaluated each function,
    loop, subdirectory and property call, so the command and the properties
    of each test are the values that ctest uses.  The backtrace graph gives
    the CMake file and the line of the add_test of each test.

    A test run prints one line for each test that ends: the count, `Test #N:`,
    the name, a row of dots, the verdict and the wall time, for example
    `12/40 Test  #7: test_arena ......   Passed    0.31 sec`.  At the end it
    prints a summary line: `100% tests passed out of N`, or with a failure
    `99% tests passed, F tests failed out of N`.  A run that selects no test
    prints `No tests were found!!!`.

WHAT THE READER GIVES
    listing() gives each test as a CtestTest: its name, its command, its
    properties and the place of its add_test.  test_times() gives the wall
    time of each test of the output of a run, and run_summary() gives its
    summary line.

Complexity: linear in the size of the JSON or of the output.
"""

import json
import re
import subprocess
from collections.abc import Mapping
from dataclasses import dataclass, field
from pathlib import Path

TIMEOUT_S = 300.0
# The line of one test that ends: its name, and its wall time in seconds.
TEST_LINE = re.compile(r"Test\s+#\d+: (\S+) \.+.*?([0-9.]+) sec$")
# The summary line of a run.
SUMMARY_LINE = re.compile(r"tests passed(, [0-9]+ tests? failed)? out of [0-9]+")
NO_TESTS_LINE = "No tests were found!!!"


class CtestError(RuntimeError):
    """ctest cannot list the tests of a build directory, or its output is not the JSON of the listing."""


@dataclass(frozen=True, slots=True)
class CtestTest:
    """One registered test: its name, its command, its properties and the CMake file and line of its add_test."""

    name: str
    command: tuple[str, ...]
    properties: Mapping[str, tuple[str, ...]] = field(default_factory=dict)
    file: str = ""
    line: int = 0

    def words_of(self, name: str) -> tuple[str, ...]:
        """Return the value of one property as words, empty when the test has no such property."""
        return self.properties.get(name, ())


def place_of(record: Mapping[str, object], graph: Mapping[str, object]) -> tuple[str, int]:
    """Return the CMake file and the line of the add_test of one test record, or an empty file and 0."""
    nodes, files = graph.get("nodes", []), graph.get("files", [])
    index = record.get("backtrace")
    if not (isinstance(index, int) and isinstance(nodes, list) and 0 <= index < len(nodes)):
        return "", 0
    node = nodes[index]
    if not (isinstance(node, dict) and isinstance(files, list) and isinstance(node.get("file"), int)
            and 0 <= node["file"] < len(files)):
        return "", 0
    line = node.get("line", 0)
    return str(files[node["file"]]), line if isinstance(line, int) else 0


def properties_of(record: Mapping[str, object]) -> dict[str, tuple[str, ...]]:
    """Return each property of one test record as a tuple of words."""
    found: dict[str, tuple[str, ...]] = {}
    for item in record.get("properties", []) or []:  # type: ignore[union-attr]
        if isinstance(item, dict) and isinstance(item.get("name"), str):
            value = item.get("value", [])
            found[item["name"]] = tuple(str(word) for word in value) if isinstance(value, list) else (str(value),)
    return found


def parse(text: str) -> list[CtestTest]:
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
    tests: list[CtestTest] = []
    for record in records:
        if not isinstance(record, dict):
            continue
        command = record.get("command", []) or []
        file, line = place_of(record, graph)
        tests.append(CtestTest(str(record.get("name", "<unnamed>")),
                               tuple(str(word) for word in command) if isinstance(command, list) else (),
                               properties_of(record), file, line))
    return tests


def listing(ctest: str, build_dir: Path) -> list[CtestTest]:
    """Run `ctest --show-only=json-v1` for a build directory, and read the tests that it registers.

    Raises:
        CtestError: If ctest cannot run or fails, or its output is not the JSON of the listing
    """
    try:
        completed = subprocess.run([ctest, "--show-only=json-v1", "--test-dir", str(build_dir)], capture_output=True,
                                   text=True, timeout=TIMEOUT_S, check=False)
    except (OSError, subprocess.TimeoutExpired) as problem:
        raise CtestError(f"ctest cannot run: {problem}") from None
    if completed.returncode != 0:
        raise CtestError(f"ctest exited {completed.returncode} on {build_dir}: {completed.stderr.strip()}")
    return parse(completed.stdout)


def test_times(text: str) -> dict[str, float]:
    """Return the wall time of each test that the output of a ctest run names, in seconds.

    A test that the output names two times keeps its last time.

    Args:
        text: The output of the run

    Returns:
        The wall time of each test, by its name
    """
    times: dict[str, float] = {}
    for line in text.splitlines():
        found = TEST_LINE.search(line)
        if found is not None:
            times[found.group(1)] = float(found.group(2))
    return times


def run_summary(text: str) -> str | None:
    """Return the last summary line of the output of a ctest run, or NO_TESTS_LINE, or None when it has neither.

    Args:
        text: The output of the run

    Returns:
        The summary line, NO_TESTS_LINE when the run selected no test, or None
    """
    found: str | None = None
    for line in text.splitlines():
        if SUMMARY_LINE.search(line) is not None:
            found = line
        elif line == NO_TESTS_LINE:
            found = NO_TESTS_LINE
    return found
