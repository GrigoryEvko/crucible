#!/usr/bin/env bash
#
# refresh-derived — regenerate every derived artifact, then re-check every
# gate that reads one.
#
# A commit that moves, renames or deletes a path can leave an artifact that
# points at it.  Each artifact has its own gate, and two kinds of rot need
# a check:
#
#   * A path goes dead.  An allowlist key or a sentence names a file
#     that is no longer there.  check-allowlist-keys.sh answers this for
#     both input sets.
#   * Content goes stale while every path still resolves.  The mint
#     inventory can stop counting a mint, and only a regenerate shows that.
#
# Usage:
#   refresh-derived.sh            # regenerate, then re-check; exit 1 if
#                                 # anything is still red
#   refresh-derived.sh --check    # re-check only, write nothing
#
# Run it before `git commit`.  The run names each file that the regenerate
# half changed, and those files belong in the same commit.  An edit that
# was in the tree before the run is not named.
#
# Exit 0 clean, 1 if a check is still red after the refresh, 2 on a
# usage error.

set -u

. "$(dirname -- "${BASH_SOURCE[0]}")/repo_root.sh"
cd "$REPO_ROOT" || exit 2

usage_() {
    printf 'usage: %s [--check]\n' "$(basename -- "$0")" >&2
    exit 2
}

MODE=refresh
while [ $# -gt 0 ]; do
    case "$1" in
        --check) MODE=check ;;
        *) usage_ ;;
    esac
    shift
done

failures=0
declare -a rewrote=()
state_before=''
state_after=''

# Prints one line for each path that differs from HEAD: the hash of its
# content and the path, or `-` and the path when no regular file is
# there.  The regenerate half runs between two calls, and a path whose
# line changed between them is a path that the half created, rewrote or
# deleted.  A path whose edit came before the run prints the same line
# both times.  Complexity: linear in the number and size of the changed
# files.
tree_state_() {
    local -a paths=() files=() hashes=()
    local path index
    mapfile -d '' -t paths < <(git ls-files -z --modified --deleted --others --exclude-standard 2>/dev/null | sort -zu)
    for path in "${paths[@]}"; do
        if [ -f "$path" ] && [ ! -L "$path" ]; then
            files+=("$path")
        else
            printf -- '- %s\n' "$path"
        fi
    done
    [ ${#files[@]} -gt 0 ] || return 0
    mapfile -t hashes < <(printf '%s\n' "${files[@]}" | git hash-object --stdin-paths)
    for index in "${!files[@]}"; do
        printf '%s %s\n' "${hashes[index]}" "${files[index]}"
    done
}

run_() {
    local label=$1
    shift
    printf '  %-34s ' "$label" >&2
    local out rc
    out=$("$@" 2>&1)
    rc=$?
    if [ "$rc" -eq 0 ]; then
        printf 'ok\n' >&2
    else
        printf 'FAILED (exit %d)\n' "$rc" >&2
        local line
        while IFS= read -r line; do
            printf '      %s\n' "$line" >&2
        done <<<"$out"
        failures=$((failures + 1))
    fi
    return 0
}

# ── The regenerate half ──────────────────────────────────────────────
#
# Every artifact here is a snapshot of the tree rather than a hand-kept
# list, so the fix for a stale one is always to regenerate it.  A
# hand-kept list is the other half, and it cannot be regenerated: the
# checks below are what name it.
if [ "$MODE" = refresh ]; then
    printf 'refresh-derived: regenerating\n' >&2
    state_before=$(tree_state_)
    run_ 'mint inventory'            python3 utils/scripts/gen-mint-inventory.py --write
    run_ 'witness roster fixtures'   python3 utils/scripts/check-witness-roster.py --gen
    run_ 'walk units'                python3 utils/scripts/check-walk-units.py --write
    state_after=$(tree_state_)
fi

# ── The re-check half ────────────────────────────────────────────────
#
# In the order that a moved path breaks them.
printf 'refresh-derived: checking\n' >&2
run_ 'allowlist keys and prose'      bash utils/scripts/check-allowlist-keys.sh
run_ 'mint inventory'                python3 utils/scripts/gen-mint-inventory.py --check
run_ 'witness roster'                python3 utils/scripts/check-witness-roster.py --check
run_ 'walk units'                    python3 utils/scripts/check-walk-units.py

# ── What the refresh rewrote ─────────────────────────────────────────
if [ "$MODE" = refresh ]; then
    mapfile -t rewrote < <(comm -3 <(printf '%s\n' "$state_before" | sort) <(printf '%s\n' "$state_after" | sort) \
                               | while IFS= read -r line; do
                                     line=${line#$'\t'}
                                     [ -n "$line" ] && printf '%s\n' "${line#* }"
                                 done | sort -u)
    if [ ${#rewrote[@]} -gt 0 ]; then
        printf '\nrefresh-derived: the refresh rewrote these, and they belong in the same commit:\n' >&2
        printf '  %s\n' "${rewrote[@]}" >&2
    fi
fi

if [ "$failures" -ne 0 ]; then
    printf '\nrefresh-derived: %d check(s) still red after the refresh.\n' "$failures" >&2
    printf 'A red that survives a regenerate is a hand-kept artifact, so it needs an edit rather than a rerun.\n' >&2
    exit 1
fi

printf '\nrefresh-derived: clean.\n' >&2
exit 0
