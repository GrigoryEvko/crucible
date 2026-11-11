"""The test of the counted rules of the engine plugin (assay.guard.engines.plugin.counted) and of their two rules.

`guard selftest tests.guard.engines.plugin.test_counted` runs the test.  The
rule heavy-include is the counted rule of most cases: a planted census and a
planted ledger in a throwaway directory.  The write of one planted build and
the reduce over two planted builds are cases too.  The seal rows of the rule
detsafe-ledger and the cost rows of the rule heavy-include are cases too.
Exit 0 when each case holds, else 1.
"""

import dataclasses
import functools
import tempfile
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import testing
from assay.guard.engines.plugin import counted, detsafe_ledger, heavy_include, runner
from assay.loop.derive import header_costs
from assay.readers import sections

RULE = heavy_include.RULE
KIND = heavy_include.HEAVY
LEDGER_HEAD = "# A planted ledger.\nformat 1\nrule heavy-include\n"
COST_ROWS = (
    "\n" + header_costs.MARKER + "\n"
    "heavy <chrono> | 1.120 G, 70628 lines\n"
    "light <cstdint> | 0.020 G, 178 lines\n"
    "light <meta> | 0.370 G, 34799 lines\n"
)


def finding(path: str, line: int, kind: str, header: str) -> sections.Finding:
    """Return one finding line of the rule heavy-include."""
    return sections.Finding(path, line, 0, RULE, kind, header)


def plant_tree(root: Path, ledger_rows: str, table_rows: str = COST_ROWS) -> None:
    """Write the ledger of the rule, a rule table with cost rows and a registry with the threshold of the rule."""
    path = rule_ledger.ledger_path(RULE, root)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(LEDGER_HEAD + ledger_rows, encoding="utf-8")
    (root / header_costs.TABLE).write_text("quarantine src/\n" + table_rows, encoding="utf-8")
    (root / rule_registry.REGISTRY_PATH).write_text(
        rule_registry.planted([rule_registry.measured_rule(RULE, 1.25, "x", "the planted threshold")]),
        encoding="utf-8",
    )


def context_of(root: Path, found: list[sections.Finding]) -> runner.PluginContext:
    """Return a planted context: a census of the finding lines."""
    return runner.PluginContext(root, None, runner.Census(1, frozenset(found)))


def key_cases(expect: testing.Expect) -> None:
    """Check the counts and the problems of the findings, and the words of a key."""
    found = [
        check_report.Finding("error", "a.h", 1, RULE, "x", KIND, "<chrono>"),
        check_report.Finding("error", "a.h", 2, RULE, "x", KIND, "<chrono>"),
        check_report.Finding("error", "obj.o", 0, RULE, "a problem"),
    ]
    counts = counted.counts_of(found)
    expect(
        "counts_of counts each path, kind and entity, with no problem",
        counts == {rule_ledger.Key("a.h", KIND, "<chrono>"): 2},
    )
    expect(
        "problems_of gives each finding with no kind", [item.path for item in counted.problems_of(found)] == ["obj.o"]
    )
    expect(
        "described names the entity when the row has one",
        counted.described(KIND, "<chrono>") == "the kind heavy-include and the entity <chrono>"
        and counted.described(KIND, "") == "the kind heavy-include",
    )


def verdict_cases(expect: testing.Expect) -> None:
    """Check the verdict of the rule heavy-include: each count against its row, and the warnings."""
    with tempfile.TemporaryDirectory(prefix="counted-") as work:
        root = Path(work)
        plant_tree(root, "a.h | heavy-include | 2 | <chrono> | -\nb.h | heavy-include | 1 | <thread> | a door\n")
        rule = rule_ledger.rule_of(RULE)
        base_found = [
            finding("a.h", 2, KIND, "<chrono>"),
            finding("a.h", 9, KIND, "<chrono>"),
            finding("b.h", 3, KIND, "<thread>"),
        ]
        status, text = testing.captured(lambda: heavy_include.run(rule, context_of(root, base_found), None))
        expect(
            "a count equal to its row passes, with a warning for a row with no reason and none for a keep row",
            status == 0 and "a.h includes <chrono>" in text and "b.h includes" not in text,
        )
        more = [*base_found, finding("c.h", 4, KIND, "<chrono>")]
        status, text = testing.captured(lambda: heavy_include.run(rule, context_of(root, more), None))
        expect("a new include of a heavy header is an error", status == 1 and "c.h includes <chrono> 1 times" in text)
        risen = [*base_found, finding("a.h", 11, KIND, "<chrono>")]
        status, text = testing.captured(lambda: heavy_include.run(rule, context_of(root, risen), None))
        expect("a count above its row is an error", status == 1 and "a.h includes <chrono> 3 times" in text)
        status, text = testing.captured(lambda: heavy_include.run(rule, context_of(root, base_found[1:]), None))
        expect(
            "a count less than its row passes, because a build of a different configuration can give the row",
            status == 0,
        )
        unmeasured = [*base_found, finding("d.h", 5, heavy_include.UNMEASURED, "<cstdio>")]
        status, text = testing.captured(lambda: heavy_include.run(rule, context_of(root, unmeasured), None))
        expect(
            "an include of an unmeasured header is an error that names the measure",
            status == 1 and "derive header-costs --measure BUILD_DIR cstdio" in text,
        )
        judged = heavy_include.judge(rule, context_of(root, more))
        expect(
            "judge gives the errors of the verdict",
            any(item.level == "error" and "c.h" in item.message for item in judged),
        )
        plant_tree(
            root, "a.h | heavy-include | 1 | <chrono> | -\n", COST_ROWS.replace("heavy <chrono>", "light <chrono>")
        )
        status, text = testing.captured(lambda: heavy_include.run(rule, context_of(root, base_found[:1]), None))
        expect(
            "a cost row with a kind that does not agree with its measure is an error",
            status == 1 and "the row of <chrono> is light" in text,
        )
        plant_tree(root, "a.h | heavy-include | 1 | <chrono> | -\n", "\n" + header_costs.MARKER + "\n")
        expect(
            "a rule table with no cost row is an error",
            any("no heavy row and no light row" in item.message for item in heavy_include.cost_problems(root)),
        )
        plant_tree(root, "a.h | heavy-include | 1 | <chrono> | -\n", "")
        expect(
            "a rule table with no marker line is an error",
            any("marker line 0 times" in item.message for item in heavy_include.cost_problems(root)),
        )


def write_cases(expect: testing.Expect) -> None:
    """Check the increases that a mode of a write permits."""
    ledger = rule_ledger.parse(
        LEDGER_HEAD + "a.h | heavy-include | 1 | <chrono> | -\n", "planted", RULE, rule_registry.Policy.COUNT
    )
    counts = {rule_ledger.Key("a.h", KIND, "<chrono>"): 2}
    permitted = functools.partial(counted.increases_permitted, heavy_include.SPEC, ledger)
    for mode, wanted in (
        (rule_ledger.WriteMode.LOWER, None),
        (rule_ledger.WriteMode.RAISE, True),
        (rule_ledger.WriteMode.ADMIT, True),
    ):
        result, _text = testing.captured(functools.partial(permitted, counts, mode))
        expect(f"the mode {mode} {'refuses' if wanted is None else 'permits'} a count above its row", result == wanted)
    unmeasured = {rule_ledger.Key("d.h", heavy_include.UNMEASURED, "<cstdio>"): 1}
    result, _text = testing.captured(functools.partial(permitted, unmeasured, rule_ledger.WriteMode.ADMIT))
    expect("--admit permits no increase of a kind that is not an admitted kind", result is None)
    result, _text = testing.captured(functools.partial(permitted, {}, rule_ledger.WriteMode.LOWER))
    expect("a write with no increase permits the write in each mode", result is False)
    split = {rule_ledger.Key("b.h", KIND, "<chrono>"): 1}
    result, text = testing.captured(functools.partial(permitted, split, rule_ledger.WriteMode.LOWER))
    expect(
        "the write reads no move: the mode lower refuses the row of a new path, also when a different path falls",
        result is None and "a path that the ledger does not hold" in text,
    )


def build_cases(expect: testing.Expect) -> None:
    """Check the write of one planted build, which lowers no row, and the reduce over two planted builds."""
    with tempfile.TemporaryDirectory(prefix="counted-") as work:
        root = Path(work)
        plant_tree(root, "a.h | heavy-include | 2 | <chrono> | -\narm.h | heavy-include | 1 | <chrono> | -\n")
        first, second, broken = root / "first", root / "second", root / "broken"
        planted = {
            first: [finding("a.h", 2, KIND, "<chrono>")],
            second: [finding("a.h", 2, KIND, "<chrono>"), finding("arm.h", 3, KIND, "<chrono>")],
        }

        def planted_context(context_root: Path, build: Path | None) -> runner.PluginContext:
            """Return the planted census of a build, or a census with a problem for the build `broken`."""
            if build == broken:
                return runner.PluginContext(
                    context_root, None, runner.Census(1, problems=(runner.Problem("x.o", "a planted problem"),))
                )
            return context_of(context_root, planted[build] if build is not None else [])

        spec = dataclasses.replace(heavy_include.SPEC, context_of=planted_context)
        rule = rule_ledger.rule_of(RULE)
        before = rule_ledger.read(rule, root).expected()
        status, _text = testing.captured(lambda: counted.write(spec, rule, runner.WriteRequest(root, first)))
        expect(
            "the write of one build lowers no row, also a row that the build does not give",
            status == 0 and rule_ledger.read(rule, root).expected() == before,
        )
        status, _text = testing.captured(
            lambda: counted.write(spec, rule, runner.WriteRequest(root, first, more_builds=(second, broken)))
        )
        expect(
            "a reduce with a build that has a problem writes nothing",
            status == 1 and rule_ledger.read(rule, root).expected() == before,
        )
        status, _text = testing.captured(
            lambda: counted.write(spec, rule, runner.WriteRequest(root, first, more_builds=(second,)))
        )
        expect(
            "the reduce takes the largest count of each key over two builds, and it lowers each row to that count",
            status == 0
            and rule_ledger.read(rule, root).expected()
            == {rule_ledger.Key("a.h", KIND, "<chrono>"): 1, rule_ledger.Key("arm.h", KIND, "<chrono>"): 1},
        )


def seal_cases(expect: testing.Expect) -> None:
    """Check the seal rows of the rule detsafe-ledger: a missing root, a ledger directory that a row does not seal."""
    with tempfile.TemporaryDirectory(prefix="counted-") as work:
        root = Path(work)
        (root / "include" / "crucible" / "ledger").mkdir(parents=True)
        (root / "include" / "fixy" / "deep" / "ledger").mkdir(parents=True)
        (root / "include" / "crucible" / "Root.h").write_text("#pragma once\n", encoding="utf-8")
        table = root / detsafe_ledger.TABLE
        table.parent.mkdir(parents=True)
        prefixes = "include/crucible/ledger/ include/fixy/ledger/ include/fixy/deep/ledger/"
        table.write_text(f"seal include/crucible/Root.h {prefixes}\n", encoding="utf-8")
        expect(
            "a seal row of each ledger directory and of each project root has no problem",
            detsafe_ledger.seal_problems(root) == [],
        )
        table.write_text(
            "seal include/crucible/Root.h include/crucible/ledger/ include/fixy/ledger/\n"
            f"seal include/crucible/Gone.h {prefixes}\n",
            encoding="utf-8",
        )
        problems = [item.message for item in detsafe_ledger.seal_problems(root)]
        expect(
            "a row that does not seal a ledger directory of the tree is a problem",
            any("does not seal include/fixy/deep/ledger/" in text for text in problems),
        )
        expect(
            "a row with a root that the tree does not hold is a problem",
            any("the tree does not hold it" in text for text in problems),
        )
        table.write_text("quarantine include/crucible/\n", encoding="utf-8")
        expect(
            "a table with no seal row is a problem",
            any("no seal row" in item.message for item in detsafe_ledger.seal_problems(root)),
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."), argv, (key_cases, verdict_cases, write_cases, build_cases, seal_cases)
    )
