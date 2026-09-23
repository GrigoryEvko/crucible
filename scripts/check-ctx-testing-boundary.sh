#!/usr/bin/env bash
#
# foundation::effects::testing hands out minted capabilities with no gate.
# Effect.h says naming it outside test and bench code is a review
# rejection, and that a grep for it finds every translation unit taking
# the test path.  Nothing performed that grep, so the boundary was prose.
# A capability is what every ctx-bound mint checks for,
# so an unnoticed use in production code silently hands out authority.
#
# This scans the new production tree and fails on any file naming the
# namespace that the allowlist does not admit.  A listed file that no
# longer names it is a stale entry and also fails, so the list drains
# with the code rather than rotting.
#
# The old tree under include/crucible is frozen and is not scanned: it
# may only shrink, so a marking pass there would violate the freeze.  The
# old tree is deleted when no consumer needs it.
#
#   --self-test   plant one violating file and one clean file, and prove
#                 the scan reports the first and not the second.
#
# Exit 0 clean, 1 on a violation or a stale entry, 2 on a usage error.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
allowlist="$root/scripts/ctx-testing-boundary-allowlist.txt"

# The trees that ship.  test/ and bench/ are absent on purpose: taking the
# test path is what they are for.
SCAN_DIRS=(include/foundation include/fixy src)

# TestWitness is named as well as the three factories, because friending
# it is how the capability constructors stay reachable at all.
#
# The word boundary after TestWitness is load-bearing.  Without it the
# pattern also matches TestWitnessCtx, which is a context TYPE alias and
# mints nothing — a file merely naming that type would read as taking the
# test path.  Match the factory, not every name that starts like it.
readonly PATTERN='effects::testing::|[^_[:alnum:]]testing::(bg|init|test)\(\)|TestWitness\b'

usage() {
    printf 'usage: %s [--self-test]\n' "${BASH_SOURCE[0]}" >&2
    exit 2
}

# Prints every file under the given root that names the namespace.
naming_files() {
    local scan_root="$1"
    shift
    local -a dirs=("$@")
    local -a present=()
    local d
    for d in "${dirs[@]}"; do
        [[ -d "$scan_root/$d" ]] && present+=("$scan_root/$d")
    done
    [[ ${#present[@]} -eq 0 ]] && return 0
    # Strip full-line comments before matching, so prose that merely
    # mentions the namespace does not read as a use.
    grep -rEl --include='*.h' --include='*.hpp' --include='*.cpp' \
        "$PATTERN" "${present[@]}" 2>/dev/null \
        | while read -r f; do
            if grep -Ev '^[[:space:]]*(//|\*|/\*)' "$f" | grep -Eq "$PATTERN"; then
                printf '%s\n' "${f#"$scan_root"/}"
            fi
        done
}

# Prints the allowlisted paths, one per line, comments and prose stripped.
allowed_paths() {
    local file="$1"
    grep -Ev '^[[:space:]]*(#|$)' "$file" | sed -E 's/[[:space:]]*—.*$//' | sed -E 's/[[:space:]]+$//'
}

run_scan() {
    local scan_root="$1"
    local list="$2"
    local rc=0

    local -a naming=()
    mapfile -t naming < <(naming_files "$scan_root" "${SCAN_DIRS[@]}" | sort -u)

    local -a allowed=()
    mapfile -t allowed < <(allowed_paths "$list" | sort -u)

    local f
    for f in "${naming[@]}"; do
        [[ -z "$f" ]] && continue
        if ! printf '%s\n' "${allowed[@]}" | grep -qxF "$f"; then
            printf 'check-ctx-testing-boundary: %s names foundation::effects::testing and is not allowlisted.\n' "$f" >&2
            printf '  That namespace returns minted capabilities with no gate, and a capability is what\n' >&2
            printf '  every ctx-bound mint checks for.  Production code must obtain its context from a\n' >&2
            printf '  real mint.  If this file genuinely carries a self-test, add it to\n' >&2
            printf '  scripts/ctx-testing-boundary-allowlist.txt with a sentence saying why.\n' >&2
            rc=1
        fi
    done

    local a
    for a in "${allowed[@]}"; do
        [[ -z "$a" ]] && continue
        if ! printf '%s\n' "${naming[@]}" | grep -qxF "$a"; then
            printf 'check-ctx-testing-boundary: %s is allowlisted but no longer names the namespace — stale entry, remove it.\n' "$a" >&2
            rc=1
        fi
    done

    return "$rc"
}

self_test() {
    local tmp
    tmp="$(mktemp -d)"
    trap 'rm -rf "$tmp"' RETURN
    mkdir -p "$tmp/include/foundation/effects"

    # The violating arm: a production-shaped header reaching for a
    # capability it was never handed.
    cat >"$tmp/include/foundation/effects/Planted.h" <<'PLANTED'
#pragma once
inline auto forged() noexcept { return ::foundation::effects::testing::init(); }
PLANTED

    # The clean arm: a file that mentions the namespace only in prose.
    # Without this arm the guard could pass by matching nothing at all.
    cat >"$tmp/include/foundation/effects/PlantedClean.h" <<'CLEAN'
#pragma once
// This header explains that effects::testing exists and does not name it
// in code, so the scan must leave it alone.
inline int clean() noexcept { return 0; }
CLEAN

    local list="$tmp/allowlist.txt"
    : >"$list"

    local rc=0

    if run_scan "$tmp" "$list" 2>/dev/null; then
        printf 'check-ctx-testing-boundary --self-test: FAIL — the planted violation was not reported.\n' >&2
        rc=1
    else
        printf 'check-ctx-testing-boundary --self-test: violating arm reported, as expected.\n'
    fi

    # Now admit the violator and confirm the prose-only file still does
    # not trip, and that a satisfied list reports clean.
    printf '%s\n' 'include/foundation/effects/Planted.h  — planted by the self-test' >"$list"
    if run_scan "$tmp" "$list" 2>/dev/null; then
        printf 'check-ctx-testing-boundary --self-test: clean arm reported clean, as expected.\n'
    else
        printf 'check-ctx-testing-boundary --self-test: FAIL — the prose-only file was reported, or an allowlisted file read as stale.\n' >&2
        rc=1
    fi

    # And a stale entry must fail, or the list would rot silently.
    printf '%s\n' 'include/foundation/effects/Planted.h  — planted' \
                  'include/foundation/effects/Absent.h  — never existed' >"$list"
    if run_scan "$tmp" "$list" 2>/dev/null; then
        printf 'check-ctx-testing-boundary --self-test: FAIL — a stale entry was not reported.\n' >&2
        rc=1
    else
        printf 'check-ctx-testing-boundary --self-test: stale entry reported, as expected.\n'
    fi

    return "$rc"
}

case "${1-}" in
    --self-test) self_test ;;
    "") run_scan "$root" "$allowlist" ;;
    *) usage ;;
esac
