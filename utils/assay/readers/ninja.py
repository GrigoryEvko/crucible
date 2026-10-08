"""ninja — the build graph, the build log and the dependency log of one build directory.

THE BUILD GRAPH
    graph(build_dir) reads BUILD_DIR/build.ninja through the lexical syntax
    of the Ninja manual: the build statements, their variables and the
    default targets.  A line that ends with an unescaped '$' continues on the
    next line.  An indented line after a build statement is a variable of
    that statement, `name = value`, until a line that is empty or not
    indented.  The reader does not read an included file, because CMake
    writes only rules there, and it does not expand a variable.

    edge_kind() gives the kind of an edge from its rule.  A compile has a
    rule with the prefix CXX_COMPILER__ or C_COMPILER__.  A link or an
    archive has a rule with the shape <LANG>_<KIND>_LINKER__.

THE BUILD LOG
    log(build_dir) reads BUILD_DIR/.ninja_log.  Its first line gives the
    version, `# ninja log vN`.  In the versions 5 to 7, a record has five
    fields with a tab between them:
      * The start and the end, in milliseconds after the start of Ninja
      * The time of the output
      * The output
      * A hash of the command.
    Ninja skips a line with fewer fields, and the reader skips it too.  Ninja
    writes the log again at a start when it holds more than three records
    for each output.  The new log has only the last record of each output,
    in no sequence.  An offset into the file then does not find the records
    of one run.  log() keeps the last record of each output.  ran_outputs()
    compares two such reads: an output with a new or changed record ran
    between the two reads.

THE DEPENDENCY LOG
    deps(build_dir, outputs) runs `ninja -t deps` with the ninja of the CMake
    cache, CMAKE_MAKE_PROGRAM (assay.readers.cmake), and it checks the exit
    status.  Each output gets one Deps: the state of its list, the files of
    the list, and the reason of a state that is not VALID:
      VALID    the list is current
      STALE    the output is newer than its list, or the output is missing
      MISSING  the dependency log holds no list of the output.  The build
               log tells why.  Ninja ran the edge of the output and recorded
               no list, or this Ninja did not build the output in this
               build directory
    With no output, deps() gives each list of the log that an edge of the
    graph can use.  inputs(build_dir, target) runs `ninja -t inputs TARGET`.

ERRORS
    NinjaError names the file or the command, and the program that writes
    the file.  NotNinja, a subclass, tells that a generator other than Ninja
    made the build, or that no configure wrote build.ninja.

THE COST
    graph() reads build.ninja of the tree in about 0.5 s.  log() reads
    .ninja_log in less than 0.1 s.  deps() starts ninja, which reads
    build.ninja and .ninja_deps, about 0.2 s for the tree, and it reads the
    build log only for a MISSING list.

`guard selftest tests.readers.test_ninja` does a test of the module.
"""

import enum
import os
import re
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import proc
from assay.readers import cmake

GRAPH_NAME = "build.ninja"
LOG_NAME = ".ninja_log"
DEPS_NAME = ".ninja_deps"
LOG_HEAD = "# ninja log v"
# The versions of the build log that have the five fields of THE BUILD LOG.
LOG_VERSIONS = range(5, 8)
LOG_FIELDS = 5
GRAPH_WRITER = "CMake writes it at each configure of the build directory, for example `cmake --preset default`"
LOG_WRITER = "Ninja writes it at each build"
COMPILE_RULE = re.compile(r"(?:CXX|C)_COMPILER__")
LINK_RULE = re.compile(r"[A-Z]+_(?:EXECUTABLE|STATIC_LIBRARY|SHARED_LIBRARY|MODULE_LIBRARY)_LINKER__")
ARCHIVE_RULE = re.compile(r"[A-Z]+_STATIC_LIBRARY_LINKER__")
# The rules of the CMake run of Ninja and of the glob check of CMake.  The glob
# check runs at each start of Ninja, before each other edge.
CMAKE_RULES = frozenset({"RERUN_CMAKE", "VERIFY_GLOBS"})
CUSTOM_RULE = "CUSTOM_COMMAND"
PHONY_RULE = "phony"
# The two shapes of the first line of one list of `ninja -t deps`.
DEPS_MARK = ": #deps "
NOT_FOUND = ": deps not found"


class NinjaError(Exception):
    """A file of Ninja cannot be read, or a tool of Ninja fails."""


class NotNinja(NinjaError):
    """A generator other than Ninja made the build, or no configure wrote build.ninja."""


class DepsState(enum.StrEnum):
    """The state of the list of one output in the dependency log (THE DEPENDENCY LOG)."""

    VALID = "VALID"
    STALE = "STALE"
    MISSING = "MISSING"


@dataclass(frozen=True, slots=True)
class Edge:
    """One build statement of build.ninja.

    `inputs` holds each explicit and each implicit input, and `order_only`
    holds each input after `||`.
    """

    line: int
    rule: str
    outputs: tuple[str, ...]
    inputs: tuple[str, ...]
    order_only: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class Graph:
    """The edges of build.ninja, the edge that writes each output, the default targets, and the variables of each edge.

    `variables` holds the variables of each edge that has one, by the index of the edge.
    """

    edges: tuple[Edge, ...]
    producer: Mapping[str, int]
    defaults: tuple[str, ...]
    variables: Mapping[int, Mapping[str, str]]


@dataclass(frozen=True, slots=True)
class LogRecord:
    """The last record of one output in the build log."""

    start_ms: int
    end_ms: int
    mtime: int
    command_hash: str


@dataclass(frozen=True, slots=True)
class Deps:
    """The list of one output in the dependency log: its state, its files, and why a state is not VALID."""

    state: DepsState
    files: tuple[str, ...]
    why: str = ""


def graph_file(build_dir: Path) -> Path:
    """Return the path of build.ninja of a build directory.  The file can be missing."""
    return build_dir / GRAPH_NAME


def log_file(build_dir: Path) -> Path:
    """Return the path of the build log of a build directory.  The file can be missing."""
    return build_dir / LOG_NAME


# ── The build graph ────────────────────────────────────────────────────────


def _split_paths(text: str) -> list[str]:
    """Divide a list of Ninja paths at each space that is not escaped, and remove the escapes.

    Complexity: linear in the length of the text.
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


def _find_unescaped(text: str, token: str) -> int:
    """Return the index of the first token that no '$' escapes, or -1 when the text holds none."""
    index = 0
    while index < len(text):
        if text[index] == "$":
            index += 2
            continue
        if text.startswith(token, index):
            return index
        index += 1
    return -1


def _parse_build_line(path: str, line: int, body: str) -> Edge:
    """Read one build statement, the text after `build `.

    Raises:
        NinjaError: If the statement has no ':' or no rule
    """
    colon = _find_unescaped(body, ":")
    if colon < 0:
        raise NinjaError(f"{path}:{line}: the build statement has no ':'.  {GRAPH_WRITER}")
    outputs_text, rest = body[:colon], body[colon + 1 :]
    implicit_outputs = _find_unescaped(outputs_text, "|")
    if implicit_outputs >= 0:
        outputs_text = outputs_text[:implicit_outputs] + " " + outputs_text[implicit_outputs + 1 :]
    validations = _find_unescaped(rest, "|@")
    if validations >= 0:
        rest = rest[:validations]
    order_only_at = _find_unescaped(rest, "||")
    order_only_text = ""
    if order_only_at >= 0:
        rest, order_only_text = rest[:order_only_at], rest[order_only_at + 2 :]
    parts = _split_paths(rest)
    if not parts:
        raise NinjaError(f"{path}:{line}: the build statement has no rule.  {GRAPH_WRITER}")
    inputs = tuple(part for part in parts[1:] if part != "|")
    return Edge(line, parts[0], tuple(_split_paths(outputs_text)), inputs, tuple(_split_paths(order_only_text)))


def graph(build_dir: Path) -> Graph:
    """Return the build statements, their variables and the default targets of build.ninja (THE BUILD GRAPH).

    Complexity: linear in the length of the file.

    Raises:
        NotNinja: If the build directory has no build.ninja
        NinjaError: If the file cannot be read, or a build statement has no ':' or no rule
    """
    path = graph_file(build_dir)
    try:
        text = path.read_text(encoding="utf-8", errors="surrogateescape")
    except FileNotFoundError:
        raise NotNinja(
            f"{path} is missing: a generator other than Ninja made the build, or no configure ran.  {GRAPH_WRITER}"
        ) from None
    except OSError as problem:
        raise NinjaError(f"{path} cannot be read: {problem}") from None
    edges: list[Edge] = []
    producer: dict[str, int] = {}
    defaults: list[str] = []
    variables: dict[int, dict[str, str]] = {}
    pending = ""
    pending_line = 0
    variable_edge: int | None = None
    for number, raw in enumerate(text.split("\n"), start=1):
        if not pending:
            pending_line = number
        trailing = len(raw) - len(raw.rstrip("$"))
        if trailing % 2 == 1:
            pending += raw[:-1]
            continue
        statement = pending + raw
        pending = ""
        if statement[:1] in (" ", "\t") and statement.strip():
            name, separator, value = statement.partition("=")
            if variable_edge is not None and separator:
                variables.setdefault(variable_edge, {})[name.strip()] = value.strip()
            continue
        variable_edge = None
        if statement.startswith("default "):
            defaults.extend(_split_paths(statement[len("default ") :]))
        elif statement.startswith("build "):
            edge = _parse_build_line(str(path), pending_line, statement[len("build ") :])
            variable_edge = len(edges)
            for output in edge.outputs:
                producer[output] = len(edges)
            edges.append(edge)
    return Graph(tuple(edges), producer, tuple(defaults), variables)


def edge_kind(rule: str) -> str:
    """Return the kind of an edge from its rule.

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


def plant_graph(build_dir: Path, text: str) -> Path:
    """Write a planted build.ninja into a directory, for a test, and return its path."""
    path = graph_file(build_dir)
    path.write_text(text, encoding="utf-8")
    return path


# ── The build log ──────────────────────────────────────────────────────────


def log(build_dir: Path) -> dict[str, LogRecord]:
    """Return the last record of each output in the build log, by the absolute normalized path of the output.

    A build directory with no build log, or with an empty one, gives no
    record.  A line with fewer than five fields, or with a time that is not a
    number, is not a record.  Complexity: linear in the lines of the log.

    Raises:
        NinjaError: If the log cannot be read, its first line is not a version line, or its version is not 5 to 7
    """
    path = log_file(build_dir)
    try:
        text = path.read_text(encoding="utf-8", errors="surrogateescape")
    except FileNotFoundError:
        return {}
    except OSError as problem:
        raise NinjaError(f"{path} cannot be read: {problem}") from None
    lines = text.splitlines()
    if not lines:
        return {}
    head = lines[0]
    version = head[len(LOG_HEAD) :] if head.startswith(LOG_HEAD) else ""
    if not version.isdigit():
        raise NinjaError(f"{path} does not start with the line `{LOG_HEAD}N`, so it is not a build log.  {LOG_WRITER}")
    if int(version) not in LOG_VERSIONS:
        raise NinjaError(
            f"{path} has the version {version}, and the reader reads the versions {LOG_VERSIONS.start} to "
            f"{LOG_VERSIONS.stop - 1}.  Build the directory again with the ninja of CMAKE_MAKE_PROGRAM, or give the "
            f"reader the new version"
        )
    records: dict[str, LogRecord] = {}
    for raw in lines[1:]:
        fields = raw.split("\t")
        if len(fields) < LOG_FIELDS:
            continue
        start, end, mtime, output, command_hash = fields[:LOG_FIELDS]
        try:
            record = LogRecord(int(start), int(end), int(mtime), command_hash)
        except ValueError:
            continue
        records[os.path.normpath(os.path.join(build_dir, output))] = record
    return records


def ran_outputs(before: Mapping[str, LogRecord], after: Mapping[str, LogRecord]) -> tuple[str, ...]:
    """Return each output with a new or a changed record, sorted.  A rewrite of the log adds no output."""
    return tuple(sorted(output for output, record in after.items() if before.get(output) != record))


# ── The tools of Ninja ─────────────────────────────────────────────────────


def _ninja_program(build_dir: Path) -> str:
    """Return the ninja of the CMake cache of a build directory.

    Raises:
        NotNinja: If the generator of the build is not Ninja
        NinjaError: If the cache cannot be read, or it names no ninja
    """
    try:
        entries = cmake.cache(build_dir)
    except cmake.CmakeError as problem:
        raise NinjaError(
            f"the reader does not know the ninja of {build_dir}, because its cache cannot be read: {problem}"
        ) from None
    generator = entries.get("CMAKE_GENERATOR")
    if generator is None or "Ninja" not in generator.value:
        name = "no generator" if generator is None else f"the generator {generator.value}"
        raise NotNinja(f"the cache of {build_dir} names {name}, and only Ninja writes a dependency log")
    program = entries.get("CMAKE_MAKE_PROGRAM")
    if program is None or not program.value:
        raise NinjaError(f"the cache of {build_dir} names no ninja in CMAKE_MAKE_PROGRAM.  {cmake.AGAIN}")
    return program.value


def _run_tool(build_dir: Path, words: Sequence[str]) -> str:
    """Run one tool of the ninja of a build directory, check its exit status, and return its standard output.

    A byte of the output that is not UTF-8 stays as a surrogate escape, and a
    path in the output then names its file.

    Raises:
        NotNinja: If the generator of the build is not Ninja
        NinjaError: If the cache names no ninja, or the tool cannot start, runs too long or fails
    """
    command = proc.Command([_ninja_program(build_dir), "-C", str(build_dir), "-t", *words], timeout_s=proc.TOOL_S)
    try:
        return proc.decoded(proc.require(proc.run(command)).stdout, "surrogateescape")
    except proc.CannotStart as problem:
        raise NinjaError(f"the ninja of {build_dir} cannot start: {problem.error}") from None
    except proc.ToolFailed as problem:
        raise NinjaError(str(problem)) from None


def inputs(build_dir: Path, target: str) -> tuple[str, ...]:
    """Return each input of a target and of its inputs, as `ninja -t inputs` gives it, one path for each line.

    Raises:
        NotNinja: If the generator of the build is not Ninja
        NinjaError: If the tool fails, for example for a target that the graph does not hold
    """
    return tuple(line for line in _run_tool(build_dir, ("inputs", target)).splitlines() if line)


def _parse_deps(text: str) -> list[tuple[str, DepsState, tuple[str, ...]]]:
    """Read the output of `ninja -t deps`: the output, the state and the files of each list, in the sequence of the text.

    Complexity: linear in the length of the text.

    Raises:
        NinjaError: If the first line of a list does not have a shape of the tool
    """
    lists: list[tuple[str, DepsState, list[str]]] = []
    for line in text.splitlines():
        if not line:
            continue
        if line[0] in " \t":
            if not lists:
                raise NinjaError(f"`ninja -t deps` gave the file {line.strip()} before the first output")
            lists[-1][2].append(line.strip())
            continue
        if line.endswith(NOT_FOUND):
            lists.append((line[: -len(NOT_FOUND)], DepsState.MISSING, []))
            continue
        mark = line.rfind(DEPS_MARK)
        state = line[line.rfind("(") + 1 : -1] if line.endswith(")") else ""
        if mark < 0 or state not in (DepsState.VALID, DepsState.STALE):
            raise NinjaError(f"`ninja -t deps` gave the line `{line}`, which does not have a shape of the tool")
        lists.append((line[:mark], DepsState(state), []))
    return [(output, state, tuple(files)) for output, state, files in lists]


def _why(build_dir: Path, output: str, state: DepsState, records: Mapping[str, LogRecord]) -> str:
    """Return why the list of one output is not VALID (THE DEPENDENCY LOG), or an empty text for a VALID list."""
    if state == DepsState.VALID:
        return ""
    path = os.path.normpath(os.path.join(build_dir, output))
    if state == DepsState.STALE:
        if not os.path.lexists(path):
            return "the output is missing, and the list is of a previous build"
        return "the output is newer than its list"
    if path in records:
        return (
            f"Ninja ran the edge of the output ({LOG_NAME} holds its record) and recorded no list in {DEPS_NAME}.  "
            f"The edge has no `deps` variable, or a different ninja wrote {DEPS_NAME}"
        )
    return (
        f"{LOG_NAME} holds no record of the output: this Ninja did not build it in this build directory.  A "
        f"different build wrote the output, or no build made it"
    )


def deps(build_dir: Path, outputs: Sequence[str] = ()) -> dict[str, Deps]:
    """Return the list of each output in the dependency log of a build directory (THE DEPENDENCY LOG).

    Args:
        build_dir: The build directory
        outputs: The outputs, as the graph names them.  With no output, each list that an edge of the graph can use

    Returns:
        The list of each output, by the name of the call, or by the name of the log for a call with no output

    Raises:
        NotNinja: If the generator of the build is not Ninja
        NinjaError: If the tool fails, for example for an output that the graph does not hold.  Also if the output
            of the tool or the build log cannot be read
    """
    parsed = _parse_deps(_run_tool(build_dir, ("deps", *outputs)))
    if outputs and len(parsed) != len(outputs):
        raise NinjaError(f"`ninja -t deps` gave {len(parsed)} lists for {len(outputs)} outputs of {build_dir}")
    records = log(build_dir) if any(state == DepsState.MISSING for _, state, _ in parsed) else {}
    lists = {output: Deps(state, files, _why(build_dir, output, state, records)) for output, state, files in parsed}
    if not outputs:
        return lists
    found: dict[str, Deps] = {}
    for name in outputs:
        listed = lists.get(name) or lists.get(os.path.normpath(name))
        if listed is None:
            raise NinjaError(f"`ninja -t deps` gave no list for the output {name} of {build_dir}")
        found[name] = listed
    return found
