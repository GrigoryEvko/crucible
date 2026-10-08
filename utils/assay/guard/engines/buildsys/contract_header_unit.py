"""contract-header-unit — the precondition of a template stays in a header unit and in a precompiled header.

THE DEFECT
    GCC 16 does not write the P2900 `pre` or `post` specifier of a template
    into a header unit or a precompiled header.  An importer then
    instantiates the template with no contract.  CRUCIBLE_PRE and
    CRUCIBLE_POST of foundation/contracts/ are statements of the body, and
    they stay.  That is one reason for the contract rule of the tree
    (CLAUDE.md section XII).

THE RULE
    The rule writes a small header with a template precondition and a
    template postcondition, each with a macro.  It compiles the header as an
    include, as a header unit and as a precompiled header.  Each of the three
    must stop a constant evaluation that breaks the contract.  The rule also
    compiles fixy/Mutation.h of the include directory of the tree as a header
    unit.  The bound of BoundedMonotonic must then stop a constant evaluation
    in the importer.

    The control is the same header with the specifiers.  The include must
    stop the evaluation, and the header unit must not.  When a compiler keeps
    the specifier in a header unit, the rule fails.  The contract rule and
    CLAUDE.md section XV then must get a new reason.  The control has no
    precompiled header, because the result of GCC 16 changes from one
    precompiled header to the next.  Of 25 precompiled headers of the same
    file, 2 kept the postcondition of the class template member.

    The compiler is CMAKE_CXX_COMPILER of the CMake cache of --build-dir.
    Each use compiles with a SARIF log and with the include trace of -H.  The
    rule reads the diagnostics from the log (assay.readers.sarif), and the
    precompiled headers from the trace (assay.readers.include_trace).  The
    rule gives one error for each case that does not hold:
      `macro`      a case of the macros, at
                   include/foundation/contracts/Post.h
      `specifier`  a case of the specifiers, at the contract rule of
                   utils/tools/quarantine/contract.cpp
      `mutation`   a case of fixy/Mutation.h, at include/fixy/Mutation.h
      `input`      the cache names no compiler
    The entity is the name of the case.  The end of the standard error of a
    case that does not hold goes to the standard error.  The rule has no
    ledger.

THE PLANTS
    A plant holds build.txt of assay.guard.engines.buildsys.planted, and the
    cache of build.txt names the compiler.  It also holds a planted include
    directory include/ with foundation/contracts/Post.h and fixy/Mutation.h.
    The planted headers are small, and each case compiles in a short time.
    The compiler is {cxx}, or the planted compiler {root}/programs/cxx that
    the file compiler.txt gives, in the rows grammar of assay.core.rows:

        format 1
        stop PATTERN | MESSAGE   stop each compile with an argument that
                                 matches the shell pattern PATTERN: write
                                 MESSAGE to the standard error, and exit 1

    The planted compiler runs {cxx} for each other compile.  A pattern holds
    only letters, digits and the characters _ - . * ?.

Complexity: nineteen compiles of small units, and one header unit of fixy/Mutation.h.
"""

import tempfile
from dataclasses import dataclass
from pathlib import Path

from assay.core import proc
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.buildsys import planted
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import cmake as cmake_reader
from assay.readers import include_trace, sarif

RULE = "contract-header-unit"
COMPILER_FILE = "compiler.txt"
FORMAT = "1"
PLANTED_COMPILER = Path("programs") / "cxx"
PATTERN_CHARACTERS = frozenset("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-.*?")
COMPILER_ENTRY = "CMAKE_CXX_COMPILER"
FLAGS = ("-std=c++26", "-fcontracts", "-freflection", "-fdiagnostics-color=never")
MACRO_PATH = "include/foundation/contracts/Post.h"
SPECIFIER_PATH = "utils/tools/quarantine/contract.cpp"
MUTATION_PATH = "include/fixy/Mutation.h"
DETAIL_CHARACTERS = 1500
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
# A macro stops the static assertion.  A specifier reports the contract itself.  A record of the log with one of
# these texts in its message shows that the compile stopped the evaluation.
STOPPED = (
    "non-constant condition for static assertion",
    "contract predicate is false",
    "contract condition is not constant",
)


@dataclass(frozen=True, slots=True)
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
    """Compile in one scratch directory, and keep a finding for each case that does not hold."""

    __slots__ = ("cxx", "found", "include", "work")

    def __init__(self, cxx: str, include: Path, work: Path) -> None:
        """Keep the compiler, the include directory of the tree and the scratch directory."""
        self.cxx = cxx
        self.include = include
        self.work = work
        self.found: list[check_report.Finding] = []

    def compile(self, *arguments: str) -> proc.Completed:
        """Run the compiler in the scratch directory with the flags of each compile (FLAGS)."""
        command = [self.cxx, *FLAGS, "-I", str(self.include), "-I", str(self.work), *arguments]
        return proc.run(proc.Command(command, timeout_s=proc.TOOL_S, cwd=self.work))

    def expect(self, kind: str, path: str, name: str, holds: bool, detail: str = "") -> None:
        """Keep an error for a case that does not hold, and write the end of its output to the standard error."""
        if holds:
            return
        self.found.append(check_report.Finding("error", path, 0, RULE, f"the case does not hold: {name}", kind, name))
        if detail:
            check_report.write_line(f"{RULE}: {name}:\n{detail[-DETAIL_CHARACTERS:]}", is_error=True)

    def use(self, header_line: str, body: str, stem: str) -> Use:
        """Compile one use of a header with -fsyntax-only, the SARIF log and the include trace."""
        source = self.work / f"{stem}.cpp"
        source.write_text(header_line + body, encoding="utf-8")
        modules = ("-fmodules",) if header_line.startswith("import") else ()
        log_path = self.work / f"{stem}.sarif"
        done = self.compile(
            *modules, "-fsyntax-only", include_trace.TRACE_FLAG, f"{sarif.SARIF_FLAG}{log_path}", source.name
        )
        try:
            log: sarif.Log | None = sarif.read(log_path)
            problem = ""
        except (OSError, sarif.SarifError) as error:
            log, problem = None, f"the compile wrote no SARIF log that the rule can read: {error}"
        return Use(done, log, problem, include_trace.read(done.err))


def run_form(runner: Runner, header: str, name: str, is_macro: bool) -> None:
    """Compile one header as an include and as a header unit, and the header of the macros as a precompiled header.

    GCC reads NAME.gch adjacent to a header when it finds one.  The
    precompiled header and its copy of the header are then in a directory of
    their own.
    """
    kind, path = ("macro", MACRO_PATH) if is_macro else ("specifier", SPECIFIER_PATH)
    precompiled_dir = f"{name}_pch"
    for directory in (name, precompiled_dir) if is_macro else (name,):
        (runner.work / directory).mkdir()
        (runner.work / directory / "witness.h").write_text(header, encoding="utf-8")
    unit = runner.compile("-fmodules", "-fmodule-header=user", "-x", "c++-header", f"{name}/witness.h")
    runner.expect(kind, path, f"{name}: the header unit builds", unit.exit == 0, unit.err)
    if is_macro:
        pch = runner.compile(
            "-x", "c++-header", f"{precompiled_dir}/witness.h", "-o", f"{precompiled_dir}/witness.h.gch"
        )
        runner.expect(kind, path, f"{name}: the precompiled header builds", pch.exit == 0, pch.err)
    for contract, body in USES.items():
        included = runner.use(f'#include "{name}/witness.h"\n', body, f"{name}_{contract}_include")
        runner.expect(
            kind,
            path,
            f"{name}: the include reads no precompiled header and stops the evaluation that breaks the {contract}",
            included.is_stopped and not included.reads_precompiled,
            included.detail,
        )
        imported = runner.use(f'import "{name}/witness.h";\n', body, f"{name}_{contract}_import")
        if not is_macro:
            runner.expect(
                kind,
                path,
                f"{name}: the header unit loses the {contract} (the compiler keeps it: give the "
                f"contract rule a new reason)",
                imported.done.exit == 0,
                imported.detail,
            )
            continue
        runner.expect(
            kind,
            path,
            f"{name}: the header unit stops the evaluation that breaks the {contract}",
            imported.is_stopped,
            imported.detail,
        )
        precompiled = runner.use(f'#include "{precompiled_dir}/witness.h"\n', body, f"{name}_{contract}_pch")
        in_use = include_trace.precompiled(precompiled.trace)
        runner.expect(
            kind,
            path,
            f"{name}: the compile reads the precompiled header and stops the evaluation that breaks the {contract}",
            f"{precompiled_dir}/witness.h.gch" in in_use and precompiled.is_stopped,
            precompiled.detail,
        )


def run_mutation(runner: Runner) -> None:
    """Import fixy/Mutation.h as a header unit, and break the bound of BoundedMonotonic."""
    header = runner.include / "fixy" / "Mutation.h"
    unit = runner.compile("-fmodules", "-fmodule-header", "-x", "c++-header", str(header))
    runner.expect("mutation", MUTATION_PATH, "fixy/Mutation.h: the header unit builds", unit.exit == 0, unit.err)
    imported = runner.use("import <fixy/Mutation.h>;\n", MUTATION_USE, "mutation_import")
    runner.expect(
        "mutation",
        MUTATION_PATH,
        "fixy/Mutation.h: the header unit stops the evaluation that breaks the bound",
        imported.is_stopped,
        imported.detail,
    )
    included = runner.use("#include <fixy/Mutation.h>\n", MUTATION_USE, "mutation_include")
    runner.expect(
        "mutation",
        MUTATION_PATH,
        "fixy/Mutation.h: the include stops the evaluation that breaks the bound",
        included.is_stopped,
        included.detail,
    )


def findings(context: BuildContext) -> list[check_report.Finding]:
    """Compile each case with the compiler of the build, and give an error for each that does not hold (THE RULE).

    Raises:
        NotApplicable: If the call gives no build directory
    """
    build = context.require_build()
    try:
        entry = cmake_reader.cache(build).get(COMPILER_ENTRY)
    except cmake_reader.CmakeError as problem:
        return [
            check_report.Finding(
                "error",
                context.shown(build),
                0,
                RULE,
                f"the CMake cache of the build cannot be read: {problem}",
                "input",
                COMPILER_ENTRY,
            )
        ]
    if entry is None or not entry.value:
        return [
            check_report.Finding(
                "error",
                context.shown(build),
                0,
                RULE,
                f"the CMake cache of the build names no {COMPILER_ENTRY}.  {cmake_reader.AGAIN}",
                "input",
                COMPILER_ENTRY,
            )
        ]
    with tempfile.TemporaryDirectory(prefix="contract-header-unit-") as scratch:
        runner = Runner(entry.value, (context.root / "include").resolve(), Path(scratch))
        run_form(runner, MACRO_HEADER, "macro", is_macro=True)
        run_form(runner, SPECIFIER_HEADER, "specifier", is_macro=False)
        run_mutation(runner)
    return runner.found


# ── The plants ─────────────────────────────────────────────────────────────


def planted_compiler(root: Path, build: Path) -> None:
    """Write the planted compiler programs/cxx that the rows of compiler.txt give (THE PLANTS).

    Raises:
        rows_grammar.RowsError: If a row does not obey its shape
    """
    rows = rows_grammar.read(root / COMPILER_FILE, COMPILER_FILE)
    if not rows or rows[0].words != ("format", FORMAT):
        raise rows_grammar.RowsError(COMPILER_FILE, rows[0].line if rows else 0, f"the first row is 'format {FORMAT}'")
    arms: list[str] = []
    for row in rows[1:]:
        pattern = row.words[1] if len(row.words) == 2 else ""
        if row.words[0] != "stop" or len(row.cells) != 1 or not pattern or not set(pattern) <= PATTERN_CHARACTERS:
            raise row.error(
                f"a row of {COMPILER_FILE} is 'stop PATTERN | MESSAGE', and a pattern holds only letters, "
                f"digits and the characters _ - . * ?"
            )
        message = row.cells[0].replace("'", "'\\''")
        arms.append(f"    {pattern}) printf '%s\\n' '{message}' >&2; exit 1 ;;\n")
    program = root / PLANTED_COMPILER
    program.parent.mkdir(parents=True, exist_ok=True)
    real = planted.expanded("{cxx}", root, build)
    program.write_text(
        '#!/bin/sh\nfor word in "$@"; do\n  case "$word" in\n'
        + "".join(arms)
        + f"  esac\ndone\nexec '{real}' \"$@\"\n",
        encoding="utf-8",
    )
    program.chmod(0o755)


def plant_context(plant: Path, scratch: Path) -> BuildContext:
    """Copy a plant, make its planted build and its planted compiler, and return its context (THE PLANTS).

    Raises:
        NotApplicable: If build.txt or compiler.txt does not obey its rows
    """
    context = planted.plant_context(plant, scratch)
    if (scratch / COMPILER_FILE).is_file():
        try:
            planted_compiler(scratch, scratch / planted.BUILD)
        except rows_grammar.RowsError as problem:
            raise meta_runner.NotApplicable(f"the planted compiler does not read: {problem}") from None
    return context
