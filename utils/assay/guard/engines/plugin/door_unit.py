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
        headers to the include closure of a door file.  The two families are
        the Linux UAPI headers and the headers of the C library, and some of
        their declarations conflict.  The entity is the first header of each
        family.

THE STALE ROWS
    The owner of each door row spells an include of its header.  The rule
    reads the parse tree of each owner (assay.readers.tsast), with each arm of
    each preprocessor conditional.  A door header that only one build compiles,
    such as an arm of __SANITIZE_THREAD__, then counts in each build.  A door
    row is stale when the tree holds no file of its owner, or when its owner
    does not spell the include.  A stale row is an error, and no ledger row
    admits it: remove the row, or include the header in its owner.  A tree
    with no utils/rule-table.txt, such as the tree of a plant of a different
    rule, has no stale row.

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

Complexity: linear in the finding lines of the build, times the items of the scope, and one parse of each owner of a
door row.
"""

from pathlib import Path

from assay.core import report as check_report
from assay.core import ruletable
from assay.guard.engines.plugin import declarations, names, quarantine, runner
from assay.readers import sections as plugin_sections
from assay.readers import tsast

RULE = "door-unit"
ACTIONS = {
    "header-door": "has a header as its door, so each unit that includes the header gets the system declarations of "
    "the door.  Move the door into a source file of src/fixy/os/ or src/foundation/os/, and keep only declarations "
    "of the base in the header",
    "mixed-headers": "are the first Linux UAPI header and the first header of the C library of the include closure of "
    "one door file, and some declarations of the two families conflict.  Make the system calls of the door with "
    "foundation::os::raw_syscall and the UAPI headers, or move the calls of the C library into a door file of their "
    "own",
}
# The kinds of the parse tree of the path of an include directive with a literal path.
LITERAL_PATHS = ("system_lib_string", "string_literal")


def message(item: plugin_sections.Finding) -> str:
    """Return the text of one finding line of the rule."""
    return f"{item.entity} {ACTIONS.get(item.kind, 'is a finding of the rule')}"


def spelled_includes(root: Path, owners: list[str]) -> dict[str, set[str]]:
    """Return the path of each include directive with a literal path of each owner, as the directive spells it.

    The parse tree holds each arm of each preprocessor conditional.  Complexity: one parse of each owner, and linear
    in its directives.

    Raises:
        OSError: If an owner cannot be read
    """
    spelled: dict[str, set[str]] = {}
    for tree in tsast.parse_files(root, owners):
        paths = (node.child_by_field("path") for node in tree.find("preproc_include"))
        spelled[str(tree.path)] = {
            tsast.prose_text(path).strip() for path in paths if path is not None and path.type in LITERAL_PATHS
        }
    return spelled


def stale_rows(root: Path) -> list[check_report.Finding]:
    """Give an error for each door row with no owner file, or with an owner that does not include it (THE STALE ROWS).

    Raises:
        BuildError: If the rule table or an owner cannot be read
    """
    path = root / quarantine.TABLE
    if not path.is_file():
        return []
    try:
        table = ruletable.load(path)
        owners = sorted({owner for _, owner in table.doors if (root / owner).is_file()})
        spelled = spelled_includes(root, owners)
    except ruletable.TableError as problem:
        raise runner.BuildError(f"the rule table cannot be read: {problem}") from None
    except OSError as problem:
        raise runner.BuildError(f"an owner of a door row cannot be read: {problem}") from None
    found: list[check_report.Finding] = []
    for (header, owner), line in zip(table.doors, table.door_lines, strict=True):
        if owner not in spelled:
            text = f"the door row of {header} names the owner {owner}, and the tree holds no such file"
        elif header not in spelled[owner]:
            text = f"the owner {owner} of the door row of {header} does not include the header"
        else:
            continue
        found.append(
            check_report.Finding(
                "error",
                str(quarantine.TABLE),
                line,
                RULE,
                f"{text}.  The row is stale: remove it, or include the header in its owner",
            )
        )
    return found


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give an error for each finding line in the scope, each row with no reason, each stale door row and each problem.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build or the rule table cannot be read
    """
    return [*names.scoped_errors(context, declarations.rule_of(context, RULE), message), *stale_rows(context.root)]
