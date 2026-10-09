"""The test of the reader of the SARIF log of GCC (assay.readers.sarif).

`guard selftest tests.readers.test_sarif` runs the test.  Each case writes one
line.  Exit 0 when each case holds, else 1.

The planted cases build a log with each shape that the reader reads.  The
compile cases use the patched compiler: ASSAY_TEST_CXX names it, and the
registration of the test in test/CMakeLists.txt gives the compiler of the
build.  The compile cases compare render() with the text output of the same
compile, with no caret display.
"""

import json
import os
import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.core import proc, testing
from assay.readers import sarif

PROBE = Path("/w/probe.cpp")
# The source of the compile cases.  It gives a constraint chain, a constexpr chain, a macro trace, a warning of an
# option and an error with no context.  Each error has a line of its own.
SOURCE = """\
#define ASSIGN(target, value) target = value
template <typename T> concept has_size = requires(T value) { value.size(); };
template <has_size T> void use(T) {}
void probe() { use(0); }
constexpr int divide(int value) { return 10 / value; }
constexpr int broken = divide(0);
void macro_user() { int* pointer = nullptr; ASSIGN(pointer, 3.5); }
int unused_user() { int unused_local; return 0; }
struct shape_holder { int {} ; };
"""
FATAL_SOURCE = "#include <absent_header_of_the_sarif_test.h>\n"


def _location(path: str, line: int, column: int, snippet: str = "", base: str = "") -> dict[str, object]:
    """Return a SARIF location object with a region and, when `snippet` is not empty, a context region."""
    artifact: dict[str, object] = {"uri": path}
    if base:
        artifact["uriBaseId"] = base
    physical: dict[str, object] = {"artifactLocation": artifact, "region": {"startLine": line, "startColumn": column}}
    if snippet:
        physical["contextRegion"] = {"startLine": line, "snippet": {"text": snippet + "\n"}}
    return {"physicalLocation": physical}


def _text_location(line: int, column: int) -> dict[str, object]:
    """Return the property gcc/textLocation of a location in PROBE."""
    return {"file": str(PROBE), "line": line, "column": column}


def _nested(message: str, nesting: int, line: int, kind: str = "note", option: str = "") -> dict[str, object]:
    """Return a related location of a nested diagnostic in PROBE."""
    properties: dict[str, object] = {
        "nestingLevel": nesting,
        "gcc/kind": kind,
        "gcc/textLocation": _text_location(line, 3),
    }
    if option:
        properties["gcc/option"] = option
    return {**_location("probe.cpp", line, 1, base="PWD"), "message": {"text": message}, "properties": properties}


def _context(message: str, kind: str, nesting: int, line: int, owner: int, placement: str) -> dict[str, object]:
    """Return a related location of a context line in PROBE."""
    properties = {
        "nestingLevel": nesting,
        "gcc/context": kind,
        "gcc/contextOf": owner,
        "gcc/placement": placement,
        "gcc/textLocation": _text_location(line, 5),
    }
    return {**_location("probe.cpp", line, 1, base="PWD"), "message": {"text": message}, "properties": properties}


def _planted_log() -> dict[str, object]:
    """Return a SARIF log with each shape that the reader reads."""
    first = {
        "ruleId": "error",
        "level": "error",
        "message": {"text": "no match for {{ the call }}, see [the manual](https://gcc.gnu.org/onlinedocs/x.html)"},
        "locations": [_location("probe.cpp", 4, 16, "void probe() { use(0); }", base="PWD")],
        "properties": {"gcc/textLocation": _text_location(4, 19)},
        "relatedLocations": [
            _context("In function 'void probe()':", "function", 0, 4, 0, "before"),
            _nested("there is 1 candidate", 1, 4),
            _nested("a nested error", 2, 3, kind="error", option="-Wsome-option"),
            _context("required from here", "instantiation-step", 3, 4, 2, "before"),
            _location("probe.h", 1, 1, base="PWD"),
        ],
    }
    second = {
        "ruleId": "-Wunused-variable",
        "level": "warning",
        "message": {"text": "unused variable 'local'"},
        "locations": [_location("/w/probe.cpp", 8, 25)],
        "relatedLocations": [_context("in definition of macro 'ASSIGN'", "macro", 0, 1, 0, "after")],
    }
    third = {
        "ruleId": "fatal error",
        "level": "error",
        "message": {"text": "absent.h: No such file or directory"},
        "locations": [_location("/w/probe.cpp", 9, 10)],
    }
    return {
        "version": "2.1.0",
        "runs": [
            {
                "originalUriBaseIds": {"PWD": {"uri": "file:///w/"}},
                "invocations": [{"arguments": ["/usr/libexec/gcc/cc1plus", "-quiet"]}],
                "results": [first, second, third],
            }
        ],
    }


EXPECTED_LINES = (
    "/w/probe.cpp: In function 'void probe()':",
    "/w/probe.cpp:4:19: error: no match for { the call }, see the manual",
    "  • there is 1 candidate",
    "    /w/probe.cpp:4:3:",
    "      • required from here",
    "        /w/probe.cpp:4:5:   ",
    "    • error: a nested error [-Wsome-option]",
    "      /w/probe.cpp:3:3:",
    "/w/probe.cpp:8:25: warning: unused variable 'local' [-Wunused-variable]",
    "/w/probe.cpp:1:5: note: in definition of macro 'ASSIGN'",
    "/w/probe.cpp:9:10: fatal error: absent.h: No such file or directory",
    sarif.TERMINATED,
)


def parsed(document: object) -> sarif.Log:
    """Return the log of a JSON document."""
    return sarif.parse(json.dumps(document).encode(), "planted.sarif")


def refuses(action: Callable[[], object], text: str = "") -> bool:
    """Return True when an action raises SarifError, with `text` in its message."""
    try:
        action()
    except sarif.SarifError as problem:
        return text in str(problem)
    return False


def parse_cases(expect: testing.Expect) -> None:
    """Check the records that parse() gives for each shape of a result and a related location."""
    log = parsed(_planted_log())
    first, second, third = log.diagnostics
    expect("the tool is the last part of the first argument of the invocation", log.tool == "cc1plus")
    expect(
        "a result has its kind, its path with the base, the text location and the source line",
        (first.kind, first.path, first.line, first.column, first.snippet)
        == (sarif.Kind.ERROR, "/w/probe.cpp", 4, 19, "void probe() { use(0); }"),
    )
    expect(
        "a message gives one brace for each pair, and the text of a hyperlink",
        first.message == "no match for { the call }, see the manual",
    )
    expect(
        "the include chain gives no record, and each other related location gives one",
        [child.kind or child.context for child in first.children]
        == [sarif.Context.FUNCTION, sarif.Kind.NOTE, sarif.Kind.ERROR, sarif.Context.INSTANTIATION_STEP],
    )
    expect(
        "a nested diagnostic gives its option, and a result of an option gives the option and the level as kind",
        first.children[2].option == "-Wsome-option"
        and second.kind is sarif.Kind.WARNING
        and second.option == "-Wunused-variable",
    )
    expect(
        "contexts() gives the context lines of one diagnostic of a result",
        [record.message for record in first.contexts(0)] == ["In function 'void probe()':"]
        and [record.message for record in first.contexts(2)] == ["required from here"],
    )
    expect(
        "is_error_head holds for an error at the nest level 0, and not for a nested error or a warning",
        first.is_error_head
        and not first.children[2].is_error_head
        and not second.is_error_head
        and third.is_error_head,
    )
    expect("records() gives each result, then its related records", len(sarif.records(log)) == 8)


def render_cases(expect: testing.Expect) -> None:
    """Check the text lines, the groups and the heads that render() gives."""
    rendering = sarif.render(parsed(_planted_log()))
    expect("render() gives the lines of the text output", rendering.lines == EXPECTED_LINES)
    expect(
        "each error head starts a group, and a warning is a part of the group before it",
        rendering.groups == (0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1) and len(rendering.errors) == 2,
    )
    expect("heads gives the line of each error head", rendering.heads == (1, 10))
    empty = sarif.render(sarif.Log("cc1plus", ()))
    expect(
        "a log with no result gives no line, no group and no head",
        empty.lines == () and empty.groups == () and empty.heads == (),
    )
    warning_only = parsed({"version": "2.1.0", "runs": [{"results": [_planted_log_result("warning")]}]})
    expect("the lines of a log with no error head have the group -1", sarif.render(warning_only).groups == (-1,))


def _plugin_log() -> dict[str, object]:
    """Return a log with a note of the plugin in a function, an error of the plugin, and an error of GCC after them."""
    note = {
        "ruleId": "note",
        "level": "note",
        "message": {"text": "quarantine: std-object std::vector"},
        "locations": [_location("/w/probe.cpp", 6, 22)],
        "relatedLocations": [
            _context("In function 'int count()':", "function", 0, 6, 0, "before"),
            _context("in definition of macro 'MAKE'", "macro", 0, 1, 0, "after"),
        ],
    }
    error = {
        "ruleId": "error",
        "level": "error",
        "message": {"text": "quarantine: c-library-call abort; use a type or an entity of fixy or foundation"},
        "locations": [_location("/w/probe.cpp", 7, 5)],
    }
    compiler = {
        "ruleId": "error",
        "level": "error",
        "message": {"text": "too many arguments to function 'int count()'"},
        "locations": [_location("/w/probe.cpp", 8, 9)],
    }
    run = {"originalUriBaseIds": {"PWD": {"uri": "file:///w/"}}, "results": [note, error, compiler]}
    return {"version": "2.1.0", "runs": [run]}


def plugin_cases(expect: testing.Expect) -> None:
    """Check is_plugin_result(), plugin_finding() and the hidden results of render()."""
    log = parsed(_plugin_log())
    note, error, compiler = log.diagnostics
    expect(
        "is_plugin_result() accepts a note and an error of the plugin, and refuses an error of GCC",
        sarif.is_plugin_result(note) and sarif.is_plugin_result(error) and not sarif.is_plugin_result(compiler),
    )
    expect(
        "plugin_finding() gives the kind and the entity, and the entity of an error stops before its advice",
        sarif.plugin_finding(note) == ("std-object", "std::vector")
        and sarif.plugin_finding(error) == ("c-library-call", "abort"),
    )
    hidden = sarif.render(log, sarif.is_plugin_result)
    expect(
        "a hidden result gives only its context lines before it, and the next diagnostic keeps them",
        hidden.lines
        == (
            "/w/probe.cpp: In function 'int count()':",
            "/w/probe.cpp:8:9: error: too many arguments to function 'int count()'",
        ),
    )
    expect(
        "a hidden error is no error head, so the error of GCC is the one head",
        hidden.errors == (compiler,) and hidden.heads == (1,) and hidden.groups == (0, 0),
    )
    expect("render() with no function gives each line of the plugin too", len(sarif.render(log).lines) == 5)
    with tempfile.TemporaryDirectory(prefix="test-sarif-") as scratch:
        include = Path(scratch) / "include" / "foundation"
        include.mkdir(parents=True)
        (include / "Platform.h").write_text("#pragma once\n", encoding="utf-8")
        roots = Path(scratch) / "layer-roots" / "foundation"
        roots.mkdir(parents=True)
        (roots / "foundation").symlink_to(include)
        through = moved(note, str(roots / "foundation" / "Platform.h"))
        expect(
            "plugin_path() gives the path of realpath() for a result through a link of an include directory",
            sarif.plugin_path(through) == os.path.realpath(include / "Platform.h"),
        )
    expect("plugin_path() gives an empty text for a result with no file", sarif.plugin_path(moved(note, "")) == "")


def moved(record: sarif.Diagnostic, path: str) -> sarif.Diagnostic:
    """Return a copy of a record with a different path."""
    return sarif.Diagnostic(
        record.kind,
        record.context,
        path,
        record.line,
        record.column,
        record.nesting,
        record.message,
        record.option,
        record.owner,
        record.after,
        record.snippet,
        record.children,
    )


def _planted_log_result(level: str) -> dict[str, object]:
    """Return a result of an option with one level and no related location."""
    return {
        "ruleId": "-Wextra",
        "level": level,
        "message": {"text": "a text"},
        "locations": [_location("/w/x.cpp", 1, 2)],
    }


def refusal_cases(expect: testing.Expect) -> None:
    """Check each log that parse() and render() refuse."""
    document = _planted_log()
    without_kind = json.loads(json.dumps(document))
    del without_kind["runs"][0]["results"][0]["relatedLocations"][1]["properties"]["gcc/kind"]
    expect(
        "a nested diagnostic with no gcc/kind names the third patch",
        refuses(lambda: parsed(without_kind), "third patch"),
    )
    expect("a version other than 2.1.0 is refused", refuses(lambda: parsed({**document, "version": "2.0.0"})))
    expect("a log of two runs is refused", refuses(lambda: parsed({"version": "2.1.0", "runs": [{}, {}]})))
    expect("a single brace in a message is refused", refuses(lambda: sarif.plain_text("a { b", "x"), "placeholder"))
    unknown = json.loads(json.dumps(document))
    unknown["runs"][0]["results"][0]["relatedLocations"][0]["properties"]["gcc/context"] = "other"
    expect("an unknown context is refused", refuses(lambda: parsed(unknown), "not known"))
    stray = json.loads(json.dumps(document))
    stray["runs"][0]["results"][0]["relatedLocations"][0]["properties"]["gcc/contextOf"] = 7
    expect(
        "a context line of a diagnostic that the result does not hold is refused by render()",
        refuses(lambda: sarif.render(parsed(stray)), "names the diagnostic 7"),
    )
    expect(
        "a level of an option result other than error, warning or note is refused",
        refuses(lambda: parsed({"version": "2.1.0", "runs": [{"results": [_planted_log_result("none")]}]})),
    )
    expect("text that is not JSON is refused", refuses(lambda: sarif.parse(b"{", "x"), "not JSON"))


def column_cases(expect: testing.Expect) -> None:
    """Check the column -1 of a text location, which GCC writes with -fno-show-column."""
    document = _planted_log()
    no_column = json.loads(json.dumps(document))
    no_column["runs"][0]["results"][0]["properties"]["gcc/textLocation"]["column"] = -1
    first = parsed(no_column).diagnostics[0]
    expect(
        "the column -1 is no column, and the text prints PATH:LINE",
        first.column == 0 and sarif.render(parsed(no_column)).lines[1].startswith("/w/probe.cpp:4: error:"),
    )
    bad = json.loads(json.dumps(document))
    bad["runs"][0]["results"][0]["properties"]["gcc/textLocation"]["column"] = -2
    expect("a column below -1 is refused", refuses(lambda: parsed(bad)))


def stored_cases(expect: testing.Expect) -> None:
    """Check the stored shape, map_texts() and texts()."""
    log = parsed(_planted_log())
    stored = json.loads(json.dumps(sarif.to_json(log)))
    expect("from_json(to_json(log)) is the log", sarif.from_json(stored) == log)
    expect(
        "from_json refuses a row of the wrong length and a value of the wrong type",
        refuses(lambda: sarif.from_json(["cc1plus", [[None]]]))
        and refuses(lambda: sarif.from_json(["cc1plus", [["error", None, 1, 0, 0, 0, "", "", 0, False, "", []]]]))
        and refuses(lambda: sarif.from_json(["cc1plus", [["error", "macro", "", 0, 0, 0, "", "", 0, False, "", []]]]))
        and refuses(lambda: sarif.from_json("not a log")),
    )
    moved = sarif.map_texts(log, lambda text: text.replace("/w/", "/v/"))
    expect(
        "map_texts() changes each path, message and source line, also of each child",
        moved.diagnostics[0].path == "/v/probe.cpp" and moved.diagnostics[0].children[1].path == "/v/probe.cpp",
    )
    expect("texts() gives three texts for each record", len(sarif.texts(log)) == 3 * len(sarif.records(log)))


def compile_with_log(cxx: str, work: Path, name: str, text: str) -> tuple[list[str], sarif.Log]:
    """Compile one source with the SARIF log and no caret display, and return the text lines and the log."""
    source = work / name
    source.write_text(text, encoding="utf-8")
    log_path = work / f"{name}.sarif"
    argv = [
        cxx,
        "-std=c++26",
        "-fsyntax-only",
        "-Wall",
        "-fdiagnostics-color=never",
        "-fno-diagnostics-show-caret",
        f"{sarif.SARIF_FLAG}{log_path}",
        str(source),
    ]
    done = proc.run(
        proc.Command(argv, timeout_s=proc.QUICK_S, cwd=work, env=proc.environment({"LC_ALL": "C", "LC_MESSAGES": "C"}))
    )
    return (done.out + done.err).splitlines(), sarif.read(log_path)


def compile_cases(expect: testing.Expect) -> None:
    """Compare render() with the text output of the patched compiler for the same compile."""
    cxx = testing.patched_compiler()
    expect("ASSAY_TEST_CXX or CRUCIBLE_GCC16_PREFIX names the patched compiler", bool(cxx) and Path(cxx).is_file())
    if not cxx or not Path(cxx).is_file():
        return
    with tempfile.TemporaryDirectory(prefix="sarif-test-") as scratch:
        work = Path(scratch)
        try:
            text, log = compile_with_log(cxx, work, "probe.cpp", SOURCE)
        except sarif.SarifError as problem:
            expect(f"the reader reads the log of the compiler: {problem}", False)
            return
        rendering = sarif.render(log)
        expect(
            "render() gives each line of the text output of a compile with four kinds of context",
            list(rendering.lines) == text,
        )
        expect(
            "the compile has four error heads, and the warning of an option has its option",
            len(rendering.errors) == 4 and any(record.option == "-Wunused-variable" for record in sarif.records(log)),
        )
        constexpr_head = rendering.errors[1]
        lines = [record.message for record in log.diagnostics[1].contexts(0)]
        expect(
            "the error of the constant evaluation holds its constexpr line as a context line",
            "not a constant expression" in constexpr_head.message
            and any("in 'constexpr' expansion of" in line for line in lines),
        )
        fatal_text, fatal_log = compile_with_log(cxx, work, "fatal.cpp", FATAL_SOURCE)
        expect(
            "render() gives the text output of a fatal error, with its last line",
            list(sarif.render(fatal_log).lines) == fatal_text and fatal_text[-1:] == [sarif.TERMINATED],
        )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    groups = (parse_cases, render_cases, plugin_cases, refusal_cases, column_cases, stored_cases, compile_cases)
    return testing.run(__name__.removeprefix("assay."), argv, groups)
