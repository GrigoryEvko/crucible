#!/usr/bin/env bash
# audit-pre-callsite-count.sh — contracts-infra adoption metric tracker.
#
# Walks the production tree (include/ + src/) and counts:
#   * CRUCIBLE_PRE    — in-body precondition cite (consteval-bypass shim)
#   * CRUCIBLE_POST   — in-body postcondition cite
#   * pre()           — P2900 precondition clause (parser-position)
#   * post()          — P2900 postcondition clause
#   * contract_assert — mid-body invariant
#   * decide::*       — named-predicate cite (catalog discharge)
#
# Output:
#   * Aggregate counts across the tree
#   * Per-decide-procedure cite count (CONTRACT-125 audit alignment —
#     every Decide procedure should accumulate ≥ 2 cites within 6 months
#     of CONTRACT-100..127 migrations; CONTRACT-126 trims unloved ones)
#   * Top-10 files by combined contract-cite density (signal of
#     where the boundary discipline is concentrated; surfaces files
#     under-served by the discipline)
#
# Modes:
#   default          — human-readable summary to stdout
#   --json           — single JSON object for machine ingestion (CI baselines)
#   --baseline FILE  — write JSON snapshot to FILE for diff tracking
#   --check FILE     — compare current counts to a saved baseline; non-zero
#                      exit if a counter regressed (decreased without
#                      explanation), zero otherwise
#
# Mirrors scripts/check-trait-injection.sh in shell idioms (ripgrep-only,
# set -euo pipefail, no awk/sed) per the user's tool preferences.
#
# Exit status:
#   0  — successful audit (or --check pass)
#   1  — --check detected regression
#   2  — bad invocation / missing dependency

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
audit-pre-callsite-count.sh — count contracts-infra adoption in include/ + src/.

Usage:
  audit-pre-callsite-count.sh                 # human summary
  audit-pre-callsite-count.sh --json          # JSON to stdout
  audit-pre-callsite-count.sh --baseline F    # write JSON snapshot to F
  audit-pre-callsite-count.sh --check F       # compare to F; nonzero on regress
USAGE
}

mode="human"
baseline_path=""
case "${1:-}" in
    --json)
        mode="json"
        ;;
    --baseline)
        if [[ $# -lt 2 ]]; then usage; exit 2; fi
        mode="baseline"
        baseline_path="$2"
        ;;
    --check)
        if [[ $# -lt 2 ]]; then usage; exit 2; fi
        mode="check"
        baseline_path="$2"
        ;;
    -h|--help)
        usage; exit 0
        ;;
    "")
        ;;
    *)
        printf 'audit-pre-callsite-count: unknown argument: %s\n' "$1" >&2
        usage; exit 2
        ;;
esac

if ! command -v rg >/dev/null 2>&1; then
    printf 'audit-pre-callsite-count: ripgrep (rg) is required\n' >&2
    exit 2
fi

# ── Aggregate counters ────────────────────────────────────────────────
# rg --type=cpp picks up .h / .hpp / .cpp / .cc — all the production
# C++26 sources.  --glob excludes vendor + build trees.
common_globs=(--type=cpp \
              --glob '!build/**' \
              --glob '!cmake-build-*/**' \
              --glob '!third_party/**' \
              --glob '!external/**' \
              --glob '!vendor/**' \
              --glob '!test/**' \
              --glob '!bench/**')

count_pattern() {
    # Sum match counts across all matching files.  rg -c emits one
    # `file:count` line per file with ≥ 1 match; we strip the file
    # prefix with `cut -d:` (POSIX, not awk/sed) and accumulate in
    # bash arithmetic.  No matches → empty stdout → total stays 0.
    local pattern="$1"
    local total=0 line n
    while IFS= read -r line; do
        n="${line##*:}"
        total=$((total + n))
    done < <(
        rg -cP "$pattern" "${common_globs[@]}" \
           "$root/include" "$root/src" 2>/dev/null || true
    )
    printf '%s' "$total"
}

# ── Counts ────────────────────────────────────────────────────────────
# Patterns are anchored to avoid false positives:
#   CRUCIBLE_PRE\b — word boundary so CRUCIBLE_PRE_FAST / CRUCIBLE_PRE_MSG
#                    are counted separately (they're variants of the same
#                    cite class but distinct mechanisms).
#   ^\s*pre\s*\(   — parser-position pre() at line start (after whitespace);
#                    excludes occurrences in identifiers like `prepare`.
#   contract_assert\b — same boundary discipline.
crucible_pre=$(count_pattern 'CRUCIBLE_PRE\b')
crucible_pre_fast=$(count_pattern 'CRUCIBLE_PRE_FAST\b')
crucible_pre_msg=$(count_pattern 'CRUCIBLE_PRE_MSG\b')
crucible_post=$(count_pattern 'CRUCIBLE_POST\b')
crucible_post_fast=$(count_pattern 'CRUCIBLE_POST_FAST\b')
crucible_post_msg=$(count_pattern 'CRUCIBLE_POST_MSG\b')

p2900_pre=$(count_pattern '^\s*pre\s*\(')
p2900_post=$(count_pattern '^\s*post\s*\(')
contract_assert=$(count_pattern 'contract_assert\b')

decide_total=$(count_pattern 'decide::')

total_pre_cites=$((crucible_pre + crucible_pre_fast + crucible_pre_msg + p2900_pre))
total_post_cites=$((crucible_post + crucible_post_fast + crucible_post_msg + p2900_post))
total_contract_cites=$((total_pre_cites + total_post_cites + contract_assert))

# ── Per-decide-procedure cite count ───────────────────────────────────
# Catalog of named predicates in safety/Decide.h.  Counts are computed
# fresh each run; CONTRACT-125 audits ratios (every procedure > 0 cites
# at 6mo); CONTRACT-126 trims unloved ones.
decide_procedures=(
    is_non_zero
    in_range
    all_in_range
    aligned_in_range
    no_overflow_mul
    no_overflow_sum
    no_overflow_pow2_shift
    is_power_of_two_le
    factorization_eq
    coprime
    intervals_pairwise_disjoint
    intervals_cover_unit
    tier_replaces
    row_subset
    fmix_preserves_non_zero
    strictly_increasing
    weakly_increasing
    conjunction
    disjunction
    implies
    positive
    non_negative
    valid_span
)

# ── Top-N file density ────────────────────────────────────────────────
# Files with the most combined contract cites — the "boundary
# discipline frontier" — useful for spotting headers that have absorbed
# the migration sweep vs. ones still holding raw assertions.
top_n=10

print_human() {
    cat <<HEADER
=== Crucible contracts-infra adoption metrics ===
(production tree: include/ + src/, excludes test/ bench/ build/ third_party/)

── Aggregate ─────────────────────────────────────────
  CRUCIBLE_PRE        $crucible_pre
  CRUCIBLE_PRE_FAST   $crucible_pre_fast
  CRUCIBLE_PRE_MSG    $crucible_pre_msg
  CRUCIBLE_POST       $crucible_post
  CRUCIBLE_POST_FAST  $crucible_post_fast
  CRUCIBLE_POST_MSG   $crucible_post_msg
  pre()  (P2900)      $p2900_pre
  post() (P2900)      $p2900_post
  contract_assert     $contract_assert
  decide:: cites      $decide_total

  Total pre-cites     $total_pre_cites
  Total post-cites    $total_post_cites
  Total contract-cites $total_contract_cites

── Per-decide-procedure cite count ──────────────────
HEADER
    for proc in "${decide_procedures[@]}"; do
        # We count `decide::PROC` outside of the Decide.h definition
        # itself (which contains the canonical declarations).  Excluding
        # safety/Decide.h gives the "production cite" count, mirroring
        # the CONTRACT-124 docstring cross-reference discipline.
        local n=0 line
        while IFS= read -r line; do
            n=$((n + ${line##*:}))
        done < <(
            rg -cP "decide::${proc}\b" "${common_globs[@]}" \
               --glob '!include/crucible/safety/Decide.h' \
               "$root/include" "$root/src" 2>/dev/null || true
        )
        printf '  %-30s %s\n' "decide::$proc" "$n"
    done

    cat <<MIDDLE

── Top-$top_n files by contract-cite density ─────────
MIDDLE

    # Build the per-file cite count: pre + post + contract_assert.
    # rg -c gives "file:count" lines; we sort by count desc, take top N.
    rg -cP '(CRUCIBLE_PRE|CRUCIBLE_POST|^\s*pre\s*\(|^\s*post\s*\(|contract_assert)\b' \
       "${common_globs[@]}" \
       "$root/include" "$root/src" 2>/dev/null \
       | sort -t: -k2 -nr -s \
       | head -n "$top_n" \
       | while IFS=: read -r file count; do
           rel="${file#"$root"/}"
           printf '  %-60s %s\n' "$rel" "$count"
         done

    cat <<FOOTER

── Notes ─────────────────────────────────────────────
  Per CLAUDE.md §XII: prefer Refined<P, T> parameter types over pre()
  cites where the predicate is structurally provable (CONTRACT-120
  subsumption discipline).  Prefer named decide::* cites over anonymous
  CRUCIBLE_PRE expressions where a catalog entry fits (CONTRACT-100..127
  rebrand discipline).

  Run with --json for machine-readable output.
  Run with --baseline FILE to snapshot for CI diff tracking.
  Run with --check FILE to flag regressions against a baseline.
FOOTER
}

print_json() {
    # Emit a flat JSON object.  Field ordering chosen so a diff between
    # two snapshots reads naturally: aggregates first, per-procedure
    # next, top-files last.  No external jq dependency — printf builds
    # the bytes directly.
    printf '{'
    printf '"crucible_pre":%s,' "$crucible_pre"
    printf '"crucible_pre_fast":%s,' "$crucible_pre_fast"
    printf '"crucible_pre_msg":%s,' "$crucible_pre_msg"
    printf '"crucible_post":%s,' "$crucible_post"
    printf '"crucible_post_fast":%s,' "$crucible_post_fast"
    printf '"crucible_post_msg":%s,' "$crucible_post_msg"
    printf '"p2900_pre":%s,' "$p2900_pre"
    printf '"p2900_post":%s,' "$p2900_post"
    printf '"contract_assert":%s,' "$contract_assert"
    printf '"decide_total":%s,' "$decide_total"
    printf '"total_pre_cites":%s,' "$total_pre_cites"
    printf '"total_post_cites":%s,' "$total_post_cites"
    printf '"total_contract_cites":%s,' "$total_contract_cites"

    printf '"decide_per_procedure":{'
    local first=1
    for proc in "${decide_procedures[@]}"; do
        local n=0 line
        while IFS= read -r line; do
            n=$((n + ${line##*:}))
        done < <(
            rg -cP "decide::${proc}\b" "${common_globs[@]}" \
               --glob '!include/crucible/safety/Decide.h' \
               "$root/include" "$root/src" 2>/dev/null || true
        )
        if [[ $first -eq 0 ]]; then printf ','; fi
        first=0
        printf '"%s":%s' "$proc" "$n"
    done
    printf '}'

    printf '}\n'
}

case "$mode" in
    human)
        print_human
        ;;
    json)
        print_json
        ;;
    baseline)
        print_json > "$baseline_path"
        printf 'audit-pre-callsite-count: baseline written to %s\n' "$baseline_path" >&2
        ;;
    check)
        # Diff strategy: any aggregate counter that DECREASED without
        # cause is flagged.  Per-procedure counters are checked the
        # same way.  Increases are silent.  This is asymmetric on
        # purpose — adoption growth is good; regression is the signal.
        if [[ ! -f "$baseline_path" ]]; then
            printf 'audit-pre-callsite-count: baseline file not found: %s\n' \
                   "$baseline_path" >&2
            exit 2
        fi
        current_json="$(print_json)"
        # Field-by-field comparison.  We use printf+rg rather than jq to
        # keep the script self-contained — same dep set as
        # check-trait-injection.sh.
        regressed=0
        for field in crucible_pre crucible_pre_fast crucible_pre_msg \
                     crucible_post crucible_post_fast crucible_post_msg \
                     p2900_pre p2900_post contract_assert \
                     decide_total total_pre_cites total_post_cites \
                     total_contract_cites; do
            # Tolerate either compact `"field":42` or pretty `"field": 42`
            # JSON formatting — third-party tools (jq, python json.dump
            # default) emit the latter; our own writer emits the former.
            old="$(rg -oP "\"${field}\":\s*\K[0-9]+" "$baseline_path" \
                    | head -n1 || printf '0')"
            new="$(printf '%s' "$current_json" \
                    | rg -oP "\"${field}\":\s*\K[0-9]+" \
                    | head -n1 || printf '0')"
            if [[ "$new" -lt "$old" ]]; then
                printf 'audit-pre-callsite-count: REGRESSION %s: %s -> %s\n' \
                       "$field" "$old" "$new" >&2
                regressed=1
            fi
        done
        if [[ "$regressed" -ne 0 ]]; then
            printf 'audit-pre-callsite-count: contracts-infra adoption regressed; investigate.\n' >&2
            exit 1
        fi
        printf 'audit-pre-callsite-count: no regression vs %s\n' \
               "$baseline_path" >&2
        ;;
esac
