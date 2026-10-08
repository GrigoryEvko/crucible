"""build-graph — a compile that waits for a link, and a custom compile with no list of its headers.

Ninja starts an edge only when each input of the edge is there, also each
input after `||` (an `order-only` input).  Of the edges that can start, it
starts first the edge with the longest chain of edges to a target of the
build.  When two edges have the same chain, it starts the edge that comes
first in build.ninja.  It does not read the time of a job.  The rules of this
family read build.ninja through BuildContext.graph() and the dependency log
of Ninja through assay.readers.ninja.

THE RULE build-order
    No compile waits for a link or an archive through an input after `||`.

    CMake gives each compile such an input.  It is a phony edge that goes to
    the custom commands of the target and of the targets that it links.  A custom
    command can write a header.  The Ninja generator also gives a custom
    command such an input on each library that its target links, unless the
    command takes DEPENDS_EXPLICIT_ONLY.  Each compile behind such a command
    then waits for the link or the archive of that library.

    For each compile edge, the rule follows each input to the edge that
    writes the input, and then each input of that edge.  It does not follow
    the inputs of a link or an archive.  It gives:
      `waits-for-link`  an input after `||` that a link or an archive writes,
                        at the line of the edge that has the input.  The
                        message gives the number of compiles that wait for it
      `input`           build.ninja cannot be read
    An explicit or an implicit input that a link writes gives no finding.  A
    command can run a tool of the build to write a file that a compile reads.
    The entity is the output of the link or the archive.

THE RULE custom-deps
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
      `no-header`   an object of such an edge that is on the disk.  Its list
                    in the dependency log is not VALID, or it holds no header:
                    a file that is not an input of the edge
      `input`       build.ninja or the dependency log cannot be read
    The rule does not read the list of an object that is not on the disk,
    because a build can make only some targets.  The entity is the object.
    The rule does not apply when no custom command writes an object, for
    example in a build with CRUCIBLE_HAVE_BPF off.

THE MISSING DEPENDENCIES
    missing_dependencies(build) gives each file of the dependency log that is
    not on the disk.  The rule reconfigure-noop calls it, because such a file
    makes its edge run again on each build.

Complexity: one read of build.ninja and one run of `ninja -t deps` for each rule.
"""

import os
from collections import Counter
from pathlib import Path

from assay.core import report as check_report
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import ninja as ninja_reader

ORDER_RULE = "build-order"
CUSTOM_RULE = "custom-deps"
# The suffix of an object that a custom command compiles.
OBJECT_SUFFIX = ".o"
# The value of the variable `deps` that makes Ninja move a depfile into its dependency log.
GCC_DEPS = "gcc"
# The edge kinds of assay.readers.ninja that a link or an archive has.
LINKED_KINDS = frozenset({"link", "archive"})


def is_linked(rule: str) -> bool:
    """Tell if an edge of a rule is a link or an archive."""
    return ninja_reader.edge_kind(rule) in LINKED_KINDS


def is_compile(rule: str) -> bool:
    """Tell if an edge of a rule is a compile of CMake."""
    return ninja_reader.edge_kind(rule) == "compile"


def graph_file_shown(context: BuildContext) -> str:
    """Return the path of build.ninja of the build, as a finding shows it.

    Raises:
        NotApplicable: If the call gives no build directory
    """
    return context.shown(ninja_reader.graph_file(context.require_build()))


def graph_or_error(context: BuildContext, rule_id: str) -> ninja_reader.Graph | check_report.Finding:
    """Return the graph of build.ninja, or the error of a file that cannot be read.

    Raises:
        NotApplicable: If the call gives no build directory, or a generator other than Ninja made the build
    """
    try:
        return context.graph()
    except ninja_reader.NinjaError as problem:
        return check_report.Finding(
            "error",
            graph_file_shown(context),
            0,
            rule_id,
            f"the rule cannot read its input: {problem}",
            "input",
            ninja_reader.GRAPH_NAME,
        )


def order_only_waits(graph: ninja_reader.Graph) -> dict[tuple[int, int], int]:
    """Find each input after `||` that a link or an archive writes, behind each compile.

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


def order_findings(context: BuildContext) -> list[check_report.Finding]:
    """Give one error for each input after `||` that a link or an archive writes, behind a compile.

    THE RULE build-order of the module text tells the rule.

    Raises:
        NotApplicable: If the call gives no build directory, or a generator other than Ninja made the build
    """
    graph = graph_or_error(context, ORDER_RULE)
    if isinstance(graph, check_report.Finding):
        return [graph]
    shown = graph_file_shown(context)
    found: list[check_report.Finding] = []
    for (waiter, link), count in sorted(order_only_waits(graph).items()):
        waiting = graph.edges[waiter]
        written = graph.edges[link].outputs[0]
        found.append(
            check_report.Finding(
                "error",
                shown,
                waiting.line,
                ORDER_RULE,
                f"the edge that writes {waiting.outputs[0]} has the input {written} after `||`, and a link or an "
                f"archive writes it.  {count} compile(s) wait for that link or archive.  If the command reads only "
                f"the files that its DEPENDS name, give add_custom_command the option DEPENDS_EXPLICIT_ONLY",
                "waits-for-link",
                written,
            )
        )
    return found


def custom_objects(graph: ninja_reader.Graph) -> list[int]:
    """Return the index of each edge of a custom command with an object as its first output, in the file sequence.

    Complexity: linear in the edges.
    """
    return [
        edge_id
        for edge_id, edge in enumerate(graph.edges)
        if edge.rule == ninja_reader.CUSTOM_RULE and edge.outputs and edge.outputs[0].endswith(OBJECT_SUFFIX)
    ]


def has_depfile(graph: ninja_reader.Graph, edge_id: int) -> bool:
    """Tell if an edge has the variable `depfile` and the variable `deps = gcc`."""
    variables = graph.variables.get(edge_id, {})
    return bool(variables.get("depfile")) and variables.get("deps") == GCC_DEPS


def no_depfile(edge: ninja_reader.Edge, shown: str) -> check_report.Finding:
    """Return the error of a custom command that compiles an object and gives Ninja no depfile."""
    output = edge.outputs[0]
    return check_report.Finding(
        "error",
        shown,
        edge.line,
        CUSTOM_RULE,
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
    return check_report.Finding(
        "error",
        shown,
        edge.line,
        CUSTOM_RULE,
        f"the dependency log of Ninja holds no header of {output}, and the state of its list is {listed.state}{why}, "
        f"although build.ninja gives the command a depfile.  Make sure that the depfile names each header that the "
        f"compile reads, and build {output} again",
        "no-header",
        output,
    )


def custom_findings(context: BuildContext) -> list[check_report.Finding]:
    """Give one error for each object of a custom command when Ninja does not record its headers.

    THE RULE custom-deps of the module text tells the rule.  An edge with no
    depfile gives one error, and the rule then does not read the list of its
    object.  Complexity: linear in the edges, their inputs and the files of
    the lists of the log.

    Raises:
        NotApplicable: If the call gives no build directory, a generator other than Ninja made the build, or no
            custom command writes an object
    """
    build = context.require_build()
    graph = graph_or_error(context, CUSTOM_RULE)
    if isinstance(graph, check_report.Finding):
        return [graph]
    shown = graph_file_shown(context)
    objects = custom_objects(graph)
    if not objects:
        raise meta_runner.NotApplicable("no custom command of build.ninja writes an object")
    built = sorted(
        {graph.edges[edge_id].outputs[0] for edge_id in objects if (build / graph.edges[edge_id].outputs[0]).is_file()}
    )
    lists: dict[str, ninja_reader.Deps] = {}
    if built:
        try:
            lists = ninja_reader.deps(build, built)
        except ninja_reader.NinjaError as problem:
            return [
                check_report.Finding(
                    "error",
                    shown,
                    0,
                    CUSTOM_RULE,
                    f"the rule cannot read the dependency log: {problem}",
                    "input",
                    ninja_reader.DEPS_NAME,
                )
            ]
    found: list[check_report.Finding] = []
    for edge_id in objects:
        edge = graph.edges[edge_id]
        if not has_depfile(graph, edge_id):
            found.append(no_depfile(edge, shown))
            continue
        listed = lists.get(edge.outputs[0])
        if listed is None:
            continue
        inputs = {os.path.normpath(os.path.join(build, name)) for name in edge.inputs}
        has_header = any(os.path.normpath(os.path.join(build, name)) not in inputs for name in listed.files)
        if listed.state != ninja_reader.DepsState.VALID or not has_header:
            found.append(no_header(edge, listed, shown))
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


RULES = {
    "build-order": meta_runner.Member(order_findings),
    "custom-deps": meta_runner.Member(custom_findings),
}
