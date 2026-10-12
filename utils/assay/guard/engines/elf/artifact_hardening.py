"""artifact-hardening — each program, shared library and module of the target all has its hardening properties.

THE ARTIFACTS
    The rule reads each executable, shared library and module that the
    target all builds (assay.guard.engines.elf.runner, THE BUILD).  The type
    of the target of an artifact in the code model tells if it is an
    executable.

THE PROPERTIES
    Each artifact must have each of these properties.  A missing property is
    an error of its kind, and no ledger row admits it:
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
    and their number.  The rule has no ledger, because each property is a
    requirement.  The rule artifact-links holds the link facts that a row can
    admit.

THE CONTROL FLOW
    The segment PT_GNU_PROPERTY of an artifact (assay.readers.elf, THE
    PROPERTIES) holds the feature property of its processor (PROTECTIONS).
    An artifact of x86_64 must have the bits IBT and SHSTK of
    GNU_PROPERTY_X86_FEATURE_1_AND.  The processor then checks each indirect
    branch and each return.  An artifact of aarch64 must have the bits BTI
    and PAC of GNU_PROPERTY_AARCH64_FEATURE_1_AND.  The link keeps a bit only
    when each input object has it.  One input with no such bit then removes
    the bit from the artifact, and the error names each such input.  The map
    of the link (cmake/LinkMap.cmake) tells each merge that removed the
    property (assay.readers.linkmap, THE MERGES), and lacking_inputs() reads
    it.  An object of the tree gets the flag of cmake/CodeFlags.cmake.  A
    start file or an archive of the toolchain gets the bits only from a
    toolchain that builds its target libraries with the protection
    (utils/assay/setup/gcc.py).  The error shows each path with the words of
    artifact-links, such as {toolchain} for the prefix of the compiler.

THE PLANTS
    A row `properties KIND...` of plant.txt limits the properties that the
    rule reads to these kinds.  A plant of one property then gives the error
    of that kind only, on each machine.  A compile on the host cannot make a
    file for a different machine, and no plant gives the error `machine`.
    The test of the module holds that kind with planted facts.

NOT APPLICABLE
    The rule does not apply (exit 77) to these builds:
      * A build with no build-kind.txt
      * A build that only parses its units
        (assay.core.configuration.is_parse_build)
      * A build with no linked artifact, or one that has not made each one.

Complexity: linear in the artifacts, their program headers and their dynamic entries.  Also linear in the lines of
the merges of the maps of the artifacts with no control-flow protection.
"""

import os
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.guard.engines.elf import artifact_links, runner
from assay.readers import census as build_census
from assay.readers import elf, linkmap

RULE = "artifact-hardening"
PIE = "pie"
RELRO = "relro"
BIND_NOW = "bind-now"
STACK = "stack"
CONTROL_FLOW = "control-flow"
MACHINE = "machine"
KINDS = (PIE, RELRO, BIND_NOW, STACK, CONTROL_FLOW, MACHINE)
PROPERTIES_ROW = "properties"
# The types of the feature properties of x86 and of aarch64 (THE CONTROL FLOW).
X86_FEATURE_1_AND = 0xC0000002
AARCH64_FEATURE_1_AND = 0xC0000000
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
class Facts:
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
class Reading:
    """The problems of the build, grouped (THE PROPERTIES), and the number of executables and of other artifacts.

    `groups` gives the artifacts of each problem, in the sequence of their paths.
    """

    groups: Mapping[Problem, tuple[str, ...]]
    executables: int
    libraries: int


# ── The facts ──────────────────────────────────────────────────────


def facts_of(member: elf.Member, is_executable: bool) -> Facts:
    """Return the hardening facts of one linked artifact (THE PROPERTIES).

    Complexity: linear in the program headers, the dynamic entries and the bytes of the program properties.

    Raises:
        elf.ElfError: If a table, a segment or a note of the file is not fully in the file
    """
    entries = member.dynamic()
    segments = member.segments()
    stack = next((item for item in segments if item.kind == elf.PT_GNU_STACK), None)
    return Facts(
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


def missing_bits(facts: Facts, protection: Protection) -> tuple[str, ...]:
    """Return the name of each bit of the control-flow protection that the artifact does not have (THE CONTROL FLOW)."""
    word = facts.features.get(protection.kind, 0)
    return tuple(name for name, bit in protection.bits if not word & bit)


def problems_of(facts: Facts, kinds: frozenset[str]) -> list[Problem]:
    """Return each property of KINDS that the artifact does not have, with the words that tell it (THE PROPERTIES).

    The problem of the control flow names its missing bits, and control_flow_problem() adds the inputs.
    """
    found: list[Problem] = []
    is_position_independent = facts.file_type == elf.ET_DYN and bool(facts.flags_1 & elf.DF_1_PIE)
    if PIE in kinds and facts.is_executable and not is_position_independent:
        held = "the type ET_EXEC" if facts.file_type == elf.ET_EXEC else "no bit DF_1_PIE of DT_FLAGS_1"
        found.append(Problem(PIE, f"is an executable that is not position independent: it has {held}"))
    if RELRO in kinds and not facts.has_relro:
        found.append(Problem(RELRO, "has no segment PT_GNU_RELRO, so no part of it is read-only after the relocations"))
    is_bound_now = facts.has_bind_now_tag or bool(facts.flags & elf.DF_BIND_NOW) or bool(facts.flags_1 & elf.DF_1_NOW)
    if BIND_NOW in kinds and not is_bound_now:
        found.append(
            Problem(
                BIND_NOW, "binds each symbol at its first call: it has no DT_BIND_NOW, no DF_BIND_NOW and no DF_1_NOW"
            )
        )
    if STACK in kinds and (facts.stack_flags is None or facts.stack_flags & elf.PF_X):
        held = "no segment PT_GNU_STACK" if facts.stack_flags is None else "the bit PF_X of PT_GNU_STACK"
        found.append(Problem(STACK, f"gives the process a stack that is executable: it has {held}"))
    protection = PROTECTIONS.get(facts.machine)
    if protection is None:
        if MACHINE in kinds:
            found.append(Problem(MACHINE, f"has the machine {facts.machine}, which is not x86_64 or aarch64"))
    elif CONTROL_FLOW in kinds and missing_bits(facts, protection):
        bits = " and ".join(missing_bits(facts, protection))
        found.append(
            Problem(
                CONTROL_FLOW,
                f"has no control-flow protection: the property {protection.kind:#x} of {protection.machine_name} "
                f"has no {bits}",
            )
        )
    return found


# ── The control flow ───────────────────────────────────────────────


def lacking_inputs(merges: Iterable[linkmap.PropertyMerge], protection: Protection) -> tuple[str, ...]:
    """Return each input file of a link that has no bit of the protection, from the merges of its map.

    The second file of a merge of the property is such an input when it has no such property.  It is one too when
    its value does not hold each bit of the protection.  The first file of the first merge can be one too, because
    it holds the properties of the first input with properties (assay.readers.linkmap, THE MERGES).  Each file comes
    one time, in the sequence of the map.  Complexity: linear in the merges.
    """
    found: dict[str, None] = {}
    is_first = True
    required = protection.required
    for merge in merges:
        if merge.kind != protection.kind:
            continue
        if is_first and (merge.first_value is None or merge.first_value & required != required):
            found.setdefault(merge.first)
        is_first = False
        if merge.second_value is None or merge.second_value & required != required:
            found.setdefault(merge.second)
    return tuple(found)


def shown_inputs(inputs: Sequence[str], words: Iterable[tuple[str, str]]) -> str:
    """Return the inputs that an error names, with the words of THE PATHS of artifact-links, at most SHOWN_INPUTS."""
    pairs = tuple(words)
    shown = [artifact_links.shown_path(os.path.normpath(item), pairs) for item in inputs[:SHOWN_INPUTS]]
    rest = len(inputs) - SHOWN_INPUTS
    return ", ".join(shown) + (f" and {rest} more" if rest > 0 else "")


def control_flow_cause(artifact: Path, protection: Protection, words: Iterable[tuple[str, str]]) -> str:
    """Return the words that name the inputs of the link of an artifact with no control-flow protection.

    The map of the link gives the inputs (THE CONTROL FLOW).  A map that cannot be read gives words that tell the
    cause.
    """
    path = artifact.with_name(artifact.name + artifact_links.MAP_SUFFIX)
    try:
        inputs = lacking_inputs(linkmap.read_merges(path), protection)
    except (OSError, linkmap.LinkMapError) as problem:
        return f".  The rule cannot name the inputs, because the map of the link cannot be read: {problem}"
    if not inputs:
        return ".  The map of the link names no merge that removed the property, and no input of the link has it"
    return f".  The link removed the bits, because these inputs do not have them: {shown_inputs(inputs, words)}"


def control_flow_problem(problem: Problem, facts: Facts, artifact: Path, words: Iterable[tuple[str, str]]) -> Problem:
    """Return the problem of the control flow with the inputs that removed the protection (THE CONTROL FLOW)."""
    protection = PROTECTIONS[facts.machine]
    return Problem(problem.kind, problem.detail + control_flow_cause(artifact, protection, words))


# ── The read and the findings ──────────────────────────────────────


def kinds_of(context: runner.ElfContext) -> frozenset[str]:
    """Return the kinds that the rule reads: each kind, or the kinds of a row `properties` of a plant (THE PLANTS).

    Raises:
        runner.BuildError: If a row `properties` names a kind that the rule does not have
    """
    if context.planted is not None:
        for row in context.planted.rows:
            if row.words[0] == PROPERTIES_ROW:
                unknown = sorted(set(row.words[1:]) - set(KINDS))
                if unknown:
                    raise runner.BuildError(f"a row {PROPERTIES_ROW} names {unknown[0]}, and the kinds are {KINDS}")
                return frozenset(row.words[1:])
    return frozenset(KINDS)


def read_problems(context: runner.ElfContext) -> Reading:
    """Read each linked artifact of the build, and group the artifacts of each problem (THE PROPERTIES).

    Raises:
        runner.NotApplicable: If the build has no linked artifact, or has not made each one
        runner.BuildError: If an artifact or the code model cannot be read
    """
    linked = context.linked_artifacts()
    if not linked:
        raise runner.NotApplicable("the build has no linked artifact in the target all")
    unmade = sorted(build_census.shown(item.path, context.root) for item in linked if not item.path.is_file())
    if unmade:
        raise runner.NotApplicable(f"the build has not made {len(unmade)} artifacts, such as {unmade[0]}")
    kinds = kinds_of(context)
    build_dir = context.build_dir if context.build_dir is not None else Path(context.root)
    words: tuple[tuple[str, str], ...] | None = None
    groups: dict[Problem, list[str]] = {}
    for artifact in linked:
        try:
            with elf.mapped(artifact.path) as member:
                facts = facts_of(member, artifact.is_executable)
        except (OSError, elf.ElfError) as unreadable:
            raise runner.BuildError(f"the ELF reader cannot read {artifact.path}: {unreadable}") from None
        for found in problems_of(facts, kinds):
            problem = found
            if found.kind == CONTROL_FLOW:
                words = words if words is not None else artifact_links.path_words(context.compiler(), build_dir)
                problem = control_flow_problem(found, facts, artifact.path, words)
            groups.setdefault(problem, []).append(build_census.shown(artifact.path, context.root))
    executables = sum(item.is_executable for item in linked)
    return Reading({key: tuple(value) for key, value in groups.items()}, executables, len(linked) - executables)


def finding_of(problem: Problem, artifacts: Sequence[str]) -> check_report.Finding:
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


def examine(context: runner.ElfContext) -> tuple[list[check_report.Finding], Reading]:
    """Return the findings of the build, and the groups and the counts that give them (THE PROPERTIES).

    Raises:
        runner.NotApplicable: If the rule does not apply to the build (NOT APPLICABLE)
        runner.BuildError: If the build cannot be read
    """
    context.kind()
    if context.build_dir is not None and build_config.is_parse_build(context.build_dir):
        raise runner.NotApplicable("the build only parses its units, and it links no artifact")
    reading = read_problems(context)
    found = [finding_of(problem, artifacts) for problem, artifacts in reading.groups.items()]
    return sorted(found, key=lambda item: (item.kind, item.path)), reading


def findings(context: runner.ElfContext) -> list[check_report.Finding]:
    """Give the findings of the linked artifacts of the build.  The rule has no ledger."""
    return examine(context)[0]


def judge(rule: rule_registry.Rule, context: runner.ElfContext) -> list[check_report.Finding]:
    """Return the findings of the build.  The rule has no ledger, and each finding stays as it is.

    Raises:
        runner.NotApplicable: If the rule does not apply to the build
        runner.BuildError: If the build cannot be read
    """
    return examine(context)[0]


def run(rule: rule_registry.Rule, context: runner.ElfContext, warnings_dir: Path | None) -> int:
    """Run the rule on the build, write its findings, and write one line with the numbers of the artifacts.

    Returns:
        0 with no error, 1 with an error, SKIPPED when the rule does not apply
    """
    try:
        found, reading = examine(context)
    except runner.NotApplicable as reason:
        check_report.write_line(f"{RULE}: {reason}.  The rule does not apply")
        return runner.SKIPPED
    status = check_report.emit(found, RULE, warnings_dir)
    lacking = sum(len(artifacts) for artifacts in reading.groups.values())
    check_report.write_line(
        f"{RULE}: {reading.executables} executables and {reading.libraries} shared libraries and modules, "
        f"{lacking} missing properties"
    )
    return status
