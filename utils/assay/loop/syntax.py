"""loop syntax — parse each unit of the target all of a parse build with the quarantine plugin.

usage: loop syntax BUILD_DIR [-j JOBS]
       loop syntax --changed [--root ROOT]

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
       build-order holds that no such target waits for a link.  A change of
       the plugin or of the rule table makes CMake configure the build
       again in this step.  The compile database then holds a new stamp,
       and the run reads the compile commands after this step.
    2. The run removes each report directory of SYNTAX_DIRECTORY that no
       unit of the build has.  No report of an object that is not in the
       build then stays.
    3. Each compile of an object of all that loads the plugin runs with
       -fsyntax-only and with no output.  It writes the dependency file
       DEPENDENCY_FILE of its report directory, and it gets the plugin
       argument out= of that directory (assay.readers.sections, THE REPORT OF
       A PARSE BUILD).  A cross compiler has no native target, and it
       refuses -march=native.  For such a compiler, the parse drops each
       native flag of NATIVE_FLAGS that a bench target or the flag matrix
       gives.  The compiler then uses its default target.  After a parse
       with the exit 0, the run writes the stamp of the compile command into
       the stamp file of the report directory.  It also writes the key of the
       parse into KEY_FILE (THE KEPT REPORTS).  JOBS parses run at one time.
    4. The run prints the number of units, the number of kept reports and
       each unit that does not parse.  It also prints the wall time and the
       CPU time of the parses, and the time of step 1.
    Then `guard check --rule quarantine --build-dir BUILD_DIR` reads the
    reports (assay.guard.engines.plugin.runner, THE SECTIONS).

THE KEPT REPORTS
    The key of a parse is the hash of the words of the parse and of the
    enforce rows of the build (quarantine/enforce.txt).  KEY_FILE also holds
    the time when the parse started.  That time is the change time of
    STARTED_FILE, which the run writes before the parse.  The file system
    then gives the two times that the run compares, and the run reads no
    wall clock.  The run keeps the report of a unit,
    and does not parse it, when these conditions hold:
      * KEY_FILE holds the hash of this run, and the stamp file holds the
        stamp of the compile command
      * the report directory holds the report file of the plugin
      * each file of DEPENDENCY_FILE is in the tree, and it changed before
        the earlier parse started.
    A change of the plugin, of the rule table or of the configure changes
    the stamp, and each unit then parses again.  The run removes the key and
    the stamp of a unit before its parse.  A parse that fails then keeps no
    report.  An include can find a new file before the file of the earlier
    parse.  That new file is no file of DEPENDENCY_FILE, and it gives no
    parse, as in a build of Ninja.  Complexity: one stat of each different
    dependency.

THE CHANGE OF AN ARM
    An arm is the code of a preprocessor conditional on a macro of an
    architecture or of an ISA extension (is_arm_macro()).  Examples of such
    a macro are __aarch64__, __AVX2__ and __ARM_NEON.  arm_files() reads the
    change of a tree (assay.core.git, THE CHANGE) and the parse tree of each
    changed C++ file (assay.readers.tsast).  It gives each such file with an
    arm in its copy in the working tree or at the base.  A change that
    removes an arm is then a change of an arm too.

    A change of an arm makes each parse build of PARSE_PRESETS necessary,
    because the arms of one conditional go to the builds of different
    targets.  The rules parse-x86-64-v3 and parse-aarch64 run the parse
    builds of such a change (assay.guard.engines.buildsys.parse_builds).
    `loop syntax --changed` prints each preset that the change of ROOT makes
    necessary, one on each line.  build_directory() gives the build
    directory of a preset from the presets files of a tree.

Exit status: 0 when each unit parses, 1 when a unit does not parse or a step
fails, 2 on a usage error.  `guard selftest tests.loop.test_syntax` does a test
of the module.
"""

import os
import shutil
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import TYPE_CHECKING

from assay.core import configuration as build_config
from assay.core import paths, proc
from assay.core import report as check_report
from assay.guard.engines.plugin import runner as plugin_runner
from assay.readers import compdb, ninja
from assay.readers import sections as quarantine_sections

if TYPE_CHECKING:
    from assay.readers import tsast

# The prefix of the name of each input target (THE RUN, step 1).
INPUT_PREFIX = "cmake_object_order_depends_target_"
# The words of a compile command that a parse drops with the word that follows each one.
DROPPED_WITH_VALUE = frozenset({"-o", "-MF", "-MT", "-MQ"})
# The words of a compile command that a parse drops.
DROPPED = frozenset({"-c", "-MD", "-MMD"})
# The native flags that a parse drops for a compiler that refuses them (THE RUN).
NATIVE_FLAGS = frozenset({"-march=native", "-mcpu=native", "-mtune=native"})
# The dependency file, the key and the start mark of the parse of a unit, in its report directory (THE KEPT
# REPORTS).
DEPENDENCY_FILE = "parse.d"
KEY_FILE = "key"
STARTED_FILE = "started"
# The enforce rows of a build, below the build directory (utils/tools/quarantine/Quarantine.cmake).
ENFORCE_FILE = Path("quarantine") / "enforce.txt"
# The time limit of one parse, in seconds.
PARSE_TIMEOUT_S = 900.0
# The time limit of the build of the input targets, in seconds.
INPUT_TIMEOUT_S = 1800.0
# The lines of the errors of a unit that the run prints.
SHOWN_ERROR_LINES = 12
# The parses at one time: the default of -j, and the PROCESSORS of the test of a rule that runs a parse build.
JOBS = proc.DEFAULT_JOBS
# The presets of the parse builds that a change of an arm makes necessary (THE CHANGE OF AN ARM).
PARSE_PRESETS = ("syntax-x86-64-v3", "syntax-aarch64")
# The architectures of the macros __NAME__ and __NAME that the compiler gives (is_arm_macro()).
ARCHITECTURES = frozenset(
    {
        "x86_64",
        "i386",
        "i486",
        "i586",
        "i686",
        "amd64",
        "aarch64",
        "arm",
        "thumb",
        "riscv",
        "powerpc",
        "powerpc64",
        "ppc64",
        "s390x",
        "loongarch64",
        "mips",
    }
)
# The ISA extensions of the macros __NAME__: each family by its first letters, and each other extension by its name.
ISA_FAMILIES = ("SSE", "AVX", "AMX_")
ISA_EXTENSIONS = frozenset(
    {
        "SSSE3",
        "BMI",
        "BMI2",
        "FMA",
        "FMA4",
        "F16C",
        "POPCNT",
        "LZCNT",
        "PCLMUL",
        "AES",
        "SHA",
        "VAES",
        "VPCLMULQDQ",
        "GFNI",
        "ADX",
        "AMX",
        "MOVBE",
    }
)
# The prefix of an ARM macro, such as __ARM_NEON, and the architecture macros of the other compilers.
ARM_PREFIX = "__ARM_"
OTHER_ARCHITECTURES = frozenset({"_M_X64", "_M_AMD64", "_M_ARM64", "_M_IX86"})
# The field of each preprocessor conditional of the parse tree that holds the names of its condition.
CONDITION_FIELDS = {
    "preproc_if": "condition",
    "preproc_elif": "condition",
    "preproc_ifdef": "name",
    "preproc_elifdef": "name",
}
# The macros of a presets file that build_directory() expands.
SOURCE_DIR_MACRO = "${sourceDir}"
PRESET_NAME_MACRO = "${presetName}"


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
    is_kept: bool = False


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
    return [
        *parsed,
        "-fsyntax-only",
        "-MD",
        "-MF",
        str(report_dir / DEPENDENCY_FILE),
        "-fdiagnostics-color=never",
        f"{plugin_runner.PLUGIN_ARGUMENT}out={report_dir}",
    ]


def words_key(words: list[str], enforce_hash: str) -> str:
    """Return the hash of the words of a parse and of the enforce rows of the build (THE KEPT REPORTS)."""
    import hashlib

    return hashlib.sha256("\0".join([*words, enforce_hash]).encode("utf-8", "surrogateescape")).hexdigest()


def enforce_hash(build_dir: Path) -> str:
    """Return the hash of the enforce rows of a build, or an empty text when the build has none."""
    import hashlib

    try:
        return hashlib.sha256((build_dir / ENFORCE_FILE).read_bytes()).hexdigest()
    except OSError:
        return ""


def is_kept(unit: Unit, report_dir: Path, key: str, changed_ns: dict[str, int | None]) -> bool:
    """Return True when the report of a unit can stay, with no parse (THE KEPT REPORTS).

    `changed_ns` holds the change time of each dependency that an earlier call read, or None for a path with no file.
    Complexity: linear in the dependencies of the unit, one stat of each new one.
    """
    from assay.loop.fixture import store as compile_store

    try:
        kept_key, _, started_text = (report_dir / KEY_FILE).read_text(encoding="utf-8").partition("\n")
        stamp = (report_dir / quarantine_sections.STAMP_FILE).read_text(encoding="utf-8").strip()
        dependency_text = (report_dir / DEPENDENCY_FILE).read_text(encoding="utf-8", errors="surrogateescape")
        has_report = bool(paths.listing(report_dir, f"*{quarantine_sections.REPORT_SUFFIX}"))
    except OSError:
        return False
    if kept_key != key or stamp != unit.stamp or not has_report or not started_text.strip().isdigit():
        return False
    started_ns = int(started_text.strip())
    dependencies = compile_store.parse_dependency_file(dependency_text)
    if not dependencies:
        return False
    for name in dependencies:
        path = os.path.join(unit.directory, name)
        if path not in changed_ns:
            try:
                changed_ns[path] = os.stat(path).st_mtime_ns
            except OSError:
                changed_ns[path] = None
        changed = changed_ns[path]
        if changed is None or changed >= started_ns:
            return False
    return True


def require_parse_build(build_dir: Path) -> None:
    """Refuse a build directory that is no parse build (THE PARSE BUILD).

    Raises:
        ParseError: If the CMake cache of the build does not set SYNTAX_OPTION
    """
    if not build_config.is_parse_build(build_dir):
        raise ParseError(
            f"{build_dir} is no parse build: its CMake cache does not set "
            f"{build_config.SYNTAX_OPTION}=ON.  Configure it with the preset syntax-x86-64-v3 or "
            f"syntax-aarch64"
        )


def units_of(build_dir: Path, root: Path) -> list[Unit]:
    """Return each unit of the run: each object of all with a compile that loads the plugin.

    Raises:
        ParseError: If the build is no parse build, or its objects or its compile database cannot be read
    """
    require_parse_build(build_dir)
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


def parse_unit(unit: Unit, words: list[str], key: str, report_dir: Path) -> Parsed:
    """Parse one unit into its report directory, and write its stamp and its key after a parse with the exit 0.

    The run removes the key and the stamp before the parse, and it reads the start time from STARTED_FILE (THE KEPT
    REPORTS).
    """
    report_dir.mkdir(parents=True, exist_ok=True)
    (report_dir / KEY_FILE).unlink(missing_ok=True)
    (report_dir / quarantine_sections.STAMP_FILE).unlink(missing_ok=True)
    started_mark = report_dir / STARTED_FILE
    started_mark.write_bytes(b"")
    started_ns = started_mark.stat().st_mtime_ns
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
        (report_dir / KEY_FILE).write_text(f"{key}\n{started_ns}\n", encoding="utf-8")
    return Parsed(unit, done.exit, done.cpu_s, done.err)


def prune_reports(build_dir: Path, kept_dirs: set[Path]) -> None:
    """Remove each report directory of SYNTAX_DIRECTORY that no unit of the build has (THE RUN, step 2).

    A report directory holds the stamp file, the key file or the dependency file, and the directory of an object holds
    no other report directory.  Complexity: linear in the files of SYNTAX_DIRECTORY.
    """
    syntax_dir = build_dir / quarantine_sections.SYNTAX_DIRECTORY
    if not syntax_dir.is_dir():
        return
    marks = {quarantine_sections.STAMP_FILE, KEY_FILE, DEPENDENCY_FILE, STARTED_FILE}
    stale = {
        path.parent for path in paths.listing(syntax_dir, "**/*") if path.name in marks and path.parent not in kept_dirs
    }
    for directory in sorted(stale):
        shutil.rmtree(directory, ignore_errors=True)


def run(build_dir: Path, root: Path, jobs: int) -> int:
    """Do the run of a parse build (THE RUN), and print what it did.

    Returns:
        0 when each unit parses, 1 when a unit does not parse or a step fails
    """
    from concurrent.futures import ThreadPoolExecutor

    try:
        require_parse_build(build_dir)
        input_s = build_input_targets(build_dir, jobs)
        units = units_of(build_dir, root)
    except ParseError as problem:
        check_report.write_line(f"loop syntax: {problem}", is_error=True)
        return 1
    report_dirs = {unit: quarantine_sections.report_directory(build_dir, unit.object_path) for unit in units}
    prune_reports(build_dir, set(report_dirs.values()))
    compilers = sorted({unit.words[0] for unit in units if unit.words})
    native_refusers = frozenset(compiler for compiler in compilers if refuses_native(compiler))
    enforce = enforce_hash(build_dir)
    start = time.monotonic()
    planned: list[tuple[Unit, list[str], str]] = []
    results: list[Parsed] = []
    changed_ns: dict[str, int | None] = {}
    for unit in units:
        words = parse_words(unit.words, report_dirs[unit], bool(unit.words) and unit.words[0] in native_refusers)
        key = words_key(words, enforce)
        if is_kept(unit, report_dirs[unit], key, changed_ns):
            results.append(Parsed(unit, 0, 0.0, "", True))
        else:
            planned.append((unit, words, key))
    with ThreadPoolExecutor(max_workers=jobs) as pool:
        results += pool.map(lambda item: parse_unit(item[0], item[1], item[2], report_dirs[item[0]]), planned)
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
    kept = sum(1 for result in results if result.is_kept)
    check_report.write_line(
        f"loop syntax: {len(results) - len(failed)} of {len(results)} units parse, {kept} of them with the kept report "
        f"of an earlier parse, in {wall_s:.1f} s of wall time and {cpu_s:.1f} s of CPU time with {jobs} jobs.  The "
        f"input targets took {input_s:.1f} s"
    )
    return 1 if failed else 0


def is_arm_macro(name: str) -> bool:
    """Return True for a macro of an architecture or of an ISA extension that a compiler gives (THE CHANGE OF AN ARM).

    The macros are __NAME__ and __NAME of ARCHITECTURES, each macro with the prefix ARM_PREFIX, and each macro of
    OTHER_ARCHITECTURES.  A macro __NAME__ of an ISA extension of ISA_FAMILIES or ISA_EXTENSIONS is one too.
    """
    if name in OTHER_ARCHITECTURES:
        return True
    if not name.startswith("__"):
        return False
    if name.startswith(ARM_PREFIX):
        return len(name) > len(ARM_PREFIX)
    bare = name[2:].removesuffix("__")
    if bare in ARCHITECTURES:
        return True
    return name.endswith("__") and (bare in ISA_EXTENSIONS or bare.startswith(ISA_FAMILIES))


def arm_macros(tree: tsast.Tree) -> frozenset[str]:
    """Return each macro of is_arm_macro() that a preprocessor condition of a parse tree names (THE CHANGE OF AN ARM).

    A comment and a string hold no identifier node.  A macro name in one then counts for nothing.  Complexity: linear
    in the nodes of the tree.
    """
    from assay.readers import tsast

    found: set[str] = set()
    for node in tree.find(*CONDITION_FIELDS):
        part = node.child_by_field(CONDITION_FIELDS[node.type])
        if part is None:
            continue
        leaves = [part] if part.type == "identifier" else list(part.descendants("identifier"))
        found.update(word for leaf in leaves if is_arm_macro(word := tsast.lexeme(leaf)))
    return frozenset(found)


def arm_files(root: Path) -> dict[str, frozenset[str]]:
    """Return each changed C++ file of a tree that holds an arm, with the macros of its arms (THE CHANGE OF AN ARM).

    The macros of a file are those of its copy in the working tree and of its copy at the base of the change.  A tree
    with no base of a change gives no file.  Complexity: one diff, one read of each changed C++ file at the base, and
    one parse tree of each copy.

    Raises:
        ParseError: If git cannot read the change, a file cannot be read, or the parse kit is not installed
    """
    from assay.core import git
    from assay.readers import tsast

    try:
        base = git.change_base(root)
        if base is None:
            return {}
        paths = [path for path in git.changed_files(root, base) if tsast.is_in_cpp_scope(path)]
        earlier = git.blobs(root, [f"{base}:{path}" for path in paths])
    except git.GitError as problem:
        raise ParseError(f"the change of {root} cannot be read: {problem}") from None
    found: dict[str, set[str]] = {}
    try:
        with tempfile.TemporaryDirectory(prefix="syntax-base-") as scratch:
            kept: list[str] = []
            for path, data in zip(paths, earlier, strict=True):
                if data is not None:
                    copy = Path(scratch) / path
                    copy.parent.mkdir(parents=True, exist_ok=True)
                    copy.write_bytes(data)
                    kept.append(path)
            present = [path for path in paths if (root / path).is_file()]
            trees = [*tsast.parse_files(root, present), *tsast.parse_files(Path(scratch), kept)]
    except (OSError, tsast.KitMissing) as problem:
        raise ParseError(f"the changed C++ files of {root} cannot be parsed: {problem}") from None
    for tree in trees:
        macros = arm_macros(tree)
        if macros:
            found.setdefault(tree.path.as_posix(), set()).update(macros)
    return {path: frozenset(macros) for path, macros in sorted(found.items())}


def build_directory(root: Path, preset: str) -> Path:
    """Return the build directory of a configure preset of the presets files of a tree (THE CHANGE OF AN ARM).

    The function expands the macros ${sourceDir} and ${presetName} of the field binaryDir.

    Raises:
        ParseError: If no presets file gives the preset, or the preset gives no binaryDir
    """
    from assay.readers import cmake

    for presets_file in cmake.presets(root):
        for item in presets_file.configure:
            if item.name != preset:
                continue
            if item.binary_dir is None:
                raise ParseError(f"the preset {preset} of {presets_file.path} gives no binaryDir")
            text = item.binary_dir.replace(SOURCE_DIR_MACRO, str(root)).replace(PRESET_NAME_MACRO, preset)
            return Path(text) if Path(text).is_absolute() else root / text
    raise ParseError(f"no presets file of {root} gives the configure preset {preset}")


def main(argv: list[str]) -> int:
    """Parse the arguments, and do the run of a parse build, or print the parse presets of a change.

    Args:
        argv: The words after `loop syntax`

    Returns:
        The exit status (the docstring of the module)
    """
    from assay.core import cli
    from assay.core.paths import REPO_ROOT

    parser = cli.parser("loop syntax", "parse each unit of all of a parse build with the quarantine plugin")
    parser.add_argument("build_dir", metavar="BUILD_DIR", nargs="?", help="a build of a preset syntax-*")
    parser.add_argument("-j", "--jobs", type=int, default=JOBS, help=f"the parses at one time; {JOBS} when not given")
    parser.add_argument("--changed", action="store_true", help="print each parse preset that the change needs")
    parser.add_argument("--root", type=Path, default=REPO_ROOT, help="the tree of --changed")
    options = parser.parse_args(argv)
    if options.jobs < 1:
        parser.error(f"JOBS must be a positive integer, not '{options.jobs}'")
    if options.changed == (options.build_dir is not None):
        parser.error("give BUILD_DIR or --changed, and not the two")
    if options.changed:
        try:
            needed = PARSE_PRESETS if arm_files(options.root.resolve()) else ()
        except ParseError as problem:
            check_report.write_line(f"loop syntax: {problem}", is_error=True)
            return 1
        for preset in needed:
            check_report.write_line(preset)
        return 0
    return run(Path(options.build_dir).resolve(), REPO_ROOT, options.jobs)
