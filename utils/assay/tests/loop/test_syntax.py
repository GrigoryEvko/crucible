"""The test of the driver of a parse build (assay.loop.syntax).

`guard selftest tests.loop.test_syntax` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The cases hold the words of a parse and the native flags of a cross compiler.
They also hold the refusal of a build that is no parse build.  Other cases
parse one unit with a planted compiler that writes a report, and hold the
check of a planted parse build, which parses nothing.
"""

import contextlib
import io
import sys
import tempfile
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import testing
from assay.guard.engines.plugin import runner as plugin_runner
from assay.loop import syntax
from assay.readers import cmake, compdb, ninja, sections

# A compiler that writes one report file into the directory of its plugin argument out=, and a dependency file with
# the source when the words give -MF.  It exits 3 for a unit named bad.cpp.
FAKE_COMPILER = """
import pathlib, sys
out = next(word.split("=", 1)[1] for word in sys.argv if word.startswith("-fplugin-arg-crucible_quarantine-out="))
pathlib.Path(out).mkdir(parents=True, exist_ok=True)
(pathlib.Path(out) / "a.cpp.0000000000000000.quarantine").write_text("# unit a.cpp\\n", encoding="utf-8")
if "-MF" in sys.argv:
    source = next(word for word in sys.argv if word.endswith(".cpp"))
    pathlib.Path(sys.argv[sys.argv.index("-MF") + 1]).write_text(f"a.o: {source}\\n", encoding="utf-8")
sys.exit(3 if "bad.cpp" in sys.argv else 0)
"""


def planted_compiler(directory: Path, name: str, text: str) -> str:
    """Write an executable Python script as a compiler, and return its path."""
    path = directory / name
    path.write_text(f"#!{sys.executable}\n{text}", encoding="utf-8")
    path.chmod(0o755)
    return str(path)


def word_cases(expect: testing.Expect) -> None:
    """Hold the words of the parse of a compile command."""
    report = Path("/build/quarantine/syntax/a.o")
    words = ("g++", "-DX", "-MD", "-MT", "a.o", "-MF", "a.d", "-march=native", "-o", "a.o", "-c", "a.cpp")
    parsed = syntax.parse_words(words, report)
    expect(
        "a parse drops -c, the output and the dependency file, writes its own dependency file, and adds the report "
        "argument last",
        parsed
        == [
            "g++",
            "-DX",
            "-march=native",
            "a.cpp",
            "-fsyntax-only",
            "-MD",
            "-MF",
            str(report / syntax.DEPENDENCY_FILE),
            "-fdiagnostics-color=never",
            f"{plugin_runner.PLUGIN_ARGUMENT}out={report}",
        ],
    )
    native_dropped = syntax.parse_words(words, report, drops_native=True)
    expect(
        "a parse for a compiler that refuses -march=native drops the native flag",
        "-march=native" not in native_dropped and "a.cpp" in native_dropped,
    )


def native_cases(expect: testing.Expect) -> None:
    """Hold the test of a compiler that refuses -march=native."""
    with tempfile.TemporaryDirectory(prefix="syntax-native-") as scratch_text:
        scratch = Path(scratch_text)
        native = planted_compiler(scratch, "native-g++", "import sys\nsys.exit(0)\n")
        cross = planted_compiler(
            scratch, "cross-g++", "import sys\nsys.exit(1 if '-march=native' in sys.argv else 0)\n"
        )
        expect("a compiler that takes -march=native does not refuse it", not syntax.refuses_native(native))
        expect("a cross compiler refuses -march=native", syntax.refuses_native(cross))
        expect("a compiler that cannot run refuses it", syntax.refuses_native(str(scratch / "missing-g++")))


def build_cases(expect: testing.Expect) -> None:
    """Hold the refusal of a build that is no parse build."""
    with tempfile.TemporaryDirectory(prefix="syntax-build-") as scratch_text:
        scratch = Path(scratch_text)
        cmake.plant(scratch, cache={build_config.SYNTAX_OPTION: ("BOOL", "OFF")})
        try:
            syntax.units_of(scratch, scratch)
            is_refused = False
        except syntax.ParseError as problem:
            is_refused = build_config.SYNTAX_OPTION in str(problem) and "preset" in str(problem)
        expect("a build that is no parse build is refused, with the option and the presets", is_refused)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()) as errors:
            status = syntax.run(scratch, scratch, 2)
        expect(
            "the run of a build that is no parse build exits 1", status == 1 and "no parse build" in errors.getvalue()
        )


def parsed_unit(unit: syntax.Unit, build: Path, enforce: str) -> tuple[syntax.Parsed, list[str], str, Path]:
    """Parse one unit with no native refuser, and return the result, the words, the key and the report directory."""
    report_dir = sections.report_directory(build, unit.object_path)
    words = syntax.parse_words(unit.words, report_dir)
    key = syntax.words_key(words, enforce)
    return syntax.parse_unit(unit, words, key, report_dir), words, key, report_dir


def unit_cases(expect: testing.Expect) -> None:
    """Hold the parse of one unit: its report directory, its stamp, a unit that does not parse, and a kept report."""
    import os

    with tempfile.TemporaryDirectory(prefix="syntax-unit-") as scratch_text:
        scratch = Path(scratch_text)
        compiler = planted_compiler(scratch, "g++", FAKE_COMPILER)
        source = scratch / "a.cpp"
        source.write_text("int a;\n", encoding="utf-8")
        os.utime(source, ns=(1, 1))
        build = scratch / "build"
        good = syntax.Unit(build / "t.dir" / "a.cpp.o", "s1", (compiler, "-c", "a.cpp", "-o", "a.o"), str(scratch))
        parsed, _, key, report_dir = parsed_unit(good, build, "e1")
        expect("a unit that parses exits 0", parsed.exit == 0)
        expect(
            "a unit that parses gives its report and its stamp to the reader",
            sections.read_report(report_dir) == sections.Section("s1", ()),
        )
        expect(
            "the report of a parse stays with the same key and stamp, and with no dependency newer than the parse",
            syntax.is_kept(good, report_dir, key, {}),
        )
        expect("a different key gives a new parse", not syntax.is_kept(good, report_dir, key + "0", {}))
        stamped = syntax.Unit(good.object_path, "s2", good.words, good.directory)
        expect("a different stamp gives a new parse", not syntax.is_kept(stamped, report_dir, key, {}))
        os.utime(source)
        expect("a dependency newer than the parse gives a new parse", not syntax.is_kept(good, report_dir, key, {}))
        os.utime(source, ns=(1, 1))
        source.unlink()
        expect("a dependency that does not exist gives a new parse", not syntax.is_kept(good, report_dir, key, {}))
        bad = syntax.Unit(build / "t.dir" / "bad.cpp.o", "s1", (compiler, "-c", "bad.cpp"), str(scratch))
        failed, _, bad_key, bad_dir = parsed_unit(bad, build, "e1")
        expect(
            "a unit that does not parse gives its exit, and the run writes no stamp and no key for it",
            failed.exit == 3
            and not (bad_dir / sections.STAMP_FILE).exists()
            and not (bad_dir / syntax.KEY_FILE).exists()
            and not syntax.is_kept(bad, bad_dir, bad_key, {}),
        )
        syntax.prune_reports(build, {report_dir})
        expect(
            "the prune removes the report directory of a unit that the build does not have, and keeps the others",
            report_dir.is_dir() and not bad_dir.exists(),
        )


# A cmake that builds the input target as a configure after a change of the plugin does.  After --build, it writes the
# new stamp into the compile database of the build directory.
RECONFIGURE_CMAKE = """
import pathlib, sys
database = pathlib.Path(sys.argv[sys.argv.index("--build") + 1]) / "compile_commands.json"
database.write_text(database.read_text().replace("stamp=0ld0ld", "stamp=fe5hfe5h"))
"""
# A ninja that gives the object of the planted build as the one input of the target all.
INPUTS_NINJA = """
import pathlib, sys
build = pathlib.Path(sys.argv[sys.argv.index("-C") + 1])
print(build / "t.dir" / "a.cpp.o")
"""


def planted_build(scratch: Path) -> tuple[Path, str]:
    """Plant a parse build of one unit a.cpp in a scratch directory, and return the build and the object of the unit.

    The cmake of the build writes a new stamp into the compile database at its first build of the input targets.  A
    configure after a change of the plugin does the same.
    """
    build = scratch / "build"
    build.mkdir()
    compiler = planted_compiler(scratch, "g++", FAKE_COMPILER)
    cmake.cache_file(build).write_text("")
    cmake.plant(
        build,
        cache={
            build_config.SYNTAX_OPTION: ("BOOL", "ON"),
            "CMAKE_GENERATOR": "Ninja",
            "CMAKE_MAKE_PROGRAM": planted_compiler(scratch, "ninja", INPUTS_NINJA),
            "CMAKE_COMMAND": planted_compiler(scratch, "cmake", RECONFIGURE_CMAKE),
        },
    )
    output = str(build / "t.dir" / "a.cpp.o")
    arguments = [compiler, f"{plugin_runner.PLUGIN_ARGUMENT}stamp=0ld0ld", "-c", "a.cpp", "-o", output]
    compdb.plant(
        build,
        [{"directory": str(scratch), "file": str(scratch / "a.cpp"), "arguments": arguments, "output": output}],
    )
    ninja.plant_graph(build, f"rule phony_rule\n  command = true\nbuild {syntax.INPUT_PREFIX}t: phony_rule\n")
    return build, output


def stamp_cases(expect: testing.Expect) -> None:
    """Hold the stamp of a run after a change of the plugin: the run reads the compile commands after step 1."""
    with tempfile.TemporaryDirectory(prefix="syntax-stamp-") as scratch_text:
        scratch = Path(scratch_text)
        build, output = planted_build(scratch)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()) as errors:
            status = syntax.run(build, scratch, 2)
        section = sections.read_report(sections.report_directory(build, output))
        expect(
            "a run after a change of the plugin writes the stamp that the input targets gave the compile database",
            status == 0 and section is not None and section.stamp == "fe5hfe5h" and not errors.getvalue(),
        )


def check_cases(expect: testing.Expect) -> None:
    """Hold the check of a parse build: it parses nothing, and only a current report keeps its unit (THE CHECK)."""
    import os

    with tempfile.TemporaryDirectory(prefix="syntax-check-") as scratch_text:
        scratch = Path(scratch_text)
        build, output = planted_build(scratch)
        source = scratch / "a.cpp"
        source.write_text("int a;\n", encoding="utf-8")
        os.utime(source, ns=(1, 1))
        before = syntax.locked_plan(build, scratch, 2)
        expect(
            "the check of a parse build with no report plans the parse of its unit, and writes no report",
            [item.unit.object_path for item in before.planned] == [Path(output)]
            and not before.kept
            and not sections.report_directory(build, output).exists(),
        )
        expect("the check holds the lock file of the build", (build / syntax.LOCK_FILE).is_file())
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            status = syntax.run(build, scratch, 2)
        after = syntax.locked_plan(build, scratch, 2)
        expect(
            "after a run, the check keeps the unit with its current report",
            status == 0 and not after.planned and [unit.object_path for unit in after.kept] == [Path(output)],
        )
        os.utime(source)
        expect(
            "a dependency that changed after the run makes the check plan the parse again",
            len(syntax.locked_plan(build, scratch, 2).planned) == 1,
        )


def macro_cases(expect: testing.Expect) -> None:
    """Hold the names that is_arm_macro() accepts, and the names that it refuses."""
    accepted = (
        "__aarch64__",
        "__x86_64__",
        "__x86_64",
        "__i386__",
        "__mips",
        "__ARM_NEON",
        "__ARM_NEON__",
        "__ARM_FEATURE_SVE",
        "__AVX2__",
        "__AVX512BW__",
        "__SSE4_2__",
        "__FMA__",
        "__AMX_TILE__",
        "_M_ARM64",
    )
    refused = (
        "__linux__",
        "__APPLE__",
        "__SANITIZE_THREAD__",
        "__SHARED__",
        "__clang__",
        "__cplusplus",
        "PLANTED_AVX2",
        "__AVX2",
        "__ARM_",
        "ARM_NEON",
        "_M_PLANTED",
    )
    expect(
        "is_arm_macro accepts each macro of an architecture or an ISA extension",
        all(syntax.is_arm_macro(name) for name in accepted),
    )
    expect("is_arm_macro refuses each other macro", not any(syntax.is_arm_macro(name) for name in refused))


def change_cases(expect: testing.Expect) -> None:
    """Hold arm_files() on a planted change, and build_directory() on a planted presets file."""
    from assay.core import git

    simd = b"int lanes() {\n#if defined(__AVX2__) && !defined(__AVX512BW__)\n    return 8;\n#endif\n    return 1;\n}\n"
    neon = b"#ifdef __ARM_NEON\nint wide = 4;\n#elif defined(OTHER)\nint wide = 2;\n#endif\n"
    plain = b"// __aarch64__ in a comment is no arm.\nint one() { return 1; }\n"
    presets = (
        b'{"version": 6, "configurePresets": [{"name": "syntax-planted", '
        b'"binaryDir": "${sourceDir}/build-${presetName}"}, {"name": "bare"}]}'
    )
    files = {"src/simd.cpp": simd, "src/neon.h": neon, "src/plain.cpp": plain, "notes.txt": b"__AVX2__\n"}
    with git.scratch_repository({**files, "CMakePresets.json": presets}) as root:
        git.commit_all(root, "The base of the change")
        expect("a tree with no change gives no arm file", syntax.arm_files(root) == {})
        (root / "src" / "plain.cpp").write_bytes(plain + b"int two() { return 2; }\n")
        (root / "notes.txt").write_bytes(b"__aarch64__\n")
        expect(
            "a change of a file with no arm and of a file that is no C++ gives no arm file",
            syntax.arm_files(root) == {},
        )
        (root / "src" / "simd.cpp").write_bytes(b"int lanes() { return 1; }\n")
        (root / "src" / "neon.h").unlink()
        (root / "src" / "fresh.cpp").write_bytes(b"#if __x86_64__\nint x = 1;\n#endif\n")
        removed = {"src/neon.h": frozenset({"__ARM_NEON"}), "src/simd.cpp": frozenset({"__AVX2__", "__AVX512BW__"})}
        expect(
            "an arm that the change removes, a removed file with an arm, and a new file with an arm are changes of arms",
            syntax.arm_files(root) == {"src/fresh.cpp": frozenset({"__x86_64__"}), **removed},
        )
        expect(
            "build_directory expands sourceDir and presetName",
            syntax.build_directory(root, "syntax-planted") == root / "build-syntax-planted",
        )
        for preset, text in (("bare", "gives no binaryDir"), ("missing", "gives the configure preset missing")):
            try:
                syntax.build_directory(root, preset)
                problem = ""
            except syntax.ParseError as error:
                problem = str(error)
            expect(f"build_directory refuses the preset {preset}", text in problem)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (word_cases, native_cases, build_cases, unit_cases, stamp_cases, check_cases, macro_cases, change_cases),
    )
