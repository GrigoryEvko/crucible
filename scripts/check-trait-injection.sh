#!/usr/bin/env bash
# check-trait-injection.sh — substrate trait injection guard.
#
# `is_graded_specialization`, `value_type_decoupled`, `graded_modality`
# and `is_numerical_tier_impl` are substrate traits.  A specialization
# from a foreign TU forges a Graded property that the wrapper never
# proved.  C++ has no orphan rule, so this guard enforces the authoring
# location at build time.
#
# Approved authoring locations:
#   include/crucible/algebra/     include/crucible/permissions/
#   include/crucible/safety/      include/crucible/handles/
#   test/test_concept_cheat_probe.cpp — the intentional cheat probe
#
# This script excludes itself from the scan: its --self-test fixture
# plants the forbidden specialization as literal heredoc text.
#
# Exit status:
#   0 — clean (no forbidden specialization outside the authoring set)
#   1 — at least one violation
#   2 — bad invocation / self-test failure

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-trait-injection.sh — substrate trait injection guard.

Usage:
  check-trait-injection.sh              # scan; exit 1 on violation
  check-trait-injection.sh --self-test  # plant a violation, verify catch
  check-trait-injection.sh -h | --help  # usage

Exemption:
  include/crucible/{algebra,safety,permissions,handles}/  — authoring set
  test/test_concept_cheat_probe.cpp                       — cheat probe

CLAUDE.md §XVI — a substrate trait is specialized only beside the
wrapper whose algebraic property it asserts.
USAGE
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Plant the SAME forbidden specialization twice: once under a
        # non-exempt path (must be flagged) and once under the blessed
        # algebra/ authoring path (must NOT be flagged).  A guard that
        # only ever sees a clean tree has never demonstrated it fires.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/src/planted" "$tmp_root/include/crucible/algebra"

        planted_rel='src/planted/planted_trait.cpp'
        exempt_rel='include/crucible/algebra/planted.h'

        cat >"$tmp_root/$planted_rel" <<'PLANTED'
// Synthetic trait-injection fixture for --self-test.
namespace crucible::planted {
template <typename T>
struct PlantedProbe {};
}  // namespace crucible::planted
namespace crucible {
template <>
struct is_graded_specialization<planted::PlantedProbe<int>> {
    static constexpr bool is_specialized = true;
};
}  // namespace crucible
PLANTED

        cat >"$tmp_root/$exempt_rel" <<'EXEMPT'
// Synthetic authoring-location fixture for --self-test.
namespace crucible::planted {
template <typename T>
struct ExemptProbe {};
}  // namespace crucible::planted
namespace crucible {
template <>
struct is_graded_specialization<planted::ExemptProbe<int>> {
    static constexpr bool is_specialized = true;
};
}  // namespace crucible
EXEMPT

        scanner_stderr="$(mktemp)"

        self_test_fail() {
            printf 'check-trait-injection: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$scanner_stderr")" >&2
            rm -f "$scanner_stderr"
            exit 2
        }

        if CRUCIBLE_TRAIT_INJECTION_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$scanner_stderr"; then
            self_test_fail 'planted specialization not caught.'
        fi
        # The planted violation sits on line 8 of the fixture.
        if ! grep -qF "$planted_rel:8" "$scanner_stderr"; then
            self_test_fail "expected diagnostic for $planted_rel:8 missing."
        fi
        # The algebra/ copy must be exempt by authoring location.
        if grep -qF "$exempt_rel" "$scanner_stderr"; then
            self_test_fail 'authoring-location exemption leaked.'
        fi

        rm -f "$scanner_stderr"
        printf 'check-trait-injection: self-test passed — injection caught, authoring-location exemption honoured.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-trait-injection: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Scan-root override for --self-test recursion ─────────────────────
scan_root="${CRUCIBLE_TRAIT_INJECTION_TEST_ROOT:-$root}"

pattern='(struct|class)\s+(is_graded_specialization|value_type_decoupled|graded_modality|is_numerical_tier_impl)\s*<'
status=0

while IFS=: read -r file line text; do
    rel="${file#"$scan_root"/}"

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
        --glob '!**/check-trait-injection.sh' \
        "$pattern" "$scan_root" || true
)

if [[ "$status" -ne 0 ]]; then
    printf 'trait_guard: specialize these substrate traits only in include/crucible/{algebra,safety,permissions,handles}/ or the intentional cheat probe fixture.\n' >&2
fi

exit "$status"
