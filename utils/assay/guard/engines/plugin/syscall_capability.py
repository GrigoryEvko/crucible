"""syscall-capability — each raw system call has a reviewed capability claim, and the affinity calls have one door.

THE CLAIMS
    The syscall rows of utils/rule-table.txt give each function of the C
    library that issues one system call.  The quarantine plugin gives the
    kind `raw-syscall` to each call of such a function, and to each call of
    syscall().  It does this in each file in include/, src/ or vessel/ that a
    build compiles (utils/tools/quarantine/owner.cpp, THE CLAIMS).  A file in
    a directory named test, bench, examples, third_party, external or vendor
    has no claim.  A file in a directory with a name that starts with build
    has no claim too.  The entity of a finding is HOLDER: SHAPE.  The holder is the function that
    holds the call, with the types of its parameters, "macro NAME" for a
    call that a macro body spells, or namespace-scope.  The shape is the name
    of the function, or syscall(SYS_NAME) for a call of syscall() (THE PLACE
    AND THE ENTITY of owner.cpp).

THE DOORS
    Two owner rows of the rule table give the one site of sched_setaffinity
    and pthread_setaffinity_np: the body of
    fixy::detail::set_calling_thread_affinity in include/fixy/os/CpuPinned.h.
    That door records a pin event, and a pin proof stays in force across a
    pin that goes around the door.  The function does not call
    pthread_setaffinity_np, and each call of it is a finding.  A call of
    syscall() with the number of SYS_sched_setaffinity is a use of
    sched_setaffinity.  Each use in a different function is the kind
    `outside-door`, in each file that a build compiles, also a test and a
    bench.  In the same way, an owner row gives the four door directories as
    the sites of the raw system call primitive of the base,
    foundation::os::raw_syscall.

THE PRIMITIVE AND THE DENIED CALLS
    The primitive row of the rule table names the primitive.  A call of it is
    a raw system call too, with the shape raw_syscall(NAME) for the enumerator
    NAME of its first argument.  The deny rows name the system calls that the
    base does not issue raw.  The C library owns the state of these calls.
    A call of syscall() or of the primitive with such a call is the kind
    `denied-syscall` (owner.cpp, THE DENIED CALLS).  The kind applies in each
    file that a build compiles, also a test and a bench.

THE LEDGER
    utils/ledgers/syscall-capability.txt counts the raw system calls of each
    file and entity (the policy count):
        PATH | raw-syscall | COUNT | HOLDER: SHAPE | CLAIM
    Two calls of one system call in one function have one row with the count
    2, and a third call fails the rule.  The reason of each row is the
    effects::* capability claim of its calls, and a row with no reason is an
    error.  No row admits a finding of the kind `outside-door` or of the
    kind `denied-syscall`, and a row of such a kind is an error too.  Each
    problem of the census of the runner is an error.

Usage
    guard check --rule syscall-capability --build-dir DIR [--warnings-dir DIR]
    guard write --rule syscall-capability --build-dir DIR [--raise] [--reason TEXT]

Complexity: linear in the finding lines of the build, and in the rows of the ledger.
"""

from assay.core import ledger as rule_ledger
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.plugin import declarations, runner
from assay.readers import sections as plugin_sections

RULE = "syscall-capability"
# The kind of a use of an entity of an owner row of the rule, when the use is not in its door.
DOOR_KIND = "outside-door"
# The kind of a raw call of a system call that a deny row names.
DENIED_KIND = "denied-syscall"
# The kinds that no ledger row admits.
UNADMITTED_KINDS = (DOOR_KIND, DENIED_KIND)


def message(item: plugin_sections.Finding) -> str:
    """Return the text of one finding line of the rule."""
    if item.kind == DOOR_KIND:
        return (
            f"a use of an entity of an owner row of the rule, and the use is not in its door ({item.entity}).  The "
            f"owner row of utils/rule-table.txt gives the door, and no ledger row admits such a use.  Call the door, "
            f"or a gated function that calls it, such as fixy::sched::apply_affinity_to_mask or "
            f"foundation::os::read_file"
        )
    if item.kind == DENIED_KIND:
        return (
            f"a raw call of a system call that a deny row of utils/rule-table.txt names ({item.entity}).  The C "
            f"library owns the state of that call, and no ledger row admits such a call.  Call the function of the C "
            f"library through its door"
        )
    return (
        f"a raw system call with no reviewed capability claim ({item.entity}).  Call a door of fixy/os in its "
        f"place, or give the call a ledger row with its effects::* capability claim as the reason"
    )


def row_errors(rows: rule_ledger.Ledger) -> list[check_report.Finding]:
    """Give one error for each row of a kind that no row admits, and for each row with no claim (THE LEDGER).

    Complexity: linear in the rows.
    """
    shown = rule_ledger.shown_path(RULE)
    errors: list[check_report.Finding] = []
    for key, entry in sorted(rows.base.items()):
        if key.kind in UNADMITTED_KINDS:
            errors.append(
                check_report.Finding(
                    "error",
                    shown,
                    entry.line,
                    RULE,
                    f"the row of {key.path} admits a finding of the kind {key.kind} ({key.entity}), and no row can "
                    f"admit one.  Call the door, and remove the row",
                )
            )
        elif not entry.reason:
            errors.append(
                check_report.Finding(
                    "error",
                    shown,
                    entry.line,
                    RULE,
                    f"the row of {key.path} ({key.entity}) gives no capability claim.  Give the effects::* claim of "
                    f"its calls in the REASON cell",
                )
            )
    return errors


def findings(context: runner.PluginContext) -> list[check_report.Finding]:
    """Give one error for each raw system call, each use of a door elsewhere, each bad row, and each problem.

    A ledger that cannot be read gives no row error here: the judgement of the ledger reports it.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    found = context.census().line_errors(RULE, message)
    try:
        rows = rule_ledger.read(declarations.rule_of(context, RULE), context.root)
    except rows_grammar.RowsError, OSError:
        return found
    return [*found, *row_errors(rows)]
