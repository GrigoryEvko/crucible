"""The rows grammar: one reader and one writer for the registry, the ledgers, the pins, the rule table and the audit table.

THE GRAMMAR
    file    = { line "\\n" }
    line    = blank | comment | row
    blank   = { " " | "\\t" }
    comment = { " " | "\\t" } "#" { each character except "\\n" }
    row     = head { " | " cell }
    head    = word { ( " " | "\\t" ) { " " | "\\t" } word }
    word    = one or more characters except " ", "\\t", "|" and "\\n"
    cell    = one or more characters except "\\n" and a "|" with no "\\" before it

THE RULES OF THE READER
    * The file is UTF-8.  decode() examines all the bytes of the file before
      parse() reads a line.  A byte that is not part of a UTF-8 sequence is
      then the first problem of the file, at its line.  A problem of the
      grammar on a line before that byte does not come first.  The decode
      keeps each line end as it is, and parse() then refuses each carriage
      return.
    * Only the space and the horizontal tab divide words.  The reader does
      not use str.split() with no argument, because that call also divides
      at "\\v", "\\f" and the Unicode spaces.
    * In a cell, "\\|" is a bar and "\\\\" is a backslash.  A backslash
      before another character, and a bar with no backslash before it, are
      errors.  The cell "-" is the empty cell.  A cell has no space or tab at
      its start or its end, so each cell has one spelling.
    * The file ends with a line break, and no line holds a carriage return.
    * A word of the form KEY=VALUE is a field.  fields() refuses a key that
      the schema does not give, and a second field with the same key.
    * A number is a run of decimal digits, and zeros before the first other
      digit are permitted.  number() refuses a number above the maximum of
      its type, and decimal() refuses a number that is not a finite decimal.
    * utils/tools/quarantine/rows.h reads the same grammar for the quarantine
      plugin.  The rule rows-readers gives the same files to the two readers.
      They must give the same rows, and refuse a file at the same line.
    * The schema of a file gives the key of each row.  unique() refuses a
      second row with the same key, so the order of the rows cannot change a
      result.

THE WRITER
    render() writes one row: the words with one space between them, and each
    cell after " | ", with each bar and each backslash escaped, and "-" for
    an empty cell.  write() keeps the head comment of the file, puts the
    header rows of the schema first, sorts the other rows by their keys, and
    writes the file only when its bytes change.

THE COST OF THE IMPORT
    The step meters of `loop step` and `loop test-step` read the registry for
    each compile, each link and each test.  So this module imports no module
    at its top level: no `re` (5.5 ms with -S) and no `dataclasses` (13 ms
    with -S).  Row is a class with slots that refuses an assignment after its
    construction.

`guard selftest tests.core.test_rows` does a test of the module.
"""

TYPE_CHECKING = False
if TYPE_CHECKING:
    from collections.abc import Callable, Iterable, Mapping, Sequence
    from pathlib import Path

SEPARATOR = " | "
EMPTY_CELL = "-"
COMMENT = "#"
DIVIDERS = " \t"


class RowsError(ValueError):
    """A line of a file does not obey the rows grammar or the schema of the file.

    Attributes:
        path: The file, as the caller names it
        line: The line, from 1, or 0 when no line applies
    """

    def __init__(self, path: str, line: int, message: str) -> None:
        """Keep the place of the error, and give the message with the place before it."""
        super().__init__(f"{path}:{line}: {message}")
        self.path = path
        self.line = line


class Row:
    """One row of a file: its place, the words of its head and its cells.

    The class has slots and refuses an assignment after its construction, as
    a frozen dataclass does, with no import of `dataclasses` (THE COST OF THE
    IMPORT).
    """

    __slots__ = ("path", "line", "words", "cells")

    path: str
    line: int
    words: tuple[str, ...]
    cells: tuple[str, ...]

    def __init__(self, path: str, line: int, words: tuple[str, ...], cells: tuple[str, ...]) -> None:
        """Set the four fields one time."""
        object.__setattr__(self, "path", path)
        object.__setattr__(self, "line", line)
        object.__setattr__(self, "words", words)
        object.__setattr__(self, "cells", cells)

    def __setattr__(self, name: str, value: object) -> None:
        """Refuse each assignment, because a row is a value."""
        raise AttributeError(f"a Row is a value, and its field {name} cannot change")

    def __eq__(self, other: object) -> bool:
        """Compare the four fields."""
        if not isinstance(other, Row):
            return NotImplemented
        return (self.path, self.line, self.words, self.cells) == (other.path, other.line, other.words, other.cells)

    def __hash__(self) -> int:
        """Hash the four fields."""
        return hash((self.path, self.line, self.words, self.cells))

    def __repr__(self) -> str:
        """Give the fields as Python text."""
        return f"Row(path={self.path!r}, line={self.line}, words={self.words!r}, cells={self.cells!r})"

    def error(self, message: str) -> RowsError:
        """Return an error at the place of this row."""
        return RowsError(self.path, self.line, message)


def head_words(text: str) -> list[str]:
    """Divide a head at each run of spaces and tabs, and only there.

    Complexity: linear in the length of the head.
    """
    return [word for word in text.replace("\t", " ").split(" ") if word]


def unescape(text: str, path: str, line: int) -> str:
    """Return the value of one cell: the escapes resolved, and "" for the cell "-".

    Raises:
        RowsError: If the cell is empty, has a space or a tab at an end, holds a bar with no backslash before it,
            or holds a backslash before a character that is not a bar or a backslash
    """
    if not text:
        raise RowsError(path, line, f"a cell is empty.  Write {EMPTY_CELL!r} for an empty cell")
    if text[0] in DIVIDERS or text[-1] in DIVIDERS:
        raise RowsError(path, line, f"the cell {text!r} has a space or a tab at its start or its end.  Remove it")
    if text == EMPTY_CELL:
        return ""
    if "\\" not in text:
        if "|" in text:
            raise RowsError(path, line, f"the cell {text!r} holds a bar with no backslash before it.  Write \\| for a "
                                        f"bar in a cell")
        return text
    value: list[str] = []
    index = 0
    while index < len(text):
        character = text[index]
        if character == "\\":
            following = text[index + 1:index + 2]
            if following not in ("|", "\\"):
                raise RowsError(path, line, f"the cell {text!r} holds a backslash before {following!r}.  A backslash "
                                            f"escapes only a bar or a backslash: write \\\\ for a backslash")
            value.append(following)
            index += 2
            continue
        if character == "|":
            raise RowsError(path, line, f"the cell {text!r} holds a bar with no backslash before it.  Write \\| for a "
                                        f"bar in a cell")
        value.append(character)
        index += 1
    return "".join(value)


def split_row(text: str, path: str, line: int) -> tuple[str, list[str]]:
    """Divide the text of a row into its head and the raw text of its cells.

    A separator is " | " with no backslash that escapes its bar.  The scan
    reads the escapes from left to right, so "\\\\|" is a backslash and then
    a bar.  Complexity: linear in the length of the row.
    """
    if "\\" not in text:
        parts = text.split(SEPARATOR)
        return parts[0], parts[1:]
    parts: list[str] = []
    start = 0
    index = 0
    while index < len(text):
        character = text[index]
        if character == "\\":
            index += 2
            continue
        if (character == "|" and index > 0 and text[index - 1] == " " and text[index + 1:index + 2] == " "):
            parts.append(text[start:index - 1])
            start = index + 2
            index += 2
            continue
        index += 1
    parts.append(text[start:])
    return parts[0], parts[1:]


def parse(text: str, path: str) -> tuple[Row, ...]:
    """Read each row of a file.  A blank line and a comment are not rows.

    Args:
        text: The text of the file
        path: The name of the file in each error

    Returns:
        The rows, in the order of the file

    Raises:
        RowsError: If a line does not obey the grammar

    Complexity: linear in the length of the text.
    """
    if not text:
        return ()
    if not text.endswith("\n"):
        raise RowsError(path, text.count("\n") + 1, "the file ends with no line break.  End the last line with one")
    rows: list[Row] = []
    for number, raw in enumerate(text[:-1].split("\n"), start=1):
        if "\r" in raw:
            raise RowsError(path, number, "the line holds a carriage return.  Use a line feed only")
        stripped = raw.lstrip(DIVIDERS)
        if not stripped or stripped.startswith(COMMENT):
            continue
        head, raw_cells = split_row(raw, path, number)
        words = head_words(head)
        if not words:
            raise RowsError(path, number, "the row has no word before its first cell")
        for word in words:
            if "|" in word:
                raise RowsError(path, number, f"the word {word!r} holds a bar.  Put \" | \" between a word and a cell")
        cells = tuple(unescape(cell, path, number) for cell in raw_cells)
        rows.append(Row(path, number, tuple(words), cells))
    return tuple(rows)


def decode(data: bytes, path: str) -> str:
    """Return the text of the bytes of a file, decoded as UTF-8, with each line end as it is.

    Args:
        data: The bytes of the file
        path: The name of the file in the error

    Raises:
        RowsError: If the bytes are not UTF-8.  The line is the line of the first byte that is not part of a UTF-8
            sequence: the line breaks before it, plus 1

    Complexity: linear in the number of bytes.
    """
    try:
        return data.decode("utf-8")
    except UnicodeDecodeError as problem:
        line = data.count(b"\n", 0, problem.start) + 1
        raise RowsError(path, line, f"the line is not UTF-8 ({problem.reason} at the byte "
                                    f"0x{data[problem.start]:02x}).  Write the file in UTF-8") from None


def read(path: Path, shown: str | None = None) -> tuple[Row, ...]:
    """Read the rows of one file: decode() and then parse().

    Raises:
        RowsError: If the file is not UTF-8, or a line does not obey the grammar
        OSError: If the file cannot be read
    """
    name = shown if shown is not None else str(path)
    return parse(decode(path.read_bytes(), name), name)


def fields(row: Row, allowed: frozenset[str], start: int = 2) -> dict[str, str]:
    """Return the fields KEY=VALUE of a row, from the word at `start`.

    Raises:
        RowsError: If a word is not a field, a key is not in `allowed`, a value is empty, or a key comes two times
    """
    found: dict[str, str] = {}
    for word in row.words[start:]:
        key, equals, value = word.partition("=")
        if not equals or not key or not value:
            raise row.error(f"the word {word!r} is not a field KEY=VALUE")
        if key not in allowed:
            raise row.error(f"the key {key!r} is not a field of the row {row.words[0]}.  The keys are: "
                            f"{', '.join(sorted(allowed))}")
        if key in found:
            raise row.error(f"the key {key!r} comes two times in the row")
        found[key] = value
    return found


def number(text: str, maximum: int, row: Row, what: str) -> int:
    """Read a whole number: a run of decimal digits, at most `maximum`.

    Zeros before the first other digit are permitted.  The length check of the digits after those zeros comes before
    int().  Then a long text does not cause a long conversion.  number() of utils/tools/quarantine/rows.h gives the
    same result.

    Raises:
        RowsError: If the text is not a run of decimal digits, or the number is above the maximum
    """
    if not text or not all("0" <= character <= "9" for character in text):
        raise row.error(f"the {what} {text!r} is not a run of decimal digits")
    significant = text.lstrip("0") or "0"
    if len(significant) > len(str(maximum)) or int(significant) > maximum:
        raise row.error(f"the {what} {text} is above the maximum {maximum} of its type")
    return int(significant)


def decimal(text: str, row: Row, what: str) -> float:
    """Read a decimal number: digits, and a point and digits when the number has a fraction.

    Raises:
        RowsError: If the text is not of that form, or it has more than 18 digits
    """
    whole, point, fraction = text.partition(".")
    digits = whole + fraction
    if (not whole or (point and not fraction) or not all("0" <= character <= "9" for character in digits)
            or len(digits) > 18):
        raise row.error(f"the {what} {text!r} is not a decimal number of at most 18 digits, such as 12 or 0.5")
    return float(text)


def unique(rows: Iterable[Row], key: Callable[[Row], object]) -> dict[object, Row]:
    """Return the rows by their keys, and refuse a second row with the same key.

    Args:
        rows: The rows of one kind
        key: A function from a row to its key

    Raises:
        RowsError: If two rows have the same key
    """
    found: dict[object, Row] = {}
    for row in rows:
        value = key(row)
        if value in found:
            raise row.error(f"the row has the key of the row at line {found[value].line}: {value!r}")
        found[value] = row
    return found


def escape(value: str) -> str:
    """Return the text of one cell: each backslash and each bar escaped, and "-" for an empty value.

    Raises:
        ValueError: If the value holds a line break or a carriage return, or has a space or a tab at an end, which
            the grammar cannot hold
    """
    if not value:
        return EMPTY_CELL
    if value == EMPTY_CELL:
        raise ValueError(f"the value {EMPTY_CELL!r} is the spelling of the empty cell, and a cell cannot hold it")
    if "\n" in value or "\r" in value or value[0] in DIVIDERS or value[-1] in DIVIDERS:
        raise ValueError(f"the value {value!r} holds a line break, or a space or a tab at an end")
    return value.replace("\\", "\\\\").replace("|", "\\|")


def render(words: Sequence[str], cells: Sequence[str] = ()) -> str:
    """Return the text of one row, with no line break.

    Raises:
        ValueError: If a word is empty or holds a space, a tab, a bar or a line break, or a cell cannot be written
    """
    for word in words:
        if not word or any(character in word for character in " \t|\n\r"):
            raise ValueError(f"the word {word!r} is empty or holds a space, a tab, a bar or a line break")
    if not words:
        raise ValueError("a row has a minimum of one word")
    return SEPARATOR.join([" ".join(words), *(escape(cell) for cell in cells)])


def head_comment(text: str) -> str:
    """Return the comment lines and the blank lines at the start of a file, with their line breaks."""
    lines: list[str] = []
    for raw in text.splitlines(keepends=True):
        stripped = raw.strip(" \t\n")
        if stripped and not stripped.startswith(COMMENT):
            break
        lines.append(raw)
    return "".join(lines)


def compose(comment: str, header: Sequence[str], rows: Iterable[tuple[tuple[str, ...], str]]) -> str:
    """Return the text of a file: the head comment, the header rows, and the other rows sorted by their keys.

    Raises:
        ValueError: If two rows have the same key
    """
    ordered = sorted(rows, key=lambda item: item[0])
    for earlier, later in zip(ordered, ordered[1:]):
        if earlier[0] == later[0]:
            raise ValueError(f"two rows have the key {later[0]!r}: {earlier[1]!r} and {later[1]!r}")
    text = comment if not comment or comment.endswith("\n") else comment + "\n"
    return text + "".join(f"{line}\n" for line in [*header, *(line for _, line in ordered)])


def write(path: Path, comment: str, header: Sequence[str], rows: Iterable[tuple[tuple[str, ...], str]]) -> bool:
    """Write a file with compose(), and only when its bytes change.

    Returns:
        True when the file changed, False when it held the same bytes

    Raises:
        ValueError: If two rows have the same key
        OSError: If the file cannot be written
    """
    data = compose(comment, header, rows).encode("utf-8")
    try:
        if path.read_bytes() == data:
            return False
    except FileNotFoundError:
        pass
    from assay.core import cache

    cache.write_atomic(path, data)
    return True


def by_kind(rows: Iterable[Row]) -> Mapping[str, tuple[Row, ...]]:
    """Return the rows of each kind: the first word of the row."""
    kinds: dict[str, list[Row]] = {}
    for row in rows:
        kinds.setdefault(row.words[0], []).append(row)
    return {kind: tuple(items) for kind, items in kinds.items()}
