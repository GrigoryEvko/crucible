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
#       `[[deprecated("CRUCIBLE_STUB: <reason>")]]` on
#       its declaration so callers ALSO see a -Wdeprecated-declarations
#       warning at COMPILE TIME — not only at runtime sentinel.
#
#   (3) Authorized callers (test fixtures, .cpp impls that forward to
#       member stubs) suppress the warning with
#       `#pragma GCC diagnostic push/ignored "-Wdeprecated-declarations"/pop`.
#
# Pair contract enforced by this guard, in both directions:
#
#       file ships `*_implemented = false` ⇔
#       same file ships at least one `[[deprecated("CRUCIBLE_STUB:` attribute
#
# A header that ships an honesty marker WITHOUT a deprecated attribute
# is a "silent stub" — surface looks live, returns sentinel at runtime,
# but production callers cannot grep / cannot see at compile time that
# they are touching a stub.  A header that ships the deprecation without
# the marker gives code no constant to test, so a test cannot pin the
# stub and a caller cannot branch on it.  The pair invariant is what
# makes both layers (runtime sentinel + compile-time visibility)
# discoverable.
#
# The scan covers every tree under include/, because a stub moves with
# its header when the header moves to a new tree.
#
# Exit status:
#   0 — clean (every honesty marker paired with at least one CRUCIBLE_STUB
#       deprecation in the same header, and every such deprecation with a
#       marker)
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

Pair invariant, in both directions, over every tree under include/:
  Every header that declares
    inline constexpr bool *_implemented = false;
  or
    inline constexpr bool *_attached    = false;
  MUST also ship at least one
    [[deprecated("CRUCIBLE_STUB:...
  attribute in the same header, and every header with such an attribute
  MUST declare such a marker.
USAGE
}

readonly MARKER_RE='inline[[:space:]]+constexpr[[:space:]]+bool[[:space:]]+[a-zA-Z_][a-zA-Z0-9_]*_(implemented|attached|ready)[[:space:]]*=[[:space:]]*false'
readonly DEPRECATION_FIXED='deprecated("CRUCIBLE_STUB:'

scan() {
    local rc=0
    local marked deprecated
    marked=$(grep -rln -E "$MARKER_RE" "$root/include/" 2>/dev/null || true)
    deprecated=$(grep -rlnF "$DEPRECATION_FIXED" "$root/include/" 2>/dev/null || true)

    if [[ -z "$marked" && -z "$deprecated" ]]; then
        printf '%s: no honesty markers found — nothing to enforce\n' \
            "check-stub-discipline.sh" >&2
        return 0
    fi

    local hdr line
    while IFS= read -r hdr; do
        [[ -z "$hdr" ]] && continue
        if ! grep -qF "$DEPRECATION_FIXED" "$hdr"; then
            line=$(grep -n -E "$MARKER_RE" "$hdr" | head -1 || true)
            printf '%s:%s: pair-invariant violation — honesty marker without [[deprecated("CRUCIBLE_STUB:...")]] attribute on any function in this header\n' \
                "${hdr#"$root"/}" "${line%%:*}" >&2
            rc=1
        fi
    done <<< "$marked"

    while IFS= read -r hdr; do
        [[ -z "$hdr" ]] && continue
        if ! grep -qE "$MARKER_RE" "$hdr"; then
            line=$(grep -nF "$DEPRECATION_FIXED" "$hdr" | head -1 || true)
            printf '%s:%s: pair-invariant violation — [[deprecated("CRUCIBLE_STUB:...")]] attribute without an honesty marker (inline constexpr bool *_implemented = false) in this header\n' \
                "${hdr#"$root"/}" "${line%%:*}" >&2
            rc=1
        fi
    done <<< "$deprecated"

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
    rm -f "$tmp/include/crucible/test/Stub.h"

    # Arm four, the reverse violation: a CRUCIBLE_STUB deprecation with no
    # honesty marker, so code has no constant to test the stub by.
    cat > "$tmp/include/crucible/test/Unmarked.h" <<'EOF'
#pragma once
namespace crucible::test_stub {
[[deprecated("CRUCIBLE_STUB: the data plane is not wired yet")]]
void connect_unmarked() noexcept;
}
EOF
    rc=0; scan >/dev/null 2>"$out" || rc=$?
    if [[ "$rc" -ne 1 ]]; then
        fail "planted deprecation-without-marker reported $rc, want 1"
        return 1
    fi
    grep -qF 'include/crucible/test/Unmarked.h' "$out" || { fail "the unmarked header was not named"; return 1; }
    violation_count=$(grep -c 'pair-invariant violation' "$out" || true)
    if [[ "$violation_count" -ne 1 ]]; then
        fail "expected exactly 1 violation for the unmarked header, got $violation_count"
        return 1
    fi
    rm -f "$tmp/include/crucible/test/Unmarked.h"

    # Arm five, a tree other than include/crucible/.  A paired header there
    # passes, and an unpaired one is caught, so the scan reaches a header
    # that moved to a new tree.
    mkdir -p "$tmp/include/fixy"
    cat > "$tmp/include/fixy/PairedMoved.h" <<'EOF'
#pragma once
namespace fixy::test_stub {
inline constexpr bool backend_v2_attached = false;
[[deprecated("CRUCIBLE_STUB: the backend is not wired yet")]]
void attach_paired() noexcept;
}
EOF
    rc=0; scan >/dev/null 2>"$out" || rc=$?
    if [[ "$rc" -ne 0 ]]; then
        fail "a paired header under include/fixy/ reported $rc, want 0"
        return 1
    fi
    cat > "$tmp/include/fixy/Moved.h" <<'EOF'
#pragma once
namespace fixy::test_stub {
inline constexpr bool backend_implemented = false;
void attach_moved() noexcept;
}
EOF
    rc=0; scan >/dev/null 2>"$out" || rc=$?
    if [[ "$rc" -ne 1 ]]; then
        fail "an unpaired marker under include/fixy/ reported $rc, want 1"
        return 1
    fi
    grep -qF 'include/fixy/Moved.h' "$out" || { fail "the unpaired header under include/fixy/ was not named"; return 1; }
    if grep -qF 'include/fixy/PairedMoved.h' "$out"; then
        fail "a paired header under include/fixy/ was flagged"
        return 1
    fi

    root="$saved_root"
    printf 'check-stub-discipline: self-test passed — paired markers and a header without one pass, an unpaired marker and an unmarked deprecation are each caught, in include/crucible/ and in include/fixy/.\n' >&2
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test) self_test; exit $? ;;
    "") scan; exit $? ;;
    *) printf 'unknown argument: %s\n' "$1" >&2; usage; exit 2 ;;
esac
