#!/usr/bin/env python3
"""check-ctx-testing-boundary — the testing door stays out of the code that ships.

The testing door, foundation::effects::testing, hands out a context with no
key.  bg(), init(), test() and foreground() each mint a context, and
TestWitness and ForegroundWitness are the friends that reach the keys.  A
context is what every ctx-bound mint checks for, so a use of the door in code
that ships hands out authority that no mint gave.

WHAT COUNTS AS A USE
    The guard reads each file from the parse tree of scripts/tsast.py, so a
    comment, a string literal and an include path are not uses, and a comment
    inside a name does not hide one.  A use is:
      * a factory or a friend named through a namespace called testing, with
        any qualifier before it: one pair of adjacent parts of a name;
      * a friend by its own name, when no such pair holds it;
      * a using-directive whose path passes through a namespace called
        testing, and a namespace alias whose target ends in one;
      * each of these through an alias of a testing namespace.  The aliases
        come from every file of the scan, chains of aliases included, so an
        alias that one header defines still marks a use in another file.
    Every declaration counts, a friend declaration and an unevaluated operand
    of sizeof or decltype too, because each one names the door.  A macro body
    is one text node in the parse, so the guard reads its preprocessing
    tokens with the same rules.

SCOPE
    The C++ files under include/foundation, include/fixy, include/crucible,
    src, vessel, tools and examples.  test/ and bench/ are not read,
    because taking the test path is what they are for.  A file in scope that
    the parser cannot read fails the guard, because its uses are unknown.

THE ALLOWLIST
    scripts/ctx-testing-boundary-allowlist.txt admits the uses of a file:
    `path xN  — reason`, where N is the number of uses the file has (1 when
    absent).  A new use in a listed file exceeds its count and fails, and an
    entry above the count of its file is stale and fails, so the list drains
    with the code.

WHAT THE GUARD CANNOT SEE
    A name of the door that a macro builds with `##`.

Exit 0 clean, 1 on a use that no entry admits, a stale entry or a file the
parser cannot read, 2 on a usage error or a failed self-test, 3 when the
parser kit is missing.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import sys
import tempfile
from collections.abc import Iterator, Sequence
from pathlib import Path
from typing import NamedTuple

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402

SCAN_DIRS = ("include/foundation", "include/fixy", "include/crucible", "src", "vessel", "tools", "examples")
ALLOWLIST = "scripts/ctx-testing-boundary-allowlist.txt"
DOOR = "testing"
MEMBERS = frozenset({"bg", "init", "test", "foreground", "TestWitness", "ForegroundWitness"})
WITNESSES = frozenset({"TestWitness", "ForegroundWitness"})
MACROS = ("preproc_def", "preproc_function_def")
LEAVES = ("identifier", "type_identifier", "namespace_identifier", "field_identifier")
ENTRY = re.compile(r"^(?P<path>\S+?)(?: x(?P<count>[1-9][0-9]*))?\s+—\s+\S")


class Run(NamedTuple):
    """One qualified name in the tokens of a macro body: its parts, their rows, and what surrounds it."""

    parts: tuple[str, ...]
    rows: tuple[int, ...]
    after_using_namespace: bool
    alias_name: str | None


def scope_files(root: Path) -> list[Path]:
    """Return every C++ file under the scan directories, sorted, relative to the root."""
    found: list[Path] = []
    for directory in SCAN_DIRS:
        base = root / directory
        if base.is_dir():
            found += [path.relative_to(root) for path in base.rglob("*")
                      if path.is_file() and tsast.is_in_cpp_scope(path.relative_to(root))]
    return sorted(found)


def macro_bodies(tree: tsast.Tree) -> Iterator[list[tsast.Token]]:
    """Yield the preprocessing tokens of each macro replacement list of a tree.

    A block comment splits a replacement list into several value nodes, so
    the tokens come from the source between the first value and the last
    one.  The comment gives no token, and a backslash-newline inside a name
    joins the name, as translation phase 2 does.
    """
    for define in tree.find(*MACROS):
        values = [child for child in define.children if child.field == "value"]
        if values:
            yield tsast.pp_tokens(tree.slice(values[0].start, values[-1].end), values[0].start[0])


def macro_runs(tokens: list[tsast.Token]) -> list[Run]:
    """Return the qualified names in the tokens of one macro replacement list.

    A run is an identifier, then any number of `::` identifier pairs, with
    an optional leading `::`.  The run records whether `using namespace`
    stands before it, and the alias name when `namespace NAME =` stands
    before it and `;` follows it.

    Complexity: linear in the number of tokens.
    """
    texts = [token.text for token in tokens]
    runs: list[Run] = []
    index = 0
    while index < len(tokens):
        start = index
        if texts[index] == "::":
            index += 1
        parts: list[str] = []
        rows: list[int] = []
        while index < len(tokens) and tokens[index].kind == "identifier":
            parts.append(texts[index])
            rows.append(tokens[index].row)
            if index + 2 < len(tokens) and texts[index + 1] == "::" and tokens[index + 2].kind == "identifier":
                index += 2
            else:
                index += 1
                break
        if not parts:
            index = start + 1
            continue
        using_namespace = start >= 2 and texts[start - 2:start] == ["using", "namespace"]
        alias = texts[start - 2] if (start >= 3 and texts[start - 3] == "namespace" and texts[start - 1] == "="
                                     and tokens[start - 2].kind == "identifier"
                                     and index < len(texts) and texts[index] == ";") else None
        runs.append(Run(tuple(parts), tuple(rows), using_namespace, alias))
    return runs


def alias_targets(trees: Sequence[tsast.Tree]) -> dict[str, set[tuple[str, ...]]]:
    """Return the target of each namespace alias of the scan, by alias name, from the trees and the macro bodies."""
    targets: dict[str, set[tuple[str, ...]]] = {}
    for tree in trees:
        for alias in tsast.namespace_aliases(tree):
            targets.setdefault(alias.name, set()).add(alias.target)
        for tokens in macro_bodies(tree):
            for run in macro_runs(tokens):
                if run.alias_name is not None:
                    targets.setdefault(run.alias_name, set()).add(run.parts)
    return targets


def door_names(targets: dict[str, set[tuple[str, ...]]]) -> frozenset[str]:
    """Return `testing` and each alias of the scan that reaches a namespace called testing.

    Complexity: the aliases times the length of their longest chain.
    """
    names = {DOOR}
    changed = True
    while changed:
        changed = False
        for alias, paths in targets.items():
            if alias not in names and any(path and path[-1] in names for path in paths):
                names.add(alias)
                changed = True
    return frozenset(names)


def pair_rows(parts: Sequence[str], rows: Sequence[int], doors: frozenset[str]) -> tuple[list[int], set[int]]:
    """Return the row of each door-member pair in a name, and the positions of the members those pairs hold."""
    found: list[int] = []
    held: set[int] = set()
    for position in range(len(parts) - 1):
        if parts[position] in doors and parts[position + 1] in MEMBERS:
            found.append(rows[position])
            held.add(position + 1)
    return found, held


def name_leaves(node: tsast.Node) -> list[tsast.Node] | None:
    """Return the leaf of each part of a qualified name, outermost first, template arguments left out.

    The walk follows the shape that tsast.qualified_parts reads, so the
    leaves line up with its parts.  A part that is not a name gives None.
    """
    if node.type in LEAVES:
        return [node]
    if node.type == "qualified_identifier":
        scope, name = node.child_by_field("scope"), node.child_by_field("name")
        head = [] if scope is None else name_leaves(scope)
        tail = None if name is None else name_leaves(name)
        return None if head is None or tail is None else head + tail
    if node.type in ("template_type", "template_function", "template_method"):
        name = node.child_by_field("name")
        return None if name is None else name_leaves(name)
    if node.type == "nested_namespace_specifier":
        leaves: list[tsast.Node] = []
        for child in node.children:
            if child.type != "comment":
                inner = name_leaves(child)
                if inner is None:
                    return None
                leaves += inner
        return leaves
    return None


def tree_uses(tree: tsast.Tree, doors: frozenset[str]) -> list[int]:
    """Return the zero-based row of each use of the door in one parsed file.

    Complexity: linear in the number of nodes, plus the length of the macro bodies.
    """
    rows: list[int] = []
    held_leaves: set[tuple[tuple[int, int], tuple[int, int]]] = set()
    for node in tree.find("qualified_identifier"):
        parent = node.parent
        if parent is not None and parent.type == "qualified_identifier" and node.field == "name":
            continue
        leaves = name_leaves(node)
        if leaves is None:
            continue
        found, held = pair_rows([tsast.leaf_name(leaf) or "" for leaf in leaves], [leaf.start[0] for leaf in leaves],
                                doors)
        rows += found
        held_leaves |= {(leaves[position].start, leaves[position].end) for position in held}
    for leaf in tree.find(*LEAVES):
        if tsast.leaf_name(leaf) in WITNESSES and (leaf.start, leaf.end) not in held_leaves:
            rows.append(leaf.start[0])
    for using in tsast.using_names(tree):
        if using.is_directive and any(part in doors for part in using.target):
            rows.append(using.node.start[0])
    for alias in tsast.namespace_aliases(tree):
        if alias.target and alias.target[-1] in doors:
            rows.append(alias.node.start[0])
    for tokens in macro_bodies(tree):
        for run in macro_runs(tokens):
            found, held = pair_rows(run.parts, run.rows, doors)
            rows += found
            rows += [run.rows[position] for position, part in enumerate(run.parts)
                     if part in WITNESSES and position not in held]
            if run.after_using_namespace and any(part in doors for part in run.parts):
                rows.append(run.rows[0])
            if run.alias_name is not None and run.parts[-1] in doors:
                rows.append(run.rows[0])
    return sorted(rows)


def scan(root: Path) -> tuple[dict[str, list[int]], list[str]]:
    """Return the lines of each use of the door by file, and a line for each file the parser cannot read.

    Complexity: one parse of each file in scope.
    """
    trees = list(tsast.parse([root / rel for rel in scope_files(root)], strict=False))
    problems = [f"{Path(tree.path).relative_to(root).as_posix()} does not parse, so its uses of the testing door "
                f"are unknown: {tree.diagnostic}" for tree in trees if tree.diagnostic is not None]
    readable = [tree for tree in trees if tree.diagnostic is None]
    doors = door_names(alias_targets(readable))
    found: dict[str, list[int]] = {}
    for tree in readable:
        rows = tree_uses(tree, doors)
        if rows:
            found[Path(tree.path).relative_to(root).as_posix()] = [row + 1 for row in rows]
    return found, problems


def read_allowlist(path: Path) -> tuple[dict[str, tuple[int, int]], list[str]]:
    """Return each listed path with the number of uses it admits and its line, and each malformed row."""
    entries: dict[str, tuple[int, int]] = {}
    problems: list[str] = []
    for number, raw in enumerate(path.read_text().splitlines() if path.is_file() else [], 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = ENTRY.match(line)
        if match is None:
            problems.append(f"{ALLOWLIST}:{number} has no path, count and reason.")
        elif match.group("path") in entries:
            problems.append(f"{ALLOWLIST}:{number} lists {match.group('path')} a second time.")
        else:
            entries[match.group("path")] = (int(match.group("count") or 1), number)
    return entries, problems


def check(root: Path) -> int:
    """Compare the uses of the door with the allowlist and report.

    Returns:
        0 clean, 1 on a use that no entry admits, a stale entry, a malformed row or a file that does not parse
    """
    found, problems = scan(root)
    entries, row_problems = read_allowlist(root / ALLOWLIST)
    problems += row_problems
    for path, lines in sorted(found.items()):
        admitted = entries.get(path, (0, 0))[0]
        if len(lines) > admitted:
            problems.append(f"{path} uses the testing door {len(lines)} time(s) at lines "
                            f"{', '.join(map(str, lines))}, and its entry admits {admitted}.  The door mints a "
                            f"context with no key.  Code that ships takes its context from a real mint.  A "
                            f"self-test in a header may use the door: give its file an entry with the count and a "
                            f"sentence saying why.")
    for path, (admitted, number) in sorted(entries.items(), key=lambda item: item[1][1]):
        if len(found.get(path, [])) < admitted:
            problems.append(f"{ALLOWLIST}:{number} admits {admitted} use(s) in {path}, and the file has "
                            f"{len(found.get(path, []))}.  Lower or remove the entry.")
    for problem in problems:
        print(f"check-ctx-testing-boundary: {problem}", file=sys.stderr)
    total = sum(len(lines) for lines in found.values())
    print(f"check-ctx-testing-boundary: {total} use(s) of the testing door in {len(found)} file(s), "
          f"{'refused' if problems else 'each one admitted'}.", file=sys.stderr)
    return 1 if problems else 0


def self_test() -> int:
    """Plant uses, direct and through aliases, and each shape that is not a use, then check the verdicts.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []

    def expect(name: str, ok: bool) -> None:
        """Record one case."""
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    planted = {
        "include/foundation/effects/Planted.h":
            "#pragma once\ninline auto forged() noexcept { return ::foundation::effects::testing::init(); }\n"
            "inline auto forged_foreground() noexcept { return ::foundation::effects::testing::foreground(); }\n",
        "src/planted.cpp":
            "inline int relative() { using namespace foundation::effects; return testing::bg(); }\n"
            "namespace eff = ::foundation::effects;\n"
            "inline int through_alias() { return eff::testing::init(); }\n"
            "inline int in_scope() { using namespace foundation::effects::testing; return bg(); }\n",
        "include/crucible/Door.h": "#pragma once\nnamespace door = ::foundation::effects::testing;\n",
        "src/cross.cpp": "#include <crucible/Door.h>\nnamespace d2 = door;\n"
                         "inline int far() { return d2::bg(); }\n",
        "src/directive.cpp": "#include <crucible/Door.h>\n"
                             "inline int near() { using namespace door; return 0; }\n",
        "src/commented.cpp": "namespace t = ::foundation::effects:: /* c */ testing;\n"
                             "inline int g() { return t::init(); }\n"
                             "inline int h() { return ::foundation::effects:: /* c */ testing::test(); }\n",
        "src/witness.cpp": "struct Key { friend struct ::foundation::effects::testing::TestWitness; };\n"
                           "struct Other { friend struct ForegroundWitness; };\n",
        "src/macro.cpp": "#define TAKE ::foundation::effects::testing::bg()\n"
                         "#define ALIAS namespace mt = foundation::effects::testing;\n"
                         "#define FRIEND friend struct TestWitness;\n",
        "src/macro_alias_use.cpp": "inline int m() { return mt::test(); }\n",
        "src/macro_split.cpp": "#define SPLIT ::foundation::effects::testing:: /* c */ bg()\n"
                               "#define SPLICE ::foundation::effects::test\\\ning::init()\n",
        "src/spliced.cpp": "inline int f() { return ::foundation::effects::test\\\ning::init(); }\n"
                           "namespace st = ::foundation::effects::test\\\ning;\n"
                           "struct Key { friend struct Test\\\nWitness; };\n",
        "include/crucible/Clean.h":
            "#pragma once\n// effects::testing::bg() hands out a context, so this header never calls it.\n"
            "inline const char* note = \"testing::bg() and TestWitness\";\n"
            "#include <foundation/effects/TestWitness.h>\n"
            "#define NOTE \"testing::bg()\"\n"
            "namespace quiet = ::foundation::effects;\ninline int unrelated() { return quiet::other(); }\n"
            "namespace other::testing { int bg_like(); }\ninline int fine() { return other::testing::bg_like(); }\n",
        "include/crucible/Listed.h":
            "#pragma once\ninline void self_test() { (void)::foundation::effects::testing::bg(); "
            "(void)::foundation::effects::testing::test(); }\n",
        "test/t.cpp": "inline auto t() { return effects::testing::test(); }\n",
        "bench/b.cpp": "inline auto b() { return effects::testing::bg(); }\n",
        ALLOWLIST: "include/crucible/Listed.h x2  — a planted self-test\n"
                   "include/crucible/Door.h x1  — the planted door alias\n",
    }
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in planted.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")
        found, problems = scan(root)
        expect("every planted file parses", not problems)
        expect("caught: two qualified uses", len(found.get("include/foundation/effects/Planted.h", [])) == 2)
        expect("caught: three uses, after a using-directive, through a namespace alias and by a using-directive",
               len(found.get("src/planted.cpp", [])) == 3)
        expect("caught: an alias of a testing namespace", len(found.get("include/crucible/Door.h", [])) == 1)
        expect("caught: a use through an alias of another file, by a chain of aliases",
               len(found.get("src/cross.cpp", [])) == 2)
        expect("caught: a using-directive of an alias of another file", len(found.get("src/directive.cpp", [])) == 1)
        expect("caught: an alias and a name with a comment inside, each counted once",
               len(found.get("src/commented.cpp", [])) == 3)
        expect("caught: a friend through the door and a friend by its own name, each counted once",
               len(found.get("src/witness.cpp", [])) == 2)
        expect("caught: a factory, an alias and a friend in macro bodies", len(found.get("src/macro.cpp", [])) == 3)
        expect("caught: a use through an alias that a macro body defines",
               len(found.get("src/macro_alias_use.cpp", [])) == 1)
        expect("caught: a macro body split by a comment, and a name split by a backslash-newline",
               len(found.get("src/macro_split.cpp", [])) == 2)
        expect("caught: a call, an alias and a friend whose door or witness name a backslash-newline splits",
               len(found.get("src/spliced.cpp", [])) == 3)
        expect("not caught: a comment, a literal, an include path, a macro string, an alias of another namespace "
               "and another namespace called testing that holds no door member",
               "include/crucible/Clean.h" not in found)
        expect("not caught: test and bench code", not any(rel.startswith(("test/", "bench/")) for rel in found))

        def captured(cwd: Path) -> tuple[int, str]:
            """Run the check from one working directory and keep its report."""
            previous = Path.cwd()
            buffer = io.StringIO()
            os.chdir(cwd)
            try:
                with contextlib.redirect_stderr(buffer):
                    code = check(root)
            finally:
                os.chdir(previous)
            return code, buffer.getvalue()

        code, report = captured(root)
        expect("unlisted uses fail, and the listed files pass",
               code == 1 and "Planted.h uses the testing door 2 time(s)" in report and "Listed.h uses" not in report)
        expect("the report from / equals the report from the scan root", captured(Path("/")) == captured(root))
        for rel in ("include/foundation/effects/Planted.h", "src/planted.cpp", "src/cross.cpp", "src/directive.cpp",
                    "src/commented.cpp", "src/witness.cpp", "src/macro.cpp", "src/macro_alias_use.cpp",
                    "src/macro_split.cpp", "src/spliced.cpp"):
            (root / rel).unlink()
        with (root / "include/crucible/Listed.h").open("a", encoding="utf-8") as listed:
            listed.write("inline void more() { (void)::foundation::effects::testing::init(); }\n")
        code, report = captured(root)
        expect("a new use in a listed file fails", code == 1 and "Listed.h uses the testing door 3 time(s)" in report)
        (root / ALLOWLIST).write_text("include/crucible/Listed.h x4  — a planted self-test\n"
                                      "include/crucible/Door.h x1  — the planted door alias\n"
                                      "include/crucible/Absent.h  — never existed\n", encoding="utf-8")
        code, report = captured(root)
        expect("an entry above the count of its file, and an entry for a file with no use, are stale",
               code == 1 and "admits 4 use(s) in include/crucible/Listed.h, and the file has 3" in report
               and "admits 1 use(s) in include/crucible/Absent.h, and the file has 0" in report)
        (root / ALLOWLIST).write_text("include/crucible/Listed.h x3  — a planted self-test\n"
                                      "include/crucible/Door.h x1  — the planted door alias\n"
                                      "include/crucible/Door.h x1  — twice\n", encoding="utf-8")
        code, report = captured(root)
        expect("a path listed twice fails", code == 1 and "a second time" in report)
        (root / ALLOWLIST).write_text("include/crucible/Listed.h x3  — a planted self-test\n"
                                      "include/crucible/Door.h x1  — the planted door alias\n",
                                      encoding="utf-8")
        expect("a satisfied list passes", captured(root)[0] == 0)
        (root / "src/broken.cpp").write_text("void f() { g(1) { } }\n", encoding="utf-8")
        code, report = captured(root)
        expect("a file the parser cannot read fails", code == 1 and "src/broken.cpp does not parse" in report)
    if failures:
        print(f"check-ctx-testing-boundary --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print("check-ctx-testing-boundary --self-test: every case passes.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    if argv not in ([], ["--self-test"]):
        print("usage: check-ctx-testing-boundary.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-ctx-testing-boundary: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
