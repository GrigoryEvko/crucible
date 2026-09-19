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
        result_file="$(mktemp)"

        st_fail() {
            printf 'check-refined-pre-subsumption: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        }

        # Arm one, the clean controls.  Three shapes that must NOT fire:
        # a Refined parameter with no in-body cite at all; a bare
        # parameter carrying a cite it genuinely needs, because no type
        # proves it; and a cite on a DIFFERENT identifier than the
        # refined one.  A regex that has drifted into matching
        # everything is caught here and nowhere else.
        cat >"$tmp_root/include/crucible/planted_clean.h" <<'CLEAN'
#pragma once
// Synthetic CONTRACT-120 clean cases for --self-test verification.
namespace crucible::planted {
struct positive {};
template <typename P, typename T> struct Refined { T v_; };
// The type proves it and the body does not re-test it.
inline void clean_no_cite(Refined<positive, int> n) {
    (void)n;
}
// No refinement in the type, so the cite is load-bearing.
inline void clean_bare_param(int n) {
    CRUCIBLE_PRE(decide::positive(n));
    (void)n;
}
// The cite names a different identifier than the refined parameter.
inline void clean_other_identifier(Refined<positive, int> n, int m) {
    CRUCIBLE_PRE(decide::positive(m));
    (void)n;
    (void)m;
}
}  // namespace crucible::planted
CLEAN
        rc=0
        CRUCIBLE_REFINED_PRE_TEST_ROOT="$tmp_root" \
            bash "${BASH_SOURCE[0]}" 2>"$result_file" || rc=$?
        if [[ "$rc" -ne 0 ]]; then
            st_fail "a tree of clean shapes reported $rc, want 0"
        fi

        # Arm two, the suppression marker.  A genuine dead-weight cite
        # that carries CONTRACT-120-OK must be skipped, which is the
        # only exercise the suppression branch ever gets.
        cat >"$tmp_root/include/crucible/planted_marked.h" <<'MARKED'
#pragma once
// Synthetic CONTRACT-120 suppressed case for --self-test verification.
namespace crucible::planted {
struct positive {};
template <typename P, typename T> struct Refined { T v_; };
inline void marked_fn(Refined<positive, int> n) {
    // CONTRACT-120-OK: kept deliberately while the caller is migrated.
    CRUCIBLE_PRE(decide::positive(n));
    (void)n;
}
}  // namespace crucible::planted
MARKED
        rc=0
        CRUCIBLE_REFINED_PRE_TEST_ROOT="$tmp_root" \
            bash "${BASH_SOURCE[0]}" 2>"$result_file" || rc=$?
        if [[ "$rc" -ne 0 ]]; then
            st_fail "a cite marked CONTRACT-120-OK reported $rc, want 0"
        fi

        # Arm three, the violation.  Exactly one site is dead weight, so
        # the count pins the scan against a regex that fires twice on one
        # match or sweeps the clean neighbours in.
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
        rc=0
        CRUCIBLE_REFINED_PRE_TEST_ROOT="$tmp_root" \
            bash "${BASH_SOURCE[0]}" 2>"$result_file" || rc=$?
        if [[ "$rc" -eq 0 ]]; then
            st_fail "planted violation was not caught"
        fi
        # Confirm the diagnostic message names the predicate.
        if ! grep -q 'CONTRACT-120 violation.*positive' "$result_file"; then
            st_fail "diagnostic missing predicate name"
        fi
        if ! grep -q 'planted_violation\.h' "$result_file"; then
            st_fail "diagnostic missing the offending file"
        fi
        if grep -q 'planted_clean\.h' "$result_file"; then
            st_fail "a clean shape was flagged"
        fi
        if grep -q 'planted_marked\.h' "$result_file"; then
            st_fail "a CONTRACT-120-OK marked cite was flagged"
        fi
        violation_count="$(grep -c 'CONTRACT-120 violation' "$result_file" || true)"
        if [[ "$violation_count" -ne 1 ]]; then
            st_fail "expected exactly 1 violation, got $violation_count"
        fi
        rm -f "$result_file"
        printf 'check-refined-pre-subsumption: self-test passed — three clean shapes and a marked cite pass, exactly one dead-weight cite is caught.\n' >&2
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

if ! command -v jq >/dev/null 2>&1; then
    printf 'check-refined-pre-subsumption: jq is required\n' >&2
    exit 2
fi

common_globs=(--type=cpp \
              --glob '!build*/**' \
              --glob '!cmake-build-*/**' \
              --glob '!third_party/**' \
              --glob '!external/**' \
              --glob '!vendor/**' \
              --glob '!test/**' \
              --glob '!bench/**')

violation_count=0

# ── Per-predicate scan ───────────────────────────────────────────────
# Multi-line PCRE2: anchors on `Refined<\s*PRED\b` then captures the
# parameter name in group 1, then within a bounded window looks for a
# CRUCIBLE_PRE invocation that mentions both `decide::CITE` and the
# captured parameter name.
#
# The window is `(?:(?!\n\})[\s\S]){0,900}` — up to 900 characters,
# none of which may be a newline followed by a closing brace at column
# zero.  Both halves of that shape are load-bearing:
#
#   * the `(?!\n\})` lookahead stops the window at the end of the
#     function that declared the parameter.  A plain `[\s\S]{0,N}` gap
#     walks out of one function and into the next, so a clean
#     `Refined<positive, int> n` parameter in one function plus a
#     legitimate `CRUCIBLE_PRE(decide::positive(n))` in an unrelated
#     function that happens to reuse the name `n` reads as a
#     violation.  The --self-test plants exactly that pair.
#   * 900 is the largest round window that still compiles.  Bounded
#     repetition of a lookahead group expands in the compiled pattern,
#     and PCRE2 rejects 2000 with "regular expression is too large".
#
# The window is deliberately short either way — beyond a handful of
# nested scopes the heuristic loses precision; CONTRACT-120 is a
# best-effort signal, not a parser.
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
    pattern="Refined<\\s*${pred}\\b[^>]*>\\s+(\\w+)(?:(?!\\n\\})[\\s\\S]){0,900}?CRUCIBLE_PRE\\([^)]*\\bdecide::${cite_alt}\\b[^)]*\\b\\1\\b"

    # One record per MATCH, not per line.  rg's line-oriented output
    # prints every line a multi-line match spans, each with its own
    # `file:line:` prefix, so a `while read` loop over it sees one
    # record per line: the count then counts lines rather than sites,
    # and a CONTRACT-120-OK marker only suppresses the single line it
    # sits on.  The --json stream gives one object per match instead,
    # carrying the match's start line and the full matched text; jq
    # folds the newlines to spaces so one match is one record and the
    # marker check below sees the whole match.
    while IFS= read -r match; do
        # Match format: file:line:matched-text
        file="${match%%:*}"
        rest="${match#*:}"
        line="${rest%%:*}"
        # Full matched text — used to check for the suppression marker.
        text="${rest#*:}"

        # Suppression: skip if the match carries CONTRACT-120-OK on any
        # of its lines.  `text` is the whole match, so one marker
        # anywhere inside it suppresses the whole site.
        if [[ "$text" == *'CONTRACT-120-OK'* ]]; then
            continue
        fi

        rel="${file#"$scan_root"/}"
        printf 'CONTRACT-120 violation: %s:%s — Refined<%s, ...> param + CRUCIBLE_PRE re-tests predicate.\n' \
            "$rel" "$line" "$pred" >&2
        violation_count=$((violation_count + 1))
    done < <(
        rg -P --multiline --json \
           "${common_globs[@]}" \
           "$pattern" "$scan_root/include" "$scan_root/src" 2>/dev/null \
        | jq -r 'select(.type == "match")
                 | [ .data.path.text,
                     (.data.line_number | tostring),
                     (.data.lines.text | gsub("\n"; " ")) ]
                 | join(":")' 2>/dev/null || true
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
