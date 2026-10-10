"""cost totals — the census totals of a build stay near the baseline rows of its configuration.

THE CENSUS TOTALS
    The rules parse-total, header-fanout and instruction-total read the census
    of assay.readers.census through BuildContext.census().  A unit is an
    object of the target all or a negative fixture.  The census gives the
    files that each unit reads, and the cost of the last compile of the unit
    that ran the compiler.  A ccache hit and a result of the fixture store
    keep their files and their cost.  The totals then do not change with the
    caches or with the load of the host.

    Each rule judges its own ledger, which has the policy baseline.  The
    measured rule of the registry gives the two thresholds of the rule, in
    percent of the baseline.  Each rule gives an error of one of these kinds
    for an input that it cannot use:
      no-kind        the build has no build kind
      no-threshold   the registry has no measured rule with the ID of the rule
      no-ledger      the ledger file is not there
      ledger-row     a row does not obey the rows grammar or the shape of the
                     rule, or it names a configuration with no configuration rows
      configuration  the configuration of the build cannot be read
      census         an input of the census is there and cannot be read
      no-count       a unit has no instruction count, and the units do not
                     show a host with no counter (THE RULE instruction-total)
    The entity of such a finding is the name of the configuration.  Before
    the configuration is known, it is the build kind, and before the kind is
    known, the ID of the rule.

THE CONFIGURATION
    The totals change with the configuration of the build
    (BuildContext.configuration(), assay.core.configuration, THE
    CONFIGURATION).  A bench target and a fuzz target add their objects.  A
    compile with -march=native compiles the arms of the processor of the
    host.  The configuration x86_64-debug-asan holds 4 such compiles of all,
    and the configuration x86_64-release holds each compile.  The KIND cell of
    a row holds the name of a configuration, and each ledger holds the facts
    of each of its configurations:
        configuration NAME KEY VALUE
    A row that names a configuration with no such rows is a ledger-row error.

    THE POLICY of assay.core.configuration judges each build, and no rule
    skips a build because of its configuration:
      * the rows of the configuration with the name of the build judge it.
        A fact of the configuration that the build does not share gives one
        line
      * a build of a name that the ledger does not hold has no baseline of
        its own.  The configuration with the largest total judges a growth
        of its totals.  A fall is no finding, because the build can compile
        fewer units.  The rule header-fanout judges such a build against the
        largest row of each header.  It then gives no warning of a fall or
        of a header with no sentinel.
    A write of a build with a configuration that the ledger does not hold
    writes nothing and fails, unless it gives --add-configuration.

    A rule does not apply to a call with no build directory.  It also does
    not apply to a build that the census does not read:
      * a build of a generator that is not Ninja
      * a build of only some targets
      * a build directory where no fixture ran.

THE RULE parse-total
    The total is the sum of the bytes of each file that each unit reads.  A
    total can increase while each budget of one job holds.  One more include
    in a base header adds a small cost to each of thousands of compiles.  On
    192 cores the wall time of a build is not less than its total CPU time
    divided by 192.  The total then sets a limit on the wall time of the
    build.

    utils/ledgers/parse-total.txt holds these rows of each configuration:
        - | KIND | BYTES | objects | REASON
        - | KIND | BYTES | fixtures | REASON
        PATH | KIND | BYTES/READERS | - | -
        - | KIND | BYTES/READERS | NAME | -
    The two total rows give the two sums at the last write, with one reason.
    A file row gives the bytes of one file and the number of units that read
    it.  The ledger has a file row for each file with bytes times readers of
    0.05 % of the total or more.  PATH is a path of the tree, or @build/PATH
    for a file of the build directory.  NAME is <system>/PATH for a different
    file (assay.readers.census.file_key).

    The rule gives:
      growth         a total more than the baseline by more than the warning
                     threshold gives a warning, and by more than the error
                     threshold an error
      fall           a total less than the baseline by more than the warning
                     threshold gives an error.  Write the baseline again in
                     the same commit, and the baseline does not stay more
                     than the total
      zero-baseline  a baseline of 0 gives an error, because no change has a
                     measure against it
    The entity is the name of the configuration.  A finding of growth or fall
    names the files with the largest change of their bytes times readers.  A
    file with no row counts as zero in the baseline.  `guard write --rule
    parse-total --build-dir DIR` writes the configuration rows and the rows of
    the configuration of the build again, and it keeps the rows of the other
    configurations.  The write refuses a growth of more than the warning
    threshold with no --reason TEXT.

    Complexity: linear in the total length of the file lists, and O(f log f)
    for the sort of f files.

THE RULE header-fanout
    The cost of a header of include/ is its bytes alone times the units that
    read it.  A sentinel of test/layer compiles each header alone.  The bytes alone are the files of the
    sentinel after its source, as for the rule header-alone.  A check file
    that includes more than its header counts those files too.  The rule
    prints the ten headers with the largest cost, the list that a developer
    reads first.

    utils/ledgers/header-fanout.txt holds the 30 headers with the largest
    cost of each configuration at the last write:
        HEADER | KIND | ALONE/READERS | - | -

    The rule gives warnings only:
      top-entry    a header in the ten largest costs of the build that is not
                   in the ten largest of the baseline
      growth       a header with a cost more than its row by more than the
                   threshold
      fall         a header with a cost less than its row by more than the
                   threshold.  Write the baseline again
      no-sentinel  a header of the baseline with no sentinel in the build.
                   Write the baseline again
    The entity is the header.  `guard write --rule header-fanout --build-dir
    DIR` writes the configuration rows and the rows of the configuration of
    the build again, and it keeps the rows of the other configurations.

    Complexity: linear in the total length of the file lists, and O(h log h)
    for the sort of h headers.

THE RULE instruction-total
    The total is the sum of the user instructions of each unit.  The count of
    a unit is the count of the compile that made its output.  An object gets
    its count from the record of the build launcher, and a fixture from its
    own record.  A compile with no ccache and a ccache miss give the same
    count (utils/assay/core/meter.py, A COMPILE THROUGH CCACHE).  The record
    of a ccache hit keeps the count of the compile that made its object, from
    the cost store (utils/assay/core/cost_store.py).  A build directory with
    a hit for each compile then has the total of a cold build.  The count covers
    the template instantiation and the constant evaluation that the bytes do
    not show.  The count does not change with the load of the host.  Two
    clean builds of one tree have sums with a difference of less than 0.01 %.

    utils/ledgers/instruction-total.txt holds the two total rows of each
    configuration:
        - | KIND | INSTRUCTIONS | objects | REASON
        - | KIND | INSTRUCTIONS | fixtures | REASON

    The rule gives the kinds growth, fall and zero-baseline of the rule
    parse-total, for the instruction total.  The entity is the name of the
    configuration.  A finding of growth names the largest compiles.  The rule
    does not apply when no unit has an instruction count and a compile ran
    with no count.  The host then gives no counter.  Each other unit with no
    count is an error of the kind no-count.  Such a unit is a hit, and the
    cost store does not hold the compile of its object.  Or it is a compile
    with no count, and a different unit has a count.  The message names the
    units and the command that compiles them again with the compiler.
    `guard write --rule instruction-total --build-dir DIR` writes the
    configuration rows and the two total rows of the configuration of the
    build again.  The write refuses a growth of more than the warning
    threshold with no --reason TEXT.

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

# The entity of each of the two total rows of a configuration.
CENSUS_PARTS = ("objects", "fixtures")
# The prefix of a file of the build directory: in the names of build_census.file_key, and in the PATH of a row.
CENSUS_BUILD_NAME = "<build>/"
CENSUS_BUILD_PATH = "@build/"
# The number of files or compiles that a finding names.
CENSUS_NAMED = 8
CENSUS_DEFAULT_REASON = "the measure at the last write of the ledger"
# The units of a number of bytes in a message: the name, the bytes of one unit and the digits after the point.
CENSUS_BYTE_UNITS = (("GB", 1 << 30, 2), ("MB", 1 << 20, 1), ("KB", 1 << 10, 1))
PARSE_RULE = "parse-total"
# A file row has bytes times readers of this part of the total or more, at the last write.
PARSE_FILE_FLOOR = 0.0005
FANOUT_RULE = "header-fanout"
# The part of the path of an object of a sentinel of test/layer, which compiles one header alone.
FANOUT_SENTINEL = "/layer_sentinel_"
FANOUT_TOP = 10
FANOUT_KEPT = 30
INSTRUCTION_RULE = "instruction-total"
INSTRUCTION_GIGA = 10**9


class CensusRefusal(rule_ledger.LedgerError):
    """An input that a census rule cannot use: the build kind, the thresholds, the ledger or the census.

    The refusal is an error finding of the rule, at a path and a line, with
    a kind and an entity (THE CENSUS TOTALS).  It is also a LedgerError.
    Then `guard write` tells it and writes no ledger.
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
    """The two total rows of one configuration: the line of the first row, the two sums and their one reason."""

    line: int
    objects: int
    fixtures: int
    reason: str

    @property
    def value(self) -> int:
        """The sum of the two totals."""
        return self.objects + self.fixtures


@dataclass(frozen=True, slots=True)
class CensusBaseline:
    """The rows of a ledger of totals: the totals and the bytes and readers of each file, by the configuration."""

    totals: Mapping[str, CensusTotal]
    files: Mapping[str, Mapping[str, tuple[int, int]]]


@dataclass(frozen=True, slots=True)
class CensusFrame:
    """The facts that a census rule reads before the census: the rule, the configuration, the thresholds, the ledger.

    `judged` gives the rows that judge the build (THE POLICY of assay.core.configuration).
    """

    rule: rule_registry.Rule
    configuration: build_config.BuildConfiguration
    budget: check_report.Budget
    ledger: rule_ledger.Ledger
    build_shown: str
    judged: build_config.Judging

    @property
    def name(self) -> str:
        """The name of the configuration of the build, which the KIND cell of its rows holds."""
        return self.configuration.name

    @property
    def shown(self) -> str:
        """The ledger of the rule, relative to the root."""
        return rule_ledger.shown_path(self.rule.id)

    @property
    def is_bound(self) -> bool:
        """True when the ledger does not hold the configuration of the build, and the largest rows judge it."""
        return self.judged.name == rule_ledger.MAXIMUM

    def write_command(self) -> str:
        """Return the command that writes the baseline of the configuration of the build again."""
        words = " --add-configuration" if self.is_bound else ""
        return f"python3 utils/bin/guard write --rule {self.rule.id} --build-dir {self.build_shown}{words}"

    def refusal(self, message: str, line: int) -> CensusRefusal:
        """Return the refusal of a row of the ledger at a line."""
        return CensusRefusal(message, self.shown, line, "ledger-row", self.name)

    def announce(self) -> None:
        """Write the line of the facts that the build does not share with the configuration of its rows, when it has one."""
        if self.judged.note:
            check_report.write_line(self.judged.note)


def census_bytes_text(count: int) -> str:
    """Return a number of bytes in GB, MB or KB of 2^30, 2^20 or 2^10 bytes."""
    for name, scale, digits in CENSUS_BYTE_UNITS[:-1]:
        if count >= scale:
            return f"{count / scale:.{digits}f} {name}"
    name, scale, digits = CENSUS_BYTE_UNITS[-1]
    return f"{count / scale:.{digits}f} {name}"


def instruction_text(count: int) -> str:
    """Return a number of instructions in G of 10^9."""
    return f"{count / INSTRUCTION_GIGA:.1f} G"


def census_frame(context: BuildContext, given: rule_ledger.Ledger | None = None) -> CensusFrame:
    """Read the rule, the configuration, the thresholds and the ledger of the call (THE CENSUS TOTALS).

    Args:
        context: The context of the engine
        given: The ledger that `guard write` read, or None to read the ledger of the tree

    Raises:
        NotApplicable: If the call names no rule or gives no build directory, or the build made only some targets
        CensusRefusal: If the build has no kind, or the registry has no measured rule.  Also if the ledger cannot be
            used, or the configuration of the build cannot be read
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
        budget = context.budget(rule.id)
    except meta_runner.NotApplicable as reason:
        raise CensusRefusal(
            f"{reason}, and the rule reads its warning threshold and its error threshold there.  Give the row of the "
            f"rule its thresholds",
            rule_registry.REGISTRY_PATH,
            0,
            "no-threshold",
            kind,
        ) from None
    ledger = given if given is not None else census_ledger(context.root, rule, kind)
    try:
        configuration = context.configuration()
    except (build_census.CensusError, compdb.CompdbError, build_config.ConfigurationError) as problem:
        raise CensusRefusal(
            f"the configuration of the build cannot be read, and the rows of the ledger belong to a configuration: "
            f"{problem}",
            context.shown(build),
            0,
            "configuration",
            kind,
        ) from None
    judged = build_config.judging(rule.id, ledger, configuration)
    return CensusFrame(rule, configuration, budget, ledger, context.shown(build), judged)


def census_written_configurations(context: BuildContext, frame: CensusFrame) -> tuple[rule_ledger.Configuration, ...]:
    """Return the configurations that a write keeps, with the configuration of the build (THE POLICY).

    A ledger with no configuration gets the configuration of the build as its base.

    Raises:
        LedgerError: If the ledger does not hold the configuration of the build, and the write does not add it
    """
    target = build_config.write_target(frame.rule.id, frame.ledger, frame.configuration, context.add_configuration)
    if isinstance(target, str):
        raise rule_ledger.LedgerError(target)
    if target.name is None:
        return (rule_ledger.Configuration(frame.name, frame.configuration.facts),)
    return target.ledger.configurations


def census_ledger(root: Path, rule: rule_registry.Rule, kind: str) -> rule_ledger.Ledger:
    """Read the ledger of a census rule from the tree.

    Raises:
        CensusRefusal: If the file is not there, cannot be read, or does not obey the rows grammar
    """
    shown = rule_ledger.shown_path(rule.id)
    if not rule_ledger.ledger_path(rule.id, root).is_file():
        raise CensusRefusal(
            f"{shown} is not there.  It holds the baseline rows of each configuration.  Write it with "
            f"`python3 utils/bin/guard write --rule {rule.id} --build-dir DIR`",
            shown,
            0,
            "no-ledger",
            kind,
        )
    try:
        return rule_ledger.read(rule, root)
    except rows_grammar.RowsError as problem:
        raise CensusRefusal(str(problem), shown, problem.line, "ledger-row", kind) from None
    except OSError as problem:
        raise CensusRefusal(f"{shown} cannot be read ({problem})", shown, 0, "ledger-row", kind) from None


def census_read(context: BuildContext, frame: CensusFrame, read_files: bool) -> build_census.Census:
    """Read the census of the build, with the files of each unit when read_files.

    Raises:
        NotApplicable: If the census does not apply to the build
        CensusRefusal: If an input of the census is there and cannot be read
    """
    try:
        return context.census(read_files)
    except build_census.CensusError as problem:
        raise CensusRefusal(str(problem), frame.build_shown, 0, "census", frame.name) from None


def census_name_of(key: rule_ledger.Key) -> str:
    """Return the name of the file of a file row, as build_census.file_key gives it."""
    if key.path.startswith(CENSUS_BUILD_PATH):
        return f"{CENSUS_BUILD_NAME}{key.path.removeprefix(CENSUS_BUILD_PATH)}"
    return key.path if key.path else key.entity


def census_key_of(name: str, kind: str) -> rule_ledger.Key:
    """Return the key of the row of a file of build_census.file_key.

    A file of the tree has its path in PATH, and a file of the build
    directory has @build/PATH there.  A different file has no PATH, and its
    name is the entity.
    """
    if name.startswith(CENSUS_BUILD_NAME):
        return rule_ledger.Key(CENSUS_BUILD_PATH + name.removeprefix(CENSUS_BUILD_NAME), kind, "")
    is_outside = name.startswith("<") or Path(name).is_absolute()
    return rule_ledger.Key("" if is_outside else name, kind, name if is_outside else "")


def census_file_row(frame: CensusFrame, entry: rule_ledger.Entry, shape: str) -> tuple[str, int, int]:
    """Return the name, the bytes and the readers of a file row.

    Raises:
        CensusRefusal: If the kind, the value, the reason or the cells of the row do not have the shape of a file
            row
    """
    name = census_name_of(entry.key)
    counts = entry.value.split("/") if isinstance(entry.value, str) else []
    is_file_row = len(counts) == 2 and bool(name) and not entry.reason
    if not census_is_kind(entry.key.kind) or not is_file_row or census_key_of(name, entry.key.kind) != entry.key:
        raise frame.refusal(
            f"{frame.shown}:{entry.line}: {shape}, with the name of a configuration in lowercase words", entry.line
        )
    census_named(frame, entry)
    return name, int(counts[0]), int(counts[1])


def census_is_kind(kind: str) -> bool:
    """Return True when a text has the shape of the name of a configuration: a build kind and its option words."""
    return rule_registry.is_rule_id(kind.replace("_", "-"))


def census_named(frame: CensusFrame, entry: rule_ledger.Entry) -> None:
    """Make sure that the KIND cell of a row names a configuration of the ledger (THE CONFIGURATION).

    Raises:
        CensusRefusal: If the ledger has no configuration row of the name
    """
    if frame.ledger.configuration(entry.key.kind) is None:
        raise frame.refusal(
            f"{frame.shown}:{entry.line}: the row names the configuration {entry.key.kind}, and the ledger has no "
            f"configuration row of it.  Write the rows of a build of that configuration with "
            f"`python3 utils/bin/guard write --rule {frame.rule.id} --build-dir DIR`",
            entry.line,
        )


def census_baseline(frame: CensusFrame) -> CensusBaseline:
    """Read the total rows and the file rows of each configuration from the ledger of parse-total or instruction-total.

    Complexity: O(r log r) for r rows.

    Raises:
        CensusRefusal: If a total row or a file row does not have its shape, or it names a configuration with no
            configuration row.  Also if a configuration has one total row or two reasons
    """
    parts: dict[str, dict[str, rule_ledger.Entry]] = {}
    files: dict[str, dict[str, tuple[int, int]]] = {}
    for entry in sorted(frame.ledger.base.values(), key=lambda item: item.line):
        if entry.key.path or entry.key.entity not in CENSUS_PARTS:
            name, size, readers = census_file_row(
                frame,
                entry,
                "a file row is `PATH | KIND | BYTES/READERS | - | -`, or `- | KIND | BYTES/READERS | "
                "NAME | -` for a file outside the tree and the build directory",
            )
            files.setdefault(entry.key.kind, {})[name] = (size, readers)
            continue
        value = entry.value
        is_whole = isinstance(value, int) or (isinstance(value, float) and value.is_integer())
        if not census_is_kind(entry.key.kind) or not is_whole or not entry.reason:
            raise frame.refusal(
                f"{frame.shown}:{entry.line}: a total row is `- | KIND | VALUE | objects | REASON` or `- | KIND | "
                f"VALUE | fixtures | REASON`, with the name of a configuration, a whole number and a reason",
                entry.line,
            )
        census_named(frame, entry)
        parts.setdefault(entry.key.kind, {})[entry.key.entity] = entry
    return CensusBaseline({kind: census_total(frame, kind, rows) for kind, rows in parts.items()}, files)


def census_total(frame: CensusFrame, name: str, rows: Mapping[str, rule_ledger.Entry]) -> CensusTotal:
    """Return the total of one configuration from its total rows.

    Raises:
        CensusRefusal: If the configuration has one total row, or its two rows give two reasons
    """
    first = min(rows.values(), key=lambda entry: entry.line)
    if set(rows) != set(CENSUS_PARTS):
        raise frame.refusal(
            f"{frame.shown}:{first.line}: the configuration {name} has a total row of {first.key.entity} and no "
            f"other.  A configuration has a total row of objects and one of fixtures",
            first.line,
        )
    if len({entry.reason for entry in rows.values()}) != 1:
        raise frame.refusal(
            f"{frame.shown}:{first.line}: the two total rows of the configuration {name} give two reasons.  The two "
            f"rows of a configuration give one reason",
            first.line,
        )
    return CensusTotal(first.line, int(rows["objects"].value), int(rows["fixtures"].value), first.reason)


def census_rows_name(frame: CensusFrame, costs: Mapping[str, int]) -> str:
    """Return the configuration of the rows that judge the build (THE CONFIGURATION).

    `costs` gives a cost of the rows of each configuration.  A build of a configuration that the ledger does not hold
    gets the configuration with the largest cost.

    Raises:
        CensusRefusal: If the ledger has no rows that can judge the build
    """
    if frame.is_bound:
        name = max(costs, key=lambda item: (costs[item], item)) if costs else None
    else:
        name = frame.judged.name or frame.ledger.base_name
    if name is None or name not in costs:
        held = f"the configuration {name}" if name is not None else "a configuration"
        raise frame.refusal(
            f"{frame.shown}: the ledger holds no rows of {held} that can judge the build of the configuration "
            f"{frame.name}.  Write the rows of the configuration of the build: {frame.write_command()}",
            0,
        )
    return name


def census_words(frame: CensusFrame, name: str) -> str:
    """Return the words that name the rows that judge the build, for the summary line of a rule."""
    if frame.is_bound:
        return (
            f"the largest baseline, of the configuration {name}, because the ledger holds no configuration {frame.name}"
        )
    return f"the baseline of the configuration {name}"


def census_total_of(frame: CensusFrame, totals: Mapping[str, CensusTotal]) -> tuple[str, CensusTotal]:
    """Return the configuration and the total of the rows that judge the build (THE CONFIGURATION).

    Raises:
        CensusRefusal: If the ledger has no total row that can judge the build
    """
    name = census_rows_name(frame, {key: total.value for key, total in totals.items()})
    return name, totals[name]


def census_judged(
    frame: CensusFrame,
    total: CensusTotal,
    value: int,
    said: str,
    shown_value: Callable[[int], str],
    named: Callable[[bool], str],
) -> list[check_report.Finding]:
    """Compare a total of the build with the total row that judges it (the kinds growth, fall and zero-baseline).

    A build of a configuration that the ledger does not hold gives no fall (THE CONFIGURATION).

    Args:
        frame: The rule, the configuration and the thresholds
        total: The total row that judges the build
        value: The total of the build
        said: The words that tell the total of the build
        shown_value: The text of a total in its unit
        named: The words that name the largest changes, for a growth when the argument is True, else for a fall.  An
            empty text names nothing
    """
    rule_id, write = frame.rule.id, frame.write_command()
    if total.value <= 0:
        return [
            check_report.Finding(
                "error",
                frame.shown,
                total.line,
                rule_id,
                f"{said}, and the baseline that judges the build is 0.  No change has a measure against a baseline of "
                f"0.  Write the baseline from this build in the same commit: {write}",
                "zero-baseline",
                frame.name,
            )
        ]
    change = (value - total.value) / total.value * 100.0
    level = check_report.classify(change, frame.budget)
    if level is not None:
        limit = frame.budget.error if level == "error" else frame.budget.warn
        advice = (
            f"Remove the cost, or write the rows of the configuration of the build: {write}"
            if frame.is_bound
            else f"Remove the cost, or raise the baseline in the same commit with a reason: {write} --reason TEXT"
        )
        message = (
            f"{said}, {change:.2f}% more than the baseline of {shown_value(total.value)}, over the {level} threshold "
            f"{limit:g}%.  {advice}"
        )
        kind, is_growth = "growth", True
    elif -change > frame.budget.warn and not frame.is_bound:
        level = "error"
        kind, is_growth = "fall", False
        message = (
            f"{said}, {-change:.2f}% less than the baseline of {shown_value(total.value)}, more than the "
            f"{frame.budget.warn:g}% that the baseline can hold.  Lower the baseline in the same commit, so that it "
            f"does not stay above the total: {write}"
        )
    else:
        return []
    extra = named(is_growth)
    return [
        check_report.Finding(
            level, frame.shown, total.line, rule_id, f"{message}.  {extra}" if extra else message, kind, frame.name
        )
    ]


def census_change_text(value: int, baseline: int) -> str:
    """Return the change of a value against its baseline as words, for the summary line of a rule."""
    if baseline > 0:
        percent = (value - baseline) / baseline * 100.0
        return f"{percent:+.2f}% against the baseline"
    return "with no change that a baseline of 0 can measure"


def census_refused(
    frame: CensusFrame, old: CensusTotal | None, value: int, reason: str | None, shown_value: Callable[[int], str]
) -> None:
    """Refuse a write that raises a total by more than the warning threshold with no reason.

    Raises:
        LedgerError: If the write has no reason and the total increased by more than the warning threshold
    """
    if old is None or old.value <= 0 or reason:
        return
    growth = (value - old.value) / old.value * 100.0
    if growth > frame.budget.warn:
        raise rule_ledger.LedgerError(
            f"the total increased by {growth:.2f}% over the baseline of {shown_value(old.value)}, more than the "
            f"warning threshold {frame.budget.warn:g}%.  Give the reason of the growth with --reason TEXT"
        )


def census_total_entries(totals: Mapping[str, CensusTotal]) -> list[rule_ledger.Entry]:
    """Return the two total rows of each configuration."""
    return [
        rule_ledger.Entry(rule_ledger.Key("", name, part), count, total.reason)
        for name, total in totals.items()
        for part, count in zip(CENSUS_PARTS, (total.objects, total.fixtures), strict=True)
    ]


def census_new_total(old: CensusTotal | None, objects: int, fixtures: int, reason: str | None) -> CensusTotal:
    """Return the new total of a configuration: the reason of the write, else the last reason, else the default."""
    kept = old.reason if old is not None else CENSUS_DEFAULT_REASON
    return CensusTotal(0, objects, fixtures, reason or kept)


def census_unit_counts(units: list[build_census.Unit], kind: str) -> int:
    """Return the number of units of one kind of unit: "object" or "fixture"."""
    return sum(1 for unit in units if unit.kind == kind)


# ── parse-total ───────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class ParseMeasure:
    """The two byte totals of one build, the units of each total, and the bytes and readers of each file."""

    objects: int
    fixtures: int
    object_units: int
    fixture_units: int
    files: Mapping[str, tuple[int, int]]

    @property
    def value(self) -> int:
        """The sum of the two totals."""
        return self.objects + self.fixtures

    def said(self) -> str:
        """Return the words that tell the totals."""
        return (
            f"the compiles of the build read {census_bytes_text(self.value)} ({self.value} bytes: objects "
            f"{census_bytes_text(self.objects)} in {self.object_units} units, fixtures "
            f"{census_bytes_text(self.fixtures)} in {self.fixture_units} units)"
        )


def parse_measure(census: build_census.Census, root: Path) -> ParseMeasure:
    """Add the bytes of each unit, and find the bytes and the readers of each file.

    Complexity: linear in the total length of the file lists.
    """
    totals = {"object": 0, "fixture": 0}
    for unit in census.units:
        totals[unit.kind] += census.unit_bytes(unit)
    files: dict[str, tuple[int, int]] = {}
    for real, readers in census.readers().items():
        name = build_census.file_key(real, root, census.build_dir)
        size, earlier = files.get(name, (census.sizes[real], 0))
        files[name] = (size, earlier + readers)
    return ParseMeasure(
        totals["object"],
        totals["fixture"],
        census_unit_counts(census.units, "object"),
        census_unit_counts(census.units, "fixture"),
        files,
    )


def parse_changes(
    now: Mapping[str, tuple[int, int]], before: Mapping[str, tuple[int, int]], is_growth: bool, floor: int
) -> str:
    """Return the files with the largest change of bytes times readers, in the direction of the change of the total.

    Complexity: O(f log f) for f files.
    """
    moved: list[tuple[int, str]] = []
    for name in set(now) | set(before):
        size, readers = now.get(name, (0, 0))
        old_size, old_readers = before.get(name, (0, 0))
        delta = size * readers - old_size * old_readers
        if delta != 0 and (delta > 0) == is_growth:
            moved.append((-abs(delta), name))
    parts: list[str] = []
    for negative, name in sorted(moved)[:CENSUS_NAMED]:
        size, readers = now.get(name, (0, 0))
        if name in before:
            was = f"was {census_bytes_text(before[name][0])} x {before[name][1]}"
        else:
            was = f"under the floor of {census_bytes_text(floor)} at the last write"
        sign = "+" if is_growth else "-"
        parts.append(
            f"{name} {census_bytes_text(size)} x {readers} readers = {census_bytes_text(size * readers)} "
            f"({sign}{census_bytes_text(-negative)}, {was})"
        )
    return f"The largest changes: {'; '.join(parts) if parts else 'no file row changed'}"


def parse_findings(context: BuildContext) -> list[check_report.Finding]:
    """Compare the byte total of the build with the baseline of its configuration (THE RULE parse-total)."""
    try:
        frame = census_frame(context)
        baseline = census_baseline(frame)
        name, total = census_total_of(frame, baseline.totals)
        measure = parse_measure(census_read(context, frame, True), context.root)
    except CensusRefusal as refusal:
        return [refusal.finding(PARSE_RULE)]
    frame.announce()
    before = baseline.files.get(name, {})
    floor = int(total.value * PARSE_FILE_FLOOR)
    found = census_judged(
        frame,
        total,
        measure.value,
        measure.said(),
        census_bytes_text,
        lambda is_growth: parse_changes(measure.files, before, is_growth, floor),
    )
    check_report.write_line(
        f"{PARSE_RULE}: {measure.value} bytes ({census_bytes_text(measure.value)}) in {measure.object_units} objects "
        f"and {measure.fixture_units} fixtures of the configuration {frame.name}, "
        f"{census_change_text(measure.value, total.value)}: {census_words(frame, name)}"
    )
    return found


def parse_written(context: BuildContext, ledger: rule_ledger.Ledger, reason: str | None) -> rule_ledger.Ledger:
    """Write the configuration rows, the total rows and the file rows of the configuration of the build again.

    The write keeps the rows of the other configurations.

    Raises:
        NotApplicable: If the census does not apply to the build
        LedgerError: If an input cannot be used, or the write has no reason for a growth over the warning threshold
    """
    frame = census_frame(context, ledger)
    configurations = census_written_configurations(context, frame)
    baseline = census_baseline(frame)
    measure = parse_measure(census_read(context, frame, True), context.root)
    old = baseline.totals.get(frame.name)
    census_refused(frame, old, measure.value, reason, census_bytes_text)
    totals = {**baseline.totals, frame.name: census_new_total(old, measure.objects, measure.fixtures, reason)}
    floor = measure.value * PARSE_FILE_FLOOR
    kept = {name: counts for name, counts in measure.files.items() if counts[0] * counts[1] >= floor}
    files = {**baseline.files, frame.name: kept}
    entries = census_total_entries(totals)
    entries += [
        rule_ledger.Entry(census_key_of(name, configuration), f"{size}/{readers}", "")
        for configuration in totals
        for name, (size, readers) in files.get(configuration, {}).items()
    ]
    return replace(ledger, configurations=configurations, base={entry.key: entry for entry in entries})


# ── header-fanout ─────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class FanoutHeader:
    """One header that a sentinel compiles alone: the bytes that it includes alone and the units that read it."""

    header: str
    alone: int
    readers: int

    @property
    def cost(self) -> int:
        """The bytes alone times the readers."""
        return self.alone * self.readers

    def said(self) -> str:
        """Return the cost of the header as words."""
        return f"{census_bytes_text(self.alone)} alone x {self.readers} readers = {census_bytes_text(self.cost)}"


@dataclass(frozen=True, slots=True)
class FanoutRow:
    """One row of the fan-out ledger: its line, and the bytes alone and the readers at the last write."""

    line: int
    alone: int
    readers: int

    @property
    def cost(self) -> int:
        """The bytes alone times the readers."""
        return self.alone * self.readers


def fanout_header_of(unit: build_census.Unit, include_root: str) -> str | None:
    """Return the path of the header of a sentinel object, as the census gives it, or None for a different unit.

    The header is the first file of the list after the source that is in include/.
    """
    if unit.kind != "object" or FANOUT_SENTINEL not in f"/{unit.item}":
        return None
    return next((path for path in unit.files[1:] if path.startswith(include_root)), None)


def fanout_headers(census: build_census.Census, root: Path) -> list[FanoutHeader]:
    """Find the bytes alone and the readers of each header that a sentinel compiles, the largest cost first.

    Complexity: linear in the total length of the file lists, and O(h log h) for the sort of h headers.
    """
    readers = census.readers()
    include_root = f"{root / 'include'}/"
    found: dict[str, FanoutHeader] = {}
    for unit in census.units:
        header = fanout_header_of(unit, include_root)
        if header is not None:
            name = build_census.file_key(header, root, census.build_dir)
            found[name] = FanoutHeader(name, sum(census.sizes[path] for path in unit.files[1:]), readers[header])
    return sorted(found.values(), key=lambda item: (-item.cost, item.header))


def fanout_baseline(frame: CensusFrame) -> dict[str, dict[str, FanoutRow]]:
    """Read the rows of each configuration of the fan-out ledger.

    Complexity: O(r log r) for r rows.

    Raises:
        CensusRefusal: If a row is malformed, or it has a count that is not positive
    """
    rows: dict[str, dict[str, FanoutRow]] = {}
    for entry in sorted(frame.ledger.base.values(), key=lambda item: item.line):
        header, alone, readers = census_file_row(frame, entry, "a row is `HEADER | KIND | ALONE/READERS | - | -`")
        if alone <= 0 or readers <= 0:
            raise frame.refusal(
                f"{frame.shown}:{entry.line}: the header {header} has a count that is not positive", entry.line
            )
        rows.setdefault(entry.key.kind, {})[header] = FanoutRow(entry.line, alone, readers)
    return rows


def fanout_top_entries(headers: list[FanoutHeader], rows: Mapping[str, FanoutRow]) -> list[check_report.Finding]:
    """Give a warning for each header in the top list of the build that is not in the top list of the baseline."""
    old_top = {name for name, _ in sorted(rows.items(), key=lambda item: (-item[1].cost, item[0]))[:FANOUT_TOP]}
    return [
        check_report.Finding(
            "warning",
            item.header,
            0,
            FANOUT_RULE,
            f"the header enters the top {FANOUT_TOP} of the fan-out at rank {rank}: {item.said()}.  Each unit that "
            f"reads the header pays the bytes that it includes.  Remove an include from it, or move code that needs a "
            f"heavy include to a source file.  When the cost stays, write the baseline again",
            "top-entry",
            item.header,
        )
        for rank, item in enumerate(headers[:FANOUT_TOP], start=1)
        if item.header not in old_top
    ]


def fanout_row_changes(
    frame: CensusFrame, headers: list[FanoutHeader], rows: Mapping[str, FanoutRow]
) -> list[check_report.Finding]:
    """Give a warning for each row with no sentinel, and for each header with a cost change over the threshold.

    A build of a configuration that the ledger does not hold gives only the warnings of a growth (THE CONFIGURATION).
    """
    now = {item.header: item for item in headers}
    found: list[check_report.Finding] = []
    for name, row in sorted(rows.items(), key=lambda item: item[1].line):
        item = now.get(name)
        was = f"was {census_bytes_text(row.alone)} alone x {row.readers} readers"
        if item is None and frame.is_bound:
            continue
        if item is None:
            found.append(
                check_report.Finding(
                    "warning",
                    frame.shown,
                    row.line,
                    FANOUT_RULE,
                    f"the build has no sentinel of {name}, which the baseline holds.  Write the baseline again: "
                    f"{frame.write_command()}",
                    "no-sentinel",
                    name,
                )
            )
        elif (item.cost - row.cost) / row.cost * 100.0 > frame.budget.warn:
            found.append(
                check_report.Finding(
                    "warning",
                    name,
                    0,
                    FANOUT_RULE,
                    f"the fan-out of the header increased by {(item.cost - row.cost) / row.cost * 100.0:.1f}%, over the "
                    f"threshold {frame.budget.warn:g}%: {item.said()} ({was}).  Remove an include from it, or write the "
                    f"baseline again when the cost stays: {frame.write_command()}",
                    "growth",
                    name,
                )
            )
        elif (row.cost - item.cost) / row.cost * 100.0 > frame.budget.warn and not frame.is_bound:
            found.append(
                check_report.Finding(
                    "warning",
                    frame.shown,
                    row.line,
                    FANOUT_RULE,
                    f"the fan-out of {name} fell by {(row.cost - item.cost) / row.cost * 100.0:.1f}%, more than the "
                    f"threshold {frame.budget.warn:g}%: {item.said()} ({was}).  Write the baseline again, so that it does "
                    f"not stay above the cost: {frame.write_command()}",
                    "fall",
                    name,
                )
            )
    return found


def fanout_table(headers: list[FanoutHeader]) -> str:
    """Return the headers with the largest costs as lines of text, for the output of the rule."""
    lines = [f"{FANOUT_RULE}: the {FANOUT_TOP} headers whose bytes alone times readers are the largest:"]
    lines += [
        f"  {rank:2d}. {census_bytes_text(item.cost):>9}  {census_bytes_text(item.alone):>9} alone x "
        f"{item.readers:5d} readers  {item.header}"
        for rank, item in enumerate(headers[:FANOUT_TOP], start=1)
    ]
    return "\n".join(lines)


def fanout_judging_rows(frame: CensusFrame, rows: Mapping[str, Mapping[str, FanoutRow]]) -> dict[str, FanoutRow]:
    """Return the rows that judge the build: of the configuration of its rows, or the largest row of each header.

    The largest row of each header judges a build of a configuration that the ledger does not hold (THE
    CONFIGURATION).  Complexity: linear in the rows.

    Raises:
        CensusRefusal: If the ledger has no rows that can judge the build
    """
    name = census_rows_name(frame, {key: sum(row.cost for row in named.values()) for key, named in rows.items()})
    if not frame.is_bound:
        return dict(rows[name])
    largest: dict[str, FanoutRow] = {}
    for named in rows.values():
        for header, row in named.items():
            if header not in largest or row.cost > largest[header].cost:
                largest[header] = row
    return largest


def fanout_findings(context: BuildContext) -> list[check_report.Finding]:
    """Compare the fan-out of the build with the baseline rows of its configuration (THE RULE header-fanout)."""
    try:
        frame = census_frame(context)
        judging_rows = fanout_judging_rows(frame, fanout_baseline(frame))
        headers = fanout_headers(census_read(context, frame, True), context.root)
    except CensusRefusal as refusal:
        return [refusal.finding(FANOUT_RULE)]
    frame.announce()
    found = fanout_top_entries(headers, judging_rows) + fanout_row_changes(frame, headers, judging_rows)
    check_report.write_line(fanout_table(headers))
    words = (
        f"the largest row of each header over the configurations of the ledger, because the ledger holds no "
        f"configuration {frame.name}"
        if frame.is_bound
        else f"the rows of the configuration {frame.judged.name or frame.ledger.base_name}"
    )
    check_report.write_line(f"{FANOUT_RULE}: the build of the configuration {frame.name} is judged against {words}")
    return found


def fanout_written(context: BuildContext, ledger: rule_ledger.Ledger, reason: str | None) -> rule_ledger.Ledger:
    """Write the configuration rows and the rows of the configuration of the build again from its largest costs.

    The write keeps the rows of the other configurations.  A row has no reason.  The write then does not read the
    reason of the call.

    Raises:
        NotApplicable: If the census does not apply to the build
        LedgerError: If an input cannot be used
    """
    frame = census_frame(context, ledger)
    configurations = census_written_configurations(context, frame)
    rows: dict[str, dict[str, FanoutRow]] = fanout_baseline(frame)
    headers = fanout_headers(census_read(context, frame, True), context.root)
    rows[frame.name] = {item.header: FanoutRow(0, item.alone, item.readers) for item in headers[:FANOUT_KEPT]}
    entries = [
        rule_ledger.Entry(census_key_of(name, configuration), f"{row.alone}/{row.readers}", "")
        for configuration, named in rows.items()
        for name, row in named.items()
    ]
    return replace(ledger, configurations=configurations, base={entry.key: entry for entry in entries})


# ── instruction-total ─────────────────────────────────────────────


def instruction_counts(frame: CensusFrame, census: build_census.Census) -> tuple[int, int]:
    """Return the user instructions of the compiles that made the objects and the fixtures (THE RULE instruction-total).

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
    """Compare the instruction total of the build with the baseline of its configuration (THE RULE instruction-total)."""
    try:
        frame = census_frame(context)
        name, total = census_total_of(frame, census_baseline(frame).totals)
        census = census_read(context, frame, False)
        objects, fixtures = instruction_counts(frame, census)
    except CensusRefusal as refusal:
        return [refusal.finding(INSTRUCTION_RULE)]
    frame.announce()
    said = (
        f"the compiles of the build ran {instruction_text(objects + fixtures)} user instructions (objects "
        f"{instruction_text(objects)}, fixtures {instruction_text(fixtures)})"
    )
    found = census_judged(
        frame,
        total,
        objects + fixtures,
        said,
        instruction_text,
        lambda is_growth: instruction_largest(census) if is_growth else "",
    )
    check_report.write_line(
        f"{INSTRUCTION_RULE}: {instruction_text(objects + fixtures)} user instructions in "
        f"{census_unit_counts(census.units, 'object')} objects and {census_unit_counts(census.units, 'fixture')} "
        f"fixtures of the configuration {frame.name}, {census_change_text(objects + fixtures, total.value)}: "
        f"{census_words(frame, name)}"
    )
    return found


def instruction_written(context: BuildContext, ledger: rule_ledger.Ledger, reason: str | None) -> rule_ledger.Ledger:
    """Write the configuration rows and the two total rows of the configuration of the build again.

    The write keeps the rows of the other configurations.

    Raises:
        NotApplicable: If the census does not apply to the build, or the host gives no counter
        LedgerError: If an input cannot be used, or a unit has no instruction count.  Also if the write has no
            reason for a growth over the warning threshold
    """
    frame = census_frame(context, ledger)
    configurations = census_written_configurations(context, frame)
    totals = census_baseline(frame).totals
    objects, fixtures = instruction_counts(frame, census_read(context, frame, False))
    old = totals.get(frame.name)
    census_refused(frame, old, objects + fixtures, reason, instruction_text)
    entries = census_total_entries({**totals, frame.name: census_new_total(old, objects, fixtures, reason)})
    return replace(ledger, configurations=configurations, base={entry.key: entry for entry in entries})


# The keys are text, because assay.guard.engines.meta.runner reads the keys of the table with no import.
RULES = {
    "parse-total": meta_runner.Member(parse_findings, parse_written, configured=True),
    "header-fanout": meta_runner.Member(fanout_findings, fanout_written, configured=True),
    "instruction-total": meta_runner.Member(instruction_findings, instruction_written, configured=True),
}
