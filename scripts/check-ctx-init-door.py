#!/usr/bin/env python3
"""check-ctx-init-door — only a reviewed entry point opens the init or the background door.

include/foundation/effects/Effect.h gives the init context one production door,
host::InitOwner::mint_init_context(), and the background context one production
door, host::BackgroundOwner::mint_background_context().  Each key has a private
constructor, and its friends are its owner and the test witness.  So production
code gets an init or a background context only through its door.  An init
context permits the effects of process startup, and a background context
permits the effects of a background thread, so a call of a door gives those
effects to the scope that makes the call.  Each call of the init door must be in
a process entry point, and each call of the background door must be in the
entry function of a background thread, that a reviewer admitted.

THE RULE (each owner is checked alone, against its own allowlist)
    - A use is the identifier of the owner (InitOwner or BackgroundOwner) in
      the code of a file under the trees of production code:
      include/foundation, include/fixy, include/crucible, src, vessel, tools
      and examples.  A comment or a literal is not a use.  Each door is a
      static member, so a call always contains the name of the owner.  A type
      alias, a using-declaration and a friend declaration also contain it,
      so each of them is a use too.
    - The scope of a use is the function whose definition encloses it, as the
      parse gives it: the names of the enclosing classes, then the name of the
      declarator.  A use in no function has the scope namespace-scope.  A use
      in a macro body has the scope macro, because the parse cannot tell where
      the macro expands.
    - Each scope with a use needs a row `PATH SCOPE [xN] — REASON` in the
      allowlist of the owner: scripts/ctx-init-door-allowlist.txt for the init
      owner, scripts/ctx-bg-door-allowlist.txt for the background owner.  N is
      the number of uses that the row admits, and it is 1 when the row gives
      no count.  A new use in a listed scope is more than the count, and it
      fails.  A row in the allowlist of one owner admits no use of the other.
    - A row that admits more uses than its scope has is stale, and it fails.
      So the list becomes shorter when the code does.
    - For each file, the count from the parse must equal the count from the
      lexer.  A difference means that the parse lost a use, and the guard
      fails.
    - A file with a use that the parser cannot read fails, because the guard
      cannot find the scope of the use.

The guard does not scan test/, bench/ and fuzz/.  The test door serves them,
and a test of the door is not production code.

What this guard does not see: a name of the owner that a macro makes by token
pasting.  The scan reads the source text, and review sees such a macro.

Exit 0 clean, 1 on an unlisted use, a stale row or a parse failure, 2 on a
usage error, a malformed row or a failed self-test, 3 when the parser kit is
missing.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import sys
import tempfile
from collections import defaultdict
from pathlib import Path
from typing import NamedTuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cxx_lex  # noqa: E402
import tsast  # noqa: E402

SCAN_DIRS = ("include/foundation", "include/fixy", "include/crucible", "src", "vessel", "tools", "examples")
SUFFIXES = frozenset({".h", ".hh", ".hpp", ".hxx", ".inl", ".ipp", ".tpp", ".c", ".cc", ".cpp", ".cxx", ".cppm"})
NAMESPACE_SCOPE = "namespace-scope"
MACRO_SCOPE = "macro"
CLASSES = ("class_specifier", "struct_specifier", "union_specifier")
MACROS = ("preproc_def", "preproc_function_def")
ENTRY = re.compile(r"^(?P<path>\S+)\s+(?P<scope>\S+?)(?:\s+x(?P<count>[1-9][0-9]*))?\s+—\s+\S")

Key = tuple[str, str]


class Door(NamedTuple):
    """One door: the owner whose name marks a use, the allowlist of its scopes, and what it gives."""

    owner: str
    allowlist: str
    context: str
    effects: str
    callers: str


DOORS = (
    Door("InitOwner", "scripts/ctx-init-door-allowlist.txt", "init", "the effects of process startup",
         "Call it only from a process entry point"),
    Door("BackgroundOwner", "scripts/ctx-bg-door-allowlist.txt", "background", "the effects of a background thread",
         "Call it only from the entry function of a background thread"),
)
INIT_DOOR, BG_DOOR = DOORS


class Refused(Exception):
    """The allowlist has a malformed or a repeated row (exit 2)."""


def read_allowlist(root: Path, door: Door) -> dict[Key, tuple[int, int]]:
    """Return the count and the line of each row of the allowlist of one door, by (path, scope).

    Raises:
        Refused: If a row does not parse, or two rows have one path and one scope
    """
    allowlist = root / door.allowlist
    entries: dict[Key, tuple[int, int]] = {}
    for number, raw in enumerate(allowlist.read_text().splitlines() if allowlist.is_file() else [], 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = ENTRY.match(line)
        if match is None:
            raise Refused(f"{door.allowlist}:{number} is not a row.  A row is PATH SCOPE [xN] — REASON.")
        key = (match.group("path"), match.group("scope"))
        if key in entries:
            raise Refused(f"{door.allowlist}:{number} repeats the row at line {entries[key][1]}.  "
                          f"Give one row a count.")
        entries[key] = (int(match.group("count") or 1), number)
    return entries


def candidate_files(root: Path, owner: str) -> list[Path]:
    """Return each file under the scan roots whose text contains the name of the owner, in sorted order.

    Complexity: one read of each file under the scan roots.
    """
    found = []
    for directory in SCAN_DIRS:
        base = root / directory
        if not base.is_dir():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix in SUFFIXES and path.is_file() and owner in path.read_text(errors="replace"):
                found.append(path)
    return found


def lexed_lines(path: Path, owner: str) -> list[int]:
    """Return the line of each use of the owner in the code of the file, from the lexer.

    Complexity: linear in the length of the file.
    """
    joined, joins = cxx_lex.splice(path.read_text(errors="replace"))
    _, hits = cxx_lex.blank(joined, frozenset({owner}), blank_literals=True)
    return [cxx_lex.line_of(joined, joins, offset) for offset in hits]


def spelled(text: str) -> str:
    """Return a name as its tokens only, with the comments and the white space removed."""
    return re.sub(r"\s+", "", cxx_lex.blank(text)[0])


def scope_of(node: tsast.Node) -> str:
    """Return the scope of one use: the enclosing classes and function, or namespace-scope."""
    function = node.ancestor_of_type("function_definition")
    if function is None:
        return NAMESPACE_SCOPE
    declarator = function.child_by_field("declarator")
    if declarator is None:
        return NAMESPACE_SCOPE
    target = declarator if declarator.type == "function_declarator" else next(
        declarator.descendants("function_declarator"), None)
    name_node = target.child_by_field("declarator") if target is not None else None
    parts = [spelled((name_node or declarator).text)]
    current = function.parent
    while current is not None:
        if current.type in CLASSES:
            name = current.child_by_field("name")
            if name is not None:
                parts.insert(0, spelled(name.text))
        current = current.parent
    return "::".join(parts)


def parsed_uses(tree: tsast.Tree, owner: str) -> list[tuple[str, int]]:
    """Return (scope, line) for each use of the owner that the parse holds, in the order of the file.

    An identifier of each kind counts: a type, a namespace qualifier, a field
    and a plain identifier.  A macro body is raw text in the parse, so the
    guard reads that text with the lexer.

    Complexity: linear in the number of nodes of the tree.
    """
    identifier_types = {kind for kind in set(tree.types) if kind.endswith("identifier")}
    owner_text = re.compile(rf"\b{owner}\b")
    found = [(scope_of(node), node.line) for node in tree.find(*identifier_types) if node.text == owner]
    for node in tree.find(*MACROS):
        body, _ = cxx_lex.blank(node.text, blank_literals=True)
        found += [(MACRO_SCOPE, node.line + body.count("\n", 0, match.start()))
                  for match in owner_text.finditer(body)]
    return sorted(found, key=lambda use: use[1])


def scan(root: Path, door: Door) -> tuple[dict[Key, list[int]], list[str]]:
    """Return the lines of the uses of one owner by (path, scope), and a line for each file the guard cannot read.

    Complexity: one lexical pass over each candidate file, and one parse of it.
    """
    lexed_by_path = {path: lines for path in candidate_files(root, door.owner)
                     if (lines := lexed_lines(path, door.owner))}
    uses: dict[Key, list[int]] = defaultdict(list)
    problems: list[str] = []
    for tree in tsast.parse(list(lexed_by_path), strict=False):
        path = Path(tree.path)
        rel = path.relative_to(root).as_posix()
        lexed = lexed_by_path[path]
        if tree.diagnostic is not None:
            problems.append(f"PARSE     {rel}:{lexed[0]} — the parser cannot read the file, so the guard cannot "
                            f"find the scope of its use of the {door.context} owner: {tree.diagnostic}")
            continue
        parsed = parsed_uses(tree, door.owner)
        if len(parsed) != len(lexed):
            problems.append(f"PARSE     {rel} — the lexer finds {len(lexed)} use(s) of the {door.context} owner and "
                            f"the parse finds {len(parsed)}, so the parse lost a use and the guard cannot find its "
                            f"scope.")
            continue
        for scope, line in parsed:
            uses[(rel, scope)].append(line)
    return uses, problems


def check_door(root: Path, door: Door) -> int:
    """Compare the uses of one owner with its allowlist, and report to stderr.

    Returns:
        0 clean, 1 on a finding, 2 on a bad allowlist
    """
    try:
        entries = read_allowlist(root, door)
    except Refused as exc:
        print(f"check-ctx-init-door: {exc}", file=sys.stderr)
        return 2
    uses, problems = scan(root, door)
    for (rel, scope), lines in sorted(uses.items()):
        admitted = entries.get((rel, scope), (0, 0))[0]
        if len(lines) > admitted:
            problems.append(f"UNLISTED  {rel}:{', '.join(map(str, lines))} — the scope {scope} names the "
                            f"{door.context} owner {len(lines)} time(s), and its row in {door.allowlist} admits "
                            f"{admitted}.  The {door.context} door gives {door.effects} to its caller.  "
                            f"{door.callers}, and give that entry point a row with a sentence that says why.")
    for (rel, scope), (admitted, number) in sorted(entries.items(), key=lambda item: item[1][1]):
        held = len(uses.get((rel, scope), []))
        if held < admitted:
            problems.append(f"STALE     {door.allowlist}:{number} admits {admitted} use(s) in the scope {scope} of "
                            f"{rel}, and the scope has {held}.  Lower or remove the row.")
    for line in problems:
        print(f"check-ctx-init-door: {line}", file=sys.stderr)
    total = sum(len(lines) for lines in uses.values())
    print(f"check-ctx-init-door: {total} use(s) of the {door.context} owner in {len(uses)} scope(s), "
          f"{'refused' if problems else 'each one admitted'}.", file=sys.stderr)
    return 1 if problems else 0


def check(root: Path) -> int:
    """Check each door against its own allowlist, and return the worst verdict.

    Returns:
        0 clean, 1 on a finding, 2 on a bad allowlist
    """
    return max(check_door(root, door) for door in DOORS)


def self_test() -> int:
    """Plant a tree, prove each verdict, and prove the verdict does not depend on the working directory.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0
    door = "foundation::effects::host::InitOwner::mint_init_context()"
    bg_door = "foundation::effects::host::BackgroundOwner::mint_background_context()"
    ALLOWLIST = INIT_DOOR.allowlist  # noqa: N806
    BG_ALLOWLIST = BG_DOOR.allowlist  # noqa: N806

    def write(root: Path, rel: str, text: str) -> None:
        """Write one planted file."""
        (root / rel).parent.mkdir(parents=True, exist_ok=True)
        (root / rel).write_text(text, encoding="utf-8")

    def captured(root: Path, cwd: Path | None = None) -> tuple[int, str]:
        """Run the check on the planted tree and keep its report."""
        buffer = io.StringIO()
        previous = Path.cwd()
        if cwd is not None:
            os.chdir(cwd)
        try:
            with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(buffer):
                code = check(root)
        finally:
            os.chdir(previous)
        return code, buffer.getvalue()

    def expect(root: Path, code: int, needles: list[str], name: str, negative: bool = False) -> None:
        """Record one case: the exit code and each text that the report must contain."""
        nonlocal negatives
        negatives += negative
        got, report = captured(root)
        ok = got == code and all(needle in report for needle in needles)
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(f"{name}: expected exit {code} and {needles}, got exit {got}:\n{report}")

    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        write(root, "include/foundation/effects/Effect.h",
              "#pragma once\nnamespace foundation::effects {\nnamespace host { struct InitOwner; }\n"
              "class init_key { friend struct ::foundation::effects::host::InitOwner; };\n"
              "namespace host { struct InitOwner final { static int mint_init_context() noexcept { return 0; } }; }\n"
              "}\n")
        write(root, "vessel/torch/api.cpp",
              f'extern "C" {{\nint crucible_create(void) noexcept {{\n    return {door};\n}}\n}}\n')
        write(root, "src/clean.cpp", "// " + door + " opens the door, so this file never calls it.\n"
              f'const char* note = "{door}";\nint clean() {{ return 0; }}\n')
        write(root, "test/t.cpp", f"int t() {{ return {door}; }}\n")
        write(root, "bench/b.cpp", f"int b() {{ return {door}; }}\n")
        write(root, ALLOWLIST, "# planted\n"
              "include/foundation/effects/Effect.h namespace-scope x3 — the definition site\n"
              "vessel/torch/api.cpp crucible_create — the entry point of the C interface\n")
        expect(root, 0, ["4 use(s) of the init owner in 2 scope(s)"],
               "listed scopes pass, and a comment, a literal, a test and a bench are not uses")

        cases = {
            "src/rogue.cpp": (f"int start() {{ return {door}; }}\n", "src/rogue.cpp:1 — the scope start",
                              "the door called from an unlisted file"),
            "vessel/torch/other.cpp": (f"struct Boot {{ int run() {{ return {door}; }} }};\n",
                                       "vessel/torch/other.cpp:1 — the scope Boot::run",
                                       "the door called from a member function of an unlisted file"),
            "src/alias.cpp": ("using Door = foundation::effects::host::InitOwner;\nint f() { return Door::mint_init_context(); }\n",
                              "src/alias.cpp:1 — the scope namespace-scope",
                              "an alias of the owner at namespace scope"),
            "src/macro.cpp": (f"#define OPEN_THE_DOOR() {door}\nint g() {{ return 1; }}\n",
                              "src/macro.cpp:1 — the scope macro", "a macro body that names the owner"),
        }
        for rel, (text, needle, name) in cases.items():
            write(root, rel, text)
            expect(root, 1, [f"UNLISTED  {needle}"], name, True)
            (root / rel).unlink()

        write(root, "vessel/torch/api.cpp",
              f'extern "C" {{\nint crucible_create(void) noexcept {{\n    return {door};\n}}\n'
              f"int crucible_other(void) noexcept {{\n    return {door};\n}}\n}}\n")
        expect(root, 1, ["UNLISTED  vessel/torch/api.cpp:6 — the scope crucible_other"],
               "the door called from a second function of a listed file", True)
        write(root, "vessel/torch/api.cpp",
              f'extern "C" {{\nint crucible_create(void) noexcept {{\n    (void){door};\n    return {door};\n}}\n}}\n')
        expect(root, 1, ["UNLISTED  vessel/torch/api.cpp:3, 4 — the scope crucible_create names the init owner 2 time(s)"],
               "a second call in a listed entry point", True)
        write(root, "vessel/torch/api.cpp",
              f'extern "C" {{\nint crucible_create(void) noexcept {{\n    return {door};\n}}\n}}\n')

        write(root, ALLOWLIST, "# planted\n"
              "include/foundation/effects/Effect.h namespace-scope x4 — the definition site\n"
              "vessel/torch/api.cpp crucible_create — the entry point of the C interface\n"
              "vessel/torch/api.cpp crucible_destroy — never called\n")
        expect(root, 1, [f"STALE     {ALLOWLIST}:2 admits 4 use(s) in the scope namespace-scope",
                         f"STALE     {ALLOWLIST}:4 admits 1 use(s) in the scope crucible_destroy"],
               "a count above the uses and a row with no use are stale", True)
        write(root, ALLOWLIST, "vessel/torch/api.cpp crucible_create\n")
        expect(root, 2, ["is not a row"], "a malformed row", True)
        write(root, ALLOWLIST, "# planted\n"
              "include/foundation/effects/Effect.h namespace-scope x3 — the definition site\n"
              "vessel/torch/api.cpp crucible_create — the entry point of the C interface\n"
              "vessel/torch/api.cpp crucible_create — the same row again\n")
        expect(root, 2, ["repeats the row at line 3"], "a repeated row", True)
        write(root, ALLOWLIST, "# planted\n"
              "include/foundation/effects/Effect.h namespace-scope x3 — the definition site\n"
              "vessel/torch/api.cpp crucible_create — the entry point of the C interface\n")

        write(root, "src/broken.cpp", f"int broken( {{ return {door}; }}}}\n")
        expect(root, 1, ["PARSE     src/broken.cpp:1"], "a file that the parser cannot read fails", True)
        (root / "src/broken.cpp").unlink()

        # The background door is checked against its own allowlist, so a
        # row for the init door admits no call of the background door.
        write(root, "src/thread.cpp", f"struct Worker {{ int run() {{ return {bg_door}; }} }};\n")
        expect(root, 1, ["UNLISTED  src/thread.cpp:1 — the scope Worker::run names the background owner 1 time(s)",
                         "4 use(s) of the init owner in 2 scope(s), each one admitted"],
               "the background door called from an unlisted thread entry", True)
        write(root, BG_ALLOWLIST, "# planted\nsrc/thread.cpp Worker::run — the entry function of the worker thread\n")
        expect(root, 0, ["1 use(s) of the background owner in 1 scope(s), each one admitted"],
               "a listed thread entry opens the background door")
        write(root, "vessel/torch/api.cpp",
              f'extern "C" {{\nint crucible_create(void) noexcept {{\n    return {bg_door};\n}}\n}}\n')
        expect(root, 1, ["UNLISTED  vessel/torch/api.cpp:3 — the scope crucible_create names the background owner",
                         f"STALE     {ALLOWLIST}:3 admits 1 use(s) in the scope crucible_create"],
               "a row of the init allowlist admits no call of the background door", True)
        write(root, "vessel/torch/api.cpp",
              f'extern "C" {{\nint crucible_create(void) noexcept {{\n    return {door};\n}}\n}}\n')
        write(root, BG_ALLOWLIST, "# planted\nsrc/thread.cpp Worker::run — the entry function of the worker thread\n"
              "src/thread.cpp Worker::stop — never called\n")
        expect(root, 1, [f"STALE     {BG_ALLOWLIST}:3 admits 1 use(s) in the scope Worker::stop"],
               "a background row with no use is stale", True)
        (root / "src/thread.cpp").unlink()
        (root / BG_ALLOWLIST).unlink()

        same = captured(root, root) == captured(root, Path("/"))
        print(f"  {'ok  ' if same else 'FAIL'} the report from / equals the report from the tree")
        if not same:
            failures.append("the report depends on the working directory")
    for failure in failures:
        print(f"check-ctx-init-door --self-test: FAIL — {failure}", file=sys.stderr)
    if failures:
        return 2
    print(f"check-ctx-init-door --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Check the tree, or run the self-test."""
    if argv not in ([], ["--self-test"]):
        print("usage: check-ctx-init-door.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-ctx-init-door: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
