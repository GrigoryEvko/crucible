"""The test of the runner of the engine build (assay.guard.engines.buildsys.runner).

`guard selftest tests.guard.engines.buildsys.test_runner` runs the test.  Each
case gives the context a planted build or no build, and reads the inputs or
runs the rule build-graph of the registry.  Exit 0 when each case holds, else 1.
"""

import tempfile
from pathlib import Path

from assay.core import registry as rule_registry
from assay.core import testing
from assay.core.paths import REPO_ROOT
from assay.guard.engines.buildsys import build_graph, planted, runner
from assay.guard.engines.meta import runner as meta_runner


def raises_not_applicable(action: object) -> bool:
    """Return True when a call with no argument raises NotApplicable."""
    assert callable(action)
    try:
        action()
    except meta_runner.NotApplicable:
        return True
    return False


def context_cases(expect: testing.Expect) -> None:
    """A context with no build refuses each input of a build, and a context reads each input one time."""
    empty = runner.BuildContext(REPO_ROOT, None)
    expect("require_build() with no build directory is not applicable", raises_not_applicable(empty.require_build))
    expect("kind() with no build directory is not applicable", raises_not_applicable(empty.kind))
    expect("census() with no build directory is not applicable", raises_not_applicable(empty.census))
    expect(
        "budget() of an ID with no measured rule is not applicable",
        raises_not_applicable(lambda: empty.budget("registry")),
    )
    expect("budget() of a measured rule gives its limit", empty.budget("header-constexpr-ops").limit == 1e6)
    expect(
        "shown() gives a path of the tree relative to the root",
        empty.shown(REPO_ROOT / "utils" / "registry.txt") == "utils/registry.txt",
    )
    with tempfile.TemporaryDirectory(prefix="test-runner-") as scratch:
        plant = Path(scratch) / "plant-kind"
        plant.mkdir()
        (plant / planted.BUILD_FILE).write_text("format 1\ncompile a.o a.cpp\n", encoding="utf-8")
        context = planted.plant_context(plant, Path(scratch) / "tree")
        expect("a build with no build-kind.txt is not applicable for kind()", raises_not_applicable(context.kind))
        expect("entries() reads the compile database one time", context.entries() is context.entries())
        expect("graph() of a build with no build.ninja is not applicable", raises_not_applicable(context.graph))


def run_cases(expect: testing.Expect) -> None:
    """Run the rule build-graph on its plants, and give its findings to the judge of the ledger."""
    rule = rule_registry.load().rules["build-graph"]
    status, _ = testing.captured(lambda: runner.witness(rule))
    expect("the witness of the rule build-graph holds", status == 0)
    with tempfile.TemporaryDirectory(prefix="test-runner-") as scratch:
        plant = REPO_ROOT / meta_runner.PLANTS / rule.id / "plant-link-order-only"
        context = build_graph.plant_context(plant, Path(scratch) / "tree")
        findings = context.findings_of(rule)
        expect(
            "findings_of() gives the error of a compile that waits for a link",
            bool(findings) and all(item.level == "error" for item in findings) and context.rule is rule,
        )
        status, said = testing.captured(lambda: meta_runner.run(rule, context, None))
        expect("the run of the rule exits 1 with an error", status == 1 and f"{len(findings)} error(s)" in said)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (context_cases, run_cases))
