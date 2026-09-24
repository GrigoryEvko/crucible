#!/usr/bin/env bash
#
# check-allowlist-keys — every allowlist key names a file that exists.
#
# An allowlist entry is keyed by a repo-relative path.  When that path
# moves, the entry stops matching anything: the guard that reads it no
# longer exempts the site it was written for, and — worse — the guard
# can no longer FIND the site, so a key pointing at a missing file is a
# guard that cannot fire.  That is a defect class this tree keeps
# closing, and it is the one a rename reintroduces silently.
#
# It has happened twice, both times in the same shape.  517a43a6 renamed
# include/crucible/concurrent/Topology.h to _Topology.h and added
# include/fixy/concurrent/Topology.h beside it; the syscall allowlist
# kept the old key, so its sched_getaffinity entry matched neither copy
# and check-syscall-capability reported two fresh violations for a site
# that had been audited for months.  5b0ad895 did the same to
# RowMismatch.h and broke the fullness allowlist the same way.  Neither
# commit touched an allowlist.
#
# Every remaining port runs the same `git mv X.h _X.h` flow, so
# the drift is not a pair of accidents.  This guard makes it loud: a key
# whose path does not exist fails here, with the path the file most
# likely moved to named in the message, before the guard that owns the
# key has a chance to go quietly wrong.
#
# Scope: the PATH half of the key, and nothing else.  Whether the line
# or the code text beside it still matches is each owning guard's
# business — check-syscall-allowlist-prose.sh holds the sentence,
# check-fullness-guard.sh reports its own stale line numbers.  This one
# answers the single question none of them can answer once the file is
# gone.
#
# ── The second input set: guidance prose ─────────────────────────────
#
# One rule, two places it applies.  An allowlist key and a sentence in
# CLAUDE.md both name a file by path, and a port breaks both the same
# way.  The difference is only that a dead key makes a guard stop
# firing, while a dead path in guidance sends a reader to a file that is
# not there.
#
# The prose half was ungated at first, and it had rotted: CLAUDE.md
# named three headers that no longer existed, two of them in section XII
# as the place the contract-enforcement story lives, and AGENTS.md named
# one.  No allowlist dangled at the same moment, because the key half of
# this guard was already holding that line.  So the prose half is not a
# second rule, it is the same rule over the input set nobody had pointed
# it at.
#
# What is in: markdown at the repository root, and docs/ if it exists.
# Those are the documents loaded as guidance, where a dead path misleads.
#
# What is out, and why:
#   * misc/*.md — dated design papers.  They record a finding against the
#     tree as it stood on their date, and several propose files that do
#     not exist yet.  A forward reference is the point of the sentence
#     rather than a defect in it.
#   * any _planned/ directory — the same, by directory convention.
#
# Usage:
#   check-allowlist-keys.sh             # scan allowlist keys and guidance prose
#   check-allowlist-keys.sh --self-test # plant drift, verify catch
#
# Exit 0 clean, 1 on a dead key or a dead prose path, 2 on a usage error.

set -u

REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)

# The directories a repo-relative key can start with.  A first field that
# does not start with one of these is not a path and is skipped, which is
# what lets one scanner read every allowlist whatever else its key
# carries.
readonly PATH_ROOTS='include|src|test|bench|tools|vessel|examples|fuzz|scripts|cmake|lean'

# Where the allowlists live.  Overridden by the self-test so the planted
# files never sit beside the real ones.
ALLOWLIST_DIR="${ALLOWLIST_DIR:-$REPO_ROOT/scripts}"
# Where a key's path is resolved from.  The self-test points this at its
# own fixture tree.
SCAN_ROOT="${SCAN_ROOT:-$REPO_ROOT}"

# ── Extracting the path half of a key ────────────────────────────────
#
# Three shapes are in use, and all three put the path first:
#   path:code text  — prose        (syscall, no-reserve, no-reinterpret)
#   path:line — prose              (fullness)
#   path  — prose                  (ctx-testing-boundary)
#   path                           (fixy-discipline)
# plus one that puts a name in front of it:
#   mint_name|path                 (mint-hs14-floor)
#
# So: drop a leading `name|`, then cut at the first colon or run of
# whitespace.  A key that carries a Windows-style drive letter or any
# other colon inside the path itself is not a shape this tree uses.
extract_path_() {
    local line=$1
    line=${line#*|}
    line=${line%%:*}
    line=${line%%[[:space:]]*}
    printf '%s' "$line"
}

# ── Naming where the file went ───────────────────────────────────────
#
# Two candidates, in the order the tree actually moves files.  First the
# superseded-marking convention, which prefixes the basename with an
# underscore in place; that is what both real failures were.  Then any
# file anywhere in the tree with the same basename, which catches a port
# that moved the file to a new directory.  Both are reported, because a
# port usually produces both at once and the reader has to re-key onto
# each.
moved_candidates_() {
    local dead=$1
    local dir base
    dir=$(dirname -- "$dead")
    base=$(basename -- "$dead")

    local -a found=()
    [ -f "$SCAN_ROOT/$dir/_$base" ] && found+=("$dir/_$base")

    local hit
    while IFS= read -r hit; do
        hit=${hit#"$SCAN_ROOT/"}
        [ "$hit" = "$dead" ] && continue
        [ "$hit" = "$dir/_$base" ] && continue
        found+=("$hit")
    done < <(find "$SCAN_ROOT" \
                  -path "$SCAN_ROOT/.git" -prune -o \
                  -path "$SCAN_ROOT/build*" -prune -o \
                  -type f \( -name "$base" -o -name "_$base" \) -print 2>/dev/null)

    [ ${#found[@]} -eq 0 ] && return 1
    printf '%s\n' "${found[@]}"
}

# ── The prose scan ───────────────────────────────────────────────────
#
# The documents this reads, one per line.  The self-test points
# PROSE_ROOT at its own fixture tree, which is why the walk takes a root
# rather than naming files.
prose_files_() {
    local root=${PROSE_ROOT:-$SCAN_ROOT}
    find "$root" -maxdepth 1 -type f -name '*.md' -print 2>/dev/null
    [ -d "$root/docs" ] && find "$root/docs" -type f -name '*.md' -print 2>/dev/null
    return 0
}

# A path in prose is not the first field of a line, it is anywhere in a
# sentence, so the match has to end where the path ends.  Without the
# terminator, `bench/serve.h` matches inside `bench/serve.html` and the
# guard reports a file that was never named.  The terminator admits `.`
# and `:` after the extension, because a path at the end of a sentence
# and a path carrying a line number are both ordinary here.
readonly PROSE_EXT='h|hpp|cpp|cc|sh|txt|py|json'

extract_prose_paths_() {
    grep -oE "($PATH_ROOTS)/[A-Za-z0-9_/.-]+\.($PROSE_EXT)(\$|[^A-Za-z0-9_-])" "$1" 2>/dev/null \
        | sed 's/[^A-Za-z0-9_/.-]*$//' \
        | sort -u
}

scan_prose_() {
    local violations=0
    local doc
    while IFS= read -r doc; do
        [ -f "$doc" ] || continue
        local rel=${doc#"$SCAN_ROOT/"}
        rel=${rel#"${PROSE_ROOT:-$SCAN_ROOT}/"}
        local path
        while IFS= read -r path; do
            [ -n "$path" ] || continue
            [ -e "$SCAN_ROOT/$path" ] && continue

            violations=$((violations + 1))
            printf 'PROSE dead path: %s names %s, which does not exist.\n' "$rel" "$path" >&2
            local -a cands=()
            mapfile -t cands < <(moved_candidates_ "$path")
            if [ ${#cands[@]} -gt 0 ]; then
                printf '    the file appears to have moved to: %s\n' "${cands[@]}" >&2
                printf '    name the surviving path, so a reader following the sentence arrives somewhere.\n' >&2
            else
                printf '    no file of that name survives anywhere in the tree.  Either the sentence is\n' >&2
                printf '    describing something deleted, in which case say so, or the path is a\n' >&2
                printf '    placeholder, in which case write it so it does not read as a path.\n' >&2
            fi
        done < <(extract_prose_paths_ "$doc")
    done < <(prose_files_)
    return "$violations"
}

# ── The scan ─────────────────────────────────────────────────────────

scan_() {
    local violations=0
    local allowlist
    for allowlist in "$ALLOWLIST_DIR"/*allowlist*.txt; do
        [ -f "$allowlist" ] || continue
        local rel=${allowlist#"$REPO_ROOT/"}
        local lineno=0 line key
        while IFS= read -r line || [ -n "$line" ]; do
            lineno=$((lineno + 1))
            case $line in
                ''|'#'*|' '*'#'*) continue ;;
            esac
            [ -z "${line//[[:space:]]/}" ] && continue

            key=$(extract_path_ "$line")
            # Not path-shaped: no slash, or a first segment that is not a
            # directory this repo has.  Skipped rather than guessed at.
            case $key in
                */*) ;;
                *) continue ;;
            esac
            printf '%s' "$key" | grep -qE "^($PATH_ROOTS)/" || continue

            [ -e "$SCAN_ROOT/$key" ] && continue

            violations=$((violations + 1))
            printf 'ALLOWLIST-KEY dead path: %s:%d keys on %s, which does not exist.\n' \
                   "$rel" "$lineno" "$key" >&2
            local -a cands=()
            mapfile -t cands < <(moved_candidates_ "$key")
            if [ ${#cands[@]} -gt 0 ]; then
                printf '    the file appears to have moved to: %s\n' "${cands[@]}" >&2
                printf '    re-key the entry onto it.  A port that leaves a copy behind needs one entry per copy.\n' >&2
            else
                printf '    no file of that name survives anywhere in the tree.  Either the site is gone,\n' >&2
                printf '    in which case prune the entry, or it moved under a new name, in which case re-key it.\n' >&2
            fi
        done < "$allowlist"
    done
    return "$violations"
}

# ── The self-test ────────────────────────────────────────────────────
#
# Four axes, and the third and fourth are the negative controls: a guard
# whose self-test only plants violations cannot tell you it is capable
# of passing.
#
#   1. a key on a path that was underscored in place  -> caught, and the
#      underscored path named
#   2. a key on a path that is gone entirely          -> caught, with the
#      "no file survives" branch
#   3. a key on a path that exists                    -> clean
#   4. a key that is not path-shaped                  -> clean, because
#      mint-hs14-floor keys on a mint name first
self_test_() {
    # Not `local`: the EXIT trap fires after the function has returned,
    # and under `set -u` a local that is out of scope by then aborts the
    # cleanup with "unbound variable".
    SELF_TEST_TMP=$(mktemp -d)
    trap 'rm -rf "$SELF_TEST_TMP"' EXIT
    local tmp=$SELF_TEST_TMP

    mkdir -p "$tmp/tree/include/planted" "$tmp/lists"
    : > "$tmp/tree/include/planted/_Moved.h"
    : > "$tmp/tree/include/planted/Live.h"

    cat > "$tmp/lists/planted-allowlist.txt" <<'PLANTED'
# Synthetic fixture for check-allowlist-keys.sh --self-test.
include/planted/Moved.h:82 — underscored in place by a port
include/planted/Vanished.h:if (::read(fd, buf, n) < 0) {  — gone entirely
include/planted/Live.h:12 — still there, must stay clean
mint_planted|include/planted/Live.h
PLANTED

    local out rc
    out=$(ALLOWLIST_DIR="$tmp/lists" SCAN_ROOT="$tmp/tree" scan_ 2>&1)
    rc=$?

    local failures=0
    if [ "$rc" -ne 2 ]; then
        printf 'check-allowlist-keys self-test: expected 2 dead keys, got %d.\n' "$rc" >&2
        failures=1
    fi
    printf '%s' "$out" | grep -q 'include/planted/Moved.h' || {
        printf 'check-allowlist-keys self-test: the underscored-in-place key was not caught.\n' >&2
        failures=1
    }
    printf '%s' "$out" | grep -q 'include/planted/_Moved.h' || {
        printf 'check-allowlist-keys self-test: the catch did not name the path it moved to.\n' >&2
        failures=1
    }
    printf '%s' "$out" | grep -q 'include/planted/Vanished.h' || {
        printf 'check-allowlist-keys self-test: the vanished key was not caught.\n' >&2
        failures=1
    }
    printf '%s' "$out" | grep -q 'no file of that name survives' || {
        printf 'check-allowlist-keys self-test: the vanished key took the wrong branch.\n' >&2
        failures=1
    }
    printf '%s' "$out" | grep -q 'include/planted/Live.h' && {
        printf 'check-allowlist-keys self-test: a live path was reported, so the guard cannot pass.\n' >&2
        failures=1
    }

    # The clean control: the same scanner over a list with only live keys
    # must exit 0 and say nothing.
    cat > "$tmp/lists/planted-allowlist.txt" <<'CLEAN'
include/planted/Live.h:12 — still there
mint_planted|include/planted/Live.h
CLEAN
    out=$(ALLOWLIST_DIR="$tmp/lists" SCAN_ROOT="$tmp/tree" scan_ 2>&1)
    rc=$?
    if [ "$rc" -ne 0 ] || [ -n "$out" ]; then
        printf 'check-allowlist-keys self-test: the clean control did not pass (rc=%d, out=%s).\n' "$rc" "$out" >&2
        failures=1
    fi

    # ── The prose half ───────────────────────────────────────────────
    #
    # Four axes again, and two of them are negative controls.  The
    # prefix axis is the one that matters most: a path is matched inside
    # a sentence rather than at the start of a line, so without a
    # terminator `bench/serve.h` matches inside `bench/serve.html` and
    # the guard reports a file nobody named.
    #
    #   1. a document naming a path that was underscored  -> caught
    #   2. a document naming a live path                  -> clean
    #   3. a longer filename with a shorter one as prefix -> clean
    #   4. a placeholder written with angle brackets      -> clean
    mkdir -p "$tmp/tree/bench"
    : > "$tmp/tree/bench/serve.html"

    cat > "$tmp/tree/Guidance.md" <<'PROSE'
The substrate lives in `include/planted/Moved.h` and the live one is
`include/planted/Live.h`.  The dashboard is `bench/serve.html`.  A task
re-targets `include/planted/<Name>.h` once the move happens.
PROSE

    out=$(SCAN_ROOT="$tmp/tree" PROSE_ROOT="$tmp/tree" scan_prose_ 2>&1)
    rc=$?
    if [ "$rc" -ne 1 ]; then
        printf 'check-allowlist-keys self-test: expected 1 dead prose path, got %d.\n' "$rc" >&2
        failures=1
    fi
    printf '%s' "$out" | grep -q 'include/planted/Moved.h' || {
        printf 'check-allowlist-keys self-test: the dead prose path was not caught.\n' >&2
        failures=1
    }
    printf '%s' "$out" | grep -q 'include/planted/_Moved.h' || {
        printf 'check-allowlist-keys self-test: the prose catch did not name the path it moved to.\n' >&2
        failures=1
    }
    printf '%s' "$out" | grep -q 'include/planted/Live.h' && {
        printf 'check-allowlist-keys self-test: a live prose path was reported, so the prose scan cannot pass.\n' >&2
        failures=1
    }
    printf '%s' "$out" | grep -q 'bench/serve.h ' && {
        printf 'check-allowlist-keys self-test: a shorter filename matched inside a longer one, so the terminator is gone.\n' >&2
        failures=1
    }
    printf '%s' "$out" | grep -q 'Name' && {
        printf 'check-allowlist-keys self-test: an angle-bracket placeholder was read as a path.\n' >&2
        failures=1
    }

    # The clean control for the prose half.
    cat > "$tmp/tree/Guidance.md" <<'PROSECLEAN'
The live header is `include/planted/Live.h` and the dashboard is
`bench/serve.html`.
PROSECLEAN
    out=$(SCAN_ROOT="$tmp/tree" PROSE_ROOT="$tmp/tree" scan_prose_ 2>&1)
    rc=$?
    if [ "$rc" -ne 0 ] || [ -n "$out" ]; then
        printf 'check-allowlist-keys self-test: the prose clean control did not pass (rc=%d, out=%s).\n' "$rc" "$out" >&2
        failures=1
    fi

    if [ "$failures" -ne 0 ]; then
        return 1
    fi
    printf 'check-allowlist-keys: self-test passed — an underscored-in-place key and a vanished key are both caught and the move is named, a live key and a non-path key stay clean, a dead prose path is caught, and a live path, a prefix filename and a placeholder all stay clean.\n' >&2
    return 0
}

case "${1-}" in
    --self-test)
        self_test_
        exit $?
        ;;
    '')
        ;;
    *)
        printf 'usage: %s [--self-test]\n' "$(basename -- "$0")" >&2
        exit 2
        ;;
esac

scan_
key_violations=$?
scan_prose_
prose_violations=$?

if [ "$key_violations" -ne 0 ] || [ "$prose_violations" -ne 0 ]; then
    cat >&2 <<'TAIL'

check-allowlist-keys found a path that no longer exists.

An allowlist key that names a missing file exempts nothing and, because
the owning guard locates its site through that key, leaves the guard
unable to fire on the code the entry was written for.  A guidance
document that names a missing file sends its reader nowhere, which is
worse in CLAUDE.md than anywhere else, because every session loads it.

The rule this breaks is the one in the superseded-marking procedure:
refresh every artifact that names a path in the SAME commit as the edit
that moved it.  A port that underscores the original and adds a copy
produces two live paths from one, so it needs two entries where there
was one.  scripts/refresh-derived.sh runs that refresh and re-checks it.
TAIL
    exit 1
fi
exit 0
