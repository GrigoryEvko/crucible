"""vessel-acceptance — the acceptance gate of the PyTorch vessel in vessel/torch.

THE GATE
    The gate accepts the vessel of one build only when three facts hold:
    1. The build compiles the vessel against a PyTorch that has
       DispatchKey::Crucible.  Each vessel translation unit has -Werror on its
       compile line, and the gate builds the two vessel libraries with no
       error.  No library holds a sanitizer runtime.
    2. Each vessel Python test (vessel/torch/test_*.py) exits 0.
    3. Replay gives the bytes that PyTorch alone gives.
       vessel/torch/test_replay_equivalence.py is one of the tests of item 2.
    The gate derives its lists from the tree: each vessel/torch/*.cpp must be
    in the compile database with -Werror, and each vessel/torch/test_*.py must
    exit 0.  A new source or a new test joins the gate with no edit here.

THE BUILD
    The rule reads TORCH_DIR and CRUCIBLE_VESSEL_PYTHON from the CMake cache
    of --build-dir.  A build with an empty TORCH_DIR does not compile the
    vessel, and the rule does not apply to it.  Its test then exits 77, the
    code SKIPPED.  test/CMakeLists.txt disables the test of the rule in a
    build with a TORCH_DIR that the gate cannot operate.  That is a sanitizer
    build, a build with CRUCIBLE_WERROR off, and a TORCH_DIR with no
    torch/include.  ctest shows the test as not run.  With no
    entry CRUCIBLE_VESSEL_PYTHON, the interpreter is the one of the virtual
    environment that holds TORCH_DIR, or else python3, as the configure step
    gives it.

THE FINDINGS
    The rule gives:
      `torch`      the PyTorch of TORCH_DIR is missing, has no key, or is not
                   the PyTorch that the interpreter imports
      `build`      the build is not configured or has CRUCIBLE_WERROR off.
                   Or the build does not compile a vessel source with -Werror,
                   fails to build the vessel targets, or makes no vessel
                   library
      `sanitizer`  a vessel library holds a sanitizer runtime
      `test`       a vessel test does not exit 0, or the vessel has no test
    Item 2 runs only when item 1 holds.  The rule has no ledger, and each
    finding is an error.  The end of the output of a test that fails goes to
    the standard error.

THE PLANTS
    A plant holds a planted vessel/torch/, a planted PyTorch, build.txt of
    assay.guard.engines.buildsys.planted, and the file vessel.txt in the rows
    grammar of assay.core.rows:

        format 1
        program PATH | LINE [| LINE]...   a shell program at PATH of the copy.
                                          Each cell is one line of it

    A line can hold {root} (the root of the copy), {build} (the build
    directory) and {python} (the interpreter of the rule).

Complexity: one build of the two vessel targets, and one run of each vessel test.
"""

import time
from dataclasses import dataclass
from pathlib import Path

from assay.core import proc
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.buildsys import planted
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import cmake as cmake_reader
from assay.readers import compdb

RULE = "vessel-acceptance"
VESSEL_DIR = "vessel/torch"
LIBRARIES = ("libcrucible_vessel.so", "libcrucible_dispatch.so")
TARGETS = ("crucible_vessel", "crucible_dispatch")
TEST_GLOB = "test_*.py"
TEST_TIMEOUT_SECONDS = 1800
# The limit of one probe of the interpreter, in seconds.
PROBE_TIMEOUT_SECONDS = 300
# register.cpp alone compiles for about 10 minutes in a Release build.
BUILD_TIMEOUT_SECONDS = 2400
FAILURE_TAIL_LINES = 40
VESSEL_FILE = "vessel.txt"
FORMAT = "1"
TRUE_VALUES = frozenset({"1", "ON", "YES", "TRUE", "Y"})


def error(kind: str, path: str, message: str, entity: str = "") -> check_report.Finding:
    """Return one error of the rule."""
    return check_report.Finding("error", path, 0, RULE, message, kind, entity)


def last_lines(text: str, count: int = 3) -> str:
    """Return the last lines of an output on one line, for the message of a finding."""
    return "  ".join(line.strip() for line in text.strip().splitlines()[-count:])


def vessel_sources(root: Path) -> list[Path]:
    """Return each C++ source of the vessel, sorted."""
    return sorted((root / VESSEL_DIR).glob("*.cpp"))


def vessel_tests(root: Path) -> list[Path]:
    """Return each Python test of the vessel, sorted."""
    return sorted((root / VESSEL_DIR).glob(TEST_GLOB))


def same_path(left: str | Path, right: str | Path) -> bool:
    """Return True when two paths name one directory after resolution."""
    return Path(left).resolve() == Path(right).resolve()


@dataclass(frozen=True, slots=True)
class VesselBuild:
    """The entries of the CMake cache of a build that the gate reads (THE BUILD).

    `python` is the interpreter of the PyTorch of TORCH_DIR, and `cmake` is
    an empty text when the cache has no entry CMAKE_COMMAND.
    """

    torch_dir: str
    python: str
    is_werror: bool
    cmake: str


def read_build(build_dir: Path) -> VesselBuild | str:
    """Return the entries of the CMake cache of a build that the gate reads, or the reason that it cannot read them."""
    if not cmake_reader.is_configured(build_dir):
        return f"{build_dir} is not a configured build."
    try:
        cache = {name: entry.value for name, entry in cmake_reader.cache(build_dir).items()}
    except cmake_reader.CmakeError as problem:
        return f"the CMake cache of {build_dir} cannot be read: {problem}"
    torch_dir = cache.get("TORCH_DIR", "")
    python = cache.get("CRUCIBLE_VESSEL_PYTHON", "")
    if not python:
        environment_python = (Path(torch_dir) / ".." / ".." / ".." / "bin" / "python").resolve()
        python = str(environment_python) if torch_dir and environment_python.exists() else "python3"
    return VesselBuild(
        torch_dir, python, cache.get("CRUCIBLE_WERROR", "").upper() in TRUE_VALUES, cache.get("CMAKE_COMMAND", "")
    )


def check_torch(torch_dir: str, python: str) -> list[check_report.Finding]:
    """Refuse a PyTorch that is missing, has no key, or is not the one that the interpreter imports."""
    if not (Path(torch_dir) / "torch" / "include").is_dir():
        return [
            error(
                "torch",
                VESSEL_DIR,
                f"{torch_dir}/torch/include does not exist, so TORCH_DIR names no PyTorch tree.",
                "TORCH_DIR",
            )
        ]
    probe = (
        "import os, torch\n"
        "print(os.path.dirname(os.path.dirname(torch.__file__)))\n"
        "print(hasattr(torch._C.DispatchKey, 'Crucible'))\n"
    )
    try:
        done = proc.run(proc.Command([python, "-c", probe], timeout_s=PROBE_TIMEOUT_SECONDS))
    except proc.ToolFailed as problem:
        return [error("torch", VESSEL_DIR, f"the interpreter {python} does not start: {problem}", python)]
    if done.exit != 0:
        return [
            error("torch", VESSEL_DIR, f"the interpreter {python} cannot import torch: {last_lines(done.err)}", python)
        ]
    lines = done.out.strip().splitlines()
    found: list[check_report.Finding] = []
    if not lines or not same_path(lines[0], torch_dir):
        found.append(
            error(
                "torch",
                VESSEL_DIR,
                f"the interpreter {python} imports torch from "
                f"{lines[0] if lines else '(nothing)'}, and the build compiles against "
                f"{torch_dir}.  Give the gate the interpreter of that PyTorch.",
                python,
            )
        )
    if len(lines) < 2 or lines[1] != "True":
        found.append(
            error(
                "torch",
                VESSEL_DIR,
                f"the PyTorch that {python} imports has no DispatchKey::Crucible.  Build the fork.",
                "DispatchKey::Crucible",
            )
        )
    return found


def werror_findings(build_dir: Path, root: Path) -> list[check_report.Finding]:
    """Refuse a vessel source that is not in the compile database, or that compiles with no -Werror."""
    database = compdb.database_file(build_dir)
    try:
        compiles = compdb.entries(build_dir)
    except compdb.CompdbError as problem:
        return [
            error(
                "build",
                VESSEL_DIR,
                f"{problem}, so the gate cannot read the compile line of the vessel.",
                compdb.DATABASE_NAME,
            )
        ]
    arguments = {Path(entry.file).resolve(): entry.argv() for entry in compiles}
    found: list[check_report.Finding] = []
    for source in vessel_sources(root):
        line = arguments.get(source.resolve())
        relative = source.relative_to(root).as_posix()
        if line is None:
            found.append(
                error(
                    "build", relative, f"{relative} is not in {database}, so the build does not compile it.", relative
                )
            )
        elif "-Werror" not in line:
            found.append(error("build", relative, f"{relative} compiles with no -Werror.", relative))
    return found


def check_build(build_dir: Path, build: VesselBuild, root: Path) -> list[check_report.Finding]:
    """Refuse a build that has no -Werror, then build the vessel targets.

    The gate builds the two vessel targets itself.  A build that is older
    than its sources compiles again here, and a compile error refuses the
    gate.  A dry run cannot answer the question: this project uses
    CONFIGURE_DEPENDS globs, and a dry run stops at the step that checks them.
    """
    found: list[check_report.Finding] = []
    if not build.is_werror:
        found.append(
            error(
                "build",
                VESSEL_DIR,
                "the build sets CRUCIBLE_WERROR to OFF.  The gate accepts only a -Werror build.",
                "CRUCIBLE_WERROR",
            )
        )
    found += werror_findings(build_dir, root)
    if found:
        return found
    # The cmake of the build, and not a cmake of PATH, which can have a different version (assay.setup.pins).
    cmake_program = build.cmake
    if not cmake_program:
        return [
            error(
                "build",
                VESSEL_DIR,
                f"the CMake cache of {build_dir} has no entry CMAKE_COMMAND.  {cmake_reader.AGAIN}",
                "CMAKE_COMMAND",
            )
        ]
    targets = " ".join(TARGETS)
    try:
        done = proc.run(
            proc.Command(
                [cmake_program, "--build", str(build_dir), "--target", *TARGETS], timeout_s=BUILD_TIMEOUT_SECONDS
            )
        )
    except proc.ToolFailed as problem:
        return [error("build", VESSEL_DIR, f"the build of the targets {targets} does not finish: {problem}", targets)]
    if done.exit != 0:
        output = done.out + done.err
        check_report.write_line("\n".join(output.splitlines()[-FAILURE_TAIL_LINES:]), is_error=True)
        return [error("build", VESSEL_DIR, f"the build of the targets {targets} fails: {last_lines(output)}", targets)]
    return [
        error("build", VESSEL_DIR, f"the build of the targets {targets} makes no {build_dir}/lib/{library}.", library)
        for library in LIBRARIES
        if not (build_dir / "lib" / library).is_file()
    ]


def check_uninstrumented(build_dir: Path, python: str, root: Path) -> list[check_report.Finding]:
    """Refuse a vessel library that holds a sanitizer runtime.

    crucible_native.sanitizer_runtime reads the dynamic symbols of the
    library.  The gate and the loader then cannot disagree about a library.
    """
    probe = (
        "import sys\n"
        f"sys.path.insert(0, {str(root / VESSEL_DIR)!r})\n"
        "import crucible_native\n"
        "for path in sys.argv[1:]:\n"
        "    runtime = crucible_native.sanitizer_runtime(path)\n"
        "    print(None if runtime is None else runtime.sanitizer)\n"
    )
    paths = [str(build_dir / "lib" / library) for library in LIBRARIES]
    try:
        done = proc.run(proc.Command([python, "-c", probe, *paths], timeout_s=PROBE_TIMEOUT_SECONDS))
    except proc.ToolFailed as problem:
        return [error("sanitizer", VESSEL_DIR, f"the interpreter {python} does not start: {problem}", python)]
    if done.exit != 0:
        return [
            error(
                "sanitizer",
                VESSEL_DIR,
                f"the sanitizer probe of the vessel libraries failed: {last_lines(done.err)}",
                "crucible_native",
            )
        ]
    return [
        error(
            "sanitizer",
            VESSEL_DIR,
            f"{path} carries the runtime {verdict}.  The gate runs Python with no "
            f"sanitizer runtime preloaded, so Python cannot load it.  Use a build of "
            f"the release or bench preset.",
            Path(path).name,
        )
        for path, verdict in zip(paths, done.out.strip().splitlines(), strict=False)
        if verdict != "None"
    ]


def run_tests(build_dir: Path, python: str, root: Path) -> list[check_report.Finding]:
    """Run each vessel test against the libraries of the build, and refuse each that does not exit 0."""
    environment = proc.environment({"CRUCIBLE_BUILD_DIR": str(build_dir)})
    tests = vessel_tests(root)
    if not tests:
        return [
            error("test", VESSEL_DIR, f"{VESSEL_DIR} holds no {TEST_GLOB}, so the gate has nothing to run.", TEST_GLOB)
        ]
    found: list[check_report.Finding] = []
    for test in tests:
        started = time.monotonic()
        try:
            done = proc.run(
                proc.Command(
                    [python, str(test)], timeout_s=TEST_TIMEOUT_SECONDS, cwd=root / VESSEL_DIR, env=environment
                )
            )
            code, output = done.exit, done.out + done.err
        except proc.TimedOut as problem:
            stopped = problem.completed
            code, output = None, "" if stopped is None else stopped.out + stopped.err
        elapsed = time.monotonic() - started
        if code == 0:
            check_report.write_line(f"  ok    {test.name} ({elapsed:.1f} s)")
            continue
        verdict = f"exits {code}" if code is not None else f"does not finish in {TEST_TIMEOUT_SECONDS} s"
        last = "\n".join(f"        {line}" for line in output.splitlines()[-FAILURE_TAIL_LINES:])
        check_report.write_line(
            f"  FAIL  {test.name} {verdict} ({elapsed:.1f} s).  The last lines of its output:\n{last}", is_error=True
        )
        relative = test.relative_to(root).as_posix()
        found.append(error("test", relative, f"{test.name} {verdict}.", test.name))
    return found


def findings(context: BuildContext) -> list[check_report.Finding]:
    """Run the three items of the gate on the build of the context (THE GATE), and give each problem.

    Raises:
        NotApplicable: If the call gives no build directory, or the build has an empty TORCH_DIR (THE BUILD)
    """
    build_dir = context.require_build()
    build = read_build(build_dir)
    if isinstance(build, str):
        return [error("build", VESSEL_DIR, build, "build directory")]
    if not build.torch_dir:
        raise meta_runner.NotApplicable(
            "the build has an empty TORCH_DIR, so it does not compile the vessel.  "
            "Configure a build of the release or bench preset with -DTORCH_DIR=<the "
            "site-packages of the PyTorch fork>"
        )
    prerequisites = check_torch(build.torch_dir, build.python) + check_build(build_dir, build, context.root)
    if not prerequisites:
        prerequisites = check_uninstrumented(build_dir, build.python, context.root)
    if prerequisites:
        return prerequisites
    return run_tests(build_dir, build.python, context.root)


# ── The plants ─────────────────────────────────────────────────────────────


def read_vessel(root: Path, build: Path) -> None:
    """Write each program that the rows of vessel.txt give (THE PLANTS).

    Raises:
        rows_grammar.RowsError: If a row does not obey its shape
    """
    rows = rows_grammar.read(root / VESSEL_FILE, VESSEL_FILE)
    if not rows or rows[0].words != ("format", FORMAT):
        raise rows_grammar.RowsError(VESSEL_FILE, rows[0].line if rows else 0, f"the first row is 'format {FORMAT}'")
    for row in rows[1:]:
        path = row.words[1] if len(row.words) == 2 else ""
        if row.words[0] != "program" or not row.cells or not path or path.startswith("/") or ".." in path.split("/"):
            raise row.error(f"a row of {VESSEL_FILE} is 'program PATH | LINE [| LINE]...', with a relative PATH")
        program = root / path
        program.parent.mkdir(parents=True, exist_ok=True)
        lines = (planted.expanded(cell, root, build) for cell in row.cells)
        program.write_text("#!/bin/sh\n" + "".join(f"{line}\n" for line in lines), encoding="utf-8")
        program.chmod(0o755)


def plant_context(plant: Path, scratch: Path) -> BuildContext:
    """Copy a plant, make its planted build and the programs of its vessel.txt, and return its context (THE PLANTS).

    Raises:
        NotApplicable: If build.txt or vessel.txt does not obey its rows
    """
    context = planted.plant_context(plant, scratch)
    if (scratch / VESSEL_FILE).is_file():
        try:
            read_vessel(scratch, scratch / planted.BUILD)
        except rows_grammar.RowsError as problem:
            raise meta_runner.NotApplicable(f"the planted vessel does not read: {problem}") from None
    return context
