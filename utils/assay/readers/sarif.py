"""sarif — read the SARIF log of a GCC compile into records, and give the text lines of the records.

THE FILE
    GCC writes one SARIF 2.1.0 log for each compile with the option
    SARIF_FLAG and a path (`-fdiagnostics-add-output=sarif:file=PATH`).  The
    log holds one run, and the run holds one result for each diagnostic at the
    top level.  The related locations of a result hold these items, in the
    sequence of the text output:

    - Each nested diagnostic of the result, with the properties
      `nestingLevel`, `gcc/kind` and `gcc/option`
    - Each context line of a diagnostic, with the properties `nestingLevel`,
      `gcc/context`, `gcc/contextOf` and `gcc/placement`
    - The locations of the include chain, with no message.

    The property `gcc/textLocation` of a result, of a nested diagnostic and of
    a context line gives the location that the text output prints.  The third
    patch of utils/toolchain/gcc/patches/ gives each property with a name that
    starts with `gcc/`.  In the log of a compiler without that patch, a nested
    diagnostic has no `gcc/kind`, and parse() refuses the log.

THE MESSAGE
    A SARIF message writes each literal "{" and "}" two times, and it writes a
    hyperlink as "[TEXT](URL)" (SARIF 2.1.0, sections 3.11.5 and 3.11.6).  The
    reader gives the plain text: one "{" or "}", and the text of a hyperlink
    with no URL.  GCC writes no placeholder, and it puts no backslash before a
    bracket.

THE TEXT LINES
    render() gives the lines that the text output of GCC prints for the
    records.  The lines hold the nested diagnostics with their bullets, their
    locations and the context lines.  After a diagnostic of the kind
    `fatal error`, they hold the line TERMINATED.  The lines hold no source
    line, no caret line and no include chain.  A negative fixture applies its
    regular expressions to these lines.  The expressions then read only the
    text of the compiler, and not the text of the fixture.

THE DIAGNOSTICS OF THE PLUGIN
    The quarantine plugin of utils/tools/quarantine/ gives each finding as a
    result with the message "quarantine: KIND ENTITY", when the compile
    writes no object and no report.  A note of the mode report has no more
    text.  An error adds "; " and its advice.  An opted-out finding is a note
    in each mode, with the kind opted-out.  Each opt-out region is a note
    "quarantine: region REASON" at the macro that opens it.  A reader then
    gives each opted-out finding the reason of its region.
    is_plugin_result() finds such a result, and plugin_finding() gives its
    kind and its entity.

    The plugin names the file of a finding by the path of `realpath()` (THE
    FILES of utils/tools/quarantine/plugin.h).  GCC names the file of the
    result by the path that the compile opened.  A compile with an include
    directory of links, such as layer-roots/ of a build, then gives the path
    through the link.  A compile can open a file by a relative path.  The text
    output and the path of a record then name the file relative to the
    directory of the compile.  plugin_path() gives the path of `realpath()`
    for the file of a result, with the directory of the compile for a
    relative path.  Then the two names of one file give one file.

    render() with a function `hidden` gives no line of a result that the
    function accepts, and no such result is an error head.  It keeps each
    context line that the text output prints before such a result, such as
    "In function 'f':".  The text output of GCC prints a context line one
    time for the diagnostics after it, and the next diagnostic does not
    print it again.
    A fixture renders its log with is_plugin_result, and the plugin then
    changes no line that its regular expressions read.

`guard selftest tests.readers.test_sarif` does the test of the module.
"""

import functools
import json
import os
import re
from collections.abc import Callable, Mapping, Sequence
from enum import StrEnum
from pathlib import Path

# The option that makes GCC write a SARIF log to the path after it.
SARIF_FLAG = "-fdiagnostics-add-output=sarif:file="
# The version of SARIF that GCC writes by default.
VERSION = "2.1.0"
# A hyperlink of a message: [TEXT](URL), with a URL that has a scheme.
_HYPERLINK = re.compile(r"\[([^\[\]]*)\]\(([A-Za-z][A-Za-z0-9+.-]*:[^()\s]*)\)")
# The bullet and the indent of one nest level in the text output.
_BULLET = "•"
_INDENT = "  "
# The line that GCC prints after a diagnostic of the kind `fatal error`, before it stops.
TERMINATED = "compilation terminated."
# The head of the message of each diagnostic of the quarantine plugin, and the separator of the advice of an error
# (THE DIAGNOSTICS OF THE PLUGIN).
PLUGIN_PREFIX = "quarantine: "
ADVICE_SEPARATOR = "; "
# The paths that plugin_path() keeps with their path of `realpath()` (THE DIAGNOSTICS OF THE PLUGIN).
_REAL_PATHS = 4096


class SarifError(ValueError):
    """A SARIF log does not have the shape that this module reads."""


class Kind(StrEnum):
    """The kind of a diagnostic, as the text output of GCC prints it before the message."""

    ERROR = "error"
    FATAL = "fatal error"
    ICE = "internal compiler error"
    SORRY = "sorry, unimplemented"
    WARNING = "warning"
    ANACHRONISM = "anachronism"
    NOTE = "note"
    DEBUG = "debug"
    PEDWARN = "pedwarn"
    PERMERROR = "permerror"


# The kinds that the text output prints as the head of an error.
ERROR_KINDS = frozenset({Kind.ERROR, Kind.FATAL})
# The SARIF level of a result with an option as its rule, and the kind of that level.
_LEVEL_KINDS = {"error": Kind.ERROR, "warning": Kind.WARNING, "note": Kind.NOTE}


class Context(StrEnum):
    """The kind of a context line: the cause of the line in the text output."""

    FUNCTION = "function"
    INSTANTIATION = "instantiation"
    INSTANTIATION_STEP = "instantiation-step"
    CONSTEXPR = "constexpr"
    CONSTRAINT = "constraint"
    MACRO = "macro"


class Diagnostic:
    """One record of a log: a diagnostic, or a context line of a diagnostic.

    A diagnostic has a kind and no context.  A context line has a context and
    no kind.  `owner` is the diagnostic of a context line in its result: 0
    for the result, and N for the Nth nested diagnostic.  `after` is True for
    a context line that the text output prints after its diagnostic.  `path`
    is empty for a record with no file, and `line` and `column` are 0 when
    the record has no line or no column.  `snippet` is the source line that
    GCC gives for the record, with no line break, or an empty text.
    `children` holds the related records of a result in sequence, and it is
    empty for a related record.

    The classes of the module have __slots__ and are no dataclasses.  The
    fixture driver reads a log on each run, and the import of dataclasses
    costs approximately 6 ms (assay.readers.compdb gives the measure).  No
    function of the module changes a record after its construction.
    """

    __slots__ = (
        "after",
        "children",
        "column",
        "context",
        "kind",
        "line",
        "message",
        "nesting",
        "option",
        "owner",
        "path",
        "snippet",
    )

    def __init__(
        self,
        kind: Kind | None,
        context: Context | None,
        path: str,
        line: int,
        column: int,
        nesting: int,
        message: str,
        option: str,
        owner: int,
        after: bool,
        snippet: str,
        children: tuple[Diagnostic, ...],
    ) -> None:
        """Hold the parts of one record."""
        self.kind = kind
        self.context = context
        self.path = path
        self.line = line
        self.column = column
        self.nesting = nesting
        self.message = message
        self.option = option
        self.owner = owner
        self.after = after
        self.snippet = snippet
        self.children = children

    def parts(self) -> tuple[object, ...]:
        """Return each part of the record, in the sequence of the constructor."""
        return (
            self.kind,
            self.context,
            self.path,
            self.line,
            self.column,
            self.nesting,
            self.message,
            self.option,
            self.owner,
            self.after,
            self.snippet,
            self.children,
        )

    def __eq__(self, other: object) -> bool:
        """Two records are equal when each part is equal."""
        if not isinstance(other, Diagnostic):
            return NotImplemented
        return self.parts() == other.parts()

    def __hash__(self) -> int:
        """Hash each part."""
        return hash(self.parts())

    def __repr__(self) -> str:
        """Return the record for a message."""
        return (
            f"Diagnostic(kind={self.kind!r}, context={self.context!r}, place={self.path}:{self.line}:"
            f"{self.column}, message={self.message!r})"
        )

    @property
    def is_error(self) -> bool:
        """Return True for a diagnostic of the kind `error` or `fatal error`."""
        return self.kind in ERROR_KINDS

    @property
    def is_error_head(self) -> bool:
        """Return True for an error at the nest level 0, which the text output prints as the head of an error.

        A nested error at a deeper level is a part of the explanation of its
        diagnostic.  The text output prints it as a bullet.
        """
        return self.is_error and self.nesting == 0

    def contexts(self, owner: int = 0) -> tuple[Diagnostic, ...]:
        """Return the context lines of the diagnostic `owner` of this result, in sequence.

        The value 0 names the result.  A related record has no children, and
        it gives an empty tuple.
        """
        return tuple(child for child in self.children if child.context is not None and child.owner == owner)


class Log:
    """The records of one SARIF log.  `tool` names the compiler program, which the text gives a record with no file."""

    __slots__ = ("diagnostics", "tool")

    def __init__(self, tool: str, diagnostics: tuple[Diagnostic, ...]) -> None:
        """Hold the tool and the records of the results."""
        self.tool = tool
        self.diagnostics = diagnostics

    def __eq__(self, other: object) -> bool:
        """Two logs are equal when the tool and each record are equal."""
        if not isinstance(other, Log):
            return NotImplemented
        return (self.tool, self.diagnostics) == (other.tool, other.diagnostics)

    def __hash__(self) -> int:
        """Hash the tool and the records."""
        return hash((self.tool, self.diagnostics))

    def __repr__(self) -> str:
        """Return the log for a message."""
        return f"Log(tool={self.tool!r}, results={len(self.diagnostics)})"


class Rendering:
    """The text lines of a log, the error group of each line, the head of each error and the line of each head.

    `groups[i]` is the index in `errors` of the head of the error that line i
    is a part of, or -1 when the log holds no error head.  `heads[k]` is the
    index in `lines` of the line of `errors[k]`.
    """

    __slots__ = ("errors", "groups", "heads", "lines")

    def __init__(
        self, lines: tuple[str, ...], groups: tuple[int, ...], errors: tuple[Diagnostic, ...], heads: tuple[int, ...]
    ) -> None:
        """Hold the lines, the groups, the error heads and the line of each head."""
        self.lines = lines
        self.groups = groups
        self.errors = errors
        self.heads = heads


# ── The parse ──────────────────────────────────────────────────────


def plain_text(text: str, where: str) -> str:
    """Return the plain text of a SARIF message: one "{" or "}" for each pair, and the text of each hyperlink.

    Complexity: linear in the length of the text.

    Raises:
        SarifError: If the text holds one "{" or "}" alone, which is a placeholder that GCC does not write
    """
    out: list[str] = []
    index = 0
    while index < len(text):
        char = text[index]
        if char in "{}":
            if text[index + 1 : index + 2] != char:
                raise SarifError(f"{where}: the message holds a placeholder or a single {char!r}: {text[:120]!r}")
            out.append(char)
            index += 2
            continue
        out.append(char)
        index += 1
    return _HYPERLINK.sub(lambda match: match.group(1), "".join(out))


def _object(value: object, where: str) -> Mapping[str, object]:
    """Return `value` as a JSON object, or raise SarifError."""
    if not isinstance(value, dict):
        raise SarifError(f"{where}: an object is necessary, and the log holds {type(value).__name__}")
    return value


def _list(value: object, where: str) -> Sequence[object]:
    """Return `value` as a JSON array, or raise SarifError.  A missing value is an empty array."""
    if value is None:
        return ()
    if not isinstance(value, list):
        raise SarifError(f"{where}: an array is necessary, and the log holds {type(value).__name__}")
    return value


def _text(value: object, where: str) -> str:
    """Return `value` as a JSON string, or raise SarifError."""
    if not isinstance(value, str):
        raise SarifError(f"{where}: a string is necessary, and the log holds {type(value).__name__}")
    return value


def _count(value: object, where: str) -> int:
    """Return `value` as a JSON integer of 0 or more, or raise SarifError.  A missing value is 0."""
    if value is None:
        return 0
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise SarifError(f"{where}: an integer of 0 or more is necessary, and the log holds {value!r}")
    return value


def _column(value: object, where: str) -> int:
    """Return the column of a gcc/textLocation, or 0 for no column.

    With -fno-show-column, the text output prints no column, and GCC writes
    the column -1.
    """
    return 0 if value == -1 and not isinstance(value, bool) else _count(value, where)


def _message(owner: Mapping[str, object], where: str) -> str:
    """Return the plain text of the message of a result or a location."""
    return plain_text(_text(_object(owner.get("message"), where).get("text"), f"{where}.text"), where)


def _place(location: Mapping[str, object], bases: Mapping[str, str], where: str) -> tuple[str, int, int, str]:
    """Return the path, the line, the column and the source line of a location object.

    A location with no physicalLocation has no path, no line and no column.
    A uriBaseId names an entry of originalUriBaseIds, and the path of that
    entry goes before the uri.
    """
    physical_value = location.get("physicalLocation")
    if physical_value is None:
        return "", 0, 0, ""
    physical = _object(physical_value, f"{where}.physicalLocation")
    artifact = _object(physical.get("artifactLocation"), f"{where}.artifactLocation")
    uri = _text(artifact.get("uri"), f"{where}.uri")
    base_id = artifact.get("uriBaseId")
    if base_id is not None:
        base = bases.get(_text(base_id, f"{where}.uriBaseId"))
        if base is None:
            raise SarifError(f"{where}: the uriBaseId {base_id!r} is not in originalUriBaseIds")
        uri = base + uri
    region_value = physical.get("region")
    region = _object(region_value, f"{where}.region") if region_value is not None else {}
    snippet = ""
    context_value = physical.get("contextRegion")
    if context_value is not None:
        snippet_value = _object(context_value, f"{where}.contextRegion").get("snippet")
        if snippet_value is not None:
            snippet = _text(_object(snippet_value, f"{where}.snippet").get("text"), f"{where}.snippet")
    return (
        uri,
        _count(region.get("startLine"), f"{where}.startLine"),
        _count(region.get("startColumn"), f"{where}.startColumn"),
        snippet.rstrip("\n"),
    )


def _text_place(
    properties: Mapping[str, object], place: tuple[str, int, int, str], where: str
) -> tuple[str, int, int, str]:
    """Return the location that the text output prints for a record: gcc/textLocation, else the start of the region.

    The text output names a diagnostic by the point of its caret, which can
    be a point other than the start of the region.  The source line stays the
    source line of the region.
    """
    value = properties.get("gcc/textLocation")
    if value is None:
        return place
    text_location = _object(value, f"{where}.gcc/textLocation")
    return (
        _text(text_location.get("file"), f"{where}.gcc/textLocation.file"),
        _count(text_location.get("line"), f"{where}.gcc/textLocation.line"),
        _column(text_location.get("column"), f"{where}.gcc/textLocation.column"),
        place[3],
    )


def _related(location: Mapping[str, object], bases: Mapping[str, str], where: str) -> Diagnostic | None:
    """Return the record of one related location, or None for a location of the include chain."""
    if "message" not in location:
        return None
    properties = _object(location.get("properties", {}), f"{where}.properties")
    message = _message(location, where)
    path, line, column, snippet = _text_place(properties, _place(location, bases, where), where)
    nesting = _count(properties.get("nestingLevel"), f"{where}.nestingLevel")
    if "gcc/context" in properties:
        context_text = _text(properties.get("gcc/context"), f"{where}.gcc/context")
        placement = _text(properties.get("gcc/placement"), f"{where}.gcc/placement")
        if context_text not in set(Context) or placement not in ("before", "after"):
            raise SarifError(f"{where}: the context {context_text!r} or the placement {placement!r} is not known")
        owner = _count(properties.get("gcc/contextOf"), f"{where}.gcc/contextOf")
        return Diagnostic(
            None,
            Context(context_text),
            path,
            line,
            column,
            nesting,
            message,
            "",
            owner,
            placement == "after",
            snippet,
            (),
        )
    if "gcc/kind" not in properties:
        raise SarifError(
            f"{where}: a nested diagnostic has no property gcc/kind.  The compiler does not have the third patch "
            f"of utils/toolchain/gcc/patches/, so its SARIF output holds no C++ context.  Build the compiler with "
            f"`python3 utils/bin/setup gcc PREFIX`, and configure with CRUCIBLE_GCC16_PREFIX=PREFIX"
        )
    kind_text = _text(properties.get("gcc/kind"), f"{where}.gcc/kind")
    if kind_text not in set(Kind):
        raise SarifError(f"{where}: the kind {kind_text!r} is not a kind of GCC")
    option = _text(properties.get("gcc/option", ""), f"{where}.gcc/option")
    return Diagnostic(Kind(kind_text), None, path, line, column, nesting, message, option, 0, False, snippet, ())


def _result(result: Mapping[str, object], bases: Mapping[str, str], where: str) -> Diagnostic:
    """Return the record of one result with its related records."""
    rule = _text(result.get("ruleId"), f"{where}.ruleId")
    option = ""
    if rule.startswith("-"):
        level = _text(result.get("level"), f"{where}.level")
        if level not in _LEVEL_KINDS:
            raise SarifError(f"{where}: the level {level!r} of a result of an option is not error, warning or note")
        kind, option = _LEVEL_KINDS[level], rule
    elif rule in set(Kind):
        kind = Kind(rule)
    else:
        raise SarifError(f"{where}: the ruleId {rule!r} is not an option and not a kind of GCC")
    locations = _list(result.get("locations"), f"{where}.locations")
    place = _place(_object(locations[0], f"{where}.locations[0]"), bases, where) if locations else ("", 0, 0, "")
    properties = _object(result.get("properties", {}), f"{where}.properties")
    place = _text_place(properties, place, where)
    nesting = _count(properties.get("nestingLevel"), f"{where}.nestingLevel")
    children = []
    for index, value in enumerate(_list(result.get("relatedLocations"), f"{where}.relatedLocations")):
        record = _related(
            _object(value, f"{where}.relatedLocations[{index}]"), bases, f"{where}.relatedLocations[{index}]"
        )
        if record is not None:
            children.append(record)
    return Diagnostic(
        kind,
        None,
        place[0],
        place[1],
        place[2],
        nesting,
        _message(result, where),
        option,
        0,
        False,
        place[3],
        tuple(children),
    )


def _bases(run: Mapping[str, object], where: str) -> dict[str, str]:
    """Return the path of each entry of originalUriBaseIds of a run, with a slash at its end."""
    bases: dict[str, str] = {}
    for name, value in _object(run.get("originalUriBaseIds", {}), f"{where}.originalUriBaseIds").items():
        uri = _text(_object(value, f"{where}.originalUriBaseIds.{name}").get("uri"), f"{where}.{name}.uri")
        bases[name] = uri.removeprefix("file://")
    return bases


def _tool(run: Mapping[str, object], where: str) -> str:
    """Return the name of the compiler program of a run: the last part of the first argument of its invocation."""
    invocations = _list(run.get("invocations"), f"{where}.invocations")
    if not invocations:
        return "cc1plus"
    arguments = _list(_object(invocations[0], f"{where}.invocations[0]").get("arguments"), f"{where}.arguments")
    return _text(arguments[0], f"{where}.arguments[0]").rsplit("/", 1)[-1] if arguments else "cc1plus"


def parse(data: bytes, where: str) -> Log:
    """Read the records of a SARIF log of GCC.

    Complexity: linear in the bytes of the log.

    Raises:
        SarifError: If the bytes are not a SARIF 2.1.0 log of one run, or a record has a different shape
    """
    try:
        document = json.loads(data.decode("utf-8"))
    except (UnicodeDecodeError, ValueError) as problem:
        raise SarifError(f"{where}: the log is not JSON in UTF-8 ({problem})") from None
    top = _object(document, where)
    if top.get("version") != VERSION:
        raise SarifError(f"{where}: the SARIF version is {top.get('version')!r}, and the reader reads {VERSION}")
    runs = _list(top.get("runs"), f"{where}.runs")
    if len(runs) != 1:
        raise SarifError(f"{where}: the log holds {len(runs)} runs, and a compile of GCC writes one")
    run = _object(runs[0], f"{where}.runs[0]")
    bases = _bases(run, where)
    results = _list(run.get("results"), f"{where}.results")
    parsed = (
        _result(_object(value, f"{where}.results[{index}]"), bases, f"{where}.results[{index}]")
        for index, value in enumerate(results)
    )
    return Log(_tool(run, where), tuple(parsed))


def read(path: Path) -> Log:
    """Read the SARIF log of GCC at `path`.

    Raises:
        SarifError: If the log does not have the shape that the text of the module gives
        OSError: If the file cannot be read
    """
    return parse(path.read_bytes(), str(path))


# ── The stored shape ───────────────────────────────────────────────


def _record_row(record: Diagnostic) -> list[object]:
    """Return a record as a JSON row, with its children."""
    return [
        record.kind.value if record.kind is not None else None,
        record.context.value if record.context is not None else None,
        record.path,
        record.line,
        record.column,
        record.nesting,
        record.message,
        record.option,
        record.owner,
        record.after,
        record.snippet,
        [_record_row(child) for child in record.children],
    ]


def to_json(log: Log) -> list[object]:
    """Return a log as JSON values, which from_json reads back.  A store keeps a log in this shape."""
    return [log.tool, [_record_row(record) for record in log.diagnostics]]


def _row_record(row: object, depth: int) -> Diagnostic:
    """Return the record of one JSON row of to_json, or raise SarifError."""
    if not isinstance(row, list) or len(row) != 12 or depth > 1:
        raise SarifError("a stored record is not a row of 12 values")
    kind, context, path, line, column, nesting, message, option, owner, after, snippet, children = row
    has_texts = all(isinstance(value, str) for value in (path, message, option, snippet))
    has_counts = all(
        isinstance(value, int) and not isinstance(value, bool) and value >= 0
        for value in (line, column, nesting, owner)
    )
    has_one_class = (kind is None) != (context is None)
    has_known_class = (kind is None or kind in set(Kind)) and (context is None or context in set(Context))
    if not (has_texts and has_counts and has_one_class and has_known_class) or not isinstance(after, bool):
        raise SarifError("a stored record has a value of the wrong type")
    if not isinstance(children, list):
        raise SarifError("a stored record has children that are not a list")
    return Diagnostic(
        Kind(kind) if isinstance(kind, str) else None,
        Context(context) if isinstance(context, str) else None,
        str(path),
        int(line),
        int(column),
        int(nesting),
        str(message),
        str(option),
        int(owner),
        after,
        str(snippet),
        tuple(_row_record(child, depth + 1) for child in children),
    )


def from_json(value: object) -> Log:
    """Return the log of a JSON value of to_json.

    Raises:
        SarifError: If the value is not a log in the shape of to_json
    """
    if not isinstance(value, list) or len(value) != 2 or not isinstance(value[0], str):
        raise SarifError("a stored log is not a pair of a tool name and a list of records")
    if not isinstance(value[1], list):
        raise SarifError("the records of a stored log are not a list")
    return Log(value[0], tuple(_row_record(row, 0) for row in value[1]))


def records(log: Log) -> list[Diagnostic]:
    """Return each record of a log in sequence: each result, then its related records."""
    return [item for record in log.diagnostics for item in (record, *record.children)]


def map_texts(log: Log, change: Callable[[str], str]) -> Log:
    """Return the log with `change` applied to each path, message and source line."""

    def changed(record: Diagnostic) -> Diagnostic:
        """Return one record with `change` applied to its texts and to its children."""
        return Diagnostic(
            record.kind,
            record.context,
            change(record.path),
            record.line,
            record.column,
            record.nesting,
            change(record.message),
            record.option,
            record.owner,
            record.after,
            change(record.snippet),
            tuple(changed(child) for child in record.children),
        )

    return Log(log.tool, tuple(changed(record) for record in log.diagnostics))


def texts(log: Log) -> list[str]:
    """Return each path, message and source line of a log, for a test of the bytes that they hold."""
    return [text for record in records(log) for text in (record.path, record.message, record.snippet)]


# ── The diagnostics of the plugin ──────────────────────────────────


def is_plugin_result(record: Diagnostic) -> bool:
    """Return True for a result of the quarantine plugin: a note or an error at the nest level 0 with PLUGIN_PREFIX."""
    return record.kind in (Kind.NOTE, Kind.ERROR) and record.nesting == 0 and record.message.startswith(PLUGIN_PREFIX)


def plugin_path(record: Diagnostic, directory: Path | str) -> str:
    """Return the path of `realpath()` for the file of a result of the plugin, or an empty text with no file.

    GCC names the file of a result by the path that the compile opened, also a path through a link.  A relative path
    is relative to `directory`, the working directory of the compile (THE DIAGNOSTICS OF THE PLUGIN).
    Complexity: one call of `realpath()` for each different path, with a bounded cache.
    """
    return _resolved(os.path.join(directory, record.path)) if record.path else ""


@functools.lru_cache(maxsize=_REAL_PATHS)
def _resolved(path: str) -> str:
    """Return the path of `os.path.realpath()` for a path, as the plugin names the file of a finding."""
    return os.path.realpath(path)


def plugin_finding(record: Diagnostic) -> tuple[str, str]:
    """Return the kind and the entity of a result of the plugin (is_plugin_result()).

    The entity of an error stops before the advice, at the first ADVICE_SEPARATOR.  An entity of the plugin holds no
    such separator: it is a name, a header or the text of a region.
    """
    text = record.message.removeprefix(PLUGIN_PREFIX)
    if record.kind is Kind.ERROR:
        text = text.partition(ADVICE_SEPARATOR)[0]
    kind, _, entity = text.partition(" ")
    return kind, entity


# ── The text lines ─────────────────────────────────────────────────


def _location(record: Diagnostic, tool: str) -> str:
    """Return the location text of a record as GCC prints it: PATH:LINE:COLUMN, or the tool for no file."""
    if not record.path:
        return tool
    if record.line == 0:
        return record.path
    return f"{record.path}:{record.line}:{record.column}" if record.column else f"{record.path}:{record.line}"


def _bullet(nesting: int) -> str:
    """Return the indent and the bullet of a nested line."""
    return f"{_INDENT * nesting}{_BULLET} "


def _context_lines(record: Diagnostic, tool: str) -> list[str]:
    """Return the text lines of one context line, as the text output prints it."""
    nesting = record.nesting
    if record.context is Context.FUNCTION:
        return [f"{record.path}: {record.message}" if record.path else record.message]
    if record.context is Context.INSTANTIATION:
        prefix = f"{record.path}: " if record.path else ""
        return [f"{prefix}{record.message}" if nesting == 0 else f"{_bullet(nesting)}{prefix}{record.message}"]
    if record.context is Context.MACRO:
        if nesting == 0:
            return [f"{_location(record, tool)}: {Kind.NOTE.value}: {record.message}"]
        return [f"{_bullet(nesting)}{record.message}"]
    if nesting == 0:
        return [f"{_location(record, tool)}:   {record.message}"]
    return [f"{_bullet(nesting)}{record.message}", f"{_INDENT * nesting}  {_location(record, tool)}:   "]


def _diagnostic_lines(record: Diagnostic, tool: str, last: str) -> list[str]:
    """Return the text lines of one diagnostic.  `last` is the location of the diagnostic before it, or empty.

    A nested diagnostic prints its location on a line of its own, only when
    the location is not the location of the diagnostic before it.
    """
    kind = record.kind
    if kind is None:
        raise SarifError("a context line is not a diagnostic")
    suffix = f" [{record.option}]" if record.option else ""
    location = _location(record, tool)
    if record.nesting == 0:
        return [f"{location}: {kind.value}: {record.message}{suffix}"]
    head = "" if kind is Kind.NOTE else f"{kind.value}: "
    lines = [f"{_bullet(record.nesting)}{head}{record.message}{suffix}"]
    if record.path and location != last:
        lines.append(f"{_INDENT * record.nesting}  {location}:")
    return lines


class _Unit:
    """One diagnostic, with its context lines before it and after it."""

    __slots__ = ("after", "before", "diagnostic")

    def __init__(self, diagnostic: Diagnostic) -> None:
        """Hold the diagnostic, and start with two empty lists of context lines."""
        self.diagnostic = diagnostic
        self.before: list[Diagnostic] = []
        self.after: list[Diagnostic] = []


def _units(result: Diagnostic) -> list[_Unit]:
    """Return each diagnostic of a result with its context lines, in the sequence of the text output.

    The context lines of a result name their diagnostic (`owner`): 0 for the
    result, and N for the Nth nested diagnostic.
    """
    nested = [child for child in result.children if child.kind is not None]
    units = [_Unit(diagnostic) for diagnostic in (result, *nested)]
    for child in result.children:
        if child.context is None:
            continue
        if child.owner >= len(units):
            raise SarifError(
                f"a context line names the diagnostic {child.owner}, and the result holds {len(units) - 1} nested "
                f"diagnostics"
            )
        unit = units[child.owner]
        (unit.after if child.after else unit.before).append(child)
    return units


def render(log: Log, hidden: Callable[[Diagnostic], bool] | None = None) -> Rendering:
    """Return the lines that the text output of GCC prints for the records of a log, with the error group of each line.

    A diagnostic with its context lines is one unit.  A unit starts a new
    group when its diagnostic is the head of an error
    (Diagnostic.is_error_head).  Each other unit is a part of the group before
    it, and the units before the first head are a part of the first error.
    A result that `hidden` accepts gives only the context lines before it
    (THE DIAGNOSTICS OF THE PLUGIN).  Complexity: linear in the number of
    records.

    Raises:
        SarifError: If a context line names a diagnostic that its result does not hold
    """
    lines: list[str] = []
    groups: list[int] = []
    errors: list[Diagnostic] = []
    heads: list[int] = []
    last = ""
    for result in log.diagnostics:
        if hidden is not None and hidden(result):
            text = [line for record in _units(result)[0].before for line in _context_lines(record, log.tool)]
            lines.extend(text)
            groups.extend([len(errors) - 1] * len(text))
            continue
        for unit in _units(result):
            diagnostic = unit.diagnostic
            text = [line for record in unit.before for line in _context_lines(record, log.tool)]
            if diagnostic.is_error_head:
                errors.append(diagnostic)
                heads.append(len(lines) + len(text))
            text.extend(_diagnostic_lines(diagnostic, log.tool, last))
            text.extend(line for record in unit.after for line in _context_lines(record, log.tool))
            if diagnostic.kind is Kind.FATAL:
                text.append(TERMINATED)
            last = _location(diagnostic, log.tool) if diagnostic.path else ""
            lines.extend(text)
            groups.extend([len(errors) - 1] * len(text))
    first = 0 if errors else -1
    return Rendering(
        tuple(lines), tuple(group if group >= 0 else first for group in groups), tuple(errors), tuple(heads)
    )
