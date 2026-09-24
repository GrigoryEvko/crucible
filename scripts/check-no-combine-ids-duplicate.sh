#!/usr/bin/env bash
# check-no-combine-ids-duplicate.sh — combine_ids drift-defense guard.
#
# Crucible's row_hash machinery folds via a SINGLE function.  That is
# the new tree's one Graded fold in foundation/diag/RowHash.h, and the
# old tree's _RowHashFold.h with its per-wrapper
# row_hash_contribution<W> specializations, which stands until the old
# tree is deleted.  Both reach:
#
#   include/foundation/reflect/Hash.h
#     [[nodiscard]] constexpr uint64_t combine_ids(
#         uint64_t a, uint64_t b) noexcept
#
# Cited by SYMBOL, not by line.  This guard shipped citing ":166" and
# the definition has since moved to :73 — prose keyed to a line number
# rots the moment anything above it changes, and this one rotted
# unnoticed for months because the guard was registered nowhere and so
# never ran.  Both halves of that are fixed: the citation is now
# line-free, and the guard is wired into ctest + CI.
#
# Two copies of the same body sit in the frozen old tree until the
# sibling-tree extraction retires them: include/crucible/Expr.h, which
# the consumer migration flips to include Hash.h, and
# include/crucible/safety/diag/StableName.h, which goes with the old
# tree.  The scan below exempts both by
# path so that the guard stays green while they coexist.  Each exemption
# goes with its file.
#
# A runtime-only or test-only copy of this body is a DRIFT SURFACE:
# any change to the salt (0x9e3779b97f4a7c15), the bit-mix shape, or
# the fmix64 finalizer in Hash.h would leave a parallel body
# stale, and silently break the wire-format witness without tripping
# the ceremony anchor static_assert.
#
# The function was once `consteval`, and test/test_row_
# hash_distinctness.cpp shipped a verbatim runtime copy named
# `combine_ids_runtime`.  The qualifier is `constexpr` instead, so
# the SAME body discharges both compile-time and
# runtime folds.  This guard ensures the parallel-runtime-copy
# pattern never reappears.
#
# The guard flags ANY identifier that contains "combine_ids" as a
# substring (other than the canonical name itself), in any production
# / test / bench / tools source file.  This catches:
#   - `combine_ids_runtime`
#   - `combine_ids_at_runtime`
#   - `runtime_combine_ids`
#   - `combine_ids_v2`, `combine_ids_test`, etc.
#
# It does NOT flag:
#   - `reflect::combine_ids` references (the canonical call form)
#   - `combine_ids` in comments or doc-strings
#
# Exit status:
#   0 — clean
#   1 — a duplicate-shaped identifier was found
#   2 — bad invocation / self-test failure

set -euo pipefail

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-no-combine-ids-duplicate.sh — single combine_ids source of truth.

Usage:
  check-no-combine-ids-duplicate.sh              # scan; exit 1 on violation
  check-no-combine-ids-duplicate.sh --self-test  # plant a violation, verify catch
  check-no-combine-ids-duplicate.sh -h | --help  # usage

Exemptions:
  include/foundation/reflect/Hash.h           the canonical definition
  include/crucible/Expr.h                     frozen chain duplicate, gone with the migration
  include/crucible/safety/diag/_StableName.h   frozen chain duplicate, gone with the old tree
  test/safety_neg/**                          negative-compile fixtures
  a match inside a `//` or `*` comment        prose, not a definition

row_hash folds through exactly one combine_ids body.
USAGE
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Plant one duplicate-shaped identifier plus one instance of
        # each exemption axis, then re-run the scanner scoped to the
        # temp tree.  A guard that never plants a violation has never
        # demonstrated it fires — and this one was registered nowhere
        # for months, so it had never even run.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/src/planted" \
                 "$tmp_root/include/foundation/reflect" \
                 "$tmp_root/include/crucible/safety/diag" \
                 "$tmp_root/test/safety_neg"
        cat >"$tmp_root/src/planted/planted_combine.cpp" <<'PLANTED'
// Synthetic combine_ids fixture for --self-test.
#include <cstdint>
namespace crucible::planted {
// FLAGGED — suffix form, a parallel runtime body.
constexpr std::uint64_t combine_ids_runtime(std::uint64_t a, std::uint64_t b) noexcept {
    return a ^ b;
}
// FLAGGED — prefix form.
constexpr std::uint64_t runtime_combine_ids(std::uint64_t a, std::uint64_t b) noexcept {
    return a ^ b;
}
// CLEAN — the canonical call form carries no prefix or suffix.
constexpr std::uint64_t planted_caller(std::uint64_t a, std::uint64_t b) noexcept {
    return ::foundation::reflect::combine_ids(a, b);
}
// CLEAN — combine_ids_in_comment named only in prose, not an identifier.
}  // namespace crucible::planted
PLANTED
        # Exemption axis: the canonical definition site.
        cat >"$tmp_root/include/foundation/reflect/Hash.h" <<'CANON'
#pragma once
// Canonical site — exempt even when it names combine_ids_runtime in prose.
constexpr unsigned long long combine_ids_exempt_here(unsigned long long a) { return a; }
CANON
        # Exemption axis: the two frozen chain duplicates of the body.
        cat >"$tmp_root/include/crucible/Expr.h" <<'FROZEN_C'
#pragma once
// Frozen duplicate until the consumer migration — exempt by path.
constexpr unsigned long long combine_ids_exempt_here(unsigned long long a) { return a; }
FROZEN_C
        cat >"$tmp_root/include/crucible/safety/diag/_StableName.h" <<'FROZEN_D'
#pragma once
// Frozen duplicate until the old tree is deleted — exempt by path.
constexpr unsigned long long combine_ids_exempt_here(unsigned long long a) { return a; }
FROZEN_D
        # Exemption axis: negative-compile fixtures may name the ban.
        cat >"$tmp_root/test/safety_neg/planted_neg.cpp" <<'NEG'
// Exempt — safety_neg fixtures document what they reject.
constexpr unsigned long long combine_ids_forbidden(unsigned long long a) { return a; }
NEG
        result_file="$(mktemp)"
        if CRUCIBLE_COMBINE_IDS_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$result_file"; then
            printf 'check-no-combine-ids-duplicate: SELF-TEST FAILED — planted duplicate not caught.\n' >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        fi
        ci_fail() {
            printf 'check-no-combine-ids-duplicate: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$result_file")" >&2
            rm -f "$result_file"
            exit 2
        }
        # Both duplicate shapes must be caught.
        grep -qF 'planted_combine.cpp' "$result_file" || \
            ci_fail "planted duplicate file missing from the diagnostic."
        if ! grep -qE 'planted_combine\.cpp:[0-9]+' "$result_file"; then
            ci_fail "diagnostic carries no line reference for the planted file."
        fi
        # Exactly two sites in the planted file — the prefix and suffix
        # forms.  A third would mean the canonical call form leaked.
        planted_hits="$(grep -cE 'planted_combine\.cpp:[0-9]+' "$result_file" || true)"
        [[ "$planted_hits" -eq 2 ]] || \
            ci_fail "expected 2 planted hits, saw ${planted_hits} — the canonical call form or a comment leaked through."
        # No exemption axis may leak.  Match the `path:line`
        # diagnostic form, NOT the bare filename: the guard's own hint
        # text names Hash.h, so a substring grep would report a
        # leak that never happened.
        if grep -qE 'Hash\.h:[0-9]+' "$result_file"; then
            ci_fail "canonical-definition exemption leaked."
        fi
        if grep -qE 'Expr\.h:[0-9]+' "$result_file"; then
            ci_fail "frozen-duplicate exemption for Expr.h leaked."
        fi
        if grep -qE 'StableName\.h:[0-9]+' "$result_file"; then
            ci_fail "frozen-duplicate exemption for StableName.h leaked."
        fi
        if grep -qE 'planted_neg\.cpp:[0-9]+' "$result_file"; then
            ci_fail "test/safety_neg exemption leaked."
        fi
        rm -f "$result_file"
        printf 'check-no-combine-ids-duplicate: self-test passed — prefix + suffix duplicates caught; canonical call form, Hash.h, the two frozen duplicates and test/safety_neg all exempt.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-no-combine-ids-duplicate: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# Scan-root override for --self-test recursion.
scan_root="${CRUCIBLE_COMBINE_IDS_TEST_ROOT:-$project_root}"

# Pattern: `combine_ids` immediately followed or preceded by a
# non-`::` non-word character, AND the immediate context is NOT
# `reflect::combine_ids` or `::foundation::reflect::combine_ids` (the
# canonical call form).
#
# We anchor on identifier shapes: `\w*combine_ids\w*\b` where the
# leading or trailing `\w*` is non-empty.  This captures any
# extension/variation of the name.

candidate_pattern='\b(combine_ids_[a-zA-Z0-9_]+|[a-zA-Z0-9_]+_combine_ids)\b'

violation_count=0
violations=""

while IFS= read -r match; do
    [[ -z "$match" ]] && continue
    file="${match%%:*}"
    rest="${match#*:}"
    line="${rest%%:*}"
    body="${rest#*:}"

    # Skip the canonical site (belt-and-braces — its identifier is
    # exactly `combine_ids` with no prefix/suffix so the pattern
    # can't match it anyway).
    if [[ "$file" == "include/foundation/reflect/Hash.h" ]]; then
        continue
    fi

    # Skip the two frozen chain duplicates of the canonical body.  The
    # identifier in each is exactly `combine_ids`, as at the canonical
    # site, so the exemption is by path and not by a renamed identifier.
    # The consumer migration flips Expr.h to include Hash.h, and
    # StableName.h goes with the old tree.  Each exemption goes with
    # its file.
    if [[ "$file" == "include/crucible/Expr.h" || \
          "$file" == "include/crucible/safety/diag/_StableName.h" ]]; then
        continue
    fi

    # Test fixtures in test/safety_neg/ may mention forbidden names
    # in static_assert messages documenting what they reject.
    if [[ "$file" == test/safety_neg/* ]]; then
        continue
    fi

    # Strip leading whitespace from the line body.  If what remains
    # starts with `//` or `*` (block-comment continuation), the
    # match lives in a comment — not an identifier — so it does
    # not constitute a parallel definition.
    trimmed="${body#"${body%%[![:space:]]*}"}"
    if [[ "$trimmed" == //* || "$trimmed" == \** ]]; then
        continue
    fi

    violations+="${file}:${line}"$'\n'
    violation_count=$((violation_count + 1))
done < <(
    cd "$scan_root"
    # Only pass directories that exist — a --self-test temp root carries
    # a subset, and rg treats a missing target as a hard error.
    scan_dirs=()
    for d in include src test bench tools vessel; do
        [[ -d "$d" ]] && scan_dirs+=("$d")
    done
    [[ ${#scan_dirs[@]} -eq 0 ]] && exit 0
    rg -nP \
       --no-heading \
       --type=cpp \
       --glob '!build*/**' \
       --glob '!cmake-build-*/**' \
       --glob '!third_party/**' \
       --glob '!external/**' \
       --glob '!vendor/**' \
       "$candidate_pattern" \
       "${scan_dirs[@]}" 2>/dev/null || true
)

if [[ "$violation_count" -ne 0 ]]; then
    cat >&2 <<HINT
check-no-combine-ids-duplicate: ${violation_count} parallel-name
site(s) detected:

${violations}
The tree keeps a single source of truth for combine_ids.
The canonical definition is constexpr and callable at BOTH compile
time AND runtime — do not re-introduce a parallel body under any
alternative name.  Route the call through:

  ::foundation::reflect::combine_ids(a, b)

(see combine_ids in include/foundation/reflect/Hash.h).
HINT
    exit 1
fi

printf 'check-no-combine-ids-duplicate: clean — single combine_ids source of truth preserved.\n' >&2
exit 0
