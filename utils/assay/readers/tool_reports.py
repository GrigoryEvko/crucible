"""The reader of the reports of mypy, ruff and clang-format.

THE REPORTS
    mypy writes one JSON object on each line of its standard output.  The
    object has the keys file, line, column, message, code and severity.
    `ruff check` and `ruff format --check` write one JSON array.  Each object
    of the array has the keys filename, location (row and column), code and
    message.  ruff format gives the code "unformatted" for each part of a file
    that it changes.

    `clang-format --output-replacements-xml` writes one XML document for each
    file that it formats, in the sequence of the files.  The document starts
    with the declaration "<?xml" and holds one element replacements, with the
    attribute incomplete_format.  Each child element replacement has the
    attributes offset and length, in bytes, and its text replaces those bytes.
    A file that .clang-format-ignore names gives no document.

THE ISSUES
    mypy_issues() and ruff_issues() give each issue as a ToolIssue: the path
    as the tool gives it, the line, the column, the code and the message.  A
    note of mypy is not an issue, because it adds a hint to the issue before
    it.  An issue with no code has the code "-".  A report that does not have
    the shape of its tool gives ReportError.

    clang_format_documents() gives each document of clang-format as a
    FormatDocument: the flag incomplete_format and each replacement.

The table of the readers of the package names this module as the one reader
of these reports.  `guard selftest tests.readers.test_tool_reports` does a
test of the module.
"""

import json
import xml.etree.ElementTree as element_tree
from dataclasses import dataclass

NO_CODE = "-"
MYPY_NOTE = "note"
XML_DECLARATION = "<?xml"
REPLACEMENTS = "replacements"
REPLACEMENT = "replacement"


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


@dataclass(frozen=True, slots=True)
class Replacement:
    """One replacement of clang-format: the byte offset and the byte length of the text that it replaces, and its text."""

    offset: int
    length: int
    text: str


@dataclass(frozen=True, slots=True)
class FormatDocument:
    """The document of clang-format for one file: True when the format is not complete, and each replacement."""

    is_incomplete: bool
    replacements: tuple[Replacement, ...]


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


def clang_format_documents(text: str) -> tuple[FormatDocument, ...]:
    """Return each document of the output of `clang-format --output-replacements-xml`, in sequence.

    Complexity: linear in the length of the text.

    Raises:
        ReportError: If the text holds a part that is not a document of the shape of THE REPORTS
    """
    found: list[FormatDocument] = []
    parts = text.split(XML_DECLARATION)
    if parts[0].strip():
        raise ReportError("the report of clang-format has text before its first XML declaration")
    for number, part in enumerate(parts[1:], start=1):
        try:
            root = element_tree.fromstring(XML_DECLARATION + part)
            if root.tag != REPLACEMENTS:
                raise ValueError(f"the root element is {root.tag}, not {REPLACEMENTS}")
            incomplete = root.attrib["incomplete_format"]
            if incomplete not in ("true", "false"):
                raise ValueError(f"the attribute incomplete_format is {incomplete!r}")
            replacements = tuple(
                Replacement(int(child.attrib["offset"]), int(child.attrib["length"]), child.text or "")
                for child in root
                if child.tag == REPLACEMENT
            )
            if len(replacements) != len(root):
                raise ValueError(f"the element {REPLACEMENTS} holds a child that is not a {REPLACEMENT}")
        except (element_tree.ParseError, KeyError, ValueError) as problem:
            raise ReportError(
                f"the document {number} of the report of clang-format has no valid shape: {problem}"
            ) from None
        found.append(FormatDocument(incomplete == "true", replacements))
    return tuple(found)
