#!/usr/bin/env python3
"""check-build-order — no compile of the build waits for a link or an archive through an order-only input.

Ninja starts an edge only when each input of the edge is ready, also each
order-only input.  CMake gives each compile an order-only input, a phony edge
that leads to the custom commands of the target and of the targets that it
links, because such a command can write a header.  The Ninja generator also
gives a custom command an order-only input on each library that its target
links, unless the command takes DEPENDS_EXPLICIT_ONLY.  Each compile behind
such a command then waits for the link or the archive of that library.  With
such an input on the BPF commands of cmake/CrucibleBpf.cmake, approximately
1000 compiles wait for the archive of foundation, which is ready
approximately 2 s after the start of an edit build.

THE RULE
    The check reads build.ninja of the build directory.  For each compile
    edge, it follows each input to the edge that writes the input, and then
    each input of that edge, and so on.  An order-only input that a link or an
    archive writes is an error.  The error names the edge that has the
    order-only input, the link or the archive, and the number of compiles
    that wait for it.  An explicit or an implicit input that a link writes is
    permitted, because a command can run a tool of the build to write a
    file that a compile reads.  The check does not follow the inputs of a
    link or an archive.

    A compile edge has a rule whose name starts with CXX_COMPILER__ or
    C_COMPILER__.  A link or an archive has a rule whose name has the form
    <LANG>_<KIND>_LINKER__, for an executable, a static library, a shared
    library or a module.

Usage
    check-build-order.py --check build-order --build-dir BUILD_DIR [--warnings-dir DIR]
    check-build-order.py --self-test

Exit 0 with no finding, 1 with an error or an input that the check cannot
read, 2 on a usage error or a failed self-test, 3 when the build directory
has no build.ninja, because a generator other than Ninja made it.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import re
import sys
import tempfile
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
from repo_root import REPO_ROOT  # noqa: E402

CHECKS = ("build-order",)
NOT_APPLICABLE = 3
COMPILE_RULE = re.compile(r"(?:CXX|C)_COMPILER__")
LINK_RULE = re.compile(r"[A-Z]+_(?:EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY)_LINKER__")


class NotApplicable(Exception):
    """The check does not apply to the build."""


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
    """The edges of build.ninja, the edge that writes each output, and the default targets."""

    edges: list[Edge]
    producer: dict[str, int]
    defaults: list[str]


def display(path: Path) -> str:
    """Return a path relative to the repository root when it is under the root.

    Args:
        path: The path

    Returns:
        The text of the path
    """
    return str(path.relative_to(REPO_ROOT)) if path.is_relative_to(REPO_ROOT) else str(path)


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
    """Read the build statements and the default targets of one build.ninja.

    A line that ends with an unescaped '$' continues on the next line.  The
    check does not read an included file, because CMake writes only rules
    there.

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
    pending = ""
    pending_line = 0
    for number, raw in enumerate(path.read_text(encoding="utf-8").split("\n"), start=1):
        if not pending:
            pending_line = number
        trailing = len(raw) - len(raw.rstrip("$"))
        if trailing % 2 == 1:
            pending += raw[:-1]
            continue
        text = pending + raw
        pending = ""
        if text.startswith("default "):
            defaults.extend(split_paths(text[len("default "):]))
        elif text.startswith("build "):
            edge = parse_build_line(pending_line, text[len("build "):])
            for output in edge.outputs:
                producer[output] = len(edges)
            edges.append(edge)
    return Graph(edges, producer, defaults)


def order_only_waits(graph: Graph) -> dict[tuple[int, int], int]:
    """Find each order-only input that a link or an archive writes, behind each compile.

    Complexity: linear in the edges and their inputs, with one memo entry for
    each edge.

    Args:
        graph: The graph

    Returns:
        For each pair (the edge with the order-only input, the link or the archive), the number of
        compiles that wait for it
    """
    memo: dict[int, frozenset[tuple[int, int]]] = {}

    def waits_of(edge_id: int) -> frozenset[tuple[int, int]]:
        # An explicit stack, because a chain of phony edges can be deeper than
        # the recursion limit.
        stack: list[tuple[int, int]] = [(edge_id, 0)]
        while stack:
            current, child = stack.pop()
            edge = graph.edges[current]
            inputs = edge.inputs + edge.order_only
            if child == 0 and current in memo:
                continue
            if child < len(inputs):
                stack.append((current, child + 1))
                producer = graph.producer.get(inputs[child])
                if (producer is not None and producer not in memo
                        and not LINK_RULE.match(graph.edges[producer].rule)):
                    memo.setdefault(current, frozenset())
                    stack.append((producer, 0))
                continue
            found: set[tuple[int, int]] = set()
            for index, name in enumerate(inputs):
                producer = graph.producer.get(name)
                if producer is None:
                    continue
                if LINK_RULE.match(graph.edges[producer].rule):
                    if index >= len(edge.inputs):
                        found.add((current, producer))
                else:
                    found |= memo.get(producer, frozenset())
            memo[current] = frozenset(found)
        return memo[edge_id]

    counts: Counter[tuple[int, int]] = Counter()
    for edge_id, edge in enumerate(graph.edges):
        if COMPILE_RULE.match(edge.rule):
            for pair in waits_of(edge_id):
                counts[pair] += 1
    return dict(counts)


def evaluate_order(graph: Graph, ninja_shown: str) -> list[check_report.Finding]:
    """Make one error for each order-only input that a link or an archive writes, behind a compile.

    Args:
        graph: The graph
        ninja_shown: The path of build.ninja, for the findings

    Returns:
        The findings
    """
    findings: list[check_report.Finding] = []
    for (waiter, link), count in sorted(order_only_waits(graph).items()):
        waiting = graph.edges[waiter]
        written = graph.edges[link].outputs[0]
        findings.append(check_report.Finding(
            "error", ninja_shown, waiting.line, "build-order",
            f"the edge that writes {waiting.outputs[0]} has the order-only input {written}, which a link or an "
            f"archive writes, so {count} compile(s) wait for that link or archive.  If the command reads only the "
            f"files that its DEPENDS name, give add_custom_command the option DEPENDS_EXPLICIT_ONLY"))
    return findings


def self_test() -> int:
    """Plant each verdict in a scratch build.ninja, and check it.

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

    planted = (
        "# a comment\n"
        "rule CXX_COMPILER__a_unscanned_Debug\n"
        "  command = g++ $in\n"
        "build liba.a: CXX_STATIC_LIBRARY_LINKER__a_Debug a.o\n"
        "build a.o: CXX_COMPILER__a_unscanned_Debug a.cpp || cmake_object_order_depends_target_a\n"
        "build cmake_object_order_depends_target_a: phony || .\n"
        "build gen.c | ${cmake_ninja_workdir}gen.c: CUSTOM_COMMAND gen.in || liba.a\n"
        "build cmake_object_order_depends_target_b: phony || gen.c cmake_object_order_depends_target_a\n"
        "build b.o: CXX_COMPILER__b_unscanned_Debug b.cpp || cmake_object_order_depends_target_b\n"
        "build b2.o: CXX_COMPILER__b_unscanned_Debug $\n"
        "    b2.cpp || cmake_object_order_depends_target_b\n"
        "build tool: CXX_EXECUTABLE_LINKER__tool_Debug tool.o\n"
        "build tool.o: CXX_COMPILER__tool_unscanned_Debug tool.cpp\n"
        "build hdr.h: CUSTOM_COMMAND tool\n"
        "build cmake_object_order_depends_target_c: phony || hdr.h\n"
        "build c.o: CXX_COMPILER__c_unscanned_Debug c.cpp || cmake_object_order_depends_target_c\n"
        "build all: phony liba.a tool\n"
        "default all\n")
    with tempfile.TemporaryDirectory(prefix="check-build-order-") as work:
        ninja = Path(work) / "build.ninja"
        ninja.write_text(planted, encoding="utf-8")
        graph = read_graph(ninja)
        expect("read_graph reads each build statement, a continued line and the default targets",
               len(graph.edges) == 13 and graph.defaults == ["all"]
               and graph.edges[graph.producer["b2.o"]].inputs == ("b2.cpp",)
               and graph.edges[graph.producer["b2.o"]].line == 10)
        findings = evaluate_order(graph, "build.ninja")
        expect("an error: a custom command with an order-only input that an archive writes, at its line, with the "
               "count of the compiles behind it",
               len(findings) == 1 and findings[0].level == "error" and findings[0].line == 7
               and "gen.c" in findings[0].message and "liba.a" in findings[0].message
               and "2 compile(s)" in findings[0].message)
        expect("no finding: a command that runs a tool of the build as an explicit input",
               not any("hdr.h" in found.message for found in findings))
        ninja.write_text(planted.replace(" || liba.a\n", "\n"), encoding="utf-8")
        expect("no finding: the same graph with no order-only input on the archive",
               evaluate_order(read_graph(ninja), "build.ninja") == [])
        warnings_dir = Path(work) / "warnings"
        with contextlib.redirect_stdout(io.StringIO()):
            status = check_report.emit(findings, "build-order", warnings_dir)
        expect("an error gives exit status 1", status == 1)
    if failures:
        print(f"check-build-order --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-build-order --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Run one check, or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    parser = argparse.ArgumentParser(prog="check-build-order.py", description=__doc__.split("\n", 1)[0])
    check_report.add_arguments(parser)
    parser.add_argument("--check", choices=CHECKS)
    parser.add_argument("--build-dir", type=Path, help="the build directory")
    parser.add_argument("--self-test", action="store_true", help="plant each verdict in a scratch build.ninja")
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return self_test()
    if arguments.check is None or arguments.build_dir is None:
        parser.error("give --check CHECK --build-dir BUILD_DIR, or --self-test")
    build = arguments.build_dir.resolve()
    ninja = build / "build.ninja"
    try:
        if not ninja.is_file():
            raise NotApplicable(f"{ninja} does not exist, so a generator other than Ninja made the build")
        graph = read_graph(ninja)
    except NotApplicable as exc:
        print(f"check-build-order: the check does not apply: {exc}.", file=sys.stderr)
        return NOT_APPLICABLE
    except (ValueError, OSError) as exc:
        return check_report.emit([check_report.Finding("error", display(ninja), 0, arguments.check,
                                                       f"the check cannot read its input: {exc}")],
                                 arguments.check, arguments.warnings_dir)
    findings = evaluate_order(graph, display(ninja))
    print(f"check-build-order: {len(graph.edges)} edges, {len(findings)} finding(s).", file=sys.stderr)
    return check_report.emit(findings, arguments.check, arguments.warnings_dir)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
