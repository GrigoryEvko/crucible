#!/usr/bin/env python3
"""check-core-shapes: each hot operation of the core families compiles to the instructions of its raw form.

THE PROBES
    test/shape/core_shapes.cpp holds two functions for each probe of PROBES.
    The function NAME does one hot operation of a family of
    include/foundation/core.  The function NAME_raw does the same work in the
    raw form that the family replaces: a std::atomic operation, a raw pointer
    loop, memcpy or memset, a std::unique_ptr or std::optional access, or
    std::unreachable.  test/shape/CMakeLists.txt compiles the unit into the
    object library core_shape_probes, with the same flags in each preset:
    -O3, the code generation flags that each build of the tree takes, and
    -march=x86-64-v3.

    The check compiles the unit again into a scratch directory, with the
    command of the object in the compile database, and it reads that object.
    So the check needs a configured build directory, and no built object.  A
    run after a change of a header of the base reads the code of the change,
    also in a guard run that does not build the object.

THE FLAGS
    The check reads the compile database of the build directory.  It takes
    the code generation flags of the probe object and of each unit of the
    foundation library.  From the probe flags it removes the fixed flags
    (--fixed-flags), and from the foundation flags it removes each flag whose
    value changes with the preset (PER_PRESET).  The two sets must be equal.
    So a flag that the tree adds to each build, or removes from each build,
    fails the check until test/shape/CMakeLists.txt follows it.  A flag that
    the tree adds to one preset fails the check in that preset until
    PER_PRESET names it.

THE SHAPE
    The check disassembles the probe object with objdump.  A function has a
    hot part, and it can have a cold part, `NAME [clone .cold]` in a section
    .text.unlikely.  A count of instructions leaves out the alignment nops.
    For each probe, the check requires:
      - The bound: the hot part of NAME has at most `bound` instructions.
      - The relation to NAME_raw:
          same   the two hot parts have the same instructions, in order
          loop   each inner loop (the instructions from the target of a
                 conditional jump back to that jump) is the same in the two
                 hot parts, and the hot part of NAME has at most `extra`
                 instructions more than the hot part of NAME_raw
          cost   the hot part of NAME has at most `extra` instructions more
                 than the hot part of NAME_raw
      - The calls: the hot part of NAME calls exactly the functions of
        `calls`, and the hot part of NAME_raw exactly those of `raw_calls`.
        A jump to another function is a call.
      - The lock: each hot part holds a locked instruction (a lock prefix,
        or xchg with a memory operand) when `locked` is true, and no hot part
        holds one when it is false.
      - The cold call: with `cold`, NAME has a cold part, its hot part jumps
        into the cold part, and the cold part calls the fatal exit of
        include/foundation/core/Report.h.  Without `cold`, NAME has no cold
        part.
    A probe that fails gives its findings, and the check prints the
    disassembly of the two functions of the probe.

THE SELF-TEST
    --self-test runs the check on planted disassembly, one plant for each
    kind of finding, and on planted compile commands.  Then it compiles the
    probe unit again with the command of the compile database and a copy of
    include/foundation/core/Region.h that has a second bounds test in
    View::window.  The probes that use the window must fail, and each other
    probe must pass.  The unit with no plant must pass.

Exit 0: each probe holds.  Exit 1: a finding.  Exit 2: the check could not
run (no objdump, no compile database, no row of the probe object, a compile
that fails, no function of a probe, no plant site), which is a failure.  Exit 3: the machine is not x86_64, and the
check has no expectations for it, so ctest reports a skip.
"""

from __future__ import annotations

import argparse
import json
import os
import platform
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Iterable
from dataclasses import dataclass, field
from pathlib import Path
from typing import Literal

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402
import tsast  # noqa: E402
from repo_root import REPO_ROOT  # noqa: E402

CHECK = "core-shapes"
PROBE_SOURCE = "test/shape/core_shapes.cpp"
PROBE_CMAKE = "test/shape/CMakeLists.txt"
PLANT_HEADER = Path("include/foundation/core/Region.h")
EXPECTED_MACHINE = "x86_64"
# The fixed flags that the expectations of PROBES assume.
EXPECTED_FIXED = ("-O3", "-march=x86-64-v3")
# The fatal exit of include/foundation/core/Report.h: each cold function of
# the detail namespace whose name starts with report_.
FATAL_PREFIX = "foundation::core::detail::report_"
RAW_SUFFIX = "_raw"
COLD_SUFFIX = " [clone .cold]"
ANONYMOUS = "(anonymous namespace)::"
# The prefixes that objdump prints before a mnemonic.  A segment prefix and
# data16 come only with a multi-byte nop in the code of this unit.
PREFIXES = frozenset({"lock", "rep", "repz", "repe", "repnz", "repne", "notrack", "bnd", "data16", "cs", "ds", "es",
                      "ss", "fs", "gs"})
# The prefixes that a compare of two instructions keeps.
KEPT_PREFIXES = frozenset({"lock", "rep", "repz", "repe", "repnz", "repne"})

# The words of a compile command that do not change the code.  An option of
# SEPARATE_VALUE takes the next word as its value.
SEPARATE_VALUE = frozenset({"-o", "-c", "-MF", "-MT", "-MQ", "-I", "-isystem", "-iquote", "-include", "-x"})
NOT_CODE_WORDS = frozenset({"-MD", "-MMD"})
NOT_CODE_PREFIXES = ("-W", "-pedantic", "-fdiagnostics-", "-fno-diagnostics-", "-ftemplate-backtrace-limit=",
                     "-fconstexpr-ops-limit=", "-fplugin=", "-fplugin-arg-", "-fdebug-prefix-map=",
                     "-ffile-prefix-map=", "-fmacro-prefix-map=", "-fmessage-length=", "-g", "-I", "-isystem",
                     "-iquote")
# The flags whose value changes with the preset or with the target in the
# rest of the tree: the level, the target, the sanitizers, the hardening,
# the contract semantic, the profile, the analyzer, the overflow rule of the
# UBSan-strict preset, the position-independent code of a library, and the
# macros of a build type.
PER_PRESET = ("-O", "-march=", "-mtune=", "-mcpu=", "-mno-outline-atomics", "-fsanitize", "-fno-sanitize",
              "-fharden-", "-fno-harden-", "-fcontract-evaluation-semantic=", "-fprofile-", "-fno-profile-",
              "-fbranch-probabilities", "-fauto-profile", "-fanalyzer", "-fstrict-overflow", "-fno-strict-overflow",
              "-fPIC", "-fPIE", "-fpic", "-fpie", "-fno-pic", "-fno-pie", "-D_GLIBCXX_ASSERTIONS",
              "-D_GLIBCXX_SANITIZE_VECTOR", "-DNDEBUG", "-UNDEBUG", "-DCRUCIBLE_PGO",
              "-DCRUCIBLE_CONTRACT_SEMANTIC_IGNORE")

Relation = Literal["same", "loop", "cost"]


class CannotRun(RuntimeError):
    """The check cannot run: an input is missing, or a tool failed."""


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


# The expectations.  A bound is the count of instructions that the hot part
# has on 2026-10-02, with the patched GCC 16.2.1.  A change that makes a hot
# part longer fails, so a family change that adds a check to a hot path stops
# here.  Each count includes the endbr64 of -fcf-protection and the ret.
PROBES: tuple[Probe, ...] = (
    Probe("atomic_load", "same", 3, "load_acquire is the acquire load of std::atomic"),
    Probe("atomic_store", "same", 3, "store_release is the release store of std::atomic, with no lock"),
    Probe("atomic_exchange", "same", 4, "exchange_acq_rel is the xchg of std::atomic", locked=True),
    Probe("atomic_cas", "same", 5, "cas_acq_rel is the lock cmpxchg of std::atomic", locked=True),
    Probe("atomic_cas_pair", "cost", 5,
          "cas_acq_rel of a value of two words keeps the value in a register.  std::atomic stores the expected "
          "value on the stack", locked=True),
    Probe("atomic_fetch_add", "same", 4, "fetch_add_acq_rel is the lock xadd of std::atomic", locked=True),
    Probe("atomic_fetch_sub", "same", 5, "fetch_sub_acq_rel is the neg and lock xadd of std::atomic", locked=True),
    Probe("atomic_fetch_or", "same", 9, "fetch_or_acq_rel is the lock cmpxchg loop of std::atomic", locked=True),
    Probe("atomic_fetch_and", "same", 9, "fetch_and_acq_rel is the lock cmpxchg loop of std::atomic", locked=True),
    Probe("atomic_fetch_max", "cost", 7,
          "fetch_max_acq_rel is a lock cmpxchg loop that does not write when the value stays the same", locked=True),
    Probe("atomic_fetch_min", "cost", 7,
          "fetch_min_acq_rel is a lock cmpxchg loop that does not write when the value stays the same", locked=True),
    Probe("tally_add", "same", 3, "Tally::add is the lock add of a relaxed std::atomic counter", locked=True),
    Probe("tally_read", "same", 3, "Tally::read is the load of a relaxed std::atomic counter"),
    Probe("view_sum", "loop", 77,
          "a range-for over a View is the vectorized loop of a raw pointer loop, because the optimizer removes the "
          "checks of the cursor"),
    Probe("view_window", "same", 13, "window gives its Option with no test of the niche of the Option"),
    Probe("view_append", "cost", 18,
          "the append path is the bounds compare of the window and one memmove.  The copy into the window does no "
          "test of the lengths.  The family code has one register move more", extra=1, calls=("memmove",),
          raw_calls=("memcpy",)),
    Probe("view_fill", "cost", 7,
          "fill of bytes is one memset.  The family code first tests for an empty View and returns, because a View "
          "can be empty with no buffer, and memset of a null pointer is undefined", extra=3, calls=("memset",),
          raw_calls=("memset",)),
    Probe("box_get", "cost", 6,
          "Box::get adds one test and one branch to a cold call of the fatal exit", extra=2, cold=True),
    Probe("option_expect", "cost", 7,
          "Option::expect adds one test and one branch to a cold call of the fatal exit.  A payload that fills the "
          "low word of the register puts the flag at bit 32, so the code also uses a move and a shift to read the "
          "flag", extra=4, cold=True),
    Probe("option_expect_niche", "cost", 6,
          "Option::expect of an Option with a niche adds one compare and one branch to a cold call of the fatal "
          "exit", extra=2, cold=True),
    Probe("switch_unreachable", "cost", 7,
          "a switch with fixy::unreachable() has one bounds compare on the hot path, and the call of the fatal "
          "exit is in the cold part", extra=2, cold=True),
)


# ── The disassembly ─────────────────────────────────────────────────────────

SECTION_LINE = re.compile(r"^Disassembly of section (\S+):$")
HEAD_LINE = re.compile(r"^[0-9a-f]+ <(.+)>:$")
RELOCATION_LINE = re.compile(r"^\s+[0-9a-f]+: (R_[A-Z0-9_]+)\s+(.+)$")
INSTRUCTION_LINE = re.compile(r"^\s*([0-9a-f]+):\s+(.*)$")
ADDEND = re.compile(r"[-+]0x[0-9a-f]+$")
LOCAL_NUMBER = re.compile(r"(\.LC|\.)\d+$")


def strip_arguments(symbol: str) -> str:
    """Remove the parameter list at the end of a demangled function name.

    Args:
        symbol: A demangled name, such as `ns::f(int, ns::T<(anonymous namespace)::U>)`

    Returns:
        The name without its last parenthesized group, or the name unchanged when it does not end with `)`
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


def relocation_target(text: str) -> str:
    """Return the symbol that a relocation names, without its addend and its parameter list.

    Args:
        text: The text after the relocation type, such as `memmove-0x4`

    Returns:
        The symbol, such as `memmove` or `foundation::core::detail::report_fatal_`
    """
    symbol = ADDEND.sub("", text.strip())
    if symbol.endswith(COLD_SUFFIX):
        return strip_arguments(symbol[: -len(COLD_SUFFIX)]) + COLD_SUFFIX
    return strip_arguments(symbol)


@dataclass(slots=True)
class Instruction:
    """One instruction of a disassembled function, with the symbol of its relocation."""

    offset: int
    prefixes: tuple[str, ...]
    mnemonic: str
    operands: str
    lines: list[str]
    relocation: str | None = None

    @property
    def is_padding(self) -> bool:
        """Whether the instruction is an alignment nop."""
        return self.mnemonic.startswith("nop") or (self.mnemonic == "xchg" and self.operands == "ax,ax")

    @property
    def is_locked(self) -> bool:
        """Whether the instruction is a locked read-modify-write: a lock prefix, or xchg with a memory operand."""
        return "lock" in self.prefixes or (self.mnemonic == "xchg" and "[" in self.operands)

    @property
    def is_jump(self) -> bool:
        """Whether the instruction is a jump, conditional or not."""
        return self.mnemonic.startswith("j")

    @property
    def signature(self) -> str:
        """The mnemonic with its lock or repeat prefix, for the compare of two loops."""
        kept = [prefix for prefix in self.prefixes if prefix in KEPT_PREFIXES]
        return " ".join([*kept, self.mnemonic])

    def target(self) -> int | None:
        """Return the offset that a jump in its own section goes to, or None."""
        if not self.is_jump or self.relocation is not None:
            return None
        word = self.operands.split(" ", 1)[0]
        try:
            return int(word, 16)
        except ValueError:
            return None

    def normalized(self, cold_section: str | None) -> str:
        """Return the instruction as text that does not depend on the name of its function.

        Args:
            cold_section: The section of the cold part of the function, or None

        Returns:
            The prefixes, the mnemonic, the operands and the relocation symbol.  A reference to the own cold part
            reads `<cold>`, and a local constant loses its number
        """
        symbol = ""
        if self.relocation is not None:
            symbol = self.relocation
            if symbol == cold_section or symbol.endswith(COLD_SUFFIX):
                symbol = "<cold>"
            elif symbol.startswith("."):
                symbol = LOCAL_NUMBER.sub(r"\1", symbol)
            symbol = f" -> {symbol}"
        kept = [prefix for prefix in self.prefixes if prefix in KEPT_PREFIXES]
        return " ".join([*kept, self.mnemonic, self.operands]).strip() + symbol


@dataclass(slots=True)
class Part:
    """The hot part or the cold part of one function: its section, its symbol and its instructions."""

    section: str
    symbol: str
    instructions: list[Instruction] = field(default_factory=list)

    def counted(self) -> list[Instruction]:
        """Return the instructions without the alignment nops."""
        return [instruction for instruction in self.instructions if not instruction.is_padding]

    def text(self) -> str:
        """Return the part as objdump prints it."""
        lines = [f"  <{self.symbol}> in {self.section}:"]
        for instruction in self.instructions:
            lines.extend(f"    {line.strip()}" for line in instruction.lines)
        return "\n".join(lines)


@dataclass(slots=True)
class Code:
    """The code of one function of the probe unit."""

    name: str
    hot: Part | None = None
    cold: Part | None = None

    def cold_section(self) -> str | None:
        """Return the section of the cold part, or None."""
        return None if self.cold is None else self.cold.section

    def is_own(self, symbol: str) -> bool:
        """Whether a relocation symbol names a part of this function."""
        sections = {part.section for part in (self.hot, self.cold) if part is not None}
        return symbol in sections or symbol.endswith(COLD_SUFFIX)

    def calls(self, part: Part | None) -> list[str]:
        """Return the callee of each call of a part, and of each jump to another function.

        A call with no relocation stays in this object, and it reads `<local>`.
        """
        if part is None:
            return []
        callees: list[str] = []
        for instruction in part.instructions:
            if instruction.mnemonic == "call":
                callees.append(instruction.relocation or "<local>")
            elif instruction.mnemonic == "jmp" and instruction.relocation is not None \
                    and not self.is_own(instruction.relocation):
                callees.append(instruction.relocation)
        return callees

    def jumps_to_cold(self) -> bool:
        """Whether the hot part has a jump into the cold part."""
        if self.hot is None or self.cold is None:
            return False
        return any(instruction.is_jump and instruction.relocation is not None
                   and (instruction.relocation == self.cold.section or instruction.relocation.endswith(COLD_SUFFIX))
                   for instruction in self.hot.instructions)

    def is_locked(self) -> bool:
        """Whether the hot part holds a locked instruction."""
        return self.hot is not None and any(instruction.is_locked for instruction in self.hot.instructions)

    def normalized(self) -> list[str]:
        """Return the hot part as text that does not depend on the name of the function."""
        if self.hot is None:
            return []
        return [instruction.normalized(self.cold_section()) for instruction in self.hot.counted()]

    def loops(self) -> list[tuple[str, ...]]:
        """Return the signatures of each inner loop of the hot part, in a sorted list.

        A loop is the instructions from the target of a conditional jump back
        to that jump.  O(n^2) in the count of instructions of the hot part.
        """
        if self.hot is None:
            return []
        found: list[tuple[str, ...]] = []
        for instruction in self.hot.instructions:
            target = instruction.target()
            if instruction.mnemonic == "jmp" or target is None or target > instruction.offset:
                continue
            found.append(tuple(inner.signature for inner in self.hot.counted()
                               if target <= inner.offset <= instruction.offset))
        return sorted(found)

    def text(self) -> str:
        """Return each part of the function as objdump prints it."""
        parts = [part.text() for part in (self.hot, self.cold) if part is not None]
        return "\n".join(parts) if parts else "  (no code)"


def function_name(symbol: str) -> tuple[str, bool]:
    """Return the name of a probe function from its demangled symbol, and whether the symbol is a cold part."""
    is_cold = symbol.endswith(COLD_SUFFIX)
    if is_cold:
        symbol = symbol[: -len(COLD_SUFFIX)]
    name = strip_arguments(symbol)
    if name.startswith(ANONYMOUS):
        name = name[len(ANONYMOUS):]
    return name, is_cold


def parse_disassembly(text: str) -> dict[str, Code]:
    """Read the output of `objdump -drC --no-show-raw-insn -M intel`.

    O(n) in the lines of the output.

    Args:
        text: The output

    Returns:
        The code of each function, by its name without the unnamed namespace and without its parameters
    """
    functions: dict[str, Code] = {}
    section = ""
    part: Part | None = None
    for line in text.splitlines():
        match = SECTION_LINE.match(line)
        if match:
            section = match.group(1)
            part = None
            continue
        match = HEAD_LINE.match(line)
        if match:
            name, is_cold = function_name(match.group(1))
            code = functions.setdefault(name, Code(name))
            part = Part(section, match.group(1))
            if is_cold:
                code.cold = part
            else:
                code.hot = part
            continue
        if part is None:
            continue
        match = RELOCATION_LINE.match(line)
        if match:
            if part.instructions:
                part.instructions[-1].relocation = relocation_target(match.group(2))
                part.instructions[-1].lines.append(line)
            continue
        match = INSTRUCTION_LINE.match(line)
        if not match:
            continue
        words = match.group(2).split("#", 1)[0].split()
        prefixes: list[str] = []
        while words and words[0] in PREFIXES:
            prefixes.append(words.pop(0))
        if not words:
            words = [prefixes.pop()] if prefixes else ["?"]
        operands = " ".join(words[1:]).split(" <", 1)[0]
        part.instructions.append(Instruction(int(match.group(1), 16), tuple(prefixes), words[0], operands, [line]))
    return functions


def disassemble(obj: Path) -> dict[str, Code]:
    """Disassemble one object.

    Raises:
        CannotRun: If objdump fails
    """
    result = subprocess.run(["objdump", "-drC", "--no-show-raw-insn", "-M", "intel", str(obj)], capture_output=True,
                            text=True)
    if result.returncode != 0:
        raise CannotRun(f"objdump failed on {obj}: {result.stderr.strip()}")
    return parse_disassembly(result.stdout)


# ── The judgment of one probe ───────────────────────────────────────────────

def judge(probe: Probe, family: Code, raw: Code) -> list[str]:
    """Return each way in which the code of one probe does not meet its expectation.

    Args:
        probe: The expectation
        family: The code of the family function
        raw: The code of the raw function

    Returns:
        One sentence for each finding, with no finding when the probe holds
    """
    if family.hot is None or raw.hot is None:
        missing = family.name if family.hot is None else raw.name
        return [f"the object holds no hot part of {missing}"]
    problems: list[str] = []
    count = len(family.hot.counted())
    raw_count = len(raw.hot.counted())
    if count > probe.bound:
        problems.append(f"the hot part of {family.name} has {count} instructions, over the bound of {probe.bound}")
    if probe.relation == "same":
        if family.normalized() != raw.normalized():
            problems.append(f"the hot part of {family.name} is not the instruction sequence of {raw.name}")
    elif count > raw_count + probe.extra:
        problems.append(f"the hot part of {family.name} has {count} instructions, and {raw.name} has {raw_count}.  "
                        f"The expectation permits {probe.extra} more")
    if probe.relation == "loop" and family.loops() != raw.loops():
        problems.append(f"the inner loops of {family.name} are {family.loops()}, and those of {raw.name} are "
                        f"{raw.loops()}")
    for code, expected in ((family, probe.calls), (raw, probe.raw_calls)):
        callees = code.calls(code.hot)
        if sorted(callees) != sorted(expected):
            problems.append(f"the hot part of {code.name} calls {callees or 'no function'}, and the expectation is "
                            f"{list(expected) or 'no call'}")
    for code in (family, raw):
        if code.is_locked() != probe.locked:
            state = "holds a locked instruction" if code.is_locked() else "holds no locked instruction"
            problems.append(f"the hot part of {code.name} {state}, and the expectation is the opposite")
    if probe.cold:
        if family.cold is None:
            problems.append(f"{family.name} has no cold part, so the call of the fatal exit is in the hot part or "
                            f"missing")
        else:
            if not family.jumps_to_cold():
                problems.append(f"the hot part of {family.name} has no jump into its cold part")
            if not any(callee.startswith(FATAL_PREFIX) for callee in family.calls(family.cold)):
                problems.append(f"the cold part of {family.name} does not call the fatal exit {FATAL_PREFIX}*")
    elif family.cold is not None:
        problems.append(f"{family.name} has a cold part, and the expectation is none.  A check that can fail is now on "
                        f"the path of the operation")
    return problems


def shape_findings(functions: dict[str, Code], probes: Iterable[Probe] = PROBES) -> tuple[list[check_report.Finding],
                                                                                          list[str]]:
    """Judge each probe against the disassembly of the probe object.

    Args:
        functions: The code of each function of the object
        probes: The expectations

    Returns:
        The findings, and the disassembly of each probe with a finding
    """
    findings: list[check_report.Finding] = []
    shown: list[str] = []
    for probe in probes:
        family = functions.get(probe.name, Code(probe.name))
        raw = functions.get(probe.name + RAW_SUFFIX, Code(probe.name + RAW_SUFFIX))
        problems = judge(probe, family, raw)
        if not problems:
            continue
        for problem in problems:
            findings.append(check_report.Finding("error", PROBE_SOURCE, 0, CHECK,
                                                 f"probe {probe.name}: {problem}.  The claim: {probe.claim}"))
        shown.append(f"probe {probe.name}, the family function:\n{family.text()}\n"
                     f"probe {probe.name}, the raw function:\n{raw.text()}")
    return findings, shown


# ── The flags ───────────────────────────────────────────────────────────────

def code_flags(words: list[str], source: str) -> list[str]:
    """Return the words of a compile command that change the code.

    Args:
        words: The command, the compiler first
        source: The source file of the command, which is not a flag

    Returns:
        The flags, in order
    """
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


def is_per_preset(flag: str) -> bool:
    """Whether a flag has a value that changes with the preset or the target in the rest of the tree."""
    return flag.startswith(PER_PRESET)


@dataclass(frozen=True, slots=True)
class Entry:
    """One row of the compile database: the words of its command, its directory, its source and its output."""

    words: tuple[str, ...]
    directory: Path
    source: str
    output: Path


def read_database(build_dir: Path, objects: Iterable[Path]) -> dict[Path, Entry]:
    """Read the rows of the compile database of a build directory that write the given objects.

    The check splits only the command of a row that writes one of the
    objects, because a split of each command of the tree costs seconds.
    O(rows) for the search.

    Args:
        build_dir: The build directory
        objects: The objects, as absolute paths

    Returns:
        The row of each object, by its absolute path

    Raises:
        CannotRun: If the database cannot be read, or no row writes one of the objects
    """
    path = build_dir / "compile_commands.json"
    try:
        rows = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError) as error:
        raise CannotRun(f"the compile database {path} cannot be read: {error}") from None
    wanted = {Path(os.path.normpath(obj)) for obj in objects}
    entries: dict[Path, Entry] = {}
    for row in rows:
        directory = row.get("directory", str(build_dir))
        output = row.get("output")
        if output is None:
            continue
        written = Path(os.path.normpath(os.path.join(directory, output)))
        if written not in wanted:
            continue
        words = row.get("arguments") or shlex.split(row.get("command", ""))
        entries[written] = Entry(tuple(words), Path(directory), row.get("file", ""), written)
    missing = sorted(str(obj) for obj in wanted - entries.keys())
    if missing:
        raise CannotRun(f"no row of the compile database {path} writes {missing}")
    return entries


def flag_findings(probe_flags: list[str], base_flags: list[list[str]], fixed: list[str]) -> list[check_report.Finding]:
    """Compare the code generation flags of the probe object with those of the foundation units.

    Args:
        probe_flags: The code flags of the probe object
        base_flags: The code flags of each foundation unit
        fixed: The fixed flags of the probe object

    Returns:
        One finding for each flag that only one side has, and for each fixed flag that is missing
    """
    findings: list[check_report.Finding] = []

    def found(message: str) -> None:
        findings.append(check_report.Finding("error", PROBE_CMAKE, 0, CHECK, message))

    for flag in EXPECTED_FIXED:
        if flag not in fixed:
            found(f"the fixed flags {fixed} do not hold {flag}, and the expectations of {Path(__file__).name} are for "
                  f"{' '.join(EXPECTED_FIXED)}")
    for flag in fixed:
        if flag not in probe_flags:
            found(f"the probe object does not compile with the fixed flag {flag}")
    probe_set = {flag for flag in probe_flags if flag not in fixed}
    base_sets = [{flag for flag in flags if not is_per_preset(flag)} for flags in base_flags]
    if not base_sets:
        found("no foundation unit is in the compile database, so the check cannot compare the flags")
        return findings
    for other in base_sets[1:]:
        if other != base_sets[0]:
            found(f"the foundation units do not have the same code generation flags: "
                  f"{sorted(other ^ base_sets[0])}")
    base_set = base_sets[0]
    for flag in sorted(base_set - probe_set):
        found(f"each foundation unit compiles with {flag}, and the probe object does not.  When each build takes "
              f"{flag}, add it to the base flags of {PROBE_CMAKE}.  When its value changes with the preset, add it "
              f"to PER_PRESET of utils/scripts/{Path(__file__).name} and to the fixed flags")
    for flag in sorted(probe_set - base_set):
        found(f"the probe object compiles with {flag}, and no foundation unit does.  Remove it from {PROBE_CMAKE}, "
              f"or add it to the fixed flags when the tree gives it a value that changes with the preset")
    return findings


# ── The check ───────────────────────────────────────────────────────────────

@dataclass(frozen=True, slots=True)
class Inputs:
    """The inputs of the check, from the test command."""

    build_dir: Path
    probe_object: Path
    base_objects: tuple[Path, ...]
    fixed: tuple[str, ...]


def compile_probe(entry: Entry, obj: Path, include_dir: Path | None = None) -> None:
    """Compile the probe unit with its command from the compile database into another object.

    The command writes `obj`, and an include directory given here comes before
    each other one.  Each other word stays: the quarantine plugin writes only
    the section of the object, because the command gives it no report
    directory.  A compile with an include directory puts a copy of a header
    of the base outside the source root in front of the original.  The plugin
    refuses that include in fixy/Core.h, a file of the language, so that
    compile loads no plugin.

    Raises:
        CannotRun: If the compile fails
    """
    words = [entry.words[0], *([] if include_dir is None else [f"-I{include_dir}"])]
    index = 1
    while index < len(entry.words):
        word = entry.words[index]
        if word == "-o":
            words += ["-o", str(obj)]
            index += 2
            continue
        is_plugin_word = word.startswith(("-fplugin=", "-fplugin-arg-")) or word == "-DCRUCIBLE_QUARANTINE_ACTIVE"
        if include_dir is None or not is_plugin_word:
            words.append(word)
        index += 1
    result = subprocess.run(words, capture_output=True, text=True, cwd=entry.directory)
    if result.returncode != 0:
        raise CannotRun(f"the compile of {PROBE_SOURCE} failed:\n{result.stderr}")


def probe_functions(entry: Entry, include_dir: Path | None = None) -> dict[str, Code]:
    """Compile the probe unit into a scratch directory, and return the code of each function of the object.

    Raises:
        CannotRun: If the compile or objdump fails, or the object holds no function of a probe
    """
    with tempfile.TemporaryDirectory(prefix="core-shapes-") as scratch:
        obj = Path(scratch) / "core_shapes.o"
        compile_probe(entry, obj, include_dir)
        functions = disassemble(obj)
    missing = [name for probe in PROBES for name in (probe.name, probe.name + RAW_SUFFIX) if name not in functions]
    if missing:
        raise CannotRun(f"the probe object holds no function {missing}.  The compiler emitted another form of the "
                        f"function, or {PROBE_SOURCE} lost it")
    return functions


def run_check(inputs: Inputs) -> int:
    """Run the flag comparison and the shape check, print the findings, and return the exit status.

    Raises:
        CannotRun: If an input is missing, or the compile of the probe unit fails
    """
    entries = read_database(inputs.build_dir, (inputs.probe_object, *inputs.base_objects))
    probe_entry = entries[inputs.probe_object]
    base_entries = [entries[obj] for obj in inputs.base_objects]
    findings = flag_findings(code_flags(list(probe_entry.words), probe_entry.source),
                             [code_flags(list(entry.words), entry.source) for entry in base_entries],
                             list(inputs.fixed))
    shapes, shown = shape_findings(probe_functions(probe_entry))
    status = check_report.emit(findings + shapes, CHECK, None)
    for block in shown:
        print(block)
    if status == 0:
        print(f"{CHECK}: {len(PROBES)} probes hold, and the flags of the probe object follow the tree")
    return status


# ── The self-test ───────────────────────────────────────────────────────────

SYNTHETIC_HEAD = "\nDisassembly of section .text.{name}:\n\n0000000000000000 <(anonymous namespace)::{name}(int)>:\n"
SYNTHETIC_COLD = ("\nDisassembly of section .text.unlikely.{name}:\n\n"
                  "0000000000000000 <(anonymous namespace)::{name}(int) [clone .cold]>:\n")
FATAL_CALL = ("   0:\tcall   QWORD PTR [rip+0x0]        # 6 <x>\n"
              "\t\t\t2: R_X86_64_GOTPCRELX\tfoundation::core::detail::report_fatal_(char const*, char const*, "
              "unsigned int, char const*)-0x4\n")


def synthetic(name: str, body: str, cold: str | None = None) -> str:
    """Return the objdump text of one synthetic function: its hot part and its cold part."""
    text = SYNTHETIC_HEAD.format(name=name) + body
    if cold is not None:
        text += SYNTHETIC_COLD.format(name=name) + cold
    return text


def self_test_shapes() -> int:
    """Plant one disassembly for each kind of finding.  Return the count of wrong verdicts."""
    load = "   0:\tendbr64\n   4:\tmov    rax,QWORD PTR [rdi]\n   7:\tret\n"
    exchange = "   0:\tendbr64\n   4:\tmov    rax,rsi\n   7:\txchg   QWORD PTR [rdi],rax\n   a:\tret\n"
    checked = ("   0:\tendbr64\n   4:\ttest   rdi,rdi\n   7:\tje     d <x+0xd>\n"
               "\t\t\t9: R_X86_64_PC32\t.text.unlikely.probe-0x4\n   d:\tmov    eax,DWORD PTR [rdi]\n   f:\tret\n")
    loop = ("   0:\tendbr64\n   4:\tvpaddd ymm0,ymm0,YMMWORD PTR [rax]\n   8:\tadd    rax,0x20\n"
            "   c:\tcmp    rax,rdx\n   f:\tjne    4 <x+0x4>\n  11:\tret\n")
    other_loop = ("   0:\tendbr64\n   4:\tvpaddd ymm0,ymm0,YMMWORD PTR [rax]\n   8:\tadd    rax,0x20\n"
                  "   c:\tlea    rcx,[rax+0x1]\n   e:\tcmp    rax,rdx\n  11:\tjne    4 <x+0x4>\n  13:\tret\n")
    copy = ("   0:\tendbr64\n   4:\tjmp    QWORD PTR [rip+0x0]        # a <x>\n"
            "\t\t\t6: R_X86_64_GOTPCRELX\tmemmove-0x4\n")
    raw_copy = ("   0:\tendbr64\n   4:\tjmp    QWORD PTR [rip+0x0]        # a <x>\n"
                "\t\t\t6: R_X86_64_GOTPCRELX\tmemcpy-0x4\n")
    plain = "   0:\tendbr64\n   4:\tmov    eax,DWORD PTR [rdi]\n   6:\tret\n"
    padded = "   0:\tendbr64\n   4:\tmov    rax,QWORD PTR [rdi]\n   7:\txchg   ax,ax\n   9:\tret\n"
    cases: tuple[tuple[str, Probe, str, str, str | None, tuple[str, ...]], ...] = (
        ("a clean pair", Probe("probe", "same", 3, "c"), load, load, None, ()),
        ("an alignment nop", Probe("probe", "same", 3, "c"), padded, load, None, ()),
        ("an extra instruction", Probe("probe", "same", 3, "c"),
         load.replace("   7:\tret", "   7:\tmov    rcx,rax\n   a:\tret"), load, None,
         ("over the bound", "not the instruction sequence")),
        ("a call where none belongs", Probe("probe", "cost", 4, "c", extra=1),
         load.replace("   7:\tret", "   7:\tcall   QWORD PTR [rip+0x0]\n\t\t\t9: R_X86_64_GOTPCRELX\thelper-0x4\n"
                      "   d:\tret"), load, None, ("calls ['helper']",)),
        ("a missing memmove", Probe("probe", "same", 3, "c", calls=("memmove",), raw_calls=("memmove",)),
         load, load, None, ("expectation is ['memmove']",)),
        ("a memmove and a memcpy", Probe("probe", "cost", 2, "c", calls=("memmove",), raw_calls=("memcpy",)),
         copy, raw_copy, None, ()),
        ("a missing lock", Probe("probe", "same", 3, "c", locked=True), load, load, None,
         ("holds no locked instruction",)),
        ("a lock where none belongs", Probe("probe", "same", 4, "c"), exchange, exchange, None,
         ("holds a locked instruction",)),
        ("a cold call in the hot part", Probe("probe", "cost", 4, "c", extra=1, cold=True),
         load.replace("   7:\tret", "   7:\tcall   QWORD PTR [rip+0x0]\n\t\t\t9: R_X86_64_GOTPCRELX\t"
                      "foundation::core::detail::report_fatal_(char const*)-0x4\n   d:\tret"), load, None,
         ("has no cold part", "calls ['foundation::core::detail::report_fatal_']")),
        ("a cold part with no jump into it", Probe("probe", "cost", 3, "c", cold=True), load, load, FATAL_CALL,
         ("no jump into its cold part",)),
        ("a cold part with no fatal exit", Probe("probe", "cost", 5, "c", extra=2, cold=True), checked, plain,
         "   0:\tud2\n", ("does not call the fatal exit",)),
        ("a checked access", Probe("probe", "cost", 5, "c", extra=2, cold=True), checked, plain, FATAL_CALL, ()),
        ("a cold part where none belongs", Probe("probe", "cost", 5, "c", extra=2), checked, plain, FATAL_CALL,
         ("has a cold part",)),
        ("a family part over the raw part", Probe("probe", "cost", 9, "c", extra=1, cold=True), checked, plain,
         FATAL_CALL, ("permits 1 more",)),
        ("the same loop", Probe("probe", "loop", 6, "c"), loop, loop, None, ()),
        ("another loop", Probe("probe", "loop", 7, "c", extra=1), other_loop, loop, None, ("inner loops",)),
    )
    failures = 0
    for title, probe, family_body, raw_body, cold, expected in cases:
        text = synthetic("probe", family_body, cold) + synthetic("probe_raw", raw_body)
        findings, shown = shape_findings(parse_disassembly(text), (probe,))
        messages = [found.message for found in findings]
        is_right = all(any(part in message for message in messages) for part in expected)
        if not expected:
            is_right = not messages
        if is_right and messages and not shown:
            is_right = False
        if not is_right:
            print(f"self-test: the shape case '{title}' gave {messages}, and the expectation is {list(expected)}")
            failures += 1
    return failures


def self_test_flags() -> int:
    """Plant compile commands for each kind of flag finding.  Return the count of wrong verdicts."""
    base = ["g++", "-DX=1", "-Iinclude", "-std=c++26", "-fno-plt", "-Wall", "-o", "out.o", "-c", "unit.cpp"]
    probe = ["g++", "-DX=1", "-std=c++26", "-fno-plt", "-O3", "-march=x86-64-v3", "-fplugin=q.so", "-c", "probe.cpp"]
    fixed = ["-O3", "-march=x86-64-v3"]
    cases: tuple[tuple[str, list[str], list[str], list[str], tuple[str, ...]], ...] = (
        ("the same flags", base, probe, fixed, ()),
        ("per-preset flags of the base", base + ["-O1", "-g", "-fsanitize=address", "-fharden-compares",
                                                 "-D_GLIBCXX_ASSERTIONS=1", "-fPIC"], probe, fixed, ()),
        ("a new base flag", base + ["-fno-jump-tables"], probe, fixed, ("compiles with -fno-jump-tables, and the probe",)),
        ("a lost base flag", base, probe + ["-fno-common"], fixed, ("no foundation unit does",)),
        ("a new per-preset flag", base + ["-fno-tree-vectorize"], probe, fixed, ("-fno-tree-vectorize",)),
        ("a missing fixed flag", base, probe, fixed + ["-DNDEBUG"], ("fixed flag -DNDEBUG",)),
        ("another target", base, [word.replace("x86-64-v3", "native") for word in probe],
         ["-O3", "-march=native"], ("expectations",)),
    )
    failures = 0
    for title, base_words, probe_words, fixed_flags, expected in cases:
        findings = flag_findings(code_flags(probe_words, "probe.cpp"), [code_flags(base_words, "unit.cpp")] * 2,
                                 fixed_flags)
        messages = [found.message for found in findings]
        is_right = all(any(part in message for message in messages) for part in expected) and bool(messages) == bool(
            expected)
        if not is_right:
            print(f"self-test: the flag case '{title}' gave {messages}, and the expectation is {list(expected)}")
            failures += 1
    return failures


def plant_window(header: Path) -> str:
    """Return the text of a header with a second bounds test at the start of View::window of a dynamic extent.

    The parse tree of the header gives the body of the member function window
    of the class View with two parameters, and the names of the parameters.

    Raises:
        CannotRun: If the header does not hold exactly one such function
    """
    sites: list[tuple[tuple[int, int], list[str]]] = []
    for tree in tsast.parse([header], strict=False):
        for function in tree.find("function_definition"):
            body = function.child_by_field("body")
            names = [name for name, _ in tsast.parameters(function)]
            if body is None or len(names) != 2 or tsast.enclosing_function(body) != ("View", "window"):
                continue
            sites.append((body.start, names))
    if len(sites) != 1:
        raise CannotRun(f"{header} holds {len(sites)} definitions of View::window with two parameters, and the plant "
                        f"must find exactly one")
    (row, column), (offset_name, count_name) = sites[0]
    data = header.read_bytes()
    starts = [0]
    for index, byte in enumerate(data):
        if byte == 0x0A:
            starts.append(index + 1)
    position = starts[row] + column + 1
    plant = (f"\n        if ({offset_name} + {count_name} > size()) [[unlikely]] {{\n"
             f"            fatal(\"the shape self-test planted this second bounds test\");\n        }}\n")
    return (data[:position] + plant.encode() + data[position:]).decode()


def self_test_plant(inputs: Inputs) -> int:
    """Compile the probe unit with a second bounds test in View::window.  Return the count of wrong verdicts.

    Raises:
        CannotRun: If an input is missing, the plant site is not unique, or a compile fails
    """
    entry = read_database(inputs.build_dir, (inputs.probe_object,))[inputs.probe_object]
    failures = 0
    real, _ = shape_findings(probe_functions(entry))
    if real:
        print(f"self-test: the probe unit with no plant does not pass: {[found.message for found in real]}")
        failures += 1
    with tempfile.TemporaryDirectory(prefix="core-shapes-plant-") as scratch:
        root = Path(scratch)
        planted = root / "include" / PLANT_HEADER.relative_to("include")
        planted.parent.mkdir(parents=True)
        planted.write_text(plant_window(REPO_ROOT / PLANT_HEADER), encoding="utf-8")
        findings, _ = shape_findings(probe_functions(entry, root / "include"))
    failed = {found.message.split(":", 1)[0].removeprefix("probe ") for found in findings}
    expected = {"view_window", "view_append"}
    if failed != expected:
        print(f"self-test: the second bounds test in View::window failed the probes {sorted(failed)}, and the "
              f"expectation is {sorted(expected)}")
        failures += 1
    return failures


def run_self_test(inputs: Inputs) -> int:
    """Run each part of the self-test and return the exit status.

    Raises:
        CannotRun: If an input of the planted compile is missing
    """
    failures = self_test_shapes() + self_test_flags() + self_test_plant(inputs)
    print("self-test:", "PASS" if failures == 0 else f"FAIL ({failures})")
    return 0 if failures == 0 else 1


def main() -> int:
    """Parse the arguments, and run the check or its self-test."""
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--build-dir", type=Path, help="the build directory, with compile_commands.json")
    parser.add_argument("--probe-object", type=Path, help="the object of the target core_shape_probes")
    parser.add_argument("--base-objects", type=Path, nargs="+", default=[], help="the objects of the foundation library")
    parser.add_argument("--fixed-flags", default="", help="the fixed flags of the probe object, with spaces between")
    parser.add_argument("--self-test", action="store_true", help="check the checker on planted code")
    args = parser.parse_args()
    machine = platform.machine()
    if machine != EXPECTED_MACHINE:
        print(f"{CHECK}: SKIP: the expectations are for {EXPECTED_MACHINE} at {' '.join(EXPECTED_FIXED)}, and this "
              f"machine is {machine}.  No compiler for {machine} was available to write its expectations")
        return 3
    if args.build_dir is None or args.probe_object is None or not args.base_objects:
        parser.error("give --build-dir, --probe-object and --base-objects")
    if shutil.which("objdump") is None:
        print(f"{CHECK}: objdump is missing", file=sys.stderr)
        return 2
    inputs = Inputs(Path(os.path.abspath(args.build_dir)), Path(os.path.abspath(args.probe_object)),
                    tuple(Path(os.path.abspath(obj)) for obj in args.base_objects), tuple(args.fixed_flags.split()))
    try:
        return run_self_test(inputs) if args.self_test else run_check(inputs)
    except CannotRun as error:
        print(f"{CHECK}: the check cannot run: {error}", file=sys.stderr)
        return 2
    except tsast.KitMissing as error:
        print(f"{CHECK}: the self-test cannot find the plant site: {error}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
