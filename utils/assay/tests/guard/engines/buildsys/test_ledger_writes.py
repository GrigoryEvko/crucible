"""The test of the writes of the rules test-time, test-memory, fixture-cpu and header-constexpr-ops (`guard write`).

`guard selftest tests.guard.engines.buildsys.test_ledger_writes` runs the
test.  Each case makes a planted build with assay.guard.engines.buildsys.planted
and writes a ledger or a list of it with `guard write`, or with the function of
the rule.  The cases of the list compile planted headers with the patched
compiler (assay.core.testing, THE COMPILER).  Exit 0 when each case holds,
else 1.
"""

import tempfile
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import testing
from assay.core.paths import REPO_ROOT
from assay.guard import write as guard_write
from assay.guard.engines.buildsys import header_constexpr, held, planted, test_time
from assay.guard.engines.buildsys.runner import BuildContext
from assay.tests.guard.engines.buildsys.test_planted import context_of

KIND = "x86_64-debug-asan"
OTHER_KIND = "x86_64-release"
RECORD_ROWS = f"""\
format 1
kind {KIND}
test slow_test | {{build}}/test/slow_test
test big_test | {{build}}/test/big_test
test fast_test | {{build}}/test/fast_test
test-record slow_test wall=40 cpu=1 peak=0.1
test-record big_test wall=1 cpu=1 peak=2
test-record fast_test wall=1 cpu=1 peak=0.1
test-record gone_test wall=50 cpu=1 peak=3
"""
# A ledger of test-memory with a row of a different kind, which each write keeps.
KEPT_LEDGER = f"""\
# A planted ledger.
format 1
rule test-memory
- | {OTHER_KIND} | 1.8 | big_test | a row of a different kind
"""
PLANTS = REPO_ROOT / "utils" / "plants" / header_constexpr.OPS_RULE
LIST_HEAD = "# The planted list of the headers with a higher limit.\n"


def run_write(context: BuildContext, rule_id: str, reason: str) -> tuple[int, str]:
    """Run `guard write` for one rule on the tree and the build of a context, and give its status and its output."""
    words = ["--rule", rule_id, "--root", str(context.root), "--build-dir", str(context.require_build())]
    return testing.captured(lambda: guard_write.main([*words, "--reason", reason]))


def ledger_values(context: BuildContext, rule_id: str) -> dict[tuple[str, str], tuple[float, str]]:
    """Return the value and the reason of each row of the ledger of a rule in the tree of a context, by kind and test."""
    rule = rule_registry.load().rules[rule_id]
    ledger = rule_ledger.read(rule, context.root)
    return {(key.kind, key.entity): (float(entry.value), entry.reason) for key, entry in ledger.base.items()}


def raised(action: object) -> str:
    """Return the text of the LedgerError that an action raises, or an empty text."""
    try:
        action()  # type: ignore[operator]
    except rule_ledger.LedgerError as problem:
        return str(problem)
    return ""


def record_cases(expect: testing.Expect) -> None:
    """Write the ledgers of test-time and test-memory from the records of a planted build (THE WRITE of test_time)."""
    with tempfile.TemporaryDirectory(prefix="test-ledger-writes-") as scratch:
        context = context_of(Path(scratch), RECORD_ROWS)
        ledgers = rule_ledger.ledger_path("test-memory", context.root).parent
        ledgers.mkdir(parents=True)
        rule_ledger.ledger_path("test-memory", context.root).write_text(KEPT_LEDGER, encoding="utf-8")
        status, said = run_write(context, "test-memory", "a planted reason")
        expect(
            "guard write writes the ledger of the held rule test-memory: a row with the peak GB of each registered "
            f"test over the error threshold, and the row of the kind {OTHER_KIND} stays",
            status == 0
            and ledger_values(context, "test-memory")
            == {
                (KIND, "big_test"): (2.0, "a planted reason"),
                (OTHER_KIND, "big_test"): (1.8, "a row of a different kind"),
            },
        )
        expect("the write tells the ledger that it writes", "test-memory.txt holds 2 rows" in said)
        status, _ = run_write(context, "test-time", "a planted reason")
        expect(
            "guard write writes the ledger of test-time with the wall time of each registered test over the error "
            "threshold, and no row of a test that the build does not register",
            status == 0 and ledger_values(context, "test-time") == {(KIND, "slow_test"): (40.0, "a planted reason")},
        )
        run_write(context, "test-memory", "a different reason")
        expect(
            "a second write keeps the reason of each row",
            ledger_values(context, "test-memory")[(KIND, "big_test")] == (2.0, "a planted reason"),
        )
        registry = rule_registry.load()
        other = rule_ledger.empty(registry.rules["compile-cpu"])
        expect(
            "written() of test_time refuses the ledger of a rule other than test-time and test-memory",
            "is not a ledger of" in raised(lambda: test_time.written(context, other, None)),
        )
        fixture_cpu = registry.rules["fixture-cpu"]
        writer = held.WRITERS.pop(fixture_cpu.id)
        try:
            expect(
                "written() of held refuses a held rule with no function in WRITERS, and names the table",
                "WRITERS" in raised(lambda: held.written(fixture_cpu, context, rule_ledger.empty(fixture_cpu), None)),
            )
            status, said = run_write(context, "fixture-cpu", "a planted reason")
            expect(
                "guard write refuses a held rule with no function in WRITERS, with status 1",
                status == 1 and "is not written" in said and "WRITERS" in said,
            )
        finally:
            held.WRITERS[fixture_cpu.id] = writer
        status, said = run_write(context, "fixture-cpu", "a planted reason")
        expect(
            "guard write writes the ledger of fixture-cpu from the records of the fixtures of the build",
            status == 0 and "fixture-cpu.txt holds 0 rows" in said,
        )


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
    return testing.run(__name__.removeprefix("assay."), argv, (record_cases, list_cases, compile_cases))
