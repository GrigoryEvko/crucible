"""The test of the witness of the engine plugin (assay.guard.engines.plugin.witness), with no compiler.

The cases plant manifests, outcomes of a compile and plant directories in
throwaway trees.  They do a test of the reader of the manifest and of the
functions that compare the rows with the findings and the diagnostics.  They
also do a test of the judgement of one compile and of the tables of the white
space.  The last cases do a test of the copy of a plant and of the status with
no compiler.

`guard selftest tests.guard.engines.plugin.test_witness` runs the test.  Each
case writes one line.  Exit 0 when each case holds, else 1.
"""

import contextlib
import io
import tempfile
from pathlib import Path

from assay.core import rows as rows_grammar
from assay.core import ruletable, testing
from assay.core.exit import ExitCode
from assay.guard.engines.plugin import witness
from assay.readers import sections

MANIFEST = """# a planted manifest
compile report unit.cpp
exit report pass
finding report quarantine std-object unit.cpp:3:7 | std::vector (std::vector<int>)
finding report quarantine region unit.cpp:5:1 | PROBE: a reason
finding report quarantine opted-out unit.cpp:6:9 | raw-pointer-object int*
finding report contract-specifier contract-specifier unit.cpp:8:20 | pre

compile error unit.cpp mode=error out=no errors=report
flag error -fcontracts -I ../outside
exit error fail
error error - | failed to initialize plugin {plugin}
text error | CRUCIBLE_PRE(condition)

compile object unit.cpp stage=c stamp=00ab findings=report notes=report root=src rules=- build=build
exit object pass
missing object report
note object other.cpp:2 | quarantine: c-library-call memset

table-unit unit.cpp
table tables/bad.txt refused 2 | the layer has a second row
table tables/good.txt accepted
white-space refused
reads {repo}/utils/rule-table.txt
procedure dependencies-named
"""


def refuses(text: str) -> int | None:
    """Return the line at which parse_manifest() refuses TEXT, or None when it reads the text."""
    try:
        witness.parse_manifest(text, "witness.txt")
    except rows_grammar.RowsError as problem:
        return problem.line
    return None


def manifest_cases(expect: testing.Expect) -> None:
    """Do a test of the reader of the manifest: each row kind, the keys, the defaults and each refusal."""
    manifest = witness.parse_manifest(MANIFEST, "witness.txt")
    report, error, planted = manifest.compiles["report"], manifest.compiles["error"], manifest.compiles["object"]
    expect(
        "a compile has the default root, rules and mode, a report and the stage S",
        report.arguments == (("root", "."), ("rules", "rules.txt"), ("mode", "report"))
        and report.stage == "S"
        and report.has_report
        and not report.has_object,
    )
    expect(
        "the flag rows of a compile keep their words in sequence",
        error.flags == ("-fcontracts", "-I", "../outside") and not error.has_report and error.errors_of == "report",
    )
    expect(
        "the value - gives no argument, and the keys stamp, build and stage c are kept",
        planted.argument("rules") == witness.NO_VALUE
        and planted.argument("root") == "src"
        and planted.argument("stamp") == "00ab"
        and planted.argument("build") == "build"
        and planted.has_object,
    )
    expect(
        "findings=OTHER adds the finding rows of OTHER to the compile",
        manifest.expected["object"].findings == manifest.expected["report"].findings
        and len(manifest.expected["report"].findings) == 4,
    )
    expect(
        "a finding row gives its rule, its kind, its place and its entity",
        manifest.expected["report"].findings[0]
        == sections.Finding("unit.cpp", 3, 7, "quarantine", "std-object", "std::vector (std::vector<int>)"),
    )
    expect(
        "the error, note, text and missing rows belong to their compiles",
        manifest.expected["error"].errors == (("-", "failed to initialize plugin {plugin}"),)
        and manifest.expected["error"].texts == ("CRUCIBLE_PRE(condition)",)
        and manifest.expected["object"].notes == (("other.cpp:2", "quarantine: c-library-call memset"),)
        and manifest.expected["object"].missing == frozenset({"report"}),
    )
    expect(
        "the table rows, the white-space row, the reads row and the procedure row",
        manifest.table_unit == "unit.cpp"
        and [(case.path, case.line, case.message) for case in manifest.tables]
        == [("tables/bad.txt", 2, "the layer has a second row"), ("tables/good.txt", 0, "")]
        and manifest.white_space == ("refused",)
        and manifest.reads == ("{repo}/utils/rule-table.txt",)
        and manifest.procedures == ("dependencies-named",),
    )
    expect(
        "a manifest with a failed compile, a refused table and a procedure states a plant",
        manifest.is_refused and manifest.has_errors,
    )
    control = witness.parse_manifest("compile a unit.cpp\nexit a pass\n", "witness.txt")
    expect(
        "a manifest with no failure, no error row and no refused table states a control",
        not control.is_refused and not control.has_errors,
    )
    expect(
        "each malformed manifest is refused at its line",
        [
            refuses("bogus a\n"),
            refuses("compile a unit.cpp\n"),
            refuses("compile a unit.cpp\nexit a maybe\n"),
            refuses("compile a unit.cpp\ncompile a other.cpp\nexit a pass\n"),
            refuses("exit b pass\n"),
            refuses("compile a unit.cpp out=no\nexit a pass\nfinding a quarantine std-object unit.cpp:1:1 | x\n"),
            refuses("compile a unit.cpp\nexit a pass\nfinding a quarantine std-object unit.cpp:1 | x\n"),
            refuses("compile a unit.cpp stage=Q\nexit a pass\n"),
            refuses("compile a unit.cpp color=red\nexit a pass\n"),
            refuses("compile a unit.cpp timeout=0\nexit a pass\n"),
            refuses("compile a unit.cpp root=src\nexit a pass\ncompile b unit.cpp errors=a\nexit b fail\n"),
            refuses("compile a unit.cpp findings=b\nexit a pass\ncompile b unit.cpp findings=a\nexit b pass\n"),
            refuses("compile a unit.cpp\nexit a pass\nerror a unit.cpp:x | text\n"),
            refuses("table-unit u.cpp\ntable t.txt refused 0 | text\n"),
            refuses("table t.txt accepted\n"),
            refuses("white-space refused\nwhite-space refused\ntable-unit u.cpp\n"),
            refuses("procedure no-such-procedure\n"),
        ]
        == [1, 1, 2, 2, 1, 1, 3, 1, 1, 1, 3, 1, 3, 2, 0, 2, 1],
    )
    expect(
        "a place FILE:* and the place - are diagnostic places",
        refuses("compile a u.cpp\nexit a fail\nerror a u.cpp:* | x\nerror a - | y\n") is None,
    )


def finding(line: int, kind: str = "std-object", entity: str = "std::vector") -> sections.Finding:
    """Return a planted finding of unit.cpp at LINE."""
    return sections.Finding("unit.cpp", line, 1, "quarantine", kind, entity)


def comparison_cases(expect: testing.Expect) -> None:
    """Do a test of the functions that compare, of the derived diagnostics and of the paths of a text."""
    missing, unexpected = witness.finding_difference([finding(1), finding(1), finding(2)], [finding(1), finding(3)])
    expect(
        "finding_difference() counts each line and each row one time",
        missing == [finding(1), finding(2)] and unexpected == [finding(3)],
    )
    expect("equal findings give no difference", witness.finding_difference([finding(1)], [finding(1)]) == ([], []))
    given = [
        ("a.cpp:3", "quarantine: raw-pointer-object const char*; use a type"),
        ("a.cpp:3", "quarantine: raw-pointer-object char*; use a type"),
        ("b.h:9", "this region has no end"),
    ]
    rows = [
        ("a.cpp:3", "raw-pointer-object char*"),
        ("a.cpp:3", "raw-pointer-object const char*"),
        ("b.h:*", "region has no"),
    ]
    expect(
        "diagnostic_difference() pairs each row with one diagnostic, the longest text first",
        witness.diagnostic_difference(rows, given) == ([], []),
    )
    expect(
        "a row with no diagnostic is missing, and a diagnostic with no row is unexpected",
        witness.diagnostic_difference([("a.cpp:4", "x")], [("a.cpp:3", "x")])
        == ([("a.cpp:4", "x")], [("a.cpp:3", "x")]),
    )
    expect(
        "a place FILE:* holds each line of FILE and no other file",
        witness.holds_place("b.h:*", "b.h:12")
        and not witness.holds_place("b.h:*", "c.h:12")
        and witness.holds_place("-", "-")
        and not witness.holds_place("a.cpp:3", "a.cpp:4"),
    )
    contract = sections.Finding("unit.cpp", 8, 20, "contract-specifier", "contract-specifier", "pre")
    region = finding(5, "region", "PROBE: a reason")
    opted = finding(6, "opted-out", "raw-pointer-object int*")
    expect(
        "derived_errors() gives an error for each finding outside a region, of the rule of the finding",
        witness.derived_errors([finding(3), contract, region, opted])
        == [
            ("unit.cpp:3", "quarantine: std-object std::vector;"),
            ("unit.cpp:8", "the 'pre' contract specifier is not permitted in this tree"),
        ],
    )
    expect(
        "derived_notes() gives a note for each finding of the rule quarantine, also an opted-out one",
        witness.derived_notes([finding(3), contract, region, opted])
        == [
            ("unit.cpp:3", "quarantine: std-object std::vector"),
            ("unit.cpp:6", "quarantine: opted-out raw-pointer-object int*"),
        ],
    )
    work, plugin, repo = Path("/w/one"), Path("/r/build/p.so"), Path("/r")
    expect(
        "normalized() writes the paths of the work directory, the plugin and the repository as marks",
        witness.normalized(
            "/w/one/tree/a.cpp /w/one/tree /w/one/outside/b.h /r/build/p.so /r/include/x.h ‘q’", work, plugin, repo
        )
        == "a.cpp . ../outside/b.h {plugin} {repo}/include/x.h 'q'",
    )
    expect(
        "ccache_hit_count() adds the direct and the preprocessed hits",
        witness.ccache_hit_count("direct_cache_hit\t2\npreprocessed_cache_hit\t3\ncache_miss\t9\n") == 5
        and witness.ccache_hit_count("") == 0,
    )


def judgement_cases(expect: testing.Expect) -> None:
    """Do a test of judge() on planted outcomes of one compile."""
    manifest = witness.parse_manifest(MANIFEST, "witness.txt")
    report = manifest.compiles["report"]
    findings = tuple(manifest.expected["report"].findings)
    good = witness.Outcome(0, reports=(Path("r.quarantine"),), report=findings)
    expect(
        "an outcome with the exit and the lines of its rows holds",
        witness.judge(report, manifest.expected["report"], good, manifest) == [],
    )
    extra = witness.Outcome(0, reports=(Path("r.quarantine"),), report=(*findings, finding(9)))
    expect(
        "an extra finding line fails",
        any(
            "a finding line with no row" in item
            for item in witness.judge(report, manifest.expected["report"], extra, manifest)
        ),
    )
    short = witness.Outcome(1, reports=(Path("r.quarantine"),), report=findings[1:])
    failures = witness.judge(report, manifest.expected["report"], short, manifest)
    expect(
        "a missing finding line and a different exit fail",
        any("no finding line" in item for item in failures) and any("the exit is 1" in item for item in failures),
    )
    absent = witness.Outcome(0)
    expect(
        "a compile with a report and no report file fails",
        any("report" in item for item in witness.judge(report, manifest.expected["report"], absent, manifest)),
    )
    error = manifest.compiles["error"]
    derived = (
        ("unit.cpp:3", "quarantine: std-object std::vector (std::vector<int>); use a type"),
        ("unit.cpp:8", "the 'pre' contract specifier is not permitted in this tree: write it"),
        ("-", "failed to initialize plugin {plugin}"),
    )
    held = witness.Outcome(1, errors=derived, texts=("write 'CRUCIBLE_PRE(condition)' of",))
    expect(
        "the derived errors and the error rows hold the errors of the log",
        witness.judge(error, manifest.expected["error"], held, manifest) == [],
    )
    without_text = witness.Outcome(1, errors=derived)
    expect(
        "a text that no diagnostic holds fails",
        any(
            "no diagnostic holds" in item
            for item in witness.judge(error, manifest.expected["error"], without_text, manifest)
        ),
    )
    planted = manifest.compiles["object"]
    section = sections.Section("00ab", findings)
    notes = (
        ("unit.cpp:3", "quarantine: std-object std::vector (std::vector<int>)"),
        ("unit.cpp:6", "quarantine: opted-out raw-pointer-object int*"),
        ("other.cpp:2", "quarantine: c-library-call memset"),
    )
    object_outcome = witness.Outcome(0, notes=notes, section=section, has_object=True)
    expect(
        "a section with the stamp and the lines, and the derived notes, hold",
        witness.judge(planted, manifest.expected["object"], object_outcome, manifest) == [],
    )
    stale = witness.Outcome(0, notes=notes, section=sections.Section("ffff", findings), has_object=True)
    expect(
        "a section with a different stamp fails",
        any("stamp" in item for item in witness.judge(planted, manifest.expected["object"], stale, manifest)),
    )


def table_cases(expect: testing.Expect) -> None:
    """Do a test of the tables of the white space."""
    refused = witness.white_space_cases("refused")
    accepted = witness.white_space_cases("accepted")
    spaces = [chr(code) for code in range(0x110000) if chr(code).isspace() and chr(code) not in " \t\n"]
    expect(
        "each white-space character gives two refused tables and one accepted table, and the carriage return "
        "three refused tables",
        len(refused) == 2 * len(spaces) + 2 and len(accepted) == len(spaces) - 1,
    )
    by_name = {case.name: case for case in refused}
    expect(
        "a carriage return is refused with the message of the rows grammar, also in a comment",
        by_name["U+000D in a comment"].line == 1
        and by_name["U+000D in a comment"].message == witness.CARRIAGE_RETURN
        and by_name["the line ends \\r\\n"].message == witness.CARRIAGE_RETURN,
    )
    expect(
        "each other character is refused with the message of the schema at the line of the row",
        by_name["U+000B between two words"].line == 2
        and by_name["U+000B in a reason"].message == ruletable.white_space_message("\x0b"),
    )
    expect(
        "each white-space table has a path of its own",
        len({case.path for case in [*refused, *accepted]}) == len(refused) + len(accepted),
    )


def plant_cases(expect: testing.Expect) -> None:
    """Do a test of the copy of a plant and of the status of the witness with no compiler."""
    with tempfile.TemporaryDirectory(prefix="test-witness-") as scratch:
        root = Path(scratch)
        plant = root / "plant"
        (plant / "tree" / "include").mkdir(parents=True)
        (plant / "outside").mkdir()
        (plant / witness.MANIFEST).write_text("compile a unit.cpp\nexit a pass\n", encoding="utf-8")
        (plant / "tree" / "unit.cpp.planted").write_text("int unit = 1;\n", encoding="utf-8")
        (plant / "tree" / "include" / "Head.h.planted").write_text("#pragma once\n", encoding="utf-8")
        (plant / "outside" / "Outside.h.planted").write_text("#pragma once\n", encoding="utf-8")
        work = root / "work"
        work.mkdir()
        witness.copy_plant(plant, work)
        copied = sorted(path.relative_to(work).as_posix() for path in work.rglob("*") if path.is_file())
        expect(
            "copy_plant() copies each entry but the manifest, and removes the suffix .planted",
            copied == ["outside/Outside.h", "tree/include/Head.h", "tree/unit.cpp"],
        )
        with contextlib.redirect_stdout(io.StringIO()):
            no_compiler = witness.witness("quarantine", root, None, None)
            no_plugin = witness.witness("quarantine", root, "sh", root / "missing.so")
        expect(
            "the witness cannot run with no compiler or no plugin file, and gives the status 77",
            no_compiler == no_plugin == ExitCode.SKIPPED,
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(
        __name__.removeprefix("assay."),
        argv,
        (manifest_cases, comparison_cases, judgement_cases, table_cases, plant_cases),
    )
