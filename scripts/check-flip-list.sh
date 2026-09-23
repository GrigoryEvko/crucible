#!/usr/bin/env bash
# check-flip-list.sh — every file that still names the old substrate is on
# the flip list, every listed file still names it, and the list only shrinks.
#
# Each consumer of the old substrate moves onto include/foundation/ and
# include/fixy/.  The old substrate is the frozen tree: the prefixes in
# FROZEN_PATHS of scripts/check-frozen-tree.sh, read from that script so
# the two guards cannot disagree about what is old.  A consumer is a file
# under the scan roots, outside every frozen prefix, that names the old
# substrate.  scripts/flip-list.txt holds one path per consumer, and its
# header comment gives the flip order.
#
# The rule
# --------
#   - A listed file that still names the old substrate passes.  It is not
#     drained yet.  The guard prints how many remain.
#   - A listed file that no longer names the old substrate FAILS as
#     drained.  Remove its entry in the same commit that drains it.  This
#     is a failure and not a warning: an entry for a drained file is a
#     standing permission, so the file could take the old substrate back
#     and the guard would stay quiet.
#   - An unlisted file that names the old substrate FAILS.  A new consumer
#     cannot appear: write the new substrate.
#   - A listed file that does not exist FAILS.  A deletion drains a file,
#     and its entry goes in the same commit.
#   - A listed path under a frozen prefix, outside every scan root, listed
#     twice, or out of sorted order FAILS, because it is a typo or a
#     merge error, and it can hide one of the conditions above.
#   - An empty list over a tree with no consumer passes.  That is the end
#     state, and after it any consumer at all is a hard error.
#
# What names the old substrate
# ----------------------------
# The guard strips `//` and `/* */` comments, then matches each line
# against three spellings:
#   1. a qualified name, `crucible::N::`;
#   2. an include, `#include <crucible/N/...>` (or with quotes), or an
#      include of the frozen umbrella `crucible/Fixy.h`;
#   3. an unqualified name, `N::`, with no identifier character and no `:`
#      before it.  Code inside `namespace crucible {` reaches the old
#      substrate this way, and the first two spellings miss it.
# N is one of safety, fixy, algebra, effects, permissions, sessions,
# bridges, handles or concurrent.  The third spelling leaves out fixy,
# because an unqualified `fixy::` is the name of the new tree.
#
# What this guard does NOT see, stated rather than implied
# --------------------------------------------------------
#   - It reads text.  A name that a macro or an alias makes is invisible.
#     A file that reaches the old substrate only through a header that
#     includes it, and spells none of it, is not a consumer here.  That is
#     correct: the file drains when its header drains.
#   - `namespace safety {` opened inside `namespace crucible {` has no
#     `::` after the name, and it is not matched.
#   - Unqualified `fixy::` inside `namespace crucible {` finds the old
#     crucible::fixy when that namespace is declared, and it is not matched.
#   - The third spelling reads `effects::` after `using namespace
#     foundation;` as old.  Write `foundation::effects::`.  This error
#     goes toward a false "not drained", which fails closed.
#   - A `//` or `/*` inside a string literal starts a comment for the
#     guard, so a match after it on that line is lost.
#   - A string literal is not stripped.  A literal that spells an old name,
#     such as a reflected type name in a golden, counts as a use.
#
# The files are the tracked and untracked, not ignored, files under the
# scan roots when the root is a git work tree, and every file under the
# scan roots otherwise.
#
# Exit codes
#   0 — every consumer is listed, and every listed file is a consumer
#   1 — an unlisted consumer, a drained or missing entry, or a bad entry
#   2 — bad invocation, an unreadable frozen list, or a failed self-test
#
# Usage
#   check-flip-list.sh [--quiet]     check the tree against the list
#   check-flip-list.sh --scan        print every consumer, one per line
#   check-flip-list.sh --self-test   plant each case and prove its verdict
#
# Environment
#   FLIP_LIST_ROOT   the repository root (the self-test sets it)

set -euo pipefail

script="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/$(basename "${BASH_SOURCE[0]}")"
root="${FLIP_LIST_ROOT:-$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)}"

usage() {
    printf 'usage: %s [--quiet | --scan | --self-test]\n' "${BASH_SOURCE[0]}" >&2
    exit 2
}

# $1 = mode (check | scan).  One pass over the scan roots: O(total bytes).
run_py() {
    python3 - "$1" "$root" <<'PY'
import re
import subprocess
import sys
from pathlib import Path

mode, root = sys.argv[1], Path(sys.argv[2])
LIST = root / "scripts" / "flip-list.txt"
FROZEN_SOURCE = root / "scripts" / "check-frozen-tree.sh"
SCAN_ROOTS = ["include/crucible", "src", "vessel", "bench", "tools", "examples", "fuzz"]
SUFFIXES = {".h", ".hh", ".hpp", ".hxx", ".H", ".C", ".c", ".cc", ".cpp", ".cxx",
            ".inl", ".ipp", ".tpp"}

OLD = "safety|fixy|algebra|effects|permissions|sessions|bridges|handles|concurrent"
OLD_NO_FIXY = "safety|algebra|effects|permissions|sessions|bridges|handles|concurrent"
SPELLINGS = [
    re.compile(r"\bcrucible::(?:" + OLD + r")::"),
    re.compile(r'#\s*include\s*[<"]crucible/(?:(?:' + OLD + r')/|Fixy\.h[>"])'),
    re.compile(r"(?<![\w:])(?:" + OLD_NO_FIXY + r")::"),
]


def die(message: str) -> None:
    print(f"check-flip-list: {message}", file=sys.stderr)
    sys.exit(2)


def frozen_prefixes() -> list[str]:
    """The FROZEN_PATHS array of check-frozen-tree.sh, in order."""
    if not FROZEN_SOURCE.is_file():
        die(f"{FROZEN_SOURCE} is missing, so the old substrate is undefined.")
    text = FROZEN_SOURCE.read_text()
    match = re.search(r"^FROZEN_PATHS=\(\n(.*?)^\)", text, re.S | re.M)
    if not match:
        die(f"no FROZEN_PATHS=( ... ) block in {FROZEN_SOURCE}.")
    prefixes = [line.split("#", 1)[0].strip() for line in match.group(1).splitlines()]
    prefixes = [p for p in prefixes if p]
    if not prefixes:
        die(f"the FROZEN_PATHS block in {FROZEN_SOURCE} is empty.")
    return prefixes


FROZEN = frozen_prefixes()


def is_frozen(path: str) -> bool:
    return any(path.startswith(p) if p.endswith("/") else path == p for p in FROZEN)


def is_under_scan_root(path: str) -> bool:
    return any(path.startswith(r + "/") for r in SCAN_ROOTS)


def candidate_files() -> list[str]:
    """Repository-relative paths of every source file under the scan roots."""
    inside = subprocess.run(["git", "-C", str(root), "rev-parse", "--is-inside-work-tree"],
                            capture_output=True, text=True)
    if inside.returncode == 0 and inside.stdout.strip() == "true":
        listed = subprocess.run(
            ["git", "-C", str(root), "ls-files", "-z", "--cached", "--others",
             "--exclude-standard", "--", *SCAN_ROOTS],
            capture_output=True, check=True).stdout.decode().split("\0")
        paths = sorted({p for p in listed if p})
    else:
        paths = sorted(str(p.relative_to(root)) for r in SCAN_ROOTS
                       for p in (root / r).rglob("*") if p.is_file())
    return [p for p in paths
            if Path(p).suffix in SUFFIXES and (root / p).is_file() and not is_frozen(p)]


BLOCK_OR_LINE = re.compile(r"/\*.*?\*/|//[^\n]*", re.S)


def names_old_substrate(path: str) -> bool:
    text = BLOCK_OR_LINE.sub(lambda m: "\n" * m.group(0).count("\n"),
                             (root / path).read_text(errors="replace"))
    return any(s.search(text) for s in SPELLINGS)


consumers = [p for p in candidate_files() if names_old_substrate(p)]

if mode == "scan":
    for p in consumers:
        print(p)
    print(f"check-flip-list: {len(consumers)} file(s) name the old substrate.", file=sys.stderr)
    sys.exit(0)

entries: list[tuple[int, str]] = []
if LIST.exists():
    for lineno, raw in enumerate(LIST.read_text().splitlines(), start=1):
        entry = raw.strip()
        if entry and not entry.startswith("#"):
            entries.append((lineno, entry))

problems: list[str] = []
seen: set[str] = set()
previous = ""
for lineno, entry in entries:
    where = f"flip-list.txt:{lineno}: {entry}"
    if entry in seen:
        problems.append(f"DUPLICATE {where} — listed twice.")
    seen.add(entry)
    if entry < previous:
        problems.append(f"UNSORTED  {where} — sorts before {previous}; keep the list sorted.")
    previous = max(previous, entry)
    if is_frozen(entry):
        problems.append(f"FROZEN    {where} — is the old substrate itself, not a consumer of it.")
    elif not is_under_scan_root(entry):
        problems.append(f"OUTSIDE   {where} — is under no scan root ({', '.join(SCAN_ROOTS)}).")
    elif not (root / entry).is_file():
        problems.append(f"MISSING   {where} — does not exist.  Remove the entry in the commit "
                        f"that deletes or moves the file.")

consumer_set = set(consumers)
for lineno, entry in entries:
    if (root / entry).is_file() and is_under_scan_root(entry) and not is_frozen(entry) \
            and entry not in consumer_set:
        problems.append(f"DRAINED   flip-list.txt:{lineno}: {entry} — names the old substrate no "
                        f"longer.  Remove the entry in this commit.")
for path in consumers:
    if path not in seen:
        problems.append(f"UNLISTED  {path} — names the old substrate and is not on the flip list.  "
                        f"A new consumer cannot appear; write include/foundation or include/fixy.  "
                        f"Run --scan to see the spellings this guard reads.")

for line in problems:
    print(line)
remaining = sum(1 for _, e in entries if e in consumer_set)
print(f"check-flip-list: {remaining} listed file(s) remain on the old substrate; "
      f"{len(entries)} entr(y/ies), {len(consumers)} consumer(s) in the tree, "
      f"{len(problems)} problem(s).", file=sys.stderr)
sys.exit(1 if problems else 0)
PY
}

self_test() {
    local tmp out rc
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    out="$tmp/out.txt"
    local repo="$tmp/repo"
    mkdir -p "$repo/scripts" "$repo/include/crucible/safety" "$repo/include/fixy" \
             "$repo/src" "$repo/bench" "$repo/test"
    git -C "$repo" init -q

    # The frozen list is read from this file, in the same shape as the real one.
    cat >"$repo/scripts/check-frozen-tree.sh" <<'EOF'
FROZEN_PATHS=(
    include/crucible/safety/
    include/crucible/Fixy.h
)
EOF
    # Frozen files name the old substrate and are never consumers.
    printf '#pragma once\nnamespace crucible::safety::detail { struct Linear {}; }\n' \
        >"$repo/include/crucible/safety/Linear.h"
    printf '#pragma once\n#include <crucible/safety/Linear.h>\n' >"$repo/include/crucible/Fixy.h"
    # Outside the scan roots: never a consumer.
    printf '#include <crucible/safety/Linear.h>\n' >"$repo/test/test_linear.cpp"
    # The new tree and its spellings are not the old substrate.
    printf '#pragma once\nnamespace fixy { struct Linear {}; }\n' >"$repo/include/fixy/Linear.h"
    cat >"$repo/src/Clean.cpp" <<'EOF'
#include <fixy/Linear.h>
// crucible::safety::Linear was the old spelling.
/* #include <crucible/safety/Linear.h>
   effects::Row as well */
namespace crucible { fixy::Linear a; foundation::effects::Row<> r; fixy::concurrent::Q q; }
EOF

    # $1 = expected exit, $2 = pattern the output must hold, $3 = case name.
    expect() {
        set +e
        FLIP_LIST_ROOT="$repo" bash "$script" --quiet >"$out" 2>&1
        rc=$?
        set -e
        if [[ $rc -eq $1 ]] && grep -Eq -- "$2" "$out"; then
            printf 'check-flip-list --self-test: %s — exit %s, as expected.\n' "$3" "$rc"
            return 0
        fi
        printf 'check-flip-list --self-test: FAIL — %s: expected exit %s and /%s/, got exit %s.\n' \
            "$3" "$1" "$2" "$rc" >&2
        sed 's/^/    /' "$out" >&2
        return 1
    }

    # Case 1: an empty list over a tree with no consumer.
    : >"$repo/scripts/flip-list.txt"
    expect 0 '0 listed file\(s\) remain' "empty list, clean tree" || return 2

    # Case 2: listed consumers that still name the old substrate, one per
    # spelling.  They pass, and the remaining count is printed.
    printf '#include <crucible/safety/Linear.h>\n' >"$repo/include/crucible/ByInclude.h"
    printf 'int x = sizeof(crucible::safety::Linear);\n' >"$repo/src/ByQualified.cpp"
    printf 'namespace crucible { effects::Row<> r; }\n' >"$repo/bench/ByUnqualified.cpp"
    printf '#include <crucible/Fixy.h>\n' >"$repo/src/ByUmbrella.cpp"
    printf '# a comment line\n\nbench/ByUnqualified.cpp\ninclude/crucible/ByInclude.h\nsrc/ByQualified.cpp\nsrc/ByUmbrella.cpp\n' \
        >"$repo/scripts/flip-list.txt"
    expect 0 '4 listed file\(s\) remain' "listed, not drained" || return 2

    # Case 3: a listed file drained to a comment is reported and fails.
    printf '// once #include <crucible/safety/Linear.h>\n#include <fixy/Linear.h>\n' \
        >"$repo/src/ByQualified.cpp"
    expect 1 'DRAINED .*src/ByQualified.cpp' "listed, drained" || return 2
    printf 'src/ByQualified.cpp\n' >"$tmp/drop"
    grep -vxFf "$tmp/drop" "$repo/scripts/flip-list.txt" >"$tmp/list" || true
    cp "$tmp/list" "$repo/scripts/flip-list.txt"
    expect 0 '3 listed file\(s\) remain' "drained entry removed" || return 2

    # Case 4: an unlisted consumer, committed or untracked, fails.
    printf 'namespace crucible { concurrent::Topology* t; }\n' >"$repo/src/NewConsumer.cpp"
    expect 1 'UNLISTED  src/NewConsumer.cpp' "unlisted consumer" || return 2
    rm "$repo/src/NewConsumer.cpp"

    # Case 5: a listed file that does not exist fails.
    rm "$repo/src/ByUmbrella.cpp"
    expect 1 'MISSING .*src/ByUmbrella.cpp' "listed, missing" || return 2
    printf 'src/ByUmbrella.cpp\n' >"$tmp/drop"
    grep -vxFf "$tmp/drop" "$repo/scripts/flip-list.txt" >"$tmp/list" || true
    cp "$tmp/list" "$repo/scripts/flip-list.txt"
    expect 0 '2 listed file\(s\) remain' "missing entry removed" || return 2

    # Case 6: bad entries — frozen, outside the scan roots, unsorted, twice.
    cp "$repo/scripts/flip-list.txt" "$tmp/good"
    printf 'include/crucible/safety/Linear.h\n' >>"$repo/scripts/flip-list.txt"
    expect 1 'FROZEN .*include/crucible/safety/Linear.h' "frozen entry" || return 2
    cp "$tmp/good" "$repo/scripts/flip-list.txt"
    printf 'test/test_linear.cpp\n' >>"$repo/scripts/flip-list.txt"
    expect 1 'OUTSIDE .*test/test_linear.cpp' "entry outside the scan roots" || return 2
    printf 'include/crucible/ByInclude.h\nbench/ByUnqualified.cpp\n' >"$repo/scripts/flip-list.txt"
    expect 1 'UNSORTED' "unsorted list" || return 2
    printf 'bench/ByUnqualified.cpp\nbench/ByUnqualified.cpp\ninclude/crucible/ByInclude.h\n' \
        >"$repo/scripts/flip-list.txt"
    expect 1 'DUPLICATE' "duplicate entry" || return 2

    # Case 7: the frozen list is read, not assumed.  Unfreezing the old
    # substrate makes its own files consumers, and they are unlisted.
    cp "$tmp/good" "$repo/scripts/flip-list.txt"
    printf 'FROZEN_PATHS=(\n    include/crucible/Fixy.h\n)\n' >"$repo/scripts/check-frozen-tree.sh"
    expect 1 'UNLISTED  include/crucible/safety/Linear.h' "frozen list read from its owner" || return 2
    : >"$repo/scripts/check-frozen-tree.sh"
    expect 2 'no FROZEN_PATHS' "missing frozen list" || return 2

    printf 'check-flip-list --self-test: PASS.\n'
}

case "${1:-}" in
    ""|--quiet)  run_py check ;;
    --scan)      run_py scan ;;
    --self-test) self_test ;;
    -h|--help)   usage ;;
    *)           usage ;;
esac
