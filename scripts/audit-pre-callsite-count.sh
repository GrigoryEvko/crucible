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
#   * Per-decide-procedure cite count (the cite-ratio audit reads it —
#     every Decide procedure should accumulate ≥ 2 cites within 6 months
#     of its introduction, and an unloved one is trimmed)
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

# ── Decide catalog ────────────────────────────────────────────────────
# Catalog of named predicates in safety/Decide.h.  Declared ABOVE the
# argument dispatcher so --self-test can synthesize a fixture that cites
# EVERY procedure.  A fixture built from this same array cannot desync
# from the catalog when a procedure is trimmed or a migration adds
# one.  Counts are computed fresh each run; audit-decide-cite-ratio.sh
# audits the ratios (every procedure > 0 cites at 6mo), and the unloved
# ones are trimmed.
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

usage() {
    cat >&2 <<'USAGE'
audit-pre-callsite-count.sh — count contracts-infra adoption in include/ + src/.

Usage:
  audit-pre-callsite-count.sh                 # human summary
  audit-pre-callsite-count.sh --json          # JSON to stdout
  audit-pre-callsite-count.sh --baseline F    # write JSON snapshot to F
  audit-pre-callsite-count.sh --check F       # compare to F; nonzero on regress
  audit-pre-callsite-count.sh --self-test     # plant a regression, verify catch
  audit-pre-callsite-count.sh -h | --help     # usage
USAGE
}

mode="human"
baseline_path=""
case "${1:-}" in
    --self-test)
        # A regression gate that never plants a regression has never
        # demonstrated it fires.  Build a synthetic production tree, then
        # drive --check against it THREE times:
        #
        #   1. baseline counts ABOVE the tree's  -> every counter regresses
        #                                          -> MUST exit non-zero
        #   2. baseline counts EQUAL to the tree -> nothing moved
        #                                          -> MUST exit 0
        #   3. baseline counts BELOW the tree    -> adoption grew
        #                                          -> MUST exit 0 (silent)
        #
        # Directions 1 and 2 are the two halves of the gate; direction 3
        # proves growth stays silent rather than tripping the guard.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/include" "$tmp_root/src"

        # Every counted pattern must match at least once.  `rg` exits 1 on
        # no-match and `set -o pipefail` turns that into a script abort, so
        # a fixture missing ONE form would look like a self-test failure
        # rather than the missing form it is.  The decide:: cites are
        # generated from decide_procedures above, two lines apiece — the
        # per-procedure regex is line-anchored, so two cites sharing a line
        # would count as one.
        fixture="$tmp_root/include/selftest_cites.h"
        cat >"$fixture" <<'FIXTURE'
#pragma once
// Synthetic contracts-infra fixture for --self-test.
namespace crucible::selftest {
inline void planted_macro_forms(int n) {
    CRUCIBLE_PRE(n > 0);
    CRUCIBLE_PRE_FAST(n > 0);
    CRUCIBLE_PRE_MSG(n > 0, "synthetic");
    CRUCIBLE_POST(r, r > 0);
    CRUCIBLE_POST_FAST(r, r > 0);
    CRUCIBLE_POST_MSG(r, r > 0, "synthetic");
    contract_assert(n > 0);
}
inline int planted_p2900_forms(int const n)
    pre (n > 0)
    post (r: r > 0)
{ return n; }
inline void planted_decide_cites(int n) {
FIXTURE
        for proc in "${decide_procedures[@]}"; do
            printf '    decide::%s(n);\n' "$proc" >>"$fixture"
            printf '    decide::%s(n);\n' "$proc" >>"$fixture"
        done
        cat >>"$fixture" <<'FIXTURE_TAIL'
}
}  // namespace crucible::selftest
FIXTURE_TAIL
        # src/ must exist and hold a cpp-typed file: the scan passes both
        # include/ and src/ to rg, and a missing path is an rg error.
        cat >"$tmp_root/src/selftest_anchor.cpp" <<'ANCHOR'
// Synthetic translation unit for --self-test.
int crucible_selftest_anchor = 0;
ANCHOR

        checked_fields=(crucible_pre crucible_pre_fast crucible_pre_msg
                        crucible_post crucible_post_fast crucible_post_msg
                        contract_assert decide_total
                        total_pre_cites total_post_cites total_contract_cites)

        # Direction 2's baseline IS the tree's own snapshot — an exact match
        # by construction, which is what "no regression" must accept.
        match_baseline="$tmp_root/baseline_match.json"
        if ! CRUCIBLE_PRE_CALLSITE_TEST_ROOT="$tmp_root" \
             bash "${BASH_SOURCE[0]}" --json >"$match_baseline" 2>"$tmp_root/json.err"; then
            printf 'audit-pre-callsite-count: SELF-TEST FAILED — --json aborted on the synthetic tree.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$tmp_root/json.err")" >&2
            exit 2
        fi

        # Direction 1: a baseline claiming counts the tree cannot meet.
        high_baseline="$tmp_root/baseline_high.json"
        { printf '{'
          high_first=1
          for field in "${checked_fields[@]}"; do
              if [[ $high_first -eq 0 ]]; then printf ','; fi
              high_first=0
              printf '"%s":9999' "$field"
          done
          printf '}\n'
        } >"$high_baseline"

        # Direction 3: a baseline the tree has already grown past.
        low_baseline="$tmp_root/baseline_low.json"
        { printf '{'
          low_first=1
          for field in "${checked_fields[@]}"; do
              if [[ $low_first -eq 0 ]]; then printf ','; fi
              low_first=0
              printf '"%s":0' "$field"
          done
          printf '}\n'
        } >"$low_baseline"

        # ── Direction 1 — MUST fail ──────────────────────────────────
        regress_err="$tmp_root/regress.err"
        if CRUCIBLE_PRE_CALLSITE_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" --check "$high_baseline" \
           >/dev/null 2>"$regress_err"; then
            printf 'audit-pre-callsite-count: SELF-TEST FAILED — planted regression not caught.\n' >&2
            printf '── checker stderr ───\n%s\n────────────────────\n' \
                "$(cat "$regress_err")" >&2
            exit 2
        fi
        # Non-zero is not enough: prove it failed for the REGRESSION reason
        # and not because the scan blew up on the synthetic tree.
        for field in "${checked_fields[@]}"; do
            if ! grep -qF "REGRESSION ${field}: 9999 ->" "$regress_err"; then
                printf 'audit-pre-callsite-count: SELF-TEST FAILED — no REGRESSION diagnostic for %s.\n' \
                    "$field" >&2
                printf '── checker stderr ───\n%s\n────────────────────\n' \
                    "$(cat "$regress_err")" >&2
                exit 2
            fi
        done

        # ── Direction 2 — equal counts MUST pass ─────────────────────
        match_err="$tmp_root/match.err"
        if ! CRUCIBLE_PRE_CALLSITE_TEST_ROOT="$tmp_root" \
             bash "${BASH_SOURCE[0]}" --check "$match_baseline" \
             >/dev/null 2>"$match_err"; then
            printf 'audit-pre-callsite-count: SELF-TEST FAILED — exact-match baseline reported a regression.\n' >&2
            printf '── checker stderr ───\n%s\n────────────────────\n' \
                "$(cat "$match_err")" >&2
            exit 2
        fi

        # ── Direction 3 — growth MUST stay silent ────────────────────
        grow_err="$tmp_root/grow.err"
        if ! CRUCIBLE_PRE_CALLSITE_TEST_ROOT="$tmp_root" \
             bash "${BASH_SOURCE[0]}" --check "$low_baseline" \
             >/dev/null 2>"$grow_err"; then
            printf 'audit-pre-callsite-count: SELF-TEST FAILED — adoption growth tripped the gate.\n' >&2
            printf '── checker stderr ───\n%s\n────────────────────\n' \
                "$(cat "$grow_err")" >&2
            exit 2
        fi

        printf 'audit-pre-callsite-count: self-test passed — regression caught (%d counters), exact-match and growth both accepted.\n' \
            "${#checked_fields[@]}" >&2
        exit 0
        ;;
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

# ── Scan-root override for --self-test recursion ─────────────────────
# The script's OWN location still resolves through BASH_SOURCE above;
# only the tree it counts moves.
scan_root="${CRUCIBLE_PRE_CALLSITE_TEST_ROOT:-$root}"

# ── Aggregate counters ────────────────────────────────────────────────
# rg --type=cpp picks up .h / .hpp / .cpp / .cc — all the production
# C++26 sources.  --glob excludes vendor + build trees.
common_globs=(--type=cpp \
              --glob '!build*/**' \
              --glob '!cmake-build-*/**' \
              --glob '!third_party/**' \
              --glob '!external/**' \
              --glob '!vendor/**' \
              --glob '!test/**' \
              --glob '!bench/**')

count_pattern() {
    # Count OCCURRENCES, not lines.  `rg -c` reports one line per file with at
    # least one match, so two cites sharing a line count as one and the total
    # moves whenever a reformat joins or splits lines.  `rg -o` emits one line
    # per match, which is the quantity this audit actually claims to report.
    #
    # Count CODE, not prose.  A cite named in a comment is not adoption, and
    # counting it makes the metric track how much the tree talks about
    # contracts rather than how much it uses them.  Two guards do that: the
    # line must not open as a comment, and no `//` may precede the match on
    # the line.  A cite named inside a multi-line block comment still counts,
    # which is the residual this cheap form accepts.
    #
    # The two paragraphs above describe the intent.  A single `rg -oP` with
    # `^(?!...)(?:(?!//).)*?\K<pattern>` did NOT implement it: the `^` anchor
    # can match only once per line, so the lazy prefix plus `\K` yields the
    # FIRST cite on a line and no more.  That silently reinstated the
    # line-counting the doc-block disclaims, and it stayed invisible until a
    # clang-format pass joined two `pre(` clauses in RefreshDaemon.h onto one
    # line — the file kept both contracts, both still abort on violation, and
    # the audit reported a regression from 2 to 1.  A metric a reformat can
    # move is not measuring adoption.
    #
    # Three stages instead, so the line filter and the occurrence count are
    # separate concerns: select lines that do not open as a comment, cut each
    # line at its first `//`, then count occurrences in what is left.
    local pattern="$1"
    local total=0
    total=$(
        rg -N --no-filename -P "^(?!\s*(?://|\*|/\*)).*${pattern}" "${common_globs[@]}" \
           "$scan_root/include" "$scan_root/src" 2>/dev/null \
        | rg --passthru -P '//.*$' -r '' \
        | rg -oP "${pattern}" \
        | wc -l
    )
    printf '%s' "$total"
}

# ── Counts ────────────────────────────────────────────────────────────
# Patterns are anchored to avoid false positives:
#   CRUCIBLE_PRE\b — word boundary so CRUCIBLE_PRE_FAST / CRUCIBLE_PRE_MSG
#                    are counted separately (they're variants of the same
#                    cite class but distinct mechanisms).
#   (?<![\w:])pre\s*\( — a pre() clause anywhere on the line.  Anchoring to
#                    line start was wrong: whether the clause shares a line
#                    with the signature is a formatting choice, not a change
#                    in contract adoption.  The lookbehind excludes both
#                    identifiers ending in "pre" (`prepare`) and qualified
#                    names (`ns::pre`).
#   contract_assert\b — same boundary discipline.
crucible_pre=$(count_pattern 'CRUCIBLE_PRE\b')
crucible_pre_fast=$(count_pattern 'CRUCIBLE_PRE_FAST\b')
crucible_pre_msg=$(count_pattern 'CRUCIBLE_PRE_MSG\b')
crucible_post=$(count_pattern 'CRUCIBLE_POST\b')
crucible_post_fast=$(count_pattern 'CRUCIBLE_POST_FAST\b')
crucible_post_msg=$(count_pattern 'CRUCIBLE_POST_MSG\b')

p2900_pre=$(count_pattern '(?<![\w:])pre\s*\(')
p2900_post=$(count_pattern '(?<![\w:])post\s*\(')
contract_assert=$(count_pattern 'contract_assert\b')

decide_total=$(count_pattern 'decide::')

total_pre_cites=$((crucible_pre + crucible_pre_fast + crucible_pre_msg + p2900_pre))
total_post_cites=$((crucible_post + crucible_post_fast + crucible_post_msg + p2900_post))
total_contract_cites=$((total_pre_cites + total_post_cites + contract_assert))

# ── Per-decide-procedure cite count ───────────────────────────────────
# The catalog itself (decide_procedures) is declared near the top of the
# script, above the argument dispatcher, so --self-test can build its
# fixture from the same array.

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
        # the docstring cross-reference discipline.
        # -o counts occurrences; -c would count lines and drop a cite whenever
        # two land on one line.
        local n=0
        n=$(
            rg -oP "^(?!\s*(?://|\*|/\*))(?:(?!//).)*?\Kdecide::${proc}\b" "${common_globs[@]}" \
               --glob '!include/crucible/safety/_Decide.h' \
               "$scan_root/include" "$scan_root/src" 2>/dev/null | wc -l
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
       "$scan_root/include" "$scan_root/src" 2>/dev/null \
       | sort -t: -k2 -nr -s \
       | head -n "$top_n" \
       | while IFS=: read -r file count; do
           rel="${file#"$scan_root"/}"
           printf '  %-60s %s\n' "$rel" "$count"
         done

    cat <<FOOTER

── Notes ─────────────────────────────────────────────
  Per CLAUDE.md §XII: prefer Refined<P, T> parameter types over pre()
  cites where the predicate is structurally provable (the subsumption
  discipline of check-refined-pre-subsumption.sh).  Prefer named
  decide::* cites over anonymous CRUCIBLE_PRE expressions where a
  catalog entry fits.

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
        # -o counts occurrences; see the human-summary loop above.
        local n=0
        n=$(
            rg -oP "^(?!\s*(?://|\*|/\*))(?:(?!//).)*?\Kdecide::${proc}\b" "${common_globs[@]}" \
               --glob '!include/crucible/safety/_Decide.h' \
               "$scan_root/include" "$scan_root/src" 2>/dev/null | wc -l
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
        # Diff strategy: any checked counter that DECREASED is flagged;
        # increases are silent (adoption growth is good; regression is the
        # signal).
        #
        # The legacy P2900 forms (p2900_pre / p2900_post) are deliberately
        # EXCLUDED from the must-not-decrease set: the codebase is actively
        # migrating vanilla `pre()` / `post (r:...)` to the stronger
        # CRUCIBLE_PRE / CRUCIBLE_POST macros (consteval-firing, toolchain-
        # independent).  That migration
        # MOVES a cite from p2900_pre to crucible_pre, so p2900_pre shrinks by
        # design.  Genuine coverage loss (a pre() deleted, not migrated) still
        # trips the AGGREGATE guards (total_pre_cites / total_post_cites /
        # total_contract_cites), which net out the migration and only fall when
        # real coverage drops.  The preferred crucible_* forms stay in the set
        # (a drop there IS a real regression).
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
                     contract_assert \
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
