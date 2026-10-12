"""The test of `guard write --all-configurations` (assay.guard.write_configurations).

`guard selftest tests.guard.test_write_configurations` runs the test.  Each
case writes one line.  Exit 0 when each case holds, else 1.

The cases plant a tree with a registry of one counted rule and its ledger
of configurations.  The tree also holds the ledger of a rule that is not
counted.  The planted
steps build nothing.  The planted write writes the counts of the
configuration of a build directory into the ledger in the mode lower.  It
uses assay.core.ledger, as `guard write --build-dir` does.
"""

import tempfile
from collections.abc import Mapping
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import testing
from assay.guard import write_configurations
from assay.loop import gate

REGISTRY = (
    "format 1\n"
    "rule door-unit engine=plugin scope=tracked ledger=count kinds=header-door doc=XXII.R4 | a door | move the door\n"
    "rule erased-brand engine=tsast scope=tracked ledger=count doc=XXII | a brand | name the brand\n"
)
# The ledger of the counted rule: the base x86_64-debug-asan, the configuration x86_64-release, and a configuration
# that the host does not build.
LEDGER = (
    "# The head of the planted ledger.\n"
    "format 1\nrule door-unit\n"
    "configuration x86_64-debug-asan kind x86_64-debug-asan\n"
    "configuration x86_64-release kind x86_64-release\n"
    "configuration aarch64-debug-asan kind aarch64-debug-asan\n"
    "a.h | header-door | 3 | x | -\n"
    "b.h | header-door | 2 | y | -\n"
    "@x86_64-release | a.h | header-door | -1 | x | -\n"
)
OTHER_LEDGER = "format 1\nrule erased-brand\nc.h | - | 1 | z | -\n"
KEY_A = rule_ledger.Key("a.h", "header-door", "x")
KEY_B = rule_ledger.Key("b.h", "header-door", "y")
# The counts of each configuration after a change that removes one finding of a.h in each configuration.
FALLS = {"x86_64-debug-asan": {KEY_A: 2, KEY_B: 2}, "x86_64-release": {KEY_A: 1, KEY_B: 2}}


def planted_tree(root: Path) -> None:
    """Plant the registry and the two ledgers."""
    for name, text in (
        ("utils/registry.txt", REGISTRY),
        ("utils/ledgers/door-unit.txt", LEDGER),
        ("utils/ledgers/erased-brand.txt", OTHER_LEDGER),
    ):
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(text, encoding="utf-8")


def counted_rule(root: Path) -> rule_registry.Rule:
    """Return the counted rule of the planted registry."""
    return rule_registry.load(root / rule_registry.REGISTRY_PATH).rules["door-unit"]


def planted_steps(counts: Mapping[str, Mapping[rule_ledger.Key, int]], seen: list[str]) -> write_configurations.Steps:
    """Return steps that build nothing and write the counts of the configuration of a build directory.

    `seen` gets each build and each write in sequence.
    """
    by_build = {item.build: item.ledger_name for item in gate.CONFIGURATIONS}

    def build(root: Path, configuration: gate.Configuration, jobs: int) -> int:
        seen.append(f"build {configuration.ledger_name} {jobs}")
        return 0

    def write(root: Path, rule_id: str, build_dir: Path) -> int:
        name = by_build[build_dir.name]
        seen.append(f"write {rule_id} {name}")
        rule = counted_rule(root)
        try:
            written = rule_ledger.write_counts(rule, rule_ledger.read(rule, root), counts[name], name)
        except rule_ledger.LedgerError as problem:
            check_report.write_line(f"planted write: {problem}", is_error=True)
            return 1
        rule_ledger.store(written, root)
        return 0

    return write_configurations.Steps(build, write)


def plan_cases(expect: testing.Expect) -> None:
    """Hold the configurations of the counted ledgers: the bases first, and a note for each one the host cannot build."""
    configurations, unbuilt = write_configurations.planned(
        {"door-unit": ("x86_64-debug-asan", "x86_64-release", "aarch64-debug-asan"), "other": ("x86_64-debug-tsan",)},
        ["door-unit", "other"],
    )
    expect(
        "the plan takes the base of each ledger first, then the table of the gate, and names a configuration that the "
        "host does not build",
        [item.ledger_name for item in configurations] == ["x86_64-debug-asan", "x86_64-debug-tsan", "x86_64-release"]
        and unbuilt == ("aarch64-debug-asan",),
    )


def write_cases(expect: testing.Expect) -> None:
    """Hold one command that writes the falls of each configuration, and the bytes after a refused count."""
    with tempfile.TemporaryDirectory(prefix="write-configurations-") as work_text:
        root = Path(work_text)
        planted_tree(root)
        seen: list[str] = []
        status, printed = testing.captured(lambda: write_configurations.run(root, [], 3, planted_steps(FALLS, seen)))
        written = rule_ledger.read(counted_rule(root), root)
        expect(
            "one command writes the fall of each configuration that the host builds, the base first, and no count "
            "below its row stays",
            status == 0
            and seen
            == [
                "build x86_64-debug-asan 3",
                "write door-unit x86_64-debug-asan",
                "build x86_64-release 3",
                "write door-unit x86_64-release",
            ]
            and written.expected("x86_64-debug-asan") == FALLS["x86_64-debug-asan"]
            and written.expected("x86_64-release") == FALLS["x86_64-release"],
        )
        expect(
            "the write keeps the head of the ledger and the counts of the configuration that the host does not build, "
            "and it writes a note for that configuration",
            written.comment.startswith("# The head of the planted ledger.")
            and written.expected("aarch64-debug-asan") == {KEY_A: 3, KEY_B: 2}
            and "the host does not build the configuration aarch64-debug-asan" in printed,
        )
        before = rule_ledger.ledger_path("door-unit", root).read_bytes()
        rises = {**FALLS, "x86_64-release": {KEY_A: 1, KEY_B: 3}}
        lower = {**rises, "x86_64-debug-asan": {KEY_A: 1, KEY_B: 2}}
        seen.clear()
        status, printed = testing.captured(lambda: write_configurations.run(root, [], 3, planted_steps(lower, seen)))
        expect(
            "a count above its row that the write of a later configuration refuses stops the command, and the ledger "
            "gets its bytes from before the call, also after the fall that the base wrote",
            status == 1
            and seen[-1] == "write door-unit x86_64-release"
            and rule_ledger.ledger_path("door-unit", root).read_bytes() == before
            and "No ledger is written" in printed,
        )
        status, printed = testing.captured(
            lambda: write_configurations.run(root, ["erased-brand"], 3, planted_steps(FALLS, []))
        )
        expect(
            "a rule that is not counted is a usage error, and the message names the counted rules",
            status == 2 and "erased-brand is not a counted rule" in printed and "door-unit" in printed,
        )


def main(argv: list[str]) -> int:
    """Run each case, and return 0 when each case holds."""
    return testing.run(__name__.removeprefix("assay."), argv, (plan_cases, write_cases))
