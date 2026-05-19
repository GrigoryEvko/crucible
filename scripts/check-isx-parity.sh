#!/usr/bin/env bash
# check-isx-parity.sh — fixy::is:: convention enforcement (FIXY-U-104).
#
# The fixy::is:: namespace (include/crucible/fixy/Is.h) re-exports the
# public surface of every safety/IsX.h header in three tiers:
#   (a) concept alias        — fixy::is::IsX  (template alias)
#   (b) trait re-export      — fixy::is::is_x_v  (using-decl)
#   (c) type-alias helpers   — fixy::is::x_*_t  (using-decl)
#
# fixy-L-05 / fixy-L-06 established the parity invariant: every public
# alias in safety::extract:: must be re-exported through fixy::is::.
# Without enforcement, future IsX.h additions silently drift out of
# parity — adding a new IsX.h compiles cleanly even when fixy::is::
# doesn't surface its aliases, and downstream consumers using the
# fixy umbrella never see the new symbol.
#
# This guard is the FORWARD-LOOKING discipline: walk safety/Is*.h,
# extract every PUBLIC `_v` trait + `_t` alias declared at the
# `safety::extract::` namespace level (not `extract::detail::`), and
# verify each appears in fixy/Is.h with a matching using-decl.
#
# Detail-namespace symbols (extract::detail::*) are excluded by
# design — they're internal probes (e.g., session_base_probe_t) that
# users should NOT see through the fixy:: discipline boundary.
#
# Concept aliases (a) are not checked structurally — concept names
# parse via `concept IsX = ...` in substrate and `concept IsX = ...`
# in fixy::is::; both have the same identifier, so a missing fixy
# concept would surface as a compile error in test_fixy_umbrella
# when downstream code references `fixy::is::IsX`.  The trait + alias
# tiers (b)+(c) are the ones this script audits.
#
# Exit status:
#   0 — parity verified (every public alias re-exported)
#   1 — one or more aliases missing from fixy/Is.h
#   2 — bad invocation / missing dependency

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-isx-parity.sh — fixy::is:: alias parity enforcement.

Usage:
  check-isx-parity.sh              # scan; exit 1 on parity drift
  check-isx-parity.sh --self-test  # plant a drift, verify catch
  check-isx-parity.sh -h | --help  # usage
USAGE
}

# Extract all `_t` and `_v` alias names from safety/Is*.h headers,
# then subtract a known-detail-namespace exclusion set.  This is
# simpler and more robust than awk-based namespace tracking on
# heterogeneous header layouts.
#
# DETAIL_EXCLUSIONS: aliases that live in extract::detail:: (or
# similar inner namespaces) and are NOT part of the public surface.
# Add new exclusions here when a new detail symbol matches the
# alias-shape regex below.
DETAIL_EXCLUSIONS=(
    session_base_probe_t   # extract::detail::, SFINAE probe (IsSessionHandle.h:99)
)

extract_substrate_aliases() {
    # Type aliases: `using NAME =` at column 0 (namespace level only —
    # indented `using` declarations are member-type aliases inside
    # structs/classes and not part of the public surface).
    # Trait variables: `inline constexpr bool NAME` at column 0.
    grep -hE '^using[[:space:]]+[a-z_]+_t[[:space:]]*=|^inline[[:space:]]+constexpr[[:space:]]+bool[[:space:]]+[a-z_]+_v[[:space:]]' \
        "$root"/include/crucible/safety/Is*.h 2>/dev/null \
        | sed -E 's/^using[[:space:]]+([a-z_]+_t)[[:space:]]*=.*/\1/;
                  s/^inline[[:space:]]+constexpr[[:space:]]+bool[[:space:]]+([a-z_]+_v)[[:space:]].*/\1/' \
        | sort -u
}

scan() {
    local sub_aliases fixy_aliases
    sub_aliases=$(mktemp)
    fixy_aliases=$(mktemp)
    trap "rm -f '$sub_aliases' '$fixy_aliases'" RETURN

    extract_substrate_aliases > "$sub_aliases.all"
    # Filter out known-detail aliases.
    local excl_pattern
    excl_pattern=$(printf '^%s$\n' "${DETAIL_EXCLUSIONS[@]}")
    grep -vE "$(echo "$excl_pattern" | paste -sd '|' -)" "$sub_aliases.all" \
        | sort -u > "$sub_aliases" || true

    # Match `using ::crucible::safety::extract::NAME;` in fixy/Is.h.
    grep -E '^using ::crucible::safety::extract::[a-z_]+_[tv];' \
        "$root/include/crucible/fixy/Is.h" \
        | sed -E 's|^using ::crucible::safety::extract::([a-z_]+_[tv]);.*|\1|' \
        | sort -u > "$fixy_aliases"

    local missing
    missing=$(comm -23 "$sub_aliases" "$fixy_aliases" || true)

    if [[ -n "$missing" ]]; then
        printf 'check-isx-parity.sh: FAIL — public safety::extract:: aliases NOT re-exported in fixy/Is.h:\n' >&2
        printf '%s\n' "$missing" | sed 's/^/  /' >&2
        printf '\nAdd `using ::crucible::safety::extract::<name>;` rows to fixy/Is.h for each.\n' >&2
        return 1
    fi

    local nsub nfixy
    nsub=$(wc -l < "$sub_aliases")
    nfixy=$(wc -l < "$fixy_aliases")
    printf 'check-isx-parity.sh: PASS — %d public aliases mirrored (sub=%d, fixy=%d).\n' "$nsub" "$nsub" "$nfixy"
    return 0
}

self_test() {
    local tmp
    tmp=$(mktemp -d)
    trap "rm -rf '$tmp'" EXIT

    mkdir -p "$tmp/include/crucible/safety" "$tmp/include/crucible/fixy"
    cat > "$tmp/include/crucible/safety/IsTest.h" <<'EOF'
#pragma once
namespace crucible::safety::extract {
template <typename T>
inline constexpr bool is_test_drift_v = true;
template <typename T>
using test_drift_value_t = int;
}
EOF
    cat > "$tmp/include/crucible/fixy/Is.h" <<'EOF'
#pragma once
#include <crucible/safety/IsTest.h>
namespace crucible::fixy::is {
// Deliberately MISSING: using ::crucible::safety::extract::is_test_drift_v;
// Deliberately MISSING: using ::crucible::safety::extract::test_drift_value_t;
}
EOF

    local saved_root="$root"
    root="$tmp"
    if scan >/dev/null 2>&1; then
        printf 'self-test FAIL: scan missed planted parity drift\n' >&2
        root="$saved_root"
        return 1
    fi
    root="$saved_root"

    printf 'check-isx-parity.sh: self-test PASS\n'
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test) self_test; exit $? ;;
    "") scan; exit $? ;;
    *) printf 'unknown argument: %s\n' "$1" >&2; usage; exit 2 ;;
esac
