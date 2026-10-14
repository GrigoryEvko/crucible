"""The test of the items and the keys of the sampled checks (assay.loop.evidence.items).

`guard selftest tests.loop.evidence.test_items` runs the test.  Each case writes one line.  Exit 0 when each case
holds, else 1.

The cases hold the builds of the test presets with the field `repeat`, the items of a fake build tree of `loop test`
and their keys, and the problem of a build that is not configured.  ASSAY_TEST_CTEST names the ctest of the fake build
tree, else the pinned ctest of PATH.
"""

import os
import tempfile
from pathlib import Path

from assay.core import cache, testing
from assay.loop import affected, tiers
from assay.loop.evidence import items as evidence_items
from assay.loop.evidence import targets

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
    """Hold the builds of the test presets with the field `repeat` (THE BUILDS)."""
    build = Path("/planted/build")
    presets = tiers.Tiers(
        (tiers.PresetTier(tiers.PresetKind.CONFIGURE, "default", tiers.Tier.GATE, build=build),),
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
        evidence_items.repeated_builds(presets) == (("stress", build),),
    )
    with tempfile.TemporaryDirectory(prefix="evidence-items-") as directory:
        found, problems = evidence_items.tree_items(Path(directory), (Path(directory) / "none",))
    expect("a build that is not configured gives a problem and no item", not found and "not configured" in problems[0])


def build_cases(expect: testing.Expect) -> None:
    """Hold the items and the keys of the tests of a fake build tree of `loop test` (THE ITEMS, THE KEYS)."""
    ctest = ctest_program()
    expect("ASSAY_TEST_CTEST or the pinned ctest of PATH gives the ctest of the fake build tree", bool(ctest))
    if not ctest:
        return
    with tempfile.TemporaryDirectory(prefix="evidence-items-") as directory, cache.scratch_root():
        fake = affected.SelfTest(Path(directory), ctest)
        found = {item.name: item for item in evidence_items.stress_items(evidence_items.TestKeys(fake.build_dir))}
        again = {item.name: item for item in evidence_items.stress_items(evidence_items.TestKeys(fake.build_dir))}
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


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(__name__.removeprefix("assay."), argv, (preset_cases, build_cases))
