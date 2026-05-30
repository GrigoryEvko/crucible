#!/usr/bin/env bash
# check-mint-pattern.sh — §XXI Universal Mint Pattern compliance scanner
# (FIXY-U-003).
#
# CLAUDE.md §XXI: every cross-tier composition factory is named
# `mint_<noun>` and MUST be `[[nodiscard]] constexpr noexcept`
# (with an explicit `requires` clause when templated) — except
# that factories which genuinely allocate may drop `constexpr` in
# favour of plain `[[nodiscard]] noexcept`.
#
# The four §XXI attributes that this scanner enforces:
#
#   1. `[[nodiscard]]`  — every mint MUST carry it.  Dropping it
#                         lets a chained mint expression whose
#                         return value is unused silently compile,
#                         defeating construction-time validation.
#   2. `noexcept`       — every mint MUST be noexcept.  Even
#                         allocating mints carry this; throwing
#                         from a mint defeats the type-level
#                         soundness gate downstream code relies on.
#   3. `constexpr`      — every mint MUST be constexpr UNLESS the
#                         factory genuinely allocates (in which
#                         case `constexpr` would lie about the
#                         runtime cost).  Allocating mints
#                         annotate `// MINT-PATTERN-OK: allocating`
#                         OR enter the allowlist with key
#                         `path:line:constexpr-ok`.
#   4. `requires` clause— every TEMPLATED mint MUST gate its
#                         instantiations through a single concept.
#                         Non-templated token mints are exempt.
#
# Per-line allowlist: scripts/mint-pattern-allowlist.txt.  Each
# line is either:
#
#   path:line                        # full exemption (all four checks)
#   path:line:<check>-ok             # partial — exempts ONE check
#                                      where <check> ∈ {nodiscard,
#                                      noexcept, constexpr, requires}
#
# Inline suppression: `// MINT-PATTERN-OK: <reason>` on the
# candidate signature line exempts ALL four checks for that line.
#
# Exempt directories: test/, bench/, examples/ — fixtures
# deliberately violate the pattern to demonstrate rejection.
#
# Exit status:
#   0  — no §XXI drift detected, no stale allowlist entries
#   1  — at least one violation outside the allowlist (takes precedence)
#   2  — stale allowlist entry (an entry whose (line, check) no longer
#        fails — the mint moved on a line shift, was fixed, or renamed;
#        prune it) OR bad invocation / missing dependency
#
# Stale-entry detection (parity with check-no-reinterpret-cast.sh):
# every allowlist entry must correspond to a LIVE check-failure.  A
# line-shifting edit silently invalidates a grandfather entry; without
# this gate the guard cannot tell the entry from a real suppression.

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-mint-pattern.sh — §XXI Universal Mint Pattern compliance scanner.

Usage:
  check-mint-pattern.sh              # scan; exit 1 on violation
  check-mint-pattern.sh --self-test  # plant violations and verify catch
  check-mint-pattern.sh -h | --help  # usage

Checks:
  nodiscard  — [[nodiscard]] in signature or preceding template block
  noexcept   — noexcept keyword in or shortly after function name
  constexpr  — constexpr keyword in signature (allocating mints exempt)
  requires   — requires-clause in template block (only when templated)

Suppression:
  // MINT-PATTERN-OK: <reason>            on signature line, all checks
  scripts/mint-pattern-allowlist.txt:
    path:line                             — exempts all four checks
    path:line:<check>-ok                  — exempts ONE check
                                            (nodiscard/noexcept/constexpr/requires)
USAGE
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Plant a canonical mint plus one drift case per check.
        # If any expected violation is missing, the regex broke.
        # If the canonical case fires, we have a false positive.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/crucible/planted" "$tmp_root/scripts"
        cat >"$tmp_root/include/crucible/planted/planted_violation.h" <<'PLANTED'
#pragma once
// Synthetic §XXI MINT-PATTERN drift fixtures for --self-test.
#include <crucible/elsewhere.h>  // mint_token_from_include_comment (re-export)
namespace crucible::planted {
struct Token { int v = 0; };
struct Source { int s = 0; };
template <typename T> concept SomeConcept = true;

// Comment-line false-positive guard: a `mint_*(` token that appears
// only inside a trailing `//` comment must NOT be flagged.  The real
// definition (mint_token_ok) carries the attributes.
constexpr int unrelated = 0;  // call mint_token_comment_only(x) elsewhere

// Canonical mint — all four attributes present.  Must NOT be flagged.
template <typename T>
    requires SomeConcept<T>
[[nodiscard]] constexpr Token mint_token_ok(Source const& src) noexcept {
    return Token{src.s};
}

// Drift 1: attribute omitted — flagged on the nodiscard axis.
template <typename T>
    requires SomeConcept<T>
constexpr Token mint_token_drift_nodiscard(Source const& src) noexcept {
    return Token{src.s + 1};
}

// Drift 2: noexcept omitted — flagged on the noexcept axis.
template <typename T>
    requires SomeConcept<T>
[[nodiscard]] constexpr Token mint_token_drift_noexcept(Source const& src) {
    return Token{src.s + 2};
}

// Drift 3: constexpr omitted — flagged on the constexpr axis.
template <typename T>
    requires SomeConcept<T>
[[nodiscard]] Token mint_token_drift_constexpr(Source const& src) noexcept {
    return Token{src.s + 3};
}

// Drift 4: templated, no requires clause — flagged on the requires axis.
template <typename T>
[[nodiscard]] constexpr Token mint_token_drift_requires(Source const& src) noexcept {
    return Token{src.s + 4};
}
}  // namespace crucible::planted
PLANTED
        : >"$tmp_root/scripts/mint-pattern-allowlist.txt"
        result_file="$(mktemp)"
        if CRUCIBLE_MINT_PATTERN_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file"; then
            printf 'check-mint-pattern: SELF-TEST FAILED — drift fixtures not caught.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        expected=(
            'mint_token_drift_nodiscard missing nodiscard'
            'mint_token_drift_noexcept missing noexcept'
            'mint_token_drift_constexpr missing constexpr'
            'mint_token_drift_requires missing requires'
        )
        for expect in "${expected[@]}"; do
            if ! grep -qF "$expect" "$result_file"; then
                printf 'check-mint-pattern: SELF-TEST FAILED — expected diagnostic missing: %s\n' \
                    "$expect" >&2
                printf '── scanner stderr ───\n%s\n────────────────────\n' \
                    "$(cat "$result_file")" >&2
                rm -f "$result_file"
                exit 2
            fi
        done
        # False-positive guard: canonical mint must NOT be flagged.
        if grep -q 'mint_token_ok' "$result_file"; then
            printf 'check-mint-pattern: SELF-TEST FAILED — false positive on mint_token_ok.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # False-positive guard: a `mint_*` token that lives only inside a
        # trailing `//` comment OR on an `#include` line must NOT be
        # flagged — those are documentation mentions, not declarations.
        for ghost in 'mint_token_from_include_comment' 'mint_token_comment_only'; do
            if grep -q "$ghost" "$result_file"; then
                printf 'check-mint-pattern: SELF-TEST FAILED — false positive on comment/include mention: %s\n' \
                    "$ghost" >&2
                printf '── scanner stderr ───\n%s\n────────────────────\n' \
                    "$(cat "$result_file")" >&2
                rm -f "$result_file"
                exit 2
            fi
        done
        rm -f "$result_file"

        # ── Phase 2: stale-allowlist-entry detection ─────────────────
        # Reuse the planted fixture.  Build an allowlist with FOUR live
        # per-check entries (one per drift mint, suppressing every
        # violation so the scan reaches the stale pass) PLUS TWO stale
        # entries that correspond to NO live failure:
        #   * a check-suffixed entry on the canonical mint (it passes
        #     every check, so its `:nodiscard-ok` has no live failure),
        #   * a dangling `:1:requires-ok` on the `#pragma once` line.
        # Expect exit 2 with EXACTLY the two stale entries flagged and
        # none of the four live entries falsely flagged.  Line numbers
        # are grepped from the fixture so editing it cannot desync them.
        planted="$tmp_root/include/crucible/planted/planted_violation.h"
        rel='include/crucible/planted/planted_violation.h'
        ln_ok="$(grep -n 'mint_token_ok(' "$planted" | head -1 || true)";              ln_ok="${ln_ok%%:*}"
        ln_nd="$(grep -n 'mint_token_drift_nodiscard(' "$planted" | head -1 || true)"; ln_nd="${ln_nd%%:*}"
        ln_ne="$(grep -n 'mint_token_drift_noexcept(' "$planted" | head -1 || true)";  ln_ne="${ln_ne%%:*}"
        ln_cx="$(grep -n 'mint_token_drift_constexpr(' "$planted" | head -1 || true)"; ln_cx="${ln_cx%%:*}"
        ln_rq="$(grep -n 'mint_token_drift_requires(' "$planted" | head -1 || true)";  ln_rq="${ln_rq%%:*}"
        cat >"$tmp_root/scripts/mint-pattern-allowlist.txt" <<ALLOW
$rel:$ln_nd:nodiscard-ok
$rel:$ln_ne:noexcept-ok
$rel:$ln_cx:constexpr-ok
$rel:$ln_rq:requires-ok
$rel:$ln_ok:nodiscard-ok
$rel:1:requires-ok
ALLOW
        stale_file="$(mktemp)"
        stale_rc=0
        CRUCIBLE_MINT_PATTERN_TEST_ROOT="$tmp_root" \
            bash "${BASH_SOURCE[0]}" 2>"$stale_file" || stale_rc=$?
        if (( stale_rc != 2 )); then
            printf 'check-mint-pattern: SELF-TEST FAILED (phase 2) — expected exit 2 (stale), got %d.\n' \
                "$stale_rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$stale_file")" >&2
            rm -f "$stale_file"
            exit 2
        fi
        # Exactly two stale diagnostics — count==2 implies the four LIVE
        # entries were NOT falsely flagged (a false positive would push
        # the count past two).
        stale_emitted="$(grep -c '^MINT-PATTERN stale:' "$stale_file" || true)"
        if [[ "$stale_emitted" -ne 2 ]]; then
            printf 'check-mint-pattern: SELF-TEST FAILED (phase 2) — expected 2 stale diagnostics, got %s.\n' \
                "$stale_emitted" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$stale_file")" >&2
            rm -f "$stale_file"
            exit 2
        fi
        stale_expected=(
            "$rel:$ln_ok:nodiscard-ok"
            "$rel:1:requires-ok"
        )
        for expect in "${stale_expected[@]}"; do
            if ! grep -qF -- "$expect" "$stale_file"; then
                printf 'check-mint-pattern: SELF-TEST FAILED (phase 2) — expected stale entry not flagged: %s\n' \
                    "$expect" >&2
                printf '── scanner stderr ───\n%s\n────────────────────\n' \
                    "$(cat "$stale_file")" >&2
                rm -f "$stale_file"
                exit 2
            fi
        done
        # Explicit false-positive guard: a LIVE per-check entry must
        # never appear in a stale diagnostic.
        if grep '^MINT-PATTERN stale:' "$stale_file" | grep -qF -- "$rel:$ln_nd:nodiscard-ok"; then
            printf 'check-mint-pattern: SELF-TEST FAILED (phase 2) — live entry %s falsely flagged stale.\n' \
                "$rel:$ln_nd:nodiscard-ok" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$stale_file")" >&2
            rm -f "$stale_file"
            exit 2
        fi
        rm -f "$stale_file"

        # ── Phase 3: full-line (path:line, all-checks) stale branch ──
        # Phase 2 exercised only the `:check-ok` grammar.  The bare
        # `path:line` grammar takes the OTHER stale-detection branch
        # (live iff ANY of the four checks fails at the line).  Suppress
        # every drift with a FULL-LINE entry (each lands on a line that
        # fails ≥1 check → live → must NOT be flagged) and add two
        # full-line entries with no live failure (canonical mint line +
        # the `#pragma once` line) that MUST be flagged stale.
        cat >"$tmp_root/scripts/mint-pattern-allowlist.txt" <<ALLOW
$rel:$ln_nd
$rel:$ln_ne
$rel:$ln_cx
$rel:$ln_rq
$rel:$ln_ok
$rel:1
ALLOW
        full_file="$(mktemp)"
        full_rc=0
        CRUCIBLE_MINT_PATTERN_TEST_ROOT="$tmp_root" \
            bash "${BASH_SOURCE[0]}" 2>"$full_file" || full_rc=$?
        if (( full_rc != 2 )); then
            printf 'check-mint-pattern: SELF-TEST FAILED (phase 3) — expected exit 2 (stale), got %d.\n' \
                "$full_rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$full_file")" >&2
            rm -f "$full_file"
            exit 2
        fi
        full_emitted="$(grep -c '^MINT-PATTERN stale:' "$full_file" || true)"
        if [[ "$full_emitted" -ne 2 ]]; then
            printf 'check-mint-pattern: SELF-TEST FAILED (phase 3) — expected 2 stale diagnostics, got %s.\n' \
                "$full_emitted" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$full_file")" >&2
            rm -f "$full_file"
            exit 2
        fi
        # Match on `<entry> — ` (entry, space, em-dash, space): the
        # trailing delimiter pins the line number so `:1 — ` cannot
        # substring-match the `:11 — ` diagnostic, and -F sidesteps the
        # `.`/`/` regex metacharacters in the path.
        for expect in "$rel:$ln_ok" "$rel:1"; do
            if ! grep -qF -- "$expect — " "$full_file"; then
                printf 'check-mint-pattern: SELF-TEST FAILED (phase 3) — expected full-line stale entry not flagged: %s\n' \
                    "$expect" >&2
                printf '── scanner stderr ───\n%s\n────────────────────\n' \
                    "$(cat "$full_file")" >&2
                rm -f "$full_file"
                exit 2
            fi
        done
        # A LIVE full-line entry must NOT appear in a stale diagnostic.
        if grep -qF -- "$rel:$ln_nd — " "$full_file"; then
            printf 'check-mint-pattern: SELF-TEST FAILED (phase 3) — live full-line entry %s falsely flagged stale.\n' \
                "$rel:$ln_nd" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$full_file")" >&2
            rm -f "$full_file"
            exit 2
        fi
        rm -f "$full_file"

        # ── Phase 4: violations take precedence over stale entries ───
        # Leave one drift UN-suppressed (a live violation) and plant a
        # dangling `:1:requires-ok` (would be stale).  The scanner must
        # exit 1 on the violation BEFORE reaching the stale pass, so the
        # dangling entry is never evaluated — no stale diagnostic.
        cat >"$tmp_root/scripts/mint-pattern-allowlist.txt" <<ALLOW
$rel:$ln_ne:noexcept-ok
$rel:$ln_cx:constexpr-ok
$rel:$ln_rq:requires-ok
$rel:1:requires-ok
ALLOW
        prec_file="$(mktemp)"
        prec_rc=0
        CRUCIBLE_MINT_PATTERN_TEST_ROOT="$tmp_root" \
            bash "${BASH_SOURCE[0]}" 2>"$prec_file" || prec_rc=$?
        if (( prec_rc != 1 )); then
            printf 'check-mint-pattern: SELF-TEST FAILED (phase 4) — expected exit 1 (violation precedence), got %d.\n' \
                "$prec_rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$prec_file")" >&2
            rm -f "$prec_file"
            exit 2
        fi
        if ! grep -qF -- 'mint_token_drift_nodiscard missing nodiscard' "$prec_file"; then
            printf 'check-mint-pattern: SELF-TEST FAILED (phase 4) — un-suppressed violation not emitted.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$prec_file")" >&2
            rm -f "$prec_file"
            exit 2
        fi
        if grep -q '^MINT-PATTERN stale:' "$prec_file"; then
            printf 'check-mint-pattern: SELF-TEST FAILED (phase 4) — stale pass ran despite a pending violation (precedence broken).\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$prec_file")" >&2
            rm -f "$prec_file"
            exit 2
        fi
        rm -f "$prec_file"

        # ── Phase 5: NAME-KEYED grammar (drift-proof suppression) ────
        # Suppress every drift mint by NAME (no line number anywhere in
        # the allowlist) so the entries cannot be invalidated by a line
        # shift.  Then PROVE drift-resistance: prepend extra lines to the
        # fixture so every mint moves to a different line, and re-run —
        # the name-keyed entries must STILL suppress (a line-keyed entry
        # would have gone stale).  Also plant one name-keyed entry that
        # matches NO live failure (the canonical mint's name) which MUST
        # be flagged stale; and verify the four live name-keyed entries
        # are NOT falsely flagged.
        cat >"$tmp_root/scripts/mint-pattern-allowlist.txt" <<ALLOW
$rel:mint_token_drift_nodiscard:nodiscard-ok
$rel:mint_token_drift_noexcept:noexcept-ok
$rel:mint_token_drift_constexpr:constexpr-ok
$rel:mint_token_drift_requires:requires-ok
$rel:mint_token_ok:nodiscard-ok
ALLOW
        # Force a line shift: prepend 7 blank lines so every mint's line
        # number changes.  Name keys are immune; line keys would stale.
        shifted="$tmp_root/include/crucible/planted/shifted.tmp"
        { printf '\n\n\n\n\n\n\n'; cat "$planted"; } > "$shifted"
        mv "$shifted" "$planted"
        name_file="$(mktemp)"
        name_rc=0
        CRUCIBLE_MINT_PATTERN_TEST_ROOT="$tmp_root" \
            bash "${BASH_SOURCE[0]}" 2>"$name_file" || name_rc=$?
        # Expect exit 2 (the canonical-name entry is stale) and NO
        # surviving violation (all four drifts suppressed by name despite
        # the line shift).
        if (( name_rc != 2 )); then
            printf 'check-mint-pattern: SELF-TEST FAILED (phase 5) — expected exit 2 (name-keyed stale after line shift), got %d.\n' \
                "$name_rc" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$name_file")" >&2
            rm -f "$name_file"
            exit 2
        fi
        if grep -q '^MINT-PATTERN violation:' "$name_file"; then
            printf 'check-mint-pattern: SELF-TEST FAILED (phase 5) — name-keyed entries failed to suppress after a line shift (drift-proofing broken).\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$name_file")" >&2
            rm -f "$name_file"
            exit 2
        fi
        # Exactly one stale: the canonical mint's name-keyed entry.
        name_stale="$(grep -c '^MINT-PATTERN stale:' "$name_file" || true)"
        if [[ "$name_stale" -ne 1 ]]; then
            printf 'check-mint-pattern: SELF-TEST FAILED (phase 5) — expected 1 name-keyed stale diagnostic, got %s.\n' \
                "$name_stale" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$name_file")" >&2
            rm -f "$name_file"
            exit 2
        fi
        if ! grep -qF -- "$rel:mint_token_ok:nodiscard-ok" "$name_file"; then
            printf 'check-mint-pattern: SELF-TEST FAILED (phase 5) — expected name-keyed stale entry not flagged: %s\n' \
                "$rel:mint_token_ok:nodiscard-ok" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$name_file")" >&2
            rm -f "$name_file"
            exit 2
        fi
        # A LIVE name-keyed entry must NOT appear in a stale diagnostic.
        if grep '^MINT-PATTERN stale:' "$name_file" \
            | grep -qF -- "$rel:mint_token_drift_nodiscard:nodiscard-ok"; then
            printf 'check-mint-pattern: SELF-TEST FAILED (phase 5) — live name-keyed entry falsely flagged stale.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$name_file")" >&2
            rm -f "$name_file"
            exit 2
        fi
        rm -f "$name_file"

        printf 'check-mint-pattern: self-test passed — drift fires on all four axes (none on canonical); comment/include mentions are not false-flagged; stale detection flags dead allowlist entries across line-keyed AND name-keyed grammars (none on live); name-keyed entries survive a line shift (drift-proof); violations preempt the stale pass.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-mint-pattern: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Scan-root override for --self-test recursion ─────────────────────
scan_root="${CRUCIBLE_MINT_PATTERN_TEST_ROOT:-$root}"
allowlist="$scan_root/scripts/mint-pattern-allowlist.txt"

if ! command -v rg >/dev/null 2>&1; then
    printf 'check-mint-pattern: ripgrep (rg) is required\n' >&2
    exit 2
fi

# ── Helper: attribute presence in surrounding declaration ────────────
# `has_attribute_above` scans the signature line plus up to 5
# preceding NON-comment lines for the `attr` token, stopping at any
# preceding-declaration boundary (`;`, `}`, `#`, `class `, `struct `,
# `namespace `).  Comment lines are transparently skipped.
#
# Word-boundary discipline (grep -Fwq) is load-bearing: substring
# match would false-positive-suppress drift sites whose mint
# identifier embeds the keyword as a suffix
# (e.g. mint_token_drift_constexpr contains "constexpr" within an
# identifier and must NOT satisfy the constexpr check).
has_attribute_above() {
    local file="$1" line="$2" attr="$3"
    local offset ln text stripped
    for offset in 0 -1 -2 -3 -4 -5; do
        ln=$((line + offset))
        (( ln < 1 )) && break
        text="$(sed -n "${ln}p" "$file" 2>/dev/null || true)"
        stripped="${text#"${text%%[![:space:]]*}"}"
        [[ -z "$stripped" ]] && continue
        case "$stripped" in
            '//'*|'///'*|'*'*|'/*'*) continue ;;
        esac
        if printf '%s\n' "$text" | grep -Fwq -- "$attr"; then
            return 0
        fi
        if (( offset != 0 )); then
            case "$stripped" in
                *';'|'}'*|'#'*|'class '*|'struct '*|'namespace '*) return 1 ;;
            esac
        fi
    done
    return 1
}

# ── Helper: noexcept presence in or shortly after signature ──────────
# Scans the candidate line plus up to 14 lines FORWARD for the
# `noexcept` keyword, stopping at end-of-declaration markers
# (`;` or opening `{` of body).  Wider than nodiscard's window
# because multi-line signatures with permission packs / long
# parameter lists routinely span 10+ lines; `noexcept` legally
# sits AFTER the closing `)`, so the scan must reach past the
# end of the parameter list.  Word-boundary discipline as above
# — bare substring would false-positive on mint_*_noexcept names.
has_noexcept_in_signature() {
    local file="$1" line="$2"
    local offset ln text
    for offset in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14; do
        ln=$((line + offset))
        text="$(sed -n "${ln}p" "$file" 2>/dev/null || true)"
        [[ -z "$text" ]] && break
        if printf '%s\n' "$text" | grep -Fwq noexcept; then
            return 0
        fi
        case "$text" in
            *';'|*'{'|*' {'|*'){') return 1 ;;
        esac
    done
    return 1
}

# ── Helper: deleted-declaration detection ────────────────────────────
# A `= delete("...")` overload is a deliberately-poisoned tombstone, not
# a factory — it carries a diagnostic for a mis-spelled call and is
# structurally exempt from all four §XXI axes (a deleted function has no
# [[nodiscard]], no noexcept, no constexpr, no body to gate).  Scan the
# candidate line plus up to 14 lines forward for an `= delete` token,
# stopping at the body-open `{` (a real definition) which precludes a
# deletion.  Returns 0 (skip) when a deletion is found before any body.
is_deleted_decl() {
    local file="$1" line="$2"
    local offset ln text
    for offset in 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14; do
        ln=$((line + offset))
        text="$(sed -n "${ln}p" "$file" 2>/dev/null || true)"
        [[ -z "$text" ]] && break
        case "$text" in
            *'= delete'*|*'=delete'*) return 0 ;;
        esac
        # A body-open brace means this is a real definition, not a
        # deletion — stop before mis-reading a later overload's deletion.
        case "$text" in
            *'{') return 1 ;;
        esac
    done
    return 1
}

# ── Helper: requires-clause presence when templated ──────────────────
# Walks back up to 15 lines.  If `template <` / `template<` is
# found in the preceding block, the same block must contain a
# `requires` keyword (either on the template line itself in the
# template <T> requires C<T> form, or on a continuation line).
# Returns 0 (pass) when:
#   * No template found in window — non-templated mint, no check.
#   * Template found AND requires-clause present in the block.
# Returns 1 (flag) when template found AND no requires-clause.
has_requires_if_templated() {
    local file="$1" line="$2"
    local offset ln text stripped
    local found_template=0
    local found_requires=0
    for offset in -1 -2 -3 -4 -5 -6 -7 -8 -9 -10 -11 -12 -13 -14 -15; do
        ln=$((line + offset))
        (( ln < 1 )) && break
        text="$(sed -n "${ln}p" "$file" 2>/dev/null || true)"
        stripped="${text#"${text%%[![:space:]]*}"}"
        [[ -z "$stripped" ]] && continue
        case "$stripped" in
            '//'*|'///'*|'*'*|'/*'*) continue ;;
        esac
        if [[ "$text" == *requires* ]]; then
            found_requires=1
        fi
        if [[ "$stripped" == 'template '* ]] || [[ "$stripped" == 'template<'* ]]; then
            found_template=1
            break
        fi
        case "$stripped" in
            *';'|'}'*|'#'*|'class '*|'struct '*|'namespace '*) break ;;
        esac
    done
    (( found_template == 0 )) && return 0
    (( found_requires == 1 )) && return 0
    return 1
}

# ── Helper: per-check allowlist lookup ───────────────────────────────
# Two interchangeable entry grammars are honoured:
#
#   LINE-KEYED  path:line[:check-ok]      — keyed on the source line.
#   NAME-KEYED  path:mint_name[:check-ok] — keyed on the mint factory
#                                           identifier (the `mint_<noun>`
#                                           token on the candidate line).
#
# NAME-KEYED entries are DRIFT-PROOF: a line-shifting edit (e.g. a
# concurrent agent inserting members above the mint) moves the line
# number but NOT the mint's identifier, so a name-keyed exemption keeps
# tracking the same factory across edits.  This is the fix-18 approach:
# eliminate the line number as the drift vector.  Prefer name-keying for
# any new exemption; line-keyed entries are retained for backward
# compatibility but are inherently fragile.
#
# A name-keyed entry's `path` must match `rel` exactly (a mint of the
# same name in another header is a DIFFERENT factory and must carry its
# own entry).  Overloads sharing one name in one file collapse to a
# single name-keyed entry — this is intentional: an overload set is one
# §XXI factory with one carve-out rationale.
allowlisted_for() {
    local rel="$1" line="$2" check="$3" name="$4"
    [[ -f "$allowlist" ]] || return 1
    # Line-keyed (legacy, fragile).
    grep -Fxq -- "$rel:$line" "$allowlist" && return 0
    grep -Fxq -- "$rel:$line:$check-ok" "$allowlist" && return 0
    # Name-keyed (drift-proof).
    if [[ -n "$name" ]]; then
        grep -Fxq -- "$rel:$name" "$allowlist" && return 0
        grep -Fxq -- "$rel:$name:$check-ok" "$allowlist" && return 0
    fi
    return 1
}

# ── Candidate signature regex ─────────────────────────────────────────
# Broad: any line containing a `mint_<name>(` token.  Filtered
# below to exclude comments, friend forwards, using-decls, method
# calls, and obvious call-expression positions (return, assignment,
# argument position).  False-positive sites land in the allowlist.
candidate_pattern='\bmint_[a-z_]+\s*\('

# ── Live-failure set of (path:line:check) for stale-entry detection ──
# Every time a check actually fails (allowlist-blind) we record the
# (rel:line:check) tuple here.  After the scan, every allowlist entry
# must correspond to a live failure; entries that don't (the mint
# moved on a line shift, was fixed, or renamed) are STALE and flagged
# — parity with check-no-reinterpret-cast.sh.  Without this, a
# line-shifting edit silently invalidates a grandfather entry and the
# guard cannot distinguish it from a genuine suppression.
live_fail_file="$(mktemp)"
trap 'rm -f "$live_fail_file"' EXIT

violation_count=0
scan_dirs=("$scan_root/include")

# Track sites that failed at least one check so we don't double-
# emit when both the candidate filter passes AND a check fails.

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

    # Skip preprocessor #include lines.  A re-export header routinely
    # documents the imported symbol in a trailing comment, e.g.
    #     #include <crucible/safety/ThreadName.h>  // mint_thread_name
    # The `mint_` token lives in the comment, not in a declaration —
    # the real definition is in the included header and is scanned
    # there.  Without this skip the `#include` line is a false
    # positive on all four axes.
    case "$stripped" in
        '#include'*|'#'[[:space:]]*'include'*) continue ;;
    esac

    # Skip lines whose `mint_` token appears ONLY inside a trailing
    # `//` comment.  A definition's `mint_<name>(` always precedes any
    # comment on the line; if every `mint_` occurrence sits after the
    # first `//`, the candidate is a documentation mention, not a
    # declaration.  (Block-comment `/* ... */` mentions are rarer and
    # the leading-`/*` case above already covers a comment that opens
    # the line.)
    code_part="${text%%//*}"
    case "$code_part" in
        *'mint_'*) ;;                 # real code occurrence — keep scanning
        *) continue ;;                # mint_ only after `//` — skip
    esac

    # Skip friend forwards — the attribute discipline lives on the
    # definition, not the friend re-declaration.  Scan the
    # candidate line PLUS the FOUR preceding non-empty lines:
    # friend declarations frequently split across the `friend`
    # keyword, the return type, and the declarator, e.g.
    #     template <...>            ← line N-3
    #     friend std::expected<     ← line N-2 (friend keyword here)
    #         Linear<Handle>, ...>  ← line N-1 (return type continues)
    #     mint_X(args) noexcept;    ← line N (candidate declarator)
    # so the `friend` keyword can sit up to three lines above the
    # declarator that carries the `mint_` token.
    is_friend_forward=0
    for friend_offset in 0 -1 -2 -3 -4; do
        friend_ln=$((line + friend_offset))
        (( friend_ln < 1 )) && break
        friend_text="$(sed -n "${friend_ln}p" "$file" 2>/dev/null || true)"
        case "$friend_text" in
            *'friend '*) is_friend_forward=1; break ;;
        esac
    done
    (( is_friend_forward == 1 )) && continue

    # Skip using-declarations — the underlying decl carries the
    # attributes; the using-decl inherits them.
    case "$text" in
        *'using '*'mint_'*) continue ;;
    esac

    # Skip deleted overloads — `= delete("...")` tombstones are not
    # factories; they exist only to produce a readable diagnostic when a
    # caller picks the wrong overload.  Structurally exempt from §XXI.
    if is_deleted_decl "$file" "$line"; then
        continue
    fi

    # Skip method-call / qualified-name call shapes:
    #   `.mint_X(`     — member call on an instance.
    #   `->mint_X(`    — member call through a pointer.
    #   `::mint_X(`    — namespace- or class-qualified call.
    # The factory DEFINITION has no qualifier prefix on its own
    # declarator (the qualifier appears AFTER the return type, on
    # the function name itself, not before `mint_`).
    case "$text" in
        *'.mint_'*|*'->mint_'*|*'::mint_'*) continue ;;
    esac

    # Skip call-expression shapes: the prefix BEFORE `mint_` on
    # this line betrays a return/assignment/argument-position call.
    prefix="${text%%mint_*}"
    case "$prefix" in
        *'return '*|*'= '*|*'co_return '*|*'co_yield '*) continue ;;
    esac
    # Skip call/argument-position: ANY `(` before the `mint_` token on
    # the same line means the mint is nested inside an outer call or a
    # condition (e.g. `static_assert(mint_fn(42)...)`, `foo(mint_x(...))`).
    # A factory DEFINITION never has a `(` before its own declarator —
    # the declarator's first `(` opens its parameter list.  Verified
    # against every live definition in include/ (none has a leading-paren
    # prefix), so this is a structural, not heuristic, skip.
    case "$prefix" in
        *'('*) continue ;;
    esac
    # Skip string-literal position: a `"` before the `mint_` token on
    # the same line means the token is text inside a string literal —
    # canonically a `static_assert(...)` diagnostic message that names
    # the mint it guards (e.g. `"mint_permission_split(ctx, ...) requires "`).
    # A factory DEFINITION never has a `"` before its declarator.
    # Verified against every live candidate in include/ (every
    # quote-prefixed occurrence is a diagnostic string, none a definition).
    case "$prefix" in
        *'"'*) continue ;;
    esac
    # Skip assignment-on-previous-line shapes: `auto x =`
    # continuation followed by `mint_X(...)` on the next line.
    # Detect by scanning ONE preceding non-comment line for a
    # dangling `= ` at end-of-line.
    prev_text="$(sed -n "$((line - 1))p" "$file" 2>/dev/null || true)"
    case "$prev_text" in
        *'='|*'= ') continue ;;
    esac

    # Inline suppression marker — exempts all four checks for this line.
    case "$text" in
        *'MINT-PATTERN-OK'*) continue ;;
    esac

    rel="${file#"$scan_root"/}"

    mint_name="$(printf '%s' "$text" | grep -oE 'mint_[a-z_]+' | head -1)"

    # ── Per-check enforcement ────────────────────────────────────────
    # Each check is independent and the candidate may fail multiple.
    # For every check that FAILS (allowlist-blind) record the
    # (rel:line:check) tuple in the live-failure set FIRST, then emit a
    # violation only when the failure is not allowlisted.  The live set
    # drives post-scan stale-entry detection.
    site_violations=()

    for check in nodiscard noexcept constexpr requires; do
        check_fails=0
        case "$check" in
            nodiscard) has_attribute_above   "$file" "$line" '[[nodiscard]]' || check_fails=1 ;;
            noexcept)  has_noexcept_in_signature "$file" "$line"             || check_fails=1 ;;
            constexpr) has_attribute_above   "$file" "$line" 'constexpr' \
                        || has_attribute_above "$file" "$line" 'consteval' \
                        || check_fails=1 ;;
            requires)  has_requires_if_templated "$file" "$line"             || check_fails=1 ;;
        esac
        (( check_fails == 0 )) && continue
        # Record BOTH the line-keyed and the name-keyed live-failure
        # tuples so post-scan stale detection can validate either entry
        # grammar.  The `@`-prefixed name key cannot collide with a
        # numeric line key.
        printf '%s:%s:%s\n' "$rel" "$line" "$check" >> "$live_fail_file"
        if [[ -n "$mint_name" ]]; then
            printf '%s:@%s:%s\n' "$rel" "$mint_name" "$check" >> "$live_fail_file"
        fi
        allowlisted_for "$rel" "$line" "$check" "$mint_name" || site_violations+=("$check")
    done

    if (( ${#site_violations[@]} > 0 )); then
        for axis in "${site_violations[@]}"; do
            printf 'MINT-PATTERN violation: %s:%s — %s missing %s.\n' \
                "$rel" "$line" "${mint_name:-mint_<unknown>}" "$axis" >&2
            violation_count=$((violation_count + 1))
        done
    fi
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

check-mint-pattern detected ${violation_count} §XXI drift site(s)
across four enforcement axes (nodiscard / noexcept / constexpr /
requires).  Each diagnostic line names ONE axis the site failed on;
a single site can fail multiple checks.

Remediations, in order of preference:

  (1) Fix the signature.  Add the missing attribute or requires
      clause directly.  This is the strongly preferred fix and
      matches CLAUDE.md §XXI.

  (2) Annotate the line with '// MINT-PATTERN-OK: <reason>' if
      the site is structurally exempt (passkey ctor, void-return
      sink, friend stub).

  (3) Per-check exemption: add 'path:line:<check>-ok' to
      scripts/mint-pattern-allowlist.txt where <check> is one of
      nodiscard / noexcept / constexpr / requires.  Use sparingly
      — every entry is a TODO.

  (4) Full exemption: add 'path:line' (no suffix) to
      scripts/mint-pattern-allowlist.txt for grandfathered §XXI-
      drift code awaiting a tracked migration.
HINT
    exit 1
fi

# ── Outcome (stale allowlist entries) ────────────────────────────────
# Every allowlist entry must correspond to a LIVE check-failure across
# either entry grammar:
#
#   LINE-KEYED  path:line[:check-ok]      — live iff the matching
#                                           (path:line[:check]) tuple is
#                                           in the live-failure set.
#   NAME-KEYED  path:mint_name[:check-ok] — live iff the matching
#                                           (path:@mint_name[:check])
#                                           tuple is in the live-failure
#                                           set.  Drift-proof: survives
#                                           the line shift that would
#                                           stale a line-keyed entry.
#
# Entries matching neither are STALE (the mint was fixed or renamed, or
# a line-keyed entry's line shifted) and must be pruned so the guard
# regains drift coverage at that site.
stale_count=0
if [[ -f "$allowlist" ]]; then
    sort -u "$live_fail_file" -o "$live_fail_file"
    while IFS= read -r entry; do
        trimmed="${entry%%#*}"
        trimmed="${trimmed%"${trimmed##*[![:space:]]}"}"
        trimmed="${trimmed#"${trimmed%%[![:space:]]*}"}"
        [[ -z "$trimmed" ]] && continue

        # Split into the base (path:key) and an optional `:check-ok`.
        if [[ "$trimmed" == *-ok ]]; then
            base="${trimmed%-ok}"              # path:key:check
            check_name="${base##*:}"           # check
            keyed="${base%:*}"                 # path:key
        else
            base="$trimmed"                    # path:key
            check_name=""
            keyed="$trimmed"
        fi
        # The key is the final segment of `path:key`; name-keyed entries
        # carry a `mint_`-prefixed identifier there, line-keyed entries a
        # bare number.  Distinguish on the prefix.
        key_seg="${keyed##*:}"                 # line number OR mint_name
        if [[ "$key_seg" == mint_* ]]; then
            # Name-keyed: rebuild the live-set lookup with the `@` prefix.
            rel_part="${keyed%:*}"             # path
            live_base="$rel_part:@$key_seg"    # path:@mint_name
        else
            live_base="$keyed"                 # path:line
        fi

        if [[ -n "$check_name" ]]; then
            # Per-check entry → live iff (live_base:check) ∈ live set.
            if ! grep -Fxq -- "$live_base:$check_name" "$live_fail_file" 2>/dev/null; then
                printf 'MINT-PATTERN stale: %s — STALE allowlist entry (no live %s failure for this key; the mint moved, was fixed, or renamed — remove from allowlist).\n' \
                    "$trimmed" "$check_name" >&2
                stale_count=$((stale_count + 1))
            fi
        else
            # All-checks entry → live iff ANY of the four fails.
            entry_is_live=0
            for chk in nodiscard noexcept constexpr requires; do
                if grep -Fxq -- "$live_base:$chk" "$live_fail_file" 2>/dev/null; then
                    entry_is_live=1
                    break
                fi
            done
            if (( entry_is_live == 0 )); then
                printf 'MINT-PATTERN stale: %s — STALE allowlist entry (no live failure for this key; the mint moved, was fixed, or renamed — remove from allowlist).\n' \
                    "$trimmed" >&2
                stale_count=$((stale_count + 1))
            fi
        fi
    done < "$allowlist"
fi

if [[ "$stale_count" -ne 0 ]]; then
    cat >&2 <<HINT

check-mint-pattern detected ${stale_count} stale allowlist entr(y/ies).
Each points at a (path:line[:check]) that no longer fails its §XXI
check — the mint moved (line shift), was fixed, or was renamed.  Prune
the stale entries from scripts/mint-pattern-allowlist.txt so the guard
regains drift coverage at those sites.  (This is the bug class that
silently invalidated 7 entries during the FIXY-U-101 line-collapse
sweep; this gate would have caught them automatically.)
HINT
    exit 2
fi

printf 'check-mint-pattern: clean — no §XXI drift on the four enforcement axes, no stale allowlist entries.\n' >&2
exit 0
