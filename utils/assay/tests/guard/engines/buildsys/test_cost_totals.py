"""The test of the configurations of the census totals (assay.guard.engines.buildsys.cost_totals, THE CONFIGURATION).

`guard selftest tests.guard.engines.buildsys.test_cost_totals` runs the test.
Each case plants a build with the rows of assay.guard.engines.buildsys.planted
and a ledger of a census rule in a throwaway tree.  The witness of each rule
holds the refusals of a ledger.  This test holds what a plant cannot hold:
  * a build with a configuration that the ledger does not hold
  * the configuration of a build that is not planted
  * the configuration rows of a write.
Exit 0 when each case holds, else 1.
"""

import sys
import tempfile
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import testing
from assay.guard.engines.buildsys import cost_totals, planted
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.guard.engines.plugin import runner as plugin_runner

KIND = "x86_64-debug-asan"
RULES = ("parse-total", "instruction-total", "header-fanout")
# A build of two objects and two fixtures, as the control control-unchanged of parse-total plants it.
BUILD_ROWS = f"""\
format 1
kind {KIND}
file include/Base.h size=1000
file src/a.cpp size=100
file src/b.cpp size=100
file neg/neg_one.cpp size=50
file neg/neg_two.cpp size=50
compile CMakeFiles/a.o src/a.cpp instructions=2
compile CMakeFiles/b.o src/b.cpp instructions=2
deps CMakeFiles/a.o | src/a.cpp include/Base.h
deps CMakeFiles/b.o | src/b.cpp include/Base.h
fixture neg_one neg/neg_one.cpp instructions=5 | neg/neg_one.cpp include/Base.h
fixture neg_two neg/neg_two.cpp instructions=5 | neg/neg_two.cpp include/Base.h
"""
# The rows of each ledger that hold the planted build: its configuration and its totals.
LEDGER_ROWS = {
    "parse-total": f"""\
configuration {KIND} kind {KIND}
- | {KIND} | 2100 | fixtures | the measure of the planted build
- | {KIND} | 2200 | objects | the measure of the planted build
""",
    "instruction-total": f"""\
configuration {KIND} kind {KIND}
- | {KIND} | 10000000000 | fixtures | the measure of the planted build
- | {KIND} | 4000000000 | objects | the measure of the planted build
""",
    "header-fanout": f"""\
configuration {KIND} kind {KIND}
include/Base.h | {KIND} | 1000/4 | - | -
""",
}
# The output of -Q --help=target of a planted compiler: x86-64 with no flag, and znver5 with -march=native.
COMPILER_HELP = "The following options are target specific:\n  -march=   \t\tx86-64\n  -mtune=   \t\tgeneric\n"
# The rows of a build that a planted compiler compiles, with one object of all that takes -march=native.
NATIVE_ROWS = f"""\
format 1
kind {KIND}
cache CMAKE_BUILD_TYPE | Debug
cache CMAKE_CXX_COMPILER type=FILEPATH | {{root}}/planted-cxx
file src/a.cpp size=100
file src/b.cpp size=100
file src/c.cpp size=100
compile CMakeFiles/a.o src/a.cpp | c++ -O1 -c src/a.cpp
compile CMakeFiles/b.o src/b.cpp | c++ -O3 -march=native -c src/b.cpp
compile CMakeFiles/c.o src/c.cpp all=no | c++ -O3 -march=native -c src/c.cpp
"""


def rule_of(rule_id: str) -> rule_registry.Rule:
    """Return the rule of the registry of this tree."""
    return rule_registry.load().rules[rule_id]


def plant(root: Path, rows: str, rule_id: str, ledger_rows: str) -> Path:
    """Write the planted build of the rows and the ledger of a rule in a tree, and return the build directory."""
    build = root / planted.BUILD
    (root / planted.BUILD_FILE).parent.mkdir(parents=True, exist_ok=True)
    (root / planted.BUILD_FILE).write_text(rows, encoding="utf-8")
    planted.write_build(planted.read_rows(root / planted.BUILD_FILE, root, build), root, build)
    ledger = rule_ledger.ledger_path(rule_id, root)
    ledger.parent.mkdir(parents=True, exist_ok=True)
    ledger.write_text(f"format 1\nrule {rule_id}\n{ledger_rows}", encoding="utf-8")
    return build


def plant_compiler(root: Path, exit_status: int = 0) -> None:
    """Write a planted compiler that prints COMPILER_HELP, with znver5 for -march=native, and exits with a status."""
    compiler = root / "planted-cxx"
    compiler.write_text(
        f"#!{sys.executable}\nimport sys\n"
        f"print({COMPILER_HELP!r}.replace('x86-64', 'znver5' if '-march=native' in sys.argv else 'x86-64'))\n"
        f"sys.exit({exit_status})\n",
        encoding="utf-8",
    )
    compiler.chmod(0o755)


def refusal_of(context: BuildContext, rule_id: str) -> str:
    """Return the reason of a rule that does not apply to the context, or an empty text when the rule applies."""
    try:
        testing.captured(lambda: context.findings_of(rule_of(rule_id)))
    except meta_runner.NotApplicable as reason:
        return str(reason)
    return ""


def not_held_cases(expect: testing.Expect) -> None:
    """A build of a configuration that the ledger does not hold gives no finding, and the line names the two sides."""
    bench = plugin_runner.BuildConfiguration(f"{KIND}-bench", ((plugin_runner.KIND_KEY, KIND),))
    other_kind = plugin_runner.BuildConfiguration(KIND, ((plugin_runner.KIND_KEY, "aarch64-debug-asan"),))
    for rule_id in RULES:
        with tempfile.TemporaryDirectory(prefix="test-cost-totals-") as scratch:
            root = Path(scratch)
            build = plant(root, BUILD_ROWS, rule_id, LEDGER_ROWS[rule_id])
            line = refusal_of(BuildContext(root, build, configuration=bench), rule_id)
            expect(
                f"{rule_id}: a bench build of the kind of the baseline does not apply, and the line names the "
                f"configuration of the build and the one of the ledger",
                f"{KIND}-bench on the tier ?" in line and f"It holds {KIND} on ?" in line,
            )
            line = refusal_of(BuildContext(root, build, configuration=other_kind), rule_id)
            expect(
                f"{rule_id}: a build of the name of the baseline with a different fact does not apply, and the line "
                f"names the fact that differs",
                "differs" in line and f"kind is aarch64-debug-asan in the build and {KIND} in the ledger" in line,
            )
            expect(
                f"{rule_id}: the build of the configuration of the ledger applies",
                refusal_of(BuildContext(root, build, configuration=planted_configuration()), rule_id) == "",
            )


def planted_configuration() -> plugin_runner.BuildConfiguration:
    """Return the configuration that a planted build of the kind KIND gives (planted.plant_context())."""
    return plugin_runner.BuildConfiguration(KIND, ((plugin_runner.KIND_KEY, KIND),))


def written_text(rule_id: str, root: Path, configuration: plugin_runner.BuildConfiguration) -> str:
    """Write the ledger of a rule from the planted build of a tree with a configuration, and return its text."""
    context = BuildContext(root, root / planted.BUILD, configuration=configuration)
    context.rule = rule_of(rule_id)
    member = cost_totals.RULES[rule_id]
    ledger = rule_ledger.read(rule_of(rule_id), root)
    written = member.written(context, ledger, None) if member.written is not None else ledger
    text = rule_ledger.render(written)
    rule_ledger.ledger_path(rule_id, root).write_text(text, encoding="utf-8")
    return text


def write_cases(expect: testing.Expect) -> None:
    """A write gives the configuration rows of the build, and a second configuration keeps the first one."""
    bench = plugin_runner.BuildConfiguration(
        f"{KIND}-bench", ((plugin_runner.KIND_KEY, KIND), ("CRUCIBLE_BENCH", "on"))
    )
    for rule_id in RULES:
        with tempfile.TemporaryDirectory(prefix="test-cost-totals-") as scratch:
            root = Path(scratch)
            plant(root, BUILD_ROWS, rule_id, "")
            first = written_text(rule_id, root, planted_configuration())
            expect(
                f"{rule_id}: a write into an empty ledger gives the configuration row of the build",
                f"configuration {KIND} kind {KIND}\n" in first,
            )
            second = written_text(rule_id, root, bench)
            expect(
                f"{rule_id}: a write of a second configuration keeps the first one before it, with its facts",
                second.index(f"configuration {KIND} kind {KIND}\n")
                < second.index(f"configuration {KIND}-bench kind {KIND}\n")
                and f"configuration {KIND}-bench CRUCIBLE_BENCH on\n" in second,
            )
            again = written_text(
                rule_id, root, plugin_runner.BuildConfiguration(KIND, ((plugin_runner.KIND_KEY, KIND), ("tier", "t")))
            )
            expect(
                f"{rule_id}: a write of a configuration that the ledger has gives its new facts at its position",
                again.index(f"configuration {KIND} tier t\n")
                < again.index(f"configuration {KIND}-bench kind {KIND}\n"),
            )


def real_cases(expect: testing.Expect) -> None:
    """A build that is not planted gives its configuration from its CMake cache, its compiler and its commands."""
    with tempfile.TemporaryDirectory(prefix="test-cost-totals-") as scratch:
        root = Path(scratch)
        plant_compiler(root)
        build = plant(root, NATIVE_ROWS, "parse-total", LEDGER_ROWS["parse-total"])
        configuration = BuildContext(root, build).configuration()
        tier = dict(configuration.facts).get(plugin_runner.TIER_KEY, "")
        expect(
            "the configuration of a Debug build has the name of its kind and the tier of its build flags",
            configuration.name == KIND and tier.startswith("x86-64-"),
        )
        expect(
            "a compile of all with -march=native adds the native tier to the tier, once",
            tier.count("+") == 1 and "+znver5-" in tier,
        )
        expect(
            "the cache entry of the build type is a fact of the configuration",
            dict(configuration.facts).get("CMAKE_BUILD_TYPE") == "Debug",
        )
    with tempfile.TemporaryDirectory(prefix="test-cost-totals-") as scratch:
        root = Path(scratch)
        plant_compiler(root, 1)
        build = plant(root, NATIVE_ROWS, "parse-total", LEDGER_ROWS["parse-total"])
        context = BuildContext(root, build)
        found, _ = testing.captured(lambda: context.findings_of(rule_of("parse-total")))
        expect(
            "a compiler that does not print its target gives one error of the kind configuration",
            [(item.level, item.kind) for item in found] == [("error", "configuration")],
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (not_held_cases, write_cases, real_cases))
