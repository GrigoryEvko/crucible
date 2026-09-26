#!/usr/bin/env python3
"""check-banned-calls — the reinterpret_cast, vector reserve, file-stream and process-spawn bans, read from the AST.

CLAUDE.md §III bans `reinterpret_cast`, and CLAUDE.md §IV bans
`std::vector::reserve`.  The fixy-only band-3 directories ban the C++ file
streams.  CLAUDE.md §IX bans a raw process spawn in production code,
because the child carries no Permission and no effect row.  This guard
finds each use in the parse tree of the pinned
tree-sitter kit (scripts/tsast.py), so a comment, a string literal or a raw
string cannot hold a false hit.  A use that spans lines, a use with a space
before its `<` or `(`, and a qualified member call are still found.

THE FOUR BANS
    reinterpret_cast   every identifier token spelled `reinterpret_cast`,
                       because the word is a keyword and has no other use.
                       Roots: include/ and vessel/.
    reserve            every call whose callee is a member access that names
                       `reserve`: `v.reserve(n)`, `p->reserve(n)`,
                       `v.Base::reserve(n)` and `v.template reserve<T>(n)`.
                       Roots: include/ and bench/.
    file stream        every name of a C++ file stream or file buffer
                       (ofstream, ifstream, fstream, filebuf, their wide and
                       basic_ spellings), with or without `std::`, and every
                       `#include <fstream>`.  A file there goes through
                       fixy::mint_file, safety::OwnedFile or
                       safety::FileHandle.  Roots: each include/crucible
                       directory that scripts/fixy-only-paths.txt lists, and
                       its src/ twin.  A list that names no such directory
                       fails the guard, because the ban would then read
                       nothing.
    process spawn      every reference to a C library function that creates
                       a process, replaces its image or reaps a child (fork,
                       vfork, clone, the exec family, posix_spawn, system,
                       popen, daemon, the wait family and more), and every
                       syscall number of the same kernel entries (SYS_clone,
                       __NR_execve and more).  A reference is a name in an
                       expression or a template argument, bare, qualified
                       from `::`, or qualified by std or a namespace alias
                       of std, so a call, an address, a function pointer
                       and a using-declaration are all found.  A member, the
                       name of a declaration or an enumerator, and a name
                       qualified by another namespace or a class are not.
                       Roots: include/, src/ and vessel/, without the kernel
                       dump under include/crucible/perf/bpf/.
    A path with a component named test, examples, third_party, external or
    vendor, or a component that starts with build, is out of scope.  The
    reinterpret_cast ban also skips bench/.

MACRO BODIES
    The body of each #define is parsed as a C++ fragment (tsast.macro_bodies),
    and the same node rules run on that tree.  In a fragment the name of a
    declaration also counts for the spawn ban, because a body such as
    `macro_name fork` is text that a use site completes.  A namespace alias
    of std comes from the file that holds the #define.  A body that does not
    parse, for example one with `#` or `##`, is read as preprocessing tokens:
    a banned name counts unless `.` or `->` stands before it, and a spawn name
    also counts only when no qualifier other than std or an alias of std
    stands before it.  A file that the parser cannot read is a guard failure.
    A file that is not C++ (tsast.is_in_cpp_scope) is out of scope.

WHAT THE GUARD CANNOT SEE
    A name that a macro builds with `##`, a syscall number written as a
    literal, and a call through a namespace of another file that holds a
    using-directive for std.

EXEMPTIONS
    A comment on the line of the banned token that says
    `NO-REINTERPRET-OK: <reason>` or `NO-RESERVE-OK: <reason>` exempts that
    line.  The reason must not be empty.
    `NO-FILE-STREAM-OK: <reason>` and `SPAWN-PROCESS-OK: <reason>` exempt a
    line of their ban the same way.
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

import tsast  # noqa: E402

EXCLUDED_COMPONENTS = frozenset({"test", "examples", "third_party", "external", "vendor"})
FILE_STREAMS = frozenset({
    "ofstream", "ifstream", "fstream", "filebuf",
    "wofstream", "wifstream", "wfstream", "wfilebuf",
    "basic_ofstream", "basic_ifstream", "basic_fstream", "basic_filebuf",
})
# The C library functions that create a process, replace its image or reap
# a child, and the syscall numbers that reach the same kernel entry through
# syscall(2).
SPAWN_NAMES = frozenset({
    "fork", "vfork", "_Fork", "forkpty", "daemon", "clone", "clone3", "__clone", "__clone2",
    "execve", "execveat", "fexecve", "execv", "execvp", "execvpe", "execl", "execlp", "execle",
    "posix_spawn", "posix_spawnp", "system", "popen", "pclose", "waitpid", "wait3", "wait4", "waitid",
    "SYS_fork", "SYS_vfork", "SYS_clone", "SYS_clone3", "SYS_execve", "SYS_execveat",
    "__NR_fork", "__NR_vfork", "__NR_clone", "__NR_clone3", "__NR_execve", "__NR_execveat",
})
# The data file that lists the fixy-only directories, one per line.
FIXY_ONLY_PATHS = "scripts/fixy-only-paths.txt"


@dataclass(frozen=True)
class Hit:
    """One use of a banned construct."""

    path: str
    row: int
    key: str


@dataclass(frozen=True)
class Scope:
    """What a node rule needs to know about the text it reads.

    Attributes:
        std_names: std and every namespace alias of std in the file that
            holds the text
        is_fragment: True for the parse of a macro body, where the name of a
            declaration is text that a use site completes
    """

    std_names: frozenset[str]
    is_fragment: bool


@dataclass(frozen=True)
class Ban:
    """One banned construct, where it is banned, and how it is exempted.

    Attributes:
        node_hits: The node rule, over a parse tree
        token_hit: The rule over the preprocessing tokens of a macro body
            that does not parse: the token list and an index, true when the
            token at the index is a use
        excluded_prefixes: Repo-relative path prefixes that the ban skips
    """

    name: str
    roots: Callable[[Path], tuple[str, ...]]
    extra_excluded: frozenset[str]
    marker: str
    allowlist: str
    rule: str
    node_hits: Callable[[tsast.Tree, Scope], Iterator[tsast.Node]]
    token_hit: Callable[[list[tsast.Token], int, Scope], bool]
    excluded_prefixes: tuple[str, ...] = ()


def _after_member_access(tokens: list[tsast.Token], index: int) -> bool:
    """Return True when a member access (`.` or `->`) stands before the token, past `template` and `X ::` pairs."""
    at = index - 1
    while at >= 1 and tokens[at].text == "::" and tokens[at - 1].kind == "identifier":
        at -= 2
    if at >= 0 and tokens[at].text == "template":
        at -= 1
    return at >= 0 and tokens[at].text in (".", "->")


def _reinterpret_nodes(tree: tsast.Tree, scope: Scope) -> Iterator[tsast.Node]:
    """Yield each identifier token spelled reinterpret_cast.

    Args:
        tree: One parsed file or macro body
        scope: Unused, the keyword has no other use

    Yields:
        The identifier node of each cast
    """
    for node in tree.find("identifier"):
        if node.text == "reinterpret_cast":
            yield node


def _reinterpret_token(tokens: list[tsast.Token], index: int, scope: Scope) -> bool:
    """Return True for the keyword reinterpret_cast in the tokens of an unparsed macro body."""
    return tokens[index].text == "reinterpret_cast"


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


def _reserve_nodes(tree: tsast.Tree, scope: Scope) -> Iterator[tsast.Node]:
    """Yield the name token of each member call named reserve.

    Args:
        tree: One parsed file or macro body
        scope: Unused, a member call has no namespace

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


def _reserve_token(tokens: list[tsast.Token], index: int, scope: Scope) -> bool:
    """Return True for `reserve` after a member access and before `(` or `<` in an unparsed macro body."""
    following = tokens[index + 1].text if index + 1 < len(tokens) else ""
    return tokens[index].text == "reserve" and following in ("(", "<") and _after_member_access(tokens, index)


def _file_stream_nodes(tree: tsast.Tree, scope: Scope) -> Iterator[tsast.Node]:
    """Yield each name of a file stream and each include of <fstream>.

    The name counts with or without a `std::` qualifier, so a
    using-directive or a namespace alias does not hide it.

    Args:
        tree: One parsed file or macro body
        scope: Unused, every spelling counts

    Yields:
        The name node or the include node of each use
    """
    for node in tree.find("identifier", "type_identifier"):
        if node.text in FILE_STREAMS:
            yield node
    for node in tree.find("preproc_include"):
        target = node.child_by_field("path")
        if target is not None and target.text == "<fstream>":
            yield node


def _file_stream_token(tokens: list[tsast.Token], index: int, scope: Scope) -> bool:
    """Return True for the name of a file stream in the tokens of an unparsed macro body, unless it is a member."""
    return tokens[index].text in FILE_STREAMS and not _after_member_access(tokens, index)


def _std_names(tree: tsast.Tree) -> frozenset[str]:
    """Return std and every namespace alias of the file that denotes std, through chains of aliases."""
    return frozenset({"std"}) | tsast.alias_closure(tsast.namespace_aliases(tree), [("std",)])


def _outer_qualified(node: tsast.Node) -> tsast.Node:
    """Return the outermost qualified_identifier whose final name is NODE, or NODE itself."""
    outer = node
    while outer.field == "name" and outer.parent is not None and outer.parent.type == "qualified_identifier":
        outer = outer.parent
    return outer


def _spawn_nodes(tree: tsast.Tree, scope: Scope) -> Iterator[tsast.Node]:
    """Yield each reference to a process-spawn function or syscall number of the C library.

    A reference is a name in an expression, bare, qualified from `::`, or
    qualified by std or an alias of std.  A call, an address, a function
    pointer and a using-declaration all hold one.  A name in a template
    argument counts as well, because `call<fork>()` parses it as a type.
    A member name does not count, and a name qualified by any other
    namespace or by a class names a project entity.  The name of a
    declaration counts only in a macro body.

    Args:
        tree: One parsed file or macro body
        scope: The std aliases of the file, and whether TREE is a fragment

    Yields:
        The name node of each reference
    """
    for node in tree.find("identifier", "type_identifier"):
        if node.text not in SPAWN_NAMES:
            continue
        outer = _outer_qualified(node)
        if node.type == "type_identifier" and (outer.parent is None or outer.parent.type != "type_descriptor"):
            continue
        if node.parent is not None and node.parent.type == "enumerator":
            continue
        if node.field == "declarator" and not scope.is_fragment:
            continue
        parts = tsast.qualified_parts(outer)
        if parts is None:
            continue
        qualifiers = parts[1][:-1]
        if not qualifiers or (len(qualifiers) == 1 and qualifiers[0] in scope.std_names):
            yield node


def _spawn_token(tokens: list[tsast.Token], index: int, scope: Scope) -> bool:
    """Return True for a spawn name in an unparsed macro body, unless a member access or a foreign qualifier precedes it."""
    if tokens[index].text not in SPAWN_NAMES or _after_member_access(tokens, index):
        return False
    if index >= 1 and tokens[index - 1].text == "::":
        qualifier = tokens[index - 2] if index >= 2 else None
        return qualifier is None or qualifier.kind != "identifier" or qualifier.text in scope.std_names
    return True


def _band3_roots(root: Path) -> tuple[str, ...]:
    """Return each fixy-only include/crucible directory that scripts/fixy-only-paths.txt lists, and its src/ twin.

    The file holds one repository-relative directory per line.  A `#` starts
    a comment, and a blank line is skipped.

    Args:
        root: The scan root

    Returns:
        The repo-relative directories, or () when the list names none
    """
    listing = root / FIXY_ONLY_PATHS
    lines = listing.read_text(encoding="utf-8").splitlines() if listing.is_file() else []
    listed = [line.split("#", 1)[0].strip() for line in lines]
    subsystems = [Path(rel).name for rel in listed if rel and Path(rel).parent == Path("include/crucible")]
    return tuple(f"{top}/{name}" for name in subsystems for top in ("include/crucible", "src"))


BANS = (
    Ban(
        name="reinterpret_cast",
        roots=lambda root: ("include", "vessel"),
        extra_excluded=frozenset({"bench"}),
        marker="NO-REINTERPRET-OK",
        allowlist="scripts/no-reinterpret-allowlist.txt",
        rule="reinterpret_cast is banned (CLAUDE.md §III). Use std::bit_cast or std::start_lifetime_as",
        node_hits=_reinterpret_nodes,
        token_hit=_reinterpret_token,
    ),
    Ban(
        name="reserve",
        roots=lambda root: ("include", "bench"),
        extra_excluded=frozenset(),
        marker="NO-RESERVE-OK",
        allowlist="scripts/no-reserve-allowlist.txt",
        rule="std::vector::reserve is banned (CLAUDE.md §IV). Use std::inplace_vector, "
             "a sized constructor, or arena storage",
        node_hits=_reserve_nodes,
        token_hit=_reserve_token,
    ),
    Ban(
        name="file stream",
        roots=_band3_roots,
        extra_excluded=frozenset(),
        marker="NO-FILE-STREAM-OK",
        allowlist="scripts/no-file-stream-allowlist.txt",
        rule="a C++ file stream is banned in a fixy-only band-3 directory. Use fixy::mint_file, "
             "safety::OwnedFile or safety::FileHandle",
        node_hits=_file_stream_nodes,
        token_hit=_file_stream_token,
    ),
    Ban(
        name="process spawn",
        roots=lambda root: ("include", "src", "vessel"),
        extra_excluded=frozenset(),
        marker="SPAWN-PROCESS-OK",
        allowlist="scripts/no-spawn-process-allowlist.txt",
        rule="a raw process spawn is banned in production code (CLAUDE.md §IX). The child carries no "
             "Permission and no effect row. Use std::jthread with permission_fork",
        node_hits=_spawn_nodes,
        token_hit=_spawn_token,
        excluded_prefixes=("include/crucible/perf/bpf/",),
    ),
)


def macro_body_rows(body: tsast.MacroBody, ban: Ban, scope: Scope) -> Iterator[int]:
    """Yield the zero-based row in its file of each use of the ban in one macro body.

    A body that parses gives its rows through the node rule.  A body that
    does not parse gives its rows through the token rule over its
    preprocessing tokens.
    """
    if body.is_parsed:
        for node in ban.node_hits(body.tree, scope):
            yield body.origin(node)[0]
        return
    tokens = tsast.pp_tokens(body.text, body.first_row)
    for index, token in enumerate(tokens):
        if token.kind == "identifier" and ban.token_hit(tokens, index, scope):
            yield token.row


def in_scope(rel: Path, ban: Ban, roots: tuple[str, ...]) -> bool:
    """Return True when a repo-relative path is inside the ban's scope.

    Args:
        rel: The path relative to the scan root
        ban: The ban
        roots: The root directories of the ban, relative to the scan root

    Returns:
        Whether the ban reads the file
    """
    if not tsast.is_in_cpp_scope(rel) or not any(rel.is_relative_to(top) for top in roots) \
            or rel.as_posix().startswith(ban.excluded_prefixes):
        return False
    excluded = EXCLUDED_COMPONENTS | ban.extra_excluded
    return not any(part in excluded or part.startswith("build") for part in rel.parts[:-1])


def scope_files(root: Path, ban: Ban, roots: tuple[str, ...]) -> list[Path]:
    """Return every file the ban reads under a scan root, sorted and without duplicates.

    Args:
        root: The scan root
        ban: The ban
        roots: The root directories of the ban, relative to the scan root

    Returns:
        Absolute paths, in sorted order
    """
    found: set[Path] = set()
    for top in roots:
        base = root / top
        if base.is_dir():
            found.update(p for p in base.rglob("*") if p.is_file() and in_scope(p.relative_to(root), ban, roots))
    return sorted(found)


def _marked(tree: tsast.Tree, row: int, marker: str) -> bool:
    """Return True when a comment node that starts on the row carries the marker and a reason.

    Args:
        tree: The parsed file
        row: The zero-based row of the banned token
        marker: The marker word

    Returns:
        Whether the row is exempt
    """
    pattern = re.compile(re.escape(marker) + r":\s*\S")
    return any(comment.start[0] == row and pattern.search(tsast.prose_text(comment))
               for comment in tsast.comments_by_row(tree).get(row, ()))


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
    roots = ban.roots(root)
    if not roots:
        return [], [f"the {ban.name} ban has no root directory, so it reads nothing. For the file-stream ban, "
                    f"{FIXY_ONLY_PATHS} must list the band-3 directories under include/crucible"]
    files = scope_files(root, ban, roots)
    hits: list[Hit] = []
    failures: list[str] = []
    rows_by_tree: dict[str, tuple[tsast.Tree, set[int], Scope]] = {}
    for tree in tsast.parse(files, strict=False):
        rel = str(Path(tree.path).relative_to(root))
        if tree.diagnostic is not None:
            failures.append(f"{rel}: the parser cannot read this file, so the ban cannot see it. "
                            f"{tree.diagnostic.strip()}")
            continue
        scope = Scope(_std_names(tree), is_fragment=False)
        rows_by_tree[rel] = (tree, {node.start[0] for node in ban.node_hits(tree, scope)}, scope)
    trees = [tree for tree, _, _ in rows_by_tree.values()]
    for body in tsast.macro_bodies(trees):
        rel = str(Path(body.define.tree.path).relative_to(root))
        tree, rows, scope = rows_by_tree[rel]
        rows.update(macro_body_rows(body, ban, Scope(scope.std_names, is_fragment=True)))
    for rel, (tree, rows, _) in rows_by_tree.items():
        for row in sorted(rows):
            if not _marked(tree, row, ban.marker):
                hits.append(Hit(rel, row + 1, tree.line(row).strip()))
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
        "#define QUALIFIED_RESERVE(v) (v).Base::reserve(24)\n"
        "#define TEMPLATE_RESERVE(v) (v).template reserve<int>(25)\n"
        "#define PASTED_RESERVE(v) v ## _tail; (v).reserve(26)\n"
        "#define RESERVE_FIELD(v) (v).reserve_count\n"
    )
    stream_fixture = (
        "#pragma once\n"
        "#include <fstream>\n"
        "#include <iostream>\n"
        "#define STREAM_IN_MACRO(p) std::ofstream macro_stream{p}\n"
        "inline void plain() { std::ofstream plain_out{\"x\"}; }\n"
        "inline void spaced() { std :: ifstream spaced_in{\"x\"}; }\n"
        "using namespace std;\n"
        "inline void bare() { fstream bare_io{\"x\"}; }\n"
        "namespace s = std;\n"
        "inline void aliased() { s::basic_ofstream<char> aliased_out{\"x\"}; }\n"
        "inline void wide() { std::wofstream wide_out{L\"x\"}; }\n"
        "inline void buffer() { std::filebuf buffer_io; }\n"
        "inline void marked() { std::ofstream marked_out{\"x\"}; }  // NO-FILE-STREAM-OK: fixture\n"
        "// std::ofstream in_comment{\"x\"};\n"
        "inline const char* text = \"std::ofstream in_string\";\n"
        "inline void other_name() { std::ofstreams_are_not_this plural; }\n"
    )
    spawn_fixture = (
        "#define SPAWN_IN_MACRO() fork(macro_call)\n"
        "#define SPAWN_NAME_IN_MACRO macro_name fork\n"
        "#define STD_SYSTEM_IN_MACRO(c) std::system(c)\n"
        "#define MEMBER_IN_MACRO(p) (p).clone(macro_member)\n"
        "#define PROJECT_IN_MACRO() crucible::fork(macro_project)\n"
        "namespace s = std;\n"
        "namespace s2 = s;\n"
        "inline int bare() { return fork(); }\n"
        "inline int global() { return ::vfork(); }\n"
        "inline int spanning() { return ::posix_spawn\n"
        "    (nullptr, nullptr, nullptr, nullptr, nullptr, nullptr); }\n"
        "inline int std_system() { return std::system(\"std_system\"); }\n"
        "inline int aliased() { return s2::system(\"aliased\"); }\n"
        "inline int global_std() { return ::std::system(\"global_std\"); }\n"
        "inline auto address() { return &::execve; }\n"
        "inline auto pointer() { auto p = execvp; return p; }\n"
        "using ::popen;\n"
        "inline long raw_syscall() { return syscall(SYS_clone3, 0); }\n"
        "inline int template_arg() { return call<fork>(); }\n"
        "inline int reap() { return ::waitpid(-1, nullptr, 0); }\n"
        "inline int marked() { return fork(); }  // SPAWN-PROCESS-OK: fixture\n"
        "inline int admitted() { return daemon(0, 0); }\n"
        "inline int member(T& t) { return t.clone(); }\n"
        "inline int arrow(T* t) { return t->fork(); }\n"
        "inline int project() { return crucible::detail::fork(); }\n"
        "inline int enum_value() { return int(Syscall::execve); }\n"
        "enum class Syscall { clone = 1, execve = 2 };\n"
        "inline int permission() { return permission_fork(0); }\n"
        "inline int declared(int popen) { return 0; }\n"
        "// return fork(in_comment);\n"
        "inline const char* text = \"system(in_string)\";\n"
        "#define ALIASED_IN_MACRO(c) s2::system(c)\n"
        "namespace s3 = /* a comment inside the alias */ std;\n"
        "inline int commented_alias() { return s3::system(\"commented_alias\"); }\n"
        "#define PASTED_THEN_SPAWN(x) x ## _tail; fork(pasted_spawn)\n"
        "#define PASTED_MEMBER(x) x ## _tail; (x).fork(pasted_member)\n"
        "#define PASTED_PROJECT(x) x ## _tail; crucible::fork(pasted_project)\n"
    )
    cast_ban, reserve_ban, stream_ban, spawn_ban = BANS
    with tempfile.TemporaryDirectory() as work:
        root = Path(work)
        for rel, text in (
            (FIXY_ONLY_PATHS, "examples/fn\n"
                              "# include/crucible/commented\n"
                              "include/crucible/cntp  # a trailing comment\n"),
            ("include/crucible/cntp/Streams.h", stream_fixture),
            ("src/cntp/streams.cpp", "void g() { std::ifstream src_twin{\"x\"}; }\n"),
            ("include/crucible/cntp/test/planted.h", "void g() { std::ifstream band_test_dir{\"x\"}; }\n"),
            ("include/crucible/commented/Streams.h", "void g() { std::ifstream commented_dir{\"x\"}; }\n"),
            ("include/crucible/planted/Streams.h", "void g() { std::ifstream not_band3{\"x\"}; }\n"),
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
            ("src/planted/Spawn.cpp", spawn_fixture),
            ("vessel/spawn.cpp", "int vessel_spawn() { return fork(); }\n"),
            ("include/crucible/perf/bpf/vmlinux.h", "int kernel_dump() { return fork(); }\n"),
            ("include/crucible/test/spawn.h", "int test_dir_spawn() { return fork(); }\n"),
            ("scripts/no-spawn-process-allowlist.txt",
             "src/planted/Spawn.cpp:inline int admitted() { return daemon(0, 0); }\n"),
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
                              ("a call in bench/", "reserve(23)"),
                              ("a qualified member call in a macro body", "reserve(24)"),
                              ("a template member call in a macro body", "reserve<int>(25)"),
                              ("a call in a macro body that does not parse", "reserve(26)")):
            expect(f"reserve: {label} is caught", any(needle in key for key in reserves))
        for label, needle in (("a marked call", "reserve(15)"), ("a call in a comment", "reserve(18)"),
                              ("a call in a string literal", "reserve(19)"),
                              ("a longer member name", "reserved_slots"),
                              ("a longer field name in a macro body", "reserve_count")):
            expect(f"reserve: {label} is not caught", not any(needle in key for key in reserves), True)

        streams = keys(root, stream_ban)
        for label, needle in (("an include of <fstream>", "#include <fstream>"),
                              ("a use in a macro body", "STREAM_IN_MACRO"), ("a plain use", "plain_out"),
                              ("a use with spaces around ::", "spaced_in"),
                              ("a use through a using-directive", "bare_io"),
                              ("a basic_ use through a namespace alias", "aliased_out"),
                              ("a wide stream", "wide_out"), ("a file buffer", "buffer_io"),
                              ("a use in the src/ twin of a band-3 directory", "src_twin")):
            expect(f"file stream: {label} is caught", any(needle in key for key in streams))
        for label, needle in (("another standard header", "<iostream>"), ("a marked use", "marked_out"),
                              ("a use in a comment", "in_comment"), ("a use in a string literal", "in_string"),
                              ("a longer name", "plural"), ("a use under a test/ directory", "band_test_dir"),
                              ("a directory that only a comment line lists", "commented_dir"),
                              ("a directory that is not band-3", "not_band3")):
            expect(f"file stream: {label} is not caught", not any(needle in key for key in streams), True)
        spawns = keys(root, spawn_ban)
        for label, needle in (("a call in a macro body", "SPAWN_IN_MACRO"),
                              ("a name with no call in a macro body", "SPAWN_NAME_IN_MACRO"),
                              ("a std-qualified call in a macro body", "STD_SYSTEM_IN_MACRO"),
                              ("a bare call", "bare()"), ("a call qualified from ::", "global()"),
                              ("a call that spans two lines", "spanning()"),
                              ("a std-qualified call", "std_system()"),
                              ("a call through a chain of aliases of std", "aliased()"),
                              ("a call qualified by ::std", "global_std()"), ("an address", "address()"),
                              ("a function pointer", "pointer()"), ("a using-declaration", "using ::popen"),
                              ("a syscall number", "raw_syscall()"), ("a template argument", "template_arg()"),
                              ("a reaping call", "reap()"), ("a call in vessel/", "vessel_spawn"),
                              ("a call through an alias of std in a macro body", "ALIASED_IN_MACRO"),
                              ("a call through an alias whose target holds a comment", "commented_alias()"),
                              ("a call in a macro body that does not parse", "PASTED_THEN_SPAWN")):
            expect(f"process spawn: {label} is caught", any(needle in key for key in spawns))
        for label, needle in (("a marked call", "marked()"), ("a member call", "member("),
                              ("a call through ->", "arrow("), ("a call qualified by a project namespace", "project()"),
                              ("an enumerator qualified by its enum", "enum_value()"),
                              ("the enumerators of an enum", "enum class"), ("a longer name", "permission()"),
                              ("the name of a parameter", "declared("), ("a member call in a macro body",
                                                                         "MEMBER_IN_MACRO"),
                              ("a project call in a macro body", "PROJECT_IN_MACRO"),
                              ("a call in a comment", "in_comment"), ("a call in a string literal", "in_string"),
                              ("the kernel dump under perf/bpf", "kernel_dump"),
                              ("a call under a test/ directory", "test_dir_spawn"),
                              ("a member call in a macro body that does not parse", "PASTED_MEMBER"),
                              ("a project call in a macro body that does not parse", "PASTED_PROJECT")):
            expect(f"process spawn: {label} is not caught", not any(needle in key for key in spawns), True)
        report = io.StringIO()
        with contextlib.redirect_stderr(report):
            check(root)
        expect("process spawn: the report names an unadmitted site by its content key",
               "Allowlist key: src/planted/Spawn.cpp:inline int bare()" in report.getvalue())
        expect("process spawn: an allowlist row keyed by content admits its site",
               "Allowlist key: src/planted/Spawn.cpp:inline int admitted()" not in report.getvalue(), True)

        registry = (root / FIXY_ONLY_PATHS).read_text(encoding="utf-8")
        (root / FIXY_ONLY_PATHS).write_text("examples/fn\n", encoding="utf-8")
        unregistered_hits, unregistered_failures = scan(root, stream_ban)
        expect("a registry with no band-3 directory fails the ban",
               not unregistered_hits
               and any("has no root directory" in failure for failure in unregistered_failures), True)
        (root / FIXY_ONLY_PATHS).write_text(registry, encoding="utf-8")

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
        (root / "include/crucible/cntp/Streams.h").unlink()
        (root / "src/cntp/streams.cpp").unlink()
        (root / "src/planted/Spawn.cpp").unlink()
        (root / "vessel/spawn.cpp").unlink()
        (root / "scripts/no-reinterpret-allowlist.txt").write_text("", encoding="utf-8")
        (root / "scripts/no-spawn-process-allowlist.txt").write_text("", encoding="utf-8")
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
