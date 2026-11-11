"""The test of the writes of the rules of a step, of a held rule and of header-constexpr-ops.

`guard selftest tests.guard.engines.buildsys.test_ledger_writes` runs the
test.  Each case makes a planted build with assay.guard.engines.buildsys.planted
and writes a ledger or a list of it with `guard write`, or with the function of
the rule.  The cases of the list compile planted headers with the patched
compiler (assay.core.testing, THE COMPILER).  Exit 0 when each case holds,
else 1.
"""

import tempfile
from dataclasses import replace
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import testing
from assay.core.paths import REPO_ROOT
from assay.guard import write as guard_write
from assay.guard.engines.buildsys import cost_budgets, header_constexpr, held, planted
from assay.guard.engines.buildsys.runner import BuildContext
from assay.tests.guard.engines.buildsys.test_planted import context_of

KIND = "x86_64-debug-asan"
# A small compile and a small link give the module of a rule of a step its inputs.  The tests and their records give
# the rows of the kind test.
RECORD_ROWS = f"""\
format 1
kind {KIND}
compile CMakeFiles/lib.dir/small.cpp.o src/small.cpp cpu=1 peak=0.1 instructions=1
link test/small_test cpu=1 peak=0.1
test slow_test | {{build}}/test/slow_test
test big_test | {{build}}/test/big_test
test fast_test | {{build}}/test/fast_test
test counted_slow_test | {{build}}/test/counted_slow_test
test heavy_test | {{build}}/test/heavy_test
test-record slow_test wall=40 cpu=1 peak=0.1
test-record big_test wall=1 cpu=1 peak=2
test-record fast_test wall=1 cpu=1 peak=0.1 instructions=1
test-record counted_slow_test wall=40 cpu=1 peak=0.1 instructions=1
test-record heavy_test wall=1 cpu=1 peak=0.1 instructions=600
test-record gone_test wall=50 cpu=1 peak=3
"""
# A ledger of step-memory with a row of the kind test of a test that the build does not register, which each write
# keeps.
KEPT_LEDGER = """\
# A planted ledger.
format 1
rule step-memory
- | test | 3.5 | gone_test | a row of a test of a different build
"""
# The fixtures of a planted build: one over the limit of the kind fixture, two under it, and one with no count.
FIXTURE_ROWS = f"""\
format 1
kind {KIND}
compile CMakeFiles/lib.dir/small.cpp.o src/small.cpp cpu=1 instructions=1
fixture neg_slow test/neg/slow.cpp user=19 system=1
fixture neg_heavy test/neg/heavy.cpp user=1 system=0 instructions=50
fixture neg_counted test/neg/counted.cpp user=19 system=1 instructions=10
fixture neg_fast test/neg/fast.cpp user=1 system=0 instructions=1
"""
PLANTS = REPO_ROOT / "utils" / "plants" / header_constexpr.OPS_RULE
LIST_HEAD = "# The planted list of the headers with a higher limit.\n"


def run_write(context: BuildContext, rule_id: str, reason: str) -> tuple[int, str]:
    """Run `guard write` for one rule on the tree and the build of a context, and give its status and its output."""
    words = ["--rule", rule_id, "--root", str(context.root), "--build-dir", str(context.require_build())]
    return testing.captured(lambda: guard_write.main([*words, "--reason", reason]))


def ledger_values(context: BuildContext, rule_id: str) -> dict[tuple[str, str], tuple[float, str]]:
    """Return the value and the reason of each row of the ledger of a rule in the tree of a context, by kind and item.

    The item of a row is its entity, or its path when it has no entity.
    """
    rule = rule_registry.load().rules[rule_id]
    ledger = rule_ledger.read(rule, context.root)
    return {
        (key.kind, key.entity or key.path): (float(entry.value), entry.reason) for key, entry in ledger.base.items()
    }


def raised(action: object) -> str:
    """Return the text of the LedgerError that an action raises, or an empty text."""
    try:
        action()  # type: ignore[operator]
    except rule_ledger.LedgerError as problem:
        return str(problem)
    return ""


def record_cases(expect: testing.Expect) -> None:
    """Write the rows of the kind test of the rules of a step from the records of a planted build (THE LEDGERS)."""
    with tempfile.TemporaryDirectory(prefix="test-ledger-writes-") as scratch:
        context = context_of(Path(scratch), RECORD_ROWS)
        ledgers = rule_ledger.ledger_path("step-memory", context.root).parent
        ledgers.mkdir(parents=True)
        rule_ledger.ledger_path("step-memory", context.root).write_text(KEPT_LEDGER, encoding="utf-8")
        registry = rule_registry.load()
        no_rows = rule_ledger.empty(registry.rules["step-memory"])
        expect(
            "the write of step-memory refuses a new row with no reason, and names --reason",
            "--reason" in raised(lambda: cost_budgets.budget_written("step-memory", context, no_rows, None)),
        )
        status, said = run_write(context, "step-memory", "a planted reason")
        expect(
            "guard write writes the ledger of step-memory: a row of the kind test with the peak GB of each "
            "registered test over the limit, and the row of a test of a different build stays",
            status == 0
            and ledger_values(context, "step-memory")
            == {
                ("test", "big_test"): (2.0, "a planted reason"),
                ("test", "gone_test"): (3.5, "a row of a test of a different build"),
            },
        )
        expect("the write tells the ledger that it writes", "step-memory.txt holds 2 rows" in said)
        status, said = run_write(context, "step-cost", "a planted reason")
        expect(
            "guard write writes the ledger of step-cost: a row of the kind test with the count in G of each "
            "registered test over the limit, and no row of a test whose record holds no count",
            status == 0
            and ledger_values(context, "step-cost") == {("test", "heavy_test"): (600.0, "a planted reason")}
            and "step-cost.txt holds 1 rows" in said,
        )
        run_write(context, "step-memory", "a different reason")
        expect(
            "a second write keeps the reason of a row that keeps its test",
            ledger_values(context, "step-memory")[("test", "big_test")] == (2.0, "a planted reason"),
        )
        held_rule = rule_registry.planted_rules([rule_registry.measured_rule("planted-held", 2, "s")])["planted-held"]
        expect(
            "written() of held refuses each held rule, because no function writes its ledger",
            "no function writes its ledger"
            in raised(lambda: held.written(held_rule, context, rule_ledger.empty(held_rule), None)),
        )


def fixture_cases(expect: testing.Expect) -> None:
    """Write the rows of the kind fixture of step-cost from the records of the fixtures of a planted build."""
    with tempfile.TemporaryDirectory(prefix="test-ledger-writes-") as scratch:
        context = context_of(Path(scratch), FIXTURE_ROWS)
        empty = rule_ledger.empty(rule_registry.load().rules["step-cost"])
        expect(
            "a new row of a fixture with no reason is refused, and the message names --reason",
            "--reason" in raised(lambda: cost_budgets.budget_written("step-cost", context, empty, None)),
        )
        written = cost_budgets.budget_written("step-cost", context, empty, "a planted reason")
        values = {(key.kind, key.entity): (entry.value, entry.reason) for key, entry in written.base.items()}
        expect(
            "step-cost writes a row of the kind fixture for the fixture over 45 G, and no row for a fixture with no "
            "count",
            values == {("fixture", "neg_heavy"): (50.0, "a planted reason")},
        )
        again = cost_budgets.budget_written("step-cost", context, written, "a different reason")
        expect("a second write keeps the row of a fixture that the row keeps", again.base == written.base)
        gone_key = rule_ledger.Key("", "fixture", "neg_gone")
        with_gone = replace(written, base={**written.base, gone_key: rule_ledger.Entry(gone_key, 60.0, "a row")})
        kept = cost_budgets.budget_written("step-cost", context, with_gone, None)
        expect("a write from a build with no such fixture keeps its row", gone_key in kept.base)
    with tempfile.TemporaryDirectory(prefix="test-ledger-writes-") as scratch:
        context = context_of(Path(scratch), FIXTURE_ROWS + "fixture neg_unrun test/neg/unrun.cpp record=no\n")
        said = raised(lambda: cost_budgets.budget_written("step-cost", context, empty, None))
        expect("a write refuses a build with a fixture that has no record", "neg_unrun" in said)


def list_text(context: BuildContext) -> str:
    """Return the text of the list of header-constexpr-ops in the tree of a context."""
    return (context.root / header_constexpr.LIST).read_text(encoding="utf-8")


def list_cases(expect: testing.Expect) -> None:
    """Remove the rows that no header makes necessary from a list with no compile, through `guard write` (THE WRITE)."""
    with tempfile.TemporaryDirectory(prefix="test-ledger-writes-") as scratch:
        context = context_of(Path(scratch), f"format 1\nkind {KIND}\n")
        header = context.root / header_constexpr.INCLUDE / "crucible" / "Light.h"
        header.parent.mkdir(parents=True)
        header.write_text("#pragma once\n", encoding="utf-8")
        listed = context.root / header_constexpr.LIST
        listed.parent.mkdir(parents=True)
        listed.write_text(
            f"{LIST_HEAD}crucible/Gone.h | 2000000 | a header that is not there\n"
            "crucible/Light.h | 1000 | a limit at the threshold of the registry or below it\n",
            encoding="utf-8",
        )
        status, said = run_write(context, header_constexpr.OPS_RULE, "unused")
        expect(
            "guard write removes a row of a header that is not in include/ and a row with no limit over the threshold, "
            "and it keeps the head comment",
            status == 0 and list_text(context) == LIST_HEAD and "holds 0 rows, and the write removed 2" in said,
        )
        listed.write_text(f"{LIST_HEAD}crucible/Light.h | 2000000\n", encoding="utf-8")
        status, said = run_write(context, header_constexpr.OPS_RULE, "unused")
        expect(
            "guard write refuses a row with no reason, with status 1, and the list stays",
            status == 1
            and "is not written" in said
            and list_text(context) == f"{LIST_HEAD}crucible/Light.h | 2000000\n",
        )


def compile_cases(expect: testing.Expect) -> None:
    """Remove each row of a header that compiles alone at the threshold, on copies of the plants of the rule."""
    compiler = testing.patched_compiler()
    expect(
        "ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the patched compiler",
        bool(compiler) and Path(compiler).is_file(),
    )
    if not compiler or not Path(compiler).is_file():
        return
    with tempfile.TemporaryDirectory(prefix="test-ledger-writes-") as scratch:
        listed = planted.plant_context(PLANTS / "control-listed", Path(scratch) / "listed")
        before = list_text(listed)
        expect(
            "the write keeps each row of a header that does not compile alone at the threshold, and the list stays",
            "holds 2 rows, and the write removed 0" in header_constexpr.ops_refresh(listed)
            and list_text(listed) == before,
        )
        unneeded = planted.plant_context(PLANTS / "plant-unneeded", Path(scratch) / "unneeded")
        expect(
            "the write removes the row of a header that compiles alone at the threshold",
            "holds 0 rows, and the write removed 1" in header_constexpr.ops_refresh(unneeded)
            and list_text(unneeded) == LIST_HEAD,
        )
        twice = planted.plant_context(PLANTS / "plant-list-duplicate", Path(scratch) / "twice")
        expect(
            "the write removes the second row of a header, and it keeps the first row",
            "holds 1 rows, and the write removed 1" in header_constexpr.ops_refresh(twice)
            and list_text(twice) == f"{LIST_HEAD}crucible/Heavy.h | 100000 | spin(2000) at namespace scope\n",
        )
        shape = planted.plant_context(PLANTS / "plant-list-shape", Path(scratch) / "shape")
        expect(
            "the write refuses a row that does not have its shape",
            "does not have its shape" in raised(lambda: header_constexpr.ops_refresh(shape)),
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (record_cases, fixture_cases, list_cases, compile_cases))
