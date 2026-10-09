"""The test of the runner of the engine elf (assay.guard.engines.elf.runner).

THE CASES
    The cases hold these parts of the runner:
      * The facts of a planted build and of a planted build directory
      * The file plant.txt of a plant
      * The build of a plant with the patched compiler
      * The run, the witness and the write of a rule.
    The compile cases use the compiler that ASSAY_TEST_CXX names.  The
    registration of the test in test/CMakeLists.txt gives the compiler of the
    build.
"""

import contextlib
import os
import platform
import sys
from collections.abc import Callable, Iterator, Mapping
from functools import partial
from pathlib import Path
from tempfile import TemporaryDirectory

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import testing
from assay.core.paths import REPO_ROOT
from assay.guard.engines.elf import runner
from assay.readers import cmake, compdb, elf

REGISTRY_ROWS = (
    "rule artifact-symbols engine=elf scope=builddir ledger=count doc=XII | a planted symbol rule | lower it",
    "rule codegen-shapes engine=elf scope=builddir ledger=none doc=XXII.R12 | a planted shape rule | keep it",
)
# A fake ninja: `ninja -C DIR -t inputs TARGET` prints the file inputs.txt of the build directory.
FAKE_NINJA = """\
import sys
from pathlib import Path
build = Path(sys.argv[sys.argv.index("-C") + 1])
sys.stdout.write((build / "inputs.txt").read_text())
"""
KIND = "planted-kind"
UNIT = 'extern "C" int planted_sink(int);\nint planted_entry(int value) { return planted_sink(value) + 1; }\n'
BROKEN_UNIT = "int planted_entry( {\n"
EMPTY_LEDGER = "# a planted ledger\nformat 1\nrule artifact-symbols\n"


def raised(action: Callable[[], object], error: type[Exception]) -> str | None:
    """Return the message of the exception of the type `error` that an action raises, or None when it raises none."""
    try:
        action()
    except error as problem:
        return str(problem)
    return None


@contextlib.contextmanager
def without_compiler() -> Iterator[None]:
    """Remove the two variables that name the patched compiler for the body, and then put them back."""
    names = (testing.TEST_COMPILER, testing.COMPILER_PREFIX)
    saved = {name: os.environ.pop(name, None) for name in names}
    try:
        yield
    finally:
        for name, value in saved.items():
            if value is not None:
                os.environ[name] = value


def quiet(action: Callable[[], int]) -> tuple[int, str]:
    """Run an action with no GitHub annotation and no output on the terminal, and return its status and its text."""
    with check_report.github_actions(False):
        return testing.captured(action)


# ── The facts of a build ───────────────────────────────────────────


def planted_cases(expect: testing.Expect) -> None:
    """Hold the facts of a planted build, and a context with no build and no plant."""
    entry = compdb.CompileEntry("/tree/a.cpp", "/build", "/build/a.o", "g++ -c /tree/a.cpp")
    archive = Path("/build/libfoundation.a")
    planted = runner.PlantedBuild(
        "k", "t", "cxx", {"foundation": archive}, (entry,), {"foundation": frozenset({"/build/a.o"})}, {"NAME": "V"}
    )
    context = runner.ElfContext(Path("/tree"), None, planted)
    expect(
        "a planted build gives each fact with no build directory",
        (context.kind(), context.tier(), context.compiler(), context.libraries(), context.entries())
        == ("k", "t", "cxx", {"foundation": archive}, (entry,))
        and context.objects_of("foundation") == frozenset({"/build/a.o"})
        and (context.cache_value("NAME"), context.cache_value("OTHER")) == ("V", ""),
    )
    expect("a planted build shows the word planted as its build directory", context.build_shown() == runner.PLANTED)
    expect(
        "the prefix of a compiler is the directory above usr/bin, or above bin",
        runner.toolchain_prefix("/opt/gcc/usr/bin/g++") == Path("/opt/gcc")
        and runner.toolchain_prefix("/opt/gcc/bin/g++") == Path("/opt/gcc"),
    )
    expect(
        "a planted build gives no objects of a target that it does not hold",
        raised(partial(context.objects_of, "crucible"), runner.NotApplicable) is not None,
    )
    said = raised(runner.ElfContext(Path("/tree")).kind, runner.NotApplicable) or ""
    expect(
        "a context with no build directory and no plant does not apply, and names --build-dir", "--build-dir" in said
    )


def directory_cases(expect: testing.Expect) -> None:
    """Hold the facts of a planted build directory: the kind, the libraries, the objects, the cache and the problems."""
    with TemporaryDirectory(prefix="elf-runner-") as scratch_text:
        root = Path(scratch_text) / "tree"
        build = root / "build"
        build.mkdir(parents=True)
        ninja = root / "ninja.py"
        ninja.write_text(f"#!{sys.executable}\n{FAKE_NINJA}", encoding="utf-8")
        ninja.chmod(0o755)
        (build / "build-kind.txt").write_text(f"{KIND}\n", encoding="utf-8")
        (build / "inputs.txt").write_text(
            "CMakeFiles/f.dir/a.o\nCMakeFiles/f.dir/a.cpp\nlibx.a\ntool\n", encoding="utf-8"
        )
        cache: dict[str, str | tuple[str, str]] = {
            "CMAKE_GENERATOR": "Ninja",
            "CMAKE_MAKE_PROGRAM": ("FILEPATH", str(ninja)),
            "CMAKE_CXX_COMPILER": ("FILEPATH", "/opt/cxx"),
            "PLANTED": ("STRING", "value"),
        }
        library = runner.STATIC_LIBRARY
        targets: list[Mapping[str, object]] = [
            {
                "name": "foundation",
                "type": library,
                "artifacts": [{"path": "libfoundation.a"}],
                "paths": {"source": "."},
            },
            {
                "name": "test_lib",
                "type": library,
                "artifacts": [{"path": "libtest_lib.a"}],
                "paths": {"source": "test"},
            },
            {
                "name": "tool",
                "type": "EXECUTABLE",
                "artifacts": [{"path": "tool"}],
                "paths": {"source": "."},
                "link": {
                    "commandFragments": [
                        {"fragment": "-O1", "role": "flags"},
                        {"fragment": "libfoundation.a", "role": "libraries"},
                        {"fragment": "-Wl,-rpath,/opt/run", "role": "libraries"},
                        {"fragment": "/opt/host/libhost.so", "role": "libraries"},
                        {"fragment": "-lstdc++exp -pthread", "role": "libraries"},
                        {"fragment": "../outside/libother.a", "role": "libraries"},
                        {"fragment": "/opt/host/libhost.so", "role": "libraries"},
                    ]
                },
            },
            {
                "name": "plugin",
                "type": "MODULE_LIBRARY",
                "artifacts": [{"path": "plugin.so"}],
                "paths": {"source": "."},
                "link": {"commandFragments": [{"fragment": "-lplugin_only", "role": "libraries"}]},
            },
        ]
        cmake.plant(build, cache=cache, targets=targets, source=str(root), build=str(build))
        context = runner.ElfContext(root, build)
        expect("the build kind is the text of build-kind.txt", context.kind() == KIND)
        expect(
            "the static libraries are the STATIC_LIBRARY targets with the source root as their source directory",
            context.libraries() == {"foundation": build / "libfoundation.a"},
        )
        expect(
            "the linked artifacts are the linked targets of the inputs of all, with the first directory of the source",
            context.linked_artifacts() == (("", build / "tool"),),
        )
        expect(
            "the link libraries are the words -l and the paths outside the build directory, once each, with no flag",
            context.link_libraries()
            == (
                "/opt/host/libhost.so",
                "-lstdc++exp",
                str(build.resolve().parent / "outside/libother.a"),
                "-lplugin_only",
            ),
        )
        expect(
            "the objects of a target are the objects that ninja -t inputs gives, as absolute paths",
            context.objects_of("foundation") == frozenset({str((build / "CMakeFiles/f.dir/a.o").resolve())}),
        )
        expect(
            "a variable of the CMake cache gives its value, and a variable that the cache does not hold an empty text",
            (context.cache_value("PLANTED"), context.cache_value("MISSING")) == ("value", ""),
        )
        expect("the compiler is CMAKE_CXX_COMPILER", context.compiler() == "/opt/cxx")
        expect("a build shows its build directory", context.build_shown() not in ("", runner.PLANTED))
        expect("a build with no compile database is refused", raised(context.entries, runner.BuildError) is not None)
        compdb.plant(build, [{"directory": str(build), "file": str(root / "a.cpp"), "output": "a.o", "command": "g++"}])
        expect("the compile database gives each compile", len(runner.ElfContext(root, build).entries()) == 1)
        (build / "build-kind.txt").unlink()
        said = raised(runner.ElfContext(root, build).kind, runner.NotApplicable) or ""
        expect("a build with no build-kind.txt does not apply, and the line names the file", "build-kind.txt" in said)
        cmake.plant(build, cache={**cache, "CMAKE_GENERATOR": "Unix Makefiles"}, targets=targets, source=str(root))
        expect(
            "the objects of a build that Ninja does not generate do not apply",
            raised(partial(runner.ElfContext(root, build).objects_of, "foundation"), runner.NotApplicable) is not None,
        )
        cmake.plant(build, cache={"PLANTED": "value"}, source=str(root))
        said = raised(runner.ElfContext(root, build).compiler, runner.BuildError) or ""
        expect("a cache that names no compiler is refused, and the message names the variable", "CMAKE_CXX" in said)
        empty = root / "empty"
        empty.mkdir()
        expect(
            "a build with no reply of the file API is refused",
            raised(runner.ElfContext(root, empty).libraries, runner.BuildError) is not None,
        )


# ── The plants ─────────────────────────────────────────────────────


def manifest_cases(expect: testing.Expect) -> None:
    """Hold the rows of plant.txt: a missing file, a row of an incorrect shape, and the facts of the rows."""
    with TemporaryDirectory(prefix="elf-manifest-") as scratch_text:
        tree = Path(scratch_text) / "tree"
        tree.mkdir()
        said = raised(partial(runner.manifest_rows, tree), runner.BuildError) or ""
        expect("a plant with no plant.txt is refused", f"no readable {runner.MANIFEST}" in said)
        build = Path(scratch_text) / "build"
        shapes = (
            ("kind a b", "kind KIND"),
            ("tier", "tier TIER"),
            ("cache NAME", "cache NAME VALUE"),
            ("library one", "library NAME UNIT..."),
            ("archive one", "archive NAME UNIT..."),
            ("shared one", "shared NAME UNIT..."),
            ("program examples tool", "program DIRECTORY NAME UNIT..."),
            ("unit target u.cpp", "unit TARGET UNIT | WORD..."),
            ("kind a | b", "kind KIND"),
        )
        for row, shape in shapes:
            (tree / runner.MANIFEST).write_text(f"{row}\n", encoding="utf-8")
            said = raised(partial(runner.planted_build, tree, build, "planted-cxx"), runner.BuildError) or ""
            expect(f"plant.txt refuses the row {row!r}", f"is '{shape}'" in said)
        (tree / runner.MANIFEST).write_text(
            "kind k\ntier t\ncache NAME VALUE\nunit foundation src/a.cpp | -O2 -I{root}/include\nprobe x | a row\n",
            encoding="utf-8",
        )
        built = runner.planted_build(tree, build, "planted-cxx")
        entry = built.entries[0] if built.entries else compdb.CompileEntry("", "", "", "")
        expect(
            "a planted build gives the kind, the tier and the cache of plant.txt, and no library",
            (built.kind, built.tier, dict(built.cache), dict(built.libraries)) == ("k", "t", {"NAME": "VALUE"}, {}),
        )
        expect(
            "a unit row gives a row of the compile database, with the root of the copy for the word {root}",
            entry.file == str(tree / "src/a.cpp")
            and f"-I{tree}/include" in entry.command
            and entry.command.startswith("planted-cxx "),
        )
        expect(
            "a unit row gives an object of its target", dict(built.targets) == {"foundation": frozenset({entry.output})}
        )
        expect(
            "a row with a different head word stays for the module of the rule",
            [row.words[0] for row in built.rows if row.words[0] not in ("kind", "tier", "cache", "unit")] == ["probe"],
        )
        (tree / runner.MANIFEST).write_text("# a plant with no row\n", encoding="utf-8")
        bare = runner.planted_build(tree, build, "planted-cxx")
        expect(
            "a plant with no kind row and no tier row has the kind and the tier planted",
            (bare.kind, bare.tier) == (runner.PLANTED, runner.PLANTED),
        )


def compile_cases(expect: testing.Expect) -> None:
    """Build the libraries of a plant with the patched compiler, and refuse a unit that does not compile."""
    compiler = runner.plant_compiler()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the patched compiler", compiler is not None)
    if compiler is None:
        return
    with TemporaryDirectory(prefix="elf-compile-") as scratch_text:
        scratch = Path(scratch_text)
        tree = scratch / "tree"
        tree.mkdir()
        (tree / "a.cpp").write_text(UNIT, encoding="utf-8")
        (tree / "b.cpp").write_text(UNIT.replace("planted_entry", "second_entry"), encoding="utf-8")
        (tree / runner.MANIFEST).write_text(
            "library foundation a.cpp b.cpp\nlibrary crucible a.cpp\n", encoding="utf-8"
        )
        built = runner.planted_build(tree, scratch / "build", compiler)
        archive = built.libraries.get("foundation")
        members = sorted(member.name for member in elf.members(archive)) if archive is not None else []
        expect(
            "a library row compiles each unit, and ar puts the objects into the archive libNAME.a",
            sorted(built.libraries) == ["crucible", "foundation"] and members == ["a.cpp.o", "b.cpp.o"],
        )
        (tree / runner.MANIFEST).write_text(
            "library foundation a.cpp\narchive extra b.cpp\nshared host a.cpp\n", encoding="utf-8"
        )
        linked = runner.planted_build(tree, scratch / "linked", compiler)
        names = [Path(item).name for item in linked.link_libraries]
        host = Path(linked.link_libraries[1]) if len(linked.link_libraries) == 2 else None
        exported = set()
        if host is not None:
            with elf.mapped(host) as library:
                exported = {symbol.name for symbol in library.dynamic_symbols() if symbol.is_defined}
        expect(
            "an archive row and a shared row give libraries of a link line, and no library of the root",
            sorted(linked.libraries) == ["foundation"]
            and names == ["libextra.a", "libhost.so"]
            and "_Z13planted_entryi" in exported,
        )
        (tree / "main.cpp").write_text("int main() { return 0; }\n", encoding="utf-8")
        (tree / runner.MANIFEST).write_text("program examples tool main.cpp\n", encoding="utf-8")
        program = runner.planted_build(tree, scratch / "program", compiler)
        placed = [(directory, path.relative_to(scratch / "program")) for directory, path in program.linked]
        expect(
            "a program row links the program in its directory, with its map adjacent to it",
            placed == [("examples", Path("examples/tool"))]
            and (scratch / "program" / "examples" / "tool.map").is_file(),
        )
        (tree / "c.cpp").write_text(BROKEN_UNIT, encoding="utf-8")
        (tree / runner.MANIFEST).write_text("library foundation c.cpp\n", encoding="utf-8")
        said = raised(partial(runner.planted_build, tree, scratch / "second", compiler), runner.BuildError) or ""
        expect(
            "a unit that does not compile is refused, with its command", "does not build" in said and "c.cpp" in said
        )
        plant = scratch / "plant-broken"
        plant.mkdir()
        (plant / "c.cpp.planted").write_text(BROKEN_UNIT, encoding="utf-8")
        (plant / runner.MANIFEST).write_text("library foundation c.cpp\n", encoding="utf-8")
        said = raised(partial(runner.plant_context, plant, scratch / "copy" / "tree"), runner.NotApplicable) or ""
        expect(
            "a plant that does not build does not apply, and the witness counts it as a failure",
            "does not build" in said,
        )


# ── The run, the witness and the write ─────────────────────────────


def run_cases(expect: testing.Expect) -> None:
    """Hold a build that the runner cannot read, a rule with no write, a plant with no facts and the witness."""
    rules = rule_registry.planted_rules(REGISTRY_ROWS)
    with TemporaryDirectory(prefix="elf-run-") as scratch_text:
        root = Path(scratch_text) / "tree"
        build = root / "build"
        build.mkdir(parents=True)
        (build / "build-kind.txt").write_text(f"{KIND}\n", encoding="utf-8")
        cmake.plant(build, cache={"CMAKE_CXX_COMPILER": ("FILEPATH", "/opt/cxx")}, source=str(root), build=str(build))
        ledger = rule_ledger.ledger_path("artifact-symbols", root)
        ledger.parent.mkdir(parents=True)
        ledger.write_text(EMPTY_LEDGER, encoding="utf-8")
        status, text = quiet(lambda: runner.run([rules["artifact-symbols"]], root, build, None)[0])
        errors = [item for item in map(check_report.parse_line, text.splitlines()) if item and item.level == "error"]
        expect(
            "a build that the rule cannot read gives one error at the build directory, and the status 1",
            status == 1
            and len(errors) == 1
            and errors[0].path == runner.ElfContext(root, build).build_shown()
            and "no static library foundation, crucible" in errors[0].message,
        )
        status, _ = quiet(lambda: runner.run([rules["artifact-symbols"]], root, None, None)[0])
        expect("a run with no build directory does not apply", status == runner.SKIPPED)
        status, text = quiet(lambda: runner.write(rules["codegen-shapes"], runner.WriteRequest(root, build)))
        expect(
            "a rule with no write of its own writes no ledger, and the status is 2", status == 2 and "no ledger" in text
        )
        probe = compdb.CompileEntry(str(root / "test/shape/core_shapes.cpp"), str(build), str(build / "p.o"), "g++ -c")
        planted = runner.PlantedBuild(
            entries=(probe,),
            targets={"foundation": frozenset()},
            rows=rows_grammar.parse("part core\nprobe bad relation=nothing bound=1 | a planted claim\n", "plant.txt"),
        )
        said = raised(
            partial(runner.judged_plant, rules["codegen-shapes"], runner.ElfContext(root, None, planted)),
            runner.NotApplicable,
        )
        expected = "the build of the plant cannot be read" if platform.machine() == "x86_64" else "x86_64"
        expect(
            "a plant whose rows the rule cannot read does not apply to the plant",
            said is not None and expected in said,
        )
    with without_compiler():
        status, text = quiet(lambda: runner.witness(rules["codegen-shapes"], REPO_ROOT))
    expect(
        "the witness does not apply when no variable names the patched compiler",
        status == runner.SKIPPED and testing.TEST_COMPILER in text,
    )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (planted_cases, directory_cases, manifest_cases, compile_cases, run_cases),
    )
