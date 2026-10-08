"""The test of the rows grammar (assay.core.rows): the reader, the escapes, the numbers, the keys, UTF-8, the writer.

`guard selftest tests.core.test_rows` runs the test.  Each case prints one
line.  Exit 0 when each case holds, else 1.
"""

import sys
import tempfile
from collections.abc import Callable
from pathlib import Path

from assay.core import rows as rows_grammar

FILE = "planted.txt"


def refuses(action: Callable[[], object], error: type[Exception] = rows_grammar.RowsError) -> bool:
    """Whether an action raises the error."""
    try:
        action()
    except error:
        return True
    return False


def parsed(text: str) -> tuple[rows_grammar.Row, ...]:
    """Read a planted text."""
    return rows_grammar.parse(text, FILE)


def reader_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the reader: the words, the cells, the comments and the end of the file."""
    rows = parsed("# a head comment\n\nrule alpha  key=1\tother=2 | first cell | -\n  # an indented comment\npath x\n")
    expect(
        "a comment and a blank line are not rows, and a row keeps its line",
        [row.line for row in rows] == [3, 5] and rows[0].words == ("rule", "alpha", "key=1", "other=2"),
    )
    expect("the cell '-' is the empty cell", rows[0].cells == ("first cell", ""))
    expect(
        "only the space and the tab divide words: a form feed stays in a word",
        parsed("one\x0ctwo three\n")[0].words == ("one\x0ctwo", "three"),
    )
    expect("an empty file has no row", parsed("") == ())
    expect("a file with no line break at its end is refused", refuses(lambda: parsed("rule alpha")))
    expect("a carriage return is refused", refuses(lambda: parsed("rule alpha\r\n")))
    expect("an empty cell is refused", refuses(lambda: parsed("rule alpha | one |  | three\n")))
    expect("a cell with a space at its end is refused", refuses(lambda: parsed("rule alpha | one \n")))
    expect("a bar with no backslash in a cell is refused", refuses(lambda: parsed("rule alpha | one|two\n")))
    expect("a bar in a word is refused", refuses(lambda: parsed("rule al|pha\n")))
    expect("a row with no word before its first cell is refused", refuses(lambda: parsed("   | cell\n")))
    expect("a backslash before another character is refused", refuses(lambda: parsed("rule alpha | a\\b\n")))


def escape_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the escapes of a cell, and that the writer and the reader agree."""
    row = parsed("rule alpha | a \\| b | c\\\\d | e\\\\\n")[0]
    expect("\\| is a bar and \\\\ is a backslash", row.cells == ("a | b", "c\\d", "e\\"))
    expect(
        "a backslash before a separator escapes its bar, so the bar is in the cell",
        parsed("rule alpha | one \\| two\n")[0].cells == ("one | two",),
    )
    for value in ("a | b", "back\\slash", "plain words", "", "x|y\\"):
        text = rows_grammar.render(["rule", "alpha"], [value, "next"]) + "\n"
        expect(f"the writer and the reader agree on {value!r}", parsed(text)[0].cells == (value, "next"))
    expect(
        "the writer refuses the value '-', which is the empty cell",
        refuses(lambda: rows_grammar.escape("-"), ValueError),
    )
    expect(
        "the writer refuses a line break and a space at an end",
        refuses(lambda: rows_grammar.escape("a\nb"), ValueError)
        and refuses(lambda: rows_grammar.escape(" a"), ValueError),
    )
    expect(
        "the writer refuses a word with a space or a bar",
        refuses(lambda: rows_grammar.render(["two words"]), ValueError)
        and refuses(lambda: rows_grammar.render(["a|b"]), ValueError)
        and refuses(lambda: rows_grammar.render([]), ValueError),
    )


def value_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the fields, the numbers, the decimals and the keys."""
    row = parsed("rule alpha engine=meta ledger=none\n")[0]
    allowed = frozenset({"engine", "ledger"})
    expect(
        "fields() gives the fields of a row", rows_grammar.fields(row, allowed) == {"engine": "meta", "ledger": "none"}
    )
    expect(
        "fields() refuses an unknown key",
        refuses(lambda: rows_grammar.fields(parsed("rule alpha colour=red\n")[0], allowed)),
    )
    expect(
        "fields() refuses a second field with the same key",
        refuses(lambda: rows_grammar.fields(parsed("rule alpha engine=a engine=b\n")[0], allowed)),
    )
    expect(
        "fields() refuses a word that is not a field, and an empty value",
        refuses(lambda: rows_grammar.fields(parsed("rule alpha engine\n")[0], allowed))
        and refuses(lambda: rows_grammar.fields(parsed("rule alpha engine=\n")[0], allowed)),
    )
    expect("number() reads a run of digits", rows_grammar.number("42", 100, row, "count") == 42)
    expect(
        "number() refuses a number above its maximum, also a long one",
        refuses(lambda: rows_grammar.number("101", 100, row, "count"))
        and refuses(lambda: rows_grammar.number("9" * 40, 100, row, "count")),
    )
    expect(
        "number() refuses a sign, a space, a point and a Unicode digit",
        all(
            refuses(lambda text=text: rows_grammar.number(text, 100, row, "count"))
            for text in ("-1", "+1", " 1", "1.0", "١", "")
        ),
    )
    expect(
        "number() permits zeros before the first other digit, also before the maximum",
        rows_grammar.number("0002147483647", 2147483647, row, "line") == 2147483647
        and rows_grammar.number("000", 5, row, "count") == 0
        and rows_grammar.number("0" * 40 + "7", 10, row, "count") == 7,
    )
    expect(
        "number() refuses a number above its maximum after the zeros",
        refuses(lambda: rows_grammar.number("0002147483648", 2147483647, row, "line"))
        and refuses(lambda: rows_grammar.number("0" * 40 + "11", 10, row, "count")),
    )
    expect(
        "decimal() reads a whole number and a fraction",
        rows_grammar.decimal("12", row, "value") == 12.0 and rows_grammar.decimal("0.5", row, "value") == 0.5,
    )
    expect(
        "decimal() refuses an exponent, nan, inf, a lone point and a long number",
        all(
            refuses(lambda text=text: rows_grammar.decimal(text, row, "value"))
            for text in ("1e5", "nan", "inf", ".5", "5.", "1" * 19, "-1")
        ),
    )
    rows = parsed("rule alpha\nrule beta\nrule alpha\n")
    expect(
        "unique() refuses a second row with the same key, and names the first line",
        refuses(lambda: rows_grammar.unique(rows, lambda item: item.words[1])),
    )
    expect(
        "by_kind() groups the rows by their first word",
        {kind: len(items) for kind, items in rows_grammar.by_kind(parsed("rule a\npath b\nrule c\n")).items()}
        == {"rule": 2, "path": 1},
    )
    expect("a row refuses an assignment", refuses(lambda: setattr(row, "line", 3), AttributeError))


def refused_line(data: bytes) -> int:
    """Return the line at which read() refuses a file of these bytes, or 0 when it accepts the file."""
    with tempfile.TemporaryDirectory(prefix="test-rows-") as scratch:
        path = Path(scratch) / FILE
        path.write_bytes(data)
        try:
            rows_grammar.read(path)
        except rows_grammar.RowsError as problem:
            return problem.line
    return 0


def decode_cases(expect: Callable[[str, bool], None]) -> None:
    """Check decode() and read(): the UTF-8 check of all the bytes, its line, and the line ends that stay."""
    expect(
        "decode() gives the text of UTF-8 bytes, with a byte order mark and a NUL as they are",
        rows_grammar.decode("﻿a\x00 | ж\n".encode(), FILE) == "﻿a\x00 | ж\n",
    )
    expect(
        "decode() refuses bytes that are not UTF-8 at the line of the first bad byte",
        refuses(lambda: rows_grammar.decode(b"a\nb\n\xc0\x80\n", FILE)) and refused_line(b"a\nb\n\xc0\x80\n") == 3,
    )
    expect(
        "read() refuses a surrogate, a byte above F4 and a stray continuation byte at their lines",
        refused_line(b"a\n\xed\xa0\x80\n") == 2
        and refused_line(b"a\nb\nc\n\xf5\n") == 4
        and refused_line(b"\x80\n") == 1,
    )
    expect(
        "read() refuses a sequence that a line break or the end of the file cuts, at the line of its first byte",
        refused_line(b"a\nb \xe2\x82\nc\n") == 2 and refused_line(b"a\nb\nc \xf0\x9f\x99") == 3,
    )
    expect(
        "the UTF-8 check of the whole file comes before the grammar: a bad byte on line 4 wins over line 2",
        refused_line(b"a\nb | c|d\nc\n\xff\n") == 4,
    )
    expect(
        "read() keeps a carriage return, and parse() refuses it at its line",
        refused_line(b"a\nb\r\nc\n") == 2 and refused_line(b"a\r\n") == 1,
    )
    expect(
        "read() accepts a file of one line break and an empty file", refused_line(b"\n") == 0 and refused_line(b"") == 0
    )


def writer_cases(expect: Callable[[str, bool], None]) -> None:
    """Check the writer: the head comment, the order of the rows, and a write only when the bytes change."""
    text = "# the head\n#   comment\n\nformat 1\nrule b\n"
    expect(
        "head_comment() gives the comment lines and the blank lines at the start",
        rows_grammar.head_comment(text) == "# the head\n#   comment\n\n",
    )
    rows = [(("rule", "beta"), "rule beta"), (("rule", "alpha"), "rule alpha")]
    composed = rows_grammar.compose("# the head\n", ["format 1"], rows)
    expect(
        "compose() keeps the head comment, puts the header first and sorts the rows by key",
        composed == "# the head\nformat 1\nrule alpha\nrule beta\n",
    )
    expect(
        "compose() refuses two rows with the same key",
        refuses(lambda: rows_grammar.compose("", [], [(("a",), "a"), (("a",), "a two")]), ValueError),
    )
    with tempfile.TemporaryDirectory(prefix="test-rows-") as scratch:
        path = Path(scratch) / FILE
        first = rows_grammar.write(path, "# the head\n", ["format 1"], rows)
        stamp = path.stat().st_mtime_ns
        second = rows_grammar.write(path, "# the head\n", ["format 1"], list(reversed(rows)))
        expect(
            "write() writes a new file, and does not write the same bytes again",
            first and not second and path.stat().st_mtime_ns == stamp and path.read_text() == composed,
        )
        expect(
            "read() reads the file that write() writes",
            [row.words for row in rows_grammar.read(path)] == [("format", "1"), ("rule", "alpha"), ("rule", "beta")],
        )


def main(argv: list[str]) -> int:
    """Run each case, and print one line for each.

    Returns:
        0 when each case holds, 1 when a case fails, 2 for a usage error
    """
    if argv:
        print("usage: guard selftest tests.core.test_rows", file=sys.stderr)
        return 2
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    reader_cases(expect)
    escape_cases(expect)
    value_cases(expect)
    decode_cases(expect)
    writer_cases(expect)
    if failures:
        print(f"tests.core.test_rows: FAILED, {len(failures)} case(s) did not hold")
        return 1
    print("tests.core.test_rows: every case holds.")
    return 0
