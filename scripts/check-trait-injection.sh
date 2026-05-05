#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pattern='(struct|class)\s+(is_graded_specialization|value_type_decoupled|graded_modality|is_numerical_tier_impl)\s*<'
status=0

while IFS=: read -r file line text; do
    rel="${file#"$root"/}"

    case "$rel" in
        include/crucible/algebra/*|\
        include/crucible/safety/*|\
        include/crucible/permissions/*|\
        include/crucible/handles/*|\
        test/test_concept_cheat_probe.cpp)
            continue
            ;;
    esac

    printf 'trait_guard: forbidden trait specialization at %s:%s\n' "$rel" "$line" >&2
    printf 'trait_guard: %s\n' "$text" >&2
    status=1
done < <(
    rg -n --no-heading --multiline --pcre2 \
        --glob '!build/**' \
        --glob '!cmake-build-*/**' \
        --glob '!third_party/**' \
        --glob '!external/**' \
        --glob '!vendor/**' \
        "$pattern" "$root" || true
)

if [[ "$status" -ne 0 ]]; then
    printf 'trait_guard: specialize these substrate traits only in include/crucible/{algebra,safety,permissions,handles}/ or the intentional cheat probe fixture.\n' >&2
fi

exit "$status"
