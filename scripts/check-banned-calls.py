#!/usr/bin/env python3
"""check-banned-calls — the reinterpret_cast ban and the vector reserve ban, read from the AST.

CLAUDE.md §III bans `reinterpret_cast`, and CLAUDE.md §IV bans
`std::vector::reserve`.  This guard finds each use in the parse tree of the
pinned tree-sitter kit (scripts/tsast.py), so a comment, a string literal or
a raw string cannot hold a false hit.  A call that spans lines, a call with a
space before its `<` or `(`, and a qualified member call are still found.

THE TWO BANS
    reinterpret_cast   every identifier token spelled `reinterpret_cast`,
                       because the word is a keyword and has no other use.
                       Roots: include/ and vessel/.
    reserve            every call whose callee is a member access that names
                       `reserve`: `v.reserve(n)`, `p->reserve(n)`,
                       `v.Base::reserve(n)` and `v.template reserve<T>(n)`.
                       Roots: include/ and bench/.
    A path with a component named test, examples, third_party, external or
    vendor, or a component that starts with build, is out of scope.  The
    reinterpret_cast ban also skips bench/.

WHAT THE PARSER CANNOT READ
    A macro body is one `preproc_arg` node of raw text.  The guard scans that
    text with the lexer of scripts/cxx_lex.py, which drops comments, string
    literals, character literals and raw strings, and it reports each banned
    token that remains.  A file in tsast.UNPARSEABLE
    gets the same lexical scan over its whole text.  A parse error in any
    other file is a guard failure.

EXEMPTIONS
    A comment on the line of the banned token that says
    `NO-REINTERPRET-OK: <reason>` or `NO-RESERVE-OK: <reason>` exempts that
    line.  The reason must not be empty.
    An entry `path:text` in the ban's allowlist exempts the site whose line,
    trimmed, equals `text`.  The key is the content of the line, not its
    number, so an edit above the site does not move it.  An entry that
    matches no live site is stale.

Exit 0 clean, 1 on a violation or a parse error, 2 on a stale entry or a
usage error, 3 when the kit is not installed.
"""

from __future__ import annotations

import contextlib
import io
import os
import re
import sys
import tempfile
from collections.abc import Callable, Iterator
from dataclasses import dataclass
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import cxx_lex  # noqa: E402
import tsast  # noqa: E402

SUFFIXES = (".h", ".hpp", ".cpp", ".cc")
EXCLUDED_COMPONENTS = frozenset({"test", "examples", "third_party", "external", "vendor"})


@dataclass(frozen=True)
class Hit:
    """One use of a banned construct."""

    path: str
    row: int
    key: str


@dataclass(frozen=True)
class Ban:
    """One banned construct, where it is banned, and how it is exempted."""

    name: str
    roots: tuple[str, ...]
    extra_excluded: frozenset[str]
    marker: str
    allowlist: str
    rule: str
    token: re.Pattern[str]
    node_hits: Callable[[tsast.Tree], Iterator[tsast.Node]]


def _reinterpret_nodes(tree: tsast.Tree) -> Iterator[tsast.Node]:
    """Yield each identifier token spelled reinterpret_cast.

    Args:
        tree: One parsed file

    Yields:
        The identifier node of each cast
    """
    for node in tree.find("identifier"):
        if node.text == "reinterpret_cast":
            yield node


def _final_name(node: tsast.Node) -> tsast.Node | None:
    """Return the name that a member-access field finally names.

    Args:
        node: The `field` child of a field_expression

    Returns:
        The field_identifier or identifier node, or None for another shape
    """
    current: tsast.Node | None = node
    while current is not None:
        if current.type in ("field_identifier", "identifier"):
            return current
        if current.type in ("qualified_identifier", "template_method"):
            current = current.child_by_field("name")
        elif current.type == "dependent_name":
            named = current.children
            current = named[-1] if named else None
        else:
            return None
    return None


def _reserve_nodes(tree: tsast.Tree) -> Iterator[tsast.Node]:
    """Yield the name token of each member call named reserve.

    Args:
        tree: One parsed file

    Yields:
        The `reserve` name node of each such call
    """
    for call in tree.find("call_expression"):
        callee = call.child_by_field("function")
        if callee is None or callee.type != "field_expression":
            continue
        field = callee.child_by_field("field")
        name = _final_name(field) if field is not None else None
        if name is not None and name.text == "reserve":
            yield name


BANS = (
    Ban(
        name="reinterpret_cast",
        roots=("include", "vessel"),
        extra_excluded=frozenset({"bench"}),
        marker="NO-REINTERPRET-OK",
        allowlist="scripts/no-reinterpret-allowlist.txt",
        rule="reinterpret_cast is banned (CLAUDE.md §III). Use std::bit_cast or std::start_lifetime_as",
        token=re.compile(r"\breinterpret_cast\b"),
        node_hits=_reinterpret_nodes,
    ),
    Ban(
        name="reserve",
        roots=("include", "bench"),
        extra_excluded=frozenset(),
        marker="NO-RESERVE-OK",
        allowlist="scripts/no-reserve-allowlist.txt",
        rule="std::vector::reserve is banned (CLAUDE.md §IV). Use std::inplace_vector, "
             "a sized constructor, or arena storage",
        token=re.compile(r"(?:\.|->)\s*reserve\s*\("),
        node_hits=_reserve_nodes,
    ),
)


def strip_literals(text: str) -> tuple[str, list[tuple[int, str]]]:
    """Blank the comments and literals of a C++ text with the shared lexer, and keep the comments.

    Each blanked character becomes a space and each newline stays, so an
    offset in the result is an offset in the input.  A prefixed character
    literal such as u8'"' is one token, so the quote inside it starts no
    string.  Complexity: linear in the length of the text.

    Args:
        text: The C++ source text

    Returns:
        The blanked text, and each comment as (start offset, comment text)
    """
    blanked, _ = cxx_lex.blank(text, blank_literals=True)
    return blanked, cxx_lex.comments(text)


def _row_of(text: str, offset: int) -> int:
    """Return the zero-based row of an offset in a text."""
    return text.count("\n", 0, offset)


def in_scope(rel: Path, ban: Ban) -> bool:
    """Return True when a repo-relative path is inside the ban's scope.

    Args:
        rel: The path relative to the scan root
        ban: The ban

    Returns:
        Whether the ban reads the file
    """
    if rel.suffix not in SUFFIXES or not rel.parts or rel.parts[0] not in ban.roots:
        return False
    excluded = EXCLUDED_COMPONENTS | ban.extra_excluded
    return not any(part in excluded or part.startswith("build") for part in rel.parts[:-1])


def scope_files(root: Path, ban: Ban) -> list[Path]:
    """Return every file the ban reads under a scan root, sorted.

    Args:
        root: The scan root
        ban: The ban

    Returns:
        Absolute paths, in sorted order
    """
    found: list[Path] = []
    for top in ban.roots:
        base = root / top
        if base.is_dir():
            found.extend(p for p in base.rglob("*") if p.is_file() and in_scope(p.relative_to(root), ban))
    return sorted(found)


def _marked(line_comments: dict[int, list[str]], row: int, marker: str) -> bool:
    """Return True when a comment on the row carries the marker and a reason.

    Args:
        line_comments: Comment texts by zero-based row
        row: The row of the banned token
        marker: The marker word

    Returns:
        Whether the row is exempt
    """
    pattern = re.compile(re.escape(marker) + r":\s*\S")
    return any(pattern.search(comment) for comment in line_comments.get(row, ()))


def scan(root: Path, ban: Ban) -> tuple[list[Hit], list[str]]:
    """Find every use of one banned construct under a scan root.

    Complexity: O(total size of the files in scope).

    Args:
        root: The scan root
        ban: The ban

    Returns:
        The unexempted hits before the allowlist, and the parse failures

    Raises:
        tsast.KitMissing: If the pinned kit is not installed
    """
    files = scope_files(root, ban)
    hits: list[Hit] = []
    failures: list[str] = []
    for tree in tsast.parse(files, strict=False):
        rel = str(Path(tree.path).relative_to(root))
        source = tree.source.decode("utf-8", "replace")
        lines = source.split("\n")
        rows: set[int] = set()
        line_comments: dict[int, list[str]] = {}
        if tree.diagnostic is not None:
            if rel not in tsast.UNPARSEABLE:
                failures.append(f"{rel}: the parser cannot read this file, so the ban cannot see it. "
                                f"{tree.diagnostic.strip()}")
                continue
            blanked, comments = strip_literals(source)
            rows.update(_row_of(blanked, match.start()) for match in ban.token.finditer(blanked))
        else:
            rows.update(node.start[0] for node in ban.node_hits(tree))
            comments = []
            for node in tree.find("comment"):
                line_comments.setdefault(node.start[0], []).append(node.text)
            for body in tree.find("preproc_arg"):
                blanked, inner = strip_literals(body.text)
                base_row = body.start[0]
                rows.update(base_row + _row_of(blanked, match.start()) for match in ban.token.finditer(blanked))
                for offset, text in inner:
                    line_comments.setdefault(base_row + _row_of(body.text, offset), []).append(text)
        for offset, text in comments:
            line_comments.setdefault(_row_of(source, offset), []).append(text)
        for row in sorted(rows):
            if _marked(line_comments, row, ban.marker):
                continue
            hits.append(Hit(rel, row + 1, lines[row].strip()))
    return hits, failures


def load_allowlist(path: Path) -> list[str]:
    """Return the entries of an allowlist, without comments or blank lines.

    Args:
        path: The allowlist file

    Returns:
        Each `path:text` entry, trimmed
    """
    if not path.is_file():
        return []
    entries = []
    for line in path.read_text(encoding="utf-8").splitlines():
        stripped = line.strip()
        if stripped and not stripped.startswith("#"):
            entries.append(stripped)
    return entries


def check(root: Path) -> int:
    """Run every ban under a scan root and report.

    Args:
        root: The scan root

    Returns:
        0 clean, 1 on a violation or a parse failure, 2 on a stale entry
    """
    violations = stale = 0
    for ban in BANS:
        hits, failures = scan(root, ban)
        entries = load_allowlist(root / ban.allowlist)
        admitted = set(entries)
        live = {f"{hit.path}:{hit.key}" for hit in hits}
        tag = ban.marker.removesuffix("-OK")
        for failure in failures:
            print(f"{tag} parse failure: {failure}", file=sys.stderr)
            violations += 1
        for hit in hits:
            key = f"{hit.path}:{hit.key}"
            if key in admitted:
                continue
            print(f"{tag} violation: {hit.path}:{hit.row} — {ban.rule}.  Allowlist key: {key}", file=sys.stderr)
            violations += 1
        for entry in entries:
            if entry not in live:
                print(f"{tag} stale: {entry} — no {ban.name} site has this line text. "
                      f"Remove the entry from {ban.allowlist}.", file=sys.stderr)
                stale += 1
    if violations:
        print(f"check-banned-calls: {violations} violation(s). Rewrite the site, mark the line with "
              "`// <MARKER>: <reason>`, or add the printed allowlist key with the migration it waits for.",
              file=sys.stderr)
        return 1
    if stale:
        print(f"check-banned-calls: {stale} stale allowlist entr(y/ies).", file=sys.stderr)
        return 2
    print("check-banned-calls: clean — no new banned call, no stale allowlist entry.", file=sys.stderr)
    return 0


def self_test() -> int:
    """Plant each shape the bans must see and must not see, then check the verdicts.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    negatives = 0

    def expect(name: str, ok: bool, negative: bool = False) -> None:
        """Record one case."""
        nonlocal negatives
        negatives += negative
        print(f"  {'ok  ' if ok else 'FAIL'} {name}")
        if not ok:
            failures.append(name)

    def keys(root: Path, ban: Ban) -> set[str]:
        """Return the content keys of the unexempted hits of one ban."""
        hits, _ = scan(root, ban)
        return {hit.key for hit in hits}

    cast_fixture = (
        "#pragma once\n"
        "#define CAST_IN_MACRO(p) reinterpret_cast<long*>(p)\n"
        "#define QUOTE_THEN_CAST(p) (u8'\"', reinterpret_cast<char*>(p))\n"
        "#define SEPARATOR_THEN_CAST(p) (1'000, reinterpret_cast<short*>(p))\n"
        "inline int* plain(void* p) { return reinterpret_cast<int*>(p); }\n"
        "inline char* spanning(void* p) { return reinterpret_cast\n"
        "    <char*>(p); }\n"
        "inline short* spaced(void* p) { return reinterpret_cast <short*>(p); }\n"
        "inline float* admitted(void* p) { return reinterpret_cast<float*>(p); }\n"
        "inline double* marked(void* p) { return reinterpret_cast<double*>(p); }  // NO-REINTERPRET-OK: fixture\n"
        "inline unsigned* bare_marker(void* p) { return reinterpret_cast<unsigned*>(p); }  // NO-REINTERPRET-OK:\n"
        "// return reinterpret_cast<int*>(in_line_comment);\n"
        "/* a block comment\n"
        "   return reinterpret_cast<int*>(in_block_comment);\n"
        "*/\n"
        'inline const char* text = "reinterpret_cast<int*>(in_string)";\n'
        'inline const char* raw = R"x(\n'
        "reinterpret_cast<int*>(in_raw_string)\n"
        ')x";\n'
    )
    reserve_fixture = (
        "#pragma once\n"
        "#define RESERVE_IN_MACRO(v) (v).reserve(1)\n"
        "inline void plain(V& v) { v.reserve(7); }\n"
        "inline void arrow(V* p) { p->reserve(8); }\n"
        "inline void spanning(V& v) { v\n"
        "    .reserve(9); }\n"
        "inline void spaced(V& v) { v . reserve (10); }\n"
        "inline void qualified(V& v) { v.Base::reserve(12); }\n"
        "inline void admitted(V& v) { v.reserve(11); }\n"
        "inline void marked(V& v) { v.reserve(15); }  // NO-RESERVE-OK: fixture\n"
        "// v.reserve(18);\n"
        'inline const char* text = "v.reserve(19);";\n'
        "inline void reserved_name(V& v) { v.reserved_slots(20); }\n"
    )
    cast_ban, reserve_ban = BANS
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in (
            ("include/crucible/planted/Casts.h", cast_fixture),
            ("include/crucible/planted/Reserve.h", reserve_fixture),
            ("vessel/planted.cpp", "int* f(void* p) { return reinterpret_cast<int*>(p); }\n"),
            ("bench/planted.cpp", "void g(V& v) { v.reserve(23); }\n"
                                  "int* f(void* p) { return reinterpret_cast<int*>(bench_cast); }\n"),
            ("include/crucible/test/planted.h", "int* f(void* p) { return reinterpret_cast<int*>(test_dir); }\n"),
            ("scripts/no-reinterpret-allowlist.txt",
             "include/crucible/planted/Casts.h:inline float* admitted(void* p) "
             "{ return reinterpret_cast<float*>(p); }\n"),
            ("scripts/no-reserve-allowlist.txt",
             "include/crucible/planted/Reserve.h:inline void admitted(V& v) { v.reserve(11); }\n"),
        ):
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text, encoding="utf-8")

        casts = keys(root, cast_ban)
        expect("a plain cast is caught", any("<int*>(p)" in key and "plain" in key for key in casts))
        expect("a cast that spans two lines is caught", any("spanning" in key for key in casts))
        expect("a cast with a space before < is caught", any("spaced" in key for key in casts))
        expect("a cast in a macro body is caught", any("CAST_IN_MACRO" in key for key in casts))
        expect("a cast after a prefixed character literal that holds a quote is caught",
               any("QUOTE_THEN_CAST" in key for key in casts))
        expect("a cast after a number with a digit separator is caught",
               any("SEPARATOR_THEN_CAST" in key for key in casts))
        expect("a cast in vessel/ is caught", any("reinterpret_cast<int*>(p)" in key and "f(" in key
                                                     for key in casts))
        expect("a marker with a reason exempts its line", not any("double" in key for key in casts), True)
        expect("a marker with no reason exempts nothing", any("bare_marker" in key for key in casts))
        expect("a cast in a line comment is not caught", not any("in_line_comment" in key for key in casts), True)
        expect("a cast in a block comment is not caught",
               not any("in_block_comment" in key for key in casts), True)
        expect("a cast in a string literal is not caught", not any("in_string" in key for key in casts), True)
        expect("a cast in a raw string is not caught", not any("in_raw_string" in key for key in casts), True)
        expect("a cast in bench/ is out of scope", not any("bench_cast" in key for key in casts), True)
        expect("a cast under a test/ directory is out of scope", not any("test_dir" in key for key in casts), True)

        reserves = keys(root, reserve_ban)
        for label, needle in (("a plain call", "reserve(7)"), ("a call through ->", "reserve(8)"),
                              ("a call that spans two lines", "reserve(9)"),
                              ("a call with spaces around . and (", "reserve (10)"),
                              ("a qualified member call", "reserve(12)"), ("a call in a macro body", "reserve(1)"),
                              ("a call in bench/", "reserve(23)")):
            expect(f"reserve: {label} is caught", any(needle in key for key in reserves))
        for label, needle in (("a marked call", "reserve(15)"), ("a call in a comment", "reserve(18)"),
                              ("a call in a string literal", "reserve(19)"),
                              ("a longer member name", "reserved_slots")):
            expect(f"reserve: {label} is not caught", not any(needle in key for key in reserves), True)

        expect("the full check reports violations", check(root) == 1)

        def captured(cwd: Path) -> tuple[int, str]:
            """Run the full check from one working directory and keep its report."""
            previous = Path.cwd()
            buffer = io.StringIO()
            os.chdir(cwd)
            try:
                with contextlib.redirect_stderr(buffer):
                    code = check(root)
            finally:
                os.chdir(previous)
            return code, buffer.getvalue()

        expect("the report from / equals the report from the scan root", captured(Path("/")) == captured(root))

        allow = root / "scripts/no-reserve-allowlist.txt"
        live_keys = sorted(f"include/crucible/planted/Reserve.h:{key}" for key in reserves if "(23)" not in key)
        (root / "bench/planted.cpp").unlink()
        (root / "include/crucible/planted/Casts.h").unlink()
        (root / "vessel/planted.cpp").unlink()
        (root / "scripts/no-reinterpret-allowlist.txt").write_text("", encoding="utf-8")
        allow.write_text("\n".join(live_keys) + "\n", encoding="utf-8")
        shifted = root / "include/crucible/planted/Reserve.h"
        shifted.write_text("\n\n" + reserve_fixture, encoding="utf-8")
        expect("a content key survives a line shift", check(root) == 0)
        allow.write_text("\n".join(live_keys) + "\ninclude/crucible/planted/Reserve.h:v.reserve(99);\n",
                         encoding="utf-8")
        expect("a stale entry exits 2", check(root) == 2, True)

        broken = root / "include/crucible/planted/Broken.h"
        broken.write_text("void f() { g(1) { } }\n", encoding="utf-8")
        allow.write_text("\n".join(live_keys) + "\n", encoding="utf-8")
        expect("a file the parser cannot read fails the check", check(root) == 1, True)

    if failures:
        print(f"check-banned-calls --self-test: FAILED — {len(failures)} case(s) did not hold")
        return 2
    print(f"check-banned-calls --self-test: every case passes, {negatives} of them negative controls.")
    return 0


def main(argv: list[str]) -> int:
    """Run the check or the self-test.

    Args:
        argv: The arguments after the program name

    Returns:
        The exit code
    """
    if argv not in ([], ["--self-test"]):
        print("usage: check-banned-calls.py [--self-test]", file=sys.stderr)
        return 2
    try:
        return self_test() if argv else check(tsast.REPO_ROOT)
    except tsast.KitMissing as exc:
        print(f"check-banned-calls: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
