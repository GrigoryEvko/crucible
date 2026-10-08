"""The test of the reader of the reports of mypy, ruff and clang-format (assay.readers.tool_reports).

`guard selftest tests.readers.test_tool_reports` runs the test.  Each case
writes one line.  Exit 0 when each case holds, else 1.
"""

import json
from collections.abc import Callable

from assay.core import testing
from assay.readers import tool_reports

MYPY_ERROR = {
    "file": "assay/core/rows.py",
    "line": 12,
    "column": 4,
    "end_line": 12,
    "end_column": 9,
    "message": "Missing type parameters",
    "hint": None,
    "code": "type-arg",
    "severity": "error",
}
MYPY_NOTE = {**MYPY_ERROR, "line": 13, "message": "A note", "code": None, "severity": "note"}
RUFF_ISSUE = {
    "cell": None,
    "code": "F401",
    "filename": "/tree/utils/assay/tool.py",
    "location": {"column": 8, "row": 3},
    "end_location": {"column": 10, "row": 3},
    "fix": None,
    "message": "`os` imported but unused",
    "name": "unused-import",
    "noqa_row": 3,
    "severity": "error",
    "url": "",
}


def refuses(action: Callable[[], object]) -> bool:
    """Return True when an action raises ReportError."""
    try:
        action()
    except tool_reports.ReportError:
        return True
    return False


def mypy_cases(expect: testing.Expect) -> None:
    """Check the read of a report of mypy."""
    text = f"{json.dumps(MYPY_ERROR)}\n{json.dumps(MYPY_NOTE)}\n\n"
    expect(
        "mypy_issues() gives each error with its path, line, column, code and message, and no note",
        tool_reports.mypy_issues(text)
        == (tool_reports.ToolIssue("assay/core/rows.py", 12, 4, "type-arg", "Missing type parameters"),),
    )
    expect(
        "an error with no code has the code -",
        tool_reports.mypy_issues(json.dumps({**MYPY_ERROR, "code": None}))[0].code == tool_reports.NO_CODE,
    )
    expect("an empty report has no issue", tool_reports.mypy_issues("") == ())
    expect(
        "a line that is not JSON, and an object with no key line, give ReportError",
        refuses(lambda: tool_reports.mypy_issues("assay/x.py:1: error: text\n"))
        and refuses(lambda: tool_reports.mypy_issues(json.dumps({"file": "x", "severity": "error"}))),
    )


def ruff_cases(expect: testing.Expect) -> None:
    """Check the read of a report of ruff."""
    issues = tool_reports.ruff_issues(
        json.dumps([RUFF_ISSUE, {**RUFF_ISSUE, "code": "unformatted", "message": "File would be reformatted"}])
    )
    expect(
        "ruff_issues() gives each object with its path, its row, its column, its code and its message",
        issues
        == (
            tool_reports.ToolIssue("/tree/utils/assay/tool.py", 3, 8, "F401", "`os` imported but unused"),
            tool_reports.ToolIssue("/tree/utils/assay/tool.py", 3, 8, "unformatted", "File would be reformatted"),
        ),
    )
    expect(
        "an empty array and an empty text have no issue",
        tool_reports.ruff_issues("[]") == () and tool_reports.ruff_issues("") == (),
    )
    expect(
        "a report that is not an array, and an object with no location, give ReportError",
        refuses(lambda: tool_reports.ruff_issues(json.dumps(RUFF_ISSUE)))
        and refuses(lambda: tool_reports.ruff_issues(json.dumps([{"code": "F401", "filename": "x"}])))
        and refuses(lambda: tool_reports.ruff_issues("not JSON")),
    )


def clang_format_cases(expect: testing.Expect) -> None:
    """Check the read of a report of clang-format."""
    drift = (
        "<?xml version='1.0'?>\n<replacements xml:space='preserve' incomplete_format='false'>\n"
        "<replacement offset='42' length='3'>&#10;  </replacement>\n"
        "<replacement offset='60' length='0'> </replacement>\n</replacements>\n"
    )
    clean = "<?xml version='1.0'?>\n<replacements xml:space='preserve' incomplete_format='false'>\n</replacements>\n"
    incomplete = clean.replace("incomplete_format='false'", "incomplete_format='true'")
    expect(
        "clang_format_documents() gives one document for each file, in sequence, with each replacement",
        tool_reports.clang_format_documents(drift + clean)
        == (
            tool_reports.FormatDocument(
                False, (tool_reports.Replacement(42, 3, "\n  "), tool_reports.Replacement(60, 0, " "))
            ),
            tool_reports.FormatDocument(False, ()),
        ),
    )
    expect(
        "an incomplete format is a flag, and an empty report has no document",
        tool_reports.clang_format_documents(incomplete)[0].is_incomplete
        and tool_reports.clang_format_documents("") == (),
    )
    expect(
        "text before the declaration, a document that is not XML and a child that is not a replacement give "
        "ReportError",
        refuses(lambda: tool_reports.clang_format_documents("warning: text\n" + clean))
        and refuses(lambda: tool_reports.clang_format_documents("<?xml version='1.0'?>\n<replacements"))
        and refuses(lambda: tool_reports.clang_format_documents(clean.replace("\n</", "<other/>\n</"))),
    )


def main(argv: list[str]) -> int:
    """Run each case, and write one line for each."""
    return testing.run(__name__.removeprefix("assay."), argv, (mypy_cases, ruff_cases, clang_format_cases))
