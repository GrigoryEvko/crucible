#!/usr/bin/env bash
# check-no-combine-ids-duplicate.sh — FIXY-FOUND-050 drift-defense guard.
#
# Crucible's row_hash machinery (RowHashFold.h + every wrapper-specific
# row_hash_contribution<W> specialization) folds via a SINGLE function:
#
#   include/crucible/safety/diag/StableName.h:166
#     [[nodiscard]] constexpr std::uint64_t combine_ids(
#         std::uint64_t a, std::uint64_t b) noexcept
#
# A runtime-only or test-only copy of this body is a DRIFT SURFACE:
# any change to the salt (0x9e3779b97f4a7c15), the bit-mix shape, or
# the fmix64 finalizer in StableName.h would leave a parallel body
# stale, and silently break the wire-format witness without tripping
# the ceremony anchor static_assert.
#
# Pre-FIXY-FOUND-050 the function was `consteval` and test/test_row_
# hash_distinctness.cpp shipped a verbatim runtime copy named
# `combine_ids_runtime`.  FOUND-050 weakened the qualifier to
# `constexpr` so the SAME body discharges both compile-time and
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
#   - `detail::combine_ids` references (the canonical call form)
#   - `combine_ids` in comments or doc-strings
#
# Exit status:
#   0 — clean
#   1 — a duplicate-shaped identifier was found

set -euo pipefail

script_dir="$(cd "$(dirname "$0")" && pwd)"
project_root="$(cd "$script_dir/.." && pwd)"

# Pattern: `combine_ids` immediately followed or preceded by a
# non-`::` non-word character, AND the immediate context is NOT
# `detail::combine_ids` or `safety::diag::detail::combine_ids` (the
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
    if [[ "$file" == "include/crucible/safety/diag/StableName.h" ]]; then
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
    cd "$project_root"
    rg -nP \
       --no-heading \
       --type=cpp \
       --glob '!build/**' \
       --glob '!cmake-build-*/**' \
       --glob '!third_party/**' \
       --glob '!external/**' \
       --glob '!vendor/**' \
       "$candidate_pattern" \
       include/ src/ test/ bench/ tools/ vessel/ 2>/dev/null || true
)

if [[ "$violation_count" -ne 0 ]]; then
    cat >&2 <<HINT
check-no-combine-ids-duplicate: ${violation_count} parallel-name
site(s) detected:

${violations}
FIXY-FOUND-050 mandates a single source of truth for combine_ids.
The canonical definition is constexpr and callable at BOTH compile
time AND runtime — do not re-introduce a parallel body under any
alternative name.  Route the call through:

  ::crucible::safety::diag::detail::combine_ids(a, b)

(see include/crucible/safety/diag/StableName.h:166).
HINT
    exit 1
fi

printf 'check-no-combine-ids-duplicate: clean — single combine_ids source of truth preserved.\n' >&2
exit 0
