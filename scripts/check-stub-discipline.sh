#!/usr/bin/env bash
# check-stub-discipline.sh — stub-vs-live deprecation discipline (FIXY-U-087).
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
    local tmp
    tmp=$(mktemp -d)
    trap "rm -rf '$tmp'" EXIT

    mkdir -p "$tmp/include/crucible/test"
    cat > "$tmp/include/crucible/test/Stub.h" <<'EOF'
#pragma once
namespace crucible::test_stub {
inline constexpr bool data_plane_implemented = false;
void connect_stub() noexcept;
}
EOF

    # The wrapper inverts roles: temporarily point $root at the tmp tree.
    local saved_root="$root"
    root="$tmp"
    if scan >/dev/null 2>&1; then
        printf 'self-test FAIL: scan missed planted stub-without-deprecation\n' >&2
        root="$saved_root"
        return 1
    fi
    root="$saved_root"

    printf 'check-stub-discipline.sh: self-test PASS\n'
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test) self_test; exit $? ;;
    "") scan; exit $? ;;
    *) printf 'unknown argument: %s\n' "$1" >&2; usage; exit 2 ;;
esac
