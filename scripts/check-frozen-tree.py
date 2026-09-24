#!/usr/bin/env python3
"""check-frozen-tree — the old substrate is frozen until it is deleted.

The canonical substrate lives in include/foundation/ and include/fixy/.  While
the old one coexists with it, the old one is frozen: a bug found in old code is
fixed in the new tree or not at all, and nothing is added to the old tree.  A
deletion passes, because the old tree ends by deletion.  The frozen prefixes are
the lines of scripts/frozen-paths.txt, the one list that this guard,
scripts/check-flip-list.py and scripts/check-start-lifetime.sh share.

HOW THE CHANGE SET IS READ
    The guard compares the tree of the freeze base with the files on disk.  It
    does not read the git index.  A file under a frozen prefix is changed when
    its content hash differs from the base blob at its path, and it is added
    when the base holds no blob at that path.  The file list is every tracked
    and untracked file that git does not ignore.  A file that is missing from
    the index still counts, so a stale index cannot hide an edit.

THREE CHANGES ARE ADMITTED
    1. The superseded marking.  A port renames an old header dir/Name.h to
       dir/_Name.h.  A file whose content equals its base twin, once each
       include of the old tree has the leading underscore stripped, passes.
    2. A soundness mirror.  scripts/frozen-soundness-mirrors.txt lists a
       frozen file that holds a live bug the new tree already fixed, in the
       shape `path — new-tree fix — reason`.  A listed file passes when the
       base holds it or its base twin, and a note names the reason.  The
       ledger fails closed: an entry that names a path outside every frozen
       prefix, or a path that does not exist, fails the guard.
    3. A macro unification.  A frozen header drops macro definitions, and
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

WHAT THE PARSER READS
    A macro definition and an include are nodes of the pinned tree-sitter
    kit, so a `#define` inside a comment or a raw string is not a definition,
    and a continuation line belongs to its definition by the parse, not by a
    count of backslashes.  A body compares as a token list: the parameter list,
    then the replacement list, one space between tokens, with `= ` before the
    body of an object-like macro.  A header that does not parse refuses the
    unification, because its macros are then unknown.

THE COMPILE DATABASE
    --compile-db names it, and build/compile_commands.json is the default.  The
    guard uses the entry of the first translation unit that includes the header
    by its path, else the first entry whose flags name include/ as a directory.
    Without a database, or when the compiler fails, a unification is refused.

Exit 0 clean, 1 on a change under a frozen prefix or a bad ledger or table
entry, 2 on a usage error, a missing frozen list or a failed self-test, 3 when
the freeze base is not in this clone (a shallow checkout) or the parser kit is
missing.
"""

from __future__ import annotations

import contextlib
import difflib
import io
import json
import os
import re
import shlex
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cxx_lex  # noqa: E402
import tsast  # noqa: E402

FREEZE_BASE = "1e0cd65e975f53d8838cb4d22e0aad9233374e35"
PATHS_FILE = "scripts/frozen-paths.txt"
MIRROR_LEDGER = "scripts/frozen-soundness-mirrors.txt"
UNIFICATION_TABLE = "scripts/frozen-macro-unifications.txt"
DEFAULT_COMPILE_DB = "build/compile_commands.json"
# An include of the old tree whose last path component carries the marking.
MARKED_INCLUDE = re.compile(r"^(\s*#\s*include\s*<crucible/(?:[^>]*/)?)_([^/>]+>.*)$")
DEFINITIONS = ("preproc_def", "preproc_function_def")
DEFINE_HEAD = re.compile(r"^\s*#\s*define\s+([A-Za-z_]\w*)(\()?")
UNDEF_HEAD = re.compile(r"^\s*#\s*undef\s+([A-Za-z_]\w*)")
# Maximal munch over the punctuators that a pp-token can hold.
PUNCTUATOR = re.compile(r">>=|<<=|<=>|->\*|\.\.\.|::|##|->|\+\+|--|<<|>>|<=|>=|==|!=|&&|\|\||[-+*/%&|^]=|\.\*|\S")
LINEMARKER = re.compile(r'^# \d+ "((?:\\.|[^"\\])*)"')
NEG_FIXTURE = re.compile(r"(?:^|/)(?:neg|[^/]+_neg)/")
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


def normalized(text: str) -> list[str]:
    """Return the lines with the superseded marking stripped from each old-tree include."""
    return [MARKED_INCLUDE.sub(r"\1\2", line) for line in lines_of(text)]


def twin_of(path: str) -> str | None:
    """Return dir/Name for dir/_Name, or None when the name carries no marking."""
    pure = PurePosixPath(path)
    return str(pure.with_name(pure.name[1:])) if pure.name.startswith("_") else None


def pp_tokens(text: str) -> list[str]:
    """Return the preprocessing tokens of a text, without its comments.

    Complexity: linear in the length of the text.
    """
    joined, _ = cxx_lex.splice(text)
    found: list[str] = []
    cursor = 0
    for match in cxx_lex.LEXER.finditer(joined):
        found += PUNCTUATOR.findall(joined[cursor:match.start()])
        if match.lastgroup not in ("line_comment", "block_comment"):
            found.append(match.group())
        cursor = match.end()
    found += PUNCTUATOR.findall(joined[cursor:])
    return found


def canonical(definition: str) -> tuple[str, str] | None:
    """Return (name, canonical body) for the text of one #define, or None for other text.

    A function-like macro keeps its parameter list at the front of the body.
    An object-like body starts with '= ', so `#define X (a)` and
    `#define X(a)` never compare equal.
    """
    joined, _ = cxx_lex.splice(definition)
    head = DEFINE_HEAD.match(joined)
    if head is None:
        return None
    body = " ".join(pp_tokens(joined[head.end(1):]))
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


def ledger_entries(root: Path) -> list[tuple[str, str]]:
    """Return (path, reason) for each entry of the soundness-mirror ledger."""
    ledger = root / MIRROR_LEDGER
    if not ledger.is_file():
        return []
    entries = []
    for line in ledger.read_text().splitlines():
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        path = line.split("—", 1)[0].strip()
        if path:
            entries.append((path, line.rsplit("—", 1)[-1].strip()))
    return entries


def ledger_rot(root: Path, prefixes: list[str]) -> list[str]:
    """Return one report line for each ledger entry that can admit no real edit."""
    rot = []
    for path, _ in ledger_entries(root):
        if not is_frozen(path, prefixes):
            rot.append(f"SOUNDNESS-MIRROR-LEDGER: {path} — not under a frozen directory, so a mirror "
                       f"admission means nothing.  Name the frozen file, or remove the entry.")
        elif not (root / path).is_file():
            rot.append(f"SOUNDNESS-MIRROR-LEDGER: {path} — does not exist, so the entry admits no edit and "
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
        read = canonical(node.text)
        if read is not None:
            end_row, end_col = node.end
            found.append((read[0], node.start[0], end_row - 1 if end_col == 0 else end_row, read[1]))
    return found


def foundation_includes(tree: tsast.Tree) -> dict[int, str]:
    """Return the row and the header of each `#include <foundation/...>` in a parse."""
    rows = {}
    for node in tree.find("preproc_include"):
        target = node.child_by_field("path")
        header = target.text.strip("<>") if target is not None and target.type == "system_lib_string" else ""
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
        self.entries: list[dict] = []
        self.problem: str | None = None
        self.sources: dict[str, str] = {}
        if not database.is_file():
            self.problem = f"there is no compile database at {database}, so the preprocessor check cannot run " \
                           f"(pass --compile-db)"
            return
        try:
            # A negative fixture compiles to fail, so a plain translation unit
            # comes first when the guard picks the flags of a header.
            self.entries = sorted(json.loads(database.read_text()),
                                  key=lambda entry: (NEG_FIXTURE.search(entry["file"]) is not None, entry["file"]))
        except (ValueError, KeyError, TypeError) as exc:
            self.problem = f"the compile database {database} does not read: {exc}"

    def entry_for(self, rel: str) -> tuple[Path, list[str], str] | str:
        """Pick the flags for a header: (working directory, argv without output options, label).

        Returns:
            The choice, or the reason no entry fits
        """
        if self.problem is not None:
            return self.problem
        spelling = str(PurePosixPath(rel).relative_to("include")) if rel.startswith("include/") else rel
        include_dir = str((self.root / "include").resolve())
        direct, fallback = None, None
        for entry in self.entries:
            source = Path(entry["directory"]) / entry["file"]
            argv = entry.get("arguments") or shlex.split(entry.get("command", ""))
            if fallback is None and any(include_dir in arg or
                                        (prev in ("-I", "-isystem") and str(Path(entry["directory"], arg)
                                                                            .resolve()) == include_dir)
                                        for prev, arg in zip([""] + argv, argv)):
                fallback = (entry, source, argv)
            key = str(source)
            if key not in self.sources:
                self.sources[key] = source.read_text(errors="replace") if source.is_file() else ""
            if spelling in self.sources[key]:
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
            read = canonical(line)
            if read is not None:
                defined[read[0]] = (current, read[1])
            elif (undone := UNDEF_HEAD.match(line)) is not None:
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


def unification(snapshot: Snapshot, path: str, context: Context) -> tuple[bool, list[str]]:
    """Decide whether the change to a frozen file is a macro unification.

    Complexity: one line diff of the file against its base twin, two parses,
    the parse of the include closure of the added headers, and at most three
    runs of the preprocessor.

    Returns:
        (True, [verdict]) when admitted, or (False, reasons) when refused
    """
    twin = snapshot.base_twin(path)
    if twin is None:
        return False, []
    base_text, new_text = snapshot.base_text(twin), snapshot.disk_text(path)
    with tempfile.TemporaryDirectory() as work:
        old_file, new_file = Path(work) / "base.h", Path(work) / "new.h"
        old_file.write_text(base_text, encoding="utf-8")
        new_file.write_text(new_text, encoding="utf-8")
        old_tree, new_tree = list(tsast.parse([old_file, new_file], strict=False))
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
    for tag, i1, i2, j1, j2 in difflib.SequenceMatcher(a=old, b=new, autojunk=False).get_opcodes():
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
    ledger = dict(ledger_entries(root))
    snapshot = Snapshot(root, base, prefixes)
    for path in snapshot.disk:
        if snapshot.base_blobs.get(path) == snapshot.disk_blobs[path] or snapshot.is_marking(path):
            continue
        change = "modified" if snapshot.base_twin(path) is not None else "added"
        if path in ledger and snapshot.base_twin(path) is not None:
            print(f"check-frozen-tree: ADMITTED soundness mirror: {path} — {ledger[path]} (per {MIRROR_LEDGER}).",
                  file=sys.stderr)
            continue
        admitted, notes = unification(snapshot, path, context)
        if admitted:
            print(f"check-frozen-tree: ADMITTED macro unification: {path} — {notes[0]}.", file=sys.stderr)
            continue
        print(f"FROZEN violation: {path} — {change} under a frozen path.  The old substrate only shrinks; put "
              f"the change in the new tree.  The permitted edits are the _ marking of a ported file and of its "
              f"includes, a file listed in {MIRROR_LEDGER}, and a macro unification.", file=sys.stderr)
        for note in notes[:3]:
            print(f"    not a macro unification: {note}", file=sys.stderr)
        if len(notes) > 3:
            print(f"    not a macro unification: {len(notes) - 3} more lines", file=sys.stderr)
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
        root = Path(work)
        sh(root, "init", "-q")
        write(root, PATHS_FILE, "# planted\ninclude/crucible/safety/\ninclude/crucible/fixy/\n"
                                "src/fixy/_Fs.cpp\nsrc/fixy/_Io.cpp\nexamples/fn/\n")
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
            "include/crucible/safety/Mirror2.h": "#pragma once\n// mirror2 base ported\n",
            "include/crucible/Root.h": "#pragma once\n// root\n",
            "include/crucible/safety/RootIncluder.h": "#pragma once\n#include <crucible/Root.h>\n// roots\n",
            "include/crucible/safety/Hidden.h": "// hidden\n",
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
        write(root, "include/crucible/safety/Mirror.h", "#pragma once\n// mirror edited\n")
        sh(root, "mv", "include/crucible/safety/Mirror2.h", "include/crucible/safety/_Mirror2.h")
        write(root, "include/crucible/safety/_Mirror2.h", "#pragma once\n// mirror2 ported, and edited\n")
        write(root, MIRROR_LEDGER,
              "# planted\n"
              "include/crucible/safety/Mirror.h — include/foundation/Mirror.h — a live bug already fixed\n"
              "include/crucible/safety/_Mirror2.h — include/foundation/Mirror2.h — fixed in the ported file\n")
        sh(root, "add", "-A")
        # The stale-index hole: a file dropped from the index and then edited.
        # A diff against the index calls it deleted and passes it.
        sh(root, "rm", "-q", "--cached", "include/crucible/safety/Hidden.h")
        write(root, "include/crucible/safety/Hidden.h", "// hidden, and edited\n")
        code, report = captured(root, base)
        expect("the planted tree fails", code == 1)
        for path, label in (("include/crucible/safety/Old.h", "a modify under a frozen directory"),
                            ("include/crucible/safety/New.h", "an untracked add under a frozen directory"),
                            ("include/crucible/fixy/Moved.h", "a rename into a frozen directory"),
                            ("src/fixy/_Fs.cpp", "a modify of a frozen single file"),
                            ("include/crucible/safety/_Tampered.h", "a marking that also edits content"),
                            ("include/crucible/safety/Hidden.h", "an edit to a file missing from the index")):
            expect(f"caught: {label}", f"violation: {path}" in report, True)
        for path, label in (("include/crucible/fixy/Gone.h", "a deletion"),
                            ("include/foundation/Fine.h", "an add in the new tree"),
                            ("include/crucible/safety/_Twin.h", "a plain superseded marking"),
                            ("src/fixy/_Io.cpp", "a marking into a frozen single-file prefix"),
                            ("include/crucible/safety/_Ported.h", "a marking whose include followed"),
                            ("include/crucible/safety/Includer.h", "an include-only edit that follows a marking"),
                            ("include/crucible/safety/RootIncluder.h",
                             "an include edit that follows a root-level marking"),
                            ("include/crucible/safety/Mirror.h", "a ledgered modify"),
                            ("include/crucible/safety/_Mirror2.h", "a ledgered edit to an already-ported file")):
            expect(f"not caught: {label}", f"violation: {path}" not in report)
        expect("the ledgered modify is admitted with its reason",
               "ADMITTED soundness mirror: include/crucible/safety/Mirror.h — a live bug already fixed" in report)
        expect("the ledgered ported edit is admitted",
               "ADMITTED soundness mirror: include/crucible/safety/_Mirror2.h" in report)
        expect("a valid ledger raises no rot", "SOUNDNESS-MIRROR-LEDGER:" not in report)
        expect("exactly six violations", report.count("FROZEN violation:") == 6)
        expect("the report from / equals the report from the repository",
               captured(root, base, Path("/")) == captured(root, base, root))

        for text, name, label in (
                ("include/crucible/safety/Ghost.h — ref — not here\n", "include/crucible/safety/Ghost.h",
                 "a ledger entry for a missing path"),
                ("include/foundation/Fine.h — ref — not frozen\n", "include/foundation/Fine.h",
                 "a ledger entry for a path that is not frozen")):
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
        cxx = os.environ.get("FROZEN_TREE_CXX", "c++")
        write(root, DEFAULT_COMPILE_DB, json.dumps([{
            "directory": str(root), "file": "src/tu.cpp",
            "arguments": [cxx, "-std=c++20", "-I", "include", "-c", "src/tu.cpp", "-o", "tu.o"]}]))
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
        expect("a unification of a file marked in the same change is admitted",
               "ADMITTED macro unification: include/crucible/safety/_UniPorted.h" in report)
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
                ("UniDead", "UNI_DEAD is not defined after the change, under the flags of src/tu.cpp",
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


def main(argv: list[str]) -> int:
    """Run the scan against the freeze base, or the self-test."""
    database = tsast.REPO_ROOT / DEFAULT_COMPILE_DB
    if argv[:1] == ["--compile-db"] and len(argv) == 2:
        database, argv = Path(argv[1]).resolve(), []
    if argv not in ([], ["--self-test"]):
        print("usage: check-frozen-tree.py [--self-test | --compile-db PATH]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else run(tsast.REPO_ROOT, FREEZE_BASE, database)
    except tsast.KitMissing as exc:
        print(f"check-frozen-tree: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
