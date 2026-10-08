"""gate-mutants — each gate of a header that a call names has a test that a mutant of the gate fails.

THE RULE
    The call names headers with --path, as paths in the source tree of the
    build (assay.guard.engines.buildsys.runner, THE PATHS).  --build-dir is
    a configured build of an export of the tree.  The run writes each mutant
    into the header of the source tree of that build, and it puts the header
    back after each mutant (assay.mutation.run).  The source tree of the
    build is CMAKE_HOME_DIRECTORY of its CMake cache.  The rule refuses a
    build of the tree of --root, and it does not write into the tree.  With
    no --path, the rule does not apply, and it exits 77.  No ctest test runs
    the rule on the tree, because a run on each header takes hours.  The
    group mutation of `guard selftest` does a test of the run.

THE FINDINGS
    The kind of a finding of a mutant is the kind of its gate.  Its entity is
    NAME: TEXT, as in a row of the ledger.  The rule gives:
      * An error for each invalid mutant (assay.mutation.run, THE PROBE)
      * A warning for each mutant that no test kills.  The text of the
        warning tells when a cap stopped the candidates of the gate
      * The error `header` for a header that is not in the source tree of
        the build, or that does not compile alone
      * The error `input` for a build that the run cannot read, and for a
        build of the tree of --root.

THE LEDGER
    utils/ledgers/gate-mutants.txt has the policy admit.  A row admits the
    invalid mutant of one gate: HEADER | GATE KIND | admit | NAME: TEXT |
    REASON.  NAME is the gated entity, and TEXT is the gate text with its
    whitespace squeezed.  REASON tells why no mutant of the gate can parse,
    or which other test holds the gate.  The run judges only the rows of the
    headers that the call names.
        guard write --rule gate-mutants --build-dir BUILD --path HEADER --admit --reason TEXT
    writes a row for each invalid mutant of the headers, and it keeps the
    rows of each other header.

THE JOBS
    The run does JOBS compiles at a time, a quarter of the processors and at
    most 32.  The caps of the candidates of one gate are the caps of
    assay.mutation.run: NEGATIVE_CAP fixtures, SYNTAX_CAP compiles and
    RUN_CAP test executables.

THE PLANTS
    A plant holds a planted export export/, a CMake project with its headers
    and its tests.  Each C++ file of the export has the suffix .planted.  The
    plant also holds the file gate.txt in the rows grammar of
    assay.core.rows:

        format 1
        path HEADER          a header of the export that the call names
        source DIRECTORY     the source tree of the build, export by default

    plant_context() configures the export with the Ninja generator into
    export-build/ of the copy.  The cmake is that of ASSAY_TEST_CMAKE, the
    ninja that of ASSAY_TEST_NINJA, and the compiler that of ASSAY_TEST_CXX.

Complexity: one run of the preprocessor for each compile entry of the build, then the run of each header
(assay.mutation.run).
"""

import os
import signal
from pathlib import Path
from types import FrameType

from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import testing
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.mutation import run as mutation_run
from assay.readers import cmake as cmake_reader
from assay.readers import tsast

RULE = "gate-mutants"
JOBS = max(1, min(32, (os.cpu_count() or 1) // 4))
NEGATIVE_CAP = 80
SYNTAX_CAP = 12
RUN_CAP = 4
GATE_FILE = "gate.txt"
FORMAT = "1"
EXPORT = "export"
EXPORT_BUILD = "export-build"
GENERATOR = "Ninja"


class Stopped(BaseException):
    """A SIGTERM stops the run, after the header with a mutant gets its bytes back."""


def stop_on_term(signum: int, _frame: FrameType | None) -> None:
    """Stop the run on SIGTERM.  The run then puts the header back before the process ends."""
    raise Stopped(f"the run stops on the signal {signum}")


def error(path: str, message: str, kind: str, entity: str) -> check_report.Finding:
    """Return one error of the rule."""
    return check_report.Finding("error", path, 0, RULE, message, kind, entity)


def outcome_finding(outcome: mutation_run.Outcome) -> check_report.Finding | None:
    """Return the finding of one mutant: an error for an invalid mutant, a warning for a survivor, else None."""
    key = mutation_run.exemption_row(outcome.header, outcome.kind, outcome.entity, outcome.original)
    place = f"line {outcome.line}, the {outcome.kind} gate of {outcome.entity or 'the header'}"
    if outcome.status == "invalid":
        return check_report.Finding(
            "error",
            outcome.header,
            outcome.line,
            RULE,
            f"the mutant of {place} does not parse, so no test holds the gate.  Correct the mutant in "
            f"utils/assay/mutation/gates.py, or admit the gate with a row and a reason",
            key.kind,
            key.entity,
        )
    if outcome.status.startswith("survived"):
        partial = ", in the candidates under the caps" if outcome.status == "survived-partial" else ""
        return check_report.Finding(
            "warning",
            outcome.header,
            outcome.line,
            RULE,
            f"no test kills the mutant of {place}{partial}.  Add a test or a fixture that the mutant fails",
            key.kind,
            key.entity,
        )
    return None


def build_inputs(context: BuildContext, build: Path) -> tuple[Path, str] | check_report.Finding:
    """Return the source tree and the ninja of the build, or the error that refuses the build (THE RULE)."""
    shown = context.shown(build)
    try:
        cache = {name: entry.value for name, entry in cmake_reader.cache(build).items()}
    except cmake_reader.CmakeError as problem:
        return error(shown, f"the CMake cache of the build cannot be read: {problem}", "input", "the CMake cache")
    source = cache.get("CMAKE_HOME_DIRECTORY", "")
    if not source or not Path(source).is_dir():
        return error(
            shown, f"the CMake cache of the build names no source tree ('{source}')", "input", "CMAKE_HOME_DIRECTORY"
        )
    src_root = Path(source).resolve()
    if src_root == context.root.resolve():
        return error(
            shown,
            f"the build is a build of the tree {context.root}, and the run writes each mutant into "
            f"the source tree of the build.  Export the tree, configure the export, and give its "
            f"build",
            "input",
            "CMAKE_HOME_DIRECTORY",
        )
    return src_root, cache.get("CMAKE_MAKE_PROGRAM", "") or mutation_run.NINJA


def findings(context: BuildContext) -> list[check_report.Finding]:
    """Run the mutants of each header that the call names, and give their findings (THE RULE).

    Raises:
        NotApplicable: If the call gives no build directory or no path, or the tree-sitter kit is not installed
    """
    build = context.require_build()
    if not context.paths:
        raise meta_runner.NotApplicable(f"{RULE} runs on the headers that a call names with --path HEADER")
    inputs = build_inputs(context, build)
    if isinstance(inputs, check_report.Finding):
        return [inputs]
    src_root, ninja_program = inputs
    found: list[check_report.Finding] = []
    previous = signal.signal(signal.SIGTERM, stop_on_term)
    try:
        candidates = mutation_run.load_candidates(build, mutation_run.header_sets(build, src_root, JOBS))
        for header in context.paths:
            if not (src_root / header).is_file():
                found.append(
                    error(header, f"the source tree {src_root} of the build has no such header", "header", header)
                )
                continue
            try:
                outcomes = mutation_run.run_header(
                    header,
                    src_root,
                    candidates,
                    JOBS,
                    NEGATIVE_CAP,
                    SYNTAX_CAP,
                    run_cap=RUN_CAP,
                    ninja_program=ninja_program,
                )
            except mutation_run.RunError as problem:
                found.append(error(header, str(problem), "header", header))
                continue
            found += [finding for finding in map(outcome_finding, outcomes) if finding is not None]
    except mutation_run.RunError as problem:
        found.append(error(context.shown(build), f"the run cannot read the build: {problem}", "input", "build"))
    except tsast.KitMissing as missing:
        raise meta_runner.NotApplicable(str(missing)) from None
    finally:
        signal.signal(signal.SIGTERM, previous)
    return found


# ── The plants ─────────────────────────────────────────────────────────────


def read_gate(root: Path) -> tuple[list[str], str]:
    """Return the headers and the source tree that the rows of gate.txt give (THE PLANTS).

    Raises:
        rows_grammar.RowsError: If a row does not obey its shape
    """
    rows = rows_grammar.read(root / GATE_FILE, GATE_FILE)
    if not rows or rows[0].words != ("format", FORMAT):
        raise rows_grammar.RowsError(GATE_FILE, rows[0].line if rows else 0, f"the first row is 'format {FORMAT}'")
    paths: list[str] = []
    source = EXPORT
    for row in rows[1:]:
        if len(row.words) != 2 or row.cells or row.words[0] not in ("path", "source"):
            raise row.error(f"a row of {GATE_FILE} is 'path HEADER' or 'source DIRECTORY'")
        if row.words[0] == "path":
            paths.append(row.words[1])
        else:
            source = row.words[1]
    return paths, source


def plant_context(plant: Path, scratch: Path) -> BuildContext:
    """Copy a plant, configure its planted export, and return the context of the build of the export (THE PLANTS).

    Raises:
        NotApplicable: If gate.txt does not read, a tool is not there, or the export does not configure
    """
    from assay.core import proc
    from assay.guard.engines.buildsys import reconfigure_noop

    meta_runner.copy_plant(plant, scratch)
    try:
        paths, source = read_gate(scratch)
    except rows_grammar.RowsError as problem:
        raise meta_runner.NotApplicable(f"the planted gate file does not read: {problem}") from None
    cmake, ninja = reconfigure_noop.planted_tools()
    cxx = testing.patched_compiler()
    if not cxx:
        raise meta_runner.NotApplicable("no compiler: give ASSAY_TEST_CXX")
    build = scratch / EXPORT_BUILD
    cmake_reader.request(build)
    try:
        done = proc.run(
            proc.Command(
                [
                    cmake,
                    "-G",
                    GENERATOR,
                    f"-DCMAKE_MAKE_PROGRAM={ninja}",
                    f"-DCMAKE_CXX_COMPILER={cxx}",
                    "-S",
                    str(scratch / source),
                    "-B",
                    str(build),
                ],
                timeout_s=proc.TOOL_S,
                stderr=proc.Output.MERGE,
            )
        )
    except proc.ToolFailed as problem:
        raise meta_runner.NotApplicable(f"the planted export does not configure: {problem}") from None
    if done.exit != 0:
        raise meta_runner.NotApplicable(f"the planted export does not configure:\n{done.out[-2000:]}")
    return BuildContext(scratch, build, paths=paths)
