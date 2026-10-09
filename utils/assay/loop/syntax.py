"""loop syntax — parse each unit of the target all of a parse build with the quarantine plugin.

usage: loop syntax BUILD_DIR [-j JOBS]

THE PARSE BUILD
    A parse build is a configured build directory with CRUCIBLE_SYNTAX_ONLY
    (assay.core.configuration.is_parse_build), such as a build of the presets
    syntax-x86-64-v3 and syntax-aarch64.  It compiles the preprocessor arms of
    its target, and no build of an object compiles them.  The ledgers of the
    rules quarantine, heavy-include and syscall-capability count its
    findings in a configuration of their own (CLAUDE.md section XXII, The
    ratchet).

THE RUN
    1. Ninja builds each input target of the build, with the name
       cmake_object_order_depends_target_*.  These targets write the inputs
       of the compiles, and they do no compile and no link.  The rule
       build-order holds that no such target waits for a link.
    2. The run removes the directory SYNTAX_DIRECTORY of the build.  No
       report of an earlier run then stays.
    3. Each compile of an object of all that loads the plugin runs with
       -fsyntax-only, with no output and no dependency file.  It gets the
       plugin argument out= of its report directory (assay.readers.sections,
       THE REPORT OF A PARSE BUILD).  A cross compiler has no native target,
       and it refuses -march=native.  For such a compiler, the parse drops
       each native flag of NATIVE_FLAGS that a bench target or the flag matrix
       gives, and the compiler uses its default target.  After a parse with
       the exit 0, the run writes the stamp of the compile command into the
       stamp file of the report directory.  JOBS parses run at one time.
    4. The run prints the number of units and each unit that does not
       parse.  It also prints the wall time and the CPU time of the parses,
       and the time of step 1.
    Then `guard check --rule quarantine --build-dir BUILD_DIR` reads the
    reports (assay.guard.engines.plugin.runner, THE SECTIONS).

Exit status: 0 when each unit parses, 1 when a unit does not parse or a step
fails, 2 on a usage error.  `guard selftest tests.loop.test_syntax` does a test
of the module.
"""

import os
import shutil
import time
from dataclasses import dataclass
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import proc
from assay.core import report as check_report
from assay.guard.engines.plugin import runner as plugin_runner
from assay.readers import compdb, ninja
from assay.readers import sections as quarantine_sections

# The prefix of the name of each input target (THE RUN, step 1).
INPUT_PREFIX = "cmake_object_order_depends_target_"
# The words of a compile command that a parse drops with the word that follows each one.
DROPPED_WITH_VALUE = frozenset({"-o", "-MF", "-MT", "-MQ"})
# The words of a compile command that a parse drops.
DROPPED = frozenset({"-c", "-MD", "-MMD"})
# The native flags that a parse drops for a compiler that refuses them (THE RUN).
NATIVE_FLAGS = frozenset({"-march=native", "-mcpu=native", "-mtune=native"})
# The time limit of one parse, in seconds.
PARSE_TIMEOUT_S = 900.0
# The time limit of the build of the input targets, in seconds.
INPUT_TIMEOUT_S = 1800.0
# The lines of the errors of a unit that the run prints.
SHOWN_ERROR_LINES = 12


class ParseError(Exception):
    """A step of the run fails, or the build directory is no parse build."""


@dataclass(frozen=True, slots=True)
class Unit:
    """One unit of the run: its object, the stamp and the words of its compile command, and its directory."""

    object_path: Path
    stamp: str
    words: tuple[str, ...]
    directory: str


@dataclass(frozen=True, slots=True)
class Parsed:
    """The result of the parse of one unit: its exit, its CPU time and the text of its errors."""

    unit: Unit
    exit: int
    cpu_s: float
    errors: str


def refuses_native(compiler: str) -> bool:
    """Return True when the compiler refuses -march=native, as a cross compiler does (THE RUN).

    A compiler that cannot run refuses it too, and its parse then gives the error of the compiler.
    """
    try:
        done = proc.run(
            proc.Command(
                [compiler, "-march=native", "-Q", "--help=target"],
                timeout_s=proc.QUICK_S,
                stdout=proc.Output.DISCARD,
                stderr=proc.Output.DISCARD,
            )
        )
    except proc.ToolFailed:
        return True
    return done.exit != 0


def parse_words(words: tuple[str, ...], report_dir: Path, drops_native: bool = False) -> list[str]:
    """Return the words of the parse of a compile: -fsyntax-only, no output, no dependency file, and the report.

    With drops_native, the words hold no flag of NATIVE_FLAGS.  Complexity: linear in the words.
    """
    parsed: list[str] = []
    skip_next = False
    for word in words:
        if skip_next:
            skip_next = False
            continue
        if word in DROPPED_WITH_VALUE:
            skip_next = True
            continue
        if word in DROPPED or (drops_native and word in NATIVE_FLAGS):
            continue
        parsed.append(word)
    # GCC takes a plugin argument only after the -fplugin= of its plugin, so the report argument goes last.
    return [*parsed, "-fsyntax-only", "-fdiagnostics-color=never", f"{plugin_runner.PLUGIN_ARGUMENT}out={report_dir}"]


def units_of(build_dir: Path, root: Path) -> list[Unit]:
    """Return each unit of the run: each object of all with a compile that loads the plugin.

    Raises:
        ParseError: If the build is no parse build, or its objects or its compile database cannot be read
    """
    if not build_config.is_parse_build(build_dir):
        raise ParseError(
            f"{build_dir} is no parse build: its CMake cache does not set "
            f"{build_config.SYNTAX_OPTION}=ON.  Configure it with the preset syntax-x86-64-v3 or "
            f"syntax-aarch64"
        )
    try:
        objects = {str(path): stamp for path, stamp, _ in plugin_runner.plugin_objects(build_dir, root, False)}
        rows = compdb.entries(build_dir)
    except (plugin_runner.NotApplicable, plugin_runner.BuildError, compdb.CompdbError) as problem:
        raise ParseError(f"the units of {build_dir} cannot be read: {problem}") from None
    units = [
        Unit(Path(row.output), objects[row.output], row.argv(), row.directory)
        for row in rows
        if row.output is not None and row.output in objects
    ]
    if not units:
        raise ParseError(f"no compile of the target all of {build_dir} loads the quarantine plugin")
    return sorted(units, key=lambda unit: str(unit.object_path))


def build_input_targets(build_dir: Path, jobs: int) -> float:
    """Build each input target of the build with Ninja (THE RUN, step 1), and return the wall time of the build.

    Raises:
        ParseError: If the graph of the build cannot be read, or the build fails
    """
    from assay.setup import pins

    try:
        graph = ninja.graph(build_dir)
        cmake = pins.configured_program(build_dir, "cmake")
    except (ninja.NinjaError, pins.PinError) as problem:
        raise ParseError(f"the input targets of {build_dir} cannot be read: {problem}") from None
    targets = sorted({output for edge in graph.edges for output in edge.outputs if output.startswith(INPUT_PREFIX)})
    if not targets:
        return 0.0
    start = time.monotonic()
    try:
        done = proc.run(
            proc.Command(
                [cmake, "--build", str(build_dir), "-j", str(jobs), "--target", *targets],
                timeout_s=INPUT_TIMEOUT_S,
                stdout=proc.Output.CAPTURE,
                stderr=proc.Output.MERGE,
            )
        )
    except proc.ToolFailed as problem:
        raise ParseError(f"the build of the input targets of {build_dir} does not run: {problem}") from None
    if done.exit != 0:
        raise ParseError(f"the build of the input targets of {build_dir} exits with {done.exit}:\n{done.out[-4000:]}")
    return time.monotonic() - start


def parse_unit(unit: Unit, build_dir: Path, native_refusers: frozenset[str]) -> Parsed:
    """Parse one unit into its report directory, and write its stamp after a parse with the exit 0.

    `native_refusers` holds each compiler that refuses -march=native.
    """
    report_dir = quarantine_sections.report_directory(build_dir, unit.object_path)
    report_dir.mkdir(parents=True, exist_ok=True)
    words = parse_words(unit.words, report_dir, bool(unit.words) and unit.words[0] in native_refusers)
    try:
        done = proc.run(
            proc.Command(
                words,
                timeout_s=PARSE_TIMEOUT_S,
                cwd=unit.directory,
                stdout=proc.Output.DISCARD,
                stderr=proc.Output.CAPTURE,
            )
        )
    except proc.ToolFailed as problem:
        return Parsed(unit, 127, 0.0, str(problem))
    if done.exit == 0:
        (report_dir / quarantine_sections.STAMP_FILE).write_text(f"{unit.stamp}\n", encoding="utf-8")
    return Parsed(unit, done.exit, done.cpu_s, done.err)


def run(build_dir: Path, root: Path, jobs: int) -> int:
    """Do the run of a parse build (THE RUN), and print what it did.

    Returns:
        0 when each unit parses, 1 when a unit does not parse or a step fails
    """
    from concurrent.futures import ThreadPoolExecutor

    try:
        units = units_of(build_dir, root)
        input_s = build_input_targets(build_dir, jobs)
    except ParseError as problem:
        check_report.write_line(f"loop syntax: {problem}", is_error=True)
        return 1
    shutil.rmtree(build_dir / quarantine_sections.SYNTAX_DIRECTORY, ignore_errors=True)
    compilers = sorted({unit.words[0] for unit in units if unit.words})
    native_refusers = frozenset(compiler for compiler in compilers if refuses_native(compiler))
    start = time.monotonic()
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        results = list(pool.map(lambda unit: parse_unit(unit, build_dir, native_refusers), units))
    wall_s = time.monotonic() - start
    failed = [result for result in results if result.exit != 0]
    for result in failed:
        lines = [line for line in result.errors.splitlines() if line.strip()][:SHOWN_ERROR_LINES]
        check_report.write_line(
            f"loop syntax: {os.path.relpath(result.unit.object_path, build_dir)} does not parse (exit {result.exit}):",
            is_error=True,
        )
        for line in lines:
            check_report.write_line(f"    {line}", is_error=True)
    cpu_s = sum(result.cpu_s for result in results)
    check_report.write_line(
        f"loop syntax: {len(results) - len(failed)} of {len(results)} units parse in {wall_s:.1f} s "
        f"of wall time and {cpu_s:.1f} s of CPU time with {jobs} jobs.  The input targets took "
        f"{input_s:.1f} s"
    )
    return 1 if failed else 0


def main(argv: list[str]) -> int:
    """Parse the arguments, and do the run of a parse build.

    Args:
        argv: The words after `loop syntax`

    Returns:
        The exit status (the docstring of the module)
    """
    from assay.core import cli
    from assay.core.paths import REPO_ROOT

    parser = cli.parser("loop syntax", "parse each unit of all of a parse build with the quarantine plugin")
    parser.add_argument("build_dir", metavar="BUILD_DIR", help="a build of a preset syntax-*")
    parser.add_argument("-j", "--jobs", type=int, default=96, help="the parses at one time; 96 when not given")
    options = parser.parse_args(argv)
    if options.jobs < 1:
        parser.error(f"JOBS must be a positive integer, not '{options.jobs}'")
    return run(Path(options.build_dir).resolve(), REPO_ROOT, options.jobs)
