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
from dataclasses import replace
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import testing
from assay.guard.engines.buildsys import held, planted
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner

# The rule of the gauge that the cases judge.
GAUGE_RULE = "edit-build-instructions"
# A gauge record whose edit build has a median of COUNT G instructions.  The limit of the rule of the tree is 11800 G.
# The count of the test run is no measure of a rule.
GAUGE_RECORD = (
    '{"jobs": 24, "steps": [], "medians": {'
    '"build": {"wall_s": 45.0, "cpu_s": 1000.0, "instructions_g": COUNT, "worst_others_pct": 2.0}, '
    '"tests": {"wall_s": 130.0, "cpu_s": 1000.0, "instructions_g": 99999.0, "worst_others_pct": 2.0}}}\n'
)
OVER_LIMIT = GAUGE_RECORD.replace("COUNT", "12000.0")
UNDER_LIMIT = GAUGE_RECORD.replace("COUNT", "11500.0")
BUILD_HEAD = "format 1\nkind x86_64-debug-asan\n"


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


def gauge_records(records: str) -> tuple[str, str]:
    """Return the planted file of the records of the gauge in the planted build of a plant."""
    return f"planted-build/{held.GAUGE_RECORDS}", records


def gauge_cases(expect: testing.Expect) -> None:
    """The judge of the gauge keeps the findings of the held rule, and refuses a record that does not read."""
    with tempfile.TemporaryDirectory(prefix="test-held-") as scratch:
        context = context_of(Path(scratch), BUILD_HEAD, [gauge_records(OVER_LIMIT)])
        found = held.held_findings(tree_rule(GAUGE_RULE), context)
        expect(
            "a gauge record above the limit gives one error of the held rule, and no finding of the tests",
            [(item.check, item.level) for item in found] == [(GAUGE_RULE, "error")],
        )
    with tempfile.TemporaryDirectory(prefix="test-held-") as scratch:
        context = context_of(Path(scratch), BUILD_HEAD, [gauge_records("no record\n")])
        expect("a gauge record that does not read is refused", is_refused(tree_rule(GAUGE_RULE), context))


def routing_cases(expect: testing.Expect) -> None:
    """A rule with no holder, or with a holder that the witness cannot judge, is refused."""
    planted_rows = [
        rule_registry.measured_rule("planted-step", 2, "s").replace("held-by=loop-gauge", "held-by=loop-step")
    ]
    step_rule = rule_registry.planted_rules(planted_rows)["planted-step"]
    with tempfile.TemporaryDirectory(prefix="test-held-") as scratch:
        context = context_of(Path(scratch), BUILD_HEAD)
        expect("a rule with no holder is refused", is_refused(tree_rule("compile-memory"), context))
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


def witness_cases(expect: testing.Expect) -> None:
    """The witness holds only when the plant gives a finding and the control gives none."""
    rule = tree_rule(GAUGE_RULE)
    with tempfile.TemporaryDirectory(prefix="test-held-") as scratch:
        plants = Path(scratch) / "utils" / "plants" / rule.id
        plant_of(plants, "plant-over", BUILD_HEAD, [gauge_records(OVER_LIMIT)])
        plant_of(plants, "control-under", BUILD_HEAD, [gauge_records(UNDER_LIMIT)])
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            holds = held.witness(rule, Path(scratch))
        expect("a plant above the limit and a control below it hold", holds == 0)
    with tempfile.TemporaryDirectory(prefix="test-held-") as scratch:
        plants = Path(scratch) / "utils" / "plants" / rule.id
        plant_of(plants, "plant-under", BUILD_HEAD, [gauge_records(UNDER_LIMIT)])
        plant_of(plants, "control-over", BUILD_HEAD, [gauge_records(OVER_LIMIT)])
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            fails = held.witness(rule, Path(scratch))
        expect("a plant with no finding and a control with an error fail", fails == 1)


def fixture_write_cases(expect: testing.Expect) -> None:
    """The write of the ledger of the fixture driver: a row for each instruction count above the limit."""
    rows = (
        BUILD_HEAD
        + "fixture neg_slow test/neg/slow.cpp user=19 system=1\n"
        + "fixture neg_heavy test/neg/heavy.cpp user=1 system=0 instructions=50\n"
        + "fixture neg_counted test/neg/counted.cpp user=19 system=1 instructions=10\n"
        + "fixture neg_fast test/neg/fast.cpp user=1 system=0 instructions=1\n"
    )

    def rows_of(ledger: rule_ledger.Ledger) -> dict[str, tuple[float | int | str, str]]:
        """Return the value and the reason of each row of a ledger, by the name of its fixture."""
        return {key.entity: (entry.value, entry.reason) for key, entry in ledger.base.items()}

    with tempfile.TemporaryDirectory(prefix="test-held-") as scratch:
        context = context_of(Path(scratch), rows)
        instructions = tree_rule(held.FIXTURE_INSTRUCTIONS)
        empty = rule_ledger.Ledger(instructions.id, rule_registry.Policy.MAX, "")
        try:
            held.written(instructions, context, empty, None)
            refused = ""
        except rule_ledger.LedgerError as problem:
            refused = str(problem)
        expect("a new row with no reason is refused, and the message names --reason", "--reason" in refused)
        written = held.written(instructions, context, empty, "a planted reason")
        expect(
            "fixture-instructions writes the fixture over 45 G with the reason, and no fixture with no count",
            rows_of(written) == {"neg_heavy": (50.0, "a planted reason")},
        )
        again = held.written(instructions, context, written, "a different reason")
        expect("a second write keeps the row of a fixture that the row keeps", rows_of(again) == rows_of(written))
        gone_key = rule_ledger.Key("", "", "neg_gone")
        with_gone = replace(written, base={**written.base, gone_key: rule_ledger.Entry(gone_key, 60.0, "a row")})
        kept = held.written(instructions, context, with_gone, None)
        expect(
            "a write from a build with no such fixture keeps its row",
            rows_of(kept) == {"neg_heavy": (50.0, "a planted reason"), "neg_gone": (60.0, "a row")},
        )
    with tempfile.TemporaryDirectory(prefix="test-held-") as scratch:
        context = context_of(Path(scratch), rows + "fixture neg_unrun test/neg/unrun.cpp record=no\n")
        try:
            held.written(instructions, context, empty, None)
            said = ""
        except rule_ledger.LedgerError as problem:
            said = str(problem)
        expect("a write refuses a build with a fixture that has no record", "neg_unrun" in said)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (
            gauge_cases,
            routing_cases,
            rule_holder_cases,
            witness_cases,
            fixture_write_cases,
        ),
    )
