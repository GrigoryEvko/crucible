#!/usr/bin/env bash
# check-refined-pre-subsumption.sh — CONTRACT-120 grep guard.
#
# Layer-C cooperative discipline: when a function takes a parameter
# typed `safety::Refined<Pred<...>, T>` (or a `using` alias of it),
# the predicate `Pred` is already proved at construction.  Any
# in-body `CRUCIBLE_PRE(decide::Pred(x))` (or `decide::is_Pred(x)`)
# on the SAME parameter is dead weight — the runtime check duplicates
# the type-level proof, and a future hardening (lifting `x` into a
# stronger Refined) won't be able to rely on the bare predicate
# disappearing because the cite remains.
#
# This is the third layer of the wrapper-discipline ratchet:
#   Layer A — zero-cost EBO collapse (the Graded substrate).
#   Layer B — DimensionTraits.h cross-product static_assert table
#             (compile-time misclassification rejection).
#   Layer C — this script.  Cooperative grep guard for source-text
#             patterns where a parser-equivalent rejection isn't
#             feasible without a full C++ frontend.
#
# Catches the canonical anti-pattern (best-effort match):
#     void fn(Refined<positive, int> n) { CRUCIBLE_PRE(decide::positive(n)); ... }
#                       ↑ already proved              ↑ dead weight
#
# Inputs scanned: include/ + src/.  test/ + bench/ are exempt because
# the cheat-probe and neg-compile fixtures DELIBERATELY express the
# anti-pattern to demonstrate the rejection.
#
# Suppression: a reviewer can annotate a deliberate defense-in-depth
# cite with the inline marker `// CONTRACT-120-OK:` on the same line
# as the CRUCIBLE_PRE invocation (e.g. when the Refined param's
# invariant has been moved through a non-typed path within the
# function body and re-establishment is intentional).  Lines with
# that marker are skipped.
#
# Exit status:
#   0  — no double-enforcement detected (or all marked OK)
#   1  — at least one violation
#   2  — bad invocation / missing dependency

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-refined-pre-subsumption.sh — Layer-C grep guard for CONTRACT-120.

Usage:
  check-refined-pre-subsumption.sh              # scan; exit 1 on violation
  check-refined-pre-subsumption.sh --list       # list scanned predicates
  check-refined-pre-subsumption.sh --self-test  # plant a violation and
                                                # verify the regex catches it
  check-refined-pre-subsumption.sh -h | --help  # usage

Suppression:
  // CONTRACT-120-OK: <reason>   on the same line as CRUCIBLE_PRE skips it.
USAGE
}

# ── Predicate catalog ────────────────────────────────────────────────
# (refined_pred_name, cite_pattern) — the cite pattern is the regex
# fragment matched against `decide::<...>` text inside CRUCIBLE_PRE.
# We accept either `decide::PRED` or `decide::is_PRED` because the
# Decide.h catalog uses both spellings depending on procedure.
predicates=(
    'positive|positive'
    'non_negative|non_negative'
    'non_zero|is_non_zero|non_zero'
    'bounded_above|in_range|bounded_above'
    'bounded_below|in_range|bounded_below'
    'in_range|in_range|all_in_range'
)

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --list)
        printf 'CONTRACT-120 scans these refined predicates:\n'
        for entry in "${predicates[@]}"; do
            printf '  %s\n' "${entry%%|*}"
        done
        exit 0
        ;;
    --self-test)
        # Plant a synthetic violation and re-run the script scoped to a
        # temp tree.  Failure here means the regex broke (e.g. a
        # ripgrep upgrade altered backreference semantics, or a
        # predicate name was added without a corresponding pattern).
        # This is the ONLY way to know the script's "clean" status is
        # informative — a regex that never matches anything is not a
        # guard, it is a placebo.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include/crucible" "$tmp_root/src"
        cat >"$tmp_root/include/crucible/planted_violation.h" <<'PLANTED'
#pragma once
// Synthetic CONTRACT-120 violation for --self-test verification.
namespace crucible::planted {
struct positive {};  // stand-in for safety::positive predicate
template <typename P, typename T> struct Refined { T v_; };
inline void planted_fn(Refined<positive, int> n) {
    CRUCIBLE_PRE(decide::positive(n));  // <- the dead-weight cite
    (void)n;
}
}  // namespace crucible::planted
PLANTED
        # Run the same scanner logic against the planted tree.  We
        # invoke ourselves recursively with the temp tree as the new
        # root via env-var override.
        result_file="$(mktemp)"
        if CRUCIBLE_REFINED_PRE_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file"; then
            printf 'check-refined-pre-subsumption: SELF-TEST FAILED — planted violation was not caught.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        # Confirm the diagnostic message names the predicate.
        if ! grep -q 'CONTRACT-120 violation.*positive' "$result_file"; then
            printf 'check-refined-pre-subsumption: SELF-TEST FAILED — diagnostic missing predicate name.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        rm -f "$result_file"
        printf 'check-refined-pre-subsumption: self-test passed — regex fires on planted violation.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-refined-pre-subsumption: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Override scan root for self-test ─────────────────────────────────
# When --self-test recurses into us, it points us at a temp tree.  In
# normal use the env var is unset and we scan the production tree.
scan_root="${CRUCIBLE_REFINED_PRE_TEST_ROOT:-$root}"

if ! command -v rg >/dev/null 2>&1; then
    printf 'check-refined-pre-subsumption: ripgrep (rg) is required\n' >&2
    exit 2
fi

common_globs=(--type=cpp \
              --glob '!build/**' \
              --glob '!cmake-build-*/**' \
              --glob '!third_party/**' \
              --glob '!external/**' \
              --glob '!vendor/**' \
              --glob '!test/**' \
              --glob '!bench/**')

violation_count=0

# ── Per-predicate scan ───────────────────────────────────────────────
# Multi-line PCRE2: anchors on `Refined<\s*PRED\b` then captures the
# parameter name in group 1, then within ≤ 2000 characters (covers
# the typical function body for instrumented hot paths) looks for
# CRUCIBLE_PRE invocation that mentions both `decide::CITE` and the
# captured parameter name.  The 2000-char window is intentionally
# small — beyond a handful of nested scopes the heuristic loses
# precision; CONTRACT-120 is a best-effort signal, not a parser.
#
# False-positive guards:
#   * the `bounded_above<N>, T> NAME` form requires the Refined
#     ctor's bounding parameter to be a proper template-arg list —
#     we tolerate `<` `>` nested by accepting `[^)]` inside the
#     param-list for one round of nesting.
#   * the in-body cite must reference the captured parameter name
#     literally (`\b\1\b`).  This eliminates noise from neighbouring
#     functions that happen to use `decide::PRED` on different
#     identifiers.
for entry in "${predicates[@]}"; do
    IFS='|' read -r pred cite_a cite_b <<<"$entry"
    cite_alt="$cite_a"
    if [[ -n "${cite_b:-}" ]]; then
        cite_alt="(?:${cite_a}|${cite_b})"
    fi

    # Multi-line regex.  rg --pcre2 supports backreferences.
    pattern="Refined<\\s*${pred}\\b[^>]*>\\s+(\\w+)[\\s\\S]{0,2000}?CRUCIBLE_PRE\\([^)]*\\bdecide::${cite_alt}\\b[^)]*\\b\\1\\b"

    while IFS= read -r match; do
        # Match format: file:line:matched-text
        file="${match%%:*}"
        rest="${match#*:}"
        line="${rest%%:*}"
        # Full matched text — used to check for the suppression marker.
        text="${rest#*:}"

        # Suppression: skip if the match contains CONTRACT-120-OK on
        # any of its lines.  rg --multiline makes `match` span lines;
        # the marker check is therefore a substring search on `text`.
        if [[ "$text" == *'CONTRACT-120-OK'* ]]; then
            continue
        fi

        rel="${file#"$scan_root"/}"
        printf 'CONTRACT-120 violation: %s:%s — Refined<%s, ...> param + CRUCIBLE_PRE re-tests predicate.\n' \
            "$rel" "$line" "$pred" >&2
        violation_count=$((violation_count + 1))
    done < <(
        rg -nP --multiline --pcre2 \
           --no-heading \
           "${common_globs[@]}" \
           "$pattern" "$scan_root/include" "$scan_root/src" 2>/dev/null || true
    )
done

# ── Outcome ──────────────────────────────────────────────────────────
if [[ "$violation_count" -ne 0 ]]; then
    cat >&2 <<HINT

CONTRACT-120 detected ${violation_count} double-enforcement site(s).
Each site has a parameter typed safety::Refined<Pred, T> AND an
in-body CRUCIBLE_PRE that re-tests the same predicate on the same
parameter.  The Refined ctor already discharges the predicate at
construction; the in-body cite is dead weight.

Three remediations:

  (1) Drop the CRUCIBLE_PRE.  The type already carries the proof.
  (2) Lift the cite to a dependent invariant (e.g., a relation
      between two parameters that the type system can't carry).
  (3) Annotate the cite with '// CONTRACT-120-OK: <reason>' on the
      same line if the re-test is genuinely defense-in-depth (the
      param's invariant has been threaded through an untyped path
      and re-establishment is intentional).
HINT
    exit 1
fi

printf 'check-refined-pre-subsumption: clean — no double-enforcement detected.\n' >&2
exit 0
