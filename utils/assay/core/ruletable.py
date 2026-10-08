"""ruletable — read the rule table of the base layers and of the quarantine.

utils/rule-table.txt holds the table, and its head comment gives the meaning of
each row.  The table is a rows file: assay.core.rows reads its grammar, and
this module reads its schema on those rows.  The quarantine plugin of
utils/tools/quarantine/ reads the same file in C++.  rows.h reads the grammar
there, and rule_table.cpp reads the schema.  The plugin refuses each table
that load() refuses, at the same line.  The plants and the controls of the
rule quarantine give the two readers the same tables, also a table for each
white-space character.

The grammar divides the words only at a space and a tab.  A word or a cell
that holds a different white-space character is an error of the schema, in
the two readers.  A reader that divides at each white-space character, as
Python str.split() does, reads different words.

The seal rows give the rule detsafe-ledger of the plugin, and the heavy and
light rows give the rule heavy-include.  `loop derive header-costs` writes the
heavy and light rows from a measure (assay.loop.derive.header_costs).  The
cite rows give the rule refined-pre-subsumption, and the grant rows give the
rule syscall-grant-coverage.
"""

from __future__ import annotations

import dataclasses
from pathlib import Path

from assay.core import rows as rows_grammar
from assay.core.paths import REPO_ROOT

TABLE = REPO_ROOT / "utils" / "rule-table.txt"
MODES = ("report", "error")
ROW_KINDS = (
    "layer",
    "allow",
    "door",
    "admit",
    "quarantine",
    "enforce",
    "language",
    "seal",
    "heavy",
    "light",
    "cite",
    "grant",
)
# The errors of a cite row and of a grant row that do not obey the format of their words.  rule_table.cpp gives
# the same texts.
CITE_FORMAT = (
    "a cite row is 'cite PREDICATE PROCEDURE...', with the qualified name of a predicate class and the "
    "name of each decide procedure that tests it"
)
GRANT_FORMAT = (
    "a grant row is 'grant FILE ALIAS ENUMERATION [DOOR...]', with the grant file, the qualified names "
    "of its alias and of the enumeration of the system calls, and each door file"
)
# The directory of the headers of the project roots.  A sealed PREFIX is a directory in it.
INCLUDE_PREFIX = "include/"
# The error of a seal row that does not obey the format of its words.  rule_table.cpp gives the same text.
SEAL_FORMAT = (
    "a seal row is 'seal ROOT PREFIX...', with one ROOT file of the source root and each PREFIX a "
    "directory under include/ whose parts are identifiers"
)
# The error of a row with two cells.  rule_table.cpp gives the same text.
CELLS_MESSAGE = "a row has at most one cell: the reason of an admit row, or the measure of a heavy or a light row"
# The prefix of a path of the build directory.  A file that the build writes
# has this prefix and its path inside the build directory, as kBuildPrefix of
# utils/tools/quarantine/plugin.h gives it.
BUILD_PREFIX = "@build/"
# The two characters that divide the words of a row.
WORD_DIVIDERS = rows_grammar.DIVIDERS
# The largest number of a rank or an arity: the largest value of the `int` of
# the reader of rule_table.cpp.
MAX_NUMBER = 2**31 - 1
# The rank of a project root of a quarantine row, above each layer: kTopRank of include.cpp.
TOP_RANK = MAX_NUMBER


class TableError(ValueError):
    """The rule table does not obey its format."""


@dataclasses.dataclass(frozen=True)
class Layer:
    """One layer row: a name, a rank and the paths of its files."""

    name: str
    rank: int
    paths: tuple[str, ...]


@dataclasses.dataclass(frozen=True)
class Admit:
    """One admit row: the entry, its restriction words, the reason and the line.

    `until` names the family that replaces the entry, or is "".  The other
    fields are the restrictions that the head comment of the table gives.
    """

    entry: str
    until: str
    reason: str
    line: int
    arity: int = -1
    concepts: bool = False
    parameters: bool = False
    unless: str = ""
    in_paths: tuple[str, ...] = ()
    plain_result: bool = False
    consteval_only: bool = False


@dataclasses.dataclass(frozen=True, slots=True)
class Seal:
    """One seal row: the include closure of ROOT holds no file in a PREFIX and names no namespace of a PREFIX.

    `namespaces` gives the namespace of each PREFIX, such as ("crucible", "ledger") for include/crucible/ledger/.
    """

    root: str
    prefixes: tuple[str, ...]
    namespaces: tuple[tuple[str, ...], ...]
    line: int


@dataclasses.dataclass(frozen=True, slots=True)
class HeaderCost:
    """One heavy or light row: the header with its angle brackets, its kind, the measure and the line."""

    header: str
    is_heavy: bool
    measure: str
    line: int


@dataclasses.dataclass(frozen=True, slots=True)
class Cite:
    """One cite row: the full name of a predicate class, and the names of the procedures that test that predicate."""

    predicate: str
    procedures: tuple[str, ...]


@dataclasses.dataclass(frozen=True, slots=True)
class Grant:
    """One grant row: the grant file, its alias, the enumeration of the system calls and the door files."""

    file: str
    alias: str
    enumeration: str
    doors: tuple[str, ...]


@dataclasses.dataclass(frozen=True)
class RuleTable:
    """The rows of one rule table, in the order of the file."""

    layers: tuple[Layer, ...]
    allows: tuple[tuple[str, str], ...]
    doors: tuple[tuple[str, str], ...]
    admits: tuple[Admit, ...]
    quarantines: tuple[str, ...]
    enforces: tuple[tuple[str, str], ...]
    # The layer whose allowance caps the outside includes, and the paths of
    # each language row.
    languages: tuple[tuple[str, tuple[str, ...]], ...] = ()
    seals: tuple[Seal, ...] = ()
    header_costs: tuple[HeaderCost, ...] = ()
    cites: tuple[Cite, ...] = ()
    grants: tuple[Grant, ...] = ()

    def layer_named(self, name: str) -> Layer | None:
        """Return the layer with NAME, or None."""
        return next((layer for layer in self.layers if layer.name == name), None)

    def class_of(self, rel: str) -> tuple[str, Layer | None]:
        """Return the class of a path relative to the source root, and its layer.

        The longest path of a layer row or a quarantine row that holds the
        file decides.  Complexity: O(rows).

        Returns:
            ("base", layer), ("quarantined", None) or ("", None) when no row holds the file
        """
        best = ("", None)
        best_length = -1
        for layer in self.layers:
            for path in layer.paths:
                if holds(path, rel) and len(path) > best_length:
                    best, best_length = ("base", layer), len(path)
        for path in self.quarantines:
            if holds(path, rel) and len(path) > best_length:
                best, best_length = ("quarantined", None), len(path)
        return best

    def mode_of(self, rel: str) -> str:
        """Return the enforce mode of a path relative to the source root: report or error."""
        mode = "report"
        best_length = -1
        for path, row_mode in self.enforces:
            if holds(path, rel) and len(path) > best_length:
                mode, best_length = row_mode, len(path)
        return mode


def holds(path: str, rel: str) -> bool:
    """Say whether a row PATH holds the file REL: a directory holds each file under it."""
    return rel.startswith(path) if path.endswith("/") else rel == path


def _check_path(path: str, where: str) -> None:
    """Refuse a PATH that is not a plain relative path."""
    parts = (path[:-1] if path.endswith("/") else path).split("/")
    if not path or path.startswith("/") or any(part in ("", ".", "..") for part in parts):
        raise TableError(f"{where}: the path {path!r} must be relative to the source root, with no '.', '..' "
                         f"or empty component")


def _check_header(header: str, where: str) -> None:
    """Refuse a HEADER that is not a name in angle brackets."""
    if len(header) < 3 or not header.startswith("<") or not header.endswith(">"):
        raise TableError(f"{where}: the header {header!r} must be a name in angle brackets, for example <cstdint>")


def white_space_message(character: str) -> str:
    """Return the error of a row that holds CHARACTER, a white-space character that is not a space or a tab.

    rule_table.cpp gives the same text.
    """
    return (f"the row holds the white-space character U+{ord(character):04X}.  Only a space and a tab divide the "
            f"words of a row")


def is_identifier(text: str) -> bool:
    """Return True when TEXT is an identifier of ASCII letters, digits and underscores that does not start with a digit.

    rule_table.cpp gives the same test.  str.isidentifier() alone also accepts a letter of a different script.
    """
    return (
        bool(text)
        and text.isascii()
        and not text[0].isdigit()
        and all(character.isalnum() or character == "_" for character in text)
    )


def is_qualified_name(text: str) -> bool:
    """Return True when TEXT is one or more identifiers with "::" between two of them, and no "::" at its start."""
    return all(is_identifier(part) for part in text.split("::"))


def is_file_path(text: str) -> bool:
    """Return True when TEXT is the path of one file of the source root, not of the build directory."""
    parts = text.split("/")
    return (
        bool(text)
        and not text.startswith("/")
        and not text.endswith("/")
        and not text.startswith(BUILD_PREFIX)
        and all(part not in ("", ".", "..") for part in parts)
    )


def is_number(text: str) -> bool:
    """Say whether TEXT is a number: a run of the ASCII digits 0 to 9.

    str.isdigit() alone also gives True for a digit of a different script and
    for a superscript digit.
    """
    return text.isascii() and text.isdigit()


def number_value(text: str, where: str) -> int:
    """Return the value of the number TEXT.

    Raises:
        TableError: When the value is more than MAX_NUMBER.  rule_table.cpp
            gives the same text
    """
    digits = text.lstrip("0") or "0"
    if len(digits) > len(str(MAX_NUMBER)) or int(digits) > MAX_NUMBER:
        raise TableError(f"{where}: the number {text} is more than {MAX_NUMBER}, the largest number of a rank or an "
                         f"arity")
    return int(digits)


def seal_namespace(prefix: str) -> tuple[str, ...]:
    """Return the namespace of a sealed PREFIX: one name for each directory after include/.

    The answer is empty when PREFIX is no directory in include/, or when a part is no identifier.  rule_table.cpp
    gives the same answer.
    """
    if not prefix.startswith(INCLUDE_PREFIX) or not prefix.endswith("/") or len(prefix) <= len(INCLUDE_PREFIX):
        return ()
    parts = tuple(prefix[len(INCLUDE_PREFIX) : -1].split("/"))
    return parts if all(part.isascii() and part.isidentifier() for part in parts) else ()


def is_plain_path(path: str) -> bool:
    """Return True when PATH is a plain path relative to the source root, as _check_path accepts it."""
    parts = path.removesuffix("/").split("/")
    return bool(path) and not path.startswith("/") and not any(part in ("", ".", "..") for part in parts)


def parse_seal(args: list[str], reason: str, number: int, where: str) -> Seal:
    """Parse the words of one seal row after the row kind.

    Raises:
        TableError: When the row does not obey SEAL_FORMAT
    """
    if (
        len(args) < 2
        or not is_plain_path(args[0])
        or args[0].endswith("/")
        or args[0].startswith(BUILD_PREFIX)
        or reason
    ):
        raise TableError(f"{where}: {SEAL_FORMAT}")
    namespaces = tuple(seal_namespace(prefix) if is_plain_path(prefix) else () for prefix in args[1:])
    if not all(namespaces):
        raise TableError(f"{where}: {SEAL_FORMAT}")
    return Seal(args[0], tuple(args[1:]), namespaces, number)


def parse_admit(args: list[str], reason: str, number: int, where: str) -> Admit:
    """Parse the words of one admit row after the row kind.

    The words are ENTRY, then each restriction one time at most, in any order,
    and `in PATH...` last.

    Raises:
        TableError: When a restriction is unknown, repeated, malformed or does not fit the entry
    """
    entry = args[0]
    is_header = entry.startswith("<")
    if is_header:
        _check_header(entry, where)
    fields: dict[str, object] = {}
    index = 1
    while index < len(args):
        word = args[index]
        if word in fields:
            raise TableError(f"{where}: the restriction {word} is in the row two times")
        value = args[index + 1] if index + 1 < len(args) else ""
        if word == "until" and value:
            fields[word] = value
            index += 2
        elif word == "arity" and is_number(value) and not is_header:
            fields[word] = number_value(value, where)
            index += 2
        elif word == "concepts" and is_header:
            fields[word] = True
            index += 1
        elif word in ("parameters", "plain-result", "consteval-only") and not is_header:
            fields[word] = True
            index += 1
        elif word == "unless" and value.isascii() and value.isidentifier():
            fields[word] = value
            index += 2
        elif word == "in" and value:
            for path in args[index + 1:]:
                _check_path(path, where)
            fields["in_paths"] = tuple(args[index + 1:])
            index = len(args)
        else:
            raise TableError(f"{where}: {word!r} is no restriction of this entry.  The restrictions are 'until "
                             f"FAMILY', 'arity N', 'parameters', 'plain-result' and 'consteval-only' of a name, "
                             f"'concepts' of a header, 'unless MACRO' and 'in PATH...', each one time")
    return Admit(entry, str(fields.get("until", "")), reason, number, int(fields.get("arity", -1)),
                 bool(fields.get("concepts", False)), bool(fields.get("parameters", False)),
                 str(fields.get("unless", "")), tuple(fields.get("in_paths", ())),  # type: ignore[arg-type]
                 plain_result=bool(fields.get("plain-result", False)),
                 consteval_only=bool(fields.get("consteval-only", False)))


def parse(text: str, name: str = TABLE.name) -> RuleTable:
    """Parse the text of a rule table.

    Complexity: O(rows squared) for the duplicate checks, and the table has
    about one hundred rows.

    Args:
        text: The text of the table
        name: The name of the table in each error message

    Raises:
        TableError: When a line does not obey the rows grammar, or a row does not obey the schema of the head comment
    """
    try:
        rows = rows_grammar.parse(text, name)
    except rows_grammar.RowsError as problem:
        raise TableError(str(problem)) from None
    layers: list[Layer] = []
    allows: list[tuple[str, str]] = []
    doors: list[tuple[str, str]] = []
    admits: list[Admit] = []
    quarantines: list[str] = []
    enforces: list[tuple[str, str]] = []
    languages: list[tuple[str, tuple[str, ...]]] = []
    class_paths: dict[str, str] = {}
    allow_lines: list[tuple[str, str]] = []
    in_path_lines: list[tuple[str, str]] = []
    language_lines: list[tuple[str, str]] = []
    language_paths: list[tuple[str, str]] = []
    cites: list[Cite] = []
    cite_lines: dict[str, int] = {}  # the predicate of each cite row, and its line
    grants: list[Grant] = []
    grant_lines: dict[str, int] = {}  # the grant file of each grant row, and its line
    admit_lines: dict[str, int] = {}  # the entry of each admit row, and its line
    seals: list[Seal] = []
    seal_lines: dict[str, int] = {}  # the root of each seal row, and its line
    header_costs: list[HeaderCost] = []
    cost_lines: dict[str, int] = {}  # the header of each heavy and light row, and its line
    for row in rows:
        number = row.line
        where = f"{name}:{number}"
        white_space = next((character for part in (*row.words, *row.cells) for character in part
                            if character.isspace() and character not in WORD_DIVIDERS), None)
        if white_space is not None:
            raise TableError(f"{where}: {white_space_message(white_space)}")
        if len(row.cells) > 1:
            raise TableError(f"{where}: {CELLS_MESSAGE}")
        kind, args = row.words[0], list(row.words[1:])
        reason = row.cells[0] if row.cells else ""
        if kind not in ROW_KINDS:
            raise TableError(f"{where}: the row kind {kind!r} is unknown; the kinds are {', '.join(ROW_KINDS)}")
        if kind == "layer":
            if len(args) < 3 or not is_number(args[1]):
                raise TableError(f"{where}: a layer row is 'layer NAME RANK PATH...', with a RANK of digits")
            rank = number_value(args[1], where)
            if any(layer.name == args[0] for layer in layers):
                raise TableError(f"{where}: the layer {args[0]} has a second row")
            if any(layer.rank == rank for layer in layers):
                raise TableError(f"{where}: the rank {args[1]} belongs to a second layer")
            for path in args[2:]:
                _check_path(path, where)
                if path.startswith(BUILD_PREFIX):
                    raise TableError(f"{where}: a layer row holds no path of the build directory ({BUILD_PREFIX}), "
                                     f"because the build writes no base file")
                if path in class_paths:
                    raise TableError(f"{where}: the path {path} is also in {class_paths[path]}")
                class_paths[path] = f"the layer {args[0]}"
            layers.append(Layer(args[0], rank, tuple(args[2:])))
        elif kind == "allow":
            if len(args) != 2:
                raise TableError(f"{where}: an allow row is 'allow LAYER HEADER'")
            _check_header(args[1], where)
            allows.append((args[0], args[1]))
            allow_lines.append((args[0], where))
        elif kind == "door":
            if len(args) != 2 or args[1].endswith("/") or args[1].startswith(BUILD_PREFIX):
                raise TableError(f"{where}: a door row is 'door HEADER OWNER', and OWNER is one file of the source "
                                 f"root")
            _check_header(args[0], where)
            _check_path(args[1], where)
            if any(header == args[0] for header, _ in doors):
                raise TableError(f"{where}: the header {args[0]} has a second door")
            doors.append((args[0], args[1]))
        elif kind == "admit":
            if not args or not reason:
                raise TableError(f"{where}: an admit row is 'admit ENTRY [RESTRICTION...] | REASON', and the reason "
                                 f"is necessary")
            admit = parse_admit(args, reason, number, where)
            # The plugin keeps the first row of an entry, so a second row
            # would make the order of the rows decide the findings.
            if admit.entry in admit_lines:
                raise TableError(f"{where}: the entry {admit.entry} has a second admit row, and its first admit row "
                                 f"is on line {admit_lines[admit.entry]}")
            admit_lines[admit.entry] = number
            admits.append(admit)
            in_path_lines.extend((path, where) for path in admit.in_paths)
        elif kind == "quarantine":
            if len(args) != 1:
                raise TableError(f"{where}: a quarantine row is 'quarantine PATH'")
            _check_path(args[0], where)
            if args[0] in class_paths:
                raise TableError(f"{where}: the path {args[0]} is also in {class_paths[args[0]]}")
            class_paths[args[0]] = "a quarantine row"
            quarantines.append(args[0])
        elif kind == "language":
            if len(args) < 2:
                raise TableError(f"{where}: a language row is 'language LAYER PATH...'")
            for path in args[1:]:
                _check_path(path, where)
                if any(known == path for known, _ in language_paths):
                    raise TableError(f"{where}: the path {path} has a second language row")
                language_paths.append((path, where))
            languages.append((args[0], tuple(args[1:])))
            language_lines.append((args[0], where))
        elif kind == "seal":
            seal = parse_seal(args, reason, number, where)
            if seal.root in seal_lines:
                raise TableError(
                    f"{where}: the root {seal.root} has a second seal row, and its first seal row is on "
                    f"line {seal_lines[seal.root]}"
                )
            seal_lines[seal.root] = number
            seals.append(seal)
        elif kind in ("heavy", "light"):
            if len(args) != 1:
                raise TableError(
                    f"{where}: a {kind} row is '{kind} HEADER [| MEASURE]', with a HEADER in angle brackets"
                )
            _check_header(args[0], where)
            if args[0] in cost_lines:
                raise TableError(
                    f"{where}: the header {args[0]} has a second heavy or light row, and its first row "
                    f"is on line {cost_lines[args[0]]}"
                )
            cost_lines[args[0]] = number
            header_costs.append(HeaderCost(args[0], kind == "heavy", reason, number))
        elif kind == "cite":
            if len(args) < 2 or reason or not is_qualified_name(args[0]) or not all(map(is_identifier, args[1:])):
                raise TableError(f"{where}: {CITE_FORMAT}")
            if args[0] in cite_lines:
                raise TableError(
                    f"{where}: the predicate {args[0]} has a second cite row, and its first cite row is on "
                    f"line {cite_lines[args[0]]}"
                )
            cite_lines[args[0]] = number
            cites.append(Cite(args[0], tuple(args[1:])))
        elif kind == "grant":
            if (
                len(args) < 3
                or reason
                or not is_file_path(args[0])
                or not is_qualified_name(args[1])
                or not is_qualified_name(args[2])
                or not all(map(is_file_path, args[3:]))
            ):
                raise TableError(f"{where}: {GRANT_FORMAT}")
            if args[0] in grant_lines:
                raise TableError(
                    f"{where}: the file {args[0]} has a second grant row, and its first grant row is on "
                    f"line {grant_lines[args[0]]}"
                )
            grant_lines[args[0]] = number
            grants.append(Grant(args[0], args[1], args[2], tuple(args[3:])))
        else:
            if len(args) != 2 or args[1] not in MODES:
                raise TableError(f"{where}: an enforce row is 'enforce PATH MODE', and MODE is report or error")
            _check_path(args[0], where)
            if any(path == args[0] for path, _ in enforces):
                raise TableError(f"{where}: the path {args[0]} has a second enforce row")
            enforces.append((args[0], args[1]))
    known = {layer.name for layer in layers}
    for layer_name, where in allow_lines:
        if layer_name not in known:
            raise TableError(f"{where}: the allow row names the layer {layer_name}, and no layer row gives it")
    for layer_name, where in language_lines:
        if layer_name not in known:
            raise TableError(f"{where}: the language row names the layer {layer_name}, and no layer row gives it")
    for path, where in in_path_lines:
        if not any(holds(layer_path, path) for layer in layers for layer_path in layer.paths):
            raise TableError(f"{where}: the path {path} after `in` is in no layer.  An `in` path must be a path of "
                             f"the base")
    for path, where in language_paths:
        if not any(holds(layer_path, path) for layer in layers for layer_path in layer.paths):
            raise TableError(f"{where}: the path {path} of the language row is in no layer.  A path of the "
                             f"language must be a path of the base")
    return RuleTable(tuple(layers), tuple(allows), tuple(doors), tuple(admits), tuple(quarantines),
                     tuple(enforces), tuple(languages), tuple(seals), tuple(header_costs), tuple(cites),
                     tuple(grants))


def load(path: Path = TABLE) -> RuleTable:
    """Read and parse the rule table at PATH.

    The bytes of the file go to parse() as the plugin reads them.
    Path.read_text() would change each '\\r\\n' and each '\\r' to '\\n'.

    Raises:
        TableError: When the table is not UTF-8, or does not obey its format
        OSError: When the file cannot be read
    """
    try:
        text = rows_grammar.decode(path.read_bytes(), path.name)
    except rows_grammar.RowsError as problem:
        raise TableError(str(problem)) from None
    return parse(text, path.name)


def root_of_path(path: str) -> str:
    """Return the project root of a path: the directory after the first directory, or "" when it has none.

    include/fixy/Fn.h and src/fixy/os/ have the root fixy.  root_of_path of include.cpp gives the same answer.
    """
    parts = path.split("/")
    return parts[1] if len(parts) >= 3 else ""


def root_ranks(table: RuleTable) -> dict[str, int]:
    """Return the rank of each project root, as the rule layer-boundary of the plugin reads it.

    The rank of a root is the lowest rank of a layer path of the root in include/ or src/.  A root of a quarantine row
    in include/ has TOP_RANK, above each layer.  A base file names only a root of its own rank or of a lower rank.
    Complexity: O(paths of the layer rows and the quarantine rows).
    """
    ranks: dict[str, int] = {}
    for layer in table.layers:
        for path in layer.paths:
            root = root_of_path(path) if path.startswith((INCLUDE_PREFIX, "src/")) else ""
            if root:
                ranks[root] = min(ranks.get(root, layer.rank), layer.rank)
    for path in table.quarantines:
        root = root_of_path(path) if path.startswith(INCLUDE_PREFIX) else ""
        if root:
            ranks.setdefault(root, TOP_RANK)
    return ranks
