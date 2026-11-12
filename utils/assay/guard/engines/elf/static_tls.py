"""static-tls — each object and each shared library puts at most a small block in the static thread-local storage.

THE RULE
    Rule R19 of misc/01_10_2026_quarantine.md gives the base a budget of
    static TLS.  A shared library with a variable of the initial-exec model
    needs its full TLS block in the static TLS of the process.  dlopen finds
    that block only in the part of the static TLS that glibc keeps free.  With
    the default tunables, glibc 2.43 keeps 1664 bytes free in a new process.
    The flag of src/foundation/ContractHandler.cpp has the initial-exec model,
    so each library that links the contract handler has such a block.  The
    last case of test/test_process_wide_across_libraries.cpp is the run-time
    witness.  A library with a block of the limit loads.  A library with a
    block of 64 times the limit does not load.

    The rule judges two kinds of items against one limit:
      * Each member of each static library of the root (THE MEMBERS)
      * Each shared library and each module of the target all (THE SHARED
        LIBRARIES).

THE MEMBERS
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
    at one position of one section are one variable.  A symbol with no bytes
    is no variable, such as _TLS_MODULE_BASE_, which the linker puts at the
    start of the block of a library.  The item of a member is
    ARCHIVE(MEMBER).

THE SHARED LIBRARIES
    The rule reads each shared library and each module of the target all
    (runner.ElfContext.linked_artifacts()).  It reads no program, because
    the loader makes the static TLS of a program and of its needed libraries
    when the program starts, with no part of the free surplus.  The block of
    a library goes into the static TLS of the process when one of these
    facts holds:
      * The library has the bit DF_STATIC_TLS in its entry DT_FLAGS.
      * The library has a relocation of an initial-exec type of its machine
        (INITIAL_EXEC_TYPES) with no symbol, or with a symbol that it
        defines.
      * A different library of the build has such a relocation with an
        undefined symbol, and the library defines that thread-local symbol in
        its dynamic table.  The loader puts the block of the library that
        defines the symbol into the static TLS.
    A linked library holds only its dynamic relocations, and the rule reads
    each relocation table of the file.

    The value of such a library is the size p_memsz of its segment PT_TLS,
    rounded up to the alignment p_align of the segment.  A block with a large
    alignment thus counts at least its alignment.  glibc can put at most
    p_align - 1 bytes of padding before a block, and the value does not count
    them.  A library with no such fact, or with no segment PT_TLS, gets no
    finding.  The variables of a library are the thread-local symbols of its
    table .symtab, the largest first, and the message names the largest.

    The item of a library is its path relative to the build directory.  A
    planted build keeps the archive of each library of the root in its build
    directory (runner, THE PLANTS).  For a planted build, the build directory
    is thus the directory of the archive of the library foundation.

THE JUDGE
    assay.core.budget judges the value of each item against the limit of the
    measured rule, with the ledger rows.  An item above the limit that no row
    keeps is an error.  An item with no value gets no finding.

THE LEDGER
    utils/ledgers/static-tls.txt has the policy max.  A row keeps one member
    or one library in each build, up to VALUE (assay.core.budget, THE
    VERDICT):
        @build/ARCHIVE(MEMBER) | - | VALUE | - | REASON
        @build/PATH | - | VALUE | - | REASON
    `guard write --rule static-tls --build-dir DIR --reason TEXT` writes a
    row for each item above the limit that no row keeps (assay.core.budget,
    THE WRITE).  It removes no row and lowers no kept value.  written() gives
    the new ledger from the items and the ledger, and it reads no build.
    write() reads the build and stores the ledger, and it tells when the
    ledger has no change.

THE ERRORS
    The rule fails closed.  These builds are errors, because the rule cannot
    read each item:
      * A root with no library foundation
      * An archive or a shared library that the ELF reader cannot read
      * An archive with two members of one name, because the item of a
        member is then not unique
      * A shared library of a machine with no initial-exec types
      * A shared library outside the build directory, because its item then
        has no path in the build.

NOT APPLICABLE
    The rule does not apply (exit 77) to a build that has not made each
    library of the root and each shared library of the target all.

Complexity: linear in the symbols of the members, and in the symbols, the dynamic entries and the relocations of the
shared libraries.
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
# The types of a relocation that puts the TLS block of its symbol into the static TLS of the process, for each machine
# (THE SHARED LIBRARIES): R_X86_64_TPOFF64 and R_X86_64_TPOFF32, and R_AARCH64_TLS_TPREL64.
INITIAL_EXEC_TYPES = {elf.EM_X86_64: frozenset({18, 23}), elf.EM_AARCH64: frozenset({1030})}
# The bit of DT_FLAGS that puts the TLS block of a library into the static TLS of the process.
DF_STATIC_TLS = 0x10
# The head comment that each write gives the ledger (assay.core.ledger, THE HEAD COMMENT).
LEDGER_COMMENT = (
    "# The ledger of the rule static-tls of utils/registry.txt: the members of the static libraries with base\n"
    "# thread-local variables and the shared libraries with a block in the static TLS that can be more than the\n"
    "# limit, one row for each item, and each row judges each build.  A row is '@build/ARCHIVE(MEMBER) | - | VALUE |\n"
    "# - | REASON' or '@build/PATH | - | VALUE | - | REASON', and VALUE is the kept value, in bytes.\n"
    "# `python3 utils/bin/guard explain static-tls` tells the rule, and `python3 utils/bin/guard write --rule\n"
    "# static-tls --build-dir DIR --reason TEXT` writes a new row or a higher VALUE.\n"
)


@dataclass(frozen=True, slots=True)
class MemberTls:
    """The base thread-local variables of one member: its item ARCHIVE(MEMBER), their bytes, and their names.

    `variables` holds the name of each variable, the largest first.
    """

    item: str
    value: int
    variables: tuple[str, ...]

    def said(self) -> str:
        """Return the words that tell the value of the member, with its largest variable."""
        count = len(self.variables)
        noun = "variable" if count == 1 else "variables"
        return (
            f"the member {self.item} holds {self.value} bytes of base thread-local storage in {count} {noun}, the "
            f"largest {self.variables[0]}"
        )


@dataclass(frozen=True, slots=True)
class LibraryRead:
    """The TLS facts of one shared library of the build (THE SHARED LIBRARIES).

    `item` is its path relative to the build directory.  `value` is the size of its block rounded up to the
    alignment, or 0 with no segment PT_TLS.  `variables` holds the name of each thread-local variable of its table
    .symtab, the largest first.  `cause` tells the fact of the library itself that puts its block into the static
    TLS, or is empty.  `defined` holds each thread-local symbol that its dynamic table defines, and `referenced` each
    undefined symbol of an initial-exec relocation of the library.
    """

    item: str
    value: int
    variables: tuple[str, ...]
    cause: str
    defined: frozenset[str]
    referenced: frozenset[str]


@dataclass(frozen=True, slots=True)
class LibraryTls:
    """One shared library whose TLS block goes into the static TLS of the process: its item, its bytes and the cause.

    `variables` holds the name of each thread-local variable, the largest first, and it is empty for a library with
    no table .symtab.
    """

    item: str
    value: int
    variables: tuple[str, ...]
    cause: str

    def said(self) -> str:
        """Return the words that tell the value of the library, with its cause and its largest variable."""
        largest = f", the largest variable {self.variables[0]}" if self.variables else ""
        return (
            f"the shared library {self.item} puts a TLS block of {self.value} bytes into the static TLS of the "
            f"process because of {self.cause}{largest}"
        )


@dataclass(frozen=True, slots=True)
class Reading:
    """The items of one build: each member with a base thread-local variable, and each library with a static block."""

    members: tuple[MemberTls, ...]
    libraries: tuple[LibraryTls, ...]

    @property
    def items(self) -> tuple[MemberTls | LibraryTls, ...]:
        """Each item of the build, the members first."""
        return (*self.members, *self.libraries)


# ── The read ───────────────────────────────────────────────────────


def member_tls(member: elf.Member, is_base_library: bool) -> tuple[int, tuple[str, ...]]:
    """Return the bytes and the names of the base thread-local variables of one member (THE MEMBERS).

    With is_base_library True, each thread-local variable of the member counts.  Complexity: linear in the symbols of
    the member.

    Raises:
        elf.ElfError: If the symbol table of the member is not fully in the file
    """
    variables: dict[tuple[int, int], tuple[int, str]] = {}
    for symbol in member.symbols():
        if not (symbol.is_tls and symbol.is_defined and symbol.size > 0):
            continue
        if not is_base_library and elf.outer_scope(symbol.name) not in BASE_SCOPES:
            continue
        place = (symbol.section, symbol.value)
        variables[place] = max(variables.get(place, (0, "")), (symbol.size, symbol.name))
    ordered = sorted(variables.values(), key=lambda pair: (-pair[0], pair[1]))
    return sum(size for size, _ in ordered), tuple(name for _, name in ordered)


def aligned_size(size: int, align: int) -> int:
    """Return the size rounded up to the alignment of a segment, or the size for an alignment of 0 or 1."""
    return size if align <= 1 else -(-size // align) * align


def library_read(member: elf.Member, item: str) -> LibraryRead:
    """Read the TLS facts of one shared library (THE SHARED LIBRARIES).

    Complexity: linear in the program headers, the dynamic entries, the symbols and the relocations of the library.

    Raises:
        elf.ElfError: If a table of the library is not fully in the file, or its machine has no initial-exec types
    """
    machine = member.machine()
    types = INITIAL_EXEC_TYPES.get(machine)
    if types is None:
        raise elf.ElfError(f"{member.where}: the rule knows no initial-exec relocation type of the machine {machine}")
    block = next((segment for segment in member.segments() if segment.kind == elf.PT_TLS), None)
    value = 0 if block is None else aligned_size(block.memory_size, block.align)
    flags = next((entry.value for entry in member.dynamic() if entry.tag == elf.DT_FLAGS), 0)
    dynamic = member.dynamic_symbols()
    defined = frozenset(symbol.name for symbol in dynamic if symbol.is_tls and symbol.is_defined)
    has_own = False
    referenced: set[str] = set()
    for relocation in member.relocations():
        if relocation.kind not in types:
            continue
        symbol = dynamic[relocation.symbol_index] if 0 < relocation.symbol_index < len(dynamic) else None
        if symbol is not None and symbol.is_undefined:
            referenced.add(symbol.name)
        else:
            has_own = True
    cause = "the flag DF_STATIC_TLS" if flags & DF_STATIC_TLS else "an initial-exec relocation" if has_own else ""
    return LibraryRead(item, value, member_tls(member, True)[1], cause, defined, frozenset(referenced))


def library_items(reads: Sequence[LibraryRead]) -> tuple[LibraryTls, ...]:
    """Give each library with a block that goes into the static TLS of the process (THE SHARED LIBRARIES).

    Complexity: linear in the defined and the referenced symbols of the libraries.
    """
    referrers: dict[str, str] = {}
    for read in sorted(reads, key=lambda each: each.item):
        for name in sorted(read.referenced):
            referrers.setdefault(name, read.item)
    found: list[LibraryTls] = []
    for read in reads:
        cause = read.cause
        if not cause:
            name = min((name for name in read.defined if name in referrers), default="")
            if name:
                cause = f"an initial-exec relocation of {referrers[name]} with its variable {name}"
        if cause and read.value > 0:
            found.append(LibraryTls(read.item, read.value, read.variables, cause))
    return tuple(found)


def build_of(context: runner.ElfContext, libraries: dict[str, Path]) -> Path:
    """Return the build directory that gives the item of a library (THE SHARED LIBRARIES)."""
    directory = context.build_dir if context.build_dir is not None else libraries[REQUIRED_LIBRARY].parent
    return directory.resolve()


def library_reads(linked: Sequence[runner.LinkedArtifact], build: Path) -> list[LibraryRead]:
    """Read the TLS facts of each shared library of the build.

    Raises:
        runner.BuildError: If a library is outside the build directory, or cannot be read
    """
    reads: list[LibraryRead] = []
    for artifact in linked:
        path = artifact.path.resolve()
        if not path.is_relative_to(build):
            raise runner.BuildError(
                f"the shared library {path} is not in the build directory {build}, so the rule cannot give its item.  "
                f"Write each library of the target all into the build directory"
            )
        try:
            with elf.mapped(path) as member:
                reads.append(library_read(member, path.relative_to(build).as_posix()))
        except (OSError, elf.ElfError) as problem:
            raise runner.BuildError(
                f"the ELF reader cannot read {path}, and the rule read nothing there: {problem}"
            ) from None
    return reads


def member_items(libraries: dict[str, Path]) -> tuple[MemberTls, ...]:
    """Read each member of each static library of the root, and give each member with a base thread-local variable.

    Raises:
        runner.BuildError: If an archive cannot be read or holds two members of one name
    """
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


def measured(context: runner.ElfContext) -> Reading:
    """Read the members of the static libraries of the root and the shared libraries of all, and give the items.

    Raises:
        runner.NotApplicable: If the build has not made each library
        runner.BuildError: If the build cannot be read (THE ERRORS)
    """
    libraries = context.libraries()
    if REQUIRED_LIBRARY not in libraries:
        raise runner.BuildError(
            f"the root makes no static library {REQUIRED_LIBRARY}, and the rule reads no member of the base.  Make "
            f"the libraries of the tree in the root CMakeLists.txt"
        )
    linked = [artifact for artifact in context.linked_artifacts() if not artifact.is_executable]
    paths = [*libraries.values(), *(artifact.path for artifact in linked)]
    unbuilt = sorted(build_census.shown(path, context.root) for path in paths if not path.is_file())
    if unbuilt:
        raise runner.NotApplicable(f"the build has not made {', '.join(unbuilt)}.  Build the target all")
    members = member_items(libraries)
    return Reading(members, library_items(library_reads(linked, build_of(context, libraries))))


# ── The judge ──────────────────────────────────────────────────────


def budget_of(rule: rule_registry.Rule) -> check_report.Budget:
    """Return the limit of the measured rule.

    Raises:
        runner.BuildError: If the registry gives the rule no limit
    """
    if rule.threshold is None:
        raise runner.BuildError(f"the registry gives the rule {rule.id} no limit and unit.  Add them")
    return check_report.budget_of(rule)


def examine(rule: rule_registry.Rule, context: runner.ElfContext) -> tuple[list[check_report.Finding], Reading]:
    """Return the findings of the build judged against the limit and the ledger rows, and the items (THE JUDGE).

    Raises:
        runner.NotApplicable: If the rule does not apply to the build (NOT APPLICABLE)
        runner.BuildError: If the build cannot be read (THE ERRORS)
    """
    budget = budget_of(rule)
    reading = measured(context)
    rows = cost_budget.read_rows(rule.id, cost_meter.ITEM_OUTPUT, rule_ledger.ledger_path(rule.id, context.root))
    found: list[check_report.Finding] = list(rows.problems)
    for item in reading.items:
        measure = cost_budget.Measure(item.item, f"{cost_meter.BUILD_PATH_PREFIX}{item.item}", item.value, item.said())
        verdict = cost_budget.judge(rule.id, budget, measure, rows)
        if verdict is not None:
            found.append(replace(verdict, entity=item.item))
    return found, reading


def judge(rule: rule_registry.Rule, context: runner.ElfContext) -> list[check_report.Finding]:
    """Return the findings of the build judged against the limit and the ledger rows (THE JUDGE).

    Raises:
        runner.NotApplicable: If the rule does not apply to the build (NOT APPLICABLE)
        runner.BuildError: If the build cannot be read (THE ERRORS)
    """
    return examine(rule, context)[0]


def findings(context: runner.ElfContext) -> list[check_report.Finding]:
    """Give the judged findings of the build, against the ledger in the tree of the context."""
    return judge(rule_ledger.rule_of(RULE), context)


def told(items: Sequence[MemberTls | LibraryTls], counted: str) -> str:
    """Return the words that tell the count and the bytes of the items, with the largest one."""
    largest = max(items, key=lambda item: item.value, default=None)
    shown = f", the largest {largest.item} with {largest.value} bytes" if largest is not None else ""
    return f"{len(items)} {counted}, {sum(item.value for item in items)} bytes in all{shown}"


def run(rule: rule_registry.Rule, context: runner.ElfContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build, write its findings and one line for the members and one for the libraries.

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    try:
        found, reading = examine(rule, context)
    except runner.NotApplicable as reason:
        check_report.write_line(f"{RULE}: {reason}.  The rule does not apply")
        return runner.SKIPPED
    status = check_report.emit(found, RULE, warnings_dir)
    check_report.write_line(f"{RULE}: {told(reading.members, 'members hold base thread-local storage')}")
    check_report.write_line(
        f"{RULE}: {told(reading.libraries, 'shared libraries put a TLS block into the static TLS of the process')}"
    )
    return status


# ── The write ──────────────────────────────────────────────────────


def written(
    budget: check_report.Budget,
    items: Sequence[MemberTls | LibraryTls],
    ledger: rule_ledger.Ledger,
    reason: str | None,
) -> rule_ledger.Ledger:
    """Return the ledger with a row for each item of the build above the limit that no row keeps (THE LEDGER).

    Each row stays, with its kept value.  Complexity: linear in the items and the rows.

    Raises:
        rule_ledger.LedgerError: If an item needs a row and the call gives no reason
    """
    measures = [(item.item, float(item.value), budget) for item in items]
    return cost_budget.raised_ledger(ledger, cost_meter.ITEM_OUTPUT, measures, reason)


def write(rule: rule_registry.Rule, request: runner.WriteRequest, context: runner.ElfContext | None = None) -> int:
    """Write a row for each item of the build above the limit that no row keeps (THE LEDGER).

    Args:
        rule: The rule static-tls of the registry
        request: The write of the ledger
        context: The build that the write reads.  The default is the build directory of the request

    Returns:
        0 when the ledger is written.  1 when the build cannot be read or a new row has no reason.  2 for a mode
        other than the default and admit, and for more than one build.  SKIPPED when the rule does not apply
    """
    if request.more_builds or request.mode == rule_ledger.WriteMode.RAISE:
        check_report.write_line(
            f"guard write: the rule {RULE} has the policy max, and each row judges each build.  Give --reason TEXT "
            f"for a new row or a higher value, one --build-dir DIR and no --raise",
            is_error=True,
        )
        return 2
    read = context if context is not None else runner.ElfContext(request.root, request.build_dir)
    try:
        ledger = rule_ledger.read(rule, request.root)
        new = written(budget_of(rule), measured(read).items, ledger, request.reason)
    except runner.NotApplicable as reason:
        check_report.write_line(f"guard write: the rule {RULE} does not apply: {reason}", is_error=True)
        return runner.SKIPPED
    except (runner.BuildError, rule_ledger.LedgerError, rows_grammar.RowsError, OSError) as problem:
        check_report.write_line(f"guard write: the ledger of {RULE} is not written: {problem}", is_error=True)
        return 1
    if not new.base and not rule_ledger.ledger_path(RULE, request.root).is_file():
        check_report.write_line(f"guard write: no item is over the limit, and {RULE} has no rows")
        return 0
    new = replace(new, comment=LEDGER_COMMENT)
    changed = rule_ledger.store(new, request.root)
    shown = "" if changed else ", with no change"
    check_report.write_line(f"guard write: {rule_ledger.shown_path(RULE)} holds {len(new.base)} rows{shown}")
    return 0
