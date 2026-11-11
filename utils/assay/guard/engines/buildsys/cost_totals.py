"""cost totals — the instruction total of a build of the base configuration stays near its baseline.

THE RULE instruction-total
    The total is the sum of the user instructions of each unit of the census
    of assay.readers.census, which BuildContext.census() reads.  A unit is an
    object of the target all or a negative fixture.  The count of a unit is
    the count of the compile that made its output.  An object gets its count
    from the record of the build launcher, and a fixture from its own record.
    A compile with no ccache and a ccache miss give the same count
    (utils/assay/core/meter.py, A COMPILE THROUGH CCACHE).

    The record of a ccache hit keeps the count of the compile that made its
    object, from the cost store (utils/assay/core/cost_store.py).  A build
    directory with a hit for each compile then has the total of a cold build.
    The count covers the template instantiation and the constant evaluation.
    The load of the host and the caches do not change it.  Two clean builds
    of one tree have sums with a difference of less than 0.01 %.

THE CONFIGURATION
    The total changes with the configuration of the build
    (BuildContext.configuration(), assay.core.configuration, THE
    CONFIGURATION).  A bench target and a fuzz target add their objects, and
    a Release build compiles other code.  The rule judges only a build of the
    configuration BASE_CONFIGURATION, which the preset default gives.  It
    does not apply to a build of a different configuration, and it then reads
    no ledger and no census.

THE LEDGER
    utils/ledgers/instruction-total.txt holds no configuration row, and it
    holds two total rows:
        - | x86_64-debug-asan | INSTRUCTIONS | objects | REASON
        - | x86_64-debug-asan | INSTRUCTIONS | fixtures | REASON
    The KIND cell holds BASE_CONFIGURATION, and the two rows give one reason.
    The module owns the head comment of the ledger,
    INSTRUCTION_LEDGER_COMMENT.  Each write gives the ledger that comment,
    and a different head comment goes (assay.core.ledger, THE HEAD COMMENT).
    A change of the rule then changes the comment in the same commit.

THE FINDINGS
    The measured rule instruction-total of the registry gives the two
    thresholds, in percent of the baseline.  The rule gives:
      growth         a total more than the baseline by more than the warning
                     threshold gives a warning, and by more than the error
                     threshold an error.  The message names the largest
                     compiles
      zero-baseline  a baseline of 0 gives an error, because no change has a
                     measure against it
    A total less than the baseline gives no finding (THE POLICIES of
    assay.core.ledger).  The baseline then stays more than the total until a
    write lowers it.

    The rule gives an error of one of these kinds for an input that it
    cannot use:
      no-kind        the build has no build kind
      configuration  the configuration of the build cannot be read
      no-threshold   the registry has no measured rule instruction-total
      no-ledger      the ledger file is not there
      ledger-row     a row does not obey the rows grammar or THE LEDGER
      census         an input of the census is there and cannot be read
      no-count       a unit has no instruction count, and the units do not
                     show a host with no counter
    The entity of each finding is BASE_CONFIGURATION.  Before the
    configuration is known, it is the build kind, and before the kind is
    known, the ID of the rule.

    A unit with no count is a hit, and the cost store does not hold the
    compile of its object.  Or it is a compile with no count, and a different
    unit has a count.  The message names the units and the command that
    compiles them again with the compiler.  The rule does not apply when no
    unit has an instruction count and a compile ran with no count.  The host
    then gives no counter.

THE WRITE
    `guard write --rule instruction-total --build-dir DIR` writes the two
    rows again from a build of BASE_CONFIGURATION.  The write refuses a
    growth of more than the warning threshold with no --reason TEXT.  A write
    with no reason keeps the last reason, and a write with a reason gives the
    two rows that reason.

    The rule and the write do not apply to a call with no build directory.
    They also do not apply to a build that the census does not read:
      * a build of a generator that is not Ninja
      * a build of only some targets
      * a build directory where no fixture ran.

Complexity: linear in the units, and O(u log u) for the sort of u units.
"""

from collections.abc import Callable, Mapping
from dataclasses import dataclass, replace
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import ledger as rule_ledger
from assay.core import meter as cost_meter
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import census as build_census
from assay.readers import compdb

INSTRUCTION_RULE = "instruction-total"
# The one configuration that the rule judges: the build of the preset default (THE CONFIGURATION).
BASE_CONFIGURATION = "x86_64-debug-asan"
# The entity of each of the two total rows of the ledger.
CENSUS_PARTS = ("objects", "fixtures")
# The number of compiles that a finding of growth names.
CENSUS_NAMED = 8
CENSUS_DEFAULT_REASON = "the measure at the last write of the ledger"
INSTRUCTION_GIGA = 10**9
# The head comment of the ledger, which each write gives the ledger (THE LEDGER).
INSTRUCTION_LEDGER_COMMENT = (
    "# The ledger of the rule instruction-total: the baseline of the user instructions of the compiles of a build\n"
    f"# of the configuration {BASE_CONFIGURATION}, the build of the preset default.\n"
    "# `python3 utils/bin/guard explain instruction-total` tells the rule, and\n"
    "# `python3 utils/bin/guard write --rule instruction-total --build-dir BUILD` writes the two rows again from a\n"
    "# build of that configuration.  The rule does not apply to a build of a different configuration.\n"
    "#\n"
    "# The two rows:\n"
    f"#   - | {BASE_CONFIGURATION} | INSTRUCTIONS | objects | REASON\n"
    f"#   - | {BASE_CONFIGURATION} | INSTRUCTIONS | fixtures | REASON\n"
    "#   The sum, over each object of the target all and over each negative fixture, of the user instructions\n"
    "#   of the compile that made it, at the last write.  The record of a ccache hit keeps the count of the\n"
    "#   compile that made its object, from the cost store of utils/assay/core/cost_store.py.  A result of the\n"
    "#   store of the fixture driver keeps the count of the compile that made it.  A direct compile and a\n"
    "#   ccache miss give the same count (utils/assay/core/meter.py, A COMPILE THROUGH CCACHE).\n"
    "#\n"
    "# A growth over the warning threshold of the measured rule instruction-total of utils/registry.txt gives\n"
    "# a warning, and over its error threshold an error.  A total less than the baseline gives no finding, and\n"
    "# a write with no reason lowers the baseline.  The count covers the template instantiation and the\n"
    "# constant evaluation.  To accept a growth, raise the baseline in the same commit with a reason:\n"
    "#   python3 utils/bin/guard write --rule instruction-total --build-dir build --reason TEXT\n"
)


class CensusRefusal(rule_ledger.LedgerError):
    """An input that the rule cannot use: the build kind, the configuration, the thresholds, the ledger or the census.

    The refusal is an error finding of the rule, at a path and a line, with
    a kind and an entity (THE FINDINGS).  It is also a LedgerError.  Then
    `guard write` tells it and writes no ledger.
    """

    def __init__(self, message: str, path: str, line: int, kind: str, entity: str) -> None:
        """Keep the message and the position, the kind and the entity of the finding."""
        super().__init__(message)
        self.path = path
        self.line = line
        self.kind = kind
        self.entity = entity

    def finding(self, rule_id: str) -> check_report.Finding:
        """Return the refusal as the error finding of a rule."""
        return check_report.Finding("error", self.path, self.line, rule_id, str(self), self.kind, self.entity)


@dataclass(frozen=True, slots=True)
class CensusTotal:
    """The two total rows of the ledger: the line of the first row, the two sums and their one reason."""

    line: int
    objects: int
    fixtures: int
    reason: str

    @property
    def value(self) -> int:
        """The sum of the two totals."""
        return self.objects + self.fixtures


@dataclass(frozen=True, slots=True)
class CensusFrame:
    """The facts that the rule reads before the census: the rule, the configuration, the thresholds and the ledger."""

    rule: rule_registry.Rule
    configuration: build_config.BuildConfiguration
    budget: check_report.Budget
    ledger: rule_ledger.Ledger
    build_shown: str

    @property
    def name(self) -> str:
        """The name of the configuration of the build, BASE_CONFIGURATION."""
        return self.configuration.name

    @property
    def shown(self) -> str:
        """The ledger of the rule, relative to the root."""
        return rule_ledger.shown_path(self.rule.id)

    def write_command(self) -> str:
        """Return the command that writes the two rows again from the build."""
        return f"python3 utils/bin/guard write --rule {self.rule.id} --build-dir {self.build_shown}"

    def refusal(self, message: str, line: int) -> CensusRefusal:
        """Return the refusal of a row of the ledger at a line."""
        return CensusRefusal(message, self.shown, line, "ledger-row", self.name)


def instruction_text(count: int) -> str:
    """Return a number of instructions in G of 10^9."""
    return f"{count / INSTRUCTION_GIGA:.1f} G"


def census_frame(context: BuildContext, given: rule_ledger.Ledger | None = None) -> CensusFrame:
    """Read the rule, the configuration, the thresholds and the ledger of the call (THE CONFIGURATION).

    The check of the configuration comes before the read of the thresholds and of the ledger.  A build of a different
    configuration then gives no finding.

    Args:
        context: The context of the engine
        given: The ledger that `guard write` read, or None to read the ledger of the tree

    Raises:
        NotApplicable: If the call names no rule or gives no build directory, the build made only some targets, or
            the build has a configuration other than BASE_CONFIGURATION
        CensusRefusal: If the build has no kind, or its configuration cannot be read.  Also if the registry has no
            measured rule, or the ledger cannot be used
    """
    rule = context.rule
    if rule is None:
        raise meta_runner.NotApplicable("the call names no rule of the census")
    build = context.require_build()
    try:
        kind = context.kind()
    except meta_runner.NotApplicable as reason:
        raise CensusRefusal(
            f"{reason}.  Configure the build again", context.shown(build / cost_meter.KIND_FILE), 0, "no-kind", rule.id
        ) from None
    try:
        configuration = context.configuration()
    except (build_census.CensusError, compdb.CompdbError, build_config.ConfigurationError) as problem:
        raise CensusRefusal(
            f"the configuration of the build cannot be read, and the rule judges only a build of the configuration "
            f"{BASE_CONFIGURATION}: {problem}",
            context.shown(build),
            0,
            "configuration",
            kind,
        ) from None
    if configuration.name != BASE_CONFIGURATION:
        raise meta_runner.NotApplicable(
            f"the rule judges only a build of the configuration {BASE_CONFIGURATION}, which the preset default "
            f"gives, and the build has the configuration {configuration.name}"
        )
    try:
        budget = context.budget(rule.id)
    except meta_runner.NotApplicable as reason:
        raise CensusRefusal(
            f"{reason}, and the rule reads its warning threshold and its error threshold there.  Give the row of the "
            f"rule its thresholds",
            rule_registry.REGISTRY_PATH,
            0,
            "no-threshold",
            configuration.name,
        ) from None
    ledger = given if given is not None else census_ledger(context.root, rule, configuration.name)
    return CensusFrame(rule, configuration, budget, ledger, context.shown(build))


def census_ledger(root: Path, rule: rule_registry.Rule, entity: str) -> rule_ledger.Ledger:
    """Read the ledger of the rule from the tree.

    Raises:
        CensusRefusal: If the file is not there, cannot be read, or does not obey the rows grammar
    """
    shown = rule_ledger.shown_path(rule.id)
    if not rule_ledger.ledger_path(rule.id, root).is_file():
        raise CensusRefusal(
            f"{shown} is not there.  It holds the two baseline rows of the configuration {BASE_CONFIGURATION}.  "
            f"Write it with `python3 utils/bin/guard write --rule {rule.id} --build-dir DIR`",
            shown,
            0,
            "no-ledger",
            entity,
        )
    try:
        return rule_ledger.read(rule, root)
    except rows_grammar.RowsError as problem:
        raise CensusRefusal(str(problem), shown, problem.line, "ledger-row", entity) from None
    except OSError as problem:
        raise CensusRefusal(f"{shown} cannot be read ({problem})", shown, 0, "ledger-row", entity) from None


def census_read(context: BuildContext, frame: CensusFrame) -> build_census.Census:
    """Read the census of the build: its units and the cost of each one.

    Raises:
        NotApplicable: If the census does not apply to the build
        CensusRefusal: If an input of the census is there and cannot be read
    """
    try:
        return context.census()
    except build_census.CensusError as problem:
        raise CensusRefusal(str(problem), frame.build_shown, 0, "census", frame.name) from None


def census_baseline(frame: CensusFrame) -> CensusTotal | None:
    """Read the two total rows of the ledger (THE LEDGER), or give None for a ledger with no row.

    Complexity: O(r log r) for r rows.

    Raises:
        CensusRefusal: If the ledger has a configuration row, or a row that is not a total row of BASE_CONFIGURATION.
            Also if it has one total row, or two rows with two reasons
    """
    if frame.ledger.configurations:
        raise frame.refusal(
            f"{frame.shown}: the ledger holds the configuration row of {frame.ledger.configurations[0].name}.  The "
            f"rule judges only the configuration {BASE_CONFIGURATION}, and its ledger holds no configuration row.  "
            f"Remove each configuration row",
            0,
        )
    rows: dict[str, rule_ledger.Entry] = {}
    for entry in sorted(frame.ledger.base.values(), key=lambda item: item.line):
        value = entry.value
        is_whole = isinstance(value, int) or (isinstance(value, float) and value.is_integer())
        if entry.key.path or entry.key.entity not in CENSUS_PARTS or not is_whole or not entry.reason:
            raise frame.refusal(
                f"{frame.shown}:{entry.line}: a row is `- | {BASE_CONFIGURATION} | VALUE | objects | REASON` or "
                f"`- | {BASE_CONFIGURATION} | VALUE | fixtures | REASON`, with a whole number and a reason",
                entry.line,
            )
        if entry.key.kind != BASE_CONFIGURATION:
            raise frame.refusal(
                f"{frame.shown}:{entry.line}: the row names the configuration {entry.key.kind}.  The rule judges "
                f"only the configuration {BASE_CONFIGURATION}, so the row judges no build.  Remove the row",
                entry.line,
            )
        rows[entry.key.entity] = entry
    return census_total(frame, rows) if rows else None


def census_total(frame: CensusFrame, rows: Mapping[str, rule_ledger.Entry]) -> CensusTotal:
    """Return the total of the ledger from its total rows.

    Raises:
        CensusRefusal: If the ledger has one total row, or its two rows give two reasons
    """
    first = min(rows.values(), key=lambda entry: entry.line)
    if set(rows) != set(CENSUS_PARTS):
        raise frame.refusal(
            f"{frame.shown}:{first.line}: the ledger has a total row of {first.key.entity} and no other.  The "
            f"ledger has a total row of objects and one of fixtures",
            first.line,
        )
    if len({entry.reason for entry in rows.values()}) != 1:
        raise frame.refusal(
            f"{frame.shown}:{first.line}: the two total rows give two reasons.  The two rows give one reason",
            first.line,
        )
    return CensusTotal(first.line, int(rows["objects"].value), int(rows["fixtures"].value), first.reason)


def census_judged(
    frame: CensusFrame,
    total: CensusTotal,
    value: int,
    said: str,
    named: Callable[[], str],
) -> list[check_report.Finding]:
    """Compare the total of the build with the total rows (the kinds growth and zero-baseline).

    A total less than its rows gives no finding (THE POLICIES of assay.core.ledger), and a write lowers the rows.

    Args:
        frame: The rule, the configuration and the thresholds
        total: The total rows of the ledger
        value: The total of the build
        said: The words that tell the total of the build
        named: The words that name the largest compiles of a growth
    """
    rule_id, write = frame.rule.id, frame.write_command()
    if total.value <= 0:
        return [
            check_report.Finding(
                "error",
                frame.shown,
                total.line,
                rule_id,
                f"{said}, and the baseline is 0.  No change has a measure against a baseline of 0.  Write the "
                f"baseline from this build in the same commit: {write}",
                "zero-baseline",
                frame.name,
            )
        ]
    change = (value - total.value) / total.value * 100.0
    level = check_report.classify(change, frame.budget)
    if level is None:
        return []
    limit = frame.budget.error if level == "error" else frame.budget.warn
    message = (
        f"{said}, {change:.2f}% more than the baseline of {instruction_text(total.value)}, over the {level} "
        f"threshold {limit:g}%.  Remove the cost, or raise the baseline in the same commit with a reason: {write} "
        f"--reason TEXT.  {named()}"
    )
    return [check_report.Finding(level, frame.shown, total.line, rule_id, message, "growth", frame.name)]


def census_change_text(value: int, baseline: int) -> str:
    """Return the change of a value against its baseline as words, for the summary line of the rule."""
    if baseline > 0:
        percent = (value - baseline) / baseline * 100.0
        return f"{percent:+.2f}% against the baseline"
    return "with no change that a baseline of 0 can measure"


def census_refused(frame: CensusFrame, old: CensusTotal | None, value: int, reason: str | None) -> None:
    """Refuse a write that raises the total by more than the warning threshold with no reason.

    Raises:
        LedgerError: If the write has no reason and the total increased by more than the warning threshold
    """
    if old is None or old.value <= 0 or reason:
        return
    growth = (value - old.value) / old.value * 100.0
    if growth > frame.budget.warn:
        raise rule_ledger.LedgerError(
            f"the total increased by {growth:.2f}% over the baseline of {instruction_text(old.value)}, more than the "
            f"warning threshold {frame.budget.warn:g}%.  Give the reason of the growth with --reason TEXT"
        )


def census_total_entries(total: CensusTotal) -> list[rule_ledger.Entry]:
    """Return the two total rows of a total, each with the KIND BASE_CONFIGURATION."""
    return [
        rule_ledger.Entry(rule_ledger.Key("", BASE_CONFIGURATION, part), count, total.reason)
        for part, count in zip(CENSUS_PARTS, (total.objects, total.fixtures), strict=True)
    ]


def census_new_total(old: CensusTotal | None, objects: int, fixtures: int, reason: str | None) -> CensusTotal:
    """Return the new total: the reason of the write, else the last reason, else the default."""
    kept = old.reason if old is not None else CENSUS_DEFAULT_REASON
    return CensusTotal(0, objects, fixtures, reason or kept)


def census_unit_counts(units: list[build_census.Unit], kind: str) -> int:
    """Return the number of units of one kind of unit: "object" or "fixture"."""
    return sum(1 for unit in units if unit.kind == kind)


def instruction_counts(frame: CensusFrame, census: build_census.Census) -> tuple[int, int]:
    """Return the user instructions of the compiles that made the objects and the fixtures (THE FINDINGS).

    Raises:
        NotApplicable: If no unit has an instruction count, and a compile ran with no count
        CensusRefusal: If a unit has no instruction count, and the units do not show a host with no counter
    """
    uncounted = [unit for unit in census.units if unit.cost.instructions is None]
    # A unit with a CPU time and no count ran with no counter.  A unit with no CPU time has no known compile.
    ran = [unit for unit in uncounted if unit.cost.cpu_s is not None]
    if ran and len(uncounted) == len(census.units):
        raise meta_runner.NotApplicable(
            f"no compile of the {len(census.units)} units has an instruction count, and {len(ran)} of them ran with "
            f"no count, because the host gives no exact counter, or {cost_meter.INSTRUCTIONS_ENV} is 0 "
            f"(utils/assay/core/meter.py, THE INSTRUCTION COUNT)"
        )
    if uncounted:
        stored = [unit.item for unit in uncounted if unit.kind == "object" and unit.cost.cpu_s is None]
        bare = [unit.item for unit in ran if unit.kind == "object"]
        fixtures = [unit.item for unit in uncounted if unit.kind == "fixture"]
        build = frame.build_shown
        recache = f"env CCACHE_RECACHE=1 cmake --build {build} --clean-first"
        parts = []
        if stored:
            parts.append(
                f"The objects with no known compile ({len(stored)}): {build_census.names_text(stored)}.  The cost "
                f"store holds no cost of the compile that made such an object (utils/assay/core/cost_store.py): "
                f"ccache stored the object before the store had its cost, or the store dropped the cost.  Compile "
                f"each object again with the compiler: {recache}"
            )
        if bare:
            parts.append(
                f"The objects that ran with no count ({len(bare)}): {build_census.names_text(bare)}.  The host gave "
                f"no exact count of such a compile, or the compile ran with {cost_meter.INSTRUCTIONS_ENV}=0.  Compile "
                f"each object again: {recache}"
            )
        if fixtures:
            parts.append(
                f"The fixtures with no count ({len(fixtures)}): {build_census.names_text(fixtures)}.  Compile each "
                f"again with no result store: env CRUCIBLE_NEG_CACHE=0 ctest --test-dir {build} -R '^neg_'"
            )
        raise CensusRefusal(
            f"{len(uncounted)} of the {len(census.units)} compiles have no instruction count, so the sum is not the "
            f"cost of the build.  " + ".  ".join(parts),
            build,
            0,
            "no-count",
            frame.name,
        )
    sums = {"object": 0, "fixture": 0}
    for unit in census.units:
        sums[unit.kind] += unit.cost.instructions or 0
    return sums["object"], sums["fixture"]


def instruction_largest(census: build_census.Census) -> str:
    """Return the compiles with the most instructions as words, for a finding of growth."""
    ordered = sorted(census.units, key=lambda unit: (-(unit.cost.instructions or 0), unit.source))
    named = "; ".join(
        f"{unit.source} {instruction_text(unit.cost.instructions or 0)}" for unit in ordered[:CENSUS_NAMED]
    )
    return f"The largest compiles: {named}"


def instruction_findings(context: BuildContext) -> list[check_report.Finding]:
    """Compare the instruction total of a build of BASE_CONFIGURATION with the total rows (THE FINDINGS)."""
    try:
        frame = census_frame(context)
        total = census_baseline(frame)
        if total is None:
            raise frame.refusal(
                f"{frame.shown}: the ledger holds no total row of the configuration {BASE_CONFIGURATION}.  Write "
                f"the rows from this build: {frame.write_command()}",
                0,
            )
        census = census_read(context, frame)
        objects, fixtures = instruction_counts(frame, census)
    except CensusRefusal as refusal:
        return [refusal.finding(INSTRUCTION_RULE)]
    said = (
        f"the compiles of the build ran {instruction_text(objects + fixtures)} user instructions (objects "
        f"{instruction_text(objects)}, fixtures {instruction_text(fixtures)})"
    )
    found = census_judged(frame, total, objects + fixtures, said, lambda: instruction_largest(census))
    check_report.write_line(
        f"{INSTRUCTION_RULE}: {instruction_text(objects + fixtures)} user instructions in "
        f"{census_unit_counts(census.units, 'object')} objects and {census_unit_counts(census.units, 'fixture')} "
        f"fixtures of the configuration {frame.name}, {census_change_text(objects + fixtures, total.value)}"
    )
    return found


def instruction_written(context: BuildContext, ledger: rule_ledger.Ledger, reason: str | None) -> rule_ledger.Ledger:
    """Write the two total rows again from a build of BASE_CONFIGURATION (THE WRITE).

    Raises:
        NotApplicable: If the census does not apply to the build, the build has a different configuration, or the
            host gives no counter
        LedgerError: If an input cannot be used, or a unit has no instruction count.  Also if the write has no
            reason for a growth over the warning threshold
    """
    frame = census_frame(context, ledger)
    old = census_baseline(frame)
    objects, fixtures = instruction_counts(frame, census_read(context, frame))
    census_refused(frame, old, objects + fixtures, reason)
    entries = census_total_entries(census_new_total(old, objects, fixtures, reason))
    return replace(ledger, comment=INSTRUCTION_LEDGER_COMMENT, base={entry.key: entry for entry in entries})


# The keys are text, because assay.guard.engines.meta.runner reads the keys of the table with no import.
RULES = {
    "instruction-total": meta_runner.Member(instruction_findings, instruction_written),
}
