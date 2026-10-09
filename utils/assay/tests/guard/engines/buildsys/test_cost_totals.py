"""The test of the configurations of the census totals (assay.guard.engines.buildsys.cost_totals, THE CONFIGURATION).

`guard selftest tests.guard.engines.buildsys.test_cost_totals` runs the test.
Each case plants a build with the rows of assay.guard.engines.buildsys.planted
and a ledger of a census rule in a throwaway tree.  The witness of each rule
holds the refusals of a ledger.  This test holds what a plant cannot hold:
  * a build with a configuration that the ledger does not hold, which the
    largest baseline judges (THE POLICY of assay.core.configuration)
  * a build of a held name with a different fact
  * the configuration of a build that is not planted
  * the configuration rows of a write, and the write of a configuration that
    the ledger does not hold.
Exit 0 when each case holds, else 1.
"""

import sys
import tempfile
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import testing
from assay.guard.engines.buildsys import cost_totals, planted
from assay.guard.engines.buildsys.runner import BuildContext

KIND = "x86_64-debug-asan"
RULES = ("parse-total", "instruction-total", "header-fanout")
# The two census rules with totals, which give an error for a growth.
TOTAL_RULES = ("parse-total", "instruction-total")
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
# The same build with a header and compiles of two times the cost: a growth of each total.
GROWN_ROWS = BUILD_ROWS.replace("size=1000", "size=2000").replace("instructions=2", "instructions=4")
# The same build with a header and compiles of less cost: a fall of each total.
FALLEN_ROWS = BUILD_ROWS.replace("size=1000", "size=400").replace("instructions=5", "instructions=1")
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


def judged_of(
    root: Path, rows: str, rule_id: str, configuration: build_config.BuildConfiguration
) -> tuple[list[check_report.Finding], str]:
    """Plant a build of the rows and the ledger of a rule, and return the findings of the rule and its output."""
    build = plant(root, rows, rule_id, LEDGER_ROWS[rule_id])
    context = BuildContext(root, build, configuration=configuration)
    return testing.captured(lambda: context.findings_of(rule_of(rule_id)))


def kinds_of(found: list[check_report.Finding]) -> list[tuple[str, str]]:
    """Return the level and the kind of each finding."""
    return [(item.level, item.kind) for item in found]


def policy_cases(expect: testing.Expect) -> None:
    """Each build is judged: the rows of its name, or the largest baseline for a name that the ledger does not hold."""
    bench = build_config.BuildConfiguration(f"{KIND}-bench", ((build_config.KIND_KEY, KIND),))
    other_kind = build_config.BuildConfiguration(KIND, ((build_config.KIND_KEY, "aarch64-debug-asan"),))
    for rule_id in RULES:
        with tempfile.TemporaryDirectory(prefix="test-cost-totals-") as scratch:
            root = Path(scratch)
            found, text = judged_of(root, BUILD_ROWS, rule_id, bench)
            expect(
                f"{rule_id}: a build of a name that the ledger does not hold is judged against the largest baseline, "
                f"and the summary line names it",
                [item for item in found if item.level == "error"] == []
                and f"because the ledger holds no configuration {KIND}-bench" in text,
            )
            found, text = judged_of(root, BUILD_ROWS, rule_id, other_kind)
            expect(
                f"{rule_id}: a build of a held name with a different fact is judged against its rows, and a line "
                f"names the different fact",
                [item for item in found if item.level == "error"] == []
                and f"kind is aarch64-debug-asan in the build and {KIND} in the ledger" in text,
            )
            found, _ = judged_of(root, FALLEN_ROWS, rule_id, bench)
            expect(
                f"{rule_id}: a build of a name that the ledger does not hold gives no fall",
                [item for item in found if item.kind in ("fall", "no-sentinel")] == [],
            )
            found, _ = judged_of(root, FALLEN_ROWS, rule_id, planted_configuration())
            expect(
                f"{rule_id}: a build of a held configuration gives the fall of its rows",
                any(item.kind in ("fall", "no-sentinel") for item in found),
            )
    for rule_id in TOTAL_RULES:
        with tempfile.TemporaryDirectory(prefix="test-cost-totals-") as scratch:
            found, _ = judged_of(Path(scratch), GROWN_ROWS, rule_id, bench)
            expect(
                f"{rule_id}: a growth over the largest baseline is an error, also in a build of a name that the ledger "
                f"does not hold",
                ("error", "growth") in kinds_of(found),
            )


def planted_configuration() -> build_config.BuildConfiguration:
    """Return the configuration that a planted build of the kind KIND gives (planted.plant_context())."""
    return build_config.BuildConfiguration(KIND, ((build_config.KIND_KEY, KIND),))


def written_text(
    rule_id: str, root: Path, configuration: build_config.BuildConfiguration, add_configuration: bool = False
) -> str:
    """Write the ledger of a rule from the planted build of a tree with a configuration, and return its text.

    Raises:
        LedgerError: If the write refuses the configuration of the build
    """
    context = BuildContext(root, root / planted.BUILD, configuration=configuration)
    context.rule = rule_of(rule_id)
    context.add_configuration = add_configuration
    member = cost_totals.RULES[rule_id]
    ledger = rule_ledger.read(rule_of(rule_id), root)
    written = member.written(context, ledger, None) if member.written is not None else ledger
    text = rule_ledger.render(written)
    rule_ledger.ledger_path(rule_id, root).write_text(text, encoding="utf-8")
    return text


def refused_write(rule_id: str, root: Path, configuration: build_config.BuildConfiguration) -> str:
    """Return the refusal of a write of a configuration with no --add-configuration, or an empty text."""
    try:
        written_text(rule_id, root, configuration)
    except rule_ledger.LedgerError as problem:
        return str(problem)
    return ""


def write_cases(expect: testing.Expect) -> None:
    """A write gives the configuration rows of the build, and only --add-configuration adds a configuration."""
    bench = build_config.BuildConfiguration(f"{KIND}-bench", ((build_config.KIND_KEY, KIND), ("CRUCIBLE_BENCH", "on")))
    for rule_id in RULES:
        with tempfile.TemporaryDirectory(prefix="test-cost-totals-") as scratch:
            root = Path(scratch)
            plant(root, BUILD_ROWS, rule_id, "")
            first = written_text(rule_id, root, planted_configuration())
            expect(
                f"{rule_id}: a write into an empty ledger gives the configuration row of the build",
                f"configuration {KIND} kind {KIND}\n" in first,
            )
            refusal = refused_write(rule_id, root, bench)
            expect(
                f"{rule_id}: a write of a configuration that the ledger does not hold writes nothing, and names "
                f"--add-configuration",
                "--add-configuration" in refusal
                and rule_ledger.ledger_path(rule_id, root).read_text(encoding="utf-8") == first,
            )
            second = written_text(rule_id, root, bench, add_configuration=True)
            expect(
                f"{rule_id}: --add-configuration keeps the first configuration before the new one, with its facts",
                second.index(f"configuration {KIND} kind {KIND}\n")
                < second.index(f"configuration {KIND}-bench kind {KIND}\n")
                and f"configuration {KIND}-bench CRUCIBLE_BENCH on\n" in second,
            )
            changed = build_config.BuildConfiguration(KIND, ((build_config.KIND_KEY, "aarch64-debug-asan"),))
            expect(
                f"{rule_id}: a write of the base name with other facts writes nothing, also with "
                f"--add-configuration, which tells how to make the ledger again",
                "--add-configuration" in refused_write(rule_id, root, changed)
                and "base configuration" in add_refused(rule_id, root, changed),
            )


def add_refused(rule_id: str, root: Path, configuration: build_config.BuildConfiguration) -> str:
    """Return the refusal of a write of a configuration with --add-configuration, or an empty text."""
    try:
        written_text(rule_id, root, configuration, add_configuration=True)
    except rule_ledger.LedgerError as problem:
        return str(problem)
    return ""


def real_cases(expect: testing.Expect) -> None:
    """A build that is not planted gives its configuration from its CMake cache, its compiler and its commands."""
    with tempfile.TemporaryDirectory(prefix="test-cost-totals-") as scratch:
        root = Path(scratch)
        plant_compiler(root)
        build = plant(root, NATIVE_ROWS, "parse-total", LEDGER_ROWS["parse-total"])
        configuration = BuildContext(root, build).configuration()
        tier = dict(configuration.facts).get(build_config.TIER_KEY, "")
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
    return testing.run(__name__.removeprefix("assay."), argv, (policy_cases, write_cases, real_cases))
