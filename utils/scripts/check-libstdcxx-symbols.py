#!/usr/bin/env python3
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
      * An object of the archive has an undefined reference to the symbol
        (the nm type U, w or v).
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
    Each row gives the class of its symbol.  The first rule of this list that
    the mangled name obeys gives the class:
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
    utils/scripts/libstdcxx-symbols-ledger.txt holds two types of row:
        kind | KIND | TARGET
        KIND | LIBRARY | MARK | CLASS | SYMBOL | DEMANGLED NAME
    The sets depend on the build, because the inlining and the macro
    _GLIBCXX_ASSERTIONS change the references.  KIND is the build kind and
    TARGET is the target tier of utils/scripts/build_target.py, for example
    x86_64-debug-asan on x86-64-eb53fd1c.  A -march=native build on a
    different processor then does not use the rows.  A kind row tells that
    the ledger holds the sets of the kind on the target.
    The demangled name is for the reader.  The check compares only the
    mangled names.

THE VERDICT
    For the kind of the build:
      * A libstdc++ symbol of a library with no row is an error.  A change
        can only remove a use of libstdc++.
      * A row whose library does not use its symbol is an error, until the
        same commit writes the ledger again (--write).  The ledger keeps no
        slack.
      * A row whose mark or class is not the mark or the class of its symbol
        is an error.  A row of a library that the inputs do not name is an
        error too.
      * Each library with no error gives one warning with its counts.  The
        libstdc++ uses that stay are then in the output of each run.
    An archive that nm cannot read, an archive with no undefined reference,
    and a libstdc++ that nm cannot read are errors.  In each case the check
    read nothing.

WRITE THE LEDGER
    --write writes the rows of the kind of the build, and it keeps the rows
    of each other kind.  It writes a new allow symbol.  It refuses a new
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
    sets of an aarch64 kind.  The check prints each row of the build with the
    prefix "libstdcxx-row: " after a failure and when the ledger does not hold
    the kind.  --import LOG puts those rows into the ledger in place of the
    old rows of the kind.

Usage
    check-libstdcxx-symbols.py --build-dir DIR [--warnings-dir DIR]
    check-libstdcxx-symbols.py --build-dir DIR --write [--raise]
    check-libstdcxx-symbols.py --import LOG
    check-libstdcxx-symbols.py --self-test

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
import subprocess
import sys
import tempfile
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass, field
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import build_census  # noqa: E402
import build_target  # noqa: E402
import check_report  # noqa: E402
import cost_meter  # noqa: E402
import elf_file  # noqa: E402
from repo_root import REPO_ROOT  # noqa: E402

CHECK = "libstdcxx-symbols"
LEDGER = "utils/scripts/libstdcxx-symbols-ledger.txt"
INPUTS = "libstdcxx-symbols-inputs.txt"
NOT_APPLICABLE = build_target.NOT_APPLICABLE
KIND_ROW = "kind"
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
# The nm types of an undefined reference: a strong one, a weak one and a weak object.
UNDEFINED_TYPES = frozenset({"U", "w", "v"})
# One line of `nm -A --undefined-only` for a member of an archive.
NM_LINE = re.compile(r"(?P<archive>[^:]+):(?P<member>[^:]+):\s+(?P<type>[A-Za-z])\s+(?P<symbol>\S+)")
ROW_MARK = "libstdcxx-row: "
RAISE_REASONS = ("a new toolchain", "code that moved from a base header into a base source file",
                 "a new build kind or target")


class LedgerError(ValueError):
    """A row of the ledger does not have the format."""


class InputError(ValueError):
    """An input of the check does not exist, or a tool cannot read it."""


@dataclass(frozen=True, slots=True)
class Row:
    """One symbol row of the ledger, with the number of its line."""

    kind: str
    library: str
    mark: str
    symbol_class: str
    symbol: str
    demangled: str
    line: int = 0

    def text(self) -> str:
        """Return the row as one line of the ledger, with no line break."""
        return f"{self.kind} | {self.library} | {self.mark} | {self.symbol_class} | {self.symbol} | {self.demangled}"


@dataclass(slots=True)
class Ledger:
    """The rows of the ledger: the head comment, the target and the line of each kind, and the symbol rows."""

    head: list[str] = field(default_factory=list)
    targets: dict[str, str] = field(default_factory=dict)
    kind_lines: dict[str, int] = field(default_factory=dict)
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


def read_ledger(path: Path) -> Ledger:
    """Read the ledger file.

    Raises:
        LedgerError: If a row does not have the format
        OSError: If the file cannot be read
    """
    return parse_ledger(path.read_text(encoding="utf-8"), str(path))


def parse_ledger(text: str, path: str) -> Ledger:
    """Read the text of a ledger.  The leading comment lines are its head.

    Raises:
        LedgerError: If a row does not have the format, a symbol row comes before the row of its kind, or a row
            has a second copy
    """
    ledger = Ledger()
    is_head = True
    for number, raw in enumerate(text.splitlines(), start=1):
        stripped = raw.strip()
        if not stripped or stripped.startswith("#"):
            if is_head:
                ledger.head.append(raw)
            continue
        is_head = False
        cells = [cell.strip() for cell in stripped.split(" | ", 5)]
        if cells[0] == KIND_ROW:
            if len(cells) != 3 or not cells[1] or not cells[2] or cells[1] in ledger.targets:
                raise LedgerError(f"{path}:{number}: a kind row is 'kind | KIND | TARGET', one for each kind")
            ledger.targets[cells[1]] = cells[2]
            ledger.kind_lines[cells[1]] = number
            continue
        if len(cells) != 6 or not all(cells) or cells[2] not in MARKS or cells[3] not in CLASSES:
            raise LedgerError(f"{path}:{number}: a symbol row is 'KIND | LIBRARY | MARK | CLASS | SYMBOL | DEMANGLED "
                              f"NAME', with a MARK of {'/'.join(MARKS)} and a CLASS of {'/'.join(CLASSES)}")
        kind, library, mark, symbol_class, symbol, demangled = cells
        if kind not in ledger.targets:
            raise LedgerError(f"{path}:{number}: the row of the kind {kind} comes before the kind row of {kind}")
        by_symbol = ledger.rows.setdefault(kind, {}).setdefault(library, {})
        if symbol in by_symbol:
            raise LedgerError(f"{path}:{number}: the symbol {symbol} of {library} has a second row in {kind}")
        by_symbol[symbol] = Row(kind, library, mark, symbol_class, symbol, demangled, number)
    return ledger


def ledger_text(ledger: Ledger) -> str:
    """Return the text of a ledger: the head and one empty line, then each kind row with the rows of its kind.

    The order is fixed, so a second write of the same sets gives the same text.
    """
    head = list(ledger.head)
    while head and not head[-1].strip():
        head.pop()
    lines = [*head, ""] if head else []
    for kind in sorted(ledger.targets):
        lines.append(f"{KIND_ROW} | {kind} | {ledger.targets[kind]}")
        libraries = ledger.rows.get(kind, {})
        for library in sorted(libraries):
            ordered = sorted(libraries[library].values(), key=lambda row: (row.mark, row.symbol_class, row.symbol))
            lines += [row.text() for row in ordered]
    return "\n".join(lines) + "\n"


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


def tool_output(command: list[str], data: str | None = None) -> str:
    """Run a tool and return its standard output.

    Raises:
        InputError: If the tool cannot start or exits with a status that is not 0
    """
    try:
        done = subprocess.run(command, input=data, capture_output=True, text=True, check=False)
    except OSError as problem:
        raise InputError(f"{command[0]} cannot start: {problem.strerror}") from None
    if done.returncode != 0:
        raise InputError(f"{shlex.join(command)[:300]} exits {done.returncode}: {done.stderr.strip()[:400]}")
    return done.stdout


def libstdcxx_exports(compiler: str) -> frozenset[str]:
    """Return the names that the libstdc++ of the compiler defines in its dynamic symbol table.

    Raises:
        InputError: If the compiler names no libstdc++.so, the file is not an ELF shared library, or nm reads
            no defined name
    """
    named = tool_output([compiler, "-print-file-name=libstdc++.so"]).strip()
    path = Path(named)
    if not path.is_absolute() or not path.is_file():
        raise InputError(f"{compiler} -print-file-name=libstdc++.so gives {named!r}, which is not a file")
    resolved = path.resolve()
    if not elf_file.is_elf(resolved):
        raise InputError(f"{resolved} is not an ELF shared library.  The check reads only an ELF libstdc++, and "
                         f"not a linker script")
    names = frozenset(fields[2].split("@", 1)[0]
                      for fields in map(str.split, tool_output(["nm", "-D", "--defined-only", str(resolved)])
                                        .splitlines())
                      if len(fields) >= 3)
    if not names:
        raise InputError(f"nm reads no defined name from {resolved}, and the check has no libstdc++ to compare with")
    return names


def read_census(inputs: Inputs) -> Census:
    """Read the undefined references of each archive, and keep the ones that libstdc++ defines.

    One nm call reads each archive, one nm call reads libstdc++, and one
    c++filt call demangles the symbols.  Complexity: linear in the number of
    undefined references and of libstdc++ exports.

    Raises:
        InputError: If a tool fails, an archive gives no undefined reference, or libstdc++ cannot be read
    """
    exports = libstdcxx_exports(inputs.compiler)
    listing = tool_output(["nm", "-A", "--undefined-only", "--no-demangle", *map(str, inputs.archives)])
    library_of = {str(archive): archive.name for archive in inputs.archives}
    references: dict[str, dict[str, set[str]]] = {archive.name: {} for archive in inputs.archives}
    census = Census()
    for line in listing.splitlines():
        match = NM_LINE.fullmatch(line)
        if match is None or match["type"] not in UNDEFINED_TYPES or match["archive"] not in library_of:
            continue
        census.references += 1
        references[library_of[match["archive"]]].setdefault(match["symbol"], set()).add(match["member"])
    empty = sorted(name for name, symbols in references.items() if not symbols)
    if empty:
        raise InputError(f"nm reads no undefined reference from {', '.join(empty)}, and the check read nothing.  An "
                         f"archive that calls nothing is not an archive that uses no libstdc++ symbol")
    census.uses = {library: {symbol: members for symbol, members in symbols.items() if symbol in exports}
                   for library, symbols in references.items()}
    symbols = sorted({symbol for uses in census.uses.values() for symbol in uses})
    names = tool_output(["c++filt"], "".join(f"{symbol}\n" for symbol in symbols)).splitlines() if symbols else []
    if len(names) != len(symbols):
        raise InputError("c++filt does not give one name for each symbol")
    census.demangled = dict(zip(symbols, names))
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
    kind_line = ledger.kind_lines.get(kind, 0)
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
            findings.append(check_report.Finding("error", ledger_shown, kind_line, CHECK, said))
        for symbol in sorted(set(rows) - set(used)):
            row = rows[symbol]
            findings.append(check_report.Finding("error", ledger_shown, row.line, CHECK,
                                                 f"{library} no longer uses {row.demangled} ({symbol}).  {write}"))
        for symbol in sorted(set(rows) & set(used)):
            row = rows[symbol]
            wanted = (mark_of(symbol), classify(symbol))
            if (row.mark, row.symbol_class) != wanted:
                findings.append(check_report.Finding(
                    "error", ledger_shown, row.line, CHECK, f"the row of {symbol} gives the mark {row.mark} and the "
                    f"class {row.symbol_class}, and the symbol has the mark {wanted[0]} and the class {wanted[1]}.  "
                    f"{write}"))
        if len(findings) == errors_before and rows:
            marks = Counter(row.mark for row in rows.values())
            classes = Counter(row.symbol_class for row in rows.values() if row.mark == RATCHET)
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
        ledger = read_ledger(ledger_path)
        inputs = read_inputs(build_dir / INPUTS)
    except (OSError, LedgerError, InputError) as problem:
        return check_report.emit([check_report.Finding("error", ledger_shown, 0, CHECK, str(problem))], CHECK,
                                 warnings_dir)
    unbuilt = [build_census.shown(archive, root) for archive in inputs.archives if not archive.is_file()]
    if unbuilt:
        print(f"{CHECK}: the build has not made {', '.join(unbuilt)}.  Build the target all.  The check does not apply")
        return NOT_APPLICABLE
    try:
        target = build_target.build_tier(build_dir, inputs.compiler)
        census = read_census(inputs)
    except (OSError, InputError, build_target.TierError) as problem:
        return check_report.emit([check_report.Finding("error", build_shown, 0, CHECK, str(problem))], CHECK,
                                 warnings_dir)
    if write:
        return write_ledger(ledger_path, ledger, kind, target, census, allow_raise)
    if ledger.targets.get(kind) != target:
        print(build_target.not_held(CHECK, f"the kind {kind}", target, sorted(ledger.targets.items())))
        print_rows(kind, target, census)
        return NOT_APPLICABLE
    write_command = f"python3 utils/scripts/check-libstdcxx-symbols.py --build-dir {build_shown} --write"
    status = check_report.emit(evaluate(census, ledger, kind, ledger_shown, write_command), CHECK, warnings_dir)
    total = sum(len(uses) for uses in census.uses.values())
    print(f"{CHECK}: {total} libstdc++ symbols in {len(census.uses)} libraries of {kind} on {target}, from "
          f"{census.references} undefined references")
    if status != 0:
        print_rows(kind, target, census)
    return status


def kind_rows(kind: str, census: Census) -> dict[str, dict[str, Row]]:
    """Return the rows of the libstdc++ symbols of each library of the census, as the ledger holds them."""
    return {library: {symbol: Row(kind, library, mark_of(symbol), classify(symbol), symbol, census.demangled[symbol])
                      for symbol in uses}
            for library, uses in census.uses.items() if uses}


def print_rows(kind: str, target: str, census: Census) -> None:
    """Print the kind row and the symbol rows of the build with ROW_MARK, as --import reads them from a log."""
    ledger = Ledger(targets={kind: target}, rows={kind: kind_rows(kind, census)})
    build_target.print_rows(ROW_MARK, ledger_text(ledger).splitlines())
    print(f"{CHECK}: the lines above give the rows of the kind {kind}.  Import them from the log with "
          f"`python3 utils/scripts/check-libstdcxx-symbols.py --import LOG`")


def import_rows(ledger_path: Path, source: Path) -> int:
    """Put the rows of one kind that print_rows printed into the ledger, in place of the old rows of the kind.

    Returns:
        0 when the ledger is written, 1 when the rows or the ledger do not have the format
    """
    try:
        ledger = read_ledger(ledger_path)
        imported = parse_ledger("\n".join(build_target.rows_in_log(
            source.read_text(encoding="utf-8", errors="replace"), ROW_MARK)) + "\n", str(source))
        if len(imported.targets) != 1:
            raise LedgerError(f"{source}: the rows name {len(imported.targets)} kinds, and an import takes one")
        kind, target = next(iter(imported.targets.items()))
        ledger.targets[kind] = target
        ledger.rows[kind] = imported.rows.get(kind, {})
        text = ledger_text(ledger)
        parse_ledger(text, str(source))
    except (OSError, LedgerError) as problem:
        print(f"{CHECK}: the ledger is not written: {problem}", file=sys.stderr)
        return 1
    ledger_path.write_text(text, encoding="utf-8")
    rows = sum(len(symbols) for symbols in ledger.rows[kind].values())
    print(f"{CHECK}: imported the kind {kind} on {target}: {rows} rows, to {ledger_path}")
    return 0


def write_ledger(ledger_path: Path, ledger: Ledger, kind: str, target: str, census: Census, allow_raise: bool) -> int:
    """Write the rows of the kind, unless a new ratchet symbol comes and --raise is absent.

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
    ledger.targets[kind] = target
    ledger.rows[kind] = kind_rows(kind, census)
    ledger_path.write_text(ledger_text(ledger), encoding="utf-8")
    rows = sum(len(uses) for uses in ledger.rows[kind].values())
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
SELF_TEST_HEAD = "# a planted ledger\n"
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
    """A scratch build with planted archives, an inputs file, a build kind, a CMakeCache.txt and a ledger."""

    def __init__(self, root: Path, compiler: str) -> None:
        """Compile the units, and make a build with the archives libbase.a and libglibc.a.

        Raises:
            InputError: If a unit does not compile or an archive cannot be made
        """
        self.root = root
        self.compiler = compiler
        self.build = root / "build"
        self.build.mkdir(parents=True)
        self.ledger = root / "libstdcxx-symbols-ledger.txt"
        self.ledger.write_text(SELF_TEST_HEAD, encoding="utf-8")
        (self.build / cost_meter.KIND_FILE).write_text(f"{SELF_TEST_KIND}\n", encoding="utf-8")
        (self.build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=Debug\nCMAKE_CXX_FLAGS:STRING=\n",
                                                   encoding="utf-8")
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
    with tempfile.TemporaryDirectory(prefix="libstdcxx-symbols-") as scratch_text:
        root = Path(scratch_text)
        for name, body in (("a row with five cells", "kind | k | t\nk | lib.a | ratchet | std | _ZSt9terminatev\n"),
                           ("a row with an unknown mark", "kind | k | t\nk | lib.a | maybe | std | _Zx | x\n"),
                           ("a symbol row before its kind row", "k | lib.a | ratchet | std | _ZSt9terminatev | x\n"),
                           ("a second kind row", "kind | k | t\nkind | k | u\n")):
            (root / "malformed.txt").write_text(body, encoding="utf-8")
            try:
                read_ledger(root / "malformed.txt")
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
        written = read_ledger(scratch.ledger)
        rows = written.rows.get(SELF_TEST_KIND, {})
        thread_rows = [row for row in rows.get("libbase.a", {}).values() if "hardware_concurrency" in row.demangled]
        expect("--write --raise writes the kind row and a ratchet row of the class std for std::thread",
               status == 0 and SELF_TEST_KIND in written.targets and len(thread_rows) == 1
               and (thread_rows[0].mark, thread_rows[0].symbol_class) == (RATCHET, "std"), text)
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
        guard_rows = read_ledger(scratch.ledger).rows[SELF_TEST_KIND]["libbase.a"]
        allowed = [guard_rows[name].mark for name in ("__cxa_guard_acquire", "__gxx_personality_v0")
                   if name in guard_rows]
        expect("--write writes the allowlist symbols without --raise, with the mark allow, and no row of libgcc",
               status == 0 and allowed == [ALLOW, ALLOW] and "_Unwind_Resume" not in guard_rows, text)
        status, findings, text = scratch.run()
        expect("after --write the build with the guard passes", status == 0 and not errors_of(findings), text)

        good = scratch.ledger.read_text(encoding="utf-8")
        scratch.ledger.write_text(good.replace("| allow | abi | __cxa_guard_acquire",
                                               "| ratchet | abi | __cxa_guard_acquire"), encoding="utf-8")
        status, findings, text = scratch.run()
        expect("a row with the wrong mark is an error", status == 1 and any("has the mark allow" in message
                                                                            for message in errors_of(findings)), text)
        scratch.ledger.write_text(good + f"{SELF_TEST_KIND} | libgone.a | ratchet | std | _ZSt9terminatev | x\n",
                                  encoding="utf-8")
        status, findings, text = scratch.run()
        expect("a row of a library that the inputs do not name is an error",
               status == 1 and any("name no such archive" in message for message in errors_of(findings)), text)
        target = written.targets[SELF_TEST_KIND]
        scratch.ledger.write_text(good.replace(f"kind | {SELF_TEST_KIND} | {target}",
                                               f"kind | {SELF_TEST_KIND} | other"), encoding="utf-8")
        status, _, text = scratch.run()
        expect("a build of another target does not apply, and one line gives the two targets",
               status == NOT_APPLICABLE and f"on the tier {target}" in text and f"{SELF_TEST_KIND} on other" in text,
               text)
        scratch.ledger.write_text(good, encoding="utf-8")

        (scratch.build / cost_meter.KIND_FILE).write_text("planted-other\n", encoding="utf-8")
        status, _, text = scratch.run()
        expect("a build kind that the ledger does not hold does not apply, and the check prints its rows",
               status == NOT_APPLICABLE and f"{ROW_MARK}kind | planted-other | {target}" in text
               and f"{ROW_MARK}planted-other | libbase.a | allow | abi | __cxa_guard_acquire" in text, text)
        log = root / "ci.log"
        log.write_text("".join(f"build+test aarch64\tTest\t2026-10-02T21:00:00Z {line}\n"
                               for line in text.splitlines()), encoding="utf-8")
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            imported = import_rows(scratch.ledger, log)
        status, findings, text = scratch.run()
        expect("--import puts the rows of the log into the ledger, and the build of the kind then passes",
               imported == 0 and status == 0 and not errors_of(findings)
               and SELF_TEST_KIND in read_ledger(scratch.ledger).targets, text)
        scratch.ledger.write_text(good, encoding="utf-8")
        (scratch.build / cost_meter.KIND_FILE).write_text(f"{SELF_TEST_KIND}\n", encoding="utf-8")

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
               and read_ledger(scratch.ledger).targets[SELF_TEST_KIND].startswith("default-"), text)
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
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
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
    if args.import_path is not None:
        return import_rows(root / LEDGER, args.import_path)
    if args.build_dir is None or (args.allow_raise and not args.write):
        parser.print_usage(sys.stderr)
        return 2
    return run(args.build_dir.resolve(), root, root / LEDGER, args.warnings_dir, args.write, args.allow_raise)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
