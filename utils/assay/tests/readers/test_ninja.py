"""The test of the reader of the build graph, the build log and the dependency log (assay.readers.ninja).

`guard selftest tests.readers.test_ninja` runs the test.  The planted cases
use no program, or a planted ninja that writes a planted output.  The last
case builds a scratch graph with the ninja that the variable
ASSAY_TEST_NINJA names, and the registration of the test sets it.  With no
variable, the case uses the ninja of PATH.  Then it reads the dependency log
that Ninja writes.  Two edges of the graph stop the build.  One edge writes
its output and then kills its Ninja, as a stopped build does.  One edge writes
its output and then ends with an error.  Each case writes one line.  Exit 0 when each case holds,
else 1.
"""

import os
import shutil
import sys
import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import proc, testing
from assay.core import record as cost_record
from assay.loop import step as launcher
from assay.readers import cmake, ninja

GRAPH_TEXT = (
    "# a comment\n"
    "rule CXX_COMPILER__a_unscanned_Debug\n"
    "  command = g++ $in\n"
    "build a.o: CXX_COMPILER__a_unscanned_Debug a.cpp || order_a\n"
    "  DEP_FILE = a.o.d\n"
    "build order_a: phony || gen.h\n"
    "build gen.h | ${cmake_ninja_workdir}gen.h: CUSTOM_COMMAND tool\n"
    "  restat = 1\n"
    "\n"
    "  depfile = after_blank.d\n"
    "build tool: CXX_EXECUTABLE_LINKER__tool_Debug tool.o $\n"
    "    liba.a\n"
    "build liba.a: CXX_STATIC_LIBRARY_LINKER__a_Debug a.o\n"
    "build all: phony tool\n"
    "default all\n"
)

# The scratch graph of the last case: an edge that records its list, and an edge with no `deps` variable.  The
# command of the rule `stops` writes its output and then kills the Ninja that started it, as a stopped build does.
# The command of the rule `fails` writes its output and then ends with an error.
SCRATCH_GRAPH = (
    "rule listed\n"
    "  command = printf '%s: %s %s\\n' $out $in $header > $out.d && cp $in $out\n"
    "  depfile = $out.d\n"
    "  deps = gcc\n"
    "rule plain\n"
    "  command = cp $in $out\n"
    "rule stops\n"
    "  command = printf '%s: %s\\n' $out $in > $out.d && cp $in $out && kill -KILL $$PPID\n"
    "  depfile = $out.d\n"
    "  deps = gcc\n"
    "rule fails\n"
    "  command = printf '%s: %s\\n' $out $in > $out.d && cp $in $out && exit 1\n"
    "  depfile = $out.d\n"
    "  deps = gcc\n"
    "build valid.o: listed valid.c\n"
    "  header = valid.h\n"
    "build stale.o: listed stale.c\n"
    "  header = stale.h\n"
    "build gone.o: listed gone.c\n"
    "  header = gone.h\n"
    "build plain.o: plain plain.c\n"
    "build never.o: plain never.c\n"
    "build stopped.o: stops stopped.c\n"
    "build failed.o: fails failed.c\n"
    "build foreign.o: listed foreign.c\n"
    "  header = foreign.h\n"
    "build all: phony valid.o stale.o gone.o plain.o never.o stopped.o failed.o foreign.o\n"
    "default all\n"
)

# A planted ninja: it writes the file tool.txt of its build directory, and it exits with the status of status.txt.
PLANTED_NINJA = """\
import sys
from pathlib import Path
build = Path(sys.argv[sys.argv.index("-C") + 1])
sys.stdout.write((build / "tool.txt").read_text())
status = build / "status.txt"
sys.exit(int(status.read_text()) if status.is_file() else 0)
"""


def refused(read: Callable[[], object], error: type[ninja.NinjaError]) -> str:
    """Return the message of the error that a call raises, or an empty text when the call raises no such error."""
    try:
        read()
    except ninja.NinjaError as problem:
        return str(problem) if isinstance(problem, error) else ""
    return ""


def graph_cases(expect: testing.Expect) -> None:
    """Check the build statements, the variables and the default targets of a planted graph, and the edge kinds."""
    with tempfile.TemporaryDirectory(prefix="assay-ninja-") as scratch:
        build = Path(scratch)
        expect(
            "graph_file names build.ninja of the build directory",
            ninja.graph_file(build).parent == build and ninja.graph_file(build).name == ninja.GRAPH_NAME,
        )
        expect(
            "a build directory with no build.ninja is not a Ninja build",
            "is missing" in refused(lambda: ninja.graph(build), ninja.NotNinja),
        )
        ninja.plant_graph(build, GRAPH_TEXT)
        graph = ninja.graph(build)
        tool = graph.edges[graph.producer["tool"]]
        expect(
            "graph reads each build statement, a continued line and the default targets",
            len(graph.edges) == 6
            and graph.defaults == ("all",)
            and tool.inputs == ("tool.o", "liba.a")
            and tool.line == 11,
        )
        expect(
            "graph keeps the variable of an implicit output, with no expansion",
            graph.edges[graph.producer["gen.h"]].outputs == ("gen.h", "{cmake_ninja_workdir}gen.h"),
        )
        expect(
            "graph reads the variables of a build statement, ends them at an empty line, and reads no variable of a rule",
            graph.variables.get(graph.producer["a.o"]) == {"DEP_FILE": "a.o.d"}
            and graph.variables.get(graph.producer["gen.h"]) == {"restat": "1"}
            and not any("command" in values for values in graph.variables.values()),
        )
        ninja.plant_graph(
            build,
            "build out.o | $${dir}out.o: CXX_COMPILER__t_Debug src.cpp | dep.h || order phony |@ check\n"
            "build one$ two.o c$:d.o: phony a$ b\n",
        )
        first, second = ninja.graph(build).edges
        expect(
            "graph reads the implicit outputs, the implicit inputs, the order-only inputs and no validation",
            first.outputs == ("out.o", "${dir}out.o")
            and first.inputs == ("src.cpp", "dep.h")
            and first.order_only == ("order", "phony"),
        )
        expect(
            "graph removes the escapes of a space and a colon",
            second.outputs == ("one two.o", "c:d.o") and second.inputs == ("a b",),
        )
        (build / "rules").mkdir()
        (build / "rules" / "pools.ninja").write_text(
            "rule CXX_COMPILER__a_Debug\n  command = g++ $in\n\npool link\n  depth = 2\ninclude rules/more.ninja\n",
            encoding="utf-8",
        )
        (build / "rules" / "more.ninja").write_text("pool archive\n  depth = 1\n", encoding="utf-8")
        ninja.plant_graph(
            build,
            "pool compile\n  depth = 4\npool broken\n  depth = many\n\ninclude rules/pools.ninja\n"
            "build a.o: CXX_COMPILER__a_Debug a.cpp\n  pool = compile\n",
        )
        pooled = ninja.graph(build)
        expect(
            "graph reads each pool statement of build.ninja and of each included file, with its file, its line and "
            "its depth, and the variable pool of an edge",
            pooled.pools
            == {
                "compile": ninja.Pool(ninja.GRAPH_NAME, 1, 4),
                "broken": ninja.Pool(ninja.GRAPH_NAME, 3, None),
                "link": ninja.Pool("rules/pools.ninja", 4, 2),
                "archive": ninja.Pool("rules/more.ninja", 1, 1),
            }
            and pooled.variables.get(0) == {"pool": "compile"}
            and len(pooled.edges) == 1
            and not graph.pools,
        )
        (build / "rules" / "more.ninja").unlink()
        expect(
            "graph refuses an included file that is missing, and names it",
            "more.ninja is missing" in refused(lambda: ninja.graph(build), ninja.NinjaError)
            and not refused(lambda: ninja.graph(build), ninja.NotNinja),
        )
        ninja.plant_graph(build, "build a.o: phony\nbuild no colon here\n")
        expect(
            "graph refuses a build statement with no ':', with the file and the line",
            f"{ninja.graph_file(build)}:2:" in refused(lambda: ninja.graph(build), ninja.NinjaError),
        )
        ninja.plant_graph(build, "build a.o:\n")
        expect(
            "graph refuses a build statement with no rule",
            "no rule" in refused(lambda: ninja.graph(build), ninja.NinjaError),
        )
    expect(
        "edge_kind names each kind of rule",
        [
            ninja.edge_kind(rule)
            for rule in (
                "phony",
                "CXX_COMPILER__a_unscanned_Debug",
                "C_COMPILER__b_Debug",
                "CXX_STATIC_LIBRARY_LINKER__a_Debug",
                "CXX_EXECUTABLE_LINKER__t_Debug",
                "CXX_SHARED_LIBRARY_LINKER__s_Debug",
                "CUSTOM_COMMAND",
                "RERUN_CMAKE",
                "VERIFY_GLOBS",
                "CLEAN",
            )
        ]
        == ["phony", "compile", "compile", "archive", "link", "link", "custom", "cmake", "cmake", "other"],
    )


def log_cases(expect: testing.Expect) -> None:
    """Check the records of a planted build log, the version line, and the outputs that ran between two reads."""
    with tempfile.TemporaryDirectory(prefix="assay-ninja-") as scratch:
        build = Path(scratch)
        expect("log gives no record for a build with no log", ninja.log(build) == {})
        path = ninja.log_file(build)
        path.write_text("", encoding="utf-8")
        expect("log gives no record for an empty log", ninja.log(build) == {})
        path.write_text(
            "# ninja log v7\n"
            "10\t20\t100\tCMakeFiles/cmake.verify_globs\taa\n"
            "30\t900\t101\ta.o\tbb\n"
            "a damaged line\n"
            "x\t9\t1\tbad.o\tcc\n"
            "40\t950\t102\ttool\tdd\n",
            encoding="utf-8",
        )
        first = ninja.log(build)
        expect(
            "log keys each record by its absolute path, and skips a line with fewer fields or a time that is no number",
            sorted(first) == [str(build / "CMakeFiles/cmake.verify_globs"), str(build / "a.o"), str(build / "tool")]
            and first[str(build / "a.o")] == ninja.LogRecord(30, 900, 101, "bb"),
        )
        with path.open("a", encoding="utf-8") as handle:
            handle.write(
                f"12\t21\t100\tCMakeFiles/cmake.verify_globs\taa\n31\t500\t103\ta.o\tbb\n35\t40\t104\t{build}/gen.h\tee\n"
            )
        second = ninja.log(build)
        expect(
            "log keeps the last record of each output, under one key for a relative and an absolute path",
            second[str(build / "a.o")] == ninja.LogRecord(31, 500, 103, "bb") and str(build / "gen.h") in second,
        )
        expect(
            "ran_outputs gives each new and each changed output of a second run, sorted",
            ninja.ran_outputs(first, second)
            == tuple(sorted([str(build / "CMakeFiles/cmake.verify_globs"), str(build / "a.o"), str(build / "gen.h")])),
        )
        lines = path.read_text(encoding="utf-8").splitlines()
        kept = {line.split("\t")[3]: line for line in lines[1:] if line.count("\t") == 4 and line[0].isdigit()}
        path.write_text("\n".join([lines[0], *sorted(kept.values(), reverse=True)]) + "\n", encoding="utf-8")
        expect(
            "a rewrite of the log with the last record of each output, in a different sequence, adds no output",
            ninja.ran_outputs(second, ninja.log(build)) == (),
        )
        for version in ("5", "6"):
            path.write_text(f"# ninja log v{version}\n1\t2\t3\tz.o\th\n", encoding="utf-8")
            expect(f"log reads the version {version}", str(build / "z.o") in ninja.log(build))
        path.write_text("# ninja log v4\n1\t2\t3\tz.o\th\n", encoding="utf-8")
        expect(
            "log refuses a version that it does not read, with the file",
            str(path) in refused(lambda: ninja.log(build), ninja.NinjaError),
        )
        path.write_text("1\t2\t3\tz.o\th\n", encoding="utf-8")
        expect(
            "log refuses a file with no version line",
            "not a build log" in refused(lambda: ninja.log(build), ninja.NinjaError),
        )


def planted_tool(build: Path, output: str, status: int = 0) -> None:
    """Make the planted ninja the ninja of a build directory, and give it its output and its exit status."""
    program = build / "planted-ninja.py"
    program.write_text(f"#!{sys.executable}\n{PLANTED_NINJA}", encoding="utf-8")
    program.chmod(0o755)
    cmake.plant(build, cache={"CMAKE_GENERATOR": "Ninja", "CMAKE_MAKE_PROGRAM": ("FILEPATH", str(program))})
    (build / "tool.txt").write_text(output, encoding="utf-8")
    (build / "status.txt").write_text(f"{status}\n", encoding="utf-8")


def tool_cases(expect: testing.Expect) -> None:
    """Check the reads of the output of `ninja -t deps` and `ninja -t inputs` with a planted ninja."""
    with tempfile.TemporaryDirectory(prefix="assay-ninja-") as scratch:
        build = Path(scratch)
        expect(
            "deps refuses a build directory with no reply of the cache, and names the cache",
            "cache" in refused(lambda: ninja.deps(build), ninja.NinjaError)
            and not refused(lambda: ninja.deps(build), ninja.NotNinja),
        )
        cmake.plant(build, cache={"CMAKE_GENERATOR": "Unix Makefiles"})
        expect(
            "deps refuses a build of a generator other than Ninja",
            "Unix Makefiles" in refused(lambda: ninja.deps(build), ninja.NotNinja),
        )
        cmake.plant(build, cache={"CMAKE_GENERATOR": "Ninja"})
        expect(
            "deps refuses a cache that names no ninja",
            "CMAKE_MAKE_PROGRAM" in refused(lambda: ninja.deps(build), ninja.NinjaError),
        )
        planted_tool(
            build,
            "dir: x/a.o: #deps 2, deps mtime 5 (VALID)\n    x/a.c\n    x/a.h\n\nb.o: deps not found\n",
        )
        (build / "b.o").write_bytes(b"")
        found = ninja.deps(build, ["dir: x/a.o", "./b.o"])
        expect(
            "deps reads an output that holds ': ', and keys each list by the name of the call",
            found["dir: x/a.o"] == ninja.Deps(ninja.DepsState.VALID, ("x/a.c", "x/a.h"))
            and found["./b.o"].state == ninja.DepsState.MISSING,
        )
        expect(
            "deps tells that this Ninja did not build an output with no record in the build log",
            "holds no record of the output" in found["./b.o"].why,
        )
        expect(
            "deps refuses a count of lists that is not the count of the outputs",
            "lists for 3 outputs" in refused(lambda: ninja.deps(build, ["a.o", "b.o", "c.o"]), ninja.NinjaError),
        )
        found = ninja.deps(build, ["b.o", "dir: x/a.o"])
        expect(
            "deps keys each list by its output, also when the tool gives the lists in a different sequence",
            found["b.o"].state == ninja.DepsState.MISSING and found["dir: x/a.o"].state == ninja.DepsState.VALID,
        )
        expect(
            "deps refuses a call with an output that gets no list from the tool",
            "no list for the output c.o" in refused(lambda: ninja.deps(build, ["c.o", "b.o"]), ninja.NinjaError),
        )
        planted_tool(build, "a.o: #deps 1, deps mtime 5 (FRESH)\n    a.c\n")
        expect(
            "deps refuses a first line that does not have a shape of the tool",
            "does not have a shape" in refused(lambda: ninja.deps(build), ninja.NinjaError),
        )
        planted_tool(build, "    a.c\n")
        expect(
            "deps refuses a file before the first output",
            "before the first output" in refused(lambda: ninja.deps(build), ninja.NinjaError),
        )
        planted_tool(build, "a.o: deps not found\n", status=1)
        expect(
            "deps checks the exit status of the tool",
            "exits with 1" in refused(lambda: ninja.deps(build, ["a.o"]), ninja.NinjaError),
        )
        planted_tool(build, "a.c\nb.c\n\n")
        expect("inputs gives one path for each line of the tool", ninja.inputs(build, "all") == ("a.c", "b.c"))


def scratch_ninja() -> str:
    """Return the ninja of the last case (the module text), or an empty text when no ninja is given."""
    return os.environ.get("ASSAY_TEST_NINJA") or shutil.which("ninja") or ""


def stopped_steps(expect: testing.Expect, program: str, build: Path) -> None:
    """Run the edge that stops its Ninja and the edge that ends with an error, and write the output of foreign.o.

    The record of each step of the build launcher (assay.loop.step) then goes
    adjacent to its output, as the launcher writes it.
    """
    stopped = proc.run(proc.Command([program, "-C", str(build), "stopped.o"], timeout_s=proc.TOOL_S))
    failed = proc.run(proc.Command([program, "-C", str(build), "failed.o"], timeout_s=proc.TOOL_S))
    expect(
        "the edge of stopped.o stops its Ninja and the edge of failed.o fails, after each one writes its output",
        stopped.exit != 0 and failed.exit != 0 and (build / "stopped.o").is_file() and (build / "failed.o").is_file(),
    )
    (build / "foreign.o").write_bytes(b"")
    for name, result, exit_code in (("stopped", "built", 0), ("failed", "failed", 1)):
        run = cost_meter.Measurement(exit_code, 0.0, 0.0, 0.0, 0, 0.0, 0.0, None)
        output = str(build / f"{name}.o")
        step = launcher.step_record(cost_record.COMPILE, output, str(build / f"{name}.c"), run, result)
        cost_record.write(cost_record.record_file(output), step)


def built_cases(expect: testing.Expect) -> None:
    """Build a scratch graph with Ninja, and read each state of the dependency log that Ninja writes."""
    program = scratch_ninja()
    expect("the ninja of the built case is given", bool(program))
    if not program:
        return
    with tempfile.TemporaryDirectory(prefix="assay-ninja-") as scratch:
        build = Path(scratch)
        ninja.plant_graph(build, SCRATCH_GRAPH)
        for name in ("valid", "stale", "gone", "plain", "never", "stopped", "failed", "foreign"):
            (build / f"{name}.c").write_text(f"int {name};\n", encoding="utf-8")
        cmake.plant(build, cache={"CMAKE_GENERATOR": "Ninja", "CMAKE_MAKE_PROGRAM": ("FILEPATH", program)})
        done = proc.run(
            proc.Command([program, "-C", str(build), "valid.o", "stale.o", "gone.o", "plain.o"], timeout_s=proc.TOOL_S)
        )
        expect("the scratch graph builds" + (f": {done.out.strip()[-300:]}" if done.exit else ""), done.exit == 0)
        if done.exit != 0:
            return
        later = (build / "stale.o").stat().st_mtime + 100
        os.utime(build / "stale.o", (later, later))
        (build / "gone.o").unlink()
        stopped_steps(expect, program, build)
        outputs = ["valid.o", "stale.o", "gone.o", "plain.o", "never.o", "stopped.o", "failed.o", "foreign.o"]
        found = ninja.deps(build, outputs)
        expect(
            "a list that Ninja recorded is VALID, with the source and the header of the depfile",
            found["valid.o"] == ninja.Deps(ninja.DepsState.VALID, ("valid.c", "valid.h")),
        )
        expect(
            "a list of an output that a different step wrote again is STALE, and the reason says that the output is newer",
            found["stale.o"].state == ninja.DepsState.STALE and "newer" in found["stale.o"].why,
        )
        expect(
            "a list of a removed output is STALE, and the reason says that the output is missing",
            found["gone.o"].state == ninja.DepsState.STALE and "missing" in found["gone.o"].why,
        )
        expect(
            "an edge with no `deps` variable that ran has no list, and the reason names the record of the build log",
            found["plain.o"].state == ninja.DepsState.MISSING and "ran the edge" in found["plain.o"].why,
        )
        expect(
            "an output that Ninja did not build has no list, and the reason says that the build log has no record",
            found["never.o"].state == ninja.DepsState.MISSING and "holds no record" in found["never.o"].why,
        )
        expect(
            "an output of a step that ended before Ninja stopped has no list, and the reason says that the build stopped",
            found["stopped.o"].state == ninja.DepsState.MISSING
            and "the build stopped after the step" in found["stopped.o"].why
            and "runs the step again" in found["stopped.o"].why,
        )
        expect(
            "an output of a failed step has no list, and the reason gives the result and the exit of its record",
            found["failed.o"].state == ninja.DepsState.MISSING
            and "did not succeed" in found["failed.o"].why
            and "the result failed and the exit 1" in found["failed.o"].why,
        )
        expect(
            "an output with no record of a step has no list, and the reason says that a different build wrote it",
            found["foreign.o"].state == ninja.DepsState.MISSING
            and "A different build wrote the output" in found["foreign.o"].why,
        )
        expect(
            "deps with no output gives each list that an edge of the graph can use",
            set(ninja.deps(build)) == {"valid.o", "stale.o", "gone.o"},
        )
        expect(
            "deps refuses an output that the graph does not hold",
            "unknown target" in refused(lambda: ninja.deps(build, ["nope.o"]), ninja.NinjaError),
        )
        expect(
            "inputs gives the inputs of a target and of its inputs",
            {"valid.o", "valid.c", "never.c"} <= set(ninja.inputs(build, "all")),
        )
        expect(
            "the build log of Ninja reads, with a record of each output that ran",
            {str(build / name) for name in ("valid.o", "stale.o", "gone.o", "plain.o")} <= set(ninja.log(build)),
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (graph_cases, log_cases, tool_cases, built_cases))
