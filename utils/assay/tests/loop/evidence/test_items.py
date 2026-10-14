"""The test of the items and the keys of the sampled checks (assay.loop.evidence.items).

`guard selftest tests.loop.evidence.test_items` runs the test.  Each case writes one line.  Exit 0 when each case
holds, else 1.

The cases hold the builds of each class, the members of a class, and the items of a fake build tree of `loop test` with
their keys.  They hold the items and the keys of the bench targets of a planted code model.  They also hold the problem
of a build that is not configured, and the pending items.  ASSAY_TEST_CTEST names the ctest of the fake build tree,
else the pinned ctest of PATH.
"""

import os
import tempfile
from pathlib import Path

from assay.core import cache, testing
from assay.loop import affected, bench, tiers
from assay.loop.evidence import items as evidence_items
from assay.loop.evidence import store as evidence_store
from assay.loop.evidence import targets
from assay.readers import cmake as cmake_reader
from assay.readers import ctest as ctest_reader

RULE = targets.STRESS.rule


def ctest_program() -> str:
    """Return the ctest of the fake build tree: the variable ASSAY_TEST_CTEST, or the pinned ctest of PATH, or ""."""
    given = os.environ.get("ASSAY_TEST_CTEST")
    if given:
        return given
    from assay.setup import pins

    try:
        return pins.pinned_program("ctest")
    except pins.PinError:
        return ""


def preset_cases(expect: testing.Expect) -> None:
    """Hold the builds of each class: the test presets with the field `repeat`, and the fuzz builds (THE BUILDS)."""
    build, fuzz = Path("/planted/build"), Path("/planted/build-fuzz")
    configure = tiers.PresetKind.CONFIGURE
    presets = tiers.Tiers(
        (
            tiers.PresetTier(configure, "default", tiers.Tier.GATE, build=build),
            tiers.PresetTier(configure, "fuzz", tiers.Tier.GATE, build=fuzz, is_fuzz=True),
            tiers.PresetTier(configure, "fuzz-parse", tiers.Tier.GATE, build=fuzz, is_fuzz=True, is_parse=True),
        ),
        (
            tiers.PresetTier(tiers.PresetKind.TEST, "default", tiers.Tier.GATE, configure="default"),
            tiers.PresetTier(
                tiers.PresetKind.TEST, "stress", tiers.Tier.NIGHTLY, configure="default", is_repeated=True
            ),
            tiers.PresetTier(tiers.PresetKind.TEST, "lost", tiers.Tier.NIGHTLY, configure="absent", is_repeated=True),
        ),
        (),
    )
    expect(
        "only a test preset with a field repeat and a known configure preset gives its build",
        evidence_items.class_builds(targets.STRESS, presets) == (("stress", build),),
    )
    expect(
        "only a configure preset that builds the fuzzers and is no parse build gives a build of a search",
        evidence_items.class_builds(targets.PROPERTY, presets) == (("fuzz", fuzz),),
    )
    benches = tiers.Tiers(
        (*presets.configure, tiers.PresetTier(configure, "bench", tiers.Tier.NIGHTLY, build=build, is_bench=True)),
        (),
        (),
    )
    expect(
        "only a configure preset that builds the benches for a measurement gives a build of the benches",
        evidence_items.class_builds(targets.MEASUREMENT, benches) == (("bench", build),),
    )
    with tempfile.TemporaryDirectory(prefix="evidence-items-") as directory:
        found, problems = evidence_items.tree_items(Path(directory), (Path(directory) / "none",))
    expect(
        "a build that is not configured gives one problem for each class together, and no item",
        not found and len(problems) == 1 and "not configured" in problems[0],
    )


def member_cases(expect: testing.Expect) -> None:
    """Hold the members of a class: the label of the class, no disabled test and no fixture (THE ITEMS)."""
    labelled = ctest_reader.TestEntry("prop_one", ("prop_one",), {ctest_reader.LABELS: (targets.PROPERTY.label,)})
    plain = ctest_reader.TestEntry("test_one", ("test_one",))
    disabled = ctest_reader.TestEntry(
        "prop_off", ("prop_off",), {ctest_reader.LABELS: (targets.PROPERTY.label,), evidence_items.DISABLED: ("ON",)}
    )
    expect(
        "a test with the label of the class is a member, and a test with no label is not",
        evidence_items.is_member(labelled, targets.PROPERTY) and not evidence_items.is_member(plain, targets.PROPERTY),
    )
    expect("a class with no label takes each test", evidence_items.is_member(plain, targets.STRESS))
    expect("a disabled test is no member", not evidence_items.is_member(disabled, targets.PROPERTY))


def build_cases(expect: testing.Expect) -> None:
    """Hold the items and the keys of the tests of a fake build tree of `loop test` (THE ITEMS, THE KEYS)."""
    ctest = ctest_program()
    expect("ASSAY_TEST_CTEST or the pinned ctest of PATH gives the ctest of the fake build tree", bool(ctest))
    if not ctest:
        return
    with tempfile.TemporaryDirectory(prefix="evidence-items-") as directory, cache.scratch_root():
        fake = affected.SelfTest(Path(directory), ctest)
        keys = evidence_items.TestKeys(fake.build_dir)
        found = {item.name: item for item in evidence_items.class_items(keys, targets.STRESS)}
        keys = evidence_items.TestKeys(fake.build_dir)
        again = {item.name: item for item in evidence_items.class_items(keys, targets.STRESS)}
    expect("a negative fixture and a disabled test are no items", not {"neg_fixture", "disabled_test"} & set(found))
    expect(
        "an executable test and a launched test have a key and no reason",
        all(found[name].key and not found[name].reason for name in ("exe_ok", "exe_bad", "exe_launched")),
    )
    expect(
        "a script test and a test that always runs have no key, and the reason names the kind",
        not found["script_guard"].key
        and affected.KIND_SCRIPT in found["script_guard"].reason
        and not found["exe_fixtures"].key,
    )
    expect("two tests have different keys", found["exe_ok"].key != found["exe_bad"].key)
    expect("a second read of the same tree gives the same keys", found == again)
    expect("each item names the rule and the build", all(item.rule == RULE and item.build for item in found.values()))


def bench_cases(expect: testing.Expect) -> None:
    """Hold the items and the keys of the bench targets of a planted code model (THE ITEMS, THE KEYS)."""
    with tempfile.TemporaryDirectory(prefix="evidence-benches-") as directory:
        build = Path(directory)
        compiler = build / "planted-cxx"
        compiler.write_text("#!/bin/sh\necho '  -march=                     \t\tznver5'\n", encoding="utf-8")
        compiler.chmod(0o755)
        (build / "bench").mkdir()
        (build / "bench" / "bench_built").write_bytes(b"planted program")
        planted = [
            {"name": name, "type": kind, "folder": {"name": folder}, "artifacts": [{"path": f"bench/{name}"}]}
            for name, kind, folder in (
                ("bench_built", "EXECUTABLE", bench.BENCH_FOLDER),
                ("bench_absent", "EXECUTABLE", bench.BENCH_FOLDER),
                ("bench_harness", "STATIC_LIBRARY", bench.BENCH_FOLDER),
                ("test_other", "EXECUTABLE", "tests"),
            )
        ]
        cache_entries: dict[str, str | tuple[str, str]] = {evidence_items.COMPILER_ENTRY: ("FILEPATH", str(compiler))}
        cmake_reader.plant(build, cache=cache_entries, targets=planted)
        first = {program.item.name: program for program in evidence_items.bench_programs(RULE, build)}
        second = {program.item.name: program for program in evidence_items.bench_programs(RULE, build)}
        (build / "bench" / "bench_built").write_bytes(b"a different program")
        changed = {program.item.name: program for program in evidence_items.bench_programs(RULE, build)}
        cmake_reader.plant(
            build, cache={evidence_items.COMPILER_ENTRY: ("FILEPATH", str(build / "absent-cxx"))}, targets=planted
        )
        no_tier = {program.item.name: program for program in evidence_items.bench_programs(RULE, build)}
    expect("only the executables of the bench folder are items", sorted(first) == ["bench_absent", "bench_built"])
    expect(
        "a built bench has a key, and a second read gives the same key",
        bool(first["bench_built"].item.key) and first["bench_built"].item.key == second["bench_built"].item.key,
    )
    expect(
        "a bench with no program has no key, and the reason tells to build it",
        not first["bench_absent"].item.key and "Build the target" in first["bench_absent"].item.reason,
    )
    expect("a different program has a different key", changed["bench_built"].item.key != first["bench_built"].item.key)
    expect(
        "a compiler that gives no native tier gives no key, and the reason names the tier",
        not no_tier["bench_built"].item.key and "native tier" in no_tier["bench_built"].item.reason,
    )


def pending_cases(expect: testing.Expect) -> None:
    """Hold the pending items of a target: a key, less evidence, and no kept failure unless named."""
    found = [
        evidence_items.Item(RULE, "fresh", "build", "key-fresh"),
        evidence_items.Item(RULE, "full", "build", "key-full"),
        evidence_items.Item(RULE, "failed", "build", "key-failed"),
        evidence_items.Item(RULE, "loose", "build", "", "the test has no key: script: a planted reason"),
    ]
    with tempfile.TemporaryDirectory(prefix="evidence-pending-") as directory:
        store = evidence_store.EvidenceStore(Path(directory))
        for _ in range(3):
            store.add(RULE, "full", "key-full", evidence_store.Sample("2026-10-14T12:00:00Z", 1.0, 0.1))
        store.keep_failure(RULE, "failed", "key-failed", evidence_store.Failure("2026-10-14T12:00:00Z", "planted", ()))
        chosen = [candidate.item.name for candidate in evidence_items.pending(found, store, 3.0, ())]
        named = [candidate.item.name for candidate in evidence_items.pending(found, store, 3.0, ("failed", "loose"))]
    expect(f"only an item with a key, less evidence and no failure is pending: {chosen}", chosen == ["fresh"])
    expect(f"a named item with a failure is pending, and an item with no key is not: {named}", named == ["failed"])


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    cases = (preset_cases, member_cases, build_cases, bench_cases, pending_cases)
    return testing.run(__name__.removeprefix("assay."), argv, cases)
