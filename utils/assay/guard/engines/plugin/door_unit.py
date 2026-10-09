"""door-unit — a door is one source file, and it includes the system headers of one family.

THE RULE
    A door row of utils/rule-table.txt names the one file of the source root
    that can include a system header (CLAUDE.md section XXII, R4).  The
    quarantine plugin gives two kinds (utils/tools/quarantine/include.cpp,
    THE DOOR UNITS):
      * header-door: an include of a door header by its owner, when the owner
        is a header and no source file.  Each unit that includes the owner
        then gets the system declarations of the door.  The entity is the
        door header
      * mixed-headers: the include that adds the second family of system
        headers to a door file.  The two families are the Linux UAPI headers
        and the headers of the C library, and some of their declarations
        conflict.  The entity is the first header of each family.

THE LEDGER
    utils/ledgers/door-unit.txt counts the findings of each file, kind and
    entity (the policy count), and each row gives a reason:
        PATH | KIND | COUNT | ENTITY | REASON
    The reason names the part of the plan that moves the door into a source
    file of src/fixy/os/.  A new finding is more than its count and fails,
    and a row above the count of its findings fails until `guard write`
    writes it again.  A row with no reason is an error.

Usage
    guard check --rule door-unit --build-dir DIR [--warnings-dir DIR]
    guard write --rule door-unit --build-dir DIR [--reason TEXT]

Complexity: linear in the finding lines of the build, times the items of the scope.
"""

from assay.core import report as check_report
from assay.guard.engines.plugin import declarations, names, runner
from assay.readers import sections as plugin_sections

RULE = "door-unit"
ACTIONS = {
    "header-door": "has a header as its door, so each unit that includes the header gets the system declarations of "
    "the door.  Move the door into a source file of src/fixy/os/ or src/foundation/os/, and keep only declarations "
    "of the base in the header",
    "mixed-headers": "are the first Linux UAPI header and the first header of the C library of one door file, and "
    "some declarations of the two families conflict.  Make the system calls of the door with "
    "foundation::os::raw_syscall and the UAPI headers, or move the calls of the C library into a door file of their "
    "own",
}


def message(item: plugin_sections.Finding) -> str:
    """Return the text of one finding line of the rule."""
    return f"{item.entity} {ACTIONS.get(item.kind, 'is a finding of the rule')}"


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each finding line of the rule in its scope, each row with no reason, and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return names.scoped_errors(context, declarations.rule_of(context, RULE), message)
