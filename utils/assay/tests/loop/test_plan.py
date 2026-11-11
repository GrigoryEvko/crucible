"""The test of the plan of the tree (assay.loop.plan).

`guard selftest tests.loop.test_plan` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The cases hold the tier of an item, and the covers of the tests and of the
rules of planted builds.  They hold each problem of the plan, the sampled
items with no target, and the plan of this tree with a planted build.
"""

from pathlib import Path

from assay.core import paths, testing
from assay.core import registry as rule_registry
from assay.loop import derive, plan, tiers
from assay.loop.evidence import targets as evidence_targets
from assay.readers import ctest as ctest_reader

GATE = tiers.Tier.GATE
NIGHTLY = tiers.Tier.NIGHTLY
# The rows of a planted registry.  One rule has a test, four rules have a holder, one rule is of a step, and one rule
# runs nowhere.
REGISTRY = (
    "format 1\n"
    "rule tested engine=meta scope=tracked ledger=none doc=XV | a fault | repair it\n"
    "rule gauged engine=build scope=builddir ledger=max warn=1 error=2 unit=s held-by=loop-gauge doc=XV | a time | "
    "lower it\n"
    "rule launched engine=build scope=builddir ledger=max warn=1 error=2 unit=s held-by=cmake doc=XV | a time | "
    "lower it\n"
    "rule header-alone engine=build scope=builddir ledger=max warn=1 error=2 unit=MB doc=XV | a size | lower it\n"
    "rule alone engine=build scope=builddir ledger=max warn=1 error=2 unit=MB held-by=header-alone doc=XV | a size | "
    "lower it\n"
    "rule reconfigure-noop engine=build scope=builddir ledger=none doc=XV | a compile | repair it\n"
    "rule lonely engine=meta scope=tracked ledger=none doc=XV | a fault | repair it\n"
    "rule stranger engine=build scope=builddir ledger=max warn=1 error=2 unit=s held-by=nobody doc=XV | a time | "
    "lower it\n"
)


def preset_tier(name: str, tier: tiers.Tier | None, configuration: str = "") -> tiers.PresetTier:
    """Return a configure preset with a tier, a configuration and the build directory build-NAME of this tree."""
    build = paths.REPO_ROOT / f"build-{name}"
    return tiers.PresetTier(tiers.PresetKind.CONFIGURE, name, tier, configuration=configuration, build=build)


def guard_test(name: str, *words: str, disabled: bool = False) -> ctest_reader.TestEntry:
    """Return a test that runs the master guard of this tree with some words."""
    properties = {plan.DISABLED: ("ON",)} if disabled else {}
    return ctest_reader.TestEntry(name, ("python3", str(paths.GUARD), *words), properties)


def planted_registry() -> rule_registry.Registry:
    """Return the planted registry of REGISTRY."""
    return rule_registry.examine(REGISTRY)[0]


def tier_cases(expect: testing.Expect) -> None:
    """Hold the tier and the entry of an item: the first tier of ORDER of its covers (THE COVERS)."""
    both = plan.Item(plan.Kind.TEST, "one", "x", 0, (plan.Cover(NIGHTLY, "a"), plan.Cover(GATE, "b")))
    expect("an item gets the first tier of its covers", both.tier is GATE and both.entry == "loop land")
    manual = plan.Item(plan.Kind.PRESET, "one", "x", 0, (plan.Cover(tiers.Tier.MANUAL, "a person"),))
    expect("an item with a manual cover has the tier manual and no entry", manual.tier is tiers.Tier.MANUAL)
    expect("an item with a manual cover has no entry", manual.entry == "")
    bare = plan.Item(plan.Kind.RULE, "one", "x")
    expect("an item with no cover has no tier, and the plan names it", bare.tier is None and bare.entity == "rule one")
    expect("the plan gives each item with no cover", plan.Plan((both, bare)).uncovered == (bare,))


def test_cases(expect: testing.Expect) -> None:
    """Hold the items of the tests of two builds: their kinds, the covers of each build and a disabled test."""
    fixture = ctest_reader.TestEntry("neg_one", ("python3", "-S", str(paths.LOOP), "fixture", "build", "one.cpp", "x"))
    gate_build = plan.Build(
        preset_tier("gate", GATE),
        paths.REPO_ROOT / "build-gate",
        (guard_test("guard_tested", "check", "--rule", "tested"), fixture, guard_test("off", "check", disabled=True)),
    )
    night_build = plan.Build(preset_tier("night", NIGHTLY), paths.REPO_ROOT / "build-night", (fixture,))
    items = {item.name: item for item in plan.test_items((gate_build, night_build), paths.REPO_ROOT)}
    expect(
        "a test that runs loop fixture is a fixture, and it gets the cover of each build that registers it",
        items["neg_one"].kind is plan.Kind.FIXTURE
        and [cover.tier for cover in items["neg_one"].covers] == [GATE, NIGHTLY],
    )
    expect(
        "a test of the gate build gets the tier gate",
        items["guard_tested"].kind is plan.Kind.TEST and items["guard_tested"].tier is GATE,
    )
    expect("a disabled test gets no cover", not items["off"].covers)


def bench_cases(expect: testing.Expect) -> None:
    """Hold the covers of a test that runs a file of a bench target of its build, also when the test is disabled."""
    program = (paths.REPO_ROOT / "build-gate" / "bench" / "bench_leaf").resolve()
    properties = {plan.DISABLED: ("ON",)}
    direct = ctest_reader.TestEntry("bench_leaf", (str(program),), properties)
    launched = ctest_reader.TestEntry(
        "bench_leaf_launched", ("python3", "-S", str(paths.LOOP), "test-step", "--name", "x", str(program)), properties
    )
    build = plan.Build(
        preset_tier("gate", GATE),
        paths.REPO_ROOT / "build-gate",
        (direct, launched, guard_test("off", "check", disabled=True)),
        ("bench_leaf",),
        bench_programs=((str(program), "bench_leaf"),),
    )
    items = {item.name: item for item in plan.test_items((build, build), paths.REPO_ROOT)}
    bench_cover = plan.Cover(NIGHTLY, "the step bench runs its program, the bench target bench_leaf")
    expect(
        "a disabled test that runs a file of a bench target gets the cover of the step bench, one time",
        items["bench_leaf"].covers == (bench_cover,),
    )
    expect(
        "the program of a test behind the test launcher is the program after it",
        items["bench_leaf_launched"].covers == (bench_cover,),
    )
    expect("a disabled test of no bench target stays with no cover", not items["off"].covers)


def rule_cases(expect: testing.Expect) -> None:
    """Hold the covers of the rules: a test, each kind of holder, a step, and a rule with no cover."""
    build = plan.Build(
        preset_tier("gate", GATE),
        paths.REPO_ROOT / "build-gate",
        (
            guard_test("guard_tested", "check", "--rule", "tested"),
            guard_test("guard_header_alone", "check", "--rule", "header-alone"),
            guard_test("guard_lonely_witness", "witness", "--rule", "lonely"),
        ),
    )
    items = {item.name: item for item in plan.rule_items((build,), planted_registry(), paths.REPO_ROOT)}
    expect("a rule with a test of the gate build gets the tier gate", items["tested"].tier is GATE)
    expect("a rule that loop gauge holds gets the tier of the step gauge", items["gauged"].tier is NIGHTLY)
    expect("a rule that the build launcher holds gets the tier of the suite of a build", items["launched"].tier is GATE)
    expect("a rule that a rule holds gets the covers of that rule", items["alone"].tier is GATE)
    expect("a rule that a step names gets the tier of the step", items["reconfigure-noop"].tier is NIGHTLY)
    expect("the witness of a rule is no cover of the rule", not items["lonely"].covers)
    expect("a holder with no row of HOLDERS gives no cover", not items["stranger"].covers)


def problem_cases(expect: testing.Expect) -> None:
    """Hold each problem of the plan: a stale row, a holder with no row, a subcommand with no class, a configuration."""
    registry = planted_registry()
    presets = tiers.Tiers((preset_tier("default", GATE),), (), ())
    commands = {name: True for name in (*plan.ENTRY_COMMANDS, *plan.TOOL_COMMANDS, *plan.ITEM_COMMANDS)}
    commands.update({"gauge": True, "bench": True, "fuzz": True, "pgo": True, "step": False})
    found = {(item.kind, item.entity) for item in plan.step_problems(registry, presets, commands)}
    expect(
        f"a step that names a preset that the file does not give is a stale row: {sorted(found)}",
        (plan.ProblemKind.STALE_ENTRY, "step bench") in found and (plan.ProblemKind.STALE_ENTRY, "step pgo") in found,
    )
    stale, no_class = plan.ProblemKind.STALE_ENTRY, plan.ProblemKind.NO_CLASS
    expect("a holder of a rule with no row of HOLDERS has no class", (no_class, "holder nobody") in found)
    expect("a row of HOLDERS that names no holder of a rule is a stale row", (stale, "holder loop-test-step") in found)
    expect("a step that names a rule of the registry is no problem", (stale, "step gauge") not in found)
    unclassed = {**commands, "brand-new": True, "hidden-tool": False}
    del unclassed["land"]
    named = {(item.kind, item.entity) for item in plan.command_problems(unclassed)}
    expect(
        f"a visible subcommand with no class, and a class name with no subcommand, are problems: {sorted(named)}",
        named == {(plan.ProblemKind.NO_CLASS, "loop brand-new"), (plan.ProblemKind.STALE_ENTRY, "loop land")},
    )
    other = plan.Build(preset_tier("release", GATE, "x86_64-release"), Path("b"), configuration="x86_64-debug-asan")
    host = plan.Build(preset_tier("release", GATE, "x86_64-release"), Path("b"), configuration="aarch64-release")
    expect(
        "a build of a different configuration than its block is a problem, on a host of the same processor only",
        [item.entity for item in plan.configuration_problems((other, host))] == ["release"],
    )


def sampled_cases(expect: testing.Expect) -> None:
    """Hold the sampled items with no target: a class rule that the registry does not hold, and UNTARGETED."""
    labels = {ctest_reader.LABELS: (evidence_targets.PROPERTY.label,)}
    labelled = ctest_reader.TestEntry("prop_one", ("prop_one",), labels)
    build = plan.Build(preset_tier("fuzz", GATE), paths.REPO_ROOT / "build-fuzz", (labelled,), ("bench_one",))
    presets = tiers.Tiers(
        (preset_tier("fuzz", GATE),),
        (tiers.PresetTier(tiers.PresetKind.TEST, "stress", NIGHTLY, configure="fuzz", is_repeated=True),),
        (),
    )
    items = [
        plan.Item(plan.Kind.TEST_PRESET, "stress", "x"),
        plan.Item(plan.Kind.TEST, "prop_one", "x"),
        plan.Item(plan.Kind.TEST, "test_plain", "x"),
        plan.Item(plan.Kind.BENCH, "bench_one", "x"),
        plan.Item(plan.Kind.STEP, "gauge", "x"),
        plan.Item(plan.Kind.STEP, "bench", "x"),
        plan.Item(plan.Kind.HARNESS, "region", "x"),
        plan.Item(plan.Kind.RULE, evidence_targets.COVERAGE.check, "x"),
        plan.Item(plan.Kind.RULE, "tested", "x"),
    ]
    bare = {item.entity for item in plan.untargeted_items(items, presets, (build,), planted_registry())}
    expect(
        f"with no class rule in the registry, each sampled item has no target: {sorted(bare)}",
        bare
        == {
            "test-preset stress",
            "test prop_one",
            "bench bench_one",
            "harness region",
            f"rule {evidence_targets.COVERAGE.check}",
        },
    )
    expect("a step that gives the full evidence of its inputs in one run is not sampled", "step gauge" not in bare)
    held = "".join(
        f"rule {target.rule} engine=build scope=builddir ledger=none warn=1 error=2 unit=s held-by=loop-evidence "
        f"doc=XV | a target | run it\n"
        for target in evidence_targets.TARGETS
    )
    registry = rule_registry.examine(REGISTRY + held)[0]
    targeted = {item.entity for item in plan.untargeted_items(items, presets, (build,), registry)}
    expect(
        f"with the class rules, only the items of UNTARGETED have no target: {sorted(targeted)}",
        targeted == {"harness region"},
    )


def tree_cases(expect: testing.Expect) -> None:
    """Hold the plan of this tree with a planted build: no problem, each preset with a tier, and the covers."""
    registry = rule_registry.load(paths.REPO_ROOT / rule_registry.REGISTRY_PATH)
    presets = tiers.read(paths.REPO_ROOT)
    default = next(item for item in presets.configure if item.name == "default")
    walk_test = guard_test("guard_walk_units", "check", "--rule", "walk-units")
    build = plan.Build(default, paths.REPO_ROOT / "build", (walk_test,))
    targets = plan.derive_targets(paths.REPO_ROOT)
    expect(
        "the read of the table of loop derive gives each target with its module",
        {(target.name, target.module) for target in targets}
        == {(name, target.module) for name, target in derive.TARGETS.items()},
    )
    found = plan.plan(paths.REPO_ROOT, registry, presets, (build,), targets)
    expect(f"the plan of this tree has no problem: {found.problems}", not found.problems)
    items = {(item.kind, item.name): item for item in found.items}
    expect(
        "each preset of this tree has a tier",
        all(item.covers for item in found.items if item.kind in (plan.Kind.PRESET, plan.Kind.TEST_PRESET)),
    )
    expect(
        "a target of loop derive gets the covers of the rule that compares it",
        items[(plan.Kind.DERIVE, "walk-units")].tier is GATE,
    )
    expect("each boundary harness is an item of the step fuzz", items[(plan.Kind.HARNESS, "region")].tier is NIGHTLY)
    expect(
        "the rule of a class of coverage gets the tier of the step evidence, which runs it",
        items[(plan.Kind.RULE, evidence_targets.COVERAGE.check)].tier is NIGHTLY,
    )
    expect(
        "the rule of a class of coverage has a target",
        all(item.entity != f"rule {evidence_targets.COVERAGE.check}" for item in found.untargeted),
    )
    commands = plan.subcommands(paths.REPO_ROOT)
    expect("the subcommands of this tree hold loop plan, and hide loop step", commands["plan"] and not commands["step"])
    expect(
        "the step bench adds the runs of each bench target to the evidence of the class of measurement",
        items[(plan.Kind.STEP, "bench")].tier is NIGHTLY
        and next(step for step in plan.STEPS if step.name == "bench").command[-1]
        == str(evidence_targets.MEASUREMENT.kind),
    )


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (tier_cases, test_cases, bench_cases, rule_cases, problem_cases, sampled_cases, tree_cases),
    )
