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

THE CONFIGURATIONS
    A raw call in a preprocessor arm that only one build kind compiles is a
    finding only in that build kind.  An example is a call in
    src/foundation/os/Intercepted.cpp, which only a build with
    ThreadSanitizer compiles.  The ledger holds the rows of each such build
    kind as a configuration, as the ledger of the rule quarantine does
    (assay.core.ledger, THE CONFIGURATIONS):
        configuration NAME kind KIND
        @NAME | PATH | raw-syscall | DELTA | HOLDER: SHAPE | CLAIM
    The first configuration is the base, and its rows are the rows with no
    @NAME.  A row @NAME holds the difference of the count of the
    configuration NAME from the base count.  A difference row of a call that
    the base does not count gives the claim of the call.

    The name of a configuration is the name of the configuration of a build
    (THE CONFIGURATION of assay.guard.engines.plugin.runner): the build kind,
    with -bench and -fuzz.  The rows of THE POLICY of
    assay.guard.engines.plugin.counted judge a build, as for the rules
    quarantine and heavy-include.  These rows are the rows of the
    configuration with its name, else the base rows.  A row of an arm of one
    build kind is then judged in that build kind only, and no build skips the
    rule.

    At this time the configuration rows of this ledger give only the build
    kind.  The other facts of a build, such as the tier of a CI runner, then
    change no judgement.  A raw call that only such a fact changes fails the
    build.

THE WRITE
    `guard write --rule syscall-capability --build-dir DIR` writes the rows of
    the configuration of the build: the base rows for the base, and the
    difference rows for each other configuration.  When the ledger holds no
    configuration, the write writes the base rows.  --add-configuration adds
    the configuration of the build with its facts: the first one that a
    ledger gets is the base.  counted.write_target() gives the rows of each
    write.  A write from a build of a configuration that the ledger does not
    hold writes no row and fails with no --add-configuration.  The modes
    --raise and --admit and the reason of --reason are those of
    assay.core.ledger (THE WRITE).

Usage
    guard check --rule syscall-capability --build-dir DIR [--warnings-dir DIR]
    guard write --rule syscall-capability --build-dir DIR [--raise | --admit] [--reason TEXT] [--add-configuration]

Complexity: linear in the finding lines of the build, and in the rows of the ledger.
"""

from pathlib import Path

from assay.core import git
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.meta import runner as meta_runner
from assay.guard.engines.plugin import counted, declarations, runner
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

    A difference row that adds a count must have a claim: its own claim, or the claim of its base row (THE
    CONFIGURATIONS).  A difference row that lowers a count has no claim.  Complexity: linear in the rows.
    """
    shown = rule_ledger.shown_path(RULE)
    errors: list[check_report.Finding] = []
    tables = [(None, rows.base), *sorted(rows.deltas.items())]
    for name, table in tables:
        holder = "" if name is None else f" of the configuration {name}"
        for key, entry in sorted(table.items()):
            if key.kind in UNADMITTED_KINDS:
                errors.append(
                    check_report.Finding(
                        "error",
                        shown,
                        entry.line,
                        RULE,
                        f"the row{holder} of {key.path} admits a finding of the kind {key.kind} ({key.entity}), and "
                        f"no row can admit one.  Call the door, and remove the row",
                    )
                )
            elif not rows.reason_of(key, name) and (name is None or int(entry.value) > 0):
                errors.append(
                    check_report.Finding(
                        "error",
                        shown,
                        entry.line,
                        RULE,
                        f"the row{holder} of {key.path} ({key.entity}) gives no capability claim.  Give the "
                        f"effects::* claim of its calls in the REASON cell",
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


def verdict(
    rule: rule_registry.Rule, context: runner.PluginContext
) -> tuple[list[check_report.Finding], counted.Judging]:
    """Return the findings of the build judged against the rows of THE POLICY of counted, and those rows.

    A context with no build, as a witness with no configuration row gives, is judged against the base rows.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    found = findings(context)
    base = counted.Judging(None, "the base rows", "")
    try:
        rows = rule_ledger.read(rule, context.root)
    except rows_grammar.RowsError, OSError:
        return meta_runner.judged(rule, context.root, found), base
    try:
        judged = counted.judging(RULE, rows, context.configuration())
    except runner.NotApplicable:
        judged = base
    return meta_runner.judged(rule, context.root, found, configuration=judged.name), judged


def judge(rule: rule_registry.Rule, context: runner.PluginContext) -> list[check_report.Finding]:
    """Return the findings of the build judged against the rows of its configuration (THE CONFIGURATIONS).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return verdict(rule, context)[0]


def run(rule: rule_registry.Rule, context: runner.PluginContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build of the context, judge its findings against the rows of its configuration, and
    write them.

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    census = runner.census_or_status(rule, context, warnings_dir)
    if isinstance(census, int):
        return census
    try:
        found, judged = verdict(rule, context)
    except runner.BuildError as problem:
        return check_report.emit(
            [check_report.Finding("error", context.build_shown(), 0, RULE, str(problem))], RULE, warnings_dir
        )
    if judged.note:
        check_report.write_line(judged.note)
    status = check_report.emit(found, RULE, warnings_dir)
    errors = sum(item.level == "error" for item in found)
    check_report.write_line(
        f"{RULE}: {errors} error(s), {len(found) - errors} warning(s), judged against {judged.words}"
    )
    return status


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the rows of the configuration of the build (THE WRITE).

    Returns:
        0 when the ledger is written.  1 when the build cannot be read, an object has a problem or the mode refuses
        a finding.  1 also when the ledger does not hold the configuration of the build and the write does not add
        it.  2 for --import.  SKIPPED when the rule does not apply to the build
    """
    if request.import_log is not None:
        check_report.write_line(
            f"guard write: the rule {RULE} imports no log.  Write the rows of a configuration in its build",
            is_error=True,
        )
        return 2
    context = runner.PluginContext(request.root, request.build_dir)
    try:
        rows = rule_ledger.read(rule, request.root)
        census = context.census()
        configuration = context.configuration()
    except runner.NotApplicable as reason:
        check_report.write_line(f"guard write: the rule {RULE} does not apply: {reason}", is_error=True)
        return runner.SKIPPED
    except (runner.BuildError, rows_grammar.RowsError, OSError) as problem:
        check_report.write_line(f"guard write: the ledger of {RULE} is not written: {problem}", is_error=True)
        return 1
    if census.problems:
        check_report.write_line(f"guard write: the ledger of {RULE} is not written, because an object has a problem")
        return check_report.emit(census.errors(RULE), RULE, None)
    found = [item for item in census.line_errors(RULE, message) if item.kind]
    target = counted.write_target(RULE, rows, configuration, request.add_configuration)
    if isinstance(target, str):
        check_report.write_line(target, is_error=True)
        return 1
    mode = request.mode
    if target.is_added and mode == rule_ledger.WriteMode.LOWER and len(target.ledger.configurations) > 1:
        mode = rule_ledger.WriteMode.RAISE
    try:
        written = rule_ledger.write(
            rule,
            target.ledger,
            found,
            configuration=target.name,
            mode=mode,
            reason=request.reason,
            moved=git.moves(request.root),
        )
    except rule_ledger.LedgerError as problem:
        check_report.write_line(f"guard write: the ledger of {RULE} is not written: {problem}", is_error=True)
        return 1
    rule_ledger.store(written, request.root)
    total = len(written.base) + sum(len(table) for table in written.deltas.values())
    check_report.write_line(
        f"guard write: {rule_ledger.shown_path(RULE)} holds {total} rows, written for "
        f"{'the base rows' if target.name is None else f'the configuration {target.name}'}"
    )
    return 0
