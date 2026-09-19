#!/usr/bin/env bash
#
# fixy-A1-018 + fixy-M-29 — splits_into / splits_into_pack +
# splits_into_authoring_witness / splits_into_pack_authoring_witness
# orphan-specialization guard.
#
# `splits_into<Parent, L, R>` and `splits_into_pack<Parent, Children...>`
# are the declarative manifests that gate `mint_permission_split` /
# `mint_permission_combine` / `mint_permission_fork`.  fixy-M-29 adds
# the companion `splits_into_authoring_witness` /
# `splits_into_pack_authoring_witness` traits — `mint_permission_*`
# requires BOTH the splits trait AND the witness trait.  The witness
# is itself orphan-rejected here to close the bypass where a foreign
# TU specializes BOTH the splits trait AND the witness; under this
# guard the witness specialization fails the same authoring-location
# check the splits specialization does, and the type system at
# mint-time enforces both must be present.
#
# The CSL frame-rule discipline (CLAUDE.md §IX, §XVI) requires that a
# specialization of any of the four traits lives in the SAME
# translation unit as the parent tag's declaration — otherwise any TU
# can declare arbitrary splits for foreign tags and forge cross-
# region authority.
#
# C++ has no native orphan-rule.  This script enforces the discipline
# at build time: only blessed authoring locations may declare
# specializations.  Any specialization outside the whitelist fails
# the build with a diagnostic naming the offending location.
#
# Approved authoring locations:
#
#   * include/crucible/permissions/* — Permission.h primary template
#                                      + FederationPermission.h
#                                      + PermissionInherit.h defensive
#                                      partials co-located with the
#                                      tag trees they manifest splits
#                                      for.
#   * include/crucible/concurrent/*  — per-channel substrate
#                                      (Permissioned{Spsc,Mpmc,Mpsc,
#                                      Snapshot,MetaLog,ChainEdge,
#                                      ChaseLevDeque}Channel.h +
#                                      Queue.h facade); each declares
#                                      its own tag tree (spsc_tag::,
#                                      mpmc_tag::, …) and ships the
#                                      manifest in the same header.
#   * include/crucible/safety/PermissionTreeGenerator.h
#   * include/crucible/safety/PermissionGridGenerator.h
#                                    — auto-generators that emit
#                                      splits_into_pack specializations
#                                      keyed on caller-supplied Parent
#                                      tags.  The generator template is
#                                      itself in safety/; users
#                                      instantiate it inside their own
#                                      blessed authoring locations.
#   * test/**                         — test code legitimately declares
#                                      test-local tag trees AND their
#                                      splits.  Includes:
#                                        - positive sentinel TUs
#                                          (test/test_*.cpp)
#                                        - negative-compile fixtures
#                                          (test/{safety,fixy}_neg/)
#                                        - attack regressions
#                                          (test/safety_attack/) — the
#                                          CR-05 fixture exercises the
#                                          residual federation-specific
#                                          orphan gap and is allowed.
#
# All other locations (vessel/, vis/, src/cipher/, src/forge/, ...) are
# review-rejected and CI-rejected.  A future PR that lands a new
# Permissioned* primitive in production code should add it to the
# concurrent/ tree (whitelisted) rather than splitting authoring
# across multiple subsystems.
#
# This script excludes itself from the scan: its --self-test fixture
# plants a forbidden specialization as literal heredoc text.
#
# Exit status:
#   0 — clean (no specialization outside the authoring set)
#   1 — at least one orphan specialization
#   2 — bad invocation / self-test failure

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-splits-into-orphan.sh — splits_into orphan-specialization guard.

Usage:
  check-splits-into-orphan.sh              # scan; exit 1 on violation
  check-splits-into-orphan.sh --self-test  # plant a violation, verify catch
  check-splits-into-orphan.sh -h | --help  # usage

Exemptions:
  include/crucible/permissions/*.h                  — authoring set
  include/foundation/permissions/*.h                — authoring set (ported)
  include/crucible/concurrent/*.h                   — per-channel substrate
  include/crucible/safety/Permission{Tree,Grid}Generator.h
  include/fixy/OwnedRegion.h                        — the ported Slice generator
  test/*                                            — test-local tag trees
  a pure-comment line (leading // or *)             — doc-block citation

CLAUDE.md §IX — the manifest must live in the same TU as the parent tag
declaration; otherwise any foreign TU can forge cross-region authority.
USAGE
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Plant the SAME specialization three ways: under a non-exempt
        # path (must be flagged), under the blessed concurrent/ path
        # (path exemption), and as a doc-comment line (comment
        # exemption).  Both exemption axes get a witness, so a future
        # refactor cannot silently widen or narrow either one.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/src/planted" "$tmp_root/include/crucible/concurrent"

        planted_rel='src/planted/planted_splits.cpp'
        exempt_rel='include/crucible/concurrent/PlantedChannel.h'

        cat >"$tmp_root/$planted_rel" <<'PLANTED'
// Synthetic orphan-specialization fixture for --self-test.
namespace crucible::planted {
struct Parent {};
struct Left {};
struct Right {};
}  // namespace crucible::planted
namespace crucible {
template <>
struct splits_into<planted::Parent, planted::Left, planted::Right> {
    static constexpr bool value = true;
};
// struct splits_into_pack<planted::Parent, planted::Left> — doc-comment only.
}  // namespace crucible
PLANTED

        cat >"$tmp_root/$exempt_rel" <<'EXEMPT'
// Synthetic blessed-authoring fixture for --self-test.
namespace crucible::planted {
struct ChanParent {};
struct ChanLeft {};
struct ChanRight {};
}  // namespace crucible::planted
namespace crucible {
template <>
struct splits_into<planted::ChanParent, planted::ChanLeft, planted::ChanRight> {
    static constexpr bool value = true;
};
}  // namespace crucible
EXEMPT

        scanner_stderr="$(mktemp)"

        self_test_fail() {
            printf 'check-splits-into-orphan: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$scanner_stderr")" >&2
            rm -f "$scanner_stderr"
            exit 2
        }

        if CRUCIBLE_SPLITS_INTO_ORPHAN_TEST_ROOT="$tmp_root" \
           bash "${BASH_SOURCE[0]}" 2>"$scanner_stderr"; then
            self_test_fail 'planted orphan specialization not caught.'
        fi
        # The planted violation sits on line 9 of the fixture.
        if ! grep -qF "$planted_rel:9" "$scanner_stderr"; then
            self_test_fail "expected diagnostic for $planted_rel:9 missing."
        fi
        # The doc-comment citation on line 12 must be filtered out.
        if grep -qF "$planted_rel:12" "$scanner_stderr"; then
            self_test_fail 'pure-comment line leaked through the filter.'
        fi
        # The concurrent/ copy must be exempt by authoring location.
        if grep -qF "$exempt_rel" "$scanner_stderr"; then
            self_test_fail 'authoring-location exemption leaked.'
        fi

        rm -f "$scanner_stderr"
        printf 'check-splits-into-orphan: self-test passed — orphan caught, authoring-location + comment exemptions honoured.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-splits-into-orphan: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Scan-root override for --self-test recursion ─────────────────────
scan_root="${CRUCIBLE_SPLITS_INTO_ORPHAN_TEST_ROOT:-$root}"
# Matches the four orphan-rejected traits:
#   splits_into< ...                        — binary splits manifest
#   splits_into_pack< ...                   — N-ary splits manifest
#   splits_into_authoring_witness< ...      — fixy-M-29 binary witness
#   splits_into_pack_authoring_witness< ... — fixy-M-29 N-ary witness
pattern='(struct|class)\s+splits_into(_pack)?(_authoring_witness)?\s*<'
status=0

while IFS=: read -r file line text; do
    rel="${file#"$scan_root"/}"

    # Skip pure-comment lines.  `rg` is line-based and would otherwise
    # flag doc-comment occurrences of the trait names (e.g. headers that
    # cite the specialization pattern in their docblock).
    stripped="${text#"${text%%[![:space:]]*}"}"
    case "$stripped" in
        '//'*|'*'*)
            continue
            ;;
    esac

    case "$rel" in
        include/crucible/permissions/*.h | \
        include/foundation/permissions/*.h | \
        include/crucible/concurrent/*.h | \
        include/crucible/safety/PermissionTreeGenerator.h | \
        include/crucible/safety/PermissionGridGenerator.h | \
        include/fixy/OwnedRegion.h | \
        test/*)
            continue
            ;;
    esac

    printf 'splits_into_orphan: forbidden specialization at %s:%s\n' "$rel" "$line" >&2
    printf 'splits_into_orphan: %s\n' "$text" >&2
    status=1
done < <(
    rg -n --no-heading --pcre2 \
        --glob '!build*/**' \
        --glob '!cmake-build-*/**' \
        --glob '!third_party/**' \
        --glob '!external/**' \
        --glob '!vendor/**' \
        --glob '!misc/**' \
        --glob '!**/*.md' \
        --glob '!**/check-splits-into-orphan.sh' \
        "$pattern" "$scan_root" || true
)

if [[ "$status" -ne 0 ]]; then
    printf 'splits_into_orphan: specializations belong only in include/crucible/{permissions,concurrent}/, include/foundation/permissions/, include/crucible/safety/Permission{Tree,Grid}Generator.h or test/**.\n' >&2
    printf 'splits_into_orphan: per CLAUDE.md §IX, the manifest must live in the same TU as the parent tag declaration; otherwise any foreign TU can forge cross-region authority.\n' >&2
fi

exit "$status"
