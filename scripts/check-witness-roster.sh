#!/usr/bin/env bash
# check-witness-roster.sh — every witness type has one door, and a
# fixture stands on it.
#
# Seven holes this session were one defect: a type that attests to a
# fact it cannot see — a pin, a mapping, a descriptor, a permission, a
# context — and whose constructor took raw data and trusted it.  Every
# fix was the same rule: the constructor either consumes the evidence
# or IS the operation that produces it.  This guard makes the rule
# mechanical, so it stops needing someone to notice.
#
# scripts/witness-roster.txt is the source of truth.  One line per
# witness type, five fields separated by `|`:
#
#     <type> | <header> | <forging expression> | <reason> | <status>
#
#   type        the class as a caller would spell it, template
#               arguments included, e.g. fixy::Secret<int>.  Spell the
#               CLASS, not an alias: the second regex is derived from
#               the last name segment, and the compiler prints the
#               class it refused.
#   header      the header that declares it, relative to include/
#   expression  a plausible direct construction a forger might write —
#               the raw data handed to the constructor.  It is the one
#               thing no walk can derive, which is why the roster
#               exists.  Two helpers stand in for values the forger
#               would have to hand over: forge::lvalue<T>() and
#               forge::rvalue<T>() yield a T& and a T&& of any type.
#   reason      how the door refuses: private | deleted | no-match.
#               For an open entry, the task that owns the hole, `#NNN`.
#   status      closed  — the door is shut; a fixture is generated that
#                         attempts the expression and must be REJECTED
#               open    — a known hole, recorded so the tally is honest;
#                         no fixture, and the walk tolerates it
#
# From each closed entry, --gen writes
# test/fixy/neg/neg_witness_<slug>_direct_construction.cpp, one
# offending expression per file.  The fixtures are committed and
# --check diffs them against what the roster produces, so a roster edit
# without a regenerate is drift.  test/fixy/CMakeLists.txt reads the
# roster too and registers one negative-compile test per closed entry,
# so the roster is the only place a witness is named.
#
# The walk.  Every class in include/fixy and include/foundation that
# shows one of the door shapes is a witness by construction, and
# --check fails if its base name is absent from the roster in either
# status.  The shapes, each measured against the tree before it was
# admitted:
#
#   - it befriends a `mint_` function;
#   - its constructor takes a passkey, a parameter type ending in
#     `_key` or `Key`;
#   - it IS a passkey: its own name ends in `_key` or `Key`;
#   - it has a static factory returning `std::expected<Self, ...>`,
#     which is where the syscall went when the constructor was closed
#     (OwnedMmap::map_region, OwnedFd::open_path);
#   - it befriends a named function that is not an operator or swap —
#     a door the mint_ naming missed (open_dirfd, transition_to,
#     retag, permission_fork_);
#   - it befriends a class other than itself, which is the door of a
#     handle its owner hands out (SharedPermissionPool lends a Guard,
#     PermissionedMpscChannel hands out its Producer and Consumer
#     handles).  A template that befriends its own other
#     specializations is not counted.
#
# The last two count only when the class also keeps a constructor out
# of public reach (not a copy, a move or a deleted one).  A friend
# beside a public constructor is not a door, it is a shortcut: a base
# that befriends its derived handles, or a pool whose constructor
# consumes the permission it parks, shows the shape and has nothing to
# guard.  Measured: with that condition the tree's friends of swap and
# operator== and SessionHandleBase drop out, and no closed door does.
#
# Friends are not members, so no reflection query can enumerate them;
# the walk reads the source.  A class that shows a shape and is not a
# witness has no place to hide: it needs a roster line, and a line
# needs a fixture that is refused, so the reviewer sees the claim.
#
# Matching.  Every generated fixture spells its construction with
# braces, `T{args}`, and GCC renders the constructor it refused with
# parentheses, `T(args) [with ...]`.  So `\bT\(` is a token the compiler
# produces and the fixture source does not, and it is the second
# required regex for every fixture — the floor the fixture helper sets,
# met without a hand-written regex per type.  The first regex is the
# refusal reason from the roster.
#
# Modes:
#   --check       walk + drift (the CI gate).  Exit 1 on either.
#   --gen         regenerate the fixtures from the roster.
#   --self-test   a clean control, two plants and a mirror.  Exit 2 on
#                 failure.  Needs a configured build dir.
#
# Exit status: 0 clean, 1 walk or drift failure, 2 bad invocation or a
# failed self-test.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
roster_default="$root/scripts/witness-roster.txt"
fixture_dir_default="$root/test/fixy/neg"

usage() {
    cat >&2 <<'USAGE'
check-witness-roster.sh — every witness type has one door, and a fixture stands on it.

Usage:
  check-witness-roster.sh --check      # walk + fixture drift; exit 1 on either
  check-witness-roster.sh --gen        # regenerate test/fixy/neg/neg_witness_*.cpp
  check-witness-roster.sh --walk       # list every class the walk counts as a witness
  check-witness-roster.sh --self-test  # clean control + two plants + mirror; exit 2 on failure
  check-witness-roster.sh -h | --help

Environment (used by --self-test, and by nothing else):
  WITNESS_ROSTER        roster file (default scripts/witness-roster.txt)
  WITNESS_FIXTURE_DIR   fixture directory (default test/fixy/neg)
  WITNESS_INCLUDE_ROOTS colon-separated include roots to walk
                        (default include/fixy:include/foundation)
  WITNESS_BUILD_DIR     a configured build dir whose compile_commands.json
                        supplies the flags the self-test compiles with
USAGE
}

mode="${1:-}"
case "$mode" in
    -h|--help) usage; exit 0 ;;
    --check|--gen|--walk|--self-test) ;;
    *) printf 'check-witness-roster: expected --check, --gen, --walk or --self-test\n\n' >&2; usage; exit 2 ;;
esac

roster="${WITNESS_ROSTER:-$roster_default}"
fixture_dir="${WITNESS_FIXTURE_DIR:-$fixture_dir_default}"
include_roots="${WITNESS_INCLUDE_ROOTS:-$root/include/fixy:$root/include/foundation}"

# The generator, the walk and the drift check are one Python program,
# so the three modes cannot disagree about the roster grammar.
run_py() {
    python3 - "$@" <<'PY'
import re
import sys
from pathlib import Path

mode, roster_path, fixture_dir, include_roots = sys.argv[1:5]
roster_path = Path(roster_path)
fixture_dir = Path(fixture_dir)
roots = [Path(p) for p in include_roots.split(":") if p]
include_base = roots[0].parent          # .../include
repo = include_base.parent

REASONS = {
    "private": "is private within this context",
    "deleted": "use of deleted function",
    "no-match": "no matching function for call",
}

# ── The roster ──────────────────────────────────────────────────────
def parse_roster():
    entries = []
    for lineno, raw in enumerate(roster_path.read_text().splitlines(), 1):
        line = raw.strip()
        if not line or line.startswith("#"):
            continue
        parts = [p.strip() for p in line.split("|")]
        if len(parts) != 5:
            sys.exit(f"witness-roster.txt:{lineno}: expected 5 fields separated by '|', got {len(parts)}")
        type_, header, expr, reason, status = parts
        if status not in ("closed", "open"):
            sys.exit(f"witness-roster.txt:{lineno}: status must be 'closed' or 'open', got '{status}'")
        if status == "closed" and reason not in REASONS:
            sys.exit(f"witness-roster.txt:{lineno}: a closed entry's reason must be one of "
                     f"{', '.join(REASONS)}; got '{reason}'")
        if status == "open" and not re.fullmatch(r"#\d+", reason):
            sys.exit(f"witness-roster.txt:{lineno}: an open entry's reason must be the task that owns "
                     f"the hole, spelled #NNN; got '{reason}'")
        if not (include_base / header).exists():
            sys.exit(f"witness-roster.txt:{lineno}: header '{header}' does not exist under include/")
        entries.append((lineno, type_, header, expr, reason, status))
    return entries

ANGLE_GROUP = re.compile(r"<[^<>]*>")
ROOT_PREFIX = re.compile(r"^(?:fixy|foundation)::")

def qualify(type_):
    """The type with every template argument list removed and the root
    namespace dropped: the key the walk and the roster share.
    fixy::Secret<int> -> Secret ; foundation::effects::detail::ctx_mint::bg_key
    -> effects::detail::ctx_mint::bg_key ;
    fixy::concurrent::PermissionedMpscChannel<int, 8>::ProducerHandle
    -> concurrent::PermissionedMpscChannel::ProducerHandle."""
    stripped = type_
    while True:
        shorter = ANGLE_GROUP.sub("", stripped)
        if shorter == stripped:
            break
        stripped = shorter
    return ROOT_PREFIX.sub("", stripped.strip())

def base_name(type_):
    # The last segment: what the compiler prints before `(` when it
    # refuses the constructor.
    return qualify(type_).rsplit("::", 1)[-1]

def slug(type_):
    return re.sub(r"[^a-z0-9]+", "_", qualify(type_).lower()).strip("_")

def fixture_path(type_):
    return fixture_dir / f"neg_witness_{slug(type_)}_direct_construction.cpp"

# ── The fixture a closed entry produces ─────────────────────────────
def render(type_, header, expr, reason):
    return f"""// GENERATED by scripts/check-witness-roster.sh --gen from
// scripts/witness-roster.txt.  Edit the roster, not this file.
//
// {type_} attests to a fact it cannot see.  The expression below is the
// raw data a forger would hand its constructor, and the fixture stands
// on the door staying shut: the construction must be refused, and the
// refusal must read "{REASONS[reason]}".
//
// The second required diagnostic is the constructor as the compiler
// renders it, with parentheses.  The source spells it with braces, so
// the match cannot be satisfied by the fixture's own text.

#include <{header}>

#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <utility>

namespace forge {{
// Stand-ins for values a forger would have to hand over.  Neither
// returns: the fixture is compiled, never run.
template <class T> [[gnu::noinline]] T& lvalue() noexcept {{ std::abort(); }}
template <class T> [[gnu::noinline]] T&& rvalue() noexcept {{ std::abort(); }}
}}  // namespace forge

int main() {{
    [[maybe_unused]] auto forged = {expr};
    return 0;
}}
"""

# ── The walk ────────────────────────────────────────────────────────
# A friend declaration may carry its own template header on the same
# line (`template <class T> friend ...`) or on the line above, and may
# break after the return type; the walk joins it up to its `;`.
FRIEND_START = re.compile(r"^\s*(?:template\s*<[^;{]*>\s*)?friend\b")
FRIEND_CLASS = re.compile(r"^\s*(?:template\s*<[^;{]*>\s*)?friend\s+(?:class|struct)\s+(?:[\w:]*::)?(\w+)")
FRIEND_MINT = re.compile(r"\bmint_[a-z_0-9]+\s*(?:<[^;{]*>)?\s*\(")
# A hidden friend that is an operator or swap is the class's own
# vocabulary, not a door.  Anything else a class befriends by name can
# reach its private constructor.
FRIEND_VOCABULARY = re.compile(r"\boperator\b|\bswap\s*\(")

def befriends_named_function(declaration):
    after = declaration.split("friend", 1)[1]
    return "(" in after and FRIEND_VOCABULARY.search(after) is None
PASSKEY_NAME = re.compile(r"(?:_key|Key)$")
PASSKEY_CTOR = re.compile(
    r"^\s*(?:(?:constexpr|explicit|inline)\s+)*([A-Za-z_][A-Za-z0-9_]*)\s*\(\s*(?:const\s+)?"
    r"(?:[A-Za-z_][\w:]*::)?([a-z_]*_key|[A-Za-z_]*Key)\b")
STATIC_FACTORY_HEAD = r"\bstatic\b[^;{(]*\bexpected\s*<\s*"
CLASS_HEAD = re.compile(
    r"^\s*(?:template\s*<.*>\s*)?(?:class|struct)\s+(?:\[\[nodiscard\]\]\s+)?"
    r"(?:alignas\([^)]*\)\s+)?([A-Za-z_][A-Za-z0-9_]*)\b")
NAMESPACE_OPEN = re.compile(r"^\s*(?:inline\s+)?namespace\s+([\w:]+)\s*\{")
STRING_LIT = re.compile(r'"(?:\\.|[^"\\])*"')
CHAR_LIT = re.compile(r"'(?:\\.|[^'\\])'")

ACCESS_LABEL = re.compile(r"^\s*(public|private|protected)\s*:")
CTOR_HEAD = r"^\s*(?:\[\[[^\]]*\]\]\s*)?(?:(?:constexpr|explicit|inline)\s+)*{name}\s*\((.*)$"
COPY_OR_MOVE_PARAM = r"^\s*(?:const\s+)?{name}\s*(?:const\s*)?&"

class Scope:
    """One namespace or class on the walk's stack."""
    def __init__(self, name, depth, is_class, access):
        self.name = name
        self.depth = depth
        self.is_class = is_class
        self.access = access            # the label in force: public | private | protected
        self.strong = False             # a shape that is a door on its own
        self.weak = False               # a shape that is a door only beside a closed constructor
        self.nonpublic_ctor = False     # a constructor that is not public, not copy, not move, not deleted
        self.line = 0

def walk_witnesses():
    """Every class that shows a door shape: qualified name -> file:line.

    The qualified name is the namespaces and enclosing classes joined
    with `::`, with the root namespace dropped, so it equals qualify()
    of the roster's spelling.  A mint_ friend, a passkey and a static
    expected<Self> factory are doors on their own.  A named friend
    function and a friend class are doors only when the class also
    keeps a constructor out of public reach — otherwise a base that
    befriends its derived, or a channel that befriends its handles, is
    reported for a door it does not have."""
    found = {}

    def close(scope, path):
        if scope.is_class and (scope.strong or (scope.weak and scope.nonpublic_ctor)):
            # A planted root in --self-test lies outside the repo.
            shown = path.relative_to(repo) if path.is_relative_to(repo) else path
            found.setdefault(scope.qualified, f"{shown}:{scope.line}")

    for root_dir in roots:
        for path in sorted(root_dir.rglob("*.h")):
            enclosing = []      # Scope stack
            pending = None      # (name, keyword) of a class head whose `{` has not arrived yet
            friend_buf = None   # a friend declaration whose `;` has not arrived yet
            depth = 0
            for lineno, line in enumerate(path.read_text().splitlines(), 1):
                code = line.split("//", 1)[0]
                code = CHAR_LIT.sub("''", STRING_LIT.sub('""', code))
                ns = NAMESPACE_OPEN.match(code)
                if ns:
                    enclosing.append(Scope(ns.group(1), depth, False, "public"))
                head = CLASS_HEAD.match(code)
                if head and ";" not in code:
                    pending = (head.group(1), head.group(0).split()[0] if head.group(0).split()[0] in ("class", "struct") else
                               ("struct" if re.search(r"\bstruct\b", head.group(0)) else "class"))
                elif ";" in code and "{" not in code:
                    pending = None
                if pending is not None and "{" in code:
                    name, keyword = pending
                    scope = Scope(name, depth, True, "public" if keyword == "struct" else "private")
                    scope.qualified = ROOT_PREFIX.sub("", "::".join([e.name for e in enclosing] + [name]))
                    enclosing.append(scope)
                    pending = None
                if enclosing and enclosing[-1].is_class:
                    scope = enclosing[-1]
                    name = scope.name
                    label = ACCESS_LABEL.match(code)
                    if label:
                        scope.access = label.group(1)
                    friend_class = FRIEND_CLASS.match(code)
                    if friend_class is not None and friend_class.group(1) != name:
                        scope.weak, scope.line = True, scope.line or lineno
                    if friend_buf is not None:
                        friend_buf += " " + code.strip()
                    elif FRIEND_START.match(code) and friend_class is None:
                        friend_buf = code
                    if friend_buf is not None and (";" in friend_buf or "{" in friend_buf):
                        if FRIEND_MINT.search(friend_buf) is not None:
                            scope.strong, scope.line = True, scope.line or lineno
                        elif befriends_named_function(friend_buf):
                            scope.weak, scope.line = True, scope.line or lineno
                        friend_buf = None
                    ctor = re.match(CTOR_HEAD.format(name=re.escape(name)), code)
                    if ctor is not None:
                        rest = ctor.group(1)
                        is_copy_or_move = re.match(COPY_OR_MOVE_PARAM.format(name=re.escape(name)), rest) is not None
                        is_deleted = "= delete" in code
                        if scope.access != "public" and not is_copy_or_move and not is_deleted:
                            scope.nonpublic_ctor = True
                    passkey = PASSKEY_CTOR.match(code)
                    if passkey is not None and passkey.group(1) == name and passkey.group(2) != name:
                        scope.strong, scope.line = True, scope.line or lineno
                    if PASSKEY_NAME.search(name) is not None:
                        scope.strong, scope.line = True, scope.line or lineno
                    if re.search(STATIC_FACTORY_HEAD + re.escape(name) + r"\b", code) is not None:
                        scope.strong, scope.line = True, scope.line or lineno
                depth += code.count("{") - code.count("}")
                while enclosing and depth <= enclosing[-1].depth:
                    close(enclosing.pop(), path)
            while enclosing:
                close(enclosing.pop(), path)
    return found

# ── Modes ───────────────────────────────────────────────────────────
entries = parse_roster()
rostered = {qualify(t) for (_, t, _, _, _, _) in entries}

if mode == "mirror":
    # The first closed entry, for the self-test's mirror arm: the
    # type, the base name, the fixture file and the refusal text.
    for (_, type_, header, expr, reason, status) in entries:
        if status == "closed":
            print("\t".join((type_, base_name(type_), fixture_path(type_).name, REASONS[reason])))
            break
    sys.exit(0)

if mode == "gen":
    fixture_dir.mkdir(parents=True, exist_ok=True)
    wanted = set()
    for (_, type_, header, expr, reason, status) in entries:
        if status != "closed":
            continue
        p = fixture_path(type_)
        wanted.add(p.name)
        p.write_text(render(type_, header, expr, reason))
    # A fixture whose roster line was removed is stale and goes with it.
    for stale in fixture_dir.glob("neg_witness_*_direct_construction.cpp"):
        if stale.name not in wanted:
            stale.unlink()
    print(f"check-witness-roster: wrote {len(wanted)} fixture(s) to {fixture_dir}")
    sys.exit(0)

if mode == "walk":
    for name, where in sorted(walk_witnesses().items()):
        print(f"{name}\t{where}")
    sys.exit(0)

if mode == "check":
    failures = []
    found = walk_witnesses()
    for name, where in sorted(found.items()):
        if name not in rostered:
            failures.append(f"UNROSTERED witness: {name} ({where}) has a door shape — a mint_, named or class "
                            f"friend, a passkey, or a static expected<{name}> factory — and is absent from "
                            f"scripts/witness-roster.txt.  Add it as closed with a forging expression, or as "
                            f"open with the task that owns the hole.")
    seen = {}
    for (lineno, type_, header, expr, reason, status) in entries:
        p = fixture_path(type_)
        if p.name in seen:
            failures.append(f"DUPLICATE: roster lines {seen[p.name]} and {lineno} both produce {p.name}.")
        seen[p.name] = lineno
        if status == "closed":
            if not p.exists():
                failures.append(f"DRIFT: roster line {lineno} ({type_}) is closed but {p.name} does not exist — "
                                f"run --gen and commit the result.")
            elif p.read_text() != render(type_, header, expr, reason):
                failures.append(f"DRIFT: {p.name} differs from what roster line {lineno} produces — "
                                f"run --gen and commit the result.")
        elif p.exists():
            failures.append(f"DRIFT: roster line {lineno} ({type_}) is open but {p.name} exists — an open "
                            f"entry has no fixture; flip the entry to closed or run --gen.")
    for stray in sorted(fixture_dir.glob("neg_witness_*_direct_construction.cpp")):
        if stray.name not in seen:
            failures.append(f"STRAY: {stray.name} has no roster line — run --gen, which removes it, or add "
                            f"the entry.")
    if failures:
        print("\n".join(failures))
        print(f"\ncheck-witness-roster: {len(failures)} failure(s).")
        sys.exit(1)
    open_count = sum(1 for e in entries if e[5] == "open")
    print(f"check-witness-roster: clean — {len(found)} witness class(es) walked, {len(entries)} rostered "
          f"({len(entries) - open_count} closed, {open_count} open), every closed fixture current.")
    sys.exit(0)
PY
}

case "$mode" in
    --gen)   run_py gen   "$roster" "$fixture_dir" "$include_roots"; exit $? ;;
    --check) run_py check "$roster" "$fixture_dir" "$include_roots"; exit $? ;;
    --walk)  run_py walk  "$roster" "$fixture_dir" "$include_roots"; exit $? ;;
esac

# ── --self-test ─────────────────────────────────────────────────────
#
# Five arms.  A clean control: the real roster, walked and diffed,
# passes.  A planted witness: a header with a class that befriends a
# mint_, absent from the roster, must fail the walk.  A planted leak: a
# closed roster entry over a type whose constructor is PUBLIC generates
# a fixture that COMPILES, and compiling is the failure the whole guard
# exists to catch — so the arm asserts the fixture builds, which is
# what the negative-compile harness would then report as red.  The
# mirror: the first closed fixture in the real corpus is refused, with
# both regexes present in the diagnostic.  And the registration: ctest
# knows exactly the fixtures on disk.

build_dir="${WITNESS_BUILD_DIR:-}"
if [[ -z "$build_dir" ]]; then
    for candidate in "$root"/build "$root"/build-*; do
        if [[ -f "$candidate/compile_commands.json" ]]; then build_dir="$candidate"; break; fi
    done
fi
if [[ -z "$build_dir" || ! -f "$build_dir/compile_commands.json" ]]; then
    printf 'check-witness-roster: SELF-TEST needs a configured build dir with compile_commands.json; set WITNESS_BUILD_DIR.\n' >&2
    exit 2
fi

# The compiler and flags a real fixture builds with, lifted from one
# known target so the planted fixture is judged by the same compiler
# and the same dialect as the committed corpus.
read -r compiler flags < <(python3 - "$build_dir/compile_commands.json" <<'PY'
import json, shlex, sys
for e in json.load(open(sys.argv[1])):
    if e["file"].endswith("test/fixy/test_os_time.cpp"):
        parts = shlex.split(e["command"])
        out, skip = [], False
        for p in parts[1:]:
            if skip: skip = False; continue
            if p == "-o": skip = True; continue
            if p == "-c" or p.endswith("test_os_time.cpp"): continue
            out.append(p)
        print(parts[0], shlex.join(out)); break
else:
    sys.exit("compile_commands.json has no entry for test/fixy/test_os_time.cpp")
PY
)

tmp_root="$(mktemp -d)"
trap 'rm -rf "$tmp_root"' EXIT

# Arm 1 — clean control.
rc=0
"$BASH" "${BASH_SOURCE[0]}" --check >"$tmp_root/clean.out" 2>&1 || rc=$?
if (( rc != 0 )); then
    printf 'check-witness-roster: SELF-TEST FAILED — the real roster must pass --check (got %d).\n' "$rc" >&2
    cat "$tmp_root/clean.out" >&2
    exit 2
fi

# Arm 2 — a planted witness the roster does not name must fail the walk.
mkdir -p "$tmp_root/include/planted"
cat >"$tmp_root/include/planted/Leaky.h" <<'PLANTED'
#pragma once
namespace planted {
class Leaky;
template <typename T> constexpr Leaky mint_leaky(T) noexcept;
class Leaky {
    int v_ = 0;
    constexpr explicit Leaky(int v) noexcept : v_{v} {}
    template <typename T> friend constexpr Leaky mint_leaky(T) noexcept;
public:
    Leaky() = default;
};
}  // namespace planted
PLANTED
rc=0
WITNESS_INCLUDE_ROOTS="$include_roots:$tmp_root/include/planted" \
    "$BASH" "${BASH_SOURCE[0]}" --check >"$tmp_root/walk.out" 2>&1 || rc=$?
if (( rc != 1 )) || ! rg -q -F 'UNROSTERED witness: planted::Leaky' "$tmp_root/walk.out"; then
    printf 'check-witness-roster: SELF-TEST FAILED — a planted class befriending a mint_ must be reported as unrostered (got %d).\n' "$rc" >&2
    cat "$tmp_root/walk.out" >&2
    exit 2
fi

# Arm 3 — a closed entry over a PUBLIC constructor produces a fixture
# that compiles, which is the failure.  The plant's header and roster
# live in a copy so the real corpus is untouched.
mkdir -p "$tmp_root/inc/planted" "$tmp_root/inc/fixy" "$tmp_root/inc/foundation" "$tmp_root/neg"
cat >"$tmp_root/inc/planted/Open.h" <<'PLANTED'
#pragma once
namespace planted {
class Open {
    int v_ = 0;
public:
    constexpr explicit Open(int v) noexcept : v_{v} {}   // the hole: public, trusts v
};
}  // namespace planted
PLANTED
printf 'planted::Open | planted/Open.h | planted::Open{42} | private | closed\n' >"$tmp_root/roster.txt"
rc=0
WITNESS_ROSTER="$tmp_root/roster.txt" WITNESS_FIXTURE_DIR="$tmp_root/neg" \
    WITNESS_INCLUDE_ROOTS="$tmp_root/inc/fixy:$tmp_root/inc/foundation" \
    "$BASH" "${BASH_SOURCE[0]}" --gen >"$tmp_root/gen.out" 2>&1 || rc=$?
if (( rc != 0 )) || [[ ! -f "$tmp_root/neg/neg_witness_planted_open_direct_construction.cpp" ]]; then
    printf 'check-witness-roster: SELF-TEST FAILED — the generator did not produce the planted fixture (got %d).\n' "$rc" >&2
    cat "$tmp_root/gen.out" >&2
    exit 2
fi
# The build's flags carry -fdiagnostics-color=always, and GCC colours
# the refused constructor's name from inside: `Bg::<esc>Bg<esc>(`.  The
# neg-compile driver strips the escapes before it matches; here the
# last colour flag wins and there are none to strip.
rc=0
( cd "$root" && eval "$compiler $flags -I$tmp_root/inc -fdiagnostics-color=never -fsyntax-only $tmp_root/neg/neg_witness_planted_open_direct_construction.cpp" ) >"$tmp_root/open.out" 2>&1 || rc=$?
if (( rc != 0 )); then
    printf 'check-witness-roster: SELF-TEST FAILED — a fixture over a PUBLIC constructor must COMPILE, and that compile is the red the guard exists to raise (compiler exit %d).\n' "$rc" >&2
    tail -5 "$tmp_root/open.out" >&2
    exit 2
fi

# Arm 4 — the mirror: a fixture over a real closed door is refused, and
# the refusal carries both regexes the roster implies.
mirror="$(run_py mirror "$roster" "$fixture_dir" "$include_roots")"
if [[ -z "$mirror" ]]; then
    printf 'check-witness-roster: SELF-TEST FAILED — the roster has no closed entry to use as the mirror.\n' >&2
    exit 2
fi
IFS=$'\t' read -r mirror_type mirror_base mirror_file mirror_reason <<<"$mirror"
rc=0
( cd "$root" && eval "$compiler $flags -fdiagnostics-color=never -fsyntax-only $fixture_dir/$mirror_file" ) >"$tmp_root/mirror.out" 2>&1 || rc=$?
# The same two regexes the CMake side registers, matched the way the
# neg-compile driver matches them: against the raw compiler output.
if (( rc == 0 )) || ! rg -q "$mirror_reason" "$tmp_root/mirror.out" || ! rg -q "\\b${mirror_base}\\(" "$tmp_root/mirror.out"; then
    printf 'check-witness-roster: SELF-TEST FAILED — %s must be refused with "%s" and %s( in the diagnostic (compiler exit %d).\n' "$mirror_file" "$mirror_reason" "$mirror_base" "$rc" >&2
    rg 'error:' "$tmp_root/mirror.out" | head -3 >&2
    exit 2
fi

# Arm 5 — the CMake side registers exactly the fixtures the roster
# produces, so a fixture cannot exist without a test standing on it.
registered="$(cd "$build_dir" && ctest -N -R '^neg_witness_.*_direct_construction$' 2>/dev/null | rg -o 'neg_witness_[a-z0-9_]+_direct_construction' | sort -u)"
generated="$(cd "$fixture_dir" && for f in neg_witness_*_direct_construction.cpp; do printf '%s\n' "${f%.cpp}"; done | sort -u)"
if [[ "$registered" != "$generated" ]]; then
    printf 'check-witness-roster: SELF-TEST FAILED — the tests ctest registers do not match the fixtures on disk; reconfigure %s or run --gen.\n' "$build_dir" >&2
    diff <(printf '%s\n' "$registered") <(printf '%s\n' "$generated") >&2 || true
    exit 2
fi

printf 'check-witness-roster: self-test passed — clean control passes, a planted mint_-befriending class is reported unrostered, a closed entry over a public constructor compiles (the red), the mirror over %s is refused with "%s" and %s( in the diagnostic, and ctest registers every fixture.\n' "$mirror_type" "$mirror_reason" "$mirror_base" >&2
exit 0
