#!/usr/bin/env python3
"""check-build-order — the order in which Ninja starts the compiles of an edit build, and the headers of a custom compile.

Ninja starts an edge only when each input of the edge is ready, also each
order-only input.  Of the ready edges, it starts first the edge with the
longest chain of edges to a target of the build, and on a tie the edge that
comes first in build.ninja.  It does not read the time of a job.  Two checks
hold the graph to that model.  The check custom-deps holds the header
dependencies of each object that a custom command compiles.

THE CHECK build-order
    No compile waits for a link or an archive through an order-only input.

    CMake gives each compile an order-only input, a phony edge that leads to
    the custom commands of the target and of the targets that it links,
    because such a command can write a header.  The Ninja generator also
    gives a custom command an order-only input on each library that its
    target links, unless the command takes DEPENDS_EXPLICIT_ONLY.  Each
    compile behind such a command then waits for the link or the archive of
    that library.  With such an input on the BPF commands of
    cmake/CrucibleBpf.cmake, approximately 1000 compiles wait for the archive
    of foundation, which is ready approximately 2 s after the start of an
    edit build.

    For each compile edge, the check follows each input to the edge that
    writes the input, and then each input of that edge, and so on.  An
    order-only input that a link or an archive writes is an error.  The error
    names the edge that has the order-only input, the link or the archive,
    and the number of compiles that wait for it.  An explicit or an implicit
    input that a link writes is permitted, because a command can run a tool
    of the build to write a file that a compile reads.  The check does not
    follow the inputs of a link or an archive.

THE CHECK compile-first
    The targets with a long compile are on utils/scripts/compile-first.txt,
    and each object of a listed target has a chain of three edges or more.

    The object of a test has a chain of two edges, the compile and the link,
    so Ninja starts the objects of the tests in the order of build.ninja, short
    or long.  cmake/CompileFirst.cmake gives each listed target one more edge,
    so Ninja starts its objects before the objects of the other tests.

    * The graph: an object of a listed target of `all` with a chain of less
      than three edges is an error at the row of the target.
    * The records: the largest compile of a target is the compile of its
      object with the most user instructions.  A target of `all` whose largest
      compile exceeds the error threshold of the row compile-first of
      utils/scripts/budgets.txt is an error when the list does not name it.  A
      listed target whose largest compile does not exceed the warning
      threshold gives a warning.  A listed target between the two thresholds
      gives no finding, so a small change of a compile does not move a target
      in and out of the list.  A listed name with no judged compile gives no
      finding, because a preset can leave out a target.

    The check judges the records only in a build of the kind of the row
    `kind` of the list, because the instruction count of a compile changes
    with the kind.  In a build of another kind, and in a build with no record
    that holds an instruction count, the check judges only the graph.  The
    list serves the build host.  A GitHub runner can use another build of the
    compiler, which gives other counts, so on a GitHub runner a target that
    the list does not name gives a warning, and the warning tells why.

    --write writes the list again from the records.  It adds each target over
    the error threshold, removes each listed target at or below the warning
    threshold, and keeps each other name and the header of the list.

THE CHECK custom-deps
    Each object that a custom command compiles has the headers of its compile
    in the dependency log of Ninja.

    A custom command can compile a source, as the BPF programs of
    cmake/CrucibleBpf.cmake do.  The option IMPLICIT_DEPENDS of
    add_custom_command applies only to the Makefile generators.  With Ninja
    and no depfile, an edit of a header does not compile the object again,
    and the build keeps the object of the old header.  The command must
    write a depfile, and add_custom_command must take the option DEPFILE.
    CMake then gives the edge the bindings `depfile` and `deps = gcc`, and
    Ninja moves the list of the depfile into its dependency log.

    * build.ninja: an edge of the rule CUSTOM_COMMAND whose first output ends
      with `.o` is an error at its line when it has no `depfile` binding or
      no `deps = gcc` binding.
    * The dependency log: for each such object that exists, `ninja -t deps`
      must give a VALID list that holds a header, a file that is not an
      input of the edge in build.ninja.  Each other state is an error.  The
      check does not read the log of an object that does not exist, because
      a build can make only some targets.

    The check exits 3 when no custom command writes an object, for example in
    a build with CRUCIBLE_HAVE_BPF off.

WHAT THE CHECKS READ
    * build.ninja of the build directory.  A line that ends with an unescaped
      '$' continues on the next line.  An indented line after a build
      statement is a binding of that statement, `name = value`, until a line
      that is empty or not indented.  The checks do not read an included
      file, because CMake writes only rules there.  A compile edge has a rule
      whose name starts with CXX_COMPILER__ or C_COMPILER__.  A link or an
      archive has a rule whose name has the form <LANG>_<KIND>_LINKER__, for
      an executable, a static library, a shared library or a module.  The
      target of an object is the directory CMakeFiles/<target>.dir of its path.
    * For custom-deps: the dependency log (`ninja -t deps`), through the
      ninja of the value CMAKE_MAKE_PROGRAM of the CMakeCache.txt of the
      build (utils/scripts/build_census.py reads the output).
    * For compile-first: BUILD_DIR/compile-first-targets.txt, each target of
      `all` that compiles a source, which cmake/CompileFirst.cmake writes at
      each configure.  The compile database, and the record OBJECT.cost of
      each object (utils/scripts/cost_meter.py): the user instructions of the
      cost block of a built object, or of the last_cost block of a ccache hit.
      The kind of the build (BUILD_DIR/build-kind.txt).

Usage
    check-build-order.py --check build-order --build-dir BUILD_DIR [--warnings-dir DIR]
    check-build-order.py --check compile-first --build-dir BUILD_DIR [--warnings-dir DIR]
    check-build-order.py --check compile-first --build-dir BUILD_DIR --write
    check-build-order.py --check custom-deps --build-dir BUILD_DIR [--warnings-dir DIR]
    check-build-order.py --self-test

Exit 0 with no finding or with warnings only, 1 with an error or an input that
the check cannot read, 2 on a usage error or a failed self-test, 3 when the
build directory has no build.ninja, because a generator other than Ninja made
it, for --write when the records do not apply, and for custom-deps when no
custom command writes an object.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
import re
import sys
import tempfile
from collections import Counter
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import build_census  # noqa: E402
import check_report  # noqa: E402
import cost_meter  # noqa: E402
from repo_root import REPO_ROOT  # noqa: E402

CHECKS = ("build-order", "compile-first", "custom-deps")
NOT_APPLICABLE = 3
COMPILE_RULE = re.compile(r"(?:CXX|C)_COMPILER__")
LINK_RULE = re.compile(r"[A-Z]+_(?:EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY)_LINKER__")
# The rule of each custom command, and the suffix of an object that a custom command compiles.
CUSTOM_RULE = "CUSTOM_COMMAND"
OBJECT_SUFFIX = ".o"
# The value of the binding `deps` that makes Ninja move a depfile into its dependency log.
GCC_DEPS = "gcc"
LIST = Path(__file__).resolve().parent / "compile-first.txt"
CANDIDATES = "compile-first-targets.txt"
KIND_PREFIX = "kind "
GIGA = 1e9
# The chain of the object of a test: the compile and the link.  An object of a
# listed target has one edge more.
LISTED_CHAIN = 3
# The results of a record whose count belongs to the object that exists.
COUNTED_RESULTS = frozenset({"built", "hit"})


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
    """The edges of build.ninja, the edge that writes each output, the default targets, and the bindings of each edge.

    `variables` holds the bindings of each edge that has one, by the index of the edge.
    """

    edges: list[Edge]
    producer: dict[str, int]
    defaults: list[str]
    variables: dict[int, dict[str, str]]


@dataclass(frozen=True, slots=True)
class Compile:
    """The largest compile of one target: its source and its user instructions."""

    source: str
    instructions: int


@dataclass(slots=True)
class CompileList:
    """utils/scripts/compile-first.txt: its header lines, its kind, and the line of each name."""

    header: list[str]
    kind: str
    names: dict[str, int]


def display(path: Path, root: Path = REPO_ROOT) -> str:
    """Return a path relative to the root when it is under the root.

    Args:
        path: The path
        root: The root

    Returns:
        The text of the path
    """
    return str(path.relative_to(root)) if path.is_relative_to(root) else str(path)


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


def target_of(object_path: Path) -> str | None:
    """Return the target of an object, from the directory CMakeFiles/<target>.dir of its path.

    Args:
        object_path: The path of the object

    Returns:
        The target name, or None when the path has no such directory
    """
    parts = object_path.parts
    for index in range(len(parts) - 1):
        if parts[index] == "CMakeFiles" and parts[index + 1].endswith(".dir"):
            return parts[index + 1][: -len(".dir")]
    return None


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


def custom_objects(graph: Graph) -> list[int]:
    """Return each edge of a custom command whose first output is an object.

    Complexity: linear in the edges.

    Args:
        graph: The graph

    Returns:
        The index of each such edge, in the order of build.ninja
    """
    return [edge_id for edge_id, edge in enumerate(graph.edges)
            if edge.rule == CUSTOM_RULE and edge.outputs and edge.outputs[0].endswith(OBJECT_SUFFIX)]


def evaluate_custom_deps(graph: Graph, build: Path, built: set[str], lists: dict[str, list[str]],
                         states: dict[str, str], ninja_shown: str) -> list[check_report.Finding]:
    """Make one error for each object of a custom command whose headers Ninja does not record.

    An edge with no `depfile` binding or no `deps = gcc` binding gives one
    error, and the check then does not read the log of its object.

    Complexity: linear in the edges, their inputs and the files of the log.

    Args:
        graph: The graph
        build: The build directory, against which a relative path of build.ninja resolves
        built: The first output of each such edge that exists
        lists: The files of each object in the dependency log, from build_census.parse_dependencies
        states: The state of the list of each object in the dependency log
        ninja_shown: The path of build.ninja, for the findings

    Returns:
        The findings
    """
    def resolved(name: str) -> str:
        return os.path.normpath(os.path.join(build, name))

    findings: list[check_report.Finding] = []
    for edge_id in custom_objects(graph):
        edge = graph.edges[edge_id]
        output = edge.outputs[0]
        bindings = graph.variables.get(edge_id, {})
        if not bindings.get("depfile") or bindings.get("deps") != GCC_DEPS:
            findings.append(check_report.Finding(
                "error", ninja_shown, edge.line, "custom-deps",
                f"the custom command that writes {output} has no `depfile` binding with `deps = {GCC_DEPS}`, so the "
                f"dependency log of Ninja holds no header of the compile, and an edit of a header does not compile "
                f"{output} again.  Give the compiler -MD -MF FILE, and give add_custom_command the option DEPFILE "
                f"FILE"))
            continue
        if output not in built:
            continue
        state = states.get(output, "MISSING")
        inputs = {resolved(name) for name in edge.inputs}
        has_header = any(resolved(name) not in inputs for name in lists.get(output, []))
        if state != "VALID" or not has_header:
            findings.append(check_report.Finding(
                "error", ninja_shown, edge.line, "custom-deps",
                f"the dependency log of Ninja holds no header of {output}, and the state of its list is {state}, "
                f"although build.ninja gives the command a depfile.  Make sure that the depfile names each header "
                f"that the compile reads, and build {output} again"))
    return findings


def chain_lengths(graph: Graph) -> dict[int, int]:
    """Return the chain of each edge that a default target reaches, as Ninja 1.12 and later count it.

    The chain of an edge is the number of edges that are not phony on the
    longest path from the edge to a default target, the edge included.  Ninja
    starts the ready edge with the longest chain first.

    Complexity: linear in the edges and their inputs.

    Args:
        graph: The graph

    Returns:
        The chain of each edge that a default target reaches
    """
    order: list[int] = []
    seen: set[int] = set()
    for target in graph.defaults or ["all"]:
        root = graph.producer.get(target)
        if root is None or root in seen:
            continue
        seen.add(root)
        stack: list[tuple[int, int]] = [(root, 0)]
        while stack:
            current, child = stack.pop()
            inputs = graph.edges[current].inputs + graph.edges[current].order_only
            if child < len(inputs):
                stack.append((current, child + 1))
                producer = graph.producer.get(inputs[child])
                if producer is not None and producer not in seen:
                    seen.add(producer)
                    stack.append((producer, 0))
            else:
                order.append(current)

    def own(edge_id: int) -> int:
        return 0 if graph.edges[edge_id].rule == "phony" else 1

    chain = {edge_id: own(edge_id) for edge_id in order}
    for edge_id in reversed(order):
        edge = graph.edges[edge_id]
        for name in edge.inputs + edge.order_only:
            producer = graph.producer.get(name)
            if producer is not None and producer in chain:
                chain[producer] = max(chain[producer], chain[edge_id] + own(producer))
    return chain


def read_list(path: Path) -> CompileList:
    """Read utils/scripts/compile-first.txt.

    The comment lines before the first row are the header.  One row is
    `kind KIND`, and each other row is one target name.

    Args:
        path: The list

    Returns:
        The list

    Raises:
        ValueError: If the list has no kind row or two, a name with a space, or a name on two rows
        OSError: If the list cannot be read
    """
    header: list[str] = []
    kind = ""
    names: dict[str, int] = {}
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        text = raw.strip()
        if not text or text.startswith("#"):
            if not kind and not names:
                header.append(raw)
            continue
        if text.startswith(KIND_PREFIX):
            if kind:
                raise ValueError(f"{path}:{number}: the list has a second kind row.  Keep one `kind KIND` row")
            kind = text[len(KIND_PREFIX):].strip()
            continue
        if any(character.isspace() for character in text):
            raise ValueError(f"{path}:{number}: the row `{text}` is not one target name")
        if text in names:
            raise ValueError(f"{path}:{number}: the target {text} has a second row.  Remove one")
        names[text] = number
    if not kind:
        raise ValueError(f"{path}: the list has no `kind KIND` row.  Add the kind of the build that the list comes "
                         f"from, for example `kind x86_64-debug-asan`")
    return CompileList(header, kind, names)


def read_candidates(build: Path) -> set[str]:
    """Read the targets of `all` that compile a source, which cmake/CompileFirst.cmake writes.

    Args:
        build: The build directory

    Returns:
        The target names

    Raises:
        ValueError: If the file does not exist
    """
    path = build / CANDIDATES
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as problem:
        raise ValueError(f"{path} cannot be read ({problem}).  Configure the build directory again, so that "
                         f"cmake/CompileFirst.cmake writes it") from None
    return {line.strip() for line in text.splitlines() if line.strip()}


def evaluate_chains(graph: Graph, candidates: set[str], compile_list: CompileList,
                    list_shown: str) -> list[check_report.Finding]:
    """Make one error for each listed target of `all` with an object whose chain is shorter than LISTED_CHAIN.

    Complexity: linear in the edges.

    Args:
        graph: The graph
        candidates: The targets of `all` that compile a source
        compile_list: The list
        list_shown: The path of the list, for the findings

    Returns:
        The findings
    """
    chain = chain_lengths(graph)
    shortest: dict[str, tuple[int, str]] = {}
    for edge_id, edge in enumerate(graph.edges):
        if not COMPILE_RULE.match(edge.rule):
            continue
        target = target_of(Path(edge.outputs[0]))
        if target is None or target not in compile_list.names or target not in candidates:
            continue
        length = chain.get(edge_id, 0)
        if target not in shortest or length < shortest[target][0]:
            shortest[target] = (length, edge.outputs[0])
    findings: list[check_report.Finding] = []
    for target, (length, output) in sorted(shortest.items()):
        if length < LISTED_CHAIN:
            findings.append(check_report.Finding(
                "error", list_shown, compile_list.names[target], "compile-first",
                f"the target {target} is on the list, and its object {output} has a chain of {length} edge(s) in "
                f"build.ninja.  cmake/CompileFirst.cmake gives each object of a listed target a chain of "
                f"{LISTED_CHAIN} edges or more, so Ninja does not start the object first.  Configure the build "
                f"again, and make sure that cmake/CompileFirst.cmake is included"))
    return findings


def record_count(object_path: Path) -> int | None:
    """Return the user instructions of the compile of one object, from its record.

    Args:
        object_path: The path of the object

    Returns:
        The count of the cost block of a built object or of the last_cost block of a ccache hit, or None
        when the record is missing, cannot be read, or holds no exact count
    """
    record_path = Path(str(object_path) + cost_meter.RECORD_SUFFIX)
    try:
        record = json.loads(record_path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if (not isinstance(record, dict) or record.get("format") != cost_meter.RECORD_FORMAT
            or record.get("step") != "compile" or record.get("result") not in COUNTED_RESULTS):
        return None
    block = record.get("last_cost" if record["result"] == "hit" else "cost")
    if not isinstance(block, dict):
        return None
    count = block.get("instructions")
    return count if isinstance(count, int) and not isinstance(count, bool) and count >= 0 else None


def read_largest(build: Path, root: Path = REPO_ROOT) -> dict[str, Compile]:
    """Return the largest compile of each target that has a record with a count.

    Complexity: linear in the rows of the compile database.

    Args:
        build: The build directory
        root: The repository root, for the source paths of the findings

    Returns:
        The largest compile of each target

    Raises:
        ValueError: If the compile database cannot be read
    """
    database = build / "compile_commands.json"
    try:
        rows = json.loads(database.read_text(encoding="utf-8"))
    except (OSError, ValueError) as problem:
        raise ValueError(f"the compile database {database} cannot be read ({problem}).  Configure the build "
                         f"directory with a preset first") from None
    largest: dict[str, Compile] = {}
    for row in rows:
        output = row.get("output") if isinstance(row, dict) else None
        if not output:
            continue
        object_path = Path(os.path.normpath(os.path.join(row["directory"], output)))
        target = target_of(object_path)
        count = record_count(object_path) if target is not None else None
        if target is None or count is None:
            continue
        if target not in largest or count > largest[target].instructions:
            source = Path(os.path.normpath(os.path.join(row["directory"], row["file"])))
            largest[target] = Compile(display(source, root), count)
    return largest


def evaluate_records(largest: dict[str, Compile], candidates: set[str], compile_list: CompileList,
                     budget: check_report.Budget, list_shown: str) -> list[check_report.Finding]:
    """Compare the largest compile of each target of `all` with the list.

    Complexity: O(n log n) in the targets, because of the sort.

    Args:
        largest: The largest compile of each target with a count
        candidates: The targets of `all` that compile a source
        compile_list: The list
        budget: The row compile-first
        list_shown: The path of the list, for the findings

    Returns:
        The findings
    """
    findings: list[check_report.Finding] = []
    command = "python3 utils/scripts/check-build-order.py --check compile-first --build-dir BUILD_DIR --write"
    on_runner = cost_meter.is_github_actions()
    for target in sorted(candidates & largest.keys()):
        compile_ = largest[target]
        giga = compile_.instructions / GIGA
        line = compile_list.names.get(target)
        if line is None and giga > budget.error:
            text = (f"the target {target} compiles {compile_.source} in {giga:.1f} G instructions, more than the "
                    f"error threshold {budget.error:g} G, and the list does not name it.  Ninja then starts the "
                    f"compile after the compiles of the other tests.  Write the list again: {command}")
            if on_runner:
                text += (".  The check gives a warning on a CI runner (GITHUB_ACTIONS is true), because the list "
                         "serves the build host and a runner can use another build of the compiler")
            findings.append(check_report.Finding("warning" if on_runner else "error", list_shown, 0,
                                                 "compile-first", text))
        elif line is not None and giga <= budget.warn:
            findings.append(check_report.judged(
                "warning", list_shown, line, "compile-first",
                f"the list names the target {target}, whose largest compile {compile_.source} takes "
                f"{giga:.1f} G instructions, no more than the warning threshold {budget.warn:g} G.  Write the list "
                f"again: {command}"))
    return findings


def write_list(path: Path, compile_list: CompileList, largest: dict[str, Compile], candidates: set[str],
               budget: check_report.Budget) -> None:
    """Write the list again from the largest compile of each target.

    Args:
        path: The list
        compile_list: The list as it is
        largest: The largest compile of each target with a count
        candidates: The targets of `all` that compile a source
        budget: The row compile-first
    """
    judged = candidates & largest.keys()
    kept = {name for name in compile_list.names
            if name not in judged or largest[name].instructions / GIGA > budget.warn}
    kept |= {target for target in judged if largest[target].instructions / GIGA > budget.error}
    header = "".join(f"{line}\n" for line in compile_list.header)
    body = "".join(f"{name}\n" for name in sorted(kept))
    path.write_text(f"{header}{KIND_PREFIX}{compile_list.kind}\n{body}", encoding="utf-8")


def records_of(build: Path, compile_list: CompileList) -> dict[str, Compile]:
    """Return the largest compile of each target, when the records of the build can judge the list.

    Args:
        build: The build directory
        compile_list: The list

    Returns:
        The largest compile of each target with a count

    Raises:
        NotApplicable: If the build has another kind than the list, or no record holds a count
        ValueError: If the kind of the build or the compile database cannot be read
    """
    kind = cost_meter.read_kind(str(build))
    if kind is None:
        raise ValueError(f"{build}/{cost_meter.KIND_FILE} does not exist, so the kind of the build is not known")
    if kind != compile_list.kind:
        raise NotApplicable(f"the build has the kind {kind}, and the list holds the counts of the kind "
                            f"{compile_list.kind}")
    largest = read_largest(build)
    if not largest:
        raise NotApplicable("no compile record of the build holds an instruction count")
    return largest


def self_test() -> int:
    """Run the cases of the self-test with GITHUB_ACTIONS removed, except in the cases that set it.

    Returns:
        0 when every case holds, 2 otherwise
    """
    with check_report.github_actions(False):
        return self_test_cases()


def plant_object(build: Path, root: Path, target: str, source: str, record: dict[str, object] | None) -> dict[str, str]:
    """Plant one object of a target and its record in a scratch build directory.

    Args:
        build: The scratch build directory
        root: The scratch repository root
        target: The target of the object
        source: The source of the object, relative to the root
        record: The record, or None for no record

    Returns:
        The row of the compile database for the object
    """
    output = f"test/CMakeFiles/{target}.dir/{Path(source).name}.o"
    object_path = build / output
    object_path.parent.mkdir(parents=True, exist_ok=True)
    object_path.write_bytes(b"object")
    if record is not None:
        Path(str(object_path) + cost_meter.RECORD_SUFFIX).write_text(json.dumps(record), encoding="utf-8")
    return {"directory": str(build), "file": str(root / source), "output": output}


ORDER_GRAPH = (
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

CHAIN_GRAPH = (
    "build t/CMakeFiles/listed.dir/listed.cpp.o: CXX_COMPILER__listed_unscanned_Debug listed.cpp\n"
    "build t/listed: CXX_EXECUTABLE_LINKER__listed_Debug t/CMakeFiles/listed.dir/listed.cpp.o\n"
    "build t/CMakeFiles/other.dir/other.cpp.o: CXX_COMPILER__other_unscanned_Debug other.cpp\n"
    "build t/other: CXX_EXECUTABLE_LINKER__other_Debug t/CMakeFiles/other.dir/other.cpp.o\n"
    "build t/CMakeFiles/unit.dir/unit.cpp.o: CXX_COMPILER__unit_unscanned_Debug unit.cpp\n"
    "build unit: phony t/CMakeFiles/unit.dir/unit.cpp.o\n"
    "build compile-first.stamp: CUSTOM_COMMAND || t/listed unit\n"
    "build compile_first: phony compile-first.stamp\n"
    "build all: phony t/listed t/other unit compile_first\n"
    "default all\n")

CUSTOM_GRAPH = (
    "rule CUSTOM_COMMAND\n"
    "  command = $COMMAND\n"
    "build plain.bpf.o | ${cmake_ninja_workdir}plain.bpf.o: CUSTOM_COMMAND /src/plain.bpf.c /src/plain.bpf.c\n"
    "  COMMAND = clang -c /src/plain.bpf.c -o plain.bpf.o\n"
    "  restat = 1\n"
    "\n"
    "  depfile = CMakeFiles/d/after_blank.d\n"
    "build halfway.bpf.o: CUSTOM_COMMAND /src/halfway.bpf.c\n"
    "  depfile = CMakeFiles/d/halfway.d\n"
    "build kept.bpf.o: CUSTOM_COMMAND /src/kept.bpf.c\n"
    "  COMMAND = clang -MD -MF kept.bpf.d -c /src/kept.bpf.c $\n"
    "      -o kept.bpf.o\n"
    "  depfile = CMakeFiles/d/kept.d\n"
    "  deps = gcc\n"
    "build empty.bpf.o: CUSTOM_COMMAND /src/empty.bpf.c\n"
    "  depfile = CMakeFiles/d/empty.d\n"
    "  deps = gcc\n"
    "build stale.bpf.o: CUSTOM_COMMAND /src/stale.bpf.c\n"
    "  depfile = CMakeFiles/d/stale.d\n"
    "  deps = gcc\n"
    "build missing.bpf.o: CUSTOM_COMMAND /src/missing.bpf.c\n"
    "  depfile = CMakeFiles/d/missing.d\n"
    "  deps = gcc\n"
    "build unbuilt.bpf.o: CUSTOM_COMMAND /src/unbuilt.bpf.c\n"
    "  depfile = CMakeFiles/d/unbuilt.d\n"
    "  deps = gcc\n"
    "build gen.c: CUSTOM_COMMAND kept.bpf.o\n"
    "  COMMAND = xxd -i kept.bpf.o > gen.c\n"
    "build lib.o: CXX_COMPILER__lib_unscanned_Debug lib.cpp\n"
    "  DEP_FILE = lib.o.d\n")

# The output of `ninja -t deps` for the built objects of CUSTOM_GRAPH.  The
# log holds no list of missing.bpf.o.
CUSTOM_DEPS_OUTPUT = (
    "kept.bpf.o: #deps 2, deps mtime 1 (VALID)\n"
    "    /src/kept.bpf.c\n"
    "    /src/common.h\n"
    "\n"
    "empty.bpf.o: #deps 1, deps mtime 1 (VALID)\n"
    "    /src/empty.bpf.c\n"
    "\n"
    "stale.bpf.o: #deps 2, deps mtime 1 (STALE)\n"
    "    /src/stale.bpf.c\n"
    "    /src/common.h\n"
    "\n"
    "missing.bpf.o: deps not found\n")


def self_test_cases() -> int:
    """Plant each verdict of each check in scratch files, and check it.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    def built(giga: float) -> dict[str, object]:
        return {"format": cost_meter.RECORD_FORMAT, "step": "compile", "result": "built",
                "cost": {"cpu_s": 1.0, "instructions": int(giga * GIGA)}}

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
    expect("target_of reads the target of an object",
           target_of(Path("/b/test/CMakeFiles/test_a.dir/x/test_a.cpp.o")) == "test_a"
           and target_of(Path("/b/test/x.o")) is None)
    budget = check_report.Budget("compile-first", 15.0, 20.0, "G instructions", "the largest compile of one target")

    with tempfile.TemporaryDirectory(prefix="check-build-order-") as work:
        root = Path(work)
        ninja = root / "build.ninja"
        ninja.write_text(ORDER_GRAPH, encoding="utf-8")
        graph = read_graph(ninja)
        expect("read_graph reads each build statement, a continued line and the default targets",
               len(graph.edges) == 13 and graph.defaults == ["all"]
               and graph.edges[graph.producer["b2.o"]].inputs == ("b2.cpp",)
               and graph.edges[graph.producer["b2.o"]].line == 10)
        findings = evaluate_order(graph, "build.ninja")
        expect("build-order, an error: a custom command with an order-only input that an archive writes, at its "
               "line, with the count of the compiles behind it",
               len(findings) == 1 and findings[0].level == "error" and findings[0].line == 7
               and "gen.c" in findings[0].message and "liba.a" in findings[0].message
               and "2 compile(s)" in findings[0].message)
        expect("build-order, no finding: a command that runs a tool of the build as an explicit input",
               not any("hdr.h" in found.message for found in findings))
        ninja.write_text(ORDER_GRAPH.replace(" || liba.a\n", "\n"), encoding="utf-8")
        expect("build-order, no finding: the same graph with no order-only input on the archive",
               evaluate_order(read_graph(ninja), "build.ninja") == [])

        ninja.write_text(CHAIN_GRAPH, encoding="utf-8")
        chain_graph = read_graph(ninja)
        chain = chain_lengths(chain_graph)
        expect("chain_lengths: three edges for the object of a listed test, two for another test, two for the "
               "object of a listed object library, and one for a link with no consumer",
               chain[chain_graph.producer["t/CMakeFiles/listed.dir/listed.cpp.o"]] == 3
               and chain[chain_graph.producer["t/CMakeFiles/other.dir/other.cpp.o"]] == 2
               and chain[chain_graph.producer["t/CMakeFiles/unit.dir/unit.cpp.o"]] == 2
               and chain[chain_graph.producer["t/other"]] == 1)
        chain_list = CompileList([], "x86_64-debug-asan", {"listed": 4, "unit": 5, "outside": 6})
        found = evaluate_chains(chain_graph, {"listed", "other", "unit"}, chain_list, "compile-first.txt")
        expect("compile-first, an error at the row: a listed object library whose object has two edges",
               len(found) == 1 and found[0].line == 5 and "unit" in found[0].message)
        ninja.write_text(CHAIN_GRAPH.replace("build compile-first.stamp: CUSTOM_COMMAND || t/listed unit\n", ""),
                         encoding="utf-8")
        found = evaluate_chains(read_graph(ninja), {"listed", "other", "unit"}, chain_list, "compile-first.txt")
        expect("compile-first, an error for each listed target when the graph has no stamp",
               [item.line for item in found] == [4, 5])

        ninja.write_text(CUSTOM_GRAPH, encoding="utf-8")
        custom_graph = read_graph(ninja)
        kept_bindings = custom_graph.variables.get(custom_graph.producer["kept.bpf.o"], {})
        plain_bindings = custom_graph.variables.get(custom_graph.producer["plain.bpf.o"], {})
        expect("read_graph reads the bindings of a build statement, also a continued one, and no binding of a rule",
               kept_bindings.get("depfile") == "CMakeFiles/d/kept.d" and kept_bindings.get("deps") == "gcc"
               and "-o kept.bpf.o" in kept_bindings.get("COMMAND", "")
               and not any("command" in bindings for bindings in custom_graph.variables.values()))
        expect("read_graph ends the bindings of a build statement at an empty line",
               set(plain_bindings) == {"COMMAND", "restat"})
        expect("custom_objects takes each custom command whose first output is an object, and no other edge",
               [custom_graph.edges[edge_id].outputs[0] for edge_id in custom_objects(custom_graph)]
               == ["plain.bpf.o", "halfway.bpf.o", "kept.bpf.o", "empty.bpf.o", "stale.bpf.o", "missing.bpf.o",
                   "unbuilt.bpf.o"])
        lists, states = build_census.parse_dependencies(CUSTOM_DEPS_OUTPUT)
        expect("parse_dependencies reads the files and the state of each list of the log",
               lists["kept.bpf.o"] == ["/src/kept.bpf.c", "/src/common.h"] and states["kept.bpf.o"] == "VALID"
               and states["stale.bpf.o"] == "STALE" and states["missing.bpf.o"] == "MISSING"
               and lists["missing.bpf.o"] == [])
        built_objects = {"halfway.bpf.o", "kept.bpf.o", "empty.bpf.o", "stale.bpf.o", "missing.bpf.o"}
        found = evaluate_custom_deps(custom_graph, root, built_objects, lists, states, "build.ninja")
        expect("custom-deps, an error at the line of each edge with no depfile or no `deps = gcc`, also for an "
               "object that is not built, and of each built object whose list is empty of headers, stale or missing",
               [(item.line, item.level) for item in found]
               == [(3, "error"), (8, "error"), (15, "error"), (18, "error"), (21, "error")]
               and "plain.bpf.o" in found[0].message and "DEPFILE" in found[0].message
               and "MISSING" in found[4].message)
        expect("custom-deps, no finding: a built object whose list holds a header, an object that is not built, a "
               "custom command that writes a source, and a compile of CMake",
               not any(name in item.message for item in found
                       for name in ("kept.bpf.o", "unbuilt.bpf.o", "gen.c", "lib.o ")))
        custom_build = root / "custom"
        custom_build.mkdir()
        (custom_build / "build.ninja").write_text(ORDER_GRAPH, encoding="utf-8")
        with contextlib.redirect_stderr(io.StringIO()):
            status = run_custom_deps(custom_build, read_graph(custom_build / "build.ninja"), "build.ninja", None)
        expect("custom-deps does not apply to a build in which no custom command writes an object",
               status == NOT_APPLICABLE)

        build = root / "build"
        build.mkdir()
        rows = [
            plant_object(build, root, "long_unlisted", "test/long_unlisted.cpp", built(25.0)),
            plant_object(build, root, "long_listed", "test/long_listed_a.cpp", built(3.0)),
            plant_object(build, root, "long_listed", "test/long_listed_b.cpp", built(30.0)),
            plant_object(build, root, "short_listed", "test/short_listed.cpp", built(15.0)),
            plant_object(build, root, "middle_listed", "test/middle_listed.cpp", built(18.0)),
            plant_object(build, root, "middle_unlisted", "test/middle_unlisted.cpp", built(20.0)),
            plant_object(build, root, "hit_long", "test/hit_long.cpp",
                         {"format": cost_meter.RECORD_FORMAT, "step": "compile", "result": "hit",
                          "last_cost": {"instructions": int(40 * GIGA)}}),
            plant_object(build, root, "hit_no_count", "test/hit_no_count.cpp",
                         {"format": cost_meter.RECORD_FORMAT, "step": "compile", "result": "hit"}),
            plant_object(build, root, "failed_long", "test/failed_long.cpp", {**built(50.0), "result": "failed"}),
            plant_object(build, root, "no_record", "test/no_record.cpp", None),
            plant_object(build, root, "outside_all", "test/outside_all.cpp", built(60.0)),
        ]
        (build / "compile_commands.json").write_text(json.dumps(rows), encoding="utf-8")
        candidates_text = "\n".join(("long_unlisted", "long_listed", "short_listed", "middle_listed",
                                     "middle_unlisted", "hit_long", "hit_no_count", "failed_long", "no_record"))
        (build / CANDIDATES).write_text(candidates_text + "\n", encoding="utf-8")
        (build / cost_meter.KIND_FILE).write_text("x86_64-debug-asan\n", encoding="utf-8")
        list_path = root / "compile-first.txt"
        list_path.write_text("# the header\n#   with a command\nkind x86_64-debug-asan\nlong_listed\nshort_listed\n"
                             "middle_listed\nabsent_target\n", encoding="utf-8")

        compile_list = read_list(list_path)
        expect("the list reads its header, its kind and the line of each name",
               compile_list.header == ["# the header", "#   with a command"]
               and compile_list.kind == "x86_64-debug-asan" and compile_list.names["short_listed"] == 5)
        largest = read_largest(build, root)
        expect("the largest compile of a target is its object with the most instructions",
               largest["long_listed"] == Compile("test/long_listed_b.cpp", int(30 * GIGA)))
        expect("a ccache hit counts the last_cost block", largest["hit_long"].instructions == int(40 * GIGA))
        expect("no count: a hit with no last_cost, a failed compile and an object with no record",
               not {"hit_no_count", "failed_long", "no_record"} & largest.keys())
        candidates = read_candidates(build)
        findings = evaluate_records(largest, candidates, compile_list, budget, "compile-first.txt")
        expect("compile-first, an error: a target over the error threshold that the list does not name, with its "
               "source",
               any(found.level == "error" and "long_unlisted" in found.message
                   and "test/long_unlisted.cpp" in found.message for found in findings))
        expect("compile-first, an error: a ccache hit over the error threshold that the list does not name",
               any(found.level == "error" and "hit_long" in found.message for found in findings))
        expect("compile-first, a warning at the row of a listed target at or below the warning threshold",
               any(found.level == "warning" and found.line == 5 and "short_listed" in found.message
                   for found in findings))
        expect("compile-first, no finding: a listed target over the error threshold, a listed or unlisted target "
               "between the thresholds, a target outside all, and a listed name with no compile",
               len(findings) == 3 and not any(name in found.message for found in findings
                                              for name in ("long_listed", "middle_listed", "middle_unlisted",
                                                           "outside_all", "absent_target")))
        with check_report.github_actions(True):
            on_runner = evaluate_records(largest, candidates, compile_list, budget, "compile-first.txt")
        expect("compile-first, on a CI runner a target that the list does not name gives a warning that tells why",
               len(on_runner) == 3 and all(found.level == "warning" for found in on_runner)
               and sum("CI runner" in found.message for found in on_runner) == 2)
        expect("records_of judges a build of the kind of the list",
               {name: item.instructions for name, item in records_of(build, compile_list).items()}
               == {name: item.instructions for name, item in largest.items()})
        (build / cost_meter.KIND_FILE).write_text("x86_64-release\n", encoding="utf-8")
        try:
            records_of(build, compile_list)
            expect("records_of does not apply to a build of another kind", False)
        except NotApplicable:
            expect("records_of does not apply to a build of another kind", True)

        write_list(list_path, compile_list, largest, candidates, budget)
        written = read_list(list_path)
        expect("--write adds the targets over the error threshold, removes the listed target at or below the "
               "warning threshold, and keeps the other names, the header and the kind",
               set(written.names) == {"long_listed", "middle_listed", "absent_target", "long_unlisted", "hit_long"}
               and written.header == compile_list.header and written.kind == compile_list.kind)
        expect("compile-first, no finding for a list that --write wrote",
               evaluate_records(largest, candidates, written, budget, "compile-first.txt") == [])

        for label, body in (("no kind row", "a_target\n"), ("two kind rows", "kind a\nkind b\n"),
                            ("a row with a space", "kind a\ntwo words\n"), ("a name on two rows", "kind a\nx\nx\n")):
            list_path.write_text(body, encoding="utf-8")
            try:
                read_list(list_path)
                expect(f"read_list refuses {label}", False)
            except ValueError:
                expect(f"read_list refuses {label}", True)
        try:
            read_candidates(root / "missing")
            expect("read_candidates refuses a build directory with no list of targets", False)
        except ValueError:
            expect("read_candidates refuses a build directory with no list of targets", True)
        try:
            read_largest(root / "missing", root)
            expect("read_largest refuses a build directory with no compile database", False)
        except ValueError:
            expect("read_largest refuses a build directory with no compile database", True)

        warnings_dir = root / "warnings"
        warning_only = [found for found in findings if found.level == "warning"]
        with contextlib.redirect_stdout(io.StringIO()) as printed:
            status = check_report.emit(warning_only, "compile-first", warnings_dir)
        expect("warnings only give exit status 0, a line of the format and a warnings file",
               status == 0 and (warnings_dir / "compile-first.txt").is_file()
               and check_report.parse_line(printed.getvalue().splitlines()[0]) is not None)
        with contextlib.redirect_stdout(io.StringIO()):
            status = check_report.emit([found for found in findings if found.level == "error"], "compile-first",
                                       warnings_dir)
        expect("an error gives exit status 1 and removes the warnings file",
               status == 1 and not (warnings_dir / "compile-first.txt").exists())
    expect("the repository budget table has the row compile-first", "compile-first" in check_report.read_budgets())
    expect("the repository list reads, and it names the walk units of test/layer",
           any(name.startswith("layer_walks_across_headers_") for name in read_list(LIST).names))
    if failures:
        print(f"check-build-order --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-build-order --self-test: every case holds.")
    return 0


def run_compile_first(build: Path, graph: Graph, ninja_shown: str, is_write: bool,
                      warnings_dir: Path | None) -> int:
    """Run the check compile-first, or write the list again.

    Args:
        build: The build directory
        graph: The graph of build.ninja
        ninja_shown: The path of build.ninja, for an input finding
        is_write: True to write the list again
        warnings_dir: The warnings directory, or None

    Returns:
        The exit code
    """
    list_shown = display(LIST)
    try:
        budget = check_report.read_budgets()["compile-first"]
        compile_list = read_list(LIST)
        candidates = read_candidates(build)
    except (ValueError, KeyError, OSError) as exc:
        problem = "the row compile-first is missing from the budget table" if isinstance(exc, KeyError) else str(exc)
        return check_report.emit([check_report.Finding("error", list_shown, 0, "compile-first",
                                                       f"the check cannot read its input: {problem}")],
                                 "compile-first", warnings_dir)
    findings = evaluate_chains(graph, candidates, compile_list, list_shown)
    try:
        largest = records_of(build, compile_list)
    except NotApplicable as exc:
        if is_write:
            print(f"check-build-order: the records cannot write the list: {exc}.", file=sys.stderr)
            return NOT_APPLICABLE
        print(f"check-build-order: the check judges only the graph: {exc}.", file=sys.stderr)
        largest = {}
    except ValueError as exc:
        input_error = check_report.Finding("error", ninja_shown, 0, "compile-first",
                                           f"the check cannot read its input: {exc}")
        if is_write:
            return check_report.emit([input_error], "compile-first", warnings_dir)
        findings.append(input_error)
        largest = {}
    if is_write:
        write_list(LIST, compile_list, largest, candidates, budget)
        print(f"check-build-order: wrote {list_shown}.", file=sys.stderr)
        return 0
    findings += evaluate_records(largest, candidates, compile_list, budget, list_shown)
    print(f"check-build-order: {len(candidates & largest.keys())} targets judged by their records, "
          f"{len(compile_list.names)} names on the list, {len(findings)} finding(s).", file=sys.stderr)
    return check_report.emit(findings, "compile-first", warnings_dir)


def run_custom_deps(build: Path, graph: Graph, ninja_shown: str, warnings_dir: Path | None) -> int:
    """Run the check custom-deps.

    Args:
        build: The build directory
        graph: The graph of build.ninja
        ninja_shown: The path of build.ninja, for the findings
        warnings_dir: The warnings directory, or None

    Returns:
        The exit code
    """
    objects = custom_objects(graph)
    if not objects:
        print("check-build-order: the check does not apply: no custom command of build.ninja writes an object.",
              file=sys.stderr)
        return NOT_APPLICABLE
    built = sorted({graph.edges[edge_id].outputs[0] for edge_id in objects
                    if (build / graph.edges[edge_id].outputs[0]).is_file()})
    lists: dict[str, list[str]] = {}
    states: dict[str, str] = {}
    if built:
        ninja = build_census.cache_value(build, "CMAKE_MAKE_PROGRAM") or "ninja"
        try:
            lists, states = build_census.parse_dependencies(
                build_census.run_tool([ninja, "-C", str(build), "-t", "deps", *built]))
        except build_census.CensusError as exc:
            return check_report.emit([check_report.Finding("error", ninja_shown, 0, "custom-deps",
                                                           f"the check cannot read the dependency log: {exc}")],
                                     "custom-deps", warnings_dir)
    findings = evaluate_custom_deps(graph, build, set(built), lists, states, ninja_shown)
    print(f"check-build-order: {len(objects)} object(s) of custom commands, {len(built)} of them built, "
          f"{len(findings)} finding(s).", file=sys.stderr)
    return check_report.emit(findings, "custom-deps", warnings_dir)


def main(argv: list[str]) -> int:
    """Run one check, the write of the list, or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    parser = argparse.ArgumentParser(prog="check-build-order.py", description=__doc__.split("\n", 1)[0])
    check_report.add_arguments(parser)
    parser.add_argument("--check", choices=CHECKS)
    parser.add_argument("--build-dir", type=Path, help="the build directory")
    parser.add_argument("--write", action="store_true",
                        help="with --check compile-first, write utils/scripts/compile-first.txt again from the records")
    parser.add_argument("--self-test", action="store_true", help="plant each verdict of each check")
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return self_test()
    if arguments.check is None or arguments.build_dir is None:
        parser.error("give --check CHECK --build-dir BUILD_DIR, or --self-test")
    if arguments.write and arguments.check != "compile-first":
        parser.error("--write applies only to --check compile-first")
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
    if arguments.check == "compile-first":
        return run_compile_first(build, graph, display(ninja), arguments.write, arguments.warnings_dir)
    if arguments.check == "custom-deps":
        return run_custom_deps(build, graph, display(ninja), arguments.warnings_dir)
    findings = evaluate_order(graph, display(ninja))
    print(f"check-build-order: {len(graph.edges)} edges, {len(findings)} finding(s).", file=sys.stderr)
    return check_report.emit(findings, arguments.check, arguments.warnings_dir)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
