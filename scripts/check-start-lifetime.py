#!/usr/bin/env python3
"""check-start-lifetime — std::start_lifetime_as only where no proof can reach it.

The rule
--------
A use of std::start_lifetime_as or std::start_lifetime_as_array is
admitted in three places only:

  the checked start    include/foundation/Lifetime.h, whose
                       start_as_array refuses at compile time a type with
                       a subobject that is not an implicit-lifetime type
  a frozen file        a path that scripts/frozen-paths.txt freezes,
                       so no new use can appear there
  a negative fixture   a file under a test directory named neg or *_neg,
                       which must fail to compile

Each admitted use also needs an entry in
scripts/start-lifetime-allowlist.txt, and each entry states how many uses
it admits.  A use anywhere else is refused, also when the allowlist names
it, and an entry for such a path is refused too.  New code calls
foundation::lifetime::start_as_array.  A use is each name node with one of
the two names: a call, a using-declaration, a macro body, or the address of
the function.

Why
---
A proof type (a mint key, a context, a capability, a permission, a read
proof) is neither trivially copyable nor an implicit-lifetime type, so
std::bit_cast and std::start_lifetime_as<P> refuse it.  Three routes of the
same library family still give a pointer to a proof object whose lifetime
never started:

  std::start_lifetime_as<P[1]>(buf)            an array type is an
                                               implicit-lifetime type for
                                               any element
  std::start_lifetime_as_array<P>(buf, 1)      libstdc++ puts no mandate on
                                               the element type
  std::start_lifetime_as<Holder>(buf)          an aggregate that holds a P,
                                               or std::array<P, 1>

A read through that pointer is undefined behavior, and no property of a
type can refuse the three routes.  The checked start refuses all three at
compile time, whatever an alias in its template argument names.  So this
guard keeps the library names out of every other place, and
test/fixy/test_forgeable_proofs.cpp states the routes that stay open.

The key
-------
An allowlist entry is `path:key`, or `path:key xN` for N uses.  The key is
the name and the template argument list of its node, spelled by
tsast.spelled: one space between two word tokens and none elsewhere, with
comments dropped.  For example

  include/crucible/concurrent/_AtomicSnapshot.h:start_lifetime_as<T> x2

The key survives a line shift, a rename of the variable that holds the
result, and a change of the qualification.  The parse tree bounds the
argument list, so a `>` inside parentheses does not end it.  A use with no
template argument list has the bare name as its key.  The count makes each
new use a change to the allowlist.  Each entry needs a comment above it
that gives the reason.

Two passes
----------
Both passes read the parse tree of scripts/tsast.py.  A name split by a
backslash-newline is one name.  A comment or a literal is its own node, so
a mention there is not a use.

The lexical pass parses each source file, and the body of each #define on
its own with tsast.macro_bodies.  It follows each #include whose operand is
a literal, whatever the suffix of the named file.  Its scope is every C and
C++ source file that git tracks, every untracked file that .gitignore does
not exclude, and with --compile-db each source that the compile database
names.  A file or a macro body that the parser cannot read is read from its
preprocessing tokens, so no use is lost.  The tokens then bound the argument
list by its angle brackets.

The preprocessed pass runs with --compile-db.  It reads the output of the
compiler of each database entry with -E and the flags of the build, from
the shared store of scripts/preprocessed.py in preprocessed-cache/ beside
the compile database, so this guard and check-proof-routes.py preprocess
each translation unit one time between them.  Preprocessing expands token
pasting and macro bodies, follows an #include whose operand is a macro, and
finds each header through the -I flags of the build.  The pass parses each
distinct expansion of a file that names the function, and the line markers
of the output give each use its file and line, so a use in a header counts
once, however many translation units include it.  For each file and key,
the count is the larger count of the two passes, so an arm that this host
does not compile still counts.  A preprocessor failure refuses the run,
because a translation unit the guard cannot read can hold a use.
START_LIFETIME_JOBS sets the number of parallel preprocessor runs, and the
default is the number of processors, at most 16.

What this guard does not see, stated rather than implied
--------------------------------------------------------
  - A file that no entry of the compile database reads.  The lexical pass
    alone scans it, so a name built by token pasting there, or an #include
    with a macro operand, is not seen.  A negative fixture is such a file,
    and it must fail to compile anyway.
  - A proof pointer from a void pointer, from std::malloc, from an
    allocator, or from the inactive member of a union.  These routes do not
    name std::start_lifetime_as, and scripts/check-proof-routes.py reads
    them.  The forgeability ledger in test/fixy/test_forgeable_proofs.cpp
    pins them.
  - A frozen site whose template argument a later change can make a proof
    type.  The only such site is SwissTableBuffer<SlotPtr> in
    include/crucible/safety, whose SlotPtr no constraint holds.  Its
    allowlist entry names it, and the frozen tree admits no new site.

Exit codes
  0  each use is admitted, and each entry admits exactly its uses
  1  a use outside the three places, a use with no entry, or a key with
     more uses than its entry admits
  2  an entry above its count or outside the three places, a preprocessor
     failure, a bad invocation, or a failed self-test
  3  the pinned tree-sitter kit is not installed

Usage
  check-start-lifetime.py [--compile-db PATH]         check the tree
  check-start-lifetime.py [--compile-db PATH] --list  print each use
  check-start-lifetime.py --self-test                 plant each route and
                                                      each known bypass,
                                                      and prove the verdicts
"""

from __future__ import annotations

import argparse
import bisect
import contextlib
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from collections import defaultdict
from collections.abc import Callable, Sequence
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import tsast  # noqa: E402
from preprocessed import Store, files_of  # noqa: E402

REPO_ROOT = Path(__file__).resolve().parent.parent
NAMES = frozenset({"start_lifetime_as", "start_lifetime_as_array"})
SOURCE_SUFFIXES = frozenset({".c", ".C", ".h", ".H", ".cc", ".hh", ".cpp", ".hpp", ".cxx", ".hxx", ".c++", ".h++",
                             ".cp", ".CPP", ".ixx", ".cppm", ".mpp", ".inl", ".ipp", ".tpp", ".tcc", ".txx",
                             ".icc", ".inc", ".ii"})
SKIPPED_DIRS = frozenset({".git", "third_party", "external", "vendor", "__pycache__", "node_modules"})
# The directories that an include path is resolved against, after the
# directory of the file that names it.
INCLUDE_ROOTS = ("include", "", "src", "test", "vessel", "bench", "tools", "fuzz", "examples")
# The one file whose use of the library start is checked at compile time.
CHECKED_START = "include/foundation/Lifetime.h"
# A negative-compile fixture: a file under a test directory named neg or *_neg.
FIXTURE = re.compile(r"^test/(?:[^/]+/)*(?:neg|[^/]+_neg)/[^/]+$")
ENTRY = re.compile(r"^(?P<key>.*?)(?: x(?P<count>[1-9][0-9]*))?$")
# The leaves that spell one part of a name.
NAME_LEAVES = ("identifier", "field_identifier", "type_identifier", "namespace_identifier")
# The nodes whose name field is a leaf and whose arguments field is its template argument list.
TEMPLATE_IDS = frozenset({"template_function", "template_method", "template_type"})
# The directive of a #include that the kit reads as an unknown directive,
# for example one with a comment between # and include.
DIRECTIVES = frozenset({"#", "%:"})
# The tokens that end a template argument list read from tokens.
STATEMENT_END = frozenset({";", "{", "}"})

Uses = dict[str, list[int]]
Expanded = dict[str, set[tuple[int, int]]]


def is_source(path: Path) -> bool:
    """True when the suffix names a C or C++ source, also under a .in template."""
    suffix = path.suffix
    if suffix == ".in":
        suffix = Path(path.stem).suffix
    return suffix in SOURCE_SUFFIXES


def listed_files(root: Path) -> list[Path]:
    """The files git lists under the root, or every file under it outside a work tree.

    Complexity: linear in the number of files under the root.
    """
    try:
        out = subprocess.run(["git", "-C", str(root), "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
                             check=True, capture_output=True).stdout
        return [root / p for p in out.decode(errors="replace").split("\0") if p]
    except (OSError, subprocess.CalledProcessError):
        found = []
        for dirpath, dirnames, filenames in os.walk(root):
            dirnames[:] = [d for d in dirnames
                           if d not in SKIPPED_DIRS and not d.startswith("build") and not d.startswith("cmake-build")]
            found.extend(Path(dirpath) / name for name in filenames)
        return found


def database_entries(compile_db: Path | None) -> list[dict]:
    """The entries of the compile database, or none without one."""
    return [] if compile_db is None else json.loads(compile_db.read_text())


def entry_source(command: dict) -> Path:
    """The source file of a compile database entry, as an absolute path."""
    source = Path(command["file"])
    return source if source.is_absolute() else Path(command["directory"]) / source


def frozen_prefixes(root: Path) -> list[str]:
    """The frozen prefixes that scripts/frozen-paths.txt lists.

    An unreadable list freezes nothing, so the rule fails closed.
    """
    listing = root / "scripts" / "frozen-paths.txt"
    if not listing.is_file():
        return []
    return [line.split("#", 1)[0].strip() for line in listing.read_text().splitlines()
            if line.split("#", 1)[0].strip()]


def is_admitted_path(path: str, frozen: Sequence[str]) -> bool:
    """True when the path is the checked start, a frozen file or a negative fixture."""
    if path == CHECKED_START or FIXTURE.match(path):
        return True
    return any(path.startswith(prefix) if prefix.endswith("/") else path == prefix for prefix in frozen)


def resolve_include(root: Path, including: Path, delimiter: str, name: str) -> Path | None:
    """The file under the root that an include directive names, if one exists."""
    candidates = [including.parent / name] if delimiter == '"' else []
    candidates += [root / base / name for base in INCLUDE_ROOTS]
    for candidate in candidates:
        if candidate.is_file():
            resolved = candidate.resolve()
            if resolved.is_relative_to(root):
                return resolved
    return None


def shown(root: Path, path: Path) -> str:
    """The path relative to the root when it is under the root."""
    return path.relative_to(root).as_posix() if path.is_relative_to(root) else path.as_posix()


def unspliced(text: str) -> str:
    """Remove each backslash-newline, as translation phase 2 does."""
    return text.replace("\\\r\n", "").replace("\\\n", "")


def spell(texts: Sequence[str]) -> str:
    """Join token texts as tsast.spelled does: one space between two word tokens, none elsewhere."""
    parts: list[str] = []
    for text in texts:
        if parts and (parts[-1][-1].isalnum() or parts[-1][-1] == "_") and (text[0].isalnum() or text[0] == "_"):
            parts.append(" ")
        parts.append(text)
    return "".join(parts)


def node_uses(root: tsast.Node, line_of: Callable[[tsast.Node], int]) -> list[tuple[int, str]]:
    """Return (line, key) for each use below root, in source order.

    Complexity: linear in the number of nodes.
    """
    found: list[tuple[int, str]] = []
    for leaf in root.descendants(*NAME_LEAVES):
        name = unspliced(leaf.text)
        if name not in NAMES:
            continue
        parent = leaf.parent
        arguments = None
        if parent is not None and parent.type in TEMPLATE_IDS and leaf.field == "name":
            arguments = parent.child_by_field("arguments")
        found.append((line_of(leaf), name + ("" if arguments is None else tsast.spelled(arguments))))
    return found


def token_key(tokens: Sequence[tsast.Token], index: int) -> str:
    """The name at tokens[index] and the template argument list that its angle brackets bound, if it has one.

    `>>` closes two lists.  A list that a `;`, `{` or `}` stops before it
    closes gives the key <unbalanced>.
    """
    name = tokens[index].text
    cursor = index + 1
    if cursor >= len(tokens) or tokens[cursor].text != "<":
        return name
    depth = 0
    for end in range(cursor, len(tokens)):
        text = tokens[end].text
        if text in STATEMENT_END:
            break
        depth += text.count("<") if text in ("<", "<<") else 0
        depth -= text.count(">") if text in (">", ">>") else 0
        if depth <= 0:
            return name + spell([t.text for t in tokens[cursor:end + 1]])
    return name + "<unbalanced>"


def token_uses(tokens: Sequence[tsast.Token]) -> list[tuple[int, str]]:
    """Return (line, key) for each use in a token list, in order.

    Complexity: linear in the number of tokens and the length of each key.
    """
    return [(token.row + 1, token_key(tokens, index)) for index, token in enumerate(tokens)
            if token.kind == "identifier" and token.text in NAMES]


def include_targets(tree: tsast.Tree) -> list[tuple[str, str]]:
    """Return (delimiter, name) for each #include of one parse whose operand is a literal."""
    found: list[tuple[str, str]] = []
    for node in tree.find("preproc_include"):
        path = node.child_by_field("path")
        if path is not None and path.type == "string_literal":
            found.append(('"', path.text.strip()[1:-1]))
        elif path is not None and path.type == "system_lib_string":
            found.append(("<", path.text.strip()[1:-1]))
    for node in tree.find("preproc_call"):
        directive = node.child_by_field("directive")
        argument = node.child_by_field("argument")
        if directive is None or argument is None or directive.text.strip() not in DIRECTIVES:
            continue
        tokens = tsast.pp_tokens(argument.text)
        if len(tokens) < 2 or tokens[0].text != "include":
            continue
        if tokens[1].kind == "string":
            found.append(('"', tokens[1].text[1:-1]))
        elif tokens[1].text == "<" and any(t.text == ">" for t in tokens[2:]):
            closing = next(i for i in range(2, len(tokens)) if tokens[i].text == ">")
            found.append(("<", "".join(t.text for t in tokens[2:closing])))
    return found


def with_defines(trees: list[tsast.Tree]) -> list[tsast.Tree]:
    """The trees that hold a #define, whose bodies the pass parses on their own."""
    return [tree for tree in trees if any(True for _ in tree.find("preproc_def", "preproc_function_def"))]


def lexical_scan(root: Path, compile_db: Path | None) -> Uses:
    """Record each use in scope, and follow each include directive to the file it names.

    Each file is parsed once, in rounds: a round parses the files that the
    last round found.  Complexity: linear in the total size of the files in
    scope and of their macro bodies.
    """
    uses: Uses = defaultdict(list)
    pending = {p.resolve() for p in listed_files(root) if is_source(p) and p.is_file()}
    pending |= {entry_source(e).resolve() for e in database_entries(compile_db) if entry_source(e).is_file()}
    seen: set[Path] = set()
    while pending:
        batch = sorted(pending - seen)
        seen |= pending
        pending = set()
        clean: list[tsast.Tree] = []
        for tree in tsast.parse(batch, strict=False):
            path = Path(tree.path)
            where = shown(root, path)
            if tree.diagnostic is None:
                found = node_uses(tree.root, lambda node: node.line)
                clean.append(tree)
            else:
                found = token_uses(tsast.pp_tokens(path.read_bytes().decode(errors="replace")))
            for line, key in found:
                uses[f"{where}:{key}"].append(line)
            for delimiter, name in include_targets(tree):
                target = resolve_include(root, path, delimiter, name)
                if target is not None and target not in seen:
                    pending.add(target)
        for body in tsast.macro_bodies(with_defines(clean)):
            where = shown(root, Path(body.define.tree.path))
            if body.is_parsed:
                found = node_uses(body.root, lambda node: body.origin(node)[0] + 1)
            else:
                found = token_uses(tsast.pp_tokens(body.text, body.first_row))
            for line, key in found:
                uses[f"{where}:{key}"].append(line)
    return uses


def expansion_rows(texts: Sequence[str]) -> tuple[str, list[int]]:
    """Join the chunk texts of one file as preprocessed.joined does, and return the row where each chunk starts."""
    starts: list[int] = []
    row = 0
    for text in texts:
        starts.append(row)
        row += text.count("\n") + 1
    return "\n".join(texts), starts


def preprocessed_scan(root: Path, compile_db: Path, failures: list[str]) -> tuple[Expanded, int, int, float]:
    """Read each database entry from the shared store of scripts/preprocessed.py.

    The store preprocesses a unit one time for this guard and
    check-proof-routes.py together.  Each distinct expansion of a file that
    names the function is parsed one time for each run, however many units
    hold it.  Complexity: linear in the number of units times the files each
    reads, plus one parse of each distinct expansion that names the
    function.
    """
    store = Store(compile_db, root, int(os.environ.get("START_LIFETIME_JOBS", "0") or 0))
    begin = time.monotonic()
    units = cached = 0
    pending: dict[tuple[str, str], tuple[list[str], list[int]]] = {}
    for unit in store.units():
        units += 1
        cached += unit.from_cache
        if unit.failure is not None:
            failures.append(unit.failure)
            continue
        for path, (key, chunks) in files_of(unit).items():
            if (path, key) in pending:
                continue
            texts = [store.text(chunk.digest) for chunk in chunks]
            if any("start_lifetime_as" in text for text in texts):
                pending[(path, key)] = (texts, [chunk.line for chunk in chunks])
    uses: Expanded = defaultdict(set)
    keys = list(pending)
    joined = [expansion_rows(pending[key][0]) for key in keys]
    items = [(f"{path} (expansion {key[:12]})", text) for (path, key), (text, _) in zip(keys, joined)]
    for expansion, (text, starts), tree in zip(keys, joined, tsast.parse_texts(items)):
        path = expansion[0]
        lines = pending[expansion][1]
        if tree.diagnostic is None:
            found = node_uses(tree.root, lambda node: node.line)
        else:
            found = token_uses(tsast.pp_tokens(text))
        ordinals: dict[int, int] = defaultdict(int)
        for row_line, use_key in found:
            row = row_line - 1
            chunk = bisect.bisect_right(starts, row) - 1
            line = lines[chunk] + row - starts[chunk]
            uses[f"{path}:{use_key}"].add((line, ordinals[line]))
            ordinals[line] += 1
    return uses, units, cached, time.monotonic() - begin


def read_allowlist(allowlist: Path) -> dict[str, tuple[int, int]]:
    """Each entry key, the number of uses it admits, and its line in the allowlist."""
    entries: dict[str, tuple[int, int]] = {}
    if not allowlist.is_file():
        return entries
    for number, raw in enumerate(allowlist.read_text().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = ENTRY.match(line)
        entries[match.group("key")] = (int(match.group("count") or 1), number)
    return entries


def check(root: Path, compile_db: Path | None, mode: str) -> int:
    """Compare each use in the tree with the three places and the allowlist, and report to stderr."""
    root = root.resolve()
    allowlist = root / "scripts" / "start-lifetime-allowlist.txt"
    frozen = frozen_prefixes(root)
    entries = read_allowlist(allowlist)
    lexical = lexical_scan(root, compile_db)
    preprocessed: Expanded = defaultdict(set)
    failures: list[str] = []
    if compile_db is not None:
        preprocessed, units, cached, seconds = preprocessed_scan(root, compile_db, failures)
        print(f"check-start-lifetime: preprocessed {units} translation unit(s) in {seconds:.1f} s, "
              f"{cached} from the cache.", file=sys.stderr)
    unreviewed = 0
    total = 0
    for key in sorted(set(lexical) | set(preprocessed)):
        path = key.split(":", 1)[0]
        lines = sorted(set(lexical.get(key, [])) | {line for line, _ in preprocessed.get(key, set())})
        count = max(len(lexical.get(key, [])), len(preprocessed.get(key, set())))
        total += count
        admitted = entries.get(key, (0, 0))[0]
        if not is_admitted_path(path, frozen):
            for line in lines:
                unreviewed += 1
                print(f"START-LIFETIME violation: {path}:{line} starts a lifetime outside the checked start, a "
                      f"frozen file and a negative fixture.  Use foundation::lifetime::start_as_array.  key: {key}",
                      file=sys.stderr)
        elif admitted == 0:
            for line in lines:
                unreviewed += 1
                print(f"START-LIFETIME violation: {path}:{line} has no reviewed entry.  Allowlist key: {key}",
                      file=sys.stderr)
        elif count > admitted:
            unreviewed += 1
            print(f"START-LIFETIME violation: {path} has {count} uses of this key at lines "
                  f"{', '.join(map(str, lines))}, and its entry admits {admitted}.  Allowlist key: "
                  f"{key} x{count}", file=sys.stderr)
        elif mode == "list":
            print(f"REVIEWED  {key}  ({count} use(s), lines {', '.join(map(str, lines))})")
    stale = 0
    for key, (admitted, number) in sorted(entries.items(), key=lambda item: item[1][1]):
        path = key.split(":", 1)[0]
        found = max(len(lexical.get(key, [])), len(preprocessed.get(key, set())))
        if not is_admitted_path(path, frozen):
            stale += 1
            print(f"START-LIFETIME refused entry: {allowlist.name}:{number} names {path}, which is not the "
                  f"checked start, a frozen file or a negative fixture.  An entry cannot admit it.", file=sys.stderr)
        elif not unreviewed and found < admitted:
            stale += 1
            print(f"START-LIFETIME stale: {allowlist.name}:{number} admits {admitted} use(s) of {key}, "
                  f"and the tree has {found}.", file=sys.stderr)
    for failure in failures:
        print(f"START-LIFETIME preprocessor failure: {failure}.  A translation unit the guard cannot read can "
              f"hold a use, so the run is refused.", file=sys.stderr)
    print(f"check-start-lifetime: {total} use(s) under {len(set(lexical) | set(preprocessed))} key(s), "
          f"{unreviewed} unreviewed, {stale} refused or stale entr(y/ies), {len(failures)} preprocessor "
          f"failure(s).", file=sys.stderr)
    if unreviewed:
        print("\nEach use of std::start_lifetime_as or std::start_lifetime_as_array outside the checked start,\n"
              "a frozen file and a negative fixture is refused.\n"
              "  (1) Prefer a construction, std::bit_cast, or a typed arena to the lifetime start.\n"
              "  (2) Use foundation::lifetime::start_as_array from <foundation/Lifetime.h>.  It refuses at\n"
              "      compile time a type with a subobject that is not an implicit-lifetime type.  A single\n"
              "      object is a span of one.\n"
              "  (3) A negative fixture adds the printed key to scripts/start-lifetime-allowlist.txt, with a\n"
              "      comment above it that names the element type.",
              file=sys.stderr)
        return 1
    return 2 if stale or failures else 0


ROUTES = r"""#pragma once
#include <array>
#include <memory>
%:include "../../misc/Hidden.txt"
# /* a comment */ include "../../misc/Other.txt"
struct Proof;
template <class P> struct Holder { P proof; };
struct Listed { int value; };
inline void routes(unsigned char* buf) {
    (void)std::start_lifetime_as<Proof[1]>(buf);
    (void)std::start_lifetime_as_array<Proof>(buf, 1);
    (void)std::start_lifetime_as<Holder<Proof>>(buf);
    (void)std::start_lifetime_as<std::array<Proof, 1>>(buf);
    (void)std :: start_lifetime_as < Proof [2] > (buf);
    (void)std::start_lifetime_as /* a comment */ <Proof[3]>(buf);
    (void)std::start_life\
time_as<Proof[4]>(buf);
    (void)std::start_lifetime_as_array<Listed>(buf, 4);
    (void)std::start_lifetime_as<std::conditional_t<(1 > 0), Proof, int>>(buf);
}
using std::start_lifetime_as;
#define PLANTED_LIFETIME std::start_lifetime_as_array
template <class T> T* forward_start(unsigned char* buf) { return std::start_lifetime_as_array<T>(buf, 1); }
"""

QUIET = r"""#pragma once
// std::start_lifetime_as<Proof[5]>(buf) in a line comment
/* std::start_lifetime_as_array<Proof>(buf, 5) in a block comment */
inline const char* quiet_string = "std::start_lifetime_as<Proof[6]>(buf)";
inline const char* quiet_raw = R"delim(std::start_lifetime_as<Proof[7]>(buf) ")" )delim";
inline constexpr long quiet_digits = 1'000'000;
inline constexpr char quiet_quote = '\'';
inline int my_start_lifetime_as_helper() { return 0; }
"""

TWICE = """#pragma once
#define TWICE_LIFETIME(p) std::start_lifetime_as<Proof[18]>(p)
inline void* twice_first(unsigned char* buf) { return TWICE_LIFETIME(buf); }
inline void* twice_second(unsigned char* buf) { return TWICE_LIFETIME(buf); }
#if 0
inline void* other_host(unsigned char* buf) { return std::start_lifetime_as<Proof[19]>(buf); }
#endif
inline void* this_host(unsigned char* buf) { return std::start_lifetime_as<Proof[19]>(buf); }
"""

PLANTED_UNIT = """#include "../include/planted/Pasted.h"
#include "../include/planted/frozen/Twice.h"
#define PLANTED_HEADER "../misc/MacroIncluded.txt"
#include PLANTED_HEADER
#include <Generated.h>
inline void* pasted(unsigned char* buf) { return PASTED_LIFETIME(Proof[13], buf); }
inline const char* mention = "std::start_lifetime_as<Proof[16]>(buf)";
"""

LEXICAL_KEYS = (
    "include/planted/Routes.h:start_lifetime_as<Proof[1]>",
    "include/planted/Routes.h:start_lifetime_as_array<Proof>",
    "include/planted/Routes.h:start_lifetime_as<Holder<Proof>>",
    "include/planted/Routes.h:start_lifetime_as<std::array<Proof,1>>",
    "include/planted/Routes.h:start_lifetime_as<Proof[2]>",
    "include/planted/Routes.h:start_lifetime_as<Proof[3]>",
    "include/planted/Routes.h:start_lifetime_as<Proof[4]>",
    "include/planted/Routes.h:start_lifetime_as_array<Listed>",
    "include/planted/Routes.h:start_lifetime_as<std::conditional_t<(1>0),Proof,int>>",
    "include/planted/Routes.h:start_lifetime_as",
    "include/planted/Routes.h:start_lifetime_as_array",
    "include/planted/Routes.h:start_lifetime_as_array<T>",
    "misc/Hidden.txt:start_lifetime_as<Proof[9]>",
    "misc/Other.txt:start_lifetime_as<Proof[12]>",
    "include/planted/Unit.cppm:start_lifetime_as<Proof[10]>",
    "lib/Stray.h:start_lifetime_as<Proof[8]>",
    "include/planted/frozen/Twice.h:start_lifetime_as<Proof[19]> x2",
)

PREPROCESSED_KEYS = LEXICAL_KEYS + (
    "build-planted/generated.gen:start_lifetime_as<Proof[11]>",
    "src/planted.cpp:start_lifetime_as<Proof[13]>",
    "misc/MacroIncluded.txt:start_lifetime_as<Proof[14]>",
    "build-planted/gen/Generated.h:start_lifetime_as<Proof[15]>",
    "include/planted/frozen/Twice.h:start_lifetime_as<Proof[18]> x2",
)


def self_test() -> int:
    """Plant each route and each known bypass, and prove the verdicts.

    Returns:
        0 when every case holds, 2 otherwise
    """
    failures: list[str] = []
    compiler = os.environ.get("START_LIFETIME_CXX") or shutil.which("c++") or shutil.which("g++") or "c++"

    def run(root: Path, compile_db: Path | None = None) -> tuple[int, str]:
        """Run the check on the planted root and keep its report."""
        buffer = io.StringIO()
        with contextlib.redirect_stdout(buffer), contextlib.redirect_stderr(buffer):
            code = check(root, compile_db, "check")
        return code, buffer.getvalue()

    def expect_reports(name: str, report: str, forbidden: str, keys: Sequence[str]) -> None:
        """Record a failure for a missing, an extra or a forbidden report."""
        lines = report.splitlines()
        reported = [line for line in lines if "key: " in line]
        missing = [key for key in keys if not any(line.endswith(f"key: {key}") for line in lines)]
        if missing or len(reported) != len(keys) or re.search(forbidden, report):
            failures.append(f"{name}: missing {missing}, {len(reported)} reports for {len(keys)} keys, "
                            f"forbidden pattern {forbidden!r}:\n{report}")

    with tempfile.TemporaryDirectory() as work:
        root = Path(work).resolve()
        files = {
            # A planted freeze.  The guard reads the frozen prefixes from it.
            "scripts/frozen-paths.txt": "# planted\ninclude/planted/frozen/\n",
            # Each route of the ledger and each bypass the guard must see.
            # Each use has a distinct template argument, so each key names
            # one planted line.  The <Listed> use has an allowlist entry,
            # and the path is none of the three places, so the entry
            # cannot admit it.
            "include/planted/Routes.h": ROUTES,
            # Two files that a header includes, each with a suffix that is
            # not a source suffix: one through the digraph of #, and one
            # through a directive with a comment inside it.
            "misc/Hidden.txt": "inline void* hidden(unsigned char* buf) "
                               "{ return std::start_lifetime_as<Proof[9]>(buf); }\n",
            "misc/Other.txt": "inline void* other(unsigned char* buf) "
                              "{ return std::start_lifetime_as<Proof[12]>(buf); }\n",
            # A module interface unit, a suffix of its own.
            "include/planted/Unit.cppm": "export module planted;\nexport inline void* unit(unsigned char* buf) "
                                         "{ return std::start_lifetime_as<Proof[10]>(buf); }\n",
            # Text that names the function but uses it nowhere.
            "include/planted/Quiet.h": QUIET,
            # A header outside the usual source trees is in scope too.
            "lib/Stray.h": "#pragma once\ninline void* stray(unsigned char* buf) "
                           "{ return std::start_lifetime_as<Proof[8]>(buf); }\n",
            # A macro that pastes the name from two halves.  The parse sees
            # two names, and only the preprocessed pass sees the expansion.
            "include/planted/Pasted.h": "#pragma once\n#define PASTED_LIFETIME(T, p) std::start_life ## time_as<T>(p)\n",
            # The three admitted places: a frozen file, a negative fixture
            # and the checked start.  Each has a reviewed entry.
            "include/planted/frozen/Reviewed.h": "#pragma once\nstruct Event { int value; };\n"
                                                 "inline Event* reviewed(unsigned char* storage) {\n"
                                                 "    auto* moved = std::start_lifetime_as_array<Event>(storage, 4);\n"
                                                 "    return moved;\n}\n",
            # A frozen header whose one macro body expands twice: the
            # lexical pass sees one use, and the preprocessed pass sees two.
            # It also has an arm that this host does not compile: the
            # lexical pass sees two uses, and the preprocessed pass sees
            # one.  Each count is the larger one.
            "include/planted/frozen/Twice.h": TWICE,
            "test/neg/fixture.cpp": "struct Proof;\ninline void* fixture(unsigned char* buf) "
                                    "{ return std::start_lifetime_as<Proof>(buf); }\n",
            # A generated source in a build directory, with no source
            # suffix.  Only the compile database brings it into scope.
            "build-planted/generated.gen": "inline void* generated(unsigned char* buf) "
                                           "{ return std::start_lifetime_as<Proof[11]>(buf); }\n",
            "scripts/start-lifetime-allowlist.txt":
                "# The planted reviewed site in a frozen file.\n"
                "include/planted/frozen/Reviewed.h:start_lifetime_as_array<Event>\n"
                "# The planted negative fixture.\ntest/neg/fixture.cpp:start_lifetime_as<Proof>\n"
                "# The frozen macro header, each key reviewed for one use.\n"
                "include/planted/frozen/Twice.h:start_lifetime_as<Proof[18]>\n"
                "include/planted/frozen/Twice.h:start_lifetime_as<Proof[19]>\n"
                "# An entry for a path that is none of the three places.  It cannot admit.\n"
                "include/planted/Routes.h:start_lifetime_as_array<Listed>\n",
        }
        for rel, text in files.items():
            (root / rel).parent.mkdir(parents=True, exist_ok=True)
            (root / rel).write_text(text)

        code, report = run(root)
        if code != 1:
            failures.append(f"the planted routes gave exit {code}, not 1:\n{report}")
        expect_reports("the lexical pass", report, r"Quiet\.h|Pasted\.h|<Event>|fixture\.cpp|generated\.gen",
                       LEXICAL_KEYS)
        if "refused entry: start-lifetime-allowlist.txt:9 names include/planted/Routes.h" not in report:
            failures.append(f"the entry for a path outside the three places was not refused:\n{report}")

        # The preprocessed pass.  A translation unit expands the pasted name,
        # includes a file through a macro operand and a header that only its
        # -I flag finds, and names the function inside a literal.  The
        # lexical pass sees none of the three uses.
        (root / "src").mkdir()
        (root / "src/planted.cpp").write_text(PLANTED_UNIT)
        (root / "misc/MacroIncluded.txt").write_text(
            "inline void* macro_included(unsigned char* buf) { return std::start_lifetime_as<Proof[14]>(buf); }\n")
        (root / "build-planted/gen").mkdir(parents=True)
        (root / "build-planted/gen/Generated.h").write_text(
            "inline void* generated_header(unsigned char* buf) { return std::start_lifetime_as<Proof[15]>(buf); }\n")
        database = root / "build-planted/compile_commands.json"
        database.write_text(json.dumps([
            {"directory": str(root), "file": "build-planted/generated.gen",
             "command": f"{compiler} -x c++ -c build-planted/generated.gen -o build-planted/generated.o"},
            {"directory": str(root), "file": "src/planted.cpp",
             "command": f"{compiler} -std=c++20 -Ibuild-planted/gen -MD -MF build-planted/planted.d "
                        f"-c src/planted.cpp -o build-planted/planted.o"}]))
        code, report = run(root, database)
        if code != 1:
            failures.append(f"the preprocessed run gave exit {code}, not 1:\n{report}")
        expect_reports("the preprocessed pass", report, r"Quiet\.h|Pasted\.h|<Event>|fixture\.cpp|Proof\[16\]",
                       PREPROCESSED_KEYS)
        if not re.search(r"preprocessed 2 translation unit\(s\) in [0-9.]+ s, 0 from the cache", report):
            failures.append(f"the first preprocessed run did not run both translation units:\n{report}")

        # The cache gives the same verdict, and a change to a file that a
        # translation unit read makes its entry stale.
        code, report = run(root, database)
        if not re.search(r"preprocessed 2 translation unit\(s\) in [0-9.]+ s, 2 from the cache", report):
            failures.append(f"the second preprocessed run did not read the cache:\n{report}")
        expect_reports("the cached preprocessed pass", report,
                       r"Quiet\.h|Pasted\.h|<Event>|fixture\.cpp|Proof\[16\]", PREPROCESSED_KEYS)
        with (root / "misc/MacroIncluded.txt").open("a") as stream:
            stream.write("inline void* later(unsigned char* buf) { return std::start_lifetime_as<Proof[17]>(buf); }\n")
        code, report = run(root, database)
        if not (re.search(r"preprocessed 2 translation unit\(s\) in [0-9.]+ s, 1 from the cache", report)
                and "key: misc/MacroIncluded.txt:start_lifetime_as<Proof[17]>" in report):
            failures.append(f"a changed dependency did not make its cache entry stale:\n{report}")

        # A translation unit the preprocessor cannot read refuses the run.
        (root / "src/broken.cpp").write_text('#include "absent.h"\n')
        broken = root / "build-planted/broken_commands.json"
        broken.write_text(json.dumps([{"directory": str(root), "file": "src/broken.cpp",
                                       "command": f"{compiler} -c src/broken.cpp -o broken.o"}]))
        for rel in ("include/planted/frozen/Twice.h", "include/planted/Routes.h", "lib/Stray.h", "misc/Hidden.txt",
                    "misc/Other.txt", "include/planted/Unit.cppm", "src/planted.cpp"):
            (root / rel).unlink()
        (root / "scripts/start-lifetime-allowlist.txt").write_text(
            "# The planted reviewed site.\ninclude/planted/frozen/Reviewed.h:start_lifetime_as_array<Event>\n"
            "# The planted fixture.\ntest/neg/fixture.cpp:start_lifetime_as<Proof>\n")
        code, report = run(root, broken)
        if code != 2 or "preprocessor failure: src/broken.cpp" not in report:
            failures.append(f"a preprocessor failure gave exit {code} without its report:\n{report}")
        (root / "src/broken.cpp").unlink()

        # The reviewed key survives a line shift and a new variable name.
        (root / "include/planted/frozen/Reviewed.h").write_text(
            "\n\n#pragma once\nstruct Event { int value; };\ninline Event* reviewed(unsigned char* storage) {\n"
            "    auto* shifted = std::start_lifetime_as_array<Event>(storage, 4);\n    return shifted;\n}\n")
        code, report = run(root)
        if code != 0:
            failures.append(f"the reviewed key did not survive a line shift (exit {code}):\n{report}")

        # A second use under the reviewed key needs its own review.
        with (root / "include/planted/frozen/Reviewed.h").open("a") as stream:
            stream.write("inline Event* second(unsigned char* storage) "
                         "{ return std::start_lifetime_as_array<Event>(storage, 1); }\n")
        code, report = run(root)
        if code != 1 or ("and its entry admits 1.  Allowlist key: "
                         "include/planted/frozen/Reviewed.h:start_lifetime_as_array<Event> x2") not in report:
            failures.append(f"a second use under a reviewed key gave exit {code} without its report:\n{report}")

        # An entry that admits more uses than the tree has is stale, and so
        # is an entry that names no use.
        (root / "scripts/start-lifetime-allowlist.txt").write_text(
            "# The planted reviewed sites, one count too high.\n"
            "include/planted/frozen/Reviewed.h:start_lifetime_as_array<Event> x3\n"
            "# The planted fixture.\ntest/neg/fixture.cpp:start_lifetime_as<Proof>\n"
            "# A planted stale entry.\ninclude/planted/frozen/Reviewed.h:start_lifetime_as<Gone>\n")
        code, report = run(root)
        if (code != 2
                or "admits 3 use(s) of include/planted/frozen/Reviewed.h:start_lifetime_as_array<Event>, and the "
                   "tree has 2." not in report
                or "admits 1 use(s) of include/planted/frozen/Reviewed.h:start_lifetime_as<Gone>, and the tree "
                   "has 0." not in report):
            failures.append(f"stale entries gave exit {code} without their reports:\n{report}")

        # With no frozen list, nothing is frozen, so the frozen site is
        # refused.  The rule fails closed.
        (root / "scripts/frozen-paths.txt").unlink()
        code, report = run(root)
        if code != 1 or "include/planted/frozen/Reviewed.h:" not in report:
            failures.append(f"a missing frozen declaration did not refuse the frozen site (exit {code}):\n{report}")

    for failure in failures:
        print(f"check-start-lifetime --self-test: FAIL — {failure}", file=sys.stderr)
    if failures:
        return 2
    print("check-start-lifetime --self-test: PASS.  Each route, the spacing, comment, continuation, alias, macro, "
          "template and parenthesized forms, an included file with any suffix, a module unit and a header outside "
          "the source trees are reported, and so are the pasted name, the macro include, the -I header, a "
          "generated source and each expansion of a macro body.  Comments, literals, the frozen site and the "
          "fixture are not.  An entry cannot admit a path outside the three places, the cache gives the same "
          "verdict, a changed dependency makes its entry stale, a preprocessor failure refuses the run, and a "
          "missing frozen list freezes nothing.")
    return 0


def main(argv: list[str]) -> int:
    """Parse the arguments and run the check or the self-test."""
    parser = argparse.ArgumentParser(description="Refuse std::start_lifetime_as outside the three admitted places.")
    parser.add_argument("--compile-db", type=Path, help="also read the preprocessed units of this compile database")
    parser.add_argument("--list", action="store_true", help="print each reviewed use")
    parser.add_argument("--self-test", action="store_true", help="plant each route and prove the verdicts")
    try:
        args = parser.parse_args(argv)
    except SystemExit as exc:
        return 0 if exc.code == 0 else 2
    if args.compile_db is not None and not args.compile_db.is_file():
        print(f"check-start-lifetime: the compile database {args.compile_db} does not exist.", file=sys.stderr)
        return 2
    try:
        if args.self_test:
            return self_test()
        compile_db = None if args.compile_db is None else args.compile_db.resolve()
        return check(REPO_ROOT, compile_db, "list" if args.list else "check")
    except tsast.KitMissing as exc:
        print(f"check-start-lifetime: {exc}", file=sys.stderr)
        return 3


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
