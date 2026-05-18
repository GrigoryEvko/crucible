#!/usr/bin/env bash
#
# fixy-A1-018 — splits_into / splits_into_pack orphan-specialization guard.
#
# `splits_into<Parent, L, R>` and `splits_into_pack<Parent, Children...>`
# are the declarative manifests that gate `mint_permission_split` /
# `mint_permission_combine` / `mint_permission_fork`.  The CSL frame-
# rule discipline (CLAUDE.md §IX, §XVI) requires that a specialization
# of either trait lives in the SAME translation unit as the parent
# tag's declaration — otherwise any TU can declare arbitrary splits
# for foreign tags and forge cross-region authority.
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

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
pattern='(struct|class)\s+splits_into(_pack)?\s*<'
status=0

while IFS=: read -r file line text; do
    rel="${file#"$root"/}"

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
        include/crucible/concurrent/*.h | \
        include/crucible/safety/PermissionTreeGenerator.h | \
        include/crucible/safety/PermissionGridGenerator.h | \
        test/*)
            continue
            ;;
    esac

    printf 'splits_into_orphan: forbidden specialization at %s:%s\n' "$rel" "$line" >&2
    printf 'splits_into_orphan: %s\n' "$text" >&2
    status=1
done < <(
    rg -n --no-heading --pcre2 \
        --glob '!build/**' \
        --glob '!cmake-build-*/**' \
        --glob '!third_party/**' \
        --glob '!external/**' \
        --glob '!vendor/**' \
        --glob '!misc/**' \
        --glob '!**/*.md' \
        --glob '!scripts/check-splits-into-orphan.sh' \
        "$pattern" "$root" || true
)

if [[ "$status" -ne 0 ]]; then
    printf 'splits_into_orphan: specializations belong only in include/crucible/{permissions,concurrent}/ or include/crucible/safety/Permission{Tree,Grid}Generator.h or test/**.\n' >&2
    printf 'splits_into_orphan: per CLAUDE.md §IX, the manifest must live in the same TU as the parent tag declaration; otherwise any foreign TU can forge cross-region authority.\n' >&2
fi

exit "$status"
