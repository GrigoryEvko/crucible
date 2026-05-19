#!/usr/bin/env bash
# check-mint-pattern.sh — §XXI Universal Mint Pattern compliance scanner
# (FIXY-U-003).
#
# CLAUDE.md §XXI rule: every cross-tier composition factory is
# named `mint_<noun>` and MUST be `[[nodiscard]] constexpr noexcept`
# unless the factory genuinely allocates (then `[[nodiscard]] noexcept`
# without constexpr).  The `[[nodiscard]]` attribute is the single
# most-frequently-forgotten piece of the §XXI discipline — drop it
# and a chained `mint_X<...>(...)` whose return value is unused
# silently compiles, defeating the construction-time validation that
# is the entire point of the pattern.
#
# This is Phase 1 of the §XXI scanner family.  Today's check:
#
#   * For every line matching `<...>constexpr<...>mint_<name>(`
#     that is NOT a friend forward, NOT a using-decl, NOT a
#     comment, NOT a method-call expression, the `[[nodiscard]]`
#     attribute MUST appear on the same line or on the immediately
#     preceding line.
#
# Phase 2 (deferred to U-003-followups) will add: missing `constexpr`
# on non-allocating mints, missing `noexcept`, missing single-concept
# `requires` clause, missing return-type concreteness.  Each phase
# extends the same allowlist + suppression-marker framework.
#
# Allowlist: scripts/mint-pattern-allowlist.txt (one
# repo-relative-path:line entry per line; lines beginning with `#`
# are comments).  Use sparingly — every entry is a TODO.
#
# Inline suppression: `// MINT-PATTERN-OK: <reason>` on the same
# line as the candidate signature.  Reserved for factories where
# `[[nodiscard]]` is genuinely impossible (e.g. void return, friend
# forwards that re-declare the same signature as the definition).
#
# Exempt directories: test/, bench/, examples/ — fixtures
# deliberately violate the pattern to demonstrate the rejection.
#
# Exit status:
#   0  — no §XXI drift detected
#   1  — at least one violation
#   2  — bad invocation / missing dependency

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-mint-pattern.sh — §XXI Universal Mint Pattern compliance scanner.

Usage:
  check-mint-pattern.sh              # scan; exit 1 on violation
  check-mint-pattern.sh --self-test  # plant a violation and verify catch
  check-mint-pattern.sh -h | --help  # usage

Suppression:
  // MINT-PATTERN-OK: <reason>   on the same line as the mint_X( signature.
  scripts/mint-pattern-allowlist.txt — one `path:line` entry per line.

Phase 1: checks `[[nodiscard]]` presence on candidate mint factory
definitions.  Phase 2 (deferred): missing constexpr / noexcept /
single-concept requires.
USAGE
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Plant a synthetic violation and re-run scoped to a temp
        # tree.  Mirrors check-fixy-discipline.sh + check-refined-
        # pre-subsumption.sh — a regex that never matches is a
        # placebo, not a guard.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/crucible/planted" "$tmp_root/scripts"
        cat >"$tmp_root/include/crucible/planted/planted_violation.h" <<'PLANTED'
#pragma once
// Synthetic §XXI MINT-PATTERN violation for --self-test verification.
namespace crucible::planted {
struct Token { int v = 0; };
struct Source { int s = 0; };

// Canonical mint passes the attribute check.
[[nodiscard]] constexpr Token mint_token_ok(Source const& src) noexcept {
    return Token{src.s};
}

// Drift case — attribute omitted, MUST be flagged by the scanner.
constexpr Token mint_token_drift(Source const& src) noexcept {
    return Token{src.s + 1};
}
}  // namespace crucible::planted
PLANTED
        : >"$tmp_root/scripts/mint-pattern-allowlist.txt"
        result_file="$(mktemp)"
        if CRUCIBLE_MINT_PATTERN_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file"; then
            printf 'check-mint-pattern: SELF-TEST FAILED — planted violation was not caught.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        if ! grep -q 'MINT-PATTERN violation.*mint_token_drift' "$result_file"; then
            printf 'check-mint-pattern: SELF-TEST FAILED — diagnostic missing mint name.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # Also confirm the ok case is NOT flagged (false-positive guard).
        if grep -q 'mint_token_ok' "$result_file"; then
            printf 'check-mint-pattern: SELF-TEST FAILED — false positive on mint_token_ok.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        rm -f "$result_file"
        printf 'check-mint-pattern: self-test passed — regex fires on drift, ignores canonical.\n' >&2
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

# ── Candidate-signature regex ─────────────────────────────────────────
# Match: `<...>constexpr<...>mint_<name>(` on a single line.  This is
# the canonical §XXI signature shape (return type and attribute prefix
# on the same line as the function name).  False positives are
# filtered out below; the multi-line form (template params on a prior
# line, signature wrap across lines) is intentionally NOT covered in
# Phase 1 — those sites get a [[nodiscard]] check via per-file review
# until Phase 2 of the scanner family lands.
candidate_pattern='\bconstexpr\b[^;{]*\bmint_[a-z_]+\s*\('

violation_count=0

# Only scan include/ — fixy/ + substrate.  Test fixtures + bench +
# examples are exempt because they deliberately demonstrate the
# rejection (HS14 / §XXI grep-discipline).  src/ has no mint factories.
scan_dirs=("$scan_root/include")

while IFS= read -r match; do
    file="${match%%:*}"
    rest="${match#*:}"
    line="${rest%%:*}"
    text="${rest#*:}"

    # Strip leading whitespace for prefix-pattern checks.
    stripped="${text#"${text%%[![:space:]]*}"}"

    # Skip C++ line comments — prose mentions of mint factory shape.
    case "$stripped" in
        '//'*|'///'*|'*'*|'/*'*) continue ;;
    esac

    # Skip friend forward declarations — the friend re-declares the
    # signature of a function defined elsewhere; the [[nodiscard]]
    # attribute belongs on the definition, not on the friend forward.
    case "$text" in
        *'friend '*) continue ;;
    esac

    # Skip using-declarations — the underlying decl carries the
    # attribute; the using-decl inherits it.
    case "$text" in
        *'using '*'mint_'*) continue ;;
    esac

    # Skip method-call expressions like `obj.mint_X(...)` or
    # `obj_->mint_X(...)`.  We're checking declarations, not calls.
    case "$text" in
        *'.mint_'*|*'->mint_'*|*'::mint_'*'('*'('*) continue ;;
    esac

    # Inline suppression marker.
    if [[ "$text" == *'MINT-PATTERN-OK'* ]]; then
        continue
    fi

    # The candidate IS a mint declaration line.  Check for
    # [[nodiscard]] on the same line OR the previous line.
    if [[ "$text" == *'[[nodiscard]]'* ]]; then
        continue
    fi
    prev_line=$((line - 1))
    if (( prev_line >= 1 )); then
        prev_text="$(sed -n "${prev_line}p" "$file" 2>/dev/null || true)"
        prev_stripped="${prev_text#"${prev_text%%[![:space:]]*}"}"
        case "$prev_stripped" in
            '//'*|'///'*|'*'*|'/*'*) ;;  # comment line; ignore for attr check
            *)
                if [[ "$prev_text" == *'[[nodiscard]]'* ]]; then
                    continue
                fi
                ;;
        esac
    fi

    rel="${file#"$scan_root"/}"

    # Per-line allowlist (`path:line` exact match).
    key="${rel}:${line}"
    if [[ -f "$allowlist" ]] && grep -Fxq -- "$key" "$allowlist"; then
        continue
    fi

    # Extract the mint name from the matched line for the diagnostic.
    mint_name="$(printf '%s' "$text" | grep -oE 'mint_[a-z_]+' | head -1)"
    printf 'MINT-PATTERN violation: %s:%s — %s missing [[nodiscard]].\n' \
        "$rel" "$line" "${mint_name:-mint_<unknown>}" >&2
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

check-mint-pattern detected ${violation_count} §XXI drift site(s).
Each site is a mint factory definition missing the [[nodiscard]]
attribute.  §XXI requires [[nodiscard]] constexpr noexcept on every
mint, because dropping [[nodiscard]] lets a chained mint expression
whose return value is unused silently compile — defeating the
construction-time-validation that is the entire point of the
Universal Mint Pattern.

Three remediations:

  (1) Add [[nodiscard]] to the mint signature.  This is the
      strongly preferred fix and matches CLAUDE.md §XXI.
  (2) If the mint is a genuine sink (e.g. ctor side-effect, void
      return), annotate the line with '// MINT-PATTERN-OK: <reason>'.
  (3) If the file is grandfathered §XXI-drift code awaiting a
      tracked migration, add 'path:line' to
      scripts/mint-pattern-allowlist.txt with a TODO referencing
      the migration task.  Trim the allowlist as sites migrate.
HINT
    exit 1
fi

printf 'check-mint-pattern: clean — no §XXI drift on [[nodiscard]] axis.\n' >&2
exit 0
