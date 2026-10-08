"""reconfigure-noop — a second configure and the two builds after it compile nothing.

A configure run that writes a file again with the same content makes each
build compile objects that did not change.  A build edge that is always
dirty does the same.  The rule finds the two.

THE RULE
    1. The rule builds the build directory with `cmake --build`.  The build
       is then current before the rule measures it.
    2. It records the symbolic links in the build directory.
    3. It configures the build directory again, with
       `cmake -S SOURCE -B BUILD_DIR`.  A symbolic link with a target that
       did not change must keep its inode and its change time, because a
       configure run must write only what it changes.
    4. It records the build log of Ninja, after the configure.  A configure
       run gives new times to the outputs of the CMake run of Ninja
       (build.ninja and the other files that CMake writes).  Their records
       then change in a build directory where Ninja ran CMake one time.  A
       record of the log before the configure can show those changes as
       edges that ran.
    5. It builds the build directory two times with `cmake --build`.  Each
       build can run only the glob check of CMake
       (CMakeFiles/cmake.verify_globs).  A change of the record of each other
       output is an error.  A record of build.ninja tells that CMake
       configured again during the build.  A glob with CONFIGURE_DEPENDS or a
       configure dependency changed, and no source changed.
    6. It reads the dependency log of Ninja with missing_dependencies() of
       assay.guard.engines.buildsys.build_graph.  Each file of the log must be
       on the disk.  A file that is not there makes its edge dirty on each
       build.  A compiler launcher that gives the dependency file of a
       different build directory records such a path.  An example is ccache
       with a base_dir above the compiler.

    The rule reads the build log of builds that ran, and not the output of
    `ninja -n`.  A glob with CONFIGURE_DEPENDS makes the build manifest wait
    for an edge that always runs, and a dry run stops at that edge.  Then
    `ninja -n` shows the glob check and the CMake run and no object.  The rule
    gives:
      `relinked`            a symbolic link that the second configure made
                            again with the same target.  The entity is the link
      `dirty-edge`          an edge that a build after the second configure
                            ran, with no change of a source.  The entity is its
                            output
      `reconfigured`        a build after the second configure that ran CMake
                            again.  The entity is build.ninja
      `missing-dependency`  a file of the dependency log that is not on the
                            disk.  The path and the entity are the output of
                            its list
      `step-fails`          the build, the second configure or a build after it
                            fails, or runs for more time than its limit.  The
                            entity is the step
      `input`               the build directory is not a configured Ninja
                            build, a program of its cache is not there, or a
                            log of Ninja cannot be read.  The entity is the input
    The rule has no ledger, and each finding is an error.  The end of the
    output of a step that explains a finding goes to the standard error.

THE JOBS
    `cmake --build` gets no --parallel.  CMake then uses
    CMAKE_BUILD_PARALLEL_LEVEL of the environment, or else the default of
    Ninja: the number of CPUs and two.

THE TEST
    The rule configures a build again and builds it, and a ctest test cannot
    do that to the build that runs it.  Its ctest test runs only the witness.
    A step of the CI build job runs the rule:
        guard check --rule reconfigure-noop --build-dir BUILD_DIR

THE PLANTS
    A plant with the file CMakeLists.txt.planted is a small CMake project.
    plant_context() copies it, and configures it with the Ninja generator
    into build/ of the copy.  For a control, it then adds a comment line to
    CMakeLists.txt, and the first build of the rule then runs CMake.  The
    build log then holds the outputs of a CMake run of Ninja before the
    second configure.  The rule must not call them edges that ran.  A
    plant gets no such line, because the rule refuses it in each state.
    Each CMake run costs approximately 0.4 s.  The rows of build.txt of a
    project plant tell only the case.  The cmake is that of
    ASSAY_TEST_CMAKE, or else the pinned cmake of PATH.  The ninja is that of
    ASSAY_TEST_NINJA, or else the ninja of PATH.  A plant with no project
    gets the planted build of assay.guard.engines.buildsys.planted, for a
    build directory that the rule cannot measure.

Complexity: three builds and one configure of the build directory, two walks of its tree, and one read of each log of
Ninja.
"""

import os
import shutil
import stat
from dataclasses import dataclass
from pathlib import Path

from assay.core import proc
from assay.core import report as check_report
from assay.guard.engines.buildsys import build_graph, planted
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import cmake as cmake_reader
from assay.readers import ninja as ninja_reader

RULE = "reconfigure-noop"
GLOB_CHECK = Path("CMakeFiles") / "cmake.verify_globs"
# The lines of the output of a step that a finding prints, at most.
OUTPUT_LINES = 60
PROJECT = "CMakeLists.txt"
PROJECT_PLANT = f"{PROJECT}{meta_runner.PLANTED_SUFFIX}"
GENERATOR = "Ninja"
ATTEMPTS = ("first", "second")


@dataclass(frozen=True, slots=True)
class BuildDir:
    """A configured Ninja build directory, its source tree and its cmake."""

    path: Path
    source: Path
    cmake: str


@dataclass(frozen=True, slots=True)
class LinkState:
    """The parts of one symbolic link that a write of the link again changes."""

    target: str
    inode: int
    ctime_ns: int


@dataclass(frozen=True, slots=True)
class Step:
    """The result of one command: its exit, or None when the command did not end, and the end of its output."""

    exit: int | None
    output: str

    @property
    def has_passed(self) -> bool:
        """True when the command ended with the exit 0."""
        return self.exit == 0


def error(path: str, message: str, kind: str, entity: str) -> check_report.Finding:
    """Return one error of the rule."""
    return check_report.Finding("error", path, 0, RULE, message, kind, entity)


def tail(text: str) -> str:
    """Return the last OUTPUT_LINES lines of an output."""
    return "\n".join(text.splitlines()[-OUTPUT_LINES:])


def run_step(argv: list[str]) -> Step:
    """Run one command of the rule with its two outputs together, and return its exit and the end of its output.

    A command that cannot start, or that runs for more time than its limit,
    gives no exit, and its output tells why.
    """
    try:
        done = proc.run(proc.Command(argv, timeout_s=proc.BUILD_S, stderr=proc.Output.MERGE))
    except proc.TimedOut as problem:
        kept = problem.completed.out if problem.completed is not None else ""
        return Step(None, f"{problem}\n{tail(kept)}")
    except proc.CannotStart as problem:
        return Step(None, str(problem))
    return Step(done.exit, tail(done.out))


def open_build(build: Path) -> BuildDir | str:
    """Return the build directory and the cmake that its cache names, or the reason that the rule cannot measure it."""
    if not cmake_reader.is_configured(build):
        return f"{build} is not a configured build directory.  Configure it before the rule"
    try:
        cache = {name: entry.value for name, entry in cmake_reader.cache(build).items()}
    except cmake_reader.CmakeError as problem:
        return f"the CMake cache of {build} cannot be read: {problem}"
    generator = cache.get("CMAKE_GENERATOR", "")
    if generator != GENERATOR:
        return f"{build} uses the generator '{generator}'.  The rule reads the logs of Ninja"
    for name in ("CMAKE_HOME_DIRECTORY", "CMAKE_COMMAND", "CMAKE_MAKE_PROGRAM"):
        value = cache.get(name, "")
        if not value or not Path(value).exists():
            return f"the cache entry {name} of {build} names no path on the disk ('{value}')"
    return BuildDir(build, Path(cache["CMAKE_HOME_DIRECTORY"]), cache["CMAKE_COMMAND"])


def read_links(build: Path) -> dict[str, LinkState]:
    """Record each symbolic link in the tree of the build directory, by its path.

    Complexity: linear in the entries of the tree, with one lstat for each entry.
    """
    links: dict[str, LinkState] = {}
    for directory, subdirectories, files in sorted(os.walk(build)):
        for name in (*subdirectories, *files):
            path = os.path.join(directory, name)
            try:
                status = os.lstat(path)
                if stat.S_ISLNK(status.st_mode):
                    links[path] = LinkState(os.readlink(path), status.st_ino, status.st_ctime_ns)
            except OSError:
                continue
    return links


def build_once(tree: BuildDir) -> Step:
    """Build the directory with `cmake --build`, with the reasons of Ninja for each edge (THE JOBS)."""
    return run_step([tree.cmake, "--build", str(tree.path), "--", "-d", "explain"])


def relinked(
    context: BuildContext, before: dict[str, LinkState], after: dict[str, LinkState]
) -> list[check_report.Finding]:
    """Return one error for each link that a configure made again with the same target."""
    found: list[check_report.Finding] = []
    for path, state in sorted(after.items()):
        old = before.get(path)
        if (
            old is not None
            and old.target == state.target
            and (old.inode, old.ctime_ns) != (state.inode, state.ctime_ns)
        ):
            shown = context.shown(path)
            found.append(
                error(
                    shown,
                    f"the second configure made this link again with the same target "
                    f"{state.target}.  Make the link only when its target changes",
                    "relinked",
                    shown,
                )
            )
    return found


def dirty_edges(
    context: BuildContext, tree: BuildDir, log: dict[str, ninja_reader.LogRecord], explained: list[str]
) -> list[check_report.Finding]:
    """Build the directory two times, and return one error for each edge that ran and for a build that fails.

    Complexity: linear in the records of the build log, for each build.
    """
    build = tree.path
    place = context.shown(build)
    allowed = os.path.normpath(build / GLOB_CHECK)
    manifest = os.path.normpath(ninja_reader.graph_file(build))
    found: list[check_report.Finding] = []
    for attempt in ATTEMPTS:
        step = build_once(tree)
        if not step.has_passed:
            found.append(
                error(place, f"the {attempt} build after the second configure fails", "step-fails", f"{attempt} build")
            )
            explained.append(step.output)
            break
        try:
            after = ninja_reader.log(build)
        except ninja_reader.NinjaError as problem:
            found.append(
                error(
                    place,
                    f"the build log of Ninja cannot be read after the {attempt} build: {problem}",
                    "input",
                    ninja_reader.LOG_NAME,
                )
            )
            break
        edges = [edge for edge in ninja_reader.ran_outputs(log, after) if edge != allowed]
        log = after
        for edge in edges:
            if edge == manifest:
                found.append(
                    error(
                        place,
                        f"the {attempt} build configured again.  A glob with CONFIGURE_DEPENDS or a "
                        f"configure dependency changed, and no source changed",
                        "reconfigured",
                        ninja_reader.GRAPH_NAME,
                    )
                )
            else:
                shown = context.shown(edge)
                found.append(
                    error(
                        shown,
                        f"the {attempt} build after the second configure ran this edge, and no "
                        f"source changed.  Run `ninja -C {build} -d explain` to see why it is "
                        f"dirty",
                        "dirty-edge",
                        shown,
                    )
                )
        if edges:
            explained.append(f"--- the {attempt} build ---\n{step.output}")
    return found


def missing_findings(context: BuildContext, build: Path) -> list[check_report.Finding]:
    """Return one error for each output when its list in the dependency log names a file that is not on the disk."""
    try:
        missing = build_graph.missing_dependencies(build)
    except ninja_reader.NinjaError as problem:
        return [
            error(
                context.shown(build),
                f"the dependency log of Ninja cannot be read: {problem}",
                "input",
                ninja_reader.DEPS_NAME,
            )
        ]
    found: list[check_report.Finding] = []
    for target, dependencies in sorted(missing.items()):
        shown = context.shown(target)
        found.append(
            error(
                shown,
                f"Ninja recorded the dependency {dependencies[0]} (and {len(dependencies) - 1} "
                f"more), which is not on the disk, and the edge runs again on each build.  A launcher "
                f"that gives the dependency file of a different build directory writes such a path, "
                f"for example ccache with a base_dir above the compiler",
                "missing-dependency",
                shown,
            )
        )
    return found


def evaluate(context: BuildContext, explained: list[str]) -> list[check_report.Finding]:
    """Build the directory, configure it again, build it two times, and return the findings (THE RULE).

    The end of the output of each step that explains a finding goes into explained.

    Raises:
        NotApplicable: If the call gives no build directory
    """
    build = context.require_build()
    place = context.shown(build)
    tree = open_build(build)
    if isinstance(tree, str):
        return [error(place, f"the rule cannot measure the build: {tree}", "input", "build directory")]
    step = build_once(tree)
    if not step.has_passed:
        explained.append(step.output)
        return [
            error(
                place,
                "the build fails, and the rule cannot measure a second configure.  Make the build pass first",
                "step-fails",
                "build",
            )
        ]
    if not ninja_reader.log_file(build).is_file():
        return [
            error(
                place,
                f"the build wrote no {ninja_reader.LOG_NAME}, and the rule cannot see which edges ran",
                "input",
                ninja_reader.LOG_NAME,
            )
        ]
    links_before = read_links(build)
    step = run_step([tree.cmake, "-S", str(tree.source), "-B", str(build)])
    if not step.has_passed:
        explained.append(step.output)
        return [error(place, "the second configure fails", "step-fails", "second configure")]
    try:
        log = ninja_reader.log(build)
    except ninja_reader.NinjaError as problem:
        return [error(place, f"the build log of Ninja cannot be read: {problem}", "input", ninja_reader.LOG_NAME)]
    found = relinked(context, links_before, read_links(build))
    found += dirty_edges(context, tree, log, explained)
    found += missing_findings(context, build)
    return found


def findings(context: BuildContext) -> list[check_report.Finding]:
    """Give the findings of the rule, and write the output of each step that explains one to the standard error.

    Raises:
        NotApplicable: If the call gives no build directory
    """
    explained: list[str] = []
    found = evaluate(context, explained)
    for text in explained:
        check_report.write_line(text, is_error=True)
    return found


# ── The plants ─────────────────────────────────────────────────────────────


def planted_tools() -> tuple[str, str]:
    """Return the cmake and the ninja of a planted project (THE PLANTS).

    Raises:
        NotApplicable: If no cmake is at its pin, or no ninja is given or on PATH
    """
    from assay.setup import pins

    cmake = os.environ.get("ASSAY_TEST_CMAKE", "")
    if not cmake:
        try:
            cmake = str(pins.pinned_program("cmake"))
        except pins.PinError as problem:
            raise meta_runner.NotApplicable(str(problem)) from None
    ninja = os.environ.get("ASSAY_TEST_NINJA", "") or shutil.which("ninja") or ""
    if not ninja:
        raise meta_runner.NotApplicable("no ninja: give ASSAY_TEST_NINJA, or put a ninja on PATH")
    return cmake, ninja


def plant_context(plant: Path, scratch: Path) -> BuildContext:
    """Copy a planted project, configure it, make the first build of a control run CMake, and give the context.

    THE PLANTS of the module text tells the steps.

    Raises:
        NotApplicable: If the tools are not there, or the project does not configure
    """
    if not (plant / PROJECT_PLANT).is_file():
        return planted.plant_context(plant, scratch)
    meta_runner.copy_plant(plant, scratch)
    cmake, ninja = planted_tools()
    build = scratch / planted.BUILD
    # The planted project has no cmake_file_api call.  Its build directory gets the query of the replies.
    cmake_reader.request(build)
    step = run_step([cmake, "-G", GENERATOR, f"-DCMAKE_MAKE_PROGRAM={ninja}", "-S", str(scratch), "-B", str(build)])
    if not step.has_passed:
        raise meta_runner.NotApplicable(f"the planted project does not configure:\n{step.output}")
    if plant.name.startswith(meta_runner.CONTROL_PREFIX):
        project = scratch / PROJECT
        project.write_text(project.read_text(encoding="utf-8") + "# changed after the configure\n", encoding="utf-8")
    return BuildContext(scratch, build)
