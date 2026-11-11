"""The test of the rule instruction-total and its base configuration (assay.guard.engines.buildsys.cost_totals).

`guard selftest tests.guard.engines.buildsys.test_cost_totals` runs the test.
Each case plants a build with the rows of assay.guard.engines.buildsys.planted
and a ledger of the rule in a throwaway tree.  The witness of the rule holds
the refusals of a ledger, and a build of a different kind.  This test holds
what a plant cannot hold:
  * a build of the kind of the base with a different name of its
    configuration, which the rule and the write do not judge
  * the two rows and the head comment of a write (THE LEDGER)
  * a fall, which a write with no reason lowers
  * a growth, which a write refuses with no reason and raises with a reason
  * the configuration of a build that is not planted.
Exit 0 when each case holds, else 1.
"""

import shutil
import tempfile
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import testing
from assay.guard.engines.buildsys import cost_totals, planted
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import cmake

KIND = cost_totals.BASE_CONFIGURATION
# A build of two objects of 2 G instructions and two fixtures of 5 G: 14 G, the total of LEDGER_ROWS.
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
# The same build with objects of 4 G: 18 G, a growth over the limit.
GROWN_ROWS = BUILD_ROWS.replace("instructions=2", "instructions=4")
# The same build with fixtures of 1 G: 6 G, a fall.
FALLEN_ROWS = BUILD_ROWS.replace("instructions=5", "instructions=1")
LEDGER_REASON = "the measure of the planted build"
LEDGER_ROWS = f"""\
- | {KIND} | 10000000000 | fixtures | {LEDGER_REASON}
- | {KIND} | 4000000000 | objects | {LEDGER_REASON}
"""
# A head comment that a different writer wrote, with an incorrect fact.
STALE_HEAD = "# The rows count the last real compile of each unit.\n"
# The rows of a build that is not planted with a configuration, with one object of all that takes -march=native.
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


def rule_of() -> rule_registry.Rule:
    """Return the rule instruction-total of the registry of this tree."""
    return rule_registry.load().rules[cost_totals.INSTRUCTION_RULE]


def plant(root: Path, rows: str, ledger_rows: str) -> Path:
    """Write the planted build of the rows and the ledger of the rule in a tree, and return the build directory."""
    build = root / planted.BUILD
    (root / planted.BUILD_FILE).parent.mkdir(parents=True, exist_ok=True)
    (root / planted.BUILD_FILE).write_text(rows, encoding="utf-8")
    planted.write_build(planted.read_rows(root / planted.BUILD_FILE, root, build), root, build)
    ledger = rule_ledger.ledger_path(cost_totals.INSTRUCTION_RULE, root)
    ledger.parent.mkdir(parents=True, exist_ok=True)
    ledger.write_text(f"format 1\nrule {cost_totals.INSTRUCTION_RULE}\n{ledger_rows}", encoding="utf-8")
    return build


def configuration_of(name: str) -> build_config.BuildConfiguration:
    """Return a planted configuration of a name, as planted.plant_context() gives it."""
    return build_config.BuildConfiguration(name)


def judged_of(
    root: Path, rows: str, configuration: build_config.BuildConfiguration
) -> tuple[list[check_report.Finding] | str, str]:
    """Plant a build of the rows and the ledger, and return the findings of the rule and its output.

    The result is the reason of the rule in place of the findings when the rule does not apply to the build.
    """
    build = plant(root, rows, LEDGER_ROWS)
    context = BuildContext(root, build, configuration=configuration)

    def findings() -> list[check_report.Finding] | str:
        """Return the findings of the rule, or the reason when it does not apply."""
        try:
            return context.findings_of(rule_of())
        except meta_runner.NotApplicable as reason:
            return str(reason)

    return testing.captured(findings)


def written_text(root: Path, configuration: build_config.BuildConfiguration, reason: str | None = None) -> str:
    """Write the ledger of the rule from the planted build of a tree, and return its text.

    Raises:
        LedgerError: If the write refuses the build
        NotApplicable: If the rule does not apply to the build
    """
    context = BuildContext(root, root / planted.BUILD, configuration=configuration)
    context.rule = rule_of()
    member = cost_totals.RULES[cost_totals.INSTRUCTION_RULE]
    if member.written is None:
        raise rule_ledger.LedgerError("the member of the rule writes no ledger")
    written = member.written(context, rule_ledger.read(rule_of(), root), reason)
    text = rule_ledger.render(written)
    rule_ledger.ledger_path(cost_totals.INSTRUCTION_RULE, root).write_text(text, encoding="utf-8")
    return text


def refused_write(root: Path, configuration: build_config.BuildConfiguration, reason: str | None = None) -> str:
    """Return the refusal of a write as text, or an empty text when the write writes the ledger."""
    try:
        written_text(root, configuration, reason)
    except (rule_ledger.LedgerError, meta_runner.NotApplicable) as problem:
        return str(problem)
    return ""


def total_of(root: Path) -> tuple[int, str]:
    """Return the sum of the two total rows of the ledger of a tree, and their reason."""
    base = rule_ledger.read(rule_of(), root).base
    entries = [base[rule_ledger.Key("", KIND, part)] for part in cost_totals.CENSUS_PARTS]
    return sum(int(entry.value) for entry in entries), entries[0].reason


def configuration_cases(expect: testing.Expect) -> None:
    """A build of the kind of the base with a different name of its configuration gets no verdict and no write."""
    bench = configuration_of(f"{KIND}-bench")
    with tempfile.TemporaryDirectory(prefix="test-cost-totals-") as scratch:
        root = Path(scratch)
        found, _ = judged_of(root, GROWN_ROWS, bench)
        expect(
            "a build of the configuration KIND-bench with a growth gives no finding, because the rule judges only "
            "the base",
            isinstance(found, str) and f"the build has the configuration {KIND}-bench" in found,
        )
        before = rule_ledger.ledger_path(cost_totals.INSTRUCTION_RULE, root).read_text(encoding="utf-8")
        refusal = refused_write(root, bench)
        expect(
            "a write from a build of the configuration KIND-bench writes nothing, and names no --add-configuration",
            f"configuration {KIND}" in refusal
            and "--add-configuration" not in refusal
            and rule_ledger.ledger_path(cost_totals.INSTRUCTION_RULE, root).read_text(encoding="utf-8") == before,
        )
        found, _ = judged_of(root, GROWN_ROWS, configuration_of(KIND))
        expect(
            "a build of the base with the same growth gives an error of the kind growth",
            not isinstance(found, str) and [(item.level, item.kind) for item in found] == [("error", "growth")],
        )


def write_cases(expect: testing.Expect) -> None:
    """A write gives the head comment of the module and the two rows of the base, and no configuration row."""
    with tempfile.TemporaryDirectory(prefix="test-cost-totals-") as scratch:
        root = Path(scratch)
        plant(root, BUILD_ROWS, "")
        first = written_text(root, configuration_of(KIND))
        rows = [line for line in first.splitlines() if not line.startswith("#")]
        expect(
            "a write into an empty ledger gives the head of the module, the two rows of the base and the default "
            "reason",
            first.startswith(cost_totals.INSTRUCTION_LEDGER_COMMENT)
            and rows
            == [
                "format 1",
                f"rule {cost_totals.INSTRUCTION_RULE}",
                f"- | {KIND} | 10000000000 | fixtures | {cost_totals.CENSUS_DEFAULT_REASON}",
                f"- | {KIND} | 4000000000 | objects | {cost_totals.CENSUS_DEFAULT_REASON}",
            ],
        )
    with tempfile.TemporaryDirectory(prefix="test-cost-totals-") as scratch:
        root = Path(scratch)
        plant(root, BUILD_ROWS, LEDGER_ROWS)
        path = rule_ledger.ledger_path(cost_totals.INSTRUCTION_RULE, root)
        path.write_text(f"{STALE_HEAD}{path.read_text(encoding='utf-8')}", encoding="utf-8")
        text = written_text(root, configuration_of(KIND))
        expect(
            "a write replaces a head comment that a different writer wrote with the head of the module",
            text.startswith(cost_totals.INSTRUCTION_LEDGER_COMMENT) and STALE_HEAD not in text,
        )


def fall_cases(expect: testing.Expect) -> None:
    """A total less than its rows gives no finding, and a write with no reason lowers the rows (THE FINDINGS)."""
    with tempfile.TemporaryDirectory(prefix="test-cost-totals-") as scratch:
        root = Path(scratch)
        found, text = judged_of(root, FALLEN_ROWS, configuration_of(KIND))
        before, _ = total_of(root)
        written_text(root, configuration_of(KIND))
        after, reason = total_of(root)
        expect(
            "a total less than the rows gives no finding, and the summary line gives the fall",
            found == [] and f"{cost_totals.census_change_text(after, before)}" in text,
        )
        expect(
            "a write with no reason lowers the rows to the total of the build, and keeps the last reason",
            after < before and reason == LEDGER_REASON,
        )


def growth_cases(expect: testing.Expect) -> None:
    """A write refuses a growth with no reason, and raises the rows with a reason."""
    with tempfile.TemporaryDirectory(prefix="test-cost-totals-") as scratch:
        root = Path(scratch)
        plant(root, GROWN_ROWS, LEDGER_ROWS)
        before, _ = total_of(root)
        refusal = refused_write(root, configuration_of(KIND))
        expect(
            "a write with no reason refuses a growth, and names --reason",
            "--reason TEXT" in refusal and total_of(root)[0] == before,
        )
        written_text(root, configuration_of(KIND), "a planted growth")
        after, reason = total_of(root)
        expect(
            "a write with a reason raises the rows, and gives the two rows that reason",
            after > before and reason == "a planted growth",
        )
    with tempfile.TemporaryDirectory(prefix="test-cost-totals-") as scratch:
        root = Path(scratch)
        plant(root, BUILD_ROWS.replace("instructions=5", "instructions=5.05"), LEDGER_ROWS)
        before, _ = total_of(root)
        expect(
            "a write with no reason refuses also a growth below the limit, because a higher baseline needs a reason",
            "--reason TEXT" in refused_write(root, configuration_of(KIND)) and total_of(root)[0] == before,
        )


def real_cases(expect: testing.Expect) -> None:
    """A build that is not planted gives the name of its configuration from its build kind and its CMake cache."""
    with tempfile.TemporaryDirectory(prefix="test-cost-totals-") as scratch:
        root = Path(scratch)
        build = plant(root, NATIVE_ROWS, LEDGER_ROWS)
        configuration = BuildContext(root, build).configuration()
        expect("the configuration of a Debug build has the name of its kind", configuration.name == KIND)
        shutil.rmtree(build.joinpath(*cmake.API_DIRECTORY))
        context = BuildContext(root, build)
        found, _ = testing.captured(lambda: context.findings_of(rule_of()))
        expect(
            "a build whose CMake cache cannot be read gives one error of the kind configuration",
            [(item.level, item.kind) for item in found] == [("error", "configuration")],
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (configuration_cases, write_cases, fall_cases, growth_cases, real_cases),
    )
