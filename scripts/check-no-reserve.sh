#!/usr/bin/env bash
# check-no-reserve.sh — std::vector::reserve ban enforcement (FIXY-U-097).
#
# CLAUDE.md §IV opt-out matrix: `std::vector::reserve` is banned
# project-wide because:
#
#   (1) Tail-latency: every growth event is O(n) (copies every
#       existing element to the new buffer); reserve only delays
#       the first O(n) spike, doesn't eliminate it.
#   (2) Silent perf cliff: move-during-growth requires noexcept
#       move ctors or falls back to COPY without warning.
#   (3) Heap churn: every growth allocates new + frees old,
#       fragments the allocator.
#
# Production replacements (per §IV opt-out matrix):
#   - Known max         → std::inplace_vector<T, N>
#   - Known exact size  → vector<T>(N) and fill by index
#   - Unbounded growth  → arena-backed storage (hot path) or
#                         accept amortization (rare, cold path)
#
# This guard is intentionally aggressive: it flags ALL `.reserve(`
# and `->reserve(` calls in include/crucible/ (excluding comments).
# False positives (e.g. third-party-style class methods named
# `reserve`) go in the allowlist with a tracked-migration TODO.
#
# Per-line allowlist: scripts/no-reserve-allowlist.txt.  Each line
# is `path:line` (relative to project root).  Comment lines start
# with `#`.  Empty lines are ignored.
#
# Inline suppression: `// NO-RESERVE-OK: <reason>` on the call line
# exempts that single line.
#
# Exempt directories: test/, bench/, examples/ — fixtures may
# deliberately plant the pattern to demonstrate rejection.
#
# Exit status:
#   0 — clean (no NEW reserve sites beyond the allowlist)
#   1 — at least one violation
#   2 — bad invocation / missing dependency

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-no-reserve.sh — std::vector::reserve ban enforcement.

Usage:
  check-no-reserve.sh              # scan; exit 1 on violation
  check-no-reserve.sh --self-test  # plant a violation, verify catch
  check-no-reserve.sh -h | --help  # usage

Suppression:
  // NO-RESERVE-OK: <reason>             on call line — exempts THIS line
  scripts/no-reserve-allowlist.txt:
    path:line                            — exempts the call at that line
    # comment                            — ignored
USAGE
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Plant ONE violation in a synthetic file under a temp root.
        # The scanner must flag it; otherwise the regex is broken.
        # Also plant ONE allowlisted call to verify the allowlist
        # mechanism (otherwise a hostile entry could mask everything).
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/crucible/planted" "$tmp_root/scripts"
        cat >"$tmp_root/include/crucible/planted/planted_violation.h" <<'PLANTED'
#pragma once
// Synthetic no-reserve fixture for --self-test.
#include <vector>
namespace crucible::planted {
inline void planted_drift() {
    std::vector<int> v;
    v.reserve(64);                    // FLAGGED — must be caught
}
inline void planted_allowlisted() {
    std::vector<int> v;
    v.reserve(64);                    // ALLOWLISTED — must NOT be caught
}
inline void planted_suppressed() {
    std::vector<int> v;
    v.reserve(64);  // NO-RESERVE-OK: synthetic --self-test inline marker
}
inline void planted_in_comment() {
    // someobj.reserve(64);           // commented out — must NOT be caught
}
}  // namespace crucible::planted
PLANTED
        # Allowlist entry targets the SECOND .reserve() call site
        # (line 11 of planted_violation.h — count manually for
        # determinism in --self-test).
        cat >"$tmp_root/scripts/no-reserve-allowlist.txt" <<'ALLOW'
# self-test grandfathered entry
include/crucible/planted/planted_violation.h:11
ALLOW
        result_file="$(mktemp)"
        if CRUCIBLE_NO_RESERVE_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file"; then
            printf 'check-no-reserve: SELF-TEST FAILED — planted violation not caught.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The planted_drift line (line 7) must be flagged.
        if ! grep -qF 'planted_violation.h:7' "$result_file"; then
            printf 'check-no-reserve: SELF-TEST FAILED — expected diagnostic for line 7 missing.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The allowlisted line (line 11) must NOT be flagged.
        if grep -qF 'planted_violation.h:11' "$result_file"; then
            printf 'check-no-reserve: SELF-TEST FAILED — allowlist entry leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The inline-suppressed line (line 15) must NOT be flagged.
        if grep -qF 'planted_violation.h:15' "$result_file"; then
            printf 'check-no-reserve: SELF-TEST FAILED — inline NO-RESERVE-OK marker leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The commented-out call (line 18) must NOT be flagged.
        if grep -qF 'planted_violation.h:18' "$result_file"; then
            printf 'check-no-reserve: SELF-TEST FAILED — comment line leaked through filter.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        rm -f "$result_file"
        printf 'check-no-reserve: self-test passed — drift caught, allowlist + inline marker + comment-filter all honoured.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-no-reserve: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Scan-root override for --self-test recursion ─────────────────────
scan_root="${CRUCIBLE_NO_RESERVE_TEST_ROOT:-$root}"
allowlist="$scan_root/scripts/no-reserve-allowlist.txt"

if ! command -v rg >/dev/null 2>&1; then
    printf 'check-no-reserve: ripgrep (rg) is required\n' >&2
    exit 2
fi

# ── Pattern ───────────────────────────────────────────────────────────
# `[obj].reserve(` and `[obj]->reserve(` are the common call shapes.
# Word-boundary discipline: `\.reserve\s*\(` requires an immediate
# `(` after `reserve` so identifiers like `.reservedSlots(` are
# unaffected.  `->reserve\s*\(` mirrors the pointer-member case.
candidate_pattern='\.reserve\s*\(|->reserve\s*\('

# ── Allowlist lookup ─────────────────────────────────────────────────
allowlisted() {
    local rel="$1" line="$2"
    [[ -f "$allowlist" ]] || return 1
    # Skip blank/comment lines; match `path:line` exactly.
    grep -E -v '^[[:space:]]*(#|$)' "$allowlist" | \
        grep -Fxq -- "$rel:$line"
}

violation_count=0
scan_dirs=("$scan_root/include")

while IFS= read -r match; do
    file="${match%%:*}"
    rest="${match#*:}"
    line="${rest%%:*}"
    text="${rest#*:}"

    stripped="${text#"${text%%[![:space:]]*}"}"

    # Skip comment lines outright.  The candidate may appear inside
    # `//`, `///`, `*`, or `/*` lines (e.g. doc-block prose).
    case "$stripped" in
        '//'*|'///'*|'*'*|'/*'*) continue ;;
    esac

    # Inline suppression — `// NO-RESERVE-OK: <reason>` on the same line.
    case "$text" in
        *'NO-RESERVE-OK'*) continue ;;
    esac

    rel="${file#"$scan_root"/}"

    if allowlisted "$rel" "$line"; then
        continue
    fi

    printf 'NO-RESERVE violation: %s:%s — std::vector::reserve banned (CLAUDE.md §IV).\n' \
        "$rel" "$line" >&2
    violation_count=$((violation_count + 1))
done < <(
    rg -nP \
       --no-heading \
       --type=cpp \
       --glob '!build/**' \
       --glob '!cmake-build-*/**' \
       --glob '!third_party/**' \
       --glob '!external/**' \
       --glob '!vendor/**' \
       --glob '!test/**' \
       --glob '!bench/**' \
       --glob '!examples/**' \
       "$candidate_pattern" "${scan_dirs[@]}" 2>/dev/null || true
)

# ── Outcome ──────────────────────────────────────────────────────────
if [[ "$violation_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-no-reserve detected ${violation_count} new std::vector::reserve
site(s) outside the allowlist.  CLAUDE.md §IV opt-out matrix bans
\`std::vector::reserve\` project-wide.

Remediations, in order of preference:

  (1) Replace with std::inplace_vector<T, N> when N is known at
      compile time (zero heap, true O(1) push_back).

  (2) If the exact size is known at construction, write
      \`std::vector<T> v(N)\` and fill by index — one allocation,
      no growth.

  (3) Annotate the line with '// NO-RESERVE-OK: <reason>' for
      structurally-justified exceptions (e.g. third-party method
      named reserve that semantically isn't vector::reserve).

  (4) Add 'path:line' to scripts/no-reserve-allowlist.txt for
      grandfathered code awaiting a tracked migration — every
      entry is a TODO referencing its fixy-A5-* migration ticket.
HINT
    exit 1
fi

printf 'check-no-reserve: clean — no new std::vector::reserve sites.\n' >&2
exit 0
