"""A lexer of C and C++ source text for the guards in scripts/.

A guard that reads source text must not count a name inside a comment or a
literal.  This module blanks those tokens and keeps each offset, so a
guard can report the line of a hit in the file on disk.  It is a lexer,
not a parser: it does not resolve names or types.
"""

import re
from bisect import bisect_right

# One pass over the text.  The order of the alternatives matters: a raw
# string and a prefixed literal start with an identifier character, so
# they come before the identifier.  A number comes before a character
# literal, so a digit separator such as 1'000 stays in the number.
LEXER = re.compile(r"""
    (?P<line_comment>//[^\n]*)
  | (?P<block_comment>/\*.*?(?:\*/|\Z))
  | (?P<raw>(?:u8|[uUL])?R"(?P<delim>[^()\\ \t\v\f\n"]{0,16})\(.*?\)(?P=delim)")
  | (?P<string>(?:u8|[uUL])?"(?:\\.|[^"\\\n])*"?)
  | (?P<number>\.?[0-9](?:[eEpP][+-]|['\w.])*)
  | (?P<char>(?:u8|[uUL])?'(?:\\.|[^'\\\n])*'?)
  | (?P<ident>[A-Za-z_][A-Za-z_0-9]*)
""", re.S | re.X)
SPLICE = re.compile(r"\\\r?\n")


def splice(text: str) -> tuple[str, list[int]]:
    """Join each backslash-newline pair, as translation phase 2 does.

    The second result holds the offset of each join in the joined text, so
    line_of can recover a line of the file on disk."""
    joins: list[int] = []
    pieces: list[str] = []
    last = 0
    removed = 0
    for match in SPLICE.finditer(text):
        pieces.append(text[last:match.start()])
        joins.append(match.start() - removed)
        removed += match.end() - match.start()
        last = match.end()
    pieces.append(text[last:])
    return "".join(pieces), joins


def line_of(joined: str, joins: list[int], offset: int) -> int:
    """The line in the file on disk that holds the offset of the joined text."""
    return joined.count("\n", 0, offset) + 1 + bisect_right(joins, offset)


def blank(text: str, names: frozenset[str] = frozenset(), blank_literals: bool = False) -> tuple[str, list[int]]:
    """Blank each comment, and each literal when asked, and find the names.

    Each blanked character becomes a space and each newline stays, so an
    offset in the result is an offset in the input.  The second result
    holds the offset of each identifier token in names, which excludes a
    name inside a comment or a literal.  Complexity: linear in the length
    of the text."""
    out: list[str] = []
    hits: list[int] = []
    last = 0
    for match in LEXER.finditer(text):
        out.append(text[last:match.start()])
        chunk = match.group(0)
        is_comment = match.group("line_comment") is not None or match.group("block_comment") is not None
        is_literal = any(match.group(kind) is not None for kind in ("raw", "string", "char"))
        if is_comment or (blank_literals and is_literal):
            out.append("".join("\n" if ch == "\n" else " " for ch in chunk))
        else:
            if match.group("ident") is not None and chunk in names:
                hits.append(match.start())
            out.append(chunk)
        last = match.end()
    out.append(text[last:])
    return "".join(out), hits
