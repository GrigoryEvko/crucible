#!/usr/bin/env bash
#
# check-allowlist-keys — each path that the guidance prose names is a file that exists.
#
# A sentence of CLAUDE.md names a file by its path.  A move of the file
# makes the sentence send its reader to a file that is not there.  This
# check makes the move loud: a path in the prose that does not exist
# fails here, and the message names the place that the file most likely
# moved to.
#
# The PATH cell of each ledger row of utils/ledgers/ names a file too.
# The rule ledger-owners (`guard check --rule ledger-owners`) holds those
# cells, so this check reads only the prose.
#
# What is in: markdown at the repository root, and docs/ if it exists.
# Those are the documents loaded as guidance, where a dead path misleads.
# A symbolic link among them is not read.  Its target is a document that the
# scan reads under its own name, or one that is out of scope.  AGENTS.md is a
# link to CLAUDE.md, so a dead path in CLAUDE.md is reported one time.
#
# What is out, and why:
#   * misc/*.md — dated design papers.  They record a finding against the
#     tree as it stood on their date, and several propose files that do
#     not exist yet.  A forward reference is the point of the sentence
#     rather than a defect in it.
#   * any _planned/ directory — the same, by directory convention.
#
# Usage:
#   check-allowlist-keys.sh             # scan the guidance prose
#   check-allowlist-keys.sh --self-test # plant drift, verify catch
#
# Exit 0 clean, 1 on a dead prose path, 2 on a usage error.

set -u

. "$(dirname -- "${BASH_SOURCE[0]}")/repo_root.sh"

# The directories that a path of the prose can start with.  fuzz, lean,
# scripts, tools and toolchain are gone from the root.  They stay in the
# list, so a sentence that still names one of them fails as a dead path.
readonly PATH_ROOTS='include|src|test|bench|utils|vessel|examples|cmake|fuzz|lean|scripts|tools|toolchain'

# Where a path is resolved from.  The self-test points this at its own
# fixture tree.
SCAN_ROOT="${SCAN_ROOT:-$REPO_ROOT}"

# ── Naming where the file went ───────────────────────────────────────
#
# The candidates are the files anywhere in the tree with the same
# basename, which catches a file that moved to a new directory.  Each one
# is reported, so the reader can name the right one.
moved_candidates_() {
    local dead=$1
    local base
    base=$(basename -- "$dead")

    local -a found=()
    local hit
    while IFS= read -r hit; do
        hit=${hit#"$SCAN_ROOT/"}
        [ "$hit" = "$dead" ] && continue
        found+=("$hit")
    # A hidden directory at the root holds no tracked source: .git and the
    # agent work trees in .worktrees.  A file there must not be named as the
    # place a file moved to.
    done < <(find "$SCAN_ROOT" \
                  -path "$SCAN_ROOT/.*" -prune -o \
                  -path "$SCAN_ROOT/build*" -prune -o \
                  -type f -name "$base" -print 2>/dev/null)

    [ ${#found[@]} -eq 0 ] && return 1
    printf '%s\n' "${found[@]}"
}

# ── The prose scan ───────────────────────────────────────────────────
#
# The documents this reads, one per line.  The self-test points
# PROSE_ROOT at its own fixture tree, which is why the walk takes a root
# rather than naming files.  `-type f` does not match a symbolic link, and
# that skip is deliberate: the header says why.
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
    # The match ends in the terminator character, which can be a `.` that
    # ends the sentence.  The path is the longest prefix that ends in an
    # extension, so the terminator never becomes part of it.
    local match
    grep -oE "($PATH_ROOTS)/[A-Za-z0-9_/.-]+\.($PROSE_EXT)(\$|[^A-Za-z0-9_-])" "$1" 2>/dev/null \
        | while IFS= read -r match; do
              [[ $match =~ ^(.*\.($PROSE_EXT)) ]] && printf '%s\n' "${BASH_REMATCH[1]}"
          done \
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
    # The status is a verdict, not the count: an exit status wraps at 256,
    # so 256 dead paths would read as clean.
    [ "$violations" -eq 0 ]
}

# ── The self-test ────────────────────────────────────────────────────
#
# Five axes, and four of them are negative controls: a guard whose
# self-test only plants violations cannot tell you it is capable of
# passing.  The prefix axis is the one that matters most: a path is
# matched inside a sentence rather than at the start of a line, so
# without a terminator `bench/serve.h` matches inside `bench/serve.html`
# and the guard reports a file nobody named.
#
#   1. a document naming a path that moved            -> caught, the new
#      path named, and a copy in a hidden work tree not named
#   2. a document naming a live path                  -> clean
#   3. a longer filename with a shorter one as prefix -> clean
#   4. a placeholder written with angle brackets      -> clean
#   5. a symbolic link to the document                -> not read, so
#      the dead path is reported one time, under the document's name
self_test_() {
    # Not `local`: the EXIT trap fires after the function has returned,
    # and under `set -u` a local that is out of scope by then aborts the
    # cleanup with "unbound variable".
    SELF_TEST_TMP=$(mktemp -d)
    trap 'rm -rf "$SELF_TEST_TMP"' EXIT
    local tmp=$SELF_TEST_TMP
    local out rc dead
    local failures=0

    mkdir -p "$tmp/tree/include/planted" "$tmp/tree/include/other" "$tmp/tree/.worktrees/copy/include/planted" \
             "$tmp/tree/bench"
    : > "$tmp/tree/include/other/Moved.h"
    : > "$tmp/tree/include/planted/Live.h"
    # A copy in an agent work tree is not a place the file moved to.
    : > "$tmp/tree/.worktrees/copy/include/planted/Moved.h"
    : > "$tmp/tree/bench/serve.html"

    cat > "$tmp/tree/Guidance.md" <<'PROSE'
The substrate lives in `include/planted/Moved.h` and the live one is
`include/planted/Live.h`.  The dashboard is `bench/serve.html`.  A task
re-targets `include/planted/<Name>.h` once the move happens.
PROSE
    ln -s Guidance.md "$tmp/tree/Mirror.md"

    out=$(SCAN_ROOT="$tmp/tree" PROSE_ROOT="$tmp/tree" scan_prose_ 2>&1)
    rc=$?
    dead=$(printf '%s\n' "$out" | grep -c 'PROSE dead path')
    if [ "$rc" -ne 1 ] || [ "$dead" -ne 1 ]; then
        printf 'check-allowlist-keys self-test: expected exit 1 and 1 dead prose path, got exit %d and %d.\n' \
               "$rc" "$dead" >&2
        failures=1
    fi
    printf '%s' "$out" | grep -q 'include/planted/Moved.h' || {
        printf 'check-allowlist-keys self-test: the dead prose path was not caught.\n' >&2
        failures=1
    }
    printf '%s' "$out" | grep -q 'include/other/Moved.h' || {
        printf 'check-allowlist-keys self-test: the prose catch did not name the path it moved to.\n' >&2
        failures=1
    }
    printf '%s' "$out" | grep -q '\.worktrees' && {
        printf 'check-allowlist-keys self-test: a copy in a hidden work tree was named as the move.\n' >&2
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
    printf '%s' "$out" | grep -q 'Mirror.md' && {
        printf 'check-allowlist-keys self-test: a symbolic link was read as a document of its own.\n' >&2
        failures=1
    }

    # The clean control for the prose half.  The last path ends its
    # sentence, so a `.` follows the extension.
    cat > "$tmp/tree/Guidance.md" <<'PROSECLEAN'
The live header is `include/planted/Live.h` and the dashboard is
`bench/serve.html`.  The same header again is include/planted/Live.h.
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
    printf 'check-allowlist-keys: self-test passed — a dead prose path is caught one time although a symbolic link names its document, the move is named and a hidden work tree is not, and a live path, a path that ends a sentence, a prefix filename and a placeholder all stay clean.\n' >&2
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

if ! scan_prose_; then
    cat >&2 <<'TAIL'

check-allowlist-keys found a path that no longer exists.

A guidance document that names a missing file sends its reader nowhere,
which is worse in CLAUDE.md than anywhere else, because every session
loads it.  The rule ledger-owners holds the paths of the ledger rows.

Refresh every artifact that names a path in the same commit as the edit
that moves it.  utils/scripts/refresh-derived.sh runs that refresh and
re-checks it.
TAIL
    exit 1
fi
exit 0
