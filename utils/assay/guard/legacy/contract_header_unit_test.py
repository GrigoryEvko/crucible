"""contract_header_unit_test — the precondition of a template stays in a header unit and in a precompiled header.

GCC 16 does not write the P2900 `pre` or `post` specifier of a template into
a header unit or a precompiled header.  An importer then instantiates the
template with no contract.  CRUCIBLE_PRE and CRUCIBLE_POST of
foundation/contracts/ are statements of the body, so they stay.  That is one
reason for the contract rule of the tree (CLAUDE.md section XII).

The test writes a small header with a template precondition and a template
postcondition in the macro form.  It compiles the header as an include, as a
header unit and as a precompiled header, and each form must stop a constant
evaluation that breaks the contract.  It also compiles fixy/Mutation.h as a
header unit, and the bound of BoundedMonotonic must stop a constant
evaluation in the importer.

The control is the same header in the specifier form.  The include must stop
the evaluation, and the header unit must not.  So the test fails when a
compiler keeps the specifier in a header unit: then the contract rule and
CLAUDE.md section XV need a new reason.  The control has no precompiled
header, because the result of GCC 16 changes from one precompiled header to
the next: of 25 precompiled headers of the same file, 2 kept the
postcondition of the class template member.

Each use compiles with a SARIF log and with the include trace of -H.  The test
reads the diagnostics from the log (assay.readers.sarif), and the precompiled
headers from the trace (assay.readers.include_trace).  For that reason,
`--cxx` names the patched compiler.

usage: guard check --rule contract-header-unit --cxx CXX --include INCLUDE_DIR

Exit 0 when each case holds, 1 when one fails, 2 on a usage error.
"""

from __future__ import annotations

import argparse
import dataclasses
import sys
import tempfile
from pathlib import Path

from assay.core import proc
from assay.readers import include_trace, sarif

FLAGS = ("-std=c++26", "-fcontracts", "-freflection", "-fdiagnostics-color=never")
MACRO_HEADER = """#pragma once
#include <foundation/contracts/Post.h>
template <class T>
constexpr int checked(T value) noexcept {
    CRUCIBLE_PRE(value > T{});
    return static_cast<int>(value);
}
template <class T>
struct Box {
    constexpr int doubled(int value) const noexcept {
        int const result = value * 2;
        CRUCIBLE_POST(result, result > 0);
        return result;
    }
};
"""
SPECIFIER_HEADER = """#pragma once
template <class T>
constexpr int checked(T value) noexcept pre(value > T{}) { return static_cast<int>(value); }
template <class T>
struct Box {
    constexpr int doubled(int value) const noexcept post(result : result > 0) { return value * 2; }
};
"""
# Each use breaks one contract in a constant evaluation.
USES = {
    "precondition": "static_assert(checked(0) == 0);\n",
    "postcondition": "static_assert(Box<int>{}.doubled(0) == 0);\n",
}
MUTATION_USE = "static_assert(::fixy::mint_bounded_monotonic<unsigned, 4u>(9u).get() == 9u);\n"
# The macro form stops the static assertion.  The specifier form reports the
# contract itself.  A record of the log with one of these texts in its message
# shows that the compile stopped the evaluation.
STOPPED = ("non-constant condition for static assertion", "contract predicate is false",
           "contract condition is not constant")


@dataclasses.dataclass(frozen=True, slots=True)
class Use:
    """One compile of a use: the completed process, the records of its SARIF log, and its include trace.

    `log` is None when the compile wrote no log that the reader reads, and
    `problem` then gives the reason.
    """

    done: proc.Completed
    log: sarif.Log | None
    problem: str
    trace: tuple[include_trace.IncludeStep, ...]

    @property
    def is_stopped(self) -> bool:
        """Return True when the compile fails and a record of the log names a stopped evaluation (STOPPED)."""
        if self.done.exit == 0 or self.log is None:
            return False
        return any(text in record.message for record in sarif.records(self.log) for text in STOPPED)

    @property
    def reads_precompiled(self) -> bool:
        """Return True when the trace names a precompiled header, in use or not."""
        return any(step.path.endswith(".gch") for step in self.trace)

    @property
    def detail(self) -> str:
        """Return the text of a case that does not hold: the reason of a missing log and the standard error."""
        return f"{self.problem}\n{self.done.err}" if self.problem else self.done.err


class Runner:
    """Compile in one scratch directory, and record the verdict of each case."""

    def __init__(self, cxx: str, include: Path, work: Path) -> None:
        self.cxx = cxx
        self.include = include
        self.work = work
        self.failures: list[str] = []

    def compile(self, *arguments: str) -> proc.Completed:
        """Run the compiler in the scratch directory with the common flags."""
        command = [self.cxx, *FLAGS, "-I", str(self.include), "-I", str(self.work), *arguments]
        return proc.run(proc.Command(command, timeout_s=proc.TOOL_S, cwd=self.work))

    def expect(self, name: str, holds: bool, detail: str = "") -> None:
        """Record the verdict of one case."""
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            self.failures.append(f"{name}{': ' + detail[-1500:] if detail else ''}")

    def use(self, header_line: str, body: str, stem: str) -> Use:
        """Compile one use of a header with -fsyntax-only, the SARIF log and the include trace."""
        source = self.work / f"{stem}.cpp"
        source.write_text(header_line + body, encoding="utf-8")
        modules = ("-fmodules",) if header_line.startswith("import") else ()
        log_path = self.work / f"{stem}.sarif"
        done = self.compile(*modules, "-fsyntax-only", include_trace.TRACE_FLAG, f"{sarif.SARIF_FLAG}{log_path}",
                            source.name)
        try:
            log: sarif.Log | None = sarif.read(log_path)
            problem = ""
        except (OSError, sarif.SarifError) as error:
            log, problem = None, f"the compile wrote no SARIF log that the test can read: {error}"
        return Use(done, log, problem, include_trace.read(done.err))


def run_form(runner: Runner, header: str, name: str, is_macro: bool) -> None:
    """Compile one header as an include and as a header unit, and the macro form also as a precompiled header.

    GCC reads NAME.gch beside a header when it finds one, so the precompiled
    header and its copy of the header are in a directory of their own.
    """
    precompiled_dir = f"{name}_pch"
    for directory in (name, precompiled_dir) if is_macro else (name,):
        (runner.work / directory).mkdir()
        (runner.work / directory / "witness.h").write_text(header, encoding="utf-8")
    unit = runner.compile("-fmodules", "-fmodule-header=user", "-x", "c++-header", f"{name}/witness.h")
    runner.expect(f"{name}: the header unit builds", unit.exit == 0, unit.err)
    if is_macro:
        pch = runner.compile("-x", "c++-header", f"{precompiled_dir}/witness.h", "-o",
                             f"{precompiled_dir}/witness.h.gch")
        runner.expect(f"{name}: the precompiled header builds", pch.exit == 0, pch.err)
    for contract, body in USES.items():
        included = runner.use(f'#include "{name}/witness.h"\n', body, f"{name}_{contract}_include")
        runner.expect(f"{name}: the include reads no precompiled header and stops the evaluation that breaks the "
                      f"{contract}", included.is_stopped and not included.reads_precompiled, included.detail)
        imported = runner.use(f'import "{name}/witness.h";\n', body, f"{name}_{contract}_import")
        if not is_macro:
            runner.expect(f"{name}: the header unit loses the {contract} (GCC keeps it: give the contract rule a "
                          f"new reason)", imported.done.exit == 0, imported.detail)
            continue
        runner.expect(f"{name}: the header unit stops the evaluation that breaks the {contract}",
                      imported.is_stopped, imported.detail)
        precompiled = runner.use(f'#include "{precompiled_dir}/witness.h"\n', body, f"{name}_{contract}_pch")
        in_use = include_trace.precompiled(precompiled.trace)
        runner.expect(f"{name}: the compile reads the precompiled header and stops the evaluation that breaks the "
                      f"{contract}", f"{precompiled_dir}/witness.h.gch" in in_use and precompiled.is_stopped,
                      precompiled.detail)


def run_mutation(runner: Runner) -> None:
    """Import fixy/Mutation.h as a header unit, and break the bound of BoundedMonotonic."""
    header = runner.include / "fixy" / "Mutation.h"
    unit = runner.compile("-fmodules", "-fmodule-header", "-x", "c++-header", str(header))
    runner.expect("fixy/Mutation.h: the header unit builds", unit.exit == 0, unit.err)
    imported = runner.use("import <fixy/Mutation.h>;\n", MUTATION_USE, "mutation_import")
    runner.expect("fixy/Mutation.h: the header unit stops the evaluation that breaks the bound",
                  imported.is_stopped, imported.detail)
    included = runner.use("#include <fixy/Mutation.h>\n", MUTATION_USE, "mutation_include")
    runner.expect("fixy/Mutation.h: the include stops the evaluation that breaks the bound",
                  included.is_stopped, included.detail)


def main(argv: list[str]) -> int:
    """Parse the arguments and run each case."""
    parser = argparse.ArgumentParser(prog="guard check --rule contract-header-unit",
                                     description=__doc__.split("\n", 1)[0])
    parser.add_argument("--cxx", required=True, help="the compiler")
    parser.add_argument("--include", required=True, type=Path, help="the include directory of the tree")
    arguments = parser.parse_args(argv)
    with tempfile.TemporaryDirectory(prefix="contract-header-unit-") as scratch:
        runner = Runner(arguments.cxx, arguments.include.resolve(), Path(scratch))
        run_form(runner, MACRO_HEADER, "macro", is_macro=True)
        run_form(runner, SPECIFIER_HEADER, "specifier", is_macro=False)
        run_mutation(runner)
    if runner.failures:
        print(f"contract_header_unit_test: {len(runner.failures)} case(s) failed:", file=sys.stderr)
        for failure in runner.failures:
            print(f"  {failure}", file=sys.stderr)
        return 1
    print("contract_header_unit_test: every case holds.")
    return 0
