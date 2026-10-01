#!/usr/bin/env python3
"""check-reconfigure-noop — a second configure and the two builds after it compile nothing.

A configure run that writes a file again with the same content, or a build
edge that is always dirty, makes each build compile objects that did not
change.  This check finds both.

WHAT THE CHECK DOES
    1. It builds BUILD_DIR with `cmake --build`, so that the build is
       current before the check measures it.
    2. It records the symbolic links in BUILD_DIR and the ninja log.
    3. It configures BUILD_DIR again, with `cmake -S SOURCE -B BUILD_DIR`.
       Each symbolic link whose target did not change must keep its inode
       and its change time, because a configure run must write only what it
       changes.
    4. It builds BUILD_DIR two times with `cmake --build`.  Each build may
       run only the glob check of CMake (CMakeFiles/cmake.verify_globs).  A
       change of the ninja log record of any other output is an error.  A
       record of build.ninja tells that CMake configured again during the
       build, so a glob with CONFIGURE_DEPENDS or a configure dependency
       changed with no source change.
    5. It reads the dependency log of Ninja (`ninja -t deps`).  Each
       recorded dependency must exist.  A dependency that does not exist
       makes its object dirty on each build.  A compiler launcher that gives
       the dependency file of another build directory, such as ccache with a
       base_dir above the compiler, records such a path.

    The check reads the ninja log of real builds and not the output of
    `ninja -n`.  A glob with CONFIGURE_DEPENDS makes the build manifest
    depend on an edge that always runs, and a dry run stops at that edge,
    so `ninja -n` shows the glob check and the CMake run and no object.

    Each finding is an error.  A build directory that is not a configured
    Ninja build, or a build that fails, is an error too, because the check
    cannot measure it.

Usage
    check-reconfigure-noop.py BUILD_DIR [--jobs N] [--warnings-dir DIR]
    check-reconfigure-noop.py --self-test [--cmake CMAKE] [--ninja NINJA]

Exit 0 with no finding, 1 on a finding, 2 on a usage error or a failed self-test.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import os
import shutil
import subprocess
import sys
import tempfile
from collections.abc import Callable
from concurrent.futures import ThreadPoolExecutor
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import check_report  # noqa: E402

CHECK = "reconfigure-noop"
GLOB_CHECK = Path("CMakeFiles") / "cmake.verify_globs"
MANIFEST = "build.ninja"
NINJA_LOG = ".ninja_log"
# The lines of build output that a finding prints, at most.
OUTPUT_LINES = 60


@dataclass(frozen=True, slots=True)
class BuildDir:
    """A configured Ninja build directory and the tools that it names."""

    path: Path
    source: Path
    cmake: str
    ninja: str


@dataclass(frozen=True, slots=True)
class LinkState:
    """The parts of one symbolic link that a rewrite changes."""

    target: str
    inode: int
    ctime_ns: int


def finding(path: str, message: str) -> check_report.Finding:
    """Return one error of this check.

    Args:
        path: The place of the finding
        message: The text

    Returns:
        The finding
    """
    return check_report.Finding("error", path, 0, CHECK, message)


def display(path: Path, root: Path) -> str:
    """Return a path relative to root when it is under root, else the absolute path.

    Args:
        path: An absolute path
        root: The repository root

    Returns:
        The text of the path
    """
    try:
        return path.relative_to(root).as_posix()
    except ValueError:
        return str(path)


def read_cache(build: Path) -> dict[str, str]:
    """Read the entries of CMakeCache.txt.

    Args:
        build: The build directory

    Returns:
        The value of each entry, by its name
    """
    entries: dict[str, str] = {}
    for raw in (build / "CMakeCache.txt").read_text(encoding="utf-8", errors="replace").splitlines():
        if not raw or raw.startswith(("#", "//")) or "=" not in raw:
            continue
        key, value = raw.split("=", 1)
        entries[key.split(":", 1)[0]] = value
    return entries


def open_build(build: Path) -> tuple[BuildDir | None, str]:
    """Read the build directory and the tools that its cache names.

    Args:
        build: The build directory

    Returns:
        The build directory, or None and the reason that the check cannot measure it
    """
    if not (build / "CMakeCache.txt").is_file():
        return None, f"{build} has no CMakeCache.txt.  Configure it before the check"
    cache = read_cache(build)
    generator = cache.get("CMAKE_GENERATOR", "")
    if generator != "Ninja":
        return None, f"{build} uses the generator '{generator}'.  The check reads the logs of Ninja"
    source = cache.get("CMAKE_HOME_DIRECTORY", "")
    cmake = cache.get("CMAKE_COMMAND", "")
    ninja = cache.get("CMAKE_MAKE_PROGRAM", "")
    for name, value in (("CMAKE_HOME_DIRECTORY", source), ("CMAKE_COMMAND", cmake), ("CMAKE_MAKE_PROGRAM", ninja)):
        if not value or not Path(value).exists():
            return None, f"the cache entry {name} of {build} names no existing path ('{value}')"
    return BuildDir(build, Path(source), cmake, ninja), ""


def run(argv: list[str], cwd: Path | None = None) -> tuple[int, str]:
    """Run one command and return its exit code and its combined output.

    Args:
        argv: The command
        cwd: The working directory, or None for the current one

    Returns:
        The exit code and the output
    """
    proc = subprocess.run(argv, cwd=cwd, text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, check=False)
    return proc.returncode, proc.stdout


def read_ninja_log(build: Path) -> dict[str, tuple[str, ...]]:
    """Read the last record of each output in the ninja log.

    Ninja can rewrite the log with one record for each output, so the check
    compares records and not lines.  Complexity: O(n) in the lines of the log.

    Args:
        build: The build directory

    Returns:
        The record of each output, by its absolute normalized path
    """
    records: dict[str, tuple[str, ...]] = {}
    for raw in (build / NINJA_LOG).read_text(encoding="utf-8", errors="replace").splitlines():
        if raw.startswith("#"):
            continue
        fields = raw.split("\t")
        if len(fields) < 5:
            continue
        start, end, mtime, output, command_hash = fields[:5]
        records[os.path.normpath(os.path.join(build, output))] = (start, end, mtime, command_hash)
    return records


def read_links(build: Path) -> dict[str, LinkState]:
    """Record each symbolic link under the build directory.

    Complexity: O(n) in the entries of the directory tree.

    Args:
        build: The build directory

    Returns:
        The state of each link, by its path
    """
    links: dict[str, LinkState] = {}
    for directory, subdirectories, files in os.walk(build):
        for name in (*subdirectories, *files):
            path = os.path.join(directory, name)
            try:
                status = os.lstat(path)
                if os.path.islink(path):
                    links[path] = LinkState(os.readlink(path), status.st_ino, status.st_ctime_ns)
            except OSError:
                continue
    return links


def build_once(tree: BuildDir, jobs: int) -> tuple[int, str]:
    """Build the directory with `cmake --build`, with the reasons of Ninja for each edge.

    Args:
        tree: The build directory
        jobs: The parallel jobs of the build

    Returns:
        The exit code and the output
    """
    return run([tree.cmake, "--build", str(tree.path), "--parallel", str(jobs), "--", "-d", "explain"])


def tail(text: str) -> str:
    """Return the last lines of a build output.

    Args:
        text: The output

    Returns:
        At most OUTPUT_LINES lines
    """
    return "\n".join(text.splitlines()[-OUTPUT_LINES:])


def ran_edges(before: dict[str, tuple[str, ...]], after: dict[str, tuple[str, ...]]) -> list[str]:
    """Return the outputs whose ninja log record is new or changed.

    Args:
        before: The records before a build
        after: The records after it

    Returns:
        The outputs in sorted order
    """
    return sorted(output for output, record in after.items() if before.get(output) != record)


def missing_dependencies(tree: BuildDir) -> tuple[dict[str, list[str]], str]:
    """Read the dependency log of Ninja and return each recorded dependency that does not exist.

    Complexity: O(n) in the lines of `ninja -t deps`.

    Args:
        tree: The build directory

    Returns:
        The missing dependencies of each output, and an error text when the log cannot be read
    """
    code, output = run([tree.ninja, "-C", str(tree.path), "-t", "deps"])
    if code != 0:
        return {}, f"`ninja -t deps` failed: {tail(output)}"
    # Ninja can list one output two times, under a relative and an absolute
    # name, so the outputs are normalized and each dependency counts once.
    missing: dict[str, list[str]] = {}
    target = ""
    for raw in output.splitlines():
        if not raw.strip():
            continue
        if not raw[0].isspace():
            target = os.path.normpath(os.path.join(tree.path, raw.split(": #deps", 1)[0]))
            continue
        dependency = raw.strip()
        known = missing.get(target, [])
        if dependency not in known and not os.path.exists(os.path.join(tree.path, dependency)):
            missing[target] = [*known, dependency]
    return missing, ""


def evaluate(build: Path, jobs: int, root: Path) -> tuple[list[check_report.Finding], str]:
    """Configure the build directory again, build it two times, and return the findings.

    Args:
        build: The build directory
        jobs: The parallel jobs of each build
        root: The repository root, for the paths of the findings

    Returns:
        The findings, and the build output that explains them
    """
    tree, reason = open_build(build)
    if tree is None:
        return [finding(str(build), f"the check cannot measure the build: {reason}.")], ""
    place = display(build, root)
    code, output = build_once(tree, jobs)
    if code != 0:
        return [finding(place, "the build fails, so the check cannot measure a second configure.  Make the build "
                               "pass first.")], tail(output)
    if not (build / NINJA_LOG).is_file():
        return [finding(place, f"the build wrote no {NINJA_LOG}, so the check cannot see which edges ran.")], ""
    links_before = read_links(build)
    log = read_ninja_log(build)
    code, output = run([tree.cmake, "-S", str(tree.source), "-B", str(build)])
    if code != 0:
        return [finding(place, "the second configure fails.")], tail(output)
    findings: list[check_report.Finding] = []
    for path, state in sorted(read_links(build).items()):
        old = links_before.get(path)
        if old is not None and old.target == state.target and (old.inode, old.ctime_ns) != (state.inode,
                                                                                            state.ctime_ns):
            findings.append(finding(display(Path(path), root),
                                    f"the second configure made this link again with the same target "
                                    f"{state.target}.  Make the link only when its target changes."))
    explained = ""
    allowed = os.path.normpath(build / GLOB_CHECK)
    manifest = os.path.normpath(build / MANIFEST)
    for attempt in ("first", "second"):
        code, output = build_once(tree, jobs)
        if code != 0:
            findings.append(finding(place, f"the {attempt} build after the second configure fails."))
            explained += tail(output) + "\n"
            break
        after = read_ninja_log(build)
        edges = [edge for edge in ran_edges(log, after) if edge != allowed]
        log = after
        for edge in edges:
            if edge == manifest:
                findings.append(finding(place, f"the {attempt} build configured again.  A glob with "
                                               f"CONFIGURE_DEPENDS or a configure dependency changed, and no source "
                                               f"changed."))
            else:
                findings.append(finding(display(Path(edge), root),
                                        f"the {attempt} build after the second configure ran this edge, and no "
                                        f"source changed.  Run `ninja -C {build} -d explain` to see why it is "
                                        f"dirty."))
        if edges:
            explained += f"--- the {attempt} build ---\n{tail(output)}\n"
    missing, problem = missing_dependencies(tree)
    if problem:
        findings.append(finding(place, problem))
    for target, dependencies in sorted(missing.items()):
        findings.append(finding(display(Path(target), root),
                                f"Ninja recorded the dependency {dependencies[0]} (and {len(dependencies) - 1} more) "
                                f"that does not exist, so the edge runs again on each build.  A launcher that gives "
                                f"the dependency file of another build directory writes such a path, for example "
                                f"ccache with a base_dir above the compiler."))
    return findings, explained


# ── The self-test ──────────────────────────────────────────────────

PROJECT = """cmake_minimum_required(VERSION 3.27)
project(reconfigure_noop_plant NONE)
file(GLOB inputs CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/src/*.txt")
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/kept.txt" CONTENT "kept\\n")
if(NOT IS_SYMLINK "${CMAKE_BINARY_DIR}/link")
  file(CREATE_LINK "${CMAKE_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/link" SYMBOLIC)
endif()
add_custom_command(OUTPUT copy.txt COMMAND "${CMAKE_COMMAND}" -E copy "${CMAKE_SOURCE_DIR}/src/input.txt" copy.txt
                   DEPENDS ${inputs} "${CMAKE_BINARY_DIR}/kept.txt")
add_custom_target(copy ALL DEPENDS copy.txt)
"""

PLANTS = {
    "clean": "",
    "link": ('file(REMOVE "${CMAKE_BINARY_DIR}/again")\n'
             'file(CREATE_LINK "${CMAKE_SOURCE_DIR}/src" "${CMAKE_BINARY_DIR}/again" SYMBOLIC)\n'),
    "rewrite": ('file(WRITE "${CMAKE_BINARY_DIR}/written.txt" "same\\n")\n'
                'add_custom_command(OUTPUT written.out COMMAND "${CMAKE_COMMAND}" -E copy written.txt written.out\n'
                '                   DEPENDS "${CMAKE_BINARY_DIR}/written.txt")\n'
                'add_custom_target(written ALL DEPENDS written.out)\n'),
    "missing": ('file(WRITE "${CMAKE_BINARY_DIR}/gone.in" "gone.out: ${CMAKE_BINARY_DIR}/no-such-header.h\\n")\n'
                'add_custom_command(OUTPUT gone.out COMMAND "${CMAKE_COMMAND}" -E copy gone.in gone.d\n'
                '                   COMMAND "${CMAKE_COMMAND}" -E touch gone.out DEPFILE gone.d)\n'
                'add_custom_target(gone ALL DEPENDS gone.out)\n'),
    "glob": ('add_custom_target(more ALL COMMAND "${CMAKE_COMMAND}" -DDIR=${CMAKE_SOURCE_DIR}/src\n'
             '                  -P "${CMAKE_SOURCE_DIR}/more.cmake")\n'),
    "failing": 'add_custom_target(failing ALL COMMAND "${CMAKE_COMMAND}" -E false)\n',
}
# The script of the glob plant writes a new input file in each build.
MORE_SCRIPT = 'string(TIMESTAMP stamp "%s%f" UTC)\nfile(WRITE "${DIR}/more-${stamp}.txt" "more\\n")\n'


def plant_project(work: Path, plant: str, cmake: str, ninja: str) -> tuple[Path, str]:
    """Write and configure one scratch project with one plant.

    Args:
        work: An empty scratch directory
        plant: The key of PLANTS
        cmake: The cmake program
        ninja: The ninja program

    Returns:
        The build directory, and the configure output when the configure failed (else an empty text)
    """
    source = work / "source"
    (source / "src").mkdir(parents=True)
    (source / "src" / "input.txt").write_text("input\n", encoding="utf-8")
    (source / "more.cmake").write_text(MORE_SCRIPT, encoding="utf-8")
    (source / "CMakeLists.txt").write_text(PROJECT + PLANTS[plant], encoding="utf-8")
    build = work / "build"
    code, output = run([cmake, "-G", "Ninja", f"-DCMAKE_MAKE_PROGRAM={ninja}", "-S", str(source), "-B", str(build)])
    return build, "" if code == 0 else output


def self_test(cmake: str, ninja: str) -> int:
    """Plant each defect in a scratch project, and check each verdict.

    The cases run at the same time, because each one waits on cmake and ninja.

    Args:
        cmake: The cmake program
        ninja: The ninja program

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    def case(plant: str) -> tuple[str, list[check_report.Finding], str]:
        work = Path(tempfile.mkdtemp(prefix=f"reconfigure-noop-{plant}-"))
        try:
            build, problem = plant_project(work, plant, cmake, ninja)
            if problem:
                return plant, [], problem
            found, _ = evaluate(build, 2, work)
            return plant, found, ""
        finally:
            shutil.rmtree(work, ignore_errors=True)

    with ThreadPoolExecutor(max_workers=len(PLANTS)) as pool:
        results = {plant: (found, problem) for plant, found, problem in pool.map(case, PLANTS)}
    for plant, (_, problem) in results.items():
        if problem:
            print(f"the scratch project of the plant {plant} did not configure:\n{problem}")

    def messages(plant: str) -> list[str]:
        return [f"{item.path}: {item.message}" for item in results[plant][0]]

    def only(plant: str, test: Callable[[check_report.Finding], bool]) -> bool:
        found = results[plant][0]
        return bool(found) and all(item.level == "error" and test(item) for item in found)

    expect("no finding: a configure that writes only what it changes, a glob and a link", messages("clean") == [])
    expect("an error: a link that the configure makes again",
           only("link", lambda item: item.path == "build/again" and "made this link again" in item.message))
    expect("an error: a file that the configure writes again, and the edge that reads it",
           only("rewrite", lambda item: item.path == "build/written.out" and "first build" in item.message))
    expect("an error: an edge with a dependency that does not exist runs in each build, and the dependency log "
           "names it",
           only("missing", lambda item: item.path == "build/gone.out") and len(results["missing"][0]) == 3
           and any("no-such-header.h" in item.message for item in results["missing"][0]))
    expect("an error: a build that changes a glob makes CMake configure again",
           any("configured again" in item.message for item in results["glob"][0]))
    expect("an error: a build that fails", only("failing", lambda item: "the build fails" in item.message))

    with tempfile.TemporaryDirectory(prefix="reconfigure-noop-") as work:
        root = Path(work)
        found, _ = evaluate(root / "missing", 2, root)
        expect("an error: a build directory that does not exist",
               len(found) == 1 and "has no CMakeCache.txt" in found[0].message)
        (root / "make").mkdir()
        (root / "make" / "CMakeCache.txt").write_text("CMAKE_GENERATOR:INTERNAL=Unix Makefiles\n", encoding="utf-8")
        found, _ = evaluate(root / "make", 2, root)
        expect("an error: a build directory of another generator",
               len(found) == 1 and "uses the generator 'Unix Makefiles'" in found[0].message)
        warnings_dir = root / "warnings"
        with contextlib.redirect_stdout(io.StringIO()) as printed:
            status = check_report.emit(found, CHECK, warnings_dir)
        expect("an error gives exit status 1, a line of the format and no warnings file",
               status == 1 and not (warnings_dir / f"{CHECK}.txt").exists()
               and check_report.parse_line(printed.getvalue().splitlines()[0]) is not None)
    if failures:
        for plant in PLANTS:
            print(f"  findings of {plant}: {messages(plant)}")
        print(f"check-reconfigure-noop --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-reconfigure-noop --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    parser = argparse.ArgumentParser(prog="check-reconfigure-noop.py", description=__doc__.split("\n", 1)[0])
    check_report.add_arguments(parser)
    parser.add_argument("build_dir", nargs="?", type=Path, help="the configured Ninja build directory")
    parser.add_argument("--jobs", type=int, default=8, help="the parallel jobs of each build (default 8)")
    parser.add_argument("--self-test", action="store_true", help="plant each defect in a scratch project")
    parser.add_argument("--cmake", default=shutil.which("cmake") or "cmake", help="the cmake of the self-test")
    parser.add_argument("--ninja", default=shutil.which("ninja") or "ninja", help="the ninja of the self-test")
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return self_test(arguments.cmake, arguments.ninja)
    if arguments.build_dir is None:
        parser.error("give BUILD_DIR, or --self-test")
    if arguments.jobs < 1:
        parser.error("--jobs must be at least 1")
    root = Path(__file__).resolve().parents[2]
    findings, explained = evaluate(arguments.build_dir.resolve(), arguments.jobs, root)
    code = check_report.emit(findings, CHECK, arguments.warnings_dir)
    if explained:
        print(explained, file=sys.stderr)
    print(f"check-reconfigure-noop: {len(findings)} finding(s).", file=sys.stderr)
    return code


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
