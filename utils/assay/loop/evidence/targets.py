"""targets — the classes of the sampled checks of the tree, and the measured rule that holds the target of each class.

THE SAMPLED CHECKS
    One run of a test with a result that does not change is full evidence
    for its key.  A sampled check gives only a part of its evidence in one
    run.  The evidence of each run then adds to the evidence of the previous
    runs.  Each class of sampled checks has one kind of evidence (Kind), and
    one measured rule of utils/registry.txt holds its target (TARGETS):
      repetition   the passes of a test at its key.  A test preset with the
                   field `repeat` runs each test of its build again, to find
                   a race
      search       the CPU seconds of a search at its key, and the inputs
                   that the search found
      measurement  the valid measurements of a program at its key.  A
                   measurement counts only on a quiet host
      coverage     the runs of a check on each item of its scope, at the key
                   of that item
    The rule of a target has the holder HOLDER.  Its warning threshold is the
    target of the nightly entry, and its error threshold is the target of the
    release entry.  The status of `loop evidence` judges each item against
    them (assay.loop.evidence.status, THE JUDGMENT).

    A row of TARGETS is data only.  assay.loop.evidence.items gives the items
    of each class and their keys, and the runner of each kind adds evidence.

THE ITEMS OF A CLASS
    The items of a class of tests are the tests with its label in its builds:
      Builds.REPEATED  the build of each test preset with the field `repeat`
      Builds.FUZZ      the build of each configure preset that builds the
                       fuzzers (assay.loop.tiers, PresetTier)
    A class with no label takes each test of its builds.  The items of the
    class of the benches are the bench targets of its builds:
      Builds.BENCH     the build of each configure preset that builds the
                       benches for a measurement: with no fuzzers and no
                       profile counters (assay.loop.tiers, PresetTier)
"""

import enum
from dataclasses import dataclass

# The holder of the rule of each target in utils/registry.txt (the field held-by).
HOLDER = "loop-evidence"


class Kind(enum.StrEnum):
    """The kind of evidence of a class of sampled checks (THE SAMPLED CHECKS)."""

    REPETITION = "repetition"
    SEARCH = "search"
    MEASUREMENT = "measurement"
    COVERAGE = "coverage"


class Builds(enum.StrEnum):
    """The builds that give the items of a class (THE ITEMS OF A CLASS)."""

    REPEATED = "repeated"
    FUZZ = "fuzz"
    BENCH = "bench"


@dataclass(frozen=True, slots=True)
class Target:
    """One class of sampled checks: the measured rule of its target, its kind of evidence, its builds and its label."""

    rule: str
    kind: Kind
    builds: Builds
    label: str = ""


STRESS = Target("stress-passes", Kind.REPETITION, Builds.REPEATED)
PROPERTY = Target("property-search", Kind.SEARCH, Builds.FUZZ, "fuzz_property")
MEASUREMENT = Target("bench-runs", Kind.MEASUREMENT, Builds.BENCH)
TARGETS = (STRESS, PROPERTY, MEASUREMENT)
