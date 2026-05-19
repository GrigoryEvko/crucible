#!/usr/bin/env bash
# check-no-reinterpret-cast.sh — reinterpret_cast ban enforcement (FIXY-U-105).
#
# CLAUDE.md §III opt-out matrix: `reinterpret_cast` is banned
# project-wide because it bypasses TBAA, hides strict-aliasing
# violations, and is almost always semantically wrong on modern C++.
#
# Production replacements (per §III + §IV):
#   - Bit-equivalent reinterpretation → `std::bit_cast<T>(v)`
#     (C++20, requires same-size trivially-copyable, compile-time
#     when constexpr).
#   - Arena/storage type punning → `std::start_lifetime_as<T>(ptr)`
#     (C++23, begins the implicit object lifetime properly).
#   - SIMD intrinsic interop → `<simd>` first-class conversions:
#     `basic_vec` has implicit ctor + operator __m256i / __m512i
#     (CLAUDE.md §IV) — no bit_cast at all.
#   - Pointer-to-integer for arithmetic → `std::bit_cast<uintptr_t>(ptr)`
#     is allowed; many production paths still use `reinterpret_cast`
#     for this because it's the documented ABI form, hence allowlist.
#
# This guard is intentionally aggressive: it flags ALL `reinterpret_cast<`
# token occurrences in include/crucible/ (excluding comments).
# False positives go in the allowlist with a tracked-migration TODO.
#
# Per-line allowlist: scripts/no-reinterpret-allowlist.txt.  Each line
# is `path:line` (relative to project root).  Comment lines start
# with `#`.  Empty lines are ignored.
#
# Inline suppression: `// NO-REINTERPRET-OK: <reason>` on the call line
# exempts that single line.
#
# Exempt directories: test/, bench/, examples/ — fixtures may
# deliberately plant the pattern to demonstrate rejection.
#
# Exit status:
#   0 — clean (no NEW reinterpret_cast sites beyond the allowlist)
#   1 — at least one violation outside the allowlist
#   2 — bad invocation / missing dependency / stale allowlist entry
#       (an allowlist entry that no longer matches a real call site
#       means the substrate migration consumed it; the entry must
#       be removed so the guard catches future drift at that line).

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-no-reinterpret-cast.sh — reinterpret_cast ban enforcement.

Usage:
  check-no-reinterpret-cast.sh              # scan; exit 1 on violation, 2 on stale
  check-no-reinterpret-cast.sh --self-test  # plant a violation, verify catch
  check-no-reinterpret-cast.sh -h | --help  # usage

Suppression:
  // NO-REINTERPRET-OK: <reason>            on call line — exempts THIS line
  scripts/no-reinterpret-allowlist.txt:
    path:line                               — exempts the call at that line
    # comment                               — ignored
USAGE
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Plant ONE violation in a synthetic file under a temp root.
        # The scanner must flag it; otherwise the regex is broken.
        # Also plant ONE allowlisted call, ONE inline-suppressed call,
        # and ONE comment-line call so all four axes are exercised.
        # Finally plant ONE STALE allowlist entry pointing at a line
        # that has no reinterpret_cast — must trigger exit 2.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/crucible/planted" "$tmp_root/scripts"
        cat >"$tmp_root/include/crucible/planted/planted_reinterpret.h" <<'PLANTED'
#pragma once
// Synthetic no-reinterpret fixture for --self-test.
#include <cstddef>
namespace crucible::planted {
inline std::uintptr_t planted_drift(void* p) {
    return reinterpret_cast<std::uintptr_t>(p);   // FLAGGED — line 6
}
inline std::uintptr_t planted_allowlisted(void* p) {
    return reinterpret_cast<std::uintptr_t>(p);   // ALLOWLISTED — line 9
}
inline std::uintptr_t planted_suppressed(void* p) {
    return reinterpret_cast<std::uintptr_t>(p);  // NO-REINTERPRET-OK: synthetic
}
inline void planted_in_comment() {
    // someobj = reinterpret_cast<int*>(0);       // commented out — line 15
}
}  // namespace crucible::planted
PLANTED
        # Allowlist entry targets the SECOND reinterpret_cast call site
        # (line 9 of planted_reinterpret.h — count manually for
        # determinism in --self-test).  Also plant a STALE entry to
        # verify stale detection.
        cat >"$tmp_root/scripts/no-reinterpret-allowlist.txt" <<'ALLOW'
# self-test grandfathered entry
include/crucible/planted/planted_reinterpret.h:9
# self-test stale entry (no reinterpret_cast at this line)
include/crucible/planted/planted_reinterpret.h:99
ALLOW
        result_file="$(mktemp)"
        rc=0
        CRUCIBLE_NO_REINTERPRET_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file" || rc=$?
        # First sub-test: with the stale entry present we expect rc=1
        # (the line-7 drift is detected first; violation reporting
        # takes precedence over stale reporting so CI tells the user
        # the most actionable thing first).
        if [[ "$rc" -ne 1 ]]; then
            printf 'check-no-reinterpret: SELF-TEST FAILED — expected violation exit 1, got %d.\n' "$rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The planted_drift line (line 6) must be flagged.
        if ! grep -qF 'planted_reinterpret.h:6' "$result_file"; then
            printf 'check-no-reinterpret: SELF-TEST FAILED — expected diagnostic for line 7 missing.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The allowlisted line (line 11) must NOT be flagged.
        if grep -qF 'planted_reinterpret.h:11 — reinterpret' "$result_file"; then
            printf 'check-no-reinterpret: SELF-TEST FAILED — allowlist entry leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The inline-suppressed line (line 15) must NOT be flagged.
        if grep -qF 'planted_reinterpret.h:15 — reinterpret' "$result_file"; then
            printf 'check-no-reinterpret: SELF-TEST FAILED — inline NO-REINTERPRET-OK marker leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The commented-out call (line 18) must NOT be flagged.
        if grep -qF 'planted_reinterpret.h:18 — reinterpret' "$result_file"; then
            printf 'check-no-reinterpret: SELF-TEST FAILED — comment line leaked through filter.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # Second sub-test: remove the line-7 drift so only the stale
        # entry remains; expect exit 2.
        cat >"$tmp_root/include/crucible/planted/planted_reinterpret.h" <<'PLANTED'
#pragma once
// Synthetic no-reinterpret fixture for --self-test (stale-only pass).
#include <cstddef>
namespace crucible::planted {
inline std::uintptr_t planted_allowlisted(void* p) {
    return reinterpret_cast<std::uintptr_t>(p);   // ALLOWLISTED — line 6
}
}  // namespace crucible::planted
PLANTED
        cat >"$tmp_root/scripts/no-reinterpret-allowlist.txt" <<'ALLOW'
# self-test grandfathered entry (still active)
include/crucible/planted/planted_reinterpret.h:6
# self-test stale entry (no reinterpret_cast at this line)
include/crucible/planted/planted_reinterpret.h:99
ALLOW
        rc=0
        CRUCIBLE_NO_REINTERPRET_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file" || rc=$?
        if [[ "$rc" -ne 2 ]]; then
            printf 'check-no-reinterpret: SELF-TEST FAILED — stale-only pass expected exit 2, got %d.\n' "$rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        if ! grep -qF 'STALE allowlist entry' "$result_file"; then
            printf 'check-no-reinterpret: SELF-TEST FAILED — stale diagnostic missing.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        rm -f "$result_file"
        printf 'check-no-reinterpret: self-test passed — drift caught, allowlist + inline marker + comment-filter + stale-detection all honoured.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-no-reinterpret: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Scan-root override for --self-test recursion ─────────────────────
scan_root="${CRUCIBLE_NO_REINTERPRET_TEST_ROOT:-$root}"
allowlist="$scan_root/scripts/no-reinterpret-allowlist.txt"

if ! command -v rg >/dev/null 2>&1; then
    printf 'check-no-reinterpret: ripgrep (rg) is required\n' >&2
    exit 2
fi

# ── Pattern ───────────────────────────────────────────────────────────
# `reinterpret_cast<` is the unambiguous token form.  We do NOT match
# bare `reinterpret_cast` (without `<`) because that's only valid as
# an identifier (the keyword always takes a template argument list).
candidate_pattern='reinterpret_cast<'

# ── Allowlist lookup ─────────────────────────────────────────────────
allowlisted() {
    local rel="$1" line="$2"
    [[ -f "$allowlist" ]] || return 1
    # Skip blank/comment lines; match `path:line` exactly.
    grep -E -v '^[[:space:]]*(#|$)' "$allowlist" | \
        grep -Fxq -- "$rel:$line"
}

# ── Live set of (path:line) for stale-entry detection ────────────────
live_set_file="$(mktemp)"
trap 'rm -f "$live_set_file"' EXIT

violation_count=0
scan_dirs=("$scan_root/include")

while IFS= read -r match; do
    file="${match%%:*}"
    rest="${match#*:}"
    line="${rest%%:*}"
    text="${rest#*:}"

    stripped="${text#"${text%%[![:space:]]*}"}"

    # Skip comment lines outright.
    case "$stripped" in
        '//'*|'///'*|'*'*|'/*'*) continue ;;
    esac

    # Inline suppression — `// NO-REINTERPRET-OK: <reason>` on the same line.
    case "$text" in
        *'NO-REINTERPRET-OK'*) continue ;;
    esac

    rel="${file#"$scan_root"/}"
    printf '%s:%s\n' "$rel" "$line" >> "$live_set_file"

    if allowlisted "$rel" "$line"; then
        continue
    fi

    printf 'NO-REINTERPRET violation: %s:%s — reinterpret_cast banned (CLAUDE.md §III).\n' \
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

# ── Outcome (violations) ─────────────────────────────────────────────
if [[ "$violation_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-no-reinterpret detected ${violation_count} new reinterpret_cast
site(s) outside the allowlist.  CLAUDE.md §III opt-out matrix bans
\`reinterpret_cast\` project-wide.

Remediations, in order of preference:

  (1) Replace with \`std::bit_cast<T>(value)\` (C++20) when the
      reinterpretation is bit-equivalent on trivially-copyable types
      of the same size.  constexpr-friendly.

  (2) Replace with \`std::start_lifetime_as<T>(ptr)\` (C++23) when
      beginning the implicit object lifetime of T inside an arena
      or storage buffer.

  (3) For SIMD intrinsic interop, use \`<simd>\` first-class
      conversions: \`basic_vec\` has implicit ctor + operator
      __m256i / __m512i — no cast needed at all.

  (4) Annotate the line with '// NO-REINTERPRET-OK: <reason>' for
      structurally-justified exceptions documented inline.

  (5) Add 'path:line' to scripts/no-reinterpret-allowlist.txt for
      grandfathered code awaiting a tracked migration (FIXY-U-082 +
      WRAP-* tickets are the active drain plans).
HINT
    exit 1
fi

# ── Outcome (stale allowlist entries) ────────────────────────────────
stale_count=0
if [[ -f "$allowlist" ]]; then
    while IFS= read -r entry; do
        # Skip blank/comment lines.
        case "$entry" in
            ''|'#'*|*[[:space:]]'#'*) continue ;;
            [[:space:]]*) ;;
        esac
        trimmed="${entry%%#*}"
        trimmed="${trimmed%"${trimmed##*[![:space:]]}"}"
        trimmed="${trimmed#"${trimmed%%[![:space:]]*}"}"
        [[ -z "$trimmed" ]] && continue

        if ! grep -Fxq -- "$trimmed" "$live_set_file" 2>/dev/null; then
            printf 'NO-REINTERPRET stale: %s — STALE allowlist entry (no reinterpret_cast at this line; substrate migration consumed it — remove from allowlist).\n' \
                "$trimmed" >&2
            stale_count=$((stale_count + 1))
        fi
    done < "$allowlist"
fi

if [[ "$stale_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-no-reinterpret detected ${stale_count} stale allowlist entr(y/ies).
Each entry points to a path:line where reinterpret_cast no longer
exists — the substrate migration consumed it.  Remove the stale
entries from scripts/no-reinterpret-allowlist.txt so the guard
catches any future drift at those lines.
HINT
    exit 2
fi

printf 'check-no-reinterpret: clean — no new reinterpret_cast sites, no stale allowlist entries.\n' >&2
exit 0
