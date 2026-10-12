"""job-pools — the compile jobs and the link jobs at one time, and the Ninja pools of the build that hold them.

THE COUNTS
    The error threshold of the measured rule compile-jobs of
    utils/registry.txt is the number of compile jobs at one time.  The error
    threshold of the rule link-jobs is the number of link jobs at one time.
    The two rows are the one home of the two counts.  cmake/JobPools.cmake
    reads them, and it gives Ninja the pool compile and the pool link with
    these depths.  Each compile, and each custom command that compiles an
    object, is in the pool compile.  Each link and each archive is in the
    pool link.  Ninja runs at most the depth of a pool of its edges at one
    time, also with a larger -j.  On 2026-10-11, a scratch project with 24
    compiles and a pool of the depth 2 built at -j 64.  It ran at most 2
    compiles at one time.  Each program that builds the tree then runs no
    more compiles and links than the two counts.

    A program of the tree that starts compiles with no build.ninja reads
    DEFAULT_JOBS of assay.core.proc, and ctest reads the jobs of a test
    preset.  The rule compile-jobs holds these counts equal to its count.
    proc.py does not read the registry, because each run of `loop fixture`
    imports it (THE JOB COUNT of that module).

THE LITERAL JOB COUNTS
    A program can give a build or a test run a job count of its own.  That
    count is a second home of the count.  The rule reads each module of
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

THE RULE compile-jobs
    The rule reads build.ninja and each file that it includes, through
    assay.readers.ninja.  It gives:
      `no-pool`       no file of the graph has the pool compile
      `pool-depth`    the depth of the pool compile is not the count of the
                      rule
      `outside-pool`  a compile, or a custom command with a first output that
                      ends with .o (custom_objects() of
                      assay.guard.engines.buildsys.build_graph), is not in the
                      pool compile.  One finding gives the number of such
                      edges for each kind of edge and each pool, and the first
                      edge
      `program-jobs`  DEFAULT_JOBS of utils/assay/core/proc.py, or the jobs of a
                      build preset or a test preset of CMakePresets.json, is
                      not the count of the rule
      `literal-jobs`  a module of the package gives a build or a test run a
                      literal job count (THE LITERAL JOB COUNTS)
      `input`         the rule cannot read a file of the graph, proc.py, a
                      module of the package or CMakePresets.json.  Or the
                      threshold of the rule is not an integer above 0
    The rule reads proc.py, the modules of the package and CMakePresets.json
    of the tree that it checks, and not the modules that run it.  CMakeUserPresets.json is a file of one
    user, and the tree does not hold it.  The rule does not read it.

THE RULE link-jobs
    The rule gives the findings `no-pool`, `pool-depth`, `outside-pool` and
    `input` for the pool link, each link and each archive.  No program of the tree
    has a count of links of its own.  The rule then gives no program-jobs.

A DIFFERENT DEPTH
    The cache variables CRUCIBLE_COMPILE_JOBS and CRUCIBLE_LINK_JOBS can give
    a build a different depth (cmake/JobPools.cmake).  The rules then give
    `pool-depth`, and the change is not silent.  `loop gauge --jobs 192` runs at
    most the depth of the pool compile of its copy at one time.

THE PLANTS
    The plants of each rule start from the tree of its control
    control-pooled.  That tree holds a planted build with the two pools,
    proc.py and CMakePresets.json.  Its planted registry gives the count 4 for
    compile-jobs and 2 for link-jobs.  plant_context() copies that tree first,
    and then the files of the plant over it.  A plant then holds only the
    files that it changes.

Complexity: one read of the graph and its included files, linear in their length.
"""

import ast
import shutil
from collections import defaultdict
from collections.abc import Sequence
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
# The largest chain of names that the rule follows to a literal.
NAME_DEPTH = 8


@dataclass(frozen=True, slots=True)
class PoolRule:
    """One rule of the module: its ID, its pool, the cache variable of the depth, its edges and the repair of an edge.

    `kinds` are the edge kinds of assay.readers.ninja in the pool.  With
    `has_programs`, the pool also holds each custom command that compiles an
    object, and the rule holds the job counts of the programs.
    """

    rule_id: str
    pool: str
    variable: str
    kinds: frozenset[str]
    has_programs: bool
    repair: str


COMPILE = PoolRule(
    COMPILE_RULE,
    "compile",
    "CRUCIBLE_COMPILE_JOBS",
    frozenset({"compile"}),
    True,
    "Create the target after cmake/JobPools.cmake, or give the custom command JOB_POOL ${CRUCIBLE_COMPILE_POOL}",
)
LINK = PoolRule(
    LINK_RULE,
    "link",
    "CRUCIBLE_LINK_JOBS",
    frozenset({"link", "archive"}),
    False,
    "Create the target after cmake/JobPools.cmake, which gives each link and each archive the pool link",
)


def error(rule: PoolRule, path: str, line: int, message: str, kind: str, entity: str) -> check_report.Finding:
    """Return one error of a rule of the module."""
    return check_report.Finding("error", path, line, rule.rule_id, message, kind, entity)


def count_of(context: BuildContext, rule: PoolRule) -> int | check_report.Finding:
    """Return the count of a rule, the error threshold of its row in the registry of the context, or the error.

    Raises:
        NotApplicable: If the registry has no measured rule with that ID
    """
    threshold = context.budget(rule.rule_id).error
    if threshold.is_integer() and threshold >= 1:
        return int(threshold)
    return error(
        rule,
        "utils/registry.txt",
        0,
        f"the error threshold {threshold:g} of the rule {rule.rule_id} is the number of jobs at one time, and it must "
        f"be an integer above 0",
        "input",
        rule.rule_id,
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


def pool_findings(context: BuildContext, rule: PoolRule, count: int) -> list[check_report.Finding]:
    """Give the errors of the pool of a rule: no pool, a depth that is not the count, and an edge not in the pool.

    Raises:
        NotApplicable: If the call gives no build directory, or a generator other than Ninja made the build
    """
    graph = build_graph.graph_or_error(context, rule.rule_id)
    if isinstance(graph, check_report.Finding):
        return [graph]
    build = context.require_build()
    shown = build_graph.graph_file_shown(context)
    found: list[check_report.Finding] = []
    pool = graph.pools.get(rule.pool)
    if pool is None:
        found.append(
            error(
                rule,
                shown,
                0,
                f"build.ninja and the files that it includes have no pool {rule.pool}.  Configure the build again: "
                f"cmake/JobPools.cmake gives the pool the depth of the rule {rule.rule_id}",
                "no-pool",
                rule.pool,
            )
        )
    elif pool.depth != count:
        depth = "a depth that is not an integer" if pool.depth is None else f"the depth {pool.depth}"
        found.append(
            error(
                rule,
                context.shown(build / pool.file),
                pool.line,
                f"the pool {rule.pool} has {depth}, and the rule {rule.rule_id} of utils/registry.txt gives {count} "
                f"jobs.  Configure the build again with an empty {rule.variable}, or raise the error threshold of the "
                f"rule with a measurement of the memory of the build",
                "pool-depth",
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
                f"{len(edge_ids)} {kind} edge(s) are in {place} and not in the pool {rule.pool}, and the count of the "
                f"rule {rule.rule_id} does not limit them.  The first writes {first.outputs[0]}.  {rule.repair}",
                "outside-pool",
                f"{kind} in {place}",
            )
        )
    return found


def default_jobs(root: Path) -> tuple[int, int] | str:
    """Return the value and the line of DEFAULT_JOBS in proc.py of a tree, or the problem of its read.

    Complexity: linear in the length of proc.py.
    """
    try:
        source = python_reader.read(root / PROC_MODULE, PROC_MODULE)
    except (OSError, UnicodeDecodeError, python_reader.PythonError) as problem:
        return f"{PROC_MODULE} cannot be read: {problem}"
    for node in source.tree.body:
        if (
            isinstance(node, ast.Assign)
            and any(isinstance(target, ast.Name) and target.id == PROC_CONSTANT for target in node.targets)
            and isinstance(node.value, ast.Constant)
            and type(node.value.value) is int
        ):
            return node.value.value, node.lineno
    return f"{PROC_MODULE} has no assignment of an integer to {PROC_CONSTANT} at the top level"


ScopeNode = ast.FunctionDef | ast.AsyncFunctionDef | ast.Lambda
# The values that a scope gives each name.  An empty list is a name with no value that the rule can read.
Bindings = dict[str, list[ast.expr]]


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
    arguments = node.args
    positional = [*arguments.posonlyargs, *arguments.args]
    defaults: dict[str, ast.expr] = dict(
        zip(
            (item.arg for item in positional[len(positional) - len(arguments.defaults) :]),
            arguments.defaults,
            strict=True,
        )
    )
    defaults.update(
        (item.arg, default)
        for item, default in zip(arguments.kwonlyargs, arguments.kw_defaults, strict=True)
        if default is not None
    )
    names = [*positional, *arguments.kwonlyargs, *(item for item in (arguments.vararg, arguments.kwarg) if item)]
    bound: Bindings = {item.arg: [defaults[item.arg]] if item.arg in defaults else [] for item in names}
    body = node.body if isinstance(node.body, list) else [node.body]
    for name, values in scope_bindings(body).items():
        bound.setdefault(name, []).extend(values)
    return bound


class LiteralJobs(ast.NodeVisitor):
    """The literal job counts of one module, each with its line and its text (THE LITERAL JOB COUNTS)."""

    def __init__(self, tree: ast.Module) -> None:
        """Read the values of the names of the module."""
        self.scopes: list[Bindings] = [scope_bindings(tree.body)]
        self.found: list[tuple[int, str]] = []

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
            scope = next((item for item in reversed(self.scopes) if node.id in item), None)
            return scope is not None and any(self.is_literal(value, depth + 1) for value in scope[node.id])
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

    def record(self, node: ast.expr) -> None:
        """Keep one literal job count."""
        self.found.append((node.lineno, ast.unparse(node)))

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

    def visit_scope(self, node: ScopeNode) -> None:
        """Visit a function with its own bindings."""
        self.scopes.append(function_bindings(node))
        self.generic_visit(node)
        self.scopes.pop()

    def visit_FunctionDef(self, node: ast.FunctionDef) -> None:
        """Visit a function with its own bindings."""
        self.visit_scope(node)

    def visit_AsyncFunctionDef(self, node: ast.AsyncFunctionDef) -> None:
        """Visit a function with its own bindings."""
        self.visit_scope(node)

    def visit_Lambda(self, node: ast.Lambda) -> None:
        """Visit a lambda with its own bindings."""
        self.visit_scope(node)

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


def literal_job_findings(root: Path, rule: PoolRule, count: int) -> list[check_report.Finding]:
    """Give one error for each literal job count of a module of the package of a tree (THE LITERAL JOB COUNTS).

    Complexity: one read of each module, and one parse of each module that holds a mark of JOB_MARKS.
    """
    found: list[check_report.Finding] = []
    tests = root / TESTS_DIRECTORY
    for path in sorted((root / PACKAGE_DIRECTORY).rglob("*.py")):
        if path.is_relative_to(tests):
            continue
        shown = path.relative_to(root).as_posix()
        try:
            text = path.read_text(encoding="utf-8")
        except (OSError, UnicodeDecodeError) as problem:
            found.append(error(rule, shown, 0, f"the rule cannot read {shown}: {problem}", "input", shown))
            continue
        if not any(mark in text for mark in JOB_MARKS):
            continue
        try:
            source = python_reader.parse(text, shown)
        except python_reader.PythonError as problem:
            found.append(error(rule, shown, problem.line, f"the rule cannot parse {shown}: {problem}", "input", shown))
            continue
        visitor = LiteralJobs(source.tree)
        visitor.visit(source.tree)
        for line, literal in visitor.found:
            found.append(
                error(
                    rule,
                    shown,
                    line,
                    f"{shown} gives a build or a test run the literal job count {literal}, a count of its own, and "
                    f"the rule {rule.rule_id} of utils/registry.txt gives {count} jobs.  Give it proc.DEFAULT_JOBS, "
                    f"or a parameter that its caller gives",
                    "literal-jobs",
                    f"{shown}:{line}",
                )
            )
    return found


def program_findings(context: BuildContext, rule: PoolRule, count: int) -> list[check_report.Finding]:
    """Give one error for DEFAULT_JOBS, a literal job count of a module and a preset when its jobs are not the count."""
    found = literal_job_findings(context.root, rule, count)
    jobs = default_jobs(context.root)
    if isinstance(jobs, str):
        found.append(error(rule, PROC_MODULE, 0, jobs, "input", PROC_CONSTANT))
    elif jobs[0] != count:
        found.append(
            error(
                rule,
                PROC_MODULE,
                jobs[1],
                f"{PROC_CONSTANT} is {jobs[0]}, and the rule {rule.rule_id} of utils/registry.txt gives {count} jobs.  "
                f"Give {PROC_CONSTANT} the count of the rule",
                "program-jobs",
                PROC_CONSTANT,
            )
        )
    presets = next((item for item in cmake_reader.presets(context.root) if item.path.name == PRESETS_FILE), None)
    if presets is None:
        return found
    if presets.problem is not None:
        problem = f"the rule cannot read {PRESETS_FILE}: {presets.problem}"
        return [*found, error(rule, PRESETS_FILE, 0, problem, "input", PRESETS_FILE)]
    for preset in presets.jobs:
        if preset.jobs != count:
            given = "no integer of jobs" if preset.jobs is None else f"{preset.jobs} jobs"
            found.append(
                error(
                    rule,
                    PRESETS_FILE,
                    0,
                    f"the {preset.kind} preset {preset.name} gives {given}, and the rule {rule.rule_id} of "
                    f"utils/registry.txt gives {count}.  Give the preset the count of the rule",
                    "program-jobs",
                    f"{preset.kind} {preset.name}",
                )
            )
    return found


def findings_of(context: BuildContext, rule: PoolRule) -> list[check_report.Finding]:
    """Give the errors of one rule of the module (THE RULE compile-jobs, THE RULE link-jobs).

    Raises:
        NotApplicable: If the call gives no build directory, or a generator other than Ninja made the build.  Also
            if the registry has no measured rule with the ID
    """
    context.require_build()
    count = count_of(context, rule)
    if isinstance(count, check_report.Finding):
        return [count]
    found = pool_findings(context, rule, count)
    if rule.has_programs:
        found += program_findings(context, rule, count)
    return found


def compile_findings(context: BuildContext) -> list[check_report.Finding]:
    """Give the errors of the rule compile-jobs."""
    return findings_of(context, COMPILE)


def link_findings(context: BuildContext) -> list[check_report.Finding]:
    """Give the errors of the rule link-jobs."""
    return findings_of(context, LINK)


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
}
