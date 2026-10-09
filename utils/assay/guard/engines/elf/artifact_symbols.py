"""artifact-symbols — a static library of the tree throws nothing, has no polymorphic type, and gets no runtime symbol.

THE TWO PROPERTIES
    -fno-exceptions and -fno-rtti are not in this build, and the opt-out
    table of section III of CLAUDE.md tells why.  The properties of the two
    flags hold all the same, and the artifact is the guarantee.  The rule
    reads each static library of the root CMakeLists.txt
    (assay.guard.engines.elf.runner, THE BUILD), and it refuses:
      throw     a reference to __cxa_throw, to __cxa_rethrow or to a throw
                helper of libstdc++ (THE THROW HELPERS) in a live function.
                An error path is std::expected or an abort
      downcast  a reference to __dynamic_cast or __cxa_bad_cast
      vtable    a typeinfo or a vtable that the archive holds: a polymorphic
                type
    A flag reads only the units of the tree.  The rule also finds a throw or
    a downcast that a library header gives in an instance of a template of
    the tree.  An undefined reference is a strong or a weak undefined symbol
    (assay.readers.elf, THE SYMBOLS).  A weak reference then counts too.

A CATCH IS NOT A VIOLATION
    A catch of a throw of a library, with an abort or a fallback, is the
    documented boundary.  It gives references to __cxa_begin_catch and
    __cxa_end_catch, and the rule does not read those two symbols.

WHERE A THROW STANDS
    Each relocation names the section that holds it.  The tree compiles with
    -ffunction-sections, and that section names the function of the throw.
    A local function is dead when no code or data section of its object
    references it.  No different object can name a local symbol, and
    --gc-sections removes the function from each link.  A throw in a dead
    function does not count.  A reference to a throw that no relocation
    gives to a function has the kind hidden-throw.

THE THROWS OF THE ALLOCATOR
    In C++26, each allocate of libstdc++ holds an inline
    `throw std::bad_alloc()` or `throw std::bad_array_new_length()`.  This is
    the failure of an allocation in the library, which ends the process, as
    section II tells for an exhausted memory.  The type of a throw is the
    last typeinfo that its section references before the call.  A throw
    loads it as the second argument of __cxa_throw.  The rule admits a throw
    of one of the two types.  A rethrow has no type.

THE THROW HELPERS
    A member function of a library class can throw through a function of
    libstdc++ in the scope std.  Examples are std::string_view::substr,
    which calls std::__throw_out_of_range_fmt, and std::vector::push_back,
    which calls std::__throw_length_error.  The helper calls __cxa_throw in
    libstdc++.so, and the archive references only the helper.  A throw
    helper is each function of the scope std with a name that starts with
    __throw_ (is_throw_helper).  The rule admits std::__throw_bad_alloc and
    std::__throw_bad_array_new_length, the helpers of the two types above.

    In an archive of the base libraries foundation and fixy
    (BASE_LIBRARIES), each other helper is a throw finding.  Only a row
    throw with its reason admits it.  In each other archive, each live
    reference to such a helper is a finding of the kind helper-throw.  Its
    key is the archive and the mangled name of the helper, and the ledger
    counts the references of each key in each configuration.  The count can
    only fall.  The migration to the types of the base removes the
    containers and the views of the standard library.

THE LIBSTDC++ SYMBOLS
    Rule R5 of misc/01_10_2026_quarantine.md says that no object of
    libcrucible.a uses a libstdc++ symbol that a short allowlist does not
    hold.  Until the link guard of the S4 gate, the ledger holds the
    libstdc++ symbols of the libraries foundation, fixy and crucible.  Then a
    library gets no new libstdc++ symbol.  A libstdc++ symbol of a library
    obeys these two conditions:
      * An object of the archive has an undefined reference to the symbol.
      * The libstdc++ of the compiler gives the symbol in its dynamic symbol
        table.  "COMPILER -print-file-name=libstdc++.so" names that shared
        library.
    The kind of the finding is the mark of the symbol: `allow` for a symbol
    of the allowlist of plan section 13.4 (ALLOWLIST), else `ratchet`.

THE C RUNTIME SYMBOLS
    Rule R5 also covers the C runtime.  The ledger holds the C runtime
    symbols of the libraries foundation, fixy and crucible, and a library
    gets no new one.  A C runtime symbol of a library obeys these two
    conditions:
      * An object of the archive has an undefined reference to the symbol,
        and libstdc++ does not export it.
      * A file of the C runtime of the compiler gives the symbol.  The files
        are the shared libraries and the archives of RUNTIME_FILES, and the
        dynamic loader, which is the interpreter of libc.so.6.
        "COMPILER -print-file-name=FILE" names each file.
    A symbol of the sanitizer runtime or of the linker is in no such file,
    and the rule does not read it.  The kind of the finding is `libc-allow`
    for a symbol of the allowlist of its library (LIBC_ALLOWLISTS), else
    `libc-ratchet`.  Each allowlist holds these symbols:
      * The calls that GCC makes itself (COMPILER_CALLS), and each function
        of the support library of GCC (SUPPORT_ARCHIVES), such as __udivti3
        and _Unwind_Resume.
      * For fixy, the calls of glibc that decision D12 keeps, each in its
        door (DOOR_CALLS).  The plan names these calls.  When a door must
        make a related call, the change that adds the door adds the call to
        the set.
      * For crucible, __cxa_atexit, which plan section 13.4 admits.
    At the S4 gate, libfixy.a holds no `libc-ratchet` row.

THE CLASS OF A SYMBOL
    The messages give the class of a symbol: the first class of this list
    that its mangled name gives (assay.readers.elf, THE MANGLED SCOPE).
      alloc  the global operator new or delete
      abi    a name that starts with __cxa_ or __gxx_, __dynamic_cast, or a
             name in the scope __cxxabiv1
      std    a name in the scope std
      gnu    a name in the scope __gnu_cxx
      c      a name with no mangling, such as __once_proxy
      other  each other mangled name

THE LEDGER
    utils/ledgers/artifact-symbols.txt has the policy count, in format 1 of
    assay.core.ledger:
        configuration KIND kind KIND
        configuration KIND tier TIER
        @build/LIBRARY | MARK | 1 | SYMBOL | -
        @KIND | @build/LIBRARY | MARK | +1 | SYMBOL | -
        @build/ARCHIVE | helper-throw | COUNT | HELPER | -
        @KIND | @build/ARCHIVE | helper-throw | DELTA | HELPER | -
        @build/ARCHIVE | throw | admit | TEXT | REASON
        @build/ARCHIVE | vtable | admit | TEXT | REASON
    MARK is one of MARKS: `allow` and `ratchet` for a libstdc++ symbol, and
    `libc-allow` and `libc-ratchet` for a C runtime symbol.  A row
    helper-throw gives the references of an archive that is not of the base
    to a throw helper (THE THROW HELPERS).  The symbols change
    with the build, because the inlining, the sanitizers and the macro
    _GLIBCXX_ASSERTIONS change the references.  A configuration
    is a build kind on a target tier (assay.core.target).  The first
    configuration is the base, and a row of a different configuration gives
    its difference.  LIBRARY and ARCHIVE are the file name of the archive.

    The rows throw and vtable belong to no configuration (assay.core.ledger,
    THE PART OF NO CONFIGURATION), and each build of each kind judges them.
    A row throw admits a throw in a function of its archive when the
    demangled name of the function holds TEXT.  It also admits the throw
    when the name of the object of the function holds TEXT.  A row vtable
    admits the typeinfo and the vtable of a class when its name
    holds TEXT.  The entity of each such finding is then TEXT.  The inlining
    moves a throw from one function to a different function between a Debug
    build, a Release build and a build with a sanitizer.  TEXT names the
    site in a way that each configuration shares.  An object name, such as
    Topology.cpp.o, names a site in each configuration.  A finding that no
    row admits has its full name as its entity.  The rule
    refuses a row of a different kind, and no row can admit a downcast.

THE VERDICT
    assay.core.ledger judges the findings.  A finding with no row is an
    error, and a row of a symbol that goes is an error too.  A row throw or
    vtable that admits nothing of the build is an error.  The exception is a
    row throw that names a function or an object of its archive
    (quiet_rows).  The
    inlining of one configuration can remove a throw that a different
    configuration keeps.  Such a row gives no error, and a write keeps it.  Each library with
    rows gives one warning.  These archives are errors, because the rule
    read nothing there:
      * An archive that the ELF reader cannot read
      * An archive with no symbol
      * A symbol library with no undefined reference.

THE WRITE
    `guard write --rule artifact-symbols --build-dir DIR` writes the
    configuration and the symbol rows of the build.  A write of the base
    keeps the symbols of each different configuration.  The write gives a
    row to a new `allow` or `libc-allow` symbol, and it refuses a new
    `ratchet` or `libc-ratchet` symbol.  It also refuses a count of
    helper-throw above its row.  --raise also writes such a symbol or count.
    Use it only for one of RAISE_REASONS, and give the reason in the commit.  A write keeps each
    row throw or vtable that admits a finding, and it removes each such row
    that admits nothing.  It adds no such row: a person writes it with its
    reason.  --import LOG puts the rows of a configuration that a CI leg
    printed into the ledger (assay.guard.engines.plugin.counted.import_rows).
    The import refuses a row that does not have a shape of THE LEDGER.

NOT APPLICABLE
    The rule does not apply (exit 77) to these builds:
      * A build with no build-kind.txt
      * A build that has not made each library
      * A build of the UBSan-strict preset.  Its checks of the dynamic type
        put the typeinfo of library classes into the archives.
    A build of a configuration that the ledger does not hold judges only
    the throws and the classes.  It does not judge its symbols and its
    counts of helper-throw.  It prints one line with its tier and the
    tiers of the ledger, and the rows of the build for --import.  A run of a
    configuration other than the base also prints its rows after a failure.
    The base has no such rows, because a write writes them
    (assay.guard.engines.plugin.counted, THE RUN).

Complexity: linear in the symbols and the relocations of the libraries and of the C runtime, with one run of c++filt.
"""

from collections import Counter
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, field, replace
from pathlib import Path

from assay.core import git, proc
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.elf import runner
from assay.guard.engines.plugin import counted
from assay.guard.engines.plugin import runner as plugin_runner
from assay.readers import census as build_census
from assay.readers import elf

RULE = "artifact-symbols"
LIBRARY_PREFIX = "@build/"
# The target names of the libraries of the part of the libstdc++ symbols.
SYMBOL_LIBRARIES = ("foundation", "fixy", "crucible")
# The libraries that the root must make.  Without them, the rule cannot read the correct set of archives.
REQUIRED_LIBRARIES = ("foundation", "crucible")
# The two facts of a configuration of the ledger (THE LEDGER).
FACTS = frozenset({plugin_runner.KIND_KEY, plugin_runner.TIER_KEY})
ALLOW = "allow"
RATCHET = "ratchet"
LIBC_ALLOW = "libc-allow"
LIBC_RATCHET = "libc-ratchet"
MARKS = (ALLOW, RATCHET, LIBC_ALLOW, LIBC_RATCHET)
# The marks of a symbol that a write refuses with no --raise.
RATCHET_MARKS = (RATCHET, LIBC_RATCHET)
THROW = "throw"
HIDDEN_THROW = "hidden-throw"
# The kind of a reference to a throw helper in an archive that is not of the base (THE THROW HELPERS).
HELPER_THROW = "helper-throw"
# The target names of the libraries of the base, where each throw helper is a throw finding.
BASE_LIBRARIES = ("foundation", "fixy")
DOWNCAST = "downcast"
VTABLE = "vtable"
ADMITTED_KINDS = (THROW, VTABLE)
# The first allowlist of the link guard, from section 13.4 of the plan.
ALLOWLIST = frozenset(
    {"__cxa_guard_acquire", "__cxa_guard_release", "__cxa_guard_abort", "__cxa_atexit", "__gxx_personality_v0"}
)
# The files of the C runtime (THE C RUNTIME SYMBOLS).  True marks a shared library, and its dynamic symbols count.
# False marks an archive, and the global symbols of its members count.  The first file that gives a name is its owner,
# and the dynamic loader comes after libm.so.6.  libc_nonshared.a holds the part of glibc that each program links
# statically, such as atexit.  libgcc_s.so.1 is the shared library of the support library of GCC.
RUNTIME_FILES = (
    ("libc.so.6", True),
    ("libm.so.6", True),
    ("libc_nonshared.a", False),
    ("libgcc.a", False),
    ("libgcc_eh.a", False),
    ("libgcc_s.so.1", True),
)
# The support library of GCC: the helpers of the arithmetic and the unwinder that GCC calls itself.
SUPPORT_ARCHIVES = ("libgcc.a", "libgcc_eh.a")
# The calls of the C library that GCC makes itself, which each library can reference (rule R5).
COMPILER_CALLS = frozenset({"memcpy", "memmove", "memset", "memcmp", "__stack_chk_fail", "__tls_get_addr"})
# The calls that decision D12 of the plan keeps in glibc.  Only a door of fixy makes one (rule R5).  The thread door
# of fixy/os/ThreadTasks.h starts each thread with the attributes of the call, and names it.
DOOR_CALLS = frozenset(
    {
        "pthread_create",
        "pthread_join",
        "pthread_attr_init",
        "pthread_attr_destroy",
        "pthread_attr_setstacksize",
        "pthread_attr_setguardsize",
        "pthread_attr_setaffinity_np",
        "pthread_self",
        "pthread_setname_np",
        "sigaction",
        "dlopen",
        "clock_gettime",
        "malloc",
        "free",
    }
)
# The allowlist of the C runtime symbols of each symbol library, by its target name, without the support library.
LIBC_ALLOWLISTS = {
    "foundation": COMPILER_CALLS,
    "fixy": COMPILER_CALLS | DOOR_CALLS,
    "crucible": COMPILER_CALLS | {"__cxa_atexit"},
}
CLASSES = ("alloc", "abi", "std", "gnu", "c", "other")
ABI_PREFIXES = ("__cxa_", "__gxx_")
ABI_SCOPE = "__cxxabiv1"
GNU_SCOPE = "__gnu_cxx"
THROW_SYMBOLS = ("__cxa_throw", "__cxa_rethrow")
# A throw helper of libstdc++ is a function of the scope std with a name that starts with __throw_ (THE THROW HELPERS).
# Its mangled name is the prefix, the length of the name in digits, and the name.
STD_FUNCTION_PREFIX = "_ZSt"
THROW_HELPER_MARK = "__throw_"
DOWNCAST_SYMBOLS = ("__dynamic_cast", "__cxa_bad_cast")
# The typeinfo of std::bad_alloc and of std::bad_array_new_length (THE THROWS OF THE ALLOCATOR).
ALLOCATION_FAILURES = frozenset({"_ZTISt9bad_alloc", "_ZTISt20bad_array_new_length"})
# The throw helpers of the same two failures: std::__throw_bad_alloc() and std::__throw_bad_array_new_length().
ALLOCATION_HELPERS = frozenset({"_ZSt17__throw_bad_allocv", "_ZSt28__throw_bad_array_new_lengthv"})
TYPEINFO_MARK = "_ZTI"
FUNCTION_PREFIXES = (".text.unlikely.", ".text.hot.", ".text.startup.", ".text.exit.", ".text.")
# The prefixes of the sections that hold no function: the debug information and the unwind tables.
UNREAD_SECTIONS = (".debug", ".eh_frame")
# The demangled names of a typeinfo and of a vtable, before the name of the class.
CLASS_MARKS = ("typeinfo for ", "vtable for ")
CLASS_DIVIDER = " for "
UBSAN_STRICT = "CRUCIBLE_UBSAN_STRICT"
CMAKE_TRUE = frozenset({"ON", "1", "YES", "TRUE", "Y"})
RAISE_REASONS = (
    "a new toolchain",
    "code that moved from a base header into a base source file",
    "a new build kind or target",
    "a class of symbols that the rule did not read before",
)
# The three shapes of a base row (THE LEDGER).
ROW_SHAPE = (
    "a row is '@build/LIBRARY | allow|ratchet|libc-allow|libc-ratchet | 1 | SYMBOL | -', "
    "'@build/ARCHIVE | helper-throw | COUNT | HELPER | -', or '@build/ARCHIVE | throw|vtable | admit | TEXT | REASON'"
)


@dataclass(frozen=True, slots=True)
class Reference:
    """One reference to a throw: live or dead, the symbol, the member, the thrown typeinfo, and the function."""

    is_live: bool
    symbol: str
    member: str
    thrown: str
    function: str


@dataclass(frozen=True, slots=True)
class Artifact:
    """The read of one static library: its name, its symbol count, its undefined names, its throws and its classes.

    `undefined` gives the members that reference each undefined name.  `defined` holds the member and the name of
    each symbol that the archive holds with a section.
    """

    name: str
    symbols: int
    undefined: Mapping[str, frozenset[str]]
    references: tuple[Reference, ...]
    defined: tuple[tuple[str, str], ...]


@dataclass(frozen=True, slots=True)
class Examination:
    """The findings of one build, judged, with the facts that the run prints and the write uses.

    `configuration` is the build kind and the tier of the build, or None when the ledger cannot be read.  `symbols`
    gives the count of each key of a libstdc++ symbol and of a C runtime symbol, and `admitted` the count of each key
    that a row throw or vtable admits.
    """

    findings: tuple[check_report.Finding, ...]
    ledger: rule_ledger.Ledger
    configuration: plugin_runner.BuildConfiguration | None = None
    is_held: bool = False
    symbols: Mapping[rule_ledger.Key, int] = field(default_factory=dict)
    admitted: Mapping[rule_ledger.Key, int] = field(default_factory=dict)
    summary: str = ""


@dataclass(frozen=True, slots=True)
class Runtime:
    """The C runtime of a compiler (THE C RUNTIME SYMBOLS).

    `owners` gives each name its owner: the first file of the sequence of runtime_of() that gives the name.
    `support` holds each function of the support library of GCC.
    """

    owners: Mapping[str, str]
    support: frozenset[str]


# ── The classes and the marks ──────────────────────────────────────


def classify(symbol: str) -> str:
    """Return the class of a symbol (THE CLASS OF A SYMBOL)."""
    scope = elf.outer_scope(symbol)
    if elf.global_operator(symbol):
        return "alloc"
    if symbol.startswith(ABI_PREFIXES) or symbol == DOWNCAST_SYMBOLS[0] or scope == ABI_SCOPE:
        return "abi"
    if scope == elf.STD_SCOPE:
        return "std"
    if scope == GNU_SCOPE:
        return "gnu"
    return "other" if symbol.startswith(elf.MANGLED_MARK) else "c"


def mark_of(symbol: str) -> str:
    """Return the mark `allow` for a symbol of the allowlist of plan section 13.4, else the mark `ratchet`."""
    return ALLOW if symbol in ALLOWLIST else RATCHET


# ── The ledger ─────────────────────────────────────────────────────


def row_problems(ledger: rule_ledger.Ledger) -> list[check_report.Finding]:
    """Return an error for each configuration and each row that does not have a shape of THE LEDGER."""
    problems: list[tuple[int, str]] = []
    for configuration in ledger.configurations:
        facts = dict(configuration.facts)
        if set(facts) != FACTS or facts[plugin_runner.KIND_KEY] != configuration.name:
            shape = (
                f"the configuration {configuration.name} has the rows 'configuration KIND kind KIND' and "
                f"'configuration KIND tier TIER', and no other"
            )
            problems.append((0, shape))
    for key, entry in sorted(ledger.base.items(), key=lambda item: item[1].line):
        is_symbol = (key.kind in MARKS or key.kind == HELPER_THROW) and entry.value != rule_ledger.ADMIT_VALUE
        is_admit = key.kind in ADMITTED_KINDS and entry.value == rule_ledger.ADMIT_VALUE
        if not key.path.startswith(LIBRARY_PREFIX) or not key.entity or not (is_symbol or is_admit):
            problems.append((entry.line, ROW_SHAPE))
    if rule_ledger.base_counts(ledger.base) and not ledger.configurations:
        problems.append((0, "the ledger has symbol rows and no configuration row"))
    for configuration in ledger.configurations:
        for key, count in ledger.expected(configuration.name).items():
            if count != 1 and key.kind != HELPER_THROW:
                shape = (
                    f"the configuration {configuration.name} gives {key.entity} of {key.path} the count {count}, "
                    f"and a symbol has the count 0 or 1"
                )
                problems.append((ledger.line_of(key, configuration.name), shape))
    return [
        check_report.Finding("error", rule_ledger.shown_path(RULE), line, RULE, message) for line, message in problems
    ]


def import_refusal(ledger: rule_ledger.Ledger) -> str:
    """Return the first problem of THE LEDGER in a ledger with the rows of a log, or an empty text."""
    problems = row_problems(ledger)
    return problems[0].message if problems else ""


def with_configuration(table: rule_ledger.Ledger, build: plugin_runner.BuildConfiguration) -> rule_ledger.Ledger:
    """Return the ledger with the configuration of a build.

    The configuration replaces a configuration of the same name, or it comes after the others.
    """
    configuration = rule_ledger.Configuration(build.name, build.facts)
    configurations = [configuration if item.name == build.name else item for item in table.configurations]
    if configuration not in configurations:
        configurations.append(configuration)
    return replace(table, configurations=tuple(configurations))


def admitting_text(rows: Sequence[rule_ledger.Key], path: str, kind: str, subject: str) -> str | None:
    """Return the TEXT of the first row of a path and a kind that the subject holds, or None (THE LEDGER)."""
    return next((key.entity for key in rows if key.path == path and key.kind == kind and key.entity in subject), None)


# ── The artifact ───────────────────────────────────────────────────


def is_throw_helper(symbol: str) -> bool:
    """Return True for the mangled name of a throw helper of libstdc++, such as _ZSt20__throw_length_errorPKc."""
    if not symbol.startswith(STD_FUNCTION_PREFIX):
        return False
    rest = symbol[len(STD_FUNCTION_PREFIX) :]
    digits = len(rest) - len(rest.lstrip("0123456789"))
    return digits > 0 and rest[digits:].startswith(THROW_HELPER_MARK)


def is_throw(symbol: str) -> bool:
    """Return True for a symbol that throws: __cxa_throw, __cxa_rethrow or a throw helper of libstdc++."""
    return symbol in THROW_SYMBOLS or is_throw_helper(symbol)


def member_references(member: elf.Member) -> list[tuple[bool, str, str, str]]:
    """Return each reference of one member to a throw or a throw helper, as WHERE A THROW STANDS tells.

    Each item is True when the function is live, the symbol, the typeinfo that
    a throw throws or "-", and the function or the section that holds the
    reference.  Complexity: linear in the relocations and the symbols.

    Raises:
        elf.ElfError: If a table of the member is not fully in the file
    """
    local_functions = {
        symbol.name for symbol in member.symbols() if symbol.is_function and symbol.is_local and symbol.name
    }
    referenced: set[str] = set()
    hits: list[tuple[str, str, str]] = []
    section_index, typeinfo = -1, "-"
    for relocation in member.relocations(UNREAD_SECTIONS):
        if relocation.section_index != section_index:
            section_index, typeinfo = relocation.section_index, "-"
        target = relocation.symbol
        referenced.add(target)
        if target.startswith(TYPEINFO_MARK):
            typeinfo = target
        if is_throw(target):
            hits.append((target, relocation.section, typeinfo if target == THROW_SYMBOLS[0] else "-"))
    found: list[tuple[bool, str, str, str]] = []
    for symbol, hit_section, thrown in hits:
        function = next(
            (hit_section[len(prefix) :] for prefix in FUNCTION_PREFIXES if hit_section.startswith(prefix)), ""
        )
        is_dead = function in local_functions and function not in referenced and hit_section not in referenced
        found.append((not is_dead, symbol, thrown, function or f"(section {hit_section})"))
    return found


def read_artifact(path: Path) -> Artifact:
    """Read the symbols and the throws of one static library.  The names stay mangled.

    Raises:
        runner.BuildError: If the ELF reader cannot read the archive, or reads no symbol from it
    """
    try:
        members = elf.members(path)
        named = 0
        undefined: dict[str, set[str]] = {}
        defined: list[tuple[str, str]] = []
        references: list[Reference] = []
        for member in members:
            for symbol in member.symbols():
                named += bool(symbol.name)
                if symbol.is_undefined:
                    undefined.setdefault(symbol.name, set()).add(member.name)
                elif symbol.is_defined:
                    defined.append((member.name, symbol.name))
            references += [
                Reference(live, symbol, member.name, thrown, function)
                for live, symbol, thrown, function in member_references(member)
            ]
    except (OSError, elf.ElfError) as problem:
        raise runner.BuildError(
            f"the ELF reader cannot read {path}, and the rule read nothing there: {problem}"
        ) from None
    if not named:
        raise runner.BuildError(
            f"the ELF reader read no symbol from {path}.  An archive with no symbol gives nothing to match, and it is "
            f"not an archive that throws nothing"
        )
    return Artifact(
        path.name,
        named,
        {name: frozenset(items) for name, items in undefined.items()},
        tuple(references),
        tuple(defined),
    )


def runtime_file(compiler: str, name: str) -> Path:
    """Return the ELF file of the runtime of the compiler that "COMPILER -print-file-name=NAME" names.

    Raises:
        runner.BuildError: If the compiler does not run, or it names no ELF file or archive.  A linker script, such as
            libc.so, is no such file
    """
    try:
        done = proc.require(proc.run(proc.Command([compiler, f"-print-file-name={name}"], timeout_s=proc.QUICK_S)))
    except proc.ToolFailed as problem:
        raise runner.BuildError(f"{compiler} does not name its {name}: {problem}") from None
    path = Path(done.out.strip())
    if not path.is_absolute() or not path.is_file():
        raise runner.BuildError(
            f"{compiler} -print-file-name={name} gives {str(path)!r}, which is not a file.  The rule reads the "
            f"runtime of the compiler of the build"
        )
    resolved = path.resolve()
    with resolved.open("rb") as handle:
        magic = handle.read(len(elf.ARCHIVE_MAGIC))
    if not magic.startswith(elf.MAGIC) and magic != elf.ARCHIVE_MAGIC:
        raise runner.BuildError(
            f"{compiler} -print-file-name={name} gives {resolved}, which is not an ELF file or an archive.  The rule "
            f"reads no linker script"
        )
    return resolved


def defined_names(path: Path, is_shared: bool) -> frozenset[str]:
    """Return the global names with a section of a file of the runtime.

    A shared library gives its dynamic symbols, and an archive gives the symbols of each member.

    Raises:
        runner.BuildError: If the ELF reader cannot read the file, or reads no such name from it
    """
    try:
        if is_shared:
            with elf.mapped(path) as library:
                names = frozenset(symbol.name for symbol in library.dynamic_symbols() if symbol.is_defined)
        else:
            names = frozenset(
                symbol.name
                for member in elf.members(path)
                for symbol in member.symbols()
                if symbol.is_defined and not symbol.is_local
            )
    except (OSError, elf.ElfError) as problem:
        raise runner.BuildError(f"the symbols of {path} cannot be read: {problem}") from None
    if not names:
        raise runner.BuildError(
            f"the ELF reader reads no global name with a section from {path}, and the rule has no runtime to compare "
            f"with"
        )
    return names


def libstdcxx_exports(compiler: str) -> frozenset[str]:
    """Return the names that the libstdc++ of the compiler gives in its dynamic symbol table, with a section.

    Raises:
        runner.BuildError: If the compiler names no libstdc++.so, the file is not an ELF shared library, or the ELF
            reader reads no such name
    """
    return defined_names(runtime_file(compiler, "libstdc++.so"), is_shared=True)


def runtime_of(compiler: str) -> Runtime:
    """Read the C runtime of the compiler: the files of RUNTIME_FILES and the dynamic loader.

    The dynamic loader is the interpreter of libc.so.6.  Complexity: linear in the symbols of the files.

    Raises:
        runner.BuildError: If a file cannot be found or read, or libc.so.6 names no dynamic loader
    """
    files = [(runtime_file(compiler, name), is_shared) for name, is_shared in RUNTIME_FILES]
    libc = files[0][0]
    try:
        loader = elf.interpreter(libc)
    except (OSError, elf.ElfError) as problem:
        raise runner.BuildError(f"the interpreter of {libc} cannot be read: {problem}") from None
    if loader is None or not Path(loader).is_absolute() or not Path(loader).is_file():
        raise runner.BuildError(
            f"{libc} names no dynamic loader that is a file ({loader!r}).  The rule reads the loader for __tls_get_addr"
        )
    files.insert(2, (Path(loader).resolve(), True))
    owners: dict[str, str] = {}
    support: set[str] = set()
    for path, is_shared in files:
        names = defined_names(path, is_shared)
        for name in names:
            owners.setdefault(name, path.name)
        if path.name in SUPPORT_ARCHIVES:
            support |= names
    return Runtime(owners, frozenset(support))


def libc_mark_of(library: str, symbol: str, runtime: Runtime) -> str:
    """Return `libc-allow` for a symbol of the allowlist of a library, else `libc-ratchet` (THE C RUNTIME SYMBOLS).

    `library` is the target name of the library: foundation, fixy or crucible.
    """
    return LIBC_ALLOW if symbol in LIBC_ALLOWLISTS[library] or symbol in runtime.support else LIBC_RATCHET


def read_libraries(context: runner.ElfContext) -> tuple[dict[str, Artifact], Mapping[str, str], frozenset[str]]:
    """Read each static library of the root, and give the target name of each archive of a symbol library.

    The third item holds the archives of the base libraries (BASE_LIBRARIES).

    Raises:
        runner.NotApplicable: If the build has not made each library
        runner.BuildError: If the root makes no library foundation or crucible, or an archive cannot be read
    """
    libraries = context.libraries()
    absent = [name for name in REQUIRED_LIBRARIES if name not in libraries]
    if absent:
        raise runner.BuildError(
            f"the root makes no static library {', '.join(absent)}.  The rule then reads an incorrect set of "
            f"artifacts.  Make the libraries of the tree in the root CMakeLists.txt"
        )
    unbuilt = sorted(build_census.shown(path, context.root) for path in libraries.values() if not path.is_file())
    if unbuilt:
        raise runner.NotApplicable(f"the build has not made {', '.join(unbuilt)}.  Build the target all")
    artifacts = {path.name: read_artifact(path) for path in libraries.values()}
    symbol_libraries = {libraries[name].name: name for name in SYMBOL_LIBRARIES if name in libraries}
    empty = sorted(name for name in symbol_libraries if not artifacts[name].undefined)
    if empty:
        raise runner.BuildError(
            f"the ELF reader reads no undefined reference from {', '.join(empty)}, and the rule read nothing there.  "
            f"An archive that calls nothing is not an archive that uses no runtime symbol"
        )
    base_archives = frozenset(libraries[name].name for name in BASE_LIBRARIES if name in libraries)
    return artifacts, symbol_libraries, base_archives


# ── The findings ───────────────────────────────────────────────────


def finding(path: str, kind: str, entity: str, message: str) -> check_report.Finding:
    """Return one error of the rule, with the key of its row."""
    return check_report.Finding("error", path, 0, RULE, message, kind, entity)


def class_findings(
    artifacts: Mapping[str, Artifact],
    names: Mapping[str, str],
    rows: Sequence[rule_ledger.Key],
    counted: Counter[str],
    base_archives: frozenset[str],
) -> list[check_report.Finding]:
    """Return the findings of the throws, the downcasts and the classes of the libraries (THE TWO PROPERTIES).

    `names` gives the demangled name of each name, and `rows` the keys of the rows throw and vtable in the sequence of
    their lines.  `counted` gets the count of the throws of the allocator.  A throw helper is a throw finding only in
    an archive of `base_archives`, and helper_findings() gives it in each other archive.
    """
    found: list[check_report.Finding] = []
    for archive in sorted(artifacts):
        artifact = artifacts[archive]
        path = f"{LIBRARY_PREFIX}{archive}"
        attributed = {reference.symbol for reference in artifact.references}
        for reference in artifact.references:
            if not reference.is_live:
                continue
            if reference.thrown in ALLOCATION_FAILURES or reference.symbol in ALLOCATION_HELPERS:
                counted["allocation"] += 1
                continue
            if is_throw_helper(reference.symbol) and archive not in base_archives:
                continue
            function = names[reference.function]
            thrower = names.get(reference.symbol, reference.symbol)
            found.append(
                finding(
                    path,
                    THROW,
                    admitting_text(rows, path, THROW, f"{reference.member} {function}") or function,
                    f"{archive} references {thrower} in {reference.member}, in {function}.  Something in this "
                    f"artifact throws.  An error path in this tree is std::expected or an abort, never a throw.  A "
                    f"throw that a library header makes and the tree cannot remove takes a row with its reason",
                )
            )
        for symbol in sorted(name for name in artifact.undefined if is_throw(name)):
            if symbol not in attributed:
                found.append(
                    finding(
                        path,
                        HIDDEN_THROW,
                        symbol,
                        f"{archive} references {symbol}, and no relocation names the function that holds it",
                    )
                )
        for symbol in DOWNCAST_SYMBOLS:
            if symbol in artifact.undefined:
                found.append(
                    finding(
                        path,
                        DOWNCAST,
                        symbol,
                        f"{archive} references {symbol}.  A runtime downcast got into the artifact.  This tree has no "
                        f"virtual and no dynamic_cast: dispatch is a kind enum plus static_cast",
                    )
                )
        for member, symbol in artifact.defined:
            text = names[symbol]
            # A space before the name keeps the test of the line of nm --demangle, which also finds a construction
            # vtable.
            if not any(f" {mark}" in f" {text}" for mark in CLASS_MARKS):
                continue
            subject = text.partition(CLASS_DIVIDER)[2]
            found.append(
                finding(
                    path,
                    VTABLE,
                    admitting_text(rows, path, VTABLE, subject) or subject,
                    f"{archive} holds {text} in {member}.  A polymorphic type got into the artifact.  Dispatch is a "
                    f"kind enum plus static_cast, and each virtual of the tree is in a planning document.  A class of "
                    f"a library that the tree uses by design takes a row with its reason",
                )
            )
    return found


def quiet_rows(
    admits: Mapping[rule_ledger.Key, rule_ledger.Entry],
    artifacts: Mapping[str, Artifact],
    names: Mapping[str, str],
    found: set[rule_ledger.Key],
) -> set[rule_ledger.Key]:
    """Return the rows throw that admit no finding of the build and name a function or an object of their archive.

    The site of such a row stays in the archive, and the inlining of the build removed its throw (THE VERDICT).
    `found` holds the keys of the findings of the build.  Complexity: linear in the rows times the symbols that the
    archives hold.
    """
    quiet: set[rule_ledger.Key] = set()
    for key in admits:
        artifact = artifacts.get(key.path.removeprefix(LIBRARY_PREFIX))
        if key.kind != THROW or key in found or artifact is None:
            continue
        if any(key.entity in f"{member} {names.get(symbol, symbol)}" for member, symbol in artifact.defined):
            quiet.add(key)
    return quiet


def helper_findings(
    artifacts: Mapping[str, Artifact], base_archives: frozenset[str], names: Mapping[str, str]
) -> list[check_report.Finding]:
    """Return one finding for each live reference to a throw helper in an archive that is not of the base.

    The allocation helpers give no finding.  Each finding has the key of its archive and helper.  The ledger then
    counts the references of each archive and helper (THE THROW HELPERS).  Complexity: linear in the references.
    """
    found: list[check_report.Finding] = []
    for archive in sorted(set(artifacts) - base_archives):
        for reference in artifacts[archive].references:
            if not reference.is_live or not is_throw_helper(reference.symbol):
                continue
            if reference.symbol in ALLOCATION_HELPERS:
                continue
            found.append(
                finding(
                    f"{LIBRARY_PREFIX}{archive}",
                    HELPER_THROW,
                    reference.symbol,
                    f"{archive} references the throw helper {names.get(reference.symbol, reference.symbol)} in "
                    f"{reference.member}, in {names[reference.function]}.  A container or a view of the standard "
                    f"library throws there.  The count of each archive and helper can only fall: use a type of fixy "
                    f"or foundation",
                )
            )
    return found


def libstdcxx_finding(archive: str, symbol: str, shown: str, used: str, write: str) -> check_report.Finding:
    """Return the finding of one libstdc++ symbol of a symbol library (THE LIBSTDC++ SYMBOLS).

    `shown` is the demangled name, and `used` names the members that reference the symbol.
    """
    if mark_of(symbol) == ALLOW:
        said = (
            f"{archive} uses {shown} ({symbol}) in {used}, a symbol of the allowlist of plan section 13.4.  Write the "
            f"ledger again: {write}"
        )
    else:
        said = (
            f"{archive} uses the libstdc++ symbol {shown} ({symbol}), of the class {classify(symbol)}, in {used}.  A "
            f"change can only remove a libstdc++ use (rule R5): use a type of fixy or foundation"
        )
    return finding(f"{LIBRARY_PREFIX}{archive}", mark_of(symbol), symbol, said)


def libc_finding(
    archive: str, library: str, symbol: str, runtime: Runtime, used: str, write: str
) -> check_report.Finding:
    """Return the finding of one C runtime symbol of a symbol library (THE C RUNTIME SYMBOLS).

    `library` is the target name of the archive, and `used` names the members that reference the symbol.
    """
    mark = libc_mark_of(library, symbol, runtime)
    owner = runtime.owners[symbol]
    if mark == LIBC_ALLOW:
        said = (
            f"{archive} uses {symbol} of {owner} in {used}, a symbol of the allowlist of rule R5 for the library "
            f"{library}.  Write the ledger again: {write}"
        )
    else:
        said = (
            f"{archive} uses the C runtime symbol {symbol} of {owner} in {used}.  A change can only remove a C "
            f"runtime use (rule R5): call a door of fixy/os, or use a type of fixy or foundation"
        )
    return finding(f"{LIBRARY_PREFIX}{archive}", mark, symbol, said)


def symbol_findings(
    artifacts: Mapping[str, Artifact],
    symbol_libraries: Mapping[str, str],
    exports: frozenset[str],
    runtime: Runtime,
    names: Mapping[str, str],
    build_shown: str,
) -> list[check_report.Finding]:
    """Return one finding for each runtime symbol of each symbol library.

    `symbol_libraries` gives the target name of each archive.  A name that libstdc++ exports is a libstdc++ symbol
    (THE LIBSTDC++ SYMBOLS).  Each other name that a file of the C runtime gives is a C runtime symbol (THE C RUNTIME
    SYMBOLS).  Complexity: linear in the undefined names of the archives.
    """
    write = f"`python3 utils/bin/guard write --rule {RULE} --build-dir {build_shown}`"
    found: list[check_report.Finding] = []
    for archive in sorted(symbol_libraries):
        for symbol, members in sorted(artifacts[archive].undefined.items()):
            used = build_census.names_text(members)
            if symbol in exports:
                found.append(libstdcxx_finding(archive, symbol, names[symbol], used, write))
            elif symbol in runtime.owners:
                found.append(libc_finding(archive, symbol_libraries[archive], symbol, runtime, used, write))
    return found


def symbol_classes(symbols: Sequence[check_report.Finding]) -> str:
    """Return the marks of the runtime symbols of each library, and the classes of its libstdc++ ratchet symbols."""
    texts: list[str] = []
    for path in sorted({item.path for item in symbols}):
        marks = Counter(item.kind for item in symbols if item.path == path)
        classes = Counter(classify(item.entity) for item in symbols if item.path == path and item.kind == RATCHET)
        shown = ", ".join(f"{name} {classes[name]}" for name in CLASSES if classes[name]) or "none"
        texts.append(
            f"{path.removeprefix(LIBRARY_PREFIX)} {marks[ALLOW]} allow and {marks[RATCHET]} ratchet ({shown}), "
            f"{marks[LIBC_ALLOW]} libc-allow and {marks[LIBC_RATCHET]} libc-ratchet"
        )
    return "; ".join(texts) or "none"


def examine(rule: rule_registry.Rule, context: runner.ElfContext) -> Examination:
    """Read the libraries of the build, and judge their findings against the ledger (THE VERDICT).

    Raises:
        runner.NotApplicable: If the rule does not apply to the build (NOT APPLICABLE)
        runner.BuildError: If the build cannot be read
    """
    if context.cache_value(UBSAN_STRICT).upper() in CMAKE_TRUE:
        raise runner.NotApplicable(
            "the UBSan-strict checks of the dynamic type put the typeinfo of library classes into the archives.  The "
            "artifact of this build is then not the artifact of a release"
        )
    kind = context.kind()
    try:
        ledger = rule_ledger.read(rule, context.root)
    except (OSError, rows_grammar.RowsError) as problem:
        unread = check_report.Finding(
            "error",
            rule_ledger.shown_path(RULE),
            getattr(problem, "line", 0),
            RULE,
            f"the ledger cannot be read: {problem}",
        )
        return Examination((unread,), rule_ledger.empty(rule))
    artifacts, symbol_libraries, base_archives = read_libraries(context)
    tier = context.tier()
    configuration = plugin_runner.BuildConfiguration(
        kind, ((plugin_runner.KIND_KEY, kind), (plugin_runner.TIER_KEY, tier))
    )
    held = plugin_runner.is_held(ledger, configuration)
    exports = libstdcxx_exports(context.compiler())
    runtime = runtime_of(context.compiler())
    wanted = {reference.function for artifact in artifacts.values() for reference in artifact.references}
    wanted |= {reference.symbol for artifact in artifacts.values() for reference in artifact.references}
    wanted |= {symbol for artifact in artifacts.values() for _, symbol in artifact.defined}
    wanted |= {symbol for name in symbol_libraries for symbol in artifacts[name].undefined if symbol in exports}
    ordered = sorted(wanted)
    try:
        names = dict(zip(ordered, elf.demangle(ordered), strict=True))
    except elf.ElfError as problem:
        raise runner.BuildError(str(problem)) from None
    admits = ledger.admits()
    admit_rows = [key for key, _ in sorted(admits.items(), key=lambda item: item[1].line)]
    counted: Counter[str] = Counter()
    classes = class_findings(artifacts, names, admit_rows, counted, base_archives)
    runtime_symbols = symbol_findings(artifacts, symbol_libraries, exports, runtime, names, context.build_shown())
    helpers = helper_findings(artifacts, base_archives, names)
    symbols = [*runtime_symbols, *helpers]
    quiet = quiet_rows(admits, artifacts, names, {rule_ledger.key_of(item) for item in classes})
    view = ledger if held else replace(ledger, configurations=(), base=admits, deltas={})
    view = replace(view, base={key: entry for key, entry in view.base.items() if key not in quiet})
    judged = rule_ledger.compare(rule, view, [*classes, *(symbols if held else [])], kind if held else None)
    summary = (
        f"{RULE}: {len(artifacts)} libraries ({sum(item.symbols for item in artifacts.values())} symbols, "
        f"{counted['allocation']} throws of the allocator, {len(helpers)} references to throw helpers outside the "
        f"base), {len(runtime_symbols)} runtime symbols in {len(symbol_libraries)} libraries of {kind} on {tier}: "
        f"{symbol_classes(runtime_symbols)}"
    )
    return Examination(
        (*row_problems(ledger), *judged),
        ledger,
        configuration,
        held,
        Counter(rule_ledger.key_of(item) for item in symbols),
        Counter(key for key in map(rule_ledger.key_of, classes) if key in admits) + Counter(quiet),
        summary,
    )


def findings(context: runner.ElfContext) -> list[check_report.Finding]:
    """Give the findings of the libraries of the build, judged against the ledger in the tree of the context."""
    return list(examine(rule_ledger.rule_of(RULE), context).findings)


# ── The run and the write ──────────────────────────────────────────


def judge(rule: rule_registry.Rule, context: runner.ElfContext) -> list[check_report.Finding]:
    """Return the findings of the build judged against the ledger in the tree of the context (THE VERDICT).

    Raises:
        runner.NotApplicable: If the rule does not apply to the build
        runner.BuildError: If the build cannot be read
    """
    return list(examine(rule, context).findings)


def run(rule: rule_registry.Rule, context: runner.ElfContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build, write its findings, and print the rows of a build that the ledger does not hold.

    The run prints the rows of a configuration other than the base also after a failure (assay.guard.engines.plugin
    .counted, THE RUN).

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    try:
        examination = examine(rule, context)
    except runner.NotApplicable as reason:
        check_report.write_line(f"{RULE}: {reason}.  The rule does not apply")
        return runner.SKIPPED
    status = check_report.emit(list(examination.findings), RULE, warnings_dir)
    configuration = examination.configuration
    if configuration is None:
        return status
    check_report.write_line(examination.summary)
    ledger = examination.ledger
    if not examination.is_held:
        check_report.write_line(
            plugin_runner.held_line(
                RULE,
                ledger,
                configuration,
                "The check judges the throws and the classes of the build, and not its symbols and its counts of "
                "throw helpers",
            )
        )
    if not examination.is_held or status != 0:
        counted.print_rows(RULE, ledger, configuration, examination.symbols, False)
    return status


def write(rule: rule_registry.Rule, request: runner.WriteRequest) -> int:
    """Write the configuration and the symbol rows of the build (THE WRITE).

    Returns:
        0 when the ledger is written.  1 when the write refuses a symbol or the build cannot be read.  2 for the mode
        admit.  SKIPPED when the rule does not apply
    """
    if request.import_log is not None:
        return counted.import_rows(rule, request.root, request.import_log, import_refusal)
    if request.mode == rule_ledger.WriteMode.ADMIT:
        check_report.write_line(
            f"guard write: a person writes each row throw or vtable of {RULE} with its reason.  The mode admit does "
            f"not apply",
            is_error=True,
        )
        return 2
    context = runner.ElfContext(request.root, request.build_dir)
    try:
        examination = examine(rule, context)
    except runner.NotApplicable as reason:
        check_report.write_line(f"guard write: the rule {RULE} does not apply: {reason}", is_error=True)
        return runner.SKIPPED
    except runner.BuildError as problem:
        check_report.write_line(f"guard write: the ledger of {RULE} is not written: {problem}", is_error=True)
        return 1
    ledger, configuration = examination.ledger, examination.configuration
    problems = row_problems(ledger) if configuration is not None else list(examination.findings)
    if configuration is None or problems:
        check_report.write_line(
            f"guard write: the ledger of {RULE} is not written: {problems[0].message}", is_error=True
        )
        return 1
    name = configuration.name
    held = ledger.expected(name) if ledger.configuration(name) is not None else {}
    new_ratchet = sorted(key for key in examination.symbols if key.kind in RATCHET_MARKS and key not in held)
    for key in new_ratchet:
        check_report.write_line(f"guard write: {key.path} gets the ratchet symbol {key.entity}", is_error=True)
    risen = sorted(
        key for key, count in examination.symbols.items() if key.kind == HELPER_THROW and count > held.get(key, 0)
    )
    for key in risen:
        check_report.write_line(
            f"guard write: {key.path} has {examination.symbols[key]} references to the throw helper {key.entity}, "
            f"and the ledger holds {held.get(key, 0)}",
            is_error=True,
        )
    if (new_ratchet or risen) and request.mode != rule_ledger.WriteMode.RAISE:
        check_report.write_line(
            f"guard write: the ledger of {RULE} is not written: {len(new_ratchet)} new ratchet symbols and "
            f"{len(risen)} risen counts of throw helpers.  Remove the uses.  Give --raise only for "
            f"{', for '.join(RAISE_REASONS)}, and give the reason in the commit",
            is_error=True,
        )
        return 1
    counts = {**examination.symbols, **examination.admitted}
    written = rule_ledger.write_counts(
        rule,
        with_configuration(ledger, configuration),
        counts,
        name,
        rule_ledger.WriteMode.RAISE,
        request.reason,
        git.moves(request.root),
        keep_others=True,
    )
    rule_ledger.store(written, request.root)
    check_report.write_line(
        f"guard write: wrote {len(examination.symbols)} symbol rows of {name} on {configuration.tier} to "
        f"{rule_ledger.shown_path(RULE)}"
    )
    return 0
