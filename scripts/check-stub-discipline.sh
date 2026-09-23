#!/usr/bin/env bash
# check-stub-discipline.sh — stub-vs-live deprecation discipline.
#
# Pairs every `*_implemented = false` / `*_attached = false` honesty marker
# in include/crucible/ with a matching [[deprecated("CRUCIBLE_STUB:`
# attribute IN THE SAME HEADER.  The discipline:
#
#   (1) An honesty marker `inline constexpr bool data_plane_implemented = false;`
#       is a compile-time statement of "this surface does not yet ship live
#       behavior; callers will observe sentinel return codes
#       (BackendUnavailable / Deferred / Unavailable) at runtime."
#
#   (2) Every stub entrypoint in that header must additionally carry
#       `[[deprecated("CRUCIBLE_STUB: <reason>; see fixy-A5-XXX")]]` on
#       its declaration so callers ALSO see a -Wdeprecated-declarations
#       warning at COMPILE TIME — not only at runtime sentinel.
#
#   (3) Authorized callers (test fixtures, .cpp impls that forward to
#       member stubs) suppress the warning with
#       `#pragma GCC diagnostic push/ignored "-Wdeprecated-declarations"/pop`.
#
# Pair contract enforced by this guard:
#
#       file ships `*_implemented = false` ⇒
#       same file ships at least one `[[deprecated("CRUCIBLE_STUB:` attribute
#
# A header that ships an honesty marker WITHOUT a deprecated attribute
# is a "silent stub" — surface looks live, returns sentinel at runtime,
# but production callers cannot grep / cannot see at compile time that
# they are touching a stub.  The pair invariant is what makes both
# layers (runtime sentinel + compile-time visibility) discoverable.
#
# Exit status:
#   0 — clean (every honesty marker paired with at least one CRUCIBLE_STUB
#       deprecation in the same header)
#   1 — at least one pair-invariant violation
#   2 — bad invocation / missing dependency

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-stub-discipline.sh — stub-vs-live deprecation discipline.

Usage:
  check-stub-discipline.sh              # scan; exit 1 on violation
  check-stub-discipline.sh --self-test  # plant a violation, verify catch
  check-stub-discipline.sh -h | --help  # usage

Pair invariant:
  Every header that declares
    inline constexpr bool *_implemented = false;
  or
    inline constexpr bool *_attached    = false;
  MUST also ship at least one
    [[deprecated("CRUCIBLE_STUB:...
  attribute in the same header.
USAGE
}

scan() {
    local rc=0
    local hdrs
    hdrs=$(grep -rln -E 'inline[[:space:]]+constexpr[[:space:]]+bool[[:space:]]+[a-zA-Z_]+_(implemented|attached|ready)[[:space:]]*=[[:space:]]*false' \
        "$root/include/crucible/" 2>/dev/null || true)

    if [[ -z "$hdrs" ]]; then
        printf '%s: no honesty markers found — nothing to enforce\n' \
            "check-stub-discipline.sh" >&2
        return 0
    fi

    while IFS= read -r hdr; do
        [[ -z "$hdr" ]] && continue
        # The honesty-marker locator is the first matching line; report it.
        local marker_line
        marker_line=$(grep -n -E 'inline[[:space:]]+constexpr[[:space:]]+bool[[:space:]]+[a-zA-Z_]+_(implemented|attached|ready)[[:space:]]*=[[:space:]]*false' \
            "$hdr" | head -1 || true)

        # Skip blank-marker false matches (defensive).
        [[ -z "$marker_line" ]] && continue

        # Look for ANY paired CRUCIBLE_STUB deprecation in the same header.
        if ! grep -q 'deprecated("CRUCIBLE_STUB:' "$hdr"; then
            local rel="${hdr#$root/}"
            printf '%s:%s: pair-invariant violation — honesty marker without [[deprecated("CRUCIBLE_STUB:...")]] attribute on any function in this header\n' \
                "$rel" "${marker_line%%:*}" >&2
            rc=1
        fi
    done <<< "$hdrs"

    if [[ $rc -eq 0 ]]; then
        printf 'check-stub-discipline.sh: PASS (all honesty markers paired)\n'
    fi
    return $rc
}

self_test() {
    local tmp out rc saved_root violation_count
    tmp=$(mktemp -d)
    out=$(mktemp)
    trap "rm -rf '$tmp'; rm -f '$out'" EXIT

    mkdir -p "$tmp/include/crucible/test"

    # The wrapper inverts roles: temporarily point $root at the tmp tree.
    saved_root="$root"
    root="$tmp"

    fail() {
        printf 'check-stub-discipline: SELF-TEST FAILED — %s\n' "$1" >&2
        printf '── scanner stderr ───\n%s\n────────────────────\n' "$(cat "$out")" >&2
        root="$saved_root"
        return 1
    }

    # Arm one, the paired control.  The honesty marker sits beside a
    # CRUCIBLE_STUB deprecation, which is the shape the discipline asks
    # for, so the scan must stay silent.  Without this arm the
    # suppression branch is never shown to suppress, and a guard that
    # flagged every marker would pass its own self-test.
    cat > "$tmp/include/crucible/test/Paired.h" <<'EOF'
#pragma once
namespace crucible::test_stub {
inline constexpr bool data_plane_implemented = false;
[[deprecated("CRUCIBLE_STUB: the data plane is not wired yet")]]
void connect_paired() noexcept;
}
EOF
    rc=0; scan >/dev/null 2>"$out" || rc=$?
    if [[ "$rc" -ne 0 ]]; then
        fail "a marker paired with a CRUCIBLE_STUB deprecation reported $rc, want 0"
        return 1
    fi

    # Arm two, a header with no honesty marker at all.  Nothing to pair,
    # so nothing to report.
    cat > "$tmp/include/crucible/test/NoMarker.h" <<'EOF'
#pragma once
namespace crucible::test_stub {
inline constexpr bool unrelated_flag = true;
void ordinary() noexcept;
}
EOF
    rc=0; scan >/dev/null 2>"$out" || rc=$?
    if [[ "$rc" -ne 0 ]]; then
        fail "a header without an honesty marker reported $rc, want 0"
        return 1
    fi

    # Arm three, the violation: a marker with no deprecation anywhere in
    # the header.  Exactly one header is unpaired, so the count pins it.
    cat > "$tmp/include/crucible/test/Stub.h" <<'EOF'
#pragma once
namespace crucible::test_stub {
inline constexpr bool data_plane_implemented = false;
void connect_stub() noexcept;
}
EOF
    rc=0; scan >/dev/null 2>"$out" || rc=$?
    if [[ "$rc" -ne 1 ]]; then
        fail "planted stub-without-deprecation reported $rc, want 1"
        return 1
    fi
    grep -qF 'include/crucible/test/Stub.h' "$out" || { fail "the unpaired header was not named"; return 1; }
    if grep -qF 'include/crucible/test/Paired.h' "$out"; then
        fail "a paired header was flagged"
        return 1
    fi
    violation_count=$(grep -c 'pair-invariant violation' "$out" || true)
    if [[ "$violation_count" -ne 1 ]]; then
        fail "expected exactly 1 violation, got $violation_count"
        return 1
    fi

    root="$saved_root"
    printf 'check-stub-discipline: self-test passed — a paired marker and a header without one pass, exactly one unpaired marker is caught.\n' >&2
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test) self_test; exit $? ;;
    "") scan; exit $? ;;
    *) printf 'unknown argument: %s\n' "$1" >&2; usage; exit 2 ;;
esac
