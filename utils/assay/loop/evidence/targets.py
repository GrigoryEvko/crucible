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


@dataclass(frozen=True, slots=True)
class Target:
    """One class of sampled checks: the measured rule of its target, its kind of evidence and what the class holds."""

    rule: str
    kind: Kind
    text: str


STRESS = Target("stress-passes", Kind.REPETITION, "each test of the build of a test preset with the field repeat")
TARGETS = (STRESS,)
