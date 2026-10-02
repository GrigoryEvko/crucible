#!/usr/bin/env python3
"""check_plugin — the two plugins find each class of finding, only where the location rule puts it.

The test builds the contract plugin and the quarantine plugin from their
sources with the flags that CMake gives, compiles each fixture of this
directory with a plugin loaded, and compares what the plugin reports with the
expectations below.  This directory is the source root of the test, and
rules.txt is its rule table: include/fixy/, src/foundation/ and src/fixy/ hold
base code, and the quarantine rows name each other fixture.

Each class of finding has an expectation that fails when the quarantine plugin
loses the check of that class.  The base rule, the admit rows and the opt-out
region each have an expectation that fails when the plugin loses that rule.

Each row kind of the rule table has a plant: a forbidden header in a layer, a
door header outside its door, an upward include and a standard library name
that the table does not admit.  The test compiles each planted file in error
mode one time with no plant, which must pass, and one time with each plant,
which must fail with the finding of the plant.  An enforce row with the mode
error makes a finding an error in report mode, and a file that no row holds
is an error.  The plugin and utils/scripts/layer_rules.py get the same
malformed tables, and each one must refuse each table.

contracts.cpp holds each form of a P2900 contract specifier that the contract
rule rejects, also in base code.  The test compiles it with the contract
plugin and with the quarantine plugin in each mode, and compares the errors
with CONTRACT_ERRORS.  A form that a plugin stops seeing, a second error for
one specifier and an error outside the list each fail the test.

A generated file of the build directory follows one rule in the two plugins:
the contract rule and the opt-out regions apply to it, and the quarantine rule
does not.  The test writes a source root with a build directory in its scratch
directory, and compiles a unit of that root with each plugin.

The two plugins build at the same time.  Then the parts of the test run at
the same time: the findings, the base files, the malformed regions, the
modes, the contract rule, the generated files, the include rules and the
readers of the table.  Each part writes its own files, and main prints the
verdicts of the parts in that order.

usage: check_plugin.py --cxx CXX --contract-source CONTRACT.cpp --source QUARANTINE.cpp --rules TABLE
                       -- BUILD_FLAGS...

Exit 0 when each expectation holds, 1 when one fails, 2 on a usage error or a
plugin that does not build.
"""

from __future__ import annotations

import argparse
import os
import re
import subprocess
import sys
import tempfile
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
TEST_RULES = HERE / "rules.txt"
PLUGIN = "crucible_quarantine"
CONTRACT_PLUGIN = "crucible_contract"

sys.path.insert(0, str(HERE.parents[2] / "scripts"))
import layer_rules  # noqa: E402

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
    ("an absolute path", "quarantine /a/\n"),
    ("a path with a parent component", "quarantine a/../b/\n"),
    ("a path with an empty component", "quarantine a//\n"),
    ("an unknown enforce mode", "enforce a/ fatal\n"),
    ("a path with two enforce rows", "enforce a/ error\nenforce a/ report\n"),
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
OUTSIDE_REGION_HEADER = ("#pragma once\n#pragma crucible I_KNOW_WHAT_IM_DOING(\"a header outside the root\")\n"
                         "inline int outside_region_value() { return 1; }\n#pragma crucible END_I_KNOW_WHAT_IM_DOING\n")

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
GENERATED_HEADER = ("#pragma once\n"
                    "#pragma crucible I_KNOW_WHAT_IM_DOING(\"a generated header that the test needs\")\n"
                    "int generated_opted(int value) pre(value > 0);\n"
                    "#pragma crucible END_I_KNOW_WHAT_IM_DOING\n"
                    "int generated_plain(int value) pre(value > 0);\n"
                    "inline char* generated_pointer = nullptr;\n"
                    "#define GENERATED_POINTER char* generated_expanded_pointer = nullptr\n")
GENERATED_UNIT = "#include \"build/Generated.h\"\n\nGENERATED_POINTER;\n"
GENERATED_CONTRACT_ERRORS = [("build/Generated.h", 5, "pre")]
UNCLOSED_GENERATED_HEADER = "#pragma once\n#pragma crucible I_KNOW_WHAT_IM_DOING(\"a generated region with no end\")\n"
UNCLOSED_GENERATED_UNIT = "#include \"build/Unclosed.h\"\n"


@dataclass(frozen=True)
class Finding:
    """One line of a report."""

    kind: str
    file: str
    line: int
    entity: str


class Checker:
    """Build each plugin one time, then compile fixtures with one of them.

    The two plugins build at the same time.  compile is safe to call from
    many threads, because each compile is its own process.
    """

    def __init__(self, cxx: str, sources: dict[str, Path], flags: list[str], work: Path) -> None:
        self.cxx = cxx
        self.work = work
        with ThreadPoolExecutor(max_workers=len(sources)) as pool:
            builds = {name: pool.submit(subprocess.run, [cxx, *flags, "-o", str(work / f"{name}.so"), str(source)],
                                        capture_output=True, text=True)
                      for name, source in sources.items()}
        for name, build in builds.items():
            if build.result().returncode != 0:
                raise RuntimeError(f"the plugin {name} did not build:\n{build.result().stderr}")

    def compile(self, fixture: str, arguments: dict[str, str],
                stage: tuple[str, ...] = ("-S", "-o", os.devnull),
                extra: tuple[str, ...] = (), plugin: str = PLUGIN) -> subprocess.CompletedProcess[str]:
        """Compile one fixture with one plugin, the given plugin arguments, stage flags and extra flags."""
        command = [self.cxx, "-std=c++26", "-I", str(HERE / "include"), *extra, f"-fplugin={self.work / plugin}.so"]
        command += [f"-fplugin-arg-{plugin}-{key}={value}" for key, value in arguments.items()]
        command += ["-fdiagnostics-color=never", *stage, str(HERE / fixture)]
        return subprocess.run(command, capture_output=True, text=True)


class Section:
    """The expectations of one part of the test, which runs at the same time as the other parts.

    A part writes only its own files under the work directory, and it keeps
    its verdict lines until main prints them in the order of the parts.
    """

    def __init__(self, checker: Checker) -> None:
        self.work = checker.work
        self.compile = checker.compile
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
        run_table_readers,
    ]
    sections = [Section(checker) for _ in parts]
    with ThreadPoolExecutor(max_workers=len(parts)) as pool:
        for future in [pool.submit(part, section) for part, section in zip(parts, sections, strict=True)]:
            future.result()
    return sections


def run_findings(section: Section, arguments: dict[str, str]) -> None:
    """Compile the two fixtures in report mode, and judge each finding that must be present or absent."""
    for fixture in ("violations.cpp", "opt_out.cpp"):
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
    """Compile each fixture of a malformed region with each plugin, and judge the error."""
    for fixture, text in PRAGMA_ERRORS:
        compiled = section.compile(fixture, {"root": str(HERE), "mode": "report", "rules": str(TEST_RULES)})
        section.expect(f"{fixture} is an error", compiled.returncode != 0 and text in compiled.stderr,
                       compiled.stderr[-2000:])
        compiled = section.compile(fixture, {"root": str(HERE)}, plugin=CONTRACT_PLUGIN)
        section.expect(f"{fixture} is an error for the contract plugin",
                       compiled.returncode != 0 and text in compiled.stderr, compiled.stderr[-2000:])


def run_modes(section: Section, arguments: dict[str, str], rules: Path) -> None:
    """Judge the report mode without out=, the error mode, -fsyntax-only, -E and the rule table of the tree.

    The rule table of the tree classifies the paths of the tree, so its compile
    has the repository as the source root.
    """
    syntax_reports = section.work / "syntax-reports"
    preprocessed_reports = section.work / "preprocessed-reports"
    test_rules = {"rules": str(TEST_RULES)}
    noted, failed, opted, syntax, preprocessed, real, missing = section.compile_all([
        ("violations.cpp", {"root": str(HERE), "mode": "report", **test_rules}),
        ("violations.cpp", {"root": str(HERE), "mode": "error", **test_rules}),
        ("opt_out.cpp", {"root": str(HERE), "mode": "error", **test_rules}),
        ("violations.cpp", {**arguments, "out": str(syntax_reports)}, ("-fsyntax-only",)),
        ("violations.cpp", {**arguments, "out": str(preprocessed_reports)}, ("-E", "-o", os.devnull)),
        ("opt_out.cpp", {"root": str(rules.parents[2]), "rules": str(rules)}),
        ("violations.cpp", {"root": str(HERE), "mode": "report"}),
    ])
    section.expect("report mode without out= gives notes and succeeds",
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
    """Compile a unit that includes a generated header with each plugin, and compare what each plugin gives."""
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
        ("the contract plugin", CONTRACT_PLUGIN, places),
        ("mode=error", PLUGIN, {**places, "mode": "error", **test_rules}),
        ("mode=report", PLUGIN, {**places, "mode": "report", "out": str(generated_reports), **test_rules}),
    )
    for name, plugin, arguments in runs:
        compiled = section.compile(str(unit), arguments, extra=("-fcontracts",), plugin=plugin)
        found = contract_errors(compiled.stderr, tree)
        section.expect(f"{name}: a region of a generated file opts out its specifier, and the other one is an error",
                       compiled.returncode != 0 and found == GENERATED_CONTRACT_ERRORS,
                       f"errors {found}; " + compiled.stderr[-2000:])
        compiled = section.compile(str(unclosed), arguments, plugin=plugin)
        section.expect(f"{name}: an unclosed region of a generated file is an error",
                       compiled.returncode != 0 and "region has no" in compiled.stderr, compiled.stderr[-2000:])
        # No out= here: a plugin that accepts the directory must not replace
        # the report of the unit.
        missing = section.compile(str(unit), {"root": str(tree), "build": str(tree / "missing")}, plugin=plugin)
        section.expect(f"{name}: a build directory that does not exist is an error",
                       missing.returncode != 0 and "does not exist" in missing.stderr, missing.stderr[-2000:])
    found = read_reports(generated_reports)
    section.expect("the region of the generated file turns the specifier on line 3 into opted_out",
                   any(f.kind == "opted_out" and f.file == "build/Generated.h" and f.line == 3
                       and f.entity == "contract_specifier pre" for f in found), "; ".join(map(str, found)))
    section.expect("a generated file is not quarantined",
                   not any(f.file.startswith("build/") and f.kind != "opted_out" and f.kind != "contract_specifier"
                           for f in found), "; ".join(map(str, found)))
    section.expect("a place in a generated macro falls through to the unit that expands it",
                   any(f.kind == "raw_pointer_object" and f.file == "generated_unit.cpp" and f.line == 3
                       for f in found), "; ".join(map(str, found)))


def run_contract_rule(section: Section) -> None:
    """Compile contracts.cpp with each plugin and in each mode, and compare the errors with CONTRACT_ERRORS."""
    outside = section.work / "outside"
    outside.mkdir(exist_ok=True)
    (outside / "Outside.h").write_text(OUTSIDE_HEADER, encoding="utf-8")
    (outside / "OutsideRegion.h").write_text(OUTSIDE_REGION_HEADER, encoding="utf-8")
    test_rules = {"rules": str(TEST_RULES)}
    for plugin, arguments in ((CONTRACT_PLUGIN, {"root": str(HERE)}),
                              (PLUGIN, {"root": str(HERE), "mode": "error", **test_rules})):
        compiled = section.compile("region_outside_root.cpp", arguments, extra=("-I", str(outside)), plugin=plugin)
        section.expect(f"{plugin}: a region of a header outside the root is no error",
                       compiled.returncode == 0 and "region" not in compiled.stderr, compiled.stderr[-2000:])
    extra = ("-fcontracts", "-I", str(outside))
    expected = sorted(CONTRACT_ERRORS)
    contract_reports = section.work / "contract-reports"
    runs = (
        ("the contract plugin", CONTRACT_PLUGIN, {"root": str(HERE)}, ("-S", "-o", os.devnull)),
        ("mode=error", PLUGIN, {"root": str(HERE), "mode": "error", **test_rules}, ("-S", "-o", os.devnull)),
        ("mode=report", PLUGIN, {"root": str(HERE), "mode": "report", "out": str(contract_reports), **test_rules},
         ("-S", "-o", os.devnull)),
        ("the contract plugin with -fsyntax-only", CONTRACT_PLUGIN, {"root": str(HERE)}, ("-fsyntax-only",)),
    )
    for name, plugin, arguments, stage in runs:
        compiled = section.compile("contracts.cpp", arguments, stage, extra, plugin)
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

    named = section.compile("contracts.cpp", {"root": str(HERE)}, extra=extra, plugin=CONTRACT_PLUGIN)
    section.expect("the error names CRUCIBLE_PRE, CRUCIBLE_POST and the reasons",
                   all(text in named.stderr for text in ("CRUCIBLE_PRE(condition)", "foundation/contracts/Pre.h",
                                                         "CRUCIBLE_POST(result, condition)",
                                                         "foundation/contracts/Post.h", "header unit",
                                                         "precompiled header", "constant evaluation")),
                   named.stderr[-2000:])
    quiet = section.compile("violations.cpp", {"root": str(HERE)}, plugin=CONTRACT_PLUGIN)
    section.expect("the contract plugin applies only the contract rule",
                   quiet.returncode == 0 and "quarantine:" not in quiet.stderr, quiet.stderr[-2000:])
    foreign = section.compile("violations.cpp", {"root": str(HERE), "mode": "report"}, plugin=CONTRACT_PLUGIN)
    section.expect("the contract plugin refuses an argument of the quarantine plugin",
                   foreign.returncode != 0 and "the arguments are root, build and stamp" in foreign.stderr,
                   foreign.stderr[-2000:])
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


# (fixture, extra flags, line, the whole entity of a std_entity finding)
LIBRARY_NAMES = (
    ("nonclass.cpp", (), 13, "std::size_t"),
    ("nonclass.cpp", (), 14, "std::byte"),
    ("nonclass.cpp", (), 15, "std::nullptr_t"),
    ("nonclass.cpp", (), 16, "std::align_val_t"),
    ("nonclass.cpp", (), 17, "std::tuple_element_t"),
    ("nonclass.cpp", (), 19, "std::size_t"),
    ("nonclass.cpp", (), 25, "std::tuple_size_v"),
    ("header_exact.cpp", (), 8, "std::experimental::nonesuch"),
    ("meta_info.cpp", ("-freflection",), 7, "std::meta::info"),
)
# (fixture, line): no finding of any kind there.
LIBRARY_NAMES_ABSENT = (
    ("nonclass.cpp", 18),  # <type_traits> admits std::remove_cvref_t
    ("nonclass.cpp", 20),  # a project alias of std::size_t names no library entity
    ("nonclass.cpp", 23),  # the entry std::tuple_size admits std::tuple_size<T>::value
    ("header_exact.cpp", 7),  # <type_traits> admits std::is_same
)


def run_library_names(section: Section) -> None:
    """Compile the fixtures of the library names that are not classes, and of the exact header entry.

    Each compile writes its report to a directory of its own, and the part
    compares each report with LIBRARY_NAMES and LIBRARY_NAMES_ABSENT.
    """
    fixtures = sorted({(fixture, extra) for fixture, extra, _, _ in LIBRARY_NAMES})
    calls: list[tuple[object, ...]] = []
    for fixture, extra in fixtures:
        out = section.work / f"library-names-{Path(fixture).stem}"
        calls.append((fixture, {"root": str(HERE), "mode": "report", "rules": str(TEST_RULES), "out": str(out)},
                      ("-S", "-o", os.devnull), extra))
    findings: list[Finding] = []
    for (fixture, _), compiled in zip(fixtures, section.compile_all(calls), strict=True):
        section.expect(f"{fixture} compiles in report mode", compiled.returncode == 0, compiled.stderr[-2000:])
        findings += read_reports(section.work / f"library-names-{Path(fixture).stem}")
    for fixture, _, line, entity in LIBRARY_NAMES:
        hits = [f for f in findings if f.kind == "std_entity" and f.file == fixture and f.line == line]
        section.expect(f"std_entity {entity} at {fixture}:{line}", [f.entity for f in hits] == [entity],
                       "; ".join(map(str, hits)))
    for fixture, line in LIBRARY_NAMES_ABSENT:
        hits = [f for f in findings if f.file == fixture and f.line == line]
        section.expect(f"no finding at {fixture}:{line}", not hits, "; ".join(map(str, hits)))


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
    for name, path in (("the test table", TEST_RULES), ("the tree table", layer_rules.TABLE)):
        try:
            layer_rules.load(path)
            is_read = True
        except layer_rules.TableError as failure:
            is_read = False
            section.expect(f"layer_rules.py reads {name}", False, str(failure))
        if is_read:
            section.expect(f"layer_rules.py reads {name}", True)


def main(argv: list[str]) -> int:
    """Parse the arguments, build the plugin and run every case."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--cxx", required=True, help="the compiler that loads the plugins")
    parser.add_argument("--contract-source", required=True, type=Path, help="the source of the contract plugin")
    parser.add_argument("--source", required=True, type=Path, help="the source of the quarantine plugin")
    parser.add_argument("--rules", required=True, type=Path, help="the rule table of the tree")
    parser.add_argument("flags", nargs="*", help="the flags that build the plugins, after --")
    args = parser.parse_args(argv)
    with tempfile.TemporaryDirectory(prefix="quarantine-plugin-") as scratch:
        try:
            checker = Checker(args.cxx, {CONTRACT_PLUGIN: args.contract_source, PLUGIN: args.source}, args.flags,
                              Path(scratch))
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
