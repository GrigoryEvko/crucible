"""no-throw-no-rtti — the two properties that -fno-exceptions and -fno-rtti stood for, checked on the artifact.

`guard check --rule no-throw-no-rtti ARTIFACT...` reads static libraries.

WHY NOT THE FLAGS
    Measured 2026-09-20: neither flag is in CMakeLists.txt and neither is on
    a compile line, in any preset, and the code guide gave both as common
    flags for as long as the list existed.  Both properties hold all the
    same.  To add the flags is the wrong repair:
      * -fno-exceptions does not compile src/fixy/concurrent/Topology.cpp,
        whose eight catch sites turn a failed sysfs read into a conservative
        topology and not a crash.  Where it compiles, it replaces a handled
        failure of a library with a bare std::terminate.
      * -fno-rtti changes nothing on this tree, and that is why nobody saw
        that it was absent: there is no dynamic_cast, no typeid, no
        std::type_info and no virtual outside a planning document.
    So the flags are not the guarantee.  The artifact is, and this module
    reads it:
      __cxa_throw or __cxa_rethrow referenced  nothing in the artifact throws.  An error path is std::expected or an
                                               abort
      __dynamic_cast or __cxa_bad_cast         no downcast at run time
      a typeinfo or a vtable defined           no polymorphic type
    The claim is stronger than the claim of each flag, because it finds a
    throw or a downcast from any place, also from inside a library header
    that this tree instantiates, which a flag on the units of the tree does
    not see.

A CATCH IS NOT A VIOLATION
    To catch a throw of a library and turn it into an abort or a fallback is
    the documented boundary, and it leaves __cxa_begin_catch and
    __cxa_end_catch behind.  The module does not read those two symbols.
    Only a throw is a violation.

THE READ
    assay.readers.elf reads each member of the artifact: its symbols and its
    relocations.  An undefined reference is a strong or a weak undefined
    symbol (assay.readers.elf, THE SYMBOLS).  A weak reference to a throw or
    to a downcast then counts too.

WHERE A THROW STANDS
    The module reads the relocations of the artifact, and each one names the
    section that holds it.  The tree compiles with
    -ffunction-sections, so that section names the function that throws.  A
    local function that no code or data section of its own object
    references is dead: no other object can name a local symbol, and
    --gc-sections drops it from each link.  A throw in a dead function does
    not count.  An optimized build can leave such a function behind, for
    example the cold part of an allocator after each call of it went.

AN ALLOCATION THAT FAILS
    In C++26, each allocate of libstdc++ holds an inline
    `throw std::bad_alloc()` or `throw std::bad_array_new_length()`, and
    std::inplace_vector throws std::bad_alloc when it is full.  An optimized
    build deletes most of these throws, and a build with no optimization
    keeps them.  This is the failure of an allocation inside the library, the
    same throw that std::__throw_bad_alloc makes from libstdc++.so, which the
    module cannot see and never refused.  Nothing in the tree catches it
    outside the documented catch boundary, so it ends the process: the policy
    of section II for an exhausted memory.  The module reads the type that
    each throw throws, and it admits a throw of one of these two types.  The
    type of a throw is the last typeinfo that its section references before
    the call, because a throw loads it as the second argument of
    __cxa_throw.  A rethrow carries no type, so it needs a row.

THE LEDGER
    utils/ledgers/no-throw-no-rtti.txt (assay.core.ledger, the policy admit)
    admits a throw or a polymorphic class that the tree cannot remove, one
    row each:
        @build/ARCHIVE | throw | admit | TEXT OF THE FUNCTION NAME | REASON
        @build/ARCHIVE | vtable | admit | TEXT OF THE CLASS NAME | REASON
    ARCHIVE is the file name of an artifact.  A row matches when the
    demangled name of the function or the class holds its text.  A throw row
    admits __cxa_throw and __cxa_rethrow in a matching function, and a
    vtable row admits the typeinfo and the vtable that an artifact defines
    for a matching class.  A downcast is never admitted.  A row whose
    artifact the run reads and that matches nothing is stale and fails, so
    the ledger only shrinks.  No command writes the ledger.

Usage
    guard check --rule no-throw-no-rtti ARTIFACT...
    guard check --rule no-throw-no-rtti --self-test     plant a throw and a downcast, and check each verdict

Exit 0 when each artifact holds the two properties, 1 on a violation, a stale row or a malformed row, 2 on a usage
error, an artifact that the ELF reader cannot read or a failed self-test, 3 when no named artifact exists.
"""

from __future__ import annotations

import os
import shutil
import sys
import tempfile
from collections.abc import Callable, Sequence
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import proc
from assay.core import rows as rows_grammar
from assay.core.registry import Policy
from assay.readers import elf

RULE = "no-throw-no-rtti"
THROW_SYMBOLS = ("__cxa_throw", "__cxa_rethrow")
DOWNCAST_SYMBOLS = ("__dynamic_cast", "__cxa_bad_cast")
# The typeinfo of std::bad_alloc and of std::bad_array_new_length (AN ALLOCATION THAT FAILS).
ALLOCATION_FAILURES = ("_ZTISt9bad_alloc", "_ZTISt20bad_array_new_length")
FUNCTION_PREFIXES = (".text.unlikely.", ".text.hot.", ".text.startup.", ".text.exit.", ".text.")
# The prefixes of the sections that hold no function: the debug information and the unwind tables.  The module does
# not read their relocations.
UNREAD_SECTIONS = (".debug", ".eh_frame")
ROW_KINDS = ("throw", "vtable")
BUILD_PREFIX = "@build/"
NOT_RUNNABLE = 3


@dataclass(frozen=True, slots=True)
class Row:
    """One row of the ledger: the artifact, the kind, the text of the name, and the line."""

    artifact: str
    kind: str
    text: str
    line: int


@dataclass(frozen=True, slots=True)
class Reference:
    """One reference to a throw symbol: live or dead, the symbol, the member, the thrown typeinfo, the function."""

    is_live: bool
    symbol: str
    member: str
    thrown: str
    function: str


@dataclass(slots=True)
class Run:
    """The state of one run: the rows, the rows that matched, the artifacts read, and the counts of the report."""

    rows: tuple[Row, ...]
    ledger_shown: str
    matched: set[int] = field(default_factory=set)
    scanned: set[str] = field(default_factory=set)
    symbols_read: int = 0
    allocation_failures: int = 0

    def admitting_row(self, artifact: str, kind: str, subject: str) -> Row | None:
        """Return the first row that admits a name of a kind in an artifact, and mark it, or None."""
        for row in self.rows:
            if row.kind == kind and row.artifact == artifact and row.text and row.text in subject:
                self.matched.add(row.line)
                return row
        return None


# ── The ledger ─────────────────────────────────────────────────────────────


def rows_of(ledger: rule_ledger.Ledger) -> tuple[tuple[Row, ...], list[str]]:
    """Return the rows of the ledger, and a message for each row that does not have the form of THE LEDGER."""
    rows: list[Row] = []
    problems: list[str] = []
    for key, entry in sorted(ledger.base.items(), key=lambda item: item[1].line):
        if key.kind not in ROW_KINDS or not key.path.startswith(BUILD_PREFIX) or not key.entity:
            problems.append(f"MALFORMED: line {entry.line} of the ledger is not `@build/ARCHIVE | throw | admit | TEXT | "
                            f"REASON` or `@build/ARCHIVE | vtable | admit | TEXT | REASON`.")
            continue
        rows.append(Row(key.path[len(BUILD_PREFIX):], key.kind, key.entity, entry.line))
    return tuple(rows), problems


def read_ledger(file: Path | None = None) -> tuple[rule_ledger.Ledger, str]:
    """Read the ledger of the rule, or a planted ledger, and give it with the path that a message shows.

    Raises:
        rows_grammar.RowsError: If the ledger does not obey its schema
    """
    if file is not None:
        return rule_ledger.parse(file.read_text(encoding="utf-8"), str(file), RULE, Policy.ADMIT), str(file)
    rule = rule_ledger.rule_of(RULE)
    return rule_ledger.read(rule), rule_ledger.shown_path(RULE)


# ── The artifact ───────────────────────────────────────────────────────────


def member_references(member: elf.Member, wanted: Sequence[str]) -> list[tuple[bool, str, str, str]]:
    """Return each reference of one member to a wanted symbol, as WHERE A THROW STANDS tells.

    Each item has four parts: True when the function is live, the symbol, the
    typeinfo that a throw throws or "-", and the function or the section that
    holds the reference.  Complexity: linear in the relocations and the
    symbols of the member.

    Raises:
        elf.ElfError: If a table of the member is not fully in the file
    """
    local_functions = {symbol.name for symbol in member.symbols()
                       if symbol.is_function and symbol.is_local and symbol.name}
    referenced: set[str] = set()
    hits: list[tuple[str, str, str]] = []
    section_index, typeinfo = -1, "-"
    for relocation in member.relocations(UNREAD_SECTIONS):
        if relocation.section_index != section_index:
            section_index, typeinfo = relocation.section_index, "-"
        target = relocation.symbol
        referenced.add(target)
        if target.startswith("_ZTI"):
            typeinfo = target
        if target in wanted:
            hits.append((target, relocation.section, typeinfo if target == "__cxa_throw" else "-"))
    found: list[tuple[bool, str, str, str]] = []
    for symbol, hit_section, thrown in hits:
        function = next((hit_section[len(prefix):] for prefix in FUNCTION_PREFIXES if hit_section.startswith(prefix)),
                        "")
        is_dead = function in local_functions and function not in referenced and hit_section not in referenced
        found.append((not is_dead, symbol, thrown, function or f"(section {hit_section})"))
    return found


def references(members: Sequence[elf.Member], wanted: Sequence[str]) -> list[Reference]:
    """Return each reference of the members of an artifact to a wanted symbol, with the name of its function.

    One run of c++filt gives the names.  Complexity: linear in the
    relocations and the symbols of the artifact.

    Raises:
        elf.ElfError: If a table of a member cannot be read, or c++filt does not give the names
    """
    found = [(member.name, *item) for member in members for item in member_references(member, wanted)]
    names = elf.demangle([entry[4] for entry in found])
    return [Reference(entry[1], entry[2], entry[0], entry[3], name) for entry, name in zip(found, names, strict=True)]


def has_undefined(undefined: set[str], symbol: str) -> bool:
    """Return True when the undefined names of an artifact hold a symbol, with or without one '_'."""
    return symbol in undefined or f"_{symbol}" in undefined


def scan_artifact(artifact: str, run: Run) -> tuple[int, list[str]]:
    """Read one artifact, and return its status and the messages of its violations.

    Returns:
        0 when it holds the two properties, 1 on a violation, 2 when it cannot be read
    """
    if not Path(artifact).is_file():
        return 2, [f"check-no-throw-no-rtti: {artifact} does not exist"]
    name = Path(artifact).name
    run.scanned.add(name)
    # An empty read is not a pass: a read that fails, or an archive with no
    # symbols, gives nothing to match, the shape of a guard that cannot fail.
    try:
        members = elf.members(artifact)
        named = sum(1 for member in members for symbol in member.symbols() if symbol.name)
        undefined = {symbol.name for member in members for symbol in member.symbols() if symbol.is_undefined}
        throws = references(members, THROW_SYMBOLS)
        defined = [(member.name, symbol.name) for member in members for symbol in member.symbols() if symbol.is_defined]
        defined_names = elf.demangle([symbol for _, symbol in defined])
    except (OSError, elf.ElfError) as problem:
        return 2, [f"check-no-throw-no-rtti: the ELF reader cannot read {artifact}, so nothing was checked: {problem}"]
    if not named:
        return 2, [f"check-no-throw-no-rtti: the ELF reader read no symbol from {artifact}, so nothing was checked.  "
                   f"An artifact with no symbol gives nothing to match, and it is not an artifact that throws nothing."]
    run.symbols_read += named
    status = 0
    messages: list[str] = []
    attributed = {reference.symbol for reference in throws}
    for reference in throws:
        if not reference.is_live:
            continue
        if reference.thrown in ALLOCATION_FAILURES:
            run.allocation_failures += 1
            continue
        if run.admitting_row(name, "throw", reference.function) is not None:
            continue
        messages.append(f"VIOLATION: {artifact} references {reference.symbol} in {reference.member}, in "
                        f"{reference.function}.\n  Something in this artifact throws.  An error path in this tree is "
                        f"std::expected or an abort, never a throw.  A throw that a library header makes and the tree "
                        f"cannot remove takes a row, with its reason, in {run.ledger_shown}.")
        status = 1
    for symbol in THROW_SYMBOLS:
        if has_undefined(undefined, symbol) and symbol not in attributed:
            messages.append(f"VIOLATION: {artifact} references {symbol}, and no relocation names the function that "
                            f"holds it.")
            status = 1
    for symbol in DOWNCAST_SYMBOLS:
        if has_undefined(undefined, symbol):
            messages.append(f"VIOLATION: {artifact} references {symbol}.\n  A runtime downcast reached the artifact.  "
                            f"This tree has no virtual and no dynamic_cast: dispatch is a kind enum plus static_cast.")
            status = 1
    # A defined typeinfo or vtable: a polymorphic type was compiled in.  A
    # reference to the typeinfo or the vtable of a library class is not a
    # definition, and the object that defines it holds the violation.  A
    # space before the name keeps the test of the line of nm --demangle.
    unadmitted = [f"{member}: {text}" for (member, _), text in zip(defined, defined_names, strict=True)
                  if (" typeinfo for " in f" {text}" or " vtable for " in f" {text}")
                  and run.admitting_row(name, "vtable", text.partition(" for ")[2]) is None]
    if unadmitted:
        messages.append(f"VIOLATION: {artifact} defines typeinfo or a vtable:\n" + "\n".join(unadmitted)
                        + "\n  A polymorphic type was compiled in.  The code guide says dispatch is a kind enum plus "
                          "static_cast, and every virtual in the tree lives in a planning document rather than in "
                          "code.  A class of a library that the tree uses by design takes a row, with its reason, in "
                          f"{run.ledger_shown}.")
        status = 1
    return status, messages


def stale_rows(run: Run) -> list[str]:
    """Return a message for each row whose artifact the run read and that matched nothing."""
    return [f"STALE: {run.ledger_shown}:{row.line} admits a reference in {row.artifact}, and the artifact holds none.  "
            f"Remove the row." for row in run.rows if row.artifact in run.scanned and row.line not in run.matched]


def check(artifacts: Sequence[str], ledger: rule_ledger.Ledger, shown: str) -> int:
    """Read each artifact against the ledger, print the report, and return the exit status."""
    print(f"check-no-throw-no-rtti: {len(artifacts)} artifact(s) to scan")
    for artifact in artifacts:
        print(f"  {artifact}")
    rows, problems = rows_of(ledger)
    run = Run(rows, shown)
    status = 0
    for problem in problems:
        print(problem, file=sys.stderr)
        status = 1
    scanned = 0
    for artifact in artifacts:
        if not Path(artifact).is_file():
            print(f"check-no-throw-no-rtti: {artifact} is not built, skipping", file=sys.stderr)
            continue
        scanned += 1
        result, messages = scan_artifact(artifact, run)
        for message in messages:
            print(message, file=sys.stderr)
        # An artifact that the tools cannot read gives 2, and a violation in another artifact does not hide it.
        status = max(status, result)
    if scanned == 0:
        print("check-no-throw-no-rtti: none of the named artifacts is built, so nothing was checked.", file=sys.stderr)
        return NOT_RUNNABLE
    for message in stale_rows(run):
        print(message, file=sys.stderr)
        status = max(status, 1)
    if status != 0:
        return status
    print(f"check-no-throw-no-rtti: {scanned} artifact(s) hold both properties ({run.symbols_read} symbols read, "
          f"{run.allocation_failures} throw(s) of a failed allocation, {len(run.matched)} ledger row(s) used) — "
          f"nothing else throws and nothing else dispatches at runtime")
    return 0


# ── The self-test ──────────────────────────────────────────────────────────

FIXTURES = {
    # An error path that returns and does not throw.
    "clean": "struct Plain { int value = 0; };\nint clean_entry(int input) noexcept {\n    Plain plain{input};\n"
             "    return plain.value;\n}\n",
    "throws": "int throwing_entry(int input) {\n    if (input < 0) { throw input; }\n    return input;\n}\n",
    # A catch with no throw: the shape of Topology.cpp, which must stay silent.
    "catches": "extern int may_fail(int);\nint catching_entry(int input) noexcept {\n    try {\n"
               "        return may_fail(input);\n    } catch (...) {\n        return -1;\n    }\n}\n",
    "rtti": "struct Base { virtual ~Base() = default; virtual int tag() const { return 0; } };\n"
            "struct Derived final : Base { int tag() const override { return 1; } };\n"
            "int downcast_entry(Base* base) noexcept {\n    auto* derived = dynamic_cast<Derived*>(base);\n"
            "    return derived ? derived->tag() : -1;\n}\n",
    # A throw that a row admits, and a throw in a local function that nothing calls, which no link keeps.
    "admitted": "int admitted_throwing_entry(int input) {\n    if (input < 0) { throw input; }\n    return input;\n}\n"
                "[[gnu::used]] static int dead_throwing_entry(int input) {\n    if (input < 0) { throw input; }\n"
                "    return input;\n}\nint also_clean(int input) noexcept { return input; }\n",
    "polymorphic": "struct Admitted { virtual ~Admitted() = default; virtual int tag() const { return 2; } };\n"
                   "int admitted_class_entry() noexcept { Admitted admitted; Admitted* held = &admitted;\n"
                   "    return held->tag(); }\nAdmitted* admitted_make() { static Admitted one; return &one; }\n",
    # A class whose vtable another object defines: a reference, and not a definition.
    "references": "#include <new>\nstruct Elsewhere { Elsewhere() = default; virtual int tag() const; };\n"
                  "Elsewhere* placed_entry(void* memory) noexcept { return new (memory) Elsewhere; }\n",
    "allocation": "#include <new>\nint allocation_entry(int input) { if (input < 0) { throw std::bad_alloc(); } "
                  "return input; }\nint array_entry(int input) { if (input < 0) { throw std::bad_array_new_length(); }"
                  " return input; }\n",
    # A function that throws std::bad_alloc and an int: the int throw stays a violation.
    "mixed": "#include <new>\nint mixed_entry(int input) {\n    if (input < 0) { throw std::bad_alloc(); }\n"
             "    if (input > 9) { throw input; }\n    return input;\n}\n",
    # A weak reference to the downcast of the C++ runtime: a weak undefined symbol, and not a strong one.
    "weak_downcast": 'extern "C" [[gnu::weak]] void* __dynamic_cast(const void*, const void*, const void*, long);\n'
                     "void* weak_downcast_entry(const void* pointer) noexcept {\n"
                     "    return __dynamic_cast ? __dynamic_cast(pointer, nullptr, nullptr, 0) : nullptr;\n}\n",
}
PLANTED_ROWS = ("@build/libadmitted.a | throw | admit | admitted_throwing_entry | a planted throw\n"
                "@build/libpolymorphic.a | vtable | admit | Admitted | a planted class\n")


def planted_ledger(work: Path, rows: str) -> tuple[rule_ledger.Ledger, str]:
    """Write a planted ledger of the rows, and read it as the module reads the ledger of the tree."""
    file = work / "ledger.txt"
    file.write_text(f"# planted\nformat {rule_ledger.FORMAT}\nrule {RULE}\n{rows}", encoding="utf-8")
    return read_ledger(file)


def _self_test_damaged_table(work: Path, expect: Callable[[str, bool], None]) -> None:
    """Check that an artifact with a symbol table that the ELF reader cannot read gives 2.

    The symbol table of the planted member links to a section that its section header table does not hold.
    """
    broken = elf.make_file([elf.Planted(".symtab", bytes(elf.SYMBOL_SIZE), elf.SHT_SYMTAB, link=99,
                                        entry_size=elf.SYMBOL_SIZE)])
    (work / "libbroken.a").write_bytes(elf.make_archive([("broken.o", broken)]))
    status, messages = scan_artifact(str(work / "libbroken.a"), Run((), "planted"))
    expect("an artifact whose symbol table cannot be read gives 2 and names the failure",
           status == 2 and any("cannot read" in message for message in messages))


def self_test() -> int:
    """Plant a throw, a downcast and their look-alikes in archives, and check each verdict.

    Returns:
        0 when every case holds, 2 on a failure, 3 when no compiler is found
    """
    compiler = os.environ.get("CRUCIBLE_CXX") or os.environ.get("CXX") or "g++"
    if shutil.which(compiler) is None:
        print(f"check-no-throw-no-rtti: SELF-TEST cannot run: no compiler at {compiler}", file=sys.stderr)
        return NOT_RUNNABLE
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="check-no-throw-no-rtti-") as work_text:
        work = Path(work_text)
        for unit, source in FIXTURES.items():
            (work / f"{unit}.cpp").write_text(source, encoding="utf-8")
            compiled = proc.run(proc.Command([compiler, "-std=c++17", "-O1", "-ffunction-sections", "-c",
                                              str(work / f"{unit}.cpp"), "-o", str(work / f"{unit}.o")],
                                             timeout_s=proc.TOOL_S))
            archived = proc.run(proc.Command(["ar", "rcs", str(work / f"lib{unit}.a"), str(work / f"{unit}.o")],
                                             timeout_s=proc.QUICK_S))
            if compiled.exit != 0 or archived.exit != 0:
                print(f"check-no-throw-no-rtti: SELF-TEST FAILED — could not build the {unit} fixture",
                      file=sys.stderr)
                return 2
        ledger, shown = planted_ledger(work, PLANTED_ROWS)
        rows, problems = rows_of(ledger)
        run = Run(rows, shown)

        def scanned(unit: str) -> tuple[int, str]:
            status, messages = scan_artifact(str(work / f"lib{unit}.a"), run)
            return status, "\n".join(messages)

        expect("the planted ledger reads with no problem", not problems and len(rows) == 2)
        expect("a clean artifact passes", scanned("clean")[0] == 0)
        status, text = scanned("throws")
        expect("a throw is reported against __cxa_throw in its function",
               status == 1 and "__cxa_throw in throws.o, in throwing_entry(int)." in text)
        expect("a catch with no throw is the documented boundary, and it stays silent", scanned("catches")[0] == 0)
        status, text = scanned("rtti")
        expect("a downcast and a polymorphic type are reported",
               status == 1 and ("__dynamic_cast" in text or "typeinfo" in text or "vtable" in text))
        expect("a throw that a row admits, and a throw in a dead local function, are not reported",
               scanned("admitted")[0] == 0)
        expect("a polymorphic class that a row admits is not reported", scanned("polymorphic")[0] == 0)
        expect("a reference to a vtable that another object defines is not reported", scanned("references")[0] == 0)
        expect("a row that matched a reference is not stale", not stale_rows(run))
        before = run.allocation_failures
        expect("a throw of std::bad_alloc or std::bad_array_new_length is not reported",
               scanned("allocation")[0] == 0 and run.allocation_failures >= before + 2)
        status, text = scanned("mixed")
        expect("an int throw beside a throw of std::bad_alloc is reported",
               status == 1 and "__cxa_throw in mixed.o, in mixed_entry(int)." in text)
        status, text = scanned("weak_downcast")
        expect("a weak reference to the downcast is reported", status == 1 and "__dynamic_cast" in text)

        (work / "libjunk.a").write_text("not an archive\n", encoding="utf-8")
        expect("an artifact that the ELF reader cannot read gives 2, also beside an artifact with a violation",
               check([str(work / "libthrows.a"), str(work / "libjunk.a")], ledger, shown) == 2)
        _self_test_damaged_table(work, expect)

        ledger, shown = planted_ledger(work, PLANTED_ROWS + "@build/libclean.a | throw | admit | no_such_function | "
                                                            "a row that matches nothing\n")
        rows, _ = rows_of(ledger)
        stale_run = Run(rows, shown)
        for unit in ("admitted", "polymorphic", "clean"):
            scan_artifact(str(work / f"lib{unit}.a"), stale_run)
        stale = stale_rows(stale_run)
        expect("a row that matches nothing is stale",
               len(stale) == 1 and f"{shown}:{rows[2].line} admits a reference in libclean.a" in stale[0])
        expect("a row for one artifact does not admit a throw in another",
               scan_artifact(str(work / "libthrows.a"), stale_run)[0] == 1)
        try:
            planted_ledger(work, "@build/libadmitted.a | throw | admit | admitted_throwing_entry | -\n")
            expect("a row with no reason is refused", False)
        except rows_grammar.RowsError:
            expect("a row with no reason is refused", True)
        ledger, _ = planted_ledger(work, "@build/libadmitted.a | catch | admit | entry | a wrong kind\n")
        expect("a row of an unknown kind is malformed", bool(rows_of(ledger)[1]))

    if failures:
        print(f"check-no-throw-no-rtti: SELF-TEST FAILED — {len(failures)} case(s) did not hold", file=sys.stderr)
        return 2
    print("check-no-throw-no-rtti: self-test passed — a throw and a downcast are caught, a catch without a throw, a "
          "clean artifact, an admitted throw or class, a dead local function, a failed allocation and a reference to "
          "a library vtable are not, and a stale or malformed row fails")
    return 0


def main(argv: list[str]) -> int:
    """Check the named artifacts against the ledger, or run the self-test.

    Args:
        argv: The words after `guard check --rule no-throw-no-rtti`

    Returns:
        The exit status
    """
    if argv == ["--self-test"]:
        return self_test()
    if not argv or any(word.startswith("-") for word in argv):
        print("usage: guard check --rule no-throw-no-rtti ARTIFACT...  |  --self-test", file=sys.stderr)
        return 2
    try:
        ledger, shown = read_ledger()
    except (rows_grammar.RowsError, rule_ledger.LedgerError, OSError) as problem:
        print(f"check-no-throw-no-rtti: the ledger cannot be read: {problem}", file=sys.stderr)
        return 1
    return check(argv, ledger, shown)
