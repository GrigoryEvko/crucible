"""The test of the runner of the kind repetition (assay.loop.evidence.repetition).

`guard selftest tests.loop.evidence.test_repetition` runs the test.  Each case writes one line.  Exit 0 when each case
holds, else 1.

The cases run the rounds on the fake build tree of `loop test`.  A test with a pass in each round gets its target.  A
test with a failure keeps its failure and goes out of the selection.  A named test runs with a kept failure.  The
budget stops a round that has no room.  ASSAY_TEST_CTEST names the ctest of the fake build tree, else the pinned ctest
of PATH.
"""

import tempfile
from pathlib import Path

from assay.core import cache, testing
from assay.loop import affected
from assay.loop.evidence import items as evidence_items
from assay.loop.evidence import repetition, targets
from assay.loop.evidence import store as evidence_store
from assay.tests.loop.evidence.test_items import ctest_program

RULE = targets.STRESS.rule
NAMES = ("exe_ok", "exe_bad")


def round_cases(expect: testing.Expect) -> None:
    """Hold the rounds on the fake build tree (THE SELECTION, THE ROUNDS, THE BUDGET)."""
    ctest = ctest_program()
    expect("ASSAY_TEST_CTEST or the pinned ctest of PATH gives the ctest of the fake build tree", bool(ctest))
    if not ctest:
        return
    with tempfile.TemporaryDirectory(prefix="evidence-repetition-") as directory, cache.scratch_root():
        root = Path(directory)
        fake = affected.SelfTest(root, ctest)
        keys = evidence_items.TestKeys(fake.build_dir)
        store = evidence_store.EvidenceStore(root / "store")
        chosen = repetition.selection(keys, store, 2.0, NAMES)
        expect("the names select only the named tests", sorted(item.test.name for item in chosen) == sorted(NAMES))
        outcome = repetition.run_rounds(keys, store, chosen, 600.0, 4)
        by_name = {item.name: item for item in evidence_items.class_items(keys, targets.STRESS)}
        passed = store.read(RULE, by_name["exe_ok"].key)
        failed = store.read(RULE, by_name["exe_bad"].key)
        expect(
            f"a passing test reaches its target in two rounds: {outcome}",
            outcome.rounds == len(outcome.round_s) == 2
            and passed is not None
            and passed.value == 2.0
            and passed.failure is None,
        )
        expect(
            "a failing test keeps its failure with the repetition and the command that runs it again, and leaves",
            failed is not None
            and failed.failure is not None
            and ("repetition", "1") in failed.failure.facts
            and "^exe_bad$" in failed.failure.rerun
            and outcome.failures == ("exe_bad",),
        )
        again = repetition.selection(keys, store, 3.0, ())
        expect(
            "a test with a kept failure is not selected, and a test under the target is",
            "exe_bad" not in {item.test.name for item in again} and "exe_ok" in {item.test.name for item in again},
        )
        named = repetition.selection(keys, store, 3.0, ("exe_bad",))
        expect("a named test is selected also with a kept failure", [item.test.name for item in named] == ["exe_bad"])
        for item in again:
            item.unit_wall_s = 1000.0
        stopped = repetition.run_rounds(keys, store, again, 1.0, 4)
        expect(
            "a round with no room in the budget does not start", stopped.rounds == 0 and "has no room" in stopped.reason
        )


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(__name__.removeprefix("assay."), argv, (round_cases,))
