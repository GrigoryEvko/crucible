#!/usr/bin/env python3
"""check_plugin — the quarantine plugin finds each class of finding, only where the location rule puts it.

The test builds the plugin from its source with the flags that CMake gives,
compiles each fixture of this directory with the plugin loaded, and compares
what the plugin reports with the expectations below.  This directory is the
source root of the test: include/fixy/Shelf.h is substrate code, and each
other fixture is quarantined.

Each class of finding has an expectation that fails when the plugin loses the
check of that class.  The substrate rule, the admitted list and the opt-out
region each have an expectation that fails when the plugin loses that rule.

contracts.cpp holds each form of a P2900 contract specifier that the contract
rule rejects, in each mode and also in substrate code.  The test compares the
errors with CONTRACT_ERRORS.  A form that the plugin stops seeing, a second
error for one specifier and an error outside the list each fail the test.

usage: check_plugin.py --cxx CXX --source PLUGIN.cpp --admitted LIST -- BUILD_FLAGS...

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
from dataclasses import dataclass
from pathlib import Path

HERE = Path(__file__).resolve().parent
PLUGIN = "crucible_quarantine"

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
    ("violations.cpp", 63, None, None),  # the body of fill is substrate code
    ("violations.cpp", 65, None, None),  # the body of clear_bytes is substrate code
    ("violations.cpp", 79, None, None),  # the instantiation of the template with a std::vector names nothing
    ("violations.cpp", 80, None, None),  # a dependent member names nothing, in the instantiation too
    ("violations.cpp", 85, None, None),  # the substrate spells the default argument
    ("violations.cpp", 87, None, None),  # the copy of an immediate default has the location of the call
    ("violations.cpp", 89, "std_entity", None),  # the compiler calls the conversion function
    ("violations.cpp", 95, None, "memory_order"),  # a constexpr variable of an enumeration is a named constant
    ("violations.cpp", 97, None, None),  # the immediate default of a template has the location of the call
    ("include/fixy/Shelf.h", None, None, None),  # substrate code
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
    ("include/fixy/Contracted.h", 9, "pre"),  # a template in substrate code
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


@dataclass(frozen=True)
class Finding:
    """One line of a report."""

    kind: str
    file: str
    line: int
    entity: str


class Checker:
    """Build the plugin one time, then compile fixtures with it."""

    def __init__(self, cxx: str, source: Path, flags: list[str], work: Path) -> None:
        self.cxx = cxx
        self.work = work
        self.plugin = work / f"{PLUGIN}.so"
        self.failures: list[str] = []
        built = subprocess.run([cxx, *flags, "-o", str(self.plugin), str(source)], capture_output=True, text=True)
        if built.returncode != 0:
            raise RuntimeError(f"the plugin did not build:\n{built.stderr}")

    def compile(self, fixture: str, arguments: dict[str, str],
                stage: tuple[str, ...] = ("-S", "-o", os.devnull),
                extra: tuple[str, ...] = ()) -> subprocess.CompletedProcess[str]:
        """Compile one fixture with the plugin, the given plugin arguments, stage flags and extra flags."""
        command = [self.cxx, "-std=c++26", "-I", str(HERE / "include"), *extra, f"-fplugin={self.plugin}"]
        command += [f"-fplugin-arg-{PLUGIN}-{key}={value}" for key, value in arguments.items()]
        command += ["-fdiagnostics-color=never", *stage, str(HERE / fixture)]
        return subprocess.run(command, capture_output=True, text=True)

    def expect(self, name: str, holds: bool, detail: str = "") -> None:
        """Record the verdict of one expectation."""
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            self.failures.append(f"{name}{': ' + detail if detail else ''}")


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


def run(checker: Checker, admitted: Path) -> None:
    """Compile each fixture and judge each expectation."""
    reports = checker.work / "reports"
    arguments = {"root": str(HERE), "mode": "report", "admitted": str(HERE / "admitted.txt"), "out": str(reports)}
    for fixture in ("violations.cpp", "opt_out.cpp"):
        compiled = checker.compile(fixture, arguments)
        checker.expect(f"{fixture} compiles in report mode", compiled.returncode == 0, compiled.stderr[-2000:])
    findings = read_reports(reports)

    for kind, file, line, text in PRESENT:
        # A text that starts with '=' must be the whole entity.
        holds = any(f.kind == kind and f.file == file and f.line == line
                    and (f.entity == text[1:] if text.startswith("=") else text in f.entity) for f in findings)
        checker.expect(f"{kind} at {file}:{line} ({text})", holds)
    for file, line, kind, text in ABSENT:
        hits = [f for f in findings
                if f.file == file and (line is None or f.line == line) and (kind is None or f.kind == kind)
                and (text is None or text in f.entity)]
        where = f"{file}:{line}" if line is not None else file
        what = kind or "finding"
        checker.expect(f"no {what}{' of ' + text if text else ''} at {where}", not hits, "; ".join(map(str, hits)))

    for fixture, text in PRAGMA_ERRORS:
        compiled = checker.compile(fixture, {"root": str(HERE), "mode": "report"})
        checker.expect(f"{fixture} is an error", compiled.returncode != 0 and text in compiled.stderr,
                       compiled.stderr[-2000:])

    noted = checker.compile("violations.cpp", {"root": str(HERE), "mode": "report"})
    checker.expect("report mode without out= gives notes and succeeds",
                   noted.returncode == 0 and "note: quarantine: raw_new_delete new" in noted.stderr,
                   noted.stderr[-2000:])
    failed = checker.compile("violations.cpp", {"root": str(HERE), "mode": "error"})
    checker.expect("error mode makes a finding an error",
                   failed.returncode != 0 and "error: quarantine: c_library_call memcpy" in failed.stderr,
                   failed.stderr[-2000:])
    opted = checker.compile("opt_out.cpp", {"root": str(HERE), "mode": "error"})
    opted_lines = {int(row.split(":")[1]) for row in opted.stderr.splitlines()
                   if "error: quarantine:" in row and row.startswith(str(HERE / "opt_out.cpp") + ":")}
    checker.expect("error mode does not stop an opted-out finding",
                   opted.returncode != 0 and opted_lines == {8, 14}, opted.stderr[-2000:])

    syntax_reports = checker.work / "syntax-reports"
    syntax = checker.compile("violations.cpp", {**arguments, "out": str(syntax_reports)}, ("-fsyntax-only",))
    syntax_findings = read_reports(syntax_reports) if syntax.returncode == 0 else []
    checker.expect("-fsyntax-only reports the namespace walk too",
                   any(f.kind == "std_object" and f.line == 15 for f in syntax_findings), syntax.stderr[-2000:])
    preprocessed_reports = checker.work / "preprocessed-reports"
    preprocessed = checker.compile("violations.cpp", {**arguments, "out": str(preprocessed_reports)},
                                   ("-E", "-o", os.devnull))
    checker.expect("-E writes no report", preprocessed.returncode == 0 and not preprocessed_reports.exists(),
                   preprocessed.stderr[-2000:])

    real = checker.compile("opt_out.cpp", {"root": str(HERE), "admitted": str(admitted)})
    checker.expect("the admitted list of the tree loads", real.returncode == 0, real.stderr[-2000:])
    malformed = checker.work / "malformed.txt"
    malformed.write_text("std::move\n", encoding="utf-8")
    broken = checker.compile("violations.cpp", {"root": str(HERE), "admitted": str(malformed)})
    checker.expect("an admitted entry without a reason is an error",
                   broken.returncode != 0 and "the reason is necessary" in broken.stderr, broken.stderr[-2000:])

    run_contract_rule(checker)


def contract_errors(stderr: str) -> list[tuple[str, int, str]]:
    """Return each error of the contract rule in STDERR, with the path relative to this directory."""
    errors: list[tuple[str, int, str]] = []
    for match in CONTRACT_ERROR.finditer(stderr):
        path = Path(match["path"])
        relative = path.relative_to(HERE).as_posix() if path.is_relative_to(HERE) else path.as_posix()
        errors.append((relative, int(match["line"]), match["specifier"]))
    return sorted(errors)


def run_contract_rule(checker: Checker) -> None:
    """Compile contracts.cpp in each mode, and compare the errors with CONTRACT_ERRORS."""
    outside = checker.work / "outside"
    outside.mkdir(exist_ok=True)
    (outside / "Outside.h").write_text(OUTSIDE_HEADER, encoding="utf-8")
    extra = ("-fcontracts", "-I", str(outside))
    expected = sorted(CONTRACT_ERRORS)
    contract_reports = checker.work / "contract-reports"
    runs = (
        ("mode=contracts", {"root": str(HERE), "mode": "contracts"}, ("-S", "-o", os.devnull)),
        ("mode=error", {"root": str(HERE), "mode": "error"}, ("-S", "-o", os.devnull)),
        ("mode=report", {"root": str(HERE), "mode": "report", "out": str(contract_reports)},
         ("-S", "-o", os.devnull)),
        ("mode=contracts with -fsyntax-only", {"root": str(HERE), "mode": "contracts"}, ("-fsyntax-only",)),
    )
    for name, arguments, stage in runs:
        compiled = checker.compile("contracts.cpp", arguments, stage, extra)
        found = contract_errors(compiled.stderr)
        missing = sorted(set(expected) - set(found))
        unexpected = [error for error in found if error not in expected or found.count(error) > 1]
        checker.expect(f"{name}: each contract specifier gives one error, and nothing else does",
                       compiled.returncode != 0 and not missing and not unexpected,
                       f"missing {missing}, unexpected {sorted(set(unexpected))}")
    opted = [f for f in read_reports(contract_reports)
             if f.kind == "opted_out" and f.file == "contracts.cpp" and f.line == 73]
    checker.expect("the opt-out region turns the specifier on line 73 into opted_out",
                   any(f.entity == "contract_specifier pre" for f in opted), "; ".join(map(str, opted)))

    named = checker.compile("contracts.cpp", {"root": str(HERE), "mode": "contracts"}, extra=extra)
    checker.expect("the error names CRUCIBLE_PRE, CRUCIBLE_POST and the reasons",
                   all(text in named.stderr for text in ("CRUCIBLE_PRE(condition)", "foundation/contracts/Pre.h",
                                                         "CRUCIBLE_POST(result, condition)",
                                                         "foundation/contracts/Post.h", "header unit",
                                                         "precompiled header", "constant evaluation")),
                   named.stderr[-2000:])
    quiet = checker.compile("violations.cpp", {"root": str(HERE), "mode": "contracts"})
    checker.expect("mode=contracts applies only the contract rule",
                   quiet.returncode == 0 and "quarantine:" not in quiet.stderr, quiet.stderr[-2000:])
    unknown = checker.compile("violations.cpp", {"root": str(HERE), "mode": "contract"})
    checker.expect("an unknown mode is an error",
                   unknown.returncode != 0 and "the modes are report, error and contracts" in unknown.stderr,
                   unknown.stderr[-2000:])


def main(argv: list[str]) -> int:
    """Parse the arguments, build the plugin and run every case."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
    parser.add_argument("--cxx", required=True, help="the compiler that loads the plugin")
    parser.add_argument("--source", required=True, type=Path, help="the source of the plugin")
    parser.add_argument("--admitted", required=True, type=Path, help="the admitted list of the tree")
    parser.add_argument("flags", nargs="*", help="the flags that build the plugin, after --")
    args = parser.parse_args(argv)
    with tempfile.TemporaryDirectory(prefix="quarantine-plugin-") as scratch:
        try:
            checker = Checker(args.cxx, args.source, args.flags, Path(scratch))
        except RuntimeError as error:
            print(f"check_plugin: {error}", file=sys.stderr)
            return 2
        run(checker, args.admitted)
    if checker.failures:
        print(f"check_plugin: {len(checker.failures)} expectation(s) failed:", file=sys.stderr)
        for failure in checker.failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("check_plugin: every expectation holds.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
