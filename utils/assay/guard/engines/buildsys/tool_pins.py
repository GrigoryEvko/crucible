"""tool-pins — each row of utils/pins.txt agrees with its holder, and the cmake and the ctest of a build with the pin.

THE RULE
    The rule calls check_table() of assay.setup.pins, which `setup check`
    calls too.  The head comment of that module gives the holder of each pin
    name and the check of each holder.  With --build-dir, the rule also holds
    the ctest that runs it to its pin.  It holds the cmake, the ctest and the
    C++ compiler of the CMake cache of the build to their pins too.  The
    ctest of the build runs the rule in the ctest test guard_tool_pins.  The
    rule has no ledger, and each finding is an error at its row of the pin
    table.

THE PLANTS
    A plant holds a planted pin table utils/pins.txt, the file build.txt of
    assay.guard.engines.buildsys.planted for its build directory, and the
    file host.txt.  host.txt tells what the host of the plant holds, in the
    rows grammar of assay.core.rows:

        format 1
        process PID PARENT NAME [| EXE]   a process of the copy of /proc.  The
                                          first row is the process of the rule
        program PATH [status=N] | LINE    a program at PATH of the copy that
                                          prints LINE and exits with N
        installed TOOL                    the directory of a tool of
                                          assay.setup.install for this host in
                                          the tools directory, with no stamp
        `partial` PIN VERSION             the directory of an install of a pin
                                          of the toolchain of the oracle that
                                          stopped

    EXE and a cache value of build.txt can hold {root}, the root of the copy.
    The tools directory is tools/ of the copy, the toolchain directory of the
    oracle is oracle/, and the copy of /proc is proc/.

Complexity: linear in the rows of the pin table, with one run of each program of the build.
"""

from pathlib import Path

from assay.core import pins as pin_table
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.buildsys import planted
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.setup import pins

HOST_FILE = "host.txt"
FORMAT = "1"
TOOLS = "tools"
ORACLE = "oracle"
PROC = "proc"
PROGRAM_FIELDS = frozenset({"status"})
# The largest exit status of a planted program.
STATUS_LIMIT = 255
# The largest process ID of a planted process.
PID_LIMIT = 1 << 22


class PinContext(BuildContext):
    """The context of a plant: the build, and the tools directory, the toolchain of the oracle and the processes.

    `pid` is the process of the rule in the copy of /proc.
    """

    __slots__ = ("oracle_root", "pid", "proc_root", "tools_root")

    def __init__(self, root: Path, build_dir: Path | None, request: pins.TableCheck) -> None:
        """Keep the root and the build directory, and the directories and the process of the request."""
        super().__init__(root, build_dir)
        self.tools_root = request.tools_root
        self.oracle_root = request.oracle_root
        self.proc_root = request.proc_root
        self.pid = request.pid


def findings(context: BuildContext) -> list[check_report.Finding]:
    """Give the errors of check_table() for the tree and the build of the context (THE RULE)."""
    if isinstance(context, PinContext):
        request = pins.TableCheck(
            context.root, context.build_dir, context.tools_root, context.oracle_root, context.proc_root, context.pid
        )
    else:
        request = pins.TableCheck(context.root, context.build_dir)
    return pins.check_table(request)


# ── The plants ─────────────────────────────────────────────────────────────


def write_program(path: Path, line: str, status: int) -> None:
    """Write a shell program that prints a line and exits with a status."""
    path.parent.mkdir(parents=True, exist_ok=True)
    quoted = line.replace("'", "'\\''")
    path.write_text(f"#!/bin/sh\nprintf '%s\\n' '{quoted}'\nexit {status}\n", encoding="utf-8")
    path.chmod(0o755)


def relative_path(text: str, row: rows_grammar.Row) -> str:
    """Return a path of the copy that a row names.

    Raises:
        rows_grammar.RowsError: If the path is absolute or goes out of the copy
    """
    if text.startswith("/") or ".." in text.split("/"):
        raise row.error(f"the path {text!r} is not a relative path in the copy")
    return text


def plant_installed(root: Path, tools_root: Path, row: rows_grammar.Row) -> None:
    """Make the directory of a tool of the planted table for this host, with no stamp.

    Raises:
        rows_grammar.RowsError: If the row names no tool, or the planted table has no pin of the tool for this host
    """
    from assay.setup import install

    tool = install.TOOLS.get(row.words[1]) if len(row.words) == 2 else None
    if tool is None:
        raise row.error(f"an installed row is 'installed TOOL', with a tool of {', '.join(install.TOOLS)}")
    table = pin_table.load(root / pin_table.PINS_PATH)
    try:
        directory = install.directory_of(tool, install.pin_of(tool, table), tools_root, table)
    except install.InstallError as problem:
        raise row.error(str(problem)) from None
    directory.mkdir(parents=True)


def read_host(root: Path, request: pins.TableCheck) -> pins.TableCheck:
    """Write what the rows of host.txt give, and return the request with the process of the rule (THE PLANTS).

    Complexity: linear in the rows.

    Raises:
        rows_grammar.RowsError: If a row does not obey its shape
    """
    rows = rows_grammar.read(root / HOST_FILE, HOST_FILE)
    if not rows or rows[0].words != ("format", FORMAT):
        raise rows_grammar.RowsError(HOST_FILE, rows[0].line if rows else 0, f"the first row is 'format {FORMAT}'")
    pid: int | None = None
    for row in rows[1:]:
        kind = row.words[0]
        if kind == "process" and len(row.words) == 4 and len(row.cells) <= 1:
            number = rows_grammar.number(row.words[1], PID_LIMIT, row, "process ID")
            exe = Path(row.cells[0].replace("{root}", str(root))) if row.cells else None
            pins.write_process(
                request.proc_root,
                number,
                rows_grammar.number(row.words[2], PID_LIMIT, row, "parent"),
                row.words[3],
                exe,
            )
            pid = number if pid is None else pid
        elif kind == "program" and len(row.words) >= 2 and len(row.cells) == 1:
            status = rows_grammar.fields(row, PROGRAM_FIELDS, start=2).get("status", "0")
            write_program(
                root / relative_path(row.words[1], row),
                row.cells[0],
                rows_grammar.number(status, STATUS_LIMIT, row, "status"),
            )
        elif kind == "installed":
            plant_installed(root, request.tools_root or root / TOOLS, row)
        elif kind == "partial" and len(row.words) == 3:
            oracle_root = request.oracle_root or root / ORACLE
            (oracle_root / f"{row.words[1]}-{row.words[2]}.partial").mkdir(parents=True)
        else:
            raise row.error(f"the row kind {kind!r} or its words do not have a shape of {HOST_FILE}")
    return pins.TableCheck(
        request.root, request.build_dir, request.tools_root, request.oracle_root, request.proc_root, pid
    )


def plant_context(plant: Path, scratch: Path) -> PinContext:
    """Copy a plant, make its planted build and what host.txt gives, and return its context (THE PLANTS).

    Raises:
        NotApplicable: If build.txt or host.txt does not obey its rows
    """
    built = planted.plant_context(plant, scratch)
    request = pins.TableCheck(scratch, built.build_dir, scratch / TOOLS, scratch / ORACLE, scratch / PROC)
    (scratch / TOOLS).mkdir(exist_ok=True)
    (scratch / ORACLE).mkdir(exist_ok=True)
    (scratch / PROC).mkdir(exist_ok=True)
    if (scratch / HOST_FILE).is_file():
        try:
            request = read_host(scratch, request)
        except rows_grammar.RowsError as problem:
            raise meta_runner.NotApplicable(f"the planted host does not read: {problem}") from None
    return PinContext(scratch, built.build_dir, request)
