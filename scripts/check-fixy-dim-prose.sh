#!/usr/bin/env bash
# check-fixy-dim-prose.sh — FIXY-V-006 axis-numbering CI grep guard.
#
# Doc-blocks in include/crucible/fixy/ used to spell axes by their
# historical FX numbering ("Dim 1 Type", "(dim 22)", etc.).  FX dim 12
# (Clock Domain) and FX dim 17 (FP Order) were dropped per fixy.md
# §24.1, so the substrate DimensionAxis enum compacts 0..23 without
# gaps and the FX numbers diverge from the substrate ordinals (FX dim
# 22 Staleness → substrate 19, FX dim 23 Synchronization → substrate
# 20, etc.).  V-006 migrated every doc-block to the substrate form
# "DimensionAxis::<Name> = <ordinal>"; this guard rejects regression
# back to the FX-only spelling.
#
# Banned patterns inside include/crucible/fixy/:
#   - "Dim <N>" where <N> is a 1-2 digit decimal (matches "Dim 1 Type",
#     "Dim 23 Synchronization", etc.; case-sensitive)
#   - "(dim <N>)" or "(dim <N>," — the parenthetical FX-ordinal form
#     that V-006 found in Fp.h and syscall/Family.h
#
# The substrate form "DimensionAxis::<Name> = <ordinal>" is NOT matched
# because the "Dim" token is preceded by "Dimension", not a word
# boundary; the grep uses `\bDim ` to anchor.
#
# Exit status:
#   0 — clean (no FX-ordinal prose under fixy/)
#   1 — at least one FX-ordinal regression found
#   2 — bad invocation

set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
fixy_dir="${root}/include/crucible/fixy"

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
