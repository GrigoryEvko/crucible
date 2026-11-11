"""targets — the classes of the sampled checks of the tree, and the two targets of each class.

THE SAMPLED CHECKS
    One run of a test with a result that does not change is full evidence
    for its key.  A sampled check gives only a part of its evidence in one
    run.  The evidence of each run then adds to the evidence of the previous
    runs.  Each class of sampled checks has one kind of evidence (Kind), a
    name, a unit and two targets (TARGETS):
      repetition   the passes of a test at its key.  A test preset with the
                   field `repeat` runs each test of its build again, to find
                   a race
      search       the CPU seconds of a search at its key, and the inputs
                   that the search found
      measurement  the valid measurements of a program at its key.  A
                   measurement counts only on a quiet host
      coverage     the runs of the gate mutants on each header of include/,
                   at the key of that header
    The nightly target is the evidence that the nightly entry gets for each
    item.  The release target is the evidence that the release entry gets.
    The store keeps the evidence of a class with its name
    (assay.loop.evidence.store).  `loop evidence status` gives each item
    against the two targets (assay.loop.evidence.status).

    A row of TARGETS is data only.  assay.loop.evidence.items gives the items
    of each class and their keys, and the runner of each kind adds evidence.

THE ITEMS OF A CLASS
    The items of a class of tests are the tests with its label in its builds:
      Builds.REPEATED  the build of each test preset with the field `repeat`.
                       A test is an item only when its program is an ELF
                       file of the build tree.  A repetition looks for a race
                       in a program of the tree (assay.loop.evidence.items)
      Builds.FUZZ      the build of each configure preset that builds the
                       fuzzers (assay.loop.tiers, PresetTier)
    A class with no label takes each test of its builds.  The items of the
    class of the benches are the bench targets of its builds:
      Builds.BENCH     the build of each configure preset that builds the
                       benches for a measurement: with no fuzzers and no
                       profile counters (assay.loop.tiers, PresetTier)
    The items of the class of coverage are the headers of include/:
      Builds.MUTANTS   the build of the configure preset MUTANT_PRESET.  Its
                       units give the key of each header of include/
                       (assay.loop.evidence.items, THE KEYS OF A HEADER)
"""

import enum
from dataclasses import dataclass

# The configure preset of the build that gives the keys of the headers (THE ITEMS OF A CLASS).
MUTANT_PRESET = "default"


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
    MUTANTS = "mutants"


@dataclass(frozen=True, slots=True)
class Target:
    """One class of sampled checks: its name, its kind of evidence, its builds, its unit, its two targets and its label.

    `rule` is the name of the class, and the store keeps the evidence of the class with it (THE SAMPLED CHECKS).
    """

    rule: str
    kind: Kind
    builds: Builds
    unit: str
    nightly: float
    release: float
    label: str = ""


# The passes of each test of the build of a test preset with the field `repeat`, at the key of the test.  The nightly
# entry gets 10 passes, and the release entry gets 50.  A failure at a key stays until the key changes.  Run `loop
# evidence run repetition`, or repair the race of the failure that it keeps.
STRESS = Target("stress-passes", Kind.REPETITION, Builds.REPEATED, "passes", 10.0, 50.0)
# The CPU seconds of the search of each test with the label fuzz_property, at the key of the test.  The nightly entry
# gets 60 s, and the release entry gets 600 s.  A failure at a key stays until the key changes.  Run `loop evidence run
# search`, or repair the defect that the kept seed shows.
PROPERTY = Target("property-search", Kind.SEARCH, Builds.FUZZ, "s", 60.0, 600.0, "fuzz_property")
# The runs that count of each bench target, at the key of its program and of the host.  The build of a configure
# preset that builds the benches for a measurement gives the targets.  A run counts only on a quiet host and with a
# small variation.  The nightly entry gets 3 runs, and the release entry gets 10.  A failure at a key stays until the
# key changes.  Run `loop evidence run measurement` on a quiet host, or repair the defect of the failure that it keeps.
MEASUREMENT = Target("bench-runs", Kind.MEASUREMENT, Builds.BENCH, "runs", 3.0, 10.0)
# The runs of the gate mutants on each header of include/ of the build of the preset default, at the key of the
# header.  One run of each mutant of a header at its key is full evidence, and the two entries get 1 run.  A survivor
# is a failure at the key.  An invalid mutant that no row of ADMITTED_GATES of assay.loop.evidence.items admits is a
# failure too.  The failure stays until the key changes.  Run `loop evidence run coverage`, or add the test that the
# kept mutant fails.
COVERAGE = Target("mutant-runs", Kind.COVERAGE, Builds.MUTANTS, "runs", 1.0, 1.0)
TARGETS = (STRESS, PROPERTY, MEASUREMENT, COVERAGE)
