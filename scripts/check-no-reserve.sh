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
# Content-keyed allowlist: scripts/no-reserve-allowlist.txt.  Each line
# is `path:text` (relative to project root), where `text` is the EXACT
# trimmed source of the violating call line — a CONTENT KEY, not a line
# number.  Comment lines start with `#`.  Empty lines are ignored.
#
# fix-18: content-keying replaces the legacy `path:line` form.  A line
# number drifts the instant ANY edit lands above the call (a concurrent
# agent inserting a member, a new `#include`, a reflowed comment),
# silently staling the entry: the real reserve reads as un-suppressed
# AND the dangling line-keyed entry trips the stale gate.  The call
# line's TEXT is stable across line shifts, so a content-keyed entry
# keeps tracking the same site no matter how the file above it churns.
# Mirror of the name-keyed mint-pattern guard (mint-pattern-allowlist.txt).
#
# Inline suppression: `// NO-RESERVE-OK: <reason>` on the call line
# exempts that single line.
#
# Exempt directories: test/, bench/, examples/ — fixtures may
# deliberately plant the pattern to demonstrate rejection.
#
# Exit status:
#   0 — clean (no NEW reserve sites, no stale allowlist entries)
#   1 — at least one violation (takes precedence over stale)
#   2 — stale allowlist entry (a path:text whose call no longer exists
#       in the file — the site was migrated away or its text changed;
#       prune it) OR bad invocation / missing dependency
#
# Stale-entry detection (parity with check-no-reinterpret-cast.sh):
# every allowlist entry must correspond to a LIVE reserve site.  A
# migrated-away or text-rewritten entry lingers as a dead exemption
# and could re-grandfather a future reserve that happens to match the
# same text; this gate flags it so the allowlist drains in lockstep
# with the §IV migration.

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
    path:text                            — exempts the call whose trimmed
                                           source equals `text` (content key)
    # comment                            — ignored
USAGE
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Plant ONE violation in a synthetic file under a temp root.
        # The scanner must flag it; otherwise the regex is broken.
        # Also plant ONE allowlisted call to verify the content-keyed
        # allowlist mechanism (otherwise a hostile entry could mask
        # everything).  Each reserve call carries TEXTUALLY DISTINCT
        # arguments so the content key maps each to exactly one site —
        # this is the fix-18 invariant the self-test must pin.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/crucible/planted" "$tmp_root/scripts"
        cat >"$tmp_root/include/crucible/planted/planted_violation.h" <<'PLANTED'
#pragma once
// Synthetic no-reserve fixture for --self-test.
// Call lines carry NO trailing comment so the content key equals the
// bare call source (mirrors real grandfathered sites in Cipher.h etc.).
#include <vector>
namespace crucible::planted {
inline void planted_drift() {
    std::vector<int> v;
    v.reserve(7);
}
inline void planted_allowlisted() {
    std::vector<int> v;
    v.reserve(11);
}
inline void planted_suppressed() {
    std::vector<int> v;
    v.reserve(15);  // NO-RESERVE-OK: synthetic --self-test inline marker
}
inline void planted_in_comment() {
    // someobj.reserve(18);  commented out — must NOT be caught
}
}  // namespace crucible::planted
PLANTED
        # Allowlist entry is CONTENT-KEYED: it names the exact trimmed
        # source of the SECOND .reserve() call (`v.reserve(11);`), not
        # its line number.  This is the fix-18 drift-proof key.
        cat >"$tmp_root/scripts/no-reserve-allowlist.txt" <<'ALLOW'
# self-test grandfathered entry (content-keyed)
include/crucible/planted/planted_violation.h:v.reserve(11);
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
        # The planted_drift call (`v.reserve(7);`) must be flagged.
        if ! grep -qF 'v.reserve(7);' "$result_file"; then
            printf 'check-no-reserve: SELF-TEST FAILED — expected diagnostic for v.reserve(7); missing.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The allowlisted call (`v.reserve(11);`) must NOT be flagged.
        if grep -qF 'v.reserve(11);' "$result_file"; then
            printf 'check-no-reserve: SELF-TEST FAILED — allowlist entry leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The inline-suppressed call (`v.reserve(15);`) must NOT be flagged.
        if grep -qF 'v.reserve(15);' "$result_file"; then
            printf 'check-no-reserve: SELF-TEST FAILED — inline NO-RESERVE-OK marker leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The commented-out call (`someobj.reserve(18);`) must NOT be flagged.
        if grep -qF 'someobj.reserve(18);' "$result_file"; then
            printf 'check-no-reserve: SELF-TEST FAILED — comment line leaked through filter.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        rm -f "$result_file"

        # ── Phase 2: drift-proofing (the fix-18 core invariant) ──────
        # Insert a blank line ABOVE the allowlisted call so its LINE
        # NUMBER shifts.  A content-keyed allowlist must STILL exempt
        # it (old line-keyed form would red here — that is the bug).
        # Re-plant the fixture with one extra leading blank line; the
        # content-keyed entry (`v.reserve(11);`) is unchanged.  Both
        # real reserve sites (drift + allowlisted) get exempted so the
        # scan reaches a clean exit 0.
        cat >"$tmp_root/include/crucible/planted/planted_violation.h" <<'PLANTED'

#pragma once
// Synthetic no-reserve fixture for --self-test (line-shifted).
#include <vector>
namespace crucible::planted {
inline void planted_drift() {
    std::vector<int> v;
    v.reserve(7);
}
inline void planted_allowlisted() {
    std::vector<int> v;
    v.reserve(11);
}
}  // namespace crucible::planted
PLANTED
        cat >"$tmp_root/scripts/no-reserve-allowlist.txt" <<'ALLOW'
# self-test live entries (content-keyed; survive the line shift)
include/crucible/planted/planted_violation.h:v.reserve(7);
include/crucible/planted/planted_violation.h:v.reserve(11);
ALLOW
        drift_file="$(mktemp)"
        drift_rc=0
        CRUCIBLE_NO_RESERVE_TEST_ROOT="$tmp_root" \
            bash "${BASH_SOURCE[0]}" 2>"$drift_file" || drift_rc=$?
        if [[ "$drift_rc" -ne 0 ]]; then
            printf 'check-no-reserve: SELF-TEST FAILED (phase 2) — content key did NOT survive line shift (got exit %d, want 0).\n' \
                "$drift_rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$drift_file")" >&2
            rm -f "$drift_file"
            exit 2
        fi
        rm -f "$drift_file"

        # ── Phase 3: stale-allowlist-entry detection ─────────────────
        # Reuse the line-shifted fixture.  Suppress BOTH real reserve
        # sites with live content-keyed entries so the scan reaches the
        # stale pass with zero violations, then add a stale entry (a
        # call text that exists NOWHERE in the file) that MUST be flagged.
        cat >"$tmp_root/scripts/no-reserve-allowlist.txt" <<'ALLOW'
# self-test live entries (real reserve sites, content-keyed)
include/crucible/planted/planted_violation.h:v.reserve(7);
include/crucible/planted/planted_violation.h:v.reserve(11);
# self-test stale entry (no such call text in the file)
include/crucible/planted/planted_violation.h:v.reserve(99);
ALLOW
        stale_file="$(mktemp)"
        stale_rc=0
        CRUCIBLE_NO_RESERVE_TEST_ROOT="$tmp_root" \
            bash "${BASH_SOURCE[0]}" 2>"$stale_file" || stale_rc=$?
        if [[ "$stale_rc" -ne 2 ]]; then
            printf 'check-no-reserve: SELF-TEST FAILED (phase 3) — expected exit 2 (stale), got %d.\n' \
                "$stale_rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$stale_file")" >&2
            rm -f "$stale_file"
            exit 2
        fi
        # Exactly one stale diagnostic — the live entries must NOT be
        # flagged (count==1 implies no false positive).
        stale_emitted="$(grep -c '^NO-RESERVE stale:' "$stale_file" || true)"
        if [[ "$stale_emitted" -ne 1 ]]; then
            printf 'check-no-reserve: SELF-TEST FAILED (phase 3) — expected 1 stale diagnostic, got %s.\n' \
                "$stale_emitted" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$stale_file")" >&2
            rm -f "$stale_file"
            exit 2
        fi
        if ! grep -qF -- 'v.reserve(99);' "$stale_file"; then
            printf 'check-no-reserve: SELF-TEST FAILED (phase 3) — stale entry v.reserve(99); not flagged.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$stale_file")" >&2
            rm -f "$stale_file"
            exit 2
        fi
        rm -f "$stale_file"

        printf 'check-no-reserve: self-test passed — drift caught, content-keyed allowlist + inline marker + comment-filter honoured; content key survives line shift; stale detection flags dead entries (none on live).\n' >&2
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

# ── Allowlist lookup (content-keyed) ─────────────────────────────────
allowlisted() {
    local rel="$1" key="$2"
    [[ -f "$allowlist" ]] || return 1
    # Skip blank/comment lines; match `path:text` exactly, where `text`
    # is the trimmed source of the call line.  The `-Fx` whole-line
    # fixed match makes the call-text comparison byte-for-byte.
    grep -E -v '^[[:space:]]*(#|$)' "$allowlist" | \
        grep -Fxq -- "$rel:$key"
}

# ── Live set of (path:text) for stale-entry detection ────────────────
# Every candidate reserve site that survives the comment / inline-marker
# filters records its (rel:trimmed-text) here, allowlist-blind.  After
# the scan, every allowlist entry must be in this set; entries that
# aren't (the site was migrated away, or its call text was rewritten)
# are STALE and flagged — parity with check-no-reinterpret-cast.sh.
# Content keys are immune to line shifts, so an entry only goes stale on
# a real source change, not on incidental churn above the call.
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

    # Skip comment lines outright.  The candidate may appear inside
    # `//`, `///`, `*`, or `/*` lines (e.g. doc-block prose).
    case "$stripped" in
        '//'*|'///'*|'*'*|'/*'*) continue ;;
    esac

    # Inline suppression — `// NO-RESERVE-OK: <reason>` on the same line.
    case "$text" in
        *'NO-RESERVE-OK'*) continue ;;
    esac

    # Content key — trimmed source of the call line (strip BOTH leading
    # and trailing whitespace so the key is stable against re-indent /
    # trailing-space edits).  `stripped` already dropped leading ws.
    key="${stripped%"${stripped##*[![:space:]]}"}"

    rel="${file#"$scan_root"/}"
    printf '%s:%s\n' "$rel" "$key" >> "$live_set_file"

    if allowlisted "$rel" "$key"; then
        continue
    fi

    # Report with BOTH the line (for the human jumping to the site) and
    # the content key (the stable allowlist key to add if grandfathering).
    printf 'NO-RESERVE violation: %s:%s — std::vector::reserve banned (CLAUDE.md §IV).  Allowlist key: %s:%s\n' \
        "$rel" "$line" "$rel" "$key" >&2
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

# ── Outcome (violations — take precedence over stale) ────────────────
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

  (4) Add 'path:text' to scripts/no-reserve-allowlist.txt for
      grandfathered code awaiting a tracked migration, where 'text'
      is the trimmed call source printed as the "Allowlist key"
      above — a content key that survives line shifts.  Every entry
      is a TODO referencing its fixy-A5-* migration ticket.
HINT
    exit 1
fi

# ── Outcome (stale allowlist entries) ────────────────────────────────
# Every allowlist entry must correspond to a LIVE reserve site.  An
# entry whose (path:text) content key is not in the live set points at a
# call that no longer exists — the site was migrated away, or its call
# text was rewritten.  Flag it so the allowlist drains as the §IV
# migration lands and can never silently re-grandfather a future site.
#
# Comment detection: a `#` is only a comment when it is the FIRST
# non-whitespace character of the line (the documented allowlist
# grammar).  We do NOT strip a trailing `#...` because a content key is
# the call source and could in principle contain `#`; full-line-comment
# detection is sufficient and avoids corrupting keys.
stale_count=0
if [[ -f "$allowlist" ]]; then
    while IFS= read -r entry; do
        # Strip leading whitespace for the comment / blank test.
        trimmed="${entry#"${entry%%[![:space:]]*}"}"
        case "$trimmed" in
            ''|'#'*) continue ;;
        esac
        # Strip trailing whitespace; the remainder is the `path:text` key.
        trimmed="${trimmed%"${trimmed##*[![:space:]]}"}"
        [[ -z "$trimmed" ]] && continue
        if ! grep -Fxq -- "$trimmed" "$live_set_file" 2>/dev/null; then
            printf 'NO-RESERVE stale: %s — STALE allowlist entry (no std::vector::reserve call with this text in the file; the site was migrated away or the call text was rewritten — remove from allowlist).\n' \
                "$trimmed" >&2
            stale_count=$((stale_count + 1))
        fi
    done < "$allowlist"
fi

if [[ "$stale_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-no-reserve detected ${stale_count} stale allowlist entr(y/ies).
Each entry points at a path:text whose std::vector::reserve call no
longer exists — the site was migrated away or its call text was
rewritten.  Prune the stale entries from scripts/no-reserve-allowlist.txt
so the guard regains drift coverage and cannot re-grandfather a future
reserve re-introduced with the same text.
HINT
    exit 2
fi

printf 'check-no-reserve: clean — no new std::vector::reserve sites, no stale allowlist entries.\n' >&2
exit 0
