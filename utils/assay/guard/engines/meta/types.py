"""types — a clean run of the pinned mypy in strict mode on the modules of the package and the masters.

THE RUN
    The rule starts the pinned mypy (`setup install mypy`, assay.setup.install)
    through assay.core.proc, in the directory utils/ of the tree, on each file
    of the scope (assay.guard.engines.meta.package).  The options are
    --strict, --output json, --scripts-are-modules, --python-version 3.14 and
    --no-sqlite-cache.  The cache of mypy is the cache "mypy" of the cache
    root (assay.core.cache).  mypy checks the hash of each file against its
    cache, and each run writes a cache file with one rename.  The runs of the
    tree and of the plants can then share the cache.  The SQLite cache of
    mypy has a log file for each database.  On 2026-10-08, runs at the same
    time increased these log files to 1 GiB in all, and the files of the
    cache took 9 MB.

THE ONE RUN AT A TIME
    mypy keeps a module in a meta file and a data file.  It writes the two
    files one after the other.  The meta file holds the time of the data
    file in full seconds.  A run can do a check of a module again with no
    change of its interface.  Then mypy writes no data file, and it reads
    the time of the data file on the disk.

    Two runs of two trees at one time can put the meta file of one tree
    adjacent to the data file of the other.  A subsequent run of
    the first tree reads the interface of the second tree.  A run with no
    cache gives no such interface.

    The rule holds the lock of the cache in the exclusive mode for the run
    (assay.core.cache, exclusive_use()).  Two runs of the rule then use the
    cache one after the other.  On 2026-10-17, 5 of 20 pairs of runs at one
    time on a planted cache read the interface of the other tree.  20 pairs
    of runs one after the other gave none.

THE BOUND
    Before its run, the rule keeps the cache in its row of LIMITS at its turn
    (assay.core.cache, A TREE OF FILES).  An entry is a file with a suffix of
    ENTRY_SUFFIXES.  The turn also removes each file with a suffix of
    UNREAD_SUFFIXES.  These files are the databases of the SQLite cache, and
    no run with --no-sqlite-cache reads them.  mypy reads the data file of a
    module after its meta file.  A data file that goes between the two reads
    stops the run.  The rule holds the lock of the cache for the run.  No
    eviction takes its turn during the run (THE ONE RUN AT A TIME).

THE BROKEN CACHE
    A rename puts each cache file at its path, but no run calls fsync.  A
    host that stops before the kernel writes the bytes can then keep an empty
    cache file.  mypy stops with an internal error on such a file, in each
    run of each tree that shares the cache.  On 2026-10-18, a restart of the
    host left 397 empty files, and no run of the rule passed.  When a run
    of mypy gives an internal error (CRASH_WORDS), the rule removes the cache
    of the version of Python of the run.  Then it runs mypy one more time
    with a cold cache, which costs one cold run.  A second internal error is
    a failure of mypy, and the rule gives it.

THE RULE
    Each error of mypy is a finding.  The kind is the error code of mypy,
    such as type-arg, and the entity is the function or the class that holds
    the line.  A note of mypy is no finding.  assay.readers.tool_reports reads
    the report.

THE TOOL
    A tree with no installed mypy at the pin gives NotApplicable, and the test
    of the rule then gives the skip status.  `python3 utils/bin/setup install
    mypy` installs it.

Complexity: one run of mypy.  On 2026-10-08 a cold run on the package took
3.8 s and a warm run 0.1 s.
"""

import shutil
from pathlib import Path

from assay.core import cache, proc
from assay.core import report as check_report
from assay.guard.engines.meta import package as package_scope
from assay.guard.engines.meta.runner import MetaContext, pinned_program
from assay.readers import python as python_reader
from assay.readers import tool_reports

RULE = "types"
UTILS = "utils"
# The version of Python of the run.  mypy keeps the cache of the run in a directory of this name.
PYTHON_VERSION = "3.14"
OPTIONS = (
    "--strict",
    "--output",
    "json",
    "--scripts-are-modules",
    "--python-version",
    PYTHON_VERSION,
    "--no-color-output",
    "--no-error-summary",
    "--no-sqlite-cache",
)
# The words that mypy writes to its standard error for an internal error (THE BROKEN CACHE).
CRASH_WORDS = "INTERNAL ERROR"
CACHE_NAME = "mypy"
# The suffixes of the files of the cache of mypy: the format ff of mypy, and the format JSON (THE BOUND).
ENTRY_SUFFIXES = (".ff", ".json")
# The suffixes of the files of the SQLite cache of mypy, which no run with --no-sqlite-cache reads (THE BOUND).
UNREAD_SUFFIXES = (".db", ".db-wal", ".db-shm", ".db-journal")
# The cache of mypy when the caches are off: mypy writes no cache to /dev/null.
NO_CACHE = "/dev/null"
# The exit statuses of mypy: no error, and errors in the report.
CLEAN, ERRORS = 0, 1


def run_mypy(context: MetaContext, files: list[str], site: Path | None = None) -> str:
    """Run the pinned mypy on files of the directory utils/ of a tree, and return its report (THE RUN).

    The run waits for each other run of the rule on the same cache (THE ONE RUN AT A TIME).  `site` is the site
    directory of a mypy, and the pinned mypy when it is None.  A test gives a planted mypy.

    Raises:
        NotApplicable: If mypy is not installed at its pin
        proc.ToolFailed: If mypy cannot start, runs longer than its time limit, or ends with a status that is not 0
            or 1.  A crash on a broken cache gives the status of the run with a cold cache (THE BROKEN CACHE)
    """
    from assay.setup import install

    site = pinned_program("mypy") if site is None else site
    directory = cache.cache_root(CACHE_NAME)
    arguments = [*OPTIONS, "--cache-dir", NO_CACHE if directory is None else str(directory), *files]
    command = proc.Command(install.mypy_command(site, arguments), timeout_s=proc.TOOL_S, cwd=context.root / UTILS)
    if directory is None:
        completed = proc.run(command)
    else:
        cache.hold_tree_bound(directory, cache.LIMITS[CACHE_NAME], ENTRY_SUFFIXES, UNREAD_SUFFIXES)
        with cache.exclusive_use(directory):
            completed = proc.run(command)
            if completed.exit not in (CLEAN, ERRORS) and CRASH_WORDS in completed.err:
                shutil.rmtree(directory / PYTHON_VERSION, ignore_errors=True)
                completed = proc.run(command)
    if completed.exit not in (CLEAN, ERRORS):
        proc.require(completed)
    return completed.out


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each error of mypy in strict mode on the package and the masters of the tree (THE RULE)."""
    scope = package_scope.read_package(context)
    found = package_scope.parse_findings(RULE, scope)
    by_path = {item.path: item for item in scope.files}
    files = [item.path.removeprefix(f"{UTILS}/") for item in scope.files]
    if not files:
        return found
    for issue in tool_reports.mypy_issues(run_mypy(context, files)):
        path = f"{UTILS}/{issue.path}"
        item = by_path.get(path)
        holder = item.holder(issue.line) if item is not None else python_reader.MODULE_SCOPE
        found.append(
            check_report.Finding(
                "error",
                path,
                issue.line,
                RULE,
                f"mypy in strict mode refuses the code: {issue.message}.  Give the code the types that mypy asks for",
                issue.code,
                holder,
            )
        )
    return found
