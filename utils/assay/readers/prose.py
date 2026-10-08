"""The reader of prose: the lines of the comments, the strings and the text of each file, and the search of a pattern.

Prose is the one input that is text by nature (the plan, section 18.4): a
comment, a docstring, a string and a page of Markdown.  A rule that refuses a
pattern in prose reads each file through this module.  It gives its pattern to
Pattern.  This module is a reader.  It can read the text of a node of the parse
tree and call the module re (the rule structured-reads).

THE PROSE OF A FILE
    prose_of() gives the prose lines of each file, each with its one-based
    line.  The kind of the file gives its prose:
      * C++ (tsast.is_in_cpp_scope): the comment, string and raw string
        nodes of the parse tree, and the argument of each directive such as
        #error or #pragma.  Also the string tokens of the body of each macro
        definition.  The parse of the files of one call is one batch.  Code
        is no prose.  A member access that looks like a word is no line.
      * Python (the suffix .py): the comments and the strings of the tokens
        of the module, with each docstring and the text of each f-string.
      * A symbolic link: the path of its target, which is the text that git
        keeps for the link.  The text behind the link is not read.
      * Each other file: each line.  A file with a NUL byte, or a file that
        is not UTF-8, is binary and has no prose.
    A C++ file that tsast.UNPARSEABLE lists has no parse tree, and it is read
    line by line.  A C++ file with an error in its parse has no lines and a
    problem that tells the error.  A Python file that does not tokenize has
    no lines and a problem too.

THE SEARCH
    Pattern holds one regular expression, compiled one time.  first() gives
    the first text of a line that the expression matches, or None.

`guard selftest tests.readers.test_prose` does a test of the module.
"""

import io
import re
import tokenize
from collections.abc import Iterator, Sequence
from dataclasses import dataclass
from pathlib import Path

from assay.readers import tsast

# The C++ node types that hold prose.
CPP_PROSE_NODES = ("comment", "string_literal", "raw_string_literal")
# A directive with an argument, such as #error, and the node of the argument of a directive or of a macro body.
DIRECTIVE_NODE = "preproc_call"
ARGUMENT_NODE = "preproc_arg"
# The parents of the body of a macro definition.
MACRO_NODES = ("preproc_def", "preproc_function_def")
STRING_TOKEN = "string"
PYTHON_SUFFIX = ".py"
# The Python token kinds that hold prose.  FSTRING_MIDDLE and TSTRING_MIDDLE are the text of an f-string and of a
# t-string.
PYTHON_PROSE = frozenset({tokenize.COMMENT, tokenize.STRING, tokenize.FSTRING_MIDDLE, tokenize.TSTRING_MIDDLE})


@dataclass(frozen=True, slots=True)
class ProseLine:
    """One line of the prose of a file: its one-based line and its content, with no line break."""

    line: int
    content: str


@dataclass(frozen=True, slots=True)
class FileProse:
    """The prose of one file, with its path relative to the root of the call and its lines in sequence.

    The problem of a file that its reader cannot read tells why, and it is an empty text for each other file.
    """

    path: str
    lines: tuple[ProseLine, ...]
    problem: str = ""


class Pattern:
    """A regular expression that a rule refuses in prose (THE SEARCH)."""

    __slots__ = ("_compiled",)

    def __init__(self, *branches: str) -> None:
        """Compile the branches as one expression: a line matches when one branch matches.

        Raises:
            re.error: If a branch is not a regular expression
        """
        self._compiled = re.compile("|".join(branches))

    def first(self, content: str) -> str | None:
        """Return the first part of a line that the expression matches, or None when no part matches.

        Complexity: linear in the length of the line for the expressions of the rules of the tree.
        """
        found = self._compiled.search(content)
        return None if found is None else found.group(0)


def split_spans(spans: Sequence[tuple[int, str]]) -> tuple[ProseLine, ...]:
    """Return one line for each line of each span (LINE, TEXT), sorted by the line and then by the content."""
    lines = [ProseLine(line + offset, part) for line, text in spans for offset, part in enumerate(text.split("\n"))]
    return tuple(sorted(lines, key=lambda item: (item.line, item.content)))


def cpp_prose(path: str, tree: tsast.Tree) -> FileProse:
    """Return the prose of one parsed C++ file, or its problem when its parse has an error (THE PROSE OF A FILE).

    Complexity: linear in the nodes of the tree and in the length of each macro body.
    """
    error = tsast.first_error(tree)
    if error is not None:
        return FileProse(path, (), f"the C++ file does not parse at line {error[0]}: {error[1]}")
    spans: list[tuple[int, str]] = [(node.line, tsast.prose_text(node)) for node in tree.find(*CPP_PROSE_NODES)]
    for call in tree.find(DIRECTIVE_NODE):
        argument = call.child_by_field("argument")
        if argument is not None:
            spans.append((argument.line, argument.text))
    for body in tree.find(ARGUMENT_NODE):
        parent = body.parent
        if parent is None or parent.type not in MACRO_NODES:
            continue
        spans += [
            (token.row + 1, token.text)
            for token in tsast.pp_tokens(body.text, first_row=body.start[0])
            if token.kind == STRING_TOKEN
        ]
    return FileProse(path, split_spans(spans))


def python_prose(path: str, data: bytes) -> FileProse:
    """Return the comments and the strings of one Python file, or its problem when it does not tokenize.

    Complexity: linear in the length of the file.
    """
    try:
        source = data.decode("utf-8").replace("\r\n", "\n").replace("\r", "\n")
        tokens = list(tokenize.generate_tokens(io.StringIO(source).readline))
    except (tokenize.TokenError, SyntaxError, UnicodeDecodeError) as problem:
        return FileProse(path, (), f"the Python file does not tokenize: {problem}")
    return FileProse(
        path, split_spans([(token.start[0], token.string) for token in tokens if token.type in PYTHON_PROSE])
    )


def text_lines(data: bytes) -> tuple[ProseLine, ...]:
    """Return each line of a file with no parser, or no line for a binary file (THE PROSE OF A FILE).

    Complexity: linear in the length of the file.
    """
    if b"\0" in data:
        return ()
    try:
        text = data.decode("utf-8")
    except UnicodeDecodeError:
        return ()
    return tuple(ProseLine(number, line) for number, line in enumerate(text.split("\n"), start=1))


def prose_of(root: Path, paths: Sequence[str]) -> Iterator[FileProse]:
    """Yield the prose of each file, in the sequence of the paths (THE PROSE OF A FILE).

    The C++ files go to the parse in one batch, and the tree cache of tsast
    gives each tree that it holds.  The function keeps the lines of each C++
    file, and no tree.

    Complexity: one read of each file, and one parse of each C++ file that the tree cache does not hold.  Linear in
    the length of the prose.

    Args:
        root: The root of the files
        paths: The files, relative to the root

    Raises:
        tsast.KitMissing: If a path is a C++ file and the pinned kit is not installed
        OSError: If a file cannot be read
    """
    links = {path for path in paths if (root / path).is_symlink()}
    cpp = [path for path in paths if path not in links and tsast.is_in_cpp_scope(path)]
    parsed = {path: cpp_prose(path, tree) for path, tree in zip(cpp, tsast.parse_files(root, cpp), strict=True)}
    for path in paths:
        if path in links:
            yield FileProse(path, (ProseLine(1, str((root / path).readlink())),))
        elif path in parsed:
            yield parsed[path]
        elif path.endswith(PYTHON_SUFFIX):
            yield python_prose(path, (root / path).read_bytes())
        else:
            yield FileProse(path, text_lines((root / path).read_bytes()))
