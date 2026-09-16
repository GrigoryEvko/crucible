#!/usr/bin/env bash
# check-frozen-tree.sh — the old substrate is frozen until Stage D deletes it.
#
# The canonical substrate is being extracted into include/foundation/ and
# include/fixy/ as a sibling tree.  While the two coexist, the old one under
# include/crucible/{safety,fixy,algebra,effects,permissions,sessions,bridges,
# handles,concurrent} is frozen: a bug found in old code is fixed in the new
# tree or not at all, and nothing is added to the old tree.  Deletions pass —
# Stage D is deletion, and Stage C removes consumers one by one.
#
# The freeze base is the commit recorded below.  The scan diffs that base
# against the working tree (committed and uncommitted changes alike), and any
# Added or Modified path under a frozen prefix fails.  Renames count as an add
# of the destination when it is under a frozen prefix.
#
# Exit status:
#   0 — clean
#   1 — an add or modify under a frozen path
#   2 — bad invocation / not a git tree / self-test failure
#   3 — the freeze base is not in this clone (shallow checkout); ctest skips

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# The freeze base: the last commit before the sibling tree existed.
FREEZE_BASE=1e0cd65e975f53d8838cb4d22e0aad9233374e35

# Frozen prefixes, relative to the repository root.  Directories end in '/',
# single files do not.
FROZEN_PATHS=(
    include/crucible/safety/
    include/crucible/fixy/
    include/crucible/algebra/
    include/crucible/effects/
    include/crucible/permissions/
    include/crucible/sessions/
    include/crucible/bridges/
    include/crucible/handles/
    include/crucible/concurrent/
    include/crucible/Fixy.h
    src/fixy/Fs.cpp
    examples/fn/
)

usage() {
    cat >&2 <<'USAGE'
check-frozen-tree.sh — the old substrate is frozen until Stage D.

Usage:
  check-frozen-tree.sh              # scan; exit 1 on an add/modify under a frozen path
  check-frozen-tree.sh --self-test  # build a throwaway repo, plant edits, verify catch
  check-frozen-tree.sh -h | --help  # usage
USAGE
}

is_frozen() {
    local path="$1" p
    for p in "${FROZEN_PATHS[@]}"; do
        case "$p" in
            */) [[ "$path" == "$p"* ]] && return 0 ;;
            *)  [[ "$path" == "$p" ]] && return 0 ;;
        esac
    done
    return 1
}

scan() {
    # $1 = repo root, $2 = base commit.  Prints violations to stderr.
    local repo="$1" base="$2" rc=0 status path dest
    if ! git -C "$repo" cat-file -e "${base}^{commit}" 2>/dev/null; then
        # Exit 3 is "cannot decide", distinct from a violation and from a bad
        # invocation.  ctest registers it as SKIP_RETURN_CODE, so a shallow
        # clone in the build job skips this guard instead of failing it; the
        # `layers` CI job checks out full history and enforces it.
        printf 'check-frozen-tree: the freeze base %s is not in this clone (shallow checkout?).  Fetch full history (fetch-depth: 0 in CI).\n' "$base" >&2
        return 3
    fi
    # Committed + uncommitted, against the base.  -z keeps paths with spaces
    # intact; status letters: A M D R C T.
    while IFS= read -r -d '' status && IFS= read -r -d '' path; do
        dest="$path"
        case "$status" in
            R*|C*) IFS= read -r -d '' dest ;;
        esac
        case "$status" in
            D) continue ;;
            R*|C*)
                if is_frozen "$dest"; then
                    printf 'FROZEN violation: %s — %s into a frozen path (from %s).  The old substrate only shrinks.\n' \
                        "$dest" "$status" "$path" >&2
                    rc=1
                fi
                ;;
            *)
                if is_frozen "$path"; then
                    printf 'FROZEN violation: %s — %s under a frozen path.  The old substrate only shrinks; put the change in the new tree.\n' \
                        "$path" "$status" >&2
                    rc=1
                fi
                ;;
        esac
    done < <(git -C "$repo" diff --name-status -z "$base" -- 2>/dev/null)
    # Untracked files are not in the diff; an untracked file under a frozen
    # path is an add that has not been staged yet.
    while IFS= read -r -d '' path; do
        if is_frozen "$path"; then
            printf 'FROZEN violation: %s — untracked file under a frozen path.\n' "$path" >&2
            rc=1
        fi
    done < <(git -C "$repo" ls-files --others --exclude-standard -z 2>/dev/null)
    return "$rc"
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        git -C "$tmp_root" init -q
        git -C "$tmp_root" -c user.name=selftest -c user.email=selftest@invalid config commit.gpgsign false
        mkdir -p "$tmp_root/include/crucible/safety" "$tmp_root/include/crucible/fixy" \
                 "$tmp_root/include/foundation" "$tmp_root/src/fixy" "$tmp_root/examples/fn"
        # Distinct contents, or git's rename detection pairs the deleted file
        # with the renamed one and the fixture stops testing what it names.
        printf '// old\n' >"$tmp_root/include/crucible/safety/Old.h"
        printf '// gone\n' >"$tmp_root/include/crucible/fixy/Gone.h"
        printf '// renamed\n' >"$tmp_root/include/crucible/fixy/Renamed.h"
        printf '// fs\n' >"$tmp_root/src/fixy/Fs.cpp"
        printf '// keep\n' >"$tmp_root/examples/fn/keep.cpp"
        git -C "$tmp_root" add -A
        git -C "$tmp_root" -c user.name=selftest -c user.email=selftest@invalid commit -q -m base
        base="$(git -C "$tmp_root" rev-parse HEAD)"

        # Clean tree reads clean.
        out="$(mktemp)"
        rc=0; scan "$tmp_root" "$base" 2>"$out" || rc=$?
        fail() {
            printf 'check-frozen-tree: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$out")" >&2
            rm -f "$out"; exit 2
        }
        [[ "$rc" -eq 0 ]] || fail "clean tree reported $rc"

        # Plant: a modify (frozen), a delete (frozen, allowed), an untracked add
        # (frozen), a rename into a frozen path, an add in the new tree
        # (allowed), a modify of the frozen single file.
        printf '// edited\n' >"$tmp_root/include/crucible/safety/Old.h"
        git -C "$tmp_root" rm -q "include/crucible/fixy/Gone.h"
        printf '// new\n' >"$tmp_root/include/crucible/safety/New.h"
        git -C "$tmp_root" mv "include/crucible/fixy/Renamed.h" "include/crucible/fixy/Moved.h"
        printf '// new layer\n' >"$tmp_root/include/foundation/Fine.h"
        printf '// edited\n' >"$tmp_root/src/fixy/Fs.cpp"
        rc=0; scan "$tmp_root" "$base" 2>"$out" || rc=$?
        [[ "$rc" -eq 1 ]] || fail "planted tree reported $rc, want 1"
        grep -qF 'include/crucible/safety/Old.h' "$out" || fail "modify under a frozen dir not caught"
        grep -qF 'include/crucible/safety/New.h' "$out" || fail "untracked add under a frozen dir not caught"
        grep -qF 'include/crucible/fixy/Moved.h' "$out" || fail "rename into a frozen dir not caught"
        grep -qF 'src/fixy/Fs.cpp' "$out" || fail "modify of a frozen single file not caught"
        if grep -qF 'violation: include/crucible/fixy/Gone.h' "$out"; then fail "a deletion was flagged"; fi
        if grep -qF 'include/foundation/Fine.h' "$out"; then fail "an add in the new tree was flagged"; fi
        rm -f "$out"
        printf 'check-frozen-tree: self-test passed — modify, add, rename-into and single-file edits caught; deletion and new-tree adds clean.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-frozen-tree: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

if ! command -v git >/dev/null 2>&1; then
    printf 'check-frozen-tree: git is required\n' >&2
    exit 2
fi

scan_root="${CRUCIBLE_FROZEN_TEST_ROOT:-$root}"
base="${CRUCIBLE_FROZEN_TEST_BASE:-$FREEZE_BASE}"
rc=0
scan "$scan_root" "$base" || rc=$?

if [[ "$rc" -eq 1 ]]; then
    cat >&2 <<'HINT'

check-frozen-tree: the old substrate changed.  It is frozen until Stage D
deletes it.  A fix belongs in include/foundation/ or include/fixy/; a
consumer that still needs the old tree is flipped at Stage C, not patched
here.  Deleting old files is always allowed.
HINT
fi
exit "$rc"
