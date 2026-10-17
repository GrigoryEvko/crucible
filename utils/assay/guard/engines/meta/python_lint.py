"""python-lint — a clean run of the pinned ruff, and no change from ruff format, on the package and the masters.

THE RUN
    The rule starts the pinned ruff (`setup install ruff`, assay.setup.install)
    through assay.core.proc, in the directory utils/ of the tree, on each file
    of the scope (assay.guard.engines.meta.package).  It starts `ruff check`
    and `ruff format --check`, each with --isolated, --no-cache,
    --target-version py314, --line-length 120 and --output-format json.  With
    --isolated, ruff reads no configuration file, and the rules are the
    default rules of the pinned version.  With --no-cache, ruff writes no
    cache into the tree.

THE RULE
    Each issue of ruff check is a finding with the code of ruff as its kind,
    such as F401.  Each part of a file that ruff format changes is a finding
    of the kind format.  The entity is the function or the class that holds
    the line.  assay.readers.tool_reports reads the two reports.

THE TOOL
    A tree with no installed ruff at the pin gives NotApplicable, and the test
    of the rule then gives the skip status.  `python3 utils/bin/setup install
    ruff` installs it.

THE THREADS
    ruff starts one thread for each CPU of the host in its pool, and each
    thread waits in a loop for work.  On 2026-10-17, the build host had 256
    CPUs.  There, a check of the package took 2.57 s of wall time and 3.64 s
    of system time.  At RUFF_THREADS threads, it took 2.58 s and 0.28 s.  The
    witness of the rule fell from 3.69 s to 0.09 s of system time.  The rule
    then gives ruff the variable RAYON_NUM_THREADS.

Complexity: two runs of ruff.  On 2026-10-08 each run on the package took
0.1 s.
"""

from pathlib import Path

from assay.core import proc
from assay.core import report as check_report
from assay.guard.engines.meta import package as package_scope
from assay.guard.engines.meta.runner import MetaContext, pinned_program
from assay.readers import python as python_reader
from assay.readers import tool_reports

RULE = "python-lint"
UTILS = "utils"
OPTIONS = ("--isolated", "--no-cache", "--target-version", "py314", "--line-length", "120", "--output-format", "json")
FORMAT_KIND = "format"
# The exit statuses of ruff: no issue, and issues in the report.
CLEAN, ISSUES = 0, 1
# The threads of the pool of ruff, and the variable that sets them (THE THREADS).
RUFF_THREADS = 8
THREADS_VARIABLE = "RAYON_NUM_THREADS"


def run_ruff(context: MetaContext, program: Path, words: list[str], files: list[str]) -> str:
    """Run the pinned ruff with a subcommand on files of the directory utils/ of a tree, and return its report.

    Raises:
        proc.ToolFailed: If ruff cannot start, runs longer than its time limit, or ends with a status that is not 0
            or 1
    """
    completed = proc.run(
        proc.Command(
            [str(program), *words, *OPTIONS, *files],
            timeout_s=proc.TOOL_S,
            cwd=context.root / UTILS,
            env=proc.environment({THREADS_VARIABLE: str(RUFF_THREADS)}),
        )
    )
    if completed.exit not in (CLEAN, ISSUES):
        proc.require(completed)
    return completed.out


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each issue of ruff check, and each part that ruff format changes, in the package and the masters."""
    scope = package_scope.read_package(context)
    found = package_scope.parse_findings(RULE, scope)
    by_path = {item.path: item for item in scope.files}
    files = [item.path.removeprefix(f"{UTILS}/") for item in scope.files]
    if not files:
        return found
    program = pinned_program("ruff")
    utils = (context.root / UTILS).resolve()
    reports = (
        (run_ruff(context, program, ["check"], files), None),
        (run_ruff(context, program, ["format", "--check"], files), FORMAT_KIND),
    )
    for report, kind in reports:
        for issue in tool_reports.ruff_issues(report):
            path = f"{UTILS}/{Path(issue.path).resolve().relative_to(utils).as_posix()}"
            item = by_path.get(path)
            holder = item.holder(issue.line) if item is not None else python_reader.MODULE_SCOPE
            action = (
                "Format the file with the pinned ruff"
                if kind == FORMAT_KIND
                else "Correct the code, or give the line the form that the rule of ruff asks for"
            )
            found.append(
                check_report.Finding(
                    "error",
                    path,
                    issue.line,
                    RULE,
                    f"ruff: {issue.code} {issue.message}.  {action}",
                    kind or issue.code,
                    holder,
                )
            )
    return found
