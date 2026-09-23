#!/usr/bin/env bash
# check-start-lifetime.sh — std::start_lifetime_as only at reviewed sites.
#
# The rule
# --------
# Each use of std::start_lifetime_as or std::start_lifetime_as_array must
# have an entry in scripts/start-lifetime-allowlist.txt, and each entry
# states how many uses it admits.  A use is each occurrence of the name in
# code: a call, a using-declaration, a macro body, or the address of the
# function.
#
# Why
# ---
# A proof type (a mint key, a context, a capability, a permission, a read
# proof) is neither trivially copyable nor an implicit-lifetime type, so
# std::bit_cast and std::start_lifetime_as<P> refuse it.  Three routes of
# the same library family still give a pointer to a proof object whose
# lifetime never started:
#
#   std::start_lifetime_as<P[1]>(buf)            an array type is an
#                                                implicit-lifetime type
#                                                for any element
#   std::start_lifetime_as_array<P>(buf, 1)      libstdc++ puts no mandate
#                                                on the element type
#   std::start_lifetime_as<Holder>(buf)          an aggregate that holds a
#                                                P, or std::array<P, 1>
#
# A read through that pointer is undefined behavior, and no property of a
# type can refuse the three routes.  So this guard refuses them outside
# reviewed sites, and test/fixy/test_forgeable_proofs.cpp states which
# routes stay open.
#
# The key
# -------
# An allowlist entry is `path:key`, or `path:key xN` for N uses.  The key
# is the name and its template argument list, with the white space
# reduced, for example
#
#   src/perf/SchedSwitch.cpp:start_lifetime_as_array<TimelineSchedEvent>
#
# The template argument is the part that decides safety, so the key names
# it and not a line number.  The key survives a line shift, a rename of
# the variable that holds the result, and a change of the qualification.
# A use with no template argument list has the bare name as its key.  The
# count makes each new use a change to the allowlist, also a new use of a
# key that names a template parameter.  Each entry needs a comment above
# it that gives the reason.
#
# How the scan reads a file
# -------------------------
# The scan joins each backslash-newline pair first, as translation phase 2
# does, so a name split across a line continuation is found.  Then a
# lexer reads comments, string literals, raw string literals, character
# literals, numbers with digit separators, and identifiers.  A use is an
# identifier token with one of the two names, so a name in a comment or
# a literal is not a use.  The include scan reads the same text with its
# comments blanked, so a comment inside a directive is white space, and
# it accepts the digraph %: for #.  This is a lexer, not a parser: it
# does not resolve names or types.
#
# Scope:
#   - every C and C++ source file that git tracks, and every untracked
#     file that .gitignore does not exclude.  A tracked file inside an
#     ignored directory is in scope.  Outside a git work tree, for example
#     in the self-test, the scan walks the whole root and skips build
#     directories;
#   - each file that a scanned file names in an #include directive and
#     that exists under the root, whatever its suffix;
#   - with --compile-db, each source that the compile database names,
#     also a generated source and a source with an unusual suffix.  The
#     ctest registration passes the database of the build.
#
# What this guard does not see, stated rather than implied
# --------------------------------------------------------
#   - a name built by token pasting, for example
#     `start_life ## time_as`.  The lexer sees two identifiers.  The
#     self-test pins this limit.
#   - an #include whose operand is a macro, and a generated header that
#     only an -I flag of the build finds.
#   - a proof pointer from a void pointer, from std::malloc, from an
#     allocator, or from the inactive member of a union.  These routes do
#     not name std::start_lifetime_as, and a lexer cannot know that the
#     target type of a cast is a proof type.  The forgeability ledger in
#     test/fixy/test_forgeable_proofs.cpp pins them.
#   - a reviewed site whose template argument is a template parameter.
#     Such a site is safe only if a constraint holds the parameter to a
#     type whose every subobject is implicit-lifetime.  The allowlist
#     comment of each such entry names the constraint, and a reviewer
#     checks it.  The guard does not.
#   - a change of the type that a reviewed spelling names.  The key is a
#     spelling, so if an alias such as SlotId comes to name a proof type,
#     the key and the count stay the same.  std::start_lifetime_as<T>
#     refuses that change by its own mandate, and the array form does not.
#     foundation::lifetime::start_as_array refuses it, so a site that
#     moves to it leaves this limit.
#
# Exit codes
#   0 — each use has an entry, and each entry admits exactly its uses
#   1 — a use has no entry, or a key has more uses than its entry admits
#   2 — an entry admits more uses than the tree has, a bad invocation, or a
#       failed self-test
#
# Usage
#   check-start-lifetime.sh [--compile-db PATH]         check the tree
#   check-start-lifetime.sh [--compile-db PATH] --list  print each use
#   check-start-lifetime.sh --self-test                 plant each route
#                                                       and each known
#                                                       bypass, and prove
#                                                       the verdicts

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
scan_root="${START_LIFETIME_TEST_ROOT:-$root}"
allowlist="$scan_root/scripts/start-lifetime-allowlist.txt"
compile_db=""

usage() {
    printf 'usage: %s [--compile-db PATH] [--list | --self-test | -h]\n' "${BASH_SOURCE[0]}" >&2
}

run_scan() {
    python3 - "$1" "$scan_root" "$allowlist" "$compile_db" <<'PY'
import json
import os
import re
import subprocess
import sys
from bisect import bisect_right
from collections import defaultdict
from pathlib import Path

mode = sys.argv[1]
root = Path(sys.argv[2]).resolve()
allowlist_path = Path(sys.argv[3])
compile_db = Path(sys.argv[4]) if sys.argv[4] else None

NAMES = frozenset({"start_lifetime_as", "start_lifetime_as_array"})
SOURCE_SUFFIXES = frozenset({".c", ".C", ".h", ".H", ".cc", ".hh", ".cpp", ".hpp", ".cxx", ".hxx", ".c++", ".h++",
                             ".cp", ".CPP", ".ixx", ".cppm", ".mpp", ".inl", ".ipp", ".tpp", ".tcc", ".txx",
                             ".icc", ".inc", ".ii"})
SKIPPED_DIRS = frozenset({".git", "third_party", "external", "vendor", "__pycache__", "node_modules"})
# The directories that an include path is resolved against, after the
# directory of the file that names it.
INCLUDE_ROOTS = ("include", "", "src", "test", "vessel", "bench", "tools", "fuzz", "examples")
# An include directive in text whose comments are blanked, so a comment
# inside the directive is white space.  %: is the digraph of #.
INCLUDE = re.compile(r'^[ \t]*(?:#|%:)[ \t]*include[ \t]*([<"])([^>"\n]+)[>"]', re.M)

# One pass over the text.  The order of the alternatives matters: a raw
# string and a prefixed literal start with an identifier character, so
# they come before the identifier.  A number comes before a character
# literal, so a digit separator such as 1'000 stays in the number.
LEXER = re.compile(r"""
    (?P<line_comment>//[^\n]*)
  | (?P<block_comment>/\*.*?(?:\*/|\Z))
  | (?P<raw>(?:u8|[uUL])?R"(?P<delim>[^()\\ \t\v\f\n"]{0,16})\(.*?\)(?P=delim)")
  | (?P<string>(?:u8|[uUL])?"(?:\\.|[^"\\\n])*"?)
  | (?P<number>\.?[0-9](?:[eEpP][+-]|['\w.])*)
  | (?P<char>(?:u8|[uUL])?'(?:\\.|[^'\\\n])*'?)
  | (?P<ident>[A-Za-z_][A-Za-z_0-9]*)
""", re.S | re.X)
SPLICE = re.compile(r"\\\r?\n")
ENTRY = re.compile(r"^(?P<key>.*?)(?: x(?P<count>[1-9][0-9]*))?$")


def is_source(path: Path) -> bool:
    """True when the suffix names a C or C++ source, also under a .in template."""
    suffix = path.suffix
    if suffix == ".in":
        suffix = Path(path.stem).suffix
    return suffix in SOURCE_SUFFIXES


def listed_files() -> list[Path]:
    """The files git lists under the root, or every file under it outside a work tree.

    Complexity: linear in the number of files under the root."""
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


def database_sources() -> list[Path]:
    """Each source that the compile database names, as an absolute path."""
    if compile_db is None:
        return []
    sources = []
    for command in json.loads(compile_db.read_text()):
        source = Path(command["file"])
        if not source.is_absolute():
            source = Path(command["directory"]) / source
        sources.append(source)
    return sources


def resolve_include(including: Path, delimiter: str, name: str) -> Path | None:
    """The file under the root that an include directive names, if one exists."""
    candidates = [including.parent / name] if delimiter == '"' else []
    candidates += [root / base / name for base in INCLUDE_ROOTS]
    for candidate in candidates:
        if candidate.is_file():
            resolved = candidate.resolve()
            if resolved.is_relative_to(root):
                return resolved
    return None


def shown(path: Path) -> str:
    """The path relative to the root when it is under the root."""
    return path.relative_to(root).as_posix() if path.is_relative_to(root) else path.as_posix()


def splice(text: str) -> tuple[str, list[int]]:
    """Join each backslash-newline pair, and return the offsets of the joins.

    The join offsets are in the joined text, so a line number can be
    recovered from them."""
    joins: list[int] = []
    pieces: list[str] = []
    last = 0
    removed = 0
    for match in SPLICE.finditer(text):
        pieces.append(text[last:match.start()])
        joins.append(match.start() - removed)
        removed += match.end() - match.start()
        last = match.end()
    pieces.append(text[last:])
    return "".join(pieces), joins


def blank_comments(text: str) -> tuple[str, list[int]]:
    """Blank each comment, and return the offsets of the two names in code.

    A literal stays in the text, so the name of an included file stays,
    but a name inside a literal is not a use.  Each blanked character
    becomes a space, and a newline stays, so each offset in the result is
    an offset in the input.  Complexity: linear in the length of the text."""
    out: list[str] = []
    hits: list[int] = []
    last = 0
    for match in LEXER.finditer(text):
        out.append(text[last:match.start()])
        chunk = match.group(0)
        if match.group("line_comment") is not None or match.group("block_comment") is not None:
            out.append("".join("\n" if ch == "\n" else " " for ch in chunk))
        else:
            if match.group("ident") is not None and chunk in NAMES:
                hits.append(match.start())
            out.append(chunk)
        last = match.end()
    out.append(text[last:])
    return "".join(out), hits


def normalized(fragment: str) -> str:
    """Reduce white space: one space between two word characters, none elsewhere."""
    squeezed = re.sub(r"\s+", " ", fragment.strip())
    return re.sub(r" (?=\W)|(?<=\W) ", "", squeezed)


def key_at(blanked: str, offset: int) -> str:
    """The name at the offset and its template argument list, if it has one."""
    name_match = re.compile(r"[A-Za-z_0-9]+").match(blanked, offset)
    name = name_match.group(0)
    cursor = name_match.end()
    while cursor < len(blanked) and blanked[cursor].isspace():
        cursor += 1
    if cursor >= len(blanked) or blanked[cursor] != "<":
        return name
    depth = 0
    for index in range(cursor, min(len(blanked), cursor + 1024)):
        ch = blanked[index]
        if ch == "<":
            depth += 1
        elif ch == ">":
            depth -= 1
            if depth == 0:
                return name + normalized(blanked[cursor:index + 1])
        elif ch in ";{}":
            break
    return name + "<unbalanced>"


def line_of(joined: str, joins: list[int], offset: int) -> int:
    """The line in the file on disk that holds the offset of the joined text."""
    return joined.count("\n", 0, offset) + 1 + bisect_right(joins, offset)


def read_allowlist() -> dict[str, tuple[int, int]]:
    """Each entry key, the number of uses it admits, and its line in the allowlist."""
    entries: dict[str, tuple[int, int]] = {}
    if not allowlist_path.is_file():
        return entries
    for number, raw in enumerate(allowlist_path.read_text().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        match = ENTRY.match(line)
        entries[match.group("key")] = (int(match.group("count") or 1), number)
    return entries


def scan(uses: dict[str, list[int]]) -> None:
    """Record each use in scope, and follow each include directive to the file it names.

    Each file is lexed once.  Complexity: linear in the total size of the
    files in scope."""
    pending = [p.resolve() for p in listed_files() if is_source(p) and p.is_file()]
    pending += [p.resolve() for p in database_sources() if p.is_file()]
    seen: set[Path] = set()
    while pending:
        path = pending.pop()
        if path in seen:
            continue
        seen.add(path)
        joined, joins = splice(path.read_bytes().decode(errors="replace"))
        blanked, hits = blank_comments(joined)
        for offset in hits:
            uses[f"{shown(path)}:{key_at(blanked, offset)}"].append(line_of(joined, joins, offset))
        for delimiter, name in INCLUDE.findall(blanked):
            target = resolve_include(path, delimiter, name)
            if target is not None and target not in seen:
                pending.append(target)


def main() -> int:
    entries = read_allowlist()
    uses: dict[str, list[int]] = defaultdict(list)
    scan(uses)

    unreviewed = 0
    for key, lines in sorted(uses.items()):
        path = key.split(":", 1)[0]
        admitted = entries.get(key, (0, 0))[0]
        if admitted == 0:
            for line in lines:
                unreviewed += 1
                print(f"START-LIFETIME violation: {path}:{line} has no reviewed entry.  Allowlist key: {key}",
                      file=sys.stderr)
        elif len(lines) > admitted:
            unreviewed += 1
            print(f"START-LIFETIME violation: {path} has {len(lines)} uses of this key at lines "
                  f"{', '.join(map(str, lines))}, and its entry admits {admitted}.  Allowlist key: "
                  f"{key} x{len(lines)}", file=sys.stderr)
        elif mode == "list":
            print(f"REVIEWED  {key}  ({len(lines)} use(s), lines {', '.join(map(str, lines))})")
    stale = 0
    if not unreviewed:
        for key, (admitted, number) in sorted(entries.items(), key=lambda item: item[1][1]):
            found = len(uses.get(key, []))
            if found < admitted:
                stale += 1
                print(f"START-LIFETIME stale: {allowlist_path.name}:{number} admits {admitted} use(s) of {key}, "
                      f"and the tree has {found}.", file=sys.stderr)
    total = sum(len(lines) for lines in uses.values())
    print(f"check-start-lifetime: {total} use(s) under {len(uses)} key(s), {unreviewed} unreviewed, "
          f"{stale} stale entr(y/ies).", file=sys.stderr)
    if unreviewed:
        print("\nEach use of std::start_lifetime_as or std::start_lifetime_as_array needs a reviewed entry.\n"
              "  (1) Prefer a construction, std::bit_cast, or a typed arena to the lifetime start.\n"
              "  (2) Use foundation::lifetime::start_as_array from <foundation/Lifetime.h>.  It refuses at\n"
              "      compile time a type with a subobject that is not an implicit-lifetime type.\n"
              "  (3) Only for a frozen or a low-level site, add the printed key to\n"
              "      scripts/start-lifetime-allowlist.txt, with a comment above it that names the element type\n"
              "      and why a proof type cannot reach the site.",
              file=sys.stderr)
        return 1
    return 2 if stale else 0


sys.exit(main())
PY
}

# True when a line of the file ends with the text.
report_ends_with() {
    local line
    while IFS= read -r line; do
        [[ "$line" == *"$1" ]] && return 0
    done <"$2"
    return 1
}

# Runs the guard over the planted root, and keeps its exit code in rc.
run_planted() {
    set +e
    START_LIFETIME_TEST_ROOT="$tmp" bash "${BASH_SOURCE[0]}" "$@" >"$out" 2>&1
    rc=$?
    set -e
}

# Prints the guard output and fails the self-test.
self_test_fail() {
    printf 'check-start-lifetime --self-test: FAIL — %s\n' "$1" >&2
    rg -N '' "$out" >&2 || true
    return 2
}

self_test() {
    local tmp rc out
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    out="$tmp/out.txt"
    mkdir -p "$tmp/scripts" "$tmp/include/planted" "$tmp/lib" "$tmp/misc" "$tmp/build-planted"

    # Each route of the ledger, each bypass the guard must see, and one
    # reviewed site.  Each use has a distinct template argument, so each
    # key names exactly one planted line.
    cat >"$tmp/include/planted/Routes.h" <<'EOF'
#pragma once
#include <array>
#include <memory>
%:include "../../misc/Hidden.txt"
# /* a comment */ include "../../misc/Other.txt"
struct Proof;
template <class P> struct Holder { P proof; };
struct Event { int value; };
inline void routes(unsigned char* buf) {
    (void)std::start_lifetime_as<Proof[1]>(buf);
    (void)std::start_lifetime_as_array<Proof>(buf, 1);
    (void)std::start_lifetime_as<Holder<Proof>>(buf);
    (void)std::start_lifetime_as<std::array<Proof, 1>>(buf);
    (void)std :: start_lifetime_as < Proof [2] > (buf);
    (void)std::start_lifetime_as /* a comment */ <Proof[3]>(buf);
    (void)std::start_life\
time_as<Proof[4]>(buf);
    (void)std::start_lifetime_as_array<Event>(buf, 4);
}
using std::start_lifetime_as;
#define PLANTED_LIFETIME std::start_lifetime_as_array
template <class T> T* forward_start(unsigned char* buf) { return std::start_lifetime_as_array<T>(buf, 1); }
EOF
    # Two files that a header includes, each with a suffix that is not a
    # source suffix: one through the digraph of #, and one through a
    # directive with a comment inside it.
    cat >"$tmp/misc/Hidden.txt" <<'EOF'
inline void* hidden(unsigned char* buf) { return std::start_lifetime_as<Proof[9]>(buf); }
EOF
    cat >"$tmp/misc/Other.txt" <<'EOF'
inline void* other(unsigned char* buf) { return std::start_lifetime_as<Proof[12]>(buf); }
EOF
    # A module interface unit, a suffix of its own.
    cat >"$tmp/include/planted/Unit.cppm" <<'EOF'
export module planted;
export inline void* unit(unsigned char* buf) { return std::start_lifetime_as<Proof[10]>(buf); }
EOF
    # Text that names the function but uses it nowhere.  None of it may be
    # reported.
    cat >"$tmp/include/planted/Quiet.h" <<'EOF'
#pragma once
// std::start_lifetime_as<Proof[5]>(buf) in a line comment
/* std::start_lifetime_as_array<Proof>(buf, 5) in a block comment */
inline const char* quiet_string = "std::start_lifetime_as<Proof[6]>(buf)";
inline const char* quiet_raw = R"delim(std::start_lifetime_as<Proof[7]>(buf) ")" )delim";
inline constexpr long quiet_digits = 1'000'000;
inline constexpr char quiet_quote = '\'';
inline int my_start_lifetime_as_helper() { return 0; }
EOF
    # A header outside the usual source trees is in scope too.
    cat >"$tmp/lib/Stray.h" <<'EOF'
#pragma once
inline void* stray(unsigned char* buf) { return std::start_lifetime_as<Proof[8]>(buf); }
EOF
    # A macro that pastes the name from two halves.  The lexer cannot see
    # it, and the self-test pins that limit: if a later version of the
    # guard reports it, this assertion fails and the limit leaves the
    # header comment.
    cat >"$tmp/include/planted/Pasted.h" <<'EOF'
#pragma once
#define PASTED_LIFETIME(T, p) std::start_life ## time_as<T>(p)
EOF
    # A generated source in a build directory, with no source suffix.
    # Only the compile database brings it into scope.
    cat >"$tmp/build-planted/generated.gen" <<'EOF'
inline void* generated(unsigned char* buf) { return std::start_lifetime_as<Proof[11]>(buf); }
EOF
    printf '[{"directory": "%s", "file": "build-planted/generated.gen", "command": "c++ -c"}]\n' "$tmp" \
        >"$tmp/build-planted/compile_commands.json"
    cat >"$tmp/scripts/start-lifetime-allowlist.txt" <<'EOF'
# The planted reviewed site.
include/planted/Routes.h:start_lifetime_as_array<Event>
EOF

    run_planted
    local expected=(
        'include/planted/Routes.h:start_lifetime_as<Proof[1]>'
        'include/planted/Routes.h:start_lifetime_as_array<Proof>'
        'include/planted/Routes.h:start_lifetime_as<Holder<Proof>>'
        'include/planted/Routes.h:start_lifetime_as<std::array<Proof,1>>'
        'include/planted/Routes.h:start_lifetime_as<Proof[2]>'
        'include/planted/Routes.h:start_lifetime_as<Proof[3]>'
        'include/planted/Routes.h:start_lifetime_as<Proof[4]>'
        'include/planted/Routes.h:start_lifetime_as'
        'include/planted/Routes.h:start_lifetime_as_array'
        'include/planted/Routes.h:start_lifetime_as_array<T>'
        'misc/Hidden.txt:start_lifetime_as<Proof[9]>'
        'misc/Other.txt:start_lifetime_as<Proof[12]>'
        'include/planted/Unit.cppm:start_lifetime_as<Proof[10]>'
        'lib/Stray.h:start_lifetime_as<Proof[8]>'
    )
    local key failed=0
    [[ $rc -eq 1 ]] || { printf 'check-start-lifetime --self-test: FAIL — the planted routes gave exit %s, not 1.\n' "$rc" >&2; failed=1; }
    # A report ends with its key, so a bare-name key does not match the
    # prefix of a longer key.
    for key in "${expected[@]}"; do
        if ! report_ends_with "Allowlist key: $key" "$out"; then
            printf 'check-start-lifetime --self-test: FAIL — no report for %s.\n' "$key" >&2
            failed=1
        fi
    done
    if [[ "$(rg -c 'Allowlist key: ' "$out")" != "${#expected[@]}" ]]; then
        printf 'check-start-lifetime --self-test: FAIL — %s reports, not %s.\n' \
            "$(rg -c 'Allowlist key: ' "$out" || printf 0)" "${#expected[@]}" >&2
        failed=1
    fi
    if rg -q 'Quiet\.h|Pasted\.h|<Event>|generated\.gen' "$out"; then
        printf 'check-start-lifetime --self-test: FAIL — a comment, a literal, a longer identifier, the pasted macro, the reviewed site or a source outside the database was reported.\n' >&2
        failed=1
    fi
    if (( failed )); then
        rg -N '' "$out" >&2 || true
        return 2
    fi
    printf 'check-start-lifetime --self-test: each route, the spacing, comment, continuation, alias, macro and template forms, an included file with any suffix, a module unit and a header outside the source trees are reported, and comments, literals and the reviewed site are not.\n'

    # The compile database brings a generated source into scope.
    run_planted --compile-db "$tmp/build-planted/compile_commands.json"
    report_ends_with 'Allowlist key: build-planted/generated.gen:start_lifetime_as<Proof[11]>' "$out" \
        || { self_test_fail 'a source that only the compile database names was not reported.'; return 2; }
    printf 'check-start-lifetime --self-test: a source that only the compile database names is reported, as expected.\n'

    # The key survives a line shift and a new variable name.
    cat >"$tmp/include/planted/Routes.h" <<'EOF'


#pragma once
struct Event { int value; };
inline Event* reviewed(unsigned char* storage) {
    auto* moved = std::start_lifetime_as_array<Event>(storage, 4);
    return moved;
}
EOF
    rm -f "$tmp/lib/Stray.h" "$tmp/misc/Hidden.txt" "$tmp/misc/Other.txt" "$tmp/include/planted/Unit.cppm"
    run_planted
    [[ $rc -eq 0 ]] || { self_test_fail "the reviewed key did not survive a line shift (exit $rc)."; return 2; }
    printf 'check-start-lifetime --self-test: the reviewed key survives a line shift and a rename, as expected.\n'

    # A second use under the reviewed key needs its own review.
    cat >>"$tmp/include/planted/Routes.h" <<'EOF'
inline Event* second(unsigned char* storage) { return std::start_lifetime_as_array<Event>(storage, 1); }
EOF
    run_planted
    [[ $rc -eq 1 ]] && rg -q -F 'and its entry admits 1.  Allowlist key: include/planted/Routes.h:start_lifetime_as_array<Event> x2' "$out" \
        || { self_test_fail "a second use under a reviewed key gave exit $rc without its report."; return 2; }
    printf 'check-start-lifetime --self-test: a second use under a reviewed key is reported, as expected.\n'

    # An entry that admits more uses than the tree has is stale, and so is
    # an entry that names no use.
    cat >"$tmp/scripts/start-lifetime-allowlist.txt" <<'EOF'
# The planted reviewed sites, one count too high.
include/planted/Routes.h:start_lifetime_as_array<Event> x3
# A planted stale entry.
include/planted/Routes.h:start_lifetime_as<Gone>
EOF
    run_planted
    [[ $rc -eq 2 ]] && rg -q -F 'admits 3 use(s) of include/planted/Routes.h:start_lifetime_as_array<Event>, and the tree has 2.' "$out" \
        && rg -q -F 'admits 1 use(s) of include/planted/Routes.h:start_lifetime_as<Gone>, and the tree has 0.' "$out" \
        || { self_test_fail "stale entries gave exit $rc without their reports."; return 2; }
    printf 'check-start-lifetime --self-test: an entry above its count and an entry with no use are reported, as expected.\n'
    printf 'check-start-lifetime --self-test: PASS.\n'
}

mode=check
while [[ $# -gt 0 ]]; do
    case "$1" in
        --compile-db) [[ $# -ge 2 ]] || { usage; exit 2; }; compile_db="$2"; shift 2 ;;
        --list)       mode=list; shift ;;
        --self-test)  mode=self-test; shift ;;
        -h|--help)    usage; exit 0 ;;
        *)            usage; exit 2 ;;
    esac
done
if [[ -n "$compile_db" && ! -f "$compile_db" ]]; then
    printf 'check-start-lifetime: the compile database %s does not exist.\n' "$compile_db" >&2
    exit 2
fi
case "$mode" in
    check)     run_scan check ;;
    list)      run_scan list ;;
    self-test) self_test ;;
esac
