"""artifact-links — each link fact of the programs and the shared libraries of the tree has a row.

THE ARTIFACTS
    The rule reads each executable, shared library and module that the
    target all builds (assay.guard.engines.elf.runner, THE BUILD).  The first
    directory of the source directory of its target gives its group
    (GROUPS).  The groups are vessel, benches and tests.  Each other
    directory, such as examples and the source root, gives the group tools.

THE FACTS
    Each artifact gives facts.  A fact is a kind and an entity:
      `needed`         each entry DT_NEEDED
      `runpath`        each directory of DT_RUNPATH (THE PATHS)
      `rpath`          each directory of DT_RPATH (THE PATHS)
      `elf-type`       executable, pie or shared (assay.readers.elf, THE
                       FILE TYPE)
      `interpreter`    the path of the segment PT_INTERP
      `version`        each version that the file gets from a shared
                       library, as FILE VERSION
      `static-member`  ARCHIVE(MEMBER) of each member that the link takes
                       from an archive out of the build directory (THE MAP)
      `init`           the function of each entry of .preinit_array and
                       .init_array
      `ifunc`          each function of the type STT_GNU_IFUNC
      `section`        the name of each section
      `comment`        each text of the section .comment
      `import`         each undefined dynamic symbol, as SYMBOL@VERSION or
                       SYMBOL.  Only the group vessel gives this kind
    A group holds a fact when one of its artifacts gives it, and the row of
    the fact has the count 1.  A new program then adds no row, unless it
    gives a new fact.

THE PATHS
    A directory of a run path holds the paths of one host.  The rule writes
    {toolchain} for the prefix of the compiler, and {build} for the build
    directory.  assay.guard.engines.elf.runner.toolchain_prefix() gives the
    prefix.

THE RUN PATH DIRECTORIES
    The loader reads each directory of DT_RUNPATH and DT_RPATH on each start
    of an artifact.  The rule refuses each such directory that the host of
    the build does not give (missing_directories()).  The ELF reader expands
    $ORIGIN and ${ORIGIN} to the directory of the artifact (assay.readers.elf,
    THE DYNAMIC SECTION).  These directories are errors:
      * A path that is not on the host, or that is not a directory
      * An empty part, which the loader reads as the working directory
      * A relative directory, which the loader also reads from the working
        directory
      * A directory with a $ other than $ORIGIN, such as $LIB, because the
        rule does not expand it.
    The error names the directory with the words of THE PATHS, and also its
    path on the host.  No ledger row admits such an error, and a write of
    the ledger refuses a build with one (configured.Inspection.unadmitted).

THE MAP
    cmake/LinkMap.cmake makes GNU ld write the map of each link adjacent to
    its output, as OUTPUT.map.  The rule reads the archive members of the map
    (assay.readers.linkmap.read_members).  An artifact with no map is an
    error of the build: configure the build again.

THE LEDGER
    utils/ledgers/artifact-links.txt has the policy count, in format 1 of
    assay.core.ledger:
        configuration NAME kind KIND
        configuration NAME tier TIER
        configuration NAME OPTION on
        @build/GROUP | KIND | 1 | ENTITY | -
        @CONFIGURATION | @build/GROUP | KIND | +1 | ENTITY | -
    The first configuration is the base, and a row of a different
    configuration gives its difference.  A new fact is an error.  A row of a
    fact that goes is an error too, until the same commit writes the ledger
    again.  The write gives no row to a new fact without --raise
    (RAISE_REASONS).  THE POLICY of assay.core.configuration judges each
    build, as in the rule artifact-symbols.

THE PLANTS
    A row `facts KIND...` of plant.txt limits the facts of a plant to these
    kinds.  The interpreter, the versions and the texts of .comment change
    with the machine and the toolchain.  A control then holds only the kinds
    that do not change.

NOT APPLICABLE
    The rule does not apply (exit 77) to these builds:
      * A build with no build-kind.txt
      * A build that only parses its units
        (assay.core.configuration.is_parse_build)
      * A build with no linked artifact, or one that has not made each one.

Complexity: linear in the artifacts, their symbols and sections, the directories of their run paths, and the archive
members of their maps.
"""

from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.elf import configured, runner
from assay.readers import census as build_census
from assay.readers import elf, linkmap

RULE = "artifact-links"
GROUP_PREFIX = "@build/"
# The group of each first directory of a source directory (THE ARTIFACTS).  Each other directory is of the group tools.
GROUPS = {"vessel": "vessel", "bench": "benches", "test": "tests"}
TOOLS = "tools"
VESSEL = "vessel"
NEEDED = "needed"
RUNPATH = "runpath"
RPATH = "rpath"
ELF_TYPE = "elf-type"
INTERPRETER = "interpreter"
VERSION = "version"
STATIC_MEMBER = "static-member"
INIT = "init"
IFUNC = "ifunc"
SECTION = "section"
COMMENT = "comment"
IMPORT = "import"
KINDS = (NEEDED, RUNPATH, RPATH, ELF_TYPE, INTERPRETER, VERSION, STATIC_MEMBER, INIT, IFUNC, SECTION, COMMENT, IMPORT)
# The names of the file types (THE FACTS).
EXECUTABLE = "executable"
PIE = "pie"
SHARED = "shared"
# The words that THE PATHS writes for the prefix of the compiler and for the build directory.
TOOLCHAIN_WORD = "{toolchain}"
BUILD_WORD = "{build}"
MAP_SUFFIX = ".map"
COMMENT_SECTION = ".comment"
FACTS_ROW = "facts"
VERSION_MARK = "@"
RAISE_REASONS = (
    "a new toolchain",
    "a new library or a new kind of artifact on a link line",
    "a new build kind or target",
    "a kind of fact that the rule did not read before",
)
ROW_SHAPE = (
    f"a row is '@build/GROUP | KIND | 1 | ENTITY | -', with a GROUP of {', '.join(sorted({*GROUPS.values(), TOOLS}))} "
    f"and a KIND of {', '.join(KINDS)}"
)
# The kind of the error of a run path directory that the host does not give, and the reason of each such directory.
MISSING_KIND = "missing-directory"
NO_PATH_REASON = "is not on the host"
FILE_REASON = "is not a directory"
EMPTY_REASON = "is empty, and the loader reads an empty part as the working directory"
RELATIVE_REASON = "is relative, and the loader reads it from the working directory"
TOKEN_REASON = "holds a $ other than $ORIGIN, and the rule does not expand it"


@dataclass(frozen=True, slots=True)
class Fact:
    """One fact of a group, with an artifact that gives it, as a message shows it (THE FACTS)."""

    group: str
    kind: str
    entity: str
    example: str


@dataclass(frozen=True, slots=True)
class MissingDirectory:
    """A run path directory that the host does not give, with the artifacts that name it (THE RUN PATH DIRECTORIES).

    `directory` is the part of the run path with the words of THE PATHS.  `host_path` is its path on the host after
    the expansion of $ORIGIN, or the part itself when the rule cannot find a directory.  `example` is one artifact
    that names it, and `artifacts` the number of such artifacts.
    """

    directory: str
    host_path: str
    reason: str
    example: str
    artifacts: int


@dataclass(frozen=True, slots=True)
class LinkReading:
    """The facts of the linked artifacts, the number of artifacts of each group, and the missing run path directories.

    `missing` holds each run path directory that the host does not give (THE RUN PATH DIRECTORIES).
    """

    facts: tuple[Fact, ...]
    counts: Mapping[str, int]
    missing: tuple[MissingDirectory, ...]


# ── The facts ──────────────────────────────────────────────────────


def group_of(directory: str) -> str:
    """Return the group of the first directory of a source directory (THE ARTIFACTS)."""
    return GROUPS.get(directory, TOOLS)


def path_words(compiler: str, build_dir: Path) -> tuple[tuple[str, str], ...]:
    """Return each path that THE PATHS replaces with its word, the longest path first."""
    prefix = runner.toolchain_prefix(compiler)
    pairs = {
        str(prefix): TOOLCHAIN_WORD,
        str(prefix.resolve()): TOOLCHAIN_WORD,
        str(build_dir): BUILD_WORD,
        str(build_dir.resolve()): BUILD_WORD,
    }
    return tuple(sorted(pairs.items(), key=lambda item: -len(item[0])))


def shown_path(directory: str, words: Iterable[tuple[str, str]]) -> str:
    """Return a directory of a run path with the word of its prefix (THE PATHS)."""
    for path, word in words:
        if directory == path or directory.startswith(f"{path}/"):
            return word + directory[len(path) :]
    return directory


def file_type_of(member: elf.Member, entries: Iterable[elf.DynamicEntry]) -> str:
    """Return the file type of an artifact: executable, pie or shared (THE FACTS)."""
    kind = member.file_type()
    if kind == elf.ET_EXEC:
        return EXECUTABLE
    if kind == elf.ET_DYN:
        is_pie = any(entry.tag == elf.DT_FLAGS_1 and entry.value & elf.DF_1_PIE for entry in entries)
        return PIE if is_pie else SHARED
    return f"type {kind}"


def member_facts(
    member: elf.Member, entries: Sequence[elf.DynamicEntry], words: Iterable[tuple[str, str]], is_vessel: bool
) -> set[tuple[str, str]]:
    """Return the facts of one linked artifact that its ELF file and its dynamic entries give (THE FACTS).

    `entries` are the entries of member.dynamic(), which the caller reads one time.

    Raises:
        elf.ElfError: If a table of the file is not fully in the file
    """
    found = {(NEEDED, entry.text) for entry in entries if entry.tag == elf.DT_NEEDED}
    for entry in entries:
        if entry.tag in (elf.DT_RUNPATH, elf.DT_RPATH):
            kind = RUNPATH if entry.tag == elf.DT_RUNPATH else RPATH
            found |= {(kind, shown_path(part, words)) for part in entry.directories()}
    found.add((ELF_TYPE, file_type_of(member, entries)))
    interpreter = member.interpreter()
    if interpreter:
        found.add((INTERPRETER, interpreter))
    found |= {(VERSION, f"{need.file} {need.name}") for need in member.version_needs()}
    found |= {(INIT, name or "-") for name in member.init_functions()}
    found |= {
        (IFUNC, symbol.name) for symbol in member.symbols() if symbol.is_defined and symbol.kind == elf.STT_GNU_IFUNC
    }
    found |= {(SECTION, section.name) for section in member.sections() if section.name}
    for section in member.named(COMMENT_SECTION):
        texts = member.data(section).decode("utf-8", "replace").split("\0")
        found |= {(COMMENT, text.strip()) for text in texts if text.strip()}
    if is_vessel:
        found |= {
            (IMPORT, f"{symbol.name}{VERSION_MARK}{symbol.version}" if symbol.version else symbol.name)
            for symbol in member.dynamic_symbols()
            if symbol.is_undefined
        }
    return found


def directory_problem(part: str, artifact: Path) -> tuple[str, str]:
    """Return the path on the host of one part of a run path, and the reason why the host does not give it.

    The reason is an empty text for a directory that the host gives (THE RUN PATH DIRECTORIES).  The ELF reader replaces
    $ORIGIN with the directory of the artifact (assay.readers.elf, THE DYNAMIC SECTION).
    """
    if not part:
        return part, EMPTY_REASON
    host_path = elf.expanded_origin(part, artifact)
    if host_path is None:
        return part, TOKEN_REASON
    path = Path(host_path)
    if not path.is_absolute():
        return host_path, RELATIVE_REASON
    if path.is_dir():
        return host_path, ""
    return host_path, FILE_REASON if path.exists() else NO_PATH_REASON


def missing_directories(entries: Iterable[elf.DynamicEntry], artifact: Path) -> list[tuple[str, str, str]]:
    """Return each part of the run path of an artifact that the host does not give (THE RUN PATH DIRECTORIES).

    Each item is the part of the run path, its path on the host and the reason.  An entry with an empty text names no
    directory.  Complexity: one stat for each part of the run path.
    """
    found: list[tuple[str, str, str]] = []
    for entry in entries:
        if entry.tag not in (elf.DT_RUNPATH, elf.DT_RPATH) or not entry.text:
            continue
        for part in entry.parts():
            host_path, reason = directory_problem(part, artifact)
            if reason:
                found.append((part, host_path, reason))
    return found


def map_facts(artifact: Path, build_dir: Path) -> set[tuple[str, str]]:
    """Return the archive members that the link of an artifact takes from archives not in the build directory (THE MAP).

    Raises:
        runner.BuildError: If the link wrote no map, or the map cannot be read
    """
    path = artifact.with_name(artifact.name + MAP_SUFFIX)
    try:
        members = linkmap.read_members(path)
    except FileNotFoundError:
        raise runner.BuildError(
            f"the link of {artifact} wrote no map {path.name}.  cmake/LinkMap.cmake gives each link its map: configure "
            f"the build again"
        ) from None
    except (OSError, linkmap.LinkMapError) as problem:
        raise runner.BuildError(f"the map of {artifact} cannot be read: {problem}") from None
    directory = build_dir.resolve()
    found: set[tuple[str, str]] = set()
    for item in members:
        archive = (directory / item.archive).resolve() if item.archive else None
        if archive is not None and not archive.is_relative_to(directory):
            found.add((STATIC_MEMBER, f"{archive.name}({item.member})"))
    return found


def kinds_of(context: runner.ElfContext) -> frozenset[str]:
    """Return the kinds that the rule reads: each kind, or the kinds of a row `facts` of a plant (THE PLANTS)."""
    if context.planted is not None:
        for row in context.planted.rows:
            if row.words[0] == FACTS_ROW:
                return frozenset(row.words[1:])
    return frozenset(KINDS)


def read_facts(context: runner.ElfContext, build_dir: Path) -> LinkReading:
    """Read the facts and the run path directories of each linked artifact, and count the artifacts of each group.

    The rule reads the run path directories of each artifact, also with a row `facts` of a plant (THE RUN PATH
    DIRECTORIES).

    Raises:
        runner.NotApplicable: If the build has no linked artifact, or has not made each one
        runner.BuildError: If an artifact, its map or the code model cannot be read
    """
    linked = context.linked_artifacts()
    if not linked:
        raise runner.NotApplicable("the build has no linked artifact in the target all")
    unmade = sorted(build_census.shown(item.path, context.root) for item in linked if not item.path.is_file())
    if unmade:
        raise runner.NotApplicable(f"the build has not made {len(unmade)} artifacts, such as {unmade[0]}")
    words = path_words(context.compiler(), build_dir)
    kinds = kinds_of(context)
    first: dict[tuple[str, str, str], str] = {}
    counts: dict[str, int] = {}
    missing: dict[tuple[str, str, str], tuple[str, int]] = {}
    for artifact in linked:
        group, path = group_of(artifact.directory), artifact.path
        counts[group] = counts.get(group, 0) + 1
        try:
            with elf.mapped(path) as member:
                entries = member.dynamic()
                facts = member_facts(member, entries, words, group == VESSEL)
                parts = missing_directories(entries, path)
        except (OSError, elf.ElfError) as problem:
            raise runner.BuildError(f"the ELF reader cannot read {path}: {problem}") from None
        if STATIC_MEMBER in kinds:
            facts |= map_facts(path, build_dir)
        shown = build_census.shown(path, context.root)
        for kind, entity in facts:
            if kind in kinds:
                first.setdefault((group, kind, entity), shown)
        for part, host_path, reason in parts:
            key = (shown_path(part, words), host_path, reason)
            example, number = missing.get(key, (shown, 0))
            missing[key] = (example, number + 1)
    return LinkReading(
        tuple(Fact(*key, example) for key, example in sorted(first.items())),
        counts,
        tuple(MissingDirectory(*key, *named) for key, named in sorted(missing.items())),
    )


# ── The ledger and the findings ────────────────────────────────────


def is_row(key: rule_ledger.Key, entry: rule_ledger.Entry) -> bool:
    """Return True for a base row with the shape of THE LEDGER: a fact of a group, with a count."""
    groups = {f"{GROUP_PREFIX}{name}" for name in (*GROUPS.values(), TOOLS)}
    return key.path in groups and key.kind in KINDS and bool(key.entity) and entry.value != rule_ledger.ADMIT_VALUE


def row_problems(ledger: rule_ledger.Ledger) -> list[check_report.Finding]:
    """Return an error for each configuration and each row that does not have a shape of THE LEDGER.

    A fact has the count 0 or 1 in each configuration.
    """
    return configured.ledger_problems(RULE, ledger, is_row, ROW_SHAPE, lambda _key: True)


def finding_of(fact: Fact) -> check_report.Finding:
    """Return the finding of one fact of a group (THE FACTS)."""
    return check_report.Finding(
        "error",
        f"{GROUP_PREFIX}{fact.group}",
        0,
        RULE,
        f"{fact.example} gives the link fact {fact.kind} {fact.entity} to the group {fact.group}.  A new link fact "
        f"takes --raise with its reason in the commit",
        fact.kind,
        fact.entity,
    )


def missing_finding(item: MissingDirectory) -> check_report.Finding:
    """Return the error of one run path directory that the host does not give (THE RUN PATH DIRECTORIES)."""
    named = f"{item.example} names" if item.artifacts == 1 else f"{item.example} and {item.artifacts - 1} more name"
    host_path = f" ({item.host_path})" if item.host_path != item.directory else ""
    return check_report.Finding(
        "error",
        item.example,
        0,
        RULE,
        f"{named} the run path directory '{item.directory}'{host_path}, which {item.reason}.  No ledger row admits a "
        f"run path directory that the host does not give: correct the link line, such as the run path of the "
        f"toolchain in cmake/ToolchainRunPath.cmake",
        MISSING_KIND,
        item.directory,
    )


def examine(rule: rule_registry.Rule, context: runner.ElfContext) -> configured.Inspection:
    """Read the linked artifacts of the build, and judge their facts against the ledger (THE LEDGER).

    Raises:
        runner.NotApplicable: If the rule does not apply to the build (NOT APPLICABLE)
        runner.BuildError: If the build cannot be read
    """
    # A build with no build kind does not apply, also when its ledger cannot be read (NOT APPLICABLE).
    context.kind()
    build_dir = context.build_dir if context.build_dir is not None else Path(context.root)
    if context.build_dir is not None and build_config.is_parse_build(context.build_dir):
        raise runner.NotApplicable("the build only parses its units, and it links no artifact")
    ledger = configured.read_ledger(rule, context.root)
    if isinstance(ledger, configured.Inspection):
        return ledger
    reading = read_facts(context, build_dir)
    counts = reading.counts
    configuration = configured.configuration_of(context)
    judged = build_config.judging(RULE, ledger, configuration)
    found = [finding_of(fact) for fact in reading.facts]
    verdict = rule_ledger.compare(rule, ledger, found, judged.name)
    # The ledger does not judge a run path directory that the host does not give (THE RUN PATH DIRECTORIES).
    missing = tuple(missing_finding(item) for item in reading.missing)
    groups = ", ".join(f"{counts[name]} {name}" for name in sorted(counts))
    summary = (
        f"{RULE}: {sum(counts.values())} linked artifacts ({groups}), {len(reading.facts)} facts of "
        f"{configuration.name} on {context.tier()}, judged against {judged.words}.  Run path directories that the host "
        f"does not give: {len(missing)}"
    )
    return configured.Inspection(
        (*row_problems(ledger), *verdict, *missing),
        ledger,
        configuration,
        judged,
        {rule_ledger.key_of(item): 1 for item in found},
        {},
        summary,
        missing,
    )


def findings(context: runner.ElfContext) -> list[check_report.Finding]:
    """Give the findings of the linked artifacts of the build, judged against the ledger in the tree of the context."""
    return list(examine(rule_ledger.rule_of(RULE), context).findings)


# ── The run and the write ──────────────────────────────────────────


def judge(rule: rule_registry.Rule, context: runner.ElfContext) -> list[check_report.Finding]:
    """Return the findings of the build judged against the ledger in the tree of the context (THE LEDGER).

    Raises:
        runner.NotApplicable: If the rule does not apply to the build
        runner.BuildError: If the build cannot be read
    """
    return list(examine(rule, context).findings)


def run(rule: rule_registry.Rule, context: runner.ElfContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build, write its findings, and print its rows (assay.guard.engines.elf.configured).

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    return configured.run_inspection(RULE, lambda: examine(rule, context), warnings_dir)


def refusals(inspection: configured.Inspection, held: Mapping[rule_ledger.Key, int]) -> list[str]:
    """Return a line for each new fact of the build (THE LEDGER)."""
    return configured.risen_rows(inspection, held, KINDS)


# The facts that a write refuses with no --raise, and the reasons for --raise (THE LEDGER).
WRITE_POLICY = configured.WritePolicy(
    row_problems, refusals, RAISE_REASONS, f"each row of {RULE} is a fact of the build", "rows of link facts"
)
# The write of the configuration and the fact rows of the build (THE LEDGER, assay.guard.engines.elf.configured).
write = configured.writer(examine, WRITE_POLICY)
