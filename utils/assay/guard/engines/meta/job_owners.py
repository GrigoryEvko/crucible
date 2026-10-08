"""job-owners — a shared job has one owner, and only its owner does the job.

THE JOBS
    A job row of the registry names a job and its owners:

        job ENTITY module=OWNER[,OWNER...] | WHY THE JOB HAS ONE OWNER

    An owner is a module of the package, such as assay.core.proc, or a
    master, such as bin.guard (assay.guard.engines.meta.package, THE NAMES).
    This module holds the recognizer of each job, in the table JOBS:
      subprocess       a use of the module subprocess, and a process start of
                       os.system, os.popen, os.exec*, os.spawn*, os.posix_spawn,
                       os.fork, os.forkpty, pty.spawn or asyncio.create_subprocess_*
      argparse         an import of argparse
      git              a list or a tuple with the text "git" as its first item:
                       the words of a git command
      sys.path         a change of sys.path: an assignment, or a call of the
                       methods `insert`, `append`, `extend`, `remove`, `pop` or
                       `clear`
      print            a call of print(), and a write to sys.stdout or sys.stderr
      row-bar          a division of a text at a bar: a call of `split`,
                       `rsplit`, `partition` or `rpartition` with the separator
                       "|" and spaces
      repository-root  a path two directories or more above __file__, through
                       .parents[N], .parent.parent or os.path.dirname twice
      cache-root       the names CRUCIBLE_CACHE_DIR and XDG_CACHE_HOME
      cost-record      a text that ends with ".cost", the suffix of a cost record
      ctest-log        the files LastTest.log, LastTestsFailed.log and
                       CTestCostData.txt that ctest writes
    The reads of the formats of the build are the kind format-read of the
    rule structured-reads, and this rule does not read them.  These formats
    are CMakeCache.txt, the compile database, the Ninja files and the list of
    ctest.

THE RULE
    The rule reads each module of the package and each master, but not this
    module.  The constants of this module are the recognizers and not uses.
    The rule refuses:
      ENTITY       a use of a job in a module or a master that is not its
                   owner.  The kind is the name of the job, and the entity is
                   the function that holds the use
      unknown-job  a job row with no recognizer in JOBS
      no-job-row   a recognizer of JOBS with no job row

Complexity: linear in the length of the files of the scope, for each job.
"""

import ast
from collections.abc import Callable, Iterator, Mapping, Sequence
from dataclasses import dataclass

from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.meta import package as package_scope
from assay.guard.engines.meta.runner import MetaContext
from assay.readers import python as python_reader

RULE = "job-owners"
PROCESS_STARTS = frozenset(
    {
        "os.system",
        "os.popen",
        "os.fork",
        "os.forkpty",
        "os.posix_spawn",
        "os.posix_spawnp",
        "pty.spawn",
        "asyncio.create_subprocess_exec",
        "asyncio.create_subprocess_shell",
    }
)
PROCESS_PREFIXES = ("subprocess.", "os.exec", "os.spawn")
PATH_CHANGES = frozenset({"insert", "append", "extend", "remove", "pop", "clear"})
STREAM_WRITES = frozenset({"sys.stdout.write", "sys.stderr.write", "sys.stdout.writelines", "sys.stderr.writelines"})
DIVISIONS = frozenset({"split", "rsplit", "partition", "rpartition"})
CACHE_VARIABLES = frozenset({"CRUCIBLE_CACHE_DIR", "XDG_CACHE_HOME"})
COST_SUFFIX = ".cost"
CTEST_FILES = frozenset({"LastTest.log", "LastTestsFailed.log", "CTestCostData.txt"})
FILE_NAME = "__file__"
DIRNAME = "os.path.dirname"


@dataclass(frozen=True, slots=True)
class Use:
    """One use of a job: its line and a short text of what it is."""

    line: int
    what: str


# A recognizer reads the nodes of one tree, from one walk, and the import table of the file.
type Recognizer = Callable[[Sequence[ast.AST], python_reader.Bindings], Iterator[Use]]


def outer_nodes(nodes: Sequence[ast.AST]) -> Iterator[ast.Name | ast.Attribute]:
    """Give each name and each chain of attributes that is not the inner part of a longer chain."""
    inner = {id(node.value) for node in nodes if isinstance(node, ast.Attribute)}
    for node in nodes:
        if isinstance(node, (ast.Name, ast.Attribute)) and id(node) not in inner:
            yield node


def calls(nodes: Sequence[ast.AST], table: python_reader.Bindings) -> Iterator[tuple[ast.Call, str]]:
    """Give each call of a function that has a dotted name, with that name."""
    for node in nodes:
        if isinstance(node, ast.Call):
            name = python_reader.dotted(node.func, table)
            if name is not None:
                yield node, name


def text_constants(nodes: Sequence[ast.AST]) -> Iterator[ast.Constant]:
    """Give each constant text of a tree, also each constant part of an f-string."""
    for node in nodes:
        if isinstance(node, ast.Constant) and isinstance(node.value, str):
            yield node


def subprocess_uses(nodes: Sequence[ast.AST], table: python_reader.Bindings) -> Iterator[Use]:
    """Give each use of the module subprocess and each process start of a different module."""
    for node in outer_nodes(nodes):
        name = python_reader.dotted(node, table)
        if name is not None and (name == "subprocess" or name.startswith(PROCESS_PREFIXES) or name in PROCESS_STARTS):
            yield Use(node.lineno, name)


def argparse_uses(nodes: Sequence[ast.AST], table: python_reader.Bindings) -> Iterator[Use]:
    """Give each import of argparse."""
    for node in nodes:
        if isinstance(node, ast.Import) and any(alias.name == "argparse" for alias in node.names):
            yield Use(node.lineno, "import argparse")
        elif isinstance(node, ast.ImportFrom) and node.module == "argparse":
            yield Use(node.lineno, "from argparse import")


def git_uses(nodes: Sequence[ast.AST], table: python_reader.Bindings) -> Iterator[Use]:
    """Give each list or tuple with the text "git" as its first item."""
    for node in nodes:
        if (
            isinstance(node, (ast.List, ast.Tuple))
            and node.elts
            and isinstance(node.elts[0], ast.Constant)
            and node.elts[0].value == "git"
        ):
            yield Use(node.lineno, "the words of a git command")


def path_uses(nodes: Sequence[ast.AST], table: python_reader.Bindings) -> Iterator[Use]:
    """Give each change of sys.path."""
    for call, name in calls(nodes, table):
        if name.startswith("sys.path.") and name.rsplit(".", 1)[1] in PATH_CHANGES:
            yield Use(call.lineno, f"{name}()")
    for statement in nodes:
        if isinstance(statement, ast.Assign):
            targets = statement.targets
        elif isinstance(statement, ast.AugAssign):
            targets = [statement.target]
        else:
            continue
        for target in targets:
            changed = target.value if isinstance(target, ast.Subscript) else target
            if python_reader.dotted(changed, table) == "sys.path":
                yield Use(statement.lineno, "an assignment to sys.path")


def print_uses(nodes: Sequence[ast.AST], table: python_reader.Bindings) -> Iterator[Use]:
    """Give each call of print() and each write to sys.stdout or sys.stderr."""
    for node, name in calls(nodes, table):
        if name == "print" or name in STREAM_WRITES:
            yield Use(node.lineno, f"{name}()")


def bar_uses(nodes: Sequence[ast.AST], table: python_reader.Bindings) -> Iterator[Use]:
    """Give each division of a text at a bar."""
    for node in nodes:
        if (
            isinstance(node, ast.Call)
            and isinstance(node.func, ast.Attribute)
            and node.func.attr in DIVISIONS
            and node.args
            and isinstance(node.args[0], ast.Constant)
            and isinstance(node.args[0].value, str)
            and node.args[0].value.strip() == "|"
        ):
            yield Use(node.lineno, f"{node.func.attr}({node.args[0].value!r})")


def holds_file_name(node: ast.AST) -> bool:
    """Return True when an expression reads __file__."""
    return any(isinstance(part, ast.Name) and part.id == FILE_NAME for part in ast.walk(node))


def root_uses(nodes: Sequence[ast.AST], table: python_reader.Bindings) -> Iterator[Use]:
    """Give each path two directories or more above __file__."""
    nested: set[int] = set()
    for node in nodes:
        if id(node) in nested:
            continue
        if (
            isinstance(node, ast.Subscript)
            and isinstance(node.value, ast.Attribute)
            and node.value.attr == "parents"
            and holds_file_name(node.value)
        ):
            yield Use(node.lineno, ".parents[N] of __file__")
        elif (
            isinstance(node, ast.Attribute)
            and node.attr == "parent"
            and isinstance(node.value, ast.Attribute)
            and node.value.attr == "parent"
            and holds_file_name(node.value)
        ):
            nested.update(id(part) for part in ast.walk(node))
            yield Use(node.lineno, ".parent.parent of __file__")
        elif (
            isinstance(node, ast.Call)
            and python_reader.dotted(node.func, table) == DIRNAME
            and node.args
            and isinstance(node.args[0], ast.Call)
            and python_reader.dotted(node.args[0].func, table) == DIRNAME
            and holds_file_name(node.args[0])
        ):
            nested.update(id(part) for part in ast.walk(node))
            yield Use(node.lineno, "os.path.dirname twice on __file__")


def cache_uses(nodes: Sequence[ast.AST], table: python_reader.Bindings) -> Iterator[Use]:
    """Give each name of a variable of the cache root."""
    for constant in text_constants(nodes):
        if constant.value in CACHE_VARIABLES:
            yield Use(constant.lineno, constant.value)


def cost_uses(nodes: Sequence[ast.AST], table: python_reader.Bindings) -> Iterator[Use]:
    """Give each text that ends with the suffix of a cost record."""
    for constant in text_constants(nodes):
        if str(constant.value).endswith(COST_SUFFIX):
            yield Use(constant.lineno, f"the suffix {COST_SUFFIX}")


def ctest_uses(nodes: Sequence[ast.AST], table: python_reader.Bindings) -> Iterator[Use]:
    """Give each name of a file that ctest writes."""
    for constant in text_constants(nodes):
        if str(constant.value).rsplit("/", 1)[-1] in CTEST_FILES:
            yield Use(constant.lineno, str(constant.value).rsplit("/", 1)[-1])


JOBS: Mapping[str, Recognizer] = {
    "subprocess": subprocess_uses,
    "argparse": argparse_uses,
    "git": git_uses,
    "sys.path": path_uses,
    "print": print_uses,
    "row-bar": bar_uses,
    "repository-root": root_uses,
    "cache-root": cache_uses,
    "cost-record": cost_uses,
    "ctest-log": ctest_uses,
}


def row_findings(registry: rule_registry.Registry) -> list[check_report.Finding]:
    """Give each job row with no recognizer, and each recognizer with no job row."""
    shown = rule_registry.REGISTRY_PATH
    rows = {job.entity for job in registry.jobs}
    found = [
        check_report.Finding(
            "error",
            shown,
            job.line,
            RULE,
            f"the job {job.entity} has no recognizer in the "
            f"table JOBS of {__name__}.  Write its recognizer, or remove the row",
            "unknown-job",
            job.entity,
        )
        for job in registry.jobs
        if job.entity not in JOBS
    ]
    found += [
        check_report.Finding(
            "error",
            shown,
            0,
            RULE,
            f"the job {entity} has a recognizer and no job row.  Add the row 'job {entity} module=OWNER | REASON'",
            "no-job-row",
            entity,
        )
        for entity in sorted(JOBS)
        if entity not in rows
    ]
    return found


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each use of a job that is not in one of its owners, and each job row and recognizer that do not agree."""
    found = row_findings(context.registry)
    owners = {job.entity: job for job in context.registry.jobs if job.entity in JOBS}
    scope = package_scope.read_package(context)
    found += package_scope.parse_findings(RULE, scope)
    for item in scope.files:
        if item.name == __name__:
            continue
        table = python_reader.bindings(item.parsed)
        nodes = tuple(ast.walk(item.parsed.tree))
        for entity, job in sorted(owners.items()):
            if item.name in job.modules:
                continue
            for use in JOBS[entity](nodes, table):
                found.append(
                    check_report.Finding(
                        "error",
                        item.path,
                        use.line,
                        RULE,
                        f"{use.what} does the job {entity}, and only "
                        f"{', '.join(job.modules)} can do it: {job.reason}.  Call the owner",
                        entity,
                        item.holder(use.line),
                    )
                )
    return found
