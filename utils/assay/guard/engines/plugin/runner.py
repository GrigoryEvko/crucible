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

THE PROBLEMS
    The census gives a problem for each object with a compile that loads the
    plugin and with no section.  It also gives a problem for each section
    with a stamp that is not the stamp of the compile command of its object.
    The stamp holds the plugin and the rule table.  A compiler cache that does
    not hash the stamp then gives the findings of a different plugin or rule
    table.  Each rule of a call gives each problem as an error, because the
    findings of the rule are then not complete.

THE CONFIGURATION
    A configuration is the build kind and the target tier of
    utils/assay/core/target.py, and each cache variable of CONFIGURATION.  A
    target can add a native flag to its own compile, as each bench target
    adds -march=native.  The tier then also holds the native tier of the
    host.  A different configuration compiles other objects or other
    preprocessor arms, and its findings are then different.

    The name of a configuration is its build kind, with -bench when
    CRUCIBLE_BENCH is on and -fuzz when CRUCIBLE_FUZZ is on, for example
    x86_64-debug-asan.  CRUCIBLE_SENSE_HUB_V2 is no key.  Each build compiles
    the SenseHub v2 sources in two object libraries, with and without
    CRUCIBLE_SENSE_HUB_EXTENDED, and that option then changes no finding line.

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

from collections.abc import Callable, Iterable, Mapping, Sequence
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import git
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core import target as build_target
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import census as build_census
from assay.readers import cmake, compdb
from assay.readers import sections as quarantine_sections

PLUGIN_ARGUMENT = "-fplugin-arg-crucible_quarantine-"
STAMP_ARGUMENT = f"{PLUGIN_ARGUMENT}stamp="
KIND_KEY = "kind"
TIER_KEY = "tier"
# The cache variables that change the objects of all or their preprocessor arms.
CONFIGURATION = (
    "CMAKE_BUILD_TYPE",
    "CMAKE_CXX_FLAGS",
    "CRUCIBLE_SANITIZE",
    "CRUCIBLE_TSAN",
    "CRUCIBLE_UBSAN_STRICT",
    "CRUCIBLE_ANALYZER",
    "CRUCIBLE_VERIFY",
    "CRUCIBLE_PGO",
    "CRUCIBLE_BENCH",
    "CRUCIBLE_FUZZ",
    "CRUCIBLE_EXAMPLES",
    "CRUCIBLE_HAVE_BPF",
    "CRUCIBLE_SENSE_HUB_EXTENDED",
    "CRUCIBLE_VALGRIND_INCLUDE",
    "TORCH_DIR",
)
# The two options that add a word to the name of a configuration.
NAME_OPTIONS = (("bench", "CRUCIBLE_BENCH"), ("fuzz", "CRUCIBLE_FUZZ"))
CMAKE_FALSE = frozenset({"", "0", "OFF", "NO", "FALSE", "N", "IGNORE", "NOTFOUND"})
# The threads that read the sections of the objects.
READERS = 16
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
class BuildConfiguration:
    """The name of the configuration of a build and its facts, as KEY VALUE pairs (THE CONFIGURATION)."""

    name: str
    facts: tuple[tuple[str, str], ...]

    @property
    def tier(self) -> str:
        """The target tier of the build."""
        return dict(self.facts).get(TIER_KEY, "?")


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


def plugin_objects(build_dir: Path, root: Path) -> list[tuple[Path, str, str]]:
    """Return each object of all with a compile that loads the plugin, with the stamp and the compile command.

    Complexity: linear in the rows of the compile database and the inputs of all.

    Raises:
        NotApplicable: If the build made only some targets, or its generator is not Ninja
        BuildError: If the compile database or the inputs of all cannot be read
    """
    try:
        objects = {str(path) for path, _ in build_census.objects_of_all(build_dir, root)}
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
    objects = plugin_objects(build_dir, root)
    if not objects:
        raise NotApplicable("no compile of the target all loads the quarantine plugin")
    with ThreadPoolExecutor(max_workers=READERS) as pool:
        items = list(pool.map(lambda item: read_object(item[0], item[1], build_dir), objects))
    return census_of(items, [command for _, _, command in objects])


# ── The configuration ──────────────────────────────────────────────


def normalized(kind: str, value: str) -> str:
    """Return a cache value as one word: on or off for a BOOL, set or unset for a path, else the value or unset."""
    is_false = value.upper() in CMAKE_FALSE or value.upper().endswith("-NOTFOUND")
    if kind == "BOOL":
        return "off" if is_false else "on"
    if kind in ("PATH", "FILEPATH"):
        return "unset" if is_false else "set"
    return value if value.strip() else "unset"


def build_configuration(build_dir: Path, commands: Iterable[str]) -> BuildConfiguration:
    """Return the configuration of a build: the build kind, the tier and each name of CONFIGURATION.

    The tier holds the native tier of each native flag of the compile commands of the objects.  A bench target with
    its own -march=native then gives the configuration the tier of the host.

    Raises:
        BuildError: If the build has no build-kind.txt, its CMake cache cannot be read, or the compiler does not
            print its target
    """
    kind = build_target.kind_of(build_dir)
    if kind is None:
        raise BuildError(f"{build_dir} has no build-kind.txt, which cmake/BuildLauncher.cmake writes")
    try:
        entries = cmake.cache(build_dir)
    except cmake.CmakeError as problem:
        raise BuildError(f"the CMake cache of {build_dir} cannot be read: {problem}") from None
    compiler = entries.get("CMAKE_CXX_COMPILER")
    try:
        tier = build_target.build_tier(build_dir, compiler.value if compiler is not None else "", commands)
    except build_target.TierError as problem:
        raise BuildError(str(problem)) from None
    facts = [(KIND_KEY, kind), (TIER_KEY, tier)]
    for name in CONFIGURATION:
        entry = entries.get(name)
        value = normalized(entry.type_name, entry.value) if entry is not None else normalized("STRING", "")
        facts.append((name, value.replace(" ", "_")))
    words = [word for word, key in NAME_OPTIONS if dict(facts)[key] == "on"]
    return BuildConfiguration("-".join([kind, *words]), tuple(facts))


def facts_of(ledger: rule_ledger.Ledger, name: str) -> tuple[tuple[str, str], ...] | None:
    """Return the facts of a configuration of the ledger, or None when the ledger does not hold it."""
    item = ledger.configuration(name)
    return item.facts if item is not None else None


def is_held(ledger: rule_ledger.Ledger, configuration: BuildConfiguration) -> bool:
    """Return True when the ledger holds the configuration of the build: its name, and each fact that its row gives.

    A row can give only some facts, as the rows of the rule syscall-capability give only the build kind.  A fact
    that the build has and the row does not give changes nothing.
    """
    facts = facts_of(ledger, configuration.name)
    if facts is None:
        return False
    built = dict(configuration.facts)
    return all(built.get(name) == value for name, value in facts)


def configuration_difference(build: Sequence[tuple[str, str]], ledger: Sequence[tuple[str, str]]) -> list[str]:
    """Return a text for each fact of the row in the ledger with a value in the build that is not its value there."""
    built, held = dict(build), dict(ledger)
    return [
        f"{name} is {built.get(name, 'absent')} in the build and {held[name]} in the ledger"
        for name in sorted(held)
        if built.get(name) != held[name]
    ]


def held_line(rule_id: str, ledger: rule_ledger.Ledger, configuration: BuildConfiguration, consequence: str) -> str:
    """Return the one line of a build with a configuration that the ledger does not hold (target.not_held).

    `consequence` tells what the rule does with the build.
    """
    held = [(item.name, dict(item.facts).get(TIER_KEY, "?")) for item in ledger.configurations]
    facts = facts_of(ledger, configuration.name)
    detail = ""
    if facts is not None:
        detail = (
            f"Its configuration {configuration.name} differs: "
            f"{'; '.join(configuration_difference(configuration.facts, facts))}"
        )
    return build_target.not_held(rule_id, configuration.name, configuration.tier, held, consequence, detail)


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
        configuration: BuildConfiguration | None = None,
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

    def configuration(self) -> BuildConfiguration:
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
            self._configuration = build_configuration(self.build_dir, self.census().commands)
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
