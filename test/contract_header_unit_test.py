#!/usr/bin/env python3
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

usage: contract_header_unit_test.py --cxx CXX --include INCLUDE_DIR

Exit 0 when each case holds, 1 when one fails, 2 on a usage error.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
import tempfile
from pathlib import Path

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
# contract itself.
STOPPED = re.compile(r"non-constant condition for static assertion|contract predicate is false|"
                     r"contract condition is not constant")


class Runner:
    """Compile in one scratch directory, and record the verdict of each case."""

    def __init__(self, cxx: str, include: Path, work: Path) -> None:
        self.cxx = cxx
        self.include = include
        self.work = work
        self.failures: list[str] = []

    def compile(self, *arguments: str) -> subprocess.CompletedProcess[str]:
        """Run the compiler in the scratch directory with the common flags."""
        command = [self.cxx, *FLAGS, "-I", str(self.include), "-I", str(self.work), *arguments]
        return subprocess.run(command, cwd=self.work, capture_output=True, text=True)

    def expect(self, name: str, holds: bool, detail: str = "") -> None:
        """Record the verdict of one case."""
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            self.failures.append(f"{name}{': ' + detail[-1500:] if detail else ''}")

    def use(self, header_line: str, body: str, stem: str) -> subprocess.CompletedProcess[str]:
        """Compile one use of a header with -fsyntax-only."""
        source = self.work / f"{stem}.cpp"
        source.write_text(header_line + body, encoding="utf-8")
        modules = ("-fmodules",) if header_line.startswith("import") else ()
        return self.compile(*modules, "-fsyntax-only", "-H", source.name)


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
    runner.expect(f"{name}: the header unit builds", unit.returncode == 0, unit.stderr)
    if is_macro:
        pch = runner.compile("-x", "c++-header", f"{precompiled_dir}/witness.h", "-o",
                             f"{precompiled_dir}/witness.h.gch")
        runner.expect(f"{name}: the precompiled header builds", pch.returncode == 0, pch.stderr)
    for contract, body in USES.items():
        included = runner.use(f'#include "{name}/witness.h"\n', body, f"{name}_{contract}_include")
        runner.expect(f"{name}: the include reads no precompiled header and stops the evaluation that breaks the "
                      f"{contract}", included.returncode != 0 and STOPPED.search(included.stderr) is not None
                      and ".gch" not in included.stderr, included.stderr)
        imported = runner.use(f'import "{name}/witness.h";\n', body, f"{name}_{contract}_import")
        if not is_macro:
            runner.expect(f"{name}: the header unit loses the {contract} (GCC keeps it: give the contract rule a "
                          f"new reason)", imported.returncode == 0, imported.stderr)
            continue
        runner.expect(f"{name}: the header unit stops the evaluation that breaks the {contract}",
                      imported.returncode != 0 and STOPPED.search(imported.stderr) is not None, imported.stderr)
        precompiled = runner.use(f'#include "{precompiled_dir}/witness.h"\n', body, f"{name}_{contract}_pch")
        runner.expect(f"{name}: the compile reads the precompiled header and stops the evaluation that breaks the "
                      f"{contract}", f"! {precompiled_dir}/witness.h.gch" in precompiled.stderr
                      and precompiled.returncode != 0 and STOPPED.search(precompiled.stderr) is not None,
                      precompiled.stderr)


def run_mutation(runner: Runner) -> None:
    """Import fixy/Mutation.h as a header unit, and break the bound of BoundedMonotonic."""
    header = runner.include / "fixy" / "Mutation.h"
    unit = runner.compile("-fmodules", "-fmodule-header", "-x", "c++-header", str(header))
    runner.expect("fixy/Mutation.h: the header unit builds", unit.returncode == 0, unit.stderr)
    imported = runner.use("import <fixy/Mutation.h>;\n", MUTATION_USE, "mutation_import")
    runner.expect("fixy/Mutation.h: the header unit stops the evaluation that breaks the bound",
                  imported.returncode != 0 and STOPPED.search(imported.stderr) is not None, imported.stderr)
    included = runner.use("#include <fixy/Mutation.h>\n", MUTATION_USE, "mutation_include")
    runner.expect("fixy/Mutation.h: the include stops the evaluation that breaks the bound",
                  included.returncode != 0 and STOPPED.search(included.stderr) is not None, included.stderr)


def main(argv: list[str]) -> int:
    """Parse the arguments and run each case."""
    parser = argparse.ArgumentParser(description=__doc__.split("\n", 1)[0])
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


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
