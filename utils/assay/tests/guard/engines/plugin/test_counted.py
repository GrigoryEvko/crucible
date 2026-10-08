"""The test of the counted rules of the engine plugin (assay.guard.engines.plugin.counted) and of their two rules.

`guard selftest tests.guard.engines.plugin.test_counted` runs the test.  The
rule heavy-include is the counted rule of most cases: a planted census, a
planted configuration and a planted ledger in a throwaway directory.  The
seal rows of the rule detsafe-ledger and the cost rows of the rule
heavy-include are cases too.  Exit 0 when each case holds, else 1.
"""

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
BASE = runner.BuildConfiguration("planted", (("kind", "planted"), ("tier", "x86-64-0")))
OTHER = runner.BuildConfiguration("other", (("kind", "other"), ("tier", "x86-64-0")))
LEDGER_HEAD = (
    "# A planted ledger.\nformat 1\nrule heavy-include\n"
    "configuration planted kind planted\nconfiguration planted tier x86-64-0\n"
    "configuration other kind other\nconfiguration other tier x86-64-0\n"
)
COST_ROWS = (
    "\n" + header_costs.MARKER + "\n"
    "heavy <chrono> | 1.12 s, 70628 lines\n"
    "light <cstdint> | 0.02 s, 178 lines\n"
    "light <meta> | 0.37 s, 34799 lines\n"
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
        rule_registry.planted([rule_registry.measured_rule(RULE, 1.25, 1.25, "x", "the planted threshold")]),
        encoding="utf-8",
    )


def context_of(
    root: Path, found: list[sections.Finding], configuration: runner.BuildConfiguration
) -> runner.PluginContext:
    """Return a planted context: a census of the finding lines, and a configuration."""
    census = runner.Census(1, frozenset(found))
    return runner.PluginContext(root, None, census, configuration)


def key_cases(expect: testing.Expect) -> None:
    """Check the counts and the problems of the findings, and the words of a key."""
    found = [
        check_report.Finding("error", "a.h", 1, RULE, "x", KIND, "<chrono>"),
        check_report.Finding("error", "a.h", 2, RULE, "x", KIND, "<chrono>"),
        check_report.Finding("error", "obj.o", 0, RULE, "a problem"),
    ]
    counts = counted.counts_of(found)
    expect(
        "counts_of counts each file, kind and entity, with no problem",
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
        plant_tree(
            root,
            "a.h | heavy-include | 1 | <chrono> | -\nb.h | heavy-include | 1 | <thread> | a door\n"
            "@other | a.h | heavy-include | +1 | <chrono> | -\n",
        )
        rule = rule_ledger.rule_of(RULE)
        base_found = [finding("a.h", 2, KIND, "<chrono>"), finding("b.h", 3, KIND, "<thread>")]
        status, text = testing.captured(lambda: heavy_include.run(rule, context_of(root, base_found, BASE), None))
        expect(
            "a count equal to its row passes, with a warning for a row with no reason and none for a keep row",
            status == 0 and "a.h includes <chrono>" in text and "b.h includes" not in text,
        )
        more = [*base_found, finding("c.h", 4, KIND, "<chrono>")]
        status, text = testing.captured(lambda: heavy_include.run(rule, context_of(root, more, BASE), None))
        expect("a new include of a heavy header is an error", status == 1 and "c.h includes <chrono> 1 times" in text)
        status, text = testing.captured(lambda: heavy_include.run(rule, context_of(root, base_found[1:], BASE), None))
        expect(
            "a count less than its row is an error that asks for a write",
            status == 1 and "the ledger admits 1 includes of <chrono> in a.h" in text,
        )
        unmeasured = [*base_found, finding("d.h", 5, heavy_include.UNMEASURED, "<cstdio>")]
        status, text = testing.captured(lambda: heavy_include.run(rule, context_of(root, unmeasured, BASE), None))
        expect(
            "an include of an unmeasured header is an error that names the measure",
            status == 1 and "derive header-costs --measure BUILD_DIR cstdio" in text,
        )
        other = [
            finding("a.h", 2, KIND, "<chrono>"),
            finding("a.h", 9, KIND, "<chrono>"),
            finding("b.h", 3, KIND, "<thread>"),
        ]
        status, _text = testing.captured(lambda: heavy_include.run(rule, context_of(root, other, OTHER), None))
        expect("a configuration other than the base adds its difference to the base count", status == 0)
        status, text = testing.captured(lambda: heavy_include.run(rule, context_of(root, base_found, OTHER), None))
        expect(
            "a failure of a configuration other than the base writes its rows for --import",
            status == 1 and rule_ledger.ROW_MARK in text,
        )
        unknown = runner.BuildConfiguration("unknown", (("kind", "unknown"), ("tier", "x86-64-0")))
        status, text = testing.captured(lambda: heavy_include.run(rule, context_of(root, base_found, unknown), None))
        expect(
            "a configuration that the ledger does not hold does not apply, and writes its rows",
            status == runner.SKIPPED and rule_ledger.ROW_MARK in text,
        )
        try:
            heavy_include.judge(rule, context_of(root, base_found, unknown))
            refused = False
        except runner.NotApplicable:
            refused = True
        expect("judge refuses a configuration that the ledger does not hold", refused)
        judged = heavy_include.judge(rule, context_of(root, more, BASE))
        expect(
            "judge gives the errors of the verdict",
            any(item.level == "error" and "c.h" in item.message for item in judged),
        )
        plant_tree(
            root, "a.h | heavy-include | 1 | <chrono> | -\n", COST_ROWS.replace("heavy <chrono>", "light <chrono>")
        )
        status, text = testing.captured(lambda: heavy_include.run(rule, context_of(root, base_found[:1], BASE), None))
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
    """Check the parts of the write: the increases that a mode permits, the rows of gone files and the import."""
    ledger = rule_ledger.parse(
        LEDGER_HEAD + "a.h | heavy-include | 1 | <chrono> | -\n"
        "@other | a.h | heavy-include | +1 | <chrono> | -\n"
        "@other | gone.h | heavy-include | +2 | <chrono> | -\n"
        "@other | @build/gen.h | heavy-include | +1 | <chrono> | -\n",
        "planted",
        RULE,
        rule_registry.Policy.COUNT,
    )
    counts = {rule_ledger.Key("a.h", KIND, "<chrono>"): 2}
    permitted = functools.partial(counted.increases_permitted, heavy_include.SPEC, ledger, "planted")
    for mode, wanted in (
        (rule_ledger.WriteMode.LOWER, None),
        (rule_ledger.WriteMode.RAISE, True),
        (rule_ledger.WriteMode.ADMIT, True),
    ):
        result, _text = testing.captured(functools.partial(permitted, counts, {}, mode))
        expect(f"the mode {mode} {'refuses' if wanted is None else 'permits'} a count above its row", result == wanted)
    unmeasured = {rule_ledger.Key("d.h", heavy_include.UNMEASURED, "<cstdio>"): 1}
    result, _text = testing.captured(functools.partial(permitted, unmeasured, {}, rule_ledger.WriteMode.ADMIT))
    expect("--admit permits no increase of a kind that is not an admitted kind", result is None)
    result, _text = testing.captured(functools.partial(permitted, {}, {}, rule_ledger.WriteMode.LOWER))
    expect("a write with no increase permits the write in each mode", result is False)
    with tempfile.TemporaryDirectory(prefix="counted-") as work:
        root = Path(work)
        (root / "a.h").write_text("", encoding="utf-8")
        kept = counted.drop_gone_files(ledger, root, {})
        other = kept.deltas["other"]
        expect(
            "a write of the base removes the difference rows of a source file that the tree does not hold",
            rule_ledger.Key("gone.h", KIND, "<chrono>") not in other,
        )
        expect(
            "a write of the base keeps the rows of a file of the build directory and of a file that stays",
            rule_ledger.Key("@build/gen.h", KIND, "<chrono>") in other
            and rule_ledger.Key("a.h", KIND, "<chrono>") in other,
        )
        log = root / "log.txt"
        log.write_text(
            "noise\n"
            + "\n".join(
                rule_ledger.rows_of_counts(
                    ledger, "other", list(OTHER.facts), {rule_ledger.Key("a.h", KIND, "<chrono>"): 3}
                )
            )
            + "\n",
            encoding="utf-8",
        )
        path = rule_ledger.ledger_path(RULE, root)
        path.parent.mkdir(parents=True)
        path.write_text(rule_ledger.render(ledger), encoding="utf-8")
        status, _text = testing.captured(lambda: counted.import_rows(rule_ledger.rule_of(RULE), root, log))
        written = rule_ledger.read(rule_ledger.rule_of(RULE), root)
        expect(
            "--import puts the rows of a log into the ledger",
            status == 0 and written.expected("other").get(rule_ledger.Key("a.h", KIND, "<chrono>")) == 3,
        )
        before = path.read_text(encoding="utf-8")
        refused, text = testing.captured(
            lambda: counted.import_rows(rule_ledger.rule_of(RULE), root, log, lambda _ledger: "a planted problem")
        )
        expect(
            "--import with a refusal of the rule writes no ledger and gives the problem",
            refused == 1 and "a planted problem" in text and path.read_text(encoding="utf-8") == before,
        )
        log.write_text("ledger-row: not a row\n", encoding="utf-8")
        status, _text = testing.captured(lambda: counted.import_rows(rule_ledger.rule_of(RULE), root, log))
        expect("--import refuses a log with rows that do not read", status == 1)
    expect(
        "--add-configuration refuses a name that is not a lowercase word",
        "lowercase" in counted.add_refusal(ledger, None, "Upper"),
    )
    expect(
        "--add-configuration refuses a configuration that the ledger holds",
        "holds the configuration" in counted.add_refusal(ledger, "planted", "planted"),
    )
    expect(
        "--add-configuration refuses the base name with other keys",
        "base configuration" in counted.add_refusal(ledger, None, "planted"),
    )
    expect("--add-configuration permits a new name", counted.add_refusal(ledger, None, "third") == "")


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
    return testing.run(__name__.removeprefix("assay."), argv, (key_cases, verdict_cases, write_cases, seal_cases))
