"""The test of the rule home of the engine plugin and of its ledger, on planted lines.

THE CASES
    A case gives planted finding lines to a context of the runner with a
    planted census.  A scratch tree holds a planted registry, a rule table
    with home rows and a build directory with a compile database.  The cases
    hold:
      * The path that a home compares, and the paths that a home row holds
      * The files that a build compiles, and the ledger rows that a run judges.
        A planted build with no build directory judges each path and each row
      * One error for each finding line and each problem of the census, no
        finding for an at-home line, and the text of each kind
      * An error for a path of a home row that holds no site, in a build that
        compiles a file of the path.  A different build gives no error
      * One error for a rule table that does not read
      * A ledger row that admits a finding, and the reason of a read of a
        crucible gate
      * A ledger row of a file that the build does not compile
      * A ledger row of a refused kind, which admits no finding, and a write
        that refuses a finding of a refused kind
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
from assay.guard.engines.plugin import home, runner
from assay.readers import compdb
from assay.readers import sections as plugin_sections

RULE = home.RULE
REGISTRY_ROWS = (
    (
        "rule home engine=plugin scope=tracked ledger=count kinds=outside-home,federation-outside-home,"
        "owner-outside-home,combine-outside-home,second-body,derived-name,away-from-arguments,open-read,at-home "
        "doc=XVI | a planted rule home | move the site to its home"
    ),
)
TABLE_TEXT = (
    "layer low 0 include/fixy/\n"
    "quarantine include/crucible/\n"
    "quarantine src/\n"
    "quarantine bench/\n"
    "home trait fixy::retag_policy | a relation with no file\n"
    "home trait admitted_retags include/fixy/Tagged.h | each edge stands in its header\n"
    "home trait foundation::contracts::armed_cell include/fixy/ bench/ | a planted extension point\n"
    "home federation foundation::permissions::FederationAdmission include/fixy/Federation.h | the planted door\n"
)
# The line of the row of armed_cell in TABLE_TEXT.
ARMED_LINE = 7
FORGED = plugin_sections.Finding("src/a.cpp", 3, 8, RULE, home.Kind.OUTSIDE_HOME, "fixy::retag_policy")
AT_HOME = plugin_sections.Finding("include/fixy/Tagged.h", 5, 1, RULE, home.Kind.AT_HOME, "admitted_retags")
DOOR_AT_HOME = plugin_sections.Finding(
    "include/fixy/Federation.h", 9, 7, RULE, home.Kind.AT_HOME, "foundation::permissions::FederationAdmission"
)
OPEN_READ = plugin_sections.Finding(
    "include/crucible/gates.h", 8, 43, RULE, home.Kind.OPEN_READ, "crucible::gates::runtime_open_v"
)
DOOR = plugin_sections.Finding(
    "src/a.cpp", 6, 44, RULE, home.Kind.FEDERATION_OUTSIDE_HOME, "foundation::permissions::FederationAdmission"
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
    root: Path,
    build_dir: Path | None,
    lines: list[plugin_sections.Finding],
    problems: tuple[runner.Problem, ...] = (),
) -> runner.PluginContext:
    """Return a context of the scratch tree with a planted census of the lines and the problems."""
    return runner.PluginContext(root, build_dir, census=runner.Census(1, frozenset(lines), problems=problems))


def rule_of() -> rule_registry.Rule:
    """Return the rule home of the planted registry."""
    registry, problems = rule_registry.examine(rule_registry.planted(REGISTRY_ROWS))
    if problems:
        raise AssertionError(f"the planted registry does not read: {problems}")
    return registry.rules[RULE]


def judged_quietly(rule: rule_registry.Rule, context: runner.PluginContext) -> list[check_report.Finding]:
    """Return the judged findings of the rule, with no output."""
    with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
        return home.judge(rule, context)


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
    planted = home.build_of(runner.PluginContext(Path("/planted")))
    expect(
        "a context with no build directory gives a planted build that compiles each path and judges each row",
        planted.compiles("vessel/") and planted.judges(rule_ledger.Key("src/c.cpp", "kind", "entity")),
    )


def finding_cases(expect: testing.Expect) -> None:
    """Hold the errors of the rule: the finding lines, the problems, the paths with no site and a broken table."""
    with tempfile.TemporaryDirectory(prefix="plugin-home-") as scratch_text:
        root = Path(scratch_text).resolve()
        build_dir = scratch_tree(root)
        problem = runner.Problem("build/src/a.cpp.o", "the object has no section")
        lines = [FORGED, AT_HOME, DOOR_AT_HOME, OPEN_READ, DOOR]
        found = home.findings(context_of(root, build_dir, lines, (problem,)))
        placed = sorted((item.path, item.line, item.kind, item.entity) for item in found)
        expect(
            "each finding line is one error with its kind and its entity, and an at-home line is none",
            ("src/a.cpp", 3, home.Kind.OUTSIDE_HOME, "fixy::retag_policy") in placed
            and ("include/crucible/gates.h", 8, home.Kind.OPEN_READ, "crucible::gates::runtime_open_v") in placed
            and ("src/a.cpp", 6, home.Kind.FEDERATION_OUTSIDE_HOME, DOOR.entity) in placed
            and not any(item.kind == home.Kind.AT_HOME for item in found),
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
        forged = next((item for item in found if item.kind == home.Kind.OUTSIDE_HOME), None)
        expect(
            "the text of a site outside its home names the paths of its row",
            forged is not None and "its home row admits only no file" in forged.message,
        )
        door = next((item for item in found if item.kind == home.Kind.FEDERATION_OUTSIDE_HOME), None)
        expect(
            "the text of a second admission names its home and the key that it builds",
            door is not None and "include/fixy/Federation.h" in door.message and "handshake" in door.message,
        )
        expect("all findings are errors", all(item.level == "error" for item in found) and len(found) == 5)
        planted = home.findings(context_of(root, None, [FORGED, AT_HOME, DOOR_AT_HOME]))
        expect(
            "a planted census with no build directory judges each path of each row, also a path of no file",
            sorted((item.line, "bench/" in item.message) for item in planted if item.path == str(home.TABLE))
            == [(ARMED_LINE, False), (ARMED_LINE, True)],
        )
    with tempfile.TemporaryDirectory(prefix="plugin-home-") as scratch_text:
        root = Path(scratch_text).resolve()
        build_dir = scratch_tree(root, TABLE_TEXT + "home trait | a row with no entity\n")
        found = home.findings(context_of(root, build_dir, [FORGED]))
        expect(
            "a rule table that does not read is one error that names the line of the row, and no finding line counts",
            [(item.path, ":9: " in item.message) for item in found] == [(str(home.TABLE), True)],
        )


def message_cases(expect: testing.Expect) -> None:
    """Hold the text of each kind of the rule: it names the fault and the action."""
    homes = ruletable.parse(TABLE_TEXT, "planted").homes

    def text_of(kind: str, entity: str) -> str:
        """Return the message of a planted line of the kind."""
        return home.message(plugin_sections.Finding("src/a.cpp", 1, 1, RULE, kind, entity), homes)

    expect(
        "a second body names the one body to call",
        home.CANONICAL_COMBINE in text_of(home.Kind.SECOND_BODY, "planted::mix"),
    )
    expect(
        "a host owner with no row of its own asks for a row",
        "no home row names it" in text_of(home.Kind.OWNER_OUTSIDE_HOME, "foundation::effects::host::Forged"),
    )
    expect(
        "a split away from its tags asks for the file of its tags",
        "the file that defines its tags" in text_of(home.Kind.AWAY_FROM_ARGUMENTS, "planted::can_split_into"),
    )
    expect(
        "each kind of the rule has a text of its own",
        len({text_of(kind, "planted::x") for kind in home.Kind if kind != home.Kind.AT_HOME}) == len(home.Kind) - 1,
    )


def judge_cases(expect: testing.Expect) -> None:
    """Hold the judgement of the ledger: a row that admits a finding, the reason of a crucible read, a row of a
    different build and a row of a refused kind."""
    rule = rule_of()
    forged_key = rule_ledger.Key(FORGED.file, FORGED.kind, FORGED.entity)
    read_key = rule_ledger.Key(OPEN_READ.file, OPEN_READ.kind, OPEN_READ.entity)
    other_key = rule_ledger.Key("bench/b.cpp", home.Kind.OUTSIDE_HOME, "fixy::retag_policy")
    door_key = rule_ledger.Key(DOOR.file, DOOR.kind, DOOR.entity)
    with tempfile.TemporaryDirectory(prefix="plugin-home-") as scratch_text:
        root = Path(scratch_text).resolve()
        build_dir = scratch_tree(root, TABLE_TEXT.replace(" include/fixy/ bench/", " bench/"))
        context = context_of(root, build_dir, [FORGED, AT_HOME, DOOR_AT_HOME, OPEN_READ])

        def judged_with(read_reason: str, lines: list[plugin_sections.Finding]) -> list[check_report.Finding]:
            """Store a ledger with the reason in its crucible row and a row of a refused kind, and judge the lines."""
            ledger = rule_ledger.Ledger(
                RULE,
                rule.policy,
                "# planted\n",
                base={
                    forged_key: rule_ledger.Entry(forged_key, 1, "a planted forgery"),
                    read_key: rule_ledger.Entry(read_key, 1, read_reason),
                    other_key: rule_ledger.Entry(other_key, 2, "a file of a different build"),
                    door_key: rule_ledger.Entry(door_key, 1, "a row that tries to admit a second admission"),
                },
            )
            rule_ledger.store(ledger, root)
            return judged_quietly(rule, context_of(root, build_dir, lines))

        judged = judged_with("a planted runtime read", [FORGED, AT_HOME, DOOR_AT_HOME, OPEN_READ])
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
        judged = judged_with("", [FORGED, AT_HOME, DOOR_AT_HOME, OPEN_READ])
        expect(
            "a crucible read whose row gives no reason is one error",
            [item.path for item in judged if item.level == "error"] == [rule_ledger.shown_path(RULE)],
        )
        judged = judged_with("a planted runtime read", [FORGED, AT_HOME, DOOR_AT_HOME, OPEN_READ, DOOR])
        expect(
            "a row of a refused kind admits no finding, and the second admission stays one error",
            [(item.kind, item.line) for item in judged if item.level == "error"]
            == [(home.Kind.FEDERATION_OUTSIDE_HOME, DOOR.line)],
        )
        counts = home.build_counts(rule, context)
        expect(
            "the counts of a build for a write hold each finding, and no row of a file that the build does not compile",
            counts == Counter({forged_key: 1, read_key: 1}),
        )
        refused = False
        try:
            home.build_counts(rule, context_of(root, build_dir, [FORGED, AT_HOME, DOOR_AT_HOME, DOOR]))
        except runner.BuildError as problem:
            refused = "no row admits" in str(problem)
        expect("a write refuses a build with a finding of a refused kind", refused)


def write_cases(expect: testing.Expect) -> None:
    """Hold a write with no build directory, and a write of a build that the rule does not apply to."""
    rule = rule_of()
    with tempfile.TemporaryDirectory(prefix="plugin-home-") as scratch_text:
        root = Path(scratch_text).resolve()
        build_dir = scratch_tree(root)
        with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
            skipped = home.write(rule, runner.WriteRequest(root, None))
            refused = home.write(rule, runner.WriteRequest(root, build_dir))
        expect("a write with no build directory does not apply", skipped == runner.SKIPPED)
        expect("a write of a build with no build kind does not apply, and it writes nothing", refused == runner.SKIPPED)
        expect("a refused write writes no ledger", not rule_ledger.ledger_path(RULE, root).exists())


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (path_cases, finding_cases, message_cases, judge_cases, write_cases),
    )
