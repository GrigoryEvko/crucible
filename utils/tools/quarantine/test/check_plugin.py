#!/usr/bin/env python3
"""check_plugin — the quarantine plugin finds each class of finding, only where the location rule puts it.

The test builds the quarantine plugin from its source with the flags that
CMake gives, compiles each fixture of this directory with the plugin loaded,
and compares what the plugin reports with the expectations below.  This
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
is an error.  The plugin and utils/scripts/layer_rules.py get the same
malformed tables, and each one must refuse each table.

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
command, which utils/scripts/check-quarantine-ratchet.py refuses.

A generated file of the build directory follows one rule: the contract rule
and the opt-out regions apply to it, and the quarantine rule does not.  The
test writes a source root with a build directory in its scratch directory,
and compiles a unit of that root in each mode.

A type walk and a walk of a macro expansion read a deep nesting to its end, so
a library type behind 70 template wrappers and a C library token behind 70
macros stay findings.  A type tree that shares its subtrees costs one visit
for each type, also in a file of the language.  Each such compile has a time
limit.

A #line directive, a line marker and #pragma GCC system_header in a file of
the root each fail the compile.  A file of the root that -isystem marks stays
a file of the root, and a library namespace that a quarantined file opens
again gets the walk of each other namespace.

The plugin builds first.  Then the parts of the test run at the same time:
the findings, the base files, the malformed regions, the modes, the contract
rule, the generated files, the include rules, the library names, the
provenance, the restrictions, the readers of the table, the section, the
dependencies, the enforce modes through ccache and the flags of the build.
Each part writes its own files, and main prints the verdicts of the parts in
that order.

usage: check_plugin.py --cxx CXX --source QUARANTINE.cpp --rules TABLE [--build-dir BUILD]
                       -- BUILD_FLAGS...

Exit 0 when each expectation holds, 1 when one fails, 2 on a usage error or a
plugin that does not build.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
# The repository.  Its include/foundation/Quarantine.h gives the macros of a
# region, from a file outside the source root of the test.
REPO = HERE.parents[3]
TEST_RULES = HERE / "rules.txt"
PLUGIN = "crucible_quarantine"

sys.path.insert(0, str(HERE.parents[2] / "scripts"))
import layer_rules  # noqa: E402
import quarantine_sections  # noqa: E402
import quarantine_stamps  # noqa: E402

# (the macro of the plant, the planted file, the kind, a text in the entity)
PLANTS = (
    ("PLANT_LAYER_HEADER", "src/fixy/Plants.cpp", "layer_header", "<cmath> in the layer low"),
    ("PLANT_DOOR_HEADER", "src/fixy/Plants.cpp", "door_header", "<sys/socket.h> has the door src/fixy/SocketDoor.cpp"),
    ("PLANT_UPWARD_INCLUDE", "src/fixy/Plants.cpp", "upward_include",
     "include/fixy/high/High.h (the layer high) from the layer low"),
    ("PLANT_STD_NAME", "plant_std.cpp", "std_entity", "std::swap"),
)

# Tables that do not obey the format.  The plugin and layer_rules.py must each
# refuse each one.
MALFORMED_TABLES = (
    ("an unknown row kind", "bogus x/\n"),
    ("a rank that is not digits", "layer low x include/\n"),
    ("a rank of two layers", "layer one 0 a/\nlayer two 0 b/\n"),
    ("a layer with two rows", "layer one 0 a/\nlayer one 1 b/\n"),
    ("a path in a layer row and a quarantine row", "layer one 0 a/\nquarantine a/\n"),
    ("an allow row of no layer", "allow none <cstdint>\n"),
    ("a header with no angle brackets", "layer one 0 a/\nallow one cstdint\n"),
    ("a door whose owner is a directory", "door <x.h> a/\n"),
    ("a header with two doors", "door <x.h> a.cpp\ndoor <x.h> b.cpp\n"),
    ("an admit row with no reason", "admit std::move\n"),
    ("an admit row with `until` and no family", "admit std::move until | a cast\n"),
    ("an admit row with a word that is no restriction", "admit std::move after Scalar | a cast\n"),
    ("a restriction two times", "admit std::move arity 1 arity 1 | a cast\n"),
    ("an arity that is not digits", "admit std::move arity one | a cast\n"),
    ("an arity of a header", "admit <utility> arity 1 | a cast\n"),
    ("`concepts` of a name", "admit std::move concepts | a cast\n"),
    ("`parameters` of a header", "admit <initializer_list> parameters | a list\n"),
    ("`plain-result` of a header", "admit <bit> plain-result | the bits\n"),
    ("`plain-result` two times", "admit std::bit_cast plain-result plain-result | the bits\n"),
    ("`unless` with no identifier", "admit <meta> unless 1DEBUG | reflection\n"),
    ("`in` with no path", "layer one 0 a/\nadmit std::move in | a cast\n"),
    ("an `in` path outside the base", "layer one 0 a/\nquarantine b/\nadmit std::move in b/ | a cast\n"),
    ("an absolute path", "quarantine /a/\n"),
    ("a path with a parent component", "quarantine a/../b/\n"),
    ("a path with an empty component", "quarantine a//\n"),
    ("an unknown enforce mode", "enforce a/ fatal\n"),
    ("a path with two enforce rows", "enforce a/ error\nenforce a/ report\n"),
    ("a language row with no path", "layer one 0 a/\nlanguage one\n"),
    ("a language row of no layer", "layer one 0 a/\nlanguage none a/x.h\n"),
    ("a language path outside the base", "layer one 0 a/\nquarantine b/\nlanguage one b/x.h\n"),
    ("a path in two language rows", "layer one 0 a/\nlanguage one a/x.h\nlanguage one a/x.h\n"),
    ("a reason with no row kind", "| a reason\n"),
)

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
# 5 stays an error.  The pointer on line 6 is no finding, because a generated
# file is not quarantined.  The macro on line 7 spells a pointer, and the
# place of that finding falls through to the unit that expands the macro.
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


class Checker:
    """Build the plugin one time, then compile fixtures with it.

    compile is safe to call from many threads, because each compile is its own
    process.
    """

    def __init__(self, cxx: str, source: Path, flags: list[str], work: Path, build_dir: Path | None) -> None:
        self.cxx = cxx
        self.work = work
        self.build_dir = build_dir
        self.plugin = work / f"{PLUGIN}.so"
        build = subprocess.run([cxx, *flags, "-o", str(self.plugin), str(source)], capture_output=True, text=True)
        if build.returncode != 0:
            raise RuntimeError(f"the plugin {PLUGIN} did not build:\n{build.stderr}")

    def compile(self, fixture: str, arguments: dict[str, str],
                stage: tuple[str, ...] = ("-S", "-o", os.devnull),
                extra: tuple[str, ...] = (), launcher: tuple[str, ...] = (),
                env: dict[str, str] | None = None, timeout: float | None = None) -> subprocess.CompletedProcess[str]:
        """Compile one fixture with the plugin, the given plugin arguments, stage flags and extra flags.

        LAUNCHER goes in front of the compiler, as a compiler cache does, and
        ENV is the environment of the compile.  A compile that runs longer than
        TIMEOUT seconds stops, and its result has the exit status -9 and the
        error text "timeout".
        """
        command = [*launcher, self.cxx, "-std=c++26", "-I", str(HERE / "include"), "-I", str(REPO / "include"),
                   "-DCRUCIBLE_QUARANTINE_ACTIVE", *extra, f"-fplugin={self.plugin}"]
        command += [f"-fplugin-arg-{PLUGIN}-{key}={value}" for key, value in arguments.items()]
        command += ["-fdiagnostics-color=never", *stage, str(HERE / fixture)]
        try:
            return subprocess.run(command, capture_output=True, text=True, env=env, timeout=timeout)
        except subprocess.TimeoutExpired:
            return subprocess.CompletedProcess(command, -9, "", "timeout")


class Section:
    """The expectations of one part of the test, which runs at the same time as the other parts.

    A part writes only its own files under the work directory, and it keeps
    its verdict lines until main prints them in the order of the parts.
    """

    def __init__(self, checker: Checker) -> None:
        self.work = checker.work
        self.compile = checker.compile
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

    def compile_all(self, calls: list[tuple[object, ...]]) -> list[subprocess.CompletedProcess[str]]:
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
        section.expect(f"{fixture} compiles in report mode", compiled.returncode == 0, compiled.stderr[-2000:])
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
        section.expect(f"{fixture} is an error", compiled.returncode != 0 and text in compiled.stderr,
                       compiled.stderr[-2000:])
    plain = [section.cxx, "-std=c++26", "-I", str(HERE / "include"), "-I", str(REPO / "include"), "-Wall", "-Werror",
             "-fdiagnostics-color=never", "-fsyntax-only", str(HERE / "opt_out.cpp")]
    without = subprocess.run(plain, capture_output=True, text=True)
    section.expect("the region macros compile with no plugin", without.returncode == 0, without.stderr[-2000:])
    leaked = subprocess.run([*plain, "-DCRUCIBLE_QUARANTINE_ACTIVE"], capture_output=True, text=True)
    section.expect("CRUCIBLE_QUARANTINE_ACTIVE with no plugin fails on the unknown pragma",
                   leaked.returncode != 0 and "pragma" in leaked.stderr, leaked.stderr[-2000:])


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
                   noted.returncode == 0 and "note: quarantine: raw_new_delete new" in noted.stderr,
                   noted.stderr[-2000:])
    section.expect("error mode makes a finding an error",
                   failed.returncode != 0 and "error: quarantine: c_library_call memcpy" in failed.stderr,
                   failed.stderr[-2000:])
    opted_lines = {int(row.split(":")[1]) for row in opted.stderr.splitlines()
                   if "error: quarantine:" in row and row.startswith(str(HERE / "opt_out.cpp") + ":")}
    section.expect("error mode does not stop an opted-out finding",
                   opted.returncode != 0 and opted_lines == {8, 14}, opted.stderr[-2000:])

    syntax_findings = read_reports(syntax_reports) if syntax.returncode == 0 else []
    section.expect("-fsyntax-only reports the namespace walk too",
                   any(f.kind == "std_object" and f.line == 15 for f in syntax_findings), syntax.stderr[-2000:])
    section.expect("-E writes no report", preprocessed.returncode == 0 and not preprocessed_reports.exists(),
                   preprocessed.stderr[-2000:])

    section.expect("the rule table of the tree loads", real.returncode == 0, real.stderr[-2000:])
    section.expect("the quarantine plugin needs a rule table",
                   missing.returncode != 0 and "give the rule table" in missing.stderr, missing.stderr[-2000:])


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
                       compiled.returncode == 0 and any(base_reports.glob("*.quarantine")) and not found,
                       compiled.stderr[-2000:] + "; ".join(map(str, found)))
        moved_reports = section.work / f"moved-reports-{name}"
        moved = next(results)
        moved_found = read_reports(moved_reports)
        relative = Path(fixture).relative_to("src").as_posix()
        for line, kind, text in BASE_FINDINGS:
            holds = moved.returncode == 0 and any(f.file == relative and f.line == line and f.kind == kind
                                                  and text in f.entity for f in moved_found)
            section.expect(f"{kind} at {relative}:{line} with src/ as the root ({text})", holds,
                           moved.stderr[-2000:])


def contract_errors(stderr: str, root: Path = HERE) -> list[tuple[str, int, str]]:
    """Return each error of the contract rule in STDERR, with the path relative to ROOT."""
    errors: list[tuple[str, int, str]] = []
    for match in CONTRACT_ERROR.finditer(stderr):
        path = Path(match["path"])
        relative = path.relative_to(root).as_posix() if path.is_relative_to(root) else path.as_posix()
        errors.append((relative, int(match["line"]), match["specifier"]))
    return sorted(errors)


def run_generated_files(section: Section) -> None:
    """Compile a unit that includes a generated header in each mode, and compare what each mode gives."""
    tree = section.work / "tree"
    build = tree / "build"
    build.mkdir(parents=True, exist_ok=True)
    (build / "Generated.h").write_text(GENERATED_HEADER, encoding="utf-8")
    (build / "Unclosed.h").write_text(UNCLOSED_GENERATED_HEADER, encoding="utf-8")
    unit = tree / "generated_unit.cpp"
    unit.write_text(GENERATED_UNIT, encoding="utf-8")
    unclosed = tree / "unclosed_unit.cpp"
    unclosed.write_text(UNCLOSED_GENERATED_UNIT, encoding="utf-8")
    places = {"root": str(tree), "build": str(build)}
    test_rules = {"rules": str(TEST_RULES)}
    generated_reports = section.work / "generated-reports"
    runs = (
        ("mode=error", {**places, "mode": "error", **test_rules}),
        ("mode=report", {**places, "mode": "report", "out": str(generated_reports), **test_rules}),
    )
    for name, arguments in runs:
        compiled = section.compile(str(unit), arguments, extra=("-fcontracts",))
        found = contract_errors(compiled.stderr, tree)
        section.expect(f"{name}: a region of a generated file opts out its specifier, and the other one is an error",
                       compiled.returncode != 0 and found == GENERATED_CONTRACT_ERRORS,
                       f"errors {found}; " + compiled.stderr[-2000:])
        compiled = section.compile(str(unclosed), arguments)
        section.expect(f"{name}: an unclosed region of a generated file is an error",
                       compiled.returncode != 0 and "region has no" in compiled.stderr, compiled.stderr[-2000:])
    # No out= here: a plugin that accepts the directory must not replace the
    # report of the unit.
    missing = section.compile(str(unit), {"root": str(tree), "build": str(tree / "missing"), **test_rules})
    section.expect("a build directory that does not exist is an error",
                   missing.returncode != 0 and "does not exist" in missing.stderr, missing.stderr[-2000:])
    found = read_reports(generated_reports)
    section.expect("the region of the generated file turns the specifier on line 3 into opted_out",
                   any(f.kind == "opted_out" and f.file == "build/Generated.h" and f.line == 3
                       and f.entity == "contract_specifier pre" for f in found), "; ".join(map(str, found)))
    section.expect("a generated file is not quarantined",
                   not any(f.file.startswith("build/") and f.kind not in ("opted_out", "contract_specifier", "region")
                           for f in found), "; ".join(map(str, found)))
    section.expect("the report holds the region of the generated file with its reason",
                   any(f.kind == "region" and f.file == "build/Generated.h" and f.line == 2
                       and f.entity == "PROBE: a generated header that the test needs" for f in found),
                   "; ".join(map(str, found)))
    section.expect("a place in a generated macro falls through to the unit that expands it",
                   any(f.kind == "raw_pointer_object" and f.file == "generated_unit.cpp" and f.line == 3
                       for f in found), "; ".join(map(str, found)))


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
                   compiled.returncode == 0 and "region" not in compiled.stderr, compiled.stderr[-2000:])
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
        found = contract_errors(compiled.stderr)
        missing = sorted(set(expected) - set(found))
        unexpected = [error for error in found if error not in expected or found.count(error) > 1]
        section.expect(f"{name}: each contract specifier gives one error, and nothing else does",
                       compiled.returncode != 0 and not missing and not unexpected,
                       f"missing {missing}, unexpected {sorted(set(unexpected))}")
    opted = [f for f in read_reports(contract_reports)
             if f.kind == "opted_out" and f.file == "contracts.cpp" and f.line == 73]
    section.expect("the opt-out region turns the specifier on line 73 into opted_out",
                   any(f.entity == "contract_specifier pre" for f in opted), "; ".join(map(str, opted)))

    named = results[0]
    section.expect("the error names CRUCIBLE_PRE, CRUCIBLE_POST and the reasons",
                   all(text in named.stderr for text in ("CRUCIBLE_PRE(condition)", "foundation/contracts/Pre.h",
                                                         "CRUCIBLE_POST(result, condition)",
                                                         "foundation/contracts/Post.h", "header unit",
                                                         "precompiled header", "constant evaluation")),
                   named.stderr[-2000:])
    unknown = section.compile("violations.cpp", {"root": str(HERE), "mode": "contracts"})
    section.expect("an unknown mode is an error",
                   unknown.returncode != 0 and "the modes are report and error" in unknown.stderr,
                   unknown.stderr[-2000:])


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
        section.expect(f"{fixture} with no plant compiles in error mode", clean.returncode == 0, clean.stderr[-2000:])
    for macro, fixture, kind, text in PLANTS:
        planted = next(results)
        errors = quarantine_errors(planted.stderr, kind)
        section.expect(f"{fixture} with {macro} fails with one {kind} ({text})",
                       planted.returncode != 0 and len(errors) == 1 and text in errors[0], planted.stderr[-2000:])
    door = next(results)
    section.expect("the door of <sys/socket.h> includes it with no finding", door.returncode == 0, door.stderr[-2000:])
    for name in ("an include that enters High.h", "an include that #pragma once stops"):
        compiled = next(results)
        errors = quarantine_errors(compiled.stderr, "upward_include")
        section.expect(f"{name}: fixy/Low.h has one upward include",
                       compiled.returncode != 0 and len(errors) == 1 and "include/fixy/Low.h:8:" in errors[0]
                       and "include/fixy/high/High.h (the layer high) from the layer low" in errors[0],
                       compiled.stderr[-2000:])
    enforced = next(results)
    section.expect("an enforce row with the mode error makes a finding an error in report mode",
                   enforced.returncode != 0 and len(quarantine_errors(enforced.stderr, "raw_pointer_object")) == 1,
                   enforced.stderr[-2000:])
    unclassified = next(results)
    section.expect("a file that no row of the table holds is an error",
                   unclassified.returncode != 0 and "no layer row and no quarantine row of the rule table holds "
                   "'unclassified.cpp'" in unclassified.stderr.replace("‘", "'").replace("’", "'"),
                   unclassified.stderr[-2000:])


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
    ("pending.cpp", (), 29, "replace_pending:<limits>", "std::numeric_limits::max"),
    ("pending.cpp", (), 33, "replace_pending:<limits>", "std::numeric_limits::max"),
    ("pending.cpp", (), 36, "replace_pending:std::unreachable", "std::unreachable"),
    ("pending.cpp", (), 41, "replace_pending:std::bit_cast", "std::bit_cast"),
    ("foreign_symbols.cpp", (), 7, "foreign_symbol", "abort (C linkage)"),
    ("foreign_symbols.cpp", (), 8, "foreign_symbol", "write (C linkage)"),
    ("foreign_symbols.cpp", (), 9, "foreign_symbol", "foreign_copy (asm label memcpy)"),
    ("foreign_symbols.cpp", (), 10, "foreign_symbol", "foreign_exit (attribute weakref)"),
    ("foreign_symbols.cpp", (), 13, "c_library_call", "abort (C linkage)"),
    ("foreign_symbols.cpp", (), 14, "c_library_call", "write (C linkage)"),
    ("foreign_symbols.cpp", (), 15, "c_library_call", "foreign_copy (asm label memcpy)"),
    ("foreign_symbols.cpp", (), 16, "c_library_call", "foreign_exit (attribute weakref)"),
    ("foreign_symbols.cpp", (), 22, "inline_asm", "asm register held"),
)
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
)
# (fixture, extra flags, line, kind): no finding of that kind there.
LIBRARY_KINDS_ABSENT = (
    ("gaps.cpp", (), 45, "c_library_call"),  # assert_expansion stands for __assert_fail
    ("gaps.cpp", (), 45, "compiler_builtin"),  # and for __builtin_FILE and __builtin_LINE
    ("pending.cpp", (), 11, "std_entity"),  # plain-result admits a result of unsigned long
    ("pending.cpp", (), 13, "replace_pending:std::bit_cast"),  # a refused use is no pending use
    ("pending.cpp", (), 22, "replace_pending:std::bit_cast"),  # a dependent result fails closed
    ("pending.cpp", (), 41, "std_entity"),  # a pointer to a dependent type holds no bool
)


def run_library_names(section: Section) -> None:
    """Compile the fixtures of the library names that are not classes, of the exact headers and of the gaps.

    gaps.cpp holds the paths around the rules: a typedef of fixy, a variable
    of the C library, builtins, va_arg and asm.  pending.cpp holds the uses
    that an admit row with `until` admits, and the uses of std::bit_cast that
    the restriction plain-result refuses.  Each compile writes its
    report to a directory of its own, and the part compares each report with
    LIBRARY_NAMES and LIBRARY_NAMES_ABSENT.
    """
    fixtures = sorted({(fixture, extra) for fixture, extra, _, _, _ in LIBRARY_NAMES})
    calls: list[tuple[object, ...]] = []
    for index, (fixture, extra) in enumerate(fixtures):
        out = section.work / f"library-names-{index}"
        calls.append((fixture, {"root": str(HERE), "mode": "report", "rules": str(TEST_RULES), "out": str(out)},
                      ("-S", "-o", os.devnull), extra))
    # include_exact.cpp: the second directive of fixy/GapsUser.h enters no
    # file, and the plugin resolves it as libcpp does.
    calls.append(("include_exact.cpp", {"root": str(HERE), "mode": "error", "rules": str(TEST_RULES)}))
    results = section.compile_all(calls)
    findings: dict[tuple[str, tuple[str, ...]], list[Finding]] = {}
    for index, ((fixture, extra), compiled) in enumerate(zip(fixtures, results, strict=False)):
        section.expect(f"{fixture} {' '.join(extra)} compiles in report mode".replace("  ", " "),
                       compiled.returncode == 0, compiled.stderr[-2000:])
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
                   exact.returncode == 0 and "upward_include" not in exact.stderr, exact.stderr[-2000:])


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
                       compiled.returncode != 0 and text in ascii_quotes(compiled.stderr), compiled.stderr[-2000:])
    findings: dict[tuple[str, tuple[str, ...]], list[Finding]] = {}
    for index, ((fixture, extra), compiled) in enumerate(zip(reported, results[len(PROVENANCE_ERRORS):], strict=True)):
        section.expect(f"{fixture} {' '.join(extra)} compiles in report mode".replace("  ", " "),
                       compiled.returncode == 0, compiled.stderr[-2000:])
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
                       compiled.returncode == 0, compiled.stderr[-2000:])
        findings[fixture] = read_reports(section.work / f"depth-{index}")
    for fixture, _, line, kind, text in DEPTH_CASES:
        if line is None:
            section.expect(f"{fixture} has no finding", not findings[fixture], "; ".join(map(str, findings[fixture])))
            continue
        hits = [f for f in findings[fixture] if f.file == fixture and f.line == line and f.kind == kind]
        section.expect(f"{kind} {text} at {fixture}:{line}", any(text in f.entity for f in hits),
                       "; ".join(map(str, hits)))


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
    section.expect("restricted.cpp with no plant compiles in error mode", clean.returncode == 0, clean.stderr[-2000:])
    for macro, kind, text in RESTRICTION_PLANTS:
        planted = next(results)
        section.expect(f"restricted.cpp with {macro} fails with a {kind} of {text}",
                       planted.returncode != 0 and any(text in row for row in quarantine_errors(planted.stderr, kind)),
                       planted.stderr[-2000:])
    debug = next(results)
    section.expect("a unit that defines _GLIBCXX_DEBUG fails, because <meta> is admitted unless it does",
                   debug.returncode != 0 and "the unit defines _GLIBCXX_DEBUG" in debug.stderr, debug.stderr[-2000:])


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
)


def run_language(section: Section) -> None:
    """Compile language_user.cpp in report mode with language.txt, with no plant and with each plant.

    The included fixy/high/Language.h is a file of the language.  With no
    plant it has no finding, also for its raw pointer, its array and the name
    that a row with `in` admits there.  Each plant is an error in report mode,
    in the file of the language.
    """
    report_mode = {"root": str(HERE), "mode": "report", "rules": str(HERE / "language.txt")}
    calls: list[tuple[object, ...]] = [("language_user.cpp", report_mode)]
    calls += [("language_user.cpp", report_mode, ("-S", "-o", os.devnull), (f"-D{macro}",))
              for macro, _, _ in LANGUAGE_PLANTS]
    results = iter(section.compile_all(calls))
    clean = next(results)
    section.expect("a file of the language with no plant compiles with no error and no note",
                   clean.returncode == 0 and "quarantine:" not in clean.stderr, clean.stderr[-2000:])
    for macro, kind, text in LANGUAGE_PLANTS:
        planted = next(results)
        errors = [row for row in quarantine_errors(planted.stderr, kind) if "include/fixy/high/Language.h:" in row]
        section.expect(f"a file of the language with {macro} fails in report mode with a {kind} of {text}",
                       planted.returncode != 0 and len(errors) == 1 and text in errors[0], planted.stderr[-2000:])


def run_table_readers(section: Section) -> None:
    """Give the plugin and layer_rules.py each malformed table, and the test table and the tree table.

    Each reader must refuse each malformed table and read each good table.
    """
    tables = section.work / "tables"
    tables.mkdir(exist_ok=True)
    calls: list[tuple[object, ...]] = []
    for index, (_, text) in enumerate(MALFORMED_TABLES):
        path = tables / f"malformed-{index}.txt"
        path.write_text(text, encoding="utf-8")
        calls.append(("plant_std.cpp", {"root": str(HERE), "mode": "report", "rules": str(path)}))
    for (name, text), compiled in zip(MALFORMED_TABLES, section.compile_all(calls), strict=True):
        try:
            layer_rules.parse(text)
            is_refused_by_python = False
        except layer_rules.TableError:
            is_refused_by_python = True
        section.expect(f"layer_rules.py refuses {name}", is_refused_by_python)
        section.expect(f"the plugin refuses {name}",
                       compiled.returncode != 0 and "error: quarantine: " in compiled.stderr, compiled.stderr[-2000:])
    until_table = tables / "until.txt"
    until_table.write_text("admit <type_traits> | traits\nadmit std::move until Scalar | a cast\n"
                           "quarantine plant_std.cpp\n", encoding="utf-8")
    until = section.compile("plant_std.cpp", {"root": str(HERE), "mode": "error", "rules": str(until_table)})
    section.expect("the mode error refuses a use that an admit row with `until` admits",
                   until.returncode != 0 and quarantine_errors(until.stderr, "replace_pending:std::move")
                   and not quarantine_errors(until.stderr, "std_entity"), until.stderr[-2000:])
    for name, path in (("the test table", TEST_RULES), ("the tree table", layer_rules.TABLE)):
        try:
            layer_rules.load(path)
            is_read = True
        except layer_rules.TableError as failure:
            is_read = False
            section.expect(f"layer_rules.py reads {name}", False, str(failure))
        if is_read:
            section.expect(f"layer_rules.py reads {name}", True)


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
    printed = subprocess.run(["ccache", "--print-stats"], capture_output=True, text=True, env=environment).stdout
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
        environment = {**os.environ, "CCACHE_DIR": str(work / name), "CCACHE_CONFIGPATH": str(work / f"{name}.conf")}
        results: list[tuple[int, object]] = []
        for output, arguments, used in steps:
            before = ccache_hits(environment)
            result = section.compile("violations.cpp", arguments, ("-c", "-o", str(work / f"{output}.o")),
                                     launcher=used, env=environment)
            results.append((ccache_hits(environment) - before, (result.returncode, section_of(work / f"{output}.o"))))
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
                   compiled.returncode == 0 and found is not None and found[0] == SECTION_STAMP
                   and found[1] == report and len(report) > 10, compiled.stderr[-2000:] + f"; {found}")
    section.expect("a unit that writes an object gives no note, and its section holds the findings",
                   quiet.returncode == 0 and "note: quarantine:" not in quiet.stderr
                   and section_of(work / "quiet.o") == found, quiet.stderr[-2000:])
    opted_found = section_of(work / "opted.o")
    section.expect("the section holds an opted_out finding",
                   opted.returncode == 0 and opted_found is not None
                   and any(line.startswith("quarantine: opted_out opt_out.cpp:11:") for line in opted_found[1]),
                   opted.stderr[-2000:])
    section.expect("-fsyntax-only writes no object", syntax.returncode == 0 and not (work / "syntax.o").exists(),
                   syntax.stderr[-2000:])
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
                   real_swap.returncode == 0 and has_swap == [True, False] and new_hits == 0
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

    def dependencies(unit: str, stage: tuple[str, ...]) -> tuple[subprocess.CompletedProcess[str], str]:
        depfile = section.work / f"dependency-{Path(unit).stem}-{stage[0][1:]}.d"
        compiled = section.compile(str(tree / unit), arguments, stage, ("-MD", "-MF", str(depfile)))
        return compiled, depfile.read_text(encoding="utf-8") if depfile.is_file() else ""

    modes = stamps / "modes" / "app"
    found, text = dependencies("app/finding.cpp", ("-c", "-o", str(section.work / "dependency-finding.o")))
    section.expect("a unit names the mode stamp of each file with a finding, and enforce.txt for a file with none",
                   found.returncode == 0 and str(modes / "finding.cpp") in text and str(modes / "Header.h") in text
                   and str(stamps / "enforce.txt") in text, found.stderr[-2000:] + text)
    section.expect("a file with no finding adds no mode stamp", str(modes / "Clean.h") not in text, text)
    opted, text = dependencies("app/opted.cpp", ("-c", "-o", str(section.work / "dependency-opted.o")))
    section.expect("an opted-out finding adds no dependency",
                   opted.returncode == 0 and str(stamps) not in text, opted.stderr[-2000:] + text)
    syntax, text = dependencies("app/finding.cpp", ("-fsyntax-only",))
    section.expect("-fsyntax-only writes the dependency file before the report, so it holds no mode stamp",
                   syntax.returncode == 0 and str(stamps) not in text, syntax.stderr[-2000:] + text)


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

    def chain(name: str, is_keyed: bool) -> list[tuple[int, subprocess.CompletedProcess[str]]]:
        tree = section.work / f"{name}-tree"
        write_tree(tree, {"app/unit.cpp": "int* cached_pointer = nullptr;\n"})
        rules, stamps = tree / "rules.txt", tree / "stamps"
        (section.work / f"{name}.conf").write_text("", encoding="utf-8")
        environment = {**os.environ, "CCACHE_DIR": str(section.work / name),
                       "CCACHE_CONFIGPATH": str(section.work / f"{name}.conf")}
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
                   stored.returncode == 0 and error_hits == 0 and failed.returncode != 0
                   and "error: quarantine: raw_pointer_object" in failed.stderr and back.returncode == 0,
                   f"{error_hits} " + failed.stderr[-2000:])
    _, (blind_hits, skipped), _ = blind
    section.expect("negative control: a cache that does not hash enforce.txt hits, and skips the error",
                   blind_hits == 1 and skipped.returncode == 0, f"{blind_hits} " + skipped.stderr[-2000:])


def build_command(build_dir: Path) -> tuple[list[str], Path, str] | None:
    """Return the compile command of a real object of BUILD_DIR, its directory and its source.

    The command is the first one that loads the quarantine plugin, and
    src/foundation/ContractHandler.cpp when the database holds it.
    """
    database = build_dir / "compile_commands.json"
    if not database.is_file():
        return None
    entries = [entry for entry in json.loads(database.read_text(encoding="utf-8"))
               if "-fplugin=" in (entry.get("command") or " ".join(entry.get("arguments", [])))]
    if not entries:
        return None
    entry = next((entry for entry in entries if entry["file"].endswith("src/foundation/ContractHandler.cpp")),
                 entries[0])
    argv = entry["arguments"] if "arguments" in entry else shlex.split(entry["command"])
    return list(argv), Path(entry["directory"]), entry["file"]


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
                holds = (planted.returncode == 0 and clean.returncode == 0 and len(rows) == 1
                         and rows[0].endswith(f" {entity}") and not clean_rows)
                detail = f"{rows} {clean_rows} " + planted.stderr[-1000:]
            else:
                errors = quarantine_errors(planted.stderr, kind)
                holds = planted.returncode != 0 and len(errors) == 1 and entity in errors[0] and clean.returncode == 0
                detail = planted.stderr[-1000:] + clean.stderr[-1000:]
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

    def compile_plant(table: Path) -> subprocess.CompletedProcess[str]:
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
        return subprocess.run(argv, capture_output=True, text=True, cwd=directory)

    with ThreadPoolExecutor(max_workers=2) as pool:
        failed, passed = pool.map(compile_plant, (error_table, tree_table))
    section.expect("with the flags of the build, an enforce row with the mode error fails the compile of the plant",
                   failed.returncode != 0 and "error: quarantine: raw_pointer_object" in failed.stderr,
                   failed.stderr[-2000:])
    section.expect("with the flags of the build and the table of the tree, the plant compiles",
                   passed.returncode == 0, passed.stderr[-2000:])


def main(argv: list[str]) -> int:
    """Parse the arguments, build the plugin and run every case."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--cxx", required=True, help="the compiler that loads the plugin")
    parser.add_argument("--source", required=True, type=Path, help="the source of the quarantine plugin")
    parser.add_argument("--rules", required=True, type=Path, help="the rule table of the tree")
    parser.add_argument("--build-dir", type=Path, help="a build directory with compile_commands.json")
    parser.add_argument("flags", nargs="*", help="the flags that build the plugin, after --")
    args = parser.parse_args(argv)
    with tempfile.TemporaryDirectory(prefix="quarantine-plugin-") as scratch:
        try:
            checker = Checker(args.cxx, args.source, args.flags, Path(scratch), args.build_dir)
        except RuntimeError as error:
            print(f"check_plugin: {error}", file=sys.stderr)
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


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
