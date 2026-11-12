"""registry — each row of the registry, and each file or test that a row names, agrees with the tree.

THE PARTS
    The rule has seven parts.  A module of this directory holds each part,
    and its docstring tells what the part refuses:
      shape    registry_shape    each row of the registry obeys its schema, and each doc field names a section of
                                 the guide
      modules  registry_modules  each rule has one function, each function has one rule, and each holder names a
                                 function that the witness runs
      witness  registry_witness  each rule has its plants, and each plants directory has a rule
      tests    registry_tests    each rule has its test with the label ci_guard, each test that runs a master
                                 has its label, its fixture, its launcher and its variables, and each part of a
                                 rule that runs in parts has its test
      kinds    registry_kinds    the kinds that the quarantine plugin gives are the kinds of the plugin rows
      ledgers  registry_ledgers  each file of utils/ledgers/ is the ledger of one rule, and obeys its policy
      layout   registry_layout   each file of utils/ has its path row and obeys its naming rule, and each path row
                                 holds a file
    A part module holds no function findings and no table RULES, so the part
    modules finds no rule in it.  The run of the rule gives each finding of
    each part with the ID of the rule.

THE BUILD
    The parts tests and kinds read a build: the tests that ctest registers,
    and the kinds that the quarantine plugin writes.  With no build
    directory, the run does the other parts.  It writes one line that names
    the parts that it did not do, and it gives no finding of them.  A part
    that does not apply for a different reason, such as a build that ctest
    cannot read, gives its reason in that line.  When no part applies, the
    rule does not apply.  `guard write --rule registry` refuses a write when
    a part does not apply (unread_parts()), because the write would remove
    the rows of that part.

THE REFUSED KINDS
    The parts shape, kinds and ledgers give an error for each finding, and no
    ledger row admits a finding of their kinds.  The three holder kinds of
    the part modules, and the three kinds of the parts of a rule of the part
    tests, are refused kinds too (REFUSED_KINDS).  The ledger admits a
    finding of each other kind with a reason.

THE PLANTS
    The name of each plant and control names its part after the prefix, such
    as plant-shape-bad-doc or control-tests-registered.  plant_context() runs
    only that part on the plant, with the context of that part: the guide
    guide.md for the part shape, the configured CMake project for the part
    tests, and the build directory planted-build for the part kinds.  The
    witness thus shows that each part refuses each of its plants.  The rule
    does not apply to a plant whose name names no part, and the witness of
    that plant fails.

Complexity: the sum of the complexity of the parts.
"""

from collections.abc import Callable
from dataclasses import dataclass, replace
from pathlib import Path

from assay.core import report as check_report
from assay.guard.engines.meta import (
    registry_kinds,
    registry_layout,
    registry_ledgers,
    registry_modules,
    registry_shape,
    registry_tests,
    registry_witness,
)
from assay.guard.engines.meta.runner import PREFIXES, MetaContext, NotApplicable, copy_tree

RULE = "registry"
# The kinds that no ledger row admits (THE REFUSED KINDS).
REFUSED_KINDS = (
    registry_shape.KINDS
    | registry_kinds.KINDS
    | registry_ledgers.KINDS
    | registry_modules.HOLDER_KINDS
    | registry_tests.PART_KINDS
)


@dataclass(frozen=True, slots=True)
class Part:
    """One part of the rule (THE PARTS).

    `findings` gives the findings of the part, and it raises NotApplicable
    when the part does not apply.  `plant_context` gives the context of a
    plant of the part.  `reads_build` is True for a part that reads a build
    (THE BUILD).  `probe` raises NotApplicable when the part does not apply
    to a context, with no other work, and it is None for a part that always
    applies.
    """

    name: str
    findings: Callable[[MetaContext], list[check_report.Finding]]
    plant_context: Callable[[Path, Path], MetaContext] = copy_tree
    reads_build: bool = False
    probe: Callable[[MetaContext], object] | None = None


PARTS = (
    Part(registry_shape.PART, registry_shape.shape_findings, registry_shape.plant_context),
    Part(registry_modules.PART, registry_modules.module_findings),
    Part(registry_witness.PART, registry_witness.witness_findings),
    Part(
        registry_tests.PART,
        registry_tests.tests_findings,
        registry_tests.plant_context,
        reads_build=True,
        probe=registry_tests.registered_tests,
    ),
    Part(
        registry_kinds.PART,
        registry_kinds.kinds_findings,
        registry_kinds.plant_context,
        reads_build=True,
        probe=registry_kinds.kinds_path,
    ),
    Part(registry_ledgers.PART, registry_ledgers.ledgers_findings),
    Part(registry_layout.PART, registry_layout.layout_findings, probe=MetaContext.tracked),
)
PARTS_BY_NAME = {part.name: part for part in PARTS}


class PartContext(MetaContext):
    """The context of a plant of one part (THE PLANTS): the context that the part gives, and the name of the part."""

    __slots__ = ("part_name",)

    def __init__(self, context: MetaContext, part_name: str) -> None:
        """Keep the root, the build directory and the guide of the context of the part, and the name of the part."""
        super().__init__(context.root, context.build_dir, context.guide)
        self.part_name = part_name


def selected_parts(context: MetaContext) -> tuple[Part, ...]:
    """Return the parts that a run reads: the part of a plant (THE PLANTS), else each part."""
    if isinstance(context, PartContext):
        return (PARTS_BY_NAME[context.part_name],)
    return PARTS


def part_of(name: str) -> Part | None:
    """Return the part that the name of a plant or a control names after its prefix, or None (THE PLANTS)."""
    for prefix in PREFIXES:
        if name.startswith(prefix):
            return PARTS_BY_NAME.get(name.removeprefix(prefix).split("-", 1)[0])
    return None


def unread_text(unread: list[tuple[str, str | None]]) -> str:
    """Return the line that names the parts that a run did not do, with the reason of each one (THE BUILD).

    A reason of None tells that the part reads a build and the run has no build directory.
    """
    with_build = [name for name, reason in unread if reason is None]
    sentences = []
    if with_build:
        names = " and ".join(with_build)
        sentences.append(f"the run did not do the parts {names}, because they read a build.  Give --build-dir DIR")
    sentences += [f"the run did not do the part {name}: {reason}" for name, reason in unread if reason is not None]
    return f"{RULE}: {'.  '.join(sentences)}"


def findings(context: MetaContext) -> list[check_report.Finding]:
    """Give the findings of each part that the run reads, with the ID of the rule (THE PARTS).

    Raises:
        NotApplicable: If no part that the run reads applies (THE BUILD)
    """
    selected = selected_parts(context)
    found: list[check_report.Finding] = []
    unread: list[tuple[str, str | None]] = []
    for part in selected:
        if part.reads_build and context.build_dir is None:
            unread.append((part.name, None))
            continue
        try:
            found += [replace(item, check=RULE) for item in part.findings(context)]
        except NotApplicable as reason:
            unread.append((part.name, str(reason)))
    if unread and len(unread) == len(selected):
        raise NotApplicable(unread_text(unread).removeprefix(f"{RULE}: "))
    if unread:
        check_report.write_line(unread_text(unread))
    return found


def unread_parts(context: MetaContext) -> list[str]:
    """Return the name of each part that does not apply to a context, with its reason (THE BUILD).

    `guard write` reads this list before it runs the rule.  Complexity: one probe of each part with a probe.
    """
    unread: list[str] = []
    for part in PARTS:
        if part.reads_build and context.build_dir is None:
            unread.append(f"{part.name} (the part reads a build)")
            continue
        if part.probe is None:
            continue
        try:
            part.probe(context)
        except NotApplicable as reason:
            unread.append(f"{part.name} ({reason})")
    return unread


def plant_context(plant: Path, scratch: Path) -> MetaContext:
    """Give the context of the part that the name of a plant names, with that part alone (THE PLANTS).

    Raises:
        NotApplicable: If the name of the plant names no part, or the part cannot prepare the plant
    """
    part = part_of(plant.name)
    if part is None:
        raise NotApplicable(
            f"the plant {plant.name} names no part of the rule {RULE} after its prefix.  The parts are "
            f"{', '.join(PARTS_BY_NAME)}"
        )
    return PartContext(part.plant_context(plant, scratch), part.name)
