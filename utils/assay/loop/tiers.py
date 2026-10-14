"""tiers — the tier of each preset of the presets file, which names the entry that runs the preset.

THE TIERS
    A tier names the entry that runs an item of the plan of the tree:
      gate     `loop land` runs the item when the change of a landing selects it (assay.loop.gate, THE SELECTION)
      nightly  the nightly entry runs the item on each night
      release  the release entry runs the item before a release
      manual   no entry runs the item.  The reason of its block tells why, and what a person runs
    ORDER gives the tiers of the entries.  Each entry also runs each item of
    each tier before its own tier.  The nightly entry then runs each item of
    the gate, and the release entry runs each item of the nightly entry.
    ENTRIES gives the command of the entry of each tier other than manual.

THE BLOCK
    Each configure preset of PRESETS_FILE gives its tier in the entry
    VENDOR_KEY of its field vendor (assay.readers.cmake, THE VENDOR ENTRIES),
    for example:

        "vendor": {"crucible": {"tier": "gate", "configuration": "x86_64-debug-asan"}}

    These are the fields of a block:
      tier           a word of Tier.  Each block gives it
      reason         a text.  The tier manual makes it necessary
      configuration  the name of the configuration of the build of the preset
                     on the build host of the tree (assay.core.configuration).
                     The tier gate makes it necessary, because the gate selects
                     a configuration by the name of its ledger before it
                     configures the build
    A block gives no other field, and each value is a text.  A child gets no
    block of a parent (assay.readers.cmake, THE INHERITANCE).  Each configure
    preset then states its own tier.

    A plain test preset runs each test of the build of its configure preset
    one time.  It gets the tier of its configure preset, and it gives no
    block.  A test preset that filters the tests or runs them again is an
    item of its own.  It gives a block with a tier and with no configuration.

    The module reads PRESETS_FILE only.  A file CMakeUserPresets.json is local
    to one host, and git does not hold it.

THE PROBLEMS
    read() gives each preset with its tier, or with the problem of its block.
    These are the problems:
      * a preset with no block, or with a block that is no object
      * a field that a block does not have, or a value that is no text
      * a tier that is no word of Tier
      * the tier manual with no reason, and the tier gate of a configure
        preset with no configuration
      * a configuration in the block of a test preset, and a block of a
        plain test preset
      * two configure presets with the same configuration
      * a test preset that names a configure preset that the file does not give
      * a configure preset with a build directory that does not resolve.
    A presets file that does not read is a problem of the file.  A plain test
    preset of a configure preset with a problem gets no tier and no problem,
    because its configure preset gives the problem.  The rule plan-coverage
    refuses each problem, and the gate refuses a tree with one
    (assay.loop.gate, THE CONFIGURATIONS).

`guard selftest tests.loop.test_tiers` does a test of the module.
"""

import enum
from collections.abc import Mapping, Sequence
from dataclasses import dataclass, replace
from pathlib import Path

from assay.core import configuration as build_config
from assay.readers import cmake

# The presets file of the plan, the key of the block in the field vendor of a preset, and the fields of a block (THE
# BLOCK).
PRESETS_FILE = cmake.PRESET_FILES[0]
VENDOR_KEY = "crucible"
TIER_FIELD = "tier"
REASON_FIELD = "reason"
CONFIGURATION_FIELD = "configuration"
FIELDS = (TIER_FIELD, REASON_FIELD, CONFIGURATION_FIELD)


class Tier(enum.StrEnum):
    """The tier of a preset or of an item of the plan (THE TIERS)."""

    GATE = "gate"
    NIGHTLY = "nightly"
    RELEASE = "release"
    MANUAL = "manual"


# The tiers of the entries, in their sequence, the command of the entry of each one, and the words of the tiers (THE
# TIERS).
ORDER = (Tier.GATE, Tier.NIGHTLY, Tier.RELEASE)
ENTRIES = {Tier.GATE: "loop land", Tier.NIGHTLY: "loop nightly", Tier.RELEASE: "loop release"}
WORDS = ", ".join(str(item) for item in Tier)


class PresetKind(enum.StrEnum):
    """The kind of a preset: a configure preset or a test preset."""

    CONFIGURE = "configure"
    TEST = "test"


@dataclass(frozen=True, slots=True)
class PresetTier:
    """The tier of one preset, or the problem of its block (THE PROBLEMS).

    `tier` is None when the preset has a problem, and `problem` is "" when it
    has none.  `build` is the build directory of a configure preset, and
    `is_parse` tells that the preset sets CRUCIBLE_SYNTAX_ONLY, itself or
    through a parent.  `configure` is the configure preset of a test preset,
    and `is_inherited` tells that a plain test preset gets the tier of its
    configure preset.  `is_repeated` tells that a test preset runs each test
    again (assay.readers.cmake, TestPreset).
    """

    kind: PresetKind
    name: str
    tier: Tier | None
    reason: str = ""
    configuration: str = ""
    build: Path | None = None
    is_parse: bool = False
    configure: str = ""
    is_inherited: bool = False
    problem: str = ""
    is_repeated: bool = False


@dataclass(frozen=True, slots=True)
class Tiers:
    """The tiers of the presets of a tree: each configure preset, each test preset, and the problems of the file."""

    configure: tuple[PresetTier, ...]
    tests: tuple[PresetTier, ...]
    problems: tuple[str, ...]

    @property
    def each_problem(self) -> tuple[str, ...]:
        """Each problem of the file and of a preset, in the sequence of the file and of the presets."""
        return (*self.problems, *(item.problem for item in (*self.configure, *self.tests) if item.problem))


@dataclass(frozen=True, slots=True)
class Block:
    """The fields of the block of one preset that reads: its tier, its reason and its configuration."""

    tier: Tier
    reason: str
    configuration: str


def read_block(kind: PresetKind, name: str, vendor: Mapping[str, Mapping[str, str | None] | None]) -> Block | str:
    """Return the block of one preset, or its problem (THE BLOCK, THE PROBLEMS).

    Args:
        kind: The kind of the preset
        name: The name of the preset
        vendor: The vendor entries of the preset
    """
    where = f"the {kind} preset {name}"
    if VENDOR_KEY not in vendor:
        return f"{where} gives no tier.  Give it the field vendor.{VENDOR_KEY}.{TIER_FIELD}, one of {WORDS}"
    fields = vendor[VENDOR_KEY]
    if fields is None:
        return f"the field vendor.{VENDOR_KEY} of {where} is no object.  Give it the fields {', '.join(FIELDS)}"
    unknown = sorted(set(fields) - set(FIELDS))
    if unknown:
        return f"the block of {where} gives the fields {unknown}, and a block has only the fields {', '.join(FIELDS)}"
    texts: dict[str, str] = {}
    for field, value in fields.items():
        if value is None:
            return f"the field {field} of the block of {where} is no text"
        texts[field] = value
    text = texts.get(TIER_FIELD, "")
    if text not in set(Tier):
        return f"the tier {text!r} of {where} is not one of {WORDS}"
    block = Block(Tier(text), texts.get(REASON_FIELD, ""), texts.get(CONFIGURATION_FIELD, ""))
    if block.tier is Tier.MANUAL and not block.reason.strip():
        return f"{where} has the tier manual and no reason.  Tell why no entry runs it, and what a person runs"
    if block.tier is Tier.GATE and kind is PresetKind.CONFIGURE and not block.configuration:
        return (
            f"{where} has the tier gate and no configuration.  Give the name of the configuration of its build on the "
            f"build host, which build-configuration.txt of its build directory holds"
        )
    if kind is PresetKind.TEST and block.configuration:
        return f"the block of {where} gives a configuration, and only a configure preset gives one"
    return block


def read_configure(root: Path, configure: Sequence[cmake.Preset]) -> list[PresetTier]:
    """Return the tier of each configure preset of a tree.

    Complexity: linear in the presets and in the edges of the field inherits of each one.
    """
    found: list[PresetTier] = []
    given: dict[str, str] = {}
    for preset in configure:
        block = read_block(PresetKind.CONFIGURE, preset.name, preset.vendor)
        try:
            effective = cmake.effective(configure, preset.name)
            build = cmake.build_directory(root, preset.name, configure)
        except cmake.CmakeError as problem:
            found.append(PresetTier(PresetKind.CONFIGURE, preset.name, None, problem=str(problem)))
            continue
        syntax = effective.cache_variables.get(build_config.SYNTAX_OPTION) or ""
        is_parse = build_config.normalized("BOOL", syntax) == "on"
        item = PresetTier(PresetKind.CONFIGURE, preset.name, None, build=build, is_parse=is_parse)
        if isinstance(block, str):
            found.append(replace(item, problem=block))
            continue
        item = replace(item, tier=block.tier, reason=block.reason, configuration=block.configuration)
        first = given.setdefault(block.configuration, preset.name) if block.configuration else preset.name
        if first != preset.name:
            twin = (
                f"the configure presets {first} and {preset.name} give the configuration {block.configuration}, and "
                f"each configuration has one build"
            )
            item = replace(item, tier=None, problem=twin)
        found.append(item)
    return found


def read_test(test: cmake.TestPreset, configure: Mapping[str, PresetTier]) -> PresetTier:
    """Return the tier of one test preset: the tier of its configure preset when it is plain, else its own block."""
    parent_name = test.configure_preset or ""
    parent = configure.get(parent_name)
    if parent is None:
        problem = (
            f"the test preset {test.name} names the configure preset {parent_name!r}, which the file does not give"
        )
        return PresetTier(PresetKind.TEST, test.name, None, configure=parent_name, problem=problem)
    if test.is_plain:
        if VENDOR_KEY in test.vendor:
            problem = (
                f"the plain test preset {test.name} gives a block, and it gets the tier of its configure preset "
                f"{parent_name}.  Remove the block"
            )
            return PresetTier(PresetKind.TEST, test.name, None, configure=parent_name, problem=problem)
        return PresetTier(
            PresetKind.TEST, test.name, parent.tier, parent.reason, configure=parent_name, is_inherited=True
        )
    block = read_block(PresetKind.TEST, test.name, test.vendor)
    if isinstance(block, str):
        return PresetTier(
            PresetKind.TEST, test.name, None, configure=parent_name, problem=block, is_repeated=test.is_repeated
        )
    return PresetTier(
        PresetKind.TEST, test.name, block.tier, block.reason, configure=parent_name, is_repeated=test.is_repeated
    )


def read(root: Path) -> Tiers:
    """Return the tier of each preset of PRESETS_FILE of a tree (THE BLOCK, THE PROBLEMS).

    Complexity: one read of the presets file, and linear in the presets.
    """
    files = [item for item in cmake.presets(root) if item.path.name == PRESETS_FILE]
    problems = tuple(f"{item.path}: {item.problem}" for item in files if item.problem is not None)
    configure = [preset for item in files for preset in item.configure]
    configure_tiers = read_configure(root, configure)
    by_name = {item.name: item for item in configure_tiers}
    tests = tuple(read_test(test, by_name) for item in files for test in item.tests)
    return Tiers(tuple(configure_tiers), tests, problems)
