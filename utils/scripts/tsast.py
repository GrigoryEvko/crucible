#!/usr/bin/env python3
"""Parse this tree's C++ with the pinned tree-sitter kit and walk the result.

Every AST gate imports this module.  It replaces the text scanning that the
older guards do in bash, where a signature is found with a line regex and a
brace is counted by hand.

WHY THE PINNED CLI IS THE ONLY ENGINE
    The kit's C++ grammar declares ABI 1019, and no upstream tree-sitter
    runtime loads that.  The `tree_sitter` package on PyPI carries an upstream
    runtime, so it cannot load the grammar either.  The kit's own CLI is the one
    consumer that can.  `utils/scripts/install-tree-sitter.sh` installs it and this
    module asks that script where it landed.

WHY `parse` AND NOT `query`
    Measured on 2026-09-21 over this tree.  `tree-sitter parse` reads 3,871
    files in 5.98 s, about 2.3 ms for each one.  `tree-sitter query` costs a
    fixed 195 ms for each file, because it compiles the query against the C++
    grammar again for every file.  One process for the whole tree therefore
    takes 6 s through `parse` and 12 minutes through `query`.  The matching
    happens here instead.

THE OUTPUT FORMAT
    `parse` writes one indented S-expression for each file, and it writes no
    file name between them.  Two rules make the stream unambiguous:
      * A line at indentation 0 opens the next file, in the order of the input
        list.  Only a root node sits at indentation 0.
      * Indentation is two spaces for each level of depth, so the indentation
        alone gives the tree shape.
    A file whose parse carries an error also gets one diagnostic line, which
    names the file and the first bad node.  This module collects those.

POSITIONS ARE BYTE COLUMNS
    tree-sitter counts a column in bytes, not in characters.  Text comes from
    the file bytes at `line_start[row] + column`, so a non-ASCII byte earlier on
    the line cannot shift a slice.

WHAT THE KIT CANNOT READ
    Each limit below has a self-test case, so a kit change that moves a limit
    fails the self-test until this text is corrected.
      * A macro body is one `preproc_arg` node that holds raw text.  The kit
        does not parse the body, so a gate that must see a construct inside a
        `#define` reads that text itself.
      * Every arm of an `#if` is parsed, because the kit does not preprocess.
        A gate cannot tell a live arm from a dead one.
      * A deep nest of qualified template-ids fails to parse when a numeric
        template argument such as `Label<1>` sits at its bottom.  The
        self-test holds an 18-level alias of that shape, reduced from a
        generated session type that failed at 21 levels.  The same alias
        parses when its names are unqualified, and it parses when the numeric
        argument is a type.  A chain of plain template-ids parses at 48
        levels.  So the limit comes from the shape, not from the depth alone.
        A generator must emit an alias for each deep sub-type, so that no
        declaration nests that deep.
      * A macro in statement position with a brace body, such as libbpf's
        bpf_object__for_each_program, fails to parse when an argument holds
        `->`.  It parses when the arguments are plain names, so a caller
        passes a member or a local rather than a member access.
      * The files in UNPARSEABLE are not C++.  Each entry names its reason.
        They are out of scope for every C++ scan: cpp_files() and
        is_in_cpp_scope() leave them out, so a guard needs no text fallback.

WHY THE ANONYMOUS TOKENS COME FROM THE GAPS
    The S-expression names only the named nodes.  A keyword such as `inline`
    or `namespace`, and an operator such as `&&` or `<<=`, is an anonymous
    token, so it has no line of its own.  Every anonymous token of a node lies
    in the text between its named children, so Node.tokens() lexes that text
    and descends into each child.  A comment is a named node, so it never
    reaches a gap.
    The alternatives were measured on 2026-09-26 over 4,698 files, with the
    machine under a load average of about 38:
      * `parse` (this module):  12.85 s, 410 MB of output
      * `parse --xml`:          17.93 s, 912 MB of output
      * `parse --cst`:          20.10 s, 1,177 MB of output
    The gap lexer costs nothing at parse time, and it runs only for the nodes
    a guard asks about.

NAMES, SCOPES AND DECLARATIONS
    The helpers after cpp_files() read the tree for the questions that the
    guards ask: the parts of a qualified name, the namespaces that enclose a
    node, namespace aliases and using-declarations, attributes, pragmas,
    calls, contract clauses, parameters and namespace-scope declarations.
    Each one reads nodes and tokens only.  None of them runs a regex over
    source text.
"""

from __future__ import annotations

import os
import re
import subprocess
import sys
import tempfile
from bisect import bisect_right
from collections.abc import Callable, Iterable, Iterator, Sequence
from pathlib import Path
from typing import NamedTuple

from repo_root import REPO_ROOT

# One output line for one node.  The type is non-greedy so that a `MISSING ";"`
# node keeps its quoted literal out of the captured type.  Everything after the
# end position is ignored, including the `text:` field, because a single-line
# text can hold any byte and this module reads text from the file instead.
_NODE_LINE = re.compile(
    r"^( *)(?:([A-Za-z_][A-Za-z_0-9]*): )?\((.+?) \[(\d+), (\d+)\] - \[(\d+), (\d+)\]"
)

# The per-file diagnostic line that `parse` writes when a parse carries an
# error.  It holds the path, the timing and the first bad node.
#
# The CLI pads the path to a column width when the path is short, and it pads
# nothing when the path is long.  So the separator is one or more tabs with any
# number of spaces before them, and the path is everything up to the first tab.
_DIAG_LINE = re.compile(r"^([^\t]+)\t+Parse:\s")

# Files that cannot parse as C++, with the reason.  A gate fails when a file
# OUTSIDE this roster reports an error, and it fails when a file INSIDE it
# parses clean, because a stale entry hides future coverage.  Keyed by path,
# never by line, so an edit above the site cannot drift the key.
UNPARSEABLE: dict[str, str] = {
    "include/crucible/perf/bpf/vmlinux.h": (
        "generated BPF C, not C++ — it declares a field named `operator`"
    ),
}


class ParseError(RuntimeError):
    """A file reported a tree-sitter error and the roster does not admit it."""


class KitMissing(RuntimeError):
    """The pinned tree-sitter kit is not installed."""


class Token(NamedTuple):
    """One preprocessing token with its zero-based row in the file.

    The kind is one of: identifier, number, string, char, punctuator,
    directive, text or other.  A directive is `#` plus a name, for example
    `#define`, and occurs only in code text.  Text is a node that this module
    does not lex, for example a macro body.
    """

    kind: str
    text: str
    row: int


# The C++26 punctuators, longest first, so that one alternation does the
# maximal munch.  `^^` is the reflection operator, and `[:` and `:]` bound a
# splice.  The digraphs are here so that a digraph stays one token.
_PUNCTUATORS = sorted(
    (
        "%:%:", "...", "<=>", "<<=", ">>=", "->*",
        "::", "^^", "[:", ":]", "->", "++", "--", "<<", ">>", "<=", ">=", "==", "!=",
        "&&", "||", "+=", "-=", "*=", "/=", "%=", "&=", "|=", "^=", ".*", "##",
        "%:", "<:", ":>", "<%", "%>",
        "{", "}", "[", "]", "(", ")", "<", ">", ";", ":", ",", ".", "?", "~", "!",
        "+", "-", "*", "/", "%", "^", "&", "|", "=", "#", "@", "$", "\\",
    ),
    key=len,
    reverse=True,
)

# One pass over spliced text.  The order of the alternatives matters: a raw
# string and a prefixed literal start with an identifier character, so they
# come before the identifier.  A number comes before a character literal, so a
# digit separator such as 1'000 stays in the number.  A literal keeps its
# user-defined suffix, as the standard's pp-token does.
_LEXER = re.compile(
    r"""
    (?P<space>[ \t\f\v\r\n]+)
  | (?P<line_comment>//[^\n]*)
  | (?P<block_comment>/\*.*?(?:\*/|\Z))
  | (?P<raw>(?:u8|[uUL])?R"(?P<delim>[^()\\ \t\v\f\n"]{0,16})\(.*?\)(?P=delim)"(?:[^\W\d]\w*)?)
  | (?P<string>(?:u8|[uUL])?"(?:\\.|[^"\\\n])*"(?:[^\W\d]\w*)?)
  | (?P<number>\.?[0-9](?:[eEpP][+-]|['\w.])*)
  | (?P<char>(?:u8|[uUL])?'(?:\\.|[^'\\\n])*'(?:[^\W\d]\w*)?)
  | (?P<identifier>[^\W\d]\w*)
  | (?P<punctuator>"""
    + "|".join(re.escape(p) for p in _PUNCTUATORS)
    + r""")
  | (?P<other>.)
    """,
    re.S | re.X,
)
_SPLICE = re.compile(r"\\\r?\n")
_DIRECTIVE = re.compile(r"#[ \t]*([^\W\d]\w*)")


def _scan(spliced: str, *, directives: bool) -> Iterator[tuple[str, str, int]]:
    """Yield (kind, text, start offset) for each token of text that has no line splices.

    Complexity: O(n) in the length of the text.

    Args:
        spliced: Source text after phase 2
        directives: As for _lex()

    Yields:
        One triple for each token.  Space and comments give none.
    """
    pos = 0
    length = len(spliced)
    while pos < length:
        if directives and spliced[pos] == "#":
            directive = _DIRECTIVE.match(spliced, pos)
            if directive is not None:
                yield "directive", "#" + directive.group(1), pos
                pos = directive.end()
                continue
        # `<::` is `<` then `::` unless a third `:` or a `>` follows.  The
        # same rule keeps `[::` from forming a splice opener.
        if spliced.startswith(("<::", "[::"), pos) and spliced[pos + 3:pos + 4] not in (":", ">", "]"):
            yield "punctuator", spliced[pos], pos
            pos += 1
            continue
        match = _LEXER.match(spliced, pos)
        assert match is not None  # the `other` alternative matches any character
        # lastgroup names the outer group, so a raw string reports `raw`.
        kind = match.lastgroup
        if kind not in ("space", "line_comment", "block_comment"):
            yield "string" if kind == "raw" else str(kind), match.group(0), pos
        pos = match.end()


def _lex(text: str, first_row: int, *, directives: bool) -> list[Token]:
    """Split source text into tokens, after the line splices of phase 2.

    Complexity: O(n) in the length of the text.

    Args:
        text: The source text
        first_row: The zero-based row of the first character of the text
        directives: When true, `#` and the name after it form one directive
            token, as in code text.  When false, `#` stays a punctuator, as in
            the replacement list of a macro, where `#` stringifies.

    Returns:
        The tokens in order.  Space and comments give no token.
    """
    # Map each offset in the spliced text back to the original offset, so a
    # token reports the row it starts on in the file.  breaks[i] is the
    # spliced offset from which splice i is gone, and shifts[i] is the number
    # of original characters removed up to and with splice i.
    pieces: list[str] = []
    breaks: list[int] = []
    shifts: list[int] = []
    cursor = 0
    kept = 0
    removed = 0
    for match in _SPLICE.finditer(text):
        pieces.append(text[cursor:match.start()])
        kept += match.start() - cursor
        removed += match.end() - match.start()
        breaks.append(kept)
        shifts.append(removed)
        cursor = match.end()
    pieces.append(text[cursor:])
    spliced = "".join(pieces)
    newlines = [i for i, char in enumerate(text) if char == "\n"]

    def row_of(offset: int) -> int:
        """Return the row of a spliced offset in the original text."""
        index = bisect_right(breaks, offset)
        original = offset + (shifts[index - 1] if index else 0)
        return first_row + bisect_right(newlines, original - 1)

    return [Token(kind, token, row_of(start)) for kind, token, start in _scan(spliced, directives=directives)]


def pp_tokens(text: str, first_row: int = 0) -> list[Token]:
    """Return the preprocessing tokens of text such as a macro body.

    The splices of phase 2 go first, so a name split by a backslash-newline
    is one identifier.  `#` and `##` stay punctuators.  Comments give no
    token.

    Args:
        text: The text to split
        first_row: The zero-based row of the first character, for the report

    Returns:
        The tokens in order
    """
    return _lex(text, first_row, directives=False)


_KIT_CACHE: Path | None = None


def kit_dir() -> Path:
    """Return the installed kit directory, and cache it for this process.

    Raises:
        KitMissing: If the installer reports that the kit is absent or off pin
    """
    global _KIT_CACHE
    if _KIT_CACHE is not None:
        return _KIT_CACHE
    installer = REPO_ROOT / "utils" / "scripts" / "install-tree-sitter.sh"
    done = subprocess.run(
        ["bash", str(installer), "--print-kit"],
        capture_output=True,
        text=True,
        check=False,
    )
    if done.returncode != 0:
        raise KitMissing(
            "the pinned tree-sitter kit is not installed. "
            "Run: bash utils/scripts/install-tree-sitter.sh\n" + done.stderr.strip()
        )
    _KIT_CACHE = Path(done.stdout.strip())
    return _KIT_CACHE


class Node:
    """One named node of a parsed file.

    A Node is a view over the flat arrays of its Tree.  Nothing is copied, so a
    walk allocates only the view objects it is asked for.
    """

    __slots__ = ("tree", "index")

    def __init__(self, tree: Tree, index: int) -> None:
        """Bind a view to one node index of one tree.

        Args:
            tree: The tree that holds the arrays
            index: The position of this node in those arrays
        """
        self.tree = tree
        self.index = index

    @property
    def type(self) -> str:
        """Return the grammar node type, for example `function_declarator`."""
        return self.tree.types[self.index]

    @property
    def field(self) -> str | None:
        """Return the field name this node fills in its parent, or None."""
        return self.tree.fields[self.index]

    @property
    def start(self) -> tuple[int, int]:
        """Return the start of the node as a zero-based (row, byte column)."""
        return (self.tree.srow[self.index], self.tree.scol[self.index])

    @property
    def end(self) -> tuple[int, int]:
        """Return the end of the node as a zero-based (row, byte column)."""
        return (self.tree.erow[self.index], self.tree.ecol[self.index])

    @property
    def line(self) -> int:
        """Return the one-based line of the node start, for a report."""
        return self.tree.srow[self.index] + 1

    @property
    def text(self) -> str:
        """Return the source text the node spans, decoded as UTF-8."""
        return self.tree.slice(self.start, self.end)

    @property
    def parent(self) -> Node | None:
        """Return the enclosing node, or None for the root."""
        owner = self.tree.parent[self.index]
        return None if owner < 0 else Node(self.tree, owner)

    @property
    def children(self) -> list[Node]:
        """Return the direct children, in source order."""
        return [Node(self.tree, i) for i in self.tree.kids[self.index]]

    def child_by_field(self, name: str) -> Node | None:
        """Return the first direct child that fills a named field.

        Args:
            name: The grammar field name, for example `declarator`

        Returns:
            The child node, or None when no child fills that field
        """
        for i in self.tree.kids[self.index]:
            if self.tree.fields[i] == name:
                return Node(self.tree, i)
        return None

    def children_of_type(self, *types: str) -> list[Node]:
        """Return the direct children whose type is one of the given types.

        Args:
            types: One or more grammar node types

        Returns:
            The matching children, in source order
        """
        want = frozenset(types)
        return [Node(self.tree, i) for i in self.tree.kids[self.index] if self.tree.types[i] in want]

    def descendants(self, *types: str) -> Iterator[Node]:
        """Yield every node below this one whose type is one of the given types.

        The walk is iterative, so a deeply nested header cannot exhaust the
        Python stack.  This tree reaches depth 119.

        Args:
            types: One or more grammar node types

        Yields:
            Each matching node, in source order
        """
        want = frozenset(types)
        stack = list(reversed(self.tree.kids[self.index]))
        while stack:
            i = stack.pop()
            if self.tree.types[i] in want:
                yield Node(self.tree, i)
            stack.extend(reversed(self.tree.kids[i]))

    def ancestor_of_type(self, *types: str) -> Node | None:
        """Return the nearest enclosing node of one of the given types, or None.

        Args:
            types: One or more grammar node types

        Returns:
            The nearest matching ancestor, or None when none encloses this node
        """
        want = frozenset(types)
        owner = self.tree.parent[self.index]
        while owner >= 0:
            if self.tree.types[owner] in want:
                return Node(self.tree, owner)
            owner = self.tree.parent[owner]
        return None

    def lexed(self, skip: Callable[[Node], bool] | None = None) -> list[Token]:
        """Return every token under this node, in source order, with its row.

        The anonymous tokens come from the text between the named children.
        A literal, a comment and a macro body are one token each, because
        the kit does not split them.  The walk descends into every named
        child that `skip` does not refuse.

        Complexity: O(n) in the size of the subtree plus the length of its gaps.

        Args:
            skip: A predicate on a named child.  A child it accepts gives no
                token.  The default refuses comments.

        Returns:
            The tokens in order
        """
        refuse = is_comment if skip is None else skip
        out: list[Token] = []
        self._lex_into(out, refuse)
        return out

    def tokens(self, skip: Callable[[Node], bool] | None = None) -> list[str]:
        """Return the text of every token under this node, in source order.

        Args:
            skip: As for lexed(); the default refuses comments

        Returns:
            The token texts in order
        """
        return [token.text for token in self.lexed(skip)]

    def _lex_into(self, out: list[Token], refuse: Callable[[Node], bool]) -> None:
        """Append the tokens of this node to out.

        Args:
            out: The list that collects the tokens
            refuse: The predicate that removes a named child
        """
        atomic = _ATOMIC.get(self.type)
        if atomic is not None:
            text = self.text
            if text:
                out.append(Token(atomic, text, self.start[0]))
            return
        cursor = self.start
        for child in self.children:
            if child.start > cursor:
                out.extend(_lex(self.tree.slice(cursor, child.start), cursor[0], directives=True))
            if not refuse(child):
                child._lex_into(out, refuse)
            cursor = max(cursor, child.end)
        if self.end > cursor:
            out.extend(_lex(self.tree.slice(cursor, self.end), cursor[0], directives=True))

    def gap_tokens(self) -> list[str]:
        """Return the tokens that lie directly in this node, outside every named child.

        A keyword such as `inline`, `namespace` or `typename`, and the `::`
        of a global qualifier, is a direct token of its node.

        Returns:
            The token texts in order
        """
        return self.tokens(skip=lambda _child: True)

    def __eq__(self, other: object) -> bool:
        """Say whether two views name the same node of the same tree.

        Each walk makes new view objects, so identity (`is`) never compares
        nodes.  Equality does.
        """
        return isinstance(other, Node) and other.tree is self.tree and other.index == self.index

    def __hash__(self) -> int:
        """Return a hash that agrees with __eq__, so a node can key a dict or join a set."""
        return hash((id(self.tree), self.index))

    def __repr__(self) -> str:
        """Return a short form that names the file and the line."""
        return f"<{self.type} {self.tree.path}:{self.line}>"


class Tree:
    """One parsed file, held as flat arrays with Node views over them.

    Complexity: the build is O(n) in the number of nodes, and a `descendants`
    walk is O(n) in the size of the subtree.  The arrays for one file are freed
    when the caller drops the Tree, so a whole-tree scan stays at a few MB.
    """

    __slots__ = (
        "path", "types", "fields", "srow", "scol", "erow", "ecol",
        "parent", "kids", "diagnostic", "_source", "_line_starts", "_comment_rows",
    )

    def __init__(self, path: Path) -> None:
        """Start an empty tree for one file path.

        Args:
            path: The file this tree parses, relative to the repo root
        """
        self.path = path
        self.types: list[str] = []
        self.fields: list[str | None] = []
        self.srow: list[int] = []
        self.scol: list[int] = []
        self.erow: list[int] = []
        self.ecol: list[int] = []
        self.parent: list[int] = []
        self.kids: list[list[int]] = []
        self.diagnostic: str | None = None
        self._source: bytes | None = None
        self._line_starts: list[int] | None = None
        self._comment_rows: dict[int, list[Node]] | None = None

    @property
    def root(self) -> Node:
        """Return the translation unit node."""
        return Node(self, 0)

    @property
    def source(self) -> bytes:
        """Return the file bytes, read once and kept for later slices."""
        if self._source is None:
            # The path is repo-relative, and a gate under ctest runs with the
            # build directory as its working directory, so resolve against the
            # repo root rather than the process cwd.
            self._source = (REPO_ROOT / self.path).read_bytes()
        return self._source

    def _starts(self) -> list[int]:
        """Return the byte offset of each line start, computed once."""
        if self._line_starts is None:
            data = self.source
            starts = [0]
            pos = data.find(b"\n")
            while pos != -1:
                starts.append(pos + 1)
                pos = data.find(b"\n", pos + 1)
            self._line_starts = starts
        return self._line_starts

    def slice(self, start: tuple[int, int], end: tuple[int, int]) -> str:
        """Return the source text between two (row, byte column) positions.

        Args:
            start: The zero-based start position
            end: The zero-based end position

        Returns:
            The spanned text, decoded as UTF-8 with replacement on a bad byte
        """
        starts = self._starts()
        lo = starts[start[0]] + start[1]
        hi = starts[end[0]] + end[1] if end[0] < len(starts) else len(self.source)
        return self.source[lo:hi].decode("utf-8", "replace")

    def line(self, row: int) -> str:
        """Return one source row, without its line break, for a report.

        A guard shows this text to the reader.  It decides nothing from it:
        a comment, a string or a splice makes a row a poor unit of code.

        Args:
            row: The zero-based row

        Returns:
            The row's text, or "" for a row past the end of the file
        """
        starts = self._starts()
        if row < 0 or row >= len(starts):
            return ""
        end = starts[row + 1] if row + 1 < len(starts) else len(self.source)
        return self.source[starts[row]:end].decode("utf-8", "replace").rstrip("\r\n")

    def find(self, *types: str) -> Iterator[Node]:
        """Yield every node in the file whose type is one of the given types.

        Args:
            types: One or more grammar node types

        Yields:
            Each matching node, in source order
        """
        want = frozenset(types)
        for i, node_type in enumerate(self.types):
            if node_type in want:
                yield Node(self, i)

    def __len__(self) -> int:
        """Return the number of named nodes in the file."""
        return len(self.types)

    def __repr__(self) -> str:
        """Return a short form that names the file and the node count."""
        return f"<Tree {self.path} {len(self.types)} nodes>"


def parse(paths: Sequence[Path], *, strict: bool = True) -> Iterator[Tree]:
    """Parse files with the pinned kit and yield one Tree for each of them.

    One CLI process serves the whole list, so the grammar loads once.  Trees
    arrive in the order of `paths`, and each one is freed when the caller moves
    on, which keeps a whole-tree scan inside a few MB.

    Args:
        paths: The files to parse, relative to the repo root or absolute
        strict: When true, raise ParseError for a file that reports a
            tree-sitter error and is not listed in UNPARSEABLE

    Yields:
        One Tree for each input path, in input order

    Raises:
        KitMissing: If the pinned kit is not installed
        ParseError: If strict is true and an unrostered file reports an error
    """
    if not paths:
        return
    kit = kit_dir()
    listing = "\n".join(str(p) for p in paths) + "\n"

    # stderr goes to a file, not a pipe, so a long error text cannot fill a
    # pipe buffer and stall the CLI while this loop reads stdout.
    with tempfile.TemporaryFile(mode="w+", encoding="utf-8", errors="replace") as errors:
        proc = subprocess.Popen(
            [
                str(kit / "bin" / "tree-sitter"), "parse",
                "--lib-path", str(kit / "lib" / "cpp.so"),
                "--lang-name", "cpp",
                "--paths", "/dev/stdin",
            ],
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=errors,
            text=True,
            bufsize=1 << 20,
            cwd=REPO_ROOT,
            env={**os.environ, "NO_COLOR": "1"},
        )
        assert proc.stdin is not None and proc.stdout is not None
        proc.stdin.write(listing)
        proc.stdin.close()

        tree: Tree | None = None
        # open_at[depth] holds the node index that currently owns depth + 1.
        open_at: list[int] = []
        order = iter(paths)
        diagnostics: dict[str, str] = {}
        saw_diagnostic = False
        delivered = 0

        for line in proc.stdout:
            match = _NODE_LINE.match(line)
            if match is None:
                diag = _DIAG_LINE.match(line)
                if diag is not None:
                    diagnostics[diag.group(1).strip()] = line.rstrip("\n")
                    saw_diagnostic = True
                continue
            indent, field, node_type = match.group(1), match.group(2), match.group(3)
            depth = len(indent) >> 1
            if depth == 0:
                if tree is not None:
                    delivered += 1
                    yield _finish(tree, diagnostics, strict)
                tree = Tree(Path(next(order)))
                open_at = []
            assert tree is not None
            index = len(tree.types)
            tree.types.append(node_type)
            tree.fields.append(field)
            tree.srow.append(int(match.group(4)))
            tree.scol.append(int(match.group(5)))
            tree.erow.append(int(match.group(6)))
            tree.ecol.append(int(match.group(7)))
            tree.kids.append([])
            if depth == 0:
                tree.parent.append(-1)
            else:
                owner = open_at[depth - 1]
                tree.parent.append(owner)
                tree.kids[owner].append(index)
            del open_at[depth:]
            open_at.append(index)

        if tree is not None:
            delivered += 1
            yield _finish(tree, diagnostics, strict)
        proc.stdout.close()
        code = proc.wait()
        errors.seek(0)
        error_text = errors.read().strip()

    # The CLI stops at the first path it cannot read and exits 1, after the
    # trees of the paths before it.  So a short count is a tool failure, even
    # when an earlier file carried a parse error.
    if delivered < len(paths):
        raise ParseError(
            f"tree-sitter parse stopped after {delivered} of {len(paths)} files, "
            f"at {paths[delivered]}. The CLI said: {error_text or '(nothing)'}"
        )
    # The CLI exits 1 when any file in the batch carries a parse error, which is
    # a result and not a tool failure.  The roster and the strict policy above
    # already decide what to do about it.  Every other nonzero exit, and a
    # nonzero exit with no diagnostic to explain it, is a real failure.
    if code == 1 and saw_diagnostic:
        return
    if code != 0:
        raise ParseError(
            f"tree-sitter parse exited {code} with no diagnostic to explain it. "
            f"The kit or the file list is wrong. The CLI said: {error_text or '(nothing)'}"
        )


def _finish(tree: Tree, diagnostics: dict[str, str], strict: bool) -> Tree:
    """Attach any diagnostic to a finished tree and apply the strict policy.

    Args:
        tree: The tree that just finished
        diagnostics: Diagnostic lines collected so far, keyed by path
        strict: When true, raise for an unrostered parse error

    Returns:
        The same tree, with its diagnostic attached

    Raises:
        ParseError: If strict is true and this file is not rostered
    """
    key = str(tree.path)
    tree.diagnostic = diagnostics.pop(key, None)
    if tree.diagnostic is not None and strict and key not in UNPARSEABLE:
        raise ParseError(
            f"{key} reports a tree-sitter parse error and is not listed in "
            f"tsast.UNPARSEABLE:\n  {tree.diagnostic}\n"
            "Either the grammar needs the construct, or the file is not C++. "
            "Do not add a roster entry without naming the reason."
        )
    return tree


# The one suffix policy of every C++ scan.  A BPF program is C (`.bpf.c`), and
# the kit reads C++, so no C file is in scope.
CPP_SUFFIXES: tuple[str, ...] = (".h", ".hpp", ".cpp", ".cc")


def is_in_cpp_scope(path: Path | str) -> bool:
    """Say whether a repo-relative path is in scope for a C++ scan.

    A path is in scope when its suffix is in CPP_SUFFIXES and UNPARSEABLE does
    not list it.  A guard that builds its own file list filters it with this
    predicate, so it needs no text fallback for a file that is not C++.

    Args:
        path: A repo-relative path

    Returns:
        True when a C++ scan must read the file
    """
    text = Path(path).as_posix()
    return text.endswith(CPP_SUFFIXES) and text not in UNPARSEABLE


def cpp_files(*roots: str | Path, include_unparseable: bool = False) -> list[Path]:
    """Return every C++ file under the given roots, sorted.

    A root is a repo-relative directory name, or an absolute directory such
    as the scratch tree of a self-test.  A file under the repository comes
    back repo-relative, and a file outside it comes back absolute, so parse()
    reads either.  Sorted order makes a gate's report stable across runs,
    which DetSafe needs.

    Args:
        roots: Directory names, repo-relative or absolute
        include_unparseable: When true, keep the files that UNPARSEABLE lists.
            Only the gate that proves the roster exact needs them.

    Returns:
        The matching paths, in sorted order
    """
    found: list[Path] = []
    for root in roots:
        base = Path(root) if Path(root).is_absolute() else REPO_ROOT / root
        if not base.is_dir():
            continue
        for suffix in CPP_SUFFIXES:
            for path in base.rglob(f"*{suffix}"):
                found.append(path.relative_to(REPO_ROOT) if path.is_relative_to(REPO_ROOT) else path)
    if not include_unparseable:
        found = [p for p in found if p.as_posix() not in UNPARSEABLE]
    return sorted(found)


_WALK_SKIPPED = (".git", ".tools", "build", "cmake-build-", "third_party", "external", "vendor")


def tracked_files(root: Path) -> list[str]:
    """Return the files that git tracks under a root, repo-relative and sorted.

    A guard reads what the commit holds.  An untracked file is out of scope:
    the export and the build of a guard run, or the scratch file of a CMake
    check, can appear under the root while a guard runs and vanish before it
    reads them.  Outside a work tree, for example in the scratch tree of a
    self-test, the walk takes every file and skips each version control,
    tool, build and vendor directory.

    Complexity: linear in the number of files under the root.

    Args:
        root: The root of the tree

    Returns:
        The paths relative to the root, in sorted order
    """
    try:
        out = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--cached"],
                             check=True, capture_output=True).stdout.decode(errors="replace")
        return sorted(path for path in out.split("\0") if path)
    except (OSError, subprocess.CalledProcessError):
        found: list[str] = []
        for directory, dirs, names in os.walk(root):
            dirs[:] = sorted(d for d in dirs if not d.startswith(_WALK_SKIPPED))
            found += [(Path(directory) / name).relative_to(root).as_posix() for name in names]
        return sorted(found)


# ── Tokens and spellings ─────────────────────────────────────────────────────

# The node types that the kit does not split, with the token kind each one is.
_ATOMIC: dict[str, str] = {
    "string_literal": "string",
    "raw_string_literal": "string",
    "char_literal": "char",
    "system_lib_string": "string",
    "number_literal": "number",
    "comment": "comment",
    "preproc_arg": "text",
}


def is_comment(node: Node) -> bool:
    """Say whether a node is a comment.

    Args:
        node: Any node

    Returns:
        True for a comment node
    """
    return node.type == "comment"


def _is_word_char(char: str) -> bool:
    """Say whether a character can continue an identifier or a number."""
    return char.isalnum() or char == "_"


def spelled(node: Node) -> str:
    """Return the canonical spelling of a node: its tokens, comments dropped.

    Two tokens are joined with no space, except two word tokens, which get
    one space so that `unsigned long` does not become one name.  So a
    comment, a line break or extra space inside the node cannot change the
    result.

    Args:
        node: Any node

    Returns:
        The canonical spelling
    """
    parts: list[str] = []
    for text in node.tokens():
        if parts and _is_word_char(parts[-1][-1]) and _is_word_char(text[0]):
            parts.append(" ")
        parts.append(text)
    return "".join(parts)


# ── Names ────────────────────────────────────────────────────────────────────

_NAME_LEAVES = frozenset({
    "identifier", "field_identifier", "type_identifier", "namespace_identifier",
    "statement_identifier", "primitive_type",
})
_SPELLED_NAMES = frozenset({"operator_name", "destructor_name", "operator_cast"})
_TEMPLATE_NAMES = frozenset({"template_type", "template_function", "template_method"})
_DECLARATORS = frozenset({
    "function_declarator", "init_declarator", "pointer_declarator", "reference_declarator",
    "array_declarator", "attributed_declarator",
})


def non_comment_children(node: Node) -> list[Node]:
    """Return the named children of a node that are not comments, in source order.

    Args:
        node: Any node

    Returns:
        The direct named children, without the comment nodes
    """
    return [child for child in node.children if child.type != "comment"]


def _leaf_text(node: Node) -> str:
    """Return the text of a one-token leaf with each line splice removed.

    A backslash-newline inside a name is gone after phase 2, so `led\\<newline>ger`
    is the name `ledger`.
    """
    return _SPLICE.sub("", node.text)


def leaf_name(node: Node) -> str | None:
    """Return the last name in a name, a declarator, a field access or a call.

    `a::b::c` gives `c`, `Box<int>` gives `Box`, `x.template f<T>` gives `f`,
    `int (*p)[3]` gives `p`, `operator<=>` gives `operator<=>` and `C::~C`
    gives `~C`.

    Args:
        node: A name, declarator, field_expression or call_expression node

    Returns:
        The name, or None when the node carries no name
    """
    kind = node.type
    if kind in _NAME_LEAVES:
        return _leaf_text(node)
    if kind in _SPELLED_NAMES:
        return spelled(node)
    if kind in ("qualified_identifier", *_TEMPLATE_NAMES):
        name = node.child_by_field("name")
        return None if name is None else leaf_name(name)
    if kind in ("dependent_name", "dependent_type", "parenthesized_declarator", "variadic_declarator"):
        inner = non_comment_children(node)
        return leaf_name(inner[-1]) if inner else None
    if kind == "field_expression":
        field = node.child_by_field("field")
        return None if field is None else leaf_name(field)
    if kind == "call_expression":
        function = node.child_by_field("function")
        return None if function is None else leaf_name(function)
    if kind in _DECLARATORS:
        inner_decl = node.child_by_field("declarator")
        return None if inner_decl is None else leaf_name(inner_decl)
    return None


def _starts_with_scope_operator(node: Node) -> bool:
    """Say whether a node begins with `::`, before its first named child."""
    named = non_comment_children(node)
    end = named[0].start if named else node.end
    head = _lex(node.tree.slice(node.start, end), node.start[0], directives=False)
    return bool(head) and head[0].text == "::"


def qualified_parts(node: Node) -> tuple[bool, tuple[str, ...]] | None:
    """Return the parts of a name and whether it starts at the global scope.

    `::a::b<int>::c` gives (True, ("a", "b", "c")).  Template arguments and
    comments fall out.  A `decltype` scope, or any shape that is not a name,
    gives None, because no name can be read from it.

    Args:
        node: A name node: an identifier of any kind, qualified_identifier,
            nested_namespace_specifier, a template name, dependent_name,
            operator_name or destructor_name

    Returns:
        (is_global, parts), or None when the node is not a name
    """
    kind = node.type
    if kind in _NAME_LEAVES:
        return (False, (_leaf_text(node),))
    if kind in _SPELLED_NAMES:
        return (False, (spelled(node),))
    if kind in _TEMPLATE_NAMES:
        name = node.child_by_field("name")
        return None if name is None else qualified_parts(name)
    if kind in ("dependent_name", "dependent_type"):
        inner = non_comment_children(node)
        return qualified_parts(inner[-1]) if inner else None
    if kind == "qualified_identifier":
        scope = node.child_by_field("scope")
        name = node.child_by_field("name")
        if name is None:
            return None
        name_parts = qualified_parts(name)
        if name_parts is None:
            return None
        if scope is None:
            return (True, name_parts[1])
        scope_parts = qualified_parts(scope)
        if scope_parts is None:
            return None
        return (scope_parts[0], scope_parts[1] + name_parts[1])
    if kind == "nested_namespace_specifier":
        parts: list[str] = []
        for child in non_comment_children(node):
            inner = qualified_parts(child)
            if inner is None:
                return None
            parts.extend(inner[1])
        return (_starts_with_scope_operator(node), tuple(parts))
    return None


# ── Namespaces ───────────────────────────────────────────────────────────────

_TEST_WORD = re.compile(r"(?:^|_)(?:self_?test|tests?|testing)(?:_|$)", re.IGNORECASE)
_PRIVATE_SUFFIXES = ("_self_test", "_smoke", "_test", "_layout")


def namespace_path(node: Node, *, skip_inline: bool = False) -> tuple[str, ...]:
    """Return the namespaces that enclose a node, outermost first.

    An anonymous namespace is "".  The node itself is not included, so the
    path of a namespace_definition is the path it sits in.

    Args:
        node: Any node
        skip_inline: When true, leave out each inline namespace

    Returns:
        The namespace names in order
    """
    groups: list[list[str]] = []
    owner = node.ancestor_of_type("namespace_definition")
    while owner is not None:
        name = owner.child_by_field("name")
        segments: list[str] = []
        if name is None:
            if not (skip_inline and "inline" in owner.gap_tokens()):
                segments.append("")
        elif name.type == "namespace_identifier":
            if not (skip_inline and "inline" in owner.gap_tokens()):
                segments.append(_leaf_text(name))
        else:
            # `namespace a::inline b` marks one segment of a nested name inline.
            is_inline = False
            for text in name.tokens():
                if text == "inline":
                    is_inline = True
                elif text != "::":
                    if not (skip_inline and is_inline):
                        segments.append(text)
                    is_inline = False
        groups.append(segments)
        owner = owner.ancestor_of_type("namespace_definition")
    return tuple(segment for group in reversed(groups) for segment in group)


def is_test_namespace(path: Sequence[str]) -> bool:
    """Say whether a namespace path names a test namespace.

    A segment is a test segment when one of its words, split at `_`, is
    test, tests, testing, selftest or self_test.

    Args:
        path: A namespace path, as namespace_path() returns it

    Returns:
        True when any segment is a test segment
    """
    return any(_TEST_WORD.search(segment) for segment in path)


def is_private_namespace(path: Sequence[str]) -> bool:
    """Say whether a namespace path is private to the tree.

    A path is private when it is a test namespace, or when a segment is
    anonymous (""), `detail` or `self_test`, or ends in `_self_test`,
    `_smoke`, `_test` or `_layout`.

    Args:
        path: A namespace path, as namespace_path() returns it

    Returns:
        True when the path is private
    """
    if is_test_namespace(path):
        return True
    for segment in path:
        if segment in ("", "detail", "self_test") or segment.endswith(_PRIVATE_SUFFIXES):
            return True
    return False


_SCOPES = ("declaration_list", "compound_statement", "field_declaration_list", "translation_unit")


def _scope_of(node: Node) -> Node:
    """Return the nearest block, namespace body or class body around a node."""
    owner = node.ancestor_of_type(*_SCOPES)
    return node.tree.root if owner is None else owner


class NamespaceAlias(NamedTuple):
    """One `namespace name = target;` definition.

    target holds the parts of the aliased namespace.  scope is the block or
    namespace body that the alias is visible in.
    """

    name: str
    target: tuple[str, ...]
    scope: Node
    node: Node
    is_global: bool = False


def namespace_aliases(tree: Tree) -> list[NamespaceAlias]:
    """Return every namespace alias in a tree, in source order.

    Args:
        tree: A parsed file

    Returns:
        One NamespaceAlias for each alias definition whose target is a name
    """
    found: list[NamespaceAlias] = []
    for node in tree.find("namespace_alias_definition"):
        name = node.child_by_field("name")
        targets = [child for child in non_comment_children(node) if child.field != "name"]
        if name is None or not targets:
            continue
        parts = qualified_parts(targets[0])
        if parts is None:
            continue
        found.append(NamespaceAlias(_leaf_text(name), parts[1], _scope_of(node), node, parts[0]))
    return found


def _encloses(outer: Node, inner: Node) -> bool:
    """Say whether a node's span contains another node of the same tree."""
    return outer.tree is inner.tree and outer.start <= inner.start and inner.end <= outer.end


def resolve_namespace(
    parts: Sequence[str],
    at: Node | None,
    aliases: Sequence[NamespaceAlias],
    *,
    is_global: bool = False,
) -> tuple[str, ...]:
    """Replace a leading namespace alias in a name by the namespace it names.

    An alias applies when its name equals the first part, and it is visible
    at `at`: it lies in the same tree, its scope encloses `at` and it ends
    before `at`.  When `at` is None, every alias applies.  The innermost
    visible alias wins.  The replacement repeats until no alias applies, so
    an alias of an alias resolves, and a cycle stops after 32 steps.

    Args:
        parts: The parts of the name
        at: The node where the name is used, or None
        aliases: The aliases to apply
        is_global: True when the name starts at `::`, which no alias can prefix

    Returns:
        The resolved parts
    """
    current = tuple(parts)
    global_now = is_global
    for _step in range(32):
        if not current or global_now:
            break
        best: NamespaceAlias | None = None
        for alias in aliases:
            if alias.name != current[0]:
                continue
            if at is not None:
                if not _encloses(alias.scope, at) or alias.node.end > at.start:
                    continue
                if best is not None and not _encloses(best.scope, alias.scope):
                    continue
            best = alias
        if best is None:
            break
        current = best.target + current[1:]
        global_now = best.is_global
    return current


def alias_closure(aliases: Sequence[NamespaceAlias], seeds: Iterable[Sequence[str]]) -> frozenset[str]:
    """Return the names of the aliases that denote one of the seed namespaces.

    Each alias target resolves through the other aliases, with no scope
    check, and the alias joins when the result equals a seed.

    Args:
        aliases: The aliases, usually from several trees
        seeds: The full parts of each namespace of interest

    Returns:
        The alias names that stand for a seed namespace
    """
    wanted = {tuple(seed) for seed in seeds}
    names: set[str] = set()
    for alias in aliases:
        if resolve_namespace(alias.target, None, aliases, is_global=alias.is_global) in wanted:
            names.add(alias.name)
    return frozenset(names)


class UsingDecl(NamedTuple):
    """One target of a `using` declaration or directive.

    kind is "namespace" for a using-directive, "enum" for `using enum`, and
    "declaration" otherwise.  A declaration with two targets gives two
    records.
    """

    target: tuple[str, ...]
    is_directive: bool
    scope: Node
    node: Node
    is_global: bool = False
    kind: str = "declaration"


def using_names(tree: Tree) -> list[UsingDecl]:
    """Return every target of every using declaration in a tree, in source order.

    Args:
        tree: A parsed file

    Returns:
        One UsingDecl for each target whose name can be read
    """
    found: list[UsingDecl] = []
    for node in tree.find("using_declaration"):
        direct = node.gap_tokens()
        kind = "namespace" if "namespace" in direct else "enum" if "enum" in direct else "declaration"
        scope = _scope_of(node)
        for child in non_comment_children(node):
            parts = qualified_parts(child)
            if parts is None:
                continue
            found.append(UsingDecl(parts[1], kind == "namespace", scope, node, parts[0], kind))
    return found


# ── Expressions and literals ─────────────────────────────────────────────────

def operator_of(node: Node) -> str:
    """Return the operator of a binary or assignment expression.

    The operator is the text of the tokens between the left and the right
    operand, so a comment beside it cannot change the result.

    Args:
        node: A binary_expression or assignment_expression

    Returns:
        The operator, for example `==` or `<<=`, or "" when the node has no
        left and right operand
    """
    left = node.child_by_field("left")
    right = node.child_by_field("right")
    if left is None or right is None:
        return ""
    return "".join(token.text for token in _lex(node.tree.slice(left.end, right.start), left.end[0], directives=False))


_INTEGER_SUFFIX = re.compile(r"(?:[uU](?:ll|LL|l|L|z|Z)?|(?:ll|LL|l|L|z|Z)[uU]?)$")


def number_value(node: Node) -> int | None:
    """Return the value of an integer literal node.

    Digit separators, the base prefixes 0x, 0b and 0, and the integer
    suffixes are handled.  A floating literal gives None.

    Args:
        node: A number_literal node

    Returns:
        The integer value, or None when the literal is not an integer
    """
    if node.type != "number_literal":
        return None
    text = _leaf_text(node).replace("'", "")
    text = _INTEGER_SUFFIX.sub("", text)
    try:
        if text[:2] in ("0x", "0X"):
            return int(text[2:], 16)
        if text[:2] in ("0b", "0B"):
            return int(text[2:], 2)
        if len(text) > 1 and text[0] == "0" and text.isdigit():
            return int(text, 8)
        return int(text, 10)
    except ValueError:
        return None


# ── Attributes and pragmas ───────────────────────────────────────────────────

class Attribute(NamedTuple):
    """One attribute, from `[[ns::name(args)]]` or `__attribute__((name(args)))`.

    namespace is None for an attribute with no prefix.  A GNU
    `__attribute__` spelling reports the namespace "gnu", because it names
    the same attribute as `[[gnu::name]]`.
    """

    namespace: str | None
    name: str
    arguments: list[Node]
    node: Node

    @property
    def qualified(self) -> str:
        """Return `namespace::name`, or the bare name when there is no namespace."""
        return self.name if self.namespace is None else f"{self.namespace}::{self.name}"


def _attributes_of(holder: Node) -> Iterator[Attribute]:
    """Yield the attributes of one attribute_declaration or attribute_specifier."""
    if holder.type == "attribute_declaration":
        # `[[using gnu: hot, cold]]` puts the namespace on the first attribute
        # as the `namespace` field, and it applies to every attribute in the list.
        shared: str | None = None
        for attribute in holder.children_of_type("attribute"):
            using_namespace = attribute.child_by_field("namespace")
            if using_namespace is not None:
                shared = _leaf_text(using_namespace)
            prefix = attribute.child_by_field("prefix")
            name = attribute.child_by_field("name")
            if name is None:
                continue
            lists = attribute.children_of_type("argument_list")
            arguments = non_comment_children(lists[0]) if lists else []
            yield Attribute(_leaf_text(prefix) if prefix is not None else shared, _leaf_text(name), arguments,
                            attribute)
    elif holder.type == "attribute_specifier":
        for argument_list in holder.children_of_type("argument_list"):
            for item in non_comment_children(argument_list):
                if item.type == "call_expression":
                    function = item.child_by_field("function")
                    lists = item.children_of_type("argument_list")
                    name_text = None if function is None else leaf_name(function)
                    if name_text is not None:
                        yield Attribute("gnu", name_text, non_comment_children(lists[0]) if lists else [], item)
                else:
                    name_text = leaf_name(item)
                    if name_text is not None:
                        yield Attribute("gnu", name_text, [], item)


def attributes(tree: Tree) -> list[Attribute]:
    """Return every attribute in a tree, in source order.

    Args:
        tree: A parsed file

    Returns:
        One Attribute for each attribute in each attribute list
    """
    found: list[Attribute] = []
    for holder in tree.find("attribute_declaration", "attribute_specifier"):
        found.extend(_attributes_of(holder))
    return found


def attribute_names(node: Node) -> frozenset[str]:
    """Return the qualified names of the attributes attached directly to a node.

    `[[no_unique_address]]` gives "no_unique_address", `[[msvc::no_unique_address]]`
    gives "msvc::no_unique_address" and `__attribute__((packed))` gives
    "gnu::packed".

    Args:
        node: A declaration, field_declaration, class specifier or similar node

    Returns:
        The names of the attributes in the attribute lists that are direct
        children of the node
    """
    names: set[str] = set()
    for holder in node.children_of_type("attribute_declaration", "attribute_specifier"):
        names.update(attribute.qualified for attribute in _attributes_of(holder))
    return frozenset(names)


_ESCAPES = {"\\\"": "\"", "\\'": "'", "\\\\": "\\", "\\n": "\n", "\\t": "\t", "\\?": "?"}


def _string_value(literal: Node) -> str:
    """Return the value of a string_literal node with its simple escapes decoded."""
    pieces: list[str] = []
    for child in literal.children:
        if child.type == "string_content":
            pieces.append(child.text)
        elif child.type == "escape_sequence":
            pieces.append(_ESCAPES.get(child.text, child.text))
    return "".join(pieces)


def pragmas(tree: Tree) -> Iterator[tuple[Node, str]]:
    """Yield each `#pragma` directive and each `_Pragma` operator with its text.

    Args:
        tree: A parsed file

    Yields:
        (node, text), where text is the pragma text after `#pragma`, or the
        decoded string of a `_Pragma` call
    """
    for node in tree.find("preproc_call", "pragma_operator"):
        if node.type == "preproc_call":
            directive = node.child_by_field("directive")
            if directive is None or directive.text.replace(" ", "").replace("\t", "") != "#pragma":
                continue
            argument = node.child_by_field("argument")
            yield (node, "" if argument is None else argument.text.strip())
        else:
            literals = list(node.descendants("string_literal"))
            yield (node, _string_value(literals[0]) if literals else "")


# ── Comments, prose and markers ──────────────────────────────────────────────

def prose_nodes(tree: Tree) -> Iterator[Node]:
    """Yield every comment, string literal and raw string literal in a tree.

    A guard that reads prose reads these nodes, never the source text, so a
    word inside code cannot match.

    Args:
        tree: A parsed file

    Yields:
        Each prose node, in source order
    """
    yield from tree.find("comment", "string_literal", "raw_string_literal")


_PROSE_TYPES = frozenset({
    "comment", "string_literal", "raw_string_literal", "string_content", "raw_string_content",
    "char_literal", "system_lib_string",
})


def prose_text(node: Node) -> str:
    """Return the text of a prose node: a comment or a string literal.

    A guard may read the words of a comment or a string with a regex, because
    prose has no C++ structure to lose.  A guard reads that text through this
    function, never through Node.text, so utils/scripts/check-guard-engines.py can
    tell a prose read from a regex over code.

    Args:
        node: A comment, string_literal, raw_string_literal, their content
            nodes, a char_literal or a system_lib_string

    Returns:
        The node's source text

    Raises:
        ValueError: If the node is code, not prose
    """
    if node.type not in _PROSE_TYPES:
        raise ValueError(f"prose_text() reads prose only, and a {node.type} node is code: {node!r}")
    return node.text


def lexeme(item: Token | Node) -> str:
    """Return the spelling of exactly one preprocessing token.

    A guard may test the spelling of one token with a regex or a string
    method, for example a naming convention on one identifier, because one
    token has no structure to lose.  A guard reads that spelling through this
    function, never through Node.text, so utils/scripts/check-guard-engines.py can
    tell a read of one token from a regex over code.  The spelling of a node
    comes after phase 2, so a name that a splice cuts is one name.

    Args:
        item: A Token, or a node that holds exactly one token, such as an
            identifier, a number or an access specifier

    Returns:
        The token's spelling

    Raises:
        ValueError: If the node holds no token or more than one
    """
    if isinstance(item, Token):
        return item.text
    tokens = item.lexed()
    if len(tokens) != 1:
        raise ValueError(f"lexeme() reads one token, and this {item.type} node holds {len(tokens)}: {item!r}")
    return tokens[0].text


def excerpt(node: Node) -> str:
    """Return the first row of a node's text, stripped, for a report.

    A guard shows this text to the reader.  It decides nothing from it.

    Args:
        node: Any node

    Returns:
        The text from the node start to the end of its first row, without
        the space at either end
    """
    return node.text.split("\n", 1)[0].strip()


def comments_by_row(tree: Tree) -> dict[int, list[Node]]:
    """Map each zero-based row to the comment nodes that occupy it.

    A block comment over several rows appears under each of its rows.  The
    map is computed once for each tree.

    Args:
        tree: A parsed file

    Returns:
        Row to comment nodes, in source order
    """
    if tree._comment_rows is None:
        rows: dict[int, list[Node]] = {}
        for comment in tree.find("comment"):
            for row in range(comment.start[0], comment.end[0] + 1):
                rows.setdefault(row, []).append(comment)
        tree._comment_rows = rows
    return tree._comment_rows


_STATEMENTS = frozenset({
    "expression_statement", "return_statement", "declaration", "field_declaration",
    "alias_declaration", "type_definition", "using_declaration", "static_assert_declaration",
    "namespace_alias_definition", "break_statement", "continue_statement", "goto_statement",
    "throw_statement", "co_return_statement", "co_yield_statement", "contract_assert_statement",
    "if_statement", "while_statement", "do_statement", "for_statement", "for_range_loop",
    "switch_statement", "case_statement", "labeled_statement", "try_statement",
    "preproc_def", "preproc_function_def", "preproc_call", "preproc_include",
    "function_definition", "concept_definition", "friend_declaration", "template_instantiation",
})
_BODY_TYPES = frozenset({
    "compound_statement", "field_declaration_list", "declaration_list", "enumerator_list",
    "requirement_seq",
})


def enclosing_statement(node: Node) -> Node:
    """Return the nearest statement or declaration that holds a node.

    A template_declaration is never the answer, so a node in a class
    template gets its member declaration, not the whole template.

    Args:
        node: Any node

    Returns:
        The statement node, or the node itself when no statement holds it
    """
    if node.type in _STATEMENTS:
        return node
    owner = node.ancestor_of_type(*_STATEMENTS)
    return node if owner is None else owner


def _statement_last_row(stmt: Node) -> int:
    """Return the last row of a statement's head: its end, or the row its body opens on."""
    for child in stmt.children:
        if child.type in _BODY_TYPES and child.field in ("body", "consequence"):
            return child.start[0]
    return stmt.end[0]


def has_marker(stmt: Node, marker: str, *, rows_above: int = 0) -> bool:
    """Say whether a comment near a statement holds a marker text.

    A comment counts when it starts on a row from the statement's first row
    to its last row.  For a statement with a body, such as an `if`, the last
    row is the row the body opens on.  With rows_above, a run of comment-only
    rows directly above the statement also counts, up to that many rows.

    Args:
        stmt: A statement, usually from enclosing_statement()
        marker: The text to find inside the comment
        rows_above: How many comment-only rows above the statement count

    Returns:
        True when a counted comment holds the marker
    """
    tree = stmt.tree
    rows = comments_by_row(tree)
    first = stmt.start[0]
    last = _statement_last_row(stmt)
    for row in range(first, last + 1):
        for comment in rows.get(row, ()):
            if comment.start[0] == row and marker in prose_text(comment):
                return True
    row = first - 1
    remaining = rows_above
    # A row above counts only while it holds comments and no code.
    while remaining > 0 and row >= 0 and row in rows and site_key(tree, row) == "":
        if any(marker in prose_text(comment) for comment in rows[row]):
            return True
        remaining -= 1
        row -= 1
    return False


def site_key(tree: Tree, row: int) -> str:
    """Return the content key of a source row: its text minus comments, trimmed.

    A key names a site by its code, not by its line number, so an edit above
    the site cannot move the key.  The comment spans come from the tree, so a
    `//` inside a string stays in the key.

    Args:
        tree: A parsed file
        row: A zero-based row

    Returns:
        The row text with every comment span removed, stripped at both ends
    """
    starts = tree._starts()
    if row >= len(starts):
        return ""
    data = tree.source
    lo = starts[row]
    hi = starts[row + 1] - 1 if row + 1 < len(starts) else len(data)
    keep = bytearray(data[lo:hi])
    for comment in comments_by_row(tree).get(row, ()):
        begin = comment.start[1] if comment.start[0] == row else 0
        end = comment.end[1] if comment.end[0] == row else len(keep)
        keep[begin:end] = b"\0" * (end - begin)
    return keep.replace(b"\0", b"").decode("utf-8", "replace").strip()


# ── Calls, functions and parameters ──────────────────────────────────────────

def calls(tree: Tree, names: Iterable[str] | None = None) -> Iterator[tuple[tuple[str, ...], Node]]:
    """Yield each call expression with the parts of the name it calls.

    `a::b::f(x)` gives ("a", "b", "f"), `obj.f(x)` and `p->f(x)` give ("f",),
    and `x.template f<T>(y)` gives ("f",).  A call through an expression with
    no name, such as a lambda call, is left out.

    Args:
        tree: A parsed file
        names: When given, only calls whose last part is one of these names

    Yields:
        (parts, call_expression node), in source order
    """
    wanted = None if names is None else frozenset(names)
    for node in tree.find("call_expression"):
        function = node.child_by_field("function")
        if function is None:
            continue
        if function.type == "field_expression":
            field = function.child_by_field("field")
            parts = None if field is None else qualified_parts(field)
        else:
            parts = qualified_parts(function)
        if parts is None or not parts[1]:
            continue
        if wanted is not None and parts[1][-1] not in wanted:
            continue
        yield (parts[1], node)


def _function_declarator(node: Node) -> Node | None:
    """Return the function_declarator of a function, a declaration or a declarator."""
    current: Node | None = node
    if current is not None and current.type in ("function_definition", "declaration", "field_declaration"):
        current = current.child_by_field("declarator")
    while current is not None and current.type != "function_declarator":
        if current.type not in _DECLARATORS and current.type != "parenthesized_declarator":
            return None
        inner = current.child_by_field("declarator")
        if inner is None and current.type == "parenthesized_declarator":
            named = non_comment_children(current)
            inner = named[0] if named else None
        current = inner
    return current


def contract_clauses(declarator: Node) -> list[Node]:
    """Return the `pre` and `post` specifiers of a function, in source order.

    Args:
        declarator: A function_declarator, or a function_definition or
            declaration that holds one

    Returns:
        The function_contract_specifier nodes
    """
    function = _function_declarator(declarator)
    return [] if function is None else function.children_of_type("function_contract_specifier")


_PARAMETER_TYPES = (
    "parameter_declaration", "optional_parameter_declaration", "variadic_parameter_declaration",
)


def parameters(function: Node) -> list[tuple[str, Node]]:
    """Return the parameters of a function with their names.

    Args:
        function: A function_definition, a declaration or a function_declarator

    Returns:
        (name, parameter node) for each parameter in order.  An unnamed
        parameter has the name "".
    """
    declarator = _function_declarator(function)
    if declarator is None:
        return []
    parameter_list = declarator.child_by_field("parameters")
    if parameter_list is None:
        return []
    found: list[tuple[str, Node]] = []
    for parameter in parameter_list.children_of_type(*_PARAMETER_TYPES):
        inner = parameter.child_by_field("declarator")
        found.append(("" if inner is None else leaf_name(inner) or "", parameter))
    return found


_CLASS_SPECIFIERS = ("class_specifier", "struct_specifier", "union_specifier")


def enclosing_function(node: Node) -> tuple[str, ...] | None:
    """Return the name of the function whose definition holds a node.

    The name holds the enclosing classes, outermost first, then the parts of
    the declarator name, so a member defined in its class and one defined out
    of line give the same result.  Namespaces are not part of it: use
    namespace_path() for those.

    Args:
        node: Any node

    Returns:
        The parts of the function name, or None when no function definition
        holds the node or its name cannot be read
    """
    owner = node.ancestor_of_type("function_definition")
    if owner is None:
        return None
    declarator = _function_declarator(owner)
    name = None if declarator is None else declarator.child_by_field("declarator")
    parts = None if name is None else qualified_parts(name)
    if parts is None:
        return None
    classes: list[str] = []
    holder = owner.ancestor_of_type(*_CLASS_SPECIFIERS)
    while holder is not None:
        class_name = holder.child_by_field("name")
        if class_name is not None:
            text = leaf_name(class_name)
            if text is not None:
                classes.append(text)
        holder = holder.ancestor_of_type(*_CLASS_SPECIFIERS)
    return tuple(reversed(classes)) + parts[1]


# ── Declarations and templates ───────────────────────────────────────────────

class Declaration(NamedTuple):
    """One name that a declaration at namespace scope introduces.

    kind is one of class, struct, union, enum, function, variable, alias,
    typedef, concept, namespace_alias or using.  scope_parts holds the
    qualifier of a qualified declarator name, for example ("C",) for an
    out-of-line `C::member`.
    """

    ns: tuple[str, ...]
    name: str
    kind: str
    forward_only: bool
    node: Node
    scope_parts: tuple[str, ...] = ()


_CONTAINERS = frozenset({
    "translation_unit", "declaration_list", "preproc_if", "preproc_ifdef", "preproc_else",
    "preproc_elif", "preproc_elifdef",
})


def _name_and_scope(name_node: Node | None) -> tuple[str, tuple[str, ...]]:
    """Split a declared name node into its last part and its qualifier."""
    if name_node is None:
        return ("", ())
    parts = qualified_parts(name_node)
    if parts is None or not parts[1]:
        text = leaf_name(name_node)
        return (text or "", ())
    return (parts[1][-1], parts[1][:-1])


def _declarations_of(item: Node, ns: tuple[str, ...]) -> Iterator[Declaration]:
    """Yield the Declarations of one namespace-scope item."""
    kind = item.type
    if kind == "template_declaration":
        for inner in non_comment_children(item):
            if inner.field != "parameters" and inner.type != "requires_clause":
                yield from _declarations_of(inner, ns)
        return
    if kind in (*_CLASS_SPECIFIERS, "enum_specifier"):
        name, scope = _name_and_scope(item.child_by_field("name"))
        yield Declaration(ns, name, kind.removesuffix("_specifier"), item.child_by_field("body") is None,
                          item, scope)
        return
    if kind == "function_definition":
        declarator = _function_declarator(item)
        name_node = None if declarator is None else declarator.child_by_field("declarator")
        name, scope = _name_and_scope(name_node)
        yield Declaration(ns, name, "function", False, item, scope)
        return
    if kind == "declaration":
        type_node = item.child_by_field("type")
        if type_node is not None and type_node.type in (*_CLASS_SPECIFIERS, "enum_specifier") \
                and type_node.child_by_field("body") is not None:
            yield from _declarations_of(type_node, ns)
        is_extern = any(
            child.type == "storage_class_specifier" and child.text == "extern" for child in item.children
        )
        for declarator in item.children:
            if declarator.field != "declarator":
                continue
            function = _function_declarator(declarator)
            if function is not None:
                name, scope = _name_and_scope(function.child_by_field("declarator"))
                yield Declaration(ns, name, "function", True, item, scope)
            else:
                name, scope = _name_and_scope(declarator)
                yield Declaration(ns, name, "variable", is_extern and declarator.type != "init_declarator",
                                  item, scope)
        return
    if kind == "alias_declaration":
        name, scope = _name_and_scope(item.child_by_field("name"))
        yield Declaration(ns, name, "alias", False, item, scope)
        return
    if kind == "type_definition":
        for declarator in item.children:
            if declarator.field == "declarator":
                name, scope = _name_and_scope(declarator)
                yield Declaration(ns, name, "typedef", False, item, scope)
        return
    if kind == "concept_definition":
        name, scope = _name_and_scope(item.child_by_field("name"))
        yield Declaration(ns, name, "concept", False, item, scope)
        return
    if kind == "namespace_alias_definition":
        name_node = item.child_by_field("name")
        yield Declaration(ns, "" if name_node is None else _leaf_text(name_node), "namespace_alias", False, item)
        return
    if kind == "using_declaration" and "namespace" not in item.gap_tokens():
        for child in non_comment_children(item):
            parts = qualified_parts(child)
            if parts is not None and parts[1]:
                yield Declaration(ns, parts[1][-1], "using", False, item, parts[1][:-1])


def namespace_scope_declarations(tree: Tree) -> Iterator[Declaration]:
    """Yield every name that a declaration at namespace scope introduces.

    The walk enters the translation unit, each namespace body, each
    `extern "C"` body and each arm of a preprocessor conditional.  It does
    not enter a class body or a function body.

    Args:
        tree: A parsed file

    Yields:
        One Declaration for each declared name, in source order
    """
    yield from _walk_namespace_scope(tree.root)


def _walk_namespace_scope(container: Node) -> Iterator[Declaration]:
    """Yield the Declarations of one namespace-scope container, in source order.

    The recursion depth is the nesting depth of namespaces, linkage bodies
    and preprocessor arms, which stays small.
    """
    for item in non_comment_children(container):
        if item.type in _CONTAINERS:
            yield from _walk_namespace_scope(item)
        elif item.type in ("namespace_definition", "linkage_specification"):
            body = item.child_by_field("body")
            if body is None:
                continue
            if body.type == "declaration_list":
                yield from _walk_namespace_scope(body)
            else:
                yield from _declarations_of(body, namespace_path(body))
        else:
            yield from _declarations_of(item, namespace_path(item))


class TemplateDecl(NamedTuple):
    """One template declaration, primary or specialization.

    kind is class, function, variable, alias, concept or other.  An explicit
    specialization has an empty parameter list, and a partial one has a
    non-empty list and a template-id name.
    """

    name: str
    kind: str
    is_specialization: bool
    partial: bool
    node: Node
    scope_parts: tuple[str, ...] = ()


def _template_item(template: Node) -> Node | None:
    """Return the declaration that a template_declaration introduces."""
    for child in non_comment_children(template):
        if child.field == "parameters" or child.type == "requires_clause":
            continue
        return child
    return None


def _is_template_id(node: Node | None) -> bool:
    """Say whether a declared name is a template-id, at its end."""
    while node is not None and node.type == "qualified_identifier":
        node = node.child_by_field("name")
    return node is not None and node.type in _TEMPLATE_NAMES


def specializations(tree: Tree) -> Iterator[TemplateDecl]:
    """Yield every template declaration in a tree, primary and specialization.

    A declaration with two template headers, such as the out-of-line member
    template of a class template, is reported once, for the inner header.

    Args:
        tree: A parsed file

    Yields:
        One TemplateDecl for each template declaration, in source order
    """
    for template in tree.find("template_declaration"):
        item = _template_item(template)
        if item is None or item.type == "template_declaration":
            continue
        params = template.child_by_field("parameters")
        has_params = params is not None and bool(non_comment_children(params))
        name_node: Node | None
        if item.type in (*_CLASS_SPECIFIERS, "enum_specifier"):
            kind = "class"
            name_node = item.child_by_field("name")
        elif item.type in ("declaration", "function_definition", "field_declaration"):
            function = _function_declarator(item)
            if function is not None:
                kind = "function"
                name_node = function.child_by_field("declarator")
            else:
                kind = "variable"
                name_node = item.child_by_field("declarator")
                if name_node is not None and name_node.type == "init_declarator":
                    name_node = name_node.child_by_field("declarator")
        elif item.type == "alias_declaration":
            kind = "alias"
            name_node = item.child_by_field("name")
        elif item.type == "concept_definition":
            kind = "concept"
            name_node = item.child_by_field("name")
        else:
            kind = "other"
            name_node = None
        is_specialization = _is_template_id(name_node)
        name, scope = _name_and_scope(name_node)
        yield TemplateDecl(name, kind, is_specialization, is_specialization and has_params, template, scope)


class SpecializedName(NamedTuple):
    """The template that one specialization specializes, as the declaration spells it.

    kind is class, variable, function or member.  A member is an explicit
    specialization of one member of a class template, such as
    `template <> int X<Fake>::value = 1;`, and its template is the class
    template in the qualifier nearest the member.  target holds the spelled
    parts of the name to that template, with the template as the last part,
    and is_global says whether they start at `::`.

    through_alias is true for an explicit specialization of a member whose
    qualifier holds no template-id, such as `using XF = X<Fake>;` and then
    `template <> int XF::value = 1;`.  Only a specialization of a class
    template has a member to specialize, so the qualifier names one through
    an alias, and target holds the qualifier as spelled.
    """

    template: Node
    kind: str
    is_explicit: bool
    is_global: bool
    target: tuple[str, ...]
    through_alias: bool = False


def _name_chain(node: Node) -> tuple[bool, list[Node]]:
    """Return whether a name starts at `::` and the node of each of its parts, outermost first."""
    parts: list[Node] = []
    is_global = False
    while node.type == "qualified_identifier":
        scope = node.child_by_field("scope")
        if scope is None:
            is_global = True
        else:
            parts.append(scope)
        inner = node.child_by_field("name")
        if inner is None:
            break
        node = inner
    parts.append(node)
    return is_global, parts


def _declared_name(item: Node) -> tuple[str, Node | None]:
    """Return the kind and the declared name of the declaration that a template_declaration introduces."""
    if item.type in (*_CLASS_SPECIFIERS, "enum_specifier"):
        return "class", item.child_by_field("name")
    if item.type in ("declaration", "function_definition", "field_declaration"):
        function = _function_declarator(item)
        if function is not None:
            return "function", function.child_by_field("declarator")
        name = item.child_by_field("declarator")
        while name is not None and name.type in ("init_declarator", "attributed_declarator"):
            name = name.child_by_field("declarator")
        return "variable", name
    return "other", None


def specialized_names(root: Node) -> Iterator[SpecializedName]:
    """Yield each explicit or partial specialization under a node, with the template it specializes.

    A declaration with two template headers, such as `template <> template <>
    void X<int>::f<char>()`, is reported once, for the inner header.  A
    partial specialization of a member is not reported, because only an
    explicit specialization can declare a member of a class template.

    Complexity: linear in the number of nodes under the root.

    Args:
        root: The root of a file, or of a macro body

    Yields:
        One SpecializedName for each specialization, in source order
    """
    for template in root.descendants("template_declaration"):
        item = _template_item(template)
        if item is None or item.type == "template_declaration":
            continue
        params = template.child_by_field("parameters")
        is_explicit = params is None or not non_comment_children(params)
        kind, name = _declared_name(item)
        if name is None or kind == "other":
            continue
        is_global, chain = _name_chain(name)
        texts = [leaf_name(part) or "" for part in chain]
        if chain[-1].type in _TEMPLATE_NAMES:
            yield SpecializedName(template, kind, is_explicit, is_global, tuple(texts))
            continue
        if not is_explicit or len(chain) < 2:
            continue
        for position in range(len(chain) - 2, -1, -1):
            if chain[position].type in _TEMPLATE_NAMES:
                yield SpecializedName(template, "member", True, is_global, tuple(texts[:position + 1]))
                break
        else:
            yield SpecializedName(template, "member", True, is_global, tuple(texts[:-1]), through_alias=True)


class TemplatePrimary(NamedTuple):
    """One declaration of a primary class template or variable template.

    is_definition is true for a class with a body and for a variable with an
    initializer.  A forward declaration defines nothing.
    """

    template: Node
    kind: str
    name: str
    is_definition: bool


def template_primaries(root: Node) -> Iterator[TemplatePrimary]:
    """Yield each declaration of a primary class template or variable template under a node.

    Complexity: linear in the number of nodes under the root.

    Args:
        root: The root of a file

    Yields:
        One TemplatePrimary for each declaration, in source order
    """
    for template in root.descendants("template_declaration"):
        params = template.child_by_field("parameters")
        item = _template_item(template)
        if params is None or not non_comment_children(params) or item is None:
            continue
        kind, name = _declared_name(item)
        if name is None or name.type not in ("type_identifier", "identifier") or kind not in ("class", "variable"):
            continue
        if kind == "class":
            yield TemplatePrimary(template, kind, _leaf_text(name), item.child_by_field("body") is not None)
        else:
            declarator = item.child_by_field("declarator")
            yield TemplatePrimary(template, kind, _leaf_text(name),
                                  declarator is not None and declarator.type == "init_declarator")


def template_functions(root: Node) -> Iterator[tuple[tuple[str, ...], Node]]:
    """Yield the qualified name of each primary function template under a node, with its template declaration.

    A primary has template parameters, so `template <>` is no primary.  A
    declared name that ends in a template-id is a specialization, and it is
    skipped.  A member function template is named through its class.

    Complexity: linear in the number of nodes under the root.

    Args:
        root: The root of a file

    Yields:
        (the qualified name, the template_declaration), in source order
    """
    for template in root.descendants("template_declaration"):
        params = template.child_by_field("parameters")
        item = _template_item(template)
        if params is None or not non_comment_children(params) or item is None:
            continue
        declarator = _function_declarator(item)
        named = None if declarator is None else declarator.child_by_field("declarator")
        if named is None or _is_template_id(named):
            continue
        parts = qualified_parts(named)
        if parts is None or not parts[1]:
            continue
        yield (parts[1] if parts[0] else scope_levels(template)[0] + parts[1]), template


# ── Name lookup ──────────────────────────────────────────────────────────────
#
# A guard that asks which declaration a spelled name refers to cannot compare
# the last part of the name alone: fixy::session::Borrowed and fixy::Borrowed
# are two templates.  The helpers below model the part of C++ name lookup that
# a type name at namespace scope needs.  The kit does not preprocess, so a
# name that a macro of another file forms stays out of reach.
#
#   * A qualified name is a tuple of parts: the enclosing namespaces with no
#     inline namespace, the enclosing classes, and the declared name.
#   * A spelled name is looked up in its enclosing scopes, innermost first.
#     The first scope that declares it gives the answer, so an inner
#     declaration hides an outer one.
#   * A using-declaration adds its name to the scope that holds it.  A
#     using-directive adds the names of its namespace to the innermost
#     namespace that encloses both the directive and that namespace.
#   * A namespace alias of the file resolves first.  A namespace alias at
#     namespace scope of a header resolves when the head of the name is not
#     known in any scope, because each file that includes the header sees it.

_LOCAL_SCOPES = ("compound_statement", "lambda_expression", "requires_expression")
_NAMESPACE_SCOPES = ("translation_unit", "declaration_list")


def qualified_path(node: Node) -> tuple[bool, tuple[str, ...]] | None:
    """Return the parts of the qualified name that ends at a node, and whether it starts at `::`.

    The node can sit anywhere in a qualified name.  In `fixy::Region<int>::wrap`
    the template-id Region<int> is the qualifier of wrap, and its path is
    ("fixy", "Region"): the qualifiers before the node, then the node.

    Args:
        node: A name node: an identifier, a template-id or a qualified name

    Returns:
        (is_global, parts), or None when the node is not a name
    """
    own = qualified_parts(node)
    if own is None:
        return None
    is_global, parts = own[0], list(own[1])
    child, parent = node, node.parent
    while parent is not None and parent.type == "qualified_identifier" and child.field in ("name", "scope"):
        if child.field == "name":
            scope = parent.child_by_field("scope")
            if scope is None:
                is_global = True
            else:
                prefix = qualified_parts(scope)
                if prefix is None:
                    return None
                parts[0:0] = prefix[1]
        child, parent = parent, parent.parent
    return is_global, tuple(parts)


class LookupSite(NamedTuple):
    """The facts that lookup needs about one spelled name, taken from its tree.

    levels holds the scopes that enclose the name, innermost first, down to
    the global scope (). parts is the spelled name with each namespace alias
    of its own file resolved.  usings holds each using-declaration and
    using-directive in force at the name, as (level, is_directive, target).
    """

    levels: tuple[tuple[str, ...], ...]
    parts: tuple[str, ...]
    is_global: bool
    usings: tuple[tuple[tuple[str, ...], bool, tuple[str, ...]], ...]


def _is_local(node: Node) -> bool:
    """Say whether a node lies in a function body, a lambda or a requires-expression."""
    return node.ancestor_of_type(*_LOCAL_SCOPES) is not None


def scope_levels(node: Node) -> tuple[tuple[str, ...], ...]:
    """Return the scopes that enclose a node, innermost first, down to the global scope.

    A scope is a namespace with no inline namespace, or a named class inside
    one.  An unnamed class adds no scope.  The node itself is not a scope of
    its own, so the levels of a class head are the scopes around the class.

    Args:
        node: Any node

    Returns:
        The qualified name of each enclosing scope, innermost first.  The last
        one is ()
    """
    classes: list[tuple[str, ...]] = []
    owner = node.ancestor_of_type(*_CLASS_SPECIFIERS, "namespace_definition")
    while owner is not None and owner.type != "namespace_definition":
        named = owner.child_by_field("name")
        parts = None if named is None else qualified_parts(named)
        if parts is not None:
            classes.append(parts[1])
        owner = owner.ancestor_of_type(*_CLASS_SPECIFIERS, "namespace_definition")
    full = namespace_path(node, skip_inline=True) + tuple(part for group in reversed(classes) for part in group)
    return tuple(full[:count] for count in range(len(full), -1, -1))


def _common_prefix(left: Sequence[str], right: Sequence[str]) -> tuple[str, ...]:
    """Return the longest common prefix of two qualified names."""
    count = 0
    while count < min(len(left), len(right)) and left[count] == right[count]:
        count += 1
    return tuple(left[:count])


def lookup_site(
    node: Node,
    name: Node,
    aliases: Sequence[NamespaceAlias],
    usings: Sequence[UsingDecl],
) -> LookupSite | None:
    """Take the facts that lookup needs about one spelled name.

    Args:
        node: The node where lookup starts, for example the declaration that
            holds the name.  Its enclosing scopes are the levels.
        name: The name node: an identifier, a template-id or a qualified name
        aliases: The namespace aliases of the tree, from namespace_aliases()
        usings: The using-declarations and using-directives of the tree, from
            using_names()

    Returns:
        The facts, or None when the node is not a name
    """
    spelled_parts = qualified_parts(name)
    if spelled_parts is None or not spelled_parts[1]:
        return None
    return lookup_site_of_parts(node, spelled_parts[0], spelled_parts[1], aliases, usings)


def lookup_site_of_parts(
    node: Node,
    is_global: bool,
    parts: tuple[str, ...],
    aliases: Sequence[NamespaceAlias],
    usings: Sequence[UsingDecl],
    levels: tuple[tuple[str, ...], ...] | None = None,
) -> LookupSite:
    """Take the facts that lookup needs about a name given as its parts.

    Use it for a prefix of a qualified name, for example the class template
    in the qualifier of an out-of-line member.

    Args:
        node: The node where lookup starts
        is_global: True when the name starts at `::`
        parts: The spelled parts of the name
        aliases: The namespace aliases of the tree, from namespace_aliases()
        usings: The using-declarations and using-directives of the tree, from
            using_names()
        levels: The enclosing scopes, innermost first, when the tree of the
            node does not hold them, as for a macro body.  None reads them
            from the node.

    Returns:
        The facts
    """
    parts = resolve_namespace(parts, node, aliases, is_global=is_global)
    levels = scope_levels(node) if levels is None else levels
    in_force: list[tuple[tuple[str, ...], bool, tuple[str, ...]]] = []
    for using in usings:
        if not _encloses(using.scope, node) or using.node.end > node.start or using.kind == "enum":
            continue
        target = resolve_namespace(using.target, using.node, aliases, is_global=using.is_global)
        if using.is_directive:
            level = _common_prefix(namespace_path(using.node, skip_inline=True), target)
        elif using.scope.type in _NAMESPACE_SCOPES or using.scope.type == "field_declaration_list":
            level = scope_levels(using.node)[0]
        else:
            level = levels[0]
        in_force.append((level, using.is_directive, target))
    return LookupSite(levels, parts, is_global, tuple(in_force))


class NameIndex:
    """The qualified names that the declarations of many files introduce.

    Each namespace and each prefix of it is a name.  So is each class, enum,
    alias, typedef, concept and variable at namespace or class scope, and
    each class template and variable template.  A function is not a name
    here, because the guards that read this index ask about types.  A
    declaration in a function body is local, so it is not a name either.
    """

    def __init__(self) -> None:
        """Start an index that knows only the namespaces of the standard library.

        No guard parses the standard headers, so a name under std is known to
        be foreign rather than unknown.
        """
        self.names: set[tuple[str, ...]] = {("std",), ("__gnu_cxx",)}
        self.aliases: list[NamespaceAlias] = []

    def add(self, tree: Tree, *, share_aliases: bool = False) -> None:
        """Add the names that one tree declares.

        Complexity: linear in the number of nodes of the tree.

        Args:
            tree: A parsed file
            share_aliases: True for a header, whose namespace aliases at
                namespace scope reach each file that includes it
        """
        for node in tree.find("namespace_definition"):
            if _is_local(node):
                continue
            own = namespace_path(node, skip_inline=True)
            named = node.child_by_field("name")
            if named is not None and "inline" not in node.gap_tokens():
                segments: list[str] = []
                is_inline = False
                for text in named.tokens():
                    if text == "inline":
                        is_inline = True
                    elif text != "::":
                        if not is_inline:
                            segments.append(text)
                        is_inline = False
                own = own + tuple(segments)
            for count in range(1, len(own) + 1):
                self.names.add(own[:count])
        # A specialization declares no name of its own, so a template-id at
        # the end of a declared name adds nothing.  Otherwise a forged
        # `mystery::X<Fake>` would make `mystery::X` known.
        for node in tree.find(*_CLASS_SPECIFIERS, "enum_specifier", "alias_declaration", "concept_definition"):
            named = node.child_by_field("name")
            parts = None if named is None or _is_template_id(named) else qualified_parts(named)
            if parts is not None and parts[1] and not _is_local(node):
                self.names.add(parts[1] if parts[0] else scope_levels(node)[0] + parts[1])
        for node in tree.find("type_definition"):
            if _is_local(node):
                continue
            for declarator in node.children:
                if declarator.field == "declarator" and declarator.type == "type_identifier":
                    self.names.add(scope_levels(node)[0] + (_leaf_text(declarator),))
        for template in tree.find("template_declaration"):
            item = _template_item(template)
            if item is None or item.type != "declaration" or _is_local(template) or _function_declarator(item):
                continue
            declarator = item.child_by_field("declarator")
            if declarator is not None and declarator.type == "init_declarator":
                declarator = declarator.child_by_field("declarator")
            parts = None if declarator is None or _is_template_id(declarator) else qualified_parts(declarator)
            if parts is not None and parts[1]:
                self.names.add(parts[1] if parts[0] else scope_levels(template)[0] + parts[1])
        if share_aliases:
            self.aliases += [alias for alias in namespace_aliases(tree) if alias.scope.type in _NAMESPACE_SCOPES]

    def resolve(self, site: LookupSite, extra: frozenset[tuple[str, ...]] = frozenset()) -> tuple[list[tuple[str, ...]], bool]:
        """Return the declarations that a spelled name refers to, and whether its qualifier is known.

        The first scope, innermost first, that declares the name gives the
        answer.  When two using-directives bring two declarations into that
        scope, both are returned.  A qualifier is known when its head names a
        namespace or a class in some scope.  An unknown qualifier means that
        the name can refer to a declaration that this index does not hold,
        and a guard that fails closed treats it as a match.

        Complexity: linear in the number of levels times the using count.

        Args:
            site: The facts from lookup_site()
            extra: More names, for example the declarations of the file itself

        Returns:
            (the qualified names it refers to, whether the qualifier is known)
        """
        found, known = self._resolve_parts(site, site.parts, extra)
        if not found and not known and not site.is_global:
            shared = resolve_namespace(site.parts, None, self.aliases)
            if shared != site.parts:
                found, known = self._resolve_parts(site, shared, extra)
        return found, known

    def _resolve_parts(
        self, site: LookupSite, parts: tuple[str, ...], extra: frozenset[tuple[str, ...]],
    ) -> tuple[list[tuple[str, ...]], bool]:
        """Resolve one spelling of the name through the levels and the usings of a site."""
        def declared(name: tuple[str, ...]) -> bool:
            return name in self.names or name in extra

        if site.is_global:
            return ([parts] if declared(parts) else []), len(parts) == 1 or declared(parts[:1])
        known = len(parts) == 1
        for level in site.levels:
            hits: list[tuple[str, ...]] = []
            if declared(level + parts):
                hits.append(level + parts)
            if len(parts) > 1 and declared(level + parts[:1]):
                known = True
            for using_level, is_directive, target in site.usings:
                if using_level != level:
                    continue
                if is_directive:
                    if declared(target + parts):
                        hits.append(target + parts)
                    if len(parts) > 1 and declared(target + parts[:1]):
                        known = True
                elif target and target[-1] == parts[0]:
                    known = True
                    if declared(target + parts[1:]):
                        hits.append(target + parts[1:])
            if hits:
                return hits, True
        return [], known


# ── In-memory parses and macro bodies ────────────────────────────────────────

def parse_texts(items: Sequence[tuple[str, str]], *, strict: bool = False) -> Iterator[Tree]:
    """Parse source texts held in memory, one CLI run for all of them.

    Each text goes to a file in a private temporary directory, which the
    generator removes when it finishes.  Each Tree keeps its text, so its
    slices do not read a file, and its path is the label the caller gave.

    Args:
        items: (label, text) pairs.  The label becomes Tree.path.
        strict: When true, raise ParseError for a text that parses with an
            error.  A label is not a roster path, so no text is admitted.

    Yields:
        One Tree for each item, in input order

    Raises:
        ParseError: If strict is true and a text parses with an error
    """
    if not items:
        return
    with tempfile.TemporaryDirectory(prefix="tsast-") as scratch:
        paths: list[Path] = []
        for index, (_label, text) in enumerate(items):
            path = Path(scratch) / f"{index}.cpp"
            path.write_text(text, encoding="utf-8")
            paths.append(path)
        for (label, text), tree in zip(items, parse(paths, strict=False), strict=True):
            tree.path = Path(label)
            tree._source = text.encode("utf-8")
            if strict and tree.diagnostic is not None:
                raise ParseError(f"{label} parses with an error:\n  {tree.diagnostic}")
            yield tree


def macro_parameters(define: Node) -> tuple[str, ...]:
    """Return the parameter names of a function-like macro, in order.

    A variadic macro reports its `...` as `__VA_ARGS__`, the name its body
    uses.  An object-like macro has no parameters.

    Args:
        define: A preproc_function_def or preproc_def node

    Returns:
        The parameter names
    """
    params = define.child_by_field("parameters")
    if params is None:
        return ()
    names: list[str] = []
    for text in params.tokens():
        if text == "...":
            names.append("__VA_ARGS__")
        elif text not in ("(", ")", ","):
            names.append(text)
    return tuple(names)


class MacroBody(NamedTuple):
    """The replacement list of one `#define`, parsed as C++.

    text is the body as the file spells it, with its line splices, so
    pp_tokens(text, first_row) gives each token its row in the file.  joined
    is the same body after phase 2, with each splice removed, and it is what
    the tree parses.  Rows run from first_row to last_row, the row of the
    last token.  define.end is not the last row: the kit ends a definition
    node at column 0 of the row after it.

    tree is the parse of joined inside a wrapper, and root is the node of
    that tree whose children are the body's own nodes, so a walk from root
    never meets the wrapper.  When no wrapper parses clean, tree.diagnostic
    is set: the body is not C++ on its own, for example because it uses `#`
    or `##`, and a guard fails closed on it or reads pp_tokens(text).
    tree.path is a label, not a file: a report names define.tree.path and the
    position that origin() gives.
    """

    define: Node
    name: str
    params: tuple[str, ...]
    text: str
    joined: str
    tree: Tree
    root: Node
    first_row: int
    first_col: int
    last_row: int
    wrapper_rows: int
    positions: tuple[tuple[int, int], ...]

    def origin(self, node: Node) -> tuple[int, int]:
        """Map a node of the body's parse back to a zero-based (row, byte column) in the file.

        joined has no line splices, so one of its rows can span several file
        rows.  positions holds the file position of each character of joined,
        and the node start is looked up there.

        Args:
            node: A node of self.tree

        Returns:
            The position of the node start in the file of the definition
        """
        row, col = node.start
        body_row = row - self.wrapper_rows
        lines = self.joined.split("\n")
        if body_row < 0 or body_row >= len(lines):
            # A node of the wrapper itself: report the start of the body.
            return (self.first_row, self.first_col)
        offset = sum(len(line) + 1 for line in lines[:body_row])
        used = 0
        for char in lines[body_row]:
            if used >= col:
                break
            used += len(char.encode("utf-8"))
            offset += 1
        return self.positions[min(offset, len(self.positions) - 1)]

    @property
    def is_parsed(self) -> bool:
        """Say whether the body parsed clean inside one of the wrappers."""
        return self.tree.diagnostic is None


# The wrappers, in the order they are tried.  Each prefix ends in a newline,
# so the body starts at column 0 of a known row.  The class member list comes
# first: the kit's error recovery reads `public: static T x;` as a clean
# translation unit and drops the access specifier, so a translation unit
# first would lose it.  A namespace-scope body fails in the member list and
# parses as a translation unit.  The block wrapper ends the body with `;`, so
# an expression body becomes an expression statement.
_WRAPPERS: tuple[tuple[str, str, str], ...] = (
    ("field_declaration_list", "struct tsast_macro_wrapper {\n", "\n};\n"),
    ("translation_unit", "\n", "\n"),
    ("compound_statement", "void tsast_macro_wrapper() {\n", "\n;}\n"),
)


def _remove_splices(raw: str, start: tuple[int, int]) -> tuple[str, tuple[tuple[int, int], ...]]:
    """Remove the line splices from a body and record the file position of each kept character.

    Complexity: O(n) in the length of the body.

    Args:
        raw: The body text as the file holds it
        start: The zero-based (row, byte column) of its first character

    Returns:
        (text without splices, one file position for each character of it
        and one more for its end)
    """
    kept: list[str] = []
    positions: list[tuple[int, int]] = []
    row, col = start
    index = 0
    length = len(raw)
    while index < length:
        splice = _SPLICE.match(raw, index)
        if splice is not None:
            row += 1
            col = 0
            index = splice.end()
            continue
        char = raw[index]
        kept.append(char)
        positions.append((row, col))
        if char == "\n":
            row += 1
            col = 0
        else:
            col += len(char.encode("utf-8"))
        index += 1
    positions.append((row, col))
    return "".join(kept), tuple(positions)


def _last_token_row(joined: str, positions: tuple[tuple[int, int], ...], first_row: int) -> int:
    """Return the file row of the last character of the last token of a body.

    Complexity: O(n) in the length of the body.

    Args:
        joined: The body after phase 2
        positions: The file position of each character of joined
        first_row: The row to give when the body has no token

    Returns:
        The zero-based row.  A token that a splice cuts ends on a later row
        than the one it starts on, and the later row is the one given.
    """
    end = 0
    for _kind, token, start in _scan(joined, directives=False):
        end = start + len(token)
    return positions[end - 1][0] if end else first_row


def _body_root(tree: Tree, root_type: str) -> Node:
    """Return the node whose children are a wrapped body's own nodes."""
    if root_type == "translation_unit":
        return tree.root
    found = next(tree.find(root_type), None)
    return tree.root if found is None else found


def macro_bodies(trees: Iterable[Tree]) -> list[MacroBody]:
    """Parse the replacement list of every `#define` in the given trees.

    The value of a definition can span several preproc_arg nodes, because a
    block comment splits it.  The body is the text from the first value to
    the last one.  The parse reads it with each line splice removed as phase
    2 removes it, so a name split by a splice is one name, and
    MacroBody.origin() maps a position back through the removed splices.
    The arguments of #pragma, #error and #warning are not definitions, so
    they never appear.

    Each body is tried as a class member list, then as a translation unit,
    then as a block.  Every body of one stage goes to the kit in one parse.
    A body that parses clean in no wrapper keeps the diagnostic of the last
    one.

    Args:
        trees: Parsed files

    Returns:
        One MacroBody for each definition that has a value, in source order
    """
    pending: list[tuple[Node, str, tuple[str, ...], str, str, int, int, tuple[tuple[int, int], ...]]] = []
    for tree in trees:
        for define in tree.find("preproc_def", "preproc_function_def"):
            values = [child for child in define.children if child.field == "value"]
            if not values:
                continue
            name = define.child_by_field("name")
            text = tree.slice(values[0].start, values[-1].end)
            joined, positions = _remove_splices(text, values[0].start)
            pending.append((define, "" if name is None else _leaf_text(name), macro_parameters(define),
                            text, joined, values[0].start[0], values[0].start[1], positions))
    results: dict[int, MacroBody] = {}
    remaining = list(range(len(pending)))
    for stage, (root_type, prefix, suffix) in enumerate(_WRAPPERS):
        if not remaining:
            break
        last = stage == len(_WRAPPERS) - 1
        items = [(f"macro-{index}", prefix + pending[index][4] + suffix) for index in remaining]
        still: list[int] = []
        for index, tree in zip(remaining, parse_texts(items), strict=True):
            if tree.diagnostic is not None and not last:
                still.append(index)
                continue
            define, name, params, text, joined, row, col, positions = pending[index]
            # The label is not a file.  A report names define.tree.path and origin().
            tree.path = Path(f"{define.tree.path}:{row + 1}:{name}")
            results[index] = MacroBody(define, name, params, text, joined, tree, _body_root(tree, root_type),
                                       row, col, _last_token_row(joined, positions, row),
                                       prefix.count("\n"), positions)
        remaining = still
    return [results[index] for index in range(len(pending))]


def token_qualified_names(tokens: Sequence[Token]) -> list[tuple[bool, tuple[str, ...], int]]:
    """Return every qualified name in a token list, with the row it starts on.

    A name is identifiers joined by `::`, with an optional `::` before the
    first one.  `template` after `::` is skipped.  A template argument list
    ends the name, because the tokens cannot tell a `<` bracket from a less
    sign.  Use it on the tokens of a body that did not parse.

    Args:
        tokens: Tokens from pp_tokens() or Node.lexed()

    Returns:
        (is_global, parts, row) for each name, in order
    """
    names: list[tuple[bool, tuple[str, ...], int]] = []
    index = 0
    count = len(tokens)
    while index < count:
        token = tokens[index]
        starts_global = (
            token.text == "::" and index + 1 < count and tokens[index + 1].kind == "identifier"
            and not (index > 0 and (tokens[index - 1].kind == "identifier" or tokens[index - 1].text == ">"))
        )
        if token.kind != "identifier" and not starts_global:
            index += 1
            continue
        row = token.row
        parts: list[str] = []
        if starts_global:
            index += 1
        while index < count and tokens[index].kind == "identifier":
            parts.append(tokens[index].text)
            nxt = index + 1
            if nxt < count and tokens[nxt].text == "::":
                nxt += 1
                if nxt < count and tokens[nxt].text == "template":
                    nxt += 1
                if nxt < count and tokens[nxt].kind == "identifier":
                    index = nxt
                    continue
            index += 1
            break
        names.append((starts_global, tuple(parts), row))
    return names


def _self_test() -> int:
    """Run the module's own checks, positive and negative.

    Returns:
        0 when every check passes, 2 otherwise
    """
    import tempfile

    failures: list[str] = []
    negatives: list[str] = []

    def check(name: str, ok: bool, *, negative: bool = False) -> None:
        """Record one check result and print it.

        Args:
            name: What the check asserts
            ok: Whether it held
            negative: True for a negative control, which proves a refusal
        """
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)
        if negative:
            negatives.append(name)

    print("tsast --self-test")
    with tempfile.TemporaryDirectory() as work:
        # A fixture with a field, a nested body and a non-ASCII byte before a
        # position the test slices, so a byte column is proved against a
        # character column.
        fixture = Path(work) / "fixture.cpp"
        fixture.write_text(
            "// ééé comment\n"
            "namespace outer {\n"
            "[[nodiscard]] constexpr int mint_thing(int count) noexcept { return count; }\n"
            "}\n",
            encoding="utf-8",
        )
        trees = list(parse([fixture]))
        check("one tree for one path", len(trees) == 1)
        tree = trees[0]
        check("the root is a translation_unit", tree.root.type == "translation_unit")
        check("no diagnostic for a clean fixture", tree.diagnostic is None)

        decls = list(tree.find("function_declarator"))
        check("exactly one function_declarator", len(decls) == 1)
        name = decls[0].child_by_field("declarator")
        check("the declarator field holds the name", name is not None)
        check(
            "the name text is exact after a multi-byte line",
            name is not None and name.text == "mint_thing",
        )
        check("the name reports line 3", name is not None and name.line == 3)
        params = decls[0].child_by_field("parameters")
        check(
            "the parameter list is reachable by field",
            params is not None and params.type == "parameter_list",
        )
        check(
            "the enclosing namespace is found by ancestor_of_type",
            decls[0].ancestor_of_type("namespace_definition") is not None,
        )
        check(
            "noexcept is a child of the declarator",
            len(decls[0].children_of_type("noexcept")) == 1,
        )
        check(
            "the attribute is a descendant of the root",
            len(list(tree.root.descendants("attribute_declaration"))) == 1,
        )
        check("parent of the root is None", tree.root.parent is None)
        check(
            "every non-root node has a parent",
            all(tree.parent[i] >= 0 for i in range(1, len(tree))),
        )

        # Negative control: an unrostered file with an error must raise.
        broken = Path(work) / "broken.cpp"
        broken.write_text("void f() { g(1) { } }\n", encoding="utf-8")
        reason = ""
        try:
            list(parse([broken]))
        except ParseError as exc:
            reason = str(exc)
        check(
            "strict parse raises for an unrostered error, naming the roster",
            "tsast.UNPARSEABLE" in reason,
            negative=True,
        )
        # Positive control: the same file passes when strict is off.
        tolerated = list(parse([broken], strict=False))
        check(
            "strict=False tolerates it and reports the diagnostic",
            len(tolerated) == 1 and tolerated[0].diagnostic is not None,
        )

        def parse_text(name: str, text: str) -> Tree:
            """Parse one source text without the strict policy.

            Args:
                name: The file name inside the scratch directory
                text: The C++ source

            Returns:
                The parsed tree, with its diagnostic if the kit reports one
            """
            path = Path(work) / name
            path.write_text(text, encoding="utf-8")
            return next(parse([path], strict=False))

        # The dialect: each C++26 construct this tree uses gets its own node.
        dialect = parse_text(
            "dialect.cpp",
            "struct S { S(const S&) = delete(\"reason\"); int field = 0; };\n"
            "constexpr int positive(int const x) noexcept pre(x > 0) post(r: r == x)\n"
            "{ contract_assert(x != 0); return x; }\n"
            "template <typename... Ts> using First = Ts...[0];\n"
            "int sum(S const& s) { int total = 0;\n"
            "  template for (constexpr auto m : members(^^S)) { total += s.[:m:]; }\n"
            "  return total; }\n",
        )
        wanted = (
            "delete_method_clause", "function_contract_specifier", "contract_assert_statement",
            "pack_index_specifier", "reflect_expression", "expansion_statement", "splice_expression",
        )
        check(
            "the C++26 dialect parses clean, each construct as its own node",
            dialect.diagnostic is None and all(list(dialect.find(kind)) for kind in wanted),
        )

        # The limits that the module docstring names, one case each.  A kit
        # change that moves a limit fails the case that pins it.
        macro = parse_text("macro.cpp", "#define CAST(p) reinterpret_cast<int*>(p)\n")
        check(
            "limit: a macro body is one preproc_arg with no call inside it",
            len(list(macro.find("preproc_arg"))) == 1
            and not list(macro.find("call_expression", "template_function")),
        )
        loop_body = "  bpf_object__for_each_program(prog, {}) {{\n    use(prog);\n  }}\n"
        check(
            "limit: a brace-body statement macro with `->` in an argument fails",
            parse_text("arrow.cpp", "void f(S* s) {\n" + loop_body.format("s->obj") + "}\n").diagnostic
            is not None,
        )
        check(
            "the same macro with a plain-name argument parses clean",
            parse_text("member.cpp", "void f() {\n" + loop_body.format("obj_") + "}\n").diagnostic is None,
        )
        arms = parse_text("arms.cpp", "#if 0\nint dead_arm();\n#else\nint live_arm();\n#endif\n")
        check(
            "limit: the kit parses both arms of an #if",
            len(list(arms.find("function_declarator"))) == 2,
        )
        plain = "A<" * 48 + "int" + ">" * 48
        check(
            "a 48-level nest of plain template-ids parses clean",
            parse_text("plain48.cpp", f"using T = {plain};\n").diagnostic is None,
        )
        qualified_row = (
            "using G = EnRouteChoice<U, U, U, Branch<U, U, Comm<U, U, Branch<U, U, "
            "Comm<U, U, fg::Branch<U, U, fg::Comm<U, U, fg::Branch<U, U, fg::Comm<U, U, "
            "fg::Branch<U, U, fg::Rec<fg::Comm<U, U, U, fg::Branch<U, U, fg::Comm<U, U, "
            "fg::Branch<U, U, fg::Comm<U, U, U, fg::Branch<Label<1>, U, U> > > > > > > > "
            "> > > >, U > > > > >;\n"
        )
        check(
            "limit: an 18-level qualified nest over a numeric argument fails",
            parse_text("limit.cpp", qualified_row).diagnostic is not None,
        )
        check(
            "the same nest with unqualified names parses clean",
            parse_text("unqualified.cpp", qualified_row.replace("fg::", "")).diagnostic is None,
        )
        check(
            "the same nest with a type argument parses clean",
            parse_text("typed.cpp", qualified_row.replace("Label<1>", "U")).diagnostic is None,
        )

        _self_test_helpers(check, parse_text, Path(work))
        _self_test_macros(check, parse_text)
        _self_test_lookup(check, parse_text)

    # Negative control: a rostered file is admitted, and it really does carry an
    # error, so the roster entry is not stale.
    rostered = Path("include/crucible/perf/bpf/vmlinux.h")
    if (REPO_ROOT / rostered).is_file():
        admitted = list(parse([rostered]))
        check(
            "a rostered file is admitted and still carries its error",
            len(admitted) == 1 and admitted[0].diagnostic is not None,
            negative=True,
        )
        check(
            "cpp_files leaves the rostered file out, unless it is asked for",
            rostered not in cpp_files("include/crucible/perf/bpf")
            and rostered in cpp_files("include/crucible/perf/bpf", include_unparseable=True),
            negative=True,
        )

    if failures:
        print(f"tsast --self-test: FAILED — {len(failures)} of the checks did not hold")
        return 2
    print(f"tsast --self-test: every check passes, {len(negatives)} of them negative controls.")
    return 0


def _self_test_helpers(
    check: Callable[..., None],
    parse_text: Callable[[str, str], Tree],
    work: Path,
) -> None:
    """Check each name, scope, token and declaration helper, positive and negative.

    Args:
        check: The recorder of the enclosing self-test
        parse_text: Parses one source text in the scratch directory
        work: The scratch directory
    """
    # parse(): a path the CLI cannot read stops the batch.  The trees before
    # it arrive, and then the short count raises.
    one = work / "one.cpp"
    one.write_text("int a;\n", encoding="utf-8")
    three = work / "three.cpp"
    three.write_text("int c;\n", encoding="utf-8")
    broken = work / "broken.cpp"
    delivered: list[Tree] = []
    stopped = ""
    try:
        for tree in parse([broken, work / "missing.cpp", three], strict=False):
            delivered.append(tree)
    except ParseError as exc:
        stopped = str(exc)
    check(
        "an unreadable path in a batch raises and names the path, after the trees before it",
        len(delivered) == 1 and "missing.cpp" in stopped and "1 of 3" in stopped,
        negative=True,
    )
    check("a batch of readable paths gives one tree for each", len(list(parse([one, three]))) == 2)

    # Node identity, the public child filter and absolute roots.
    same = parse_text("same.cpp", "int f(int /*c*/ x);\n")
    first_view = next(same.find("parameter_declaration"))
    second_view = next(same.find("parameter_declaration"))
    check(
        "two views of one node are equal and hash equal, so a set holds one",
        first_view == second_view and hash(first_view) == hash(second_view)
        and len({first_view, second_view}) == 1 and first_view is not second_view,
    )
    check(
        "two different nodes are not equal",
        first_view != first_view.parent and first_view != next(same.find("identifier")),
        negative=True,
    )
    check(
        "non_comment_children leaves the comment out",
        [child.type for child in non_comment_children(first_view)] == ["primitive_type", "identifier"],
    )
    planted = work / "planted"
    (planted / "sub").mkdir(parents=True)
    (planted / "a.h").write_text("int a;\n", encoding="utf-8")
    (planted / "sub" / "b.cpp").write_text("int b;\n", encoding="utf-8")
    (planted / "prog.bpf.c").write_text("int c;\n", encoding="utf-8")
    check(
        "cpp_files takes an absolute root, keeps the suffix policy and gives absolute paths",
        cpp_files(planted) == [planted / "a.h", planted / "sub" / "b.cpp"],
    )

    # Tokens and spellings.
    tokens_tree = parse_text(
        "tokens.cpp",
        "int f(int&& x, int /*c*/ y) { return x << 6 && y; }\n"
        "std :: /*c*/ vector < int > v;\n"
        "unsigned\n  long w;\n",
    )
    declarator = next(tokens_tree.find("function_declarator"))
    check(
        "tokens() carries the anonymous tokens of a node",
        "&&" in declarator.tokens() and "(" in declarator.tokens(),
    )
    check("tokens() drops a comment by default", "/*c*/" not in declarator.tokens(), negative=True)
    check(
        "tokens() keeps a comment when skip admits it",
        "/*c*/" in declarator.tokens(skip=lambda _node: False),
    )
    body_ops = [node for node in tokens_tree.find("binary_expression") if operator_of(node) == "&&"]
    check("the operators of a nested expression read apart", len(body_ops) == 1)
    vector_decl = list(tokens_tree.find("declaration"))[0]
    type_node = vector_decl.child_by_field("type")
    check(
        "spelled() drops the comment and the space inside a qualified template name",
        type_node is not None and spelled(type_node) == "std::vector<int>",
    )
    unsigned_long = list(tokens_tree.find("sized_type_specifier"))
    check(
        "spelled() keeps one space between two words, so two keywords stay apart",
        len(unsigned_long) == 1 and spelled(unsigned_long[0]) == "unsigned long",
    )

    # Names.
    names_tree = parse_text(
        "names.cpp",
        "namespace a::b { int x; }\n"
        "namespace cr = ::crucible /*x*/ ::safety;\n"
        "struct C { ~C(); };\n"
        "C::~C() {}\n"
        "auto operator<=>(C const&, C const&) = default;\n"
        "decltype(C{})::type y;\n"
        "int z = a::b::c<int>(1);\n",
    )
    specifiers = list(names_tree.find("nested_namespace_specifier"))
    by_text = {node.text: node for node in specifiers}
    check(
        "qualified_parts reads a leading :: through a comment",
        qualified_parts(by_text["::crucible /*x*/ ::safety"]) == (True, ("crucible", "safety")),
    )
    check(
        "qualified_parts does not call a nested namespace name global",
        qualified_parts(by_text["a::b"]) == (False, ("a", "b")),
        negative=True,
    )
    destructor = [node for node in names_tree.find("qualified_identifier") if node.text == "C::~C"]
    check(
        "leaf_name and qualified_parts read a destructor",
        len(destructor) == 1 and leaf_name(destructor[0]) == "~C"
        and qualified_parts(destructor[0]) == (False, ("C", "~C")),
    )
    spaceship = list(names_tree.find("operator_name"))
    check(
        "leaf_name reads an operator name whole",
        len(spaceship) == 1 and leaf_name(spaceship[0]) == "operator<=>",
    )
    decltype_scope = [node for node in names_tree.find("qualified_identifier") if node.text.startswith("decltype")]
    check(
        "qualified_parts refuses a decltype scope",
        len(decltype_scope) == 1 and qualified_parts(decltype_scope[0]) is None,
        negative=True,
    )
    call = next(names_tree.find("call_expression"))
    check(
        "leaf_name of a call to a template names the template",
        leaf_name(call) == "c" and leaf_name(next(names_tree.find("number_literal"))) is None,
    )

    # Namespaces, aliases and using-declarations.
    scope_tree = parse_text(
        "scope.cpp",
        "namespace a::b { inline namespace v1 { namespace { int hidden; } } }\n"
        "void early() { cs::Refined r0; }\n"
        "namespace cr = crucible;\n"
        "namespace cs = cr::safety;\n"
        "void user() { cs::Refined r1; }\n"
        "using namespace ::crucible::ledger;\n"
        "enum class E { A };\n"
        "void k() { using enum E; }\n"
        "using Alias = int;\n",
    )
    hidden = [node for node in scope_tree.find("identifier") if node.text == "hidden"]
    check(
        "namespace_path reads nested, inline and anonymous namespaces",
        len(hidden) == 1 and namespace_path(hidden[0]) == ("a", "b", "v1", ""),
    )
    check(
        "namespace_path with skip_inline leaves the inline namespace out",
        len(hidden) == 1 and namespace_path(hidden[0], skip_inline=True) == ("a", "b", ""),
    )
    check(
        "namespace_path of a top-level node is empty",
        namespace_path(scope_tree.root) == (),
        negative=True,
    )
    check(
        "is_test_namespace splits words at underscores",
        is_test_namespace(("crucible", "selftest")) and is_test_namespace(("x", "fmt_tests"))
        and not is_test_namespace(("crucible", "contest")),
    )
    check(
        "is_private_namespace admits detail, anonymous and the private suffixes only",
        is_private_namespace(("a", "detail")) and is_private_namespace(("",))
        and is_private_namespace(("x_smoke",)) and not is_private_namespace(("a", "details"))
        and not is_private_namespace(("crucible", "safety")),
    )
    aliases = namespace_aliases(scope_tree)
    uses = [node for node in scope_tree.find("qualified_identifier") if node.text == "cs::Refined"]
    check(
        "resolve_namespace applies an alias of an alias at a later use",
        len(uses) == 2 and resolve_namespace(("cs", "Refined"), uses[1], aliases)
        == ("crucible", "safety", "Refined"),
    )
    check(
        "resolve_namespace does not apply an alias above its definition",
        len(uses) == 2 and resolve_namespace(("cs", "Refined"), uses[0], aliases) == ("cs", "Refined"),
        negative=True,
    )
    check(
        "alias_closure names the aliases of a namespace and no other",
        alias_closure(aliases, [("crucible", "safety")]) == frozenset({"cs"}),
    )
    usings = using_names(scope_tree)
    check(
        "using_names reads a global qualified using-directive",
        any(u.is_directive and u.is_global and u.target == ("crucible", "ledger") for u in usings),
    )
    check(
        "using_names reads `using enum` as its own kind",
        any(u.kind == "enum" and u.target == ("E",) and not u.is_directive for u in usings),
    )
    check(
        "using_names leaves an alias declaration out",
        not any(u.target == ("Alias",) for u in usings),
        negative=True,
    )

    # Expressions and literals.
    expr_tree = parse_text(
        "expr.cpp",
        "bool t = a /* c */ == b;\n"
        "void f() { v <<= 6; g(1); }\n"
        "long n1 = 0x9e37'79b9'7f4a'7c15ULL; int n2 = 0b101; int n3 = 017; double n4 = 1.5;\n",
    )
    operators = {operator_of(node) for node in expr_tree.find("binary_expression", "assignment_expression")}
    check("operator_of reads an operator beside a comment, and a compound one", {"==", "<<="} <= operators)
    check(
        "operator_of of a call is empty",
        operator_of(next(expr_tree.find("call_expression"))) == "",
        negative=True,
    )
    values = [number_value(node) for node in expr_tree.find("number_literal")]
    check(
        "number_value reads separators, suffixes, hex, binary and octal",
        0x9E3779B97F4A7C15 in values and 5 in values and 15 in values,
    )
    check("number_value refuses a floating literal", None in values, negative=True)

    # Attributes and pragmas.
    attr_tree = parse_text(
        "attrs.cpp",
        "struct P { [[msvc::no_unique_address]] int a; [[no_unique_address]] int b; };\n"
        "__attribute__((hot, optimize(\"O3\"))) void h1();\n"
        "[[using gnu: hot, cold]] void h2();\n"
        "#pragma GCC optimize(\"fast-math\")\n"
        "_Pragma(\"GCC optimize(\\\"O3\\\")\")\n"
        "#error stop\n",
    )
    fields = list(attr_tree.find("field_declaration"))
    check(
        "attribute_names keeps a namespace prefix apart from the bare name",
        len(fields) == 2 and attribute_names(fields[0]) == frozenset({"msvc::no_unique_address"})
        and attribute_names(fields[1]) == frozenset({"no_unique_address"}),
    )
    qualified = [attribute.qualified for attribute in attributes(attr_tree)]
    check(
        "attributes reads the GNU spelling and a using-prefix list",
        qualified.count("gnu::hot") == 2 and "gnu::optimize" in qualified and "gnu::cold" in qualified,
    )
    found_pragmas = [text for _node, text in pragmas(attr_tree)]
    check(
        "pragmas reads #pragma and _Pragma, and decodes the operator string",
        found_pragmas == ['GCC optimize("fast-math")', 'GCC optimize("O3")'],
    )
    check("pragmas leaves #error out", all("stop" not in text for text in found_pragmas), negative=True)

    # Comments, prose, markers and keys.
    prose_tree = parse_text(
        "prose.cpp",
        "/* one\n   two\n   three */\n"
        "int test_value = 1;\n"
        "// MARK: above\n"
        "void w() { return; }\n"
        "void u(int x) {\n"
        "  if (cond(x)) {  // MARK: head\n"
        "    // MARK: body\n"
        "    act(x);\n"
        "  }\n"
        "  int v = sink(x);\n"
        "}\n"
        "int y = ::write(1, \"a//b\", 3);  // KEY-NOTE\n",
    )
    rows = comments_by_row(prose_tree)
    check("comments_by_row lists a block comment under each of its rows", all(r in rows for r in (0, 1, 2)))
    prose_texts = [node.text for node in prose_nodes(prose_tree)]
    check(
        "prose_nodes gives comments and strings, not code",
        '"a//b"' in prose_texts and not any("test_value" in text for text in prose_texts),
    )
    first_comment = next(prose_tree.find("comment"))
    code_refused = ""
    try:
        prose_text(next(prose_tree.find("identifier")))
    except ValueError as exc:
        code_refused = str(exc)
    check("prose_text reads a comment", prose_text(first_comment).startswith("/* one"))
    check("prose_text refuses a code node", "reads prose only" in code_refused, negative=True)
    lexeme_tree = parse_text("lexeme.cpp", "int long_na\\\nme = 7;\nclass K { public: int f; };\n")
    spliced_name = next(lexeme_tree.find("identifier"))
    access = next(lexeme_tree.find("access_specifier"))
    check(
        "lexeme reads one identifier with its splice joined, one access specifier and one Token",
        lexeme(spliced_name) == "long_name" and lexeme(access) == "public"
        and lexeme(Token("identifier", "x", 0)) == "x",
    )
    many_refused = ""
    try:
        lexeme(next(lexeme_tree.find("init_declarator")))
    except ValueError as exc:
        many_refused = str(exc)
    check("lexeme refuses a node of several tokens", "reads one token" in many_refused, negative=True)
    declaration = next(lexeme_tree.find("declaration"))
    check(
        "excerpt gives the first row of a node, and Tree.line gives one row without its break",
        excerpt(declaration) == "int long_na\\" and lexeme_tree.line(1) == "me = 7;"
        and lexeme_tree.line(99) == "",
    )
    calls_by_name = {parts[-1]: node for parts, node in calls(prose_tree)}
    head_stmt = enclosing_statement(calls_by_name["cond"])
    check(
        "has_marker reads the head row of an if statement",
        head_stmt.type == "if_statement" and has_marker(head_stmt, "MARK: head"),
    )
    check(
        "has_marker does not read the body of an if statement",
        not has_marker(head_stmt, "MARK: body"),
        negative=True,
    )
    sink_stmt = enclosing_statement(calls_by_name["sink"])
    check(
        "has_marker with rows_above reads a comment-only row, not a code row",
        has_marker(enclosing_statement(next(prose_tree.find("return_statement"))).parent.parent,
                   "MARK: above", rows_above=1)
        and not has_marker(sink_stmt, "MARK", rows_above=2),
    )
    key_row = calls_by_name["write"].start[0]
    check(
        "site_key keeps a // inside a string and drops the comment",
        site_key(prose_tree, key_row) == 'int y = ::write(1, "a//b", 3);',
    )

    # Calls, functions and parameters.
    func_tree = parse_text(
        "funcs.cpp",
        "struct K { void m() { call(1); } void n(); };\n"
        "void K::n() { obj.f(1); a::b::g(2); x.template h<int>(3); [](){}(); }\n"
        "int top = seed(4);\n"
        "constexpr int p(int n, int, auto... rest) noexcept pre(n > 0) post(r: r == n) { return n; }\n",
    )
    call_parts = [parts for parts, _node in calls(func_tree)]
    check(
        "calls reads member, qualified and template-member calls",
        ("f",) in call_parts and ("a", "b", "g") in call_parts and ("h",) in call_parts,
    )
    check(
        "calls leaves out a call through a lambda, and filters by name",
        len(call_parts) == 5 and [p for p, _n in calls(func_tree, ["g"])] == [("a", "b", "g")],
        negative=True,
    )
    owners = {parts[-1]: enclosing_function(node) for parts, node in calls(func_tree)}
    check(
        "enclosing_function gives the same shape in class and out of line",
        owners["call"] == ("K", "m") and owners["f"] == ("K", "n"),
    )
    check("enclosing_function is None outside a function body", owners["seed"] is None, negative=True)
    definitions = list(func_tree.find("function_definition"))
    last = definitions[-1]
    check(
        "contract_clauses and parameters read a function with a pack and an unnamed parameter",
        [clause.text for clause in contract_clauses(last)] == ["pre(n > 0)", "post(r: r == n)"]
        and [name for name, _node in parameters(last)] == ["n", "", "rest"],
    )

    # Declarations and templates.
    decl_tree = parse_text(
        "decls.cpp",
        "struct S;\n"
        "extern \"C\" { int cfun(int); }\n"
        "#if X\nint in_if;\n#else\nint in_else;\n#endif\n"
        "namespace n1 { struct Inner { int member; }; }\n"
        "int C::out_of_line() { return 0; }\n"
        "template <class T> struct Box {};\n"
        "template <> struct trait<int> {};\n"
        "template <class T> struct part<T*> {};\n"
        "template <> struct ns::trait<long> {};\n"
        "template <class T> inline constexpr bool is_x_v = false;\n"
        "template <> inline constexpr bool is_x_v<int> = true;\n",
    )
    declarations = list(namespace_scope_declarations(decl_tree))
    by_name = {(decl.ns, decl.name): decl for decl in declarations}
    check(
        "namespace_scope_declarations reads a forward declaration, a linkage body and both #if arms",
        by_name[((), "S")].forward_only and ((), "cfun") in by_name
        and ((), "in_if") in by_name and ((), "in_else") in by_name,
    )
    check(
        "namespace_scope_declarations does not enter a class body",
        (("n1",), "Inner") in by_name and not any(decl.name == "member" for decl in declarations),
        negative=True,
    )
    check(
        "an out-of-line member keeps its qualifier in scope_parts",
        by_name[((), "out_of_line")].scope_parts == ("C",),
    )
    templates = {(t.name, t.scope_parts, t.is_specialization, t.partial) for t in specializations(decl_tree)}
    check(
        "specializations tells primary, explicit and partial apart, qualified or not",
        {("Box", (), False, False), ("trait", (), True, False), ("part", (), True, True),
         ("trait", ("ns",), True, False), ("is_x_v", (), False, False), ("is_x_v", (), True, False)}
        <= templates,
    )

    # pp_tokens and the scope policy.
    spliced = pp_tokens("a <::std [::x] start_life\\\ntime_as\nnext", 7)
    texts = [token.text for token in spliced]
    check(
        "pp_tokens joins a spliced name and keeps its first row",
        "start_lifetime_as" in texts and spliced[texts.index("start_lifetime_as")].row == 7,
    )
    check(
        "pp_tokens reports the row after a splice and a newline",
        spliced[texts.index("next")].row == 9,
    )
    check(
        "pp_tokens splits <:: and [:: as the standard does",
        texts[:3] == ["a", "<", "::"] and texts[4:6] == ["[", "::"],
    )
    check(
        "is_in_cpp_scope refuses a C file and a rostered file",
        is_in_cpp_scope("include/x.h") and not is_in_cpp_scope("src/prog.bpf.c")
        and not is_in_cpp_scope("include/crucible/perf/bpf/vmlinux.h"),
        negative=True,
    )


def _self_test_macros(check: Callable[..., None], parse_text: Callable[[str, str], Tree]) -> None:
    """Check the in-memory parse, the macro-body parse and the token name reader.

    Args:
        check: The recorder of the enclosing self-test
        parse_text: Parses one source text in the scratch directory
    """
    texts = list(parse_texts([("first.h", "int a;\n"), ("second.h", "void f() { g(1) { } }\n")]))
    check(
        "parse_texts labels each tree and slices its text from memory",
        [str(tree.path) for tree in texts] == ["first.h", "second.h"]
        and next(texts[0].find("identifier")).text == "a",
    )
    check("parse_texts reports a broken text as a diagnostic", texts[1].diagnostic is not None)
    refused = ""
    try:
        list(parse_texts([("bad.h", "void f() { g(1) { } }\n")], strict=True))
    except ParseError as exc:
        refused = str(exc)
    check("parse_texts with strict raises and names the label", "bad.h" in refused, negative=True)

    macros_tree = parse_text(
        "macros.cpp",
        "#define WRAP(fixy) (fixy + 1) /* c */ \\\n"
        "    - reinterpret_cast<int*>(fixy)\n"
        "#define MEMBER(T) public: static T slot_;\n"
        "#define DECL(T) template <> struct trait<T> : std::true_type {};\n"
        "#define PASTE(a, b) a ## b\n"
        "#define VARIADIC(fmt, ...) log(fmt, __VA_ARGS__)\n"
        "#define EMPTY\n"
        "#pragma GCC optimize(\"fast-math\")\n"
        "#error reinterpret_cast<int*>(p)\n",
    )
    bodies = {body.name: body for body in macro_bodies([macros_tree])}
    check(
        "macro_bodies gives one body for each definition with a value, and none for a pragma or #error",
        sorted(bodies) == ["DECL", "MEMBER", "PASTE", "VARIADIC", "WRAP"],
    )
    wrap = bodies["WRAP"]
    casts = [node for node in wrap.root.descendants("template_function") if leaf_name(node) == "reinterpret_cast"]
    check(
        "a body split by a block comment and a splice parses as one expression",
        wrap.is_parsed and len(casts) == 1,
    )
    check(
        "origin maps a node of the body back to its row and column in the file",
        len(casts) == 1 and wrap.origin(casts[0]) == (1, 6),
    )
    check(
        "macro_parameters reads the parameters, and __VA_ARGS__ for a variadic macro",
        wrap.params == ("fixy",) and bodies["VARIADIC"].params == ("fmt", "__VA_ARGS__"),
    )
    member = bodies["MEMBER"]
    check(
        "a member declaration body parses in the class wrapper",
        member.is_parsed and member.root.type == "field_declaration_list"
        and len(list(member.root.descendants("field_declaration"))) == 1,
    )
    decl = bodies["DECL"]
    check(
        "a specialization body parses, and the specialization is found",
        decl.is_parsed and any(t.is_specialization for t in specializations(decl.tree)),
    )
    namespace_tree = parse_text("nsmacro.cpp", "#define OPEN_NS namespace crucible::detail { int hidden; }\n")
    open_ns = macro_bodies([namespace_tree])
    check(
        "a namespace-scope body fails the member list and parses as a translation unit",
        len(open_ns) == 1 and open_ns[0].is_parsed and open_ns[0].root.type == "translation_unit",
    )
    paste = bodies["PASTE"]
    check(
        "a body with ## parses in no wrapper, and its tokens stay readable",
        not paste.is_parsed and "##" in [token.text for token in pp_tokens(paste.text)],
        negative=True,
    )
    check(
        "the wrapper's own names are not in the body's walk",
        not any("tsast_macro_wrapper" in node.text
                for body in bodies.values() for node in body.root.descendants("identifier", "type_identifier")),
        negative=True,
    )

    split_tree = parse_text(
        "splitname.cpp",
        "#define OWNER struct host::In\\\nitOwner { int field; };\n"
        "int crucible::led\\\nger::entry = 0;\n",
    )
    split_bodies = macro_bodies([split_tree])
    split_class = [node for node in split_bodies[0].root.descendants("struct_specifier")] if split_bodies else []
    split_name = split_class[0].child_by_field("name") if split_class else None
    check(
        "a splice inside a name in a macro body joins the name, so the class body is seen",
        len(split_class) == 1 and split_class[0].child_by_field("body") is not None
        and split_name is not None and qualified_parts(split_name) == (False, ("host", "InitOwner")),
    )
    check(
        "origin maps a node after a removed splice to its row and column in the file",
        len(split_class) == 1 and split_bodies[0].origin(split_class[0].child_by_field("body")) == (1, 8),
    )
    spliced_decl = [node for node in split_tree.find("qualified_identifier")
                    if node.parent is not None and node.parent.type != "qualified_identifier"]
    check(
        "qualified_parts and leaf_name join a splice inside a leaf name",
        len(spliced_decl) == 1 and qualified_parts(spliced_decl[0]) == (False, ("crucible", "ledger", "entry"))
        and leaf_name(spliced_decl[0]) == "entry",
    )

    spliced_ns = parse_text(
        "splicens.cpp",
        "namespace cru\\\ncible { namespace ali\\\nas = ::cru\\\ncible; [[gn\\\nu::ho\\\nt]] int value; }\n",
    )
    spliced_value = next((node for node in spliced_ns.find("identifier") if node.text == "value"), None)
    spliced_alias = namespace_aliases(spliced_ns)
    check(
        "namespace_path, namespace_aliases and attributes join a splice inside a name",
        spliced_value is not None and namespace_path(spliced_value) == ("crucible",)
        and [(alias.name, alias.target) for alias in spliced_alias] == [("alias", ("crucible",))]
        and [attribute.qualified for attribute in attributes(spliced_ns)] == ["gnu::hot"],
    )

    rows_tree = parse_text(
        "macrorows.cpp",
        "#define OPENAT(dir, path) ::open\\\nat(dir, path, 0)\n"
        "// marker on the row after the definition\n"
        "#define ONE_ROW(x) (x + 1) // trailing note\n"
        "#define TAIL value_\\\nend\n",
    )
    rows = {body.name: body for body in macro_bodies([rows_tree])}
    openat = rows.get("OPENAT")
    openat_tokens = [] if openat is None else pp_tokens(openat.text, openat.first_row)
    check(
        "a splice inside a name in a macro body gives one token with its file row",
        openat is not None and ("identifier", "openat", 0) in openat_tokens
        and "open" not in [token.text for token in openat_tokens],
    )
    openat_calls = [] if openat is None else [
        qualified_parts(call.child_by_field("function")) for call in openat.root.descendants("call_expression")
    ]
    check(
        "the parse of a spliced body calls the joined name",
        (True, ("openat",)) in openat_calls,
    )
    check(
        "last_row is the row of the last token of a body that spans two rows",
        openat is not None and openat.first_row == 0 and openat.last_row == 1,
    )
    check(
        "last_row is not the row after the definition, where the definition node ends",
        openat is not None and openat.define.end[0] == 2 and openat.last_row != openat.define.end[0],
        negative=True,
    )
    check(
        "last_row of a one-row body is its first row, and a trailing comment adds no row",
        "ONE_ROW" in rows and rows["ONE_ROW"].last_row == rows["ONE_ROW"].first_row == 3,
    )
    check(
        "last_row of a body whose last token a splice cuts is the row the token ends on",
        "TAIL" in rows and rows["TAIL"].last_row == 5
        and [token.text for token in pp_tokens(rows["TAIL"].text)] == ["value_end"],
    )

    names = token_qualified_names(pp_tokens("::foundation::effects::X y = a::template b<int>::c; A<T>::z", 3))
    check(
        "token_qualified_names reads a global name, skips `template` and stops at a template argument list",
        (True, ("foundation", "effects", "X"), 3) in names and (False, ("a", "b"), 3) in names,
    )
    check(
        "token_qualified_names does not call `::` after `>` global",
        not any(is_global and parts == ("z",) for is_global, parts, _row in names),
        negative=True,
    )


def _self_test_lookup(check: Callable[..., None], parse_text: Callable[[str, str], Tree]) -> None:
    """Check NameIndex and lookup_site against the lookup rules, positive and negative.

    Args:
        check: The recorder of the enclosing self-test
        parse_text: Parses one source text into a Tree
    """
    header = parse_text(
        "lookup_header.h",
        "namespace fixy {\n"
        "template <class T, class S, class Brand = int> class Borrowed;\n"
        "namespace session { template <class T, class S> class Borrowed; }\n"
        "inline namespace v1 { template <class T> struct Versioned {}; }\n"
        "struct Outer { template <class T> struct Inner {}; };\n"
        "template <class T> inline constexpr bool is_thing_v = false;\n"
        "}\n"
        "namespace fx = ::fixy;\n",
    )
    index = NameIndex()
    index.add(header, share_aliases=True)
    check(
        "the index holds a nested template, a class member template, a variable template and a namespace",
        {("fixy", "Borrowed"), ("fixy", "session", "Borrowed"), ("fixy", "Outer", "Inner"),
         ("fixy", "is_thing_v"), ("fixy", "session")} <= index.names,
    )
    check("an inline namespace adds no part", ("fixy", "Versioned") in index.names
          and ("fixy", "v1", "Versioned") not in index.names)
    forged = NameIndex()
    forged.add(parse_text("lookup_forged.cpp", "template <> struct mystery::Planted<int> {};\n"
                                               "template <> inline constexpr bool mystery::planted_v<int> = true;\n"))
    check("a specialization declares no name, so its qualifier stays unknown",
          not any(name[:1] == ("mystery",) for name in forged.names), negative=True)

    user = parse_text(
        "lookup_user.cpp",
        "namespace fixy::session { Borrowed<int, int> inner; }\n"
        "namespace fixy { Borrowed<int, int> outer; }\n"
        "namespace t { using namespace fixy; Borrowed<int, int> directive; }\n"
        "namespace u { using fixy::session::Borrowed; Borrowed<int, int> declared; }\n"
        "namespace fs = fixy::session;\n"
        "fs::Borrowed<int, int> aliased;\n"
        "fx::Borrowed<int, int> shared_alias;\n"
        "mystery::Borrowed<int, int> unknown_head;\n"
        "std::Borrowed<int, int> foreign;\n"
        "::fixy::Versioned<int> versioned;\n",
    )
    aliases = namespace_aliases(user)
    usings = using_names(user)
    found: dict[int, tuple[list[tuple[str, ...]], bool]] = {}
    for node in user.find("template_type"):
        named = node.child_by_field("name")
        if named is None or leaf_name(named) not in ("Borrowed", "Versioned"):
            continue
        holder = node.parent if node.parent is not None and node.parent.type == "qualified_identifier" else node
        site = lookup_site(holder, holder, aliases, usings)
        if site is not None:
            found[node.line] = index.resolve(site)
    check("an inner declaration hides the outer one", found.get(1) == ([("fixy", "session", "Borrowed")], True))
    check("the enclosing namespace declares the outer one", found.get(2) == ([("fixy", "Borrowed")], True))
    check("a using-directive at namespace scope reaches the nominated namespace",
          found.get(3) == ([("fixy", "Borrowed")], True))
    check("a using-declaration names the one it declares",
          found.get(4) == ([("fixy", "session", "Borrowed")], True))
    check("a namespace alias of the file resolves", found.get(6) == ([("fixy", "session", "Borrowed")], True))
    check("a namespace alias of a header resolves", found.get(7) == ([("fixy", "Borrowed")], True))
    check("an unknown qualifier head resolves to nothing and says so", found.get(8) == ([], False), negative=True)
    check("a name under std resolves to nothing, with a known qualifier", found.get(9) == ([], True), negative=True)
    check("a global name finds a template in an inline namespace", found.get(10) == ([("fixy", "Versioned")], True))
    levels = scope_levels(next(header.find("template_declaration")))
    check("the levels of a namespace member end at the global scope", levels == (("fixy",), ()))
    paths = parse_text("qualified_path.cpp", "auto a = fixy::Region<int>::wrap(1);\n"
                                             "::fixy::session::Borrowed<int> b;\n")
    found_paths = [qualified_path(node) for node in paths.find("template_type")]
    check("qualified_path reads the qualifiers before a template-id in the scope and in the name of a qualified name",
          found_paths == [(False, ("fixy", "Region")), (True, ("fixy", "session", "Borrowed"))])

    shapes = parse_text(
        "specialized.cpp",
        "template <class T> struct Primary { static constexpr bool value = false; };\n"
        "template <class T> struct Declared;\n"
        "template <class T> inline constexpr bool gate_v = false;\n"
        "template <> struct ::ns::Primary<int> {};\n"
        "template <class T> struct ns::Primary<T*> {};\n"
        "template <> inline constexpr bool ns::gate_v<Fake> = true;\n"
        "template <class T> inline constexpr bool gate_v<T*> = true;\n"
        "template <> constexpr bool ns::Primary<Fake>::value = true;\n"
        "template <> void ns::Primary<Fake>::run() {}\n"
        "template <> template <> void ns::Outer<int>::run<char>() {}\n"
        "template <> void mint_door<Tag>() {}\n"
        "template <> const bool FakeGate::value = true;\n"
        "template <> bool ns::FakeGate::admits() { return true; }\n",
    )
    found_names = [(name.template.line, name.kind, name.is_explicit, name.is_global, name.target, name.through_alias)
                   for name in specialized_names(shapes.root)]
    check("specialized_names reads a class, a variable, a member, a function and a member through an alias",
          found_names == [(4, "class", True, True, ("ns", "Primary"), False),
                          (5, "class", False, False, ("ns", "Primary"), False),
                          (6, "variable", True, False, ("ns", "gate_v"), False),
                          (7, "variable", False, False, ("gate_v",), False),
                          (8, "member", True, False, ("ns", "Primary"), False),
                          (9, "member", True, False, ("ns", "Primary"), False),
                          (10, "function", True, False, ("ns", "Outer", "run"), False),
                          (11, "function", True, False, ("mint_door",), False),
                          (12, "member", True, False, ("FakeGate",), True),
                          (13, "member", True, False, ("ns", "FakeGate"), True)])
    primaries = [(item.template.line, item.kind, item.name, item.is_definition)
                 for item in template_primaries(shapes.root)]
    check("template_primaries reads a class body and a variable initializer as definitions, and a forward "
          "declaration as none",
          primaries == [(1, "class", "Primary", True), (2, "class", "Declared", False),
                        (3, "variable", "gate_v", True)], negative=True)
    functions = parse_text(
        "functions.cpp",
        "namespace ns {\n"
        "template <class T> constexpr bool holds() { return true; }\n"
        "template <class T> requires (sizeof(T) > 0) bool checked(T value);\n"
        "struct Door { template <class T> static bool admits(); };\n"
        "template <> constexpr bool holds<int>() { return false; }\n"
        "consteval bool plain(int) { return true; }\n"
        "}\n",
    )
    function_names = [name for name, _template in template_functions(functions.root)]
    check("template_functions reads a primary, a constrained and a member function template, and no "
          "specialization or plain function",
          function_names == [("ns", "holds"), ("ns", "checked"), ("ns", "Door", "admits")], negative=True)


if __name__ == "__main__":
    # Exit 3 when the kit is absent, so ctest reports a SKIP with the install
    # instruction rather than a failure. CI installs the kit, so CI enforces.
    try:
        if len(sys.argv) > 1 and sys.argv[1] == "--self-test":
            sys.exit(_self_test())
        print(__doc__)
        print(f"kit: {kit_dir()}")
    except KitMissing as exc:
        print(f"tsast: {exc}", file=sys.stderr)
        sys.exit(3)
