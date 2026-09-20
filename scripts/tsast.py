#!/usr/bin/env python3
"""Parse this tree's C++ with the pinned tree-sitter kit and walk the result.

Every AST gate imports this module.  It replaces the text scanning that the
older guards do in bash, where a signature is found with a line regex and a
brace is counted by hand.

WHY THE PINNED CLI IS THE ONLY ENGINE
    The kit's C++ grammar declares ABI 1019, and no upstream tree-sitter
    runtime loads that.  The `tree_sitter` package on PyPI carries an upstream
    runtime, so it cannot load the grammar either.  The kit's own CLI is the one
    consumer that can.  `scripts/install-tree-sitter.sh` installs it and this
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
"""

from __future__ import annotations

import re
import subprocess
import sys
from collections.abc import Iterator, Sequence
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

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
    "src/perf/LockContention.cpp": "libbpf statement macro with a brace body",
    "src/perf/SchedSwitch.cpp": "libbpf statement macro with a brace body",
    "src/perf/SchedTpBtf.cpp": "libbpf statement macro with a brace body",
    "src/perf/SenseHub.cpp": "libbpf statement macro with a brace body",
    "src/perf/SyscallLatency.cpp": "libbpf statement macro with a brace body",
    "src/perf/SyscallTpBtf.cpp": "libbpf statement macro with a brace body",
}


class ParseError(RuntimeError):
    """A file reported a tree-sitter error and the roster does not admit it."""


class KitMissing(RuntimeError):
    """The pinned tree-sitter kit is not installed."""


_KIT_CACHE: Path | None = None


def kit_dir() -> Path:
    """Return the installed kit directory, and cache it for this process.

    Raises:
        KitMissing: If the installer reports that the kit is absent or off pin
    """
    global _KIT_CACHE
    if _KIT_CACHE is not None:
        return _KIT_CACHE
    installer = REPO_ROOT / "scripts" / "install-tree-sitter.sh"
    done = subprocess.run(
        ["bash", str(installer), "--print-kit"],
        capture_output=True,
        text=True,
        check=False,
    )
    if done.returncode != 0:
        raise KitMissing(
            "the pinned tree-sitter kit is not installed. "
            "Run: bash scripts/install-tree-sitter.sh\n" + done.stderr.strip()
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
        "parent", "kids", "diagnostic", "_source", "_line_starts",
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

    @property
    def root(self) -> Node:
        """Return the translation unit node."""
        return Node(self, 0)

    @property
    def source(self) -> bytes:
        """Return the file bytes, read once and kept for later slices."""
        if self._source is None:
            self._source = self.path.read_bytes()
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

    proc = subprocess.Popen(
        [
            str(kit / "bin" / "tree-sitter"), "parse",
            "--lib-path", str(kit / "lib" / "cpp.so"),
            "--lang-name", "cpp",
            "--paths", "/dev/stdin",
        ],
        stdin=subprocess.PIPE,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        text=True,
        bufsize=1 << 20,
        cwd=REPO_ROOT,
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
        yield _finish(tree, diagnostics, strict)
    proc.stdout.close()
    code = proc.wait()
    # The CLI exits 1 when any file in the batch carries a parse error, which is
    # a result and not a tool failure.  The roster and the strict policy above
    # already decide what to do about it.  Every other nonzero exit, and a
    # nonzero exit with no diagnostic to explain it, is a real failure.
    if code == 1 and saw_diagnostic:
        return
    if code != 0:
        raise ParseError(
            f"tree-sitter parse exited {code} with no diagnostic to explain it. "
            "The kit or the file list is wrong."
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


def cpp_files(*roots: str) -> list[Path]:
    """Return every C++ file under the given repo-relative roots, sorted.

    Sorted order makes a gate's report stable across runs, which DetSafe needs.

    Args:
        roots: Repo-relative directory names, for example "include"

    Returns:
        The matching paths, relative to the repo root, in sorted order
    """
    found: list[Path] = []
    for root in roots:
        base = REPO_ROOT / root
        if not base.is_dir():
            continue
        for suffix in (".h", ".hpp", ".cpp", ".cc"):
            found.extend(p.relative_to(REPO_ROOT) for p in base.rglob(f"*{suffix}"))
    return sorted(found)


def _self_test() -> int:
    """Run the module's own checks, positive and negative.

    Returns:
        0 when every check passes, 2 otherwise
    """
    import tempfile

    failures: list[str] = []

    def check(name: str, ok: bool) -> None:
        """Record one check result and print it.

        Args:
            name: What the check asserts
            ok: Whether it held
        """
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

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
        )
        # Positive control: the same file passes when strict is off.
        tolerated = list(parse([broken], strict=False))
        check(
            "strict=False tolerates it and reports the diagnostic",
            len(tolerated) == 1 and tolerated[0].diagnostic is not None,
        )

    # Negative control: a rostered file is admitted, and it really does carry an
    # error, so the roster entry is not stale.
    rostered = Path("src/perf/LockContention.cpp")
    if (REPO_ROOT / rostered).is_file():
        admitted = list(parse([rostered]))
        check(
            "a rostered file is admitted and still carries its error",
            len(admitted) == 1 and admitted[0].diagnostic is not None,
        )

    if failures:
        print(f"tsast --self-test: FAILED — {len(failures)} of the checks did not hold")
        return 2
    print("tsast --self-test: every check passes, 3 of them negative controls.")
    return 0


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
