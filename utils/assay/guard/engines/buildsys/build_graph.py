"""build-graph — the graph of build.ninja: the pools, the order of the edges, the custom compiles and the linker.

The rule reads build.ninja and each file that it includes through
BuildContext.graph(), and the dependency log of Ninja through
assay.readers.ninja.  One run judges the four parts of the graph that
follow, and it gives the findings of each part.

THE POOLS
    cmake/JobPools.cmake gives Ninja the pool compile and the pool link.  The
    depth of each pool comes from the job count of the host at each
    configure.  Each compile, and each custom command that compiles an object,
    is in the pool compile.  Each link and each archive is in the pool link.
    Ninja runs at most the depth of a pool of its edges at one time, also with
    a larger -j.  On 2026-10-11, a scratch project with 24 compiles and a pool
    of the depth 2 built at -j 64.  It ran at most 2 compiles at one time.  The
    rule does not read the depth of a pool, because the host gives it.  The
    rule gives:
      `no-pool`       no file of the graph has the pool compile or the pool
                      link.  The entity is the pool
      `outside-pool`  a compile, or a custom command with a first output that
                      ends with .o (custom_objects()), is not in the pool
                      compile.  Or a link or an archive is not in the pool
                      link.  One finding gives the number of such edges for
                      each kind of edge and each pool, and the first edge

THE ORDER
    No compile waits for a link or an archive through an input after `||`
    (an `order-only` input).

    Ninja starts an edge only when each input of the edge is there, also each
    input after `||`.  Of the edges that can start, it starts first the edge
    with the longest chain of edges to a target of the build.  When two edges
    have the same chain, it starts the edge that comes first in build.ninja.
    It does not read the time of a job.

    CMake gives each compile an input after `||`.  It is a phony edge that
    goes to the custom commands of the target and of the targets that it
    links.  A custom command can write a header.  The Ninja generator also
    gives a custom command such an input on each library that its target
    links, unless the command takes DEPENDS_EXPLICIT_ONLY.  Each compile
    behind such a command then waits for the link or the archive of that
    library.

    For each compile edge, the rule follows each input to the edge that
    writes the input, and then each input of that edge.  It does not follow
    the inputs of a link or an archive.  It gives:
      `waits-for-link`  an input after `||` that a link or an archive writes,
                        at the line of the edge that has the input.  The
                        message gives the number of compiles that wait for it
    An explicit or an implicit input that a link writes gives no finding.  A
    command can run a tool of the build to write a file that a compile reads.
    The entity is the output of the link or the archive.

THE CUSTOM OBJECTS
    Each object that a custom command compiles has the headers of its compile
    in the dependency log of Ninja.

    A custom command can compile a source, as the BPF programs of
    cmake/CrucibleBpf.cmake do.  With Ninja and no depfile, an edit of a
    header does not compile the object again.  The command must write a
    depfile, and add_custom_command must take the option DEPFILE.  CMake then
    gives the edge the variables `depfile` and `deps = gcc`.  Ninja then moves
    the list of the depfile into its dependency log.  The rule gives:
      `no-depfile`  an edge of the rule CUSTOM_COMMAND with a first output
                    that ends with `.o`, and with no variable `depfile` or no
                    variable `deps = gcc`, at its line
      `no-header`   an object of such an edge with a depfile that is on the
                    disk.  Its list in the dependency log is not VALID, or it
                    holds no header: a file that is not an input of the edge
    The rule does not read the list of an object that is not on the disk,
    because a build can make only some targets.  The entity is the object.  A
    build with no custom command that writes an object, for example a build
    with CRUCIBLE_HAVE_BPF off, gives no finding of this part.

THE LINKER
    Each link of the tree runs mold (cmake/Linker.cmake).  The compiler gets
    -fuse-ld=mold, -B with the bin directory of the pinned mold, and the
    linker options --threads=N and --no-fork.  N is the limit of the rule in
    utils/registry.txt, the number of threads of one link.  With its fork,
    mold links in a child process, and the parent ends before the child.
    wait4 of the build launcher then measures only the compiler driver.  With
    --no-fork, the build launcher measures the link.  The rule reads the
    variables FLAGS and LINK_FLAGS of each link edge, in the sequence of the
    command of CMake (link_setting()).  It reads a linker option from -Wl,A,B
    and from -Xlinker A.  It gives:
      `linker`            the last -fuse-ld of the edge is not mold, or the
                          edge has no -fuse-ld
      `linker-directory`  no -B prefix of the edge holds ld.mold.  GCC then
                          runs an ld.mold of PATH.  The rule reads a prefix
                          relative to the build directory, as the link does
      `threads`           the last thread count that the edge gives mold is
                          not the limit of the rule.  --no-threads is one
                          thread, and --threads with no count is the default
                          of mold, one thread for each CPU
      `fork`              the edge does not give mold --no-fork after its
                          last --fork
    One finding gives the number of the edges of each kind and value, and
    the first edge.  An archive has no linker, and this part does not read
    it.  The configure step refuses a mold of a different version.  For that
    reason, the rule does not run ld.mold.

THE INPUT
    The rule gives `input` when it cannot read build.ninja, a file that
    build.ninja includes or the dependency log.  It also gives `input` when
    it cannot divide a variable of a link edge into words, and when the
    limit of the rule is not an integer above 0.  With no graph, the rule
    judges no part.

THE MISSING DEPENDENCIES
    missing_dependencies(build) gives each file of the dependency log that is
    not on the disk.  The rule reconfigure-noop calls it, because such a file
    makes its edge run again on each build.

THE PLANTS
    The plants of the rule start from the tree of its control control-graph.
    That tree holds a planted build with the two pools, a custom object with
    a depfile, the linker flags of cmake/Linker.cmake on each link, and a
    planted registry with the limit 4.  The -B prefix of each link is the
    directory planted-mold/ of the build, which holds a stand-in for ld.mold.
    plant_context() copies that tree first, and then the files of the plant
    over it.  A plant then holds only the files that it changes.  A plant
    with a build.ninja of its own gives each edge that it does not plant the
    pool and the flags of the control.

Complexity: one read of build.ninja and its included files, linear in their length, and one run of `ninja -t deps`
when a custom object with a depfile is on the disk.
"""

import os
import shlex
import shutil
from collections import Counter, defaultdict
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import report as check_report
from assay.guard.engines.buildsys import planted
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import ninja as ninja_reader

RULE = "build-graph"
# The path of the registry that gives the limit of the rule, in a finding.
REGISTRY_FILE = "utils/registry.txt"
# The tree of each plant of the rule (THE PLANTS).
BASE_CONTROL = "control-graph"
# The suffix of an object that a custom command compiles.
OBJECT_SUFFIX = ".o"
# The value of the variable `deps` that makes Ninja move a depfile into its dependency log.
GCC_DEPS = "gcc"
# The edge kinds of assay.readers.ninja that a link or an archive has.
LINKED_KINDS = frozenset({"link", "archive"})
# The word of an edge with no pool, in a finding.
NO_POOL = "no pool"
# The linker of the tree, the option of the compiler that selects it, and the program that GCC runs for it.
LINKER = "mold"
LINKER_OPTION = "-fuse-ld="
LINKER_PROGRAM = "ld.mold"
# The option of the compiler that adds a prefix to the search of its programs.
PREFIX_OPTION = "-B"
# The two forms that give the linker options: -Wl,A,B and -Xlinker A.
LINKER_PASS = "-Wl,"
LINKER_WORD = "-Xlinker"
# The options of mold that give its thread count, the option of one thread, and the options of the fork.
THREAD_COUNT_OPTIONS = ("--threads=", "--thread-count=")
THREAD_COUNT_WORD = "--thread-count"
DEFAULT_THREADS = "--threads"
ONE_THREAD = "--no-threads"
FORK = "--fork"
NO_FORK = "--no-fork"
# The thread count of a link with --threads and no count, in a finding.
DEFAULT_COUNT = "the default of mold"
# The variables of a link edge that hold the words of the compiler, in the sequence of the command of CMake.
LINK_VARIABLES = ("FLAGS", "LINK_FLAGS")
# The characters that a '$' escapes in a value of a Ninja variable.
NINJA_ESCAPED = frozenset("$ :")
# The repair of a link edge (THE LINKER).
LINKER_REPAIR = (
    "Configure the build again: cmake/Linker.cmake gives each link -fuse-ld=mold, -B with the bin directory of the "
    "pinned mold, the thread count of the rule build-graph and --no-fork"
)


@dataclass(frozen=True, slots=True)
class Pool:
    """One Ninja pool of cmake/JobPools.cmake: its name, its edges and the repair of an edge outside it (THE POOLS).

    `kinds` are the edge kinds of assay.readers.ninja in the pool.  With
    `has_custom_objects`, the pool also holds each custom command that
    compiles an object.
    """

    name: str
    kinds: frozenset[str]
    has_custom_objects: bool
    repair: str


POOLS = (
    Pool(
        "compile",
        frozenset({"compile"}),
        True,
        "Create the target after cmake/JobPools.cmake, or give the custom command JOB_POOL ${CRUCIBLE_COMPILE_POOL}",
    ),
    Pool(
        "link",
        LINKED_KINDS,
        False,
        "Create the target after cmake/JobPools.cmake, which gives each link and each archive the pool link",
    ),
)


def error(path: str, line: int, message: str, kind: str, entity: str) -> check_report.Finding:
    """Return one error of the rule."""
    return check_report.Finding("error", path, line, RULE, message, kind, entity)


def is_linked(rule: str) -> bool:
    """Tell if an edge of a rule is a link or an archive."""
    return ninja_reader.edge_kind(rule) in LINKED_KINDS


def is_compile(rule: str) -> bool:
    """Tell if an edge of a rule is a compile of CMake."""
    return ninja_reader.edge_kind(rule) == "compile"


def custom_objects(graph: ninja_reader.Graph) -> list[int]:
    """Return the index of each edge of a custom command with an object as its first output, in the file sequence.

    Complexity: linear in the edges.
    """
    return [
        edge_id
        for edge_id, edge in enumerate(graph.edges)
        if edge.rule == ninja_reader.CUSTOM_RULE and edge.outputs and edge.outputs[0].endswith(OBJECT_SUFFIX)
    ]


def pooled_edges(graph: ninja_reader.Graph, pool: Pool, objects: Sequence[int]) -> list[tuple[str, int]]:
    """Return the kind and the index of each edge of the graph that a pool holds, in the file sequence.

    `objects` are the edges of custom_objects().  Complexity: linear in the edges.
    """
    found = [
        (ninja_reader.edge_kind(edge.rule), edge_id)
        for edge_id, edge in enumerate(graph.edges)
        if ninja_reader.edge_kind(edge.rule) in pool.kinds
    ]
    if pool.has_custom_objects:
        found += [("custom object", edge_id) for edge_id in objects]
    return sorted(found, key=lambda item: item[1])


def pool_findings(graph: ninja_reader.Graph, shown: str, objects: Sequence[int]) -> list[check_report.Finding]:
    """Give the errors of the two pools: a pool that the graph does not have, and an edge outside its pool (THE POOLS).

    Complexity: linear in the edges.
    """
    found: list[check_report.Finding] = []
    for pool in POOLS:
        if pool.name not in graph.pools:
            found.append(
                error(
                    shown,
                    0,
                    f"build.ninja and the files that it includes have no pool {pool.name}.  Configure the build "
                    f"again: cmake/JobPools.cmake gives the pool a depth from the host",
                    "no-pool",
                    pool.name,
                )
            )
        unpooled: dict[tuple[str, str], list[int]] = defaultdict(list)
        for kind, edge_id in pooled_edges(graph, pool, objects):
            actual = graph.variables.get(edge_id, {}).get("pool", "")
            if actual != pool.name:
                unpooled[(kind, actual)].append(edge_id)
        for (kind, actual), edge_ids in sorted(unpooled.items()):
            first = graph.edges[edge_ids[0]]
            place = NO_POOL if not actual else f"the pool {actual}"
            found.append(
                error(
                    shown,
                    first.line,
                    f"{len(edge_ids)} {kind} edge(s) are in {place} and not in the pool {pool.name}, and the depth "
                    f"of the pool does not limit them.  The first writes {first.outputs[0]}.  {pool.repair}",
                    "outside-pool",
                    f"{kind} in {place}",
                )
            )
    return found


def order_only_waits(graph: ninja_reader.Graph) -> dict[tuple[int, int], int]:
    """Find each input after `||` that a link or an archive writes, behind each compile (THE ORDER).

    Complexity: linear in the edges and their inputs, with one memo entry for each edge.

    Returns:
        For each pair (the edge with the input after `||`, the link or the archive), the number of compiles that
        wait for it
    """
    memo: dict[int, frozenset[tuple[int, int]]] = {}

    def waits_of(edge_id: int) -> frozenset[tuple[int, int]]:
        # An explicit stack, because a chain of phony edges can be deeper than the recursion limit.
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
                if producer is not None and producer not in memo and not is_linked(graph.edges[producer].rule):
                    memo.setdefault(current, frozenset())
                    stack.append((producer, 0))
                continue
            found: set[tuple[int, int]] = set()
            for index, name in enumerate(inputs):
                producer = graph.producer.get(name)
                if producer is None:
                    continue
                if is_linked(graph.edges[producer].rule):
                    if index >= len(edge.inputs):
                        found.add((current, producer))
                else:
                    found |= memo.get(producer, frozenset())
            memo[current] = frozenset(found)
        return memo[edge_id]

    counts: Counter[tuple[int, int]] = Counter()
    for edge_id, edge in enumerate(graph.edges):
        if is_compile(edge.rule):
            for pair in waits_of(edge_id):
                counts[pair] += 1
    return dict(counts)


def order_findings(graph: ninja_reader.Graph, shown: str) -> list[check_report.Finding]:
    """Give one error for each input after `||` that a link or an archive writes, behind a compile (THE ORDER)."""
    found: list[check_report.Finding] = []
    for (waiter, link), count in sorted(order_only_waits(graph).items()):
        waiting = graph.edges[waiter]
        written = graph.edges[link].outputs[0]
        found.append(
            error(
                shown,
                waiting.line,
                f"the edge that writes {waiting.outputs[0]} has the input {written} after `||`, and a link or an "
                f"archive writes it.  {count} compile(s) wait for that link or archive.  If the command reads only "
                f"the files that its DEPENDS name, give add_custom_command the option DEPENDS_EXPLICIT_ONLY",
                "waits-for-link",
                written,
            )
        )
    return found


def has_depfile(graph: ninja_reader.Graph, edge_id: int) -> bool:
    """Tell if an edge has the variable `depfile` and the variable `deps = gcc`."""
    variables = graph.variables.get(edge_id, {})
    return bool(variables.get("depfile")) and variables.get("deps") == GCC_DEPS


def no_depfile(edge: ninja_reader.Edge, shown: str) -> check_report.Finding:
    """Return the error of a custom command that compiles an object and gives Ninja no depfile."""
    output = edge.outputs[0]
    return error(
        shown,
        edge.line,
        f"the custom command that writes {output} has no variable `depfile` with `deps = {GCC_DEPS}`.  The dependency "
        f"log of Ninja then holds no header of the compile, and an edit of a header does not compile {output} again.  "
        f"Give the compiler -MD -MF FILE, and give add_custom_command the option DEPFILE FILE",
        "no-depfile",
        output,
    )


def no_header(edge: ninja_reader.Edge, listed: ninja_reader.Deps, shown: str) -> check_report.Finding:
    """Return the error of an object of a custom command when its list in the dependency log holds no header."""
    output = edge.outputs[0]
    why = f" ({listed.why})" if listed.why else ""
    return error(
        shown,
        edge.line,
        f"the dependency log of Ninja holds no header of {output}, and the state of its list is {listed.state}{why}, "
        f"although build.ninja gives the command a depfile.  Make sure that the depfile names each header that the "
        f"compile reads, and build {output} again",
        "no-header",
        output,
    )


def custom_findings(
    build: Path, graph: ninja_reader.Graph, shown: str, objects: Sequence[int]
) -> list[check_report.Finding]:
    """Give one error for each object of a custom command when Ninja does not record its headers (THE CUSTOM OBJECTS).

    An edge with no depfile gives one error, and the rule then does not read
    the list of its object.  Complexity: linear in the edges, their inputs and
    the files of the lists of the log.
    """
    found = [no_depfile(graph.edges[edge_id], shown) for edge_id in objects if not has_depfile(graph, edge_id)]
    listed_edges = [edge_id for edge_id in objects if has_depfile(graph, edge_id)]
    built = sorted(
        {
            graph.edges[edge_id].outputs[0]
            for edge_id in listed_edges
            if (build / graph.edges[edge_id].outputs[0]).is_file()
        }
    )
    if not built:
        return found
    try:
        lists = ninja_reader.deps(build, built)
    except ninja_reader.NinjaError as problem:
        message = f"the rule cannot read the dependency log: {problem}"
        return [*found, error(shown, 0, message, "input", ninja_reader.DEPS_NAME)]
    for edge_id in listed_edges:
        edge = graph.edges[edge_id]
        listed = lists.get(edge.outputs[0])
        if listed is None:
            continue
        inputs = {os.path.normpath(os.path.join(build, name)) for name in edge.inputs}
        has_header = any(os.path.normpath(os.path.join(build, name)) not in inputs for name in listed.files)
        if listed.state != ninja_reader.DepsState.VALID or not has_header:
            found.append(no_header(edge, listed, shown))
    return found


@dataclass(frozen=True, slots=True)
class LinkSetting:
    """The linker words of one link edge (THE LINKER).

    `linker` is the value of the last -fuse-ld, or empty when the edge has
    none.  `prefixes` holds each -B prefix in sequence.  `threads` is the
    last thread count that the edge gives mold: None for no count, and
    DEFAULT_COUNT for --threads with no count.  `has_no_fork` is True when
    --no-fork comes after the last --fork.
    """

    linker: str
    prefixes: tuple[str, ...]
    threads: str | None
    has_no_fork: bool


def ninja_value(text: str) -> str:
    """Return the value of a Ninja variable with no escape: $$ gives $, '$ ' a space and $: a colon.

    The reader does not expand a variable reference such as $out, and this
    function keeps it.  Complexity: linear in the length of the text.
    """
    kept: list[str] = []
    index = 0
    while index < len(text):
        if text[index] == "$" and index + 1 < len(text) and text[index + 1] in NINJA_ESCAPED:
            kept.append(text[index + 1])
            index += 2
            continue
        kept.append(text[index])
        index += 1
    return "".join(kept)


def linker_options(words: Sequence[str]) -> list[str]:
    """Return the options that the words of a compiler give the linker, from -Wl,A,B and -Xlinker A, in sequence."""
    options: list[str] = []
    for index, word in enumerate(words):
        if word.startswith(LINKER_PASS):
            options.extend(word[len(LINKER_PASS) :].split(","))
        elif word == LINKER_WORD and index + 1 < len(words):
            options.append(words[index + 1])
    return options


def link_setting(words: Sequence[str]) -> LinkSetting:
    """Read the linker, the -B prefixes, the thread count and the fork from the words of one link.

    The words are those of the variables FLAGS and LINK_FLAGS of the edge,
    in that sequence.  The compiler uses the last -fuse-ld, and mold uses the
    last thread count and the last of --fork and --no-fork.  Complexity:
    linear in the words.
    """
    linker = ""
    prefixes: list[str] = []
    for index, word in enumerate(words):
        if word.startswith(LINKER_OPTION):
            linker = word[len(LINKER_OPTION) :]
        elif word == PREFIX_OPTION and index + 1 < len(words):
            prefixes.append(words[index + 1])
        elif word.startswith(PREFIX_OPTION) and len(word) > len(PREFIX_OPTION):
            prefixes.append(word[len(PREFIX_OPTION) :])
    threads: str | None = None
    has_no_fork = False
    options = linker_options(words)
    for index, option in enumerate(options):
        count = next((option[len(prefix) :] for prefix in THREAD_COUNT_OPTIONS if option.startswith(prefix)), None)
        if count is not None:
            threads = count
        elif option == THREAD_COUNT_WORD and index + 1 < len(options):
            threads = options[index + 1]
        elif option == DEFAULT_THREADS:
            threads = DEFAULT_COUNT
        elif option == ONE_THREAD:
            threads = "1"
        elif option in (FORK, NO_FORK):
            has_no_fork = option == NO_FORK
    return LinkSetting(linker, tuple(prefixes), threads, has_no_fork)


def holds_linker(build: Path, prefix: str, known: dict[Path, bool]) -> bool:
    """Return True when a -B prefix of a link, read relative to the build directory, holds ld.mold.

    GCC adds a slash to a prefix that names a directory.  Each other prefix
    is the start of the name of a program.  `known` keeps the answer for each
    path.  The rule then reads each path one time.
    """
    base = build / prefix
    candidate = base / LINKER_PROGRAM if prefix.endswith("/") or base.is_dir() else Path(f"{base}{LINKER_PROGRAM}")
    if candidate not in known:
        known[candidate] = candidate.is_file()
    return known[candidate]


def link_words(variables: Mapping[str, str]) -> list[str]:
    """Return the words of the compiler of a link edge, from its variables FLAGS and LINK_FLAGS in that sequence.

    Raises:
        ValueError: If shlex cannot divide a variable into words
    """
    return [word for name in LINK_VARIABLES for word in shlex.split(ninja_value(variables.get(name, "")))]


def thread_count(context: BuildContext) -> int | check_report.Finding:
    """Return the number of threads of one link, the limit of the rule in the registry of the context, or the error."""
    try:
        limit = context.budget(RULE).limit
    except meta_runner.NotApplicable:
        message = (
            f"the registry has no limit of the rule {RULE}, the number of threads of one link.  Give its row "
            f"limit=N and unit=threads"
        )
        return error(REGISTRY_FILE, 0, message, "input", RULE)
    if limit.is_integer() and limit >= 1:
        return int(limit)
    message = (
        f"the limit {limit:g} of the rule {RULE} is the number of threads of one link, and it must be an integer "
        f"above 0"
    )
    return error(REGISTRY_FILE, 0, message, "input", RULE)


def linker_message(kind: str, value: str, count: int) -> str:
    """Return the start of the message of a group of link edges of one kind and one value (THE LINKER)."""
    if kind == "linker":
        given = f"{LINKER_OPTION}{value}, and not {LINKER_OPTION}{LINKER}" if value else f"no {LINKER_OPTION}{LINKER}"
        return f"give the compiler {given}, and GCC then runs a different linker"
    if kind == "linker-directory":
        return (
            f"give the compiler no {PREFIX_OPTION} prefix that holds {LINKER_PROGRAM}, and GCC then runs an "
            f"{LINKER_PROGRAM} of PATH and not the pinned {LINKER}"
        )
    if kind == "threads":
        if not value:
            given = "no thread count"
        elif value == DEFAULT_COUNT:
            given = f"{DEFAULT_THREADS} with no count"
        else:
            given = f"the thread count {value}"
        return f"give {LINKER} {given}, and the rule {RULE} of utils/registry.txt gives {count} threads"
    return (
        f"do not give {LINKER} {NO_FORK}.  {LINKER} then links in a child process, and the build launcher "
        f"measures only the compiler driver"
    )


def linker_findings(build: Path, graph: ninja_reader.Graph, shown: str, count: int) -> list[check_report.Finding]:
    """Give the errors of the linker words of each link edge (THE LINKER).

    Complexity: linear in the edges and their variables, and one read of the
    file system for each different -B prefix.
    """
    known: dict[Path, bool] = {}
    groups: defaultdict[tuple[str, str], list[int]] = defaultdict(list)
    found: list[check_report.Finding] = []
    for edge_id, edge in enumerate(graph.edges):
        if ninja_reader.edge_kind(edge.rule) != "link":
            continue
        try:
            setting = link_setting(link_words(graph.variables.get(edge_id, {})))
        except ValueError as problem:
            message = (
                f"the rule cannot divide the variables {' and '.join(LINK_VARIABLES)} of the link of "
                f"{edge.outputs[0]} into words: {problem}"
            )
            found.append(error(shown, edge.line, message, "input", edge.outputs[0]))
            continue
        if setting.linker != LINKER:
            groups[("linker", setting.linker)].append(edge_id)
        if not any(holds_linker(build, prefix, known) for prefix in setting.prefixes):
            groups[("linker-directory", "")].append(edge_id)
        if setting.threads != str(count):
            groups[("threads", setting.threads or "")].append(edge_id)
        if not setting.has_no_fork:
            groups[("fork", "")].append(edge_id)
    entities = {"linker-directory": LINKER_PROGRAM, "fork": NO_FORK}
    for (kind, value), edge_ids in sorted(groups.items()):
        first = graph.edges[edge_ids[0]]
        message = (
            f"{len(edge_ids)} link edge(s) {linker_message(kind, value, count)}.  The first writes "
            f"{first.outputs[0]}.  {LINKER_REPAIR}"
        )
        found.append(error(shown, first.line, message, kind, entities.get(kind, value or "none")))
    return found


def findings(context: BuildContext) -> list[check_report.Finding]:
    """Give the errors of the four parts of the graph of a build: the pools, the order, the custom objects, the linker.

    The module text tells each part.  An error of the limit of the rule
    stops only the part of the linker, and a graph that cannot be read stops
    each part (THE INPUT).

    Raises:
        NotApplicable: If the call gives no build directory, or a generator other than Ninja made the build
    """
    build = context.require_build()
    shown = context.shown(ninja_reader.graph_file(build))
    count = thread_count(context)
    limit_errors = [count] if isinstance(count, check_report.Finding) else []
    try:
        graph = context.graph()
    except ninja_reader.NinjaError as problem:
        message = f"the rule cannot read its input: {problem}"
        return [error(shown, 0, message, "input", ninja_reader.GRAPH_NAME), *limit_errors]
    objects = custom_objects(graph)
    found = [
        *pool_findings(graph, shown, objects),
        *order_findings(graph, shown),
        *custom_findings(build, graph, shown, objects),
        *limit_errors,
    ]
    if isinstance(count, int):
        found += linker_findings(build, graph, shown, count)
    return found


def missing_dependencies(build: Path) -> dict[str, tuple[str, ...]]:
    """Return each file of the dependency log of Ninja that is not on the disk, by the output of its list.

    THE MISSING DEPENDENCIES of the module text tells the use.  Ninja can
    list one output two times, with a relative and an absolute name.  The
    output is the absolute normalized path, and each file counts one time for
    its output.  Complexity: linear in the files of the lists of the log, with
    one stat for each file.

    Raises:
        ninja_reader.NotNinja: If the generator of the build is not Ninja
        ninja_reader.NinjaError: If the dependency log cannot be read
    """
    missing: dict[str, list[str]] = {}
    for output, listed in sorted(ninja_reader.deps(build).items()):
        target = os.path.normpath(os.path.join(build, output))
        for dependency in listed.files:
            known = missing.get(target, [])
            if dependency not in known and not os.path.exists(os.path.join(build, dependency)):
                missing[target] = [*known, dependency]
    return {target: tuple(files) for target, files in missing.items()}


def plant_context(plant: Path, scratch: Path) -> BuildContext:
    """Copy the tree of the base control of a plant, then make the planted build of the plant over it (THE PLANTS).

    Raises:
        NotApplicable: If build.txt or the planted registry does not obey its rows
    """
    if plant.name != BASE_CONTROL:
        shutil.copytree(plant.parent / BASE_CONTROL, scratch, dirs_exist_ok=True)
    return planted.plant_context(plant, scratch)
