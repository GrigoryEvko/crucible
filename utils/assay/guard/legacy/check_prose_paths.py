"""prose-paths — each path that the guidance prose names is a file that exists (`guard check --rule prose-paths`).

A sentence of CLAUDE.md names a file by its path.  A move of the file makes
the sentence send its reader to a file that is not there.  This check makes
the move loud: a path of the prose that does not exist fails, and the message
names the place that the file most likely moved to.

The PATH cell of each ledger row of utils/ledgers/ names a file too.  The
rule ledger-owners (`guard check --rule ledger-owners`) holds those cells, so
this check reads only the prose.

THE DOCUMENTS
    Each regular file *.md at the root of the tree, and each regular file
    *.md under docs/ when it exists: the documents that a session loads as
    guidance, where a dead path misleads.  A symbolic link among them is not
    read.  Its target is a document that the check reads under its own name,
    or one that is out of scope.  AGENTS.md is a link to CLAUDE.md, so a
    dead path of CLAUDE.md is reported one time.

    Out, and why:
      * misc/*.md, the dated design papers.  They record a finding against
        the tree as it stood on their date, and several of them propose
        files that do not exist yet.  A forward reference is the point of the
        sentence, and not a defect in it.
      * a _planned/ directory, for the same reason.

THE PATHS
    A path of the prose starts with one of PATH_ROOTS and a '/', holds the
    characters of PATH_CHARACTERS, ends in one of PROSE_EXTENSIONS, and
    stands before the end of the line or a character that is not a letter,
    a digit, '_' or '-'.  It is anywhere in a sentence, not only at the start
    of a line, so the path must end where its extension ends: without the end
    character, bench/serve.h would match inside bench/serve.html.  A '.' or a
    ':' after the extension ends a path, because a path at the end of a
    sentence and a path with a line number are both usual.  fuzz, lean,
    scripts, tools and toolchain are gone from the root.  They stay in
    PATH_ROOTS, so a sentence that still names one of them fails as a dead
    path.

THE MOVE
    The candidates of a dead path are the files of the tree with the same
    name, which finds a file that moved to a new directory.  A hidden entry
    at the root (.git, the agent work trees in .worktrees) and a build
    directory at the root hold no tracked source, so the check does not name
    a file there.

Usage
    guard check --rule prose-paths
    guard check --rule prose-paths --self-test

Exit 0 when each path exists, 1 on a dead path, 2 on a usage error or a failed self-test.
"""

from __future__ import annotations

import os
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path

from assay.core.paths import REPO_ROOT

PATH_ROOTS = ("include", "src", "test", "bench", "utils", "vessel", "examples", "cmake", "fuzz", "lean", "scripts",
              "tools", "toolchain")
PROSE_EXTENSIONS = ("h", "hpp", "cpp", "cc", "sh", "txt", "py", "json")
LETTERS = frozenset("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789")
PATH_CHARACTERS = LETTERS | frozenset("_/.-")
WORD_CHARACTERS = LETTERS | frozenset("_-")
DOCS = "docs"
MOVE_ADVICE = ("    name the surviving path, so a reader following the sentence arrives somewhere.")
GONE_ADVICE = ("    no file of that name survives anywhere in the tree.  Either the sentence is\n"
               "    describing something deleted, in which case say so, or the path is a\n"
               "    placeholder, in which case write it so it does not read as a path.")
TAIL = """
check-prose-paths found a path that no longer exists.

A guidance document that names a missing file sends its reader nowhere,
which is worse in CLAUDE.md than anywhere else, because every session
loads it.  The rule ledger-owners holds the paths of the ledger rows.

Refresh every artifact that names a path in the same commit as the edit
that moves it.  `python3 utils/bin/loop derive` runs that refresh and
checks it again."""


@dataclass(frozen=True, slots=True)
class Verdict:
    """The count of dead paths, and the lines of the report."""

    dead: int
    report: tuple[str, ...]


# ── The paths of the prose ─────────────────────────────────────────────────


def path_at(line: str, start: int) -> tuple[str, int] | None:
    """Return the longest path of THE PATHS that starts at one place of a line, and the end of the match, or None.

    The end of the match is after the end character, as `grep -o` reads it.
    Complexity: linear in the length of the run of path characters.
    """
    root = next((name for name in PATH_ROOTS if line.startswith(f"{name}/", start)), None)
    if root is None:
        return None
    body = start + len(root) + 1
    stop = body
    while stop < len(line) and line[stop] in PATH_CHARACTERS:
        stop += 1
    for end in range(stop, body, -1):
        candidate = line[start:end]
        dot = candidate.rfind(".")
        if dot <= len(root) + 1 or candidate[dot + 1:] not in PROSE_EXTENSIONS:
            continue
        if end < len(line) and line[end] in WORD_CHARACTERS:
            continue
        return candidate, min(end + 1, len(line))
    return None


def prose_paths(text: str) -> list[str]:
    """Return each path of THE PATHS that a document names, sorted and each one time.

    Complexity: O(n * r) for n characters and the length r of the longest run of path characters.
    """
    found: set[str] = set()
    for line in text.splitlines():
        place = 0
        while place < len(line):
            match = path_at(line, place)
            if match is None:
                place += 1
                continue
            found.add(match[0])
            place = match[1]
    return sorted(found)


# ── The documents and the move ─────────────────────────────────────────────


def documents(root: Path) -> list[Path]:
    """Return each regular *.md file at the root and under docs/, with no symbolic link, sorted."""
    found = [path for path in root.glob("*.md") if path.is_file() and not path.is_symlink()]
    docs = root / DOCS
    if docs.is_dir():
        found += [path for path in docs.rglob("*.md") if path.is_file() and not path.is_symlink()]
    return sorted(found)


def moved_candidates(root: Path, dead: str) -> list[str]:
    """Return each file of the tree with the name of a dead path, relative to the root, outside THE MOVE exclusions.

    Complexity: one walk of the tree.
    """
    name = Path(dead).name
    found: list[str] = []
    for directory, subdirectories, files in os.walk(root):
        if Path(directory) == root:
            subdirectories[:] = [entry for entry in subdirectories
                                 if not entry.startswith(".") and not entry.startswith("build")]
            files = [entry for entry in files if not entry.startswith(".") and not entry.startswith("build")]
        if name in files:
            relative = (Path(directory) / name).relative_to(root).as_posix()
            if relative != dead:
                found.append(relative)
    return sorted(found)


def scan(root: Path) -> Verdict:
    """Read each document, and report each path that does not exist under the root.

    Args:
        root: The root of the tree and of the documents

    Returns:
        The verdict
    """
    report: list[str] = []
    dead = 0
    for document in documents(root):
        shown = document.relative_to(root).as_posix()
        for path in prose_paths(document.read_text(encoding="utf-8", errors="replace")):
            if (root / path).exists():
                continue
            dead += 1
            report.append(f"PROSE dead path: {shown} names {path}, which does not exist.")
            candidates = moved_candidates(root, path)
            if candidates:
                report += [f"    the file appears to have moved to: {candidate}" for candidate in candidates]
                report.append(MOVE_ADVICE)
            else:
                report.append(GONE_ADVICE)
    return Verdict(dead, tuple(report))


# ── The self-test ──────────────────────────────────────────────────────────


def self_test() -> int:
    """Plant a dead path, a live path, a longer name, a placeholder and a link, and check each verdict.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, holds: bool) -> None:
        print(f"  {'ok  ' if holds else 'FAIL'} {name}")
        if not holds:
            failures.append(name)

    with tempfile.TemporaryDirectory(prefix="check-prose-paths-") as work:
        tree = Path(work)
        for path in ("include/other/Moved.h", "include/planted/Live.h", ".worktrees/copy/include/planted/Moved.h",
                     "bench/serve.html"):
            (tree / path).parent.mkdir(parents=True, exist_ok=True)
            (tree / path).write_text("", encoding="utf-8")
        guide = tree / "Guidance.md"
        guide.write_text("The substrate lives in `include/planted/Moved.h` and the live one is\n"
                         "`include/planted/Live.h`.  The dashboard is `bench/serve.html`.  A task\n"
                         "re-targets `include/planted/<Name>.h` once the move happens.\n", encoding="utf-8")
        (tree / "Mirror.md").symlink_to("Guidance.md")
        verdict = scan(tree)
        text = "\n".join(verdict.report)
        expect("one dead path is caught, one time, although a link names its document",
               verdict.dead == 1 and text.count("PROSE dead path") == 1 and "Mirror.md" not in text)
        expect("the dead path and the place that it moved to are named",
               "include/planted/Moved.h" in text and "include/other/Moved.h" in text)
        expect("a copy in a hidden work tree is not named as the move", ".worktrees" not in text)
        expect("a live path, a longer file name and a placeholder are not reported",
               "include/planted/Live.h" not in text and "bench/serve.h " not in text and "Name" not in text)
        guide.write_text("The live header is `include/planted/Live.h` and the dashboard is\n"
                         "`bench/serve.html`.  The same header again is include/planted/Live.h.\n", encoding="utf-8")
        clean = scan(tree)
        expect("a clean document passes, also with a path that ends a sentence", clean == Verdict(0, ()))
    expect("a path ends at its extension, and a '.' or a ':' after it ends the path",
           prose_paths("see utils/a.py: and test/b.cpp. and cmake/x.txt:12") == ["cmake/x.txt", "test/b.cpp",
                                                                               "utils/a.py"])
    expect("a path before a letter, a digit, '_' or '-' is no path",
           prose_paths("bench/serve.html bench/x.hx test/y.py_ test/z.sh-") == [])

    if failures:
        print(f"check-prose-paths --self-test: FAILED, {len(failures)} case(s) did not hold", file=sys.stderr)
        return 2
    print("check-prose-paths: self-test passed — a dead prose path is caught one time although a symbolic link names "
          "its document, the move is named and a hidden work tree is not, and a live path, a path that ends a "
          "sentence, a prefix file name and a placeholder all stay clean.", file=sys.stderr)
    return 0


def main(argv: list[str]) -> int:
    """Check the prose of the tree, or run the self-test.

    Args:
        argv: The words after `guard check --rule prose-paths`

    Returns:
        The exit status
    """
    if argv == ["--self-test"]:
        return self_test()
    if argv:
        print("usage: guard check --rule prose-paths [--self-test]", file=sys.stderr)
        return 2
    verdict = scan(REPO_ROOT)
    for line in verdict.report:
        print(line, file=sys.stderr)
    if verdict.dead:
        print(TAIL, file=sys.stderr)
        return 1
    return 0
