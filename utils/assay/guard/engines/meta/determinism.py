"""determinism — a rule module reads no clock, no git history and no state of the host, and its verdict reads the tree.

THE RULE
    A verdict reads only the tree.  The rule reads each module of SCOPE,
    the modules of the rules, and it refuses four kinds of read:
      clock    a read of the wall clock, of the date or of a time measure.
               CLOCKS names the calls.  time.localtime, time.gmtime,
               time.ctime, time.asctime and time.strftime read the clock
               when the call gives no time
      history  a call of a function of assay.core.git that reads a commit or
               a ref (READS_HISTORY of that module).  A call of git.run() or
               git.run_any_exit() with a command of HISTORY_COMMANDS, such
               as log, blame or rev-list, is one too
      host     a read of the state of the host: a call of HOST_READS, such
               as os.getloadavg or os.cpu_count.  A text that names a file
               of /proc/ or /sys/ is one too, and a docstring is no such text
      listing  a listing of a directory that is not the first argument of
               sorted().  LISTING_FUNCTIONS names the functions.
               LISTING_METHODS names the methods of an object that is not a
               module, such as a Path.  The file system of the host gives
               the sequence of the entries.  A call of
               assay.core.paths.listing() is sorted
    The entity of a finding is the function that holds the call.

THE LEDGER
    A row of utils/ledgers/determinism.txt admits the findings of one kind
    in one function: `PATH | KIND | admit | ENTITY | REASON`.  The reason
    tells why no verdict reads the value.  Examples are a job count, a time
    that only a printed line shows, and a ledger write.  A ledger write reads
    the ledger at the base of the change.  A row that admits no finding gives
    no finding, and a write removes it.

WHAT THE RULE DOES NOT READ
    A set gives its strings in a sequence that changes with PYTHONHASHSEED.
    A test of a golden output with two values of PYTHONHASHSEED holds that
    part, and this rule does not read it.  The rule reads the calls of a
    module, and not the calls of the modules that it imports.

Complexity: linear in the length of the files of the scope.
"""

import ast

from assay.core import git
from assay.core import report as check_report
from assay.guard.engines.meta import package as package_scope
from assay.guard.engines.meta.runner import MetaContext
from assay.readers import python as python_reader

RULE = "determinism"
# The modules of the rules, the scope of the row of the rule in utils/registry.txt.
SCOPE = "utils/assay/guard/engines/"
# The functions of a module that give a listing of a directory.
LISTING_FUNCTIONS = frozenset({"os.listdir", "os.scandir", "os.walk", "glob.glob", "glob.iglob"})
# The methods of an object that give a listing of a directory, such as the methods of a Path.
LISTING_METHODS = frozenset({"iterdir", "glob", "rglob", "walk"})
SORTED_LISTING = "assay.core.paths.listing"
# The reads of the clock with each argument, and the reads of the wall clock with no argument.
CLOCKS = frozenset(
    {
        "time.time",
        "time.time_ns",
        "time.monotonic",
        "time.monotonic_ns",
        "time.perf_counter",
        "time.perf_counter_ns",
        "time.process_time",
        "time.process_time_ns",
        "time.thread_time",
        "time.thread_time_ns",
        "datetime.datetime.now",
        "datetime.datetime.utcnow",
        "datetime.datetime.today",
        "datetime.date.today",
    }
)
CLOCKS_WITHOUT_TIME = frozenset({"time.localtime", "time.gmtime", "time.ctime", "time.asctime"})
STRFTIME = "time.strftime"
# The dotted name of each function of assay.core.git, and the two functions that run a git command of the caller.
GIT_PREFIX = "assay.core.git."
GIT_COMMANDS = frozenset({"run", "run_any_exit"})
# The git commands that read a commit or a ref.
HISTORY_COMMANDS = frozenset(
    {
        "blame",
        "cat-file",
        "describe",
        "diff",
        "log",
        "merge-base",
        "reflog",
        "rev-list",
        "rev-parse",
        "shortlog",
        "show",
        "status",
    }
)
# The calls that read the state of the host, and the directories of the files of the kernel that hold it.
HOST_READS = frozenset(
    {
        "os.getloadavg",
        "os.cpu_count",
        "os.process_cpu_count",
        "os.sched_getaffinity",
        "multiprocessing.cpu_count",
        "shutil.disk_usage",
    }
)
HOST_FILES = ("/proc/", "/sys/")
ACTION = "or add a row with the reason why no verdict reads the value"


def is_listing(call: ast.Call, table: python_reader.Bindings) -> bool:
    """Return True when a call gives a listing of a directory (THE RULE)."""
    name = python_reader.dotted(call.func, table)
    if name in LISTING_FUNCTIONS:
        return True
    if not isinstance(call.func, ast.Attribute) or call.func.attr not in LISTING_METHODS:
        return False
    receiver = python_reader.dotted(call.func.value, table)
    return receiver is None or not table.is_module(receiver.split(".", 1)[0])


def is_clock_read(call: ast.Call, table: python_reader.Bindings) -> bool:
    """Return True when a call reads the clock (THE RULE)."""
    name = python_reader.dotted(call.func, table)
    if name in CLOCKS:
        return True
    if name in CLOCKS_WITHOUT_TIME:
        return not call.args and not call.keywords
    return name == STRFTIME and len(call.args) == 1


def is_history_read(call: ast.Call, table: python_reader.Bindings) -> bool:
    """Return True when a call reads a commit or a ref through assay.core.git (THE RULE)."""
    name = python_reader.dotted(call.func, table)
    if name is None or not name.startswith(GIT_PREFIX):
        return False
    function = name.removeprefix(GIT_PREFIX)
    if function in git.READS_HISTORY:
        return True
    if function not in GIT_COMMANDS or len(call.args) < 2:
        return False
    command = call.args[1]
    return isinstance(command, ast.Constant) and command.value in HISTORY_COMMANDS


def is_host_read(call: ast.Call, table: python_reader.Bindings) -> bool:
    """Return True when a call reads the state of the host (THE RULE)."""
    return python_reader.dotted(call.func, table) in HOST_READS


def docstring_ids(tree: ast.Module) -> set[int]:
    """Return the id of the constant of each docstring of a module, a class and a function."""
    scopes = [tree, *(node for _, node in python_reader.scope_nodes(tree))]
    return {id(constant) for scope in scopes if (constant := python_reader.docstring_of(scope)) is not None}


def host_files(tree: ast.Module) -> set[int]:
    """Return the id of each text of a module that names a file of /proc/ or /sys/ (THE RULE).

    A text names such a file when it starts with a directory of HOST_FILES and
    holds more than the directory.  An f-string names one when its first part
    starts with such a directory and a value comes after it, as in
    f"/proc/{pid}/stat".  The parts of an f-string are no text of their own,
    and a docstring is none.
    """
    docstrings = docstring_ids(tree)
    joined = [node for node in ast.walk(tree) if isinstance(node, ast.JoinedStr)]
    excluded = docstrings | {id(part) for node in joined for part in node.values}
    found: set[int] = set()
    for text in joined:
        head = text.values[0] if len(text.values) > 1 else None
        if isinstance(head, ast.Constant) and isinstance(head.value, str) and head.value.startswith(HOST_FILES):
            found.add(id(text))
    for node in ast.walk(tree):
        if not isinstance(node, ast.Constant) or not isinstance(node.value, str) or id(node) in excluded:
            continue
        if node.value.startswith(HOST_FILES) and node.value not in HOST_FILES:
            found.add(id(node))
    return found


def sorted_arguments(tree: ast.Module, table: python_reader.Bindings) -> set[int]:
    """Return the id of each call that sorted() sorts, and of each call of paths.listing().

    sorted() sorts the call that is its first argument, and the call that a
    comprehension of its first argument reads, as in
    sorted(path for path in directory.iterdir() if path.is_dir()).
    """
    found: set[int] = set()
    for node in ast.walk(tree):
        if not isinstance(node, ast.Call):
            continue
        name = python_reader.dotted(node.func, table)
        if name == SORTED_LISTING:
            found.add(id(node))
        if name != "sorted" or not node.args:
            continue
        argument = node.args[0]
        if isinstance(argument, ast.Call):
            found.add(id(argument))
        elif isinstance(argument, (ast.GeneratorExp, ast.ListComp, ast.SetComp)):
            found.update(
                id(generator.iter) for generator in argument.generators if isinstance(generator.iter, ast.Call)
            )
    return found


def kind_of(node: ast.AST, table: python_reader.Bindings, covered: set[int], files: set[int]) -> str | None:
    """Return the kind of THE RULE that a node of a module gives, or None.

    `covered` holds each call that sorted() sorts, and `files` each text that names a file of the kernel.
    """
    if id(node) in files:
        return "host"
    if not isinstance(node, ast.Call):
        return None
    if is_listing(node, table) and id(node) not in covered:
        return "listing"
    if is_clock_read(node, table):
        return "clock"
    if is_history_read(node, table):
        return "history"
    return "host" if is_host_read(node, table) else None


# The message of each kind.
MESSAGES = {
    "clock": f"a read of the clock makes the verdict change with the time.  Judge the facts of the tree, {ACTION}",
    "history": (
        f"a read of the git history makes the verdict change with the commits, and not with the tree.  Judge the "
        f"files of the tree, {ACTION}"
    ),
    "host": (
        f"a read of the state of the host makes the verdict change from one host to a different host.  Judge the "
        f"facts of the tree, {ACTION}"
    ),
    "listing": (
        "a listing of a directory gives its entries in the sequence of the file system of the host.  Give the call "
        "to sorted(), or call assay.core.paths.listing()"
    ),
}


def file_findings(item: package_scope.PythonFile) -> list[check_report.Finding]:
    """Give each read of the clock, of the git history or of the host, and each listing that no sort holds."""
    if not item.path.startswith(SCOPE):
        return []
    table = python_reader.bindings(item.parsed)
    covered = sorted_arguments(item.parsed.tree, table)
    files = host_files(item.parsed.tree)
    found: list[check_report.Finding] = []
    for node in ast.walk(item.parsed.tree):
        kind = kind_of(node, table, covered, files)
        if kind is None:
            continue
        line = getattr(node, "lineno", 0)
        found.append(check_report.Finding("error", item.path, line, RULE, MESSAGES[kind], kind, item.holder(line)))
    return found


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each read of the clock, of the git history or of the host in a rule module, and each unsorted listing.

    A file of SCOPE that does not parse gives the error of the kind parse.
    """
    package = package_scope.read_package(context)
    found = [item for item in package_scope.parse_findings(RULE, package) if item.path.startswith(SCOPE)]
    for item in package.files:
        found += file_findings(item)
    return found
