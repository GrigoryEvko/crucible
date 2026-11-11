"""fixture-quarantine — the count of quarantine findings in the source of each negative fixture can only fall.

THE CENSUS
    The fixture driver keeps the quarantine plugin in the compile of each
    negative fixture.  The record of each run holds the findings of the plugin
    in the source of the fixture.  It also holds the stamp argument of the
    plugin (assay.loop.fixture.store, The quarantine plugin).  The rule reads
    the fixtures of the build (assay.readers.census.fixtures_of) and the
    record of each one (assay.core.record).  It counts the findings of the
    kinds of THE SCOPE, each finding one time, by fixture source, kind and
    entity (THE ENTITY).  The census holds each region of the source, with its
    first line and its reason.  The reason has one space between each two
    words (assay.readers.sections, THE ENTITIES OF THE RECORDS).  The region
    of an opted-out finding is the region with the last first line at or
    before its line, as in the rule quarantine (THE OPT-OUT REGIONS of
    assay.guard.engines.plugin.quarantine).  The row of the finding holds the
    reason of that region.

THE ENTITY
    The entity of a row is the part of the entity of a finding that names
    what the plugin refuses (entity_key()).  The reader gives the parts of
    each entity (assay.readers.sections, THE ENTITIES OF THE RECORDS), and
    each part has one space between each two words:
      * raw-pointer-object, raw-function-pointer and c-array-object: no
        entity.  The plugin gives the name of the declaration, and the row
        counts the findings of the fixture and the kind
      * `layer-header`: the header that the include names, for example
        <vector>, without the layer of the file
      * `upward-include`: the file that the include names, without the two
        layers
      * opted-out: "REASON | KIND ENTITY", the reason of the region, and the
        kind and the entity of the finding in it
      * region: the reason of the region
      * each other kind: the full text, for example std::span,
        std::span::operator[], memcpy, new, or the header of a `door-header`
        include
    The entity holds no line, and a change that only moves code keeps each
    row.  A change that removes std::vector from a fixture and adds std::map
    to it increases the count of std::map, and the rule refuses that change.

    The entity of an object finding holds no printed type.  GCC prints a type
    with the template arguments and the parameter names of the first
    specialization that the unit made with the same arguments, so one
    declaration could give two texts.  The plugin writes these entities
    (utils/tools/quarantine/declaration.cpp, THE ENTITY OF AN OBJECT):
      * std-object and c-library-object: the library class with no template
        argument, for example std::span
      * raw-pointer-object, raw-function-pointer and c-array-object: the name
        of the declaration
      * raw-new-delete: the operator, for example new or placement new.

THE SCOPE
    The rule counts each kind of the rule quarantine of the registry, and the
    four kinds of PROBE_KINDS: `detail-use`, `lifetime-start`,
    `unchecked-use` and `door-use`.  For its probe, a fixture can use a
    private namespace of the base, or a lifetime that
    `std::start_lifetime_as` starts.  It can also use an unchecked access,
    or a second definition of a door owner.  Each row of such a kind gives
    its reason, which tells what the fixture probes.  A row of such a kind
    with no reason is an error.  The rule counts no other kind of the
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

THE FILES OF THE FIXTURES
    The census of a fixture holds the findings of its source only.  The rule
    quarantine counts the findings of the files that an object of the target
    all reads.  A file that only the compiles of fixtures read then has
    findings that no ledger counts.  The rule reads the files of each unit
    (assay.readers.census.read_census).  Such a file is in the source root or
    in the build directory, a fixture compile reads it, and no object of all
    reads it.  The rule gives an error for each such file, with no kind and
    no ledger row.  The source of each fixture
    is no such file, because its census counts it.  A file in the directory
    of the argument stamps= of the plugin, a mode stamp of assay.loop.stamps,
    is no such file too.  Give such a file a compile in all, as test/layer
    gives each header of include/, or include it from a unit of all.  The
    census cannot read the files of the objects of a build that made only
    some targets.  The rule then prints the reason, and it judges the counts
    with no error of this kind.

THE LEDGER
    utils/ledgers/fixture-quarantine.txt holds the count of each fixture
    source, kind and entity in one set of rows, and these rows judge each
    build.  The run and the write are those of
    assay.guard.engines.plugin.counted, as for the rule quarantine.

THE VERDICT
    * A count above its row is an error.  A new finding of a fixture cannot
      stay when a different finding goes.
    * A fixture source with a finding and no row in the ledger is an error: a
      new fixture has no finding (CLAUDE.md section XXII).
    * A count below its row gives no finding (THE POLICIES of
      assay.core.ledger).  A complete write lowers the row.
    * A count at its row gives no finding either.  `guard list --path FILE`
      prints the rows of a fixture source.

WRITE THE LEDGER
    `guard write --rule fixture-quarantine --build-dir DIR` writes the counts
    of one build, with the modes of the rule quarantine: --raise for a change
    of the plugin or of the rule table, and --reason TEXT
    (assay.guard.engines.plugin.counted).  It lowers no row.  The write of a
    build of each configuration of the landing gate lowers the rows (THE
    REDUCE of assay.core.ledger).  Run each fixture of each build first.

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
        read NAME | FILE
                The compile of the fixture NAME reads FILE, a path of the
                tree.  The name `all` gives a file that an object of the
                target all reads (THE FILES OF THE FIXTURES)
        read stamps | DIRECTORY
                DIRECTORY, a path of the tree, is the directory of the mode
                stamps that an argument stamps= of the plugin names

Usage
    guard check --rule fixture-quarantine --build-dir DIR [--warnings-dir DIR]
    guard write --rule fixture-quarantine --build-dir DIR [--raise] [--reason TEXT]

Complexity: linear in the fixtures of the build, the findings of their records and the files of the units.  O(k log k)
for k rows of the ledger and counts of the build.
"""

import dataclasses
import os
from collections.abc import Iterable, Mapping, Sequence
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import record as cost_record
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import ruletable
from assay.guard.engines.buildsys.runner import BuildContext
from assay.guard.engines.meta import runner as meta_runner
from assay.guard.engines.plugin import counted, quarantine
from assay.guard.engines.plugin import runner as plugin_runner
from assay.readers import census as build_census
from assay.readers import compdb
from assay.readers import sections as plugin_sections

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
# The kinds with the name of a declaration as the entity: a row has no entity (THE ENTITY).
DECLARATION_KINDS = frozenset(("raw-pointer-object", "raw-function-pointer", "c-array-object"))
# The separator between the reason of a region and the finding in it, in the entity of an opted-out row.
REASON_SEPARATOR = " | "
# The census stamp of a planted fixture with no record (THE PLANTS).
MISSING = "missing"
# The name of a read row of a plant for a file that an object of the target all reads (THE PLANTS).
ALL_UNITS = "all"
# The name of a read row of a plant for the directory of the mode stamps (THE PLANTS).
STAMPS_UNIT = "stamps"
# The plugin argument of the directory of the mode stamps (THE FILES OF THE FIXTURES).
STAMPS_ARGUMENT = f"{plugin_runner.PLUGIN_ARGUMENT}stamps="
# The file of the planted fixtures of a plant (THE PLANTS).
PLANT_FILE = "fixtures.txt"
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
    """The build that the rule reads: the root, the build directory and its fixtures.

    The context of assay.guard.engines.plugin.counted (THE CONTEXT of that module).  It reads the census of the
    fixtures, and not the sections of the objects.  It reads the fixtures and the census one time, on the first use.  A
    plant gives planted fixtures, and then no build directory (THE PLANTS).
    """

    __slots__ = ("_fixtures", "_found", "_reads")

    def __init__(
        self,
        root: Path,
        build_dir: Path | None = None,
        fixtures: Sequence[FixtureFacts] | None = None,
        reads: Sequence[tuple[str, str]] | None = None,
    ) -> None:
        """Keep the root, the build directory, and planted fixtures and planted reads.

        `reads` holds the read rows of a plant: the name of a fixture, or ALL_UNITS, and a path of the tree (THE
        PLANTS).
        """
        super().__init__(root, build_dir, None)
        self._fixtures = tuple(fixtures) if fixtures is not None else None
        self._found: FixtureCensus | None = None
        self._reads = tuple(reads) if reads is not None else None

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
            counted_census = census_of(fixtures, kinds, self.build_shown())
            self._found = FixtureCensus(counted_census.objects, (*counted_census.findings, *self.file_findings()))
        return self._found

    def file_findings(self) -> tuple[check_report.Finding, ...]:
        """Return an error for each file that only the compiles of fixtures read (THE FILES OF THE FIXTURES).

        A plant gives its read rows.  A build gives the files of the census of its units.  The census of a build that
        made only some targets cannot give the files of its objects.  The function then prints the reason and gives no
        finding.  A census that cannot be read gives one error.
        """
        root = Path(os.path.realpath(self.root))
        if self._reads is not None:
            real = {(name, os.path.realpath(root / path)) for name, path in self._reads}
            fixture_reads = sorted((name, path) for name, path in real if name not in (ALL_UNITS, STAMPS_UNIT))
            objects = frozenset(path for name, path in real if name == ALL_UNITS)
            stamps = tuple(sorted(path for name, path in real if name == STAMPS_UNIT))
            sources = frozenset(os.path.realpath(root / item.source) for item in self._fixtures or ())
            return unjudged_findings(fixture_reads, objects, sources, stamps, root, None)
        if self.build_dir is None:
            return ()
        try:
            units = build_census.read_census(self.build_dir, self.root).units
            stamp_directories = stamps_directories(self.build_dir)
        except build_census.NotApplicable as reason:
            check_report.write_line(f"{RULE}: the files of the fixtures are not judged: {reason}")
            return ()
        except (build_census.CensusError, compdb.CompdbError) as problem:
            return (
                check_report.Finding(
                    "error", self.build_shown(), 0, RULE, f"the files of the fixtures cannot be read: {problem}"
                ),
            )
        fixture_reads = [(unit.item, path) for unit in units if unit.kind == "fixture" for path in unit.files]
        objects = frozenset(path for unit in units if unit.kind == "object" for path in unit.files)
        sources = frozenset(
            os.path.realpath(os.path.join(root, unit.source)) for unit in units if unit.kind == "fixture"
        )
        return unjudged_findings(fixture_reads, objects, sources, stamp_directories, root, self.build_dir)


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


def joined(*fields: str) -> str:
    """Return the fields with one space between them.  An empty field at the end gives no space."""
    return " ".join(fields).rstrip(" ")


def row_entity(kind: str, parsed: plugin_sections.FindingEntity) -> str:
    """Return the entity of the row of one finding from the record of its entity (THE ENTITY)."""
    if kind in DECLARATION_KINDS:
        return ""
    if isinstance(parsed, plugin_sections.LayerHeader):
        return parsed.header
    if isinstance(parsed, plugin_sections.UpwardInclude):
        return parsed.target
    return parsed.text


def entity_key(kind: str, entity: str) -> str:
    """Return the entity of the row of one finding: the part of its entity that names what the plugin refuses.

    THE ENTITY gives the rule for each kind.
    """
    return row_entity(kind, plugin_sections.quarantine_entity(kind, entity))


def opted_out_key(reason: str, entity: str) -> str:
    """Return the entity of the row of an opted-out finding: the reason of its region, the inner kind and entity.

    `reason` is the reason of the region as the reader gives it, with one space between each two words.
    """
    parsed = plugin_sections.opted_out(entity)
    return joined(f"{reason}{REASON_SEPARATOR}{parsed.kind}", row_entity(parsed.kind, parsed.inner))


def entity_of(kind: str, entity: str, reason: str) -> str:
    """Return the entity of the row of one finding of a census (THE ENTITY).

    An opted-out finding gives the reason of its region, and the kind and the entity of the finding in it.  For each
    other kind, `reason` has no effect.
    """
    if kind != quarantine.OPTED_OUT:
        return entity_key(kind, entity)
    return opted_out_key(reason, entity)


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
        starts = sorted(
            (line, plugin_sections.quarantine_entity(kind, entity).text)
            for kind, line, _, entity in census
            if kind == quarantine.REGION
        )
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


def stamps_directories(build_dir: Path) -> tuple[str, ...]:
    """Return the path of `realpath()` of each directory that an argument stamps= of the compile database names.

    Complexity: linear in the arguments of the compile database.

    Raises:
        CompdbError: If the compile database cannot be read
    """
    found = {
        os.path.realpath(argument[len(STAMPS_ARGUMENT) :])
        for entry in compdb.entries(build_dir)
        for argument in entry.argv()
        if argument.startswith(STAMPS_ARGUMENT)
    }
    return tuple(sorted(found))


def file_shown(real: str, root: Path, build_dir: Path | None) -> str:
    """Return a path of `realpath()` as a message shows it.

    A file of the build directory is @build/PATH, and a file of the root is relative to the root.
    """
    if build_dir is not None and Path(real).is_relative_to(os.path.realpath(build_dir)):
        return f"{ruletable.BUILD_PREFIX}{Path(real).relative_to(os.path.realpath(build_dir)).as_posix()}"
    return Path(real).relative_to(root).as_posix() if Path(real).is_relative_to(root) else real


def unjudged_findings(
    fixture_reads: Iterable[tuple[str, str]],
    objects: frozenset[str],
    sources: frozenset[str],
    stamp_directories: Sequence[str],
    root: Path,
    build_dir: Path | None,
) -> tuple[check_report.Finding, ...]:
    """Return an error for each file of the tree that the compile of a fixture reads and no object of all reads.

    Each path is a path of `realpath()`, and `root` is one too.  Only a file of the root or of the build directory
    can be such a file.  The source of a fixture and a file of a directory of the mode stamps are no such file (THE
    FILES OF THE FIXTURES).  Complexity: linear in the pairs, with one prefix test for each directory of the stamps.
    """
    directories = [str(root)] if build_dir is None else [str(root), os.path.realpath(build_dir)]
    places = tuple(f"{directory}{os.sep}" for directory in directories)
    exempt = tuple(f"{directory}{os.sep}" for directory in stamp_directories)
    readers: dict[str, set[str]] = {}
    for name, path in fixture_reads:
        if path in objects or path in sources or not path.startswith(places) or path.startswith(exempt):
            continue
        readers.setdefault(path, set()).add(name)
    return tuple(
        check_report.Finding(
            "error",
            file_shown(path, root, build_dir),
            0,
            RULE,
            f"only the compiles of the fixtures {build_census.names_text(names)} read the file, and no object of the "
            f"target all reads it.  The census of a fixture counts its source only, and the rule quarantine counts "
            f"the files of the objects of all.  No ledger then counts the findings of the file.  Give it a compile "
            f"in all, as test/layer gives each header of include/, or include it from a unit of all",
        )
        for path, names in sorted(readers.items())
    )


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


def reason_problems(ledger: rule_ledger.Ledger) -> list[check_report.Finding]:
    """Give one error for each row of a kind of PROBE_KINDS with no reason (THE SCOPE).

    Complexity: linear in the rows of the ledger.
    """
    shown = rule_ledger.shown_path(RULE)
    return [
        check_report.Finding(
            "error",
            shown,
            entry.line,
            RULE,
            f"the row of {entry.key.path} with {counted.described(entry.key.kind, entry.key.entity)} gives no "
            f"reason.  The REASON cell of a row of a kind of a probe tells what the fixture probes",
        )
        for entry in ledger.base.values()
        if entry.key.kind in PROBE_KINDS and not entry.reason
    ]


def evaluate(
    counts: Mapping[rule_ledger.Key, int],
    ledger: rule_ledger.Ledger,
    inputs: None,
    build_shown: str,
) -> list[check_report.Finding]:
    """Compare each count of the build with its ledger count (THE VERDICT).

    Complexity: O(k log k) for k counts of the build and rows of the ledger.
    """
    found = reason_problems(ledger)
    expected = ledger.expected()
    held = {key.path for key in expected}
    shown = rule_ledger.shown_path(RULE)
    for key in sorted(set(counts) | set(expected)):
        measured, admitted = counts.get(key, 0), expected.get(key, 0)
        if measured <= admitted:
            continue
        what = counted.described(key.kind, key.entity)
        if key.path not in held:
            message = (
                f"the fixture {key.path} has {measured} findings of {what}, and the ledger holds no row of the "
                f"fixture.  A new fixture has no finding (CLAUDE.md section XXII): use a type of the base"
            )
        else:
            message = (
                f"the fixture {key.path} has {measured} findings of {what}, and the ledger admits {admitted}.  A "
                f"change can only lower a count, also when a finding of a different entity goes: use a type of the "
                f"base"
            )
        found.append(check_report.Finding("error", shown, ledger.line_of(key), RULE, message))
    return found


# The line that tells the findings, the rows and the fixtures of one run or one write.
summary = counted.summary_of(RULE, "findings", "fixtures", "fixtures with a census")

SPEC = counted.CountedRule(RULE, findings, counted.no_inputs, evaluate, summary, frozenset(), REFUSAL, FixtureContext)


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
    """Write the counts of the builds of the request (WRITE THE LEDGER).

    Returns:
        0 when the ledger is written, 1 when a fixture has a problem or a count increased, SKIPPED when the rule does
        not apply to a build
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
        reads = planted_reads(rows)
    except (OSError, rows_grammar.RowsError) as problem:
        raise meta_runner.NotApplicable(f"the planted fixtures cannot be read: {problem}") from None
    unknown = sorted({name for name, _ in reads} - {item.name for item in facts} - {ALL_UNITS, STAMPS_UNIT})
    if unknown:
        raise meta_runner.NotApplicable(f"a read row names no fixture of the plant: {', '.join(unknown)}")
    return FixtureContext(scratch, None, facts, reads)


def planted_reads(rows: Sequence[rows_grammar.Row]) -> tuple[tuple[str, str], ...]:
    """Return the name and the path of each read row of a plant (THE PLANTS).

    Raises:
        rows_grammar.RowsError: If a read row has a path that is not a relative path of the tree
    """
    reads: list[tuple[str, str]] = []
    for row in rows:
        if row.words[:1] != ("read",):
            continue
        if len(row.words) != 2 or len(row.cells) != 1:
            raise row.error("a read row is `read NAME | FILE`")
        file = row.cells[0]
        if not file or Path(file).is_absolute() or ".." in Path(file).parts:
            raise row.error(f"the file {file!r} of a read row is not a relative path of the tree")
        reads.append((row.words[1], file))
    return tuple(reads)


def planted_fixtures(rows: Sequence[rows_grammar.Row]) -> tuple[FixtureFacts, ...]:
    """Return the fixtures of the rows of a plant (THE PLANTS).

    Raises:
        rows_grammar.RowsError: If a row is not a fixture row, a finding row or a read row of the plant format
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
        elif row.words[:1] != ("read",):
            raise row.error(
                "a row is `fixture NAME | SOURCE | STAMP | CENSUS_STAMP`, `finding NAME | KIND | LINE | ENTITY` or "
                "`read NAME | FILE`"
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
