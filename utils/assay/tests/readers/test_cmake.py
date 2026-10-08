"""The test of the reader of the CMake file API and of the presets (assay.readers.cmake).

`guard selftest tests.readers.test_cmake` runs the test.  The planted cases
use no program.  The last case configures a scratch project with a unity
target and two targets with a precompiled header.  Then it reads the reply
that CMake writes.  Each case writes one line.  Exit 0 when each case holds,
else 1.

The last case uses the programs that the variables ASSAY_TEST_CMAKE,
ASSAY_TEST_CXX and ASSAY_TEST_NINJA name, which the registration of the test
sets.  With no variable, it uses the pinned cmake of PATH, and the c++ and
the ninja of PATH.
"""

import os
import shutil
import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.core import proc, testing
from assay.readers import cmake

PLANTED_TARGET = {
    "name": "planted",
    "type": "EXECUTABLE",
    "artifacts": [{"path": "bin/planted"}],
    "folder": {"name": "crucible_bench"},
    "paths": {"source": "sub", "build": "sub"},
    "backtrace": 1,
    "backtraceGraph": {
        "commands": ["add_executable", "target_precompile_headers"],
        "files": ["sub/CMakeLists.txt"],
        "nodes": [
            {"file": 0},
            {"command": 0, "file": 0, "line": 4, "parent": 0},
            {"command": 1, "file": 0, "line": 9, "parent": 0},
        ],
    },
    "sources": [
        {"path": "sub/main.cpp", "compileGroupIndex": 0},
        {"path": "/b/CMakeFiles/planted.dir/cmake_pch.hxx", "isGenerated": True},
    ],
    "compileGroups": [
        {"language": "CXX", "sourceIndexes": [0], "precompileHeaders": [{"header": "<vector>", "backtrace": 2}]}
    ],
    "link": {"commandFragments": [{"fragment": "-O1", "role": "flags"}, {"fragment": "-lm", "role": "libraries"}]},
}

SCRATCH_PROJECT = """cmake_minimum_required(VERSION 3.27)
project(planted CXX)
add_library(unity_target STATIC a.cpp b.cpp)
set_target_properties(unity_target PROPERTIES UNITY_BUILD ON)
add_library(header_target STATIC c.cpp)
target_precompile_headers(header_target PRIVATE <vector>)
add_library(reuse_target STATIC d.cpp)
target_precompile_headers(reuse_target REUSE_FROM header_target)
"""


def refused(read: Callable[[], object], error: type[cmake.CmakeError]) -> str:
    """Return the message of the error that a call raises, or an empty text when the call raises no such error."""
    try:
        read()
    except error as problem:
        return str(problem)
    return ""


def planted_cases(expect: testing.Expect) -> None:
    """Check each object of a planted reply: the cache, the roots, the inputs, the toolchains and the code model."""
    with tempfile.TemporaryDirectory(prefix="assay-cmake-") as scratch:
        build = Path(scratch)
        cmake.plant(
            build,
            cache={"CMAKE_HOME_DIRECTORY": "/r", "CMAKE_BUILD_TYPE": ("STRING", "Debug")},
            targets=[PLANTED_TARGET],
            inputs=["CMakeLists.txt", "sub/CMakeLists.txt"],
            source="/r",
            build="/b",
            compilers={"CXX": "/usr/bin/g++"},
        )
        entries = cmake.cache(build)
        expect(
            "the cache gives each entry with its type and its value",
            entries["CMAKE_HOME_DIRECTORY"] == cmake.CacheEntry("CMAKE_HOME_DIRECTORY", "INTERNAL", "/r")
            and entries["CMAKE_BUILD_TYPE"].type_name == "STRING"
            and entries["CMAKE_BUILD_TYPE"].value == "Debug",
        )
        expect(
            "cache_value() gives one value, or None for no entry",
            cmake.cache_value(build, "CMAKE_BUILD_TYPE") == "Debug" and cmake.cache_value(build, "ABSENT") is None,
        )
        expect("roots() gives the source root and the build root", cmake.roots(build) == ("/r", "/b"))
        expect(
            "cmake_files() gives each input as an absolute path",
            cmake.cmake_files(build) == ("/r/CMakeLists.txt", "/r/sub/CMakeLists.txt"),
        )
        chains = cmake.toolchains(build)
        expect(
            "toolchains() gives the compiler of each language",
            len(chains) == 1 and chains[0].language == "CXX" and chains[0].compiler == "/usr/bin/g++",
        )
        model = cmake.codemodel(build)
        (target,) = model.targets
        expect(
            "codemodel() gives the roots and the type, the folder and the absolute artifacts of a target",
            model.source == "/r"
            and model.build == "/b"
            and target.type_name == "EXECUTABLE"
            and target.folder == "crucible_bench"
            and target.artifacts == ("/b/bin/planted",),
        )
        expect(
            "a target gives its sources as absolute paths, with the generated flag and the compile group",
            [(source.path, source.is_generated, source.compile_group) for source in target.sources]
            == [("/r/sub/main.cpp", False, 0), ("/b/CMakeFiles/planted.dir/cmake_pch.hxx", True, None)],
        )
        (header,) = target.precompile_headers
        expect(
            "a precompiled header gives the file and the line of the call that adds it",
            header.header == "<vector>" and header.origin == cmake.Origin("/r/sub/CMakeLists.txt", 9),
        )
        expect(
            "a target gives the position of the command that makes it, and the directory of its CMake file",
            target.origin == cmake.Origin("/r/sub/CMakeLists.txt", 4) and target.source_dir == "/r/sub",
        )
        expect(
            "a target gives each link fragment with its role",
            target.link_fragments == (("-O1", "flags"), ("-lm", "libraries")),
        )
        cmake.plant(build, cache={"CMAKE_HOME_DIRECTORY": "/second"})
        expect(
            "a second plant replaces the first reply",
            cmake.cache_value(build, "CMAKE_HOME_DIRECTORY") == "/second" and cmake.codemodel(build).targets == (),
        )


def error_cases(expect: testing.Expect) -> None:
    """Check each refusal: no reply, no object of a kind, a file that is not JSON and a field of an incorrect shape."""
    with tempfile.TemporaryDirectory(prefix="assay-cmake-") as scratch:
        build = Path(scratch)
        empty = build / "empty"
        empty.mkdir()
        message = refused(lambda: cmake.cache(empty), cmake.MissingReply)
        expect(
            "a build with no reply is MissingReply, which names the directory and the configure",
            str(empty) in message and "cmake --preset default" in message,
        )
        reply = cmake.plant(build, cache={"A": "1"})
        index = next(path for path in sorted(reply.iterdir()) if cmake.is_index(path.name))
        index.write_text('{"objects": []}', encoding="utf-8")
        message = refused(lambda: cmake.cache(build), cmake.MissingReply)
        expect(
            "an index with no object of the kind is MissingReply, which names the index and the object",
            str(index) in message and "cache-v2" in message,
        )
        reply = cmake.plant(build, cache={"A": "1"})
        later = reply / "index-9999-12-31T00-00-00-0000.json"
        later.write_text("{", encoding="utf-8")
        message = refused(lambda: cmake.cache(build), cmake.CmakeError)
        expect(
            "the index with the largest name is the current one, and a file that is not JSON is an error",
            str(later) in message and "not JSON" in message,
        )
        reply = cmake.plant(build, cache={"A": "1"})
        (reply / "cache-v2-0.json").write_text(
            '{"entries": [{"name": "A", "type": 3, "value": "1"}]}', encoding="utf-8"
        )
        message = refused(lambda: cmake.cache(build), cmake.CmakeError)
        expect(
            "a field of an incorrect shape is an error that names the file and the field",
            "cache-v2-0.json" in message and "entry type" in message,
        )
        reply = cmake.plant(build, cache={"A": "1"})
        (reply / "cache-v2-0.json").unlink()
        message = refused(lambda: cmake.cache(build), cmake.MissingReply)
        expect("an object file that a configure removed is MissingReply", "cache-v2-0.json" in message)
        cmake.plant(build, targets=[{**PLANTED_TARGET, "backtrace": 7}])
        message = refused(lambda: cmake.codemodel(build), cmake.CmakeError)
        expect("a backtrace that is not a node of the graph is an error", "backtrace 7" in message)


def preset_cases(expect: testing.Expect) -> None:
    """Check the presets: the values of a preset, a missing file and a file that is not JSON."""
    with tempfile.TemporaryDirectory(prefix="assay-cmake-") as scratch:
        root = Path(scratch)
        expect("a tree with no presets file has no preset file", cmake.presets(root) == ())
        (root / "CMakePresets.json").write_text(
            '{"version": 6, "configurePresets": [{"name": "planted", "cacheVariables": {"A": "1", "B": {"type": '
            '"BOOL", "value": "ON"}, "C": true}, "environment": {"E": "x", "N": null}}]}',
            encoding="utf-8",
        )
        (root / "CMakeUserPresets.json").write_text("{", encoding="utf-8")
        first, second = cmake.presets(root)
        (preset,) = first.configure
        expect(
            "a preset gives each cache variable and each environment value as a text, or None for no text",
            preset.name == "planted"
            and preset.cache_variables == {"A": "1", "B": "ON", "C": None}
            and preset.environment == {"E": "x", "N": None}
            and first.problem is None,
        )
        expect(
            "a presets file that is not JSON gives a problem, and the other file still gives its presets",
            second.configure == () and second.problem is not None and "not valid JSON" in second.problem,
        )
        (root / "CMakeUserPresets.json").write_text('{"configurePresets": {}}', encoding="utf-8")
        expect(
            "configurePresets that is not a list gives a problem",
            cmake.presets(root)[1].problem == "configurePresets is not a list of objects",
        )
        for label, document in (
            ("no name", '{"configurePresets": [{"name": "first"}, {"cacheVariables": {"A": "1"}}]}'),
            ("a name that is not a text", '{"configurePresets": [{"name": 3}]}'),
            ("an empty name", '{"configurePresets": [{"name": ""}]}'),
        ):
            (root / "CMakeUserPresets.json").write_text(document, encoding="utf-8")
            unnamed = cmake.presets(root)[1]
            position = "2" if label == "no name" else "1"
            expect(
                f"a preset with {label} gives a problem that names its position, and the file gives no preset",
                unnamed.configure == ()
                and unnamed.problem == f"the item {position} of configurePresets has no name, and CMake refuses it",
            )


def programs() -> tuple[str, str, str]:
    """Return the cmake, the compiler and the ninja of the configured case (the module text)."""
    from assay.setup import pins

    try:
        default_cmake = pins.pinned_program("cmake")
    except pins.PinError:
        default_cmake = ""
    return (
        os.environ.get("ASSAY_TEST_CMAKE") or default_cmake,
        os.environ.get("ASSAY_TEST_CXX") or shutil.which("c++") or "",
        os.environ.get("ASSAY_TEST_NINJA") or shutil.which("ninja") or "",
    )


def configured_cases(expect: testing.Expect) -> None:
    """Configure a scratch project with request(), and read the reply that CMake writes."""
    program, compiler, ninja = programs()
    expect("the programs of the configured case are given", bool(program and compiler and ninja))
    if not (program and compiler and ninja):
        return
    with tempfile.TemporaryDirectory(prefix="assay-cmake-") as scratch:
        source = Path(scratch) / "project"
        build = Path(scratch) / "project-build"
        source.mkdir()
        (source / "CMakeLists.txt").write_text(SCRATCH_PROJECT, encoding="utf-8")
        for name in ("a", "b", "c", "d"):
            (source / f"{name}.cpp").write_text(f"int {name}_value() {{ return 1; }}\n", encoding="utf-8")
        cmake.request(build)
        done = proc.run(
            proc.Command(
                [
                    program,
                    "-S",
                    str(source),
                    "-B",
                    str(build),
                    "-G",
                    "Ninja",
                    f"-DCMAKE_CXX_COMPILER={compiler}",
                    f"-DCMAKE_MAKE_PROGRAM={ninja}",
                ],
                timeout_s=proc.TOOL_S,
            )
        )
        expect(f"the scratch project configures: {done.err.strip()[-300:]}", done.exit == 0)
        if done.exit != 0:
            return
        expect("the configure writes the cache file, and is_configured() sees it", cmake.is_configured(build))
        expect(
            "the reply of the configure gives the roots and the cache",
            cmake.roots(build) == (str(source), str(build))
            and cmake.cache_value(build, "CMAKE_HOME_DIRECTORY") == str(source),
        )
        expect(
            "the reply of the configure names the root CMakeLists.txt as an input",
            str(source / "CMakeLists.txt") in cmake.cmake_files(build),
        )
        expect(
            "the reply of the configure gives the C++ compiler",
            any(chain.language == "CXX" for chain in cmake.toolchains(build)),
        )
        targets = {target.name: target for target in cmake.codemodel(build).targets}
        unity = targets["unity_target"].sources
        expect(
            "a unity target compiles a unity source of CMake, and its own sources are in no compile group",
            any("/Unity/unity_" in item.path and item.compile_group is not None for item in unity)
            and all(item.compile_group is None for item in unity if item.path.endswith(("/a.cpp", "/b.cpp"))),
        )
        expect(
            "a target with a precompiled header gives the header and the line of its call",
            any(
                item.header == "<vector>" and item.origin == cmake.Origin(str(source / "CMakeLists.txt"), 6)
                for item in targets["header_target"].precompile_headers
            ),
        )
        reuse = targets["reuse_target"]
        expect(
            "a target that uses the precompiled header of a different target has no header and a source cmake_pch.hxx",
            not reuse.precompile_headers
            and any(Path(item.path).name == "cmake_pch.hxx" for item in reuse.sources)
            and reuse.origin == cmake.Origin(str(source / "CMakeLists.txt"), 7),
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."), argv, (planted_cases, error_cases, preset_cases, configured_cases)
    )
