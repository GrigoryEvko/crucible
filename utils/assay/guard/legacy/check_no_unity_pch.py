"""no-unity-pch — no target of the tree compiles as a unity build or with a precompiled header.

The reflection walks of this tree see each header that a translation unit
includes.  A unity build puts some source files into one translation unit.
A precompiled header puts the headers of the header file into each unit that
uses it.  Each one changes what a walk sees, and it can change a result.
CLAUDE.md §XV states the rule.

WHAT THE CHECK READS
    The effect, from the compile database of a configured build
    (assay.readers.compdb):
      * A row of a unity source of CMake: a file unity_<N>_cxx.cxx or
        unity_<N>_c.c, or a file in a Unity directory of CMakeFiles.
      * A row with a precompiled header in its command: `-include` of a file
        with a name that starts with cmake_pch, `-include-pch`,
        `-Winvalid-pch`, or a word that ends in .gch or .pch.
    The cause, from the replies of the CMake file API of the same build
    (assay.readers.cmake).  The replies give the result of the configure of
    each CMake file of the tree:
      * A precompiled header of a compile group of a target, at the line of
        the call that adds it, such as target_precompile_headers.
      * A target that compiles a unity source, at the line of the command
        that makes the target.
      * A target that uses the precompiled header of a different target: a
        source cmake_pch.hxx and no header of its own.  The finding is at the
        line of the command that makes the target.
      * An entry of the CMake cache with a name that starts with
        CMAKE_UNITY_BUILD.
    And from CMakePresets.json and CMakeUserPresets.json: a cache variable
    of a preset with a name that starts with CMAKE_UNITY_BUILD.

    The check reads the configured build and not the text of a CMake file.
    A name in a CMake branch that no configure takes gives no finding.  Each
    CI leg runs the check on the build of its own preset.

    The tree has no unity build and no precompiled header, and the check has
    no ledger.  Each finding is an error.

Usage
    guard check --rule no-unity-pch --build-dir BUILD_DIR [--warnings-dir DIR]
    guard check --rule no-unity-pch --self-test

Exit 0 with no finding, 1 on a finding or an input that the check cannot
read, 2 on a usage error or a failed self-test.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import sys
import tempfile
from pathlib import Path, PurePosixPath

from assay.core import report as check_report
from assay.core.paths import REPO_ROOT
from assay.readers import cmake, compdb

CHECK = "no-unity-pch"
UNITY_CACHE_PREFIX = "CMAKE_UNITY_BUILD"
# The file that CMake writes for a precompiled header starts with this name: cmake_pch.hxx and cmake_pch.hxx.cxx.
PCH_FILE_PREFIX = "cmake_pch"
# Each word that pch_words() keeps contains one of these texts.
PCH_MARKERS = (PCH_FILE_PREFIX, "-include-pch", "-Winvalid-pch", ".gch", ".pch")
WHY = ("The reflection walks of this tree see each header that a translation unit includes, so a unity build or a "
       "precompiled header can change a result.")


def shown_path(path: str, root: Path) -> str:
    """Return a path relative to the root when it is in the tree, else the path as it is."""
    named = Path(path)
    return named.relative_to(root).as_posix() if named.is_absolute() and named.is_relative_to(root) else path


def preset_findings(root: Path) -> list[check_report.Finding]:
    """Return one error for each cache variable of a preset that turns a unity build on, and for each unread file.

    Args:
        root: The repository root

    Returns:
        The findings, in the sequence of the files and the presets
    """
    findings: list[check_report.Finding] = []
    for found in cmake.presets(root):
        rel = shown_path(str(found.path), root)
        if found.problem is not None:
            findings.append(check_report.Finding("error", rel, 0, CHECK, f"the check cannot read its presets: "
                                                                         f"{found.problem}"))
            continue
        for preset in found.configure:
            for name in preset.cache_variables:
                if name.startswith(UNITY_CACHE_PREFIX):
                    findings.append(check_report.Finding(
                        "error", rel, 0, CHECK,
                        f"the preset {preset.name} sets {name}, which turns a unity build on.  {WHY}  Remove the "
                        f"cache variable."))
    return findings


def is_unity_source(source: str) -> bool:
    """Say whether a source path is a unity source that CMake writes."""
    path = PurePosixPath(source)
    stem = path.name.split(".", 1)[0]
    parts = stem.split("_")
    is_unity_name = len(parts) == 3 and parts[0] == "unity" and parts[1].isdigit() and parts[2] in ("c", "cxx")
    return is_unity_name or ("Unity" in path.parts and "CMakeFiles" in path.parts)


def pch_words(argv: list[str]) -> list[str]:
    """Return each word of a compile command that includes a precompiled header."""
    found: list[str] = []
    for index, word in enumerate(argv):
        if word == "-include" and index + 1 < len(argv) and PurePosixPath(argv[index + 1]).name.startswith(
                PCH_FILE_PREFIX):
            found.append(f"-include {argv[index + 1]}")
        elif word.startswith("-include") and PurePosixPath(word[len("-include"):]).name.startswith(PCH_FILE_PREFIX):
            found.append(word)
        elif word in ("-include-pch", "-Winvalid-pch") or word.endswith((".gch", ".pch")):
            found.append(word)
    return found


def command_words(row: compdb.CompileEntry) -> list[str]:
    """Return the words of the compile command of one row of the compile database.

    A command string that holds no marker of a precompiled header gives no
    word that pch_words() keeps, so it is not split.  The split costs most of
    the time of the check on a database of thousands of rows.

    Raises:
        ValueError: If the command string is not valid shell quoting
    """
    if row.arguments is not None:
        return list(row.arguments)
    return list(row.argv()) if any(marker in row.command for marker in PCH_MARKERS) else []


def database_findings(build_dir: Path, root: Path) -> list[check_report.Finding]:
    """Return one error for each row of the compile database that compiles a unity source or uses a PCH.

    Complexity: linear in the size of the database.

    Args:
        build_dir: A configured build directory
        root: The repository root, to name a source inside the tree by its relative path

    Returns:
        The findings, or one error when the database does not exist or cannot be read
    """
    shown = str(compdb.database_file(build_dir))
    try:
        commands = [(row.file, command_words(row)) for row in compdb.entries(build_dir)]
    except compdb.MissingDatabase:
        return [check_report.Finding("error", shown, 0, CHECK, "the compile database does not exist, so the check "
                                                               "cannot see the compile commands.  Configure the "
                                                               "build first.")]
    except (compdb.CompdbError, ValueError) as exc:
        return [check_report.Finding("error", shown, 0, CHECK, f"the compile database cannot be read: {exc}")]
    findings: list[check_report.Finding] = []
    for source, argv in commands:
        where = shown_path(source, root)
        if is_unity_source(source):
            findings.append(check_report.Finding(
                "error", where, 0, CHECK,
                f"the compile database compiles the unity source {source}.  {WHY}  Turn the unity build off."))
        for word in pch_words(argv):
            findings.append(check_report.Finding(
                "error", where, 0, CHECK,
                f"the compile command of {source} includes a precompiled header ({word}).  {WHY}  Remove the "
                f"precompiled header."))
    return findings


def position_of(origin: cmake.Origin | None, root: Path, build_shown: str) -> tuple[str, int]:
    """Return the file and the line of a position in a CMake file, or the build directory when the reply gives none."""
    if origin is None:
        return build_shown, 0
    return shown_path(origin.file, root), origin.line


def target_findings(model: cmake.CodeModel, root: Path, build_shown: str) -> list[check_report.Finding]:
    """Return one error for each precompiled header and each unity source of a target.

    A target that uses the precompiled header of a different target gives an error too.

    Complexity: linear in the number of the targets and their sources.
    """
    findings: list[check_report.Finding] = []
    for target in model.targets:
        # Each compile group of a target names its headers, and one header can occur in more than one group.
        unique = {(header.header, header.origin or target.origin): header for header in target.precompile_headers}
        for header in unique.values():
            path, line = position_of(header.origin or target.origin, root, build_shown)
            findings.append(check_report.Finding(
                "error", path, line, CHECK,
                f"the target {target.name} gets the precompiled header {header.header}.  {WHY}  Remove the call that "
                f"adds it."))
        path, line = position_of(target.origin, root, build_shown)
        unity = sorted({source.path for source in target.sources if is_unity_source(source.path)})
        if unity:
            findings.append(check_report.Finding(
                "error", path, line, CHECK,
                f"the target {target.name} compiles the unity source {unity[0]} of a unity build.  {WHY}  Turn the "
                f"unity build of the target off."))
        borrowed = sorted({source.path for source in target.sources
                           if PurePosixPath(source.path).name.startswith(PCH_FILE_PREFIX)})
        if borrowed and not target.precompile_headers:
            findings.append(check_report.Finding(
                "error", path, line, CHECK,
                f"the target {target.name} uses the precompiled header {borrowed[0]} of a different target.  {WHY}  "
                f"Remove the precompiled header."))
    return findings


def reply_findings(build_dir: Path, root: Path) -> list[check_report.Finding]:
    """Return the findings of the code model and the cache of a build, or one error when a reply cannot be read."""
    build_shown = shown_path(str(build_dir), root)
    try:
        model = cmake.codemodel(build_dir)
        entries = cmake.cache(build_dir)
    except cmake.CmakeError as exc:
        return [check_report.Finding("error", build_shown, 0, CHECK, f"the check cannot read what CMake decided: "
                                                                     f"{exc}")]
    findings = target_findings(model, root, build_shown)
    for name in sorted(entries):
        if name.startswith(UNITY_CACHE_PREFIX):
            findings.append(check_report.Finding(
                "error", build_shown, 0, CHECK,
                f"the CMake cache of the build sets {name}={entries[name].value}, which turns a unity build on.  "
                f"{WHY}  Remove the cache entry, and configure the build again."))
    return findings


def evaluate(root: Path, build_dir: Path) -> list[check_report.Finding]:
    """Read the compile database and the replies of one build and the presets of one tree, and return each finding.

    Args:
        root: The repository root
        build_dir: A configured build directory

    Returns:
        The findings
    """
    return database_findings(build_dir, root) + reply_findings(build_dir, root) + preset_findings(root)


def _planted_target(name: str, sources: list[dict[str, object]], headers: list[str] | None = None) -> dict[str, object]:
    """Return a target object of the reply codemodel-v2: made at sub/CMakeLists.txt:4, with headers added at :9."""
    groups: list[dict[str, object]] = [{"language": "CXX", "sourceIndexes": [0]}]
    if headers:
        groups[0]["precompileHeaders"] = [{"header": header, "backtrace": 2} for header in headers]
    return {"name": name, "type": "STATIC_LIBRARY", "backtrace": 1, "paths": {"source": "sub", "build": "sub"},
            "backtraceGraph": {"commands": ["add_library", "target_precompile_headers"],
                               "files": ["sub/CMakeLists.txt"],
                               "nodes": [{"file": 0}, {"command": 0, "file": 0, "line": 4, "parent": 0},
                                         {"command": 1, "file": 0, "line": 9, "parent": 0}]},
            "sources": sources, "compileGroups": groups}


def self_test() -> int:
    """Plant each kind of unity build and precompiled header in a scratch tree and build, and check each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    clean_presets = {"version": 6, "configurePresets": [{"name": "default", "cacheVariables": {"CMAKE_BUILD_TYPE":
                                                                                               "Debug"}}]}
    clean_rows = [{"directory": "/b", "file": "/r/src/a.cpp", "arguments": ["g++", "-c", "/r/src/a.cpp"]},
                  {"directory": "/b", "file": "/r/src/b.cpp", "command": "g++ -include config.h -c /r/src/b.cpp"}]
    with tempfile.TemporaryDirectory(prefix="no-unity-pch-") as work:
        root = Path(work) / "tree"
        build = Path(work) / "build"
        build.mkdir()
        root.mkdir()
        (root / "CMakePresets.json").write_text(json.dumps(clean_presets), encoding="utf-8")
        database = compdb.plant(build, clean_rows)
        clean_target = _planted_target("plain", [{"path": "sub/a.cpp", "compileGroupIndex": 0}])
        cache: dict[str, str | tuple[str, str]] = {"CMAKE_BUILD_TYPE": ("STRING", "Debug"),
                                                   "CMAKE_SKIP_UNITY_BUILD_INCLUSION_X": ("BOOL", "ON")}

        def plant(targets: list[dict[str, object]], entries: dict[str, str | tuple[str, str]]) -> None:
            """Write the replies of the scratch build."""
            cmake.plant(build, cache=entries, targets=targets, source=str(root), build=str(build))

        def run() -> list[check_report.Finding]:
            """Evaluate the scratch tree."""
            return evaluate(root, build)

        plant([clean_target], cache)
        expect("a clean build gives no finding: a plain target, a cache entry that only contains a unity name, and a "
               "forced include that is no precompiled header", run() == [])

        cases: list[tuple[list[dict[str, object]], str, int]] = [
            ([_planted_target("header", [{"path": "sub/c.cpp", "compileGroupIndex": 0},
                                         {"path": f"{build}/CMakeFiles/header.dir/cmake_pch.hxx",
                                          "isGenerated": True}], ["<vector>"])],
             "a precompiled header of a target, at the line of its call", 9),
            ([_planted_target("reuse", [{"path": "sub/d.cpp", "compileGroupIndex": 0},
                                        {"path": f"{build}/CMakeFiles/header.dir/cmake_pch.hxx"}])],
             "a target that uses the precompiled header of a different target", 4),
            ([_planted_target("unity", [{"path": f"{build}/CMakeFiles/unity.dir/Unity/unity_0_cxx.cxx",
                                         "compileGroupIndex": 0}, {"path": "sub/a.cpp"}, {"path": "sub/b.cpp"}])],
             "a target that compiles a unity source", 4)]
        for targets, label, line in cases:
            plant(targets, cache)
            found = run()
            expect(f"an error: {label}", len(found) == 1 and found[0].level == "error"
                   and found[0].path == "sub/CMakeLists.txt" and found[0].line == line)
        plant([clean_target], {**cache, "CMAKE_UNITY_BUILD": ("BOOL", "ON")})
        found = run()
        expect("an error: a cache entry CMAKE_UNITY_BUILD",
               len(found) == 1 and "CMAKE_UNITY_BUILD=ON" in found[0].message)
        plant([clean_target], cache)

        presets = json.loads(json.dumps(clean_presets))
        presets["configurePresets"][0]["cacheVariables"]["CMAKE_UNITY_BUILD"] = "ON"
        (root / "CMakePresets.json").write_text(json.dumps(presets), encoding="utf-8")
        found = run()
        expect("an error: a preset that sets CMAKE_UNITY_BUILD",
               len(found) == 1 and found[0].path == "CMakePresets.json")
        (root / "CMakePresets.json").write_text(json.dumps(clean_presets), encoding="utf-8")
        (root / "CMakeUserPresets.json").write_text("{ not json", encoding="utf-8")
        found = run()
        expect("an error: a presets file that is not JSON",
               len(found) == 1 and found[0].path == "CMakeUserPresets.json" and "not valid JSON" in found[0].message)
        (root / "CMakeUserPresets.json").unlink()

        for row, label in (
                ({"directory": "/b", "file": "/b/CMakeFiles/t.dir/Unity/unity_0_cxx.cxx",
                  "arguments": ["g++", "-c", "unity_0_cxx.cxx"]}, "a unity source in the compile database"),
                ({"directory": "/b", "file": "/r/src/c.cpp",
                  "command": "g++ -Winvalid-pch -include /b/CMakeFiles/t.dir/cmake_pch.hxx -c /r/src/c.cpp"},
                 "the precompiled header of CMake in a command"),
                ({"directory": "/b", "file": "/r/src/d.cpp", "arguments": ["g++", "-include-pch", "x.pch", "-c", "d"]},
                 "an -include-pch option")):
            compdb.plant(build, clean_rows + [row])
            found = run()
            expect(f"an error: {label}", bool(found) and all(item.level == "error" for item in found))
        compdb.plant(build, "[{]")
        found = run()
        expect("an error: a compile database that cannot be read",
               len(found) == 1 and "cannot be read" in found[0].message)
        database.unlink()
        found = run()
        expect("an error: a missing compile database", len(found) == 1 and "does not exist" in found[0].message)
        compdb.plant(build, clean_rows)
        index = next(path for path in sorted((cmake.api_directory(build) / "reply").iterdir())
                     if cmake.is_index(path.name))
        index.unlink()
        found = run()
        expect("an error: a build with no reply of the file API",
               len(found) == 1 and "cannot read what CMake decided" in found[0].message)

        plant([_planted_target("header", [{"path": "sub/c.cpp", "compileGroupIndex": 0}], ["<vector>"])], cache)
        warnings_dir = Path(work) / "warnings"
        with contextlib.redirect_stdout(io.StringIO()) as printed:
            status = check_report.emit(run(), CHECK, warnings_dir)
        expect("an error gives exit status 1 and writes no warnings file",
               status == 1 and not (warnings_dir / f"{CHECK}.txt").exists()
               and check_report.parse_line(printed.getvalue().splitlines()[0]) is not None)
    if failures:
        print(f"check-no-unity-pch --self-test: FAILED, {len(failures)} case(s) did not hold")
        return 2
    print("check-no-unity-pch --self-test: every case holds.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    parser = argparse.ArgumentParser(prog="guard check --rule no-unity-pch", description=__doc__.split("\n", 1)[0])
    check_report.add_arguments(parser)
    parser.add_argument("--build-dir", type=Path, help="the configured build directory whose compile database and "
                                                       "file API replies the check reads")
    parser.add_argument("--self-test", action="store_true", help="plant each case in a scratch tree")
    arguments = parser.parse_args(argv)
    if arguments.self_test:
        return self_test()
    if arguments.build_dir is None:
        parser.error("give --build-dir BUILD_DIR, or --self-test")
    findings = evaluate(REPO_ROOT, arguments.build_dir.resolve())
    code = check_report.emit(findings, CHECK, arguments.warnings_dir)
    print(f"check-no-unity-pch: {len(findings)} finding(s).", file=sys.stderr)
    return code
