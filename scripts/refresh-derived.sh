#!/usr/bin/env bash
#
# refresh-derived — the step a superseded-marking commit runs before it
# commits.
#
# A marking is `git mv X.h _X.h` plus an include migration, and it moves
# a path that several artifacts point at.  Each of those artifacts rots
# in its own way and each has its own gate, so the marking commit lands
# green in the working tree and reds a gate on main.  That has now
# happened three times in four commits:
#
#   517a43a6  moved Topology.h      -> broke two syscall allowlists
#   5b0ad895  moved RowMismatch.h   -> broke the fullness allowlist
#   62c32f16  moved Stage.h and Pipeline.h
#                                   -> dropped three mints from the
#                                      mint inventory, count 169 -> 166
#
# The rule was already written down.  What was missing is a single step
# that runs it, so this script is that step: regenerate every derived
# artifact, then re-check every gate that reads one, and say which of
# them the marking moved.
#
# Two kinds of rot, and both need covering, which is why one gate is not
# enough:
#
#   * A path goes dead.  An allowlist key or a sentence names a file
#     that is no longer there.  check-allowlist-keys.sh answers this for
#     both input sets.
#   * Content goes stale while every path still resolves.  This is the
#     one that bit 62c32f16: no path dangled, because the underscored
#     header exists.  The inventory simply stopped counting three mints,
#     and only regenerating it shows that.
#
# Usage:
#   refresh-derived.sh            # regenerate, then re-check; exit 1 if
#                                 # anything is still red
#   refresh-derived.sh --check    # re-check only, write nothing
#
# Run it before `git commit` on a marking.  What it rewrites is tracked,
# so `git status` after a run names exactly the artifacts the marking
# moved, and those belong in the same commit as the marking.
#
# Exit 0 clean, 1 if a check is still red after the refresh, 2 on a
# usage error.

set -u

REPO_ROOT=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
cd "$REPO_ROOT" || exit 2

MODE=refresh
case "${1-}" in
    --check) MODE=check ;;
    '') ;;
    *)
        printf 'usage: %s [--check]\n' "$(basename -- "$0")" >&2
        exit 2
        ;;
esac

failures=0
declare -a rewrote=()

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
        printf '%s\n' "$out" | sed 's/^/      /' >&2
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
    run_ 'mint inventory'            bash scripts/gen-mint-inventory.sh --write
    run_ 'witness roster fixtures'   bash scripts/check-witness-roster.sh --gen
fi

# ── The re-check half ────────────────────────────────────────────────
#
# In the order a marking breaks them.  The frozen-tree guard comes first
# because a marking that is not an admitted rename is not a marking at
# all, and the rest of the run would be measuring the wrong thing.
printf 'refresh-derived: checking\n' >&2
run_ 'frozen tree'                   bash scripts/check-frozen-tree.sh
run_ 'allowlist keys and prose'      bash scripts/check-allowlist-keys.sh
run_ 'port completeness'             bash scripts/check-port-completeness.sh
run_ 'mint inventory'                bash scripts/gen-mint-inventory.sh --check
run_ 'witness roster'                bash scripts/check-witness-roster.sh --check

# ── What the marking moved ───────────────────────────────────────────
if [ "$MODE" = refresh ]; then
    mapfile -t rewrote < <(git diff --name-only -- misc/mint-inventory.md scripts/witness-roster.txt test 2>/dev/null)
    if [ ${#rewrote[@]} -gt 0 ]; then
        printf '\nrefresh-derived: the refresh rewrote these, and they belong in the marking commit:\n' >&2
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
