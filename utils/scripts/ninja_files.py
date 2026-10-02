#!/usr/bin/env python3
"""ninja_files — read build.ninja and the ninja log of one build directory.

BUILD.NINJA
    read_graph() reads the build statements, their bindings and the default
    targets.  A line that ends with an unescaped '$' continues on the next
    line.  An indented line after a build statement is a binding of that
    statement, `name = value`, until a line that is empty or not indented.
    The reader does not read an included file, because CMake writes only
    rules there, and it does not expand a variable.  edge_kind() names the
    kind of an edge from its rule: a compile has a rule whose name starts
    with CXX_COMPILER__ or C_COMPILER__, and a link or an archive has a rule
    whose name has the form <LANG>_<KIND>_LINKER__.

THE NINJA LOG
    Ninja appends one record to .ninja_log for each output of each edge that
    it runs: the start and the end in milliseconds after the start of the
    Ninja process, the modification time of the output, the output path and
    a hash of the command.  When the log holds more than three records for
    each output, Ninja writes it again at its next start, with only the last
    record of each output and in no order.  So an offset into the file does
    not find the records of one run.  read_log() keeps the last record of
    each output, and ran_outputs() compares two such reads: an output whose
    record is new or changed ran between the two reads.  A rewrite keeps the
    last record of each output, so it adds no output.

Usage
    ninja_files.py --self-test

Exit 0 when each case of the self-test holds, 2 otherwise.
"""

from __future__ import annotations

import os
import re
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

LOG_NAME = ".ninja_log"
COMPILE_RULE = re.compile(r"(?:CXX|C)_COMPILER__")
LINK_RULE = re.compile(r"[A-Z]+_(?:EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY)_LINKER__")
ARCHIVE_RULE = re.compile(r"[A-Z]+_STATIC_LIBRARY_LINKER__")
# The rules of the CMake run of Ninja and of the glob check of CMake.  The glob
# check runs at each start of Ninja, before each other edge.
CMAKE_RULES = frozenset({"RERUN_CMAKE", "VERIFY_GLOBS"})
CUSTOM_RULE = "CUSTOM_COMMAND"
PHONY_RULE = "phony"


@dataclass(frozen=True, slots=True)
class Edge:
    """One build statement of build.ninja.

    `inputs` holds each explicit and each implicit input, and `order_only`
    holds each order-only input.
    """

    line: int
    rule: str
    outputs: tuple[str, ...]
    inputs: tuple[str, ...]
    order_only: tuple[str, ...]


@dataclass(slots=True)
class Graph:
    """The edges of build.ninja, the edge that writes each output, the default targets, and the bindings of each edge.

    `variables` holds the bindings of each edge that has one, by the index of the edge.
    """

    edges: list[Edge]
    producer: dict[str, int]
    defaults: list[str]
    variables: dict[int, dict[str, str]]


@dataclass(frozen=True, slots=True)
class LogRecord:
    """The last record of one output in the ninja log."""

    start_ms: int
    end_ms: int
    mtime: int
    command_hash: str


def split_paths(text: str) -> list[str]:
    """Split a list of Ninja paths at each space that is not escaped, and remove the escapes.

    Complexity: linear in the length of the text.

    Args:
        text: The paths, as build.ninja writes them

    Returns:
        The paths
    """
    paths: list[str] = []
    current: list[str] = []
    index = 0
    while index < len(text):
        character = text[index]
        if character == "$" and index + 1 < len(text):
            current.append(text[index + 1])
            index += 2
            continue
        if character == " ":
            if current:
                paths.append("".join(current))
                current = []
        else:
            current.append(character)
        index += 1
    if current:
        paths.append("".join(current))
    return paths


def find_unescaped(text: str, token: str) -> int:
    """Return the index of the first occurrence of a token that is not escaped with '$', or -1.

    Args:
        text: The text
        token: The token

    Returns:
        The index, or -1 when the text holds no such occurrence
    """
    index = 0
    while index < len(text):
        if text[index] == "$":
            index += 2
            continue
        if text.startswith(token, index):
            return index
        index += 1
    return -1


def parse_build_line(line: int, body: str) -> Edge:
    """Read one build statement, the text after `build `.

    Args:
        line: The line of the statement in build.ninja
        body: The statement

    Returns:
        The edge

    Raises:
        ValueError: If the statement has no ':' or no rule
    """
    colon = find_unescaped(body, ":")
    if colon < 0:
        raise ValueError(f"build.ninja:{line}: the build statement has no ':'")
    outputs_text, rest = body[:colon], body[colon + 1:]
    implicit_outputs = find_unescaped(outputs_text, "|")
    if implicit_outputs >= 0:
        outputs_text = outputs_text[:implicit_outputs] + " " + outputs_text[implicit_outputs + 1:]
    validations = find_unescaped(rest, "|@")
    if validations >= 0:
        rest = rest[:validations]
    order_only_at = find_unescaped(rest, "||")
    order_only_text = ""
    if order_only_at >= 0:
        rest, order_only_text = rest[:order_only_at], rest[order_only_at + 2:]
    parts = split_paths(rest)
    if not parts:
        raise ValueError(f"build.ninja:{line}: the build statement has no rule")
    inputs = tuple(part for part in parts[1:] if part != "|")
    return Edge(line, parts[0], tuple(split_paths(outputs_text)), inputs, tuple(split_paths(order_only_text)))


def read_graph(path: Path) -> Graph:
    """Read the build statements, their bindings and the default targets of one build.ninja.

    An indented line after a build statement is a binding of that statement,
    until a line that is empty or not indented.  The indented lines after a
    rule statement are not read.

    Complexity: linear in the size of the file.

    Args:
        path: The build.ninja

    Returns:
        The graph

    Raises:
        ValueError: If a build statement cannot be read
        OSError: If the file cannot be read
    """
    edges: list[Edge] = []
    producer: dict[str, int] = {}
    defaults: list[str] = []
    variables: dict[int, dict[str, str]] = {}
    pending = ""
    pending_line = 0
    binding_edge: int | None = None
    for number, raw in enumerate(path.read_text(encoding="utf-8").split("\n"), start=1):
        if not pending:
            pending_line = number
        trailing = len(raw) - len(raw.rstrip("$"))
        if trailing % 2 == 1:
            pending += raw[:-1]
            continue
        text = pending + raw
        pending = ""
        if text[:1] in (" ", "\t") and text.strip():
            name, separator, value = text.partition("=")
            if binding_edge is not None and separator:
                variables.setdefault(binding_edge, {})[name.strip()] = value.strip()
            continue
        binding_edge = None
        if text.startswith("default "):
            defaults.extend(split_paths(text[len("default "):]))
        elif text.startswith("build "):
            edge = parse_build_line(pending_line, text[len("build "):])
            binding_edge = len(edges)
            for output in edge.outputs:
                producer[output] = len(edges)
            edges.append(edge)
    return Graph(edges, producer, defaults, variables)


def edge_kind(rule: str) -> str:
    """Return the kind of an edge from its rule.

    Args:
        rule: The rule of the edge

    Returns:
        "phony", "compile", "archive", "link", "custom", "cmake" (the CMake run or the glob check) or "other"
    """
    if rule == PHONY_RULE:
        return "phony"
    if COMPILE_RULE.match(rule):
        return "compile"
    if ARCHIVE_RULE.match(rule):
        return "archive"
    if LINK_RULE.match(rule):
        return "link"
    if rule == CUSTOM_RULE:
        return "custom"
    if rule in CMAKE_RULES:
        return "cmake"
    return "other"


def read_log(build: Path) -> dict[str, LogRecord]:
    """Read the last record of each output in the ninja log of a build directory.

    A line that does not have five fields, or whose times are not numbers,
    is not a record.  Complexity: O(n) in the lines of the log.

    Args:
        build: The build directory

    Returns:
        The record of each output, by its absolute normalized path.  An empty dictionary when the build has no log
    """
    records: dict[str, LogRecord] = {}
    try:
        text = (build / LOG_NAME).read_text(encoding="utf-8", errors="replace")
    except FileNotFoundError:
        return records
    for raw in text.splitlines():
        if raw.startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) < 5:
            continue
        start, end, mtime, output, command_hash = fields[:5]
        try:
            record = LogRecord(int(start), int(end), int(mtime), command_hash)
        except ValueError:
            continue
        records[os.path.normpath(os.path.join(build, output))] = record
    return records


def ran_outputs(before: dict[str, LogRecord], after: dict[str, LogRecord]) -> list[str]:
    """Return the outputs whose ninja log record is new or changed.

    Args:
        before: The records before a build
        after: The records after it

    Returns:
        The outputs in sorted order
    """
    return sorted(output for output, record in after.items() if before.get(output) != record)


# ── The self-test ──────────────────────────────────────────────────

GRAPH_TEXT = (
    "# a comment\n"
    "rule CXX_COMPILER__a_unscanned_Debug\n"
    "  command = g++ $in\n"
    "build a.o: CXX_COMPILER__a_unscanned_Debug a.cpp || order_a\n"
    "  DEP_FILE = a.o.d\n"
    "build order_a: phony || gen.h\n"
    "build gen.h | ${cmake_ninja_workdir}gen.h: CUSTOM_COMMAND tool\n"
    "  restat = 1\n"
    "\n"
    "  depfile = after_blank.d\n"
    "build tool: CXX_EXECUTABLE_LINKER__tool_Debug tool.o $\n"
    "    liba.a\n"
    "build liba.a: CXX_STATIC_LIBRARY_LINKER__a_Debug a.o\n"
    "build all: phony tool\n"
    "default all\n")


def self_test() -> int:
    """Check each reader on planted files.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    expect("split_paths removes the escapes of a space and a colon",
           split_paths("a$ b c$:d  e") == ["a b", "c:d", "e"])
    edge = parse_build_line(7, "out.o | $${dir}out.o: CXX_COMPILER__t_Debug src.cpp | dep.h || order phony |@ check")
    expect("parse_build_line reads the outputs, the rule, the inputs and the order-only inputs",
           edge.outputs == ("out.o", "${dir}out.o") and edge.rule == "CXX_COMPILER__t_Debug"
           and edge.inputs == ("src.cpp", "dep.h") and edge.order_only == ("order", "phony"))
    try:
        parse_build_line(1, "no colon here")
        expect("parse_build_line refuses a statement with no ':'", False)
    except ValueError:
        expect("parse_build_line refuses a statement with no ':'", True)
    expect("edge_kind names each kind of rule",
           [edge_kind(rule) for rule in ("phony", "CXX_COMPILER__a_unscanned_Debug", "C_COMPILER__b_Debug",
                                         "CXX_STATIC_LIBRARY_LINKER__a_Debug", "CXX_EXECUTABLE_LINKER__t_Debug",
                                         "CXX_SHARED_LIBRARY_LINKER__s_Debug", "CUSTOM_COMMAND", "RERUN_CMAKE",
                                         "VERIFY_GLOBS", "CLEAN")]
           == ["phony", "compile", "compile", "archive", "link", "link", "custom", "cmake", "cmake", "other"])

    with tempfile.TemporaryDirectory(prefix="ninja-files-") as work:
        build = Path(work)
        (build / "build.ninja").write_text(GRAPH_TEXT, encoding="utf-8")
        graph = read_graph(build / "build.ninja")
        tool = graph.edges[graph.producer["tool"]]
        expect("read_graph reads each build statement, a continued line and the default targets",
               len(graph.edges) == 6 and graph.defaults == ["all"] and tool.inputs == ("tool.o", "liba.a")
               and tool.line == 11)
        expect("read_graph keeps the variable of an implicit output unexpanded",
               graph.edges[graph.producer["gen.h"]].outputs == ("gen.h", "{cmake_ninja_workdir}gen.h"))
        gen_bindings = graph.variables.get(graph.producer["gen.h"], {})
        expect("read_graph reads the bindings of a build statement, ends them at an empty line, and reads no "
               "binding of a rule",
               graph.variables.get(graph.producer["a.o"]) == {"DEP_FILE": "a.o.d"} and gen_bindings == {"restat": "1"}
               and not any("command" in bindings for bindings in graph.variables.values()))

        expect("read_log gives no record for a build with no log", read_log(build) == {})
        log = build / LOG_NAME
        log.write_text("# ninja log v7\n"
                       "10\t20\t100\tCMakeFiles/cmake.verify_globs\taa\n"
                       "30\t900\t101\ta.o\tbb\n"
                       "a damaged line\n"
                       "x\t9\t1\tbad.o\tcc\n"
                       "40\t950\t102\ttool\tdd\n", encoding="utf-8")
        first = read_log(build)
        expect("read_log keys each record by its absolute path, and skips a damaged line",
               sorted(first) == [str(build / "CMakeFiles/cmake.verify_globs"), str(build / "a.o"), str(build / "tool")]
               and first[str(build / "a.o")] == LogRecord(30, 900, 101, "bb"))
        with log.open("a", encoding="utf-8") as handle:
            handle.write("12\t21\t100\tCMakeFiles/cmake.verify_globs\taa\n"
                         "31\t500\t103\ta.o\tbb\n"
                         f"35\t40\t104\t{build}/gen.h\tee\n")
        second = read_log(build)
        expect("read_log keeps the last record of each output, under one key for a relative and an absolute path",
               second[str(build / "a.o")] == LogRecord(31, 500, 103, "bb") and str(build / "gen.h") in second)
        expect("ran_outputs gives each new and each changed output of a second run",
               ran_outputs(first, second) == sorted([str(build / "CMakeFiles/cmake.verify_globs"), str(build / "a.o"),
                                                     str(build / "gen.h")]))
        lines = log.read_text(encoding="utf-8").splitlines()
        kept = {line.split("\t")[3]: line for line in lines[1:] if line.count("\t") == 4 and line[0].isdigit()}
        log.write_text("\n".join([lines[0], *sorted(kept.values(), reverse=True)]) + "\n", encoding="utf-8")
        expect("a rewrite of the log with the last record of each output, in another order, adds no output",
               ran_outputs(second, read_log(build)) == [])

    if failures:
        print(f"ninja_files --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("ninja_files --self-test: every case holds.")
    return 0


if __name__ == "__main__":
    if sys.argv[1:] == ["--self-test"]:
        sys.exit(self_test())
    print("usage: ninja_files.py --self-test", file=sys.stderr)
    sys.exit(2)
