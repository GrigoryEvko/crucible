"""The test of the family home of the engine plugin, and of the ledger of its rule trait-guard, on planted lines.

THE CASES
    A case gives planted finding lines to a context of the runner with a
    planted census.  A scratch tree holds a planted registry, a rule table
    with home rows and a build directory with a compile database.  The cases
    hold:
      * The path that a home compares, and the paths that a home row holds
      * The files that a build compiles, and the ledger rows that a run judges
      * One error for each finding line and each problem of the census, no
        finding for an at-home line, and the text of each finding
      * An error for a path of a home row that holds no site, in a build that
        compiles a file of the path.  A different build gives no error
      * One error for a rule table that does not read
      * A ledger row that admits a finding, and the reason of a read of a
        crucible gate
      * A ledger row of a file that the build does not compile
      * The counts of a write, and a write that refuses its options.
"""

import contextlib
import io
import tempfile
from collections import Counter
from pathlib import Path

from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import ruletable, testing
from assay.guard.engines.plugin import home, runner, trait_guard
from assay.readers import compdb
from assay.readers import sections as plugin_sections

RULE = trait_guard.RULE
REGISTRY_ROWS = (
    (
        "rule trait-guard engine=plugin scope=tracked ledger=count kinds=outside-home,open-read,at-home doc=XVI | a "
        "planted rule of the family home | specialize the trait in its home"
    ),
)
TABLE_TEXT = (
    "layer low 0 include/fixy/\n"
    "quarantine include/crucible/\n"
    "quarantine src/\n"
    "quarantine bench/\n"
    "home trait-guard fixy::retag_policy | a relation with no file\n"
    "home trait-guard admitted_retags include/fixy/Tagged.h | each edge stands in its header\n"
    "home trait-guard foundation::contracts::armed_cell include/fixy/ bench/ | a planted extension point\n"
)
# The line of the row of armed_cell in TABLE_TEXT.
ARMED_LINE = 7
FORGED = plugin_sections.Finding("src/a.cpp", 3, 8, RULE, "outside-home", "fixy::retag_policy")
AT_HOME = plugin_sections.Finding("include/fixy/Tagged.h", 5, 1, RULE, home.AT_HOME, "admitted_retags")
OPEN_READ = plugin_sections.Finding(
    "include/crucible/gates.h", 8, 43, RULE, trait_guard.OPEN_READ, "crucible::gates::runtime_open_v"
)


def scratch_tree(root: Path, table: str = TABLE_TEXT) -> Path:
    """Write the planted registry, the rule table and a build directory that compiles src/a.cpp, and return the
    build directory."""
    (root / "utils").mkdir(parents=True)
    (root / rule_registry.REGISTRY_PATH).write_text(rule_registry.planted(REGISTRY_ROWS), encoding="utf-8")
    (root / home.TABLE).write_text(table, encoding="utf-8")
    build_dir = root / "build"
    build_dir.mkdir()
    compdb.plant(build_dir, [{"directory": str(root), "file": str(root / "src" / "a.cpp"), "command": "g++ -c a.cpp"}])
    return build_dir


def context_of(
    root: Path, build_dir: Path, lines: list[plugin_sections.Finding], problems: tuple[runner.Problem, ...] = ()
) -> runner.PluginContext:
    """Return a context of the scratch tree with a planted census of the lines and the problems."""
    return runner.PluginContext(root, build_dir, census=runner.Census(1, frozenset(lines), problems=problems))


def rule_of() -> rule_registry.Rule:
    """Return the rule trait-guard of the planted registry."""
    registry, problems = rule_registry.examine(rule_registry.planted(REGISTRY_ROWS))
    if problems:
        raise AssertionError(f"the planted registry does not read: {problems}")
    return registry.rules[RULE]


def path_cases(expect: testing.Expect) -> None:
    """Hold the path that a home compares, the paths that a row holds, and the files that a build compiles."""
    expect(
        "a check file stands where its header stands",
        home.read_as("test/layer/checks/fixy/Tagged.cpp") == "include/fixy/Tagged.h",
    )
    expect(
        "a different file stands where it is", home.read_as("test/fixy/test_tagged.cpp") == "test/fixy/test_tagged.cpp"
    )
    expect(
        "a file of the check tree with a different suffix stands where it is",
        home.read_as("test/layer/checks/fixy/Tagged.h") == "test/layer/checks/fixy/Tagged.h",
    )
    expect("a directory of a row holds each file in it", ruletable.holds("include/fixy/", "include/fixy/os/File.h"))
    expect(
        "a file of a row holds only that file",
        ruletable.holds("include/fixy/Tagged.h", "include/fixy/Tagged.h")
        and not ruletable.holds("include/fixy/Tagged.h", "include/fixy/Tagged.hpp"),
    )
    build = home.Build(frozenset({"src/a.cpp", "bench/b.cpp"}))
    expect("each header under include/ is compiled", build.compiles("include/fixy/") and build.compiles("include/x.h"))
    expect(
        "a directory with a source is compiled, and one with no source is not",
        build.compiles("bench/") and not build.compiles("vessel/"),
    )
    expect(
        "a file is compiled only when it is a source", build.compiles("src/a.cpp") and not build.compiles("src/c.cpp")
    )
    expect(
        "a row with no file is judged, and a row of a file that the build does not compile is not",
        build.judges(rule_ledger.Key("", "kind", "entity"))
        and not build.judges(rule_ledger.Key("src/c.cpp", "kind", "entity")),
    )


def finding_cases(expect: testing.Expect) -> None:
    """Hold the errors of the rule: the finding lines, the problems, the paths with no site and a broken table."""
    with tempfile.TemporaryDirectory(prefix="plugin-home-") as scratch_text:
        root = Path(scratch_text).resolve()
        build_dir = scratch_tree(root)
        problem = runner.Problem("build/src/a.cpp.o", "the object has no section")
        found = trait_guard.findings(context_of(root, build_dir, [FORGED, AT_HOME, OPEN_READ], (problem,)))
        placed = sorted((item.path, item.line, item.kind, item.entity) for item in found)
        expect(
            "each finding line is one error with its kind and its entity, and an at-home line is none",
            ("src/a.cpp", 3, "outside-home", "fixy::retag_policy") in placed
            and ("include/crucible/gates.h", 8, trait_guard.OPEN_READ, "crucible::gates::runtime_open_v") in placed
            and not any(item.kind == home.AT_HOME for item in found),
        )
        expect("a problem of the census is one error", any(item.path == "build/src/a.cpp.o" for item in found))
        stale = [item for item in found if item.path == str(home.TABLE)]
        expect(
            "a path that holds no site is one error at the line of its row, and a path of no file of the build is none",
            [
                (item.line, "include/fixy/" in item.message, "bench/" in item.message.split("path ")[1][:7])
                for item in stale
            ]
            == [(ARMED_LINE, True, False)],
        )
        forged = next((item for item in found if item.kind == "outside-home"), None)
        expect(
            "the text of a site outside its home names the paths of its row",
            forged is not None and "its home row admits only no file" in forged.message,
        )
        expect("all findings are errors", all(item.level == "error" for item in found) and len(found) == 4)
    with tempfile.TemporaryDirectory(prefix="plugin-home-") as scratch_text:
        root = Path(scratch_text).resolve()
        build_dir = scratch_tree(root, TABLE_TEXT + "home trait-guard | a row with no entity\n")
        found = trait_guard.findings(context_of(root, build_dir, [FORGED]))
        expect(
            "a rule table that does not read is one error that names the line of the row, and no finding line counts",
            [(item.path, ":8: " in item.message) for item in found] == [(str(home.TABLE), True)],
        )


def judge_cases(expect: testing.Expect) -> None:
    """Hold the judgement of the ledger: a row that admits a finding, the reason of a crucible read, and a row of a
    different build."""
    rule = rule_of()
    forged_key = rule_ledger.Key(FORGED.file, FORGED.kind, FORGED.entity)
    read_key = rule_ledger.Key(OPEN_READ.file, OPEN_READ.kind, OPEN_READ.entity)
    other_key = rule_ledger.Key("bench/b.cpp", "outside-home", "fixy::retag_policy")
    with tempfile.TemporaryDirectory(prefix="plugin-home-") as scratch_text:
        root = Path(scratch_text).resolve()
        build_dir = scratch_tree(root, TABLE_TEXT.replace(" include/fixy/ bench/", " bench/"))
        context = context_of(root, build_dir, [FORGED, AT_HOME, OPEN_READ])

        def judged_with(read_reason: str) -> list[check_report.Finding]:
            """Store a ledger with the reason in its crucible row, and return the judged findings."""
            ledger = rule_ledger.Ledger(
                RULE,
                rule.policy,
                "# planted\n",
                base={
                    forged_key: rule_ledger.Entry(forged_key, 1, "a planted forgery"),
                    read_key: rule_ledger.Entry(read_key, 1, read_reason),
                    other_key: rule_ledger.Entry(other_key, 2, "a file of a different build"),
                },
            )
            rule_ledger.store(ledger, root)
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                return trait_guard.judge(rule, context)

        judged = judged_with("a planted runtime read")
        expect(
            "the rows admit each finding, and a row of a file that the build does not compile is not judged",
            not [item for item in judged if item.level == "error"],
        )
        expect(
            "a crucible read that a row admits gives one warning with its reason",
            [
                item.message.endswith("a planted runtime read")
                for item in judged
                if item.level == "warning" and item.path == OPEN_READ.file
            ]
            == [True],
        )
        judged = judged_with("")
        expect(
            "a crucible read whose row gives no reason is one error",
            [item.path for item in judged if item.level == "error"] == [rule_ledger.shown_path(RULE)],
        )
        ledger = rule_ledger.read(rule, root)
        counts = home.counts_to_write(rule, ledger, context.findings_of(rule), home.build_of(context))
        expect(
            "a write counts each finding and keeps each row of a file that the build does not compile",
            counts == Counter({forged_key: 1, read_key: 1, other_key: 2}),
        )


def write_cases(expect: testing.Expect) -> None:
    """Hold a write that refuses its options, and a write with no build directory."""
    rule = rule_of()
    with tempfile.TemporaryDirectory(prefix="plugin-home-") as scratch_text:
        root = Path(scratch_text).resolve()
        build_dir = scratch_tree(root)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            refused = home.write(rule, runner.WriteRequest(root, build_dir, add_configuration=True))
            imported = home.write(rule, runner.WriteRequest(root, build_dir, import_log=root / "log.txt"))
            skipped = home.write(rule, runner.WriteRequest(root, None))
        expect("a write refuses --add-configuration and --import", refused == 2 and imported == 2)
        expect("a write with no build directory does not apply", skipped == runner.SKIPPED)
        expect("a refused write writes no ledger", not rule_ledger.ledger_path(RULE, root).exists())


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (path_cases, finding_cases, judge_cases, write_cases))
