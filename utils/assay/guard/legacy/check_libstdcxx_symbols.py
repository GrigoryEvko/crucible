"""check-libstdcxx-symbols — the libstdc++ symbols that the libraries of the base and libcrucible.a use can only decrease.

RULE R5
    Rule R5 of misc/01_10_2026_quarantine.md says that no object of
    libcrucible.a uses a libstdc++ symbol outside a short allowlist.  The
    plan adds the link guard of the rule at the S4 gate.  Until that gate,
    this check holds the libstdc++ symbols of each library in a ledger.  No
    library can then get a new libstdc++ symbol while the families of the
    plan replace std.  A symbol that goes leaves the ledger in the same
    commit.

THE LIBRARIES
    test/CMakeLists.txt writes BUILD_DIR/libstdcxx-symbols-inputs.txt.  Its
    rows "archive PATH" name the archives of the targets foundation, fixy and
    crucible.  Its row "compiler PATH" names the compiler of the build.  A
    --write by hand reads the same file, and it then reads the inputs of the
    test.  The ledger names a library by the file name of its archive.

A LIBSTDC++ SYMBOL
    A symbol is a libstdc++ symbol of a library when two facts are true:
      * An object of the archive has an undefined reference to the symbol.
        Such a reference is a strong or a weak undefined symbol of its symbol
        table (assay.readers.elf, THE SYMBOLS).
      * The libstdc++ of the compiler defines the symbol in its dynamic
        symbol table.  "COMPILER -print-file-name=libstdc++.so" names that
        shared library, and each link of the tree uses it.
    These are the references that a link binds to libstdc++.  The rule then
    uses no list of name patterns.  A std template instance that an object
    defines is not a reference.  A reference that glibc, libgcc or the tree
    defines is not a libstdc++ symbol, also when its name looks like one.
    For example, glibc defines __cxa_atexit and libgcc defines
    _Unwind_Resume.  The check reads each undefined reference of each object.
    It does not find which functions a link keeps.

THE CLASS OF A SYMBOL
    The check gives the class of each symbol in its messages.  The first rule
    of this list that the mangled name obeys gives the class:
      alloc  The global operator new or delete.  The name starts with _Znw,
             _Zna, _Zdl or _Zda.
      abi    The C++ ABI runtime.  The name starts with __cxa_ or __gxx_, or
             it is __dynamic_cast, or it is in namespace __cxxabiv1.  The
             vtable of a typeinfo class is an example.
      std    A name in namespace std.  The functions that the std templates
             call are in this class, for example std::__throw_length_error
             and std::_Rb_tree_insert_and_rebalance.
      gnu    A name in namespace __gnu_cxx.
      c      A name with no mangling, for example __once_proxy, which
             std::call_once calls.
      other  Each other mangled name.
    A mangled name is in a namespace when the namespace comes after _Z and an
    optional special prefix (TV, TI, TS, TT, TH, TW, GV, GR or GTt).  An N and
    its qualifiers (r, V, K, R and O) can come before the namespace.  The
    mangled forms of std are St and the abbreviations Sa, Sb, Ss, Si, So and
    Sd.

THE ALLOWLIST
    Plan section 13.4 gives the first allowlist of the link guard:
    __cxa_guard_acquire, __cxa_guard_release, __cxa_guard_abort, __cxa_atexit
    and __gxx_personality_v0.  The row of such a symbol has the mark allow.
    Each other row has the mark ratchet.  At the S4 gate, no ratchet row can
    stay.  The plan adds operator new and operator delete to the allowlist
    only if the Ref family uses them.  That family does not exist at this
    time, and their rows have the mark ratchet.  glibc defines __cxa_atexit,
    and no row of this host names it.

THE LEDGER
    utils/ledgers/libstdcxx-symbols.txt is a ledger of the policy count, in
    format 1 of assay.core.ledger:
        configuration KIND kind KIND
        configuration KIND tier TARGET
        @build/LIBRARY | MARK | 1 | SYMBOL | -
        @KIND | @build/LIBRARY | MARK | +1 | SYMBOL | -
    The sets depend on the build, because the inlining and the macro
    _GLIBCXX_ASSERTIONS change the references.  A configuration is one build
    kind: KIND is the build kind and TARGET is the target tier of
    utils/assay/core/target.py, for example x86_64-debug-asan on
    x86-64-eb53fd1c.  A -march=native build on a different processor then
    does not use the rows.  LIBRARY is the file name of the archive in the
    build directory.  The first configuration is the base, and its rows give
    its set: each row is one symbol of one library.  A row of another
    configuration gives a difference from the base: +1 for a symbol that only
    that configuration uses, and -1 for a symbol of the base that it does not
    use.  The check gives the class and the demangled name of each symbol, so
    the ledger holds neither.

THE VERDICT
    For the kind of the build:
      * A libstdc++ symbol of a library with no row is an error.  A change
        can only remove a use of libstdc++.
      * A row whose library does not use its symbol is an error, until the
        same commit writes the ledger again (--write).  The ledger keeps no
        slack.
      * A row whose mark is not the mark of its symbol is an error.  A row of
        a library that the inputs do not name is an error too.
      * Each library with no error gives one warning with its counts.  The
        libstdc++ uses that stay are then in the output of each run.
    An archive or a libstdc++ that the ELF reader cannot read is an error.
    An archive with no undefined reference is an error too.  In each case
    the check read nothing.

WRITE THE LEDGER
    --write writes the rows of the kind of the build, and it keeps the set of
    each other kind: a write of the base writes the difference rows of the
    other kinds again.  It writes a new allow symbol.  It refuses a new
    ratchet symbol: remove the use.  --raise writes a new ratchet symbol too.
    Use it only for one of these reasons, and give the reason in the commit:
      * A new toolchain changed the exports of libstdc++, or the code of a
        library header.
      * A change moved code from a base header into a base source file.  The
        archive then shows a use that the header had before.
      * The ledger starts to hold a new build kind or a new target.

NOT APPLICABLE
    The check exits 3, with the reason, in three conditions: the build has no
    build-kind.txt, the build has not made each archive, or the ledger holds
    no sets of the kind and the target of the build.  In the last case it
    prints one line with the target of the build and the targets of the ledger
    (build_target.not_held), and then the rows of the build.

A KIND THAT ONLY CI BUILDS
    No aarch64 compiler is on the build host, so only a CI leg can read the
    sets of an aarch64 kind.  After a failure, and when the ledger does not
    hold the kind, the check prints the configuration rows and the difference
    rows of the build, each with the prefix "ledger-row: "
    (assay.core.ledger.rows_of_configuration).  --import LOG puts those rows
    into the ledger in place of the old rows of the kind.  The base kind has
    no difference rows, so the check prints no rows of the base: write them
    with --write.

Usage
    guard check --rule libstdcxx-symbols --build-dir DIR [--warnings-dir DIR]
    guard check --rule libstdcxx-symbols --build-dir DIR --write [--raise]
    guard check --rule libstdcxx-symbols --import LOG
    guard check --rule libstdcxx-symbols --self-test

Exit 0 with no finding or with warnings only, 1 with an error, 2 on a usage error
or a failed self-test, 3 when the check does not apply to the build.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import os
import re
import shlex
import shutil
import sys
import tempfile
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from collections.abc import Iterable
from dataclasses import dataclass, field, replace
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import meter as cost_meter
from assay.core import proc
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import target as build_target
from assay.core.paths import REPO_ROOT
from assay.readers import census as build_census
from assay.readers import cmake
from assay.readers import elf as elf_file

CHECK = "libstdcxx-symbols"
INPUTS = "libstdcxx-symbols-inputs.txt"
NOT_APPLICABLE = build_target.NOT_APPLICABLE
# The PATH of the rows of a library: the archive in the build directory.
LIBRARY_PREFIX = "@build/"
# The two facts of a configuration: the build kind and the target tier.
FACT_KIND = "kind"
FACT_TIER = "tier"
ALLOW = "allow"
RATCHET = "ratchet"
MARKS = (ALLOW, RATCHET)
# The first allowlist of the link guard, from section 13.4 of the plan.
ALLOWLIST = frozenset({"__cxa_guard_acquire", "__cxa_guard_release", "__cxa_guard_abort", "__cxa_atexit",
                       "__gxx_personality_v0"})
CLASSES = ("alloc", "abi", "std", "gnu", "c", "other")
_SPECIAL = r"(?:T[VISTHW]|G[VR]|GTt)?"
_NESTED = r"(?:N[rVK]*[RO]?)?"
ALLOC_NAME = re.compile(r"_Z(?:nw|na|dl|da)")
ABI_NAME = re.compile(rf"_Z{_SPECIAL}{_NESTED}10__cxxabiv1")
STD_NAME = re.compile(rf"_Z{_SPECIAL}{_NESTED}S[tabsiod]")
GNU_NAME = re.compile(rf"_Z{_SPECIAL}{_NESTED}9__gnu_cxx")
RAISE_REASONS = ("a new toolchain", "code that moved from a base header into a base source file",
                 "a new build kind or target")


class LedgerError(ValueError):
    """The ledger does not exist, or a row does not have the format.

    Attributes:
        line: The line of the row, or 0 when no row applies
    """

    def __init__(self, message: str, line: int = 0) -> None:
        """Keep the message and the line of the row."""
        super().__init__(message)
        self.line = line


class InputError(ValueError):
    """An input of the check does not exist, or a tool cannot read it."""


@dataclass(frozen=True, slots=True)
class Row:
    """The symbol of one library in the set of one kind, with the line of its row."""

    kind: str
    library: str
    mark: str
    symbol: str
    line: int = 0


@dataclass(slots=True)
class Ledger:
    """The ledger: its rows in format 1, the target of each kind, and the set of each kind as symbol rows."""

    table: rule_ledger.Ledger
    targets: dict[str, str] = field(default_factory=dict)
    rows: dict[str, dict[str, dict[str, Row]]] = field(default_factory=dict)


@dataclass(frozen=True, slots=True)
class Inputs:
    """The compiler of the build and the archives that the check reads."""

    compiler: str
    archives: tuple[Path, ...]


@dataclass(slots=True)
class Census:
    """The libstdc++ symbols of each library with the members that reference them, and their demangled names."""

    uses: dict[str, dict[str, set[str]]] = field(default_factory=dict)
    demangled: dict[str, str] = field(default_factory=dict)
    references: int = 0


# ── The classes and the marks ──────────────────────────────────────


def classify(symbol: str) -> str:
    """Return the class of a symbol: the first rule of THE CLASS OF A SYMBOL that its name obeys."""
    if ALLOC_NAME.match(symbol):
        return "alloc"
    if symbol.startswith(("__cxa_", "__gxx_")) or symbol == "__dynamic_cast" or ABI_NAME.match(symbol):
        return "abi"
    if STD_NAME.match(symbol):
        return "std"
    if GNU_NAME.match(symbol):
        return "gnu"
    return "other" if symbol.startswith("_Z") else "c"


def mark_of(symbol: str) -> str:
    """Return allow for a symbol of the allowlist of plan section 13.4, else ratchet."""
    return ALLOW if symbol in ALLOWLIST else RATCHET


# ── The ledger ─────────────────────────────────────────────────────


def read_ledger(rule: rule_registry.Rule, path: Path) -> Ledger:
    """Read the ledger file.

    Raises:
        LedgerError: If the file does not exist, or a row does not have the format
        OSError: If the file cannot be read
    """
    if not path.is_file():
        raise LedgerError(f"{path} does not exist.  It holds the configuration rows and the symbol rows that --write "
                          f"writes")
    try:
        table = rule_ledger.read(rule, file=path)
    except rows_grammar.RowsError as problem:
        raise LedgerError(str(problem), problem.line) from None
    return ledger_of(table, str(path))


def ledger_of(table: rule_ledger.Ledger, shown: str) -> Ledger:
    """Give the set of each kind of a ledger in format 1.

    Complexity: O(r log r) for r rows.

    Raises:
        LedgerError: If a configuration does not have the two facts kind and tier, the ledger has symbol rows and no
            configuration, or a row does not name a library of the build directory, a mark and a symbol with the
            count 1 in its configuration
    """
    ledger = Ledger(table)
    for configuration in table.configurations:
        facts = dict(configuration.facts)
        if set(facts) != {FACT_KIND, FACT_TIER} or facts[FACT_KIND] != configuration.name:
            raise LedgerError(f"{shown}: the configuration {configuration.name} has the rows 'configuration KIND kind "
                              f"KIND' and 'configuration KIND tier TARGET', and no other")
        ledger.targets[configuration.name] = facts[FACT_TIER]
    if table.base and not table.configurations:
        first = min(entry.line for entry in table.base.values())
        raise LedgerError(f"{shown}:{first}: the ledger has symbol rows and no configuration row", first)
    for kind in ledger.targets:
        counts = table.expected(kind)
        for key in sorted(counts, key=lambda each: table.line_of(each, kind)):
            line = table.line_of(key, kind)
            if not key.path.startswith(LIBRARY_PREFIX) or key.kind not in MARKS or not key.entity or counts[key] != 1:
                raise LedgerError(f"{shown}:{line}: a symbol row is '@build/LIBRARY | MARK | 1 | SYMBOL | -', with a "
                                  f"MARK of {'/'.join(MARKS)}, and a row of another kind gives the count 0 or 1 to its "
                                  f"symbol", line)
            library = key.path.removeprefix(LIBRARY_PREFIX)
            by_symbol = ledger.rows.setdefault(kind, {}).setdefault(library, {})
            if key.entity in by_symbol:
                raise LedgerError(f"{shown}:{line}: the symbol {key.entity} of {library} has a second row in {kind}",
                                  line)
            by_symbol[key.entity] = Row(kind, library, key.kind, key.entity, line)
    return ledger


def census_findings(census: Census) -> list[check_report.Finding]:
    """Return one finding for each libstdc++ symbol of each library, with the key of its row."""
    return [check_report.Finding("error", f"{LIBRARY_PREFIX}{library}", 0, CHECK, "a libstdc++ symbol",
                                 mark_of(symbol), symbol)
            for library, uses in census.uses.items() for symbol in uses]


def with_configuration(table: rule_ledger.Ledger, kind: str, target: str) -> rule_ledger.Ledger:
    """Return the ledger with the configuration of a kind on a target: in place of its old one, or after the others."""
    configuration = rule_ledger.Configuration(kind, ((FACT_KIND, kind), (FACT_TIER, target)))
    configurations = [configuration if item.name == kind else item for item in table.configurations]
    if configuration not in configurations:
        configurations.append(configuration)
    return replace(table, configurations=tuple(configurations))


def keep_other_sets(written: rule_ledger.Ledger, before: rule_ledger.Ledger) -> rule_ledger.Ledger:
    """Return a ledger after a write of its base, with the difference rows of each other kind written again.

    The set of each other kind then stays the set that it had before the
    write.  Complexity: linear in the rows of the base and of each kind.
    """
    base_counts = {key: int(entry.value) for key, entry in written.base.items()}
    deltas: dict[str, dict[rule_ledger.Key, rule_ledger.Entry]] = {}
    for configuration in written.configurations[1:]:
        counts = before.expected(configuration.name)
        old = before.deltas.get(configuration.name, {})
        deltas[configuration.name] = {
            key: rule_ledger.Entry(key, counts.get(key, 0) - base_counts.get(key, 0),
                                   old[key].reason if key in old else "")
            for key in set(counts) | set(base_counts) if counts.get(key, 0) != base_counts.get(key, 0)}
    return replace(written, deltas=deltas)


# ── The inputs ─────────────────────────────────────────────────────


def read_inputs(path: Path) -> Inputs:
    """Read the inputs file that test/CMakeLists.txt writes.

    Raises:
        InputError: If the file does not exist, a row does not have the format, it names no compiler or no
            archive, or two archives have the same file name
    """
    try:
        text = path.read_text(encoding="utf-8")
    except OSError as problem:
        raise InputError(f"{path} cannot be read ({problem.strerror}).  Configure the build directory again, so "
                         f"that test/CMakeLists.txt writes it") from None
    compiler = ""
    archives: list[Path] = []
    for number, raw in enumerate(text.splitlines(), start=1):
        if not raw.strip():
            continue
        word, _, value = raw.partition(" ")
        if word == "compiler" and value and not compiler:
            compiler = value
        elif word == "archive" and value:
            archives.append(Path(value))
        else:
            raise InputError(f"{path}:{number}: a row is 'compiler PATH' (one time) or 'archive PATH'")
    names = Counter(archive.name for archive in archives)
    twice = sorted(name for name, count in names.items() if count > 1)
    if not compiler or not archives or twice:
        raise InputError(f"{path} must name one compiler and archives with different file names (names twice: "
                         f"{', '.join(twice) or 'none'})")
    return Inputs(compiler, tuple(archives))


def tool_output(command: list[str]) -> str:
    """Run a tool and return its standard output.

    Raises:
        InputError: If the tool cannot start, runs past its limit, or exits with a status that is not 0
    """
    try:
        done = proc.run(proc.Command(command, timeout_s=proc.TOOL_S))
    except proc.CannotStart as problem:
        raise InputError(f"{command[0]} cannot start: {problem.error.strerror}") from None
    except proc.TimedOut:
        raise InputError(f"{shlex.join(command)[:300]} runs past its limit of {proc.TOOL_S:g} s") from None
    if done.exit != 0:
        raise InputError(f"{shlex.join(command)[:300]} exits {done.exit}: {done.err.strip()[:400]}")
    return done.out


def libstdcxx_exports(compiler: str) -> frozenset[str]:
    """Return the names that the libstdc++ of the compiler defines in its dynamic symbol table.

    Raises:
        InputError: If the compiler names no libstdc++.so, the file is not an ELF shared library, or the ELF reader
            reads no defined dynamic name
    """
    named = tool_output([compiler, "-print-file-name=libstdc++.so"]).strip()
    path = Path(named)
    if not path.is_absolute() or not path.is_file():
        raise InputError(f"{compiler} -print-file-name=libstdc++.so gives {named!r}, which is not a file")
    resolved = path.resolve()
    if not elf_file.is_elf(resolved):
        raise InputError(f"{resolved} is not an ELF shared library.  The check reads only an ELF libstdc++, and "
                         f"not a linker script")
    try:
        with elf_file.mapped(resolved) as library:
            names = frozenset(symbol.name for symbol in library.dynamic_symbols() if symbol.is_defined)
    except (OSError, elf_file.ElfError) as problem:
        raise InputError(f"the dynamic symbols of {resolved} cannot be read: {problem}") from None
    if not names:
        raise InputError(f"the ELF reader reads no defined dynamic name from {resolved}, and the check has no "
                         f"libstdc++ to compare with")
    return names


def read_census(inputs: Inputs, held: Iterable[str] = ()) -> Census:
    """Read the undefined references of each archive, and keep the ones that libstdc++ defines.

    assay.readers.elf reads each archive and libstdc++.  One call of its
    demangle() gives the names of the symbols and of the symbols of `held`,
    the rows of the ledger, so that a message can name a symbol that goes.
    Complexity: linear in the number of undefined references and of
    libstdc++ exports.

    Raises:
        InputError: If an archive or libstdc++ cannot be read, an archive gives no undefined reference, or
            c++filt does not give the names
    """
    exports = libstdcxx_exports(inputs.compiler)
    references: dict[str, dict[str, set[str]]] = {archive.name: {} for archive in inputs.archives}
    census = Census()
    for archive in inputs.archives:
        try:
            members = elf_file.members(archive)
        except (OSError, elf_file.ElfError) as problem:
            raise InputError(f"the archive {archive} cannot be read: {problem}") from None
        for member in members:
            for symbol in member.symbols():
                if symbol.is_undefined:
                    census.references += 1
                    references[archive.name].setdefault(symbol.name, set()).add(member.name)
    empty = sorted(name for name, symbols in references.items() if not symbols)
    if empty:
        raise InputError(f"the ELF reader reads no undefined reference from {', '.join(empty)}, and the check read "
                         f"nothing.  An archive that calls nothing is not an archive that uses no libstdc++ symbol")
    census.uses = {library: {symbol: members for symbol, members in symbols.items() if symbol in exports}
                   for library, symbols in references.items()}
    symbols = sorted({symbol for uses in census.uses.values() for symbol in uses} | set(held))
    try:
        census.demangled = dict(zip(symbols, elf_file.demangle(symbols), strict=True))
    except elf_file.ElfError as problem:
        raise InputError(str(problem)) from None
    return census


# ── The verdict ────────────────────────────────────────────────────


def evaluate(census: Census, ledger: Ledger, kind: str, ledger_shown: str,
             write_command: str) -> list[check_report.Finding]:
    """Compare the libstdc++ symbols of each library with the rows of the kind.

    Each library with no error gives a warning with its counts.  Complexity:
    linear in the number of symbols and rows, with a sort of each library.
    """
    findings: list[check_report.Finding] = []
    held = ledger.rows.get(kind, {})
    write = f"Write the ledger again in the same commit: `{write_command}`"
    for library in sorted(set(census.uses) | set(held)):
        rows = held.get(library, {})
        if library not in census.uses:
            findings += [check_report.Finding(
                "error", ledger_shown, row.line, CHECK, f"the row names {library}, and the inputs of the build name no "
                f"such archive.  {write}") for row in sorted(rows.values(), key=lambda row: row.line)]
            continue
        used = census.uses[library]
        errors_before = len(findings)
        for symbol in sorted(set(used) - set(rows)):
            shown = f"{census.demangled[symbol]} ({symbol})"
            members = build_census.names_text(used[symbol])
            if mark_of(symbol) == ALLOW:
                said = (f"{library} uses {shown} in {members}, and the ledger has no row for it.  The symbol is on the "
                        f"allowlist of plan section 13.4.  {write}")
            else:
                said = (f"{library} uses the libstdc++ symbol {shown}, of the class {classify(symbol)}, in "
                        f"{members}, and the ledger has no row for it.  A change can only remove a libstdc++ use "
                        f"(rule R5).  Use a type of fixy or foundation in its place")
            # A missing row has no line.  Its place is the first base row of its library, or 0.
            line = ledger.table.line_of(rule_ledger.Key(f"{LIBRARY_PREFIX}{library}", mark_of(symbol), symbol), kind)
            findings.append(check_report.Finding("error", ledger_shown, line, CHECK, said))
        for symbol in sorted(set(rows) - set(used)):
            row = rows[symbol]
            findings.append(check_report.Finding("error", ledger_shown, row.line, CHECK,
                                                 f"{library} no longer uses {census.demangled[symbol]} ({symbol}).  "
                                                 f"{write}"))
        for symbol in sorted(set(rows) & set(used)):
            row = rows[symbol]
            if row.mark != mark_of(symbol):
                findings.append(check_report.Finding(
                    "error", ledger_shown, row.line, CHECK, f"the row of {symbol} gives the mark {row.mark}, and the "
                    f"symbol has the mark {mark_of(symbol)}.  {write}"))
        if len(findings) == errors_before and rows:
            marks = Counter(row.mark for row in rows.values())
            classes = Counter(classify(row.symbol) for row in rows.values() if row.mark == RATCHET)
            shown_classes = ", ".join(f"{name} {classes[name]}" for name in CLASSES if classes[name])
            findings.append(check_report.Finding(
                "warning", ledger_shown, min(row.line for row in rows.values()), CHECK,
                f"{library} keeps {len(rows)} libstdc++ symbols in {kind}: {marks[ALLOW]} allow and {marks[RATCHET]} "
                f"ratchet ({shown_classes or 'none'})"))
    return findings


# ── The command ────────────────────────────────────────────────────


def run(build_dir: Path, root: Path, ledger_path: Path, warnings_dir: Path | None, write: bool = False,
        allow_raise: bool = False) -> int:
    """Run the check, or write the rows of the kind of the build.

    Returns:
        The exit status
    """
    ledger_shown = build_census.shown(ledger_path, root)
    build_shown = build_census.shown(build_dir, root)
    kind = build_target.kind_of(build_dir)
    if kind is None:
        print(f"{CHECK}: {build_shown} has no {cost_meter.KIND_FILE}.  The check does not apply")
        return NOT_APPLICABLE
    try:
        rule = rule_ledger.rule_of(CHECK)
    except (OSError, rule_ledger.LedgerError, rows_grammar.RowsError) as problem:
        return check_report.emit([check_report.Finding("error", rule_registry.REGISTRY_PATH, 0, CHECK, str(problem))],
                                 CHECK, warnings_dir)
    try:
        ledger = read_ledger(rule, ledger_path)
        inputs = read_inputs(build_dir / INPUTS)
    except (OSError, LedgerError, InputError) as problem:
        line = problem.line if isinstance(problem, LedgerError) else 0
        return check_report.emit([check_report.Finding("error", ledger_shown, line, CHECK, str(problem))], CHECK,
                                 warnings_dir)
    unbuilt = [build_census.shown(archive, root) for archive in inputs.archives if not archive.is_file()]
    if unbuilt:
        print(f"{CHECK}: the build has not made {', '.join(unbuilt)}.  Build the target all.  The check does not apply")
        return NOT_APPLICABLE
    try:
        target = build_target.build_tier(build_dir, inputs.compiler)
        census = read_census(inputs, (symbol for symbols in ledger.rows.get(kind, {}).values() for symbol in symbols))
    except (OSError, InputError, build_target.TierError) as problem:
        return check_report.emit([check_report.Finding("error", build_shown, 0, CHECK, str(problem))], CHECK,
                                 warnings_dir)
    if write:
        return write_ledger(rule, ledger_path, ledger, kind, target, census, allow_raise)
    if ledger.targets.get(kind) != target:
        print(build_target.not_held(CHECK, f"the kind {kind}", target, sorted(ledger.targets.items())))
        print_rows(ledger, kind, target, census)
        return NOT_APPLICABLE
    write_command = f"python3 utils/bin/guard check --rule libstdcxx-symbols --build-dir {build_shown} --write"
    status = check_report.emit(evaluate(census, ledger, kind, ledger_shown, write_command), CHECK, warnings_dir)
    total = sum(len(uses) for uses in census.uses.values())
    print(f"{CHECK}: {total} libstdc++ symbols in {len(census.uses)} libraries of {kind} on {target}, from "
          f"{census.references} undefined references")
    if status != 0:
        print_rows(ledger, kind, target, census)
    return status


def print_rows(ledger: Ledger, kind: str, target: str, census: Census) -> None:
    """Print the configuration rows and the difference rows of the build, as --import reads them from a log.

    The base kind has no difference rows, so the function prints only one
    line that tells how to write its rows.
    """
    if kind == ledger.table.base_name:
        print(f"{CHECK}: the kind {kind} is the base configuration of the ledger, and its rows have no difference "
              f"from it.  Write its rows with --write")
        return
    for line in rule_ledger.rows_of_configuration(ledger.table, kind, ((FACT_KIND, kind), (FACT_TIER, target)),
                                                  census_findings(census)):
        print(line)
    print(f"{CHECK}: the lines above give the rows of the kind {kind}.  Import them from the log with "
          f"`python3 utils/bin/guard check --rule libstdcxx-symbols --import LOG`")


def import_rows(rule: rule_registry.Rule, ledger_path: Path, source: Path) -> int:
    """Put the rows of one kind that print_rows printed into the ledger, in place of the old rows of the kind.

    Returns:
        0 when the ledger is written, 1 when the rows or the ledger do not have the format
    """
    try:
        ledger = read_ledger(rule, ledger_path)
        table = rule_ledger.import_rows(ledger.table, source.read_text(encoding="utf-8", errors="replace"))
        kind = table.configurations[-1].name
        imported = ledger_of(table, str(source))
    except (OSError, LedgerError, rule_ledger.LedgerError, rows_grammar.RowsError) as problem:
        print(f"{CHECK}: the ledger is not written: {problem}", file=sys.stderr)
        return 1
    rule_ledger.store(table, file=ledger_path)
    rows = sum(len(symbols) for symbols in imported.rows.get(kind, {}).values())
    print(f"{CHECK}: imported the kind {kind} on {imported.targets[kind]}: {rows} rows, to {ledger_path}")
    return 0


def write_ledger(rule: rule_registry.Rule, ledger_path: Path, ledger: Ledger, kind: str, target: str, census: Census,
                 allow_raise: bool) -> int:
    """Write the rows of the kind, unless a new ratchet symbol comes and --raise is absent.

    The write keeps the set of each other kind.  The first kind of an empty
    ledger becomes its base.

    Returns:
        The exit status
    """
    held = ledger.rows.get(kind, {})
    new_ratchet = sorted((library, symbol) for library, uses in census.uses.items() for symbol in uses
                         if symbol not in held.get(library, {}) and mark_of(symbol) == RATCHET)
    for library, symbol in new_ratchet:
        print(f"{CHECK}: {library} gets the ratchet symbol {census.demangled[symbol]} ({symbol})", file=sys.stderr)
    if new_ratchet and not allow_raise:
        print(f"{CHECK}: the ledger is not written: {len(new_ratchet)} new ratchet symbols.  Remove the uses.  Give "
              f"--raise only for {', for '.join(RAISE_REASONS)}, and give the reason in the commit", file=sys.stderr)
        return 1
    for library in sorted(set(held) - set(census.uses)):
        print(f"{CHECK}: the inputs name no archive {library}, so its rows of {kind} go", file=sys.stderr)
    table = with_configuration(ledger.table, kind, target)
    # The check refused each new ratchet symbol above, so the write takes each count of the build.
    written = rule_ledger.write(rule, table, census_findings(census), kind, rule_ledger.WriteMode.RAISE)
    if kind == written.base_name:
        written = keep_other_sets(written, ledger.table)
    rule_ledger.store(written, file=ledger_path)
    rows = sum(len(uses) for uses in census.uses.values())
    print(f"{CHECK}: wrote {rows} rows of {kind} on {target} to {ledger_path}")
    return 0


# ── The self-test ──────────────────────────────────────────────────


# The units of the planted libraries.  base holds a plain function, and
# threads holds a std::thread call.  text is the planted std::string use.
# guard holds a function-local static, whose initializer calls a function
# that can throw.  It references the guard functions and the personality
# routine of the allowlist, and _Unwind_Resume of libgcc.  glibc holds calls
# that glibc defines.
SELF_TEST_UNITS = {
    "base": 'extern "C" int crucible_symbols_extern(int);\n'
            "int plain_entry(int value) { return crucible_symbols_extern(value) + 1; }\n",
    "threads": "#include <thread>\n"
               "unsigned thread_entry() noexcept { return std::thread::hardware_concurrency(); }\n",
    "text": "#include <cstddef>\n#include <string>\n"
            "std::size_t text_entry(int count) {\n"
            "    std::string text(static_cast<std::size_t>(count), 'x');\n"
            "    return text.size();\n"
            "}\n",
    "guard": 'extern "C" int crucible_symbols_seed();\n'
             "int guard_entry() { static const int seeded = crucible_symbols_seed(); return seeded; }\n",
    "glibc": "#include <cstdlib>\n#include <cstring>\n"
             'extern "C" void crucible_symbols_at_exit();\n'
             "void copy_entry(char* to, const char* from, unsigned long size) { std::memcpy(to, from, size); }\n"
             "int exit_entry() { return std::atexit(crucible_symbols_at_exit); }\n",
}
SELF_TEST_KIND = "planted-self-test"
SELF_TEST_HEAD = f"# a planted ledger\nformat {rule_ledger.FORMAT}\nrule {CHECK}\n"
# A compiler that prints the target of an aarch64 GCC with no target flag, and runs the real compiler for each
# other command: it has no -march value, and it gives its target through -mcpu.
AARCH64_WRAPPER = """\
import os
import sys
if "--help=target" in sys.argv:
    sys.stdout.write({help!r})
    sys.exit(0)
os.execv({real!r}, [{real!r}, *sys.argv[1:]])
"""


class Scratch:
    """A scratch build with planted archives, an inputs file, a build kind, a reply of the CMake cache and a ledger."""

    def __init__(self, root: Path, compiler: str) -> None:
        """Compile the units, and make a build with the archives libbase.a and libglibc.a.

        Raises:
            InputError: If a unit does not compile or an archive cannot be made
        """
        self.root = root
        self.compiler = compiler
        self.build = root / "build"
        self.build.mkdir(parents=True)
        self.ledger = root / f"{CHECK}.txt"
        self.ledger.write_text(SELF_TEST_HEAD, encoding="utf-8")
        (self.build / cost_meter.KIND_FILE).write_text(f"{SELF_TEST_KIND}\n", encoding="utf-8")
        cmake.plant(self.build, cache={"CMAKE_BUILD_TYPE": ("STRING", "Debug"), "CMAKE_CXX_FLAGS": ("STRING", "")})
        with ThreadPoolExecutor(max_workers=len(SELF_TEST_UNITS)) as pool:
            list(pool.map(self.compile, SELF_TEST_UNITS))
        self.libraries: dict[str, list[str]] = {}
        self.archive("libbase.a", ["base", "threads"])
        self.archive("libglibc.a", ["glibc", "base"])

    def compile(self, unit: str) -> None:
        """Compile one unit of SELF_TEST_UNITS into an object.

        Raises:
            InputError: If the unit does not compile
        """
        source = self.root / f"{unit}.cpp"
        source.write_text(SELF_TEST_UNITS[unit], encoding="utf-8")
        tool_output([self.compiler, "-std=c++17", "-O1", "-c", str(source), "-o", str(self.root / f"{unit}.o")])

    def archive(self, name: str, units: list[str]) -> None:
        """Make one archive of the units again, and write the inputs file.

        Raises:
            InputError: If ar fails
        """
        path = self.build / name
        path.unlink(missing_ok=True)
        tool_output(["ar", "rcs", str(path), *(str(self.root / f"{unit}.o") for unit in units)])
        self.libraries[name] = units
        self.write_inputs(self.libraries)

    def write_inputs(self, libraries: dict[str, list[str]]) -> None:
        """Write the inputs file with the compiler and the archives of the libraries."""
        rows = [f"compiler {self.compiler}", *(f"archive {self.build / name}" for name in libraries)]
        (self.build / INPUTS).write_text("".join(f"{row}\n" for row in rows), encoding="utf-8")

    def run(self, write: bool = False, allow_raise: bool = False) -> tuple[int, list[check_report.Finding], str]:
        """Run the check on the scratch build, and return the status, the findings and the printed text."""
        with contextlib.redirect_stdout(io.StringIO()) as printed, contextlib.redirect_stderr(io.StringIO()) as errors:
            status = run(self.build, self.root, self.ledger, None, write, allow_raise)
        text = printed.getvalue() + errors.getvalue()
        return status, [found for found in map(check_report.parse_line, text.splitlines()) if found], text


def errors_of(findings: list[check_report.Finding]) -> list[str]:
    """Return the messages of the errors of a run."""
    return [found.message for found in findings if found.level == "error"]


def self_test() -> int:
    """Do a test of the check on planted archives and ledgers, with negative controls.

    Returns:
        0 when each case holds, 2 when a case fails, 3 when no compiler is available
    """
    failures: list[str] = []

    def expect(name: str, holds: bool, detail: object = "") -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(f"{name}: {detail}")

    for symbol, wanted in (("_Znwm", "alloc"), ("_ZdlPvm", "alloc"), ("_ZnwmSt11align_val_t", "alloc"),
                           ("__cxa_throw", "abi"), ("__gxx_personality_v0", "abi"), ("__dynamic_cast", "abi"),
                           ("_ZTVN10__cxxabiv120__si_class_type_infoE", "abi"), ("_ZSt9terminatev", "std"),
                           ("_ZNKSt5ctypeIcE13_M_widen_initEv", "std"), ("_ZTISt9bad_alloc", "std"),
                           ("_ZTTSt14basic_ifstreamIcSt11char_traitsIcEE", "std"), ("_ZNSaIcEC1Ev", "std"),
                           ("_ZNSt3_V215system_categoryEv", "std"), ("_ZTSSt9bad_alloc", "std"),
                           ("_ZN9__gnu_cxx27__verbose_terminate_handlerEv", "gnu"), ("__once_proxy", "c"),
                           ("_ZN8crucible6detail5entryEv", "other"), ("_ZN3stdx1fEv", "other")):
        expect(f"the class of {symbol} is {wanted}", classify(symbol) == wanted, classify(symbol))
    expect("the allowlist marks the guard functions and the personality routine, and not operator new",
           [mark_of(name) for name in ("__cxa_guard_acquire", "__gxx_personality_v0", "_Znwm", "__cxa_throw")]
           == [ALLOW, ALLOW, RATCHET, RATCHET])

    compiler = os.environ.get("CRUCIBLE_CXX") or os.environ.get("CXX") or "g++"
    if shutil.which(compiler) is None:
        print(f"check-libstdcxx-symbols --self-test: no compiler at {compiler}, so the self-test cannot run")
        return NOT_APPLICABLE
    try:
        rule = rule_ledger.rule_of(CHECK)
    except (OSError, rule_ledger.LedgerError, rows_grammar.RowsError) as problem:
        print(f"check-libstdcxx-symbols --self-test: FAILED, the registry gives no rule {CHECK}: {problem}")
        return 2
    kind_k = "configuration k kind k\nconfiguration k tier t\n"
    with tempfile.TemporaryDirectory(prefix="libstdcxx-symbols-") as scratch_text:
        root = Path(scratch_text)
        for name, body in (("a row with three cells", f"{kind_k}@build/lib.a | ratchet | 1 | _ZSt9terminatev\n"),
                           ("a row with an unknown mark", f"{kind_k}@build/lib.a | maybe | 1 | _Zx | -\n"),
                           ("a row that names no archive of the build directory",
                            f"{kind_k}lib.a | ratchet | 1 | _Zx | -\n"),
                           ("a symbol row with the count 2", f"{kind_k}@build/lib.a | ratchet | 2 | _Zx | -\n"),
                           ("a symbol row before the configuration rows",
                            f"@build/lib.a | ratchet | 1 | _Zx | -\n{kind_k}"),
                           ("a second tier of one kind", f"{kind_k}configuration k tier u\n"),
                           ("a configuration with no tier", "configuration k kind k\n"),
                           ("symbol rows and no configuration", "@build/lib.a | ratchet | 1 | _Zx | -\n"),
                           ("a difference row that gives a symbol the count 2",
                            f"{kind_k}configuration m kind m\nconfiguration m tier t\n"
                            "@build/lib.a | ratchet | 1 | _Zx | -\n@m | @build/lib.a | ratchet | +1 | _Zx | -\n")):
            (root / "malformed.txt").write_text(SELF_TEST_HEAD + body, encoding="utf-8")
            try:
                read_ledger(rule, root / "malformed.txt")
                expect(f"read_ledger refuses {name}", False)
            except LedgerError:
                expect(f"read_ledger refuses {name}", True)

        try:
            scratch = Scratch(root / "first", compiler)
        except InputError as problem:
            print(f"check-libstdcxx-symbols --self-test: FAILED, the planted libraries cannot be made: {problem}")
            return 2
        status, _, text = scratch.run(write=True)
        expect("--write refuses the first ratchet symbols without --raise",
               status == 1 and scratch.ledger.read_text(encoding="utf-8") == SELF_TEST_HEAD, text)
        status, _, text = scratch.run(write=True, allow_raise=True)
        written = read_ledger(rule, scratch.ledger)
        rows = written.rows.get(SELF_TEST_KIND, {})
        thread_rows = [row for row in rows.get("libbase.a", {}).values() if "hardware_concurrency" in row.symbol]
        expect("--write --raise writes the configuration of the kind as the base, and a ratchet row of the class std "
               "for std::thread",
               status == 0 and written.table.base_name == SELF_TEST_KIND and len(thread_rows) == 1
               and (thread_rows[0].mark, classify(thread_rows[0].symbol)) == (RATCHET, "std")
               and f"configuration {SELF_TEST_KIND} kind {SELF_TEST_KIND}\n" in scratch.ledger.read_text(), text)
        expect("memcpy and atexit of glibc give no row", "libglibc.a" not in rows, rows.get("libglibc.a"))
        status, findings, text = scratch.run()
        expect("an equal build passes, with one warning for the library that keeps symbols",
               status == 0 and not errors_of(findings) and len(findings) == 1 and "libbase.a keeps" in text, text)

        scratch.archive("libbase.a", ["base", "threads", "text"])
        status, findings, text = scratch.run()
        expect("a planted std::string use adds ratchet symbols, and the check fails and names the member",
               status == 1 and all("text.o" in message for message in errors_of(findings))
               and any("A change can only remove" in message for message in errors_of(findings)), text)
        status, _, text = scratch.run(write=True)
        expect("--write refuses the planted symbols", status == 1 and "not written" in text, text)
        scratch.archive("libbase.a", ["base", "threads"])
        status, _, text = scratch.run()
        expect("the build without the plant passes again", status == 0, text)

        scratch.archive("libbase.a", ["base"])
        status, findings, text = scratch.run()
        expect("a symbol that goes fails until --write",
               status == 1 and any("no longer uses" in message and "--write" in message
                                   for message in errors_of(findings)), text)
        status, _, _ = scratch.run(write=True)
        status, findings, text = scratch.run()
        expect("after --write the build passes, and the ledger keeps no row of std::thread",
               status == 0 and "hardware_concurrency" not in scratch.ledger.read_text(encoding="utf-8"), text)

        scratch.archive("libbase.a", ["base", "guard"])
        status, findings, text = scratch.run()
        expect("a new allowlist symbol fails until --write, and the message names the allowlist",
               status == 1 and errors_of(findings) and all("allowlist" in message for message in errors_of(findings)
                                                          if "__cxa_guard" in message), text)
        status, _, text = scratch.run(write=True)
        guard_rows = read_ledger(rule, scratch.ledger).rows[SELF_TEST_KIND]["libbase.a"]
        allowed = [guard_rows[name].mark for name in ("__cxa_guard_acquire", "__gxx_personality_v0")
                   if name in guard_rows]
        expect("--write writes the allowlist symbols without --raise, with the mark allow, and no row of libgcc",
               status == 0 and allowed == [ALLOW, ALLOW] and "_Unwind_Resume" not in guard_rows, text)
        status, findings, text = scratch.run()
        expect("after --write the build with the guard passes", status == 0 and not errors_of(findings), text)

        good = scratch.ledger.read_text(encoding="utf-8")
        scratch.ledger.write_text(good.replace("@build/libbase.a | allow | 1 | __cxa_guard_acquire |",
                                               "@build/libbase.a | ratchet | 1 | __cxa_guard_acquire |"),
                                  encoding="utf-8")
        status, findings, text = scratch.run()
        expect("a row with the wrong mark is an error", status == 1 and any("has the mark allow" in message
                                                                            for message in errors_of(findings)), text)
        scratch.ledger.write_text(good + "@build/libgone.a | ratchet | 1 | _ZSt9terminatev | -\n", encoding="utf-8")
        status, findings, text = scratch.run()
        expect("a row of a library that the inputs do not name is an error",
               status == 1 and any("name no such archive" in message for message in errors_of(findings)), text)
        target = written.targets[SELF_TEST_KIND]
        scratch.ledger.write_text(good.replace(f"configuration {SELF_TEST_KIND} tier {target}\n",
                                               f"configuration {SELF_TEST_KIND} tier other\n"), encoding="utf-8")
        status, _, text = scratch.run()
        expect("a build of another target does not apply, one line gives the two targets, and the check prints no "
               "rows of the base",
               status == NOT_APPLICABLE and f"on the tier {target}" in text and f"{SELF_TEST_KIND} on other" in text
               and "is the base configuration" in text and rule_ledger.ROW_MARK not in text, text)
        scratch.ledger.write_text(good, encoding="utf-8")

        # The kind planted-other builds libbase.a with no guard.  Its rows are then the differences -1 of the guard
        # symbols.
        def set_kind(kind: str, units: list[str]) -> None:
            (scratch.build / cost_meter.KIND_FILE).write_text(f"{kind}\n", encoding="utf-8")
            scratch.archive("libbase.a", units)

        set_kind("planted-other", ["base"])
        status, _, text = scratch.run()
        mark = rule_ledger.ROW_MARK
        expect("a build kind that the ledger does not hold does not apply, and the check prints its configuration "
               "rows and its difference rows",
               status == NOT_APPLICABLE and f"{mark}configuration planted-other tier {target}" in text
               and f"{mark}@planted-other | @build/libbase.a | allow | -1 | __cxa_guard_acquire | -" in text, text)
        log = root / "ci.log"
        log.write_text("".join(f"build+test aarch64\tTest\t2026-10-02T21:00:00Z {line}\n"
                               for line in text.splitlines()), encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            imported = import_rows(rule, scratch.ledger, log)
        status, findings, text = scratch.run()
        expect("--import puts the rows of the log into the ledger, and the build of the kind then passes",
               imported == 0 and status == 0 and not errors_of(findings)
               and set(read_ledger(rule, scratch.ledger).targets) == {SELF_TEST_KIND, "planted-other"}, text)

        set_kind(SELF_TEST_KIND, ["base", "threads", "guard"])
        status, _, text = scratch.run(write=True, allow_raise=True)
        set_kind("planted-other", ["base"])
        status_other, findings, text = scratch.run()
        expect("a write of the base keeps the set of the other kind",
               status == 0 and status_other == 0 and not errors_of(findings), text)
        set_kind("planted-other", ["base", "guard"])
        status, _, text = scratch.run(write=True)
        set_kind(SELF_TEST_KIND, ["base", "threads", "guard"])
        status_base, findings, text = scratch.run()
        expect("a write of the other kind writes its differences and keeps the set of the base",
               status == 0 and status_base == 0 and not errors_of(findings), text)
        scratch.ledger.write_text(good, encoding="utf-8")
        set_kind(SELF_TEST_KIND, ["base", "guard"])

        # An aarch64 compiler prints no -march value and gives its target through -mcpu.
        wrapper = root / "aarch64-g++"
        wrapper.write_text(f"#!{sys.executable}\n" + AARCH64_WRAPPER.format(help=build_target.AARCH64_HELP,
                                                                            real=shutil.which(compiler)),
                           encoding="utf-8")
        wrapper.chmod(0o755)
        scratch.compiler = str(wrapper)
        scratch.write_inputs(scratch.libraries)
        status, _, text = scratch.run(write=True)
        status, findings, text = scratch.run()
        expect("a compiler with no -march value has a target, so the build of an aarch64 compiler is read",
               status == 0 and not errors_of(findings)
               and read_ledger(rule, scratch.ledger).targets[SELF_TEST_KIND].startswith("default-"), text)
        scratch.compiler = compiler
        scratch.write_inputs(scratch.libraries)
        scratch.ledger.write_text(good, encoding="utf-8")

        scratch.write_inputs({**scratch.libraries, "libunbuilt.a": []})
        status, _, text = scratch.run()
        expect("an archive that the build has not made does not apply",
               status == NOT_APPLICABLE and "libunbuilt.a" in text, text)
        (scratch.build / INPUTS).unlink()
        status, findings, text = scratch.run()
        expect("a build with no inputs file is an error",
               status == 1 and any("Configure the build directory again" in message
                                   for message in errors_of(findings)), text)

    if failures:
        print(f"check-libstdcxx-symbols --self-test: FAILED, {len(failures)} case(s) did not hold")
        for failure in failures:
            print(f"  {failure}")
        return 2
    print("check-libstdcxx-symbols --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Parse the arguments and run the check, the writer or the self-test."""
    parser = argparse.ArgumentParser(prog="guard check --rule libstdcxx-symbols",
                                     description=__doc__.split("\n", 1)[0])
    parser.add_argument("--build-dir", type=Path, help="the build directory whose archives the check reads")
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help="the source root")
    parser.add_argument("--write", action="store_true", help="write the rows of the kind of the build")
    parser.add_argument("--raise", dest="allow_raise", action="store_true",
                        help="with --write, also write a new ratchet symbol")
    parser.add_argument("--import", dest="import_path", type=Path, metavar="LOG",
                        help="put the rows of a kind that the check printed into the ledger, from a log")
    parser.add_argument("--self-test", action="store_true", help="run the self-test")
    check_report.add_arguments(parser)
    args = parser.parse_args(argv)
    if args.self_test:
        return self_test()
    root = args.root.resolve()
    ledger_path = rule_ledger.ledger_path(CHECK, root)
    if args.import_path is not None:
        try:
            rule = rule_ledger.rule_of(CHECK)
        except (OSError, rule_ledger.LedgerError, rows_grammar.RowsError) as problem:
            print(f"{CHECK}: the ledger is not written: {problem}", file=sys.stderr)
            return 1
        return import_rows(rule, ledger_path, args.import_path)
    if args.build_dir is None or (args.allow_raise and not args.write):
        parser.print_usage(sys.stderr)
        return 2
    return run(args.build_dir.resolve(), root, ledger_path, args.warnings_dir, args.write, args.allow_raise)
