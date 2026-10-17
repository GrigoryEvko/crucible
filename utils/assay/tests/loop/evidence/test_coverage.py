"""The test of the runner of the kind coverage (assay.loop.evidence.coverage).

`guard selftest tests.loop.evidence.test_coverage` runs the test.  Each case writes one line.  Exit 0 when each case
holds, else 1.

The cases hold the judgment of the run of a header.  A run with each mutant killed adds evidence, and a survivor is a
failure.  An invalid mutant is a failure only when no row of the ledger admits it.  The cases also run three planted
headers through the runner with a planted run of the mutants.  One gets its target, one keeps its failure, and one has no room in the budget.
"""

import tempfile
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import testing
from assay.loop.evidence import coverage, targets
from assay.loop.evidence import items as evidence_items
from assay.loop.evidence import store as evidence_store
from assay.mutation import run as mutation_run

RULE = targets.COVERAGE.rule
HEADER = "include/fixy/Planted.h"


def outcome(status: str, line: int = 10, header: str = HEADER) -> mutation_run.Outcome:
    """Return a planted outcome of one mutant of a header, with a status."""
    return mutation_run.Outcome(f"key-{line}", header, "requires", line, "planted", "requires Planted<T>", status)


def ledger_of(*keys: rule_ledger.Key) -> rule_ledger.Ledger:
    """Return a planted ledger of gate-mutants that admits the invalid mutant of each key."""
    base = {key: rule_ledger.Entry(key, "admit", "a planted reason") for key in keys}
    return rule_ledger.Ledger(targets.COVERAGE.check, rule_registry.Policy.ADMIT, "", base=base)


def judged_cases(expect: testing.Expect) -> None:
    """Hold the judgment of the run of one header (THE RUN OF A HEADER)."""
    killed = coverage.judged([outcome("killed", 10), outcome("killed", 20)], None)
    expect(
        "a run with each mutant killed adds one unit, with the count of the gates and of the kills",
        killed.quantity == 1.0 and not killed.failure and dict(killed.facts) == {"gates": "2", "killed": "2"},
    )
    survived = coverage.judged([outcome("killed", 10), outcome("survived-partial", 20)], None)
    expect(
        "a survivor is a failure that names the survivor",
        survived.quantity == 0.0
        and "no test kills" in survived.failure
        and "line 20" in dict(survived.facts).get("first", ""),
    )
    invalid = outcome("invalid", 30)
    row = mutation_run.exemption_row(invalid.header, invalid.kind, invalid.entity, invalid.original)
    admitted = coverage.judged([outcome("killed", 10), invalid], ledger_of(row))
    expect("an invalid mutant that a row admits is no failure", admitted.quantity == 1.0 and not admitted.failure)
    bare = coverage.judged([invalid], ledger_of())
    expect("an invalid mutant that no row admits is a failure", bare.quantity == 0.0 and "invalid" in bare.failure)
    expect(
        "the command of a failure runs the rule on the header again",
        coverage.rerun_words(Path("/b"), HEADER)[-6:]
        == ("--rule", "gate-mutants", "--build-dir", "/b", "--path", HEADER),
    )


def run_cases(expect: testing.Expect) -> None:
    """Run three planted headers through the runner with a planted run of the mutants (THE BUDGET)."""
    keyed = evidence_items.HeaderKeys(Path("/planted"), (), {}, {}, None)
    statuses = {"include/a/Covered.h": "killed", "include/b/Survivor.h": "survived", "include/c/Long.h": "killed"}

    def planted_run(header: str) -> list[mutation_run.Outcome]:
        return [outcome(statuses[header], header=header)]

    with tempfile.TemporaryDirectory(prefix="evidence-coverage-") as directory:
        store = evidence_store.EvidenceStore(Path(directory))
        long_record = evidence_store.Sample("2026-10-16T12:00:00Z", 1.0, 10_000.0)
        store.add(RULE, "include/c/Long.h", "key-long", long_record)
        chosen = [
            evidence_items.Pending(evidence_items.Item(RULE, name, "build", key), store.read(RULE, key))
            for name, key in (
                ("include/a/Covered.h", "key-covered"),
                ("include/b/Survivor.h", "key-survivor"),
                ("include/c/Long.h", "key-long"),
            )
        ]
        found = coverage.run_headers(store, chosen, keyed, Path("/b"), 600.0, planted_run)
        covered = store.read(RULE, "key-covered")
        survivor = store.read(RULE, "key-survivor")
        long = store.read(RULE, "key-long")
    expect(
        f"a header with each mutant killed gets one unit of evidence: {found}",
        covered is not None and covered.value == 1.0 and found.covered == 1,
    )
    expect(
        "a header with a survivor keeps its failure with the command of the rule",
        survivor is not None
        and survivor.failure is not None
        and "gate-mutants" in survivor.failure.rerun
        and found.failures == ("include/b/Survivor.h",),
    )
    expect(
        "a header whose time does not fit in the budget does not run, and the outcome gives the reason",
        long is not None and long.value == 1.0 and len(long.samples) == 1 and "no room" in found.reason,
    )
    expect(
        "the time of one header is the wall time of one unit, or the first estimate with no sample",
        coverage.header_time(None) == coverage.FIRST_HEADER_S,
    )


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(__name__.removeprefix("assay."), argv, (judged_cases, run_cases))
