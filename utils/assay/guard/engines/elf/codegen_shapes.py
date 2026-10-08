"""codegen-shapes — a hot operation of a core family compiles to its raw code, and a constant-time primitive has no branch.

THE PARTS
    The rule reads the compile database of a build
    (assay.guard.engines.elf.runner, THE BUILD), and it has two parts:
      core           the probes of test/shape/core_shapes.cpp (THE PROBES)
      constant-time  the wrappers of the primitives of fixy::ct (THE WRAPPERS)
    The core part reads the compile of the probe unit, and the
    constant-time part reads the compile of the release unit
    test/ct/dudect.cpp.  A build with no such compile is an error.  The
    expectations apply to x86_64.  On a host of a different architecture,
    the rule does not apply (exit 77).  A plant gives its parts with rows
    "part NAME" of its plant.txt.

THE PROBES
    test/shape/core_shapes.cpp holds two functions for each probe of PROBES.
    The function NAME does one hot operation of a family of
    include/foundation/core.  The function NAME_raw does the same work with
    the raw code that the family replaces:
      * A std::atomic operation
      * A loop on a raw pointer
      * memcpy or memset
      * An access through std::unique_ptr or std::optional
      * std::unreachable.
    test/shape/CMakeLists.txt compiles the unit into the object library
    core_shape_probes.  Each preset gives the
    unit the same flags: the code flags of cmake/CodeFlags.cmake, and
    FIXED_FLAGS, with -O3 and -march=x86-64-v3.  The rule compiles the unit
    again into a scratch directory with the command of the compile database.
    A run after a change of a header of the base then reads the code of the
    change.  A plant gives its expectations with rows of its plant.txt, and
    PROBES does not apply to it:

        probe NAME relation=same|loop|cost bound=N [extra=N] [calls=A,B] [raw-calls=A,B] [cold=yes] [locked=yes] | CLAIM

THE FLAGS OF THE PROBES
    The rule takes the code words of the probe unit and of each unit of the
    target foundation.  The words of the probe unit that change with the
    preset (PER_PRESET) must be FIXED_FLAGS.  Its other words must be the
    words of the foundation units, less their words that change with the
    preset.  A flag that the tree adds to each build, or removes from each
    build, then fails the rule until test/shape/CMakeLists.txt changes too.

THE SHAPE
    The rule disassembles the probe object with assay.readers.objdump.  A
    function has a hot part, and it can have a cold part,
    `NAME [clone .cold]` in a section .text.unlikely.  A count of
    instructions does not count the nop instructions that align the code.
    For each probe:
      - The bound: the hot part of NAME has at most `bound` instructions.
      - The relation to NAME_raw:
          same   the two hot parts have the same instructions, in sequence
          loop   each inner loop is the same in the two hot parts.  NAME
                 has at most `extra` more instructions.  An inner loop is
                 the instructions from the target of a conditional jump back
                 to that jump
          cost   the hot part of NAME has at most `extra` instructions more
                 than the hot part of NAME_raw
      - The calls: the hot part of NAME calls the functions of `calls`, and
        no other function.  NAME_raw calls those of `raw_calls`.  A jump to
        a different function is a call.
      - The lock: with `locked`, each hot part holds a locked instruction (a
        lock prefix, or xchg with a memory operand).  Without `locked`, no
        hot part holds one.
      - The cold call: with `cold`, NAME has a cold part, and its hot part
        jumps into it.  The cold part calls the exit function of
        include/foundation/core/Report.h.  Without `cold`, NAME has no cold
        part.
    The run prints the disassembly of the two functions of a probe that
    fails.

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

Complexity: one compile of the probe unit, twelve compiles of the wrapper unit, and one load of each parse tree.
"""

import os
import platform
import shlex
import tempfile
from collections.abc import Iterable, Iterator, Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path, PurePosixPath
from typing import Literal

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

# The flags of the probe unit that have a different value in each preset of the tree.  The expectations of PROBES
# apply to these values.
FIXED_FLAGS = (
    "-O3",
    "-march=x86-64-v3",
    "-mtune=generic",
    "-DNDEBUG",
    "-fPIC",
    "-fno-strict-overflow",
    "-fcontract-evaluation-semantic=observe",
)
# The exit function of include/foundation/core/Report.h: each cold function of `foundation::core::detail` with a
# name that starts with report_.
FATAL_PREFIX = "foundation::core::detail::report_"
RAW_SUFFIX = "_raw"
COLD_SUFFIX = " [clone .cold]"
ANONYMOUS = "(anonymous namespace)::"
# The marks of a local constant, before the number that a compare of two instructions does not read.
LOCAL_MARKS = (".LC", ".")
# The prefixes that a compare of two instructions keeps.
KEPT_PREFIXES = frozenset({"lock", "rep", "repz", "repe", "repnz", "repne"})
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
# The flags that change with the preset or with the target in the tree: the level, the target and the sanitizers.
# The list also holds the -fharden- flags, the contract semantic, the profile and the analyzer.  It also holds the
# overflow rule of the UBSan-strict preset, the -fPIC of a library and the macros of a build type.
PER_PRESET = (
    "-O",
    "-march=",
    "-mtune=",
    "-mcpu=",
    "-mno-outline-atomics",
    "-fsanitize",
    "-fno-sanitize",
    "-fharden-",
    "-fno-harden-",
    "-fcontract-evaluation-semantic=",
    "-fprofile-",
    "-fno-profile-",
    "-fbranch-probabilities",
    "-fauto-profile",
    "-fanalyzer",
    "-fstrict-overflow",
    "-fno-strict-overflow",
    "-fPIC",
    "-fPIE",
    "-fpic",
    "-fpie",
    "-fno-pic",
    "-fno-pie",
    "-D_GLIBCXX_ASSERTIONS",
    "-D_GLIBCXX_SANITIZE_VECTOR",
    "-DNDEBUG",
    "-UNDEBUG",
    "-DCRUCIBLE_PGO",
    "-DCRUCIBLE_CONTRACT_SEMANTIC_IGNORE",
)
RELATIONS = ("same", "loop", "cost")
PROBE_KEYS = frozenset({"relation", "bound", "extra", "calls", "raw-calls", "cold", "locked"})
FLAG_KEYS = ("cold", "locked")
YES = "yes"

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

Relation = Literal["same", "loop", "cost"]


class CompileFailed(RuntimeError):
    """A compile or a disassembly of the rule fails."""


# ── The probes ─────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Probe:
    """The expectation of one pair of functions of the probe unit."""

    name: str
    relation: Relation
    bound: int
    claim: str
    extra: int = 0
    calls: tuple[str, ...] = ()
    raw_calls: tuple[str, ...] = ()
    cold: bool = False
    locked: bool = False


# The expectations.  A bound is the count of instructions that the hot part has on 2026-10-02, with the patched GCC
# 16.2.1.  A change that makes a hot part longer fails.  A change of a family that adds a check to a hot path then
# stops here.  Each count includes the endbr64 of -fcf-protection and the ret.
PROBES: tuple[Probe, ...] = (
    Probe("atomic_load", "same", 3, "load_acquire is the acquire load of std::atomic"),
    Probe("atomic_store", "same", 3, "store_release is the release store of std::atomic, with no lock"),
    Probe("atomic_exchange", "same", 4, "exchange_acq_rel is the xchg of std::atomic", locked=True),
    Probe("atomic_cas", "same", 5, "cas_acq_rel is the lock cmpxchg of std::atomic", locked=True),
    Probe(
        "atomic_cas_pair",
        "cost",
        5,
        "cas_acq_rel of a value of two words keeps the value in a register.  std::atomic stores the expected "
        "value on the stack",
        locked=True,
    ),
    Probe("atomic_fetch_add", "same", 4, "fetch_add_acq_rel is the lock xadd of std::atomic", locked=True),
    Probe("atomic_fetch_sub", "same", 5, "fetch_sub_acq_rel is the neg and lock xadd of std::atomic", locked=True),
    Probe("atomic_fetch_or", "same", 9, "fetch_or_acq_rel is the lock cmpxchg loop of std::atomic", locked=True),
    Probe("atomic_fetch_and", "same", 9, "fetch_and_acq_rel is the lock cmpxchg loop of std::atomic", locked=True),
    Probe(
        "atomic_fetch_max",
        "cost",
        7,
        "fetch_max_acq_rel is a lock cmpxchg loop that does not write when the value stays the same",
        locked=True,
    ),
    Probe(
        "atomic_fetch_min",
        "cost",
        7,
        "fetch_min_acq_rel is a lock cmpxchg loop that does not write when the value stays the same",
        locked=True,
    ),
    Probe("tally_add", "same", 3, "Tally::add is the lock add of a relaxed std::atomic counter", locked=True),
    Probe("tally_read", "same", 3, "Tally::read is the load of a relaxed std::atomic counter"),
    Probe(
        "view_sum",
        "loop",
        77,
        "a range-for over a View is the vectorized loop of a raw pointer loop, because the optimizer removes the "
        "checks of the cursor",
    ),
    Probe("view_window", "same", 13, "window gives its Option with no test of the niche of the Option"),
    Probe(
        "view_append",
        "cost",
        18,
        "the append path is the bounds compare of the window and one memmove.  The copy into the window does no "
        "test of the lengths.  The family code has one register move more",
        extra=1,
        calls=("memmove",),
        raw_calls=("memcpy",),
    ),
    Probe(
        "view_fill",
        "cost",
        7,
        "fill of bytes is one memset.  The family code first tests for an empty View and returns, because a View "
        "can be empty with no buffer, and memset of a null pointer is undefined",
        extra=3,
        calls=("memset",),
        raw_calls=("memset",),
    ),
    Probe(
        "box_get",
        "cost",
        6,
        "Box::get adds one test and one branch to a cold call of the fatal exit",
        extra=2,
        cold=True,
    ),
    Probe(
        "option_expect",
        "cost",
        7,
        "Option::expect adds one test and one branch to a cold call of the fatal exit.  A payload that fills the "
        "low word of the register puts the flag at bit 32, so the code also uses a move and a shift to read the "
        "flag",
        extra=4,
        cold=True,
    ),
    Probe(
        "option_expect_niche",
        "cost",
        6,
        "Option::expect of an Option with a niche adds one compare and one branch to a cold call of the fatal exit",
        extra=2,
        cold=True,
    ),
    Probe(
        "switch_unreachable",
        "cost",
        7,
        "a switch with fixy::unreachable() has one bounds compare on the hot path, and the call of the fatal "
        "exit is in the cold part",
        extra=2,
        cold=True,
    ),
)


def probe_of_row(row: rows_grammar.Row) -> Probe:
    """Return the probe of a row "probe NAME KEY=VALUE... | CLAIM" of the plant.txt of a plant (THE PROBES).

    Raises:
        rows_grammar.RowsError: If the row does not have that shape
    """
    if len(row.words) < 2 or len(row.cells) != 1:
        raise row.error("a probe row is 'probe NAME KEY=VALUE... | CLAIM'")
    values = rows_grammar.fields(row, PROBE_KEYS, 2)
    relation = values.get("relation", "")
    if relation not in RELATIONS or "bound" not in values or any(values.get(key, YES) != YES for key in FLAG_KEYS):
        raise row.error(
            f"a probe row has relation={'|'.join(RELATIONS)} and bound=N, and cold and locked have the value {YES}"
        )

    def names(key: str) -> tuple[str, ...]:
        """Return the names of a list field of the row."""
        return tuple(item for item in values.get(key, "").split(",") if item)

    relation_value: Relation = "same" if relation == "same" else "loop" if relation == "loop" else "cost"
    return Probe(
        row.words[1],
        relation_value,
        rows_grammar.number(values["bound"], 10**6, row, "bound"),
        row.cells[0],
        rows_grammar.number(values.get("extra", "0"), 10**6, row, "extra"),
        names("calls"),
        names("raw-calls"),
        "cold" in values,
        "locked" in values,
    )


# ── The disassembly ────────────────────────────────────────────────


def without_local_number(symbol: str) -> str:
    """Return a symbol with no number at its end after ".LC" or ".", such as ".LC" for ".LC12"."""
    stripped = symbol.rstrip("0123456789")
    return stripped if stripped != symbol and stripped.endswith(LOCAL_MARKS) else symbol


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
    """One instruction of a disassembled function, with the symbol of its relocation."""

    offset: int
    prefixes: tuple[str, ...]
    mnemonic: str
    operands: str
    lines: tuple[str, ...]
    relocation: str | None = None

    @property
    def is_padding(self) -> bool:
        """True when the instruction is a nop that aligns the code."""
        return self.mnemonic.startswith("nop") or (self.mnemonic == "xchg" and self.operands == "ax,ax")

    @property
    def is_locked(self) -> bool:
        """True when the instruction is a locked read-modify-write: a lock prefix, or xchg with a memory operand."""
        return "lock" in self.prefixes or (self.mnemonic == "xchg" and "[" in self.operands)

    @property
    def is_jump(self) -> bool:
        """True when the instruction is a jump, conditional or not."""
        return self.mnemonic.startswith("j")

    @property
    def signature(self) -> str:
        """The mnemonic with its lock prefix or rep prefix, for the compare of two loops."""
        return " ".join([*(prefix for prefix in self.prefixes if prefix in KEPT_PREFIXES), self.mnemonic])

    def target(self) -> int | None:
        """Return the offset that a jump in its own section goes to, or None."""
        if not self.is_jump or self.relocation is not None:
            return None
        word = self.operands.split(" ", 1)[0]
        return objdump.hex_number(word.removeprefix("0x"))

    def normalized(self, cold_section: str | None) -> str:
        """Return the instruction as text that does not change with the name of its function.

        A reference to the cold part of the function reads `<cold>`, and a local constant has no number.
        """
        symbol = ""
        if self.relocation is not None:
            symbol = self.relocation
            if symbol == cold_section or symbol.endswith(COLD_SUFFIX):
                symbol = "<cold>"
            elif symbol.startswith("."):
                symbol = without_local_number(symbol)
            symbol = f" -> {symbol}"
        kept = [prefix for prefix in self.prefixes if prefix in KEPT_PREFIXES]
        return " ".join([*kept, self.mnemonic, self.operands]).strip() + symbol


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

    def cold_section(self) -> str | None:
        """Return the section of the cold part, or None."""
        return None if self.cold is None else self.cold.section

    def is_own(self, symbol: str) -> bool:
        """True when a relocation symbol names a part of this function."""
        sections = {part.section for part in (self.hot, self.cold) if part is not None}
        return symbol in sections or symbol.endswith(COLD_SUFFIX)

    def calls(self, part: Part | None) -> list[str]:
        """Return the callee of each call of a part, and of each jump to a different function.

        A call with no relocation stays in this object, and it reads `<local>`.
        """
        if part is None:
            return []
        callees: list[str] = []
        for instruction in part.instructions:
            if instruction.mnemonic == "call":
                callees.append(instruction.relocation or "<local>")
            elif (
                instruction.mnemonic == "jmp"
                and instruction.relocation is not None
                and not self.is_own(instruction.relocation)
            ):
                callees.append(instruction.relocation)
        return callees

    def jumps_to_cold(self) -> bool:
        """True when the hot part has a jump into the cold part."""
        if self.hot is None or self.cold is None:
            return False
        cold = self.cold.section
        return any(
            instruction.is_jump
            and instruction.relocation is not None
            and (instruction.relocation == cold or instruction.relocation.endswith(COLD_SUFFIX))
            for instruction in self.hot.instructions
        )

    def is_locked(self) -> bool:
        """True when the hot part holds a locked instruction."""
        return self.hot is not None and any(instruction.is_locked for instruction in self.hot.instructions)

    def normalized(self) -> list[str]:
        """Return the hot part as text that does not change with the name of the function."""
        if self.hot is None:
            return []
        return [instruction.normalized(self.cold_section()) for instruction in self.hot.counted()]

    def loops(self) -> list[tuple[str, ...]]:
        """Return the signatures of each inner loop of the hot part, sorted.

        A loop is the instructions from the target of a conditional jump back to that jump.  Complexity: O(n^2) in
        the instructions of the hot part.
        """
        if self.hot is None:
            return []
        found: list[tuple[str, ...]] = []
        for instruction in self.hot.instructions:
            target = instruction.target()
            if instruction.mnemonic == "jmp" or target is None or target > instruction.offset:
                continue
            found.append(
                tuple(inner.signature for inner in self.hot.counted() if target <= inner.offset <= instruction.offset)
            )
        return sorted(found)

    def text(self) -> str:
        """Return each part of the function as objdump prints it."""
        parts = [part.text() for part in (self.hot, self.cold) if part is not None]
        return "\n".join(parts) if parts else "  (no code)"


def function_name(symbol: str) -> tuple[str, bool]:
    """Return the name of a probe function from its demangled symbol, and True when the symbol is a cold part."""
    is_cold = symbol.endswith(COLD_SUFFIX)
    name = strip_arguments(symbol.removesuffix(COLD_SUFFIX) if is_cold else symbol)
    return name.removeprefix(ANONYMOUS), is_cold


def codes_of(functions: Sequence[objdump.Function]) -> dict[str, Code]:
    """Return the code of each function of a disassembly, by its name with no unnamed namespace and no parameters.

    Complexity: linear in the instructions of the disassembly.
    """
    parts: dict[str, dict[bool, Part]] = {}
    for function in functions:
        name, is_cold = function_name(function.name)
        instructions = tuple(
            Instruction(
                item.offset,
                item.prefixes,
                item.mnemonic,
                item.operands,
                item.lines,
                None if item.relocation is None else relocation_target(item.relocation.name),
            )
            for item in function.instructions
        )
        parts.setdefault(name, {})[is_cold] = Part(function.section, function.name, instructions)
    return {name: Code(name, held.get(False), held.get(True)) for name, held in parts.items()}


def probe_problems(probe: Probe, family: Code, raw: Code) -> list[str]:
    """Return each way in which the code of one probe does not meet its expectation (THE SHAPE)."""
    if family.hot is None or raw.hot is None:
        return [f"the object holds no hot part of {family.name if family.hot is None else raw.name}"]
    problems: list[str] = []
    count = len(family.hot.counted())
    raw_count = len(raw.hot.counted())
    if count > probe.bound:
        problems.append(f"the hot part of {family.name} has {count} instructions, over the bound of {probe.bound}")
    if probe.relation == "same":
        if family.normalized() != raw.normalized():
            problems.append(f"the hot part of {family.name} is not the instruction sequence of {raw.name}")
    elif count > raw_count + probe.extra:
        problems.append(
            f"the hot part of {family.name} has {count} instructions, and {raw.name} has {raw_count}.  "
            f"The expectation permits {probe.extra} more"
        )
    if probe.relation == "loop" and family.loops() != raw.loops():
        problems.append(
            f"the inner loops of {family.name} are {family.loops()}, and those of {raw.name} are {raw.loops()}"
        )
    for code, expected in ((family, probe.calls), (raw, probe.raw_calls)):
        callees = code.calls(code.hot)
        if sorted(callees) != sorted(expected):
            problems.append(
                f"the hot part of {code.name} calls {callees or 'no function'}, and the expectation is "
                f"{list(expected) or 'no call'}"
            )
    for code in (family, raw):
        if code.is_locked() != probe.locked:
            state = "holds a locked instruction" if code.is_locked() else "holds no locked instruction"
            problems.append(f"the hot part of {code.name} {state}, and the expectation is the opposite")
    problems += cold_problems(probe, family)
    return problems


def cold_problems(probe: Probe, family: Code) -> list[str]:
    """Return each way in which the cold part of a family function does not meet the cold call of its probe."""
    if not probe.cold:
        if family.cold is None:
            return []
        unwanted = (
            f"{family.name} has a cold part, and the expectation is none.  A check that can fail is then on the "
            f"path of the operation"
        )
        return [unwanted]
    if family.cold is None:
        return [
            f"{family.name} has no cold part.  The call of the fatal exit is then in the hot part, or it is missing"
        ]
    problems: list[str] = []
    if not family.jumps_to_cold():
        problems.append(f"the hot part of {family.name} has no jump into its cold part")
    if not any(callee.startswith(FATAL_PREFIX) for callee in family.calls(family.cold)):
        problems.append(f"the cold part of {family.name} does not call the fatal exit {FATAL_PREFIX}*")
    return problems


def shape_findings(
    functions: Mapping[str, Code], probes: Iterable[Probe]
) -> tuple[list[check_report.Finding], list[str]]:
    """Judge each probe against the disassembly of the probe object.

    Returns:
        The findings, and the disassembly of each probe with a finding
    """
    found: list[check_report.Finding] = []
    shown: list[str] = []
    for probe in probes:
        family = functions.get(probe.name, Code(probe.name))
        raw = functions.get(probe.name + RAW_SUFFIX, Code(probe.name + RAW_SUFFIX))
        problems = probe_problems(probe, family, raw)
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
            for problem in problems
        ]
        if problems:
            shown.append(
                f"probe {probe.name}, the family function:\n{family.text()}\n"
                f"probe {probe.name}, the raw function:\n{raw.text()}"
            )
    return found, shown


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
    """Compare the code words of the probe unit with those of the foundation units (THE FLAGS OF THE PROBES)."""
    messages: list[str] = []
    fixed = sorted({flag for flag in probe_flags if flag.startswith(PER_PRESET)})
    if fixed != sorted(FIXED_FLAGS):
        messages.append(
            f"the probe unit has the flags {fixed} of PER_PRESET, and the expectations of PROBES apply to "
            f"{' '.join(FIXED_FLAGS)}"
        )
    probe_set = {flag for flag in probe_flags if not flag.startswith(PER_PRESET)}
    base_sets = [{flag for flag in flags if not flag.startswith(PER_PRESET)} for flags in base_flags]
    if not base_sets:
        messages.append("the compile database has no unit of the target foundation.  The rule cannot compare the flags")
    for other in base_sets[1:]:
        if other != base_sets[0]:
            messages.append(f"the foundation units do not have the same code flags: {sorted(other ^ base_sets[0])}")
    base_set = base_sets[0] if base_sets else probe_set
    messages += [
        f"each foundation unit compiles with {flag}, and the probe unit does not.  When each build takes "
        f"{flag}, give it to {PROBE_CMAKE} through a list of cmake/CodeFlags.cmake.  When its value changes "
        f"with the preset, add it to PER_PRESET and to FIXED_FLAGS"
        for flag in sorted(base_set - probe_set)
    ]
    messages += [
        f"the probe unit compiles with {flag}, and no foundation unit does.  Remove it from {PROBE_CMAKE}, or "
        f"add it to FIXED_FLAGS when the tree gives it a value that changes with the preset"
        for flag in sorted(probe_set - base_set)
    ]
    return [check_report.Finding("error", PROBE_CMAKE, 0, RULE, message, "flags") for message in messages]


# ── The core part ──────────────────────────────────────────────────


def compiled_object(entry: compdb.CompileEntry, obj: Path) -> None:
    """Compile the unit of a row of the compile database into a different object.

    The quarantine plugin writes only the section of the object, because the command gives it no report directory.

    Raises:
        CompileFailed: If the compile fails
    """
    words = list(entry.argv())
    if "-o" in words:
        place = words.index("-o")
        words[place + 1] = str(obj)
    result = proc.run(proc.Command(words, timeout_s=proc.TOOL_S, cwd=Path(entry.directory)))
    if result.exit != 0:
        raise CompileFailed(f"the compile of {entry.file} fails:\n{result.err[-2000:]}")


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
    """Return the probes of the plant.txt of a plant, or None for a build (THE PROBES).

    Raises:
        runner.BuildError: If a probe row does not have its shape
    """
    if context.planted is None:
        return None
    try:
        return tuple(probe_of_row(row) for row in context.planted.rows if row.words[0] == "probe")
    except rows_grammar.RowsError as problem:
        raise runner.BuildError(str(problem)) from None


def core_findings(context: runner.ElfContext) -> tuple[list[check_report.Finding], list[str]]:
    """Return the findings of the core part, and the disassembly of each probe with a finding.

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
    found = flag_findings(
        code_flags(probe.argv(), probe.file),
        [code_flags(entry.argv(), entry.file) for entry in foundation_entries(context)],
    )
    probes = planted_probes(context) or PROBES
    try:
        with tempfile.TemporaryDirectory(prefix="codegen-shapes-") as scratch:
            obj = Path(scratch) / "core_shapes.o"
            compiled_object(probe, obj)
            functions = codes_of(disassembled(obj))
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
    shapes, shown = shape_findings(functions, probes)
    return [*found, *shapes], shown


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
    """Return the findings of each part of the build, and the disassembly of each probe with a finding (THE PARTS).

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
    """Run the rule on the build, write its findings, and print the disassembly of each probe with a finding.

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
            f"{RULE}: {len(PROBES)} probes hold, the flags of the probe unit agree with the tree, and "
            f"{len(wrapper_source(CTCRYPTO_CASES)[1])} wrappers have no refused instruction for "
            f"{len(MARCHES) * 4} targets"
        )
    return status
