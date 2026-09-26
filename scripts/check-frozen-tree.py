#!/usr/bin/env python3
"""check-frozen-tree — the old substrate is frozen until it is deleted.

The canonical substrate lives in include/foundation/ and include/fixy/.  While
the old one coexists with it, the old one is frozen: a bug found in old code is
fixed in the new tree or not at all, and nothing is added to the old tree.  A
deletion passes, because the old tree ends by deletion.  The frozen prefixes are
the lines of scripts/frozen-paths.txt, the one list that this guard,
scripts/check-flip-list.py and scripts/check-start-lifetime.py share.

HOW THE CHANGE SET IS READ
    The guard compares the tree of the freeze base with the files on disk.  It
    does not read the git index.  A file under a frozen prefix is changed when
    its content hash differs from the base blob at its path, and it is added
    when the base holds no blob at that path.  The file list is every tracked
    and untracked file that git does not ignore.  A file that is missing from
    the index still counts, so a stale index cannot hide an edit.

FOUR CHANGES ARE ADMITTED
    1. The superseded marking.  A port renames an old header dir/Name.h to
       dir/_Name.h.  A file whose content equals its base twin, once each
       include of the old tree has the leading underscore stripped, passes.
    2. An include removal.  A hunk that only removes lines, where each line
       is an `#include <X>` that the parser reads and nothing else, and X is
       a header that the base holds and the disk does not.  The change set
       is the difference between the base and the disk, so the change that
       removes the include also deletes X, or marks it _X.
    3. A reviewed hunk.  scripts/frozen-soundness-mirrors.txt admits reviewed
       changes to a frozen file, one row for each change, in the shape
       `path — keys — new-tree fix — reason`.  A key pins the exact text of
       one hunk of the line diff between the base twin and the file: the
       first 16 hex digits of the SHA-256 of its removed lines prefixed `-`
       and its added lines prefixed `+`.  A row can key several hunks of one
       file.  A row admits its hunks and nothing else, so a second edit to
       the file makes a hunk that no row keys, and fails.  An edit next to a
       keyed hunk changes the text of that hunk, and fails too.
       `--mirror-hunks PATH` prints the hunks of a frozen file with their
       keys.  The ledger fails closed: a row that names a path outside every
       frozen prefix, a path that does not exist, a malformed key, or a key
       that matches no hunk which needs a row fails the guard.
    4. A macro unification.  A frozen header drops macro definitions, and
       adds includes of foundation headers that define the same macros.  All
       four conditions that follow are necessary:
       a. Against the base twin, each removed line is part of a whole macro
          definition, and each added line is an include of a header that
          exists under include/foundation/.
       b. For each removed definition, a header that the added includes pull
          in defines the same name.  Each definition of the name there (every
          arm of an #if) is a candidate new body.
       c. When no candidate body is token-equal to the removed body, a row of
          scripts/frozen-macro-unifications.txt names the pair (name, old
          body, new body) and the commit that proved the new body at least as
          strict.  A row whose new body no candidate has admits nothing.
       d. The compiler preprocesses the header before and after the change,
          on the flags of a compile database entry.  After the change, each
          removed name is defined, its definition comes from a header that the
          added includes pull in, and the active old body and the active new
          body are equal or form a row of the table.

NO ONE CHECK ADMITS A FILE
    The diff of a changed file against its base twin splits into hunks, and
    each hunk needs exactly one admission.  The include-removal kind reads
    each hunk first.  A ledger key then admits one hunk that the kind does
    not admit, so a key for a hunk that the kind admits matches nothing and
    fails as stale.  The unification check reads only the hunks that are
    still open, and it must explain every one of them.  A file passes when
    no hunk is open, so no admission reaches a hunk beside its own.

WHAT THE PARSER READS
    A macro definition and an include are nodes of the pinned tree-sitter
    kit, so a `#define` or an `#include` inside a comment or a raw string is
    neither.  The guard strips the superseded marking only from the path of a
    real include node.  An include removal needs a row that holds that one node
    and nothing else, a comment included.  A continuation line belongs to its
    definition by the parse, not by a count of backslashes.  A body compares as
    a token list: the parameter list, then the replacement list, one space
    between tokens, with `= ` before the body of an object-like macro.  The
    tokens come from tsast.pp_tokens over the nodes of the definition.  A header
    that does not parse refuses the unification, because its macros are then
    unknown.  The only text this guard reads with a pattern is compiler output:
    the `#define`, `#undef` and line-marker lines of `-E -dD`.

THE COMPILE DATABASE
    --compile-db names it, and build/compile_commands.json is the default.  The
    guard uses the entry of the first translation unit whose preprocessor run
    reads the header, else the first entry whose flags name include/ as a
    directory.  A negative fixture comes last.  Without a database, or when the
    compiler fails, the guard refuses the unification.

    The files that each unit reads come from Unit.dependencies of the store in
    scripts/preprocessed.py, which holds the -MD output of each unit.  The
    include nodes of a unit give the wrong fact.  They miss a header that the
    unit reads through another header, and they count an include in an arm
    that the compiler skips.  The store runs only when a unification needs the
    flags of a header.  On the tree of 2026-09-26, with 4210 units and two
    unifications, the whole scan took 248 s on a cold store and 30 s on a warm
    one.  With a parse of the include nodes of every unit, it took 34 s.  The
    build legs share the cold pass with scripts/check-proof-routes.py.

    The guard keeps its own `-E -dD` run of each header, because the store
    keeps no macro definition and never sees the base text of a header.

Exit 0 clean, 1 on a change under a frozen prefix or a bad ledger or table
entry, 2 on a usage error, a missing frozen list or a failed self-test, 3 when
the freeze base is not in this clone (a shallow checkout) or the parser kit is
missing.
"""

from __future__ import annotations

import contextlib
import difflib
import hashlib
import io
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
from collections.abc import Iterable
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parent))

import preprocessed  # noqa: E402
import throwaway_repo  # noqa: E402
import tsast  # noqa: E402

FREEZE_BASE = "1e0cd65e975f53d8838cb4d22e0aad9233374e35"
PATHS_FILE = "scripts/frozen-paths.txt"
MIRROR_LEDGER = "scripts/frozen-soundness-mirrors.txt"
UNIFICATION_TABLE = "scripts/frozen-macro-unifications.txt"
DEFAULT_COMPILE_DB = "build/compile_commands.json"
DEFINITIONS = ("preproc_def", "preproc_function_def")
# These three patterns read compiler output only: the lines of `-E -dD`, where
# the compiler writes each #define, #undef and line marker in one fixed form.
COMPILER_DEFINE = re.compile(r"^#define ([A-Za-z_]\w*)(\()?")
COMPILER_UNDEF = re.compile(r"^#undef ([A-Za-z_]\w*)")
LINEMARKER = re.compile(r'^# \d+ "((?:\\.|[^"\\])*)"')
NEG_FIXTURE = re.compile(r"(?:^|/)(?:neg|[^/]+_neg)/")
HUNK_KEY = re.compile(r"^[0-9a-f]{16}$")
# The options of a compile command that name an output or produce one.
DROPPED_WITH_VALUE = frozenset({"-o", "-MF", "-MT", "-MQ"})
DROPPED = frozenset({"-c", "-MD", "-MMD"})


class Refused(Exception):
    """The guard cannot run: a missing frozen list or a git failure (exit 2)."""


class BaseMissing(Exception):
    """The freeze base is not in this clone (exit 3)."""


def git(root: Path, *args: str, data: bytes | None = None) -> bytes:
    """Run one git command in the repository and return its stdout.

    Raises:
        Refused: If git fails
    """
    done = subprocess.run(["git", "-C", str(root), *args], input=data, capture_output=True, check=False)
    if done.returncode != 0:
        raise Refused(f"git {' '.join(args[:3])} failed: {done.stderr.decode(errors='replace').strip()}")
    return done.stdout


def frozen_prefixes(root: Path) -> list[str]:
    """Return the frozen prefixes that scripts/frozen-paths.txt lists.

    Raises:
        Refused: If the list is missing or empty, since then nothing is defined as old
    """
    listing = root / PATHS_FILE
    if not listing.is_file():
        raise Refused(f"{PATHS_FILE} is missing, so the old substrate is undefined.")
    prefixes = [line.split("#", 1)[0].strip() for line in listing.read_text().splitlines()]
    prefixes = [p for p in prefixes if p]
    if not prefixes:
        raise Refused(f"{PATHS_FILE} lists no prefix, so the old substrate is undefined.")
    return prefixes


def is_frozen(path: str, prefixes: list[str]) -> bool:
    """Return True when the path is under a frozen directory or is a frozen file."""
    return any(path.startswith(p) if p.endswith("/") else path == p for p in prefixes)


def lines_of(text: str) -> list[str]:
    """Split text into lines the way the parser counts rows: at each newline only."""
    lines = text.split("\n")
    return lines[:-1] if text.endswith("\n") else lines


class Parses:
    """The parse tree of each source text, keyed by its content, so each text parses one time.

    A text comes from the freeze base or from the disk.  prefetch parses many
    texts in one run of the kit, which keeps a scan over the marked headers fast.
    The cache lives for one run of the guard.
    """

    def __init__(self) -> None:
        """Start with no tree."""
        self.trees: dict[str, tsast.Tree] = {}

    @staticmethod
    def key(text: str) -> str:
        """Return the cache key of a text."""
        return hashlib.sha256(text.encode("utf-8", "replace")).hexdigest()

    def prefetch(self, texts: Iterable[str]) -> None:
        """Parse, in one run of the kit, each text that has no tree yet."""
        todo = {}
        for text in texts:
            key = self.key(text)
            if key not in self.trees:
                todo[key] = text
        if todo:
            for key, tree in zip(todo, tsast.parse_texts(list(todo.items()))):
                self.trees[key] = tree

    def tree(self, text: str) -> tsast.Tree:
        """Return the parse tree of one text."""
        self.prefetch([text])
        return self.trees[self.key(text)]


PARSES = Parses()


@dataclass(frozen=True)
class IncludeSite:
    """One `#include <...>` node: its row, the byte span of its path, the header, and whether the row holds nothing else."""

    row: int
    start: int
    end: int
    header: str
    alone: bool


def include_sites(tree: tsast.Tree) -> list[IncludeSite]:
    """Return each angle include of a parse, with whether its row holds that include and nothing else.

    A row holds the include alone when no other node starts or ends on it: a
    trailing comment, a second directive or a declaration on the row is a node.

    Complexity: O(nodes) to index the rows, then O(includes).
    """
    touching: dict[int, list[int]] = {}
    for index in range(1, len(tree)):
        last = tree.erow[index] if tree.ecol[index] > 0 else tree.erow[index] - 1
        for row in {tree.srow[index], last}:
            touching.setdefault(row, []).append(index)
    sites = []
    for node in tree.find("preproc_include"):
        target = node.child_by_field("path")
        if target is None or target.type != "system_lib_string" or target.start[0] != target.end[0]:
            continue
        own = {node.index} | {child.index for child in node.descendants(*set(tree.types))}
        row = node.start[0]
        others = [i for i in touching.get(row, []) if i not in own
                  and not (tree.srow[i] < row and (tree.erow[i] > row or (tree.erow[i] == row + 1 and tree.ecol[i] == 0)))]
        sites.append(IncludeSite(row, target.start[1], target.end[1], target.text[1:-1], not others))
    return sites


def normalized(text: str) -> list[str]:
    """Return the lines with the superseded marking stripped from the path of each old-tree include node.

    Only a real include node is rewritten, so a line inside a comment or a raw
    string keeps its text, and a change to it is a change.
    """
    lines = lines_of(text)
    for site in include_sites(PARSES.tree(text)):
        parts = site.header.split("/")
        if parts[0] != "crucible" or not parts[-1].startswith("_") or site.row >= len(lines):
            continue
        raw = lines[site.row].encode("utf-8", "surrogateescape")
        unmarked = "/".join(parts[:-1] + [parts[-1][1:]]).encode()
        lines[site.row] = (raw[:site.start + 1] + unmarked + raw[site.end - 1:]).decode("utf-8", "surrogateescape")
    return lines


def twin_of(path: str) -> str | None:
    """Return dir/Name for dir/_Name, or None when the name carries no marking."""
    pure = PurePosixPath(path)
    return str(pure.with_name(pure.name[1:])) if pure.name.startswith("_") else None


def token_text(text: str) -> list[str]:
    """Return the preprocessing tokens of a text, without its comments."""
    return [token.text for token in tsast.pp_tokens(text)]


def canonical_node(node: tsast.Node) -> tuple[str, str] | None:
    """Return (name, canonical body) for a `#define` node of a parse.

    A function-like macro keeps its parameter list at the front of the body.  An
    object-like body starts with '= ', so `#define X (a)` and `#define X(a)`
    never compare equal.  A block comment splits the body into several value
    nodes, and the tokens of each are joined.
    """
    name = node.child_by_field("name")
    if name is None:
        return None
    values = " ".join(child.text for child in node.children if child.field == "value")
    body = token_text(values)
    if node.type == "preproc_function_def":
        parameters = node.child_by_field("parameters")
        head = token_text(parameters.text) if parameters is not None else []
        return name.text, " ".join(head + body)
    return name.text, f"= {' '.join(body)}".rstrip()


def canonical_line(line: str) -> tuple[str, str] | None:
    """Return (name, canonical body) for one `#define` line of compiler output, in the same form as canonical_node."""
    head = COMPILER_DEFINE.match(line)
    if head is None:
        return None
    body = " ".join(token_text(line[head.end(1):]))
    return head.group(1), body if head.group(2) else f"= {body}".rstrip()


class Snapshot:
    """The frozen files of the base and of the disk, with their blob hashes.

    Complexity: one ls-tree, one ls-files and one hash-object batch, each
    linear in the number of files it lists.
    """

    def __init__(self, root: Path, base: str, prefixes: list[str]) -> None:
        """Read both sides of the comparison."""
        self.root, self.base, self.prefixes = root, base, prefixes
        specs = [p.rstrip("/") for p in prefixes]
        # The whole base tree: the twin of a marked single-file prefix, such as
        # src/fixy/Fs.cpp for src/fixy/_Fs.cpp, lies outside every prefix.
        self.base_blobs: dict[str, str] = {}
        for record in git(root, "ls-tree", "-r", "-z", base).decode().split("\0"):
            if record:
                meta, path = record.split("\t", 1)
                if meta.split()[1] == "blob":
                    self.base_blobs[path] = meta.split()[2]
        listed = git(root, "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", *specs)
        self.disk = sorted({p for p in listed.decode().split("\0")
                            if p and is_frozen(p, prefixes) and (root / p).is_file()})
        hashes = git(root, "hash-object", "--stdin-paths", data="\n".join(self.disk).encode() + b"\n")
        self.disk_blobs = dict(zip(self.disk, hashes.decode().split(), strict=True)) if self.disk else {}

    def base_text(self, path: str) -> str:
        """Return the base content of a path."""
        return git(self.root, "cat-file", "blob", self.base_blobs[path]).decode(errors="replace")

    def disk_text(self, path: str) -> str:
        """Return the content of a path on disk."""
        return (self.root / path).read_text(errors="replace")

    def base_twin(self, path: str) -> str | None:
        """Return the base path of a file: itself, its unmarked twin, or None."""
        if path in self.base_blobs:
            return path
        twin = twin_of(path)
        return twin if twin is not None and twin in self.base_blobs else None

    def is_marking(self, path: str) -> bool:
        """True when the file equals its base twin once the marking is stripped."""
        for old in (path, twin_of(path)):
            if old is not None and old in self.base_blobs \
                    and normalized(self.base_text(old)) == normalized(self.disk_text(path)):
                return True
        return False


def changed_opcodes(old: list[str], new: list[str]) -> list[tuple[str, int, int, int, int]]:
    """Return the opcodes of the line diff that are not equal: one per hunk.

    Complexity: quadratic in the number of lines in the worst case, as
    difflib is.
    """
    return [op for op in difflib.SequenceMatcher(a=old, b=new, autojunk=False).get_opcodes() if op[0] != "equal"]


def hunk_key(old: list[str], new: list[str], opcode: tuple[str, int, int, int, int]) -> str:
    """Return the ledger key of one hunk: the SHA-256 of its removed and added lines, 16 hex digits."""
    _, i1, i2, j1, j2 = opcode
    text = "\n".join(["-" + line for line in old[i1:i2]] + ["+" + line for line in new[j1:j2]])
    return hashlib.sha256(text.encode()).hexdigest()[:16]


def mirror_hunks(base_text: str, new_text: str) -> list[tuple[str, tuple[str, int, int, int, int]]]:
    """Return (key, opcode) for each hunk of a frozen file against its base, with the marking stripped."""
    old, new = normalized(base_text), normalized(new_text)
    return [(hunk_key(old, new, opcode), opcode) for opcode in changed_opcodes(old, new)]


def include_rows(text: str) -> dict[int, str]:
    """Return the row and the header of each `#include <...>` node that is alone on its row.

    A text that does not parse gives no row, so no include removal is
    admitted from it.
    """
    tree = PARSES.tree(text)
    if tree.diagnostic is not None:
        return {}
    return {site.row: site.header for site in include_sites(tree) if site.alone}


def removed_headers(snapshot: Snapshot, old: list[str], includes: dict[int, str],
                    opcode: tuple[str, int, int, int, int]) -> list[str]:
    """Return the headers of an include-removal hunk, or [] when the hunk is not one.

    Each removed row must hold one include node and nothing else, of a header
    that the base holds under include/ and the disk does not.

    Complexity: linear in the number of removed lines.
    """
    tag, i1, i2, _, _ = opcode
    if tag != "delete":
        return []
    headers = []
    for row in range(i1, i2):
        header = includes.get(row)
        if header is None:
            return []
        target = f"include/{header}"
        if target not in snapshot.base_blobs or (snapshot.root / target).exists():
            return []
        headers.append(header)
    return headers


@dataclass(frozen=True)
class LedgerRow:
    """One reviewed change of the soundness-mirror ledger."""

    number: int
    path: str
    keys: tuple[str, ...]
    reason: str


def ledger_rows(root: Path) -> tuple[list[LedgerRow], list[str]]:
    """Return each row of the soundness-mirror ledger, and one report line for each malformed row."""
    ledger = root / MIRROR_LEDGER
    if not ledger.is_file():
        return [], []
    rows: list[LedgerRow] = []
    rot: list[str] = []
    for number, line in enumerate(ledger.read_text().splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        fields = [field.strip() for field in line.split(" — ", 3)]
        keys = tuple(key.strip() for key in fields[1].split(",")) if len(fields) == 4 else ()
        if len(fields) != 4 or not all(fields) or not keys or not all(HUNK_KEY.match(key) for key in keys):
            rot.append(f"SOUNDNESS-MIRROR-LEDGER: line {number} — not `path — keys — new-tree fix — reason` with "
                       f"16-hex hunk keys.  Run check-frozen-tree.py --mirror-hunks PATH for the keys of a change.")
            continue
        rows.append(LedgerRow(number, fields[0], keys, fields[3]))
    return rows, rot


def ledger_rot(root: Path, prefixes: list[str]) -> list[str]:
    """Return one report line for each ledger row that is malformed or can admit no real edit."""
    rows, rot = ledger_rows(root)
    for row in rows:
        if not is_frozen(row.path, prefixes):
            rot.append(f"SOUNDNESS-MIRROR-LEDGER: {row.path} — not under a frozen directory, so a mirror "
                       f"admission means nothing.  Name the frozen file, or remove the entry.")
        elif not (root / row.path).is_file():
            rot.append(f"SOUNDNESS-MIRROR-LEDGER: {row.path} — does not exist, so the entry admits no edit and "
                       f"hides a typo.  Re-key it onto the surviving path, or prune it.")
    return rot


@dataclass(frozen=True)
class Row:
    """One reviewed body change of the unification table."""

    name: str
    commit: str
    old: str
    new: str
    reason: str


class Table:
    """The rows of scripts/frozen-macro-unifications.txt, and the lines that are not rows."""

    def __init__(self, root: Path) -> None:
        """Read and validate the table.  A missing table has no rows."""
        self.rows: list[Row] = []
        self.rot: list[str] = []
        table = root / UNIFICATION_TABLE
        if not table.is_file():
            return
        for number, line in enumerate(table.read_text().splitlines(), 1):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            fields = [field.strip() for field in line.split(" — ")]
            if len(fields) != 5 or not all(fields):
                self.rot.append(f"UNIFICATION-TABLE: line {number} — has {len(fields)} fields, and a row has "
                                f"five non-empty fields: NAME — COMMIT — OLD BODY — NEW BODY — REASON.")
                continue
            row = Row(*fields)
            if subprocess.run(["git", "-C", str(root), "cat-file", "-e", f"{row.commit}^{{commit}}"],
                              capture_output=True, check=False).returncode != 0:
                self.rot.append(f"UNIFICATION-TABLE: line {number} — {row.name} names {row.commit}, which is "
                                f"not a commit of this repository.")
                continue
            self.rows.append(row)

    def admits(self, name: str, old: str, new: str) -> bool:
        """True when a row names exactly this change of body."""
        return any(row.name == name and row.old == old and row.new == new for row in self.rows)

    def rows_for(self, name: str, old: str) -> list[Row]:
        """Return the rows that change this old body of this name."""
        return [row for row in self.rows if row.name == name and row.old == old]


def definitions(tree: tsast.Tree) -> list[tuple[str, int, int, str]]:
    """Return (macro name, first row, last row, canonical body) for each definition in a parse."""
    found = []
    for node in tree.find(*DEFINITIONS):
        read = canonical_node(node)
        if read is not None:
            end_row, end_col = node.end
            found.append((read[0], node.start[0], end_row - 1 if end_col == 0 else end_row, read[1]))
    return found


def foundation_includes(tree: tsast.Tree) -> dict[int, str]:
    """Return the row and the header of each `#include <foundation/...>` in a parse."""
    rows = {}
    for node in tree.find("preproc_include"):
        target = node.child_by_field("path")
        header = tsast.prose_text(target).strip("<>") if target is not None and target.type == "system_lib_string" \
            else ""
        if header.startswith("foundation/"):
            rows[node.start[0]] = header
    return rows


class Closure:
    """The macro definitions that a set of headers pulls in, read by the parser.

    Complexity: one parse of each header in the include closure, cached across calls.
    """

    def __init__(self, root: Path) -> None:
        """Bind to a repository root."""
        self.root = root
        self.read: dict[Path, tuple[list[Path], list[tuple[str, str]], str | None]] = {}

    def _read(self, path: Path) -> tuple[list[Path], list[tuple[str, str]], str | None]:
        """Parse one header: its resolved includes, its definitions, and its diagnostic."""
        if path not in self.read:
            tree = next(iter(tsast.parse([path], strict=False)))
            includes = []
            for node in tree.find("preproc_include"):
                target = node.child_by_field("path")
                if target is None:
                    continue
                name = target.text[1:-1]
                places = [self.root / "include" / name]
                if target.type == "string_literal":
                    places.insert(0, path.parent / name)
                includes += [place for place in places[:1] if place.is_file()] \
                    or [place for place in places[1:] if place.is_file()][:1]
            defined = [(name, body) for name, _, _, body in definitions(tree)]
            self.read[path] = (includes, defined, tree.diagnostic)
        return self.read[path]

    def of(self, headers: list[str]) -> tuple[dict[str, set[str]], list[str]]:
        """Return the bodies of each name that the headers pull in, and the headers that do not parse."""
        bodies: dict[str, set[str]] = {}
        unreadable: list[str] = []
        todo = [self.root / "include" / header for header in headers]
        seen: set[Path] = set()
        while todo:
            path = todo.pop()
            if path in seen or not path.is_file():
                continue
            seen.add(path)
            includes, defined, diagnostic = self._read(path)
            if diagnostic is not None:
                unreadable.append(str(path.relative_to(self.root)))
            for name, body in defined:
                bodies.setdefault(name, set()).add(body)
            todo += includes
        return bodies, sorted(unreadable)


class Preprocessor:
    """The compiler's view of a header, on the flags of a compile database entry."""

    def __init__(self, root: Path, database: Path) -> None:
        """Load the compile database, or record why it cannot be used."""
        self.root = root
        self.database = database
        self.entries: list[dict] = []
        self.order: list[int] = []
        self.problem: str | None = None
        self.readers: list[frozenset[str]] | None = None
        if not database.is_file():
            self.problem = f"there is no compile database at {database}, so the preprocessor check cannot run " \
                           f"(pass --compile-db)"
            return
        try:
            self.entries = json.loads(database.read_text())
            # A negative fixture compiles to fail, so a plain translation unit
            # comes first when the guard picks the flags of a header.
            self.order = sorted(range(len(self.entries)),
                                key=lambda index: (NEG_FIXTURE.search(self.entries[index]["file"]) is not None,
                                                   self.entries[index]["file"]))
        except (ValueError, KeyError, TypeError) as exc:
            self.problem = f"the compile database {database} does not read: {exc}"

    def read_by(self) -> list[frozenset[str]] | str:
        """Return, for each entry in database order, the files under the root that the compiler reads for its unit.

        The preprocessor store of scripts/preprocessed.py gives this list from
        the -MD dependency file of each unit.  The store runs the first time a
        unification needs the flags of a header.  A unit that does not
        preprocess reads nothing here, so it cannot supply the flags.

        Complexity: one manifest read for each unit on a warm store, and one
        preprocessor run for each unit on a cold one.

        Returns:
            The lists, or the reason the store gives none
        """
        if self.readers is None:
            try:
                units = list(preprocessed.Store(self.database, self.root).units())
            except (OSError, ValueError, KeyError, TypeError) as exc:
                return f"the preprocessor store beside {self.database} does not open: {exc}"
            if len(units) != len(self.entries):
                return f"the compile database {self.database} changed while the preprocessor store read it"
            self.readers = [unit.dependencies for unit in units]
        return self.readers

    def entry_for(self, rel: str) -> tuple[Path, list[str], str] | str:
        """Pick the flags for a header: (working directory, argv without output options, label).

        Returns:
            The choice, or the reason no entry fits
        """
        if self.problem is not None:
            return self.problem
        include_dir = str((self.root / "include").resolve())
        readers = self.read_by()
        if isinstance(readers, str):
            return readers
        direct, fallback = None, None
        for index in self.order:
            entry = self.entries[index]
            source = Path(entry["directory"]) / entry["file"]
            argv = entry.get("arguments") or shlex.split(entry.get("command", ""))
            if fallback is None and any(include_dir in arg or
                                        (prev in ("-I", "-isystem") and str(Path(entry["directory"], arg)
                                                                            .resolve()) == include_dir)
                                        for prev, arg in zip([""] + argv, argv)):
                fallback = (entry, source, argv)
            if rel in readers[index]:
                direct = (entry, source, argv)
                break
        choice = direct or fallback
        if choice is None:
            return f"no entry of the compile database compiles with include/ on its path"
        entry, source, argv = choice
        kept = [argv[0]]
        skip = False
        for arg in argv[1:]:
            if skip:
                skip = False
            elif arg in DROPPED_WITH_VALUE:
                skip = True
            elif arg in DROPPED:
                continue
            elif not arg.startswith("-") and (Path(entry["directory"]) / arg).resolve() == source.resolve():
                continue
            else:
                kept.append(arg)
        label = os.path.relpath(source, self.root) if source.is_absolute() else str(source)
        return Path(entry["directory"]), kept, label

    def macros(self, cwd: Path, argv: list[str], include: str, quote_dir: Path, work: Path) \
            -> tuple[dict[str, tuple[Path, str]], set[Path]] | str:
        """Preprocess a stub that includes one header, and read the macros it leaves defined.

        Returns:
            (name → (defining file, canonical body), every file read), or the failure
        """
        stub = work / f"stub{len(list(work.glob('stub*')))}.cpp"
        stub.write_text(f"#include {include}\n", encoding="utf-8")
        done = subprocess.run([*argv, "-E", "-dD", "-w", "-iquote", str(quote_dir), "-x", "c++", str(stub)],
                              cwd=cwd, capture_output=True, text=True, check=False, timeout=300)
        if done.returncode != 0:
            first = next((line for line in done.stderr.splitlines() if line.strip()), "no message")
            return f"the preprocessor fails on {include}: {first}"
        defined: dict[str, tuple[Path, str]] = {}
        files: set[Path] = set()
        current = Path()
        for line in done.stdout.splitlines():
            marker = LINEMARKER.match(line)
            if marker is not None:
                named = marker.group(1)
                current = Path(named) if named.startswith("<") else (cwd / named).resolve()
                if current != stub.resolve() and not named.startswith("<"):
                    files.add(current)
                continue
            read = canonical_line(line)
            if read is not None:
                defined[read[0]] = (current, read[1])
            elif (undone := COMPILER_UNDEF.match(line)) is not None:
                defined.pop(undone.group(1), None)
        return defined, files

    def check(self, rel: str, base_text: str, names: list[str], headers: list[str], table: Table) \
            -> tuple[list[str], str]:
        """Compare the active definitions before and after the change.

        Returns:
            (the reasons to refuse, the label of the entry whose flags were used)
        """
        choice = self.entry_for(rel)
        if isinstance(choice, str):
            return [choice], ""
        cwd, argv, label = choice
        disk = self.root / rel
        with tempfile.TemporaryDirectory() as scratch:
            work = Path(scratch)
            (work / "base").mkdir()
            base_copy = work / "base" / disk.name
            base_copy.write_text(base_text, encoding="utf-8")
            after = self.macros(cwd, argv, f'"{disk}"', disk.parent, work)
            before = self.macros(cwd, argv, f'"{base_copy}"', disk.parent, work)
            pulled: set[Path] | str = set()
            for header in headers:
                read = self.macros(cwd, argv, f"<{header}>", disk.parent, work)
                pulled = read if isinstance(read, str) else pulled | read[1] if isinstance(pulled, set) else pulled
        failures = [value for value in (after, before, pulled) if isinstance(value, str)]
        if failures:
            return [f"{failure}, under the flags of {label}" for failure in failures], label
        assert isinstance(after, tuple) and isinstance(before, tuple) and isinstance(pulled, set)
        reasons = []
        for name in names:
            new, old = after[0].get(name), before[0].get(name)
            if new is None:
                reasons.append(f"{name} is not defined after the change, under the flags of {label}")
            elif new[0] not in pulled:
                reasons.append(f"{name} comes from {new[0]}, which the added includes do not pull in, "
                               f"under the flags of {label}")
            elif old is None:
                reasons.append(f"{name} is not defined in the base, and the change defines it, under the flags "
                               f"of {label}")
            elif old[1] != new[1] and not table.admits(name, old[1], new[1]):
                reasons.append(f"the active body of {name} changes, and no row of {UNIFICATION_TABLE} admits "
                               f"it, under the flags of {label}: {name} — COMMIT — {old[1]} — {new[1]} — REASON")
        return reasons, label


@dataclass
class Context:
    """What each unification check reads: the table, the parser closure and the preprocessor."""

    table: Table
    closure: Closure
    preprocessor: Preprocessor


def unification(snapshot: Snapshot, path: str, context: Context,
                 admitted: frozenset[tuple[str, int, int, int, int]] = frozenset()) -> tuple[bool, list[str]]:
    """Decide whether the open hunks of a frozen file form a macro unification.

    Complexity: one line diff of the file against its base twin, two parses,
    the parse of the include closure of the added headers, and at most three
    runs of the preprocessor.

    Args:
        admitted: The hunks that an include removal or a ledger key admits.
            The check reads every other hunk, and must explain each one of them

    Returns:
        (True, [verdict]) when admitted, or (False, reasons) when refused
    """
    twin = snapshot.base_twin(path)
    if twin is None:
        return False, []
    base_text, new_text = snapshot.base_text(twin), snapshot.disk_text(path)
    PARSES.prefetch([base_text, new_text])
    old_tree, new_tree = PARSES.tree(base_text), PARSES.tree(new_text)
    owner: dict[int, tuple[str, int, int, str]] = {}
    for found in definitions(old_tree):
        for row in range(found[1], found[2] + 1):
            owner[row] = found
    includes = foundation_includes(new_tree)
    unparsed = [side for side, tree in (("base", old_tree), ("new", new_tree)) if tree.diagnostic]
    old, new = normalized(base_text), normalized(new_text)
    reasons: list[str] = [f"the {side} text does not parse" for side in unparsed]
    removed: set[int] = set()
    added: list[int] = []
    for tag, i1, i2, j1, j2 in changed_opcodes(old, new):
        if (tag, i1, i2, j1, j2) in admitted:
            continue
        if tag in ("delete", "replace"):
            removed.update(range(i1, i2))
        if tag in ("insert", "replace"):
            added.extend(range(j1, j2))
    dropped: list[tuple[str, str]] = []
    seen: set[int] = set()
    for row in sorted(removed):
        if row not in owner:
            reasons.append(f"line {row + 1} of the base is not part of a macro definition: {old[row].strip()}")
            continue
        name, first, last, body = owner[row]
        if first in seen:
            continue
        seen.add(first)
        if not set(range(first, last + 1)) <= removed:
            reasons.append(f"the definition of {name} is removed in part, not whole")
        else:
            dropped.append((name, body))
    headers: list[str] = []
    for row in added:
        header = includes.get(row)
        if header is None:
            reasons.append(f"an added line is not an include of a foundation header: {new[row].strip()}")
        elif not (snapshot.root / "include" / header).is_file():
            reasons.append(f"an added line includes {header}, which does not exist")
        else:
            headers.append(header)
    if not reasons and not dropped and not headers:
        reasons.append("the file differs from its base in no admitted way")
    if reasons:
        return False, reasons
    bodies, unreadable = context.closure.of(headers)
    reasons += [f"{header} does not parse, so the macros it defines are unknown" for header in unreadable]
    for name, body in dropped:
        candidates = bodies.get(name, set())
        rows = context.table.rows_for(name, body)
        if not candidates:
            reasons.append(f"{name} is removed, but no header that the added includes pull in defines it")
        elif body in candidates:
            continue
        elif not rows:
            reasons.append(f"the body of {name} changes, and no row of {UNIFICATION_TABLE} admits it: "
                           f"{name} — COMMIT — {body} — {' | '.join(sorted(candidates))} — REASON")
        elif not any(row.new in candidates for row in rows):
            reasons.append(f"the row of {UNIFICATION_TABLE} for {name} names a new body that no header the "
                           f"added includes pull in defines")
    # Every arm of the new definition must answer to a removed arm, whatever
    # the flags of the compile database select.  An arm that no removed body
    # equals, and no row pairs with one, is a change nobody reviewed, such as
    # an NDEBUG arm that drops the check.
    for name in dict.fromkeys(name for name, _ in dropped):
        old_bodies = [body for dropped_name, body in dropped if dropped_name == name]
        for arm in sorted(bodies.get(name, set())):
            if arm not in old_bodies and not any(context.table.admits(name, old, arm) for old in old_bodies):
                reasons.append(f"the arm `{arm}` of {name} equals no removed arm, and no row of {UNIFICATION_TABLE} "
                               f"admits it: {name} — COMMIT — {old_bodies[0]} — {arm} — REASON")
    if reasons:
        return False, reasons
    names = list(dict.fromkeys(name for name, _ in dropped))
    reasons, label = context.preprocessor.check(path, base_text, names, headers, context.table)
    if reasons:
        return False, reasons
    return True, [f"removes {', '.join(names) or 'no macro'}, and includes {', '.join(headers) or 'no header'} "
                  f"(preprocessed on the flags of {label})"]


def scan(root: Path, base: str, database: Path) -> int:
    """Compare the frozen files on disk with the freeze base, and report to stderr.

    Returns:
        0 clean, 1 on a violation or on ledger or table rot

    Raises:
        BaseMissing: If the base commit is not in the clone
        Refused: If the frozen list is missing or git fails
    """
    prefixes = frozen_prefixes(root)
    if subprocess.run(["git", "-C", str(root), "cat-file", "-e", f"{base}^{{commit}}"],
                      capture_output=True, check=False).returncode != 0:
        # Only a shallow clone may skip.  A git that fails, or a full clone
        # without the base, is an error, so a CI checkout cannot skip in silence.
        if git(root, "rev-parse", "--is-shallow-repository").decode().strip() == "true":
            raise BaseMissing(base)
        raise Refused(f"the freeze base {base} is not in this clone, and the clone is not shallow.")
    context = Context(Table(root), Closure(root), Preprocessor(root, database))
    rc = 0
    for line in ledger_rot(root, prefixes) + context.table.rot:
        print(line, file=sys.stderr)
        rc = 1
    rows_of: dict[str, list[LedgerRow]] = {}
    for row in ledger_rows(root)[0]:
        rows_of.setdefault(row.path, []).append(row)
    # The keys each row still has to find in its file.  A key that no open
    # hunk of the file carries is stale, so the ledger only shrinks.
    unfound: dict[int, list[str]] = {row.number: list(row.keys) for rows in rows_of.values() for row in rows}
    snapshot = Snapshot(root, base, prefixes)
    changed = [path for path in snapshot.disk if snapshot.base_blobs.get(path) != snapshot.disk_blobs[path]]
    # Every text the marking test reads parses in one run of the kit.
    PARSES.prefetch([snapshot.disk_text(path) for path in changed]
                    + [snapshot.base_text(old) for path in changed for old in (path, twin_of(path))
                       if old is not None and old in snapshot.base_blobs])
    for path in changed:
        if snapshot.is_marking(path):
            continue
        twin = snapshot.base_twin(path)
        change = "modified" if twin is not None else "added"
        notes: list[str] = []
        if twin is not None:
            base_text = snapshot.base_text(twin)
            hunks = mirror_hunks(base_text, snapshot.disk_text(path))
            old = normalized(base_text)
            includes = include_rows(base_text) if any(op[0] == "delete" for _, op in hunks) else {}
            admitted: set[tuple[str, int, int, int, int]] = set()
            reasons: list[str] = []
            for key, opcode in hunks:
                headers = removed_headers(snapshot, old, includes, opcode)
                if headers:
                    admitted.add(opcode)
                    for header in headers:
                        spot = PurePosixPath("include") / header
                        state = "marked superseded" if (root / spot.with_name("_" + spot.name)).is_file() else "deleted"
                        print(f"check-frozen-tree: ADMITTED include removal: {path} — {header} is {state}.",
                              file=sys.stderr)
                    continue
                row = next((r for r in rows_of.get(path, []) if key in unfound[r.number]), None)
                if row is not None:
                    unfound[row.number].remove(key)
                    admitted.add(opcode)
                    if row.reason not in reasons:
                        reasons.append(row.reason)
            for reason in reasons:
                print(f"check-frozen-tree: ADMITTED reviewed hunk: {path} — {reason} (per {MIRROR_LEDGER}).",
                      file=sys.stderr)
            if len(admitted) == len(hunks):
                continue
            passed, notes = unification(snapshot, path, context, frozenset(admitted))
            if passed:
                print(f"check-frozen-tree: ADMITTED macro unification: {path} — {notes[0]}.", file=sys.stderr)
                continue
        print(f"FROZEN violation: {path} — {change} under a frozen path.  The old substrate only shrinks.  "
              f"Admitted: the deletion of a whole file; the _ marking of a ported file (git mv dir/X.h "
              f"dir/_X.h) and of each include of it; the removal of an include of a header that the same "
              f"change deletes or marks; a hunk that a row of {MIRROR_LEDGER} keys; a macro unification with "
              f"its rows in {UNIFICATION_TABLE}.  Each hunk of the file needs one of these.  To change what the "
              f"file does, port it to include/foundation or include/fixy, move its consumers there, and mark "
              f"the old file superseded.", file=sys.stderr)
        for note in notes[:3]:
            print(f"    not a macro unification: {note}", file=sys.stderr)
        if len(notes) > 3:
            print(f"    not a macro unification: {len(notes) - 3} more lines", file=sys.stderr)
        rc = 1
    for rows in rows_of.values():
        for row in rows:
            if unfound[row.number] and (root / row.path).is_file():
                print(f"SOUNDNESS-MIRROR-LEDGER: line {row.number}: {row.path} — no hunk of the file that needs "
                      f"a row carries {', '.join(unfound[row.number])}.  The file no longer has that change, "
                      f"another row keys the hunk, or the include-removal kind admits it.  Remove the key, or "
                      f"re-key the row with --mirror-hunks.", file=sys.stderr)
                rc = 1
    return rc


def run(root: Path, base: str, database: Path) -> int:
    """Run the scan and turn each refusal into its exit code and message."""
    try:
        rc = scan(root, base, database)
    except BaseMissing:
        print(f"check-frozen-tree: the freeze base {base} is not in this clone (shallow checkout?).  Fetch "
              f"full history (fetch-depth: 0 in CI).", file=sys.stderr)
        return 3
    except Refused as exc:
        print(f"check-frozen-tree: {exc}", file=sys.stderr)
        return 2
    if rc == 1:
        print("\ncheck-frozen-tree: the old substrate changed.  It is frozen until it is deleted.  A fix "
              "belongs in include/foundation/ or include/fixy/.  A consumer that still needs the old tree "
              "moves to the new tree, and the old tree is not patched.  Deleting old files is always allowed.",
              file=sys.stderr)
    return rc


def self_test() -> int:
    """Build a throwaway repository, plant each change shape, and check each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def expect(name: str, ok: bool, negative: bool = False) -> None:
        """Record one case.  A negative control is a case where the guard must refuse."""
        nonlocal negatives
        negatives += negative
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    # The fixture commits must land in the fixture repository, so the
    # repository variables of the caller go before the environment is copied.
    throwaway_repo.isolate()
    env = dict(os.environ, GIT_AUTHOR_NAME="selftest", GIT_AUTHOR_EMAIL="selftest@invalid",
               GIT_COMMITTER_NAME="selftest", GIT_COMMITTER_EMAIL="selftest@invalid")

    def sh(root: Path, *args: str) -> str:
        """Run git in the planted repository and return its stdout."""
        return subprocess.run(["git", "-C", str(root), *args], env=env, capture_output=True, text=True,
                              check=True).stdout.strip()

    def write(root: Path, rel: str, text: str) -> None:
        """Write one planted file."""
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_text(text, encoding="utf-8")

    def captured(root: Path, base: str, cwd: Path | None = None, database: Path | None = None) -> tuple[int, str]:
        """Run the guard on a planted repository and keep its report."""
        buffer = io.StringIO()
        previous = Path.cwd()
        if cwd is not None:
            os.chdir(cwd)
        try:
            with contextlib.redirect_stderr(buffer):
                code = run(root, base, database or root / DEFAULT_COMPILE_DB)
        finally:
            os.chdir(previous)
        return code, buffer.getvalue()

    def refusal(report: str, path: str) -> str:
        """Return the report block of one refused file, or '' when it is not refused."""
        block = report.split(f"FROZEN violation: {path} ", 1)
        return block[1].split("FROZEN violation:", 1)[0] if len(block) == 2 else ""

    with tempfile.TemporaryDirectory() as work:
        # The preprocessor store resolves the root, so the planted compile
        # database names the resolved directory too.
        root = Path(work).resolve()
        throwaway_repo.init(root)
        write(root, PATHS_FILE, "# planted\ninclude/crucible/safety/\ninclude/crucible/fixy/\n"
                                "src/fixy/_Fs.cpp\nsrc/fixy/_Io.cpp\nexamples/fn/\n")
        # A reviewed change of three hunks, and the same change with one more
        # line next to its last hunk.
        pinned_base = "#pragma once\n// pin one\nint p = 1;\n// pin two\nint q = 2;\n// pin three\n"
        pinned_new = "#pragma once\n// pin one fixed\nint p = 1;\nint q = 2;\n// pin three fixed\n// pin four\n"
        pinned_plus = pinned_new + "// pin five, not reviewed\n"
        # Each umbrella includes one header that the change deletes, marks or
        # keeps, and the change removes one include line from it.
        umbrellas = {
            "UmbrellaA": "#pragma once\n#include <crucible/safety/GoneA.h>\n#include <crucible/safety/Kept.h>\n// a\n",
            "UmbrellaM": "#pragma once\n#include <crucible/safety/MarkedM.h>\n#include <crucible/safety/Kept.h>\n// m\n",
            "UmbrellaB": "#pragma once\n#include <crucible/safety/KeptB.h>\n#include <crucible/safety/GoneB.h>\n// b\n",
            "UmbrellaC": "#pragma once\n#include <crucible/safety/GoneC.h>\n#include <crucible/safety/Kept.h>\n// c\n",
            "UmbrellaE": "#pragma once\n#include <crucible/safety/KeptE.h>\n#include <crucible/safety/Kept.h>\n// e\n",
            "UmbrellaF": "#pragma once\n/*\n#include <crucible/safety/GoneF.h>\n*/\n#include <crucible/safety/Kept.h>\n",
            "UmbrellaK": "#pragma once\n#include <crucible/safety/GoneK.h>\n#include <crucible/safety/Kept.h>\n"
                         "int k = 1;\n// k\n",
            "UmbrellaL": "#pragma once\n#include <crucible/safety/GoneL.h>\n#include <crucible/safety/Kept.h>\n"
                         "int k = 1;\n// k\nint m = 0;\nint l = 2;\n",
        }
        umbrella_edits = {
            "UmbrellaA": "#pragma once\n#include <crucible/safety/Kept.h>\n// a\n",
            "UmbrellaM": "#pragma once\n#include <crucible/safety/Kept.h>\n// m\n",
            "UmbrellaB": "#pragma once\n#include <crucible/safety/GoneB.h>\n// b\n",
            "UmbrellaC": "#pragma once\n#include <crucible/safety/Kept.h>\n// c, and edited\n",
            "UmbrellaE": "#pragma once\n#include <crucible/safety/Kept.h>\n// e\n",
            "UmbrellaF": "#pragma once\n/*\n*/\n#include <crucible/safety/Kept.h>\n",
            "UmbrellaK": "#pragma once\n#include <crucible/safety/Kept.h>\nint k = 1;\n// k, reviewed\n",
            "UmbrellaL": "#pragma once\n#include <crucible/safety/Kept.h>\nint k = 1;\n// k, reviewed\nint m = 0;\n"
                         "int l = 3;\n",
        }

        def keys(base_text: str, new_text: str, *, skip_deletes: bool = False) -> str:
            """Return the ledger keys of a change, without its pure deletions when asked."""
            return ",".join(key for key, opcode in mirror_hunks(base_text, new_text)
                            if not (skip_deletes and opcode[0] == "delete"))

        planted = {
            "include/crucible/safety/Old.h": "// old\n",
            "include/crucible/fixy/Gone.h": "// gone\n",
            "include/crucible/fixy/Renamed.h": "// renamed\n",
            "src/fixy/_Fs.cpp": "// fs\n",
            "src/fixy/Io.cpp": "// io\n",
            "examples/fn/keep.cpp": "// keep\n",
            "include/crucible/safety/Ported.h": "#pragma once\n#include <crucible/safety/Twin.h>\n// ported\n",
            "include/crucible/safety/Twin.h": "#pragma once\n// twin\n",
            "include/crucible/safety/Includer.h": "#pragma once\n#include <crucible/safety/Twin.h>\n// frozen\n",
            "include/crucible/safety/Tampered.h": "#pragma once\n// tampered\n",
            "include/crucible/safety/Mirror.h": "#pragma once\n// mirror base\n",
            "include/crucible/safety/MirrorCopy.h": "#pragma once\n// mirror base\n",
            "include/crucible/safety/Mirror2.h": "#pragma once\n// mirror2 base ported\n",
            "include/crucible/safety/Twice.h": "#pragma once\n// twice one\nint twice = 1;\n// twice two\n",
            "include/crucible/safety/Pinned.h": pinned_base,
            "include/crucible/safety/PinnedPlus.h": pinned_base,
            "include/crucible/safety/Kept.h": "// kept\n",
            **{f"include/crucible/safety/{name}.h": f"// {name}\n"
               for name in ("GoneA", "MarkedM", "KeptB", "GoneB", "GoneC", "KeptE", "GoneF", "GoneK", "GoneL")},
            **{f"include/crucible/safety/{name}.h": text for name, text in umbrellas.items()},
            "include/crucible/Root.h": "#pragma once\n// root\n",
            "include/crucible/safety/RootIncluder.h": "#pragma once\n#include <crucible/Root.h>\n// roots\n",
            "include/crucible/safety/Hidden.h": "// hidden\n",
            "include/crucible/safety/RawMark.h": 'const char* raw_mark = R"x(\n#include <crucible/safety/Twin.h>\n)x";\n',
        }
        for rel, text in planted.items():
            write(root, rel, text)
        sh(root, "add", "-A")
        sh(root, "commit", "-q", "-m", "base")
        base = sh(root, "rev-parse", "HEAD")
        code, report = captured(root, base)
        expect("a clean tree reads clean", code == 0)

        write(root, "include/crucible/safety/Old.h", "// edited\n")
        sh(root, "rm", "-q", "include/crucible/fixy/Gone.h")
        write(root, "include/crucible/safety/New.h", "// new\n")
        sh(root, "mv", "include/crucible/fixy/Renamed.h", "include/crucible/fixy/Moved.h")
        write(root, "include/foundation/Fine.h", "// new layer\n")
        write(root, "src/fixy/_Fs.cpp", "// edited\n")
        sh(root, "mv", "src/fixy/Io.cpp", "src/fixy/_Io.cpp")
        sh(root, "mv", "include/crucible/safety/Twin.h", "include/crucible/safety/_Twin.h")
        sh(root, "mv", "include/crucible/safety/Ported.h", "include/crucible/safety/_Ported.h")
        write(root, "include/crucible/safety/_Ported.h",
              "#pragma once\n#include <crucible/safety/_Twin.h>\n// ported\n")
        write(root, "include/crucible/safety/Includer.h",
              "#pragma once\n#include <crucible/safety/_Twin.h>\n// frozen\n")
        sh(root, "mv", "include/crucible/safety/Tampered.h", "include/crucible/safety/_Tampered.h")
        write(root, "include/crucible/safety/_Tampered.h", "#pragma once\n// tampered, and edited\n")
        sh(root, "mv", "include/crucible/Root.h", "include/crucible/_Root.h")
        write(root, "include/crucible/safety/RootIncluder.h",
              "#pragma once\n#include <crucible/_Root.h>\n// roots\n")
        # The marking inside a raw string is text and not an include node, so
        # this edit changes the content of the file.
        write(root, "include/crucible/safety/RawMark.h",
              'const char* raw_mark = R"x(\n#include <crucible/safety/_Twin.h>\n)x";\n')
        mirror_new ="#pragma once\n// mirror edited\n"
        write(root, "include/crucible/safety/Mirror.h", mirror_new)
        write(root, "include/crucible/safety/MirrorCopy.h", mirror_new)
        sh(root, "mv", "include/crucible/safety/Mirror2.h", "include/crucible/safety/_Mirror2.h")
        mirror2_new = "#pragma once\n// mirror2 ported, and edited\n"
        write(root, "include/crucible/safety/_Mirror2.h", mirror2_new)
        twice_reviewed = "#pragma once\n// twice one, reviewed\nint twice = 1;\n// twice two\n"
        write(root, "include/crucible/safety/Twice.h", twice_reviewed.replace("// twice two", "// twice two, not"))
        write(root, "include/crucible/safety/Pinned.h", pinned_new)
        write(root, "include/crucible/safety/PinnedPlus.h", pinned_plus)
        for name, text in umbrella_edits.items():
            write(root, f"include/crucible/safety/{name}.h", text)
        for name in ("GoneA", "GoneB", "GoneC", "GoneF", "GoneK", "GoneL"):
            sh(root, "rm", "-q", f"include/crucible/safety/{name}.h")
        sh(root, "mv", "include/crucible/safety/MarkedM.h", "include/crucible/safety/_MarkedM.h")
        k_reviewed = keys(umbrellas["UmbrellaK"], umbrella_edits["UmbrellaK"], skip_deletes=True)
        mirror_keys = keys(planted["include/crucible/safety/Mirror.h"], mirror_new)
        mirror2_keys = keys(planted["include/crucible/safety/Mirror2.h"], mirror2_new)
        twice_keys = keys(planted["include/crucible/safety/Twice.h"], twice_reviewed)
        ledger = (
            "# planted\n"
            f"include/crucible/safety/Mirror.h — {mirror_keys} — include/foundation/Mirror.h — "
            "a live bug already fixed\n"
            f"include/crucible/safety/_Mirror2.h — {mirror2_keys} — include/foundation/Mirror2.h — "
            "fixed in the ported file\n"
            f"include/crucible/safety/Twice.h — {twice_keys} — include/foundation/Twice.h — the first edit only\n"
            f"include/crucible/safety/Pinned.h — {keys(pinned_base, pinned_new)} — include/foundation/Pinned.h — "
            "a pinned change of three hunks\n"
            f"include/crucible/safety/PinnedPlus.h — {keys(pinned_base, pinned_new)} — "
            "include/foundation/Pinned.h — the pinned change, with one more line\n"
            f"include/crucible/safety/UmbrellaK.h — {k_reviewed} — include/foundation/K.h — a reviewed hunk\n"
            f"include/crucible/safety/UmbrellaL.h — {k_reviewed} — include/foundation/K.h — a reviewed hunk\n")
        write(root, MIRROR_LEDGER, ledger)
        sh(root, "add", "-A")
        # The stale-index hole: a file dropped from the index and then edited.
        # A diff against the index calls it deleted and passes it.
        sh(root, "rm", "-q", "--cached", "include/crucible/safety/Hidden.h")
        write(root, "include/crucible/safety/Hidden.h", "// hidden, and edited\n")
        code, report = captured(root, base)
        expect("the planted tree fails", code == 1)
        caught = (("include/crucible/safety/Old.h", "a modify under a frozen directory"),
                  ("include/crucible/safety/New.h", "an untracked add under a frozen directory"),
                  ("include/crucible/fixy/Moved.h", "a rename into a frozen directory"),
                  ("src/fixy/_Fs.cpp", "a modify of a frozen single file"),
                  ("include/crucible/safety/_Tampered.h", "a marking that also edits content"),
                  ("include/crucible/safety/Hidden.h", "an edit to a file missing from the index"),
                  ("include/crucible/safety/MirrorCopy.h", "the reviewed hunk of one file in another file"),
                  ("include/crucible/safety/Twice.h", "a second, unreviewed edit to a ledger path"),
                  ("include/crucible/safety/PinnedPlus.h", "a pinned multi-hunk change plus one more line"),
                  ("include/crucible/safety/UmbrellaB.h",
                   "an include removal while a different header is deleted"),
                  ("include/crucible/safety/UmbrellaC.h", "an include removal plus another change"),
                  ("include/crucible/safety/UmbrellaE.h", "the removal of an include of a header that exists"),
                  ("include/crucible/safety/UmbrellaF.h", "the removal of an include inside a block comment"),
                  ("include/crucible/safety/UmbrellaL.h",
                   "a reviewed hunk and an include removal plus one unadmitted hunk"),
                  ("include/crucible/safety/RawMark.h", "the marking of an include inside a raw string"))
        for path, label in caught:
            expect(f"caught: {label}", f"violation: {path}" in report, True)
        for path, label in (("include/crucible/fixy/Gone.h", "a deletion"),
                            ("include/foundation/Fine.h", "an add in the new tree"),
                            ("include/crucible/safety/_Twin.h", "a plain superseded marking"),
                            ("src/fixy/_Io.cpp", "a marking into a frozen single-file prefix"),
                            ("include/crucible/safety/_Ported.h", "a marking whose include followed"),
                            ("include/crucible/safety/Includer.h", "an include-only edit that follows a marking"),
                            ("include/crucible/safety/RootIncluder.h",
                             "an include edit that follows a root-level marking"),
                            ("include/crucible/safety/Mirror.h", "a keyed modify"),
                            ("include/crucible/safety/_Mirror2.h", "a keyed edit to an already-ported file"),
                            ("include/crucible/safety/Pinned.h", "a pinned change of three hunks"),
                            ("include/crucible/safety/UmbrellaA.h", "an include removal with the deletion"),
                            ("include/crucible/safety/UmbrellaM.h", "an include removal with the marking"),
                            ("include/crucible/safety/UmbrellaK.h", "a reviewed hunk and an include removal")):
            expect(f"not caught: {label}", f"violation: {path}" not in report)
        expect("the keyed modify is admitted with its reason",
               "ADMITTED reviewed hunk: include/crucible/safety/Mirror.h — a live bug already fixed" in report)
        expect("the keyed ported edit is admitted", "ADMITTED reviewed hunk: include/crucible/safety/_Mirror2.h" in report)
        expect("the reviewed edit of a twice-edited file is still admitted",
               "ADMITTED reviewed hunk: include/crucible/safety/Twice.h" in report)
        expect("the include removal names the deleted header",
               "ADMITTED include removal: include/crucible/safety/UmbrellaA.h — crucible/safety/GoneA.h is deleted"
               in report)
        expect("the include removal names the marked header",
               "ADMITTED include removal: include/crucible/safety/UmbrellaM.h — crucible/safety/MarkedM.h is "
               "marked superseded" in report)
        expect("the include removal of a kept header is not admitted",
               "ADMITTED include removal: include/crucible/safety/UmbrellaB.h" not in report
               and "ADMITTED include removal: include/crucible/safety/UmbrellaE.h" not in report, True)
        expect("an include inside a block comment is not an include",
               "ADMITTED include removal: include/crucible/safety/UmbrellaF.h" not in report, True)
        expect("the marking inside a raw string is a change of content and not a marking",
               "FROZEN violation: include/crucible/safety/RawMark.h — modified under a frozen path" in report, True)
        expect("the one rot is the stale last key of the pinned row",
               report.count("SOUNDNESS-MIRROR-LEDGER:") == 1
               and "SOUNDNESS-MIRROR-LEDGER: line 6: include/crucible/safety/PinnedPlus.h — no hunk of the file "
                   f"that needs a row carries {keys(pinned_base, pinned_new).split(',')[-1]}" in report, True)
        expect(f"exactly {len(caught)} violations", report.count("FROZEN violation:") == len(caught))
        expect("the report from / equals the report from the repository",
               captured(root, base, Path("/")) == captured(root, base, root))
        printed = io.StringIO()
        with contextlib.redirect_stdout(printed):
            print_mirror_hunks(root, base, "include/crucible/safety/UmbrellaK.h")
        expect("--mirror-hunks keys the reviewed hunk and not the include removal",
               f"keys: {k_reviewed}\n" in printed.getvalue()
               and "(an include removal: it needs no key)" in printed.getvalue())

        # Each hunk takes exactly one admission, so a second row for a hunk
        # that another row or the include-removal kind admits is stale.
        removal_key = next(key for key, opcode in mirror_hunks(umbrellas["UmbrellaA"], umbrella_edits["UmbrellaA"])
                           if opcode[0] == "delete")
        for extra, stale, label in (
                (f"include/crucible/safety/UmbrellaA.h — {removal_key} — ref — keys a removal\n",
                 "include/crucible/safety/UmbrellaA.h", "a row that keys an admitted include removal"),
                (f"include/crucible/safety/Mirror.h — {mirror_keys} — ref — a second row\n",
                 "include/crucible/safety/Mirror.h", "a second row for a hunk that a row admits")):
            write(root, MIRROR_LEDGER, ledger + extra)
            code, report = captured(root, base)
            expect(f"stale: {label}", f"SOUNDNESS-MIRROR-LEDGER: line 9: {stale} — no hunk" in report, True)
        write(root, MIRROR_LEDGER, ledger)

        for text, name, label in (
                ("include/crucible/safety/Ghost.h — 0123456789abcdef — ref — not here\n",
                 "include/crucible/safety/Ghost.h", "a ledger entry for a missing path"),
                ("include/foundation/Fine.h — 0123456789abcdef — ref — not frozen\n", "include/foundation/Fine.h",
                 "a ledger entry for a path that is not frozen"),
                ("include/crucible/safety/Mirror.h — ref — a row with no keys\n", "line 1",
                 "a row in the three-field form, with no keys"),
                ("include/crucible/safety/Mirror.h — 0123456789ABCDEF — ref — upper case\n", "line 1",
                 "a key that is not 16 lowercase hex digits"),
                ("include/crucible/safety/Mirror.h — 0123456789abcdef, — ref — an empty key\n", "line 1",
                 "a key list with an empty key")):
            write(root, MIRROR_LEDGER, text)
            expect(f"rot: {label}", f"SOUNDNESS-MIRROR-LEDGER: {name}" in "\n".join(ledger_rot(
                root, frozen_prefixes(root))), True)

        # The macro unification, against a second base.
        write(root, MIRROR_LEDGER, "# planted\n")
        write(root, "include/foundation/contracts/Uni.h",
              "#pragma once\n#define UNI_ONE(c) ((void)(c))\n#define UNI_TWO(c) \\\n    ((void)(c))\n"
              "#define UNI_SAME(c) ((void)(c))\n#define UNI_THREE(c) ((void)(c))\n#define UNI_FOUR(c) ((void)(c))\n"
              "#define UNI_STRONG(c) ((void)(c))\n#if 0\n#define UNI_DEAD(c) ((void)(c))\n#endif\n"
              "#ifdef UNI_NEVER\n#define UNI_ARM(c) ((void)(c))\n#else\n#define UNI_ARM(c) ((void)0)\n#endif\n"
              "#ifdef NDEBUG\n#define UNI_CHECK(x) ((void)0)\n#else\n#define UNI_CHECK(x) check(x)\n#endif\n"
              "#ifdef UNI_FLAG\n#define UNI_SW(c) ((void)(c))\n#else\n#define UNI_SW(c) ((void)0)\n#endif\n"
              "/*\n#define UNI_COMMENTED 1\n*/\n")
        write(root, "include/crucible/safety/UniRelease.h", "#pragma once\n#define UNI_CHECK(x) check(x)\nint uni = 1;\n")
        write(root, "include/crucible/safety/UniSwapArms.h",
              "#pragma once\n#ifdef UNI_FLAG\n#define UNI_SW(c) ((void)0)\n#else\n#define UNI_SW(c) ((void)(c))\n#endif\n"
              "int uni = 1;\n")
        write(root, "include/foundation/contracts/Other.h", "#pragma once\n#define UNI_OTHER 1\n")
        uni_base = ("#pragma once\n#include <crucible/safety/Old.h>\n#define UNI_ONE(c) ((void)0)\n"
                    "#define UNI_TWO(c) \\\n    ((void)0)\nint uni = 1;\n")
        for name in ("UniPlain", "UniPorted", "UniNonMacro", "UniBadInclude", "UniMissing", "UniPartial"):
            write(root, f"include/crucible/safety/{name}.h", uni_base + f"// {name}\n")
        single = {"UniLacks": "UNI_OLD_ONLY 1", "UniCommentOnly": "UNI_COMMENTED 1", "UniSame": "UNI_SAME(c) ((void)(c))",
                  "UniNoRow": "UNI_THREE(c) ((void)0)", "UniStaleRow": "UNI_FOUR(c) ((void)0)",
                  "UniSwap": "UNI_STRONG(c) ((void)(c))", "UniDead": "UNI_DEAD(c) ((void)(c))",
                  "UniArm": "UNI_ARM(c) ((void)(c))"}
        for name, definition in single.items():
            write(root, f"include/crucible/safety/{name}.h", f"#pragma once\n#define {definition}\nint uni = 1;\n")
        write(root, "include/crucible/safety/UniInComment.h",
              "#pragma once\n/*\n#define UNI_ONE(c) ((void)0)\n*/\nint uni = 1;\n")
        write(root, "include/crucible/safety/UniInRaw.h",
              '#pragma once\nconst char* uni = R"x(\n#define UNI_ONE(c) ((void)0)\n)x";\n')
        write(root, "include/crucible/safety/UniIncludeInComment.h", "#pragma once\n/*\n*/\nint uni = 1;\n")
        write(root, "src/tu.cpp", "#include <crucible/safety/UniPlain.h>\n")
        # A unit that sorts first names UniPlain.h in a comment and includes it
        # in an arm that the compiler skips.  The compiler reads the header only
        # for src/tu.cpp, so the flags come from there.
        write(root, "src/aaa.cpp", "// crucible/safety/UniPlain.h is named here, and included in a skipped arm.\n"
                                   "#if 0\n#include <crucible/safety/UniPlain.h>\n#endif\nint aaa = 1;\n")
        # The last unit reaches _UniPorted.h only through a header of its own,
        # so no include node of a unit names it, and the compiler still reads it.
        write(root, "src/umbrella.h", "#pragma once\n#include <crucible/safety/_UniPorted.h>\n")
        write(root, "src/zzz.cpp", '#include "umbrella.h"\n')
        cxx = os.environ.get("FROZEN_TREE_CXX", "c++")
        write(root, DEFAULT_COMPILE_DB, json.dumps([
            {"directory": str(root), "file": unit,
             "arguments": [cxx, "-std=c++20", "-I", "include", "-c", unit, "-o", "unit.o"]}
            for unit in ("src/aaa.cpp", "src/tu.cpp", "src/zzz.cpp")]))
        sh(root, "add", "-A")
        sh(root, "commit", "-q", "-m", "unification base")
        base3 = sh(root, "rev-parse", "HEAD")
        write(root, UNIFICATION_TABLE,
              "# planted\n"
              f"UNI_ONE — {base3} — ( c ) ( ( void ) 0 ) — ( c ) ( ( void ) ( c ) ) — the argument is evaluated\n"
              f"UNI_TWO — {base3} — ( c ) ( ( void ) 0 ) — ( c ) ( ( void ) ( c ) ) — the argument is evaluated\n"
              f"UNI_FOUR — {base3} — ( c ) ( ( void ) 0 ) — ( c ) ( ( void ) ( c ) ( c ) ) — a stale new body\n")
        uni_edit = "#pragma once\n#include <crucible/safety/Old.h>\n#include <foundation/contracts/Uni.h>\nint uni = 1;\n"
        write(root, "include/crucible/safety/UniPlain.h", uni_edit + "// UniPlain\n")
        sh(root, "mv", "include/crucible/safety/UniPorted.h", "include/crucible/safety/_UniPorted.h")
        write(root, "include/crucible/safety/_UniPorted.h", uni_edit + "// UniPorted\n")
        write(root, "include/crucible/safety/UniLacks.h", "#pragma once\nint uni = 1;\n")
        write(root, "include/crucible/safety/UniNonMacro.h",
              "#pragma once\n#include <crucible/safety/Old.h>\n#define UNI_TWO(c) \\\n    ((void)0)\n"
              "int uni = 2;\n// UniNonMacro\n")
        write(root, "include/crucible/safety/UniBadInclude.h",
              "#pragma once\n#include <crucible/safety/Old.h>\n#include <crucible/safety/Twin.h>\n"
              "#define UNI_TWO(c) \\\n    ((void)0)\nint uni = 1;\n// UniBadInclude\n")
        write(root, "include/crucible/safety/UniMissing.h",
              "#pragma once\n#include <crucible/safety/Old.h>\n#include <foundation/contracts/Missing.h>\n"
              "#define UNI_TWO(c) \\\n    ((void)0)\nint uni = 1;\n// UniMissing\n")
        write(root, "include/crucible/safety/UniPartial.h",
              "#pragma once\n#include <crucible/safety/Old.h>\n#define UNI_ONE(c) ((void)0)\n    ((void)0)\n"
              "int uni = 1;\n// UniPartial\n")
        for name in ("UniCommentOnly", "UniSame", "UniNoRow", "UniStaleRow", "UniDead", "UniArm"):
            write(root, f"include/crucible/safety/{name}.h",
                  "#pragma once\n#include <foundation/contracts/Uni.h>\nint uni = 1;\n")
        write(root, "include/crucible/safety/UniSwap.h",
              "#pragma once\n#include <foundation/contracts/Other.h>\nint uni = 1;\n")
        write(root, "include/crucible/safety/UniRelease.h",
              "#pragma once\n#include <foundation/contracts/Uni.h>\nint uni = 1;\n")
        write(root, "include/crucible/safety/UniSwapArms.h",
              "#pragma once\n#include <foundation/contracts/Uni.h>\n#ifdef UNI_FLAG\n#else\n#endif\nint uni = 1;\n")
        write(root, "include/crucible/safety/UniInComment.h", "#pragma once\n/*\n*/\nint uni = 1;\n")
        write(root, "include/crucible/safety/UniInRaw.h", '#pragma once\nconst char* uni = R"x(\n)x";\n')
        write(root, "include/crucible/safety/UniIncludeInComment.h",
              "#pragma once\n/*\n#include <foundation/contracts/Uni.h>\n*/\nint uni = 1;\n")
        code, report = captured(root, base3)
        expect("the unification tree fails", code == 1)
        expect("a unification through reviewed rows is admitted",
               "ADMITTED macro unification: include/crucible/safety/UniPlain.h — removes UNI_ONE, UNI_TWO, "
               "and includes foundation/contracts/Uni.h (preprocessed on the flags of src/tu.cpp)" in report)
        expect("a unification of a file marked in the same change is admitted, on the flags of the unit that "
               "reads it through another header",
               "ADMITTED macro unification: include/crucible/safety/_UniPorted.h — removes UNI_ONE, UNI_TWO, "
               "and includes foundation/contracts/Uni.h (preprocessed on the flags of src/zzz.cpp)" in report)
        expect("a unification to an equal body needs no row",
               "ADMITTED macro unification: include/crucible/safety/UniSame.h" in report)
        for name, reason, label in (
                ("UniLacks", "UNI_OLD_ONLY is removed, but no header that the added includes pull in defines it",
                 "a macro that no added include defines"),
                ("UniNonMacro", "not part of a macro definition: int uni = 1;", "a changed non-macro line"),
                ("UniBadInclude", "not an include of a foundation header: #include <crucible/safety/Twin.h>",
                 "an added include outside foundation"),
                ("UniMissing", "includes foundation/contracts/Missing.h, which does not exist",
                 "an added include of a missing foundation header"),
                ("UniPartial", "the definition of UNI_TWO is removed in part, not whole",
                 "a definition removed in part"),
                ("UniCommentOnly", "UNI_COMMENTED is removed, but no header that the added includes pull in",
                 "a macro that foundation defines only inside a comment"),
                ("UniInComment", "not part of a macro definition: #define UNI_ONE(c) ((void)0)",
                 "the removal of a definition inside a block comment"),
                ("UniInRaw", "not part of a macro definition: #define UNI_ONE(c) ((void)0)",
                 "the removal of a definition inside a raw string"),
                ("UniIncludeInComment", "not an include of a foundation header: #include <foundation/contracts/Uni.h>",
                 "an added include inside a block comment"),
                ("UniSwap", "UNI_STRONG is removed, but no header that the added includes pull in defines it",
                 "a swap to an include that does not define the name"),
                ("UniNoRow", f"the body of UNI_THREE changes, and no row of {UNIFICATION_TABLE} admits it",
                 "a body change with no row"),
                ("UniStaleRow", "names a new body that no header the added includes pull in defines",
                 "a row whose new body no longer matches the foundation definition"),
                # No unit reads UniDead.h, so the flags come from the first
                # unit with include/ on its path, which is src/aaa.cpp.
                ("UniDead", "UNI_DEAD is not defined after the change, under the flags of src/aaa.cpp",
                 "a foundation definition in an arm the compiler never takes"),
                ("UniArm", "the arm `( c ) ( ( void ) 0 )` of UNI_ARM equals no removed arm",
                 "a foundation arm weaker than the removed body"),
                ("UniRelease", "the arm `( x ) ( ( void ) 0 )` of UNI_CHECK equals no removed arm",
                 "an NDEBUG arm that drops the check, under flags without NDEBUG"),
                ("UniSwapArms", "the active body of UNI_SW changes, and no row",
                 "two arms that swap their conditions")):
            expect(f"refused: {label}", reason in refusal(report, f"include/crucible/safety/{name}.h"), True)
        expect("exactly sixteen unification refusals", report.count("FROZEN violation:") == 16)
        expect("exactly three admitted unifications", report.count("ADMITTED macro unification:") == 3)
        with (root / UNIFICATION_TABLE).open("a", encoding="utf-8") as table:
            table.write(f"UNI_CHECK — {base3} — ( x ) check ( x ) — ( x ) ( ( void ) 0 ) — a reviewed release arm\n")
        code, report = captured(root, base3)
        expect("a row for the NDEBUG arm admits the unification",
               "ADMITTED macro unification: include/crucible/safety/UniRelease.h" in report)
        code, report = captured(root, base3, database=root / "no-such-db.json")
        expect("no compile database refuses a unification",
               "there is no compile database" in refusal(report, "include/crucible/safety/UniPlain.h"), True)
        expect("the unification report from / equals the one from the repository",
               captured(root, base3, Path("/")) == captured(root, base3, root))

        for text, label in (("UNI_ONE — only three — fields\n", "a row with the wrong field count"),
                            ("UNI_ONE — 0123456789abcdef0123456789abcdef01234567 — ( c ) — ( c ) — gone\n",
                             "a row naming a commit that is not in the repository")):
            write(root, UNIFICATION_TABLE, text)
            expect(f"rot: {label}", len(Table(root).rot) == 1, True)
        code, report = captured(root, base3)
        expect("table rot fails the scan", code == 1 and "UNIFICATION-TABLE: line 1" in report, True)

        code, report = captured(root, "0" * 40)
        expect("a base missing from a full clone fails with exit 2",
               code == 2 and "the clone is not shallow" in report, True)
        shallow = root / "shallow"
        subprocess.run(["git", "clone", "-q", "--depth", "1", f"file://{root}", str(shallow)], env=env,
                       capture_output=True, check=True)
        code, report = captured(shallow, base)
        expect("a base missing from a shallow clone exits 3", code == 3 and "shallow checkout" in report, True)
        (root / PATHS_FILE).unlink()
        code, report = captured(root, base3)
        expect("a missing frozen list fails with exit 2", code == 2 and PATHS_FILE in report, True)
    if failures:
        print(f"check-frozen-tree --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-frozen-tree --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def print_mirror_hunks(root: Path, base: str, path: str) -> int:
    """Print each hunk of a frozen file against its base twin with its ledger key, for a new ledger row.

    Returns:
        0 when the file has a base twin, 2 otherwise
    """
    snapshot = Snapshot(root, base, frozen_prefixes(root))
    twin = snapshot.base_twin(path)
    if twin is None or not (root / path).is_file():
        print(f"check-frozen-tree: {path} has no base twin in {base}, so no mirror row can admit it.",
              file=sys.stderr)
        return 2
    base_text, new_text = snapshot.base_text(twin), snapshot.disk_text(path)
    old, new = normalized(base_text), normalized(new_text)
    includes = include_rows(base_text)
    keys = []
    for key, opcode in mirror_hunks(base_text, new_text):
        _, i1, i2, j1, j2 = opcode
        kind = removed_headers(snapshot, old, includes, opcode)
        print(f"{key}  base lines {i1 + 1}-{i2}, file lines {j1 + 1}-{j2}"
              f"{'  (an include removal: it needs no key)' if kind else ''}")
        keys += [] if kind else [key]
        for line in old[i1:i2]:
            print(f"    -{line}")
        for line in new[j1:j2]:
            print(f"    +{line}")
    print(f"keys: {','.join(keys)}")
    return 0


def main(argv: list[str]) -> int:
    """Run the scan against the freeze base, the self-test, or print the mirror hunks of one file."""
    database = tsast.REPO_ROOT / DEFAULT_COMPILE_DB
    if argv[:1] == ["--compile-db"] and len(argv) == 2:
        database, argv = Path(argv[1]).resolve(), []
    hunks_of = argv[1] if argv[:1] == ["--mirror-hunks"] and len(argv) == 2 else None
    if hunks_of is None and argv not in ([], ["--self-test"]):
        print("usage: check-frozen-tree.py [--self-test | --compile-db PATH | --mirror-hunks PATH]",
              file=sys.stderr)
        return 2
    try:
        if hunks_of is not None:
            return print_mirror_hunks(tsast.REPO_ROOT, FREEZE_BASE, hunks_of)
        return self_test() if argv else run(tsast.REPO_ROOT, FREEZE_BASE, database)
    except tsast.KitMissing as exc:
        print(f"check-frozen-tree: {exc}", file=sys.stderr)
        return 3
    except Refused as exc:
        print(f"check-frozen-tree: {exc}", file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
