"""artifacts — each program and shared library of the tree has its hardening properties, and each link fact has a row.

THE ARTIFACTS
    The rule reads each executable, shared library and module that the
    target all builds (assay.guard.engines.elf.runner, THE BUILD).  It reads
    the ELF file of each artifact one time.  The type of the target of an
    artifact in the code model tells if it is an executable.  The first directory of
    the source directory of its target gives its group (GROUPS).  The groups
    are vessel, benches and tests.  Each other directory, such as examples
    and the source root, gives the group tools.

THE FACTS
    Each artifact gives facts.  A fact is a kind and an entity:
      `needed`         each entry DT_NEEDED
      `runpath`        each directory of DT_RUNPATH (THE PATHS)
      `rpath`          each directory of DT_RPATH (THE PATHS)
      `static-member`  ARCHIVE(MEMBER) of each member that the link takes
                       from an archive out of the build directory (THE MAP)
    A group holds a fact when one of its artifacts gives it, and the row of
    the fact has the count 1.  A new program then adds no row, unless it
    gives a new fact.  The rule reads no fact that changes with the host and
    not with the code, such as the interpreter, a needed version or a text
    of .comment.

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
    path on the host.

THE MAP
    cmake/LinkMap.cmake makes mold write the map of each link adjacent to
    its output, as OUTPUT.map.  The rule reads the archive members that the
    input sections of the map name (assay.readers.linkmap.read_members).
    A member that keeps no section in the output gives no fact (THE INPUTS
    of assay.readers.linkmap).  An artifact with no map is an error of the
    build: configure the build again.

THE PROPERTIES
    Each artifact must have each of these properties.  A missing property is
    an error of its kind:
      `pie`           an executable is a position-independent executable
                      (PIE): the type ET_DYN with the bit DF_1_PIE of
                      DT_FLAGS_1.  The loader then puts it at a random address
      `relro`         a segment PT_GNU_RELRO, which the loader makes
                      read-only after the relocations
      `bind-now`      DT_BIND_NOW, the bit DF_BIND_NOW of DT_FLAGS or the bit
                      DF_1_NOW of DT_FLAGS_1.  The loader then gives each
                      symbol its address before the program starts.  RELRO
                      then also makes the global offset table read-only
      `stack`         a segment PT_GNU_STACK with no bit PF_X.  With no such
                      segment, the loader gives the process a stack that is
                      executable
      `control-flow`  the bits of the control-flow protection in the program
                      property of the processor (THE CONTROL FLOW)
      `machine`       a machine of the tree: x86_64 or aarch64
    CMakeLists.txt gives each executable `-fPIE` and `-pie`.  It gives each
    link `-z relro`, `-z now` and `-z noexecstack`.  cmake/CodeFlags.cmake
    gives each unit the flag of the control-flow protection.  The rule groups
    the artifacts with one problem into one error, which names one of them
    and their number.

THE CONTROL FLOW
    The segment PT_GNU_PROPERTY of an artifact (assay.readers.elf, THE
    PROPERTIES) holds the feature property of its processor (PROTECTIONS).
    An artifact of x86_64 must have the bits IBT and SHSTK of
    GNU_PROPERTY_X86_FEATURE_1_AND.  The processor then checks each indirect
    branch and each return.  An artifact of aarch64 must have the bits BTI
    and PAC of GNU_PROPERTY_AARCH64_FEATURE_1_AND.  The link keeps a bit only
    when each input object has it.  One input with no such bit then removes
    the bit from the artifact, and the error names each such input.

    InputReader reads the inputs of the map of the link (THE MAP, THE INPUTS
    of assay.readers.linkmap).  It reads the feature property of an object
    from its section .note.gnu.property, because an object has no program
    header.  It reads a member of an archive through the archive reader of
    assay.readers.elf, and it reads each archive one time in a run.  The map
    names no object that keeps no section in the output.  When such an
    object removed the bits, the error names no input.  An object of the
    tree gets the flag of cmake/CodeFlags.cmake.  A start file or an archive
    of the toolchain gets the bits only from a toolchain that builds its
    target libraries with the protection (utils/assay/setup/gcc.py).  The
    error shows each path with the words of THE PATHS.

THE LEDGER
    utils/ledgers/artifacts.txt has the policy count, in format 1 of
    assay.core.ledger.  It holds the facts in one set of rows, and the set
    judges each build (assay.guard.engines.elf.one_set, THE SET):
        @build/GROUP | KIND | 1 | ENTITY | -
    A new fact is an error, and a row of a fact that goes gives no finding
    (THE POLICIES of assay.core.ledger).  A write from one build keeps each
    row, and it gives no row to a new fact without --raise (RAISE_REASONS,
    one_set, THE WRITE).  The reduce of the landing gate removes the row of
    a fact that no build of the gate gives (one_set, THE REDUCE).

    The ledger does not judge a missing property or a run path directory
    that the host does not give, because each one is a requirement.  Such an
    error has no count, no row admits it, and a write refuses a build with
    one (one_set.Inspection.unadmitted).

THE PLANTS
    A row `kinds KIND...` of plant.txt limits the facts and the properties
    that the rule reads to these kinds (KINDS).  The rule reads the run path
    directories of each artifact, also with such a row.  The link of a
    planted program can add a run path of the toolchain of the host, and a
    unit of a plant gets the flags of the hardening only from its row flags.
    A control then holds only the kinds that it plants.  A plant of one
    property gives the error of that kind only, on each machine.  A compile
    on the host cannot make a file for a different machine, and no plant
    gives the error `machine`.  The test of the module holds that kind with
    planted properties.

NOT APPLICABLE
    The rule does not apply (exit 77) to these builds:
      * A build with no build-kind.txt
      * A build that only parses its units
        (assay.core.configuration.is_parse_build)
      * A build with no linked artifact, or one that has not made each one.

Complexity: linear in the artifacts, their program headers, their dynamic entries, the directories of their run paths
and the archive members of their maps.  Also linear in the lines of the maps of the artifacts with no control-flow
protection, and in the bytes of their input files.
"""

import os
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.elf import one_set, runner
from assay.readers import census as build_census
from assay.readers import elf, linkmap

RULE = "artifacts"
GROUP_PREFIX = "@build/"
# The group of each first directory of a source directory (THE ARTIFACTS).  Each other directory is of the group tools.
GROUPS = {"vessel": "vessel", "bench": "benches", "test": "tests"}
TOOLS = "tools"
# The kinds of the facts (THE FACTS).
NEEDED = "needed"
RUNPATH = "runpath"
RPATH = "rpath"
STATIC_MEMBER = "static-member"
FACT_KINDS = (NEEDED, RUNPATH, RPATH, STATIC_MEMBER)
# The kinds of the properties (THE PROPERTIES).
PIE = "pie"
RELRO = "relro"
BIND_NOW = "bind-now"
STACK = "stack"
CONTROL_FLOW = "control-flow"
MACHINE = "machine"
PROPERTY_KINDS = (PIE, RELRO, BIND_NOW, STACK, CONTROL_FLOW, MACHINE)
# The kinds that a row `kinds` of a plant can name (THE PLANTS).
KINDS = (*FACT_KINDS, *PROPERTY_KINDS)
KINDS_ROW = "kinds"
# The words that THE PATHS writes for the prefix of the compiler and for the build directory.
TOOLCHAIN_WORD = "{toolchain}"
BUILD_WORD = "{build}"
MAP_SUFFIX = ".map"
RAISE_REASONS = (
    "a new toolchain",
    "a new library or a new kind of artifact on a link line",
    "a new build kind or target",
)
ROW_SHAPE = (
    f"a row is '@build/GROUP | KIND | 1 | ENTITY | -', with a GROUP of {', '.join(sorted({*GROUPS.values(), TOOLS}))} "
    f"and a KIND of {', '.join(FACT_KINDS)}"
)
# The kind of the error of a run path directory that the host does not give, and the reason of each such directory.
MISSING_KIND = "missing-directory"
NO_PATH_REASON = "is not on the host"
FILE_REASON = "is not a directory"
EMPTY_REASON = "is empty, and the loader reads an empty part as the working directory"
RELATIVE_REASON = "is relative, and the loader reads it from the working directory"
TOKEN_REASON = "holds a $ other than $ORIGIN, and the rule does not expand it"
# The types of the feature properties of x86 and of aarch64, and the section of the properties of an object (THE
# CONTROL FLOW).
X86_FEATURE_1_AND = 0xC0000002
AARCH64_FEATURE_1_AND = 0xC0000000
PROPERTY_SECTION = ".note.gnu.property"
# The number of inputs that an error names before the count of the others.
SHOWN_INPUTS = 8
# The action of each kind of problem (THE PROPERTIES).
LINK_ACTION = (
    "Link each artifact with -z relro, -z now and -z noexecstack, as the add_link_options() call of CMakeLists.txt does"
)
ACTIONS = {
    PIE: (
        "Compile each unit of an executable with -fPIE and link it with -pie, as CMAKE_POSITION_INDEPENDENT_CODE and "
        "check_pie_supported() of CMakeLists.txt do"
    ),
    RELRO: LINK_ACTION,
    BIND_NOW: LINK_ACTION,
    STACK: LINK_ACTION,
    CONTROL_FLOW: (
        "Compile each object of the tree with the flag of cmake/CodeFlags.cmake.  A file of the toolchain needs a "
        "toolchain whose target libraries the compiler builds with the protection (utils/assay/setup/gcc.py)"
    ),
    MACHINE: "Build the tree for x86_64 or aarch64 (CLAUDE.md section XIV)",
}


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
class Protection:
    """The control-flow property of one machine (THE CONTROL FLOW).

    `kind` is the type of the feature property, and `bits` gives the name and the value of each bit that an artifact
    must have.  `flag` is the flag that cmake/CodeFlags.cmake gives each unit for the protection.
    """

    machine_name: str
    kind: int
    bits: tuple[tuple[str, int], ...]
    flag: str

    @property
    def required(self) -> int:
        """The value of all the bits that an artifact must have."""
        value = 0
        for _, bit in self.bits:
            value |= bit
        return value


PROTECTIONS = {
    elf.EM_X86_64: Protection("x86_64", X86_FEATURE_1_AND, (("IBT", 0x1), ("SHSTK", 0x2)), "-fcf-protection=full"),
    elf.EM_AARCH64: Protection(
        "aarch64", AARCH64_FEATURE_1_AND, (("BTI", 0x1), ("PAC", 0x2)), "-mbranch-protection=standard"
    ),
}


@dataclass(frozen=True, slots=True)
class Hardening:
    """The hardening facts of one linked artifact that its ELF file gives (THE PROPERTIES).

    `flags` and `flags_1` are the values of DT_FLAGS and DT_FLAGS_1, or 0.  `stack_flags` is p_flags of the segment
    PT_GNU_STACK, or None when the file has no such segment.  `features` gives the word of each program property.
    """

    is_executable: bool
    file_type: int
    machine: int
    flags: int
    flags_1: int
    has_bind_now_tag: bool
    has_relro: bool
    stack_flags: int | None
    features: Mapping[int, int]


@dataclass(frozen=True, slots=True)
class Problem:
    """One missing property of one artifact: its kind and the words that tell it, with no name of the artifact."""

    kind: str
    detail: str


@dataclass(frozen=True, slots=True)
class InputScan:
    """The inputs of a link with no bit of a protection, and the inputs that the rule cannot read (THE CONTROL FLOW).

    Each input is the file of the map, with the build directory before a relative path.  `unread` gives each input
    that cannot be read with its reason.
    """

    lacking: tuple[str, ...]
    unread: tuple[tuple[str, str], ...]


@dataclass(frozen=True, slots=True)
class Reading:
    """The facts, the missing run path directories and the problems of the linked artifacts of a build.

    `counts` gives the number of artifacts of each group, and `executables` the number of executables.  `missing`
    holds each run path directory that the host does not give (THE RUN PATH DIRECTORIES).  `problems` gives the
    artifacts of each problem, in the sequence of their paths (THE PROPERTIES).
    """

    facts: tuple[Fact, ...]
    counts: dict[str, int]
    missing: tuple[MissingDirectory, ...]
    problems: Mapping[Problem, tuple[str, ...]]
    executables: int


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


def member_facts(entries: Sequence[elf.DynamicEntry], words: Iterable[tuple[str, str]]) -> set[tuple[str, str]]:
    """Return the facts of one linked artifact that its dynamic entries give: the needed libraries and the run paths.

    `entries` are the entries of member.dynamic(), which the caller reads one time, and `words` are the words of
    path_words() (THE FACTS).
    """
    pairs = tuple(words)
    found = {(NEEDED, entry.text) for entry in entries if entry.tag == elf.DT_NEEDED}
    for entry in entries:
        if entry.tag in (elf.DT_RUNPATH, elf.DT_RPATH):
            kind = RUNPATH if entry.tag == elf.DT_RUNPATH else RPATH
            found |= {(kind, shown_path(part, pairs)) for part in entry.directories()}
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


# ── The properties ─────────────────────────────────────────────────


def hardening_of(member: elf.Member, entries: Sequence[elf.DynamicEntry], is_executable: bool) -> Hardening:
    """Return the hardening facts of one linked artifact (THE PROPERTIES).

    `entries` are the entries of member.dynamic(), which the caller reads one time.  Complexity: linear in the program
    headers, the dynamic entries and the bytes of the program properties.

    Raises:
        elf.ElfError: If a table, a segment or a note of the file is not fully in the file
    """
    segments = member.segments()
    stack = next((item for item in segments if item.kind == elf.PT_GNU_STACK), None)
    return Hardening(
        is_executable,
        member.file_type(),
        member.machine(),
        next((entry.value for entry in entries if entry.tag == elf.DT_FLAGS), 0),
        next((entry.value for entry in entries if entry.tag == elf.DT_FLAGS_1), 0),
        any(entry.tag == elf.DT_BIND_NOW for entry in entries),
        any(item.kind == elf.PT_GNU_RELRO for item in segments),
        None if stack is None else stack.flags,
        {item.kind: item.word for item in member.properties()},
    )


def missing_bits(hardening: Hardening, protection: Protection) -> tuple[str, ...]:
    """Return the name of each bit of the control-flow protection that the artifact does not have (THE CONTROL FLOW)."""
    word = hardening.features.get(protection.kind, 0)
    return tuple(name for name, bit in protection.bits if not word & bit)


def problems_of(hardening: Hardening, kinds: frozenset[str]) -> list[Problem]:
    """Return each property of `kinds` that the artifact does not have, with the words that tell it (THE PROPERTIES).

    The problem of the control flow names its missing bits, and control_flow_problem() adds the inputs.
    """
    found: list[Problem] = []
    is_position_independent = hardening.file_type == elf.ET_DYN and bool(hardening.flags_1 & elf.DF_1_PIE)
    if PIE in kinds and hardening.is_executable and not is_position_independent:
        held = "the type ET_EXEC" if hardening.file_type == elf.ET_EXEC else "no bit DF_1_PIE of DT_FLAGS_1"
        found.append(Problem(PIE, f"is an executable that is not position independent: it has {held}"))
    if RELRO in kinds and not hardening.has_relro:
        found.append(Problem(RELRO, "has no segment PT_GNU_RELRO, so no part of it is read-only after the relocations"))
    is_bound_now = (
        hardening.has_bind_now_tag or bool(hardening.flags & elf.DF_BIND_NOW) or bool(hardening.flags_1 & elf.DF_1_NOW)
    )
    if BIND_NOW in kinds and not is_bound_now:
        found.append(
            Problem(
                BIND_NOW, "binds each symbol at its first call: it has no DT_BIND_NOW, no DF_BIND_NOW and no DF_1_NOW"
            )
        )
    if STACK in kinds and (hardening.stack_flags is None or hardening.stack_flags & elf.PF_X):
        held = "no segment PT_GNU_STACK" if hardening.stack_flags is None else "the bit PF_X of PT_GNU_STACK"
        found.append(Problem(STACK, f"gives the process a stack that is executable: it has {held}"))
    protection = PROTECTIONS.get(hardening.machine)
    if protection is None:
        if MACHINE in kinds:
            found.append(Problem(MACHINE, f"has the machine {hardening.machine}, which is not x86_64 or aarch64"))
    elif CONTROL_FLOW in kinds and missing_bits(hardening, protection):
        bits = " and ".join(missing_bits(hardening, protection))
        found.append(
            Problem(
                CONTROL_FLOW,
                f"has no control-flow protection: the property {protection.kind:#x} of {protection.machine_name} "
                f"has no {bits}",
            )
        )
    return found


# ── The control flow ───────────────────────────────────────────────


def object_word(member: elf.Member, kind: int) -> int | None:
    """Return the word of the property KIND of the section .note.gnu.property of an object, or None with no such one.

    An object has no program header, and Member.properties() then gives no property of it (THE CONTROL FLOW).
    Complexity: linear in the sections of the object and the bytes of its notes.

    Raises:
        elf.ElfError: If the section or a note of it is not fully in the file
    """
    for section in member.named(PROPERTY_SECTION):
        for item in elf.note_properties(member.data(section), member.where):
            if item.kind == kind:
                return item.word
    return None


def archive_words(path: Path, kind: int) -> dict[str, tuple[int | None, ...]]:
    """Return the words of the property KIND of the members of an archive, by member name (THE CONTROL FLOW).

    An archive can hold two members with one name, and the name then gives two words.  Complexity: linear in the
    bytes of the archive.

    Raises:
        elf.ElfError: If the archive or a member of it is not ELF or is damaged
        OSError: If the archive cannot be read
    """
    found: dict[str, list[int | None]] = {}
    for member in elf.members(path):
        found.setdefault(member.name, []).append(object_word(member, kind))
    return {name: tuple(words) for name, words in found.items()}


class InputReader:
    """The reader of the feature property of the inputs of the links of one build (THE CONTROL FLOW).

    `archives` keeps the words of each archive that the reader read, or the error of its read.  The reader then reads
    each archive one time.
    """

    __slots__ = ("archives", "build_dir")

    def __init__(self, build_dir: Path) -> None:
        """Start a reader for the links of a build directory, the working directory of each link."""
        self.build_dir = build_dir
        self.archives: dict[tuple[Path, int], dict[str, tuple[int | None, ...]] | OSError | elf.ElfError] = {}

    def words(self, item: str, kind: int) -> tuple[int | None, ...]:
        """Return the word of the property KIND of each file that an input of a map names, or None for no property.

        An object gives one word.  ARCHIVE(MEMBER) gives one word for each member of the archive with the name, and
        no word when the archive has no such member.

        Raises:
            elf.ElfError: If a file is not ELF or is damaged
            OSError: If a file cannot be read
        """
        archive, member = linkmap.split_member(item)
        if not archive:
            with elf.mapped(self.build_dir / member) as found:
                return (object_word(found, kind),)
        key = (self.build_dir / archive, kind)
        if key not in self.archives:
            try:
                self.archives[key] = archive_words(key[0], kind)
            except (OSError, elf.ElfError) as problem:
                self.archives[key] = problem
        held = self.archives[key]
        if isinstance(held, (OSError, elf.ElfError)):
            raise held
        return held.get(member, ())

    def scan(self, inputs: Iterable[str], protection: Protection) -> InputScan:
        """Return the inputs with no bit of the protection, and the inputs that cannot be read, in their sequence.

        An input has no bit of the protection when one file that it names has no feature property, or a word with no
        such bit.
        Complexity: linear in the inputs and the bytes of their files.
        """
        lacking: list[str] = []
        unread: list[tuple[str, str]] = []
        required = protection.required
        for item in inputs:
            shown = str(self.build_dir / item)
            try:
                words = self.words(item, protection.kind)
            except (OSError, elf.ElfError) as problem:
                unread.append((shown, str(problem)))
                continue
            if not words:
                unread.append((shown, f"the archive has no member {linkmap.split_member(item)[1]}"))
            elif any(word is None or word & required != required for word in words):
                lacking.append(shown)
        return InputScan(tuple(lacking), tuple(unread))


def shown_inputs(inputs: Sequence[str], words: Iterable[tuple[str, str]]) -> str:
    """Return the inputs that an error names, with the words of THE PATHS, at most SHOWN_INPUTS."""
    pairs = tuple(words)
    shown = [shown_path(os.path.normpath(item), pairs) for item in inputs[:SHOWN_INPUTS]]
    rest = len(inputs) - SHOWN_INPUTS
    return ", ".join(shown) + (f" and {rest} more" if rest > 0 else "")


def control_flow_cause(
    artifact: Path, protection: Protection, words: Iterable[tuple[str, str]], reader: InputReader
) -> str:
    """Return the words that name the inputs of the link of an artifact with no control-flow protection.

    The inputs of the map of the link give the inputs (THE CONTROL FLOW).  A map or an input that cannot be read
    gives words that tell the cause.
    """
    pairs = tuple(words)
    path = artifact.with_name(artifact.name + MAP_SUFFIX)
    try:
        scan = reader.scan(linkmap.read_inputs(path), protection)
    except (OSError, linkmap.LinkMapError) as problem:
        return f".  The rule cannot name the inputs, because the map of the link cannot be read: {problem}"
    said = ""
    if scan.lacking:
        named = shown_inputs(scan.lacking, pairs)
        said = f".  The link removed the bits, because these inputs do not have them: {named}"
    if scan.unread:
        first, reason = scan.unread[0]
        said += (
            f".  The rule cannot read {len(scan.unread)} inputs of the map, such as "
            f"{shown_inputs((first,), pairs)}: {reason}"
        )
    if not said:
        said = (
            ".  Each input of the map has the bits.  An object that keeps no section in the output has no line in the "
            "map, and such an object removed them"
        )
    return said


def control_flow_problem(
    problem: Problem, hardening: Hardening, artifact: Path, words: Iterable[tuple[str, str]], reader: InputReader
) -> Problem:
    """Return the problem of the control flow with the inputs that removed the protection (THE CONTROL FLOW)."""
    protection = PROTECTIONS[hardening.machine]
    return Problem(problem.kind, problem.detail + control_flow_cause(artifact, protection, words, reader))


# ── The read ───────────────────────────────────────────────────────


def kinds_of(context: runner.ElfContext) -> frozenset[str]:
    """Return the kinds that the rule reads: each kind, or the kinds of a row `kinds` of a plant (THE PLANTS).

    Raises:
        runner.BuildError: If a row `kinds` names a kind that the rule does not have
    """
    if context.planted is not None:
        for row in context.planted.rows:
            if row.words[0] == KINDS_ROW:
                unknown = sorted(set(row.words[1:]) - set(KINDS))
                if unknown:
                    raise runner.BuildError(f"a row {KINDS_ROW} names {unknown[0]}, and the kinds are {KINDS}")
                return frozenset(row.words[1:])
    return frozenset(KINDS)


def read_build(context: runner.ElfContext, build_dir: Path) -> Reading:
    """Read each linked artifact of the build one time: its facts, its run path directories and its problems.

    The rule reads the run path directories of each artifact, also with a row `kinds` of a plant (THE PLANTS).
    Complexity: one map of each artifact, and the reads of THE MAP and THE CONTROL FLOW.

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
    reader = InputReader(build_dir)
    first: dict[tuple[str, str, str], str] = {}
    counts: dict[str, int] = {}
    missing: dict[tuple[str, str, str], tuple[str, int]] = {}
    problems: dict[Problem, list[str]] = {}
    for artifact in linked:
        group, path = group_of(artifact.directory), artifact.path
        counts[group] = counts.get(group, 0) + 1
        try:
            with elf.mapped(path) as member:
                entries = member.dynamic()
                hardening = hardening_of(member, entries, artifact.is_executable)
        except (OSError, elf.ElfError) as unreadable:
            raise runner.BuildError(f"the ELF reader cannot read {path}: {unreadable}") from None
        facts = member_facts(entries, words)
        if STATIC_MEMBER in kinds:
            facts |= map_facts(path, build_dir)
        shown = build_census.shown(path, context.root)
        for kind, entity in facts:
            if kind in kinds:
                first.setdefault((group, kind, entity), shown)
        for part, host_path, reason in missing_directories(entries, path):
            key = (shown_path(part, words), host_path, reason)
            example, number = missing.get(key, (shown, 0))
            missing[key] = (example, number + 1)
        for lacked in problems_of(hardening, kinds):
            told = (
                control_flow_problem(lacked, hardening, path, words, reader) if lacked.kind == CONTROL_FLOW else lacked
            )
            problems.setdefault(told, []).append(shown)
    return Reading(
        tuple(Fact(*key, example) for key, example in sorted(first.items())),
        counts,
        tuple(MissingDirectory(*key, *named) for key, named in sorted(missing.items())),
        {key: tuple(value) for key, value in problems.items()},
        sum(item.is_executable for item in linked),
    )


# ── The ledger and the findings ────────────────────────────────────


def is_row(key: rule_ledger.Key, entry: rule_ledger.Entry) -> bool:
    """Return True for a row with the shape of THE LEDGER: a fact of a group, with a count."""
    groups = {f"{GROUP_PREFIX}{name}" for name in (*GROUPS.values(), TOOLS)}
    return key.path in groups and key.kind in FACT_KINDS and bool(key.entity) and entry.value != rule_ledger.ADMIT_VALUE


def row_problems(ledger: rule_ledger.Ledger) -> list[check_report.Finding]:
    """Return an error for each row that does not have a shape of THE LEDGER.

    A fact has the count 1.
    """
    return one_set.ledger_problems(RULE, ledger, is_row, ROW_SHAPE, lambda _key: True)


def fact_finding(fact: Fact) -> check_report.Finding:
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


def problem_finding(problem: Problem, artifacts: Sequence[str]) -> check_report.Finding:
    """Return the error of one problem of one or more artifacts (THE PROPERTIES)."""
    subject = artifacts[0] if len(artifacts) == 1 else f"{artifacts[0]} and {len(artifacts) - 1} more artifacts: each"
    return check_report.Finding(
        "error",
        artifacts[0],
        0,
        RULE,
        f"{subject} {problem.detail}.  {ACTIONS[problem.kind]}",
        problem.kind,
        artifacts[0],
    )


def examine(rule: rule_registry.Rule, context: runner.ElfContext) -> one_set.Inspection:
    """Read the linked artifacts of the build, and judge their facts against the set of the ledger (THE LEDGER).

    Raises:
        runner.NotApplicable: If the rule does not apply to the build (NOT APPLICABLE)
        runner.BuildError: If the build cannot be read
    """
    # A build with no build kind does not apply, also when its ledger cannot be read (NOT APPLICABLE).
    context.kind()
    build_dir = context.build_dir if context.build_dir is not None else Path(context.root)
    if context.build_dir is not None and build_config.is_parse_build(context.build_dir):
        raise runner.NotApplicable("the build only parses its units, and it links no artifact")
    ledger = one_set.read_ledger(rule, context.root)
    if isinstance(ledger, one_set.Inspection):
        return ledger
    reading = read_build(context, build_dir)
    counts = reading.counts
    found = [fact_finding(fact) for fact in reading.facts]
    verdict = rule_ledger.compare(rule, ledger, found)
    # The ledger judges no run path directory that the host does not give and no missing property (THE LEDGER).
    properties = sorted(
        (problem_finding(problem, artifacts) for problem, artifacts in reading.problems.items()),
        key=lambda item: (item.kind, item.path),
    )
    refused = (*(missing_finding(item) for item in reading.missing), *properties)
    groups = ", ".join(f"{counts[name]} {name}" for name in sorted(counts))
    lacking = sum(len(artifacts) for artifacts in reading.problems.values())
    summary = (
        f"{RULE}: {sum(counts.values())} linked artifacts ({groups}), {reading.executables} of them executables.  "
        f"{len(reading.facts)} facts, judged against the set of the ledger.  Missing properties: {lacking}.  Run path "
        f"directories that the host does not give: {len(reading.missing)}"
    )
    return one_set.Inspection(
        (*row_problems(ledger), *verdict, *refused),
        ledger,
        {rule_ledger.key_of(item): 1 for item in found},
        summary,
        refused,
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
    """Run the rule on the build, and write its findings and its summary (assay.guard.engines.elf.one_set).

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    return one_set.run_inspection(RULE, lambda: examine(rule, context), warnings_dir)


# The facts that a write refuses with no --raise, and the reasons for --raise (THE LEDGER).
WRITE_POLICY = one_set.WritePolicy(
    row_problems, FACT_KINDS, RAISE_REASONS, f"each row of {RULE} is a fact of the build", "rows of link facts"
)
# The write of the fact rows of the build into the set (THE LEDGER, assay.guard.engines.elf.one_set).
write = one_set.writer(examine, WRITE_POLICY)
