"""codegen-shapes — a hot operation of a core family adds no instruction to its raw code, and a constant-time primitive has no branch.

THE PARTS
    The rule reads the compile database of a build
    (assay.guard.engines.elf.runner, THE BUILD), and it has two parts:
      core           the probe pairs of test/shape/core_shapes.cpp (THE PAIRS)
      constant-time  the wrappers of the primitives of fixy::ct (THE WRAPPERS)
    The core part reads the compile of the probe unit, and the
    constant-time part reads the compile of the release unit
    test/ct/dudect.cpp.  A build with no such compile is an error.  The
    expectations apply to x86_64.  On a host of a different architecture,
    the rule does not apply (exit 77).  A plant gives its parts with rows
    "part NAME" of its plant.txt.

THE PAIRS
    test/shape/core_shapes.cpp holds two functions for each probe of PROBES.
    The function NAME does one hot operation of a family of
    include/foundation/core.  The function NAME_raw does the same work with
    the raw code that the family replaces:
      * A std::atomic operation
      * A loop on a raw pointer
      * memcpy or memset
      * An access through std::unique_ptr or std::optional
      * std::unreachable.
    test/shape/CMakeLists.txt compiles the unit with the flags of the
    foundation library of the build.  Each build then compares the two
    functions of a pair with its own level, target, `-fharden-` flags and
    contract semantic.  A probe declares the differences that its family
    side can add (THE VERDICT).  A plant gives its probes with rows of its
    plant.txt, and PROBES does not apply to it:

        probe NAME [check=KIND[,KIND...]] [branch=yes] [calls=NAME[,NAME...]] | CLAIM

THE FLAGS OF THE PAIRS
    The probe unit must have the code words of each unit of the target
    foundation, and no other code word.  A code word is a word of the compile
    command that changes the code.  The level, the target, a sanitizer, a
    `-fharden-` flag and the contract semantic count too.  Each foundation
    unit must have the same code words.  A word that does not change the
    code, such as a warning, an include directory or the debug information,
    is no code word.

THE COMPILES
    The rule compiles the probe unit again into a scratch directory, with
    the command of the compile database less each word that starts with
    SANITIZER_WORDS.  A sanitizer adds a test to each memory access, and the
    test is not a part of the code of an operation.  The verdict reads this
    compile.  When the command holds a sanitizer word, the rule also compiles
    the command as it is.  The run then prints the counts of each pair with
    the sanitizer, and it gives no verdict for them.  A run after a change of
    a header of the base reads the code of the change.

THE WALK
    The rule disassembles the object with assay.readers.objdump.  A function
    has a hot part, and it can have a cold part, `NAME [clone .cold]` in a
    section .text.unlikely.  The walk reads the instructions of the two parts,
    with no nop that aligns the code.  An instruction is on a returning path
    when a path from the entry of the hot part goes to it.  A path from it
    must also go to a ret, or to a jump to a different function.  These
    instructions end a path that does not return:
      - a trap: ud2, int3 or hlt
      - a call of a function that does not return (NORETURN_NAMES,
        NORETURN_PREFIXES, and a name with the end NORETURN_SUFFIX)
      - a call before a trap, or at the end of its part.
    A path that does not return is a failure path.  Its kind is the first
    kind of FAILURE_KINDS that has a callee of the path, or trap when the
    path has no such callee.  The verdict reads only the instructions on
    returning paths, in the sequence of their addresses, the hot part first.

THE NORMALIZATION
    Each instruction becomes one text:
      - A general register becomes g64, g32, g16 or g8 for its width.  A
        vector register becomes v128, v256 or v512, and each register `k0`
        to `k7` becomes k.  rsp, rbp, rip, fs and gs keep their names,
        because each one has a specified role.  Two functions with a
        difference only in the allocation of their registers then give the
        same texts.
      - A jump in the function becomes the kind of its target: "->" for code
        that returns, "<-" for an earlier instruction of the same part (the
        edge of a loop), and "!KIND" for a failure path of the kind KIND.  A
        conditional jump with one returning edge and one failure path names
        the failure path.
      - A relocation replaces its address with its symbol.  A call and a tail
        jump name their callee.  A local constant .LC12 becomes .LC, and a
        different section symbol, such as a switch table, keeps only the
        kind of its section.  The displacement of rip goes.
      - The mnemonic keeps its lock or rep prefix.  A constant operand and
        each other displacement stay.

THE VERDICT
    The rule finds one longest subsequence of the texts of the two
    functions.  The family-only instructions are the instructions of NAME
    that are not in it.  Each family-only instruction has one class:
      check     a conditional jump into a failure path
      branch    a jump into returning code
      call      a call, or a jump to a different function
      division  div or idiv
      indexed   a memory operand with an index register, other than lea
      plain     each other instruction.
    These family-only instructions are errors:
      - A check of a kind that the probe does not declare with check=KIND.
        In a unit with a flag of HARDENING_FLAGS, a check of the kind trap
        is a second test on an edge of a branch.  The rule accepts two such
        checks for each family-only check and branch that the probe declares.
      - A branch, when the probe does not declare branch=yes.
      - A call of a callee that the probe does not declare with calls=NAME.
      - A division, and an indexed load.
      - An instruction in a loop of NAME, when the probe does not declare
        branch=yes.  A loop is the instructions from the target of a jump "<-"
        to that jump.
    The two functions must hold the same locked instructions (a lock prefix,
    or xchg with a memory operand).  The family side then does the same atomic
    work.  A probe that declares nothing must give NAME at most the count of
    instructions of NAME_raw.  The raw side can do more work, such as a spill
    to the stack.  The family side can also do the same work with different
    plain instructions.

THE COSTS
    A probe that declares a difference has a known cost on the hot path.
    When its family side has more instructions than its raw side, the run
    gives a warning with the two counts and the family-only instructions.
    The cost then stays in the output of each run.  The run also prints the
    counts of each pair, and the run prints the disassembly of the two
    functions of a probe with an error.

THE WRAPPERS
    The rule compiles one wrapper for each constant-time primitive of
    fixy::ct and each unsigned width.  It also compiles one wrapper of the
    eq of a static extent for each length of STATIC_EQ_LENGTHS.  It compiles
    them with the flags of each variant of a kind of build, for
    -march=native, x86-64-v3 and the x86-64 baseline.  In the disassembly of
    each wrapper it refuses:
      - a conditional branch (each jcc, jrcxz and loop): its direction shows
        the operand that it tests
      - a call, or a jump to a different function: the rule cannot see the
        code that it gets to
      - div or idiv: their latency changes with the operands
      - a memory operand with an index register: a load that a secret
        indexes shows the index through the cache
    The eq of a dynamic length is no wrapper, because its loop branches on
    the length, which is public.  The taint tests of test/ct cover it.

THE FLAGS OF THE WRAPPERS
    Each build takes the code words of the foundation units, with no word
    that changes with the kind of build (PER_BUILD).  The rule also removes
    the words that no variant holds (DROPPED).  Each variant adds its words
    and its levels:
      debug    DEBUG_FLAGS, at -O1
      release  the words of the release unit that change with the kind of
               build, at -O2 and -O3
      `verify` the release words with the contract semantic enforce and
               with no NDEBUG, at -O3.
    crucible_release_code_target() of cmake/CodeFlags.cmake gives the
    release unit the code of a Release build in each build type.  The preset
    `verify` gives the words of its variant.  The rule removes the level and
    the target from the words of PER_BUILD of the foundation units.  The
    words that stay must be the words of one variant.  A new flag of one kind
    of build then fails the rule in a build of that kind, until a variant has
    it.

THE CENSUSES
    A reflection walk in the wrapper unit stops its compile when the
    namespace of the primitives holds a function with no wrapper
    (COVERED_NAMES).  The rule reads the parse tree of each C++ file that
    git holds in include/, src/ and vessel/ (assay.readers.tsast).  It finds
    each name that states the constant-time grade, through role::CtCrypto or
    atom::constant_time.  A namespace alias of fixy::atom resolves.  A file
    with such a name must have its cases in CTCRYPTO_CASES.  The map is
    empty, because no production function states the grade.

Complexity: one or two compiles of the probe unit, and for each pair one longest subsequence, quadratic in its
instructions.  Twelve compiles of the wrapper unit, and one load of each parse tree.
"""

import os
import platform
import shlex
import tempfile
from bisect import bisect_left
from collections.abc import Iterable, Iterator, Mapping, Sequence
from dataclasses import dataclass, field
from enum import StrEnum
from pathlib import Path, PurePosixPath

from assay.core import proc
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.elf import runner
from assay.readers import compdb, objdump, tsast

RULE = "codegen-shapes"
CORE = "core"
CONSTANT_TIME = "constant-time"
PARTS = (CORE, CONSTANT_TIME)
EXPECTED_MACHINE = "x86_64"
FOUNDATION = "foundation"
PROBE_SOURCE = "test/shape/core_shapes.cpp"
PROBE_CMAKE = "test/shape/CMakeLists.txt"
RELEASE_SOURCE = "test/ct/dudect.cpp"
CT_CMAKE = "test/ct/CMakeLists.txt"
CT_HEADER = "include/fixy/ConstantTime.h"

# ── The constants of the core part ─────────────────────────────────

# The exit function of include/foundation/core/Report.h: each cold function of `foundation::core::detail` with a
# name that starts with report_.
FATAL_PREFIX = "foundation::core::detail::report_"
RAW_SUFFIX = "_raw"
COLD_SUFFIX = " [clone .cold]"
ANONYMOUS = "(anonymous namespace)::"
# The mark of a local constant, before the number that a compare of two instructions does not read.
LOCAL_CONSTANT = ".LC"
# The prefixes that a compare of two instructions keeps.
KEPT_PREFIXES = frozenset({"lock", "rep", "repz", "repe", "repnz", "repne"})
# The words of a compile command that start a sanitizer.  The compile of the verdict has none of them (THE COMPILES).
SANITIZER_WORDS = ("-fsanitize", "-fno-sanitize")
# The flags that put a second test and a trap on each edge of a conditional branch (THE VERDICT).  A word with the
# prefix HARDENING_OFF turns such a flag off.
HARDENING_FLAGS = ("-fharden-compares", "-fharden-conditional-branches")
HARDENING_OFF = "-fno-harden-"
# The sentence of a claim that tells the builds with the flags of HARDENING_FLAGS (cmake/CodeFlags.cmake).
HARDENED_BUILDS = f"Each build type other than Debug takes {' and '.join(HARDENING_FLAGS)}"
# The number of second tests after one branch: one on each of its two edges.
HARDENING_TESTS = 2
# The kinds of a failure path, the first kind first, each with the prefixes of the names of its callees (THE WALK).
FAILURE_KINDS: tuple[tuple[str, tuple[str, ...]], ...] = (
    ("fatal", (FATAL_PREFIX,)),
    ("contract", ("handle_contract_violation",)),
    ("assertion", ("std::__glibcxx_assert_fail",)),
    ("stack", ("__stack_chk_fail",)),
    ("exit", ("abort", "exit", "_exit", "std::terminate", "__cxa_throw", "__cxa_rethrow", "std::__throw_")),
    ("sanitizer", ("__asan_report_", "__ubsan_handle_")),
)
TRAP = "trap"
CHECK_KINDS = frozenset({*(kind for kind, _ in FAILURE_KINDS), TRAP})
# The callees that do not return.  A call of one ends its path (THE WALK).
NORETURN_NAMES = frozenset(
    {"abort", "exit", "_exit", "std::terminate", "__cxa_throw", "__cxa_rethrow", "__stack_chk_fail"}
)
NORETURN_PREFIXES = (
    FATAL_PREFIX,
    "std::__glibcxx_assert_fail",
    "std::__throw_",
    "__asan_report_",
    "__ubsan_handle_builtin_unreachable",
)
# The end of the name of an UBSan handler that stops the program.
NORETURN_SUFFIX = "_abort"
TRAP_MNEMONICS = frozenset({"ud2", "int3", "hlt"})
DIVISIONS = frozenset({"div", "idiv"})
# The mnemonics that calculate an address and read no memory.
ADDRESS_ONLY = frozenset({"lea"})
# The callee of a call or a jump with no symbol: a call in the object, or a jump through a register.
LOCAL_CALLEE = "<local>"
INDIRECT_CALLEE = "<indirect>"
# The distance from the address of a 32-bit relocation of a jump to the end of the jump.
JUMP_FIELD = 4
# The register names of x86-64 that keep their names, because each one has a specified role (THE NORMALIZATION).
FIXED_REGISTERS = frozenset({"rsp", "esp", "sp", "spl", "rbp", "ebp", "bp", "bpl", "rip", "fs", "gs"})
# The four legacy registers with a high byte, the two with no high byte, and the vector classes.
HIGH_BYTE_REGISTERS = ("a", "b", "c", "d")
INDEX_REGISTERS = ("si", "di")
VECTOR_CLASSES = (("xmm", "v128"), ("ymm", "v256"), ("zmm", "v512"))
MASK_CLASS = "k"
PROBE_KEYS = frozenset({"check", "branch", "calls"})
YES = "yes"
LIST_SEPARATOR = ","
# The words of a compile command that do not change the code.  An option of SEPARATE_VALUE takes the next word as
# its value.
SEPARATE_VALUE = frozenset({"-o", "-c", "-MF", "-MT", "-MQ", "-I", "-isystem", "-iquote", "-include", "-x"})
NOT_CODE_WORDS = frozenset({"-MD", "-MMD"})
NOT_CODE_PREFIXES = (
    "-W",
    "-pedantic",
    "-fdiagnostics-",
    "-fno-diagnostics-",
    "-ftemplate-backtrace-limit=",
    "-fconstexpr-ops-limit=",
    "-fplugin=",
    "-fplugin-arg-",
    "-fdebug-prefix-map=",
    "-ffile-prefix-map=",
    "-fmacro-prefix-map=",
    "-fmessage-length=",
    "-g",
    "-I",
    "-isystem",
    "-iquote",
)

# ── The constants of the constant-time part ────────────────────────

WIDTHS = (("u8", "std::uint8_t"), ("u16", "std::uint16_t"), ("u32", "std::uint32_t"), ("u64", "std::uint64_t"))
CT_NAMESPACE = "::fixy::ct"
# 8 is the tag length of include/fixy/SipHash.h.  12 has bytes after its last word of eight bytes.  16 fills a
# vector of SSE, and 32 fills a vector of AVX.
STATIC_EQ_LENGTHS = (8, 12, 16, 32)
MARCHES = ("native", "x86-64-v3", "x86-64")
DEBUG_FLAGS = ("-D_GLIBCXX_ASSERTIONS=1", "-D_GLIBCXX_SANITIZE_VECTOR=1")
OBSERVE = "-fcontract-evaluation-semantic=observe"
ENFORCE = "-fcontract-evaluation-semantic=enforce"
NDEBUG = "-DNDEBUG"
# The flags that change with the kind of build.  The rule removes them from the words of the compile database, and
# each variant adds its own flags.
PER_BUILD = (
    "-O",
    "-march=",
    "-mtune=",
    "-mcpu=",
    "-fharden-",
    "-fno-harden-",
    "-fcontract-evaluation-semantic=",
    "-DNDEBUG",
    "-UNDEBUG",
    "-D_GLIBCXX_ASSERTIONS",
    "-D_GLIBCXX_SANITIZE_VECTOR",
)
# The flags of PER_BUILD that the rule replaces with each level of a variant and each target of MARCHES.
SWEPT = ("-O", "-march=", "-mtune=", "-mcpu=")
# The flags that no variant holds: the sanitizers, the profile flags, the analyzer and the macro of the quarantine
# plugin.
DROPPED = (
    "-fsanitize",
    "-fno-sanitize",
    "-fprofile-",
    "-fno-profile-",
    "-fbranch-probabilities",
    "-fauto-profile",
    "-DCRUCIBLE_PGO",
    "-fanalyzer",
    "-DCRUCIBLE_QUARANTINE_ACTIVE",
)
# The functions that the namespace of the primitives can hold.  A new one fails the reflection census until it has a
# wrapper.
COVERED_NAMES = ("mask_from_bit", "select", "eq", "less", "is_zero", "cswap")
# The production files that state the constant-time grade, each with the wrappers of its functions.  A case is the
# name of a wrapper, the flag of a wrapper that takes a pointer, and the C++ definition of an extern "C" function of
# that name.  The map is empty: no production function states the grade.
CTCRYPTO_CASES: dict[str, tuple[tuple[str, bool, str], ...]] = {}
# The files that hold the definition of the role, of the atom or of the rules that read them.  A name there is part
# of a definition or of a self-test, and it gives the grade to no function.
CTCRYPTO_DEFINITION_FILES = frozenset({"include/fixy/Atom.h", "include/fixy/Collision.h", "include/fixy/Role.h"})
CTCRYPTO_ROLE = "CtCrypto"
CTCRYPTO_ATOM = "constant_time"
CTCRYPTO_ATOM_QUALIFIERS = frozenset({"atom", "at"})
CTCRYPTO_ROOTS = ("include/", "src/", "vessel/")
# The leaf nodes of a name in the grammar of the kit that can spell a grade.
NAME_LEAVES = ("identifier", "type_identifier", "namespace_identifier", "field_identifier")
# The nodes of a name above its leaf node, up to the node that holds the full name.
NAME_OWNERS = frozenset({"template_type", "template_function", "template_method"})
# The registers of a memory operand that give no address from a secret.
PUBLIC_REGISTERS = frozenset({"rsp", "rip", "ptr"})


class CompileFailed(RuntimeError):
    """A compile or a disassembly of the rule fails."""


# ── The probes ─────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Probe:
    """The declaration of one pair of functions of the probe unit (THE VERDICT).

    `checks` holds the kinds of the failure paths that the family side can add a check into, `branches` lets the
    family side add a branch and work in a loop, and `calls` holds the callees that the family side can add.
    """

    name: str
    claim: str
    checks: tuple[str, ...] = ()
    branches: bool = False
    calls: tuple[str, ...] = ()

    @property
    def declares_cost(self) -> bool:
        """True when the probe declares a difference that its family side can add."""
        return bool(self.checks or self.branches or self.calls)


# The declarations.  A probe with no declaration claims that its family operation adds no instruction to its raw
# code.  The claim of a probe with a declaration tells its cost.  When the family side of such a pair is longer, each
# run gives a warning with the counts of the pair (THE COSTS).
PROBES: tuple[Probe, ...] = (
    Probe("atomic_load", "load_acquire is the acquire load of std::atomic"),
    Probe("atomic_store", "store_release is the release store of std::atomic, with no lock"),
    Probe("atomic_exchange", "exchange_acq_rel is the xchg of std::atomic"),
    Probe("atomic_cas", "cas_acq_rel is the lock cmpxchg of std::atomic"),
    Probe(
        "atomic_cas_pair",
        "cas_acq_rel of a value of two words keeps the value in a register.  std::atomic stores the expected value "
        "on the stack",
    ),
    Probe("atomic_fetch_add", "fetch_add_acq_rel is the lock xadd of std::atomic"),
    Probe("atomic_fetch_sub", "fetch_sub_acq_rel is the neg and lock xadd of std::atomic"),
    Probe("atomic_fetch_or", "fetch_or_acq_rel is the lock cmpxchg loop of std::atomic"),
    Probe("atomic_fetch_and", "fetch_and_acq_rel is the lock cmpxchg loop of std::atomic"),
    Probe(
        "atomic_fetch_max",
        "fetch_max_acq_rel is a lock cmpxchg loop with a branch that does not write when the value stays the same.  "
        f"The raw form writes each time, and its loop has a different shape.  {HARDENED_BUILDS}.  On each edge of the "
        "two branches of the loop, these flags add a second test with a branch to a trap.  They also add copies of "
        "the operands and keep the success flag of the cmpxchg in a register",
        branches=True,
    ),
    Probe(
        "atomic_fetch_min",
        "fetch_min_acq_rel is a lock cmpxchg loop with a branch that does not write when the value stays the same.  "
        f"The raw form writes each time, and its loop has a different shape.  {HARDENED_BUILDS}.  On each edge of the "
        "two branches of the loop, these flags add a second test with a branch to a trap.  They also add copies of "
        "the operands and keep the success flag of the cmpxchg in a register",
        branches=True,
    ),
    Probe("tally_add", "Tally::add is the lock add of a relaxed std::atomic counter"),
    Probe("tally_read", "Tally::read is the load of a relaxed std::atomic counter"),
    Probe(
        "view_sum",
        "a range-for over a View is the loop of a raw pointer, because the optimizer removes the checks of the cursor",
    ),
    Probe("view_window", "window gives its Option with no test of the niche of the Option"),
    Probe(
        "view_append",
        "the append path is the bounds compare of the window and one memmove.  The copy into the window does no test "
        "of the lengths.  In a Release build, the family side has two more register moves than the raw side.  The "
        "memmove takes the place of the memcpy of the raw form, and on x86_64 glibc resolves the two to one function.  "
        "At -O1, the loop over the Option of the window keeps the test of its niche",
        branches=True,
        calls=("memmove",),
    ),
    Probe(
        "view_fill",
        "fill of bytes is the memset of the raw form.  The memset builtin accepts the null pointer of an empty View "
        "with the count 0, so the fill does no test for an empty View",
    ),
    Probe(
        "box_get",
        f"Box::get adds one test and one branch to a call of the fatal exit.  {HARDENED_BUILDS}.  On the hot path, "
        "these flags add a copy of the pointer and a second test of the copy, with a branch to a trap",
        checks=("fatal",),
    ),
    Probe(
        "option_expect",
        "Option::expect adds one test and one branch to a call of the fatal exit.  The flag is the low byte of the "
        "register and the payload is its high word, so the code also uses a shift and a move to read the payload.  "
        f"{HARDENED_BUILDS}.  On the hot path, these flags add a copy of the register and a second test of the flag "
        "in the copy, with a branch to a trap",
        checks=("fatal",),
    ),
    Probe(
        "option_expect_niche",
        "Option::expect of an Option with a niche adds one compare and one branch to a call of the fatal exit.  "
        f"{HARDENED_BUILDS}.  On the hot path, these flags add a copy of the count of the View and a second compare "
        "of the copy, with a branch to a trap",
        checks=("fatal",),
    ),
    Probe(
        "switch_unreachable",
        "a switch with fixy::unreachable() adds one bounds compare and one branch to a call of the fatal exit.  At "
        f"-O1, std::unreachable stops the process with a trap after the same compare.  {HARDENED_BUILDS}.  On the hot "
        "path, these flags add a copy of the value and a second bounds compare of the copy, with a branch to a trap",
        checks=("fatal",),
    ),
)


def probe_of_row(row: rows_grammar.Row) -> Probe:
    """Return the probe of a row "probe NAME KEY=VALUE... | CLAIM" of the plant.txt of a plant (THE PAIRS).

    Raises:
        rows_grammar.RowsError: If the row does not have that shape
    """
    if len(row.words) < 2 or len(row.cells) != 1:
        raise row.error("a probe row is 'probe NAME KEY=VALUE... | CLAIM'")
    values = rows_grammar.fields(row, PROBE_KEYS, 2)

    def names(key: str) -> tuple[str, ...]:
        """Return the names of a list field of the row."""
        return tuple(item for item in values.get(key, "").split(LIST_SEPARATOR) if item)

    checks = names("check")
    if values.get("branch", YES) != YES or not set(checks) <= CHECK_KINDS:
        raise row.error(f"branch has the value {YES}, and each kind of check is one of {sorted(CHECK_KINDS)}")
    return Probe(row.words[1], row.cells[0], checks, "branch" in values, names("calls"))


# ── The disassembly ────────────────────────────────────────────────


def strip_arguments(symbol: str) -> str:
    """Return a demangled function name with no parameter list at its end.

    A name that does not end with ")" stays as it is.  Complexity: linear in the length of the name.
    """
    if not symbol.endswith(")"):
        return symbol
    depth = 0
    for index in range(len(symbol) - 1, -1, -1):
        character = symbol[index]
        if character == ")":
            depth += 1
        elif character == "(":
            depth -= 1
            if depth == 0:
                return symbol[:index]
    return symbol


def relocation_target(symbol: str) -> str:
    """Return the symbol that a relocation names, without its parameter list, such as memmove."""
    if symbol.endswith(COLD_SUFFIX):
        return strip_arguments(symbol[: -len(COLD_SUFFIX)]) + COLD_SUFFIX
    return strip_arguments(symbol)


@dataclass(frozen=True, slots=True)
class Instruction:
    """One instruction of a disassembled function, with its memory operands and its relocation.

    `memory` holds the text of each memory operand.  `symbol` is the symbol of the relocation as objdump prints it,
    such as the section symbol .text.unlikely.NAME, `name` is the name that c++filt gives for it with no parameter
    list, and `addend` is the addend of the relocation.
    """

    offset: int
    prefixes: tuple[str, ...]
    mnemonic: str
    operands: str
    lines: tuple[str, ...]
    memory: tuple[str, ...] = ()
    symbol: str | None = None
    name: str | None = None
    addend: int = 0

    @property
    def is_padding(self) -> bool:
        """True when the instruction is a nop that aligns the code."""
        return self.mnemonic.startswith("nop") or (self.mnemonic == "xchg" and self.operands == "ax,ax")

    @property
    def is_locked(self) -> bool:
        """True when the instruction is a locked read-modify-write: a lock prefix, or xchg with a memory operand."""
        return "lock" in self.prefixes or (self.mnemonic == "xchg" and bool(self.memory))

    @property
    def is_jump(self) -> bool:
        """True when the instruction is a jump, conditional or not, or a loop instruction."""
        return self.mnemonic.startswith(("j", "loop"))

    @property
    def head(self) -> str:
        """The mnemonic with its lock prefix or rep prefix (THE NORMALIZATION)."""
        return " ".join([*(prefix for prefix in self.prefixes if prefix in KEPT_PREFIXES), self.mnemonic])

    @property
    def shown(self) -> str:
        """The instruction line as objdump prints it, with one space between two words."""
        return " ".join(self.lines[0].split()) if self.lines else self.head


@dataclass(frozen=True, slots=True)
class Part:
    """The hot part or the cold part of one function: its section, its symbol and its instructions."""

    section: str
    symbol: str
    instructions: tuple[Instruction, ...] = ()

    def counted(self) -> list[Instruction]:
        """Return the instructions with no nop that aligns the code."""
        return [instruction for instruction in self.instructions if not instruction.is_padding]

    def text(self) -> str:
        """Return the part as objdump prints it."""
        lines = [f"  <{self.symbol}> in {self.section}:"]
        lines += [f"    {line.strip()}" for instruction in self.instructions for line in instruction.lines]
        return "\n".join(lines)


@dataclass(frozen=True, slots=True)
class Code:
    """The code of one function of the probe unit: its hot part and its cold part."""

    name: str
    hot: Part | None = None
    cold: Part | None = None

    def parts(self) -> tuple[Part, ...]:
        """Return the hot part and the cold part that the function has, the hot part first."""
        return tuple(part for part in (self.hot, self.cold) if part is not None)

    def text(self) -> str:
        """Return each part of the function as objdump prints it."""
        parts = [part.text() for part in self.parts()]
        return "\n".join(parts) if parts else "  (no code)"


def function_name(symbol: str, keeps_arguments: bool = False) -> tuple[str, bool]:
    """Return the name of a function from its demangled symbol, and True when the symbol is a cold part.

    The name has no unnamed namespace.  It has the parameter list only when `keeps_arguments` is True.
    """
    is_cold = symbol.endswith(COLD_SUFFIX)
    name = symbol.removesuffix(COLD_SUFFIX) if is_cold else symbol
    if not keeps_arguments:
        name = strip_arguments(name)
    return name.removeprefix(ANONYMOUS), is_cold


def codes_of(functions: Sequence[objdump.Function], keeps_arguments: bool = False) -> dict[str, Code]:
    """Return the code of each function of a disassembly, by its name of function_name().

    The probe functions have no overloads, and the rule keys them by the name with no parameters.  A key with the
    parameter list (`keeps_arguments`) keeps two overloads apart.

    Complexity: linear in the instructions of the disassembly.
    """
    parts: dict[str, dict[bool, Part]] = {}
    for function in functions:
        name, is_cold = function_name(function.name, keeps_arguments)
        instructions = tuple(
            Instruction(
                item.offset,
                item.prefixes,
                item.mnemonic,
                item.operands,
                item.lines,
                item.memory_operands,
                None if item.relocation is None else item.relocation.symbol,
                None if item.relocation is None else relocation_target(item.relocation.name),
                0 if item.relocation is None else item.relocation.addend,
            )
            for item in function.instructions
        )
        parts.setdefault(name, {})[is_cold] = Part(function.section, function.name, instructions)
    return {name: Code(name, held.get(False), held.get(True)) for name, held in parts.items()}


# ── The walk ───────────────────────────────────────────────────────

# The position of one instruction of a function: the index of its part, and its index in the part.  The entry of the
# function is the first instruction of its hot part.
Place = tuple[int, int]
ENTRY: Place = (0, 0)


class StepClass(StrEnum):
    """The class of one instruction on a returning path (THE VERDICT)."""

    CHECK = "check"
    BRANCH = "branch"
    CALL = "call"
    DIVISION = "division"
    INDEXED = "indexed"
    PLAIN = "plain"


@dataclass(frozen=True, slots=True)
class Step:
    """One instruction on a returning path, after THE NORMALIZATION.

    `detail` is the kind of the failure path of a check, and the callee of a call.  `head` is the mnemonic with its
    kept prefix, and `shown` is the line that objdump prints.
    """

    text: str
    kind: StepClass
    detail: str
    head: str
    is_locked: bool
    is_in_loop: bool
    shown: str


@dataclass(frozen=True, slots=True)
class Flow:
    """The control flow of the parts of one function (THE WALK).

    `successors` gives the positions that each position can go to, `callees` gives the callee of each call and of
    each jump to a different function, and `returning` holds the positions on a returning path.
    """

    parts: tuple[tuple[Instruction, ...], ...]
    successors: Mapping[Place, tuple[Place, ...]]
    callees: Mapping[Place, str]
    returning: frozenset[Place]


def is_noreturn(callee: str) -> bool:
    """True when a call of the callee ends its path (THE WALK)."""
    return callee in NORETURN_NAMES or callee.startswith(NORETURN_PREFIXES) or callee.endswith(NORETURN_SUFFIX)


def place_at(offsets: Sequence[int], part: int, offset: int) -> Place | None:
    """Return the position of the first instruction of a part at or after an offset, or None after the last one.

    A jump to a nop that aligns the code then goes to the instruction after the nop.  Complexity: logarithmic in the
    instructions of the part.
    """
    index = bisect_left(offsets, offset)
    return (part, index) if index < len(offsets) else None


def jump_target(
    instruction: Instruction, part: int, sections: Mapping[str, int], offsets: Sequence[Sequence[int]]
) -> tuple[Place | None, bool]:
    """Return the position that a jump goes to in its function, and True when the jump goes out of the function.

    A jump with a relocation names a section of the function or a different symbol.  The address of the relocation is
    the last field of the jump, and the target is then the addend and JUMP_FIELD after the section start.  A jump with
    no relocation names an offset of its own part, or a register.
    """
    if instruction.symbol is not None:
        section = sections.get(instruction.symbol)
        if section is None:
            return None, True
        return place_at(offsets[section], section, instruction.addend + JUMP_FIELD), False
    number = objdump.hex_number(instruction.operands.split(" ", 1)[0].removeprefix("0x"))
    return (None, True) if number is None else (place_at(offsets[part], part, number), False)


def callee_of(instruction: Instruction) -> str:
    """Return the callee of a call or of a jump out of the function: its name, or a mark with no name."""
    if instruction.name is not None:
        return instruction.name
    number = objdump.hex_number(instruction.operands.split(" ", 1)[0].removeprefix("0x"))
    return INDIRECT_CALLEE if number is None else LOCAL_CALLEE


def reachable(edges: Mapping[Place, Sequence[Place]], starts: Iterable[Place]) -> set[Place]:
    """Return each position on a walk of the edges from the starts, the starts too.

    Complexity: linear in the edges.
    """
    found = set(starts)
    pending = list(found)
    while pending:
        for after in edges.get(pending.pop(), ()):
            if after not in found:
                found.add(after)
                pending.append(after)
    return found


def returning_places(successors: Mapping[Place, tuple[Place, ...]], exits: Iterable[Place]) -> frozenset[Place]:
    """Return each position on a path from the entry of the hot part to an exit (THE WALK).

    Complexity: linear in the edges.
    """
    predecessors: dict[Place, list[Place]] = {}
    for place, targets in successors.items():
        for target in targets:
            predecessors.setdefault(target, []).append(place)
    entry = [ENTRY] if ENTRY in successors else []
    return frozenset(reachable(predecessors, exits) & reachable(successors, entry))


def flow_of(code: Code) -> Flow:
    """Return the control flow of a function (THE WALK).

    A ret is an exit, and a jump to a different function is an exit too.  A trap and a call that does not return end
    a path.
    Complexity: linear in the instructions of the function.
    """
    owned = code.parts()
    parts = tuple(tuple(part.counted()) for part in owned)
    sections = {part.section: index for index, part in enumerate(owned)}
    offsets = [[instruction.offset for instruction in items] for items in parts]
    successors: dict[Place, tuple[Place, ...]] = {}
    callees: dict[Place, str] = {}
    exits: list[Place] = []
    for part, items in enumerate(parts):
        for index, instruction in enumerate(items):
            place = (part, index)
            following: tuple[Place, ...] = ((part, index + 1),) if index + 1 < len(items) else ()
            mnemonic = instruction.mnemonic
            successors[place] = following
            if mnemonic == "ret" or mnemonic in TRAP_MNEMONICS:
                successors[place] = ()
                if mnemonic == "ret":
                    exits.append(place)
            elif instruction.is_jump:
                target, leaves = jump_target(instruction, part, sections, offsets)
                is_conditional = mnemonic != "jmp"
                if leaves:
                    callees[place] = callee_of(instruction)
                    exits.append(place)
                taken = () if target is None else (target,)
                successors[place] = tuple(dict.fromkeys((*following, *taken))) if is_conditional else taken
            elif mnemonic == "call":
                callee = callee_of(instruction)
                callees[place] = callee
                if is_noreturn(callee) or not following or items[index + 1].mnemonic in TRAP_MNEMONICS:
                    successors[place] = ()
    return Flow(parts, successors, callees, returning_places(successors, exits))


def failure_kind(flow: Flow, start: Place) -> str:
    """Return the kind of the failure path from a position (THE WALK).

    The kind is the first kind of FAILURE_KINDS that has a callee of the path, or trap.  Complexity: linear in the
    instructions of the path.
    """
    called = {flow.callees[place] for place in reachable(flow.successors, [start]) if place in flow.callees}
    return next((kind for kind, prefixes in FAILURE_KINDS if any(name.startswith(prefixes) for name in called)), TRAP)


# ── The normalization ──────────────────────────────────────────────


# The class of each name of a general register of x86-64 with no specified role (THE NORMALIZATION).
GENERAL_CLASSES = (
    {
        name: width
        for letter in HIGH_BYTE_REGISTERS
        for name, width in (
            (f"r{letter}x", "g64"),
            (f"e{letter}x", "g32"),
            (f"{letter}x", "g16"),
            (f"{letter}l", "g8"),
            (f"{letter}h", "g8"),
        )
    }
    | {
        name: width
        for base in INDEX_REGISTERS
        for name, width in ((f"r{base}", "g64"), (f"e{base}", "g32"), (base, "g16"), (f"{base}l", "g8"))
    }
    | {
        name: width
        for number in range(8, 16)
        for name, width in (
            (f"r{number}", "g64"),
            (f"r{number}d", "g32"),
            (f"r{number}w", "g16"),
            (f"r{number}b", "g8"),
        )
    }
)


def register_class(word: str) -> str:
    """Return the class of a register name, or the word itself when it names no register of a class."""
    general = GENERAL_CLASSES.get(word)
    if general is not None or word in FIXED_REGISTERS:
        return general or word
    for prefix, vector in VECTOR_CLASSES:
        if word.startswith(prefix) and word[len(prefix) :].isdigit():
            return vector
    is_mask = len(word) == 2 and word.startswith(MASK_CLASS) and word[1].isdigit()
    return MASK_CLASS if is_mask else word


def operand_words(operands: str) -> list[str]:
    """Return the operands as words and marks: a word is a run of letters, digits and "_", and each other character
    is a mark.  Complexity: linear in the text."""
    words: list[str] = []
    current: list[str] = []
    for character in operands:
        if character.isalnum() or character == "_":
            current.append(character)
            continue
        if current:
            words.append("".join(current))
            current = []
        words.append(character)
    if current:
        words.append("".join(current))
    return words


def normalized_operands(operands: str) -> str:
    """Return the operands with the class of each register, and with no displacement of rip (THE NORMALIZATION)."""
    words = operand_words(operands)
    kept: list[str] = []
    index = 0
    while index < len(words):
        word = words[index]
        index += 1
        kept.append(register_class(word))
        has_displacement = index + 1 < len(words) and words[index] in ("+", "-") and words[index + 1].startswith("0x")
        if word == "rip" and has_displacement:
            index += 2
    return "".join(kept)


def symbol_text(instruction: Instruction) -> str:
    """Return the symbol of the relocation of an instruction that is no jump and no call (THE NORMALIZATION)."""
    symbol = instruction.symbol
    if symbol is None:
        return ""
    if symbol.startswith(LOCAL_CONSTANT) and symbol[len(LOCAL_CONSTANT) :].isdigit():
        return f" -> {LOCAL_CONSTANT}"
    if symbol.startswith("."):
        return f" -> .{symbol[1:].split('.', 1)[0]}"
    return f" -> {instruction.name or symbol}"


def classified(flow: Flow, place: Place, instruction: Instruction) -> tuple[str, StepClass, str, Place | None]:
    """Return the text and the class of one instruction on a returning path, and its failure kind or its callee.

    The fourth value is the target of a loop edge, or None.

    A conditional jump with a returning edge and a failure edge is a check of the failure path.  A jump into an
    earlier instruction of its own part is the edge of a loop.
    """
    head = instruction.head
    callee = flow.callees.get(place)
    if callee is not None:
        return f"{head} {callee}", StepClass.CALL, callee, None
    if instruction.is_jump:
        targets = flow.successors[place]
        returning = [target for target in targets if target in flow.returning]
        failures = [target for target in targets if target not in flow.returning]
        if failures and returning:
            kind = failure_kind(flow, failures[0])
            return f"{head} !{kind}", StepClass.CHECK, kind, None
        taken = returning[-1] if returning else None
        loop = taken if taken is not None and taken[0] == place[0] and taken[1] <= place[1] else None
        return f"{head} {'->' if loop is None else '<-'}", StepClass.BRANCH, "", loop
    text = f"{head} {normalized_operands(instruction.operands)}".strip() + symbol_text(instruction)
    if instruction.mnemonic in DIVISIONS:
        return text, StepClass.DIVISION, "", None
    if instruction.mnemonic not in ADDRESS_ONLY and any("*" in address for address in instruction.memory):
        return text, StepClass.INDEXED, "", None
    return text, StepClass.PLAIN, "", None


def steps_of(code: Code) -> tuple[Step, ...]:
    """Return the instructions of a function on its returning paths, in sequence (THE WALK, THE NORMALIZATION).

    Complexity: linear in the instructions of the function, and quadratic in the instructions of its loops.
    """
    flow = flow_of(code)
    places = [
        (part, index)
        for part, items in enumerate(flow.parts)
        for index in range(len(items))
        if (part, index) in flow.returning
    ]
    position = {place: number for number, place in enumerate(places)}
    in_loop: set[int] = set()
    drafts: list[tuple[str, StepClass, str, Instruction]] = []
    for number, place in enumerate(places):
        instruction = flow.parts[place[0]][place[1]]
        text, kind, detail, loop = classified(flow, place, instruction)
        if loop is not None and loop in position:
            in_loop.update(range(position[loop], number + 1))
        drafts.append((text, kind, detail, instruction))
    return tuple(
        Step(text, kind, detail, instruction.head, instruction.is_locked, number in in_loop, instruction.shown)
        for number, (text, kind, detail, instruction) in enumerate(drafts)
    )


# ── The verdict ────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Verdict:
    """The verdict of one pair: its problems, the family-only instructions, and the counts of the two sides."""

    problems: tuple[str, ...]
    added: tuple[Step, ...]
    family_count: int
    raw_count: int

    @property
    def is_longer(self) -> bool:
        """True when the family side has more instructions on its returning paths than the raw side."""
        return self.family_count > self.raw_count


def family_only(family: Sequence[Step], raw: Sequence[Step]) -> list[Step]:
    """Return the instructions of the family side that are not in one longest subsequence of the texts (THE VERDICT).

    Of two subsequences of the same length, the rule always takes the same one, and each run then gives the same
    instructions.  Complexity: O(n * m) in the instructions of the two sides.
    """
    rows, columns = len(family), len(raw)
    lengths = [[0] * (columns + 1) for _ in range(rows + 1)]
    for row in range(rows - 1, -1, -1):
        for column in range(columns - 1, -1, -1):
            if family[row].text == raw[column].text:
                lengths[row][column] = lengths[row + 1][column + 1] + 1
            else:
                lengths[row][column] = max(lengths[row + 1][column], lengths[row][column + 1])
    added: list[Step] = []
    row = column = 0
    while row < rows and column < columns:
        if family[row].text == raw[column].text:
            row += 1
            column += 1
        elif lengths[row + 1][column] >= lengths[row][column + 1]:
            added.append(family[row])
            row += 1
        else:
            column += 1
    added.extend(family[row:])
    return added


def is_hardened(flags: Sequence[str]) -> bool:
    """True when the last word of a flag of HARDENING_FLAGS, or of its negation, turns that flag on."""
    states: dict[str, bool] = {}
    for word in flags:
        for flag in HARDENING_FLAGS:
            if word == flag:
                states[flag] = True
            elif word == HARDENING_OFF + flag.removeprefix("-fharden-"):
                states[flag] = False
    return any(states.values())


def step_problem(probe: Probe, step: Step) -> str | None:
    """Return the problem of one family-only instruction, or None when the probe declares it (THE VERDICT)."""
    if step.kind is StepClass.CHECK and step.detail not in probe.checks:
        return f"the family side adds a check into a {step.detail} path: {step.shown}"
    if step.kind is StepClass.BRANCH and not probe.branches:
        return f"the family side adds a branch: {step.shown}"
    if step.kind is StepClass.CALL and step.detail not in probe.calls:
        return f"the family side adds a call of {step.detail}: {step.shown}"
    if step.kind is StepClass.DIVISION:
        return f"the family side adds a division: {step.shown}"
    if step.kind is StepClass.INDEXED:
        return f"the family side adds a load with an index register: {step.shown}"
    if step.is_in_loop and not probe.branches:
        return f"the family side adds work to a loop: {step.shown}"
    return None


def verdict(probe: Probe, family: Sequence[Step], raw: Sequence[Step], hardened: bool) -> Verdict:
    """Return the verdict of one pair (THE VERDICT).

    In a unit with a flag of HARDENING_FLAGS, a family-only check into a trap path that the probe does not declare is
    a second test.  The rule accepts HARDENING_TESTS of them for each family-only check and branch that the probe
    declares.
    """
    added = family_only(family, raw)
    problems: list[str] = []
    declared = hardening_tests = 0
    for step in added:
        if hardened and step.kind is StepClass.CHECK and step.detail == TRAP and TRAP not in probe.checks:
            hardening_tests += 1
            continue
        problem = step_problem(probe, step)
        if problem is not None:
            problems.append(problem)
        elif step.kind in (StepClass.CHECK, StepClass.BRANCH):
            declared += 1
    if hardening_tests > HARDENING_TESTS * declared:
        problems.append(
            f"the family side adds {hardening_tests} checks into a trap path, and the hardening of its {declared} "
            f"declared checks and branches gives at most {HARDENING_TESTS * declared}"
        )
    family_locks = sorted({step.head for step in family if step.is_locked})
    raw_locks = sorted({step.head for step in raw if step.is_locked})
    if family_locks != raw_locks:
        problems.append(
            f"the family side holds the locked instructions {family_locks}, and the raw side holds {raw_locks}.  The "
            f"two sides do not do the same atomic work"
        )
    if not probe.declares_cost and len(family) > len(raw):
        problems.append(
            f"the family side has {len(family)} instructions on its returning paths, and the raw side has "
            f"{len(raw)}.  The probe declares no cost"
        )
    return Verdict(tuple(problems), tuple(added), len(family), len(raw))


def pair_findings(
    functions: Mapping[str, Code], probes: Iterable[Probe], hardened: bool
) -> tuple[list[check_report.Finding], list[str], dict[str, Verdict]]:
    """Judge each pair of the probe object (THE VERDICT, THE COSTS).

    Returns:
        The errors and the warnings of the costs, the disassembly of each probe with an error, and the verdict of
        each probe
    """
    found: list[check_report.Finding] = []
    shown: list[str] = []
    verdicts: dict[str, Verdict] = {}
    for probe in probes:
        family = functions.get(probe.name, Code(probe.name))
        raw = functions.get(probe.name + RAW_SUFFIX, Code(probe.name + RAW_SUFFIX))
        result = verdict(probe, steps_of(family), steps_of(raw), hardened)
        verdicts[probe.name] = result
        found += [
            check_report.Finding(
                "error",
                PROBE_SOURCE,
                0,
                RULE,
                f"probe {probe.name}: {problem}.  The claim: {probe.claim}",
                "shape",
                probe.name,
            )
            for problem in result.problems
        ]
        if result.problems:
            shown.append(
                f"probe {probe.name}, the family function:\n{family.text()}\n"
                f"probe {probe.name}, the raw function:\n{raw.text()}"
            )
        elif probe.declares_cost and result.is_longer:
            added = ", ".join(step.text for step in result.added)
            message = (
                f"probe {probe.name}: the family side has {result.family_count} instructions on its returning paths, "
                f"and the raw side has {result.raw_count}.  The family-only instructions are: {added}.  The claim: "
                f"{probe.claim}"
            )
            found.append(check_report.Finding("warning", PROBE_SOURCE, 0, RULE, message, "cost", probe.name))
    return found, shown, verdicts


# ── The flags ──────────────────────────────────────────────────────


def code_flags(words: Sequence[str], source: str) -> list[str]:
    """Return the words of a compile command that change the code, in sequence, with no compiler and no source."""
    flags: list[str] = []
    index = 1
    while index < len(words):
        word = words[index]
        if word in SEPARATE_VALUE:
            index += 2
            continue
        index += 1
        if word == source or word in NOT_CODE_WORDS or word.startswith(NOT_CODE_PREFIXES):
            continue
        flags.append(word)
    return flags


def flag_findings(probe_flags: Sequence[str], base_flags: Sequence[Sequence[str]]) -> list[check_report.Finding]:
    """Compare the code words of the probe unit with those of the foundation units (THE FLAGS OF THE PAIRS)."""
    messages: list[str] = []
    base_sets = [set(flags) for flags in base_flags]
    if not base_sets:
        messages.append("the compile database has no unit of the target foundation.  The rule cannot compare the flags")
    for other in base_sets[1:]:
        if other != base_sets[0]:
            messages.append(f"the foundation units do not have the same code flags: {sorted(other ^ base_sets[0])}")
    probe_set = set(probe_flags)
    base_set = base_sets[0] if base_sets else probe_set
    messages += [
        f"each foundation unit compiles with {flag}, and the probe unit does not.  Give the probe unit of "
        f"{PROBE_CMAKE} the flags of the foundation library.  Then each build compares the pairs with its own flags"
        for flag in sorted(base_set - probe_set)
    ]
    messages += [
        f"the probe unit compiles with {flag}, and no foundation unit does.  Remove the flag from {PROBE_CMAKE}.  "
        f"Then the pairs compile with the flags of the foundation library"
        for flag in sorted(probe_set - base_set)
    ]
    return [check_report.Finding("error", PROBE_CMAKE, 0, RULE, message, "flags") for message in messages]


# ── The core part ──────────────────────────────────────────────────


def has_sanitizer(entry: compdb.CompileEntry) -> bool:
    """True when the compile command of a row holds a sanitizer word (THE COMPILES)."""
    return any(word.startswith(SANITIZER_WORDS) for word in entry.argv())


def command_words(entry: compdb.CompileEntry, obj: Path, with_sanitizers: bool) -> list[str]:
    """Return the compile command of a row with a different object (THE COMPILES).

    The command has no sanitizer word when `with_sanitizers` is False.
    """
    words: list[str] = []
    arguments = list(entry.argv())
    index = 0
    while index < len(arguments):
        word = arguments[index]
        index += 1
        if word == "-o" and index < len(arguments):
            words += [word, str(obj)]
            index += 1
        elif with_sanitizers or not word.startswith(SANITIZER_WORDS):
            words.append(word)
    return words


def compiled_functions(entry: compdb.CompileEntry, with_sanitizers: bool) -> tuple[objdump.Function, ...]:
    """Compile the unit of a row of the compile database into a scratch object, and return each of its functions.

    The quarantine plugin writes only the section of the object, because the command gives it no report directory.

    Raises:
        CompileFailed: If the compile or the disassembly fails
    """
    with tempfile.TemporaryDirectory(prefix="codegen-shapes-") as scratch:
        obj = Path(scratch) / "core_shapes.o"
        words = command_words(entry, obj, with_sanitizers)
        result = proc.run(proc.Command(words, timeout_s=proc.TOOL_S, cwd=Path(entry.directory)))
        if result.exit != 0:
            raise CompileFailed(f"the compile of {entry.file} fails:\n{result.err[-2000:]}")
        return disassembled(obj)


def compiled_codes(entry: compdb.CompileEntry, with_sanitizers: bool) -> dict[str, Code]:
    """Compile the unit of a row of the compile database, and return the code of each function (compiled_functions()).

    Raises:
        CompileFailed: If the compile or the disassembly fails
    """
    return codes_of(compiled_functions(entry, with_sanitizers))


def disassembled(obj: Path) -> tuple[objdump.Function, ...]:
    """Return each function of an object.

    Raises:
        CompileFailed: If objdump or c++filt fails
    """
    try:
        return objdump.functions(obj)
    except objdump.ObjdumpError as problem:
        raise CompileFailed(str(problem)) from None


def unit_entry(context: runner.ElfContext, source: str) -> compdb.CompileEntry | None:
    """Return the row of the compile database that compiles a unit of the tree, or None."""
    wanted = os.path.normpath(context.root / source)
    return next((entry for entry in context.entries() if os.path.normpath(entry.file) == wanted), None)


def foundation_entries(context: runner.ElfContext) -> list[compdb.CompileEntry]:
    """Return the rows of the compile database that compile an object of the target foundation, sorted by object."""
    objects = context.objects_of(FOUNDATION)
    return sorted(
        (entry for entry in context.entries() if entry.output in objects), key=lambda entry: entry.output or ""
    )


def planted_probes(context: runner.ElfContext) -> tuple[Probe, ...] | None:
    """Return the probes of the plant.txt of a plant, or None for a build (THE PAIRS).

    Raises:
        runner.BuildError: If a probe row does not have its shape
    """
    if context.planted is None:
        return None
    try:
        return tuple(probe_of_row(row) for row in context.planted.rows if row.words[0] == "probe")
    except rows_grammar.RowsError as problem:
        raise runner.BuildError(str(problem)) from None


def count_lines(
    verdicts: Mapping[str, Verdict], instrumented: Mapping[str, Code] | None, probes: Iterable[Probe]
) -> list[str]:
    """Return one line with the counts of each pair, and the counts with the sanitizer of the build (THE COSTS)."""
    lines: list[str] = []
    for probe in probes:
        result = verdicts[probe.name]
        line = f"{RULE}: probe {probe.name}: family {result.family_count}, raw {result.raw_count}"
        if instrumented is not None:
            family = len(steps_of(instrumented.get(probe.name, Code(probe.name))))
            raw = len(steps_of(instrumented.get(probe.name + RAW_SUFFIX, Code(probe.name + RAW_SUFFIX))))
            line += f".  With the sanitizer: family {family}, raw {raw}"
        lines.append(line)
    return lines


def core_findings(context: runner.ElfContext) -> tuple[list[check_report.Finding], list[str]]:
    """Return the findings of the core part, and the lines that the run prints (THE COMPILES, THE COSTS).

    Raises:
        runner.BuildError: If the build cannot be read
    """
    probe = unit_entry(context, PROBE_SOURCE)
    if probe is None:
        return [
            check_report.Finding(
                "error",
                PROBE_CMAKE,
                0,
                RULE,
                f"the compile database has no compile of {PROBE_SOURCE}.  The rule reads no probe",
                "unit",
            )
        ], []
    probe_flags = code_flags(probe.argv(), probe.file)
    found = flag_findings(probe_flags, [code_flags(entry.argv(), entry.file) for entry in foundation_entries(context)])
    probes = planted_probes(context) or PROBES
    try:
        functions = compiled_codes(probe, with_sanitizers=False)
        instrumented = compiled_codes(probe, with_sanitizers=True) if has_sanitizer(probe) else None
    except CompileFailed as problem:
        return [
            *found,
            check_report.Finding("error", PROBE_SOURCE, 0, RULE, " ".join(str(problem).split()), "compile"),
        ], []
    missing = [name for item in probes for name in (item.name, item.name + RAW_SUFFIX) if name not in functions]
    if missing:
        return [
            *found,
            check_report.Finding(
                "error",
                PROBE_SOURCE,
                0,
                RULE,
                f"the probe object holds no function {missing}.  The compiler gave the function a different "
                f"name, or {PROBE_SOURCE} does not hold it",
                "missing",
            ),
        ], []
    pairs, shown, verdicts = pair_findings(functions, probes, is_hardened(probe_flags))
    return [*found, *pairs], [*shown, *count_lines(verdicts, instrumented, probes)]


# ── The wrappers ───────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Variant:
    """The words and the levels that one kind of build adds to the words that each build takes."""

    name: str
    levels: tuple[str, ...]
    flags: tuple[str, ...]


@dataclass(frozen=True, slots=True)
class Plan:
    """The compiler, the words that each build takes, and the variants of the kinds of build."""

    compiler: str
    base: tuple[str, ...]
    variants: tuple[Variant, ...] = field(default_factory=tuple)

    def targets(self) -> Iterator[tuple[str, list[str]]]:
        """Yield the name and the words of each compile: each level of each variant, for each target."""
        for variant in self.variants:
            for level in variant.levels:
                for march in MARCHES:
                    yield (
                        f"{variant.name} -march={march} {level}",
                        [*self.base, *variant.flags, level, f"-march={march}"],
                    )


def split_flags(flags: Iterable[str]) -> tuple[tuple[str, ...], tuple[str, ...]]:
    """Return the words that each build takes, and each word of PER_BUILD with no word of SWEPT, one time each."""
    words = list(flags)
    base = tuple(flag for flag in words if not flag.startswith(PER_BUILD + DROPPED))
    per_build = tuple(
        dict.fromkeys(flag for flag in words if flag.startswith(PER_BUILD) and not flag.startswith(SWEPT))
    )
    return base, per_build


def variants_of(release: Sequence[str]) -> tuple[Variant, ...]:
    """Return the variants of the kinds of build from the words of PER_BUILD of the release unit.

    THE FLAGS OF THE WRAPPERS gives the variants.
    """
    verify = tuple(ENFORCE if flag == OBSERVE else flag for flag in release if flag != NDEBUG)
    return (
        Variant("debug", ("-O1",), DEBUG_FLAGS),
        Variant("release", ("-O2", "-O3"), tuple(release)),
        Variant("verify", ("-O3",), verify),
    )


def flag_problems(
    bases: Sequence[tuple[str, ...]], per_builds: Sequence[tuple[str, ...]], variants: Sequence[Variant]
) -> list[str]:
    """Return a problem for units that do not agree, and for words of PER_BUILD that no variant has."""
    problems: list[str] = []
    if any(base != bases[0] for base in bases) or any(set(item) != set(per_builds[0]) for item in per_builds):
        problems.append(
            "the foundation units do not have the same code flags.  The rule cannot take one set from them.  "
            "Give each unit of the foundation library the flags of the tree"
        )
    if not any(set(variant.flags) == set(per_builds[0]) for variant in variants):
        kinds = "; ".join(f"{variant.name} {' '.join(variant.flags) or 'with no word'}" for variant in variants)
        problems.append(
            f"the build gives the flags {sorted(per_builds[0])} of PER_BUILD, and no variant has these flags "
            f"and no other ({kinds}).  Give the flag to the variant of the kind of build that takes it.  Then "
            f"the rule compiles the wrappers with the flags of this build"
        )
    return problems


def census_prelude() -> str:
    """Return the reflection census, which is True when a namespace holds only covered functions (THE CENSUSES)."""
    covered = ", ".join(f'"{name}"' for name in COVERED_NAMES)
    return "\n".join(
        [
            "#include <meta>",
            "#include <string_view>",
            "namespace ct_disassembly {",
            "consteval bool is_covered(std::string_view name) {",
            f"    constexpr std::string_view covered[] = {{{covered}}};",
            "    for (std::string_view c : covered) if (c == name) return true;",
            "    return false;",
            "}",
            "consteval bool covers_every_primitive(std::meta::info ns) {",
            "    for (std::meta::info m : std::meta::members_of(ns, std::meta::access_context::unchecked())) {",
            "        if (!std::meta::is_function(m) && !std::meta::is_function_template(m)) continue;",
            "        if (!std::meta::has_identifier(m) || !is_covered(std::meta::identifier_of(m))) return false;",
            "    }",
            "    return true;",
            "}",
            "}  // namespace ct_disassembly",
        ]
    )


def wrapper_source(cases: Mapping[str, tuple[tuple[str, bool, str], ...]]) -> tuple[str, dict[str, bool]]:
    """Return the wrapper unit, and for each wrapper True when it takes pointers (THE WRAPPERS)."""
    names: dict[str, bool] = {}
    ns = CT_NAMESPACE
    coverage = (
        f'static_assert(ct_disassembly::covers_every_primitive(^^{ns}), "{ns} holds a function that the rule '
        f"{RULE} does not wrap.  Add its wrappers and its name to COVERED_NAMES.  Then the rule reads the compiled "
        f'code of the new primitive.");'
    )
    lines = ["#include <fixy/ConstantTime.h>", "#include <cstddef>", "#include <cstdint>", census_prelude(), coverage]
    for width, spelled in WIDTHS:
        specifications = (
            ("mask_from_bit", f"{spelled} b", f"return {ns}::mask_from_bit<{spelled}>(b);", spelled),
            ("select", f"{spelled} b, {spelled} x, {spelled} y", f"return {ns}::select<{spelled}>(b, x, y);", spelled),
            ("less", f"{spelled} x, {spelled} y", f"return {ns}::less<{spelled}>(x, y);", spelled),
            ("is_zero", f"{spelled} x", f"return {ns}::is_zero<{spelled}>(x);", spelled),
            ("cswap", f"{spelled} c, {spelled}* x, {spelled}* y", f"{ns}::cswap<{spelled}>(c, *x, *y);", "void"),
        )
        for primitive, parameters, body, result in specifications:
            name = f"ct_fixy_{primitive}_{width}"
            names[name] = "*" in parameters
            lines.append(f'extern "C" {result} {name}({parameters}) noexcept {{ {body} }}')
    for length in STATIC_EQ_LENGTHS:
        name = f"ct_fixy_eq_static_{length}"
        names[name] = True
        lines.append(
            f'extern "C" bool {name}(const std::byte* a, const std::byte* b) noexcept {{ '
            f"return {ns}::eq(std::span<const std::byte, {length}>{{a, {length}}}, "
            f"std::span<const std::byte, {length}>{{b, {length}}}); }}"
        )
    for rel, file_cases in cases.items():
        # A case of a header includes the header here.  A case of a source file includes its own headers.
        if rel.startswith("include/"):
            lines.append(f"#include <{PurePosixPath(rel).relative_to('include')}>")
        for name, takes_pointers, definition in file_cases:
            names[name] = takes_pointers
            lines.append(definition)
    return "\n".join(lines) + "\n", names


def instruction_text(instruction: objdump.Instruction) -> str:
    """Return the mnemonic and the operands of an instruction, and the symbol of its relocation."""
    base = f"{instruction.mnemonic} {instruction.operands}".strip()
    return base if instruction.relocation is None else f"{base} (relocation {instruction.relocation.symbol})"


def wrapper_problems(name: str, body: Sequence[objdump.Instruction], takes_pointers: bool) -> list[str]:
    """Return each refused instruction of one wrapper (THE WRAPPERS).

    A function that takes no pointer has no public address to read.  Each memory operand of a general register then
    reads an address that a secret can make.  A function that takes pointers can read through them, but not with an
    index register.
    """
    problems: list[str] = []
    for instruction in body:
        mnemonic = instruction.mnemonic
        text = instruction_text(instruction)
        if mnemonic.startswith("nop"):
            continue
        if (mnemonic.startswith("j") and mnemonic != "jmp") or mnemonic.startswith("loop"):
            problems.append(f"conditional branch: {text}")
        elif mnemonic == "call":
            problems.append(f"call: {text}")
        elif mnemonic == "jmp" and instruction.relocation is not None and instruction.relocation.symbol != name:
            problems.append(f"jump out of the function: {text}")
        elif mnemonic in ("div", "idiv"):
            problems.append(f"operand-dependent latency: {text}")
        # lea calculates an address and reads no memory.  It is arithmetic.
        if mnemonic == "lea":
            continue
        for address in instruction.memory_operands:
            registers = set(objdump.register_words(address)) - PUBLIC_REGISTERS
            if "*" in address:
                problems.append(f"indexed load or store: {text}")
            elif registers and not takes_pointers:
                problems.append(f"load or store through a calculated address: {text}")
    return problems


def wrapper_results(plan: Plan, source: Path, expected: Mapping[str, bool], include: Path) -> list[str]:
    """Compile the wrapper unit for each target of the plan, and return each problem with its target and wrapper.

    The compiles of the targets run at the same time, one thread for each.

    Raises:
        CompileFailed: If a compile or objdump fails, or a target gives no code for a wrapper
    """
    from concurrent.futures import ThreadPoolExecutor

    targets = list(plan.targets())

    def one_target(index: int) -> list[str]:
        """Compile and read the wrapper unit for one target."""
        target, flags = targets[index]
        obj = source.parent / f"ct_{index}.o"
        command = [
            plan.compiler,
            *flags,
            "-fno-asynchronous-unwind-tables",
            f"-I{include}",
            "-c",
            str(source),
            "-o",
            str(obj),
        ]
        result = proc.run(proc.Command(command, timeout_s=proc.TOOL_S))
        if result.exit != 0:
            raise CompileFailed(
                f"the compile of the wrappers fails for {target}: {shlex.join(command)[:300]}: {result.err[-1500:]}"
            )
        functions = {function.symbol: function.instructions for function in disassembled(obj)}
        missing = [name for name in expected if name not in functions]
        if missing:
            raise CompileFailed(f"{target} gives no code for {missing}")
        return [
            f"{target} {name}: {problem}"
            for name, takes_pointers in expected.items()
            for problem in wrapper_problems(name, functions[name], takes_pointers)
        ]

    with ThreadPoolExecutor(max_workers=len(targets)) as pool:
        return [problem for found in pool.map(one_target, range(len(targets))) for problem in found]


# ── The census of the grade ────────────────────────────────────────


def whole_name(leaf: tsast.Node) -> tsast.Node:
    """Return the full name of a leaf node: its template name, then the name with its namespaces around it.

    A leaf in the template arguments of a name is a name of its own.  The walk then stops at an argument list.
    """
    node = leaf
    while node.parent is not None and (
        node.parent.type == "qualified_identifier" or (node.parent.type in NAME_OWNERS and node.field == "name")
    ):
        node = node.parent
    return node


def states_grade(name: tsast.Node, aliases: Sequence[tsast.NamespaceAlias]) -> bool:
    """Return True when a full name states the constant-time grade through the role or the atom."""
    qualified = tsast.qualified_parts(name)
    if qualified is None:
        return False
    is_global, parts = qualified
    for index, part in enumerate(parts):
        before = parts[:index]
        if part == CTCRYPTO_ROLE:
            return True
        if part == CTCRYPTO_ATOM and before:
            resolved = tsast.resolve_namespace(before, name, list(aliases), is_global=is_global)
            if before[-1] in CTCRYPTO_ATOM_QUALIFIERS or resolved[-1:] == ("atom",):
                return True
    return False


def grade_statements(root: Path) -> tuple[dict[str, list[tuple[int, str]]], list[str]]:
    """Return the names that state the constant-time grade in each file of the roots, and each file with no tree.

    The rule reads the parse tree of each C++ file that git holds in
    CTCRYPTO_ROOTS (THE CENSUSES).  Each full name counts one time.
    Complexity: one load of each parse tree, and linear in the leaf nodes of
    names.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    files = [
        path
        for path in tsast.tracked_files(root)
        if path.startswith(CTCRYPTO_ROOTS) and tsast.is_in_cpp_scope(path) and path not in CTCRYPTO_DEFINITION_FILES
    ]
    statements: dict[str, list[tuple[int, str]]] = {}
    unreadable: list[str] = []
    for path, tree in zip(files, tsast.parse_files(root, files), strict=True):
        if tree.diagnostic is not None:
            unreadable.append(path)
            continue
        aliases = tsast.namespace_aliases(tree)
        seen: set[int] = set()
        for leaf in tree.find(*NAME_LEAVES):
            if leaf.text not in (CTCRYPTO_ROLE, CTCRYPTO_ATOM):
                continue
            name = whole_name(leaf)
            if name.index in seen or not states_grade(name, aliases):
                continue
            seen.add(name.index)
            statements.setdefault(path, []).append((name.line, tsast.spelled(name)))
    return statements, unreadable


def grade_findings(root: Path, cases: Mapping[str, tuple[tuple[str, bool, str], ...]]) -> list[check_report.Finding]:
    """Return an error for each statement of the grade with no case, each incorrect case and each unparsed file.

    THE CENSUSES gives the census.  An unparsed file is a file that the kit cannot parse.

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    statements, unreadable = grade_statements(root)
    found = [
        check_report.Finding(
            "error",
            path,
            line,
            RULE,
            f"a production name states the constant-time grade with {spelled}, and it has no case in CTCRYPTO_CASES",
            "grade",
            spelled,
        )
        for path, lines in statements.items()
        if path not in cases
        for line, spelled in lines
    ]
    found += [
        check_report.Finding(
            "error",
            path,
            0,
            RULE,
            "the tree-sitter kit cannot parse the file.  The census cannot read it",
            "unparsed",
        )
        for path in unreadable
    ]
    for path, file_cases in cases.items():
        if not file_cases:
            found.append(
                check_report.Finding(
                    "error",
                    path,
                    0,
                    RULE,
                    "the case entry of the file holds no wrapper.  It does a check of nothing",
                    "case",
                )
            )
        if path not in statements:
            found.append(
                check_report.Finding(
                    "error", path, 0, RULE, "the case entry names a file that states no constant-time grade", "case"
                )
            )
    return found


def constant_time_findings(context: runner.ElfContext) -> list[check_report.Finding]:
    """Return the findings of the constant-time part (THE WRAPPERS).

    Raises:
        runner.BuildError: If the build cannot be read
        runner.NotApplicable: If the pinned kit of the parse trees is not installed
    """
    release = unit_entry(context, RELEASE_SOURCE)
    units = foundation_entries(context)
    if release is None or not units:
        return [
            check_report.Finding(
                "error",
                CT_CMAKE,
                0,
                RULE,
                f"the compile database has no compile of {RELEASE_SOURCE} or of the target foundation.  The rule "
                f"has no flags for the wrappers",
                "unit",
            )
        ]
    splits = [split_flags(code_flags(entry.argv(), entry.file)) for entry in units]
    variants = variants_of(split_flags(code_flags(release.argv(), release.file))[1])
    found = [
        check_report.Finding("error", CT_CMAKE, 0, RULE, problem, "flags")
        for problem in flag_problems([base for base, _ in splits], [item for _, item in splits], variants)
    ]
    plan = Plan(units[0].argv()[0], splits[0][0], variants)
    text, names = wrapper_source(CTCRYPTO_CASES)
    try:
        with tempfile.TemporaryDirectory(prefix="codegen-shapes-ct-") as scratch:
            source = Path(scratch) / "ct_wrappers.cpp"
            source.write_text(text, encoding="utf-8")
            problems = wrapper_results(plan, source, names, context.root / "include")
    except CompileFailed as failed:
        return [*found, check_report.Finding("error", CT_HEADER, 0, RULE, " ".join(str(failed).split()), "compile")]
    found += [check_report.Finding("error", CT_HEADER, 0, RULE, problem, "wrapper") for problem in problems]
    try:
        return [*found, *grade_findings(context.root, CTCRYPTO_CASES)]
    except tsast.KitMissing as problem:
        raise runner.NotApplicable(f"the census of the grade reads the parse trees: {problem}") from None


# ── The rule ───────────────────────────────────────────────────────


def parts_of(context: runner.ElfContext) -> tuple[str, ...]:
    """Return the parts that the rule reads: each part for a build, and the rows "part NAME" of a plant."""
    if context.planted is None:
        return PARTS
    return tuple(
        row.words[1]
        for row in context.planted.rows
        if row.words[0] == "part" and len(row.words) == 2 and row.words[1] in PARTS
    )


def examine(context: runner.ElfContext) -> tuple[list[check_report.Finding], list[str]]:
    """Return the findings of each part of the build, and the lines that the run prints (THE PARTS, THE COSTS).

    Raises:
        runner.NotApplicable: If the host is not x86_64, or the call gives no build directory
        runner.BuildError: If the build cannot be read
    """
    machine = platform.machine()
    if machine != EXPECTED_MACHINE:
        raise runner.NotApplicable(f"the expectations apply to {EXPECTED_MACHINE}, and this host is {machine}")
    parts = parts_of(context)
    found: list[check_report.Finding] = []
    shown: list[str] = []
    if CORE in parts:
        found, shown = core_findings(context)
    if CONSTANT_TIME in parts:
        found += constant_time_findings(context)
    return found, shown


def findings(context: runner.ElfContext) -> list[check_report.Finding]:
    """Give the findings of each part of the build (THE PARTS)."""
    return examine(context)[0]


def judge(rule: rule_registry.Rule, context: runner.ElfContext) -> list[check_report.Finding]:
    """Return the findings of the build.  The rule has no ledger, and each finding stays as it is.

    Raises:
        runner.NotApplicable: If the rule does not apply to the build
        runner.BuildError: If the build cannot be read
    """
    return examine(context)[0]


def run(rule: rule_registry.Rule, context: runner.ElfContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build, write its findings, and print the counts of each pair (THE COSTS).

    The run also prints the disassembly of the two functions of each probe with an error.

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    try:
        found, shown = examine(context)
    except runner.NotApplicable as reason:
        check_report.write_line(f"{RULE}: {reason}.  The rule does not apply")
        return runner.SKIPPED
    status = check_report.emit(found, RULE, warnings_dir)
    for block in shown:
        check_report.write_line(block)
    if status == 0:
        check_report.write_line(
            f"{RULE}: the {len(PROBES)} pairs hold with the flags of this build, the probe unit has the flags of the "
            f"foundation library, and {len(wrapper_source(CTCRYPTO_CASES)[1])} wrappers have no refused instruction "
            f"for {len(MARCHES) * 4} targets"
        )
    return status
