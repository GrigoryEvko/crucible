"""fixture-quarantine — the count of quarantine findings in the source of each negative fixture can only fall.

THE CENSUS
    The fixture driver keeps the quarantine plugin in the compile of each
    negative fixture.  The record of each run holds the findings of the plugin
    in the source of the fixture.  It also holds the stamp argument of the
    plugin (assay.loop.fixture.store, The quarantine plugin).  The rule reads
    the fixtures of the build (assay.readers.census.fixtures_of) and the
    record of each one (assay.core.record).  It counts the findings of the
    kinds of THE SCOPE, each finding one time.  The entity of a row is the
    entity of the rule quarantine (THE ENTITY of
    assay.guard.engines.plugin.quarantine).  The census holds each region of
    the source, with its first line and its reason.  The region of an
    opted-out finding is the region with the last first line at or before its
    line.  The row of the finding holds the reason of that region, as in the
    rule quarantine (THE OPT-OUT REGIONS of that module).

THE SCOPE
    The rule counts each kind of the rule quarantine of the registry, and the
    four kinds of PROBE_KINDS: `detail-use`, `lifetime-start`,
    `unchecked-use` and `door-use`.  For its probe, a fixture can use a
    private namespace of the base, or a lifetime that
    `std::start_lifetime_as` starts.  It can also use an unchecked access,
    or a second definition of a door owner.  Each row of such a kind gives
    its reason, which tells what the fixture probes.  A row of such a kind
    with no reason is an error.  A difference row of a key with a base row
    has the reason of the base row.  The rule counts no other kind of the
    census.  Test code can use the testing door, and the rule
    ctx-testing-boundary does not read test/.  The rule does not count
    `testing-use` for that reason.  It does not count the kinds of the rules
    mint-pattern, permission-storage and proof-routes, or the kind
    `outside-home`.  A fixture with such a finding probes the rule of that
    kind.

THE PROBLEMS
    A fixture with a compile command that loads no quarantine plugin has no
    census and no count.  For each other fixture, these conditions are
    problems:
      * the fixture has no record, because it did not run in the build
      * the record holds no census.  The compile wrote no log, or it
        succeeded and its object holds no section of the plugin, or an
        earlier driver wrote the record
      * the census holds a stamp that is not the stamp argument of the compile
        command of the fixture.  The plugin, the rule table or the configure
        changed after the run
      * the census holds an opted-out finding with no region at or before its
        line
      * the record cannot be read.
    Each problem is an error, and `guard write` writes no ledger with one.
    The rule does not apply when no compile command of a fixture loads the
    plugin, or when no fixture has a record.

THE LEDGER
    utils/ledgers/fixture-quarantine.txt holds the count of each fixture
    source, kind and entity in the rows of each configuration.  The ledger of
    the rule quarantine has the same rows (assay.guard.engines.plugin.counted).
    The rows of the configuration of a build judge it (THE POLICY of
    assay.core.configuration).

THE VERDICT
    * A count above its row is an error.  A new finding of a fixture cannot
      stay when a different finding goes.
    * A fixture source with a finding and no row in the ledger is an error: a
      new fixture has no finding (CLAUDE.md section XXII).
    * A count below its row is an error too, until the same commit writes
      the ledger again.
    * Each directory of fixture sources with a kept count gives one warning.

WRITE THE LEDGER
    `guard write --rule fixture-quarantine --build-dir DIR` writes the rows of
    the configuration of the build, with the modes of the rule quarantine:
    --raise for a change of the plugin or of the rule table, --reason TEXT,
    --add-configuration and --import LOG (assay.guard.engines.plugin.counted).
    Run each fixture of the build first.

THE ORDER
    The test guard_fixture_quarantine starts after each fixture test
    (test/CMakeLists.txt), and it then reads the records of the run.

THE PLANTS
    A plant holds the file fixtures.txt and a planted ledger.  The rows of
    fixtures.txt (the rows grammar of assay.core.rows):
        fixture NAME | SOURCE | STAMP | CENSUS_STAMP
                The fixture NAME with the source SOURCE.  STAMP is the stamp
                argument of its compile command, "-" for a command with no
                plugin.  CENSUS_STAMP is the stamp of its census, "-" for a
                record with no census, and the word missing for no record
        finding NAME | KIND | LINE | ENTITY
                One finding of the census of the fixture NAME
    The configuration of a planted build is PLANTED_CONFIGURATION.

Usage
    guard check --rule fixture-quarantine --build-dir DIR [--warnings-dir DIR]
    guard write --rule fixture-quarantine --build-dir DIR [--raise] [--reason TEXT] [--add-configuration]
    guard write --rule fixture-quarantine --import LOG

Complexity: linear in the fixtures of the build and the findings of their records.  O(k log k) for k rows of the
ledger and counts of the build.
"""

import dataclasses
import os
from collections import Counter
from collections.abc import Mapping, Sequence
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import ledger as rule_ledger
from assay.core import record as cost_record
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.guard.engines.plugin import counted, quarantine
from assay.guard.engines.plugin import runner as plugin_runner
from assay.readers import census as build_census
from assay.readers import compdb

RULE = "fixture-quarantine"
# The rule with the kinds that the census counts (THE SCOPE).
COUNTED_RULE = quarantine.RULE
# The kinds of the census that the rule counts, and that the rule quarantine does not give (THE SCOPE).  Each row of
# such a kind has its reason.  The value is the rule of the registry that gives the kind.
PROBE_KINDS = {
    "detail-use": "detail-namespace",
    "lifetime-start": "start-lifetime",
    "unchecked-use": "no-unchecked-access",
    "door-use": "ctx-init-door",
}
# The census stamp of a planted fixture with no record (THE PLANTS).
MISSING = "missing"
# The file of the planted fixtures of a plant, and the configuration of a planted build (THE PLANTS).
PLANT_FILE = "fixtures.txt"
PLANTED_CONFIGURATION = build_config.BuildConfiguration("planted", (("kind", "planted"), ("tier", "planted")))
# The advice of a write that refuses a count above its row.
REFUSAL = (
    "Remove the new findings from the fixture, and use a type of the base.  Give --raise only when the plugin or the "
    "rule table changed what it reports"
)


@dataclasses.dataclass(frozen=True, slots=True)
class FixtureFacts:
    """One fixture of the build: its name, its source, the stamp of its command, and the census of its record.

    `source` is the source as a message shows it, relative to the root.  `stamp` is the stamp argument of the
    compile command, or None when the command loads no plugin.  `census_stamp` and `census` are those of the record,
    each None when the record holds no census.  `problem` tells why the record cannot be read, MISSING for no record,
    or is empty.
    """

    name: str
    source: str
    stamp: str | None
    census_stamp: str | None
    census: tuple[cost_record.CensusFinding, ...] | None
    problem: str = ""


@dataclasses.dataclass(frozen=True, slots=True)
class FixtureCensus:
    """The census of the build: the number of fixtures with a census, and the findings and the problems."""

    objects: int
    findings: tuple[check_report.Finding, ...]


class FixtureContext(plugin_runner.PluginContext):
    """The build that the rule reads: the root, the build directory, its fixtures and its configuration.

    The context of assay.guard.engines.plugin.counted (THE CONTEXT of that module).  It reads the census of the
    fixtures, and not the sections of the objects.  It reads the fixtures, the census and the configuration one time, on
    the first use.  A plant gives planted fixtures and a planted configuration, and then no build directory (THE
    PLANTS).
    """

    __slots__ = ("_fixtures", "_found")

    def __init__(
        self,
        root: Path,
        build_dir: Path | None = None,
        fixtures: Sequence[FixtureFacts] | None = None,
        configuration: build_config.BuildConfiguration | None = None,
    ) -> None:
        """Keep the root, the build directory, and planted fixtures and a planted configuration."""
        super().__init__(root, build_dir, None, configuration)
        self._fixtures = tuple(fixtures) if fixtures is not None else None
        self._found: FixtureCensus | None = None

    def census(self) -> plugin_runner.Census:
        """Return the census of the build: the number of fixtures with a census in its field objects (THE CENSUS).

        found() gives the findings and the problems of the same read.

        Raises:
            NotApplicable: If the call gives no build directory, no fixture command loads the plugin, or no fixture
                has a record
            BuildError: If the test list, the compile database or the registry cannot be read
        """
        return plugin_runner.Census(self.found().objects)

    def found(self) -> FixtureCensus:
        """Return the findings and the problems of the census of the fixtures (THE CENSUS, THE PROBLEMS).

        Raises:
            NotApplicable: If the call gives no build directory, no fixture command loads the plugin, or no fixture
                has a record
            BuildError: If the test list, the compile database or the registry cannot be read
        """
        if self._found is None:
            fixtures = self._fixtures if self._fixtures is not None else read_fixtures(self.root, self.build_dir)
            try:
                kinds = counted_kinds(rule_registry.load())
            except (OSError, rows_grammar.RowsError, KeyError) as problem:
                raise plugin_runner.BuildError(f"the kinds of THE SCOPE cannot be read: {problem}") from None
            self._found = census_of(fixtures, kinds, self.build_shown())
        return self._found

    def configuration(self) -> build_config.BuildConfiguration:
        """Return the configuration of the build (assay.core.configuration, THE CONFIGURATION).

        Raises:
            NotApplicable: If the call gives no build directory, or the build made only some targets
            BuildError: If the build cannot be read
        """
        if self._configuration is None:
            try:
                self._configuration = BuildContext(self.root, self.build_dir).configuration()
            except (build_census.CensusError, compdb.CompdbError, build_config.ConfigurationError) as problem:
                raise plugin_runner.BuildError(str(problem)) from None
        return self._configuration


# ── The census ─────────────────────────────────────────────────────


def counted_kinds(registry: rule_registry.Registry) -> frozenset[str]:
    """Return the kinds that the rule counts: each kind of the rule quarantine and each kind of PROBE_KINDS (THE SCOPE).

    Raises:
        KeyError: If the registry has no rule quarantine, or a rule of PROBE_KINDS does not give its kind
    """
    for kind, rule_id in PROBE_KINDS.items():
        if kind not in registry.rules[rule_id].kinds:
            raise KeyError(f"the rule {rule_id} of the registry gives no kind {kind}")
    return frozenset((*registry.rules[COUNTED_RULE].kinds, *PROBE_KINDS))


def read_fixtures(root: Path, build_dir: Path | None) -> tuple[FixtureFacts, ...]:
    """Return the facts of each fixture of the build: the stamp of its command and the census of its record.

    Complexity: linear in the tests of the build, the rows of the compile database and the records.

    Raises:
        NotApplicable: If the call gives no build directory
        BuildError: If the test list or the compile database cannot be read
    """
    if build_dir is None:
        raise meta_runner.NotApplicable(
            f"{RULE} reads the records of the fixtures of --build-dir DIR, and the call gives none"
        )
    try:
        fixtures = build_census.fixtures_of(build_dir)
        stamps = {
            os.path.realpath(entry.file): plugin_runner.stamp_of(entry.command) for entry in compdb.entries(build_dir)
        }
    except build_census.CensusError as problem:
        raise plugin_runner.BuildError(str(problem)) from None
    except compdb.CompdbError as problem:
        raise plugin_runner.BuildError(f"the compile database cannot be read: {problem}") from None
    facts: list[FixtureFacts] = []
    for name, source in fixtures:
        stamp = stamps.get(os.path.realpath(source))
        shown = build_census.shown(source, root)
        try:
            record = cost_record.read_fixture(Path(cost_record.fixture_record_file(build_dir, name)))
        except cost_record.MissingRecord:
            facts.append(FixtureFacts(name, shown, stamp, None, None, MISSING))
            continue
        except cost_record.RecordError as problem:
            facts.append(FixtureFacts(name, shown, stamp, None, None, str(problem)))
            continue
        facts.append(FixtureFacts(name, shown, stamp, record.census_stamp, record.census))
    return tuple(facts)


def entity_of(kind: str, entity: str, reason: str) -> str:
    """Return the entity of the row of one finding of a census (THE CENSUS).

    An opted-out finding gives the reason of its region, and the kind and the entity of the finding in it.  Each other
    kind gives the entity of the rule quarantine, and `reason` has no effect.
    """
    if kind != quarantine.OPTED_OUT:
        return quarantine.entity_key(kind, entity)
    return quarantine.opted_out_key(reason, entity)


def problem_of(item: FixtureFacts, build_shown: str) -> str:
    """Return the message of the problem of one fixture with a plugin, or an empty text (THE PROBLEMS)."""
    rerun = f"Run the fixture again: ctest --test-dir {build_shown} -R '^{item.name}$'"
    if item.problem == MISSING:
        return ""
    if item.problem:
        return f"the record of the fixture {item.name} cannot count: {item.problem}.  {rerun}"
    if item.census is None or item.census_stamp is None:
        return (
            f"the record of the fixture {item.name} holds no census.  The compile wrote no log, or it succeeded and its "
            f"object holds no section of the plugin, or an earlier driver wrote the record.  {rerun}"
        )
    if item.census_stamp != item.stamp:
        return (
            f"the census of the fixture {item.name} has the stamp {item.census_stamp}, and its compile command has "
            f"the stamp {item.stamp}.  The plugin, the rule table or the configure changed after the run.  {rerun}"
        )
    return ""


def census_of(fixtures: Sequence[FixtureFacts], kinds: frozenset[str], build_shown: str) -> FixtureCensus:
    """Return the findings and the problems of the census of the fixtures (THE CENSUS, THE PROBLEMS).

    A finding has the kind and the entity of its row, and a problem has no kind.  Complexity: linear in the fixtures
    and the findings of their censuses.

    Raises:
        NotApplicable: If no fixture command loads the plugin, or no fixture has a record
    """
    counted_fixtures = [item for item in fixtures if item.stamp is not None]
    if not counted_fixtures:
        raise meta_runner.NotApplicable(
            "no compile command of a negative fixture of the build loads the quarantine plugin"
        )
    missing = [item.name for item in counted_fixtures if item.problem == MISSING]
    if len(missing) == len(counted_fixtures):
        raise meta_runner.NotApplicable(
            f"no fixture of the build has a record, because the fixtures did not run in this build directory.  The "
            f"record is {cost_record.fixture_record_file('', 'NAME')}, and ctest -R '^neg_' writes it"
        )
    found: list[check_report.Finding] = []
    if missing:
        found.append(
            check_report.Finding(
                "error",
                build_shown,
                0,
                RULE,
                f"{len(missing)} of the {len(counted_fixtures)} fixtures with the plugin have no record, because they "
                f"did not run in this build directory: {build_census.names_text(missing)}.  Run every fixture, then the "
                f"check",
            )
        )
    for item in counted_fixtures:
        problem = problem_of(item, build_shown)
        if problem:
            found.append(check_report.Finding("error", item.source, 0, RULE, problem))
            continue
        census = item.census or ()
        starts = sorted((line, entity) for kind, line, _, entity in census if kind == quarantine.REGION)
        for kind, line, column, entity in census:
            if kind.partition(":")[0] not in kinds:
                continue
            text = f"{kind} {item.source}:{line}:{column} {entity}"
            reason = quarantine.reason_at(starts, line) if kind == quarantine.OPTED_OUT else ""
            if reason is None:
                found.append(check_report.Finding("error", item.source, line, RULE, no_region_problem(item, text)))
                continue
            found.append(
                check_report.Finding("error", item.source, line, RULE, text, kind, entity_of(kind, entity, reason))
            )
    return FixtureCensus(len(counted_fixtures) - len(missing), tuple(found))


def no_region_problem(item: FixtureFacts, text: str) -> str:
    """Return the problem of an opted-out finding of a census with no region at or before its line (THE PROBLEMS)."""
    return (
        f"the census of the fixture {item.name} holds the opted-out finding {text!r}, and no region of the source at "
        f"or before its line.  The plugin gives a note for each region of a unit with no report: run the fixture again"
    )


def findings(context: FixtureContext) -> list[check_report.Finding]:
    """Give the findings and the problems of the census of the build of the context (THE CENSUS).

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return list(context.found().findings)


# ── The verdict ────────────────────────────────────────────────────


def load_inputs(root: Path) -> None:
    """Read no input: the verdict reads only the counts and the ledger."""


def reason_problems(ledger: rule_ledger.Ledger) -> list[check_report.Finding]:
    """Give one error for each row of a kind of PROBE_KINDS with no reason (THE SCOPE).

    A difference row of a key with a base row has the reason of the base row.  Complexity: linear in the rows of the
    ledger.
    """
    shown = rule_ledger.shown_path(RULE)
    unheld = (entry for table in ledger.deltas.values() for entry in table.values() if entry.key not in ledger.base)
    entries = [*ledger.base.values(), *unheld]
    return [
        check_report.Finding(
            "error",
            shown,
            entry.line,
            RULE,
            f"the row of {entry.key.path} with {counted.described(entry.key.kind, entry.key.entity)} gives no "
            f"reason.  The REASON cell of a row of a kind of a probe tells what the fixture probes",
        )
        for entry in entries
        if entry.key.kind in PROBE_KINDS and not entry.reason
    ]


def directory_of(path: str) -> str:
    """Return the directory of a fixture source, for the warning of its directory."""
    return path.rsplit("/", 1)[0] if "/" in path else "."


def evaluate(
    counts: Mapping[rule_ledger.Key, int],
    ledger: rule_ledger.Ledger,
    name: str | None,
    inputs: None,
    build_shown: str,
) -> list[check_report.Finding]:
    """Compare each count of the build with its ledger count, and give one warning for each directory (THE VERDICT).

    Complexity: O(k log k) for k counts of the build and rows of the ledger.
    """
    found = reason_problems(ledger)
    expected = ledger.expected(name)
    held = {key.path for key in expected}
    failed: set[str] = set()
    shown = rule_ledger.shown_path(RULE)
    command = f"python3 utils/bin/guard write --rule {RULE} --build-dir {build_shown}"
    is_bound = name == rule_ledger.MAXIMUM
    for key in sorted(set(counts) | set(expected)):
        measured, admitted = counts.get(key, 0), expected.get(key, 0)
        if measured == admitted or (is_bound and measured < admitted):
            continue
        failed.add(directory_of(key.path))
        what = counted.described(key.kind, key.entity)
        if measured > admitted and key.path not in held:
            message = (
                f"the fixture {key.path} has {measured} findings of {what}, and the ledger holds no row of the "
                f"fixture.  A new fixture has no finding (CLAUDE.md section XXII): use a type of the base"
            )
        elif measured > admitted:
            message = (
                f"the fixture {key.path} has {measured} findings of {what}, and the ledger admits {admitted}.  A "
                f"change can only lower a count, also when a finding of a different entity goes: use a type of the "
                f"base"
            )
        else:
            message = (
                f"the fixture {key.path} has {measured} findings of {what}, and the ledger holds {admitted}.  Write "
                f"the ledger again in the same commit: `{command}`"
            )
        found.append(check_report.Finding("error", shown, ledger.line_of(key, name), RULE, message))
    kept: dict[str, Counter[str]] = {}
    first_line: dict[str, int] = {}
    for key, admitted in sorted(expected.items()):
        directory = directory_of(key.path)
        kept_count = min(admitted, counts.get(key, 0)) if is_bound else admitted
        if directory in failed or kept_count == 0:
            continue
        kept.setdefault(directory, Counter())[key.kind] += kept_count
        first_line.setdefault(directory, ledger.line_of(key, name))
    for directory, kinds in sorted(kept.items()):
        listed = ", ".join(f"{kind} {count}" for kind, count in sorted(kinds.items()))
        found.append(
            check_report.Finding(
                "warning",
                shown,
                first_line[directory],
                RULE,
                f"the fixtures of {directory} keep {sum(kinds.values())} quarantine findings: {listed}",
            )
        )
    return found


# The line that tells the findings, the rows and the fixtures of one run or one write.
summary = counted.summary_of(RULE, "findings", "fixtures", "fixtures with a census")

SPEC = counted.CountedRule(RULE, findings, load_inputs, evaluate, summary, frozenset(), REFUSAL, FixtureContext)


# ── The run, the judge and the write ───────────────────────────────


def context_of(context: BuildContext | FixtureContext) -> FixtureContext:
    """Return the context of the rule for the context of the engine build, or the planted context itself."""
    if isinstance(context, FixtureContext):
        return context
    return FixtureContext(context.root, context.build_dir)


def run(rule: rule_registry.Rule, context: BuildContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build, judge its counts against the ledger, and write the findings (THE VERDICT).

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    return counted.run(SPEC, rule, context_of(context), warnings_dir)


def judge(rule: rule_registry.Rule, context: BuildContext | FixtureContext) -> list[check_report.Finding]:
    """Return the findings of the build judged against the ledger of the tree of the context, for the witness.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return counted.judge(SPEC, rule, context_of(context))


def write(rule: rule_registry.Rule, request: plugin_runner.WriteRequest) -> int:
    """Write the rows of the configuration of the build (WRITE THE LEDGER), or import the rows of a log.

    Returns:
        0 when the ledger is written, 1 when a fixture has a problem or a count increased, SKIPPED when the rule does
        not apply
    """
    return counted.write(SPEC, rule, request)


# ── The plants ─────────────────────────────────────────────────────


def plant_context(plant: Path, scratch: Path) -> FixtureContext:
    """Copy a plant to a throwaway repository, and give the context of its planted fixtures (THE PLANTS).

    Raises:
        NotApplicable: If the plant has no file of planted fixtures, or the file does not obey its rows
    """
    meta_runner.copy_plant(plant, scratch)
    path = scratch / PLANT_FILE
    try:
        rows = rows_grammar.read(path, f"{plant.name}/{PLANT_FILE}")
        facts = planted_fixtures(rows)
    except (OSError, rows_grammar.RowsError) as problem:
        raise meta_runner.NotApplicable(f"the planted fixtures cannot be read: {problem}") from None
    return FixtureContext(scratch, None, facts, PLANTED_CONFIGURATION)


def planted_fixtures(rows: Sequence[rows_grammar.Row]) -> tuple[FixtureFacts, ...]:
    """Return the fixtures of the rows of a plant (THE PLANTS).

    Raises:
        rows_grammar.RowsError: If a row is not a fixture row or a finding row of the plant format
    """
    heads: dict[str, tuple[str, str | None, str]] = {}
    found: dict[str, list[cost_record.CensusFinding]] = {}
    for row in rows:
        if row.words[:1] == ("fixture",) and len(row.words) == 2 and len(row.cells) == 3:
            source_cell, stamp_cell, census_cell = row.cells
            heads[row.words[1]] = (source_cell, stamp_cell or None, census_cell)
        elif row.words[:1] == ("finding",) and len(row.words) == 2 and len(row.cells) == 3:
            kind, line, entity = row.cells
            found.setdefault(row.words[1], []).append(
                (kind, rows_grammar.number(line, 1 << 31, row, "the line"), 1, entity)
            )
        else:
            raise row.error(
                "a row is `fixture NAME | SOURCE | STAMP | CENSUS_STAMP` or `finding NAME | KIND | LINE | ENTITY`"
            )
    facts: list[FixtureFacts] = []
    for name, (source, stamp, census_stamp) in sorted(heads.items()):
        if census_stamp == MISSING:
            facts.append(FixtureFacts(name, source, stamp, None, None, MISSING))
        elif not census_stamp:
            facts.append(FixtureFacts(name, source, stamp, None, None))
        else:
            facts.append(FixtureFacts(name, source, stamp, census_stamp, tuple(sorted(found.get(name, ())))))
    return tuple(facts)
