"""The witness of the engine plugin: each plant and each control of a rule, compiled with the quarantine plugin of GCC.

THE PLANTS
    The plants of a rule of the engine plugin are in utils/plants/RULE/
    (assay.guard.engines.meta.runner, THE WITNESS).  A plant is a tree that
    the rule must refuse, and a control is a tree that the rule must accept.
    Each one is complete in its own directory.  Its manifest witness.txt
    gives its compiles and the full result of each compile.  Its directory
    tree/ is the source root of each compile.  Each other directory, such as
    outside/, is not in the root.  The witness copies each entry of the
    directory other than the manifest to a throwaway work directory.  A file
    NAME.planted gets the name NAME there, and no rule of the tree reads the
    C++ of a plant.

THE MANIFEST
    The manifest is a rows file (assay.core.rows).  Each path in it is
    relative to the tree.  A path that starts with {repo}/ is relative to the
    root of the repository of the plants.  The rows are:
      compile NAME UNIT [KEY=VALUE]...       one compile of the file UNIT
      flag NAME WORD...                      more compiler flags of NAME, in sequence
      exit NAME pass|fail                    the status of NAME: 0, or each other status
      finding NAME RULE KIND FILE:LINE:COLUMN | ENTITY
                                             one line of the report of NAME
      error NAME LOCATION | TEXT             one error of NAME at LOCATION, with a message that holds TEXT
      note NAME LOCATION | TEXT              one note of the plugin, with the same shape
      text NAME | TEXT                       a diagnostic or a part of one holds TEXT
      missing NAME report|object             NAME writes no report file, or no object
      table-unit UNIT                        the unit of each compile of the table rows
      table FILE accepted                    the two readers of the rule table read FILE
      table FILE refused LINE | MESSAGE      the two readers refuse FILE at LINE
      white-space accepted|refused           the tables of THE WHITE SPACE
      reads PATH                             assay.core.ruletable reads the table PATH
      procedure WORD                         one procedure of THE PROCEDURES
      judge NAME                             the module of the rule judges the finding lines of NAME
                                             (THE KINDS OF A PLANT)
      configuration NAME                     the judged lines are the lines of a build of the
                                             configuration NAME (THE KINDS OF A PLANT)
      parent DIRECTORY                       the directory DIRECTORY of the plant is the tree at the
                                             parent commit of the change (THE PARENT)
    A LOCATION is FILE:LINE, FILE:* for each line of FILE, or "-" for a
    diagnostic with no file.  The cell "-" of a finding row is an empty
    entity.  Each compile has one exit row.

THE KEYS OF A COMPILE
      root=PATH     the plugin argument root.  The default is ".", the tree
      rules=PATH    the plugin argument rules.  The default is rules.txt
      mode=WORD     the plugin argument mode.  The default is report
      padded-list-limit=N   the plugin argument padded-list-limit.  The
                    default is the error threshold of the row padded-list
                    of utils/registry.txt, as for each compile of the tree
      build=PATH, stamps=PATH, stamp=TEXT, sentinel=PATH, not-standalone=PATH
                    the plugin arguments, with no default.  A sentinel is
                    relative to the root, and each other PATH is a path of
                    the manifest
    The value "-" gives no argument.
      out=no        no argument out.  By default the witness gives out=DIR, a
                    directory of its own, and reads the report file there
      stage=S|c|syntax|E   -S (the default), -c with an object, -fsyntax-only
                    or -E.  Each stage other than E names an object
      plugin=no     no plugin and no define CRUCIBLE_QUARANTINE_ACTIVE
      timeout=SECONDS   the time limit of the compile
      findings=OTHER    the finding rows of the compile OTHER are rows of
                    this compile too, for a second stage of one unit
      errors=OTHER, notes=OTHER    THE DERIVED DIAGNOSTICS
    Each compile gets -std=c++26, the include directories include/ of the
    tree and of the repository, and a SARIF log (assay.readers.sarif).

THE JUDGEMENT
    * The exit row gives the status.
    * A compile with a report has one report file, unless its row
      `missing NAME report` says that it writes none.  The lines of the
      report are the finding rows, each line one row and each row one line.
    * A compile with the stage c writes an object.  The section of the
      object holds the stamp of the compile, or an empty stamp, and the
      lines of the report.  A compile with no report gives its finding rows
      in the section.
    * The errors at the nest level 0 of the log are the error rows and the
      derived errors.  Each error is one row, and each row is one error.
    * The notes at the nest level 0 with a message that starts with
      "quarantine: " are the note rows and the derived notes in the same
      way.  The plugin writes such a note for each finding of a unit with no
      report and no object in the mode report.
    * Each text row names a text that a record of the log holds, at each
      nest level.
    Before the witness compares a text, it writes each path of the work
    directory relative to the tree, the plugin as {plugin} and the
    repository as {repo}.  It also writes an ASCII quotation mark for each
    typographic quotation mark of GCC.

THE PARENT
    A rule that reads the change of a tree, such as quarantine-lines, reads a
    git history.  With a row `parent DIRECTORY`, the witness makes the work
    copy of tree/ a git repository before the compiles.  Its one commit holds
    the files of DIRECTORY, and the files of tree/ are its change, staged with
    `git add` as after `git mv`.  git then reports a moved file as a move.

THE DERIVED DIAGNOSTICS
    In the mode error, each finding that no region opts out is an error at
    its location.  The key errors=OTHER says that the errors of the compile
    are the finding rows of the compile OTHER.  In the mode report, only a
    finding of the rules of EACH_MODE_RULES gives an error.  The message of
    the error of a finding of a rule other than contract-specifier holds
    "quarantine: KIND ENTITY" with a semicolon after the entity.  A finding of the rule
    contract-specifier gives the error of the contract rule.  A finding at
    the line 0, a fact of all the unit, gives an error with no location.
    The key notes=OTHER says that each finding row of OTHER gives a note
    "quarantine: KIND ENTITY" at its location, other than a finding of the
    rules of EACH_MODE_RULES, which give an error in each mode.  A region
    line is no finding and gives no diagnostic.  OTHER has the root ".", and
    its paths are paths of the tree.

THE TABLES
    The quarantine plugin and assay.core.ruletable read the rule table, and
    they must give the same verdict at the same line.  The witness compiles
    the table-unit with each table of a table row.  The plugin refuses a
    refused table when it loads: the compile fails with the error of the
    row at its line, with the error "failed to initialize plugin" and with
    no other error.  ruletable.load() refuses the table at the same line.
    Each reader accepts an accepted table, and the compile has no error.

THE WHITE SPACE
    Python str.isspace() gives the white-space characters.  Each one other
    than the space, the tab and the line break gets three tables.  Each
    reader refuses the character between two words and the character in a
    reason, and each reader accepts the character in a comment.  The
    carriage return is refused in each position, also in a comment, because
    the rows grammar refuses it on each line.  The table with the line ends
    \\r\\n is refused too.  The row white-space refused gives each refused
    table, and the row white-space accepted gives each accepted table.

THE PROCEDURES
    A procedure is a mechanism that rows cannot state, such as a chain of
    compiles through ccache.  Its function in this module holds the results
    that it must give.  A procedure that cannot run on the host, as with no
    ccache or no build directory, writes "skip" and gives no failure.

THE KINDS OF A PLANT
    A plant has a compile with the exit fail, a refused table, a procedure or
    a judge row.  A control has no compile with the exit fail, no error row
    and no refused table.

    A ledger of a family rule judges its findings, and the compile in the
    mode report does not fail.  A judge row names a compile, and the module
    of the rule judges the finding lines of that compile (runner.census_of
    and runner.PluginContext).  The module uses the ledger of the tree of
    the plant, which is empty when the plant holds none.  The judged
    compiles of a plant give a minimum of one error, and the judged compiles
    of a control give none.  A configuration row gives the context the
    configuration NAME of a build (THE CONFIGURATION of assay.core.configuration), with
    NAME as its build kind.  The function judge of the module of the rule
    then judges the lines.  A manifest has at most one configuration row,
    and only with a judge row.

THE STATUS
    witness() returns 0 when each plant and each control holds, and 1 when
    one fails.  It returns 77 when the witness cannot run: no compiler, no
    plugin, or a plugin path with no file.

`guard selftest tests.guard.engines.plugin.test_witness` does a test of the reader and of the functions that compare.
"""

from __future__ import annotations

import functools
import os
import shutil
import signal
import tempfile
import threading
from collections.abc import Callable, Iterable, Sequence
from dataclasses import dataclass, field
from pathlib import Path

from assay.core import git, proc, ruletable
from assay.core import report as check_report
from assay.core import rows as rows_grammar
from assay.core.exit import ExitCode
from assay.core.paths import REPO_ROOT
from assay.guard.engines.meta import runner as meta_runner
from assay.readers import compdb, sarif
from assay.readers import sections as plugin_sections

MANIFEST = "witness.txt"
TREE = "tree"
REPO_MARK = "{repo}"
PLUGIN_MARK = "{plugin}"
PLUGIN_NAME = "crucible_quarantine"
PLUGIN_ARGUMENT = f"-fplugin-arg-{PLUGIN_NAME}-"
NO_VALUE = "-"
# The line of a location FILE:* of a diagnostic row, which holds each line of FILE.
ANY_LINE = "*"
# The plugin arguments of a compile row, and the default of each one that has a default.
PLUGIN_KEYS = ("root", "rules", "mode", "build", "stamps", "stamp", "sentinel", "padded-list-limit", "not-standalone")
PATH_KEYS = frozenset({"root", "rules", "build", "stamps", "not-standalone"})
DEFAULTS = {"root": ".", "rules": "rules.txt", "mode": "report"}
# The plugin argument of the threshold of the rule padded-list, and the row of utils/registry.txt that gives its default.
PADDED_LIMIT_KEY = "padded-list-limit"
PADDED_LIMIT_RULE = "padded-list"
COMPILE_KEYS = frozenset({*PLUGIN_KEYS, "out", "stage", "plugin", "timeout", "findings", "errors", "notes"})
STAGES = ("S", "c", "syntax", "E")
EXITS = ("pass", "fail")
OUTPUTS = ("report", "object")
VERDICTS = ("accepted", "refused")
# The start of the message of each note of the plugin.
QUARANTINE_PREFIX = "quarantine: "
OPTED_OUT = plugin_sections.OPTED_OUT
REGION = plugin_sections.REGION
QUARANTINE_RULE = "quarantine"
CONTRACT_RULE = "contract-specifier"
# The rules that give an error in each mode for each finding: the contract rule and the rules of the family unit fact.
EACH_MODE_RULES = frozenset((CONTRACT_RULE, "contract-semantic", "fast-math-override"))
# The error of the plugin after a refused rule table.
FAILED_PLUGIN = "failed to initialize plugin"
# The error of the rows grammar for a carriage return.  assay.core.rows and rows.h of the plugin give this text.
CARRIAGE_RETURN = "the line holds a carriage return.  Use a line feed only"
# The default time limit of a compile, in seconds.
COMPILE_S = proc.TOOL_S
SKIP = "skip"


# ── The manifest ────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Compile:
    """One compile row of a manifest, with the flag rows of the compile."""

    name: str
    unit: str
    line: int
    arguments: tuple[tuple[str, str], ...]
    stage: str = "S"
    has_plugin: bool = True
    has_out: bool = True
    timeout_s: float = COMPILE_S
    findings_of: str = ""
    errors_of: str = ""
    notes_of: str = ""
    flags: tuple[str, ...] = ()

    def argument(self, key: str) -> str:
        """Return the value of a plugin argument of the compile, or NO_VALUE when the compile gives none."""
        return dict(self.arguments).get(key, NO_VALUE)

    @property
    def has_report(self) -> bool:
        """True when the plugin writes a report file: the compile loads the plugin and gives the argument out."""
        return self.has_plugin and self.has_out

    @property
    def has_object(self) -> bool:
        """True when the compile writes an object with a section."""
        return self.stage == "c"


@dataclass(frozen=True, slots=True)
class Expected:
    """The result that the rows of one compile state: its exit, its finding lines, its diagnostics and its outputs.

    `missing` holds each output that the compile must not write.
    """

    exit: str = ""
    findings: tuple[plugin_sections.Finding, ...] = ()
    errors: tuple[tuple[str, str], ...] = ()
    notes: tuple[tuple[str, str], ...] = ()
    texts: tuple[str, ...] = ()
    missing: frozenset[str] = frozenset()


@dataclass(frozen=True, slots=True)
class TableCase:
    """One table of the two readers: its name, its file relative to the tree, its text, its line and its message.

    The line is 0 for a table that each reader accepts.  The text is "" for a
    table that the plant holds as a file.
    """

    name: str
    path: str
    line: int
    message: str
    text: str = ""


@dataclass(frozen=True, slots=True)
class Manifest:
    """The rows of one manifest."""

    compiles: dict[str, Compile] = field(default_factory=dict)
    expected: dict[str, Expected] = field(default_factory=dict)
    table_unit: str = ""
    tables: tuple[TableCase, ...] = ()
    white_space: tuple[str, ...] = ()
    reads: tuple[str, ...] = ()
    procedures: tuple[str, ...] = ()
    judged: tuple[str, ...] = ()
    configuration: str = ""
    parent: str = ""

    @property
    def is_refused(self) -> bool:
        """True when the manifest states that the rule refuses the plant (THE KINDS OF A PLANT)."""
        return (
            any(item.exit == "fail" for item in self.expected.values())
            or any(case.line > 0 for case in self.tables)
            or "refused" in self.white_space
            or bool(self.procedures)
            or bool(self.judged)
        )

    @property
    def has_errors(self) -> bool:
        """True when a row of the manifest states an error."""
        return (
            any(item.exit == "fail" or item.errors for item in self.expected.values())
            or any(case.line > 0 for case in self.tables)
            or "refused" in self.white_space
        )


def parse_place(text: str, row: rows_grammar.Row, with_column: bool) -> tuple[str, int, int]:
    """Read a location FILE:LINE, or FILE:LINE:COLUMN when WITH_COLUMN.

    Raises:
        rows_grammar.RowsError: If the text does not have the shape
    """
    parts = text.rsplit(":", 2 if with_column else 1)
    numbers = parts[1:]
    if (
        len(numbers) != (2 if with_column else 1)
        or not parts[0]
        or not all(number.isascii() and number.isdigit() for number in numbers)
    ):
        raise row.error(f"the location {text!r} is not {'FILE:LINE:COLUMN' if with_column else 'FILE:LINE'}")
    return parts[0], int(numbers[0]), int(numbers[1]) if with_column else 0


def diagnostic_place(text: str, row: rows_grammar.Row) -> str:
    """Return the location of an error row or a note row: FILE:LINE, FILE:* for each line of FILE, or NO_VALUE.

    Raises:
        rows_grammar.RowsError: If the text is none of them
    """
    if text == NO_VALUE or (text.endswith(f":{ANY_LINE}") and len(text) > 2):
        return text
    file, line, _ = parse_place(text, row, with_column=False)
    return f"{file}:{line}"


def shape(row: rows_grammar.Row, words: int, cells: int, at_least: bool = False) -> None:
    """Refuse a row with a number of words or cells that is not the number of its kind.

    Raises:
        rows_grammar.RowsError: If the row has a different shape
    """
    has_words = len(row.words) >= words if at_least else len(row.words) == words
    if not has_words or len(row.cells) != cells:
        raise row.error(f"a row {row.words[0]} has {'at least ' if at_least else ''}{words} words and {cells} cells")


@functools.cache
def padded_limit() -> str:
    """Return the error threshold of the row padded-list of utils/registry.txt, the default of PADDED_LIMIT_KEY.

    Raises:
        ValueError: If the registry gives the rule no thresholds
    """
    from assay.core import registry as rule_registry

    threshold = rule_registry.load().rules[PADDED_LIMIT_RULE].threshold
    if threshold is None:
        raise ValueError(f"the row {PADDED_LIMIT_RULE} of utils/registry.txt has no error threshold")
    return f"{threshold.error:g}"


def with_limit(arguments: tuple[tuple[str, str], ...]) -> tuple[tuple[str, str], ...]:
    """Return the plugin arguments of a compile of a procedure, with the default of PADDED_LIMIT_KEY when they have none."""
    if any(key == PADDED_LIMIT_KEY for key, _ in arguments):
        return arguments
    return (*arguments, (PADDED_LIMIT_KEY, padded_limit()))


def compile_of(row: rows_grammar.Row) -> Compile:
    """Read one compile row (THE KEYS OF A COMPILE).

    Raises:
        rows_grammar.RowsError: If a key or a value is not permitted
    """
    shape(row, 3, 0, at_least=True)
    values = rows_grammar.fields(row, COMPILE_KEYS, start=3)
    arguments = {
        **DEFAULTS,
        PADDED_LIMIT_KEY: padded_limit(),
        **{key: value for key, value in values.items() if key in PLUGIN_KEYS},
    }
    stage = values.get("stage", "S")
    if stage not in STAGES:
        raise row.error(f"the stage {stage!r} is not one of {', '.join(STAGES)}")
    for key in ("out", "plugin"):
        if values.get(key, "yes") not in ("yes", "no"):
            raise row.error(f"the key {key} is yes or no")
    timeout_s = rows_grammar.decimal(values.get("timeout", f"{COMPILE_S:g}"), row, "time limit")
    if timeout_s <= 0:
        raise row.error("the time limit is more than 0 seconds")
    return Compile(
        row.words[1],
        row.words[2],
        row.line,
        tuple((key, value) for key, value in arguments.items() if value != NO_VALUE),
        stage,
        values.get("plugin", "yes") == "yes",
        values.get("out", "yes") == "yes",
        timeout_s,
        values.get("findings", ""),
        values.get("errors", ""),
        values.get("notes", ""),
    )


def parse_manifest(text: str, path: str) -> Manifest:
    """Read the rows of a manifest (THE MANIFEST).  Complexity: linear in the rows.

    Raises:
        rows_grammar.RowsError: If a row does not obey the grammar or the schema
    """
    compiles: dict[str, Compile] = {}
    flags: dict[str, list[str]] = {}
    exits: dict[str, str] = {}
    findings: dict[str, list[plugin_sections.Finding]] = {}
    errors: dict[str, list[tuple[str, str]]] = {}
    notes: dict[str, list[tuple[str, str]]] = {}
    texts: dict[str, list[str]] = {}
    missing: dict[str, set[str]] = {}
    tables: list[TableCase] = []
    table_unit = ""
    white_space: list[str] = []
    reads: list[str] = []
    procedures: list[str] = []
    judged: list[str] = []
    configuration = ""
    configuration_row: rows_grammar.Row | None = None
    parent = ""
    named: list[rows_grammar.Row] = []
    for row in rows_grammar.parse(text, path):
        kind = row.words[0]
        if kind == "compile":
            item = compile_of(row)
            if item.name in compiles:
                raise row.error(f"the compile {item.name} has a second compile row")
            compiles[item.name] = item
        elif kind == "flag":
            shape(row, 3, 0, at_least=True)
            flags.setdefault(row.words[1], []).extend(row.words[2:])
            named.append(row)
        elif kind == "exit":
            shape(row, 3, 0)
            if row.words[2] not in EXITS or row.words[1] in exits:
                raise row.error("an exit row is 'exit NAME pass|fail', one for each compile")
            exits[row.words[1]] = row.words[2]
            named.append(row)
        elif kind == "finding":
            shape(row, 5, 1)
            file, line, column = parse_place(row.words[4], row, with_column=True)
            findings.setdefault(row.words[1], []).append(
                plugin_sections.Finding(file, line, column, row.words[2], row.words[3], row.cells[0])
            )
            named.append(row)
        elif kind in ("error", "note"):
            shape(row, 3, 1)
            target = errors if kind == "error" else notes
            target.setdefault(row.words[1], []).append((diagnostic_place(row.words[2], row), row.cells[0]))
            named.append(row)
        elif kind == "text":
            shape(row, 2, 1)
            texts.setdefault(row.words[1], []).append(row.cells[0])
            named.append(row)
        elif kind == "missing":
            shape(row, 3, 0)
            if row.words[2] not in OUTPUTS:
                raise row.error(f"a missing row names one of {', '.join(OUTPUTS)}")
            missing.setdefault(row.words[1], set()).add(row.words[2])
            named.append(row)
        elif kind == "table-unit":
            shape(row, 2, 0)
            table_unit = row.words[1]
        elif kind == "table":
            if len(row.words) == 3 and not row.cells and row.words[2] == "accepted":
                tables.append(TableCase(row.words[1], row.words[1], 0, ""))
            elif (
                len(row.words) == 4
                and len(row.cells) == 1
                and row.words[2] == "refused"
                and row.words[3].isascii()
                and row.words[3].isdigit()
                and int(row.words[3]) > 0
            ):
                tables.append(TableCase(row.words[1], row.words[1], int(row.words[3]), row.cells[0]))
            else:
                raise row.error("a table row is 'table FILE accepted' or 'table FILE refused LINE | MESSAGE'")
        elif kind == "white-space":
            shape(row, 2, 0)
            if row.words[1] not in VERDICTS or row.words[1] in white_space:
                raise row.error("a white-space row is 'white-space accepted|refused', one of each")
            white_space.append(row.words[1])
        elif kind == "reads":
            shape(row, 2, 0)
            reads.append(row.words[1])
        elif kind == "procedure":
            shape(row, 2, 0)
            if row.words[1] not in PROCEDURES:
                raise row.error(f"the procedure {row.words[1]!r} is not one of {', '.join(sorted(PROCEDURES))}")
            procedures.append(row.words[1])
        elif kind == "judge":
            shape(row, 2, 0)
            if row.words[1] in judged:
                raise row.error("a judge row is 'judge NAME', one for each compile")
            judged.append(row.words[1])
            named.append(row)
        elif kind == "configuration":
            shape(row, 2, 0)
            if configuration_row is not None:
                raise row.error("a manifest has at most one configuration row")
            configuration, configuration_row = row.words[1], row
        elif kind == "parent":
            shape(row, 2, 0)
            if parent or row.words[1] in ("", TREE):
                raise row.error(f"a manifest has at most one parent row, and its directory is not {TREE}")
            parent = row.words[1]
        else:
            raise row.error(
                f"the row kind {kind!r} is unknown.  The kinds are compile, flag, exit, finding, error, note, text, "
                f"missing, table-unit, table, white-space, reads, procedure, judge, configuration and parent"
            )
    if configuration_row is not None and not judged:
        raise configuration_row.error("a configuration row names the build of the judged compiles, and no judge row")
    for row in named:
        if row.words[1] not in compiles:
            raise row.error(f"the row names the compile {row.words[1]}, and no compile row gives it")
    for item in compiles.values():
        if item.name not in exits:
            raise rows_grammar.RowsError(path, item.line, f"the compile {item.name} has no exit row")
        for key, other_name in (("findings", item.findings_of), ("errors", item.errors_of), ("notes", item.notes_of)):
            other = compiles.get(other_name) if other_name else item
            if (
                other is None
                or (key == "findings" and other.findings_of)
                or (
                    key != "findings"
                    and other_name
                    and (
                        other.argument("root") != "."
                        or any(
                            finding.file.startswith(ruletable.BUILD_PREFIX) for finding in findings.get(other_name, ())
                        )
                    )
                )
            ):
                raise rows_grammar.RowsError(
                    path,
                    item.line,
                    f"{key}={other_name} names no compile with the root '.', findings in the tree and rows of its own",
                )
        own = [*findings.get(item.name, ()), *(findings.get(item.findings_of, ()) if item.findings_of else ())]
        if own and not item.has_report and not item.has_object:
            raise rows_grammar.RowsError(
                path, item.line, f"the compile {item.name} has finding rows, and no report and no object"
            )
    if (tables or white_space) and not table_unit:
        raise rows_grammar.RowsError(path, 0, "the table rows need a row 'table-unit UNIT'")
    full = {
        name: Compile(
            item.name,
            item.unit,
            item.line,
            item.arguments,
            item.stage,
            item.has_plugin,
            item.has_out,
            item.timeout_s,
            item.findings_of,
            item.errors_of,
            item.notes_of,
            tuple(flags.get(name, ())),
        )
        for name, item in compiles.items()
    }
    expected = {
        name: Expected(
            exits[name],
            (*findings.get(name, ()), *(findings.get(item.findings_of, ()) if item.findings_of else ())),
            tuple(errors.get(name, ())),
            tuple(notes.get(name, ())),
            tuple(texts.get(name, ())),
            frozenset(missing.get(name, ())),
        )
        for name, item in full.items()
    }
    return Manifest(
        full,
        expected,
        table_unit,
        tuple(tables),
        tuple(white_space),
        tuple(reads),
        tuple(procedures),
        tuple(judged),
        configuration,
        parent,
    )


def read_manifest(path: Path) -> Manifest:
    """Read the manifest at PATH.

    Raises:
        rows_grammar.RowsError: If a row does not obey the grammar or the schema
        OSError: If the file cannot be read
    """
    return parse_manifest(rows_grammar.decode(path.read_bytes(), str(path)), str(path))


# ── The functions that compare ──────────────────────────────────────


def finding_difference(
    expected: Sequence[plugin_sections.Finding], given: Sequence[plugin_sections.Finding]
) -> tuple[list[plugin_sections.Finding], list[plugin_sections.Finding]]:
    """Return the rows that the given lines do not hold, and the given lines that no row holds.

    Each line and each row count one time, and two equal lines must have two
    rows.  Complexity: O(e + g) with a count of each finding.
    """
    counts: dict[plugin_sections.Finding, int] = {}
    for item in expected:
        counts[item] = counts.get(item, 0) + 1
    unexpected: list[plugin_sections.Finding] = []
    for item in given:
        if counts.get(item, 0) > 0:
            counts[item] -= 1
        else:
            unexpected.append(item)
    missing: list[plugin_sections.Finding] = []
    for item in expected:
        if counts[item] > 0:
            missing.append(item)
            counts[item] -= 1
    return missing, unexpected


def place_of_any_line(place: str) -> bool:
    """True when a location of a row is FILE:*, which holds each line of FILE."""
    return place.endswith(f":{ANY_LINE}")


def holds_place(expected: str, given: str) -> bool:
    """True when the location of a row holds the location of a diagnostic: the same one, or FILE:* and a line of FILE."""
    if place_of_any_line(expected):
        return given.rpartition(":")[0] == expected.removesuffix(f":{ANY_LINE}")
    return expected == given


def diagnostic_difference(
    expected: Sequence[tuple[str, str]], given: Sequence[tuple[str, str]]
) -> tuple[list[tuple[str, str]], list[tuple[str, str]]]:
    """Return the rows that no given diagnostic holds, and the given diagnostics that no row holds.

    A row (LOCATION, TEXT) holds a diagnostic (LOCATION, MESSAGE) when
    MESSAGE holds TEXT.  Each diagnostic holds at most one row.  The rows
    with a line take their diagnostics first, and then the longest texts.
    Then a short text does not take the diagnostic of a long text.
    Complexity: O(e * g).
    """
    free = list(range(len(given)))
    missing: list[tuple[str, str]] = []
    for place, text in sorted(expected, key=lambda item: (place_of_any_line(item[0]), -len(item[1]))):
        found = next((index for index in free if holds_place(place, given[index][0]) and text in given[index][1]), None)
        if found is None:
            missing.append((place, text))
        else:
            free.remove(found)
    return missing, [given[index] for index in free]


def derived_errors(findings: Iterable[plugin_sections.Finding], is_error_mode: bool) -> list[tuple[str, str]]:
    """Return the error of each finding that no region opts out and that the mode makes an error.

    In the mode report, only a finding of the rules of EACH_MODE_RULES gives an error (THE DERIVED DIAGNOSTICS).
    """
    errors: list[tuple[str, str]] = []
    for finding in findings:
        if finding.kind in (OPTED_OUT, REGION) or not (is_error_mode or finding.rule in EACH_MODE_RULES):
            continue
        place = f"{finding.file}:{finding.line}" if finding.line > 0 else NO_VALUE
        if finding.rule == CONTRACT_RULE:
            errors.append((place, f"the '{finding.entity}' contract specifier is not permitted in this tree"))
        else:
            errors.append((place, f"{QUARANTINE_PREFIX}{finding.kind} {finding.entity};"))
    return errors


def derived_notes(findings: Iterable[plugin_sections.Finding]) -> list[tuple[str, str]]:
    """Return the note of each finding that the mode report makes a note, also of each opted-out one.

    The rules of EACH_MODE_RULES give an error in each mode and no note (THE DERIVED DIAGNOSTICS).
    """
    return [
        (f"{finding.file}:{finding.line}", f"{QUARANTINE_PREFIX}{finding.kind} {finding.entity}")
        for finding in findings
        if finding.kind != REGION and finding.rule not in EACH_MODE_RULES
    ]


def normalized(text: str, work: Path, plugin: Path, repo: Path) -> str:
    """Return a text of GCC with the paths of the work directory, the plugin and the repository as marks.

    A path of the tree becomes relative to the tree.  A path of the work
    directory that is not in the tree starts with ../.  Each typographic
    quotation mark of GCC becomes an ASCII quotation mark.  Complexity:
    linear in the text.
    """
    tree = work / TREE
    text = text.replace("‘", "'").replace("’", "'")
    for path, mark in (
        (f"{tree}/", ""),
        (str(tree), "."),
        (f"{work}/", "../"),
        (str(plugin), PLUGIN_MARK),
        (f"{repo}/", f"{REPO_MARK}/"),
    ):
        text = text.replace(path, mark)
    return text


def white_space_cases(verdict: str) -> list[TableCase]:
    """Return the tables of THE WHITE SPACE with the verdict VERDICT.  Complexity: linear in the code points."""
    cases: list[TableCase] = []
    for code in range(0x110000):
        character = chr(code)
        if not character.isspace() or character in " \t\n":
            continue
        message = CARRIAGE_RETURN if character == "\r" else ruletable.white_space_message(character)
        tables = (
            (f"U+{code:04X} between two words", 2, message, f"quarantine plant_std.cpp\nquarantine{character}a/\n"),
            (
                f"U+{code:04X} in a reason",
                1,
                message,
                f"admit std::move | a{character}cast\nquarantine plant_std.cpp\n",
            ),
            (
                f"U+{code:04X} in a comment",
                1 if character == "\r" else 0,
                message if character == "\r" else "",
                f"# a comment{character}with a white-space character\nquarantine plant_std.cpp\n",
            ),
        )
        cases += [
            TableCase(name, f"white-space/{code:04X}-{index}.txt", line, message_text, text)
            for index, (name, line, message_text, text) in enumerate(tables)
            if (line > 0) == (verdict == "refused")
        ]
    if verdict == "refused":
        cases.append(
            TableCase(
                "the line ends \\r\\n",
                "white-space/crlf.txt",
                1,
                CARRIAGE_RETURN,
                "quarantine plant_std.cpp\r\n# a comment\r\n",
            )
        )
    return cases


# ── The compiles ────────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Log:
    """The SARIF log of one compile: the error heads and the notes of the plugin with their locations, and each text."""

    errors: tuple[tuple[str, str], ...] = ()
    notes: tuple[tuple[str, str], ...] = ()
    texts: tuple[str, ...] = ()
    problem: str = ""


@dataclass(frozen=True, slots=True)
class Outcome:
    """The result of one compile: its exit, its diagnostics, its report, its section and its outputs."""

    exit: int
    errors: tuple[tuple[str, str], ...] = ()
    notes: tuple[tuple[str, str], ...] = ()
    texts: tuple[str, ...] = ()
    log_problem: str = ""
    reports: tuple[Path, ...] = ()
    report: tuple[plugin_sections.Finding, ...] | None = None
    report_problem: str = ""
    section: plugin_sections.Section | None = None
    section_problem: str = ""
    has_object: bool = False
    stderr: str = ""


@dataclass(frozen=True, slots=True)
class Cache:
    """A cache of ccache that one procedure owns: its directory and its empty configuration file."""

    directory: Path
    configuration: Path


def cache_of(work: Path, name: str) -> Cache:
    """Return the cache NAME in WORK, and write its empty configuration file."""
    configuration = work / f"{name}.conf"
    configuration.write_text("", encoding="utf-8")
    return Cache(work / name, configuration)


def _cache_environment(cache: Cache | None) -> dict[str, str] | None:
    """Return the environment of a process that uses CACHE, or None for the environment of this process."""
    if cache is None:
        return None
    return proc.environment({"CCACHE_DIR": str(cache.directory), "CCACHE_CONFIGPATH": str(cache.configuration)})


def read_report(reports: Sequence[Path]) -> tuple[tuple[plugin_sections.Finding, ...] | None, str]:
    """Return the finding lines of the one report file of a compile, and the problem when the file cannot be read."""
    if len(reports) != 1:
        return None, ""
    try:
        return tuple(
            plugin_sections.parse_line(line, str(reports[0]))
            for line in reports[0].read_text(encoding="utf-8").splitlines()
            if line and not line.startswith("#")
        ), ""
    except (OSError, UnicodeDecodeError, plugin_sections.SectionError) as problem:
        return None, f"the report cannot be read: {problem}"


def read_object(output: Path) -> tuple[plugin_sections.Section | None, str]:
    """Return the section of an object, and the problem when the object holds none or cannot be read."""
    try:
        section = plugin_sections.read(output)
    except (OSError, plugin_sections.SectionError) as problem:
        return None, str(problem)
    return (None, "the object holds no section") if section is None else (section, "")


class Compiler:
    """The compiler, the plugin and the repository of the witness, and the gate of its compiler processes.

    At most JOBS compiler processes run at one time.  run() is safe to call
    from many threads, because each compiler is its own process.
    """

    def __init__(self, cxx: str, plugin: Path, repo: Path, build_dir: Path | None, jobs: int) -> None:
        """Keep the compiler, the plugin, the repository, the build directory and the gate of JOBS processes."""
        self.cxx = cxx
        self.plugin = plugin
        self.repo = repo
        self.build_dir = build_dir
        self.slots = threading.BoundedSemaphore(jobs)

    def run(
        self, argv: list[str], cwd: Path, timeout_s: float = COMPILE_S, cache: Cache | None = None
    ) -> proc.Completed:
        """Run one compiler command when a slot is free.  A command that runs longer than its limit gives the exit -9.

        A command with CACHE runs with the environment of that cache of ccache.

        Raises:
            proc.CannotStart: If the compiler cannot start
        """
        with self.slots:
            try:
                return proc.run(proc.Command(argv, timeout_s=timeout_s, cwd=cwd, env=_cache_environment(cache)))
            except proc.TimedOut as problem:
                return proc.Completed(problem.command, signal.SIGKILL, b"", b"timeout", 0.0, 0.0, 0.0, 0)

    def resolved(self, value: str, work: Path) -> str:
        """Return a path of a manifest as an absolute path: under the repository for {repo}/, else under the tree."""
        if value.startswith(f"{REPO_MARK}/"):
            return str(self.repo / value[len(REPO_MARK) + 1 :])
        return os.path.normpath(work / TREE / value)

    def command(self, item: Compile, work: Path, launcher: Sequence[str] = ()) -> list[str]:
        """Return the words of one compile (THE KEYS OF A COMPILE), with LAUNCHER before the compiler."""
        argv = [*launcher, self.cxx, "-std=c++26", "-I", str(work / TREE / "include"), "-I", str(self.repo / "include")]
        if item.has_plugin:
            argv += ["-DCRUCIBLE_QUARANTINE_ACTIVE", f"-fplugin={self.plugin}"]
            argv += [
                f"{PLUGIN_ARGUMENT}{key}={self.resolved(value, work) if key in PATH_KEYS else value}"
                for key, value in item.arguments
            ]
            if item.has_out:
                argv.append(f"{PLUGIN_ARGUMENT}out={work / 'reports' / item.name}")
        argv += [*item.flags, "-fdiagnostics-color=never", f"{sarif.SARIF_FLAG}{work / 'logs' / item.name}.sarif"]
        output = str(work / "objects" / f"{item.name}.o")
        stages = {
            "S": ["-S", "-o", os.devnull],
            "c": ["-c", "-o", output],
            "syntax": ["-fsyntax-only", "-o", output],
            "E": ["-E", "-o", os.devnull],
        }
        return [*argv, *stages[item.stage], self.resolved(item.unit, work)]

    def compile(self, item: Compile, work: Path, launcher: Sequence[str] = (), cache: Cache | None = None) -> Outcome:
        """Run one compile of a manifest in the tree of WORK, and read its log, its report and its object."""
        for directory in ("reports", "logs", "objects"):
            (work / directory).mkdir(exist_ok=True)
        completed = self.run(self.command(item, work, launcher), work / TREE, item.timeout_s, cache)
        log = self.read_log(work / "logs" / f"{item.name}.sarif", work)
        report_directory = work / "reports" / item.name
        reports = tuple(sorted(report_directory.glob("*.quarantine"))) if report_directory.is_dir() else ()
        report, report_problem = read_report(reports)
        output = work / "objects" / f"{item.name}.o"
        has_object = output.is_file()
        section, section_problem = read_object(output) if has_object and item.has_object else (None, "")
        return Outcome(
            completed.exit,
            errors=log.errors,
            notes=log.notes,
            texts=log.texts,
            log_problem=log.problem,
            reports=reports,
            report=report,
            report_problem=report_problem,
            section=section,
            section_problem=section_problem,
            has_object=has_object,
            stderr=completed.err,
        )

    def read_log(self, path: Path, work: Path) -> Log:
        """Return the error heads, the notes of the plugin and each text of the SARIF log at PATH."""
        try:
            log = sarif.read(path)
        except (OSError, sarif.SarifError) as problem:
            return Log(problem=f"the SARIF log cannot be read: {problem}")

        def place(record: sarif.Diagnostic) -> str:
            """Return the location of a record relative to the tree, or NO_VALUE for a record with no file."""
            return f"{normalized(record.path, work, self.plugin, self.repo)}:{record.line}" if record.path else NO_VALUE

        errors: list[tuple[str, str]] = []
        notes: list[tuple[str, str]] = []
        texts: list[str] = []
        for record in log.diagnostics:
            message = normalized(record.message, work, self.plugin, self.repo)
            if record.is_error_head:
                errors.append((place(record), message))
            elif record.kind == sarif.Kind.NOTE and record.nesting == 0 and message.startswith(QUARANTINE_PREFIX):
                notes.append((place(record), message))
            texts += [
                message,
                *(normalized(child.message, work, self.plugin, self.repo) for child in record.children),
            ]
        return Log(tuple(errors), tuple(notes), tuple(texts))


def judge(item: Compile, expected: Expected, outcome: Outcome, manifest: Manifest) -> list[str]:
    """Return each difference between the result of one compile and its rows (THE JUDGEMENT)."""
    failures: list[str] = []
    name = item.name
    if (outcome.exit == 0) != (expected.exit == "pass"):
        failures.append(f"{name}: the exit is {outcome.exit}, and the row says {expected.exit}")
    if outcome.log_problem:
        failures.append(f"{name}: {outcome.log_problem}")
    given = list(outcome.report) if outcome.report is not None else None
    if item.has_report and "report" in expected.missing:
        if outcome.reports:
            failures.append(f"{name}: the compile wrote a report, and the row says that it writes none")
    elif item.has_report and given is None:
        failures.append(
            f"{name}: {outcome.report_problem or f'the compile wrote {len(outcome.reports)} report files'}"
            f", and the row says that it writes one report"
        )
    if item.has_object and "object" not in expected.missing:
        if outcome.section is None:
            failures.append(f"{name}: no section: {outcome.section_problem or 'the compile wrote no object'}")
        else:
            stamp = item.argument("stamp")
            if outcome.section.stamp != ("" if stamp == NO_VALUE else stamp):
                failures.append(f"{name}: the section has the stamp {outcome.section.stamp!r}, not {stamp!r}")
            if given is not None and list(outcome.section.findings) != given:
                failures.append(f"{name}: the section and the report hold different lines")
            if given is None:
                given = list(outcome.section.findings)
    if "object" in expected.missing and outcome.has_object:
        failures.append(f"{name}: the compile wrote an object, and the row says that it writes none")
    if given is not None:
        lost, extra = finding_difference(expected.findings, given)
        failures += [f"{name}: no finding line {finding.text()}" for finding in lost]
        failures += [f"{name}: a finding line with no row: {finding.text()}" for finding in extra]
    rows = list(expected.errors)
    if item.errors_of:
        rows += derived_errors(manifest.expected[item.errors_of].findings, item.argument("mode") == "error")
    note_rows = list(expected.notes)
    if item.notes_of:
        note_rows += derived_notes(manifest.expected[item.notes_of].findings)
    for kind, rows_of_kind, actual in (("error", rows, outcome.errors), ("note", note_rows, outcome.notes)):
        missing_rows, unexpected_rows = diagnostic_difference(rows_of_kind, actual)
        failures += [f"{name}: no {kind} at {place} that holds {text!r}" for place, text in missing_rows]
        failures += [f"{name}: an {kind} with no row at {place}: {text!r}" for place, text in unexpected_rows]
    failures += [
        f"{name}: no diagnostic holds {text!r}"
        for text in expected.texts
        if not any(text in message for message in outcome.texts)
    ]
    return failures


# ── The tables ──────────────────────────────────────────────────────


def table_failures(compiler: Compiler, work: Path, unit: str, case: TableCase) -> list[str]:
    """Give one table to the plugin and to assay.core.ruletable, and return each difference from its verdict."""
    path = work / TREE / case.path
    if case.text:
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(case.text.encode("utf-8"))
    item = Compile(
        f"table-{case.path.replace('/', '-')}",
        unit,
        0,
        with_limit((("root", "."), ("rules", case.path), ("mode", "report"))),
        has_out=False,
    )
    outcome = compiler.compile(item, work)
    failures: list[str] = []
    try:
        ruletable.load(path)
        python = "accepted"
    except ruletable.TableError as problem:
        python = str(problem)
    except (OSError, ValueError, LookupError, TypeError) as problem:
        # The reader refuses a table only with TableError.  Each other exception is a defect of the reader.
        python = f"{type(problem).__name__}: {problem}"
    if case.line == 0:
        if outcome.exit != 0 or outcome.errors:
            failures.append(f"the plugin refuses {case.name}: {outcome.errors or outcome.stderr[-400:]}")
        if python != "accepted":
            failures.append(f"ruletable.py refuses {case.name}: {python}")
        return failures
    expected = [
        (NO_VALUE, f"{QUARANTINE_PREFIX}{case.path}:{case.line}: {case.message}"),
        (NO_VALUE, f"{FAILED_PLUGIN} {PLUGIN_MARK}"),
    ]
    exact = [(place, message) for place, message in outcome.errors]
    if outcome.exit == 0 or sorted(exact) != sorted(expected):
        failures.append(
            f"the plugin does not refuse {case.name} at line {case.line} with the error of the row and "
            f"no other error: {exact}"
        )
    if not python.startswith(f"{path.name}:{case.line}: "):
        failures.append(f"ruletable.py does not refuse {case.name} at line {case.line}: {python}")
    return failures


# ── The procedures ──────────────────────────────────────────────────


@dataclass(frozen=True, slots=True)
class Plant:
    """The context of one procedure: the compiler, the plant directory and its work directory."""

    compiler: Compiler
    directory: Path
    work: Path

    @property
    def tree(self) -> Path:
        """The source root of the plant."""
        return self.work / TREE


type Cases = list[tuple[str, bool, str]]

# The stamps of the section procedures, and the admit row of the changed table.
SECTION_STAMP = "5ec7105ec7105ec7"
SECTION_NEW_STAMP = "0afe0afe0afe0afe"
SECTION_BLIND_STAMP = "b1b1b1b1b1b1b1b1"
SECTION_ADMIT_ROW = "admit std::swap | a planted row of the cache cases\n"
CACHE_UNIT = "cache_unit.cpp"
# The plugin arguments that the launcher of utils/tools/quarantine/Quarantine.cmake makes ccache ignore.  The path
# of the SARIF log of the witness names the step, and ccache ignores it too.
CCACHE_LOG = f"{sarif.SARIF_FLAG}*"
CCACHE_IGNORED = f"-fplugin=* {PLUGIN_ARGUMENT}root=* {PLUGIN_ARGUMENT}build=* {PLUGIN_ARGUMENT}rules=* {CCACHE_LOG}"


def ccache_hits(cache: Cache) -> int:
    """Return the hits that the statistics of CACHE count.

    Raises:
        proc.CannotStart: If ccache cannot start
    """
    printed = proc.run(
        proc.Command(["ccache", "--print-stats"], timeout_s=proc.QUICK_S, env=_cache_environment(cache))
    ).out
    return ccache_hit_count(printed)


def ccache_hit_count(printed: str) -> int:
    """Return the sum of the counts direct_cache_hit and preprocessed_cache_hit of the text of `ccache --print-stats`."""
    counts = dict(line.split("\t", 1) for line in printed.splitlines() if "\t" in line)
    return sum(int(counts.get(name, "0") or "0") for name in ("direct_cache_hit", "preprocessed_cache_hit"))


def is_swap(finding: plugin_sections.Finding) -> bool:
    """True when a finding is the use of std::swap in the unit of the cache procedures."""
    return (finding.kind, finding.file, finding.entity) == ("std-entity", CACHE_UNIT, "std::swap")


def cache_chain(
    plant: Plant, name: str, steps: Sequence[tuple[str, str, str]], launcher: Sequence[str]
) -> list[tuple[int, int, plugin_sections.Section | None]]:
    """Compile the cache unit through ccache, one step after the other, in a cache of its own.

    Each step is (OUTPUT, TABLE, STAMP).  Return the new hits, the exit and
    the section of each step.
    """
    cache = cache_of(plant.work, name)
    results: list[tuple[int, int, plugin_sections.Section | None]] = []
    for output, table, stamp in steps:
        before = ccache_hits(cache)
        item = Compile(
            output,
            CACHE_UNIT,
            0,
            with_limit((("root", "."), ("rules", table), ("mode", "report"), ("stamp", stamp))),
            stage="c",
            has_out=False,
        )
        outcome = plant.compiler.compile(item, plant.work, launcher, cache)
        results.append((ccache_hits(cache) - before, outcome.exit, outcome.section))
    return results


def swap_table(plant: Plant) -> str:
    """Write the table of the tree with the admit row of std::swap, and return its path relative to the tree."""
    (plant.tree / "admit-swap.txt").write_text(
        (plant.tree / "rules.txt").read_text(encoding="utf-8") + SECTION_ADMIT_ROW, encoding="utf-8"
    )
    return "admit-swap.txt"


def plain_section(plant: Plant, name: str, table: str, stamp: str) -> plugin_sections.Section | None:
    """Compile the cache unit with no cache, and return its section."""
    item = Compile(
        name,
        CACHE_UNIT,
        0,
        with_limit((("root", "."), ("rules", table), ("mode", "report"), ("stamp", stamp))),
        stage="c",
        has_out=False,
    )
    return plant.compiler.compile(item, plant.work).section


def section_cache_keyed(plant: Plant) -> Cases:
    """A hit gives the section of a compile with no cache, and a changed table with a new stamp misses."""
    launcher = ("ccache", f"ignore_options={CCACHE_IGNORED}")
    changed = swap_table(plant)
    found = plain_section(plant, "plain", "rules.txt", SECTION_STAMP)
    swapped = plain_section(plant, "plain-swap", changed, SECTION_NEW_STAMP)
    (stored_hits, stored_exit, stored), (hit_hits, hit_exit, hit), (new_hits, new_exit, new) = cache_chain(
        plant,
        "ccache",
        [
            ("stored", "rules.txt", SECTION_STAMP),
            ("hit", "rules.txt", SECTION_STAMP),
            ("new", changed, SECTION_NEW_STAMP),
        ],
        launcher,
    )
    has_swap = [
        section is not None and any(is_swap(finding) for finding in section.findings) for section in (found, swapped)
    ]
    return [
        (
            "a hit of the cache gives the section of a compile with no cache",
            found is not None
            and stored_hits == 0
            and hit_hits == 1
            and stored_exit == hit_exit == 0
            and stored == hit == found,
            f"hits {stored_hits} {hit_hits}",
        ),
        (
            "a changed rule table with a new stamp misses, and gives the findings of a compile with no cache",
            has_swap == [True, False] and new_hits == 0 and new_exit == 0 and new == swapped,
            f"swap {has_swap}, hits {new_hits}",
        ),
    ]


def section_cache_blind(plant: Plant) -> Cases:
    """The negative controls: only the stamp puts the table into the key of the cache."""
    launcher = ("ccache", f"ignore_options={CCACHE_IGNORED}")
    blind = ("ccache", f"ignore_options={CCACHE_IGNORED} {PLUGIN_ARGUMENT}stamp=*")
    changed = swap_table(plant)
    found = plain_section(plant, "plain", "rules.txt", SECTION_STAMP)
    (_, _, _), (stale_hits, stale_exit, stale) = cache_chain(
        plant, "ccache", [("stored", "rules.txt", SECTION_STAMP), ("stale", changed, SECTION_STAMP)], launcher
    )
    (_, _, _), (blind_hits, blind_exit, blind_section) = cache_chain(
        plant,
        "ccache-blind",
        [("blind-stored", "rules.txt", SECTION_STAMP), ("blind", "rules.txt", SECTION_BLIND_STAMP)],
        blind,
    )
    return [
        (
            "a changed table with the old stamp hits, and gives the old findings",
            found is not None and stale_hits == 1 and stale_exit == 0 and stale == found,
            f"hits {stale_hits}",
        ),
        (
            "a key without the stamp gives a section with the stamp of another command",
            found is not None
            and blind_hits == 1
            and blind_exit == 0
            and blind_section == found
            and blind_section.stamp != SECTION_BLIND_STAMP,
            f"hits {blind_hits}",
        ),
    ]


def enforce_chain(plant: Plant, name: str, is_keyed: bool) -> list[tuple[int, Outcome]]:
    """Change the mode of app/unit.cpp between three compiles through ccache: report, error and report."""
    from assay.loop import stamps as plugin_stamps

    ignored = " ".join(f"{PLUGIN_ARGUMENT}{key}=*" for key in ("root", "build", "rules", "stamps"))
    rules, stamps = plant.tree / "rules.txt", plant.tree / "stamps"
    cache = cache_of(plant.work, name)
    launcher = (
        "ccache",
        f"ignore_options=-fplugin=* {ignored} {CCACHE_LOG}",
        *((f"extra_files_to_hash={stamps / 'enforce.txt'}",) if is_keyed else ()),
    )
    results: list[tuple[int, Outcome]] = []
    for index, mode in enumerate(("report", "error", "report")):
        rules.write_text(f"quarantine app/\nenforce app/unit.cpp {mode}\n", encoding="utf-8")
        plugin_stamps.write_stamps(plant.tree, rules, stamps, None)
        before = ccache_hits(cache)
        item = Compile(
            f"{name}-{index}",
            "app/unit.cpp",
            0,
            with_limit(
                (
                    ("root", "."),
                    ("rules", "rules.txt"),
                    ("stamps", "stamps"),
                    ("mode", "report"),
                    ("stamp", "e4f0e4f0e4f0e4f0"),
                )
            ),
            stage="c",
            has_out=False,
        )
        outcome = plant.compiler.compile(item, plant.work, launcher, cache)
        results.append((ccache_hits(cache) - before, outcome))
    return results


def enforce_cache_keyed(plant: Plant) -> Cases:
    """Through ccache, a unit misses and fails after its file changes to the mode error."""
    (_, stored), (error_hits, failed), (_, back) = enforce_chain(plant, "enforce-keyed", is_keyed=True)
    return [
        (
            "through ccache, a unit misses and fails after its file changes to the mode error",
            stored.exit == 0
            and error_hits == 0
            and failed.exit != 0
            and back.exit == 0
            and any(message.startswith(f"{QUARANTINE_PREFIX}raw-pointer-object") for _, message in failed.errors),
            f"hits {error_hits}, errors {failed.errors}",
        )
    ]


def enforce_cache_blind(plant: Plant) -> Cases:
    """The negative control: a cache that does not hash enforce.txt hits, and skips the error."""
    _, (blind_hits, skipped), _ = enforce_chain(plant, "enforce-blind", is_keyed=False)
    return [
        (
            "a cache that does not hash enforce.txt hits, and skips the error",
            blind_hits == 1 and skipped.exit == 0,
            f"hits {blind_hits}, exit {skipped.exit}",
        )
    ]


def dependency_text(plant: Plant, unit: str, stage: str) -> tuple[Outcome, str]:
    """Write the mode stamps of the tree, add the files of fresh/, and compile UNIT with a dependency file.

    Return the outcome and the text of the dependency file.
    """
    from assay.loop import stamps as plugin_stamps

    stamps = plant.tree / "stamps"
    if not stamps.is_dir():
        plugin_stamps.write_stamps(plant.tree, plant.tree / "rules.txt", stamps, None)
        fresh = plant.work / "fresh"
        if fresh.is_dir():
            shutil.copytree(fresh, plant.tree, dirs_exist_ok=True)
    depfile = plant.work / f"dependency-{Path(unit).stem}-{stage}.d"
    item = Compile(
        f"dependency-{Path(unit).stem}-{stage}",
        unit,
        0,
        with_limit((("root", "."), ("rules", "rules.txt"), ("stamps", "stamps"), ("mode", "report"))),
        stage=stage,
        has_out=False,
        flags=("-MD", "-MF", str(depfile)),
    )
    outcome = plant.compiler.compile(item, plant.work)
    return outcome, depfile.read_text(encoding="utf-8") if depfile.is_file() else ""


def dependencies_named(plant: Plant) -> Cases:
    """A unit names the mode stamp of each file with a finding, and enforce.txt for a file with no mode stamp."""
    modes, stamps = plant.tree / "stamps" / "modes" / "app", plant.tree / "stamps"
    found, text = dependency_text(plant, "app/finding.cpp", "c")
    return [
        (
            "a unit names the mode stamp of each file with a finding, and enforce.txt for a file with none",
            found.exit == 0
            and str(modes / "finding.cpp") in text
            and str(modes / "Header.h") in text
            and str(stamps / "enforce.txt") in text,
            text,
        ),
    ]


def dependencies_absent(plant: Plant) -> Cases:
    """A clean file, an opted-out finding and a unit of -fsyntax-only add no mode stamp."""
    modes, stamps = plant.tree / "stamps" / "modes" / "app", plant.tree / "stamps"
    found, text = dependency_text(plant, "app/finding.cpp", "c")
    opted, opted_text = dependency_text(plant, "app/opted.cpp", "c")
    syntax, syntax_text = dependency_text(plant, "app/finding.cpp", "syntax")
    return [
        ("a file with no finding adds no mode stamp", found.exit == 0 and str(modes / "Clean.h") not in text, text),
        ("an opted-out finding adds no dependency", opted.exit == 0 and str(stamps) not in opted_text, opted_text),
        (
            "-fsyntax-only writes the dependency file before the report, so it holds no mode stamp",
            syntax.exit == 0 and str(stamps) not in syntax_text,
            syntax_text,
        ),
    ]


BUILD_PLANT = "test/build_plant.cpp"


def build_command(build_dir: Path) -> tuple[list[str], Path, str] | None:
    """Return the compile command of an object of BUILD_DIR that loads the plugin, its directory and its source.

    The command is the command of src/foundation/ContractHandler.cpp when the
    database holds it, else the first one that loads the plugin.
    """
    try:
        compiles = compdb.entries(build_dir)
    except compdb.CompdbError:
        return None
    entries = [entry for entry in compiles if "-fplugin=" in entry.command]
    if not entries:
        return None
    entry = next((entry for entry in entries if entry.file.endswith("src/foundation/ContractHandler.cpp")), entries[0])
    return list(entry.argv()), Path(entry.directory), entry.file


def build_plant(plant: Plant, has_error_row: bool) -> Outcome | str:
    """Compile test/build_plant.cpp of the tree with the flags of an object of the build directory.

    The tree is the root, and the table is the rule table of the build
    command.  With HAS_ERROR_ROW, the table also has an enforce row of the
    mode error for the plant.  Return the outcome, or SKIP when the call
    gives no build directory with such a command.
    """
    build_dir = plant.compiler.build_dir
    found = build_command(build_dir) if build_dir is not None else None
    if found is None:
        return SKIP
    command, directory, source = found
    rules_option, root_option = f"{PLUGIN_ARGUMENT}rules=", f"{PLUGIN_ARGUMENT}root="
    tree_table = next((Path(word[len(rules_option) :]) for word in command if word.startswith(rules_option)), None)
    if tree_table is None:
        return SKIP
    table = plant.work / f"build-rules-{int(has_error_row)}.txt"
    table.write_text(
        tree_table.read_text(encoding="utf-8") + (f"enforce {BUILD_PLANT} error\n" if has_error_row else ""),
        encoding="utf-8",
    )
    log = plant.work / f"build-plant-{int(has_error_row)}.sarif"
    argv: list[str] = []
    skip_next = False
    for word in command:
        if skip_next:
            skip_next = False
        elif word in ("-o", "-MF", "-MT", "-MQ"):
            skip_next = True
        elif word in ("-MD", "-MMD") or word == source:
            pass
        elif word.startswith(rules_option):
            argv.append(f"{rules_option}{table}")
        elif word.startswith(root_option):
            argv.append(f"{root_option}{plant.tree}")
        else:
            argv.append(word)
    argv += [
        "-fdiagnostics-color=never",
        f"{sarif.SARIF_FLAG}{log}",
        "-o",
        str(plant.work / f"build-plant-{int(has_error_row)}.o"),
        str(plant.tree / BUILD_PLANT),
    ]
    completed = plant.compiler.run(argv, directory)
    read = plant.compiler.read_log(log, plant.work)
    return Outcome(
        completed.exit,
        errors=read.errors,
        notes=read.notes,
        texts=read.texts,
        log_problem=read.problem,
        stderr=completed.err,
    )


def build_flags_error(plant: Plant) -> Cases | str:
    """With the flags of the build, an enforce row with the mode error fails the compile of the plant."""
    failed = build_plant(plant, has_error_row=True)
    if isinstance(failed, str):
        return failed
    return [
        (
            "with the flags of the build, an enforce row with the mode error fails the compile of the plant",
            failed.exit != 0
            and [message.split(";")[0] for _, message in failed.errors]
            == [f"{QUARANTINE_PREFIX}raw-pointer-object build_plant_pointer"],
            str(failed.errors),
        )
    ]


def build_flags_report(plant: Plant) -> Cases | str:
    """With the flags of the build and the table of the tree, the plant compiles."""
    passed = build_plant(plant, has_error_row=False)
    if isinstance(passed, str):
        return passed
    return [
        (
            "with the flags of the build and the table of the tree, the plant compiles",
            passed.exit == 0 and not passed.errors,
            f"exit {passed.exit}, errors {passed.errors}",
        )
    ]


def needs_ccache(procedure: Callable[[Plant], Cases]) -> Callable[[Plant], Cases | str]:
    """Return PROCEDURE, which gives SKIP on a host with no ccache."""

    def guarded(plant: Plant) -> Cases | str:
        """Give SKIP with no ccache on PATH, else run the procedure."""
        return SKIP if shutil.which("ccache") is None else procedure(plant)

    return guarded


PROCEDURES: dict[str, Callable[[Plant], Cases | str]] = {
    "section-cache-keyed": needs_ccache(section_cache_keyed),
    "section-cache-blind": needs_ccache(section_cache_blind),
    "enforce-cache-keyed": needs_ccache(enforce_cache_keyed),
    "enforce-cache-blind": needs_ccache(enforce_cache_blind),
    "dependencies-named": dependencies_named,
    "dependencies-absent": dependencies_absent,
    "build-flags-error": build_flags_error,
    "build-flags-report": build_flags_report,
}


# ── The witness ─────────────────────────────────────────────────────


def copy_plant(plant: Path, work: Path) -> None:
    """Copy each entry of a plant other than its manifest to WORK, and give each file NAME.planted its name NAME.

    Complexity: linear in the files of the plant.
    """
    for entry in sorted(plant.iterdir()):
        if entry.name == MANIFEST:
            continue
        if entry.is_dir():
            shutil.copytree(entry, work / entry.name, dirs_exist_ok=True)
        else:
            shutil.copy2(entry, work / entry.name)
    for planted in sorted(work.rglob(f"*{meta_runner.PLANTED_SUFFIX}")):
        planted.rename(planted.with_name(planted.name.removesuffix(meta_runner.PLANTED_SUFFIX)))
    (work / TREE).mkdir(exist_ok=True)


def make_parent(work: Path, directory: str) -> None:
    """Make the tree of WORK a git repository with one commit of DIRECTORY, and stage the tree as its change (THE PARENT).

    Raises:
        OSError: If the plant has no such directory, or a file cannot be copied
        git.GitError: If git fails
        git.NotIsolated: If git resolves a repository other than the tree
    """
    tree = work / TREE
    change = work / f"{TREE}.change"
    tree.rename(change)
    shutil.copytree(work / directory, tree)
    git.init(tree)
    git.commit_all(tree, "The parent of the change")
    for entry in sorted(tree.iterdir()):
        if entry.name == ".git":
            continue
        if entry.is_dir():
            shutil.rmtree(entry)
        else:
            entry.unlink()
    shutil.copytree(change, tree, dirs_exist_ok=True)
    shutil.rmtree(change)
    git.run(tree, "add", "-A", "--", ".")


def given_lines(outcome: Outcome) -> tuple[plugin_sections.Finding, ...]:
    """Return the finding lines of one compile: its report, else the section of its object, else none."""
    if outcome.report is not None:
        return outcome.report
    return outcome.section.findings if outcome.section is not None else ()


def module_errors(
    rule_id: str, tree: Path, lines: Sequence[plugin_sections.Finding], configuration: str = ""
) -> list[str]:
    """Return the errors that the module of a rule gives on the finding lines of a plant (THE KINDS OF A PLANT).

    The module judges the lines against the ledger of the tree of the plant.  A configuration gives the lines the
    configuration of a build, and the judge of the module judges them.  Complexity: linear in the lines.
    """
    from assay.core import configuration as build_config
    from assay.core import registry as rule_registry
    from assay.guard.engines.plugin import runner as plugin_runner

    rule = rule_registry.load().rules[rule_id]
    section = plugin_sections.Section("", tuple(lines))
    census = plugin_runner.census_of([plugin_runner.ObjectSection("plant", "", section)])
    if configuration:
        build = build_config.BuildConfiguration(configuration, ((build_config.KIND_KEY, configuration),))
        context = plugin_runner.PluginContext(tree, census=census, configuration=build)
        findings = meta_runner.module_judged(rule, context)
    else:
        context = plugin_runner.PluginContext(tree, census=census)
        findings = meta_runner.judged(rule, tree, context.findings_of(rule))
    return [item.text() for item in findings if item.level == "error"]


def run_plant(compiler: Compiler, plant: Path, rule_id: str = QUARANTINE_RULE) -> tuple[list[str], int, bool]:
    """Run each row of one plant or control of a rule in a throwaway work directory.

    Returns:
        The failures, the number of cases that the plant ran, and True when a procedure gave SKIP
    """
    from concurrent.futures import ThreadPoolExecutor

    try:
        manifest = read_manifest(plant / MANIFEST)
    except (rows_grammar.RowsError, OSError) as problem:
        return [f"the manifest cannot be read: {problem}"], 0, False
    is_plant = plant.name.startswith(meta_runner.PLANT_PREFIX)
    failures: list[str] = []
    if is_plant and not manifest.is_refused:
        failures.append("the plant has no compile with the exit fail, no refused table, no procedure and no judge row")
    if not is_plant and manifest.has_errors:
        failures.append("the control has a compile with the exit fail, an error row or a refused table")
    lines: list[plugin_sections.Finding] = []
    with tempfile.TemporaryDirectory(prefix=f"witness-{plant.name}-") as scratch:
        work = Path(scratch).resolve()
        copy_plant(plant, work)
        if manifest.parent:
            try:
                make_parent(work, manifest.parent)
            except (OSError, git.GitError, git.NotIsolated) as problem:
                return [f"the parent {manifest.parent} cannot be committed: {problem}"], 0, False
        compiles = list(manifest.compiles.values())
        tables = [*manifest.tables, *(case for verdict in manifest.white_space for case in white_space_cases(verdict))]
        with ThreadPoolExecutor(max_workers=max(1, len(compiles) + len(tables))) as pool:
            outcomes = [pool.submit(compiler.compile, item, work) for item in compiles]
            table_results = [pool.submit(table_failures, compiler, work, manifest.table_unit, case) for case in tables]
            for item, future in zip(compiles, outcomes, strict=True):
                try:
                    outcome = future.result()
                    failures += judge(item, manifest.expected[item.name], outcome, manifest)
                    if item.name in manifest.judged:
                        lines += given_lines(outcome)
                except (OSError, proc.ToolFailed) as problem:
                    failures.append(f"{item.name}: the compile cannot run: {problem}")
            for table_future in table_results:
                try:
                    failures += table_future.result()
                except (OSError, proc.ToolFailed) as problem:
                    failures.append(f"a table cannot run: {problem}")
        for path in manifest.reads:
            try:
                ruletable.load(Path(compiler.resolved(path, work)))
            except (ruletable.TableError, OSError) as problem:
                failures.append(f"ruletable.py does not read {path}: {problem}")
        if manifest.judged:
            errors = module_errors(rule_id, work / TREE, lines, manifest.configuration)
            if is_plant and not errors:
                failures.append(f"the module of {rule_id} gives no error on the finding lines of the judged compiles")
            if not is_plant and errors:
                failures += [f"the module of {rule_id} gives an error: {error}" for error in errors]
        skipped = False
        cases = len(compiles) + len(tables) + len(manifest.reads)
        for word in manifest.procedures:
            result = PROCEDURES[word](Plant(compiler, plant, work))
            if isinstance(result, str):
                skipped = True
                continue
            cases += len(result)
            failures += [f"{word}: {name}: {detail}" for name, holds, detail in result if not holds]
    return failures, cases, skipped


def find_program(program: str) -> str | None:
    """Return the path of a program: the word itself when it names a file, else the program of PATH."""
    return program if Path(program).is_file() else shutil.which(program)


def witness(
    rule_id: str,
    root: Path = REPO_ROOT,
    cxx: str | None = None,
    plugin: Path | None = None,
    build_dir: Path | None = None,
    jobs: int = 16,
) -> int:
    """Run each plant and each control of a rule of the engine plugin, and print each verdict (THE STATUS).

    Args:
        rule_id: The rule.  Its plants are in utils/plants/RULE/
        root: The root of the repository of the plants
        cxx: The compiler that loads the plugin, or None
        plugin: The quarantine plugin, or None
        build_dir: A build directory with a compile database, for the procedures of the flags of the build
        jobs: The most compiler processes at one time
    """
    from concurrent.futures import ThreadPoolExecutor

    program = find_program(cxx) if cxx is not None else None
    if program is None or plugin is None or not plugin.is_file():
        check_report.write_line(
            f"{rule_id}: the witness cannot run: give --cxx with a compiler and --plugin with the "
            f"quarantine plugin that the configure built (compiler {cxx}, plugin {plugin})"
        )
        return ExitCode.SKIPPED
    plants = meta_runner.plants_of(rule_id, root)
    if not plants:
        check_report.write_line(f"{rule_id}: no plant in {meta_runner.PLANTS}/{rule_id}/", is_error=True)
        return ExitCode.FINDINGS
    compiler = Compiler(program, plugin.resolve(), root.resolve(), build_dir, max(1, jobs))
    with ThreadPoolExecutor(max_workers=len(plants)) as pool:
        results = list(pool.map(lambda plant: run_plant(compiler, plant, rule_id), plants))
    failures: list[str] = []
    for plant, (found, cases, skipped) in zip(plants, results, strict=True):
        verdict = "FAIL" if found else ("skip" if skipped and cases == 0 else "ok  ")
        skip_note = "; a procedure cannot run on this host" if skipped else ""
        check_report.write_line(f"  {verdict} {plant.name}: {cases} case(s){skip_note}")
        failures += [f"{plant.name}: {failure}" for failure in found]
    if failures:
        check_report.write_line(f"{rule_id}: the witness fails, {len(failures)} difference(s):", is_error=True)
        for failure in failures:
            check_report.write_line(f"  {failure}", is_error=True)
        return ExitCode.FINDINGS
    check_report.write_line(f"{rule_id}: the witness holds for {len(plants)} plants and controls")
    return ExitCode.OK
