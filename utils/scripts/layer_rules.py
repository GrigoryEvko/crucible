"""layer_rules — read the rule table of the base layers and of the quarantine.

utils/scripts/layer-rules.txt holds the table, and its head comment gives the
format and the meaning of each row.  The quarantine plugin of
utils/tools/quarantine/ reads the same file in C++, and it refuses each table
that load() refuses.  utils/tools/quarantine/test/check_plugin.py compares the
two readers on the tables of utils/tools/quarantine/test/table-cases.txt and
on a table for each white-space character.

The two readers divide the lines only at '\\n' and the words only at a space
and a tab.  Python str.splitlines() and str.split() also divide at '\\v', '\\f'
and the Unicode spaces, so this reader does not use them.  A row that holds a
different white-space character is an error in the two readers.

The layer guard (check-layer-boundary.py) reads the layer rows.  It names the
project roots at the granularity of a directory under include/, so
guard_layers() folds the layers of the table into one layer for each root.
"""

from __future__ import annotations

import dataclasses
from pathlib import Path

TABLE = Path(__file__).resolve().parent / "layer-rules.txt"
MODES = ("report", "error")
ROW_KINDS = ("layer", "allow", "door", "admit", "quarantine", "enforce", "language")
# The prefix of a path of the build directory.  A file that the build writes
# has this prefix and its path inside the build directory, as kBuildPrefix of
# utils/tools/quarantine/plugin_core.h gives it.
BUILD_PREFIX = "@build/"
# The two characters that divide the words of a row.
WORD_DIVIDERS = " \t"
# The largest number of a rank or an arity: the largest value of the `int` of
# the reader of plugin_core.h.
MAX_NUMBER = 2**31 - 1


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

    plugin_core.h gives the same text, and check_plugin.py compares the two.
    """
    return (f"the row holds the white-space character U+{ord(character):04X}.  Only a space and a tab divide the "
            f"words of a row")


def is_number(text: str) -> bool:
    """Say whether TEXT is a number: a run of the ASCII digits 0 to 9.

    str.isdigit() alone also gives True for a digit of a different script and
    for a superscript digit.
    """
    return text.isascii() and text.isdigit()


def number_value(text: str, where: str) -> int:
    """Return the value of the number TEXT.

    Raises:
        TableError: When the value is more than MAX_NUMBER.  plugin_core.h
            gives the same text
    """
    digits = text.lstrip("0") or "0"
    if len(digits) > len(str(MAX_NUMBER)) or int(digits) > MAX_NUMBER:
        raise TableError(f"{where}: the number {text} is more than {MAX_NUMBER}, the largest number of a rank or an "
                         f"arity")
    return int(digits)


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


def parse(text: str, name: str = "layer-rules.txt") -> RuleTable:
    """Parse the text of a rule table.

    Complexity: O(rows squared) for the duplicate checks, and the table has
    about one hundred rows.

    Args:
        text: The text of the table
        name: The name of the table in each error message

    Raises:
        TableError: When a row does not obey the format of the head comment
    """
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
    admit_lines: dict[str, int] = {}  # the entry of each admit row, and its line
    for number, line in enumerate(text.split("\n"), start=1):
        content = line.strip(WORD_DIVIDERS)
        if not content or content.startswith("#"):
            continue
        where = f"{name}:{number}"
        white_space = next((character for character in content
                            if character.isspace() and character not in WORD_DIVIDERS), None)
        if white_space is not None:
            raise TableError(f"{where}: {white_space_message(white_space)}")
        words_text, _, reason = content.partition("|")
        words = [word for word in words_text.replace("\t", " ").split(" ") if word]
        reason = reason.strip(WORD_DIVIDERS)
        if not words:
            raise TableError(f"{where}: the row has a reason and no kind")
        kind, args = words[0], words[1:]
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
                     tuple(enforces), tuple(languages))


def load(path: Path = TABLE) -> RuleTable:
    """Read and parse the rule table at PATH.

    The bytes of the file go to parse() as the plugin reads them.
    Path.read_text() would change each '\\r\\n' and each '\\r' to '\\n'.

    Raises:
        TableError: When the table does not obey its format
        OSError: When the file cannot be read
        UnicodeDecodeError: When the file is not UTF-8
    """
    return parse(path.read_bytes().decode("utf-8"), path.name)


def guard_layers(table: RuleTable) -> tuple[tuple[tuple[str, str], ...], dict[str, frozenset[str]]]:
    """Fold the layers of TABLE into one layer for each project root, for the layer guard.

    The root of a layer path is its second component: include/fixy/Fn.h and
    src/fixy/os/ have the root fixy.  The roots are in the order of the
    lowest rank of their layers, and a root can name itself and each root
    before it.

    Returns:
        The scope prefix of each root, such as ("include/fixy/", "fixy"), and the
        roots that each root can name
    """
    lowest: dict[str, int] = {}
    prefixes: list[tuple[str, str]] = []
    for layer in table.layers:
        for path in layer.paths:
            parts = path.split("/")
            if len(parts) < 3:
                raise TableError(f"the layer path {path} has no project root: it must start with a directory "
                                 f"and a root, such as include/fixy/")
            root = parts[1]
            lowest[root] = min(lowest.get(root, layer.rank), layer.rank)
            prefix = f"{parts[0]}/{root}/"
            if (prefix, root) not in prefixes:
                prefixes.append((prefix, root))
    order = sorted(lowest, key=lambda root: lowest[root])
    allowed = {root: frozenset(order[:index + 1]) for index, root in enumerate(order)}
    return tuple(prefixes), allowed
