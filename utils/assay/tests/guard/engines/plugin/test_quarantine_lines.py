"""The test of the payment of the rule quarantine-lines, on planted findings of the engine plugin.

THE CASES
    A case gives unpaid() planted findings on added lines, the findings that
    the change took away and the moved lines.  The plants of the rule hold the
    compiled cases (utils/plants/quarantine-lines/).  These cases hold:
      * A hunk that removes lines pays from the findings of its file, with the
        same entity or an entity that agrees
      * A moved line pays from the pool of the change, also with an entity
        that agrees, and a line that did not move gets nothing
      * The same entity pays before an entity that agrees.
"""

from collections import Counter

from assay.core import git, testing
from assay.guard.engines.plugin import quarantine_lines
from assay.readers import sections as quarantine_sections

KIND = "std-entity"
OLD = "foundation::algebra::read(foundation::algebra::node)"
NEW = "fixy::session::read(fixy::session::node)"
# A hunk that removes one line and adds two, and a hunk that only adds two lines.
REMOVING = git.Hunk(1, 1, 1, 2)
ADDING = git.Hunk(0, 0, 1, 2)

type Added = dict[quarantine_lines.LineKey, list[tuple[quarantine_sections.Finding, git.Hunk]]]


def added_of(path: str, hunk: git.Hunk, *entities: str) -> Added:
    """Return one planted finding on each added line of a hunk, with the entities in the sequence of the lines."""
    added: Added = {}
    for line, entity in enumerate(entities, start=hunk.new_start):
        found = quarantine_sections.Finding(path, line, 1, "quarantine", KIND, entity)
        added.setdefault((path, KIND, entity), []).append((found, hunk))
    return added


def refused_lines(
    added: Added, own: dict[quarantine_lines.LineKey, int], pool: Counter[quarantine_lines.PoolKey], moves: git.Moves
) -> list[int]:
    """Return the line of each added finding that unpaid() refuses."""
    return [entry.item.line for entry in quarantine_lines.unpaid(added, own, pool, moves)]


def hunk_cases(expect: testing.Expect) -> None:
    """A hunk that removes lines pays from the findings that the change took away from its file."""
    expect(
        "a hunk that removes lines pays an added finding with the same entity",
        refused_lines(added_of("a.h", REMOVING, NEW), {("a.h", KIND, NEW): 1}, Counter(), git.Moves()) == [],
    )
    expect(
        "a hunk that removes lines pays an entity that agrees, as a change of the namespace of a line gives",
        refused_lines(added_of("a.h", REMOVING, NEW), {("a.h", KIND, OLD): 1}, Counter(), git.Moves()) == [],
    )
    expect(
        "a hunk that removes lines pays nothing with an entity that does not agree",
        refused_lines(added_of("a.h", REMOVING, "std::vector"), {("a.h", KIND, OLD): 1}, Counter(), git.Moves()) == [1],
    )


def move_cases(expect: testing.Expect) -> None:
    """A moved line pays from the pool of the change, and a line that did not move gets nothing."""
    moved = git.Moves(lines={"b.h": frozenset({1})})
    pool = Counter({(KIND, OLD): 1})
    expect(
        "a moved line pays from the pool with an entity that agrees",
        refused_lines(added_of("b.h", ADDING, NEW), {}, pool, moved) == [],
    )
    expect(
        "a line that did not move gets nothing from the pool",
        refused_lines(added_of("b.h", ADDING, NEW, NEW), {}, pool, moved) == [2],
    )
    expect(
        "the pool of the caller does not change",
        pool == Counter({(KIND, OLD): 1}),
    )


def sequence_cases(expect: testing.Expect) -> None:
    """The same entity pays before an entity that agrees, also when the agreeing finding comes first."""
    exact = "fixy::alpha::read(int)"
    agreeing = "fixy::gamma::read(int)"
    added = added_of("c.h", REMOVING, agreeing, exact)
    expect(
        "the finding with the same entity takes the one credit, and the finding that only agrees is refused",
        refused_lines(added, {("c.h", KIND, exact): 1}, Counter(), git.Moves()) == [1],
    )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (hunk_cases, move_cases, sequence_cases))
