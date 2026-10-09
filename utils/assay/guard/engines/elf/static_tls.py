"""static-tls — each object holds at most a small block of the thread-local storage of the base.

THE RULE
    Rule R19 of misc/01_10_2026_quarantine.md gives the base a budget of
    static TLS.  The flag of src/foundation/ContractHandler.cpp has the
    initial-exec model.  Such a variable puts the full TLS block of its
    shared library in the static TLS of the process.  dlopen finds that block only
    in the part of the static TLS that glibc keeps free.  With the default
    tunables, that part has approximately 500 bytes.  The vessel library
    loads only when its TLS block is small.

    The rule reads each member of each static library of the root
    (assay.guard.engines.elf.runner, THE BUILD).  The value of a member is
    the sum of the bytes of its base thread-local variables:
      * Each thread-local variable of a member of the library foundation or
        fixy (BASE_LIBRARIES).
      * Each thread-local variable of a different member with a name in the
        scope foundation or fixy (assay.readers.elf, THE MANGLED SCOPE).  An
        example is the storage of foundation/ThreadLocalRef.h in an object of
        crucible.
    A thread-local variable is a symbol with a section and the type STT_TLS.
    The symbol gives the count of its bytes in .tbss or .tdata.  Two symbols
    at one position of one section are one variable.

THE JUDGE
    assay.core.budget judges the value of each member against the
    thresholds of the measured rule, with the ledger rows of the kind of the
    build.  A member with no base thread-local variable gets no finding.

THE LEDGER
    utils/ledgers/static-tls.txt has the policy max.  A row admits one member
    over the error threshold in the builds of one kind:
        @build/ARCHIVE(MEMBER) | KIND | VALUE | - | REASON
    `guard write --rule static-tls --build-dir DIR --reason TEXT` writes the
    rows of the kind of the build.  A row keeps its reason, and the write
    removes each row of the kind with a member that is not more than the
    error threshold.  written() gives the new ledger from the members and the
    ledger, and it reads no build.  write() reads the build and stores the
    ledger, and it tells when the ledger has no change.

THE ERRORS
    The rule fails closed.  These builds are errors, because the rule cannot
    read each member:
      * A root with no library foundation
      * An archive that the ELF reader cannot read
      * An archive with two members of one name, because the item of a
        member is then not unique.

NOT APPLICABLE
    The rule does not apply (exit 77) to a build with no build-kind.txt, and
    to a build that has not made each library.

Complexity: linear in the symbols of the members of the libraries.
"""

from collections import Counter
from collections.abc import Sequence
from dataclasses import dataclass, replace
from pathlib import Path

from assay.core import budget as cost_budget
from assay.core import ledger as rule_ledger
from assay.core import meter as cost_meter
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.elf import runner
from assay.readers import census as build_census
from assay.readers import elf

RULE = "static-tls"
# The libraries of the base: each thread-local variable of their members counts.
BASE_LIBRARIES = frozenset({"foundation", "fixy"})
# The scopes of a base name: a thread-local variable of a different library counts when its name has one of them.
BASE_SCOPES = frozenset({"foundation", "fixy"})
# The library that the root must make.  Without it, the rule reads no member of the base.
REQUIRED_LIBRARY = "foundation"
# The head comment of a new ledger.
LEDGER_COMMENT = (
    "# The ledger of the rule static-tls of utils/registry.txt: the members of the static libraries with base\n"
    "# thread-local variables that can be more than the error threshold, one row for each member and build kind.\n"
    "# A row is '@build/ARCHIVE(MEMBER) | KIND | VALUE | - | REASON'.  KIND is the build kind of\n"
    "# BUILD_DIR/build-kind.txt, and VALUE is the value at the last write, in bytes.  `python3 utils/bin/guard\n"
    "# explain static-tls` tells the rule, and `python3 utils/bin/guard write --rule static-tls --build-dir DIR\n"
    "# --reason TEXT` writes this file.\n"
)


@dataclass(frozen=True, slots=True)
class MemberTls:
    """The base thread-local variables of one member: its item ARCHIVE(MEMBER), their bytes, and their names.

    `variables` holds the name of each variable, the largest first.
    """

    item: str
    value: int
    variables: tuple[str, ...]


# ── The read ───────────────────────────────────────────────────────


def member_tls(member: elf.Member, is_base_library: bool) -> tuple[int, tuple[str, ...]]:
    """Return the bytes and the names of the base thread-local variables of one member (THE RULE).

    Complexity: linear in the symbols of the member.

    Raises:
        elf.ElfError: If the symbol table of the member is not fully in the file
    """
    variables: dict[tuple[int, int], tuple[int, str]] = {}
    for symbol in member.symbols():
        if not (symbol.is_tls and symbol.is_defined):
            continue
        if not is_base_library and elf.outer_scope(symbol.name) not in BASE_SCOPES:
            continue
        place = (symbol.section, symbol.value)
        variables[place] = max(variables.get(place, (0, "")), (symbol.size, symbol.name))
    ordered = sorted(variables.values(), key=lambda pair: (-pair[0], pair[1]))
    return sum(size for size, _ in ordered), tuple(name for _, name in ordered)


def measured(context: runner.ElfContext) -> tuple[MemberTls, ...]:
    """Read each member of each static library of the root, and give each member with a base thread-local variable.

    Raises:
        runner.NotApplicable: If the build has not made each library
        runner.BuildError: If the root makes no library foundation, or an archive cannot be read or holds two members
            of one name
    """
    libraries = context.libraries()
    if REQUIRED_LIBRARY not in libraries:
        raise runner.BuildError(
            f"the root makes no static library {REQUIRED_LIBRARY}, and the rule reads no member of the base.  Make "
            f"the libraries of the tree in the root CMakeLists.txt"
        )
    unbuilt = sorted(build_census.shown(path, context.root) for path in libraries.values() if not path.is_file())
    if unbuilt:
        raise runner.NotApplicable(f"the build has not made {', '.join(unbuilt)}.  Build the target all")
    found: list[MemberTls] = []
    for target, path in sorted(libraries.items()):
        try:
            members = elf.members(path)
            read = [(member.name, member_tls(member, target in BASE_LIBRARIES)) for member in members]
        except (OSError, elf.ElfError) as problem:
            raise runner.BuildError(
                f"the ELF reader cannot read {path}, and the rule read nothing there: {problem}"
            ) from None
        repeated = sorted(name for name, count in Counter(name for name, _ in read).items() if count > 1)
        if repeated:
            raise runner.BuildError(
                f"{path.name} holds two members with the name {repeated[0]}, so the item of a member is not unique.  "
                f"Give the source files of the library different names"
            )
        found += [MemberTls(f"{path.name}({name})", value, variables) for name, (value, variables) in read if variables]
    return tuple(found)


# ── The judge ──────────────────────────────────────────────────────


def budget_of(rule: rule_registry.Rule) -> check_report.Budget:
    """Return the thresholds of the measured rule.

    Raises:
        runner.BuildError: If the registry gives the rule no thresholds
    """
    if rule.threshold is None:
        raise runner.BuildError(f"the registry gives the rule {rule.id} no warn, error and unit.  Add them")
    return check_report.Budget(
        rule.id, rule.threshold.warn, rule.threshold.error, check_report.unit_text(rule.threshold.unit), rule.refuses
    )


def said_of(member: MemberTls) -> str:
    """Return the words that tell the value of one member, with its largest variable."""
    count = len(member.variables)
    noun = "variable" if count == 1 else "variables"
    return (
        f"the member {member.item} holds {member.value} bytes of base thread-local storage in {count} {noun}, the "
        f"largest {member.variables[0]}"
    )


def examine(
    rule: rule_registry.Rule, context: runner.ElfContext
) -> tuple[list[check_report.Finding], tuple[MemberTls, ...]]:
    """Return the findings of the build judged against the thresholds and the ledger rows of its kind (THE JUDGE).

    The second part is each member with a base thread-local variable.

    Raises:
        runner.NotApplicable: If the rule does not apply to the build (NOT APPLICABLE)
        runner.BuildError: If the build cannot be read (THE ERRORS)
    """
    kind = context.kind()
    budget = budget_of(rule)
    members = measured(context)
    rows = cost_budget.read_rows(rule.id, cost_meter.ITEM_OUTPUT, rule_ledger.ledger_path(rule.id, context.root), kind)
    found: list[check_report.Finding] = list(rows.problems)
    for member in members:
        measure = cost_budget.Measure(
            member.item, f"{cost_meter.BUILD_PATH_PREFIX}{member.item}", member.value, said_of(member)
        )
        verdict = cost_budget.judge(rule.id, budget, measure, rows)
        if verdict is not None:
            found.append(replace(verdict, entity=member.item))
    found += cost_budget.unused(rule.id, rows, (member.item for member in members))
    return found, members


def judge(rule: rule_registry.Rule, context: runner.ElfContext) -> list[check_report.Finding]:
    """Return the findings of the build judged against the thresholds and the ledger rows of its kind (THE JUDGE).

    Raises:
        runner.NotApplicable: If the rule does not apply to the build (NOT APPLICABLE)
        runner.BuildError: If the build cannot be read (THE ERRORS)
    """
    return examine(rule, context)[0]


def findings(context: runner.ElfContext) -> list[check_report.Finding]:
    """Give the judged findings of the build, against the ledger in the tree of the context."""
    return judge(rule_ledger.rule_of(RULE), context)


def run(rule: rule_registry.Rule, context: runner.ElfContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build, write its findings and one line with the members and their bytes.

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    try:
        found, members = examine(rule, context)
    except runner.NotApplicable as reason:
        check_report.write_line(f"{RULE}: {reason}.  The rule does not apply")
        return runner.SKIPPED
    status = check_report.emit(found, RULE, warnings_dir)
    largest = max(members, key=lambda member: member.value, default=None)
    shown = f", the largest {largest.item} with {largest.value} bytes" if largest is not None else ""
    check_report.write_line(
        f"{RULE}: {len(members)} members hold base thread-local storage, {sum(item.value for item in members)} bytes "
        f"in all{shown}"
    )
    return status


# ── The write ──────────────────────────────────────────────────────


def written(
    budget: check_report.Budget,
    kind: str,
    members: Sequence[MemberTls],
    ledger: rule_ledger.Ledger,
    reason: str | None,
) -> rule_ledger.Ledger:
    """Return the ledger with the rows of the build kind KIND written again from the members of the build (THE LEDGER).

    Each row of a different kind stays.  A member over the error threshold gets a row with its value, and the reason
    of its earlier row or REASON.  Each other row of the kind goes.  Complexity: linear in the members and the rows.

    Raises:
        rule_ledger.LedgerError: If a member over the error threshold has no row and the call gives no reason
    """
    base = {key: entry for key, entry in ledger.base.items() if key.kind != kind}
    unexplained: list[str] = []
    for member in members:
        if member.value <= budget.error:
            continue
        path, entity = cost_meter.item_cells(member.item, cost_meter.ITEM_OUTPUT)
        key = rule_ledger.Key(path, kind, entity)
        earlier = ledger.base.get(key)
        why = earlier.reason if earlier is not None else reason
        if not why:
            unexplained.append(member.item)
            continue
        base[key] = rule_ledger.Entry(key, member.value, why)
    if unexplained:
        raise rule_ledger.LedgerError(
            f"{', '.join(unexplained)} is more than the error threshold and has no row.  Give the reason of its row "
            f"with --reason"
        )
    return replace(ledger, base=base)


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the rows of the kind of the build (THE LEDGER).

    Returns:
        0 when the ledger is written.  1 when the build cannot be read or a new row has no reason.  2 for a mode
        other than the default and admit, for --import and for --add-configuration.  SKIPPED when the rule does not
        apply
    """
    is_refused = request.import_log is not None or request.add_configuration
    if is_refused or request.mode == rule_ledger.WriteMode.RAISE:
        check_report.write_line(
            f"guard write: the rule {RULE} has the policy max, and its rows hold the build kind.  Give --reason TEXT "
            f"for a row, and no --raise, --import or --add-configuration",
            is_error=True,
        )
        return 2
    context = runner.ElfContext(request.root, request.build_dir)
    try:
        kind = context.kind()
        ledger = rule_ledger.read(rule, request.root)
        new = written(budget_of(rule), kind, measured(context), ledger, request.reason)
    except runner.NotApplicable as reason:
        check_report.write_line(f"guard write: the rule {RULE} does not apply: {reason}", is_error=True)
        return runner.SKIPPED
    except (runner.BuildError, rule_ledger.LedgerError, rows_grammar.RowsError, OSError) as problem:
        check_report.write_line(f"guard write: the ledger of {RULE} is not written: {problem}", is_error=True)
        return 1
    if not rule_ledger.ledger_path(RULE, request.root).is_file():
        if not new.base:
            told = f"no member of {kind} is over the error threshold, and {RULE} has no rows"
            check_report.write_line(f"guard write: {told}")
            return 0
        new = replace(new, comment=LEDGER_COMMENT)
    changed = rule_ledger.store(new, request.root)
    told = "" if changed else ", with no change"
    check_report.write_line(f"guard write: {rule_ledger.shown_path(RULE)} holds {len(new.base)} rows{told}")
    return 0
