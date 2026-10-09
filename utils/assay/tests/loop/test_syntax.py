"""The test of the driver of a parse build (assay.loop.syntax).

`guard selftest tests.loop.test_syntax` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The cases hold the words of a parse and the native flags of a cross compiler.
They also hold the refusal of a build that is no parse build.  The last case
parses one unit with a planted compiler that writes a report.
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
from assay.readers import cmake, sections

# A compiler that writes one report file into the directory of its plugin argument out=, and exits 3 for a unit
# named bad.cpp.
FAKE_COMPILER = """
import pathlib, sys
out = next(word.split("=", 1)[1] for word in sys.argv if word.startswith("-fplugin-arg-crucible_quarantine-out="))
pathlib.Path(out).mkdir(parents=True, exist_ok=True)
(pathlib.Path(out) / "a.cpp.0000000000000000.quarantine").write_text("# unit a.cpp\\n", encoding="utf-8")
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
        "a parse drops -c, the output and the dependency file, and adds the report argument last",
        parsed
        == [
            "g++",
            "-DX",
            "-march=native",
            "a.cpp",
            "-fsyntax-only",
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


def unit_cases(expect: testing.Expect) -> None:
    """Hold the parse of one unit: its report directory, its stamp, and a unit that does not parse."""
    with tempfile.TemporaryDirectory(prefix="syntax-unit-") as scratch_text:
        scratch = Path(scratch_text)
        compiler = planted_compiler(scratch, "g++", FAKE_COMPILER)
        build = scratch / "build"
        good = syntax.Unit(build / "t.dir" / "a.cpp.o", "s1", (compiler, "-c", "a.cpp", "-o", "a.o"), str(scratch))
        parsed = syntax.parse_unit(good, build, frozenset())
        expect("a unit that parses exits 0", parsed.exit == 0)
        expect(
            "a unit that parses gives its report and its stamp to the reader",
            sections.read_report(sections.report_directory(build, good.object_path)) == sections.Section("s1", ()),
        )
        bad = syntax.Unit(build / "t.dir" / "bad.cpp.o", "s1", (compiler, "-c", "bad.cpp"), str(scratch))
        failed = syntax.parse_unit(bad, build, frozenset())
        expect(
            "a unit that does not parse gives its exit, and the run writes no stamp for it",
            failed.exit == 3 and not (sections.report_directory(build, bad.object_path) / sections.STAMP_FILE).exists(),
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (word_cases, native_cases, build_cases, unit_cases))
