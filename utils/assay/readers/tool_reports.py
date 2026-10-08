"""The reader of the reports of mypy and ruff: the JSON of `mypy --output json` and of `ruff --output-format json`.

THE REPORTS
    mypy writes one JSON object on each line of its standard output.  The
    object has the keys file, line, column, message, code and severity.
    `ruff check` and `ruff format --check` write one JSON array.  Each object
    of the array has the keys filename, location (row and column), code and
    message.  ruff format gives the code "unformatted" for each part of a file
    that it changes.

THE ISSUES
    mypy_issues() and ruff_issues() give each issue as a ToolIssue: the path
    as the tool gives it, the line, the column, the code and the message.  A
    note of mypy is not an issue, because it adds a hint to the issue before
    it.  An issue with no code has the code "-".  A report that does not have
    the shape of its tool gives ReportError.

The table of the readers of the package names this module as the one reader
of these reports.  `guard selftest tests.readers.test_tool_reports` does a
test of the module.
"""

import json
from dataclasses import dataclass

NO_CODE = "-"
MYPY_NOTE = "note"


class ReportError(ValueError):
    """A report of a tool does not have the shape of the reports of that tool."""


@dataclass(frozen=True, slots=True)
class ToolIssue:
    """One issue of a report: the path as the tool gives it, the line, the column, the code and the message."""

    path: str
    line: int
    column: int
    code: str
    message: str


def mypy_issues(text: str) -> tuple[ToolIssue, ...]:
    """Return each issue of the output of `mypy --output json`, with no note (THE ISSUES).

    Complexity: linear in the length of the text.

    Raises:
        ReportError: If a line that is not empty is not an object of the report of mypy
    """
    found: list[ToolIssue] = []
    for number, line in enumerate(text.splitlines(), start=1):
        if not line.strip():
            continue
        try:
            record = json.loads(line)
            if record["severity"] == MYPY_NOTE:
                continue
            found.append(
                ToolIssue(
                    str(record["file"]),
                    int(record["line"]),
                    int(record["column"]),
                    str(record["code"] or NO_CODE),
                    str(record["message"]),
                )
            )
        except (json.JSONDecodeError, KeyError, TypeError, ValueError) as problem:
            raise ReportError(
                f"the line {number} of the report of mypy is not an object of the report: {problem}"
            ) from None
    return tuple(found)


def ruff_issues(text: str) -> tuple[ToolIssue, ...]:
    """Return each issue of the output of `ruff check` or `ruff format --check` with --output-format json.

    Complexity: linear in the length of the text.

    Raises:
        ReportError: If the text is not an array of the objects of the report of ruff
    """
    try:
        records = json.loads(text) if text.strip() else []
        if not isinstance(records, list):
            raise TypeError(f"the report is a {type(records).__name__}, not an array")
        return tuple(
            ToolIssue(
                str(record["filename"]),
                int(record["location"]["row"]),
                int(record["location"]["column"]),
                str(record["code"] or NO_CODE),
                str(record["message"]),
            )
            for record in records
        )
    except (json.JSONDecodeError, KeyError, TypeError, ValueError) as problem:
        raise ReportError(f"the report of ruff is not an array of the objects of the report: {problem}") from None
