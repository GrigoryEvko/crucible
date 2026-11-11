"""job-pools — the compile jobs and the link jobs at one time, the Ninja pools that hold them, and the linker of a link.

THE COUNTS
    The host gives each job count.  cmake/JobPools.cmake gives Ninja the
    pool compile with one job for each logical core of the host, and the
    pool link with half of them.  Each compile, and each custom command that
    compiles an object, is in the pool compile.  Each link and each archive
    is in the pool link.  Ninja runs at most the depth of a pool of its edges
    at one time, also with a larger -j.  On 2026-10-11, a scratch project
    with 24 compiles and a pool of the depth 2 built at -j 64.  It ran at
    most 2 compiles at one time.

    A program of the tree that starts compiles with no build.ninja reads
    DEFAULT_JOBS of assay.core.proc, one job for each CPU of the process.  A
    build preset gives no jobs, so Ninja runs its default count.  A test
    preset gives execution.jobs the empty text, and ctest then runs one test
    for each processor of the host.  With no jobs, ctest runs one test at a
    time.  The rule compile-jobs refuses a literal count in each of these
    places, because a change of host makes it wrong.  The rule does not
    compare a count with the host, because a verdict reads only the tree.

THE LITERAL JOB COUNTS
    A program can give a build or a test run a job count of its own.  That
    count does not change with the host.  The rule reads each module of
    utils/assay of the tree, other than the tests in utils/assay/tests/.  A
    test gives the -j option of a parser literal values, such as 0, to check
    the parser, and it starts no build.  A job count is literal when it is one
    of these values:
      * An integer, or a text of digits
      * str() or int() of a literal
      * A name that an assignment of the module or of its function, or a
        parameter default, gives a literal value.
    The rule finds a literal job count at these positions:
      * The word after -j, --jobs or --parallel in a list or a tuple
      * A word -jN, or an f-string -j{X} with a literal X
      * The second argument of a call with the name of an environment
        variable that ends with _JOBS as its first argument, such as
        os.environ.get.
    The value proc.DEFAULT_JOBS is not literal.  A parameter with no literal
    default is not literal, and its caller gives the count.  The rule parses
    only a module with one of JOB_MARKS in its text.  On 2026-10-12, 20
    modules held one, and the rule took 0.15 s more than with no such scan.

THE WORKER POOLS
    A pool of threads or processes that starts compilers or other heavy
    processes has a job count too.  The rule reads each call
    of ThreadPoolExecutor or ProcessPoolExecutor in each module of utils/assay
    of the tree, also in the tests.  The count of workers is the keyword
    max_workers, or the first argument.  The count is bounded when the rule
    can prove that it is at most DEFAULT_JOBS:
      * proc.DEFAULT_JOBS, or a name that only bounded values give
      * A parameter, when its default, each value that its function gives
        it, and each argument that a call of the module gives it are bounded
      * An attribute that names an option of add_argument() in the module,
        such as options.jobs for --jobs, when the default of the option is
        bounded.  A larger count on the command line is the decision of a
        person
      * min() with one bounded argument
      * max() with bounded arguments and integers of at most 1
      * A bounded count // a positive integer.
    A literal, the length of a list and each other attribute are not bounded.
    A call with no count gets the default of Python.  A pool of threads then
    gets min(32, DEFAULT_JOBS + 4) workers, which can be more than
    DEFAULT_JOBS.  Give such a count min(proc.DEFAULT_JOBS, COUNT).

    The rule follows a parameter to each call of its function in the same
    module.  A plain call f(X) calls a function, and self.f(X) or cls.f(X) a
    method.  A call through a different attribute, such as proc.run(X), calls
    a function of a different module.  A call that gives the arguments through
    * or ** is not bounded.  A function with no call in the module gets the
    count from a caller in a different module.  The rule then takes the
    parameter as bounded.  The rule follows at most NAME_DEPTH names and
    calls.  It parses only a module with POOL_MARK in its text.  On
    2026-10-12, the rule took 1.22 s, and 1.02 s with no walk of the calls.

THE RULE compile-jobs
    The rule reads build.ninja and each file that it includes, through
    assay.readers.ninja.  It gives:
      `no-pool`       no file of the graph has the pool compile
      `outside-pool`  a compile, or a custom command with a first output that
                      ends with .o (custom_objects() of
                      assay.guard.engines.buildsys.build_graph), is not in the
                      pool compile.  One finding gives the number of such
                      edges for each kind of edge and each pool, and the first
                      edge
      `program-jobs`  DEFAULT_JOBS of utils/assay/core/proc.py is a literal
                      count (THE LITERAL JOB COUNTS), or a build preset or a
                      test preset of CMakePresets.json gives an integer of
                      jobs.  A test preset that does not give execution.jobs
                      the empty text is an error too
      `literal-jobs`  a module of the package gives a build or a test run a
                      literal job count (THE LITERAL JOB COUNTS)
      `pool-workers`  a pool of a module of the package or of its tests has a
                      count of workers that is not bounded (THE WORKER POOLS)
      `input`         the rule cannot read a file of the graph, proc.py, a
                      module of the package or CMakePresets.json.  Or proc.py
                      has no assignment to DEFAULT_JOBS at the top level
    The rule reads proc.py, the modules of the package and CMakePresets.json
    of the tree that it checks, and not the modules that run it.  CMakeUserPresets.json is a file of one
    user, and the tree does not hold it.  The rule does not read it.

THE RULE link-jobs
    The rule gives the findings `no-pool`, `outside-pool` and `input` for the
    pool link, each link and each archive.  No program of the tree has a
    count of links of its own.  The rule then gives no program-jobs.

THE RULE link-threads
    Each link of the tree runs mold (cmake/Linker.cmake).  The compiler gets
    -fuse-ld=mold, -B with the bin directory of the pinned mold, and the
    linker options --threads=N and --no-fork.  N is the limit of the rule
    link-threads.  With its fork, mold links in a child process, and the
    parent ends before the child.  wait4 of the build launcher then measures
    only the compiler driver.  With --no-fork, the rule link-memory
    measures the link.  The rule reads the variables FLAGS and
    LINK_FLAGS of each link edge of build.ninja, in the sequence of the
    command of CMake (link_setting()).  It reads a linker option from
    -Wl,A,B and from -Xlinker A.  It gives:
      `linker`            the last -fuse-ld of the edge is not mold, or the
                          edge has no -fuse-ld
      `linker-directory`  no -B prefix of the edge holds ld.mold.  GCC then
                          runs an ld.mold of PATH.  The rule reads a prefix
                          relative to the build directory, as the link does
      `threads`           the last thread count that the edge gives mold is
                          not the count of the rule.  --no-threads is one
                          thread, and --threads with no count is the default
                          of mold, one thread for each CPU
      `fork`              the edge does not give mold --no-fork after its
                          last --fork
      `input`             the rule cannot read the graph, or it cannot divide
                          a variable of an edge into words.  Or the limit of
                          the rule is not an integer above 0
    One finding gives the number of the edges of each kind and value, and
    the first edge.  An archive has no linker, and the rule does not read it.
    The configure step refuses a mold of a different version.  For that
    reason, the rule does not run ld.mold.

THE PLANTS
    The plants of each rule start from the tree of its control
    control-pooled.  That tree holds a planted build with the two pools,
    proc.py and CMakePresets.json.  plant_context() copies that tree first,
    and then the files of the plant over it.  A plant then holds only the
    files that it changes.  The control control-pooled of link-threads holds
    a planted build.  Each link of that build gives the flags of
    cmake/Linker.cmake, with the -B prefix planted-mold/ of the build
    directory.  Its planted registry gives the count 4 for link-threads.

Complexity: one read of the graph and its included files, linear in their length.
"""

import ast
import enum
import operator
import shlex
import shutil
from collections import defaultdict
from collections.abc import Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import report as check_report
from assay.guard.engines.buildsys import build_graph, planted
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import cmake as cmake_reader
from assay.readers import ninja as ninja_reader
from assay.readers import python as python_reader

COMPILE_RULE = "compile-jobs"
LINK_RULE = "link-jobs"
# The tree of the plants of each rule (THE PLANTS).
BASE_CONTROL = "control-pooled"
# The module with the default job count of the package, relative to the root of the tree, and the name of the count.
PROC_MODULE = "utils/assay/core/proc.py"
PROC_CONSTANT = "DEFAULT_JOBS"
PRESETS_FILE = "CMakePresets.json"
# The jobs of a test preset that give ctest --parallel with no count: one test for each processor of the host.
HOST_JOBS = ""
# The word of an edge with no pool, in a finding.
NO_POOL = "no pool"
# The modules of the package and its tests, relative to the root of the tree (THE LITERAL JOB COUNTS).
PACKAGE_DIRECTORY = "utils/assay"
TESTS_DIRECTORY = "utils/assay/tests"
# The words of a command that take a job count after them, and the start of a word that holds one.
JOB_OPTIONS = frozenset({"-j", "--jobs", "--parallel"})
JOB_PREFIX = "-j"
# The end of the name of an environment variable of a job count.
JOB_VARIABLE_SUFFIX = "_JOBS"
# The texts of a module that can start a literal job count.  The rule parses only a module that holds one.
JOB_MARKS = ('"-j', "'-j", '_JOBS"', "_JOBS'", '"--jobs"', "'--jobs'", '"--parallel"', "'--parallel'")
# The calls that keep a literal: str(X) and int(X).
CONVERSIONS = frozenset({"str", "int"})
# The largest chain of names that the rule follows to a literal or to a bounded count.
NAME_DEPTH = 8
# The classes of a pool of workers, the keyword of its count, and the text of a module that can start one (THE WORKER
# POOLS).
POOL_CLASSES = frozenset({"ThreadPoolExecutor", "ProcessPoolExecutor"})
WORKERS_KEYWORD = "max_workers"
POOL_MARK = "PoolExecutor"
# The calls that bound a count: min() with one bounded argument, and max() with only bounded arguments and small
# integers.  The largest integer that max() can take with a bounded count.
LOWEST_OF = "min"
HIGHEST_OF = "max"
SMALL_INTEGER = 1
# The first parameter of a method.  A call through it, such as self.run(X), does not give it.
BOUND_RECEIVERS = frozenset({"self", "cls"})
# The call of argparse that gives an option, and its keywords of the attribute name and of the default.
OPTION_CALL = "add_argument"
OPTION_DEST = "dest"
OPTION_DEFAULT = "default"
# The prefix of a long option word.
LONG_OPTION = "--"
# The rule of the linker of each link (THE RULE link-threads).
THREADS_RULE = "link-threads"
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
# The repair of a link edge of the rule link-threads.
THREADS_REPAIR = (
    "Configure the build again: cmake/Linker.cmake gives each link -fuse-ld=mold, -B with the bin directory of the "
    "pinned mold, the thread count of the rule link-threads and --no-fork"
)


@dataclass(frozen=True, slots=True)
class PoolRule:
    """One rule of the module: its ID, its pool, its edges and the repair of an edge.

    `kinds` are the edge kinds of assay.readers.ninja in the pool.  With
    `has_programs`, the pool also holds each custom command that compiles an
    object, and the rule holds the job counts of the programs.
    """

    rule_id: str
    pool: str
    kinds: frozenset[str]
    has_programs: bool
    repair: str


COMPILE = PoolRule(
    COMPILE_RULE,
    "compile",
    frozenset({"compile"}),
    True,
    "Create the target after cmake/JobPools.cmake, or give the custom command JOB_POOL ${CRUCIBLE_COMPILE_POOL}",
)
LINK = PoolRule(
    LINK_RULE,
    "link",
    frozenset({"link", "archive"}),
    False,
    "Create the target after cmake/JobPools.cmake, which gives each link and each archive the pool link",
)


def error(rule: PoolRule, path: str, line: int, message: str, kind: str, entity: str) -> check_report.Finding:
    """Return one error of a rule of the module."""
    return check_report.Finding("error", path, line, rule.rule_id, message, kind, entity)


def count_of(context: BuildContext, rule_id: str, meaning: str) -> int | check_report.Finding:
    """Return the count of a rule, the limit of its row in the registry of the context, or the error.

    Args:
        context: The context of the run
        rule_id: The ID of the measured rule
        meaning: What the count is, for the message of the error, such as "the number of threads of one link"

    Raises:
        NotApplicable: If the registry has no measured rule with that ID
    """
    limit = context.budget(rule_id).limit
    if limit.is_integer() and limit >= 1:
        return int(limit)
    return check_report.Finding(
        "error",
        "utils/registry.txt",
        0,
        rule_id,
        f"the limit {limit:g} of the rule {rule_id} is {meaning}, and it must be an integer above 0",
        "input",
        rule_id,
    )


def pooled_edges(graph: ninja_reader.Graph, rule: PoolRule) -> list[tuple[str, int]]:
    """Return the kind and the index of each edge of the graph that the pool of the rule holds, in the file sequence.

    The pool compile holds each compile and each custom command that compiles
    an object.  The pool link holds each link and each archive.  Complexity:
    linear in the edges.
    """
    found = [
        (ninja_reader.edge_kind(edge.rule), edge_id)
        for edge_id, edge in enumerate(graph.edges)
        if ninja_reader.edge_kind(edge.rule) in rule.kinds
    ]
    if rule.has_programs:
        found += [("custom object", edge_id) for edge_id in build_graph.custom_objects(graph)]
    return sorted(found, key=lambda item: item[1])


def pool_findings(context: BuildContext, rule: PoolRule) -> list[check_report.Finding]:
    """Give the errors of the pool of a rule: no pool, and an edge that is not in the pool.

    Raises:
        NotApplicable: If the call gives no build directory, or a generator other than Ninja made the build
    """
    graph = build_graph.graph_or_error(context, rule.rule_id)
    if isinstance(graph, check_report.Finding):
        return [graph]
    shown = build_graph.graph_file_shown(context)
    found: list[check_report.Finding] = []
    if rule.pool not in graph.pools:
        found.append(
            error(
                rule,
                shown,
                0,
                f"build.ninja and the files that it includes have no pool {rule.pool}.  Configure the build again: "
                f"cmake/JobPools.cmake gives the pool a depth from the host",
                "no-pool",
                rule.pool,
            )
        )
    unpooled: dict[tuple[str, str], list[int]] = defaultdict(list)
    for kind, edge_id in pooled_edges(graph, rule):
        actual = graph.variables.get(edge_id, {}).get("pool", "")
        if actual != rule.pool:
            unpooled[(kind, actual)].append(edge_id)
    for (kind, actual), edge_ids in sorted(unpooled.items()):
        first = graph.edges[edge_ids[0]]
        place = NO_POOL if not actual else f"the pool {actual}"
        found.append(
            error(
                rule,
                shown,
                first.line,
                f"{len(edge_ids)} {kind} edge(s) are in {place} and not in the pool {rule.pool}, and the depth of the "
                f"pool does not limit them.  The first writes {first.outputs[0]}.  {rule.repair}",
                "outside-pool",
                f"{kind} in {place}",
            )
        )
    return found


FunctionNode = ast.FunctionDef | ast.AsyncFunctionDef
ScopeNode = FunctionNode | ast.Lambda
# The values that a scope gives each name.  An empty list is a name with no value that the rule can read.
Bindings = dict[str, list[ast.expr]]


def parameters_of(node: ScopeNode) -> dict[str, ast.expr | None]:
    """Return each parameter of a function with its default, or None for a parameter with no default.

    The names of *args and **kwargs have no default.
    """
    arguments = node.args
    positional = [*arguments.posonlyargs, *arguments.args]
    found: dict[str, ast.expr | None] = {item.arg: None for item in positional}
    found.update(
        zip(
            (item.arg for item in positional[len(positional) - len(arguments.defaults) :]),
            arguments.defaults,
            strict=True,
        )
    )
    found.update((item.arg, default) for item, default in zip(arguments.kwonlyargs, arguments.kw_defaults, strict=True))
    found.update((item.arg, None) for item in (arguments.vararg, arguments.kwarg) if item)
    return found


def scope_bindings(body: Sequence[ast.AST]) -> Bindings:
    """Return the value of each assignment to a name in a body, with no nested function or class.

    Complexity: linear in the nodes of the body.
    """
    bound: Bindings = {}
    pending = list(body)
    while pending:
        node = pending.pop()
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef, ast.Lambda, ast.ClassDef)):
            continue
        if isinstance(node, ast.Assign):
            for target in node.targets:
                if isinstance(target, ast.Name):
                    bound.setdefault(target.id, []).append(node.value)
        elif isinstance(node, ast.AnnAssign) and node.value is not None and isinstance(node.target, ast.Name):
            bound.setdefault(node.target.id, []).append(node.value)
        pending.extend(ast.iter_child_nodes(node))
    return bound


def function_bindings(node: ScopeNode) -> Bindings:
    """Return the values of the names of a function: each parameter with its default, and each assignment of its body.

    A parameter with no default has no value.  The name then does not get a value of the module.
    """
    bound: Bindings = {name: [] if default is None else [default] for name, default in parameters_of(node).items()}
    body = node.body if isinstance(node.body, list) else [node.body]
    for name, values in scope_bindings(body).items():
        bound.setdefault(name, []).extend(values)
    return bound


class ScopedVisitor(ast.NodeVisitor):
    """A visitor of one module that knows the values that each scope gives each name, and keeps its findings.

    `owners` holds the function of each scope of `scopes`, and None for the module.
    """

    def __init__(self, tree: ast.Module) -> None:
        """Read the values of the names of the module."""
        self.scopes: list[Bindings] = [scope_bindings(tree.body)]
        self.owners: list[ScopeNode | None] = [None]
        self.found: list[tuple[int, str]] = []

    def scope_of(self, name: str) -> int | None:
        """Return the index of the innermost scope that gives the name values, or None when no scope gives it values."""
        return next((index for index in range(len(self.scopes) - 1, -1, -1) if name in self.scopes[index]), None)

    def values_of(self, name: str) -> list[ast.expr] | None:
        """Return the values that the innermost scope with the name gives it, or None when no scope gives it values.

        A parameter with no default has an empty list.
        """
        index = self.scope_of(name)
        return None if index is None else self.scopes[index][name]

    def record(self, node: ast.expr) -> None:
        """Keep one finding of the expression."""
        self.found.append((node.lineno, ast.unparse(node)))

    def visit_scope(self, node: ScopeNode) -> None:
        """Visit a function with its own bindings."""
        self.scopes.append(function_bindings(node))
        self.owners.append(node)
        self.generic_visit(node)
        self.scopes.pop()
        self.owners.pop()

    def visit_FunctionDef(self, node: ast.FunctionDef) -> None:
        """Visit a function with its own bindings."""
        self.visit_scope(node)

    def visit_AsyncFunctionDef(self, node: ast.AsyncFunctionDef) -> None:
        """Visit a function with its own bindings."""
        self.visit_scope(node)

    def visit_Lambda(self, node: ast.Lambda) -> None:
        """Visit a lambda with its own bindings."""
        self.visit_scope(node)


class LiteralJobs(ScopedVisitor):
    """The literal job counts of one module, each with its line and its text (THE LITERAL JOB COUNTS)."""

    def is_literal(self, node: ast.expr, depth: int = 0) -> bool:
        """Return if an expression is a literal job count, through at most NAME_DEPTH names.

        The innermost scope that gives a name values holds the answer, and one literal value of the name is sufficient.
        """
        if isinstance(node, ast.Constant):
            value = node.value
            return type(value) is int or (isinstance(value, str) and value.isdigit())
        if isinstance(node, ast.Call) and isinstance(node.func, ast.Name) and node.func.id in CONVERSIONS:
            return len(node.args) == 1 and not node.keywords and self.is_literal(node.args[0], depth)
        if isinstance(node, ast.Name) and depth < NAME_DEPTH:
            values = self.values_of(node.id)
            return values is not None and any(self.is_literal(value, depth + 1) for value in values)
        return False

    def is_joined_literal(self, node: ast.JoinedStr) -> bool:
        """Return if an f-string is -j{X} with a literal X."""
        if len(node.values) != 2:
            return False
        prefix, formatted = node.values
        return (
            isinstance(prefix, ast.Constant)
            and prefix.value == JOB_PREFIX
            and isinstance(formatted, ast.FormattedValue)
            and formatted.format_spec is None
            and self.is_literal(formatted.value)
        )

    def is_literal_word(self, word: ast.expr) -> bool:
        """Return if one word holds a literal job count: -jN, or the f-string -j{X} with a literal X."""
        if isinstance(word, ast.JoinedStr):
            return self.is_joined_literal(word)
        text = word.value if isinstance(word, ast.Constant) and isinstance(word.value, str) else ""
        return text.startswith(JOB_PREFIX) and text[len(JOB_PREFIX) :].isdigit()

    def check_words(self, words: Sequence[ast.expr]) -> None:
        """Keep each literal job count of the words of a list or a tuple."""
        for index, word in enumerate(words):
            if isinstance(word, ast.Constant) and word.value in JOB_OPTIONS:
                if index + 1 < len(words) and self.is_literal(words[index + 1]):
                    self.record(words[index + 1])
            elif self.is_literal_word(word):
                self.record(word)

    def visit_List(self, node: ast.List) -> None:
        """Keep each literal job count of a list."""
        self.check_words(node.elts)
        self.generic_visit(node)

    def visit_Tuple(self, node: ast.Tuple) -> None:
        """Keep each literal job count of a tuple."""
        self.check_words(node.elts)
        self.generic_visit(node)

    def visit_Call(self, node: ast.Call) -> None:
        """Keep a literal default of a call that reads an environment variable of a job count."""
        first = node.args[0] if node.args else None
        if (
            isinstance(first, ast.Constant)
            and isinstance(first.value, str)
            and first.value.endswith(JOB_VARIABLE_SUFFIX)
            and len(node.args) >= 2
            and self.is_literal(node.args[1])
        ):
            self.record(node.args[1])
        self.generic_visit(node)


class Absent(enum.Enum):
    """Why a call gives a parameter no argument that the rule can read.  The value completes a sentence."""

    OMITTED = "gives the parameter no argument"
    SPREAD = "gives its arguments through * or **"


@dataclass(frozen=True, slots=True)
class CallSite:
    """One call of a function of a module, with the scopes and the functions around the call (THE WORKER POOLS)."""

    call: ast.Call
    scopes: tuple[Bindings, ...]
    owners: tuple[ScopeNode | None, ...]


def called_name(call: ast.Call) -> str:
    """Return the name of the function of a call: f in f(X) and in X.f(Y), or an empty text for other calls."""
    function = call.func
    if isinstance(function, ast.Name):
        return function.id
    if isinstance(function, ast.Attribute):
        return function.attr
    return ""


def is_method(owner: FunctionNode) -> bool:
    """Return if a function takes self or cls as its first parameter."""
    positional = [*owner.args.posonlyargs, *owner.args.args]
    return bool(positional) and positional[0].arg in BOUND_RECEIVERS


def is_receiver_call(call: ast.Call) -> bool:
    """Return if a call goes through self or cls, such as self.run(X)."""
    function = call.func
    return (
        isinstance(function, ast.Attribute)
        and isinstance(function.value, ast.Name)
        and function.value.id in BOUND_RECEIVERS
    )


def local_callee(call: ast.Call) -> str:
    """Return the name of the function of the module that a call can call, or an empty text.

    A plain call f(X) can call a function of the module, and self.f(X) or cls.f(X) a method.  A call through a
    different attribute, such as proc.run(X), calls a function of a different module.
    """
    function = call.func
    if isinstance(function, ast.Name):
        return function.id
    if is_receiver_call(call) and isinstance(function, ast.Attribute):
        return function.attr
    return ""


def option_dest(call: ast.Call) -> str:
    """Return the name of the attribute that a call of add_argument() gives its option, or an empty text.

    The keyword dest gives it.  Else the first option word with two hyphens gives it, with each hyphen after them as an
    underscore, as argparse does.
    """
    for keyword in call.keywords:
        if keyword.arg == OPTION_DEST and isinstance(keyword.value, ast.Constant):
            return str(keyword.value.value)
    for argument in call.args:
        word = argument.value if isinstance(argument, ast.Constant) and isinstance(argument.value, str) else ""
        if word.startswith(LONG_OPTION) and len(word) > len(LONG_OPTION):
            return word[len(LONG_OPTION) :].replace("-", "_")
    return ""


def given_argument(call: ast.Call, owner: FunctionNode, parameter: str) -> ast.expr | Absent:
    """Return the argument that a call gives a parameter of its function, or why the rule cannot read one.

    A call through self or cls does not give the first parameter of a method.
    """
    for keyword in call.keywords:
        if keyword.arg == parameter:
            return keyword.value
    is_spread = any(isinstance(item, ast.Starred) for item in call.args) or any(
        item.arg is None for item in call.keywords
    )
    positional = [item.arg for item in (*owner.args.posonlyargs, *owner.args.args)]
    collectors = [item.arg for item in (owner.args.vararg, owner.args.kwarg) if item]
    if is_spread or parameter in collectors:
        return Absent.SPREAD
    if parameter not in positional:
        return Absent.OMITTED
    index = positional.index(parameter) - (1 if is_receiver_call(call) else 0)
    return call.args[index] if 0 <= index < len(call.args) else Absent.OMITTED


class CallSites(ScopedVisitor):
    """The calls of each function of one module, and the options of add_argument(), with their scopes.

    `sites` holds the calls of each name that local_callee() gives.  `options` holds the calls of add_argument() for
    each name that option_dest() gives (THE WORKER POOLS).
    """

    def __init__(self, tree: ast.Module) -> None:
        """Read the values of the names of the module, and start with no call."""
        super().__init__(tree)
        self.sites: defaultdict[str, list[CallSite]] = defaultdict(list)
        self.options: defaultdict[str, list[CallSite]] = defaultdict(list)

    def visit_Call(self, node: ast.Call) -> None:
        """Keep the call with the name of the function of the module that it calls, or with the name of its option."""
        site = CallSite(node, tuple(self.scopes), tuple(self.owners))
        callee = local_callee(node)
        if callee:
            self.sites[callee].append(site)
        if isinstance(node.func, ast.Attribute) and node.func.attr == OPTION_CALL:
            dest = option_dest(node)
            if dest:
                self.options[dest].append(site)
        self.generic_visit(node)


class PoolWorkers(ScopedVisitor):
    """The pools of one module with a count of workers that is not bounded, each with its line (THE WORKER POOLS).

    `blame` names the first call that gives a parameter a count that is not bounded, in the check of one pool.
    """

    def __init__(self, tree: ast.Module) -> None:
        """Read the values of the names, the calls of each function and the options of the module."""
        super().__init__(tree)
        sites = CallSites(tree)
        sites.visit(tree)
        self.sites = sites.sites
        self.options = sites.options
        self.blame: str | None = None

    def is_bounded_at(self, site: CallSite, node: ast.expr, depth: int) -> bool:
        """Return if an expression of a call is bounded, with the names of the scopes around that call."""
        saved = self.scopes, self.owners
        self.scopes, self.owners = list(site.scopes), list(site.owners)
        try:
            return self.is_bounded(node, depth)
        finally:
            self.scopes, self.owners = saved

    def is_given_bounded(self, owner: FunctionNode, parameter: str, depth: int) -> bool:
        """Return if each call of the function in the module gives the parameter a bounded count.

        A plain call can call a function, and a call through self or cls a method.  A call that gives no argument
        keeps the default, and the bindings of the function hold the default.  Complexity: linear in the calls of the
        function, through at most NAME_DEPTH names and calls.
        """
        has_default = parameters_of(owner)[parameter] is not None
        for site in self.sites.get(owner.name, []):
            if is_receiver_call(site.call) != is_method(owner):
                continue
            given = given_argument(site.call, owner, parameter)
            if given is Absent.OMITTED and has_default:
                continue
            where = f"the call of {owner.name} at line {site.call.lineno}"
            if isinstance(given, Absent):
                self.blame = self.blame or f"{where} {given.value}"
                return False
            if not self.is_bounded_at(site, given, depth):
                self.blame = self.blame or f"{where} gives {parameter} the count {ast.unparse(given)}"
                return False
        return True

    def is_option_bounded(self, node: ast.Attribute, depth: int) -> bool:
        """Return if an attribute names an option of add_argument() of the module with a bounded default.

        An attribute of self or cls names no option.  An option with no default has the default None, and the pool
        then gets the default count of Python.
        """
        receiver = node.value
        if isinstance(receiver, ast.Name) and receiver.id in BOUND_RECEIVERS:
            return False
        sites = self.options.get(node.attr, [])
        for site in sites:
            default = next((item.value for item in site.call.keywords if item.arg == OPTION_DEFAULT), None)
            where = f"the option {node.attr} of the call of {OPTION_CALL} at line {site.call.lineno}"
            if default is None:
                self.blame = self.blame or f"{where} has no default"
                return False
            if not self.is_bounded_at(site, default, depth):
                self.blame = self.blame or f"{where} has the default {ast.unparse(default)}"
                return False
        return bool(sites)

    def is_bounded(self, node: ast.expr, depth: int = 0) -> bool:
        """Return if the rule can prove that a count is at most DEFAULT_JOBS, through at most NAME_DEPTH names.

        Each value that the innermost scope gives a name must be bounded.  For a parameter of a function, each call of
        the function in the module must also give a bounded argument (is_given_bounded()).  An attribute is bounded
        when it is DEFAULT_JOBS, or an option with a bounded default (is_option_bounded()).
        """
        if isinstance(node, ast.Attribute):
            return node.attr == PROC_CONSTANT or (depth < NAME_DEPTH and self.is_option_bounded(node, depth + 1))
        if isinstance(node, ast.Name):
            index = self.scope_of(node.id)
            if index is None:
                return node.id == PROC_CONSTANT
            if depth >= NAME_DEPTH:
                return False
            owner = self.owners[index]
            if (
                isinstance(owner, (ast.FunctionDef, ast.AsyncFunctionDef))
                and node.id in parameters_of(owner)
                and not self.is_given_bounded(owner, node.id, depth + 1)
            ):
                return False
            return all(self.is_bounded(value, depth + 1) for value in self.scopes[index][node.id])
        if isinstance(node, ast.BinOp) and isinstance(node.op, ast.FloorDiv):
            divisor = node.right
            is_positive = isinstance(divisor, ast.Constant) and type(divisor.value) is int and divisor.value >= 1
            return is_positive and self.is_bounded(node.left, depth)
        if not isinstance(node, ast.Call) or not isinstance(node.func, ast.Name) or node.keywords or not node.args:
            return False
        bounded = [self.is_bounded(argument, depth) for argument in node.args]
        if node.func.id == LOWEST_OF:
            return any(bounded)
        is_small = [
            isinstance(item, ast.Constant) and type(item.value) is int and item.value <= SMALL_INTEGER
            for item in node.args
        ]
        return node.func.id == HIGHEST_OF and any(bounded) and all(map(operator.or_, bounded, is_small))

    def visit_Call(self, node: ast.Call) -> None:
        """Keep a pool with a count of workers that is not bounded, or with no count."""
        if called_name(node) in POOL_CLASSES:
            given = [item.value for item in node.keywords if item.arg == WORKERS_KEYWORD]
            workers = given[0] if given else node.args[0] if node.args else None
            self.blame = None
            if workers is None:
                self.found.append((node.lineno, "with no count of workers, so Python gives it its default count"))
            elif not self.is_bounded(workers):
                cause = f", and {self.blame}" if self.blame else ""
                self.found.append((workers.lineno, f"with the count {ast.unparse(workers)} of workers{cause}"))
        self.generic_visit(node)


def module_findings(root: Path, rule: PoolRule) -> list[check_report.Finding]:
    """Give one error for each literal job count and each pool that is not bounded of a module of the package.

    THE LITERAL JOB COUNTS do not read the tests, and THE WORKER POOLS read them.  Complexity: one read of each
    module, and one parse of each module that holds a mark of JOB_MARKS or POOL_MARK.  A module with POOL_MARK gets
    one more walk, which collects the calls of its functions.
    """
    found: list[check_report.Finding] = []
    tests = root / TESTS_DIRECTORY
    for path in sorted((root / PACKAGE_DIRECTORY).rglob("*.py")):
        shown = path.relative_to(root).as_posix()
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as problem:
            found.append(error(rule, shown, 0, f"the rule cannot read {shown}: {problem}", "input", shown))
            continue
        has_jobs = not path.is_relative_to(tests) and any(mark in text for mark in JOB_MARKS)
        has_pools = POOL_MARK in text
        if not has_jobs and not has_pools:
            continue
        try:
            source = python_reader.parse(text, shown)
        except python_reader.PythonError as problem:
            found.append(error(rule, shown, problem.line, f"the rule cannot parse {shown}: {problem}", "input", shown))
            continue
        literals = LiteralJobs(source.tree)
        if has_jobs:
            literals.visit(source.tree)
        pools = PoolWorkers(source.tree)
        if has_pools:
            pools.visit(source.tree)
        for line, literal in literals.found:
            found.append(
                error(
                    rule,
                    shown,
                    line,
                    f"{shown} gives a build or a test run the literal job count {literal}, and a change of host makes "
                    f"it wrong.  Give it proc.DEFAULT_JOBS, one job for each CPU of the process, or a parameter that "
                    f"its caller gives",
                    "literal-jobs",
                    f"{shown}:{line}",
                )
            )
        for line, described in pools.found:
            found.append(
                error(
                    rule,
                    shown,
                    line,
                    f"{shown} starts a pool {described}, and the rule cannot prove that the count is at most "
                    f"proc.DEFAULT_JOBS, one job for each CPU of the process.  Give the pool "
                    f"min(proc.DEFAULT_JOBS, COUNT), proc.DEFAULT_JOBS, or a parameter that its caller gives",
                    "pool-workers",
                    f"{shown}:{line}",
                )
            )
    return found


def default_jobs_findings(root: Path, rule: PoolRule) -> list[check_report.Finding]:
    """Give the error of DEFAULT_JOBS in proc.py of a tree: no assignment at the top level, or a literal count.

    Complexity: linear in the length of proc.py.
    """
    try:
        source = python_reader.read(root / PROC_MODULE, PROC_MODULE)
    except (OSError, UnicodeDecodeError, python_reader.PythonError) as problem:
        return [error(rule, PROC_MODULE, 0, f"{PROC_MODULE} cannot be read: {problem}", "input", PROC_CONSTANT)]
    literals = LiteralJobs(source.tree)
    for node in source.tree.body:
        targets: Sequence[ast.expr] = ()
        value: ast.expr | None = None
        if isinstance(node, ast.Assign):
            targets, value = node.targets, node.value
        elif isinstance(node, ast.AnnAssign):
            targets, value = (node.target,), node.value
        if value is None or not any(isinstance(target, ast.Name) and target.id == PROC_CONSTANT for target in targets):
            continue
        if not literals.is_literal(value):
            return []
        message = (
            f"{PROC_CONSTANT} is the literal job count {ast.unparse(value)}, and a change of host makes it wrong.  "
            f"Give it os.process_cpu_count() or 1, one job for each CPU of the process"
        )
        return [error(rule, PROC_MODULE, node.lineno, message, "program-jobs", PROC_CONSTANT)]
    message = f"{PROC_MODULE} has no assignment to {PROC_CONSTANT} at the top level"
    return [error(rule, PROC_MODULE, 0, message, "input", PROC_CONSTANT)]


def preset_message(preset: cmake_reader.PresetJobs) -> str | None:
    """Return the error of the jobs of one build preset or test preset, or None when the preset takes the host count.

    A build preset with no integer of jobs runs the default count of Ninja.  A test preset takes the host count only
    with the empty text of execution.jobs.
    """
    if type(preset.jobs) is int:
        repair = (
            'Give it "jobs": "", one test for each processor of the host'
            if preset.kind == "test"
            else "Remove the field jobs: Ninja then runs its default count, and the pools limit the compiles and links"
        )
        return (
            f"the {preset.kind} preset {preset.name} gives the literal job count {preset.jobs}, and a change of host "
            f"makes it wrong.  {repair}"
        )
    if preset.kind == "test" and preset.jobs != HOST_JOBS:
        given = "no jobs" if preset.jobs is None else f"the jobs {preset.jobs!r}"
        return (
            f"the test preset {preset.name} gives {given}, and not the empty text of execution.jobs.  ctest with no "
            f'jobs runs one test at a time.  Give it "jobs": "", one test for each processor of the host'
        )
    return None


def program_findings(context: BuildContext, rule: PoolRule) -> list[check_report.Finding]:
    """Give one error for a literal DEFAULT_JOBS, a literal job count of a module and a preset with no host count."""
    found = [*module_findings(context.root, rule), *default_jobs_findings(context.root, rule)]
    presets = next((item for item in cmake_reader.presets(context.root) if item.path.name == PRESETS_FILE), None)
    if presets is None:
        return found
    if presets.problem is not None:
        problem = f"the rule cannot read {PRESETS_FILE}: {presets.problem}"
        return [*found, error(rule, PRESETS_FILE, 0, problem, "input", PRESETS_FILE)]
    for preset in presets.jobs:
        message = preset_message(preset)
        if message is not None:
            found.append(error(rule, PRESETS_FILE, 0, message, "program-jobs", f"{preset.kind} {preset.name}"))
    return found


def findings_of(context: BuildContext, rule: PoolRule) -> list[check_report.Finding]:
    """Give the errors of one rule of the module (THE RULE compile-jobs, THE RULE link-jobs).

    Raises:
        NotApplicable: If the call gives no build directory, or a generator other than Ninja made the build
    """
    context.require_build()
    found = pool_findings(context, rule)
    if rule.has_programs:
        found += program_findings(context, rule)
    return found


def compile_findings(context: BuildContext) -> list[check_report.Finding]:
    """Give the errors of the rule compile-jobs."""
    return findings_of(context, COMPILE)


def link_findings(context: BuildContext) -> list[check_report.Finding]:
    """Give the errors of the rule link-jobs."""
    return findings_of(context, LINK)


@dataclass(frozen=True, slots=True)
class LinkSetting:
    """The linker words of one link edge (THE RULE link-threads).

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


def threads_message(kind: str, value: str, count: int) -> str:
    """Return the start of the message of a group of link edges of one kind and one value (THE RULE link-threads)."""
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
        return f"give {LINKER} {given}, and the rule {THREADS_RULE} of utils/registry.txt gives {count} threads"
    return (
        f"do not give {LINKER} {NO_FORK}.  {LINKER} then links in a child process, and the build launcher "
        f"measures only the compiler driver"
    )


def threads_findings(context: BuildContext) -> list[check_report.Finding]:
    """Give the errors of the rule link-threads (THE RULE link-threads).

    Complexity: linear in the edges and their variables, and one read of the
    file system for each different -B prefix.

    Raises:
        NotApplicable: If the call gives no build directory, or a generator other than Ninja made the build.  Also
            if the registry has no measured rule link-threads
    """
    build = context.require_build()
    count = count_of(context, THREADS_RULE, "the number of threads of one link")
    if isinstance(count, check_report.Finding):
        return [count]
    graph = build_graph.graph_or_error(context, THREADS_RULE)
    if isinstance(graph, check_report.Finding):
        return [graph]
    shown = build_graph.graph_file_shown(context)
    known: dict[Path, bool] = {}
    groups: defaultdict[tuple[str, str], list[int]] = defaultdict(list)
    found: list[check_report.Finding] = []
    for edge_id, edge in enumerate(graph.edges):
        if ninja_reader.edge_kind(edge.rule) != "link":
            continue
        try:
            setting = link_setting(link_words(graph.variables.get(edge_id, {})))
        except ValueError as problem:
            found.append(
                check_report.Finding(
                    "error",
                    shown,
                    edge.line,
                    THREADS_RULE,
                    f"the rule cannot divide the variables {' and '.join(LINK_VARIABLES)} of the link of "
                    f"{edge.outputs[0]} into words: {problem}",
                    "input",
                    edge.outputs[0],
                )
            )
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
        found.append(
            check_report.Finding(
                "error",
                shown,
                first.line,
                THREADS_RULE,
                f"{len(edge_ids)} link edge(s) {threads_message(kind, value, count)}.  The first writes "
                f"{first.outputs[0]}.  {THREADS_REPAIR}",
                kind,
                entities.get(kind, value or "none"),
            )
        )
    return found


def plant_context(plant: Path, scratch: Path) -> BuildContext:
    """Copy the tree of the base control of a plant, then make the planted build of the plant over it (THE PLANTS).

    Raises:
        NotApplicable: If build.txt or the planted registry does not obey its rows
    """
    if plant.name != BASE_CONTROL:
        shutil.copytree(plant.parent / BASE_CONTROL, scratch, dirs_exist_ok=True)
    return planted.plant_context(plant, scratch)


RULES = {
    "compile-jobs": meta_runner.Member(compile_findings),
    "link-jobs": meta_runner.Member(link_findings),
    "link-threads": meta_runner.Member(threads_findings),
}
