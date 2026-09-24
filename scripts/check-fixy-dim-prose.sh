#!/usr/bin/env bash
# check-fixy-dim-prose.sh — axis-numbering CI grep guard.
#
# Doc-blocks in include/crucible/fixy/ used to spell axes by their
# historical FX numbering ("Dim 1 Type", "(dim 22)", etc.).  FX dim 12
# (Clock Domain) and FX dim 17 (FP Order) were dropped per fixy.md
# §24.1, so the substrate DimensionAxis enum compacts 0..23 without
# gaps and the FX numbers diverge from the substrate ordinals (FX dim
# 22 Staleness → substrate 19, FX dim 23 Synchronization → substrate
# 20, etc.).  Every doc-block moved to the substrate form
# "DimensionAxis::<Name> = <ordinal>"; this guard rejects regression
# back to the FX-only spelling.
#
# Banned patterns inside include/crucible/fixy/:
#   - "Dim <N>" where <N> is a 1-2 digit decimal (matches "Dim 1 Type",
#     "Dim 23 Synchronization", etc.; case-sensitive)
#   - "(dim <N>)" or "(dim <N>," — the parenthetical FX-ordinal form
#     that Fp.h and syscall/Family.h once carried
#
# The substrate form "DimensionAxis::<Name> = <ordinal>" is NOT matched
# because the "Dim" token is preceded by "Dimension", not a word
# boundary; the grep uses `\bDim ` to anchor.  --self-test plants both
# spellings and proves that claim rather than asserting it.
#
# Exit status:
#   0 — clean (no FX-ordinal prose under fixy/)
#   1 — at least one FX-ordinal regression found
#   2 — bad invocation / self-test failure

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
    cat >&2 <<'USAGE'
check-fixy-dim-prose.sh — axis-numbering guard.

Usage:
  check-fixy-dim-prose.sh              # scan; exit 1 on violation
  check-fixy-dim-prose.sh --self-test  # plant FX prose, verify catch
  check-fixy-dim-prose.sh -h | --help  # usage

Scan scope:
  include/crucible/fixy/**/*.h

Re-spell any FX ordinal as 'DimensionAxis::<Name> = <substrate-ordinal>'.
See include/crucible/safety/DimensionTraits.h for the canonical enum.
USAGE
}

case "${1:-}" in
    -h|--help) usage; exit 0 ;;
    --self-test)
        # Two synthetic roots.  The dirty root carries both FX-ordinal
        # spellings plus the compliant substrate spelling; the clean
        # root carries the compliant spelling alone.  The pair proves
        # the guard fires on regression AND stays quiet on the migrated
        # form — the claim the doc-block above makes.
        tmp_root="$(mktemp -d)"
        trap 'rm -rf "$tmp_root"' EXIT
        mkdir -p "$tmp_root/dirty/include/crucible/fixy" \
                 "$tmp_root/clean/include/crucible/fixy"

        cat >"$tmp_root/dirty/include/crucible/fixy/planted.h" <<'PLANTED'
// Synthetic FX-ordinal prose fixture for --self-test.
// Dim 22 Staleness — FX-only header spelling; must be flagged.
// Staleness (dim 22) — FX-only parenthetical spelling; must be flagged.
// DimensionAxis::Staleness = 19 — substrate spelling; must NOT be flagged.
#pragma once
PLANTED

        cat >"$tmp_root/clean/include/crucible/fixy/planted.h" <<'COMPLIANT'
// Synthetic substrate-spelling fixture for --self-test.
// DimensionAxis::Staleness = 19 — substrate spelling; must NOT be flagged.
#pragma once
COMPLIANT

        scanner_stderr="$(mktemp)"

        self_test_fail() {
            printf 'check-fixy-dim-prose: SELF-TEST FAILED — %s\n' "$1" >&2
            printf '── scanner stderr ───\n%s\n────────────────────\n' \
                "$(cat "$scanner_stderr")" >&2
            rm -f "$scanner_stderr"
            exit 2
        }

        # ── Phase 1 — the dirty root must fail ───────────────────────
        if CRUCIBLE_FIXY_DIM_PROSE_TEST_ROOT="$tmp_root/dirty" \
           bash "${BASH_SOURCE[0]}" 2>"$scanner_stderr"; then
            self_test_fail 'planted FX-ordinal prose not caught.'
        fi
        if ! grep -qF 'planted.h:2' "$scanner_stderr"; then
            self_test_fail 'expected diagnostic for the "Dim <N>" form missing.'
        fi
        if ! grep -qF 'planted.h:3' "$scanner_stderr"; then
            self_test_fail 'expected diagnostic for the "(dim <N>)" form missing.'
        fi
        if grep -qF 'planted.h:4' "$scanner_stderr"; then
            self_test_fail 'substrate spelling "DimensionAxis::<Name> = <N>" was flagged.'
        fi

        # ── Phase 2 — the clean root must pass ───────────────────────
        if ! CRUCIBLE_FIXY_DIM_PROSE_TEST_ROOT="$tmp_root/clean" \
             bash "${BASH_SOURCE[0]}" 2>"$scanner_stderr"; then
            self_test_fail 'compliant-only root rejected.'
        fi

        rm -f "$scanner_stderr"
        printf 'check-fixy-dim-prose: self-test passed — both FX spellings caught, substrate spelling honoured.\n' >&2
        exit 0
        ;;
    "") ;;
    *) printf 'check-fixy-dim-prose: unknown argument: %s\n' "$1" >&2
       usage; exit 2 ;;
esac

# ── Scan-root override for --self-test recursion ─────────────────────
scan_root="${CRUCIBLE_FIXY_DIM_PROSE_TEST_ROOT:-$root}"
fixy_dir="${scan_root}/include/crucible/fixy"

if [[ ! -d "${fixy_dir}" ]]; then
    echo "check-fixy-dim-prose.sh: fixy directory not found: ${fixy_dir}" >&2
    exit 2
fi

# Two patterns combined with grep -E alternation.
#   \bDim [0-9]{1,2}\b   — header-style "Dim 1 Type ..."
#   \(dim [0-9]{1,2}[,)] — parenthetical "(dim 22," or "(dim 22)"
pattern='(\bDim [0-9]{1,2}\b|\(dim [0-9]{1,2}[,)])'

# `grep -r -n -E -H` walks include/crucible/fixy recursively, prints
# file:line:match.  --include='*.h' keeps the scan to headers (the
# only place we ship doc-blocks).  We INVERT the exit semantics:
# matches mean regression → exit 1; no matches → exit 0.
if matches=$(grep -r -n -E -H --include='*.h' "${pattern}" "${fixy_dir}" 2>/dev/null); then
    echo "check-fixy-dim-prose.sh: FX-ordinal prose found under include/crucible/fixy/" >&2
    echo "" >&2
    echo "${matches}" >&2
    echo "" >&2
    echo "Re-spell as 'DimensionAxis::<Name> = <substrate-ordinal>'." >&2
    echo "See include/crucible/safety/DimensionTraits.h for the canonical" >&2
    echo "DimensionAxis enum + per-enumerator substrate ordinals." >&2
    exit 1
fi

exit 0
