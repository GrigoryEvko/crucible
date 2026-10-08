"""exit-codes — sys.exit and SystemExit take only a member of assay.core.exit.ExitCode.

THE RULE
    The package has one enumeration of the exit statuses, ExitCode of
    assay.core.exit, and each status has one name.  The rule reads each
    module of the package and each master (assay.guard.engines.meta.package).
    It refuses an exit with a status that is not a member of ExitCode:
      exit-code  a call of sys.exit or os._exit, a call of the builtin `exit`
                 or `quit`, and a raise of SystemExit, each with no argument
                 or with an argument that is not a member of ExitCode
    A member is ExitCode.NAME or ExitCode(VALUE).  The name ExitCode, or the
    module before it, comes from an import of assay.core.exit.  The entity of
    a finding is the function that holds the exit.

Complexity: linear in the length of the files of the scope.
"""

import ast

from assay.core import report as check_report
from assay.guard.engines.meta import package as package_scope
from assay.guard.engines.meta.runner import MetaContext
from assay.readers import python as python_reader

RULE = "exit-codes"
ENUMERATION = "assay.core.exit.ExitCode"
EXITS = frozenset({"sys.exit", "os._exit", "exit", "quit"})
SYSTEM_EXIT = "SystemExit"
# The longest text of a status expression that a message shows.
SHOWN_LENGTH = 80


def is_member(node: ast.expr, table: python_reader.Bindings) -> bool:
    """Return True when an expression is a member of ExitCode: ExitCode.NAME or ExitCode(VALUE) of assay.core.exit."""
    if isinstance(node, ast.Attribute):
        return python_reader.dotted(node.value, table) == ENUMERATION
    if isinstance(node, ast.Call):
        return python_reader.dotted(node.func, table) == ENUMERATION
    return False


def exit_status(node: ast.AST, table: python_reader.Bindings) -> tuple[str, ast.expr | None] | None:
    """Return the name of the exit call and its status expression, or None when the node is no exit."""
    if isinstance(node, ast.Call):
        name = python_reader.dotted(node.func, table)
        if name in EXITS:
            return f"{name}()", node.args[0] if node.args else None
    if isinstance(node, ast.Raise) and node.exc is not None:
        exception = node.exc.func if isinstance(node.exc, ast.Call) else node.exc
        if python_reader.dotted(exception, table) == SYSTEM_EXIT:
            arguments = node.exc.args if isinstance(node.exc, ast.Call) else []
            return SYSTEM_EXIT, arguments[0] if arguments else None
    return None


def file_findings(item: package_scope.PythonFile) -> list[check_report.Finding]:
    """Give each exit of one file with a status that is not a member of ExitCode."""
    table = python_reader.bindings(item.parsed)
    found: list[check_report.Finding] = []
    for node in ast.walk(item.parsed.tree):
        exit_call = exit_status(node, table)
        if exit_call is None:
            continue
        call_name, status = exit_call
        if status is not None and is_member(status, table):
            continue
        line = getattr(node, "lineno", 0)
        shown = "no status" if status is None else f"the status {' '.join(ast.unparse(status).split())[:SHOWN_LENGTH]}"
        found.append(
            check_report.Finding(
                "error",
                item.path,
                line,
                RULE,
                f"{call_name} takes {shown}, which is not a member of ExitCode.  Give a "
                f"member of assay.core.exit.ExitCode",
                "exit-code",
                item.holder(line),
            )
        )
    return found


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give each exit of the package and the masters with a status that is not a member of ExitCode."""
    return package_scope.each_file(context, RULE, file_findings)
