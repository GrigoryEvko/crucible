"""fixtures — the census of the negative fixtures of a build, which the rule quarantine counts with its objects.

THE CENSUS
    The fixture driver keeps the quarantine plugin in the compile of each
    negative fixture.  The record of each run holds the findings of the
    plugin in the source of the fixture, and the stamp argument of the plugin
    (assay.loop.fixture.store, The quarantine plugin).  The census of a build
    is the census of each fixture with a compile that loads the plugin
    (assay.readers.census.fixtures_of, assay.core.record).  Each finding of a
    census of a kind of THE KINDS becomes one finding line of its source, as
    a line of a section does (assay.readers.sections.Finding).  The same line
    of two fixtures with one source counts one time.  A line of the kind
    region gives the first line and the reason of a region of the source,
    with one space between each two words of the reason.

THE SOURCE
    The plugin names each file relative to its argument root=, the source
    root of the build.  The census names the source of a fixture in the same
    way, relative to the argument root= of the compile commands of the
    fixtures.  A check from a different checkout then gives the keys of the
    build.  A planted build names its sources relative to its root.

THE KINDS
    The rule quarantine counts each kind of its row of the registry in the
    census, and the four kinds of PROBE_KINDS: detail-use, lifetime-start,
    unchecked-use and door-use.  For its probe, a fixture can use a private
    namespace of the base, or a lifetime that std::start_lifetime_as starts.
    It can also use an unchecked access, or a second definition of a door
    owner.  The line of such a kind has the rule of the registry that gives
    the kind.  The rule quarantine counts these kinds only in the census,
    because the rule of each kind holds its findings in the objects of all.
    It counts no other kind of the census.  Test code can use the testing
    door, so the rule does not count testing-use.  It does not count the
    kinds of the rules mint, permission-storage and proof-routes, or
    the kind outside-home, because a fixture with such a finding probes the
    rule of that kind.

NO FIXTURE RUN
    A build with no record of a fixture has no census, and the rule counts
    its objects only.  A parse build has no fixture run
    (assay.core.configuration.is_parse_build).  The context then lists no
    test and reads no dependency log.  Such a build refuses no row of a
    directory of fixtures, and a census is not necessary.  A reduce takes the
    largest count over its builds (THE REDUCE of assay.core.ledger).  Thus a
    reduce in which no build of objects has a fixture run removes each row of
    a fixture.  reduce_refusal() refuses such a reduce when the build has
    fixtures with the plugin.

THE PROBLEMS
    For each fixture with a compile that loads the plugin, these conditions
    are problems:
      * the fixture has no record, and a different fixture has one
      * the record holds no census.  The compile wrote no log, or it
        succeeded and its object holds no section of the plugin, or an
        earlier driver wrote the record
      * the census holds a stamp that is not the stamp argument of the
        compile command of the fixture.  The plugin, the rule table or the
        configure changed after the run
      * the record cannot be read.
    Each problem is an error with no kind, and `guard write` writes no ledger
    with one.  An opted-out line with no region at or before its line is a
    problem of the rule quarantine (THE OPT-OUT REGIONS of that module).

THE FILES OF THE FIXTURES
    The census of a fixture holds the findings of its source only.  A file
    that only the compiles of fixtures read then has findings that no row
    counts.  file_problems() reads the files of each unit
    (assay.readers.census.read_census).  Such a file is in the source root or
    in the build directory, a fixture compile reads it, and no object of all
    reads it.  The source of a fixture is no such file.  A file in the
    directory of the argument stamps= of the plugin, a mode stamp of
    assay.loop.stamps, is no such file too.  Each such file gives an error
    with no kind.  Give the file a compile in all, as test/layer gives each
    header of include/, or include it from a unit of all.  A census with a
    problem gets no such check, because the problem fails the rule first.
    The census cannot read the files of the objects of a build that made
    only some targets.  The function then prints the reason and gives no
    error.

THE CONTEXT
    FixtureContext is the context of the rule quarantine.  It reads the
    census of the objects through the context of the engine (base), or it
    keeps the census of a plant.  objects() gives that census.  census()
    gives it with the number of the units, the objects and the fixtures with
    a census, for the summary line.  part() gives the census of the fixtures.
    The context reads each census one time.

THE PLANTS
    A plant of the rule quarantine can name a file of planted fixtures with
    a row `fixtures FILE` of its manifest (assay.guard.engines.plugin.witness).
    The rows of FILE (the rows grammar of assay.core.rows):
        fixture NAME | SOURCE | STAMP | CENSUS_STAMP
                The fixture NAME with the source SOURCE.  STAMP is the stamp
                argument of its compile command, "-" for a command with no
                plugin.  CENSUS_STAMP is the stamp of its census, "-" for a
                record with no census, and the word missing for no record
        finding NAME | KIND | LINE | ENTITY
                One finding of the census of the fixture NAME
        read NAME | FILE
                The compile of the fixture NAME reads FILE, a path of the
                tree.  The name all gives a file that an object of the
                target all reads (THE FILES OF THE FIXTURES)
        read stamps | DIRECTORY
                DIRECTORY, a path of the tree, is the directory of the mode
                stamps that an argument stamps= of the plugin names
        parse
                The planted build is a parse build (NO FIXTURE RUN)

Complexity: linear in the fixtures of the build, the findings of their records and the files of the units.
"""

import dataclasses
import os
from collections.abc import Iterable, Mapping, Sequence
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import record as cost_record
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import ruletable
from assay.guard.engines.plugin import runner
from assay.readers import census as build_census
from assay.readers import compdb
from assay.readers import sections as plugin_sections

# The rule that counts the census (assay.guard.engines.plugin.quarantine).
RULE = "quarantine"
# The kinds of a probe that the rule counts in the census, and the rule of the registry that gives each one (THE KINDS).
PROBE_KINDS = {
    "detail-use": "owners",
    "lifetime-start": "owners",
    "unchecked-use": "owners",
    "door-use": "owners",
}
# The census stamp of a planted fixture with no record (THE PLANTS).
MISSING = "missing"
# The name of a read row of a plant for a file that an object of the target all reads (THE PLANTS).
ALL_UNITS = "all"
# The name of a read row of a plant for the directory of the mode stamps (THE PLANTS).
STAMPS_UNIT = "stamps"
# The row of a plant that makes the planted build a parse build (THE PLANTS).
PARSE_ROW = "parse"
# The plugin arguments of the directory of the mode stamps and of the source root.
STAMPS_ARGUMENT = f"{runner.PLUGIN_ARGUMENT}stamps="
ROOT_ARGUMENT = f"{runner.PLUGIN_ARGUMENT}root="


@dataclasses.dataclass(frozen=True, slots=True)
class FixtureFacts:
    """One fixture of the build: its name, its source, the stamp of its command, and the census of its record.

    `source` is the source as a message shows it, relative to the source root (THE SOURCE).  `stamp` is the stamp
    argument of the compile command, or None when the command loads no plugin.  `census_stamp` and `census` are those
    of the record, each None when the record holds no census.  `problem` tells why the record cannot be read, MISSING
    for no record, or is empty.
    """

    name: str
    source: str
    stamp: str | None
    census_stamp: str | None
    census: tuple[cost_record.CensusFinding, ...] | None
    problem: str = ""


@dataclasses.dataclass(frozen=True, slots=True)
class FixturePart:
    """The census of the fixtures of a build (THE CENSUS).

    `fixtures` is the number of fixtures with a census.  `lines` holds each finding line of a kind of THE KINDS one
    time.  `regions` holds the first line and the reason of each region of each source, sorted.  `problems` holds an
    error with no kind for each problem (THE PROBLEMS).  `source_root` is the root of the sources (THE SOURCE).
    """

    fixtures: int = 0
    lines: frozenset[plugin_sections.Finding] = frozenset()
    regions: Mapping[str, tuple[tuple[int, str], ...]] = dataclasses.field(default_factory=dict)
    problems: tuple[check_report.Finding, ...] = ()
    source_root: Path | None = None


class FixtureContext(runner.PluginContext):
    """The context of the rule quarantine: the census of the objects of a build and the census of its fixtures.

    THE CONTEXT gives the parts.  A plant gives a census of objects, planted fixtures and planted reads, and then no
    build directory (THE PLANTS).
    """

    __slots__ = ("_base", "_facts", "_part", "_planted", "_reads", "is_parse")

    def __init__(
        self,
        root: Path,
        build_dir: Path | None = None,
        census: runner.Census | None = None,
        *,
        base: runner.PluginContext | None = None,
        fixtures: Sequence[FixtureFacts] | None = None,
        reads: Sequence[tuple[str, str]] = (),
        is_parse: bool = False,
    ) -> None:
        """Keep the root, the build directory, and the census of the objects, a base context or planted fixtures.

        Args:
            root: The root of the tree of the ledger
            build_dir: The build directory, or None for a plant
            census: A planted census of the objects
            base: The context of the engine, which reads the census of the objects
            fixtures: The planted fixtures, or None to read the fixtures of the build
            reads: The read rows of a plant: the name of a fixture, ALL_UNITS or STAMPS_UNIT, and a path of the tree
            is_parse: The planted build is a parse build (NO FIXTURE RUN)
        """
        super().__init__(root, build_dir, census)
        self._base = base
        self._planted = tuple(fixtures) if fixtures is not None else None
        self._reads = tuple(reads)
        self.is_parse = is_parse
        self._part: FixturePart | None = None
        self._facts: tuple[FixtureFacts, ...] = ()

    def objects(self) -> runner.Census:
        """Return the census of the objects of the build (THE CONTEXT).

        Raises:
            NotApplicable: If the call gives no build directory, or the rules do not apply to the build
            BuildError: If the build cannot be read
        """
        return self._base.census() if self._base is not None else super().census()

    def census(self) -> runner.Census:
        """Return the census of the objects, with the number of the objects and of the fixtures with a census.

        Raises:
            NotApplicable: If the call gives no build directory, or the rules do not apply to the build
            BuildError: If the build cannot be read
        """
        objects = self.objects()
        return dataclasses.replace(objects, objects=objects.objects + self.part().fixtures)

    def part(self) -> FixturePart:
        """Return the census of the fixtures (THE CENSUS), or an empty census with no fixture run (NO FIXTURE RUN).

        Raises:
            BuildError: If the test list, the compile database or the registry cannot be read
        """
        if self._part is None:
            self._part = self.read_part()
        return self._part

    def read_part(self) -> FixturePart:
        """Read the census of the planted fixtures or of the fixtures of the build, one time.

        Raises:
            BuildError: If the test list, the compile database or the registry cannot be read
        """
        if self.is_parse:
            return FixturePart()
        if self._planted is not None:
            facts, source_root = self._planted, self.root
        elif self.build_dir is None or build_config.is_parse_build(self.build_dir) or not has_records(self.build_dir):
            return FixturePart()
        else:
            facts, source_root = read_fixtures(self.build_dir, self.root)
        try:
            kinds = counted_kinds(rule_registry.load())
        except (OSError, rows_grammar.RowsError, KeyError) as problem:
            raise runner.BuildError(f"the kinds of the census cannot be read: {problem}") from None
        self._facts = facts
        return census_of(facts, kinds, self.build_shown(), source_root)

    def file_problems(self) -> tuple[check_report.Finding, ...]:
        """Return an error for each file that only the compiles of fixtures read (THE FILES OF THE FIXTURES).

        A build with no fixture run gives none, and so does a census with a problem, which fails the rule first.  A
        plant gives its read rows, and a build gives the files of the census of its units.  A census that cannot be
        read gives one error.

        Raises:
            BuildError: If the test list, the compile database or the registry cannot be read
        """
        part = self.part()
        root = Path(os.path.realpath(part.source_root or self.root))
        if not part.fixtures or part.problems:
            return ()
        sources = frozenset(os.path.realpath(root / item.source) for item in self._facts)
        if self._planted is not None:
            real = {(name, os.path.realpath(root / path)) for name, path in self._reads}
            fixture_reads = sorted((name, path) for name, path in real if name not in (ALL_UNITS, STAMPS_UNIT))
            objects = frozenset(path for name, path in real if name == ALL_UNITS)
            stamps = tuple(sorted(path for name, path in real if name == STAMPS_UNIT))
            return unjudged_findings(fixture_reads, objects, sources, stamps, root, None)
        if self.build_dir is None:
            return ()
        names = [(item.name, os.path.join(root, item.source)) for item in self._facts]
        try:
            units = build_census.read_census(self.build_dir, root, fixtures=names).units
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
        return unjudged_findings(fixture_reads, objects, sources, stamp_directories, root, self.build_dir)


def context_of(context: runner.PluginContext) -> FixtureContext:
    """Return the context of the rule quarantine for the context of the engine, or the context itself."""
    if isinstance(context, FixtureContext):
        return context
    return FixtureContext(context.root, context.build_dir, base=context)


# ── The census ─────────────────────────────────────────────────────


def counted_kinds(registry: rule_registry.Registry) -> frozenset[str]:
    """Return the kinds that the rule counts in a census: each kind of its row and each kind of PROBE_KINDS.

    Raises:
        KeyError: If the registry has no rule quarantine, or a rule of PROBE_KINDS does not give its kind
    """
    for kind, rule_id in PROBE_KINDS.items():
        if kind not in registry.rules[rule_id].kinds:
            raise KeyError(f"the rule {rule_id} of the registry gives no kind {kind}")
    return frozenset((*registry.rules[RULE].kinds, *PROBE_KINDS))


def has_records(build_dir: Path) -> bool:
    """Return True when the build directory holds the record of a run of a fixture (NO FIXTURE RUN).

    Complexity: linear in the directories of the records.
    """
    directory = build_dir / cost_record.FIXTURE_DIRECTORY
    return bool(sorted(directory.glob(f"*/*{cost_record.FIXTURE_SUFFIX}")))


def root_of(words: Iterable[str]) -> str:
    """Return the value of the plugin argument root= of the words of a compile command, or an empty text."""
    return next((word[len(ROOT_ARGUMENT) :] for word in words if word.startswith(ROOT_ARGUMENT)), "")


def read_fixtures(build_dir: Path, root: Path) -> tuple[tuple[FixtureFacts, ...], Path]:
    """Return the facts of each fixture of the build, and the source root of the build (THE SOURCE).

    The source root is the argument root= of the compile commands of the fixtures, or `root` when no fixture command
    gives one.  Complexity: linear in the tests of the build, the rows of the compile database and the records.

    Raises:
        BuildError: If the test list or the compile database cannot be read
    """
    try:
        fixtures = build_census.fixtures_of(build_dir)
        commands = {os.path.realpath(entry.file): entry for entry in compdb.entries(build_dir)}
    except build_census.CensusError as problem:
        raise runner.BuildError(str(problem)) from None
    except compdb.CompdbError as problem:
        raise runner.BuildError(f"the compile database cannot be read: {problem}") from None
    entries = [(name, source, commands.get(os.path.realpath(source))) for name, source in fixtures]
    roots = sorted({found for _, _, entry in entries if entry is not None and (found := root_of(entry.argv()))})
    source_root = Path(os.path.realpath(roots[0] if roots else root))
    facts: list[FixtureFacts] = []
    for name, source, entry in entries:
        stamp = runner.stamp_of(entry.command) if entry is not None else None
        shown = build_census.shown(os.path.realpath(source), source_root)
        try:
            record = cost_record.read_fixture(Path(cost_record.fixture_record_file(build_dir, name)))
        except cost_record.MissingRecord:
            facts.append(FixtureFacts(name, shown, stamp, None, None, MISSING))
            continue
        except cost_record.RecordError as problem:
            facts.append(FixtureFacts(name, shown, stamp, None, None, str(problem)))
            continue
        facts.append(FixtureFacts(name, shown, stamp, record.census_stamp, record.census))
    return tuple(facts), source_root


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


def unsettled_fixtures(build_dir: Path, root: Path) -> frozenset[str]:
    """Return the name of each fixture with the plugin whose record census_of() refuses (THE PROBLEMS).

    A run of such a fixture writes its record again with the stamp of its compile command.  The problems are a census
    of a different stamp, a record with no census, and a record that does not read.  When the build holds the record
    of a fixture with the plugin, a fixture with no record is a problem too (NO FIXTURE RUN).  `loop test` runs each
    such fixture also with --only (assay.loop.affected).  Complexity: the cost of read_fixtures().

    Raises:
        runner.BuildError: If the test list or the compile database cannot be read
    """
    facts, _ = read_fixtures(build_dir, root)
    counted = [item for item in facts if item.stamp is not None]
    missing = [item.name for item in counted if item.problem == MISSING]
    if len(missing) == len(counted):
        return frozenset()
    return frozenset(item.name for item in counted if item.problem == MISSING or problem_of(item, ""))


def census_of(
    fixtures: Sequence[FixtureFacts], kinds: frozenset[str], build_shown: str, source_root: Path
) -> FixturePart:
    """Return the census of the fixtures: the lines, the regions and the problems (THE CENSUS, THE PROBLEMS).

    A build with no fixture with the plugin, or with no record of such a fixture, has no census (NO FIXTURE RUN).
    Complexity: linear in the fixtures and the findings of their censuses.
    """
    counted = [item for item in fixtures if item.stamp is not None]
    missing = [item.name for item in counted if item.problem == MISSING]
    if len(missing) == len(counted):
        return FixturePart(source_root=source_root)
    problems: list[check_report.Finding] = []
    if missing:
        problems.append(
            check_report.Finding(
                "error",
                build_shown,
                0,
                RULE,
                f"{len(missing)} of the {len(counted)} fixtures with the plugin have no record, because they did not "
                f"run in this build directory: {build_census.names_text(missing)}.  Run every fixture, then the check",
            )
        )
    lines: set[plugin_sections.Finding] = set()
    regions: set[tuple[str, int, str]] = set()
    for item in counted:
        problem = problem_of(item, build_shown)
        if problem:
            problems.append(check_report.Finding("error", item.source, 0, RULE, problem))
            continue
        for kind, line, column, entity in item.census or ():
            if kind == plugin_sections.REGION:
                regions.add((item.source, line, plugin_sections.quarantine_entity(kind, entity).text))
                continue
            base_kind = kind.partition(":")[0]
            if base_kind in kinds:
                rule_id = PROBE_KINDS.get(base_kind, RULE)
                lines.add(plugin_sections.Finding(item.source, line, column, rule_id, kind, entity))
    grouped: dict[str, list[tuple[int, str]]] = {}
    for file, line, reason in sorted(regions):
        grouped.setdefault(file, []).append((line, reason))
    return FixturePart(
        len(counted) - len(missing),
        frozenset(lines),
        {file: tuple(starts) for file, starts in grouped.items()},
        tuple(problems),
        source_root,
    )


def reduce_refusal(root: Path, builds: Sequence[Path]) -> str:
    """Return why a reduce over the builds cannot lower the rows of the fixtures, or an empty text (NO FIXTURE RUN).

    A reduce in which a build of objects has a fixture run, and a reduce of builds with no fixture with the plugin, can
    lower the rows.  Complexity: one walk of the records of each build, and one test list for a build with none.
    """
    objects = [build for build in builds if not build_config.is_parse_build(build)]
    if any(has_records(build) for build in objects):
        return ""
    for build in objects:
        try:
            facts, _ = read_fixtures(build, root)
        except runner.BuildError as problem:
            return f"the fixtures of {build} cannot be read: {problem}"
        plugged = [item.name for item in facts if item.stamp is not None]
        if plugged:
            return (
                f"no build of objects of the reduce has a fixture run, and {build} has {len(plugged)} fixtures with the "
                f"plugin, such as {build_census.names_text(plugged)}.  Such a reduce removes each row of a fixture.  "
                f"Run the fixtures of the builds first"
            )
    return ""


# ── The files of the fixtures ──────────────────────────────────────


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
            f"target all reads it.  The census of a fixture counts its source only, so no row counts the findings of "
            f"the file.  Give it a compile in all, as test/layer gives each header of include/, or include it from a "
            f"unit of all",
        )
        for path, names in sorted(readers.items())
    )


# ── The plants ─────────────────────────────────────────────────────


def plant_context(root: Path, census: runner.Census, path: Path) -> FixtureContext:
    """Return the context of a plant: the census of its judged compiles and its planted fixtures (THE PLANTS).

    Raises:
        rows_grammar.RowsError: If the file of the planted fixtures does not obey its rows
        OSError: If the file cannot be read
    """
    rows = rows_grammar.read(path, path.name)
    facts = planted_fixtures(rows)
    reads = planted_reads(rows)
    unknown = sorted({name for name, _ in reads} - {item.name for item in facts} - {ALL_UNITS, STAMPS_UNIT})
    if unknown:
        raise rows_grammar.RowsError(path.name, 0, f"a read row names no fixture of the plant: {', '.join(unknown)}")
    is_parse = any(row.words == (PARSE_ROW,) and not row.cells for row in rows)
    return FixtureContext(root, None, census, fixtures=facts, reads=reads, is_parse=is_parse)


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
        rows_grammar.RowsError: If a row is not a fixture row, a finding row, a read row or the parse row
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
        elif row.words[:1] != ("read",) and not (row.words == (PARSE_ROW,) and not row.cells):
            raise row.error(
                "a row is `fixture NAME | SOURCE | STAMP | CENSUS_STAMP`, `finding NAME | KIND | LINE | ENTITY`, "
                "`read NAME | FILE` or `parse`"
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
