"""syscall-grant-coverage — the grant set of a grant file is the set of the system calls of its code.

THE RULE
    A grant row of the rule table names a grant file, its grant alias, the
    enumeration of the system calls and the door files.  The quarantine
    plugin of utils/tools/quarantine/ reads each unit that reads the grant
    file.  It finds the system calls of the code of the grant file, also
    through the functions of the door files.  It compares them with the
    enumerators that the alias grants (THE GRANTS in expression.cpp).  The
    kinds are:
      `not-granted`   a system call of the code that the alias does not grant
      `not-issued`    a grant of the alias that the code does not issue
      `wrong-helper`  a call of syscall() with the number of a different call
                      in a helper NAME_sys
      `unread-grant`  an alias, an enumeration, a grant set or a call set
                      that the unit cannot read
      `compared`      the line of each unit that compared the two sets.  It
                      is no error.
    The rule reads the finding lines in the sections of the objects of the
    target all of one build (assay.guard.engines.plugin.runner, THE
    SECTIONS).  Each line other than `compared` is an error.  A grant row of
    the rule table of the tree with no line `compared` is an error too.  No
    unit of the build then read its grant file.  Each problem of the census
    of the runner is an error too.

    The rule has no ledger.

Usage
    guard check --rule syscall-grant-coverage --build-dir DIR [--warnings-dir DIR]

Complexity: linear in the finding lines of the build and the grant rows.
"""

from assay.core import report as check_report
from assay.core import ruletable
from assay.core.paths import REPO_ROOT
from assay.guard.engines.plugin import runner

RULE = "syscall-grant-coverage"
COMPARED = "compared"
# The rule table of a tree, relative to its root.
TABLE = ruletable.TABLE.relative_to(REPO_ROOT)
MESSAGES = {
    "not-granted": "the code issues a system call that the grant alias does not grant",
    "not-issued": "the grant alias grants a system call that the code does not issue",
    "wrong-helper": "a helper NAME_sys calls syscall() with the number of a different system call",
    "unread-grant": "the unit cannot read the grant alias, its enumeration, its grants or its calls",
}


def grant_files(context: runner.PluginContext) -> list[str]:
    """Return the grant file of each grant row of the rule table of the tree, or none when the tree has no table.

    Raises:
        ruletable.TableError: If the table does not obey its format
    """
    path = context.root / TABLE
    return [grant.file for grant in ruletable.load(path).grants] if path.is_file() else []


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each difference of a grant set and each grant file that no unit compared.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    census = context.census()
    found = census.errors(RULE)
    compared: set[str] = set()
    for item in census.of_rule(RULE):
        if item.kind == COMPARED:
            compared.add(item.file)
            continue
        found.append(
            check_report.Finding(
                "error",
                item.file,
                item.line,
                RULE,
                f"{MESSAGES.get(item.kind, item.kind)} ({item.entity}).  Give the grant set the system calls of the "
                f"path",
                item.kind,
                item.entity,
            )
        )
    try:
        files = grant_files(context)
    except (ruletable.TableError, OSError) as problem:
        return [*found, check_report.Finding("error", str(TABLE), 0, RULE, f"the rule table cannot be read: {problem}")]
    found += [
        check_report.Finding(
            "error",
            file,
            0,
            RULE,
            "no unit of the build compared the grant set of this file with its system calls.  A unit of the target "
            "all must include the file",
            "unread-grant",
            file,
        )
        for file in files
        if file not in compared
    ]
    return found
