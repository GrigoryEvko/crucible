"""The test of the configuration of a build and of the one policy (assay.core.configuration).

`guard selftest tests.core.test_configuration` runs the test.  Each case plants
a ledger with configurations in the rows grammar, or a log of rows in a
throwaway directory.  The cases hold the rows that judge a build and the
largest count of each key (THE MAXIMUM of assay.core.ledger).  They also hold
the rows that a write writes, --add-configuration and the import of rows.
Exit 0 when each case holds, else 1.
"""

import tempfile
from pathlib import Path

from assay.core import configuration as build_config
from assay.core import ledger as rule_ledger
from assay.core import registry as rule_registry
from assay.core import report as check_report
from assay.core import testing
from assay.readers import cmake

RULE = "quarantine"
KIND = "c-library-call"
BASE = build_config.BuildConfiguration("planted", (("kind", "planted"), ("tier", "x86-64-0")))
OTHER = build_config.BuildConfiguration("other", (("kind", "other"), ("tier", "x86-64-0")))
THIRD = build_config.BuildConfiguration("third", (("kind", "third"),))
HEAD = (
    "# A planted ledger.\nformat 1\nrule quarantine\n"
    "configuration planted kind planted\nconfiguration planted tier x86-64-0\n"
    "configuration other kind other\nconfiguration other tier x86-64-0\n"
)
# The base counts one close in a.cpp.  The configuration other counts one more close in a.cpp, and one dup in b.cpp.
ROWS = (
    "a.cpp | c-library-call | 1 | close | -\n"
    "@other | a.cpp | c-library-call | +1 | close | an arm of other\n"
    "@other | b.cpp | c-library-call | +1 | dup | -\n"
)
CLOSE = rule_ledger.Key("a.cpp", KIND, "close")
DUP = rule_ledger.Key("b.cpp", KIND, "dup")


def planted_ledger(text: str = HEAD + ROWS) -> rule_ledger.Ledger:
    """Return the planted ledger of the text."""
    return rule_ledger.parse(text, "planted", RULE, rule_registry.Policy.COUNT)


def finding(path: str, entity: str) -> check_report.Finding:
    """Return one error of the planted rule with the key of a row."""
    return check_report.Finding("error", path, 1, RULE, "a planted finding", KIND, entity)


def cache_cases(expect: testing.Expect) -> None:
    """Hold the one word of a cache value, and the tier of a configuration."""
    expect(
        "normalized reads a BOOL, a path and a string",
        (
            build_config.normalized("BOOL", "TRUE"),
            build_config.normalized("BOOL", "OFF"),
            build_config.normalized("PATH", "x-NOTFOUND"),
            build_config.normalized("FILEPATH", "/usr/bin/x"),
            build_config.normalized("STRING", ""),
            build_config.normalized("STRING", "Debug"),
        )
        == ("on", "off", "unset", "set", "unset", "Debug"),
    )
    expect("a configuration gives its tier, and ? with no tier", BASE.tier == "x86-64-0" and THIRD.tier == "?")
    with tempfile.TemporaryDirectory(prefix="test-configuration-") as work:
        try:
            build_config.read_configuration(Path(work), [])
            expect("a build with no build-kind.txt is a ConfigurationError", False)
        except build_config.ConfigurationError as problem:
            expect("a build with no build-kind.txt is a ConfigurationError", "build-kind.txt" in str(problem))
        expect("a build with no CMake cache is no parse build", not build_config.is_parse_build(Path(work)))
        cmake.plant(Path(work), cache={build_config.SYNTAX_OPTION: ("BOOL", "ON")})
        expect("a build whose cache sets the syntax option is a parse build", build_config.is_parse_build(Path(work)))
        cmake.plant(Path(work), cache={build_config.SYNTAX_OPTION: ("BOOL", "OFF")})
        expect(
            "a build whose cache clears the syntax option is no parse build",
            not build_config.is_parse_build(Path(work)),
        )
    expect(
        "the syntax option is a key and a word of the name",
        build_config.SYNTAX_OPTION in build_config.CONFIGURATION
        and ("syntax", build_config.SYNTAX_OPTION) in build_config.NAME_OPTIONS,
    )


def judging_cases(expect: testing.Expect) -> None:
    """Hold the rows that judge a build (THE POLICY)."""
    ledger = planted_ledger()
    kind_only = planted_ledger("# A planted ledger.\nformat 1\nrule quarantine\nconfiguration planted kind planted\n")
    expect(
        "a row that gives only the kind holds a build of that kind with more facts",
        build_config.is_held(kind_only, BASE),
    )
    expect("a row does not hold a build of a different kind", not build_config.is_held(kind_only, OTHER))
    held = build_config.judging(RULE, ledger, OTHER)
    expect(
        "the rows of its name judge a held build, with no note",
        held == build_config.Judging("other", "the rows of the configuration other", ""),
    )
    other_tier = build_config.BuildConfiguration("other", (("kind", "other"), ("tier", "x86-64-9")))
    judged = build_config.judging(RULE, ledger, other_tier)
    expect(
        "the rows of its name judge a build with other facts, and the note names each fact that differs",
        judged.name == "other" and "tier is x86-64-9 in the build and x86-64-0 in the ledger" in judged.note,
    )
    unheld = build_config.judging(RULE, ledger, THIRD)
    expect(
        "the largest count of each key judges a build of a name that the ledger does not hold, and the words name "
        "the configurations",
        unheld.name == rule_ledger.MAXIMUM
        and "over the configurations planted, other" in unheld.words
        and "holds no configuration third" in unheld.words,
    )
    empty = planted_ledger("# A planted ledger.\nformat 1\nrule quarantine\n")
    expect(
        "the base rows of a ledger with no configuration judge each build",
        build_config.judging(RULE, empty, THIRD) == build_config.Judging(None, build_config.BASE_WORDS, ""),
    )


def maximum_cases(expect: testing.Expect) -> None:
    """Hold the largest count of each key over the configurations (assay.core.ledger, THE MAXIMUM)."""
    ledger = planted_ledger()
    expect(
        "the largest count of each key is the largest count over the configurations",
        ledger.expected(rule_ledger.MAXIMUM) == {CLOSE: 2, DUP: 1},
    )
    expect(
        "the rows of the largest count give the reason and the line of a difference row",
        ledger.reason_of(CLOSE, rule_ledger.MAXIMUM) == "an arm of other"
        and ledger.line_of(DUP, rule_ledger.MAXIMUM) == ledger.deltas["other"][DUP].line,
    )
    rule = rule_ledger.rule_of(RULE)
    fewer = rule_ledger.compare(rule, ledger, [finding("a.cpp", "close")], rule_ledger.MAXIMUM)
    expect(
        "a count below the largest count is no error, because the build can compile fewer arms",
        [item for item in fewer if item.level == "error"] == [],
    )
    arm = rule_ledger.compare(rule, ledger, [finding("b.cpp", "dup")], rule_ledger.MAXIMUM)
    expect(
        "an arm that a different configuration counts passes",
        [item for item in arm if item.level == "error"] == [],
    )
    new = rule_ledger.compare(rule, ledger, [finding("a.cpp", "close"), finding("c.cpp", "read")], rule_ledger.MAXIMUM)
    expect(
        "a new finding is an error",
        [item.path for item in new if item.level == "error"] == ["c.cpp"],
    )
    base = rule_ledger.compare(rule, ledger, [finding("a.cpp", "close"), finding("b.cpp", "dup")], None)
    expect(
        "the base rows give an error for an arm that only a different configuration counts",
        any(item.level == "error" and item.path == "b.cpp" for item in base),
    )


def write_cases(expect: testing.Expect) -> None:
    """Hold the rows that a write writes, and the refusals of --add-configuration (THE POLICY)."""
    ledger = planted_ledger()
    expect(
        "--add-configuration refuses a name that is not a lowercase word",
        "lowercase" in build_config.add_refusal(ledger, None, "Upper"),
    )
    expect(
        "--add-configuration refuses a configuration that the ledger holds",
        "holds the configuration" in build_config.add_refusal(ledger, "planted", "planted"),
    )
    expect(
        "--add-configuration refuses the base name with other keys",
        "base configuration" in build_config.add_refusal(ledger, None, "planted"),
    )
    expect("--add-configuration permits a new name", build_config.add_refusal(ledger, None, "third") == "")
    target = build_config.write_target(RULE, ledger, BASE, False)
    expect(
        "a write of a configuration that the ledger holds writes its rows",
        isinstance(target, build_config.WriteTarget) and target.name == "planted" and not target.is_added,
    )
    refusal = build_config.write_target(RULE, ledger, THIRD, False)
    expect(
        "a write of a configuration that the ledger does not hold writes nothing, and names --add-configuration",
        isinstance(refusal, str) and "--add-configuration" in refusal,
    )
    added = build_config.write_target(RULE, ledger, THIRD, True)
    expect(
        "--add-configuration adds the configuration of the build with its facts, after the others",
        isinstance(added, build_config.WriteTarget)
        and added.is_added
        and added.ledger.configurations[-1] == rule_ledger.Configuration("third", THIRD.facts),
    )
    empty = planted_ledger("# A planted ledger.\nformat 1\nrule quarantine\n")
    base_target = build_config.write_target(RULE, empty, THIRD, False)
    expect(
        "a write into a ledger with no configuration writes the base rows",
        isinstance(base_target, build_config.WriteTarget) and base_target.name is None,
    )


def import_cases(expect: testing.Expect) -> None:
    """Hold the rows that a run prints for an import, and the import of a log."""
    rule = rule_ledger.rule_of(RULE)
    ledger = planted_ledger()
    with tempfile.TemporaryDirectory(prefix="test-configuration-") as work:
        root = Path(work)
        path = rule_ledger.ledger_path(RULE, root)
        path.parent.mkdir(parents=True)
        path.write_text(rule_ledger.render(ledger), encoding="utf-8")
        _, printed = testing.captured(lambda: build_config.print_rows(RULE, ledger, OTHER, {CLOSE: 3}, False))
        expect(
            "a build of a configuration other than the base prints its rows for an import",
            rule_ledger.ROW_MARK in printed and "--import" in printed,
        )
        _, base_printed = testing.captured(lambda: build_config.print_rows(RULE, ledger, BASE, {CLOSE: 3}, False))
        expect("a build of the base prints no rows", base_printed == "")
        log = root / "log.txt"
        log.write_text(f"noise\n{printed}", encoding="utf-8")
        status, _ = testing.captured(lambda: build_config.import_rows(rule, root, log))
        written = rule_ledger.read(rule, root)
        expect(
            "--import puts the rows of a log into the ledger",
            status == 0 and written.expected("other").get(CLOSE) == 3,
        )
        before = path.read_text(encoding="utf-8")
        refused, text = testing.captured(
            lambda: build_config.import_rows(rule, root, log, lambda _ledger: "a planted problem")
        )
        expect(
            "--import with a refusal of the rule writes no ledger and gives the problem",
            refused == 1 and "a planted problem" in text and path.read_text(encoding="utf-8") == before,
        )
        log.write_text(f"{rule_ledger.ROW_MARK}not a row\n", encoding="utf-8")
        status, _ = testing.captured(lambda: build_config.import_rows(rule, root, log))
        expect("--import refuses a log with rows that do not read", status == 1)


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (cache_cases, judging_cases, maximum_cases, write_cases, import_cases),
    )
