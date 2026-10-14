"""The test of the runner of the kind search (assay.loop.evidence.search).

`guard selftest tests.loop.evidence.test_search` runs the test.  Each case writes one line.  Exit 0 when each case
holds, else 1.

The cases hold the slices: the count of iterations, the rate, the seed and the command of a slice.  They also run the
slices on two planted property tests of the fake build tree of `loop test`: one gets its target, and one keeps its
failure with the command that runs its slice again.  ASSAY_TEST_CTEST names the ctest of the fake build tree, else the
pinned ctest of PATH.
"""

import shutil
import tempfile
from pathlib import Path

from assay.core import cache, testing
from assay.loop import affected
from assay.loop.evidence import items as evidence_items
from assay.loop.evidence import search, targets
from assay.loop.evidence import store as evidence_store
from assay.readers import ctest as ctest_reader
from assay.tests.loop.evidence.test_items import ctest_program

RULE = targets.PROPERTY.rule
# The shell scripts of a planted property test that uses CPU time and passes, and of one that stops with the status 3.
PASSING = "i=0; while [ $i -lt 20000 ]; do i=$((i+1)); done; exit 0"
FAILING = "exit 3"


def slice_cases(expect: testing.Expect) -> None:
    """Hold the parts of a slice: the count, the rate, the seed and the command (THE SLICES)."""
    test = ctest_reader.TestEntry("prop_one", ("/build/prop_one", "--iters=500", "--seed=7", "--verbose"))
    expect("the count of the command of a test gives the first count", search.command_iterations(test.command) == 500)
    expect(
        "a command with no count gives the default count",
        search.command_iterations(("/build/prop_two",)) == search.DEFAULT_ITERATIONS,
    )
    samples = (
        evidence_store.Sample("2026-10-14T12:00:00Z", 2.0, 2.0, (("iterations", "1000"),)),
        evidence_store.Sample("2026-10-14T12:00:01Z", 3.0, 3.0, (("iterations", "2000"),)),
        evidence_store.Sample("2026-10-14T12:00:02Z", 1.0, 1.0),
    )
    record = evidence_store.Record(RULE, "prop_one", "key", 6.0, samples)
    expect(
        "the rate is the iterations for each CPU second of the samples that give a count", search.rate_of(record) == 600
    )
    expect(
        "a record with no count gives no rate", search.rate_of(evidence_store.Record(RULE, "prop_one", "key")) is None
    )
    expect(
        "two slices of one key and two keys get different seeds",
        len({search.seed_for("a", 1.0, 0), search.seed_for("a", 1.0, 1), search.seed_for("b", 1.0, 0)}) == 3,
    )
    searched = search.Searched(test, "key", 0.0, 10.0)
    first = search.slice_of(searched, 1.0)
    searched.rate = 100.0
    second = search.slice_of(searched, 1.0)
    expect(
        "a slice replaces the seed and the count of the command, and keeps the other words",
        first.words == ("/build/prop_one", "--verbose", f"--seed={first.seed:#x}", "--iters=500")
        and second.iterations == 100
        and first.seed != second.seed
        and searched.number == 2,
    )


def planted_tree(directory: Path, ctest: str) -> affected.SelfTest | None:
    """Return the fake build tree of `loop test` with two planted property tests of the label of the class.

    Each planted test runs a copy of the shell in the build tree.  The planner of assay.loop.affected then keys it as an
    executable test.  Returns None when PATH has no shell.
    """
    shell = shutil.which("sh")
    if shell is None:
        return None
    fake = affected.SelfTest(directory, ctest)
    program = fake.build_dir / "bin" / "prop_shell"
    shutil.copyfile(shell, program)
    program.chmod(0o755)
    testfile = fake.build_dir / "CTestTestfile.cmake"
    lines = [testfile.read_text(encoding="utf-8")]
    for name, script in (("prop_pass", PASSING), ("prop_fail", FAILING)):
        lines.append(f'add_test([=[{name}]=] "{program}" "-c" [=[{script}]=] "--iters=5")\n')
        lines.append(f'set_tests_properties([=[{name}]=] PROPERTIES LABELS "{targets.PROPERTY.label}")\n')
    testfile.write_text("".join(lines), encoding="utf-8")
    return fake


def run_cases(expect: testing.Expect) -> None:
    """Run the slices on the planted property tests (THE SELECTION, THE SLICES, THE BUDGET)."""
    ctest = ctest_program()
    expect("ASSAY_TEST_CTEST or the pinned ctest of PATH gives the ctest of the fake build tree", bool(ctest))
    if not ctest:
        return
    with tempfile.TemporaryDirectory(prefix="evidence-search-") as directory, cache.scratch_root():
        root = Path(directory)
        fake = planted_tree(root, ctest)
        expect("PATH gives a shell for the planted property tests", fake is not None)
        if fake is None:
            return
        keys = evidence_items.TestKeys(fake.build_dir)
        store = evidence_store.EvidenceStore(root / "store")
        chosen = search.selection(keys, store, 0.05, ())
        expect(
            "only the tests with the label of the class are selected",
            sorted(item.test.name for item in chosen) == ["prop_fail", "prop_pass"],
        )
        outcome = search.run_slices(keys, store, chosen, 60.0, 2, 0.01)
        by_name = {item.name: item for item in evidence_items.class_items(keys, targets.PROPERTY)}
        passed = store.read(RULE, by_name["prop_pass"].key)
        failed = store.read(RULE, by_name["prop_fail"].key)
        expect(
            f"a passing property test gets its target in CPU seconds: {outcome}",
            passed is not None and passed.value >= 0.05 and passed.failure is None and outcome.cpu_s > 0,
        )
        facts = {} if failed is None or failed.failure is None else dict(failed.failure.facts)
        expect(
            "a failing property test keeps its failure with the seed, the count, the status and its command",
            failed is not None
            and failed.failure is not None
            and facts.get("status") == "3"
            and facts.get("iterations") == "5"
            and f"--seed={facts.get('seed')}" in failed.failure.rerun
            and outcome.failures == ("prop_fail",),
        )
        stopped = search.run_slices(keys, store, search.selection(keys, store, 1000.0, ()), 0.5, 2, 1.0)
        expect("a slice with no room in the budget does not start", stopped.slices == 0 and "no room" in stopped.reason)


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(__name__.removeprefix("assay."), argv, (slice_cases, run_cases))
