"""The runner of the engine plugin: the sections of the objects of a build, the configuration of the build, and each rule.

THE SECTIONS
    The quarantine plugin of utils/tools/quarantine/ writes the findings of
    each unit into the section .crucible.quarantine of its object
    (assay.readers.sections).  The runner reads the section of each object of
    the target all with a compile that loads the plugin (objects_of_all of
    assay.readers.census).  It reads each section one time for each call.  A
    finding is one finding line.  The same line from two objects counts one
    time, for example a finding of a header that two units include.  A line
    of the kind region is no finding: the census keeps the first line and the
    reason of each opt-out region of each file.

    A build of a configuration that only parses has no object
    (assay.core.configuration.is_parse_build).  `loop syntax` parses each
    unit of all there.  The runner reads the report file of each unit in its
    report directory (assay.readers.sections, THE REPORT OF A PARSE BUILD).
    The stamp file of that directory has the function of the stamp of a
    section.

THE PROBLEMS
    The census gives a problem for each object with a compile that loads the
    plugin and with no section.  It also gives a problem for each section
    with a stamp that is not the stamp of the compile command of its object.
    The stamp holds the plugin and the rule table.  A compiler cache that does
    not hash the stamp then gives the findings of a different plugin or rule
    table.  Each rule of a call gives each problem as an error, because the
    findings of the rule are then not complete.

THE CONFIGURATION
    The context gives the configuration of the build (assay.core.configuration,
    THE CONFIGURATION), with the compile commands of the objects of all.  A
    rule with rows of each configuration judges each build with the rows of
    THE POLICY of that module.

THE RULES
    Each finding line has a rule of the engine plugin of utils/registry.txt.
    A rule is the function findings(context) of its module,
    assay/guard/engines/plugin/ID.py with underscores for the hyphens.  Its
    errors are its violations, and assay.core.ledger judges them against the
    ledger of the rule (assay.guard.engines.meta.runner.judged).  A module
    that judges its ledger itself also gives judge(rule, context),
    run(rule, context, warnings_dir) and write(rule, request).  The module of
    the rule quarantine does that.

THE STATUS
    A run exits 0 with no error and 1 with an error.  It exits 77, SKIPPED of
    assay.guard.engines.meta.runner, when the rule does not apply to the
    build:
      * The call gives no build directory
      * The build has no build kind
      * The build made only some targets, or its generator is not Ninja
      * No compile of the target all loads the plugin.
    A build that the runner cannot read gives one error.

`guard selftest tests.guard.engines.plugin.test_runner` does a test of the module.
"""

from collections.abc import Callable, Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import git, proc
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import target as build_target
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import census as build_census
from assay.readers import compdb
from assay.readers import sections as quarantine_sections

PLUGIN_ARGUMENT = "-fplugin-arg-crucible_quarantine-"
STAMP_ARGUMENT = f"{PLUGIN_ARGUMENT}stamp="
# The threads that read the sections of the objects: the job count of the package (THE WORKER POOLS of
# assay.guard.engines.buildsys.job_pools).
READERS = proc.DEFAULT_JOBS
# The rule of the region lines of a section (the head comment of utils/tools/quarantine/plugin.h).
REGION_RULE = "quarantine"
SKIPPED = meta_runner.SKIPPED
NotApplicable = meta_runner.NotApplicable


class BuildError(Exception):
    """The runner cannot read the build: its compile database, the inputs of all, its CMake cache or its target."""


@dataclass(frozen=True, slots=True)
class Problem:
    """One problem of the census: an object with no section, a section of a different stamp, or a section that does
    not read."""

    path: str
    message: str


@dataclass(frozen=True, slots=True)
class ObjectSection:
    """The section of one object as the runner read it.

    `shown` is the object as a message shows it, `stamp` is the stamp of its compile command, `section` is the
    section or None, and `problem` is the text of a read that did not succeed.
    """

    shown: str
    stamp: str
    section: quarantine_sections.Section | None
    problem: str = ""


@dataclass(frozen=True, slots=True)
class Census:
    """The finding lines of the objects of all, the opt-out regions, and the problems of the objects.

    `findings` holds each finding line one time, with no line of the kind region.  `regions` holds the first line
    and the reason of each region of each file, sorted.  `commands` holds the compile command of each object.
    """

    objects: int
    findings: frozenset[quarantine_sections.Finding] = frozenset()
    regions: Mapping[str, tuple[tuple[int, str], ...]] = field(default_factory=dict)
    problems: tuple[Problem, ...] = ()
    commands: tuple[str, ...] = ()

    def of_rule(self, rule_id: str) -> list[quarantine_sections.Finding]:
        """Return the finding lines of one rule, sorted."""
        return sorted(item for item in self.findings if item.rule == rule_id)

    def errors(self, rule_id: str) -> list[check_report.Finding]:
        """Return each problem of the census as an error of one rule (THE PROBLEMS)."""
        return [check_report.Finding("error", problem.path, 0, rule_id, problem.message) for problem in self.problems]

    def line_errors(
        self, rule_id: str, message: Callable[[quarantine_sections.Finding], str]
    ) -> list[check_report.Finding]:
        """Return each problem of the census, and one error for each finding line of one rule.

        Args:
            rule_id: The rule of the finding lines
            message: The function that gives the message of the error of one finding line
        """
        return [
            *self.errors(rule_id),
            *(
                check_report.Finding("error", item.file, item.line, rule_id, message(item), item.kind, item.entity)
                for item in self.of_rule(rule_id)
            ),
        ]


@dataclass(frozen=True, slots=True)
class WriteRequest:
    """What one write of the ledger of a plugin rule does.

    `mode` is the mode of assay.core.ledger.  `add_configuration` writes the configuration of the build with its
    name, and `import_log` puts the rows of a configuration from a log into the ledger, with no build.
    """

    root: Path
    build_dir: Path | None
    mode: rule_ledger.WriteMode = rule_ledger.WriteMode.LOWER
    reason: str | None = None
    add_configuration: bool = False
    import_log: Path | None = None


# ── The sections ───────────────────────────────────────────────────


def stamp_of(command: str) -> str | None:
    """Return the value of the plugin argument stamp= of a compile command, or None when the command has none."""
    start = command.find(STAMP_ARGUMENT)
    if start < 0:
        return None
    rest = command[start + len(STAMP_ARGUMENT) :]
    return rest.split(None, 1)[0] if rest and not rest[0].isspace() else ""


def plugin_objects(build_dir: Path, root: Path, has_objects: bool = True) -> list[tuple[Path, str, str]]:
    """Return each object of all with a compile that loads the plugin, with the stamp and the compile command.

    has_objects is False for a build of a configuration that only parses, which has no object.  Complexity: linear
    in the rows of the compile database and the inputs of all.

    Raises:
        NotApplicable: If the build made only some targets, or its generator is not Ninja
        BuildError: If the compile database or the inputs of all cannot be read
    """
    try:
        objects = {str(path) for path, _ in build_census.objects_of_all(build_dir, root, has_objects)}
        rows = compdb.entries(build_dir)
    except build_census.NotApplicable as reason:
        raise NotApplicable(str(reason)) from None
    except build_census.CensusError as problem:
        raise BuildError(str(problem)) from None
    except compdb.CompdbError as problem:
        raise BuildError(f"the compile database cannot be read: {problem}") from None
    found: list[tuple[Path, str, str]] = []
    for row in rows:
        output = row.output or row.directory
        stamp = stamp_of(row.command)
        if output in objects and stamp is not None:
            found.append((Path(output), stamp, row.command))
    return sorted(found)


def read_object(path: Path, stamp: str, build_dir: Path) -> ObjectSection:
    """Read the section of one object.  A read that does not succeed gives its problem."""
    shown = build_census.shown(path, build_dir)
    try:
        section = quarantine_sections.read(path)
    except (OSError, quarantine_sections.SectionError) as problem:
        return ObjectSection(shown, stamp, None, str(problem))
    return ObjectSection(shown, stamp, section)


def read_parsed(path: Path, stamp: str, build_dir: Path) -> ObjectSection:
    """Read the report of one unit of a parse build (THE SECTIONS).  A missing report or a different stamp is a problem."""
    shown = build_census.shown(path, build_dir)
    rerun = f"Run `python3 utils/bin/loop syntax {build_dir}` again"
    try:
        section = quarantine_sections.read_report(quarantine_sections.report_directory(build_dir, path))
    except (OSError, quarantine_sections.SectionError) as problem:
        return ObjectSection(shown, stamp, None, f"{problem}.  {rerun}")
    if section is None:
        return ObjectSection(shown, stamp, None, f"the parse build has no report of the unit of this object.  {rerun}")
    if section.stamp != stamp:
        return ObjectSection(
            shown,
            stamp,
            None,
            f"the report holds the stamp {section.stamp!r}, and the compile command has the stamp {stamp!r}.  The "
            f"plugin, the rule table or the configure step changed after the parse.  {rerun}",
        )
    return ObjectSection(shown, stamp, section)


def census_of(items: Sequence[ObjectSection], commands: Sequence[str] = ()) -> Census:
    """Return the census of the sections of the objects: each finding line one time, the regions and the problems.

    A test or a witness can give planted sections here.  Complexity: linear in the finding lines of the sections.
    """
    findings: set[quarantine_sections.Finding] = set()
    problems: list[Problem] = []
    # The file, the first line and the reason of each region of each unit.
    regions: set[tuple[str, int, str]] = set()
    for item in items:
        if item.problem:
            problems.append(Problem(item.shown, item.problem))
        elif item.section is None:
            problems.append(
                Problem(
                    item.shown,
                    "the compile of the object loads the quarantine plugin, and the "
                    "object holds no section .crucible.quarantine.  Compile it again",
                )
            )
        elif item.section.stamp != item.stamp:
            problems.append(
                Problem(
                    item.shown,
                    f"the section holds the stamp {item.section.stamp!r}, and the "
                    f"compile command has the stamp {item.stamp!r}.  The compiler cache "
                    f"gave the findings of a different plugin or rule table: its key "
                    f"must hold the stamp argument",
                )
            )
        else:
            for found in item.section.findings:
                if found.kind == quarantine_sections.REGION:
                    regions.add((found.file, found.line, " ".join(found.entity.split())))
                else:
                    findings.add(found)
    grouped: dict[str, list[tuple[int, str]]] = {}
    for file, line, reason in sorted(regions):
        grouped.setdefault(file, []).append((line, reason))
    return Census(
        len(items),
        frozenset(findings),
        {file: tuple(starts) for file, starts in grouped.items()},
        tuple(problems),
        tuple(commands),
    )


def read_census(build_dir: Path, root: Path) -> Census:
    """Read the section of each object of all with a compile that loads the plugin (THE SECTIONS).

    Complexity: linear in the sections and the finding lines of the objects.

    Raises:
        NotApplicable: If the build has no build kind or made only some targets.  Also if its generator is not
            Ninja, or no compile of all loads the plugin
        BuildError: If the compile database or the inputs of all cannot be read
    """
    from concurrent.futures import ThreadPoolExecutor

    if build_target.kind_of(build_dir) is None:
        raise NotApplicable(
            f"{build_census.shown(build_dir, root)} has no build-kind.txt, which cmake/BuildLauncher.cmake writes"
        )
    is_parse = build_config.is_parse_build(build_dir)
    objects = plugin_objects(build_dir, root, has_objects=not is_parse)
    if not objects:
        raise NotApplicable("no compile of the target all loads the quarantine plugin")
    reader = read_parsed if is_parse else read_object
    with ThreadPoolExecutor(max_workers=READERS) as pool:
        items = list(pool.map(lambda item: reader(item[0], item[1], build_dir), objects))
    return census_of(items, [command for _, _, command in objects])


# ── The context ────────────────────────────────────────────────────


class PluginContext:
    """The build that the rules of the engine plugin read: the root of the tree, the build directory and its census.

    The context reads the census and the configuration one time, on the first use.  A test or a witness can give a
    planted census and a planted configuration, and then no build directory.
    """

    __slots__ = ("_census", "_configuration", "build_dir", "root")

    def __init__(
        self,
        root: Path,
        build_dir: Path | None = None,
        census: Census | None = None,
        configuration: build_config.BuildConfiguration | None = None,
    ) -> None:
        """Keep the root, the build directory, and a planted census and configuration."""
        self.root = root
        self.build_dir = build_dir
        self._census = census
        self._configuration = configuration

    def build_shown(self) -> str:
        """Return the build directory as a message shows it, relative to the root when the root holds it."""
        return build_census.shown(self.build_dir, self.root) if self.build_dir is not None else "-"

    def census(self) -> Census:
        """Return the census of the build (THE SECTIONS).

        Raises:
            NotApplicable: If the call gives no build directory, or the rules do not apply to the build
            BuildError: If the build cannot be read
        """
        if self._census is None:
            if self.build_dir is None:
                raise NotApplicable(
                    "the engine plugin reads the objects of a build, and the call gives no --build-dir DIR"
                )
            self._census = read_census(self.build_dir, self.root)
        return self._census

    def configuration(self) -> build_config.BuildConfiguration:
        """Return the configuration of the build (THE CONFIGURATION).

        Raises:
            NotApplicable: If the call gives no build directory, or the rules do not apply to the build
            BuildError: If the build cannot be read, or the compiler does not print its target
        """
        if self._configuration is None:
            if self.build_dir is None:
                raise NotApplicable(
                    "the engine plugin reads the configuration of a build, and the call gives no --build-dir DIR"
                )
            try:
                self._configuration = build_config.read_configuration(self.build_dir, self.census().commands)
            except build_config.ConfigurationError as problem:
                raise BuildError(str(problem)) from None
        return self._configuration

    def findings_of(self, rule: rule_registry.Rule) -> list[check_report.Finding]:
        """Return the findings of the function findings(context) of the module of a rule.

        Raises:
            NotApplicable: If the rule does not apply to the build
            BuildError: If the build cannot be read
        """
        return list(meta_runner.rule_module(rule).findings(self))


# ── The runs ───────────────────────────────────────────────────────


def census_or_status(rule: rule_registry.Rule, context: PluginContext, warnings_dir: Path | None) -> Census | int:
    """Return the census of the context, or the status of a rule that cannot read it, after its output.

    A rule that does not apply writes one line and gives SKIPPED.  A build that cannot be read gives one error.
    """
    try:
        return context.census()
    except NotApplicable as reason:
        check_report.write_line(f"{rule.id}: {reason}.  The rule does not apply")
        return SKIPPED
    except BuildError as problem:
        return check_report.emit(
            [check_report.Finding("error", context.build_shown(), 0, rule.id, str(problem))], rule.id, warnings_dir
        )


def run_rule(rule: rule_registry.Rule, context: PluginContext, warnings_dir: Path | None) -> int:
    """Run one rule on the build of the context, judge its findings against its ledger and write them (THE RULES).

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    own_run = getattr(meta_runner.rule_module(rule), "run", None)
    if own_run is not None:
        return int(own_run(rule, context, warnings_dir))
    census = census_or_status(rule, context, warnings_dir)
    if isinstance(census, int):
        return census
    return meta_runner.run(rule, context, warnings_dir)


def run(
    rules: Sequence[rule_registry.Rule], root: Path, build_dir: Path | None, warnings_dir: Path | None
) -> list[int]:
    """Run the rules on one context, and give the status of each.  The call then reads each section one time."""
    context = PluginContext(root, build_dir)
    return [run_rule(rule, context, warnings_dir) for rule in rules]


def judged(rule: rule_registry.Rule, context: PluginContext) -> list[check_report.Finding]:
    """Return the findings of one rule judged against its ledger in the tree of the context.

    Raises:
        NotApplicable: If the rule does not apply to the build
        BuildError: If the build cannot be read
    """
    return meta_runner.module_judged(rule, context)


def write(rule: rule_registry.Rule, request: WriteRequest) -> int:
    """Write the ledger of one plugin rule.

    A module that writes its ledger itself writes it.  For each other rule, assay.core.ledger writes it.

    Returns:
        0 when the ledger is written.  1 when the mode refuses a finding or the build cannot be read.  2 on a usage
        error.  SKIPPED when the rule does not apply
    """
    own_write = getattr(meta_runner.rule_module(rule), "write", None)
    if own_write is not None:
        return int(own_write(rule, request))
    if request.add_configuration or request.import_log is not None:
        check_report.write_line(
            f"guard write: the rule {rule.id} has no configurations, so --add-configuration and "
            f"--import do not apply to it",
            is_error=True,
        )
        return 2
    context = PluginContext(request.root, request.build_dir)
    try:
        written = rule_ledger.write(
            rule,
            rule_ledger.read(rule, request.root),
            context.findings_of(rule),
            mode=request.mode,
            reason=request.reason,
            moved=git.moves(request.root),
        )
    except NotApplicable as reason:
        check_report.write_line(f"guard write: the rule {rule.id} does not apply: {reason}", is_error=True)
        return SKIPPED
    except (BuildError, rule_ledger.LedgerError, rows_grammar.RowsError, OSError) as problem:
        check_report.write_line(f"guard write: the ledger of {rule.id} is not written: {problem}", is_error=True)
        return 1
    rule_ledger.store(written, request.root)
    rows = len(written.base) + sum(len(table) for table in written.deltas.values())
    check_report.write_line(f"guard write: {rule_ledger.shown_path(rule.id)} holds {rows} rows")
    return 0


def print_findings(root: Path, build_dir: Path, place: str | None, kind: str | None) -> int:
    """Write each finding line and each region line of a build, of a file or a directory, and of one kind.

    Returns:
        0, or SKIPPED when the build does not apply, or 1 when the build cannot be read
    """
    context = PluginContext(root, build_dir)
    try:
        census = context.census()
    except NotApplicable as reason:
        check_report.write_line(f"guard list: {reason}.  The build does not apply")
        return SKIPPED
    except BuildError as problem:
        check_report.write_line(f"guard list: the build cannot be read: {problem}", is_error=True)
        return 1
    lines = [
        *census.findings,
        *(
            quarantine_sections.Finding(file, line, 1, REGION_RULE, quarantine_sections.REGION, reason)
            for file, items in census.regions.items()
            for line, reason in items
        ),
    ]
    prefix = (place or "").rstrip("/")
    selected = sorted(
        item
        for item in lines
        if (not prefix or item.file == prefix or item.file.startswith(f"{prefix}/"))
        and (kind is None or item.kind == kind)
    )
    for item in selected:
        check_report.write_line(item.text())
    check_report.write_line(f"guard list: {len(selected)} findings")
    return 0
