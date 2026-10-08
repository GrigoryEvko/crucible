"""The test of the witness of a measured rule with a holder (assay.guard.engines.buildsys.held).

`guard selftest tests.guard.engines.buildsys.test_held` runs the test.  Each
case writes a plant in a throwaway directory, makes its planted build
(assay.guard.engines.buildsys.planted), and runs the judge of the holder of a
rule on it.  Exit 0 when each case holds, else 1.
"""

import contextlib
import io
import tempfile
from collections.abc import Sequence
from pathlib import Path

from assay.core import registry as rule_registry
from assay.core import testing
from assay.guard.engines.buildsys import held, planted
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner

# A gauge record at 192 jobs.  The wall time of the build and of the test run are each over their error threshold.
SLOW_RECORD = (
    '{"jobs": 192, "steps": [], "medians": {'
    '"build": {"wall_s": 40.0, "cpu_s": 1000.0, "instructions_g": 5000.0, "worst_others_pct": 2.0}, '
    '"tests": {"wall_s": 31.0, "cpu_s": 1000.0, "instructions_g": 10000.0, "worst_others_pct": 2.0}}}\n'
)
BUILD_HEAD = "format 1\nkind x86_64-debug-asan\n"
# Two links of 25 s of CPU time each, over the warning threshold 40 s of total-link-cpu together.
SLOW_LINKS = BUILD_HEAD + "link bin/first cpu=25\nlink bin/second cpu=25\n"
FAST_LINKS = BUILD_HEAD + "link bin/first cpu=5\nlink bin/second cpu=5\n"


def plant_of(directory: Path, name: str, rows: str | None, files: Sequence[tuple[str, str]] = ()) -> Path:
    """Write a plant with a build.txt of the rows and each (path, text) of `files`, and return its directory."""
    plant = directory / name
    plant.mkdir(parents=True, exist_ok=True)
    if rows is not None:
        (plant / planted.BUILD_FILE).write_text(rows, encoding="utf-8")
    for relative, text in files:
        path = plant / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")
    return plant


def context_of(scratch: Path, rows: str | None, files: Sequence[tuple[str, str]] = ()) -> BuildContext:
    """Write a plant, and return the context of its planted build."""
    return planted.plant_context(plant_of(scratch, "plant-held", rows, files), scratch / "tree")


def tree_rule(rule_id: str) -> rule_registry.Rule:
    """Return a rule of the registry of the tree."""
    return rule_registry.load().rules[rule_id]


def is_refused(rule: rule_registry.Rule, context: BuildContext) -> bool:
    """Return True when the judge of the holder refuses the plant with NotApplicable."""
    try:
        held.held_findings(rule, context)
    except meta_runner.NotApplicable:
        return True
    return False


def gauge_cases(expect: testing.Expect) -> None:
    """The judge of the gauge keeps the findings of the held rule, and refuses a record that does not read."""
    with tempfile.TemporaryDirectory(prefix="test-held-") as scratch:
        context = context_of(Path(scratch), BUILD_HEAD, [(f"planted-build/{held.GAUGE_RECORDS}", SLOW_RECORD)])
        found = held.held_findings(tree_rule("edit-test-wall"), context)
        expect(
            "a gauge record over the error threshold gives one error of the held rule, and no finding of another rule",
            [(item.check, item.level) for item in found] == [("edit-test-wall", "error")],
        )
    with tempfile.TemporaryDirectory(prefix="test-held-") as scratch:
        context = context_of(Path(scratch), BUILD_HEAD, [(f"planted-build/{held.GAUGE_RECORDS}", "no record\n")])
        expect("a gauge record that does not read is refused", is_refused(tree_rule("edit-test-wall"), context))


def routing_cases(expect: testing.Expect) -> None:
    """A rule with no holder, or with a holder that the witness cannot judge, is refused."""
    planted_rows = [
        rule_registry.measured_rule("planted-step", 1, 2, "s").replace("held-by=cmake", "held-by=loop-step")
    ]
    step_rule = rule_registry.planted_rules(planted_rows)["planted-step"]
    with tempfile.TemporaryDirectory(prefix="test-held-") as scratch:
        context = context_of(Path(scratch), BUILD_HEAD)
        expect("a rule with no holder is refused", is_refused(tree_rule("compile-cpu"), context))
        expect("a holder with no judge in the witness is refused", is_refused(step_rule, context))


def rule_holder_cases(expect: testing.Expect) -> None:
    """A rule that a rule holds gets the findings of its holder on the plant."""
    rows = (
        BUILD_HEAD
        + "file system.h size=1000\n"
        + "file include/fixy/Base.h size=7024459\n"
        + "compile test/layer/CMakeFiles/layer_sentinel_fixy.dir/checks/fixy/Base.cpp.o "
        + "test/layer/checks/fixy/Base.cpp cpu=3\n"
        + "deps test/layer/CMakeFiles/layer_sentinel_fixy.dir/checks/fixy/Base.cpp.o | "
        + "test/layer/checks/fixy/Base.cpp system.h include/fixy/Base.h\n"
    )
    with tempfile.TemporaryDirectory(prefix="test-held-") as scratch:
        found = held.held_findings(tree_rule("header-alone-fixy"), context_of(Path(scratch), rows))
        expect(
            "a header of 6.7 MB alone gives the error of the holder header-alone for the layer fixy",
            len(found) == 1 and found[0].check == "header-alone" and "header-alone-fixy" in found[0].message,
        )


def cmake_cases(expect: testing.Expect) -> None:
    """The holder cmake refuses a threshold that is not an integer number of operations."""
    row = rule_registry.measured_rule("constexpr-ops", 1000.5, 1000.5, "operations")
    registry_text = rule_registry.planted([row])
    rule = rule_registry.planted_rules([row])["constexpr-ops"]
    with tempfile.TemporaryDirectory(prefix="test-held-") as scratch:
        context = context_of(Path(scratch), BUILD_HEAD, [(str(rule_registry.REGISTRY_PATH), registry_text)])
        expect("a threshold of 1000.5 operations is refused", is_refused(rule, context))


def witness_cases(expect: testing.Expect) -> None:
    """The witness holds only when the plant gives a finding and the control gives none."""
    rule = tree_rule("total-link-cpu")
    with tempfile.TemporaryDirectory(prefix="test-held-") as scratch:
        plants = Path(scratch) / "utils" / "plants" / rule.id
        plant_of(plants, "plant-over", SLOW_LINKS)
        plant_of(plants, "control-under", FAST_LINKS)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            holds = held.witness(rule, Path(scratch))
        expect("a plant with a warning and a clean control hold, because a warning counts", holds == 0)
    with tempfile.TemporaryDirectory(prefix="test-held-") as scratch:
        plants = Path(scratch) / "utils" / "plants" / rule.id
        plant_of(plants, "plant-under", FAST_LINKS)
        plant_of(plants, "control-over", SLOW_LINKS)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            fails = held.witness(rule, Path(scratch))
        expect("a plant with no finding and a control with a warning fail", fails == 1)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (gauge_cases, routing_cases, rule_holder_cases, cmake_cases, witness_cases),
    )
