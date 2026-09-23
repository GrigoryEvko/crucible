#!/usr/bin/env bash
# check-isx-parity.sh — fixy::is:: convention enforcement.
#
# The fixy::is:: namespace (include/crucible/fixy/Is.h) re-exports the
# public surface of every safety/IsX.h header in three tiers:
#   (a) concept alias        — fixy::is::IsX  (template alias)
#   (b) trait re-export      — fixy::is::is_x_v  (using-decl)
#   (c) type-alias helpers   — fixy::is::x_*_t  (using-decl)
#
# The parity invariant: every public
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
# Why this stays a text scan and not a reflection walk
# ----------------------------------------------------
# The obvious C++26 rewrite is a `std::meta::members_of` walk over
# `^^crucible::safety::extract` and `^^crucible::fixy::is`, comparing
# the two identifier sets.  It cannot work, for one reason that lands
# on both sides of the comparison: members_of does not report names
# introduced by a using-declaration.
#
#   * The whole fixy side IS using-declarations.  A walk over
#     `^^crucible::fixy::is` reports only the locally declared concept
#     aliases and none of the 97 `using ::crucible::safety::extract::X;`
#     rows this guard exists to audit.
#   * The substrate side uses them too.  `extract::is_owned_mmap_v` is
#     a using-declaration over `safety::is_owned_mmap_v`
#     (safety/IsOwnedMmap.h), so a walk over `^^crucible::safety::extract`
#     drops it from the demand set.
#
# So a reflection walk would trade this scan's blind spots for a
# different, quieter set.  Reach through `fixy::is::` is instead
# asserted in-language, per symbol, in test/test_fixy_umbrella_reach.cpp.
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
    # The glob is `*Is*.h`, not `Is*.h`.  A header that has been ported
    # to the new tree is marked superseded by a leading underscore
    # (`IsRefined.h` becomes `_IsRefined.h`), and it keeps shipping its
    # aliases through fixy/Is.h until the old substrate is deleted.  A
    # `Is*.h` glob stops matching the moment a header is marked, which
    # silently drops that header's aliases out of the demand set — the
    # guard keeps passing while it audits less and less.  When this was
    # found, 35 of 96 aliases (36% of the surface) had fallen out that
    # way.  The --self-test plants a superseded header for this reason.
    #
    # Type aliases: `using NAME =` at column 0 (namespace level only —
    # indented `using` declarations are member-type aliases inside
    # structs/classes and not part of the public surface).
    # Trait variables: `inline constexpr bool NAME` at column 0.
    grep -hE '^using[[:space:]]+[a-z_]+_t[[:space:]]*=|^inline[[:space:]]+constexpr[[:space:]]+bool[[:space:]]+[a-z_]+_v[[:space:]]' \
        "$root"/include/crucible/safety/*Is*.h 2>/dev/null \
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
    local tmp out rc saved_root missing_count
    tmp=$(mktemp -d)
    out=$(mktemp)
    trap "rm -rf '$tmp'; rm -f '$out'" EXIT

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

    saved_root="$root"
    root="$tmp"

    fail() {
        printf 'check-isx-parity: SELF-TEST FAILED — %s\n' "$1" >&2
        printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$out")" >&2
        root="$saved_root"
        return 1
    }

    # Arm one, the clean control.  Both aliases are re-exported, so a
    # guard that has degenerated into always-fire reports a violation
    # here and the arm catches it.  Without this arm the self-test
    # cannot tell a working guard from one that flags everything.
    cat > "$tmp/include/crucible/fixy/Is.h" <<'EOF'
#pragma once
#include <crucible/safety/IsTest.h>
namespace crucible::fixy::is {
using ::crucible::safety::extract::is_test_drift_v;
using ::crucible::safety::extract::test_drift_value_t;
}
EOF
    rc=0; scan >/dev/null 2>"$out" || rc=$?
    if [[ "$rc" -ne 0 ]]; then
        fail "a tree whose aliases are all re-exported reported $rc, want 0"
        return 1
    fi

    # Arm two, the drift.  Neither alias is re-exported, so both must
    # appear in the report and the count must be exactly two.  The count
    # is what stops the arm from passing on a guard that reports one
    # name and drops the other.
    cat > "$tmp/include/crucible/fixy/Is.h" <<'EOF'
#pragma once
#include <crucible/safety/IsTest.h>
namespace crucible::fixy::is {
// Deliberately MISSING: using ::crucible::safety::extract::is_test_drift_v;
// Deliberately MISSING: using ::crucible::safety::extract::test_drift_value_t;
}
EOF
    rc=0; scan >/dev/null 2>"$out" || rc=$?
    if [[ "$rc" -ne 1 ]]; then
        fail "planted parity drift reported $rc, want 1"
        return 1
    fi
    grep -qE '^  is_test_drift_v$' "$out" || { fail "the missing trait was not named"; return 1; }
    grep -qE '^  test_drift_value_t$' "$out" || { fail "the missing type alias was not named"; return 1; }
    missing_count=$(grep -cE '^  [a-z_]+_[tv]$' "$out" || true)
    if [[ "$missing_count" -ne 2 ]]; then
        fail "expected exactly 2 missing aliases, got $missing_count"
        return 1
    fi

    # Arm three, the detail exclusion.  A detail-namespace probe matches
    # the alias-shape regex but is not public surface, so it must not be
    # demanded of fixy/Is.h.
    cat > "$tmp/include/crucible/safety/IsProbe.h" <<'EOF'
#pragma once
namespace crucible::safety::extract::detail {
template <typename T>
using session_base_probe_t = int;
}
EOF
    cat > "$tmp/include/crucible/fixy/Is.h" <<'EOF'
#pragma once
#include <crucible/safety/IsTest.h>
namespace crucible::fixy::is {
using ::crucible::safety::extract::is_test_drift_v;
using ::crucible::safety::extract::test_drift_value_t;
}
EOF
    rc=0; scan >/dev/null 2>"$out" || rc=$?
    if [[ "$rc" -ne 0 ]]; then
        fail "an excluded detail probe was demanded of fixy/Is.h (got $rc)"
        return 1
    fi

    # Arm four, the superseded header.  A ported header keeps its aliases
    # and gains a leading underscore, so it must stay in the demand set.
    # This arm is what a `Is*.h` glob fails: the alias below simply stops
    # being asked for and the drift goes unreported.
    cat > "$tmp/include/crucible/safety/_IsSuperseded.h" <<'EOF'
#pragma once
namespace crucible::safety::extract {
template <typename T>
inline constexpr bool is_superseded_drift_v = true;
}
EOF
    rc=0; scan >/dev/null 2>"$out" || rc=$?
    if [[ "$rc" -ne 1 ]]; then
        fail "an alias in a superseded _Is header reported $rc, want 1"
        return 1
    fi
    grep -qE '^  is_superseded_drift_v$' "$out" \
        || { fail "the alias of the superseded header was not demanded"; return 1; }
    missing_count=$(grep -cE '^  [a-z_]+_[tv]$' "$out" || true)
    if [[ "$missing_count" -ne 1 ]]; then
        fail "expected exactly 1 missing alias, got $missing_count"
        return 1
    fi

    root="$saved_root"
    printf 'check-isx-parity: self-test passed — a mirrored tree passes, planted drift names both aliases and exits 1, a detail probe is excluded, a superseded _Is header is still demanded.\n' >&2
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test) self_test; exit $? ;;
    "") scan; exit $? ;;
    *) printf 'unknown argument: %s\n' "$1" >&2; usage; exit 2 ;;
esac
