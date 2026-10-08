"""guard check --rule quarantine-plugin — the plugin finds each class of finding, only where the location rule puts it.

The test loads the quarantine plugin that the configure step built, compiles
each fixture of utils/tools/quarantine/test with the plugin loaded, and
compares what the plugin reports with the expectations below.  That
directory is the source root of the test, and rules.txt is its rule table:
include/fixy/, src/foundation/ and src/fixy/ hold base code, and the
quarantine rows name each other fixture.

Each class of finding has an expectation that fails when the quarantine plugin
loses the check of that class.  The base rule, the admit rows and the opt-out
region each have an expectation that fails when the plugin loses that rule.

Each row kind of the rule table has a plant: a forbidden header in a layer, a
door header outside its door, an upward include and a standard library name
that the table does not admit.  The test compiles each planted file in error
mode one time with no plant, which must pass, and one time with each plant,
which must fail with the finding of the plant.  The same four plants also
compile against the rule table of the tree, in a scratch root with the paths
of the tree: in report mode the section of the object holds the one finding of
the plant, and in error mode the plant fails the compile.  An enforce row with the mode
error makes a finding an error in report mode, and a file that no row holds
is an error.  The plugin and utils/assay/core/ruletable.py read each table of
table-cases.txt and a table for each white-space character of Python.  Each
one must refuse a malformed table at the line of its case, and the plugin must
give the error of the case and no other error.  Each one must accept each
other table.

contracts.cpp holds each form of a P2900 contract specifier that the contract
rule rejects, also in base code.  The test compiles it in each mode and with
-fsyntax-only, and compares the errors with CONTRACT_ERRORS.  A form that the
plugin stops seeing, a second error for one specifier and an error outside
the list each fail the test.

The plugin names the mode stamp of each file with a finding in the dependency
file of the unit, and enforce.txt for a file with no mode stamp.  Through
ccache, a unit whose file changes to the mode error after a stored compile
must miss and fail.  The negative control shows that a cache without
extra_files_to_hash gives the stored object and skips the error.  The last
part takes the compile command of a real object of the build directory, and
compiles build_plant.cpp with the flags of that preset: an enforce row with the
mode error fails the compile, and a row with the mode report does not.

A unit that writes an object holds its findings in the section
.crucible.quarantine of the object.  The test compares the section with the
report file of out= for the same compile.  Through ccache, a cache hit must
give the section of a real compile.  A changed rule table with a new stamp
must miss and give the new findings.  The negative controls show that the
stamp alone carries the table into the key of the cache, and that a key
without the stamp gives a section whose stamp is not the stamp of the
command, which the rule quarantine (`guard check --rule quarantine`)
refuses.

A generated file of the build directory has the name @build/ and its path in
the build directory, and the row `quarantine @build/` quarantines it.  The
contract rule and the opt-out regions apply to it too.  The test writes a
source root with a build directory in its scratch directory, and compiles a
unit of that root in each mode.  A build directory outside the root gives the
same name, and a table with no row for @build/ refuses the generated file.

A type walk and a walk of a macro expansion read a deep nesting to its end, so
a library type behind 70 template wrappers and a C library token behind 70
macros stay findings.  A type tree that shares its subtrees costs one visit
for each type, also in a file of the language.  Each such compile has a time
limit.

A #line directive, a line marker and #pragma GCC system_header in a file of
the root each fail the compile.  A file of the root that -isystem marks stays
a file of the root, and a library namespace that a quarantined file opens
again gets the walk of each other namespace.

The parts of the test run at the same time: the findings, the base files, the
malformed regions, the modes, the contract rule, the generated files, the
include rules, the library names, the provenance, the restrictions, the
readers of the table, the section, the dependencies, the enforce modes through
ccache and the flags of the build.  Each part writes its own files, and main
prints the verdicts of the parts in that order.  At most JOBS compiler
processes of all parts run at one time, and utils/tools/quarantine/
Quarantine.cmake gives the test the same number of ctest processors.  So a
ctest run with -j counts each compiler of the test.

usage: guard check --rule quarantine-plugin --cxx CXX --plugin PLUGIN.so --rules TABLE --jobs JOBS
       [--build-dir BUILD]

Exit 0 when each expectation holds, 1 when one fails, 2 on a usage error or a
plugin that does not exist.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import signal
import sys
import tempfile
import threading
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

from assay.core import proc
from assay.core import ruletable as layer_rules
from assay.core.paths import REPO_ROOT
from assay.loop import stamps as quarantine_stamps
from assay.readers import compdb
from assay.readers import sections as quarantine_sections

# The repository.  Its include/foundation/Quarantine.h gives the macros of a
# region, from a file outside the source root of the test.
REPO = REPO_ROOT
# The directory of the fixtures, the tables and the planted tree: the source root of the test.
HERE = REPO / "utils" / "tools" / "quarantine" / "test"
TEST_RULES = HERE / "rules.txt"
PLUGIN = "crucible_quarantine"

# (the macro of the plant, the planted file, the kind, a text in the entity)
PLANTS = (
    ("PLANT_LAYER_HEADER", "src/fixy/Plants.cpp", "layer_header", "<cmath> in the layer low"),
    ("PLANT_DOOR_HEADER", "src/fixy/Plants.cpp", "door_header", "<sys/socket.h> has the door src/fixy/SocketDoor.cpp"),
    ("PLANT_UPWARD_INCLUDE", "src/fixy/Plants.cpp", "upward_include",
     "include/fixy/high/High.h (the layer high) from the layer low"),
    ("PLANT_STD_NAME", "plant_std.cpp", "std_entity", "std::swap"),
)

# The rule tables of the two readers, with the verdict of each one.  The head
# comment of the file gives its format.
TABLE_CORPUS = HERE / "table-cases.txt"

# (kind, fixture, line, a text that the entity holds, or '=' and the whole entity)
PRESENT = (
    ("std_object", "violations.cpp", 15, "std::vector"),
    ("raw_pointer_object", "violations.cpp", 18, "int*"),
    ("c_array_object", "violations.cpp", 19, "int [4]"),
    ("raw_function_pointer", "violations.cpp", 20, "void (*)(int)"),
    ("raw_function_pointer", "violations.cpp", 21, "Holder::*"),
    ("std_object", "violations.cpp", 22, "std::basic_string"),
    ("std_object", "violations.cpp", 29, "std::basic_string (std::string)"),
    ("std_object", "violations.cpp", 29, "std::basic_string (const std::string&)"),
    ("raw_pointer_object", "violations.cpp", 31, "const char*"),
    ("c_library_call", "violations.cpp", 31, "memcpy"),
    ("raw_pointer_object", "violations.cpp", 34, "int*"),
    ("raw_new_delete", "violations.cpp", 34, "new"),
    ("raw_new_delete", "violations.cpp", 36, "delete"),
    ("c_library_call", "violations.cpp", 40, "strlen"),
    ("std_object", "violations.cpp", 43, "std::vector"),
    ("std_entity", "violations.cpp", 44, "std::swap"),
    ("std_object", "violations.cpp", 54, "std::vector<T>"),
    ("c_library_call", "violations.cpp", 57, "memcpy"),
    ("c_array_object", "violations.cpp", 64, "char [8]"),
    ("c_library_object", "violations.cpp", 69, "max_align_t"),
    ("raw_new_delete", "violations.cpp", 75, "new T"),
    ("std_object", "violations.cpp", 89, "std::basic_string_view"),
    ("std_entity", "violations.cpp", 95, "load"),
    ("std_entity", "violations.cpp", 101, "=std::basic_string_view"),
    ("std_entity", "violations.cpp", 102, "=std::basic_string_view"),
    ("raw_pointer_object", "opt_out.cpp", 8, "char*"),
    ("c_library_call", "opt_out.cpp", 8, "memset"),
    ("opted_out", "opt_out.cpp", 11, "raw_pointer_object char*"),
    ("opted_out", "opt_out.cpp", 11, "c_library_call memset"),
    ("raw_pointer_object", "opt_out.cpp", 14, "char*"),
    ("c_library_call", "opt_out.cpp", 14, "memset"),
    ("std_object", "type_walks.cpp", 10, "std::basic_string (std::type_identity<"),
    ("std_entity", "type_walks.cpp", 12, "=std::basic_string"),
    ("std_entity", "type_walks.cpp", 13, "=std::vector"),
    ("c_library_call", "type_walks.cpp", 15, "=strlen"),
    ("c_library_call", "type_walks.cpp", 17, "=strlen"),
    ("raw_function_pointer", "type_walks.cpp", 20, "std::string"),
    ("std_entity", "type_walks.cpp", 20, "=std::basic_string"),
)

# (fixture, line or None for each line, kind or None for each kind, a text in
# the entity or None): the plugin reports nothing of that shape there.
ABSENT = (
    ("violations.cpp", 40, "std_object", None),  # std::size_t names a fundamental type
    ("violations.cpp", 48, None, None),  # std::move is admitted
    ("violations.cpp", 55, None, None),  # a dependent member names nothing
    ("violations.cpp", 56, None, None),  # std::is_trivially_copyable_v is admitted
    ("violations.cpp", 62, None, None),  # a fixy type with a payload of a local type
    ("violations.cpp", 63, None, None),  # the body of fill is base code
    ("violations.cpp", 65, None, None),  # the body of clear_bytes is base code
    ("violations.cpp", 79, None, None),  # the instantiation of the template with a std::vector names nothing
    ("violations.cpp", 80, None, None),  # a dependent member names nothing, in the instantiation too
    ("violations.cpp", 85, None, None),  # the base spells the default argument
    ("violations.cpp", 87, None, None),  # the copy of an immediate default has the location of the call
    ("violations.cpp", 89, "std_entity", None),  # the compiler calls the conversion function
    ("violations.cpp", 95, None, "memory_order"),  # a constexpr variable of an enumeration is a named constant
    ("violations.cpp", 97, None, None),  # the immediate default of a template has the location of the call
    ("include/fixy/Shelf.h", None, None, None),  # base code
    ("opt_out.cpp", 11, "raw_pointer_object", None),  # the region opts it out
    ("opt_out.cpp", 11, "c_library_call", None),  # the region opts it out
)

# (fixture, a text that the diagnostics of the failed compile hold)
PRAGMA_ERRORS = (
    ("unclosed_region.cpp", "region has no"),
    ("region_without_reason.cpp", "takes one string that gives the reason"),
    ("end_without_region.cpp", "has no open region"),
    ("raw_pragma.cpp", "comes only from the macro"),
    ("own_region_macro.cpp", "comes only from the macro"),
    ("reason_without_class.cpp", "starts with its class"),
    ("region_macro_reason.cpp", "as one plain string literal in the call"),
    ("region_wrapper.cpp", "is used inside another macro"),
    ("region_end_wrapper.cpp", "is used inside another macro"),
)

# (file, line, specifier): each error of the contract rule for contracts.cpp.
# The specifier on line 73 is in an opt-out region.  Outside.h is outside the
# root, and the test writes it in its scratch directory.
CONTRACT_ERRORS = (
    ("include/fixy/Contracted.h", 9, "pre"),  # a template in base code
    ("contracts.cpp", 12, "pre"),  # a declaration
    ("contracts.cpp", 13, "post"),  # a definition
    ("contracts.cpp", 14, "pre"),  # two specifiers on one declaration
    ("contracts.cpp", 14, "post"),
    ("contracts.cpp", 16, "pre"),  # a template with no definition
    ("contracts.cpp", 20, "pre"),  # a member of a class template
    ("contracts.cpp", 21, "post"),  # a defined member of a class template, instantiated
    ("contracts.cpp", 23, "pre"),  # a member template of a class template
    ("contracts.cpp", 25, "pre"),  # a member of a nested class of a class template
    ("contracts.cpp", 27, "pre"),  # a hidden friend
    ("contracts.cpp", 29, "pre"),  # a friend template of a class template
    ("contracts.cpp", 34, "pre"),  # a member of a partial specialization
    ("contracts.cpp", 39, "pre"),  # a member template of a class
    ("contracts.cpp", 41, "pre"),  # a friend template of a class
    ("contracts.cpp", 44, "pre"),  # a lambda
    ("contracts.cpp", 45, "pre"),  # a generic lambda
    ("contracts.cpp", 48, "pre"),  # a local declaration
    ("contracts.cpp", 50, "pre"),  # a member of a local class
    ("contracts.cpp", 55, "pre"),  # an explicit specialization
    ("contracts.cpp", 57, "pre"),  # the spelling in a macro
)
CONTRACT_ERROR = re.compile(r"^(?P<path>.+?):(?P<line>\d+):\d+: error: the .(?P<specifier>pre|post). contract "
                            r"specifier is not permitted in this tree", re.MULTILINE)
OUTSIDE_HEADER = "#pragma once\nint outside_pre(int value) pre(value > 0);\n"
# A region that a header outside the root opens and closes.  The two pragmas
# of such a header agree: each one does nothing.
OUTSIDE_REGION_HEADER = ("#pragma once\n#pragma quarantine I_KNOW_WHAT_IM_DOING(\"a header outside the root\")\n"
                         "inline int outside_region_value() { return 1; }\n#pragma quarantine END_I_KNOW_WHAT_IM_DOING\n")

# The source files of the base, and the findings of each when src/ is the
# root and the file is quarantined: (line, kind, a text in the entity).
BASE_FIXTURES = ("src/foundation/Floor.cpp", "src/fixy/Door.cpp")
BASE_FINDINGS = (
    (11, "std_object", "std::vector"),
    (12, "c_array_object", "char [8]"),
    (14, "raw_pointer_object", "char*"),
    (14, "c_library_call", "memset"),
)

# A source root in the scratch directory, with a build directory.  The
# generated header opts out the specifier on line 3, and the specifier on line
# 5 stays an error.  The row `quarantine @build/` of rules.txt holds the
# generated header, so the pointer on line 6 is a finding of
# @build/Generated.h.  The macro on line 7 spells a pointer, and the generated
# header owns the place of that finding too.
GENERATED_HEADER = ("#include <foundation/Quarantine.h>\n"
                    "CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: a generated header that the test needs\")\n"
                    "int generated_opted(int value) pre(value > 0);\n"
                    "CRUCIBLE_END_I_KNOW_WHAT_IM_DOING\n"
                    "int generated_plain(int value) pre(value > 0);\n"
                    "inline char* generated_pointer = nullptr;\n"
                    "#define GENERATED_POINTER char* generated_expanded_pointer = nullptr\n")
GENERATED_UNIT = "#include \"build/Generated.h\"\n\nGENERATED_POINTER;\n"
GENERATED_CONTRACT_ERRORS = [("build/Generated.h", 5, "pre")]
UNCLOSED_GENERATED_HEADER = ("#include <foundation/Quarantine.h>\n"
                             "CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: a generated region with no end\")\n")
UNCLOSED_GENERATED_UNIT = "#include \"build/Unclosed.h\"\n"


@dataclass(frozen=True)
class Finding:
    """One line of a report."""

    kind: str
    file: str
    line: int
    entity: str


@dataclass(frozen=True)
class TableCase:
    """One rule table and the verdict that each of its two readers must give."""

    name: str
    line: int  # the line of the bad row in the table, or 0 when each reader must accept the table
    message: str  # the message of the error of the plugin, or "" for a table that each reader accepts
    text: str


def read_table_cases(path: Path) -> tuple[TableCase, ...]:
    """Read the cases of the corpus at PATH, in the order of the file.

    The head comment of the corpus gives its format.  Complexity: linear in
    the lines of the corpus.

    Raises:
        ValueError: When the corpus does not obey its format
    """
    lines = path.read_text(encoding="utf-8").split("\n")
    starts = [index for index, line in enumerate(lines) if line.startswith("case ")]
    cases: list[TableCase] = []
    for start, end in zip(starts, [*starts[1:], len(lines)], strict=True):
        name = lines[start][len("case "):].strip()
        verdict = lines[start + 1] if start + 1 < end else ""
        number, separator, message = verdict.removeprefix("refused ").partition(": ")
        table = lines[start + 2:end]
        while table and not table[-1]:
            table.pop()
        where = f"{path.name}:{start + 1}"
        if not name or any(case.name == name for case in cases):
            raise ValueError(f"{where}: a case needs a name that no other case has")
        if verdict == "accepted":
            number, message = "0", ""
        elif (not verdict.startswith("refused ") or not separator or not message
              or not (number.isascii() and number.isdigit() and 1 <= int(number) <= len(table))):
            raise ValueError(f"{where}: the line after `case NAME` is `accepted`, or `refused LINE: MESSAGE` with "
                             f"the number of a line of the table")
        if not table:
            raise ValueError(f"{where}: the case has no table")
        cases.append(TableCase(name, int(number), message, "".join(f"{row}\n" for row in table)))
    if not cases:
        raise ValueError(f"{path.name}: the corpus holds no case")
    return tuple(cases)


def white_space_cases() -> tuple[TableCase, ...]:
    """Return the tables of each white-space character that is not a space, a tab or a newline.

    Python str.isspace() gives the characters, so the plugin must refuse each
    character at which the Python reader could divide.  Each character gets a
    table that holds it between two words and a table that holds it in a
    reason, which each reader must refuse, and a table that holds it in a
    comment, which each reader must accept.  A table with the line ends
    '\\r\\n' is refused at its first row.  Complexity: linear in the code
    points of Unicode.
    """
    cases: list[TableCase] = []
    for code in range(0x110000):
        character = chr(code)
        if not character.isspace() or character in " \t\n":
            continue
        message = layer_rules.white_space_message(character)
        cases += [
            TableCase(f"U+{code:04X} between two words", 2, message,
                      f"quarantine plant_std.cpp\nquarantine{character}a/\n"),
            TableCase(f"U+{code:04X} in a reason", 1, message, f"admit std::move | a{character}cast\n"
                                                                "quarantine plant_std.cpp\n"),
            TableCase(f"U+{code:04X} in a comment", 0, "",
                      f"# a comment{character}with a white-space character\nquarantine plant_std.cpp\n"),
        ]
    cases.append(TableCase("the line ends \\r\\n", 1, layer_rules.white_space_message("\r"),
                           "quarantine plant_std.cpp\r\n# a comment\r\n"))
    return tuple(cases)


class Checker:
    """Compile fixtures with the quarantine plugin of the build.

    The configure step builds the plugin with the flags of the tree, and each
    compile of the build loads it, so the test loads the same file.  At most
    JOBS compiler processes of the test run at one time, and ctest gives the
    test JOBS processors.  compile and run_compiler are safe to call from many
    threads, because each compiler is its own process.
    """

    def __init__(self, cxx: str, plugin: Path, work: Path, build_dir: Path | None, jobs: int) -> None:
        if not plugin.is_file():
            raise RuntimeError(f"the plugin {plugin} does not exist.  Configure the build directory: the configure "
                               f"step builds the plugin")
        self.cxx = cxx
        self.work = work
        self.build_dir = build_dir
        self.plugin = plugin
        self.slots = threading.BoundedSemaphore(jobs)

    def run_compiler(self, command: list[str], cwd: Path | None = None, env: dict[str, str] | None = None,
                     timeout_s: float = proc.TOOL_S) -> proc.Completed:
        """Run one compiler COMMAND when a slot is free, in CWD, with the environment ENV and the limit TIMEOUT_S.

        Each compiler process of the test goes through this gate, so the
        processors that ctest gives the test bound the processes.

        Raises:
            proc.CannotStart: If the compiler cannot start
            proc.TimedOut: If the compiler runs past its limit
        """
        with self.slots:
            return proc.run(proc.Command(command, timeout_s=timeout_s, cwd=cwd, env=env))

    def compile(self, fixture: str, arguments: dict[str, str],
                stage: tuple[str, ...] = ("-S", "-o", os.devnull),
                extra: tuple[str, ...] = (), launcher: tuple[str, ...] = (),
                env: dict[str, str] | None = None, timeout_s: float = proc.TOOL_S) -> proc.Completed:
        """Compile one fixture with the plugin, the given plugin arguments, stage flags and extra flags.

        LAUNCHER goes in front of the compiler, as a compiler cache does, and
        ENV is the environment of the compile.  A compile that runs longer than
        TIMEOUT_S seconds stops, and its result has the exit status -9 and the
        error text "timeout".
        """
        command = [*launcher, self.cxx, "-std=c++26", "-I", str(HERE / "include"), "-I", str(REPO / "include"),
                   "-DCRUCIBLE_QUARANTINE_ACTIVE", *extra, f"-fplugin={self.plugin}"]
        command += [f"-fplugin-arg-{PLUGIN}-{key}={value}" for key, value in arguments.items()]
        command += ["-fdiagnostics-color=never", *stage, str(HERE / fixture)]
        try:
            return self.run_compiler(command, env=env, timeout_s=timeout_s)
        except proc.TimedOut as problem:
            # The raw wait status of an end by a signal is the number of the signal, so the exit is -9.
            return proc.Completed(problem.command, signal.SIGKILL, b"", b"timeout", 0.0, 0.0, 0.0, 0)


class Section:
    """The expectations of one part of the test, which runs at the same time as the other parts.

    A part writes only its own files under the work directory, and it keeps
    its verdict lines until main prints them in the order of the parts.
    """

    def __init__(self, checker: Checker) -> None:
        self.work = checker.work
        self.compile = checker.compile
        self.run_compiler = checker.run_compiler
        self.plugin = checker.plugin
        self.cxx = checker.cxx
        self.build_dir = checker.build_dir
        self.lines: list[str] = []
        self.failures: list[str] = []

    def expect(self, name: str, holds: bool, detail: str = "") -> None:
        """Record the verdict of one expectation."""
        self.lines.append(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            self.failures.append(f"{name}{': ' + detail if detail else ''}")

    def compile_all(self, calls: list[tuple[object, ...]]) -> list[proc.Completed]:
        """Run the compiles of ``calls``, each the positional arguments of compile, at the same time.

        Return the results in the order of the calls.
        """
        with ThreadPoolExecutor(max_workers=len(calls)) as pool:
            futures = [pool.submit(self.compile, *call) for call in calls]  # type: ignore[arg-type]
            return [future.result() for future in futures]


def read_reports(directory: Path) -> list[Finding]:
    """Read each report file in DIRECTORY."""
    findings: list[Finding] = []
    for path in sorted(directory.glob("*.quarantine")):
        for row in path.read_text(encoding="utf-8").splitlines():
            if not row.startswith("quarantine: "):
                continue
            kind, place, entity = row[len("quarantine: "):].split(" ", 2)
            file, line, _column = place.rsplit(":", 2)
            findings.append(Finding(kind, file, int(line), entity))
    return findings


def run(checker: Checker, rules: Path) -> list[Section]:
    """Run each part of the test at the same time, and return the parts in their order.

    The parts write disjoint files under the work directory, so no part
    reads what another part writes.
    """
    arguments = {"root": str(HERE), "mode": "report", "rules": str(TEST_RULES),
                 "out": str(checker.work / "reports")}
    parts: list[Callable[[Section], None]] = [
        lambda section: run_findings(section, arguments),
        lambda section: run_base_files(section, arguments),
        run_pragma_errors,
        lambda section: run_modes(section, arguments, rules),
        run_contract_rule,
        run_generated_files,
        run_include_rules,
        run_library_names,
        run_provenance,
        run_depth,
        run_known_gaps,
        run_restrictions,
        run_language,
        lambda section: run_tree_plants(section, rules),
        run_table_readers,
        run_section,
        run_dependencies,
        run_enforce_cache,
        run_build_flags,
    ]
    sections = [Section(checker) for _ in parts]
    with ThreadPoolExecutor(max_workers=len(parts)) as pool:
        for future in [pool.submit(part, section) for part, section in zip(parts, sections, strict=True)]:
            future.result()
    return sections


def run_findings(section: Section, arguments: dict[str, str]) -> None:
    """Compile the three fixtures in report mode, and judge each finding that must be present or absent."""
    for fixture in ("violations.cpp", "opt_out.cpp", "type_walks.cpp"):
        compiled = section.compile(fixture, arguments)
        section.expect(f"{fixture} compiles in report mode", compiled.exit == 0, compiled.err[-2000:])
    findings = read_reports(Path(arguments["out"]))

    for kind, file, line, text in PRESENT:
        # A text that starts with '=' must be the whole entity.
        holds = any(f.kind == kind and f.file == file and f.line == line
                    and (f.entity == text[1:] if text.startswith("=") else text in f.entity) for f in findings)
        section.expect(f"{kind} at {file}:{line} ({text})", holds)
    for file, line, kind, text in ABSENT:
        hits = [f for f in findings
                if f.file == file and (line is None or f.line == line) and (kind is None or f.kind == kind)
                and (text is None or text in f.entity)]
        where = f"{file}:{line}" if line is not None else file
        what = kind or "finding"
        section.expect(f"no {what}{' of ' + text if text else ''} at {where}", not hits, "; ".join(map(str, hits)))


def run_pragma_errors(section: Section) -> None:
    """Compile each fixture of a malformed region, and judge the error.

    The region macros also compile with no plugin: they expand to nothing
    when CRUCIBLE_QUARANTINE_ACTIVE is not defined.  With the define and no
    plugin, GCC warns about the unknown pragma, so the define never goes
    without the plugin.
    """
    for fixture, text in PRAGMA_ERRORS:
        compiled = section.compile(fixture, {"root": str(HERE), "mode": "report", "rules": str(TEST_RULES)})
        section.expect(f"{fixture} is an error", compiled.exit != 0 and text in compiled.err,
                       compiled.err[-2000:])
    plain = [section.cxx, "-std=c++26", "-I", str(HERE / "include"), "-I", str(REPO / "include"), "-Wall", "-Werror",
             "-fdiagnostics-color=never", "-fsyntax-only", str(HERE / "opt_out.cpp")]
    without = section.run_compiler(plain)
    section.expect("the region macros compile with no plugin", without.exit == 0, without.err[-2000:])
    leaked = section.run_compiler([*plain, "-DCRUCIBLE_QUARANTINE_ACTIVE"])
    section.expect("CRUCIBLE_QUARANTINE_ACTIVE with no plugin fails on the unknown pragma",
                   leaked.exit != 0 and "pragma" in leaked.err, leaked.err[-2000:])


def run_modes(section: Section, arguments: dict[str, str], rules: Path) -> None:
    """Judge the report mode without out=, the error mode, -fsyntax-only, -E and the rule table of the tree.

    The rule table of the tree classifies the paths of the tree, so its compile
    has the repository as the source root.
    """
    syntax_reports = section.work / "syntax-reports"
    preprocessed_reports = section.work / "preprocessed-reports"
    test_rules = {"rules": str(TEST_RULES)}
    noted, failed, opted, syntax, preprocessed, real, missing = section.compile_all([
        ("violations.cpp", {"root": str(HERE), "mode": "report", **test_rules}, ("-fsyntax-only",)),
        ("violations.cpp", {"root": str(HERE), "mode": "error", **test_rules}),
        ("opt_out.cpp", {"root": str(HERE), "mode": "error", **test_rules}),
        ("violations.cpp", {**arguments, "out": str(syntax_reports)}, ("-fsyntax-only",)),
        ("violations.cpp", {**arguments, "out": str(preprocessed_reports)}, ("-E", "-o", os.devnull)),
        ("opt_out.cpp", {"root": str(rules.resolve().parents[2]), "rules": str(rules)}),
        ("violations.cpp", {"root": str(HERE), "mode": "report"}),
    ])
    section.expect("report mode without out= and with no object gives notes and succeeds",
                   noted.exit == 0 and "note: quarantine: raw_new_delete new" in noted.err,
                   noted.err[-2000:])
    section.expect("error mode makes a finding an error",
                   failed.exit != 0 and "error: quarantine: c_library_call memcpy" in failed.err,
                   failed.err[-2000:])
    opted_lines = {int(row.split(":")[1]) for row in opted.err.splitlines()
                   if "error: quarantine:" in row and row.startswith(str(HERE / "opt_out.cpp") + ":")}
    section.expect("error mode does not stop an opted-out finding",
                   opted.exit != 0 and opted_lines == {8, 14}, opted.err[-2000:])

    syntax_findings = read_reports(syntax_reports) if syntax.exit == 0 else []
    section.expect("-fsyntax-only reports the namespace walk too",
                   any(f.kind == "std_object" and f.line == 15 for f in syntax_findings), syntax.err[-2000:])
    section.expect("-E writes no report", preprocessed.exit == 0 and not preprocessed_reports.exists(),
                   preprocessed.err[-2000:])

    section.expect("the rule table of the tree loads", real.exit == 0, real.err[-2000:])
    section.expect("the quarantine plugin needs a rule table",
                   missing.exit != 0 and "give the rule table" in missing.err, missing.err[-2000:])


def run_base_files(section: Section, arguments: dict[str, str]) -> None:
    """Compile each source file of the base with this directory as the root, and again with src/ as the root."""
    calls: list[tuple[object, ...]] = []
    for fixture in BASE_FIXTURES:
        # The plugin makes the last directory of out= and not its parents.
        name = Path(fixture).stem
        calls.append((fixture, {**arguments, "out": str(section.work / f"base-reports-{name}")}))
        moved = {**arguments, "root": str(HERE / "src"), "out": str(section.work / f"moved-reports-{name}")}
        calls.append((fixture, moved))
    results = iter(section.compile_all(calls))
    for fixture in BASE_FIXTURES:
        name = Path(fixture).stem
        base_reports = section.work / f"base-reports-{name}"
        compiled = next(results)
        found = read_reports(base_reports)
        section.expect(f"{fixture} is base code: the unit reports no finding",
                       compiled.exit == 0 and any(base_reports.glob("*.quarantine")) and not found,
                       compiled.err[-2000:] + "; ".join(map(str, found)))
        moved_reports = section.work / f"moved-reports-{name}"
        moved = next(results)
        moved_found = read_reports(moved_reports)
        relative = Path(fixture).relative_to("src").as_posix()
        for line, kind, text in BASE_FINDINGS:
            holds = moved.exit == 0 and any(f.file == relative and f.line == line and f.kind == kind
                                                  and text in f.entity for f in moved_found)
            section.expect(f"{kind} at {relative}:{line} with src/ as the root ({text})", holds,
                           moved.err[-2000:])


def contract_errors(stderr: str, root: Path = HERE) -> list[tuple[str, int, str]]:
    """Return each error of the contract rule in STDERR, with the path relative to ROOT."""
    errors: list[tuple[str, int, str]] = []
    for match in CONTRACT_ERROR.finditer(stderr):
        path = Path(match["path"])
        relative = path.relative_to(root).as_posix() if path.is_relative_to(root) else path.as_posix()
        errors.append((relative, int(match["line"]), match["specifier"]))
    return sorted(errors)


def run_generated_files(section: Section) -> None:
    """Compile a unit that includes a generated header in each mode, and compare what each mode gives.

    The build directory is in the scratch root in one compile, and outside it in
    another.  The two compiles name the generated header @build/Generated.h.
    A table with no row for @build/ refuses the generated header.
    """
    tree = section.work / "tree"
    build = tree / "build"
    outside_build = section.work / "outside-build"
    for directory in (build, outside_build):
        directory.mkdir(parents=True, exist_ok=True)
        (directory / "Generated.h").write_text(GENERATED_HEADER, encoding="utf-8")
        (directory / "Unclosed.h").write_text(UNCLOSED_GENERATED_HEADER, encoding="utf-8")
    unit = tree / "generated_unit.cpp"
    unit.write_text(GENERATED_UNIT, encoding="utf-8")
    unclosed = tree / "unclosed_unit.cpp"
    unclosed.write_text(UNCLOSED_GENERATED_UNIT, encoding="utf-8")
    places = {"root": str(tree), "build": str(build)}
    test_rules = {"rules": str(TEST_RULES)}
    generated_reports = section.work / "generated-reports"
    outside_reports = section.work / "generated-outside-reports"
    runs = (
        ("mode=error", {**places, "mode": "error", **test_rules}),
        ("mode=report", {**places, "mode": "report", "out": str(generated_reports), **test_rules}),
    )
    for name, arguments in runs:
        compiled = section.compile(str(unit), arguments, extra=("-fcontracts",))
        found = contract_errors(compiled.err, tree)
        section.expect(f"{name}: a region of a generated file opts out its specifier, and the other one is an error",
                       compiled.exit != 0 and found == GENERATED_CONTRACT_ERRORS,
                       f"errors {found}; " + compiled.err[-2000:])
        compiled = section.compile(str(unclosed), arguments)
        section.expect(f"{name}: an unclosed region of a generated file is an error",
                       compiled.exit != 0 and "region has no" in compiled.err, compiled.err[-2000:])
    # No out= here: a plugin that accepts the directory must not replace the
    # report of the unit.
    missing = section.compile(str(unit), {"root": str(tree), "build": str(tree / "missing"), **test_rules})
    section.expect("a build directory that does not exist is an error",
                   missing.exit != 0 and "does not exist" in missing.err, missing.err[-2000:])
    # The second unit includes the header of the build directory outside the
    # root through -iquote.
    outside_unit = tree / "outside_generated_unit.cpp"
    outside_unit.write_text("#include \"outside-build/Generated.h\"\n", encoding="utf-8")
    outside = section.compile(str(outside_unit), {"root": str(tree), "build": str(outside_build), "mode": "report",
                                                  "out": str(outside_reports), **test_rules},
                              extra=("-fcontracts", "-iquote", str(section.work)))
    unrowed_rules = section.work / "rules-without-build.txt"
    unrowed_rules.write_text("".join(line for line in TEST_RULES.read_text(encoding="utf-8").splitlines(True)
                                     if line.strip() != "quarantine @build/"), encoding="utf-8")
    unrowed = section.compile(str(unit), {**places, "mode": "report", "rules": str(unrowed_rules)},
                              extra=("-fcontracts",))
    section.expect("a generated file that no row of the table holds is an error",
                   unrowed.exit != 0 and "no quarantine row of the rule table holds '@build/Generated.h'"
                   in ascii_quotes(unrowed.err), unrowed.err[-2000:])
    found = read_reports(generated_reports)
    section.expect("the region of the generated file turns the specifier on line 3 into opted_out",
                   any(f.kind == "opted_out" and f.file == "@build/Generated.h" and f.line == 3
                       and f.entity == "contract_specifier pre" for f in found), "; ".join(map(str, found)))
    section.expect("the row quarantine @build/ quarantines the generated file under its path in the build directory",
                   any(f.kind == "raw_pointer_object" and f.file == "@build/Generated.h" and f.line == 6
                       for f in found), "; ".join(map(str, found)))
    section.expect("the report holds the region of the generated file with its reason",
                   any(f.kind == "region" and f.file == "@build/Generated.h" and f.line == 2
                       and f.entity == "PROBE: a generated header that the test needs" for f in found),
                   "; ".join(map(str, found)))
    section.expect("a place in a generated macro is a place of the generated file",
                   any(f.kind == "raw_pointer_object" and f.file == "@build/Generated.h" and f.line == 7
                       for f in found), "; ".join(map(str, found)))
    outside_found = read_reports(outside_reports)
    section.expect("a build directory outside the root names its generated file @build/Generated.h too",
                   any(f.kind == "raw_pointer_object" and f.file == "@build/Generated.h" and f.line == 6
                       for f in outside_found),
                   outside.err[-2000:] + "; ".join(map(str, outside_found)))


def run_contract_rule(section: Section) -> None:
    """Compile contracts.cpp in each mode and with -fsyntax-only, and compare the errors with CONTRACT_ERRORS."""
    outside = section.work / "outside"
    outside.mkdir(exist_ok=True)
    (outside / "Outside.h").write_text(OUTSIDE_HEADER, encoding="utf-8")
    (outside / "OutsideRegion.h").write_text(OUTSIDE_REGION_HEADER, encoding="utf-8")
    test_rules = {"rules": str(TEST_RULES)}
    compiled = section.compile("region_outside_root.cpp", {"root": str(HERE), "mode": "error", **test_rules},
                               extra=("-I", str(outside)))
    section.expect("a region of a header outside the root is no error",
                   compiled.exit == 0 and "region" not in compiled.err, compiled.err[-2000:])
    extra = ("-fcontracts", "-I", str(outside))
    expected = sorted(CONTRACT_ERRORS)
    contract_reports = section.work / "contract-reports"
    error_mode = {"root": str(HERE), "mode": "error", **test_rules}
    runs = (
        ("mode=error", error_mode, ("-S", "-o", os.devnull)),
        ("mode=report", {"root": str(HERE), "mode": "report", "out": str(contract_reports), **test_rules},
         ("-S", "-o", os.devnull)),
        ("mode=report with -fsyntax-only", {"root": str(HERE), "mode": "report", **test_rules}, ("-fsyntax-only",)),
    )
    results = [section.compile("contracts.cpp", arguments, stage, extra) for _, arguments, stage in runs]
    for (name, _, _), compiled in zip(runs, results, strict=True):
        found = contract_errors(compiled.err)
        missing = sorted(set(expected) - set(found))
        unexpected = [error for error in found if error not in expected or found.count(error) > 1]
        section.expect(f"{name}: each contract specifier gives one error, and nothing else does",
                       compiled.exit != 0 and not missing and not unexpected,
                       f"missing {missing}, unexpected {sorted(set(unexpected))}")
    opted = [f for f in read_reports(contract_reports)
             if f.kind == "opted_out" and f.file == "contracts.cpp" and f.line == 73]
    section.expect("the opt-out region turns the specifier on line 73 into opted_out",
                   any(f.entity == "contract_specifier pre" for f in opted), "; ".join(map(str, opted)))

    named = results[0]
    section.expect("the error names CRUCIBLE_PRE, CRUCIBLE_POST and the reasons",
                   all(text in named.err for text in ("CRUCIBLE_PRE(condition)", "foundation/contracts/Pre.h",
                                                         "CRUCIBLE_POST(result, condition)",
                                                         "foundation/contracts/Post.h", "header unit",
                                                         "precompiled header", "constant evaluation")),
                   named.err[-2000:])
    unknown = section.compile("violations.cpp", {"root": str(HERE), "mode": "contracts"})
    section.expect("an unknown mode is an error",
                   unknown.exit != 0 and "the modes are report and error" in unknown.err,
                   unknown.err[-2000:])


def quarantine_errors(stderr: str, kind: str) -> list[str]:
    """Return each error of the quarantine plugin of KIND in STDERR."""
    return [row for row in stderr.splitlines() if f"error: quarantine: {kind} " in row]


def run_include_rules(section: Section) -> None:
    """Compile each planted file in error mode with no plant and with each plant, and judge the findings.

    The part also judges the upward include that a directive with no entered
    file makes, the enforce row of the test table and a file that no row
    holds.
    """
    error_mode = {"root": str(HERE), "mode": "error", "rules": str(TEST_RULES)}
    report_mode = {"root": str(HERE), "mode": "report", "rules": str(TEST_RULES)}
    calls: list[tuple[object, ...]] = []
    for fixture in sorted({fixture for _, fixture, _, _ in PLANTS}):
        calls.append((fixture, error_mode))
    for macro, fixture, _, _ in PLANTS:
        calls.append((fixture, error_mode, ("-S", "-o", os.devnull), (f"-D{macro}",)))
    calls += [
        ("src/fixy/SocketDoor.cpp", error_mode),
        ("low_includes_high.cpp", error_mode),
        ("low_includes_high.cpp", error_mode, ("-S", "-o", os.devnull), ("-DHIGH_FIRST",)),
        ("enforced.cpp", report_mode),
        ("unclassified.cpp", report_mode),
    ]
    results = iter(section.compile_all(calls))
    for fixture in sorted({fixture for _, fixture, _, _ in PLANTS}):
        clean = next(results)
        section.expect(f"{fixture} with no plant compiles in error mode", clean.exit == 0, clean.err[-2000:])
    for macro, fixture, kind, text in PLANTS:
        planted = next(results)
        errors = quarantine_errors(planted.err, kind)
        section.expect(f"{fixture} with {macro} fails with one {kind} ({text})",
                       planted.exit != 0 and len(errors) == 1 and text in errors[0], planted.err[-2000:])
    door = next(results)
    section.expect("the door of <sys/socket.h> includes it with no finding", door.exit == 0, door.err[-2000:])
    for name in ("an include that enters High.h", "an include that #pragma once stops"):
        compiled = next(results)
        errors = quarantine_errors(compiled.err, "upward_include")
        section.expect(f"{name}: fixy/Low.h has one upward include",
                       compiled.exit != 0 and len(errors) == 1 and "include/fixy/Low.h:8:" in errors[0]
                       and "include/fixy/high/High.h (the layer high) from the layer low" in errors[0],
                       compiled.err[-2000:])
    enforced = next(results)
    section.expect("an enforce row with the mode error makes a finding an error in report mode",
                   enforced.exit != 0 and len(quarantine_errors(enforced.err, "raw_pointer_object")) == 1,
                   enforced.err[-2000:])
    unclassified = next(results)
    section.expect("a file that no row of the table holds is an error",
                   unclassified.exit != 0 and "no layer row and no quarantine row of the rule table holds "
                   "'unclassified.cpp'" in unclassified.err.replace("‘", "'").replace("’", "'"),
                   unclassified.err[-2000:])


# (fixture, extra flags, line, kind, the whole entity of the one finding of that
# kind on the line).  An object of a library enumeration is a std_object.
LIBRARY_NAMES = (
    ("nonclass.cpp", (), 13, "std_entity", "std::size_t"),
    ("nonclass.cpp", (), 14, "std_object", "std::byte (std::byte)"),
    ("nonclass.cpp", (), 15, "std_entity", "std::nullptr_t"),
    ("nonclass.cpp", (), 16, "std_object", "std::align_val_t (std::align_val_t)"),
    ("nonclass.cpp", (), 17, "std_entity", "std::tuple_element_t"),
    ("nonclass.cpp", (), 19, "std_entity", "std::size_t"),
    ("nonclass.cpp", (), 25, "std_entity", "std::tuple_size_v"),
    ("header_exact.cpp", (), 8, "std_entity", "std::experimental::nonesuch"),
    ("meta_info.cpp", ("-freflection",), 7, "std_entity", "std::meta::info"),
    ("gaps.cpp", (), 14, "std_object", "std::byte (fixy::GapsByte)"),
    ("gaps.cpp", (), 15, "c_library_call", "optind"),
    ("gaps.cpp", (), 17, "c_library_call", "__errno_location"),
    ("gaps.cpp", (), 19, "compiler_builtin", "__builtin_trap"),
    ("gaps.cpp", (), 21, "c_library_call", "memcpy (__builtin_memcpy)"),
    ("gaps.cpp", (), 23, "compiler_builtin", "__atomic_load_n"),
    ("gaps.cpp", (), 25, "compiler_builtin", "__builtin_bit_cast"),
    ("gaps.cpp", (), 35, "compiler_builtin", "__builtin_c23_va_start"),
    ("gaps.cpp", (), 36, "compiler_builtin", "__builtin_va_arg"),
    ("gaps.cpp", (), 37, "compiler_builtin", "__builtin_va_end"),
    ("gaps.cpp", (), 41, "inline_asm", "asm"),
    ("gaps.cpp", (), 45, "assert_expansion", "assert"),
    ("gaps.cpp", ("-DNDEBUG",), 45, "assert_expansion", "assert"),
    ("pending.cpp", (), 11, "replace_pending:std::bit_cast", "std::bit_cast"),
    ("pending.cpp", (), 13, "std_entity", "std::bit_cast"),
    ("pending.cpp", (), 17, "replace_pending:std::bit_cast", "std::bit_cast"),
    ("pending.cpp", (), 22, "std_entity", "std::bit_cast"),
    ("pending.cpp", (), 25, "replace_pending:std::source_location", "std::source_location"),
    ("pending.cpp", (), 26, "replace_pending:std::source_location", "std::source_location::line"),
    ("pending.cpp", (), 30, "replace_pending:<limits>", "std::numeric_limits::max"),
    ("pending.cpp", (), 34, "replace_pending:<limits>", "std::numeric_limits::max"),
    ("pending.cpp", (), 37, "replace_pending:std::unreachable", "std::unreachable"),
    ("pending.cpp", (), 42, "replace_pending:std::bit_cast", "std::bit_cast"),
    ("foreign_symbols.cpp", (), 7, "foreign_symbol", "abort (C linkage)"),
    ("foreign_symbols.cpp", (), 8, "foreign_symbol", "write (C linkage)"),
    ("foreign_symbols.cpp", (), 9, "foreign_symbol", "foreign_copy (asm label memcpy)"),
    ("foreign_symbols.cpp", (), 10, "foreign_symbol", "foreign_exit (attribute weakref)"),
    ("foreign_symbols.cpp", (), 13, "c_library_call", "abort (C linkage)"),
    ("foreign_symbols.cpp", (), 14, "c_library_call", "write (C linkage)"),
    ("foreign_symbols.cpp", (), 15, "c_library_call", "foreign_copy (asm label memcpy)"),
    ("foreign_symbols.cpp", (), 16, "c_library_call", "foreign_exit (attribute weakref)"),
    ("foreign_symbols.cpp", (), 22, "inline_asm", "asm register held"),
    ("allocation.cpp", (), 8, "raw_new_delete", "declaration of operator new"),
    ("allocation.cpp", (), 12, "raw_new_delete", "declaration of operator delete"),
    ("outside_user.cpp", (), 9, "c_library_object", "OutsideType (OutsideType)"),
    ("outside_user.cpp", (), 10, "c_library_call", "outside_value"),
    ("unevaluated.cpp", (), 8, "std_entity", "std::basic_string"),
    ("unevaluated.cpp", (), 12, "std_entity", "std::vector"),
    ("unevaluated.cpp", (), 17, "std_entity", "std::vector"),
    ("unevaluated.cpp", (), 22, "std_entity", "std::vector"),
    ("unevaluated.cpp", (), 27, "std_entity", "std::basic_string"),
    ("unevaluated.cpp", (), 32, "std_entity", "std::vector"),
    ("unevaluated.cpp", (), 37, "std_entity", "std::vector"),
    ("unevaluated.cpp", (), 42, "std_entity", "std::vector"),
    ("unevaluated.cpp", (), 48, "std_entity", "std::vector"),
    ("unevaluated.cpp", (), 50, "std_entity", "std::vector"),
    ("unevaluated.cpp", (), 55, "std_entity", "std::vector"),
    ("unevaluated.cpp", (), 58, "std_entity", "std::vector"),
    ("unevaluated.cpp", (), 62, "std_entity", "std::vector"),
    ("deduced.cpp", (), 13, "std_object", "std::vector (deduced)"),
    ("deduced.cpp", (), 19, "std_object", "std::vector (deduced)"),
    ("deduced.cpp", (), 33, "std_object", "std::vector (std::vector<int, std::allocator<int> >)"),
    ("temporaries.cpp", (), 22, "std_entity", "std::vector"),
    ("temporaries.cpp", (), 23, "std_entity", "std::basic_string"),
    ("temporaries.cpp", (), 24, "std_entity", "std::vector"),
    ("usings.cpp", (), 12, "std_entity", "std::basic_string::size"),
    ("usings.cpp", (), 16, "std_entity", "std::swap"),
    ("usings.cpp", (), 22, "std_entity", "std::swap"),
    ("usings.cpp", (), 26, "std_entity", "std::pmr"),
    ("usings.cpp", (), 29, "c_library_call", "memcpy"),
)
# The header of a library outside the source root that no system flag marks.
# run_library_names writes it in its scratch directory, and each compile of the
# part names that directory with -I.
OUTSIDE_LIBRARY_HEADER = "#pragma once\nstruct OutsideType {\n    int value = 0;\n};\ninline int outside_value() { return 1; }\n"
# (fixture, line): no finding of any kind there.
LIBRARY_NAMES_ABSENT = (
    ("nonclass.cpp", 18),  # <type_traits> admits std::remove_cvref_t
    ("nonclass.cpp", 20),  # a project alias of std::size_t names no library entity
    ("nonclass.cpp", 23),  # the entry std::tuple_size admits std::tuple_size<T>::value
    ("header_exact.cpp", 7),  # <type_traits> admits std::is_same
    ("gaps.cpp", 29),  # the front end calls the atomic load of the guard of the static local
    ("foreign_symbols.cpp", 11),  # the unit defines the function of C language linkage later
    ("foreign_symbols.cpp", 17),  # a call before the definition of the unit
    ("foreign_symbols.cpp", 19),  # the definition
    ("deduced.cpp", 25),  # a template parameter gives the type, and the call spells no library name
    ("deduced.cpp", 34),  # the call deduces the template argument from an object with a finding of its own
    ("temporaries.cpp", 25),  # an implicit list initialization of a parameter names no class
    ("temporaries.cpp", 26),  # an implicit conversion of a variable names no class
    ("temporaries.cpp", 27),  # an implicit conversion of a literal names no class
    ("temporaries.cpp", 28),  # a temporary of a class of the source
)
# (fixture, extra flags, line, kind): no finding of that kind there.
LIBRARY_KINDS_ABSENT = (
    ("gaps.cpp", (), 45, "c_library_call"),  # assert_expansion stands for __assert_fail
    ("gaps.cpp", (), 45, "compiler_builtin"),  # and for __builtin_FILE and __builtin_LINE
    ("pending.cpp", (), 11, "std_entity"),  # plain-result admits a result of unsigned long
    ("pending.cpp", (), 13, "replace_pending:std::bit_cast"),  # a refused use is no pending use
    ("pending.cpp", (), 22, "replace_pending:std::bit_cast"),  # a dependent result fails closed
    ("pending.cpp", (), 42, "std_entity"),  # a pointer to a dependent type holds no bool
    ("allocation.cpp", (), 11, "raw_new_delete"),  # a deleted allocation function allocates nothing
)


def run_library_names(section: Section) -> None:
    """Compile the fixtures of the library names that are not classes, of the exact headers and of the gaps.

    gaps.cpp holds the paths around the rules: a typedef of fixy, a variable
    of the C library, builtins, va_arg and asm.  pending.cpp holds the uses
    that an admit row with `until` admits, and the uses of std::bit_cast that
    the restriction plain-result refuses.  outside_user.cpp uses a library
    header outside the root with no system flag.  Each compile writes its
    report to a directory of its own, and the part compares each report with
    LIBRARY_NAMES and LIBRARY_NAMES_ABSENT.
    """
    outside = section.work / "outside-library"
    outside.mkdir(exist_ok=True)
    (outside / "OutsideLibrary.h").write_text(OUTSIDE_LIBRARY_HEADER, encoding="utf-8")
    fixtures = sorted({(fixture, extra) for fixture, extra, _, _, _ in LIBRARY_NAMES})
    calls: list[tuple[object, ...]] = []
    for index, (fixture, extra) in enumerate(fixtures):
        out = section.work / f"library-names-{index}"
        calls.append((fixture, {"root": str(HERE), "mode": "report", "rules": str(TEST_RULES), "out": str(out)},
                      ("-S", "-o", os.devnull), (*extra, "-I", str(outside))))
    # include_exact.cpp: the second directive of fixy/GapsUser.h enters no
    # file, and the plugin resolves it as libcpp does.
    calls.append(("include_exact.cpp", {"root": str(HERE), "mode": "error", "rules": str(TEST_RULES)}))
    results = section.compile_all(calls)
    findings: dict[tuple[str, tuple[str, ...]], list[Finding]] = {}
    for index, ((fixture, extra), compiled) in enumerate(zip(fixtures, results, strict=False)):
        section.expect(f"{fixture} {' '.join(extra)} compiles in report mode".replace("  ", " "),
                       compiled.exit == 0, compiled.err[-2000:])
        findings[(fixture, extra)] = read_reports(section.work / f"library-names-{index}")
    for fixture, extra, line, kind, entity in LIBRARY_NAMES:
        hits = [f for f in findings[(fixture, extra)] if f.kind == kind and f.file == fixture and f.line == line]
        section.expect(f"{kind} {entity} at {fixture}:{line} {' '.join(extra)}".rstrip(),
                       [f.entity for f in hits] == [entity], "; ".join(map(str, hits)))
    for fixture, line in LIBRARY_NAMES_ABSENT:
        hits = [f for f in findings[(fixture, ())] if f.file == fixture and f.line == line]
        section.expect(f"no finding at {fixture}:{line}", not hits, "; ".join(map(str, hits)))
    for fixture, extra, line, kind in LIBRARY_KINDS_ABSENT:
        hits = [f for f in findings[(fixture, extra)] if f.file == fixture and f.line == line and f.kind == kind]
        section.expect(f"no {kind} at {fixture}:{line}", not hits, "; ".join(map(str, hits)))
    exact = results[-1]
    section.expect("a directive that enters no file resolves through the search chain, not by the end of a path",
                   exact.exit == 0 and "upward_include" not in exact.err, exact.err[-2000:])


# (fixture, extra flags, a text that the error holds, with ASCII quotes): each
# directive that changes the file, the line or the system flag of the code
# after it is an error in report mode.
PROVENANCE_ERRORS = (
    ("line_directive.cpp", (), "'line_directive.cpp' holds a '#line' directive"),
    ("line_marker.cpp", (), "a line marker of 'line_marker.cpp' enters 'fake_system.h'"),
    ("system_marked.cpp", (), "'gapsother/marked/SystemMarked.h' holds a '#line' directive"),
)


def ascii_quotes(text: str) -> str:
    """Return TEXT with the typographic quotes of GCC in a UTF-8 locale as ASCII quotes."""
    return text.replace("‘", "'").replace("’", "'")
# (fixture, extra flags, line, kind, a text in the entity, or None for no
# finding of the kind on the line): a file of the root stays a file of the
# root under -isystem, and a library namespace that a quarantined file opens
# again is walked.
PROVENANCE_FINDINGS = (
    ("isystem_user.cpp", ("-isystem", str(HERE / "gapsother/sys")), "gapsother/sys/Marked.h", 8,
     "raw_pointer_object", "int*"),
    ("isystem_user.cpp", ("-isystem", str(HERE / "gapsother/sys")), "isystem_user.cpp", 7, "std_entity", None),
    ("reopened_std.cpp", (), "reopened_std.cpp", 14, "raw_pointer_object", "int*"),
    ("reopened_std.cpp", (), "reopened_std.cpp", 19, "std_object", "std::vector"),
    ("reopened_std.cpp", (), "reopened_std.cpp", 24, "raw_pointer_object", "int*"),
)


def run_provenance(section: Section) -> None:
    """Compile each fixture of a directive that changes a place, and each fixture of a library file that is not one.

    Each directive of PROVENANCE_ERRORS fails the compile in report mode.  The
    compiles of PROVENANCE_FINDINGS write their reports to directories of their
    own, and the part compares each report with the rows.
    """
    report_mode = {"root": str(HERE), "mode": "report", "rules": str(TEST_RULES)}
    calls: list[tuple[object, ...]] = [(fixture, report_mode, ("-S", "-o", os.devnull), extra)
                                       for fixture, extra, _ in PROVENANCE_ERRORS]
    reported = sorted({(fixture, extra) for fixture, extra, _, _, _, _ in PROVENANCE_FINDINGS})
    for index, (fixture, extra) in enumerate(reported):
        out = section.work / f"provenance-{index}"
        calls.append((fixture, {**report_mode, "out": str(out)}, ("-S", "-o", os.devnull), extra))
    results = section.compile_all(calls)
    for (fixture, _, text), compiled in zip(PROVENANCE_ERRORS, results, strict=False):
        section.expect(f"{fixture} fails in report mode: {text}",
                       compiled.exit != 0 and text in ascii_quotes(compiled.err), compiled.err[-2000:])
    findings: dict[tuple[str, tuple[str, ...]], list[Finding]] = {}
    for index, ((fixture, extra), compiled) in enumerate(zip(reported, results[len(PROVENANCE_ERRORS):], strict=True)):
        section.expect(f"{fixture} {' '.join(extra)} compiles in report mode".replace("  ", " "),
                       compiled.exit == 0, compiled.err[-2000:])
        findings[(fixture, extra)] = read_reports(section.work / f"provenance-{index}")
    for fixture, extra, file, line, kind, text in PROVENANCE_FINDINGS:
        hits = [f for f in findings[(fixture, extra)] if f.file == file and f.line == line and f.kind == kind]
        holds = not hits if text is None else any(text in f.entity for f in hits)
        what = f"no {kind}" if text is None else f"{kind} {text}"
        section.expect(f"{what} at {file}:{line}", holds, "; ".join(map(str, hits)))


# (fixture, table, line, kind, a text in the entity, or None for a compile with
# no finding): a walk reads a deep type and a deep macro expansion to its end,
# and a walk over a shared type tree stays linear.  Each compile stops after
# DEPTH_TIMEOUT seconds, and the plugin needs much less than a second.
DEPTH_CASES = (
    ("deep_nesting.cpp", TEST_RULES, 25, "std_object", "std::vector"),
    ("deep_nesting.cpp", TEST_RULES, 98, "c_library_call", "__errno_location"),
    ("struct_tree.cpp", TEST_RULES, 62, "replace_pending:std::bit_cast", "std::bit_cast"),
    ("deep_language_user.cpp", HERE / "language.txt", None, None, None),
)
DEPTH_TIMEOUT = 60.0


def run_depth(section: Section) -> None:
    """Compile each fixture of DEPTH_CASES in report mode with a time limit, and judge its findings."""
    fixtures = sorted({(fixture, table) for fixture, table, _, _, _ in DEPTH_CASES})
    calls: list[tuple[object, ...]] = []
    for index, (fixture, table) in enumerate(fixtures):
        arguments = {"root": str(HERE), "mode": "report", "rules": str(table),
                     "out": str(section.work / f"depth-{index}")}
        calls.append((fixture, arguments, ("-S", "-o", os.devnull), (), (), None, DEPTH_TIMEOUT))
    results = section.compile_all(calls)
    findings: dict[str, list[Finding]] = {}
    for index, ((fixture, _), compiled) in enumerate(zip(fixtures, results, strict=True)):
        section.expect(f"{fixture} compiles in report mode in less than {DEPTH_TIMEOUT:.0f} s",
                       compiled.exit == 0, compiled.err[-2000:])
        findings[fixture] = read_reports(section.work / f"depth-{index}")
    for fixture, _, line, kind, text in DEPTH_CASES:
        if line is None:
            section.expect(f"{fixture} has no finding", not findings[fixture], "; ".join(map(str, findings[fixture])))
            continue
        hits = [f for f in findings[fixture] if f.file == fixture and f.line == line and f.kind == kind]
        section.expect(f"{kind} {text} at {fixture}:{line}", any(text in f.entity for f in hits),
                       "; ".join(map(str, hits)))


# The known gaps of the plugin: (fixture, table, file, line, reason).  The line
# names a library entity, and the plugin gives no finding there.  A row fails
# when the plugin gives a finding on its line, so a change that closes a gap
# removes its row and adds the finding to the expectations.  The list only
# becomes shorter.
KNOWN_GAPS = (
    ("known_gaps.cpp", TEST_RULES, "known_gaps.cpp", 11,
     "outside a template, the front end folds the call of strlen in the initializer, the alignof and the noexcept "
     "to constants while it parses, and keeps no tree of the names"),
    ("known_gaps.cpp", TEST_RULES, "known_gaps.cpp", 13,
     "a using-declaration of namespace scope is a binding of the namespace, with no tree that has a location"),
    ("known_gaps.cpp", TEST_RULES, "known_gaps.cpp", 14,
     "a using-directive of namespace scope is a list of the namespace, with no tree that has a location"),
)


def run_known_gaps(section: Section) -> None:
    """Compile each fixture of KNOWN_GAPS in report mode, and judge that each pinned line has no finding."""
    fixtures = sorted({(fixture, table) for fixture, table, _, _, _ in KNOWN_GAPS})
    calls: list[tuple[object, ...]] = []
    for index, (fixture, table) in enumerate(fixtures):
        arguments = {"root": str(HERE), "mode": "report", "rules": str(table),
                     "out": str(section.work / f"gaps-{index}")}
        calls.append((fixture, arguments, ("-S", "-o", os.devnull)))
    findings: dict[str, list[Finding]] = {}
    for index, ((fixture, _), compiled) in enumerate(zip(fixtures, section.compile_all(calls), strict=True)):
        section.expect(f"{fixture} compiles in report mode", compiled.exit == 0, compiled.err[-2000:])
        findings[fixture] = read_reports(section.work / f"gaps-{index}")
    for fixture, _, file, line, reason in KNOWN_GAPS:
        hits = [f for f in findings[fixture] if f.file == file and f.line == line]
        section.expect(f"the known gap at {file}:{line} stays open: {reason}", not hits,
                       "the gap is closed; remove its row and add the finding to the expectations: "
                       + "; ".join(map(str, hits)))


# (the macro of the plant of a restriction, the kind, a text in the entity)
RESTRICTION_PLANTS = (
    ("PLANT_MOVE_ALGORITHM", "std_entity", "std::move"),
    ("PLANT_RANGES_SWAP", "std_entity", "std::ranges::swap"),
    ("PLANT_LIST_OBJECT", "std_object", "std::initializer_list"),
    ("PLANT_TO_INTEGER", "std_entity", "std::to_integer"),
    ("PLANT_BYTE_OPERATOR", "std_entity", "std::operator<<"),
    ("PLANT_ALIGNMENT", "std_object", "std::align_val_t"),
    ("PLANT_BIT_CAST_BOOL", "std_entity", "std::bit_cast"),
    ("PLANT_BIT_CAST_ENUM", "std_entity", "std::bit_cast"),
    ("PLANT_BIT_CAST_MEMBER", "std_entity", "std::bit_cast"),
    ("PLANT_BIT_CAST_BASE", "std_entity", "std::bit_cast"),
    ("PLANT_BIT_CAST_ARRAY", "std_entity", "std::bit_cast"),
    ("PLANT_BIT_CAST_DEPENDENT", "std_entity", "std::bit_cast"),
)


def run_restrictions(section: Section) -> None:
    """Compile restricted.cpp in error mode with no plant, with each plant, and with _GLIBCXX_DEBUG.

    The table restricted.txt holds the restrictions of the audit verdicts.
    The file with no plant passes, each plant fails with a finding of its
    restriction, and a unit that defines _GLIBCXX_DEBUG fails because of the
    row `admit <meta> unless _GLIBCXX_DEBUG`.
    """
    error_mode = {"root": str(HERE), "mode": "error", "rules": str(HERE / "restricted.txt")}
    calls: list[tuple[object, ...]] = [("restricted.cpp", error_mode)]
    calls += [("restricted.cpp", error_mode, ("-S", "-o", os.devnull), (f"-D{macro}",))
              for macro, _, _ in RESTRICTION_PLANTS]
    calls.append(("restricted.cpp", error_mode, ("-S", "-o", os.devnull), ("-D_GLIBCXX_DEBUG",)))
    results = iter(section.compile_all(calls))
    clean = next(results)
    section.expect("restricted.cpp with no plant compiles in error mode", clean.exit == 0, clean.err[-2000:])
    for macro, kind, text in RESTRICTION_PLANTS:
        planted = next(results)
        section.expect(f"restricted.cpp with {macro} fails with a {kind} of {text}",
                       planted.exit != 0 and any(text in row for row in quarantine_errors(planted.err, kind)),
                       planted.err[-2000:])
    debug = next(results)
    section.expect("a unit that defines _GLIBCXX_DEBUG fails, because <meta> is admitted unless it does",
                   debug.exit != 0 and "the unit defines _GLIBCXX_DEBUG" in debug.err, debug.err[-2000:])


# (the macro of a plant of fixy/high/Language.h, the kind of its error, a text
# in the entity)
LANGUAGE_PLANTS = (
    ("PLANT_LANGUAGE_HIGH_HEADER", "layer_header",
     "<cstdint> in the layer high, a file of the language with the allowance of the layer low"),
    ("PLANT_LANGUAGE_SYSTEM_HEADER", "layer_header", "<unistd.h> in the layer high"),
    ("PLANT_LANGUAGE_DOOR_HEADER", "door_header", "<sys/socket.h> has the door src/fixy/SocketDoor.cpp"),
    ("PLANT_LANGUAGE_MEMBER", "std_object", "std::vector"),
    ("PLANT_LANGUAGE_LOCAL", "std_object", "std::vector"),
    ("PLANT_LANGUAGE_PARAMETER", "std_object", "std::source_location"),
    ("PLANT_LANGUAGE_ALIAS", "std_entity", "std::vector"),
    ("PLANT_LANGUAGE_TYPEDEF", "std_entity", "std::ptrdiff_t"),
    ("PLANT_LANGUAGE_BASE", "std_entity", "std::vector"),
    ("PLANT_LANGUAGE_TEMPLATE", "std_object", "std::vector"),
    ("PLANT_LANGUAGE_STD_CALL", "std_entity", "std::swap"),
    ("PLANT_LANGUAGE_RUNTIME_VECTOR", "std_entity", "std::vector::size"),
)

# (the line of fixy/high/LanguageOther.h, the kind of its error, a text in the
# entity): a call in a file of the language, and a name that a row with `in`
# admits only in a different file of the language.
LANGUAGE_OTHER_ERRORS = (
    (14, "c_library_call", "memset"),
    (16, "std_entity", "std::nothrow"),
)


def run_language(section: Section) -> None:
    """Compile the two includers of the files of the language in report mode with language.txt.

    The included fixy/high/Language.h is a file of the language.  With no
    plant it has no error and one finding: the use of std::nothrow, which a
    row with `until` and `in` admits there.  Its raw pointer, its array, the
    name that a row with `in` admits there and the members of a std::vector of
    std::meta::info that a row with `consteval-only` admits give no finding.
    Each plant is an error in report mode, in the file of the language.  Each
    body of fixy/high/LanguageOther.h is an error too.
    """
    report_mode = {"root": str(HERE), "mode": "report", "rules": str(HERE / "language.txt")}
    reflection = ("-freflection",)
    out = section.work / "language"
    calls: list[tuple[object, ...]] = [("language_user.cpp", report_mode, ("-S", "-o", os.devnull), reflection),
                                       ("language_user.cpp", {**report_mode, "out": str(out)},
                                        ("-S", "-o", os.devnull), reflection),
                                       ("language_other_user.cpp", report_mode, ("-S", "-o", os.devnull))]
    calls += [("language_user.cpp", report_mode, ("-S", "-o", os.devnull), (*reflection, f"-D{macro}"))
              for macro, _, _ in LANGUAGE_PLANTS]
    results = iter(section.compile_all(calls))
    clean = next(results)
    section.expect("a file of the language with no plant compiles with no error and no note",
                   clean.exit == 0 and "quarantine:" not in clean.err, clean.err[-2000:])
    reported = next(results)
    findings = read_reports(out) if reported.exit == 0 else []
    expected = [Finding("replace_pending:std::nothrow", "include/fixy/high/Language.h", 52, "std::nothrow")]
    section.expect("the use of std::nothrow in fixy/high/Language.h is its one finding, and no error",
                   reported.exit == 0 and findings == expected,
                   reported.err[-2000:] + "; ".join(map(str, findings)))
    other = next(results)
    for line, kind, text in LANGUAGE_OTHER_ERRORS:
        errors = [row for row in quarantine_errors(other.err, kind)
                  if f"include/fixy/high/LanguageOther.h:{line}:" in row]
        section.expect(f"fixy/high/LanguageOther.h:{line} fails in report mode with a {kind} of {text}",
                       other.exit != 0 and len(errors) == 1 and text in errors[0], other.err[-2000:])
    for macro, kind, text in LANGUAGE_PLANTS:
        planted = next(results)
        errors = [row for row in quarantine_errors(planted.err, kind) if "include/fixy/high/Language.h:" in row]
        section.expect(f"a file of the language with {macro} fails in report mode with a {kind} of {text}",
                       planted.exit != 0 and len(errors) == 1 and text in errors[0], planted.err[-2000:])


def table_refusals(stderr: str, table: Path) -> list[tuple[int, str]]:
    """Return the line and the message of each error of the plugin about the rule table TABLE in STDERR."""
    prefix = f"error: quarantine: {table}:"
    refusals: list[tuple[int, str]] = []
    for row in stderr.splitlines():
        at = row.find(prefix)
        if at >= 0:
            number, _, message = row[at + len(prefix):].partition(": ")
            refusals.append((int(number) if number.isascii() and number.isdigit() else 0, message))
    return refusals


def python_verdict(table: Path) -> str:
    """Return the error of ruletable.py for the rule table TABLE, or "accepted" when it reads the table.

    The reader must refuse a table only with TableError, so the text of each
    other exception names its type.
    """
    try:
        layer_rules.load(table)
    except layer_rules.TableError as failure:
        return str(failure)
    except Exception as failure:  # each other exception is a defect of the reader, and the test reports it
        return f"{type(failure).__name__}: {failure}"
    return "accepted"


def run_table_readers(section: Section) -> None:
    """Give the plugin and ruletable.py each table of the corpus and each white-space table.

    The plugin must refuse each refused table when it loads, with the error of
    the case at the line of the case and with no other error.  ruletable.py
    must refuse the table at the same line.  Each reader must accept each
    accepted table, and read the test table and the tree table.
    """
    tables = section.work / "tables"
    tables.mkdir(exist_ok=True)
    cases = read_table_cases(TABLE_CORPUS) + white_space_cases()
    paths = [tables / f"table-{index}.txt" for index in range(len(cases))]
    calls: list[tuple[object, ...]] = []
    for case, path in zip(cases, paths, strict=True):
        path.write_bytes(case.text.encode("utf-8"))
        calls.append(("plant_std.cpp", {"root": str(HERE), "mode": "report", "rules": str(path)}))
    for case, path, compiled in zip(cases, paths, section.compile_all(calls), strict=True):
        verdict = python_verdict(path)
        if case.line == 0:
            section.expect(f"the plugin accepts {case.name}",
                           compiled.exit == 0 and "error:" not in compiled.err, compiled.err[-2000:])
            section.expect(f"ruletable.py accepts {case.name}", verdict == "accepted", verdict)
            continue
        errors = [row for row in compiled.err.splitlines() if "error: quarantine: " in row]
        section.expect(f"the plugin refuses {case.name} at line {case.line} with the error of the case, and with "
                       f"no other error",
                       compiled.exit != 0 and "failed to initialize plugin" in compiled.err
                       and table_refusals(compiled.err, path) == [(case.line, case.message)] and len(errors) == 1,
                       compiled.err[-2000:])
        section.expect(f"ruletable.py refuses {case.name} at line {case.line}",
                       verdict.startswith(f"{path.name}:{case.line}: "), verdict)
    until_table = tables / "until.txt"
    until_table.write_text("admit <type_traits> | traits\nadmit std::move until Scalar | a cast\n"
                           "quarantine plant_std.cpp\n", encoding="utf-8")
    until = section.compile("plant_std.cpp", {"root": str(HERE), "mode": "error", "rules": str(until_table)})
    section.expect("the mode error refuses a use that an admit row with `until` admits",
                   until.exit != 0 and quarantine_errors(until.err, "replace_pending:std::move")
                   and not quarantine_errors(until.err, "std_entity"), until.err[-2000:])
    for name, path in (("the test table", TEST_RULES), ("the tree table", layer_rules.TABLE)):
        try:
            layer_rules.load(path)
            is_read = True
        except layer_rules.TableError as failure:
            is_read = False
            section.expect(f"ruletable.py reads {name}", False, str(failure))
        if is_read:
            section.expect(f"ruletable.py reads {name}", True)


# The stamps of the section part, and the planted row of its second table.
SECTION_STAMP = "5ec7105ec7105ec7"
SECTION_NEW_STAMP = "0afe0afe0afe0afe"
SECTION_ADMIT_ROW = "admit std::swap | a planted row of the cache cases\n"
SECTION_SWAP = re.compile(r"quarantine: std_entity violations\.cpp:44:\d+ std::swap")


def section_of(path: Path) -> tuple[str, list[str]] | None:
    """Return the stamp and the finding lines of the section of one object, or None when it has no section."""
    data = quarantine_sections.read_section(path) if path.is_file() else None
    if data is None:
        return None
    read = quarantine_sections.parse_section(data, str(path))
    return read.stamp, [finding.text() for finding in read.findings]


def ccache_hits(environment: dict[str, str]) -> int:
    """Return the hits that the statistics of the cache of ENVIRONMENT count."""
    printed = proc.run(proc.Command(["ccache", "--print-stats"], timeout_s=proc.QUICK_S, env=environment)).out
    counts = dict(line.split("\t", 1) for line in printed.splitlines() if "\t" in line)
    return sum(int(counts.get(name, "0")) for name in ("direct_cache_hit", "preprocessed_cache_hit"))


def run_section(section: Section) -> None:
    """Compile with -c, and judge the section of each object, also through ccache.

    The compiles with no cache run at the same time.  The cases through ccache
    make two chains, each with a cache of its own in the work directory and
    the ignore_options of utils/tools/quarantine/Quarantine.cmake.  The two
    chains run at the same time as the other compiles.
    """
    work = section.work / "section"
    work.mkdir(exist_ok=True)
    reports = work / "reports"
    table = {"root": str(HERE), "mode": "report", "rules": str(TEST_RULES)}
    stamped = {**table, "stamp": SECTION_STAMP}
    swap_table = work / "admit-swap.txt"
    swap_table.write_text(TEST_RULES.read_text(encoding="utf-8") + SECTION_ADMIT_ROW, encoding="utf-8")
    swapped = {**table, "rules": str(swap_table), "stamp": SECTION_NEW_STAMP}
    ignored = f"-fplugin=* -fplugin-arg-{PLUGIN}-root=* -fplugin-arg-{PLUGIN}-build=* -fplugin-arg-{PLUGIN}-rules=*"
    launcher = ("ccache", f"ignore_options={ignored}")
    blind = ("ccache", f"ignore_options={ignored} -fplugin-arg-{PLUGIN}-stamp=*")

    def chain(name: str, steps: list[tuple[str, dict[str, str], tuple[str, ...]]]) -> list[tuple[int, object]]:
        """Compile each step through ccache, in order, and return its hits and its exit status and section."""
        (work / f"{name}.conf").write_text("", encoding="utf-8")
        environment = proc.environment({"CCACHE_DIR": str(work / name),
                                        "CCACHE_CONFIGPATH": str(work / f"{name}.conf")})
        results: list[tuple[int, object]] = []
        for output, arguments, used in steps:
            before = ccache_hits(environment)
            result = section.compile("violations.cpp", arguments, ("-c", "-o", str(work / f"{output}.o")),
                                     launcher=used, env=environment)
            results.append((ccache_hits(environment) - before, (result.exit, section_of(work / f"{output}.o"))))
        return results

    has_ccache = shutil.which("ccache") is not None
    with ThreadPoolExecutor(max_workers=3) as pool:
        plain = pool.submit(section.compile_all, [
            ("violations.cpp", {**stamped, "out": str(reports)}, ("-c", "-o", str(work / "out.o"))),
            ("violations.cpp", stamped, ("-c", "-o", str(work / "quiet.o"))),
            ("opt_out.cpp", stamped, ("-c", "-o", str(work / "opted.o"))),
            ("violations.cpp", stamped, ("-fsyntax-only", "-o", str(work / "syntax.o"))),
            ("violations.cpp", swapped, ("-c", "-o", str(work / "real-swap.o"))),
        ])
        chains = [pool.submit(chain, "ccache", [("stored", stamped, launcher), ("hit", stamped, launcher),
                                                ("new", swapped, launcher),
                                                ("stale", {**swapped, "stamp": SECTION_STAMP}, launcher)]),
                  pool.submit(chain, "ccache-blind", [("blind-stored", stamped, blind),
                                                      ("blind", {**table, "stamp": "b1b1b1b1b1b1b1b1"}, blind)])
                  ] if has_ccache else []
        compiled, quiet, opted, syntax, real_swap = plain.result()
        keyed_results, unkeyed_results = [future.result() for future in chains] if chains else ([], [])

    found = section_of(work / "out.o")
    report = [row for path in sorted(reports.glob("*.quarantine"))
              for row in path.read_text(encoding="utf-8").splitlines() if row.startswith("quarantine: ")]
    section.expect("the section of the object holds the stamp and the lines of the report file",
                   compiled.exit == 0 and found is not None and found[0] == SECTION_STAMP
                   and found[1] == report and len(report) > 10, compiled.err[-2000:] + f"; {found}")
    section.expect("a unit that writes an object gives no note, and its section holds the findings",
                   quiet.exit == 0 and "note: quarantine:" not in quiet.err
                   and section_of(work / "quiet.o") == found, quiet.err[-2000:])
    opted_found = section_of(work / "opted.o")
    section.expect("the section holds an opted_out finding",
                   opted.exit == 0 and opted_found is not None
                   and any(line.startswith("quarantine: opted_out opt_out.cpp:11:") for line in opted_found[1]),
                   opted.err[-2000:])
    section.expect("-fsyntax-only writes no object", syntax.exit == 0 and not (work / "syntax.o").exists(),
                   syntax.err[-2000:])
    if not has_ccache:
        section.lines.append("  skip the cases through ccache: no ccache on PATH")
        return
    (stored_hits, stored), (hit_hits, hit), (new_hits, new), (stale_hits, stale) = keyed_results
    section.expect("a hit of the cache gives the section of a real compile",
                   found is not None and stored_hits == 0 and hit_hits == 1 and stored == hit == (0, found),
                   f"{stored_hits} {hit_hits}")
    swap_found = section_of(work / "real-swap.o")
    has_swap = [found is not None and any(SECTION_SWAP.fullmatch(line) for line in found[1]),
                swap_found is not None and any(SECTION_SWAP.fullmatch(line) for line in swap_found[1])]
    section.expect("a changed rule table with a new stamp misses, and gives the findings of a real compile",
                   real_swap.exit == 0 and has_swap == [True, False] and new_hits == 0
                   and new == (0, swap_found), f"{has_swap} {new_hits}")
    section.expect("negative control: a changed table with the old stamp hits, and gives the old findings",
                   found is not None and stale_hits == 1 and stale == (0, found), f"{stale_hits} {stale}")
    _, (blind_hits, blind_found) = unkeyed_results
    section.expect("negative control: a key without the stamp gives a section with the stamp of another command",
                   found is not None and blind_hits == 1 and blind_found == (0, found),
                   f"{blind_hits} {blind_found}")


def write_tree(tree: Path, files: dict[str, str]) -> None:
    """Write each file of FILES, a map from a path relative to TREE to its text."""
    for relative, text in files.items():
        (tree / relative).parent.mkdir(parents=True, exist_ok=True)
        (tree / relative).write_text(text, encoding="utf-8")


# The tree of the dependency part.  fresh.h is written after the stamps, so it
# has no mode stamp, as a file that the tree got after the last configure.
DEPENDENCY_TREE = {
    "rules.txt": "quarantine app/\nenforce app/ report\n",
    "app/Header.h": "#pragma once\ninline int* header_pointer = nullptr;\n",
    "app/Clean.h": "#pragma once\ninline int clean_value = 1;\n",
    "app/finding.cpp": "#include \"Header.h\"\n#include \"Clean.h\"\n#include \"fresh.h\"\n"
                       "int* finding_pointer = nullptr;\n",
    "app/opted.cpp": "#include <foundation/Quarantine.h>\n"
                     "CRUCIBLE_I_KNOW_WHAT_IM_DOING(\"PROBE: a test of the dependencies of the plugin\")\n"
                     "int* opted_pointer = nullptr;\nCRUCIBLE_END_I_KNOW_WHAT_IM_DOING\n",
}


def run_dependencies(section: Section) -> None:
    """Compile units of a scratch tree with a dependency file, and judge the mode stamps in it."""
    tree = section.work / "dependency-tree"
    write_tree(tree, DEPENDENCY_TREE)
    stamps = tree / "stamps"
    quarantine_stamps.write_stamps(tree, tree / "rules.txt", stamps, None)
    write_tree(tree, {"app/fresh.h": "#pragma once\ninline int* fresh_pointer = nullptr;\n"})
    arguments = {"root": str(tree), "rules": str(tree / "rules.txt"), "stamps": str(stamps), "mode": "report"}

    def dependencies(unit: str, stage: tuple[str, ...]) -> tuple[proc.Completed, str]:
        depfile = section.work / f"dependency-{Path(unit).stem}-{stage[0][1:]}.d"
        compiled = section.compile(str(tree / unit), arguments, stage, ("-MD", "-MF", str(depfile)))
        return compiled, depfile.read_text(encoding="utf-8") if depfile.is_file() else ""

    modes = stamps / "modes" / "app"
    found, text = dependencies("app/finding.cpp", ("-c", "-o", str(section.work / "dependency-finding.o")))
    section.expect("a unit names the mode stamp of each file with a finding, and enforce.txt for a file with none",
                   found.exit == 0 and str(modes / "finding.cpp") in text and str(modes / "Header.h") in text
                   and str(stamps / "enforce.txt") in text, found.err[-2000:] + text)
    section.expect("a file with no finding adds no mode stamp", str(modes / "Clean.h") not in text, text)
    opted, text = dependencies("app/opted.cpp", ("-c", "-o", str(section.work / "dependency-opted.o")))
    section.expect("an opted-out finding adds no dependency",
                   opted.exit == 0 and str(stamps) not in text, opted.err[-2000:] + text)
    syntax, text = dependencies("app/finding.cpp", ("-fsyntax-only",))
    section.expect("-fsyntax-only writes the dependency file before the report, so it holds no mode stamp",
                   syntax.exit == 0 and str(stamps) not in text, syntax.err[-2000:] + text)


def run_enforce_cache(section: Section) -> None:
    """Change the mode of a file between two compiles through ccache, and judge the second compile.

    The keyed chain has the options of Quarantine.cmake: ccache ignores the
    paths of the plugin arguments and hashes enforce.txt.  The blind chain does
    not hash enforce.txt, and it is the negative control.
    """
    if shutil.which("ccache") is None:
        section.lines.append("  skip the enforce modes through ccache: no ccache on PATH")
        return
    ignored = " ".join(f"-fplugin-arg-{PLUGIN}-{key}=*" for key in ("root", "build", "rules", "stamps"))

    def chain(name: str, is_keyed: bool) -> list[tuple[int, proc.Completed]]:
        tree = section.work / f"{name}-tree"
        write_tree(tree, {"app/unit.cpp": "int* cached_pointer = nullptr;\n"})
        rules, stamps = tree / "rules.txt", tree / "stamps"
        (section.work / f"{name}.conf").write_text("", encoding="utf-8")
        environment = proc.environment({"CCACHE_DIR": str(section.work / name),
                                        "CCACHE_CONFIGPATH": str(section.work / f"{name}.conf")})
        launcher = ("ccache", f"ignore_options=-fplugin=* {ignored}",
                    *((f"extra_files_to_hash={stamps / 'enforce.txt'}",) if is_keyed else ()))
        arguments = {"root": str(tree), "rules": str(rules), "stamps": str(stamps), "mode": "report",
                     "stamp": "e4f0e4f0e4f0e4f0"}
        results = []
        for mode in ("report", "error", "report"):
            rules.write_text(f"quarantine app/\nenforce app/unit.cpp {mode}\n", encoding="utf-8")
            quarantine_stamps.write_stamps(tree, rules, stamps, None)
            before = ccache_hits(environment)
            compiled = section.compile(str(tree / "app/unit.cpp"), arguments,
                                       ("-c", "-o", str(section.work / f"{name}.o")), launcher=launcher,
                                       env=environment)
            results.append((ccache_hits(environment) - before, compiled))
        return results

    with ThreadPoolExecutor(max_workers=2) as pool:
        keyed_future, blind_future = pool.submit(chain, "enforce-keyed", True), pool.submit(chain, "enforce-blind", False)
        keyed, blind = keyed_future.result(), blind_future.result()
    (_, stored), (error_hits, failed), (_, back) = keyed
    section.expect("through ccache, a unit whose file changes to the mode error misses and fails",
                   stored.exit == 0 and error_hits == 0 and failed.exit != 0
                   and "error: quarantine: raw_pointer_object" in failed.err and back.exit == 0,
                   f"{error_hits} " + failed.err[-2000:])
    _, (blind_hits, skipped), _ = blind
    section.expect("negative control: a cache that does not hash enforce.txt hits, and skips the error",
                   blind_hits == 1 and skipped.exit == 0, f"{blind_hits} " + skipped.err[-2000:])


def build_command(build_dir: Path) -> tuple[list[str], Path, str] | None:
    """Return the compile command of a real object of BUILD_DIR, its directory and its source.

    The command is the first one that loads the quarantine plugin, and
    src/foundation/ContractHandler.cpp when the database holds it.
    """
    try:
        compiles = compdb.entries(build_dir)
    except compdb.MissingDatabase:
        return None
    entries = [entry for entry in compiles if "-fplugin=" in entry.command]
    if not entries:
        return None
    entry = next((entry for entry in entries if entry.file.endswith("src/foundation/ContractHandler.cpp")),
                 entries[0])
    return list(entry.argv()), Path(entry.directory), entry.file


# A scratch root whose paths the rule table of the tree classifies: a base file
# of the layer F1_algebra, a header of the higher layer X1_wrappers and a
# quarantined file.  Each plant macro adds one use that a row kind of the table
# refuses.
TREE_PLANT_FILES = {
    "include/fixy/PlantHigh.h": "#pragma once\ninline int plant_high = 1;\n",
    "src/foundation/Plant.cpp": ("#if defined(PLANT_LAYER_HEADER)\n#include <cmath>\n#endif\n"
                                 "#if defined(PLANT_DOOR_HEADER)\n#include <sys/socket.h>\n#endif\n"
                                 "#if defined(PLANT_UPWARD_INCLUDE)\n#include <fixy/PlantHigh.h>\n#endif\n"
                                 "int plant_base_value() { return 1; }\n"),
    "test/Plant.cpp": ("#include <utility>\n"
                       "void plant_quarantined(int& left, int& right) {\n"
                       "#if defined(PLANT_STD_NAME)\n    std::swap(left, right);\n#endif\n"
                       "    left = std::move(right);\n}\n"),
}
# (the macro of the plant, the planted file, the kind, the whole entity): one
# plant for each row kind of the rule table of the tree.
TREE_PLANTS = (
    ("PLANT_LAYER_HEADER", "src/foundation/Plant.cpp", "layer_header", "<cmath> in the layer F1_algebra"),
    ("PLANT_DOOR_HEADER", "src/foundation/Plant.cpp", "door_header",
     "<sys/socket.h> has the door include/fixy/os/Socket.h"),
    ("PLANT_UPWARD_INCLUDE", "src/foundation/Plant.cpp", "upward_include",
     "include/fixy/PlantHigh.h (the layer X1_wrappers) from the layer F1_algebra"),
    ("PLANT_STD_NAME", "test/Plant.cpp", "std_entity", "std::swap"),
)


def run_tree_plants(section: Section, rules: Path) -> None:
    """Compile a plant of each row kind against the rule table of the tree, in report mode and in error mode.

    In report mode the section of the object holds the one finding of the
    plant, and the file with no plant has no finding of that kind.  In error
    mode the plant fails the compile, and the file with no plant passes.
    """
    tree = section.work / "tree-plants"
    write_tree(tree, TREE_PLANT_FILES)
    for mode in ("report", "error"):
        arguments = {"root": str(tree), "mode": mode, "rules": str(rules)}
        cases = [(fixture, None) for fixture in sorted({fixture for _, fixture, _, _ in TREE_PLANTS})]
        cases += [(fixture, macro) for macro, fixture, _, _ in TREE_PLANTS]
        calls: list[tuple[object, ...]] = []
        for index, (fixture, macro) in enumerate(cases):
            stage = ("-c", "-o", str(section.work / f"tree-plant-{mode}-{index}.o")) if mode == "report" \
                else ("-S", "-o", os.devnull)
            calls.append((str(tree / fixture), arguments, stage,
                          ("-I", str(tree / "include"), *((f"-D{macro}",) if macro else ()))))
        results = dict(zip(cases, section.compile_all(calls), strict=True))
        objects = {case: section.work / f"tree-plant-{mode}-{index}.o" for index, case in enumerate(cases)}
        for macro, fixture, kind, entity in TREE_PLANTS:
            planted, clean = results[(fixture, macro)], results[(fixture, None)]
            if mode == "report":
                rows = [line for line in (section_of(objects[(fixture, macro)]) or ("", []))[1]
                        if line.startswith(f"quarantine: {kind} {fixture}:")]
                clean_rows = [line for line in (section_of(objects[(fixture, None)]) or ("", []))[1]
                              if line.startswith(f"quarantine: {kind} {fixture}:")]
                holds = (planted.exit == 0 and clean.exit == 0 and len(rows) == 1
                         and rows[0].endswith(f" {entity}") and not clean_rows)
                detail = f"{rows} {clean_rows} " + planted.err[-1000:]
            else:
                errors = quarantine_errors(planted.err, kind)
                holds = planted.exit != 0 and len(errors) == 1 and entity in errors[0] and clean.exit == 0
                detail = planted.err[-1000:] + clean.err[-1000:]
            section.expect(f"the tree table, mode {mode}: {macro} in {fixture} gives one {kind} ({entity})", holds,
                           detail)


def run_build_flags(section: Section) -> None:
    """Compile build_plant.cpp with the compile command of a real object of the build directory.

    The command keeps each flag of the preset.  An enforce row with the mode
    error for the plant fails the compile, and the table of the tree, which
    gives the plant the mode report, does not.
    """
    found = build_command(section.build_dir) if section.build_dir is not None else None
    if found is None:
        section.lines.append("  skip the flags of the build: no compile command with the plugin in the build directory")
        return
    command, directory, source = found
    plant = HERE / "build_plant.cpp"
    rules_option = f"-fplugin-arg-{PLUGIN}-rules="
    tree_table = next((Path(arg[len(rules_option):]) for arg in command if arg.startswith(rules_option)), None)
    root_option = f"-fplugin-arg-{PLUGIN}-root="
    root = next((Path(arg[len(root_option):]) for arg in command if arg.startswith(root_option)), None)
    if tree_table is None or root is None:
        section.expect("the compile command names the rule table and the root", False, " ".join(command))
        return
    error_table = section.work / "build-error-rules.txt"
    error_table.write_text(tree_table.read_text(encoding="utf-8")
                           + f"enforce {plant.relative_to(root).as_posix()} error\n", encoding="utf-8")

    def compile_plant(table: Path) -> proc.Completed:
        argv: list[str] = []
        skip_next = False
        for arg in command:
            if skip_next:
                skip_next = False
            elif arg in ("-o", "-MF", "-MT", "-MQ"):
                skip_next = True
            elif arg in ("-MD", "-MMD") or arg == source:
                pass
            elif arg.startswith(rules_option):
                argv.append(f"{rules_option}{table}")
            else:
                argv.append(arg)
        argv += ["-fdiagnostics-color=never", "-o", str(section.work / f"build-plant-{table.stem}.o"), str(plant)]
        return section.run_compiler(argv, cwd=directory)

    with ThreadPoolExecutor(max_workers=2) as pool:
        failed, passed = pool.map(compile_plant, (error_table, tree_table))
    section.expect("with the flags of the build, an enforce row with the mode error fails the compile of the plant",
                   failed.exit != 0 and "error: quarantine: raw_pointer_object" in failed.err,
                   failed.err[-2000:])
    section.expect("with the flags of the build and the table of the tree, the plant compiles",
                   passed.exit == 0, passed.err[-2000:])


def main(argv: list[str]) -> int:
    """Parse the arguments, and run every case with the plugin of the build.

    Args:
        argv: The words after the rule ID

    Returns:
        0 when each expectation holds, 1 when one fails, 2 on a usage error or a plugin that does not exist
    """
    parser = argparse.ArgumentParser(prog="guard check --rule quarantine-plugin",
                                     description=__doc__.split("\n", 1)[0])
    parser.add_argument("--cxx", required=True, help="the compiler that loads the plugin")
    parser.add_argument("--plugin", required=True, type=Path, help="the quarantine plugin that the configure built")
    parser.add_argument("--rules", required=True, type=Path, help="the rule table of the tree")
    parser.add_argument("--jobs", required=True, type=int,
                        help="the most compiler processes at one time: the PROCESSORS of the ctest test")
    parser.add_argument("--build-dir", type=Path, help="a build directory with a compile database")
    args = parser.parse_args(argv)
    if args.jobs < 1:
        parser.error(f"--jobs is {args.jobs}, and it must be 1 or more")
    with tempfile.TemporaryDirectory(prefix="quarantine-plugin-") as scratch:
        try:
            checker = Checker(args.cxx, args.plugin, Path(scratch), args.build_dir, args.jobs)
        except RuntimeError as error:
            print(f"guard check --rule quarantine-plugin: {error}", file=sys.stderr)
            return 2
        sections = run(checker, args.rules)
    for section in sections:
        print("\n".join(section.lines))
    failures = [failure for section in sections for failure in section.failures]
    if failures:
        print(f"check_plugin: {len(failures)} expectation(s) failed:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("check_plugin: every expectation holds.")
    return 0
