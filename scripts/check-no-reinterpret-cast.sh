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
# Content-keyed allowlist: scripts/no-reinterpret-allowlist.txt.  Each
# line is `path:text` (relative to project root), where `text` is the
# EXACT trimmed source of the violating call line — a CONTENT KEY, not
# a line number.  Comment lines start with `#`.  Empty lines ignored.
#
# fix-18: content-keying replaces the legacy `path:line` form.  A line
# number drifts the instant ANY edit lands above the call (a concurrent
# agent inserting a member, a new `#include`, a reflowed comment),
# silently staling the entry: the real reinterpret_cast reads as
# un-suppressed AND the dangling line-keyed entry trips the stale gate.
# The call line's TEXT is stable across line shifts, so a content-keyed
# entry keeps tracking the same site no matter how the file above it
# churns.  Mirror of the name-keyed mint-pattern guard.
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
#       (a path:text whose reinterpret_cast call no longer exists in the
#       file means the substrate migration consumed it or its text was
#       rewritten; the entry must be removed so the guard catches future
#       drift at that site).

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
    path:text                               — exempts the call whose trimmed
                                              source equals `text` (content key)
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
        # Finally plant ONE STALE allowlist entry whose call text exists
        # nowhere in the file — must trigger exit 2.  Each cast carries
        # a TEXTUALLY DISTINCT target type so the content key maps each
        # to exactly one site — the fix-18 invariant the self-test pins.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/crucible/planted" "$tmp_root/scripts"
        cat >"$tmp_root/include/crucible/planted/planted_reinterpret.h" <<'PLANTED'
#pragma once
// Synthetic no-reinterpret fixture for --self-test.
// Each cast has a distinct target type so the content key is unique.
#include <cstddef>
namespace crucible::planted {
inline std::uintptr_t planted_drift(void* p) {
    return reinterpret_cast<std::uintptr_t>(p);
}
inline std::uint64_t planted_allowlisted(void* p) {
    return reinterpret_cast<std::uint64_t>(p);
}
inline std::uint32_t planted_suppressed(void* p) {
    return reinterpret_cast<std::uint32_t>(p);  // NO-REINTERPRET-OK: synthetic
}
inline void planted_in_comment() {
    // someobj = reinterpret_cast<int*>(0);  commented out
}
}  // namespace crucible::planted
PLANTED
        # Allowlist entry is CONTENT-KEYED: it names the exact trimmed
        # source of the SECOND reinterpret_cast call
        # (`return reinterpret_cast<std::uint64_t>(p);`), not its line
        # number.  This is the fix-18 drift-proof key.  Also plant a
        # STALE entry (cast text present nowhere) to verify stale
        # detection rides alongside.
        cat >"$tmp_root/scripts/no-reinterpret-allowlist.txt" <<'ALLOW'
# self-test grandfathered entry (content-keyed)
include/crucible/planted/planted_reinterpret.h:return reinterpret_cast<std::uint64_t>(p);
# self-test stale entry (no such cast text in the file)
include/crucible/planted/planted_reinterpret.h:return reinterpret_cast<std::int8_t>(p);
ALLOW
        result_file="$(mktemp)"
        rc=0
        CRUCIBLE_NO_REINTERPRET_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file" || rc=$?
        # First sub-test: with the stale entry present we expect rc=1
        # (the drift is detected first; violation reporting takes
        # precedence over stale reporting so CI tells the user the most
        # actionable thing first).
        if [[ "$rc" -ne 1 ]]; then
            printf 'check-no-reinterpret: SELF-TEST FAILED — expected violation exit 1, got %d.\n' "$rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The planted_drift call (uintptr_t) must be flagged.
        if ! grep -qF 'reinterpret_cast<std::uintptr_t>(p);' "$result_file"; then
            printf 'check-no-reinterpret: SELF-TEST FAILED — expected diagnostic for uintptr_t cast missing.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The allowlisted call (uint64_t) must NOT be flagged as a violation.
        if grep -qF 'reinterpret_cast<std::uint64_t>(p); — reinterpret' "$result_file"; then
            printf 'check-no-reinterpret: SELF-TEST FAILED — allowlist entry leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The inline-suppressed call (uint32_t) must NOT be flagged.
        if grep -qF 'reinterpret_cast<std::uint32_t>(p); — reinterpret' "$result_file"; then
            printf 'check-no-reinterpret: SELF-TEST FAILED — inline NO-REINTERPRET-OK marker leaked.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # The commented-out call (int*) must NOT be flagged.
        if grep -qF 'reinterpret_cast<int*>(0); — reinterpret' "$result_file"; then
            printf 'check-no-reinterpret: SELF-TEST FAILED — comment line leaked through filter.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi

        # ── Second sub-test: drift-proofing (the fix-18 core invariant)
        # Re-plant with an extra leading blank line so the allowlisted
        # call's LINE NUMBER shifts; the content-keyed entry is unchanged
        # and must STILL exempt it (line-keyed form would red here).
        cat >"$tmp_root/include/crucible/planted/planted_reinterpret.h" <<'PLANTED'

#pragma once
// Synthetic no-reinterpret fixture for --self-test (line-shifted).
#include <cstddef>
namespace crucible::planted {
inline std::uint64_t planted_allowlisted(void* p) {
    return reinterpret_cast<std::uint64_t>(p);
}
}  // namespace crucible::planted
PLANTED
        cat >"$tmp_root/scripts/no-reinterpret-allowlist.txt" <<'ALLOW'
# self-test live entry (content-keyed; survives the line shift)
include/crucible/planted/planted_reinterpret.h:return reinterpret_cast<std::uint64_t>(p);
ALLOW
        rc=0
        CRUCIBLE_NO_REINTERPRET_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file" || rc=$?
        if [[ "$rc" -ne 0 ]]; then
            printf 'check-no-reinterpret: SELF-TEST FAILED — content key did NOT survive line shift (got exit %d, want 0).\n' "$rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi

        # ── Third sub-test: stale-only pass.  Keep the live entry and
        # add a stale entry (cast text present nowhere); expect exit 2.
        cat >"$tmp_root/scripts/no-reinterpret-allowlist.txt" <<'ALLOW'
# self-test grandfathered entry (still active, content-keyed)
include/crucible/planted/planted_reinterpret.h:return reinterpret_cast<std::uint64_t>(p);
# self-test stale entry (no such cast text in the file)
include/crucible/planted/planted_reinterpret.h:return reinterpret_cast<std::int8_t>(p);
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
        printf 'check-no-reinterpret: self-test passed — drift caught, content-keyed allowlist + inline marker + comment-filter honoured; content key survives line shift; stale-detection flags dead entries.\n' >&2
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

# ── Allowlist lookup (content-keyed) ─────────────────────────────────
allowlisted() {
    local rel="$1" key="$2"
    [[ -f "$allowlist" ]] || return 1
    # Skip blank/comment lines; match `path:text` exactly, where `text`
    # is the trimmed source of the call line.  `-Fx` whole-line fixed
    # match makes the call-text comparison byte-for-byte.
    grep -E -v '^[[:space:]]*(#|$)' "$allowlist" | \
        grep -Fxq -- "$rel:$key"
}

# ── Live set of (path:text) for stale-entry detection ────────────────
# Records each surviving candidate's (rel:trimmed-text) content key,
# allowlist-blind.  An allowlist entry not in this set is STALE — its
# call was migrated away or its text was rewritten.  Content keys are
# immune to line shifts, so an entry only goes stale on a real change.
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
    printf 'NO-REINTERPRET violation: %s:%s — reinterpret_cast banned (CLAUDE.md §III).  Allowlist key: %s:%s\n' \
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

  (5) Add 'path:text' to scripts/no-reinterpret-allowlist.txt for
      grandfathered code awaiting a tracked migration, where 'text' is
      the trimmed call source printed as the "Allowlist key" above — a
      content key that survives line shifts (FIXY-U-082 + WRAP-* tickets
      are the active drain plans).
HINT
    exit 1
fi

# ── Outcome (stale allowlist entries) ────────────────────────────────
# Every allowlist entry must correspond to a LIVE reinterpret_cast site.
# An entry whose (path:text) content key is not in the live set points
# at a call that no longer exists — the substrate migration consumed it,
# or its call text was rewritten.
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
            printf 'NO-REINTERPRET stale: %s — STALE allowlist entry (no reinterpret_cast call with this text in the file; substrate migration consumed it or the call text was rewritten — remove from allowlist).\n' \
                "$trimmed" >&2
            stale_count=$((stale_count + 1))
        fi
    done < "$allowlist"
fi

if [[ "$stale_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-no-reinterpret detected ${stale_count} stale allowlist entr(y/ies).
Each entry points to a path:text whose reinterpret_cast call no longer
exists — the substrate migration consumed it or its call text was
rewritten.  Remove the stale entries from
scripts/no-reinterpret-allowlist.txt so the guard catches any future
drift at those sites.
HINT
    exit 2
fi

printf 'check-no-reinterpret: clean — no new reinterpret_cast sites, no stale allowlist entries.\n' >&2
exit 0
