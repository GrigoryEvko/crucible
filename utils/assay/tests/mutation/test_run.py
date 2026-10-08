"""The test of the run of the gate mutants (assay.mutation.run) and of the write of the ledger of gate-mutants.

THE CASES
    verdict_cases   A planted header has gates that a negative fixture, a
                    syntax-only compile and a death test hold, and gates that
                    no test holds.  Each mutant must get its verdict and its
                    killer.
    probe_cases     The verdict of the probe, from planted SARIF logs.
    model_cases     A small CMake project: the reader of its tests and the
                    code model of the CMake file API.
    launched_cases  A small CMake project with a test that runs behind the
                    test launcher.  The run must give the program behind the
                    launcher as a candidate.
    write_cases     `guard write --rule gate-mutants --path HEADER --admit`
                    writes the row of each invalid mutant of the header, and it
                    keeps the row of a different header.  A check of the header
                    then gives no error.

THE TOOLS
    ASSAY_TEST_CXX names the compiler, ASSAY_TEST_CMAKE the cmake and
    ASSAY_TEST_NINJA the ninja.  The registration of the test in CMake gives
    them.  The run reads the parse trees of each planted header.  The test
    returns SKIPPED (77) when the tree-sitter kit is not installed.

`guard selftest mutation` runs the test.
"""

import os
import sys
import tempfile
from pathlib import Path

from assay.core import proc, testing
from assay.core import report as check_report
from assay.core.exit import ExitCode
from assay.core.paths import LOOP, REPO_ROOT
from assay.mutation import run
from assay.readers import cmake as cmake_reader
from assay.readers import compdb, sarif, tsast
from assay.readers import ninja as ninja_reader

TOOL_VARIABLES = ("ASSAY_TEST_CMAKE", "ASSAY_TEST_NINJA")
HEADER = "include/planted.h"
# The two macros stand for CRUCIBLE_PRE and CRUCIBLE_POST of foundation/contracts/.  The parse keeps the body of a
# macro as raw text, and each gate is then a call in the body of a function.  The result name is the first argument
# of CRUCIBLE_POST, and the span of the postcondition of positive holds only its condition.
PLANTED = """#pragma once
#include <concepts>
#include <type_traits>
#define CRUCIBLE_PRE(condition) contract_assert(condition)
#define CRUCIBLE_POST(result, condition) contract_assert(condition)
namespace planted {
template <class T> concept Small = sizeof(T) <= 4;
template <class T> requires Small<T> constexpr int take(T) { return 1; }
template <class T> requires std::integral<T> constexpr int count(T) { return 2; }
template <class T> requires(std::is_integral_v<T>) constexpr int widen(T) { return 3; }
inline int half(int n) { contract_assert(n % 2 == 0); return n / 2; }
inline int third(int n) noexcept { CRUCIBLE_PRE(n % 3 == 0); return n / 3; }
inline auto negate(int n) -> int { CRUCIBLE_PRE(n < 0); return -n; }
inline int positive(int n) { const int r = n < 0 ? -n : n + 1; CRUCIBLE_POST(r, r > 0); return r; }
template <class T> struct Wide {
    T v;
    friend constexpr bool operator==(Wide const&, Wide const&) requires Small<T> { return true; }
};
template <class T> requires Small<T> && /* and */ std::integral<T> constexpr int both(T) { return 4; }
template <class T> struct Box {};
template <class T> struct Box<std::type_identity<T>> { static_assert(sizeof(T) > 0); };
}
"""
FIXTURE = """#include "planted.h"
int main() { return planted::take(1.0L); }
"""
# The gate of widen has `requires(` with no space.  A mutant that puts `true` against it reads `requirestrue`, one
# name, and it does not parse.
WIDEN_FIXTURE = """#include "planted.h"
int main() { return planted::widen(1.5); }
"""
USER = """#include "planted.h"
static_assert(planted::take(1) == 1 && planted::count(1) == 2);
int main() {}
"""
# A test of the gate on the operator of Wide names the class, and not the name of the operator.  The tests of the
# tree do the same.
WIDE_USER = """#include "planted.h"
static_assert(!std::equality_comparable<planted::Wide<long double>>);
int main() {}
"""
# A death test: it exits 0 only when the contract of half stops the call.
RUNNER = """#include <contracts>
#include <cstdlib>
#include "planted.h"
void handle_contract_violation(const std::contracts::contract_violation&) noexcept { std::_Exit(0); }
int main() { (void)planted::half(3); return 1; }
"""
# A death test of the precondition of third: it exits 0 only when that precondition stops the call.
THIRD_RUNNER = """#include <contracts>
#include <cstdlib>
#include "planted.h"
void handle_contract_violation(const std::contracts::contract_violation&) noexcept { std::_Exit(0); }
int main() { (void)planted::third(4); return 1; }
"""
NINJA_FILE = """rule cxx
  command = {compiler} -std=c++26 -fcontracts -Iinclude $in -o $out
build run_half: cxx run_half.cpp | include/planted.h
build run_third: cxx run_third.cpp | include/planted.h
"""
# The verdict of each mutant of the planted header.
VERDICTS = {
    ("concept", "Small"): "killed",
    ("requires", "take"): "killed",
    ("requires", "count"): "survived",
    ("requires", "widen"): "killed",
    ("contract", "half"): "killed",
    ("contract", "third"): "killed",
    ("contract", "negate"): "survived",
    ("contract", "positive"): "survived",
    ("requires", "operator=="): "killed",
    ("requires", "both"): "survived",
    ("static_assert", "Box"): "survived",
}
# The killer of each mutant that the case names.
KILLERS = {
    ("contract", "half"): "run:run_half",
    ("contract", "third"): "run:run_third",
    ("requires", "widen"): "neg_widen_double",
    ("requires", "operator=="): "syntax:wide_user.cpp",
}
# A project with two tests that the reader of the tests and the file API must read.  CMake writes an argument of the
# first test as a bracket argument, and the second test is in a subdirectory.
MODEL_PROJECT = """cmake_minimum_required(VERSION 3.20)
project(planted_api CXX)
enable_testing()
add_executable(planted_test planted_test.cpp)
add_test(NAME planted_case COMMAND planted_test "has \\"quotes\\"" plain)
add_subdirectory(sub)
"""
MODEL_SUBDIRECTORY = """add_test(NAME sub_case COMMAND planted_test second)
"""
# A project with a test that runs behind the test launcher, as each test of the tree does.  The test source is in
# test/, where the run looks for the source of a test.
LAUNCHED_PROJECT = """cmake_minimum_required(VERSION 3.29)
project(planted_launched CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_TEST_LAUNCHER {python} -S {loop} test-step --name planted_launched)
add_compile_options(-std=c++26 -fcontracts)
include_directories(include)
enable_testing()
add_executable(test_launched test/test_launched.cpp)
add_test(NAME test_launched COMMAND test_launched)
"""
LAUNCHED_TEST = """#include "planted.h"
static_assert(planted::take(1) == 1);
int main() { return planted::take(2) - 1; }
"""
# The invalid plant of the rule and its header, and a row of a different header that a write must keep.
INVALID_PLANT = REPO_ROOT / "utils" / "plants" / "gate-mutants" / "plant-invalid-mutant"
INVALID_HEADER = "include/planted/holder.h"
OTHER_ROW = "include/planted/other.h | requires | admit | Other: (true) | a planted row of another header"


def configure(cmake: str, ninja: str, cxx: str, source: Path, build: Path) -> proc.Completed:
    """Configure a planted project with the Ninja generator, with the query of the replies of the CMake file API."""
    cmake_reader.request(build)
    return proc.run(
        proc.Command(
            [
                cmake,
                "-S",
                str(source),
                "-B",
                str(build),
                "-G",
                "Ninja",
                f"-DCMAKE_MAKE_PROGRAM={ninja}",
                f"-DCMAKE_CXX_COMPILER={cxx}",
            ],
            timeout_s=proc.TOOL_S,
        )
    )


def verdict_cases(expect: testing.Expect) -> None:
    """Run the mutants of the planted header with candidates of each tier, and check each verdict and killer."""
    cxx, ninja = testing.patched_compiler(), os.environ.get("ASSAY_TEST_NINJA", "")
    expect("ASSAY_TEST_CXX and ASSAY_TEST_NINJA name the compiler and the ninja", bool(cxx and ninja))
    if not (cxx and ninja):
        return
    with tempfile.TemporaryDirectory(prefix="mutation-verdicts-") as directory:
        root = Path(directory)
        (root / "include").mkdir()
        (root / HEADER).write_text(PLANTED, encoding="utf-8")
        for name, text in (
            ("neg_take_large.cpp", FIXTURE),
            ("neg_widen_double.cpp", WIDEN_FIXTURE),
            ("user.cpp", USER),
            ("wide_user.cpp", WIDE_USER),
            ("run_half.cpp", RUNNER),
            ("run_third.cpp", THIRD_RUNNER),
        ):
            (root / name).write_text(text, encoding="utf-8")
        ninja_reader.plant_graph(root, NINJA_FILE.format(compiler=cxx))
        base = [cxx, "-std=c++26", "-fcontracts", f"-I{root / 'include'}"]
        compdb.plant(
            root,
            [
                {
                    "directory": str(root),
                    "file": str(root / name),
                    "arguments": [*base, "-c", str(root / name), "-o", str(root / f"{name}.o")],
                }
                for name in ("neg_take_large.cpp", "neg_widen_double.cpp", "user.cpp")
            ],
        )
        fixture = [sys.executable, str(LOOP), run.FIXTURE_SUBCOMMAND, str(root)]
        headers = frozenset({HEADER})
        candidates = [
            run.Candidate(
                "neg_take_large",
                "neg",
                [*fixture, str(root / "neg_take_large.cpp"), "neg_take_large", "constraints not satisfied", "Small"],
                str(root),
                {},
                str(root / "neg_take_large.cpp"),
                headers,
            ),
            run.Candidate(
                "neg_widen_double",
                "neg",
                [
                    *fixture,
                    str(root / "neg_widen_double.cpp"),
                    "neg_widen_double",
                    "constraints not satisfied",
                    "is_integral_v",
                ],
                str(root),
                {},
                str(root / "neg_widen_double.cpp"),
                headers,
            ),
            run.Candidate(
                "syntax:user.cpp",
                "syntax",
                [*base, "-fsyntax-only", str(root / "user.cpp")],
                str(root),
                {},
                str(root / "user.cpp"),
                headers,
            ),
            run.Candidate(
                "syntax:wide_user.cpp",
                "syntax",
                [*base, "-fsyntax-only", str(root / "wide_user.cpp")],
                str(root),
                {},
                str(root / "wide_user.cpp"),
                headers,
            ),
            run.Candidate(
                "run:run_half",
                "run",
                [str(root / "run_half")],
                str(root),
                {},
                str(root / "run_half.cpp"),
                headers,
                target="run_half",
                build_dir=str(root),
            ),
            run.Candidate(
                "run:run_third",
                "run",
                [str(root / "run_third")],
                str(root),
                {},
                str(root / "run_third.cpp"),
                headers,
                target="run_third",
                build_dir=str(root),
            ),
        ]
        outcomes = run.run_header(HEADER, root, candidates, 2, 100, 100, probe_argv=base, ninja_program=ninja)
        verdicts = {(outcome.kind, outcome.entity): outcome.status for outcome in outcomes}
        killers = {(outcome.kind, outcome.entity): outcome.killer for outcome in outcomes}
        for gate, verdict in VERDICTS.items():
            expect(f"the mutant of the {gate[0]} gate of {gate[1]} is {verdict}", verdicts.get(gate) == verdict)
        for gate, killer in KILLERS.items():
            expect(f"{killer} kills the mutant of the {gate[0]} gate of {gate[1]}", killers.get(gate) == killer)
        # A comment adjacent to && does not stop the division of the constraint of `both`, and it has two gates.
        expect(
            "the constraint of both divides into two gates", sum(outcome.entity == "both" for outcome in outcomes) == 2
        )
        expect("each gate of the header has one mutant", len(outcomes) == len(VERDICTS) + 1)
        spans = {(outcome.kind, outcome.entity): outcome.original for outcome in outcomes}
        expect(
            "the span of the postcondition of positive holds its condition only",
            spans.get(("contract", "positive")) == "r > 0",
        )
        expect("the run puts the header back", (root / HEADER).read_text(encoding="utf-8") == PLANTED)


def probe_cases(expect: testing.Expect) -> None:
    """Check that only a refusal by a check of the header kills a mutant at the probe (assay.mutation.run)."""
    cases = {
        ("static assertion failed: small",): "killed",
        ("call of overloaded 'take(int)' is ambiguous",): "killed",
        ("'requirestrue' does not name a type",): "invalid",
        ("expected ';' before '}' token",): "invalid",
        (
            "redeclaration of 'template<class T>  requires  true struct S' with different constraints",
            "static assertion failed",
        ): "invalid",
        (): "invalid",
    }
    for messages, verdict in cases.items():
        log = sarif.Log(
            "cc1plus",
            tuple(
                sarif.Diagnostic(sarif.Kind.ERROR, None, "planted.h", 3, 1, 0, message, "", 0, False, "", ())
                for message in messages
            ),
        )
        expect(f"the probe errors {messages!r} give the verdict {verdict}", run.probe_verdict(log) == verdict)
    expect("a probe with no log gives the verdict invalid", run.probe_verdict(None) == "invalid")


def model_cases(expect: testing.Expect) -> None:
    """Configure a small project, and check what the reader of its tests and the file API read from it.

    CMake writes an argument of the first test that holds quotation marks as a
    bracket argument, and the reader must keep it.
    """
    cxx, (cmake, ninja) = testing.patched_compiler(), (os.environ.get(name, "") for name in TOOL_VARIABLES)
    expect("ASSAY_TEST_CMAKE and ASSAY_TEST_NINJA name the cmake and the ninja", bool(cmake and ninja and cxx))
    if not (cmake and ninja and cxx):
        return
    with tempfile.TemporaryDirectory(prefix="mutation-model-") as directory:
        source, build = Path(directory) / "src", Path(directory) / "build"
        (source / "sub").mkdir(parents=True)
        (source / "CMakeLists.txt").write_text(MODEL_PROJECT, encoding="utf-8")
        (source / "sub" / "CMakeLists.txt").write_text(MODEL_SUBDIRECTORY, encoding="utf-8")
        (source / "planted_test.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
        configured = configure(cmake, ninja, cxx, source, build)
        expect("the planted project configures", configured.exit == 0)
        if configured.exit != 0:
            check_report.write_line(configured.err[-2000:], is_error=True)
            return
        executable = str((build / "planted_test").resolve())
        commands = run._ctest_commands(build)
        expect(
            "the reader of the tests keeps the argument with quotes",
            commands.get("planted_case") == [executable, 'has "quotes"', "plain"],
        )
        expect("the reader of the tests reads the subdirectory", commands.get("sub_case") == [executable, "second"])
        expect(
            "the file API gives the source of the test",
            run._target_sources(build).get(executable) == [str((source / "planted_test.cpp").resolve())],
        )


def launched_cases(expect: testing.Expect) -> None:
    """Configure a project with a test that runs behind the test launcher, and check its candidates.

    The command of the test starts with `loop test-step`.  The run must take the
    program behind the launcher, and give its syntax candidate and its run
    candidate.
    """
    cxx, (cmake, ninja) = testing.patched_compiler(), (os.environ.get(name, "") for name in TOOL_VARIABLES)
    if not (cmake and ninja and cxx):
        expect("ASSAY_TEST_CMAKE and ASSAY_TEST_NINJA name the cmake and the ninja", False)
        return
    with tempfile.TemporaryDirectory(prefix="mutation-launched-") as directory:
        source, build = Path(directory) / "src", Path(directory) / "build"
        (source / "include").mkdir(parents=True)
        (source / "test").mkdir()
        (source / "CMakeLists.txt").write_text(
            LAUNCHED_PROJECT.format(python=sys.executable, loop=LOOP), encoding="utf-8"
        )
        (source / HEADER).write_text(PLANTED, encoding="utf-8")
        (source / "test" / "test_launched.cpp").write_text(LAUNCHED_TEST, encoding="utf-8")
        configured = configure(cmake, ninja, cxx, source, build)
        expect("the planted project with the launcher configures", configured.exit == 0)
        if configured.exit != 0:
            check_report.write_line(configured.err[-2000:], is_error=True)
            return
        candidates = run.load_candidates(build, run.header_sets(build, source.resolve(), 2))
        names = {candidate.name: candidate for candidate in candidates}
        expect("the test behind the launcher gives a syntax candidate", "syntax:test_launched.cpp" in names)
        launched = names.get("run:test_launched")
        expect(
            "the test behind the launcher gives a run candidate of its program",
            launched is not None and launched.argv == [str((build / "test_launched").resolve())],
        )


def write_cases(expect: testing.Expect) -> None:
    """Write the ledger of the invalid plant of the rule with --path, and check the rows and a check after it."""
    from assay.guard import check, write
    from assay.guard.engines.buildsys import gate_mutants
    from assay.guard.engines.meta import runner as meta_runner

    with tempfile.TemporaryDirectory(prefix="mutation-write-") as directory:
        try:
            context = gate_mutants.plant_context(INVALID_PLANT, Path(directory) / "tree")
        except meta_runner.NotApplicable as reason:
            expect(f"the invalid plant of the rule configures: {reason}", False)
            return
        if context.build_dir is None:
            expect("the invalid plant of the rule gives a build directory", False)
            return
        ledger = context.root / "utils" / "ledgers" / "gate-mutants.txt"
        ledger.parent.mkdir(parents=True, exist_ok=True)
        ledger.write_text(f"format 1\nrule gate-mutants\n{OTHER_ROW}\n", encoding="utf-8")
        words = [
            "--rule",
            "gate-mutants",
            "--root",
            str(context.root),
            "--build-dir",
            str(context.build_dir),
            "--path",
            INVALID_HEADER,
        ]
        before, _ = testing.captured(lambda: check.main(words))
        expect("a check of the header with an invalid mutant and no row of it fails", before == ExitCode.FINDINGS)
        status, _ = testing.captured(lambda: write.main([*words, "--admit", "--reason", "a planted reason"]))
        rows = ledger.read_text(encoding="utf-8")
        expect("guard write writes the ledger", status == ExitCode.OK)
        expect(
            "the ledger holds a row of the invalid gate of the header",
            f"{INVALID_HEADER} | requires | admit | Holder: (sizeof(T) <= 4) | a planted reason" in rows,
        )
        expect("the ledger keeps the row of the other header", OTHER_ROW in rows)
        after, _ = testing.captured(lambda: check.main(words))
        expect(
            "a check of the header passes after the write, and the row of the other header gives no error",
            after == ExitCode.OK,
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each (THE CASES)."""
    try:
        tsast.kit_dir()
    except tsast.KitMissing as missing:
        check_report.write_line(f"tests.mutation.test_run: SKIP, {missing}")
        return ExitCode.SKIPPED
    return testing.run(
        __name__.removeprefix("assay."), argv, (verdict_cases, probe_cases, model_cases, launched_cases, write_cases)
    )
