"""determinism — each listing of a directory is sorted, and only three modules read the wall clock.

THE RULE
    The sequence that the file system gives to the entries of a directory
    must not change the output of a module.  The time must not change it
    too.  The rule reads each module of the package and each master
    (assay.guard.engines.meta.package), and it refuses:
      listing     a listing of a directory that is not the first argument of
                  sorted(): a call of os.listdir, os.scandir, os.walk,
                  glob.glob or glob.iglob, and a call of the method `iterdir`,
                  `glob`, `rglob` or `walk` on an object that is not a module,
                  such as a Path.  A call of assay.core.paths.listing() is
                  sorted
      wall-clock  a read of the wall clock in a module that is not
                  assay.core.meter, assay.core.host or assay.loop.history:
                  time.time, time.time_ns, datetime.datetime.now,
                  datetime.datetime.utcnow, datetime.datetime.today,
                  datetime.date.today, and time.localtime, time.gmtime,
                  time.ctime, time.asctime and time.strftime with no time
                  argument
    The entity of a finding is the function that holds the call.

WHAT THE RULE DOES NOT READ
    A set gives its strings in a sequence that changes with PYTHONHASHSEED.
    A test of a golden output with two values of PYTHONHASHSEED holds that
    part, and this rule does not read it.

Complexity: linear in the length of the files of the scope.
"""

import ast

from assay.core import report as check_report
from assay.guard.engines.meta import package as package_scope
from assay.guard.engines.meta.runner import MetaContext
from assay.readers import python as python_reader

RULE = "determinism"
# The functions of a module that give a listing of a directory.
LISTING_FUNCTIONS = frozenset({"os.listdir", "os.scandir", "os.walk", "glob.glob", "glob.iglob"})
# The methods of an object that give a listing of a directory, such as the methods of a Path.
LISTING_METHODS = frozenset({"iterdir", "glob", "rglob", "walk"})
SORTED_LISTING = "assay.core.paths.listing"
# The reads of the wall clock with each argument, and the reads of the wall clock with no argument.
CLOCKS = frozenset(
    {
        "time.time",
        "time.time_ns",
        "datetime.datetime.now",
        "datetime.datetime.utcnow",
        "datetime.datetime.today",
        "datetime.date.today",
    }
)
CLOCKS_WITHOUT_TIME = frozenset({"time.localtime", "time.gmtime", "time.ctime", "time.asctime"})
STRFTIME = "time.strftime"
CLOCK_OWNERS = frozenset({"assay.core.meter", "assay.core.host", "assay.loop.history"})


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
    """Return True when a call reads the wall clock (THE RULE)."""
    name = python_reader.dotted(call.func, table)
    if name in CLOCKS:
        return True
    if name in CLOCKS_WITHOUT_TIME:
        return not call.args and not call.keywords
    return name == STRFTIME and len(call.args) == 1


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


def file_findings(item: package_scope.PythonFile) -> list[check_report.Finding]:
    """Give each listing that no sort holds and each read of the wall clock of one file."""
    table = python_reader.bindings(item.parsed)
    is_owner = item.name in CLOCK_OWNERS
    covered = sorted_arguments(item.parsed.tree, table)
    found: list[check_report.Finding] = []
    for node in ast.walk(item.parsed.tree):
        if not isinstance(node, ast.Call):
            continue
        if is_listing(node, table) and id(node) not in covered:
            found.append(
                check_report.Finding(
                    "error",
                    item.path,
                    node.lineno,
                    RULE,
                    "a listing of a directory gives its entries in the sequence of "
                    "the file system.  Give the call to sorted(), or call assay.core.paths.listing()",
                    "listing",
                    item.holder(node.lineno),
                )
            )
        elif not is_owner and is_clock_read(node, table):
            found.append(
                check_report.Finding(
                    "error",
                    item.path,
                    node.lineno,
                    RULE,
                    "a read of the wall clock makes the output change with the "
                    "time.  Read the clock in assay.core.meter, assay.core.host or assay.loop.history",
                    "wall-clock",
                    item.holder(node.lineno),
                )
            )
    return found


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each listing that no sort holds, and each read of the wall clock that its owners do not do."""
    return package_scope.each_file(context, RULE, file_findings)
