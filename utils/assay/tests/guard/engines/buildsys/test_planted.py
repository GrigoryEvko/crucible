"""The test of the planted build of a plant of a build rule (assay.guard.engines.buildsys.planted).

`guard selftest tests.guard.engines.buildsys.test_planted` runs the test.  Each
case writes a plant with a file build.txt, makes its planted build, and reads
the build back through the readers of assay.readers and assay.core.record.
Exit 0 when each case holds, else 1.
"""

import tempfile
from pathlib import Path

from assay.core import meter as cost_meter
from assay.core import record as cost_record
from assay.core import registry as rule_registry
from assay.core import testing
from assay.guard.engines.buildsys import planted
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import census as build_census
from assay.readers import cmake as cmake_reader
from assay.readers import compdb, ctest
from assay.readers import ninja as ninja_reader

BUILD_ROWS = """\
# A planted build with each kind of row.
format 1
kind x86_64-debug-asan
cache CRUCIBLE_PLANTED | yes
compile test/unit.cpp.o test/unit.cpp result=built cpu=3 instructions=7 functions=f:300,g:40
compile test/hit.cpp.o test/hit.cpp result=hit last-cpu=12 last-instructions=60 all=no
link bin/tool type=EXECUTABLE cpu=1.5 peak=0.25
target placed line=12 | test/unit.cpp
target unplaced backtrace=no | test/hit.cpp
deps test/unit.cpp.o | test/unit.cpp include/one.h
deps test/hit.cpp.o state=MISSING
fixture neg_planted test/neg/planted.cpp instructions=2 | test/neg/planted.cpp include/one.h
test exe_planted labels=ci_guard,meta | {build}/test/exe_planted
test-record exe_planted wall=4.5 cpu=4 peak=0.75
file include/one.h size=100
cache-file neg/aa/entry size=64
limit neg 4096
"""


def context_of(scratch: Path, rows: str) -> BuildContext:
    """Write a plant with a build.txt, and return the context of its planted build."""
    plant = scratch / "plant-planted"
    plant.mkdir(parents=True, exist_ok=True)
    (plant / planted.BUILD_FILE).write_text(rows, encoding="utf-8")
    return planted.plant_context(plant, scratch / "tree")


def is_refused(rows: str) -> bool:
    """Return True when plant_context() refuses a build.txt with NotApplicable."""
    with tempfile.TemporaryDirectory(prefix="test-planted-") as scratch:
        try:
            context_of(Path(scratch), rows)
        except meta_runner.NotApplicable:
            return True
    return False


def build_cases(expect: testing.Expect) -> None:
    """Make a planted build with each kind of row, and read each part back through its reader."""
    with tempfile.TemporaryDirectory(prefix="test-planted-") as scratch:
        context = context_of(Path(scratch), BUILD_ROWS)
        build = context.require_build()
        expect("the kind row gives the build kind", context.kind() == "x86_64-debug-asan")
        outputs = {Path(entry.output).name for entry in compdb.entries(build) if entry.output is not None}
        expect("each compile row gives a row of the compile database", outputs == {"unit.cpp.o", "hit.cpp.o"})
        expect(
            "the field all=no keeps an object out of the inputs of the target all",
            list(ninja_reader.inputs(build, "all")) == ["test/unit.cpp.o"],
        )
        unit = cost_record.read_of(build / "test" / "unit.cpp.o")
        expect(
            "a compile row writes the record of `loop step` with its CPU time and its count",
            unit.cost is not None and unit.cost.cpu_s == 3.0 and unit.cost.compiler_instructions == 7 * planted.GIGA,
        )
        hit = cost_record.read_of(build / "test" / "hit.cpp.o")
        expect(
            "a hit keeps the cost of the last compile that ran the compiler",
            hit.is_hit and hit.last_cost is not None and hit.last_cost.compiler_instructions == 60 * planted.GIGA,
        )
        link = cost_record.read_of(build / "bin" / "tool")
        expect(
            "a link row writes the output and its record of a link",
            link.step == cost_record.Step.LINK and link.cost is not None and link.cost.cpu_s == 1.5,
        )
        model = cmake_reader.codemodel(build)
        expect(
            "a link row gives a target of the code model with its artifact",
            any(target.name == "tool" and target.type_name == "EXECUTABLE" for target in model.targets),
        )
        targets = {target.name: target for target in model.targets}
        placed, unplaced = targets.get("placed"), targets.get("unplaced")
        expect(
            "a target row gives the position of its command, and backtrace=no gives a target with no position",
            placed is not None
            and placed.origin is not None
            and placed.origin.line == 12
            and unplaced is not None
            and unplaced.origin is None,
        )
        lists = ninja_reader.deps(build, ["test/unit.cpp.o", "test/hit.cpp.o"])
        expect(
            "a deps row gives a VALID list with its files, and state=MISSING gives no list",
            lists["test/unit.cpp.o"].state == ninja_reader.DepsState.VALID
            and len(lists["test/unit.cpp.o"].files) == 2
            and lists["test/hit.cpp.o"].state != ninja_reader.DepsState.VALID,
        )
        names = {test.name for test in ctest.tests(build)}
        expect("a fixture row and a test row give the tests of the build", names == {"neg_planted", "exe_planted"})
        expect(
            "the fixture row writes the record that the census reads",
            build_census.fixtures_of(build) == [("neg_planted", str(context.root / "test/neg/planted.cpp"))],
        )
        records = ctest.costs(build)
        expect(
            "a test-record row writes the record of `loop test-step`",
            records["exe_planted"].wall_s == 4.5 and records["exe_planted"].cpu_s == 4.0,
        )
        expect("a file row writes a file of the tree", (context.root / "include" / "one.h").stat().st_size == 100)
        expect(
            "a cache-file row writes a file in the root of the caches, and a limit row gives its limit",
            context.cache_root is not None
            and (context.cache_root / "neg" / "aa" / "entry").stat().st_size == 64
            and context.cache_limits == {"neg": 4096},
        )
        expect(
            "the CMake cache names the planted tools and the cache rows",
            cmake_reader.cache(build)["CRUCIBLE_PLANTED"].value == "yes",
        )


def configure_cases(expect: testing.Expect) -> None:
    """A configure row gives a configure of its seconds to the reader of the configure time."""
    from assay.loop import history as loop_history

    with tempfile.TemporaryDirectory(prefix="test-planted-") as scratch:
        context = context_of(Path(scratch), "format 1\nkind x86_64-debug-asan\nconfigure seconds=12.5\n")
        times = loop_history.configure_time(context.require_build())
        expect(
            "a configure row gives a configure of its seconds, with no generate step and no reply after it",
            times is not None and times["total_s"] == 12.5 and times["configure_s"] == 12.5,
        )
    expect("a configure row with no seconds is refused", is_refused("format 1\nconfigure\n"))
    expect("a configure row with a word that is no number is refused", is_refused("format 1\nconfigure seconds=x\n"))


def refused_cases(expect: testing.Expect) -> None:
    """Refuse a build.txt with no format row, an unknown row kind and a field that its kind does not take."""
    expect("a build.txt with no format row is refused", is_refused("kind k\n"))
    expect("an unknown row kind is refused", is_refused("format 1\nobject a.o\n"))
    expect("a field of a different kind is refused", is_refused("format 1\nlink bin/tool instructions=4\n"))
    expect("a value that the field does not take is refused", is_refused("format 1\ncompile a.o a.cpp result=late\n"))
    expect("a file row with a path out of the tree is refused", is_refused("format 1\nfile ../x size=1\n"))


def registry_cases(expect: testing.Expect) -> None:
    """A plant with no build.txt has no build directory, and a planted registry gives the thresholds."""
    with tempfile.TemporaryDirectory(prefix="test-planted-") as scratch:
        plant = Path(scratch) / "plant-registry"
        (plant / "utils").mkdir(parents=True)
        (plant / rule_registry.REGISTRY_PATH).write_text(
            rule_registry.planted([rule_registry.measured_rule("cache-size", 3, 9, "x")]), encoding="utf-8"
        )
        context = planted.plant_context(plant, Path(scratch) / "tree")
        expect("a plant with no build.txt gives no build directory", context.build_dir is None)
        budget = context.budget("cache-size")
        expect("a planted registry gives the thresholds of a rule", (budget.warn, budget.error) == (3.0, 9.0))
        expect(
            "the root of the caches is in the copy of the plant", context.cache_root == Path(scratch) / "tree" / "cache"
        )
        expect("the build kind file name is the one of the meter", cost_meter.KIND_FILE == "build-kind.txt")


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."), argv, (build_cases, configure_cases, refused_cases, registry_cases)
    )
