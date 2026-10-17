"""The test of the tiers of the presets (assay.loop.tiers).

`guard selftest tests.loop.test_tiers` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The cases hold the block of one preset and each of its problems.  They hold
the tiers of a planted presets file, and the presets of this tree.
"""

import json
import tempfile
from dataclasses import replace
from pathlib import Path

from assay.core import paths, testing
from assay.loop import tiers
from assay.readers import cmake

CONFIGURE = tiers.PresetKind.CONFIGURE
TEST = tiers.PresetKind.TEST

# The presets of a planted presets file.  Each preset name tells the case that it holds.
PLANTED_CONFIGURE: list[dict[str, object]] = [
    {
        "name": "base",
        "binaryDir": "${sourceDir}/build",
        "vendor": {"crucible": {"tier": "gate", "configuration": "x86_64-debug-asan"}},
    },
    {
        "name": "parse",
        "inherits": "base",
        "binaryDir": "${sourceDir}/build-parse",
        "cacheVariables": {"CRUCIBLE_SYNTAX_ONLY": {"type": "BOOL", "value": "ON"}},
        "vendor": {"crucible": {"tier": "gate", "configuration": "x86_64-debug-asan-syntax"}},
    },
    {"name": "child", "inherits": "parse", "vendor": {"crucible": {"tier": "manual", "reason": "a person runs it"}}},
    {"name": "loose", "inherits": "base", "binaryDir": "out/loose"},
    {
        "name": "twin",
        "inherits": "base",
        "binaryDir": "out/twin",
        "vendor": {"crucible": {"tier": "gate", "configuration": "x86_64-debug-asan"}},
    },
    {"name": "orphan", "vendor": {"crucible": {"tier": "nightly"}}},
    {
        "name": "bench",
        "binaryDir": "out/bench",
        "cacheVariables": {"CRUCIBLE_BENCH": "ON"},
        "vendor": {"crucible": {"tier": "nightly"}},
    },
    {
        "name": "bench-counters",
        "inherits": "bench",
        "binaryDir": "out/bench-counters",
        "cacheVariables": {"CRUCIBLE_PGO": "generate"},
        "vendor": {"crucible": {"tier": "release"}},
    },
    {
        "name": "bench-fuzz",
        "inherits": "bench",
        "binaryDir": "out/bench-fuzz",
        "cacheVariables": {"CRUCIBLE_FUZZ": "ON"},
        "vendor": {"crucible": {"tier": "nightly"}},
    },
    {
        "name": "profiled",
        "inherits": "bench",
        "binaryDir": "out/profiled",
        "cacheVariables": {"CRUCIBLE_PGO": "use"},
        "vendor": {"crucible": {"tier": "release"}},
    },
]
PLANTED_TESTS: list[dict[str, object]] = [
    {"name": "base", "configurePreset": "base"},
    {"name": "loose-run", "configurePreset": "loose"},
    {"name": "marked", "configurePreset": "base", "vendor": {"crucible": {"tier": "nightly"}}},
    {
        "name": "again",
        "configurePreset": "base",
        "execution": {"repeat": {"mode": "until-fail", "count": 2}},
        "vendor": {"crucible": {"tier": "nightly"}},
    },
    {"name": "filtered", "configurePreset": "base", "filter": {"include": {"name": "slow"}}},
    {"name": "lost", "configurePreset": "nowhere"},
]


def block_cases(expect: testing.Expect) -> None:
    """Hold the block of one preset and each problem of a block (THE BLOCK, THE PROBLEMS)."""
    gate = {"crucible": {"tier": "gate", "configuration": "x86_64-release"}}
    expect(
        "a block with a tier and a configuration reads",
        tiers.read_block(CONFIGURE, "one", gate) == tiers.Block(tiers.Tier.GATE, "", "x86_64-release"),
    )
    problems = {
        "gives no tier": tiers.read_block(CONFIGURE, "one", {}),
        "is no object": tiers.read_block(CONFIGURE, "one", {"crucible": None}),
        "gives the fields ['extra']": tiers.read_block(CONFIGURE, "one", {"crucible": {"tier": "gate", "extra": "x"}}),
        "is no text": tiers.read_block(CONFIGURE, "one", {"crucible": {"tier": None}}),
        "is not one of": tiers.read_block(CONFIGURE, "one", {"crucible": {"tier": "weekly"}}),
        "has the tier manual and no reason": tiers.read_block(CONFIGURE, "one", {"crucible": {"tier": "manual"}}),
        "has the tier gate and no configuration": tiers.read_block(CONFIGURE, "one", {"crucible": {"tier": "gate"}}),
        "only a configure preset gives one": tiers.read_block(TEST, "one", gate),
    }
    for text, found in problems.items():
        expect(f"a block gives the problem: {text}", isinstance(found, str) and text in found)
    expect(
        "a test preset with the tier gate needs no configuration",
        tiers.read_block(TEST, "one", {"crucible": {"tier": "gate"}}) == tiers.Block(tiers.Tier.GATE, "", ""),
    )


def planted_tiers() -> tiers.Tiers:
    """Return the tiers of a planted tree with the planted presets, and a user presets file that the module ignores."""
    with tempfile.TemporaryDirectory(prefix="tiers-") as root_text:
        root = Path(root_text)
        document = {"version": 6, "configurePresets": PLANTED_CONFIGURE, "testPresets": PLANTED_TESTS}
        (root / tiers.PRESETS_FILE).write_text(json.dumps(document), encoding="utf-8")
        user = {"version": 6, "configurePresets": [{"name": "mine", "binaryDir": "out/mine"}]}
        (root / cmake.PRESET_FILES[1]).write_text(json.dumps(user), encoding="utf-8")
        found = tiers.read(root)
        configure = tuple(item if item.build is None else relative(item, item.build, root) for item in found.configure)
        return tiers.Tiers(configure, found.tests, found.problems)


def relative(item: tiers.PresetTier, build: Path, root: Path) -> tiers.PresetTier:
    """Return a configure preset with its build directory relative to the planted root."""
    return replace(item, build=build.relative_to(root))


def read_cases(expect: testing.Expect) -> None:
    """Hold the tiers of the presets of a planted presets file (THE BLOCK, THE PROBLEMS)."""
    found = planted_tiers()
    configure = {item.name: item for item in found.configure}
    tests = {item.name: item for item in found.tests}
    expect("the module reads the tracked presets file only", "mine" not in configure and not found.problems)
    expect(
        "a gate preset gives its tier, its configuration and its build directory",
        configure["base"].tier is tiers.Tier.GATE
        and configure["base"].configuration == "x86_64-debug-asan"
        and configure["base"].build == Path("build")
        and not configure["base"].is_parse,
    )
    expect(
        "a preset that sets CRUCIBLE_SYNTAX_ONLY in an object value is a parse preset",
        configure["parse"].is_parse and configure["parse"].build == Path("build-parse"),
    )
    expect(
        "a child gets the build directory and the parse option of its parents, and its own manual block",
        configure["child"].tier is tiers.Tier.MANUAL
        and configure["child"].reason == "a person runs it"
        and configure["child"].is_parse
        and configure["child"].build == Path("build-parse"),
    )
    expect(
        "only a preset that builds the benches with no fuzzers and no profile counters builds them for a measurement",
        configure["bench"].is_bench
        and not configure["bench-counters"].is_bench
        and not configure["bench-fuzz"].is_bench
        and configure["bench-fuzz"].is_fuzz
        and not configure["base"].is_bench,
    )
    expect(
        "only a preset that compiles with the profile uses the profile",
        configure["profiled"].uses_profile
        and not configure["bench-counters"].uses_profile
        and not configure["bench"].uses_profile,
    )
    expect(
        "a preset with no block has a problem and no tier",
        configure["loose"].tier is None and "gives no tier" in configure["loose"].problem,
    )
    expect(
        "a second preset with the same configuration has a problem",
        configure["twin"].tier is None and "the configure presets base and twin" in configure["twin"].problem,
    )
    expect(
        "a preset with no build directory has a problem",
        configure["orphan"].tier is None and "gives no binaryDir" in configure["orphan"].problem,
    )
    expect(
        "a plain test preset gets the tier of its configure preset",
        tests["base"].tier is tiers.Tier.GATE and tests["base"].is_inherited and not tests["base"].problem,
    )
    expect(
        "a plain test preset of a configure preset with a problem gets no tier and no problem",
        tests["loose-run"].tier is None and not tests["loose-run"].problem,
    )
    expect(
        "a plain test preset with a block has a problem",
        tests["marked"].tier is None and "Remove the block" in tests["marked"].problem,
    )
    expect(
        "a test preset that runs the tests again gives its own tier",
        tests["again"].tier is tiers.Tier.NIGHTLY and not tests["again"].is_inherited,
    )
    expect(
        "a test preset that filters the tests and gives no block has a problem",
        tests["filtered"].tier is None and "gives no tier" in tests["filtered"].problem,
    )
    expect(
        "a test preset of a configure preset that the file does not give has a problem",
        tests["lost"].tier is None and "'nowhere'" in tests["lost"].problem,
    )
    expect(
        "each_problem gives the problem of each preset, in the sequence of the presets",
        [item.name for item in found.configure if item.problem] == ["loose", "twin", "orphan"]
        and len(found.each_problem) == 6,
    )


def tree_cases(expect: testing.Expect) -> None:
    """Hold the presets of this tree: each preset has a tier, and the file has no problem."""
    found = tiers.read(paths.REPO_ROOT)
    expect(f"the presets of this tree have no problem: {found.each_problem}", not found.each_problem)
    expect(
        "each configure preset of this tree has a tier",
        bool(found.configure) and all(item.tier is not None for item in found.configure),
    )


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(__name__.removeprefix("assay."), argv, (block_cases, read_cases, tree_cases))
